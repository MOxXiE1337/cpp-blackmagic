#ifndef __CPPBM_HOOK_ERROR_H__
#define __CPPBM_HOOK_ERROR_H__

#include <atomic>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cpp::blackmagic::hook
{
    enum class HookErrorCode
    {
        InvalidInstallArgument,
        CreateHookFailed,
        EnableHookFailed,
    };

    struct HookError
    {
        HookErrorCode code = HookErrorCode::CreateHookFailed;
        const void* target = nullptr;
        const void* detour = nullptr;
        const char* message = "hook failure";
    };

    enum class HookFailPolicy
    {
        Ignore,
        Throw,
        Callback,
        Terminate,
    };

    using HookErrorCallback = void(*)(const HookError&);

    class HookException : public std::runtime_error
    {
    public:
        explicit HookException(HookError error)
            : std::runtime_error(error.message ? error.message : "hook failure"),
            error_(std::move(error))
        {
        }

        const HookError& Error() const noexcept
        {
            return error_;
        }

    private:
        HookError error_{};
    };

    inline std::atomic<HookFailPolicy>& HookFailPolicyStorage()
    {
        // Default keeps previous behavior: Install() reports failure via return false.
        static std::atomic<HookFailPolicy> policy{ HookFailPolicy::Ignore };
        return policy;
    }

    inline std::atomic<HookErrorCallback>& HookErrorCallbackStorage()
    {
        static std::atomic<HookErrorCallback> callback{ nullptr };
        return callback;
    }

    inline std::optional<HookError>& LastHookErrorStorage()
    {
        static thread_local std::optional<HookError> last_error = std::nullopt;
        return last_error;
    }

    inline void SetHookFailPolicy(HookFailPolicy policy)
    {
        HookFailPolicyStorage().store(policy, std::memory_order_release);
    }

    inline HookFailPolicy GetHookFailPolicy()
    {
        return HookFailPolicyStorage().load(std::memory_order_acquire);
    }

    inline void SetHookErrorCallback(HookErrorCallback callback)
    {
        HookErrorCallbackStorage().store(callback, std::memory_order_release);
    }

    inline HookErrorCallback GetHookErrorCallback()
    {
        return HookErrorCallbackStorage().load(std::memory_order_acquire);
    }

    inline std::optional<HookError> GetLastHookError()
    {
        return LastHookErrorStorage();
    }

    inline void ClearLastHookError()
    {
        LastHookErrorStorage().reset();
    }

    inline bool HandleHookFailure(HookError error)
    {
        LastHookErrorStorage() = error;

        if (auto cb = GetHookErrorCallback(); cb != nullptr)
        {
            cb(error);
        }

        switch (GetHookFailPolicy())
        {
        case HookFailPolicy::Throw:
            throw HookException(std::move(error));
        case HookFailPolicy::Terminate:
            std::terminate();
        case HookFailPolicy::Callback:
        case HookFailPolicy::Ignore:
        default:
            return false;
        }
    }
}

#endif // __CPPBM_HOOK_ERROR_H__
