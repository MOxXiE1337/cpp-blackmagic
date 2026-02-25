// File role:
// Centralized error model and failure policy for dependency injection.
//
// This layer provides:
// - structured error payload (InjectError)
// - configurable failure policy (terminate / throw / callback)
// - unified fail-fast entry (FailInject)

#ifndef __CPPBM_DEPENDS_ERROR_H__
#define __CPPBM_DEPENDS_ERROR_H__

#include <atomic>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <typeindex>
#include <utility>

namespace cpp::blackmagic::depends
{
    enum class InjectErrorCode
    {
        MissingDependency,
        TypeMismatch,
        FactoryMismatch,
        InvalidPlaceholder,
        InternalInvariantBreak,
    };

    struct InjectError
    {
        InjectErrorCode code = InjectErrorCode::MissingDependency;
        const void* target_key = nullptr;
        std::size_t param_index = static_cast<std::size_t>(-1);
        std::type_index requested_type = typeid(void);
        const void* factory_key = nullptr;
        const char* message = "dependency injection failure";
    };

    enum class InjectFailPolicy
    {
        Terminate,
        Throw,
        Callback,
    };

    using InjectErrorCallback = void(*)(const InjectError&);

    class InjectException : public std::runtime_error
    {
    public:
        explicit InjectException(InjectError error)
            : std::runtime_error(error.message ? error.message : "dependency injection failure"),
            error_(std::move(error))
        {
        }

        const InjectError& Error() const noexcept
        {
            return error_;
        }

    private:
        InjectError error_{};
    };

    inline std::atomic<InjectFailPolicy>& InjectFailPolicyStorage()
    {
        static std::atomic<InjectFailPolicy> policy{ InjectFailPolicy::Terminate };
        return policy;
    }

    inline std::atomic<InjectErrorCallback>& InjectErrorCallbackStorage()
    {
        static std::atomic<InjectErrorCallback> callback{ nullptr };
        return callback;
    }

    inline void SetInjectFailPolicy(InjectFailPolicy policy)
    {
        InjectFailPolicyStorage().store(policy, std::memory_order_release);
    }

    inline InjectFailPolicy GetInjectFailPolicy()
    {
        return InjectFailPolicyStorage().load(std::memory_order_acquire);
    }

    inline void SetInjectErrorCallback(InjectErrorCallback callback)
    {
        InjectErrorCallbackStorage().store(callback, std::memory_order_release);
    }

    inline InjectErrorCallback GetInjectErrorCallback()
    {
        return InjectErrorCallbackStorage().load(std::memory_order_acquire);
    }

    [[noreturn]] inline void HandleInjectFailure(InjectError error)
    {
        if (auto cb = GetInjectErrorCallback(); cb != nullptr)
        {
            cb(error);
        }

        switch (GetInjectFailPolicy())
        {
        case InjectFailPolicy::Throw:
            throw InjectException(std::move(error));
        case InjectFailPolicy::Callback:
            std::terminate();
        case InjectFailPolicy::Terminate:
        default:
            std::terminate();
        }
    }

    template <typename R>
    [[noreturn]] R FailInject(InjectError error)
    {
        HandleInjectFailure(std::move(error));
    }
}

#endif // __CPPBM_DEPENDS_ERROR_H__
