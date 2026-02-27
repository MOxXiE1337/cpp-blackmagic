#ifndef __CPPBM_HOOK_HOOK_H__
#define __CPPBM_HOOK_HOOK_H__

#include <cstring>
#include <type_traits>
#include <utility>

#include "pipeline.h"
#include "registry.h"
#include "../utils/noncopyable.h"

#ifdef _WIN32
#ifndef _WIN64
#define _CPPBM_HOOK_WIN32
#endif // _WIN64
#endif // _WIN32

namespace cpp::blackmagic::hook
{
    // Convert a member-function pointer into a code address for MinHook.
    // Requires the member pointer representation to fit one machine pointer.
    template <typename MemFn>
        requires std::is_member_function_pointer_v<std::remove_cvref_t<MemFn>>
    inline void* MemberPointerToAddress(MemFn fn)
    {
        void* addr = nullptr;
        std::memcpy(&addr, &fn, sizeof(addr));
        return addr;
    }

    template <typename R, typename ThisPtr, typename... Args>
    struct MemberOriginalFnT
    {
#ifdef _CPPBM_HOOK_WIN32
        using type = R(__thiscall*)(ThisPtr, Args...);
#else
        using type = R(*)(ThisPtr, Args...);
#endif
    };

    template <typename R, typename ThisPtr, typename... Args>
    using MemberOriginalFn = typename MemberOriginalFnT<R, ThisPtr, Args...>::type;

    // Common helper base for bridge nodes that register/unregister into HookPipeline.
    template <typename Derived, typename Node>
    class HookPipelineNodeBase : public Node, private utils::NonCopyable
    {
    protected:
        bool RegisterDecoratorNode()
        {
            return Derived::GetPipeline().RegisterDecorator(static_cast<Node*>(this));
        }

        void UnregisterDecoratorNode()
        {
            Derived::GetPipeline().UnregisterDecorator(static_cast<Node*>(this));
        }
    };

    // Free-function bridge:
    // - exposes GetPipeline
    // - routes detour to pipeline dispatch
    // - exposes CallOriginal to derived decorators
    template <auto Target, typename R, typename... Args>
    class FreeHookBase
        : public HookPipelineNodeBase<FreeHookBase<Target, R, Args...>, DecoratorNode<R, Args...>>
    {
    public:
        using Fn = R(*)(Args...);
        using Pipeline = HookPipeline<Fn, R, Args...>;

    protected:
        static R CallOriginal(Args... args)
        {
            return GetPipeline().CallOriginal(std::forward<Args>(args)...);
        }

    public:
        static Pipeline& GetPipeline()
        {
            return GetOrCreateHookPipeline<Pipeline>(
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(&Detour));
        }

    private:
        static R Detour(Args... args)
        {
            return GetPipeline().Dispatch(args...);
        }
    };

    // Member-function bridge for mutable/const member functions.
    template <auto Target, typename MemberFn, typename ThisPtr, typename R, typename... Args>
    class MemberHookBase
        : public HookPipelineNodeBase<
        MemberHookBase<Target, MemberFn, ThisPtr, R, Args...>,
        DecoratorNode<R, ThisPtr, Args...>>
    {
    public:
        using OrigFn = MemberOriginalFn<R, ThisPtr, Args...>;
        using Pipeline = HookPipeline<OrigFn, R, ThisPtr, Args...>;

    protected:
        static R CallOriginal(ThisPtr thiz, Args... args)
        {
            return GetPipeline().CallOriginal(
                std::forward<ThisPtr>(thiz),
                std::forward<Args>(args)...);
        }

    public:
        static Pipeline& GetPipeline()
        {
            return GetOrCreateHookPipeline<Pipeline>(
                MemberPointerToAddress(Target),
                MemberPointerToAddress(Target),
                reinterpret_cast<void*>(&Detour));
        }

    private:
#ifdef _CPPBM_HOOK_WIN32
        static R __fastcall Detour(ThisPtr thiz, void* /*edx*/, Args... args)
        {
            return GetPipeline().Dispatch(thiz, args...);
        }
#else
        static R Detour(ThisPtr thiz, Args... args)
        {
            return GetPipeline().Dispatch(thiz, args...);
        }
#endif
    };

#ifdef _CPPBM_HOOK_WIN32
    template <auto Target, typename R, typename... Args>
    class FreeHookBaseStdcall
        : public HookPipelineNodeBase<FreeHookBaseStdcall<Target, R, Args...>, DecoratorNode<R, Args...>>
    {
    public:
        using Fn = R(__stdcall*)(Args...);
        using Pipeline = HookPipeline<Fn, R, Args...>;

    protected:
        static R CallOriginal(Args... args)
        {
            return GetPipeline().CallOriginal(std::forward<Args>(args)...);
        }

    public:
        static Pipeline& GetPipeline()
        {
            return GetOrCreateHookPipeline<Pipeline>(
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(&Detour));
        }

    private:
        static R __stdcall Detour(Args... args)
        {
            return GetPipeline().Dispatch(args...);
        }
    };

    template <auto Target, typename R, typename... Args>
    class FreeHookBaseFastcall
        : public HookPipelineNodeBase<FreeHookBaseFastcall<Target, R, Args...>, DecoratorNode<R, Args...>>
    {
    public:
        using Fn = R(__fastcall*)(Args...);
        using Pipeline = HookPipeline<Fn, R, Args...>;

    protected:
        static R CallOriginal(Args... args)
        {
            return GetPipeline().CallOriginal(std::forward<Args>(args)...);
        }

    public:
        static Pipeline& GetPipeline()
        {
            return GetOrCreateHookPipeline<Pipeline>(
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(&Detour));
        }

    private:
        static R __fastcall Detour(Args... args)
        {
            return GetPipeline().Dispatch(args...);
        }
    };
#endif // _CPPBM_HOOK_WIN32
}

#endif // __CPPBM_HOOK_HOOK_H__
