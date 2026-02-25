#ifndef __CPPBM_HOOK_HOOK_H__
#define __CPPBM_HOOK_HOOK_H__

#include <mutex>
#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include <type_traits>

#include "error.h"
#include "hooker.h"
#include "../utils/noncopyable.h"
#include "../utils/scope_exit.h"

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
        requires std::is_member_function_pointer_v<std::remove_cvref_t<MemFn>>
    inline void* MemberPointerToAddress(MemFn fn)
    {
        void* addr = nullptr;
        std::memcpy(&addr, &fn, sizeof(addr));
        return addr;
    }

    // Member function pointer
    template <typename R, typename ThisPtr, typename... Args>
    struct MemberOriginalFnT
    {
#ifdef _CPPBM_HOOK_WIN32
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
    // - Installed/Uninstall are protected by an mutex.
    template <typename Owner, typename OrigFn>
    class HookState : private utils::NonCopyable
    {
    public:
        bool InstallAt(void* target, void* detour, Owner* self)
        {
            std::lock_guard<std::mutex> guard{ mtx_ };
            if (installed_.load(std::memory_order_acquire)) // already installed
                return true;

            ClearLastHookError();

            if (!target || !detour || !self)
            {
                return HandleHookFailure(HookError{
                    HookErrorCode::InvalidInstallArgument,
                    target,
                    detour,
                    "Hook install failed: target/detour/self cannot be null."
                    });
            }

            Hooker& hooker = Hooker::GetInstance();

            // create hook
            OrigFn original = nullptr;
            if (!hooker.CreateHook(target, detour, reinterpret_cast<void**>(&original)))
            {
                return HandleHookFailure(HookError{
                    HookErrorCode::CreateHookFailed,
                    target,
                    detour,
                    "Hook install failed: backend CreateHook() returned false."
                    });
            }

            // Enable hook
            if (!hooker.EnableHook(target))
            {
                hooker.RemoveHook(target);
                return HandleHookFailure(HookError{
                    HookErrorCode::EnableHookFailed,
                    target,
                    detour,
                    "Hook install failed: backend EnableHook() returned false."
                    });
            }

            // Store data
            original_.store(original, std::memory_order_release);
            self_.store(self, std::memory_order_release);
            installed_.store(true, std::memory_order_release);
            return true;
        }

        void UninstallAt(void* target)
        {
            std::lock_guard<std::mutex> guard{ mtx_ };
            if (!installed_.load(std::memory_order_acquire))
                return;

            Hooker& hooker = Hooker::GetInstance();

            hooker.DisableHook(target);
            hooker.RemoveHook(target);

            self_.store(nullptr, std::memory_order_release);
            original_.store(nullptr, std::memory_order_release);
            installed_.store(false, std::memory_order_release);
        }

        [[nodiscard]] bool IsInstalled() const
        {
            return installed_.load(std::memory_order_acquire);
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

        std::atomic_bool installed_{ false };
        std::mutex mtx_;
    };

    // Unified parent:
    // - owns Install/Uninstall/IsInstalled
    // - defines BeforeCall/Call/AfterCall extension points
    // - defines CallOriginal and detour dispatch pipeline
    template <typename Derived, typename OrigFn, typename R, typename... CallArgs>
    class HookPipeline : private HookState<Derived, OrigFn>
    {
    public:
        using Core = HookState<Derived, OrigFn>;

        HookPipeline(void* target, void* detour)
            : target_(target), detour_(detour)
        {
            // Will be installed by decorator, or manually install.
            // Install();
        }

        virtual ~HookPipeline()
        {
            Uninstall();
        }

        virtual bool BeforeCall(CallArgs... /*args*/) { return true; }
        virtual R Call(CallArgs... args) { return CallOriginal(args...); }
        virtual void AfterCall() {}

    public:
        [[nodiscard]] bool Install()
        {
            return Core::InstallAt(target_, detour_, static_cast<Derived*>(this));
        }

        void Uninstall()
        {
            return Core::UninstallAt(target_);
        }

        [[nodiscard]] bool IsInstalled() const
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
                    std::invoke(orig, args...);
                return;
            }
            else
            {
                if (!orig)
                    return DefaultReturn();
                return std::invoke(orig, args...);
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

            auto on_exit = utils::ScopeExit([&]
                { self->AfterCall(); });

            if constexpr (std::is_void_v<R>)
            {
                self->Call(args...); // if Call throw exceptions, AfterCall will still be invoked
                return;
            }
            else
            {
                return self->Call(args...);
            }
        }

    private:
        template <typename Q = R>
            requires(!std::is_void_v<Q>)
        static Q DefaultReturn()
        {
            if constexpr (std::is_reference_v<Q>)
            {
                assert(false && "HookPipeline DefaultReturn failed: reference return has no default value.");
                std::terminate();
            }
            else if constexpr (std::is_default_constructible_v<Q>)
            {
                return Q{};
            }
            else
            {
                assert(false && "HookPipeline DefaultReturn failed: return type is not default-constructible.");
                std::terminate();
            }
        }

        void* target_ = nullptr;
        void* detour_ = nullptr;
    };

    // Shared implementation for free-function hooks
    template <typename Derived, typename R, typename... Args>
    class FreeHookBase : public HookPipeline<Derived, R(*)(Args...), R, Args...>
    {
    public:
        using Base = HookPipeline<Derived, R(*)(Args...), R, Args...>;
        using Fn = R(*)(Args...);

        explicit FreeHookBase(Fn target) : Base(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&Detour)) {}

    private:
        static R Detour(Args... args)
        {
            return Base::Dispatch(args...);
        }
    };

    template <typename Derived, typename MemberFn, typename ThisPtr, typename R, typename... Args>
    class MemberHookBase : public HookPipeline<Derived, MemberOriginalFn<R, ThisPtr, Args...>, R, ThisPtr, Args...>
    {
    public:
        using OrigFn = MemberOriginalFn<R, ThisPtr, Args...>;
        using Base = HookPipeline<Derived, OrigFn, R, ThisPtr, Args...>;

        explicit MemberHookBase(MemberFn target)
            : Base(MemberPointerToAddress(target), reinterpret_cast<void*>(&Detour))
        {
        }

    private:
#ifdef _CPPBM_HOOK_WIN32
        // x86 thiscall bridge:
        // - ECX carries "this" (thiz)
        // - EDX is an unused placeholder for fastcall compatibility.
        static R __fastcall Detour(ThisPtr thiz, void* /*edx*/, Args... args)
        {
            return Base::Dispatch(thiz, args...);
        }
#else
        // Non-x86 path uses explicit "thiz" parameter directly.
        static R Detour(ThisPtr thiz, Args... args)
        {
            return Base::Dispatch(thiz, args...);
        }
#endif
    };

    // __stdcall and fastcall for msvc x86
