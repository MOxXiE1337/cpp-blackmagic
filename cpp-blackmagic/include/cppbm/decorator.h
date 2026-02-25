#ifndef __CPPBM_DECORATOR_H__
#define __CPPBM_DECORATOR_H__

#include <concepts>
#include <tuple>
#include <type_traits>

#include "internal/hook/error.h"
#include "internal/hook/hook.h"

namespace cpp::blackmagic
{
    namespace impl
    {
        // Decorators
        template <typename Func, typename... Args>
        class Decorator;

        // Useful concepts
        template <typename DeclaredTuple, typename RealTuple>
        concept ArgsCompatible =
            std::same_as<DeclaredTuple, std::tuple<>> ||
            std::same_as<DeclaredTuple, RealTuple>;

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

        // Free-function specialization.
        template <typename R, typename... RealArgs, typename... DeclaredArgs>
            requires ArgsCompatible<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>
        class Decorator<R(*)(RealArgs...), DeclaredArgs...>
            : public hook::FreeHookBase<Decorator<R(*)(RealArgs...), DeclaredArgs...>, R, RealArgs...>
        {
        public:
            using Base = hook::FreeHookBase<Decorator<R(*)(RealArgs...), DeclaredArgs...>, R, RealArgs...>;
            using Base::Base;
        };

        // __stdcall and fastcall for msvc x86
#ifdef _CPPBM_HOOK_WIN32
        // __stdcall
        template <typename R, typename... RealArgs, typename... DeclaredArgs>
            requires ArgsCompatible<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>
        class Decorator<R(__stdcall*)(RealArgs...), DeclaredArgs...>
            : public hook::FreeHookBaseStdcall<Decorator<R(__stdcall*)(RealArgs...), DeclaredArgs...>, R, RealArgs...>
        {
        public:
            using Base = hook::FreeHookBaseStdcall<Decorator<R(__stdcall*)(RealArgs...), DeclaredArgs...>, R, RealArgs...>;
            using Base::Base;
        };

        // __fastcall
        template <typename R, typename... RealArgs, typename... DeclaredArgs>
            requires ArgsCompatible<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>
        class Decorator<R(__fastcall*)(RealArgs...), DeclaredArgs...>
            : public hook::FreeHookBaseFastcall<Decorator<R(__fastcall*)(RealArgs...), DeclaredArgs...>, R, RealArgs...>
        {
        public:
            using Base = hook::FreeHookBaseFastcall<Decorator<R(__fastcall*)(RealArgs...), DeclaredArgs...>, R, RealArgs...>;
            using Base::Base;
        };

#endif // _CPPBM_HOOK_WIN32

        // Non-const member-function specialization.
        template <typename C, typename R, typename... RealArgs, typename... DeclaredArgs>
            requires ArgsCompatible<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>
        class Decorator<R(C::*)(RealArgs...), DeclaredArgs...>
            : public hook::MemberHookBase<
            Decorator<R(C::*)(RealArgs...), DeclaredArgs...>,
            R(C::*)(RealArgs...),
            C*,
            R,
            RealArgs...>
        {
        public:
            using Base = hook::MemberHookBase<
                Decorator<R(C::*)(RealArgs...), DeclaredArgs...>,
                R(C::*)(RealArgs...),
                C*,
                R,
                RealArgs...>;
            using Base::Base;
        };

        // Const member-function specialization.
        template <typename C, typename R, typename... RealArgs, typename... DeclaredArgs>
            requires ArgsCompatible<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>
        class Decorator<R(C::*)(RealArgs...) const, DeclaredArgs...>
            : public hook::MemberHookBase<
            Decorator<R(C::*)(RealArgs...) const, DeclaredArgs...>,
            R(C::*)(RealArgs...) const,
            const C*,
            R,
            RealArgs...>
        {
        public:
            using Base = hook::MemberHookBase<
                Decorator<R(C::*)(RealArgs...) const, DeclaredArgs...>,
                R(C::*)(RealArgs...) const,
                const C*,
                R,
                RealArgs...>;
            using Base::Base;
        };
    }

    // Auto decorator
    // Can be used like decorator(@xxx)
    template <auto Target>
        requires impl::DecoratorTarget<Target>
    class FunctionDecorator
        : public impl::Decorator<decltype(Target)>
    {
    public:
        using Fn = decltype(Target);
        using Base = impl::Decorator<Fn>;
        static constexpr Fn kTarget = Target;

        FunctionDecorator() : Base(kTarget)
        {
            (void)this->Install();
        }

        explicit FunctionDecorator(Fn /*unused*/) : Base(kTarget)
        {
            (void)this->Install();
        }
    };
}

#endif // __CPPBM_DECORATOR_H__

// Comment: to decorate a class, use design pattern XD

// Use this macro to decorate a function (must enable preprocessor!)
#define decorator(t) /* t */

// Use this macro to tag a decorator class
// Then, the decorator class can be correctly parsed with @name
#define decorator_class(name) _Decorator_##name##_

// Warning: decorator macro only applies to the immediately following function.
// No matter where the function is
// e.g
// class test { decorator(@test) };
// namespace testns { void func() {} }

// The decorator will be applied to testns::func!

// The used decorator class will be parsed to Decorator_test
// If you want to use decorator macro and create decorator class manually, please name the class to Decorator_classname
