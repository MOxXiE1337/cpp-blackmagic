#ifndef __CPPBM_HOOK_STATE_H__
#define __CPPBM_HOOK_STATE_H__

#include <atomic>
#include <mutex>

#include "error.h"
#include "hooker.h"
#include "../utils/noncopyable.h"

namespace cpp::blackmagic::hook
{
    // HookState owns backend hook installation state for one target function.
    //
    // Responsibilities:
    // 1) Ensure CreateHook/EnableHook runs once.
    // 2) Store original trampoline pointer.
    // 3) Publish install status atomically.
    //
    // Not handled here:
    // - decorator chain ordering
    // - argument rewriting / dispatch logic
    template <typename OrigFn>
    class HookState : private utils::NonCopyable
    {
    public:
        bool InstallAt(void* target, void* detour)
        {
            std::lock_guard<std::mutex> guard{ mtx_ };
            if (installed_.load(std::memory_order_acquire))
            {
                return true;
            }

            ClearLastHookError();
            if (target == nullptr || detour == nullptr)
            {
                return HandleHookFailure(HookError{
                    HookErrorCode::InvalidInstallArgument,
                    target,
                    detour,
                    "Hook install failed: target/detour cannot be null."
                    });
            }

            Hooker& hooker = Hooker::GetInstance();
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

            original_.store(original, std::memory_order_release);
            installed_.store(true, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool IsInstalled() const
        {
            return installed_.load(std::memory_order_acquire);
        }

    protected:
        OrigFn Original() const
        {
            return original_.load(std::memory_order_acquire);
        }

    private:
        std::atomic<OrigFn> original_{ nullptr };
        std::atomic_bool installed_{ false };
        mutable std::mutex mtx_{};
    };
}

#endif // __CPPBM_HOOK_STATE_H__
