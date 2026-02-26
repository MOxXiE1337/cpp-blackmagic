// File role:
// Synchronous resolution layer on top of runtime state/slots.
//
// This layer decides:
// - where dependency comes from (slot, explicit value, default metadata, default construction)
// - how to map requested type category (T, T*, T&) to slot representation
// - how to cache resolved values back into current context

#ifndef __CPPBM_DEPENDS_RESOLVE_SYNC_H__
#define __CPPBM_DEPENDS_RESOLVE_SYNC_H__

#include "../placeholder.h"
#include "../context.h"

namespace cpp::blackmagic::depends
{
    // Ensure one raw slot of type T is available in current context chain.
    //
    // Behavior knobs:
    // - allow_default: permit constructing default T when no slot/explicit value exists
    // - factory: slot key partition for Depends(factory) isolation
    // - cached: when true, first try reusing existing slot from current/parent contexts
    template <typename T>
    [[nodiscard]] ContextSlot* EnsureRawSlot(
        const void* target,
        bool allow_default,
        const void* factory = nullptr,
        bool cached = true)
    {
        // Resolution order:
        // 1) existing slot in current/parent chain
        // 2) explicit registration (InjectDependency)
        // 3) optional default construction (if allowed)
        if (cached)
        {
            if (auto* slot = FindSlotInChain(typeid(T), factory); slot && slot->obj != nullptr)
            {
                return slot;
            }
        }

        if (TryPopulateRawSlotFromExplicit<T>(target, factory))
        {
            auto* slot = FindSlotInChain(typeid(T), factory);
            if (slot && slot->obj != nullptr)
            {
                return slot;
            }
        }

        if (!allow_default)
        {
            return nullptr;
        }

        if constexpr (std::is_default_constructible_v<T>)
        {
            CacheOwnedDefault<T>(factory);
            return FindSlotInChain(typeid(T), factory);
        }

        return nullptr;
    }

    // Try resolve a non-reference dependency type U.
    // Returns nullopt on miss instead of raising failure.
    template <typename U>
    [[nodiscard]] std::optional<U> TryResolveByType(
        const void* target,
        const void* factory = nullptr,
        bool cached = true)
    {
        static_assert(!std::is_reference_v<U>,
            "TryResolveByType does not support reference types.");

        using V = RemoveCvRefT<U>;

        if constexpr (std::is_pointer_v<U>)
        {
            using Pointee = std::remove_cv_t<std::remove_pointer_t<U>>;
            auto* slot = EnsureRawSlot<Pointee>(target, false, factory, cached);
            if (slot == nullptr || slot->obj == nullptr)
            {
                return std::nullopt;
            }
            return static_cast<U>(slot->obj);
        }
        else
        {
            auto* slot = EnsureRawSlot<V>(target, false, factory, cached);
            if (slot == nullptr || slot->obj == nullptr)
            {
                return std::nullopt;
            }
            return static_cast<U>(*static_cast<V*>(slot->obj));
        }
    }

    // Lightweight raw-pointer resolver used by reference-parameter call paths.
    // Returns nullptr on miss.
    template <typename T>
    [[nodiscard]] std::remove_cv_t<T>* TryResolveRawPtr(
        const void* target,
        const void* factory = nullptr,
        bool cached = true)
    {
        using Raw = std::remove_cv_t<T>;
        auto* slot = EnsureRawSlot<Raw>(target, false, factory, cached);
        if (slot == nullptr || slot->obj == nullptr)
        {
            return nullptr;
        }
        return static_cast<Raw*>(slot->obj);
    }

