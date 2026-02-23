#include <mutex>
#include <atomic>
#include <cstring>
#include <utility>
#include <type_traits>

#include "hooker.h"
#include "noncopyable.h"

#ifndef __CPPBM_HOOK_H__
#define __CPPBM_HOOK_H__

#ifdef _WIN32
#ifndef _WIN64
#define _CPPBM_HOOK_WIN32
#endif // _WIN32
#endif // _WIN64

namespace cpp::blackmagic::hook
{
    // Convert a member-function pointer into a code address for MinHook.
    //
    // This requires the member-function pointer representation to fit into one
    // fits in one machine pointer (typically single inheritance).
    template <typename MemFn>
    inline void* MemberPointerToAddress(MemFn fn)
    {
        static_assert(std::is_member_function_pointer_v<MemFn>,
            "MemFn must be a member-function pointer.");

        void* addr = nullptr;
        std::memcpy(&addr, &fn, sizeof(addr));
        return addr;
    }

    // Member function pointer
    template <typename R, typename ThisPtr, typename... Args>
    struct MemberOriginalFnT {
#ifdef HOOK_WIN32
        // x86 member functions use __thiscall semantics for original trampoline.
        using type = R(__thiscall*)(ThisPtr, Args...);
#else
        using type = R(*)(ThisPtr, Args...);
#endif
    };

    template <typename R, typename ThisPtr, typename... Args>
    using MemberOriginalFn = typename MemberOriginalFnT<R, ThisPtr, Args...>::type;

    // Per-hook shared state:
    // - self_ points to the current active decorator instance.
    // - original_ stores the trampoline returned by MinHook.
    // - install/uninstall are protected by an instance mutex.
    template <typename Owner, typename OrigFn>
    class HookState : private NonCopyable
    {
    public:
        bool InstallAt(void* target, void* detour, Owner* self)
        {
            std::lock_guard<std::mutex> guard{ mtx_ };
            if (installed_)
                return true;
            if (!target || !detour || !self) return false;

            Hooker* hooker = Hooker::GetInstance();
            if (!hooker)
            {
                return false;
            }

            // create hook
            OrigFn original = nullptr;
            if (!hooker->CreateHook(
                target,
                detour,
                reinterpret_cast<void**>(&original)
            ))
            {
                return false;
            }

            // Enable hook
            if (!hooker->EnableHook(target))
            {
                hooker->RemoveHook(target);
                return false;
            }

            // Store data
            original_.store(original, std::memory_order_release);
            self_.store(self, std::memory_order_release);
            installed_ = true;
            return true;
        }

        void UninstallAt(void* target)
        {
            std::lock_guard<std::mutex> guard{ mtx_ };
            if (!installed_)
                return;

            Hooker* hooker = Hooker::GetInstance();
            if (hooker)
            {
                hooker->DisableHook(target);
                hooker->RemoveHook(target);
            }

            self_.store(nullptr, std::memory_order_release);
            original_.store(nullptr, std::memory_order_release);
            installed_ = false;
        }

        bool IsInstalled() const
        {
            return installed_;
        }

    protected:
        static Owner* Self()
        {
            return self_.load(std::memory_order_acquire);
        }

        static OrigFn Original()
        {
            return original_.load(std::memory_order_acquire);
        }

    private:
        inline static std::atomic<Owner*> self_{ nullptr };
        inline static std::atomic<OrigFn> original_{ nullptr };

        bool installed_ = false;
        std::mutex mtx_;
    };

    // Unified parent:
    // - owns install/uninstall/is_installed
    // - defines BeforeCall/Call/AfterCall extension points
    // - defines CallOriginal and detour dispatch pipeline
    template <typename Derived, typename OrigFn, typename R, typename... CallArgs>
    class HookRuntime : private HookState<Derived, OrigFn>
    {
    public:
        using Core = HookState<Derived, OrigFn>;

        HookRuntime(void* target, void* detour)
            : target_(target), detour_(detour)
        {
            (void)Install();
        }

        virtual ~HookRuntime()
        {
            Uninstall();
        }

        virtual bool BeforeCall(CallArgs... /*args*/) { return true; }
        virtual R Call(CallArgs... args) { return CallOriginal(args...); }
        virtual void AfterCall() {}

    public:
        bool Install()
        {
            return Core::InstallAt(target_, detour_, static_cast<Derived*>(this));
        }

        void Uninstall()
        {
            return Core::UninstallAt(target_);
        }

