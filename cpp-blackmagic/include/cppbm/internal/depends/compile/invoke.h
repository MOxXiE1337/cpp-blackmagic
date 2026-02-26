// File role:
// Factory-invocation utilities used by Depends(factory):
// - convert factory result to requested parameter category
// - invoke no-argument factories
// - compute slot positions for same-type parameters

#ifndef __CPPBM_DEPENDS_INVOKE_H__
#define __CPPBM_DEPENDS_INVOKE_H__

#include <array>
#include <concepts>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <unordered_map>

namespace cpp::blackmagic::depends
{
    // Task-like dependency factory result protocol:
    // if a type exposes Get(), Depends can treat it as "deferred result".
    // Example: Task<T*>, Task<T&>.
    template <typename V>
    concept GettableValue = requires(V & value)
    {
        value.Get();
    };

    template <typename V>
    concept Dereferenceable = requires(V & value)
    {
        *value;
    };

    // Safe dereference conversion checks.
    // Use requires-based concepts so non-dereferenceable types do not instantiate
    // invalid decltype(*x) expressions during template substitution on MSVC.
    template <typename From, typename To>
    concept DerefConvertibleTo = requires(From & value)
    {
        { *value } -> std::convertible_to<To>;
    };

    template <typename From, typename To>
    concept DerefConstructibleTo = requires(From & value)
    {
        To(*value);
    };

    // Factory return-category traits used by static_assert and ownership policy.
    // Supported recursive shape:
    // - pointer/reference directly
    // - task-like (Get()) whose result eventually resolves to pointer/reference
    template <typename R, typename = void>
    struct FactoryResultTraits
    {
        static constexpr bool kIsSupported =
            std::is_pointer_v<R> || std::is_reference_v<R>;
        static constexpr bool kProducesPointer = std::is_pointer_v<R>;
    };

    template <typename R>
    struct FactoryResultTraits<R, std::void_t<decltype(std::declval<std::remove_reference_t<R>&>().Get())>>
    {
        using Raw = std::remove_reference_t<R>;
        using GetResult = decltype(std::declval<Raw&>().Get());

        // Guard against malformed self-recursive task wrappers.
        static_assert(!std::is_same_v<GetResult, Raw>,
            "Depends(factory): task-like Get() must not return its own type recursively.");

        static constexpr bool kDirectSupported =
            std::is_pointer_v<R> || std::is_reference_v<R>;

        static constexpr bool kIsSupported =
            kDirectSupported || FactoryResultTraits<GetResult>::kIsSupported;

        static constexpr bool kProducesPointer =
            std::is_pointer_v<R> || FactoryResultTraits<GetResult>::kProducesPointer;
    };

    template <>
    struct FactoryResultTraits<void>
    {
        static constexpr bool kIsSupported = false;
        static constexpr bool kProducesPointer = false;
    };

    template <typename R>
    inline constexpr bool kIsSupportedFactoryReturnV = FactoryResultTraits<R>::kIsSupported;

    // Pointer-producing factories are treated as "owned by DI call scope".
    // This is intentionally aligned with existing pointer factory semantics.
    template <typename R>
    inline constexpr bool kFactoryProducesPointerV = FactoryResultTraits<R>::kProducesPointer;

    template <typename To, typename From>
    To ConvertFactoryResult(From&& from)
    {
        using FromValue = std::remove_reference_t<From>;

        if constexpr (std::is_reference_v<To>)
        {
            if constexpr (std::is_convertible_v<From&&, To>)
            {
                return static_cast<To>(std::forward<From>(from));
            }
            else if constexpr (GettableValue<FromValue>)
            {
                // Async/task-like factory result:
                // drive to completion and continue with normal conversion.
                return ConvertFactoryResult<To>(std::forward<From>(from).Get());
            }
            else if constexpr (DerefConvertibleTo<FromValue, To>)
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
        else if constexpr (GettableValue<FromValue>)
        {
            // Async/task-like factory result:
            // drive to completion and continue with normal conversion.
            return ConvertFactoryResult<To>(std::forward<From>(from).Get());
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
        else if constexpr (DerefConstructibleTo<FromValue, To>)
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
    decltype(auto) InvokeFactory(R(*factory)())
    {
        // Preserve exact factory return category.
        // Important for reference-returning factories (e.g. Config&):
        // using decltype(auto) + direct return avoids accidental decay.
        return factory();
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

#endif // __CPPBM_DEPENDS_INVOKE_H__