#ifdef _CPPBM_HOOK_WIN32

    // stdcall
    template <typename Derived, typename R, typename... Args>
    class FreeHookBaseStdcall : public HookPipeline<Derived, R(__stdcall*)(Args...), R, Args...>
    {
    public:
        using Base = HookPipeline<Derived, R(__stdcall*)(Args...), R, Args...>;
        using Fn = R(__stdcall*)(Args...);

        explicit FreeHookBaseStdcall(Fn target)
            : Base(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&Detour))
        {
        }

    private:
        static R __stdcall Detour(Args... args)
        {
            return Base::Dispatch(args...);
        }
    };

    // fastcall
    template <typename Derived, typename R, typename... Args>
    class FreeHookBaseFastcall : public HookPipeline<Derived, R(__fastcall*)(Args...), R, Args...>
    {
    public:
        using Base = HookPipeline<Derived, R(__fastcall*)(Args...), R, Args...>;
        using Fn = R(__fastcall*)(Args...);

        explicit FreeHookBaseFastcall(Fn target)
            : Base(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&Detour))
        {
        }

    private:
        static R __fastcall Detour(Args... args)
        {
            return Base::Dispatch(args...);
        }
    };

#endif // _CPPBM_HOOK_WIN32

}

#endif // __CPPBM_HOOK_HOOK_H__