    // Strict resolver variant that fails via FailInject on missing dependency.
    // Used in paths where missing dependency is a hard runtime error.
    template <typename U>
    U ResolveByType(
        const void* target,
        bool allow_default,
        const void* factory = nullptr,
        bool cached = true)
    {
        if constexpr (std::is_reference_v<U>)
        {
            using V = std::remove_cv_t<std::remove_reference_t<U>>;
            auto* slot = EnsureRawSlot<V>(target, allow_default, factory, cached);
            if (slot == nullptr || slot->obj == nullptr)
            {
                return FailInject<U>(InjectError{
                    InjectErrorCode::MissingDependency,
                    target,
                    static_cast<std::size_t>(-1),
                    typeid(V),
                    factory,
                    "Depends resolve failed: missing slot(T&) in ResolveByType(reference)."
                    });
            }
            return static_cast<U>(*static_cast<V*>(slot->obj));
        }
        if constexpr (std::is_pointer_v<U>)
        {
            using V = std::remove_cv_t<std::remove_pointer_t<U>>;
            auto* slot = EnsureRawSlot<V>(target, allow_default, factory, cached);
            if (slot == nullptr || slot->obj == nullptr)
            {
                return FailInject<U>(InjectError{
                    InjectErrorCode::MissingDependency,
                    target,
                    static_cast<std::size_t>(-1),
                    typeid(V),
                    factory,
                    "Depends resolve failed: missing slot(T*) in ResolveByType(pointer)."
                    });
            }
            return static_cast<U>(slot->obj);
        }
        else
        {
            using V = RemoveCvRefT<U>;
            auto* slot = EnsureRawSlot<V>(target, allow_default, factory, cached);
            if (slot == nullptr || slot->obj == nullptr)
            {
                return FailInject<U>(InjectError{
                    InjectErrorCode::MissingDependency,
                    target,
                    static_cast<std::size_t>(-1),
                    typeid(V),
                    factory,
                    "Depends resolve failed: missing slot(T) in ResolveByType(value)."
                    });
            }
            return static_cast<U>(*static_cast<V*>(slot->obj));
        }
    }

    // Cache one resolved value into current context with category-aware ownership:
    // - pointer -> borrowed slot
    // - value   -> owned slot
    template <typename U>
    void CacheResolvedValue(U&& value, const void* factory = nullptr)
    {
        using V = std::remove_cvref_t<U>;
        if constexpr (std::is_pointer_v<V>)
        {
            if (value != nullptr)
            {
                CacheBorrowedRaw<std::remove_pointer_t<V>>(value, factory);
            }
        }
        else
        {
            CacheOwnedValue<V>(std::forward<U>(value), factory);
        }
    }

