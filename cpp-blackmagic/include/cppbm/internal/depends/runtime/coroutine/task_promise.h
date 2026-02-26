// File role:
// Promise-layer definitions for Task<T> coroutine types.

#ifndef __CPPBM_INTERNAL_DEPENDS_COROUTINE_TASK_PROMISE_H__
#define __CPPBM_INTERNAL_DEPENDS_COROUTINE_TASK_PROMISE_H__

#include <cassert>
#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include "task_forward.h"
#include "scheduler.h"

namespace cpp::blackmagic::depends
{
    struct TaskFinalAwaiter
    {
        bool await_ready() const noexcept
        {
            return false;
        }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) const noexcept
        {
            auto& promise = handle.promise();
            // Child task completion: wake parent continuation by queueing it
            // with the parent's captured DI state.
            if (auto continuation = promise.TakeContinuation())
            {
                auto continuation_state = promise.TakeContinuationState();
                auto current_state = CurrentInjectStateOwner();

                // Continuation fast-path:
                // when continuation state is already the current active state,
                // resume parent directly and skip one queue round-trip.
                // If state differs, keep queue-based handoff to preserve context binding.
                const bool same_state =
                    continuation_state.get() == nullptr
                    || continuation_state.get() == current_state.get();
                if (same_state)
                {
                    return continuation;
                }

                CurrentTaskScheduler().Enqueue(continuation, std::move(continuation_state));
            }
            return std::noop_coroutine();
        }

        void await_resume() const noexcept {}
    };

    class TaskPromiseBase
    {
    public:
        std::suspend_always initial_suspend() const noexcept
        {
            return {};
        }

        TaskFinalAwaiter final_suspend() const noexcept
        {
            return {};
        }

        void unhandled_exception() noexcept
        {
            exception_ = std::current_exception();
        }

        void SetContinuation(
            std::coroutine_handle<> continuation,
            std::shared_ptr<InjectContextState> continuation_state)
        {
            continuation_ = continuation;
            continuation_state_ = std::move(continuation_state);
        }

        std::coroutine_handle<> TakeContinuation() noexcept
        {
            auto out = continuation_;
            continuation_ = {};
            return out;
        }

        std::shared_ptr<InjectContextState> TakeContinuationState() noexcept
        {
            return std::exchange(continuation_state_, {});
        }

        void SetInjectContext(InjectContextLeaseHandle lease)
        {
            inject_context_ = std::move(lease);
        }

        void BindInjectContext(InjectContextLeaseHandle lease)
        {
            inject_context_ = std::move(lease);
        }

        const InjectContextLeaseHandle& InjectContext() const noexcept
        {
            return inject_context_;
        }

        std::shared_ptr<InjectContextState> InjectStateOwner() const
        {
            return InjectStateFromLease(inject_context_);
        }

        void RethrowIfFailed() const
        {
            if (exception_)
            {
                std::rethrow_exception(exception_);
            }
        }

    private:
        std::exception_ptr exception_{};
        std::coroutine_handle<> continuation_{};
        std::shared_ptr<InjectContextState> continuation_state_{};
        InjectContextLeaseHandle inject_context_{};
    };

    template <typename T>
    class TaskPromise : public TaskPromiseBase
    {
    public:
        Task<T> get_return_object();

        template <typename U>
            requires std::constructible_from<T, U&&>
        void return_value(U&& value)
        {
            value_.emplace(std::forward<U>(value));
        }

        T TakeValue()
        {
            this->RethrowIfFailed();
            assert(value_.has_value() && "Task<T>: result is missing.");
            return std::move(*value_);
        }

    private:
        std::optional<T> value_{};
    };

    template <typename T>
    class TaskPromise<T&> : public TaskPromiseBase
    {
    public:
        Task<T&> get_return_object();

        template <typename U>
            requires std::convertible_to<U&&, T&>
        void return_value(U&& value)
        {
            // Reference result is represented as stable address in promise storage.
            value_ = std::addressof(static_cast<T&>(value));
        }

        T& TakeValue()
        {
            this->RethrowIfFailed();
            assert(value_ != nullptr && "Task<T&>: result is missing.");
            return *value_;
        }

    private:
        T* value_ = nullptr;
    };

    template <>
    class TaskPromise<void> : public TaskPromiseBase
    {
    public:
        Task<void> get_return_object();

        void return_void() noexcept {}

        void EnsureCompleted()
        {
            this->RethrowIfFailed();
        }
    };
}

#endif // __CPPBM_INTERNAL_DEPENDS_COROUTINE_TASK_PROMISE_H__
