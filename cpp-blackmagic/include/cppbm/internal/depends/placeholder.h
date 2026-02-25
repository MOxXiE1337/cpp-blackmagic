// File role:
// This header contains the "Depends placeholder protocol".
//
// Why protocol exists:
// - In user code, default argument is written as Depends(...).
// - At function declaration stage we cannot resolve real dependency yet.
// - So Depends(...) first returns a marker value ("placeholder").
// - Later, @inject runtime checks whether an argument is this marker.
// - If marker is detected, resolver performs dependency injection for that parameter.
//
// Important design rule:
// - nullptr is a valid user argument and MUST NOT mean "inject".
// - Therefore pointer placeholder uses a unique non-null marker pointer.

#ifndef __CPPBM_DEPENDS_PLACEHOLDER_H__
#define __CPPBM_DEPENDS_PLACEHOLDER_H__

#include <type_traits>

#include "error.h"

namespace cpp::blackmagic::depends
{
    // Types that can carry placeholder markers directly.
    // In this design we only support:
    // - reference parameter types
    // - pointer parameter types
    template <typename T>
    inline constexpr bool SupportsDependsPlaceholderV =
        std::is_reference_v<T>
        || std::is_pointer_v<T>;

    // Reference placeholder marker:
    // Depends() used in a T& parameter returns a thread-local singleton T.
    //
    // Why thread-local:
    // - marker identity must be stable within one thread
    // - avoids cross-thread sharing and locking
    //
    // Why default-constructible requirement:
    // - marker object needs concrete storage.
    template <typename T>
    std::remove_reference_t<T>& DependsReferenceMarker()
    {
        using U = std::remove_reference_t<T>;
        if constexpr (std::is_default_constructible_v<U>)
        {
            static thread_local U marker{};
            return marker;
        }
        else
        {
            return FailInject<U&>(InjectError{
                InjectErrorCode::InvalidPlaceholder,
                nullptr,
                static_cast<std::size_t>(-1),
                typeid(U),
                nullptr,
                "Depends(T&) requires default-constructible T for reference placeholder."
                });
        }
    }

    // Pointer placeholder marker:
    // Returns a unique, stable, non-null pointer value used only for identity checks.
    //
    // Implementation note:
    // - We use address of a thread-local int and reinterpret_cast it to T*.
    // - The pointee object is never dereferenced as T.
    // - Only pointer equality is used.
    template <typename T>
    std::remove_pointer_t<T>* DependsPointerMarker()
    {
        static thread_local int marker = 0;
        return reinterpret_cast<std::remove_pointer_t<T>*>(&marker);
    }

    // Build placeholder value by declared parameter type.
    // - T& => reference marker
    // - T* => non-null pointer marker
    // - T  => default-constructed value (not treated as placeholder)
    template <typename T>
    T DependsPlaceholder()
    {
        if constexpr (std::is_reference_v<T>)
            return DependsReferenceMarker<T>();
        else if constexpr (std::is_pointer_v<T>)
            return DependsPointerMarker<T>();
        else
            return T{};
    }

    // Runtime predicate used by @inject to decide whether this argument
    // explicitly requested dependency injection via Depends(...).
    //
    // Detection rules:
    // - reference type: compare object address with reference marker address
    // - pointer type:   compare pointer value with pointer marker value
    // - value type:     always false
    template <typename T>
    bool IsDependsPlaceholder(const T& value)
    {
        if constexpr (std::is_reference_v<T>)
        {
            using U = std::remove_reference_t<T>;
            return std::addressof(value) == std::addressof(DependsReferenceMarker<U&>());
        }
        else if constexpr (std::is_pointer_v<T>)
        {
            return value == DependsPointerMarker<T>();
        }
        else
        {
            return false;
        }
    }
}

#endif // __CPPBM_DEPENDS_PLACEHOLDER_H__