    // Resolve one default-argument metadata entry for parameter A.
    //
    // Expected usage:
    // - called only when caller passed Depends placeholder
    // - returns true when argument is resolved and/or slot is prepared
    // - returns false when no usable metadata exists or resolution failed
    //
    // Resolution phases:
    // 1) pointer-metadata path: DependsPtrValue<Raw> for reference/pointer params
    // 2) fallback plain-value metadata path: Resolve<Param>
    // 3) legacy compatibility path for old RefRaw* metadata (reference params only)
    template <typename A>
    bool TryResolveDefaultArgForParam(
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

        // Shared pointer-metadata resolver for both:
        // - reference params (WriteOut=false): slot preparation only
        // - pointer params   (WriteOut=true): also writes out pointer argument
        auto resolve_ptr_meta = [&]<typename Raw, bool WriteOut>() -> bool
            {
                if (auto ptr_meta = InjectRegistry::Resolve<DependsPtrValue<Raw>>(target, index))
                {
                    set_factory(ptr_meta->factory);

                    const bool is_plain_depends_placeholder =
                        ptr_meta->factory == nullptr
                        && IsDependsPlaceholder<Raw*>(ptr_meta->ptr);

                    // Highest priority: explicit override registry for exact key.
                    if (TryPopulateRawSlotFromExplicit<Raw>(target, ptr_meta->factory))
                    {
                        if constexpr (WriteOut)
                        {
                            if (auto* resolved = TryResolveRawPtr<Raw>(target, ptr_meta->factory, ptr_meta->cached))
                            {
                                out = static_cast<Param>(resolved);
                                return true;
                            }
                        }
                        else
                        {
                            return true;
                        }
                    }

                    // Depends() placeholder metadata:
                    // resolve from existing/explicit/default slot flow (factory == nullptr).
                    if (is_plain_depends_placeholder)
                    {
                        auto* slot = EnsureRawSlot<Raw>(target, true, nullptr, ptr_meta->cached);
                        if (slot == nullptr || slot->obj == nullptr)
                        {
                            return false;
                        }
                        if constexpr (WriteOut)
                        {
                            out = static_cast<Param>(slot->obj);
                        }
                        return true;
                    }

                    // Metadata already produced concrete pointer.
                    // Cache it into current context with owned/borrowed policy.
                    if (ptr_meta->ptr != nullptr)
                    {
                        if constexpr (WriteOut)
                        {
                            out = static_cast<Param>(ptr_meta->ptr);
                        }

                        auto* existing = FindSlotInChain(typeid(Raw), ptr_meta->factory);
                        const bool same_cached_ptr =
                            (existing != nullptr) && (existing->obj == ptr_meta->ptr);

                        // Keep existing ownership state when metadata pointer is exactly the same.
                        // Avoid replacing owned slot by borrowed slot for same pointer value.
                        if (same_cached_ptr && !ptr_meta->owned)
                        {
                            return true;
                        }

                        if (ptr_meta->owned)
                        {
                            CacheOwnedRaw<Raw>(ptr_meta->ptr, ptr_meta->factory);
                        }
                        else
                        {
                            CacheBorrowedRaw<Raw>(ptr_meta->ptr, ptr_meta->factory);
                        }
                        return true;
                    }
                    return false;
                }
                return false;
            };

        if constexpr (std::is_reference_v<A>)
        {
            using RefRaw = std::remove_cv_t<std::remove_reference_t<A>>;
            if (resolve_ptr_meta.template operator()<RefRaw, false>())
            {
                return true;
            }

            // Backward-compatibility path for old generated metadata.
            if (auto ptr_value = InjectRegistry::Resolve<RefRaw*>(target, index))
            {
                set_factory(nullptr);
                if (TryPopulateRawSlotFromExplicit<RefRaw>(target, nullptr))
                {
                    return true;
                }
                if (*ptr_value != nullptr)
                {
                    CacheBorrowedRaw<RefRaw>(*ptr_value, nullptr);
                    return true;
                }
                return false;
            }
        }
        else if constexpr (std::is_pointer_v<Param>)
        {
            // Pointer parameter path uses the same metadata flow with WriteOut=true.
            using Pointee = std::remove_cv_t<std::remove_pointer_t<Param>>;
            if (resolve_ptr_meta.template operator()<Pointee, true>())
            {
                return true;
            }
        }

        // Plain-value metadata path.
        // This is defensive/compatibility behavior and is not the main Depends flow.
        auto value = InjectRegistry::Resolve<Param>(target, index);
        set_factory(nullptr);
        if (!value)
        {
            return false;
        }

        if (IsDependsPlaceholder<Param>(*value))
        {
            return false;
        }

        if constexpr (std::is_reference_v<A>)
        {
            using RefRaw = std::remove_cv_t<std::remove_reference_t<A>>;
            if constexpr (std::is_pointer_v<Param>)
            {
                if (*value != nullptr)
                {
                    CacheBorrowedRaw<std::remove_pointer_t<Param>>(*value, nullptr);
                    return true;
                }
                return false;
            }
            else if constexpr (std::is_constructible_v<RefRaw, Param&&>)
            {
                CacheOwnedValue<RefRaw>(RefRaw(std::move(*value)), nullptr);
                return true;
            }
            return false;
        }

        if constexpr (std::is_assignable_v<A&, Param>)
        {
            out = std::move(*value);
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
            return true;
        }

        return false;
    }
}

#endif // __CPPBM_DEPENDS_RESOLVE_SYNC_H__
