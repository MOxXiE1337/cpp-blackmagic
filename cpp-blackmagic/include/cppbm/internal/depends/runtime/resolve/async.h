// File role:
// Asynchronous default-argument resolver used by coroutine @inject pipeline.
//
// This layer:
// - resolves async metadata entries emitted by inject.py
// - caches resolved values/slots with same semantics as sync resolver
// - falls back to sync resolver for backward compatibility

#ifndef __CPPBM_DEPENDS_RESOLVE_ASYNC_H__
#define __CPPBM_DEPENDS_RESOLVE_ASYNC_H__

#include "sync.h"
#include "../coroutine/task.h"

namespace cpp::blackmagic::depends
{
    // Async default-arg resolver for coroutine parameter pipeline.
    //
    // Resolution strategy:
    // 1) prefer async metadata entries (Task<...>) emitted by inject.py
    // 2) fallback to existing sync resolver for compatibility with old generated code
    //
    // Behavioral note:
    // this function intentionally mirrors TryResolveDefaultArgForParam semantics,
    // but metadata payloads are awaited before slot update/argument write-back.
    template <typename A>
    Task<bool> TryResolveDefaultArgForParamAsync(
        const void* target,
        std::size_t index,
        A& out,
        const void** out_factory = nullptr)
    {
        using Param = std::remove_cv_t<std::remove_reference_t<A>>;
        auto set_factory = [&](const void* key)
            {
                if (out_factory != nullptr)
                {
                    *out_factory = key;
                }
            };

        // Shared async pointer-metadata resolver for both:
        // - reference params (WriteOut=false): slot preparation only
        // - pointer params   (WriteOut=true): also writes pointer argument
        auto resolve_ptr_meta_async = [&]<typename Raw, bool WriteOut>() -> Task<bool>
            {
                if (auto ptr_meta_task = InjectRegistry::Resolve<Task<DependsPtrValue<Raw>>>(target, index))
                {
                    auto ptr_meta = co_await std::move(*ptr_meta_task);
                    set_factory(ptr_meta.factory);

                    const bool is_plain_depends_placeholder =
                        ptr_meta.factory == nullptr
                        && IsDependsPlaceholder<Raw*>(ptr_meta.ptr);

                    // Highest priority: context override table for exact key.
                    if (TryPopulateRawSlotFromOverride<Raw>(target, ptr_meta.factory))
                    {
                        if constexpr (WriteOut)
                        {
                            if (auto* resolved = TryResolveRawPtr<Raw>(target, ptr_meta.factory, ptr_meta.cached))
                            {
                                out = static_cast<Param>(resolved);
                                co_return true;
                            }
                        }
                        else
                        {
                            co_return true;
                        }
                    }

                    // Depends() placeholder metadata:
                    // resolve from existing/explicit/default slot flow (factory == nullptr).
                    if (is_plain_depends_placeholder)
                    {
                        auto* slot = EnsureRawSlot<Raw>(target, true, nullptr, ptr_meta.cached);
                        if (slot == nullptr || slot->obj == nullptr)
                        {
                            co_return false;
                        }
                        if constexpr (WriteOut)
                        {
                            out = static_cast<Param>(slot->obj);
                        }
                        co_return true;
                    }

                    // Metadata already produced concrete pointer.
                    // Cache it into current context with owned/borrowed policy.
                    if (ptr_meta.ptr != nullptr)
                    {
                        if constexpr (WriteOut)
                        {
                            out = static_cast<Param>(ptr_meta.ptr);
                        }

                        auto* existing = FindSlotInChain(typeid(Raw), ptr_meta.factory);
                        const bool same_cached_ptr =
                            (existing != nullptr) && (existing->obj == ptr_meta.ptr);
                        // Avoid replacing owned slot by borrowed slot for same pointer value.
                        if (same_cached_ptr && !ptr_meta.owned)
                        {
                            co_return true;
                        }

                        if (ptr_meta.owned)
                        {
                            CacheOwnedRaw<Raw>(ptr_meta.ptr, ptr_meta.factory);
                        }
                        else
                        {
                            CacheBorrowedRaw<Raw>(ptr_meta.ptr, ptr_meta.factory);
                        }
                        co_return true;
                    }
                    co_return false;
                }
                co_return false;
            };

        if constexpr (std::is_reference_v<A>)
        {
            using RefRaw = std::remove_cv_t<std::remove_reference_t<A>>;
            if (co_await resolve_ptr_meta_async.template operator()<RefRaw, false>())
            {
                co_return true;
            }
        }
        else if constexpr (std::is_pointer_v<Param>)
        {
            // Pointer parameter path uses the same metadata flow with WriteOut=true.
            using Pointee = std::remove_cv_t<std::remove_pointer_t<Param>>;
            if (co_await resolve_ptr_meta_async.template operator()<Pointee, true>())
            {
                co_return true;
            }
        }

        // Async plain-value metadata path.
        // This is defensive/compatibility behavior and is not the main Depends flow.
        if (auto value_task = InjectRegistry::Resolve<Task<Param>>(target, index))
        {
            auto value = co_await std::move(*value_task);
            set_factory(nullptr);
            if (IsDependsPlaceholder<Param>(value))
            {
                co_return false;
            }

            if constexpr (std::is_reference_v<A>)
            {
                using RefRaw = std::remove_cv_t<std::remove_reference_t<A>>;
                if constexpr (std::is_pointer_v<Param>)
                {
                    if (value != nullptr)
                    {
                        CacheBorrowedRaw<std::remove_pointer_t<Param>>(value, nullptr);
                        co_return true;
                    }
                    co_return false;
                }
                else if constexpr (std::is_constructible_v<RefRaw, Param&&>)
                {
                    CacheOwnedValue<RefRaw>(RefRaw(std::move(value)), nullptr);
                    co_return true;
                }
                co_return false;
            }

            if constexpr (std::is_assignable_v<A&, Param>)
            {
                out = std::move(value);
                if constexpr (std::is_pointer_v<Param>)
                {
                    if (out != nullptr)
                    {
                        CacheBorrowedRaw<std::remove_pointer_t<Param>>(out, nullptr);
                    }
                }
                else if constexpr (std::is_copy_constructible_v<Param>)
                {
                    CacheResolvedValue(out, nullptr);
                }
                co_return true;
            }

            co_return false;
        }

        // Metadata fallback for plain Depends() style placeholders:
        // if generated metadata is unavailable at runtime, keep pointer/reference
        // paths usable by resolving from default-constructible slot.
        if constexpr (std::is_reference_v<A>)
        {
            using RefRaw = std::remove_cv_t<std::remove_reference_t<A>>;
            auto* slot = EnsureRawSlot<RefRaw>(target, true, nullptr, true);
            if (slot != nullptr && slot->obj != nullptr)
            {
                set_factory(nullptr);
                co_return true;
            }
        }
        else if constexpr (std::is_pointer_v<Param>)
        {
            using Pointee = std::remove_cv_t<std::remove_pointer_t<Param>>;
            auto* slot = EnsureRawSlot<Pointee>(target, true, nullptr, true);
            if (slot != nullptr && slot->obj != nullptr)
            {
                out = static_cast<Param>(slot->obj);
                set_factory(nullptr);
                co_return true;
            }
        }

        // Fallback to sync metadata resolver for backward compatibility.
        co_return TryResolveDefaultArgForParam<A>(target, index, out, out_factory);
    }
}

#endif // __CPPBM_DEPENDS_RESOLVE_ASYNC_H__