        bool IsInstalled() const
        {
            return Core::IsInstalled();
        }

    protected:
        static OrigFn Original() { return Core::Original(); }

        static R CallOriginal(CallArgs... args)
        {
            OrigFn orig = Core::Original();
            if constexpr (std::is_void_v<R>)
            {
                if (orig)
                    return orig(args...);
                return;
            }
            else
            {
                if (!orig) return DefaultReturn();
                return orig(args...);
            }
        }

        static R Dispatch(CallArgs... args)
        {
            auto* self = Core::Self();
            if (!self)
            {
                return CallOriginal(args...);
            }

            if (!self->BeforeCall(args...))
            {
                if constexpr (std::is_void_v<R>)
                {
                    return;
                }
                else
                {
                    return DefaultReturn();
                }
            }

            if constexpr (std::is_void_v<R>)
            {
                self->Call(args...);
                self->AfterCall();
                return;
            }
            else
            {
                R ret = self->Call(args...);
                self->AfterCall();
                return ret;
            }
        }


    private:
        template <typename Q = R, std::enable_if_t<!std::is_void_v<Q>, int> = 0>
        static Q DefaultReturn() {
            static_assert(std::is_default_constructible_v<Q>,
                "R must be default-constructible when BeforeCall returns false "
                "or original function is unavailable.");
            return Q{};
        }

        void* target_ = nullptr;
        void* detour_ = nullptr;
    };

    // Shared implementation for free-function hooks
    template <typename Derived, typename R, typename... Args>
    class FreeHookBase : public HookRuntime<Derived, R(*)(Args...), R, Args...>
    {
    public:
        using Base = HookRuntime<Derived, R(*)(Args...), R, Args...>;
        using Fn = R(*)(Args...);

        explicit FreeHookBase(Fn target) : Base(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&Detour)) {}

    private:
        static R Detour(Args... args)
        {
            return Base::Dispatch(args...);
        }
    };

    template <typename Derived, typename MemberFn, typename ThisPtr, typename R, typename... Args>
    class MemberHookBase : public HookRuntime<Derived, MemberOriginalFn<R, ThisPtr, Args...>, R, ThisPtr, Args...> {
    public:
        using OrigFn = MemberOriginalFn<R, ThisPtr, Args...>;
        using Base = HookRuntime<Derived, OrigFn, R, ThisPtr, Args...>;

        explicit MemberHookBase(MemberFn target)
            : Base(MemberPointerToAddress(target), reinterpret_cast<void*>(&Detour)) {
        }

    private:

#ifdef _CPPBM_HOOK_WIN32
        // x86 thiscall bridge:
        // - ECX carries "this" (thiz)
        // - EDX is an unused placeholder for fastcall compatibility.
        static R __fastcall Detour(ThisPtr thiz, void* /*edx*/, Args... args) {
            return Base::Dispatch(thiz, args...);
        }
#else
        // Non-x86 path uses explicit "thiz" parameter directly.
        static R Detour(ThisPtr thiz, Args... args) {
            return Base::Dispatch(thiz, args...);
        }
#endif
    };



    // __stdcall and fastcall for msvc x86
#ifdef _CPPBM_HOOK_WIN32

        // stdcall
    template <typename Derived, typename R, typename... Args>
    class FreeHookBaseStdcall
        : public HookRuntime<Derived, R(__stdcall*)(Args...), R, Args...> {
    public:
        using Base = HookRuntime<Derived, R(__stdcall*)(Args...), R, Args...>;
        using Fn = R(__stdcall*)(Args...);

        explicit FreeHookBaseStdcall(Fn target)
            : Base(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&Detour)) {
        }

    private:
        static R __stdcall Detour(Args... args) {
            return Base::Dispatch(args...);
        }
    };

    // fastcall
    template <typename Derived, typename R, typename... Args>
    class FreeHookBaseFastcall
        : public HookRuntime<Derived, R(__fastcall*)(Args...), R, Args...> {
    public:
        using Base = HookRuntime<Derived, R(__fastcall*)(Args...), R, Args...>;
        using Fn = R(__fastcall*)(Args...);

        explicit FreeHookBaseFastcall(Fn target)
            : Base(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&Detour)) {
        }

    private:
        static R __fastcall Detour(Args... args) {
            return Base::Dispatch(args...);
        }
    };

#endif // _CPPBM_HOOK_WIN32

}


#endif // __CPPBM_HOOK_H__
