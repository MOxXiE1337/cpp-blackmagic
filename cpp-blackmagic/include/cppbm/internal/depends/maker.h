// File role:
// Concrete Depends maker objects used in default argument expressions.
//
// Two forms:
// - DependsMaker            : from Depends()
// - DependsMakerWithFactory : from Depends(factory)
//
// Both makers have dual behavior:
// 1) outside DependsExecutionScope:
//    return placeholder markers (so function declaration remains valid)
// 2) inside DependsExecutionScope:
//    resolve real dependencies immediately

#ifndef __CPPBM_DEPENDS_MAKER_H__
#define __CPPBM_DEPENDS_MAKER_H__

#include <concepts>
#include <type_traits>

#include "factory_invoke.h"
#include "placeholder.h"
#include "resolve.h"

namespace cpp::blackmagic::depends
{
    struct DependsMaker
    {
        bool cached = true;

        template <typename T>
            requires (!std::is_reference_v<T> && !std::is_pointer_v<T>)
        operator T& () const
        {
            if (ShouldExecuteDependsFactories())
            {
                return ResolveByType<T&>(nullptr, true, nullptr, cached);
            }
            return DependsReferenceMarker<T&>();
        }

        template <typename T>
            requires (!std::is_reference_v<T> && !std::is_pointer_v<T>)
        operator T* () const
        {
            if (ShouldExecuteDependsFactories())
            {
                return ResolveByType<T*>(nullptr, true, nullptr, cached);
            }
            return DependsPointerMarker<T*>();
        }

        template <typename T>
            requires (!std::is_reference_v<T> && !std::is_pointer_v<T>)
        operator T() const
        {
            if (ShouldExecuteDependsFactories())
            {
                return ResolveByType<T>(nullptr, true, nullptr, cached);
            }
            if constexpr (std::is_default_constructible_v<T>)
            {
                return T{};
            }
            return FailInject<T>(InjectError{
                InjectErrorCode::InvalidPlaceholder,
                nullptr,
                static_cast<std::size_t>(-1),
                typeid(T),
                nullptr,
                "Depends() placeholder requires default-constructible T when T is value type."
                });
        }
    };

    template <typename T>
    struct DependsMakerWithFactory
    {
        // Factory must be no-arg and return:
        // - pointer/reference directly, or
        // - task-like object with Get() whose final result is pointer/reference.
        static_assert(kIsSupportedFactoryReturnV<T>,
            "Depends(factory): factory return type must be pointer/reference "
            "or task-like with Get() resolving to pointer/reference.");

        T(*factory)() = nullptr;
        bool cached = true;

        template <typename U>
            requires (!std::is_reference_v<U> && !std::is_pointer_v<U>)
        operator U& () const
        {
            if (ShouldExecuteDependsFactories())
            {
                return ConvertFactoryResult<U&>(InvokeFactory<T>(factory));
            }
            return DependsReferenceMarker<U&>();
        }

        template <typename U>
            requires (!std::is_reference_v<U> && !std::is_pointer_v<U>)
        operator U* () const
        {
            if (ShouldExecuteDependsFactories())
            {
                return ConvertFactoryResult<U*>(InvokeFactory<T>(factory));
            }
            return DependsPointerMarker<U*>();
        }

        template <typename U>
            requires (!std::is_reference_v<U> && !std::is_pointer_v<U>)
        operator U() const
        {
            if (ShouldExecuteDependsFactories())
            {
                return ConvertFactoryResult<U>(InvokeFactory<T>(factory));
            }
            if constexpr (std::is_default_constructible_v<U>)
            {
                return U{};
            }
            return FailInject<U>(InjectError{
                InjectErrorCode::InvalidPlaceholder,
                nullptr,
                static_cast<std::size_t>(-1),
                typeid(U),
                FactoryKeyOf(factory),
                "Depends(factory) placeholder requires default-constructible U when U is value type."
                });
        }
    };
}

#endif // __CPPBM_DEPENDS_MAKER_H__

