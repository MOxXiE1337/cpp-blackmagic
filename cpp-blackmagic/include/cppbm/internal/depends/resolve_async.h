// File role:
// Asynchronous default-argument resolver used by coroutine @inject pipeline.
//
// This layer:
// - resolves async metadata entries emitted by inject.py
// - caches resolved values/slots with same semantics as sync resolver
// - falls back to sync resolver for backward compatibility

#ifndef __CPPBM_DEPENDS_RESOLVE_ASYNC_H__
#define __CPPBM_DEPENDS_RESOLVE_ASYNC_H__

#include "resolve_sync.h"
#include "coroutine.h"

namespace cpp::blackmagic::depends
{
    // Async default-arg resolver for coroutine parameter pipeline.
    //
    // Resolution strategy:
    // 1) prefer async metadata entries (Task<...>) emitted by inject.py
    // 2) fallback to existing sync resolver for compatibility with old generated code
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

        DependsExecutionScope scope{};

        if constexpr (std::is_reference_v<A>)
        {
            using RefRaw = std::remove_cv_t<std::remove_reference_t<A>>;
            if (auto ptr_meta_task = InjectRegistry::Resolve<Task<DependsPtrValue<RefRaw>>>(target, index))
            {
                auto ptr_meta = co_await std::move(*ptr_meta_task);
                set_factory(ptr_meta.factory);

                if (TryPopulateRawSlotFromExplicit<RefRaw>(target, ptr_meta.factory))
                {
                    co_return true;
                }

                if (ptr_meta.ptr != nullptr)
                {
                    auto* existing = FindSlotInChain(typeid(RefRaw), ptr_meta.factory);
                    const bool same_cached_ptr =
                        (existing != nullptr) && (existing->obj == ptr_meta.ptr);
                    if (same_cached_ptr && !ptr_meta.owned)
                    {
                        co_return true;
                    }

                    if (ptr_meta.owned)
                    {
                        CacheOwnedRaw<RefRaw>(ptr_meta.ptr, ptr_meta.factory);
                    }
                    else
                    {
                        CacheBorrowedRaw<RefRaw>(ptr_meta.ptr, ptr_meta.factory);
                    }
                    co_return true;
                }
                co_return false;
            }
        }
        else if constexpr (std::is_pointer_v<Param>)
        {
            using Pointee = std::remove_cv_t<std::remove_pointer_t<Param>>;
            if (auto ptr_meta_task = InjectRegistry::Resolve<Task<DependsPtrValue<Pointee>>>(target, index))
            {
                auto ptr_meta = co_await std::move(*ptr_meta_task);
                set_factory(ptr_meta.factory);

                if (TryPopulateRawSlotFromExplicit<Pointee>(target, ptr_meta.factory))
                {
                    if (auto* resolved = TryResolveRawPtr<Pointee>(target, ptr_meta.factory, ptr_meta.cached))
                    {
                        out = static_cast<Param>(resolved);
                        co_return true;
                    }
                }

                if (ptr_meta.ptr != nullptr)
                {
                    out = static_cast<Param>(ptr_meta.ptr);
                    auto* existing = FindSlotInChain(typeid(Pointee), ptr_meta.factory);
                    const bool same_cached_ptr =
                        (existing != nullptr) && (existing->obj == ptr_meta.ptr);
                    if (same_cached_ptr && !ptr_meta.owned)
                    {
                        co_return true;
                    }

                    if (ptr_meta.owned)
                    {
                        CacheOwnedRaw<Pointee>(ptr_meta.ptr, ptr_meta.factory);
                    }
                    else
                    {
                        CacheBorrowedRaw<Pointee>(ptr_meta.ptr, ptr_meta.factory);
                    }
                    co_return true;
                }
                co_return false;
            }
        }

        // Async plain-value metadata path (non-Depends expressions).
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

        // Fallback to sync metadata resolver for backward compatibility.
        co_return TryResolveDefaultArgForParam<A>(target, index, out, out_factory);
    }
}

#endif // __CPPBM_DEPENDS_RESOLVE_ASYNC_H__
