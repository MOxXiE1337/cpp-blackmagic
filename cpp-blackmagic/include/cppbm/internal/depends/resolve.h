// File role:
// Resolution layer on top of runtime state/slots.
//
// This layer decides:
// - where dependency comes from (slot, explicit value, default metadata, default construction)
// - how to map requested type category (T, T*, T&) to slot representation
// - how to cache resolved values back into current context

#ifndef __CPPBM_DEPENDS_RESOLVE_H__
#define __CPPBM_DEPENDS_RESOLVE_H__

#include "placeholder.h"
#include "context.h"

namespace cpp::blackmagic::depends
{
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

        DependsExecutionScope scope{};

        if constexpr (std::is_reference_v<A>)
        {
            using RefRaw = std::remove_cv_t<std::remove_reference_t<A>>;
            if (auto ptr_meta = InjectRegistry::Resolve<DependsPtrValue<RefRaw>>(target, index))
            {
                set_factory(ptr_meta->factory);

                // Explicit injection override by (target, factory, type).
                if (TryPopulateRawSlotFromExplicit<RefRaw>(target, ptr_meta->factory))
                {
                    return true;
                }

                if (ptr_meta->ptr != nullptr)
                {
                    auto* existing = FindSlotInChain(typeid(RefRaw), ptr_meta->factory);
                    const bool same_cached_ptr =
                        (existing != nullptr) && (existing->obj == ptr_meta->ptr);

                    // Keep existing ownership state when metadata pointer is exactly the same.
                    // This avoids replacing an owned slot with a borrowed slot and creating
                    // a dangling pointer (common path for plain Depends()).
                    if (same_cached_ptr && !ptr_meta->owned)
                    {
                        return true;
                    }

                    if (ptr_meta->owned)
                    {
                        CacheOwnedRaw<RefRaw>(ptr_meta->ptr, ptr_meta->factory);
                    }
                    else
                    {
                        CacheBorrowedRaw<RefRaw>(ptr_meta->ptr, ptr_meta->factory);
                    }
                    return true;
                }
                return false;
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
            using Pointee = std::remove_cv_t<std::remove_pointer_t<Param>>;
            if (auto ptr_meta = InjectRegistry::Resolve<DependsPtrValue<Pointee>>(target, index))
            {
                set_factory(ptr_meta->factory);

                if (TryPopulateRawSlotFromExplicit<Pointee>(target, ptr_meta->factory))
                {
                    if (auto* resolved = TryResolveRawPtr<Pointee>(target, ptr_meta->factory, ptr_meta->cached))
                    {
                        out = static_cast<Param>(resolved);
                        return true;
                    }
                }

                if (ptr_meta->ptr != nullptr)
                {
                    out = static_cast<Param>(ptr_meta->ptr);
                    auto* existing = FindSlotInChain(typeid(Pointee), ptr_meta->factory);
                    const bool same_cached_ptr =
                        (existing != nullptr) && (existing->obj == ptr_meta->ptr);

                    // Keep existing ownership state when metadata pointer is exactly the same.
                    // This avoids replacing an owned slot with a borrowed slot and creating
                    // a dangling pointer (common path for plain Depends()).
                    if (same_cached_ptr && !ptr_meta->owned)
                    {
                        return true;
                    }

                    if (ptr_meta->owned)
                    {
                        CacheOwnedRaw<Pointee>(ptr_meta->ptr, ptr_meta->factory);
                    }
                    else
                    {
                        CacheBorrowedRaw<Pointee>(ptr_meta->ptr, ptr_meta->factory);
                    }
                    return true;
                }
                return false;
            }
        }

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

#endif // __CPPBM_DEPENDS_RESOLVE_H__
