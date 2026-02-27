#ifndef __CPPBM_DECORATOR_H__
#define __CPPBM_DECORATOR_H__

#include <concepts>
#include <tuple>
#include <type_traits>

#include "internal/hook/error.h"
#include "internal/hook/hook.h"
#include "internal/utils/noncopyable.h"

namespace cpp::blackmagic
{
    namespace decorator
    {
        template <typename T>
        concept FreeFunctionPointer =
            std::is_pointer_v<std::remove_cvref_t<T>> &&
            std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<T>>>;

        template <typename T>
        concept MemberFunctionPointer =
            std::is_member_function_pointer_v<std::remove_cvref_t<T>>;

        template <auto Target>
        concept DecoratorTarget =
            MemberFunctionPointer<decltype(Target)> ||
            FreeFunctionPointer<decltype(Target)>;
    }

    namespace detail
    {
        template <auto Target, typename Fn>
        class Decorator;

        template <auto Target, typename R, typename... Args>
        class Decorator<Target, R(*)(Args...)>
            : public hook::FreeHookBase<Target, R, Args...>
        {
        };

#ifdef _CPPBM_HOOK_WIN32
        template <auto Target, typename R, typename... Args>
        class Decorator<Target, R(__stdcall*)(Args...)>
            : public hook::FreeHookBaseStdcall<Target, R, Args...>
        {
        };

        template <auto Target, typename R, typename... Args>
        class Decorator<Target, R(__fastcall*)(Args...)>
            : public hook::FreeHookBaseFastcall<Target, R, Args...>
        {
        };
#endif // _CPPBM_HOOK_WIN32

        template <auto Target, typename C, typename R, typename... Args>
        class Decorator<Target, R(C::*)(Args...)>
            : public hook::MemberHookBase<Target, R(C::*)(Args...), C*, R, Args...>
        {
        };

        template <auto Target, typename C, typename R, typename... Args>
        class Decorator<Target, R(C::*)(Args...) const>
            : public hook::MemberHookBase<Target, R(C::*)(Args...) const, const C*, R, Args...>
        {
        };
    }

    template <auto Target>
        requires decorator::DecoratorTarget<Target>
    class FunctionDecorator
        : public detail::Decorator<Target, decltype(Target)>
    {
    public:
        using Base = detail::Decorator<Target, decltype(Target)>;

        FunctionDecorator()
        {
            (void)this->RegisterDecoratorNode();
        }

        ~FunctionDecorator()
        {
            this->UnregisterDecoratorNode();
        }
    };

    // Decorator binding, to maintain decorator object lifetime.
    template <auto Target, template<auto> class DecoratorT>
    class DecoratorBinding : private utils::NonCopyable
    {
    public:
        DecoratorBinding() = default;
        ~DecoratorBinding() = default;

    private:
        DecoratorT<Target> decorator_{};
    };

    // Decorator binder, to be used like:
    //   inline auto reg = logger.Bind<&Foo>();
    template <template<auto> class DecoratorT>
    class DecoratorBinder
    {
    public:
        template <auto Target, typename... Metas>
        auto Bind(Metas&&...) const
        {
            // Unknown metadata is intentionally ignored by generic binder.
            // Specialized binders (inject/router/...) can selectively consume
            // metadata they understand.
            return DecoratorBinding<Target, DecoratorT>{};
        }
    };
}

#endif // __CPPBM_DECORATOR_H__

// Decorate a function with this macro:
//   decorator(@inject, @router.get("/"))
#define decorator(expr) /* expr */

// Binder declaration helper.
#define CPPBM_DECORATOR_BINDER(decorator, name) inline constexpr ::cpp::blackmagic::DecoratorBinder<decorator> name{}
