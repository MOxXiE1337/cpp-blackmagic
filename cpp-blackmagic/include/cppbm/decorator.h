#include "hook.h"

#ifndef __CPPBM_DECORATOR_H__
#define __CPPBM_DECORATOR_H__

namespace cpp::blackmagic
{
    // Decorators 
    template <typename Func, typename... Args>
    class Decorator;

    // Free-function specialization.
    template <typename R, typename... RealArgs, typename... DeclaredArgs>
    class Decorator<R(*)(RealArgs...), DeclaredArgs...> : public hook::FreeHookBase<
        Decorator<R(*)(RealArgs...), DeclaredArgs...>,
        R,
        RealArgs...>
    {
    public:
        using Self = Decorator<R(*)(RealArgs...), DeclaredArgs...>;
        using Base = hook::FreeHookBase<Self, R, RealArgs...>;

        static_assert(
            sizeof...(DeclaredArgs) == 0 ||
            std::is_same_v<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>,
            "If Args... is provided, it must exactly match Func argument list.");

        using Base::Base;
        using Base::BeforeCall;
        using Base::Call;
        using Base::AfterCall;
        using Base::CallOriginal;
        using Base::Install;
        using Base::Uninstall;
        using Base::IsInstalled;
    };

// __stdcall and fastcall for msvc x86
#ifdef _CPPBM_HOOK_WIN32
    // __stdcall
    template <typename R, typename... RealArgs, typename... DeclaredArgs>
    class Decorator<R(__stdcall*)(RealArgs...), DeclaredArgs...> : public hook::FreeHookBaseStdcall<
        Decorator<R(__stdcall*)(RealArgs...), DeclaredArgs...>,
        R,
        RealArgs...>
    {
    public:
        using Self = Decorator<R(__stdcall*)(RealArgs...), DeclaredArgs...>;
        using Base = hook::FreeHookBaseStdcall<Self, R, RealArgs...>;

        static_assert(
            sizeof...(DeclaredArgs) == 0 ||
            std::is_same_v<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>,
            "If Args... is provided, it must exactly match Func argument list.");

        using Base::Base;
        using Base::BeforeCall;
        using Base::Call;
        using Base::AfterCall;
        using Base::CallOriginal;
        using Base::Install;
        using Base::Uninstall;
        using Base::IsInstalled;
    };

    // __fastcall
    template <typename R, typename... RealArgs, typename... DeclaredArgs>
    class Decorator<R(__fastcall*)(RealArgs...), DeclaredArgs...> : public hook::FreeHookBaseFastcall<
        Decorator<R(__fastcall*)(RealArgs...), DeclaredArgs...>,
        R,
        RealArgs...>
    {
    public:
        using Self = Decorator<R(__fastcall*)(RealArgs...), DeclaredArgs...>;
        using Base = hook::FreeHookBaseFastcall<Self, R, RealArgs...>;

        static_assert(
            sizeof...(DeclaredArgs) == 0 ||
            std::is_same_v<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>,
            "If Args... is provided, it must exactly match Func argument list.");

        using Base::Base;
        using Base::BeforeCall;
        using Base::Call;
        using Base::AfterCall;
        using Base::CallOriginal;
        using Base::Install;
        using Base::Uninstall;
        using Base::IsInstalled;
    };

#endif // _CPPBM_HOOK_WIN32

    // Non-const member-function specialization.
    template <typename C, typename R, typename... RealArgs, typename... DeclaredArgs>
    class Decorator<R(C::*)(RealArgs...), DeclaredArgs...>
        : public hook::MemberHookBase<
        Decorator<R(C::*)(RealArgs...), DeclaredArgs...>,
        R(C::*)(RealArgs...),
        C*,
        R,
        RealArgs...> {
    public:
        using Self = Decorator<R(C::*)(RealArgs...), DeclaredArgs...>;
        using Base = hook::MemberHookBase<Self, R(C::*)(RealArgs...), C*, R, RealArgs...>;

        static_assert(
            sizeof...(DeclaredArgs) == 0 ||
            std::is_same_v<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>,
            "If Args... is provided, it must exactly match Func argument list.");

        using Base::Base;
        using Base::BeforeCall;
        using Base::Call;
        using Base::AfterCall;
        using Base::CallOriginal;
        using Base::Install;
        using Base::Uninstall;
        using Base::IsInstalled;
    };

    // Const member-function specialization.
    template <typename C, typename R, typename... RealArgs, typename... DeclaredArgs>
    class Decorator<R(C::*)(RealArgs...) const, DeclaredArgs...>
        : public hook::MemberHookBase<
        Decorator<R(C::*)(RealArgs...) const, DeclaredArgs...>,
        R(C::*)(RealArgs...) const,
        const C*,
        R,
        RealArgs...> {
    public:
        using Self = Decorator<R(C::*)(RealArgs...) const, DeclaredArgs...>;
        using Base = hook::MemberHookBase<Self, R(C::*)(RealArgs...) const, const C*, R, RealArgs...>;

        static_assert(
            sizeof...(DeclaredArgs) == 0 ||
            std::is_same_v<std::tuple<DeclaredArgs...>, std::tuple<RealArgs...>>,
            "If Args... is provided, it must exactly match Func argument list.");

        using Base::Base;
        using Base::BeforeCall;
        using Base::Call;
        using Base::AfterCall;
        using Base::CallOriginal;
        using Base::Install;
        using Base::Uninstall;
        using Base::IsInstalled;
    };

    // Helper classes, not fully tested
    template <auto Target>
    class AutoDecorator : public Decorator<decltype(Target)>
    {
    public:
        using Fn = decltype(Target);
      
        AutoDecorator() : Decorator<Fn>(Target) {}
        explicit AutoDecorator(Fn /*unused*/) : Decorator<Fn>(Target) {}
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
