// File role:
// Factory-invocation utilities used by Depends(factory):
// - convert factory result to requested parameter category
// - invoke no-argument factories
// - compute slot positions for same-type parameters

#ifndef __CPPBM_DEPENDS_FACTORY_INVOKE_H__
#define __CPPBM_DEPENDS_FACTORY_INVOKE_H__

#include <array>
#include <concepts>
#include <memory>
#include <type_traits>
#include <unordered_map>

namespace cpp::blackmagic::depends
{
    template <typename V>
    concept Dereferenceable = requires(V & value)
    {
        *value;
    };

    template <typename To, typename From>
    To ConvertFactoryResult(From&& from)
    {
        using FromValue = std::remove_reference_t<From>;
        constexpr bool can_deref = Dereferenceable<FromValue>;

        if constexpr (std::is_reference_v<To>)
        {
            if constexpr (std::is_convertible_v<From&&, To>)
            {
                return static_cast<To>(std::forward<From>(from));
            }
            else if constexpr (can_deref
                && std::is_convertible_v<decltype(*std::declval<FromValue&>()), To>)
            {
                return static_cast<To>(*static_cast<FromValue&>(from));
            }
            else
            {
                static_assert(std::is_same_v<To, void>,
                    "Depends(factory): factory return type cannot be converted to target reference type.");
            }
        }
        else if constexpr (std::is_constructible_v<To, From&&>)
        {
            return To(std::forward<From>(from));
        }
        else if constexpr (
            std::is_pointer_v<To>
            && std::is_lvalue_reference_v<From&&>
            && std::is_convertible_v<
            std::add_pointer_t<std::remove_reference_t<From>>,
            To>)
        {
            // Allow borrowing address from reference-returning factories
            // when target parameter expects pointer.
            return static_cast<To>(std::addressof(from));
        }
        else if constexpr (can_deref
            && std::is_constructible_v<To, decltype(*std::declval<FromValue&>())>)
        {
            return To(*static_cast<FromValue&>(from));
        }
        else if constexpr (std::is_convertible_v<From&&, To>)
        {
            return static_cast<To>(std::forward<From>(from));
        }
        else
        {
            static_assert(std::is_same_v<To, void>,
                "Depends(factory): factory return type cannot be converted to target parameter type.");
        }

    }

    // Invoke dependency factory directly.
    // In this design factory is always no-argument.
    template <typename R>
    R InvokeFactory(R(*factory)())
    {
        return static_cast<R>(factory());
    }

    template <typename T>
    using SlotCanonicalT = std::remove_cv_t<std::remove_pointer_t<std::remove_reference_t<T>>>;

    template <typename... Args>
    std::array<std::size_t, sizeof...(Args)> BuildSlotPositionsByTypeOrder()
    {
        std::array<std::size_t, sizeof...(Args)> out{};
        std::unordered_map<std::type_index, std::size_t> counters{};
        std::size_t index = 0;
        ((out[index++] = counters[typeid(SlotCanonicalT<Args>)]++), ...);
        return out;
    }
}

#endif // __CPPBM_DEPENDS_FACTORY_INVOKE_H__
