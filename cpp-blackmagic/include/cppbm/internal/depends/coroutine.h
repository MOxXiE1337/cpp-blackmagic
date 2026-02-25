// File role:
// Scheduler-backed coroutine primitives for Depends/@inject integration.
//
// User-facing aliases are exported in namespace cpp::blackmagic:
// - cpp::blackmagic::Task<T = void>

#ifndef __CPPBM_INTERNAL_DEPENDS_COROUTINE_H__
#define __CPPBM_INTERNAL_DEPENDS_COROUTINE_H__

#include <cassert>
#include <concepts>
#include <coroutine>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "context.h"

namespace cpp::blackmagic::depends
{
    template <typename T = void>
    class Task;

    class TaskScheduler
    {
    public:
        struct Step
        {
            std::coroutine_handle<> handle{};
            std::shared_ptr<InjectContextState> state{};
        };

        void Enqueue(std::coroutine_handle<> handle, std::shared_ptr<InjectContextState> state)
        {
            if (!handle || handle.done())
            {
                return;
            }
            queue_.push_back(Step{ handle, std::move(state) });
        }

        bool RunOne()
        {
            if (queue_.empty())
            {
                return false;
            }

            Step step = std::move(queue_.front());
            queue_.pop_front();

            if (!step.handle || step.handle.done())
            {
                return true;
            }

            // Resume one coroutine frame under the DI state captured when it was queued.
            // This is the core "contextvars-like" handoff point between await/resume edges.
            ActiveInjectStateScope guard{ step.state ? std::move(step.state) : GetActiveStateOwner() };
            step.handle.resume();
            return true;
        }

        void RunUntilIdle()
        {
            while (RunOne())
            {
            }
        }

    private:
        std::deque<Step> queue_{};
    };

    inline TaskScheduler& CurrentTaskScheduler()
    {
        static thread_local TaskScheduler scheduler{};
        return scheduler;
    }

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
                CurrentTaskScheduler().Enqueue(continuation, promise.TakeContinuationState());
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

    inline bool RunTaskSchedulerOnce()
    {
        return CurrentTaskScheduler().RunOne();
    }

    inline void RunTaskSchedulerUntilIdle()
    {
        CurrentTaskScheduler().RunUntilIdle();
    }

    template <typename T>
    class Task
    {
    public:
        using promise_type = TaskPromise<T>;
        using Handle = std::coroutine_handle<promise_type>;

        Task() = default;

        explicit Task(Handle handle) noexcept
            : handle_(handle)
        {
        }

        ~Task()
        {
            if (handle_)
            {
                handle_.destroy();
            }
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& rhs) noexcept
            : handle_(std::exchange(rhs.handle_, {}))
        {
        }

        Task& operator=(Task&& rhs) noexcept
        {
            if (this != &rhs)
            {
                if (handle_)
                {
                    handle_.destroy();
                }
                handle_ = std::exchange(rhs.handle_, {});
            }
            return *this;
        }

        bool Done() const noexcept
        {
            return !handle_ || handle_.done();
        }

        explicit operator bool() const noexcept
        {
            return handle_ != nullptr;
        }

        void Resume()
        {
            if (!handle_ || handle_.done())
            {
                return;
            }
            auto guard = ActivateInjectStateFromLease(handle_.promise().InjectContext());
            handle_.resume();
        }

        void Schedule()
        {
            if (!handle_ || handle_.done())
            {
                return;
            }
            CurrentTaskScheduler().Enqueue(
                handle_,
                handle_.promise().InjectStateOwner());
        }

        T Get()
        {
            Schedule();
            while (!Done())
            {
                if (!RunTaskSchedulerOnce())
                {
                    throw std::runtime_error("Task::Get deadlock: scheduler queue drained before completion.");
                }
            }
            return handle_.promise().TakeValue();
        }

        void SetInjectContext(InjectContextLeaseHandle lease)
        {
            if (handle_)
            {
                handle_.promise().SetInjectContext(std::move(lease));
            }
        }

        void BindInjectContext(InjectContextLeaseHandle lease)
        {
            SetInjectContext(std::move(lease));
        }

        struct Awaiter
        {
            Handle handle{};
            bool owns_handle = false;

            Awaiter(Handle h, bool own) noexcept
                : handle(h), owns_handle(own)
            {
            }

            ~Awaiter()
            {
                // Rvalue co_await path transfers handle ownership into awaiter.
                // If await_resume was skipped by exception/cancellation paths,
                // ensure coroutine frame is still released here.
                if (owns_handle && handle)
                {
                    handle.destroy();
                }
            }

            Awaiter(const Awaiter&) = delete;
            Awaiter& operator=(const Awaiter&) = delete;
            Awaiter(Awaiter&& rhs) noexcept
                : handle(std::exchange(rhs.handle, {})),
                owns_handle(rhs.owns_handle)
            {
                rhs.owns_handle = false;
            }
            Awaiter& operator=(Awaiter&&) = delete;

            bool await_ready() const noexcept
            {
                return !handle || handle.done();
            }

            bool await_suspend(std::coroutine_handle<> continuation)
            {
                if (!handle || handle.done())
                {
                    return false;
                }

                // Save parent continuation and parent DI context, then schedule child.
                // Parent resumes in final_suspend of the child.
                handle.promise().SetContinuation(
                    continuation,
                    CurrentInjectStateOwner());
                CurrentTaskScheduler().Enqueue(
                    handle,
                    handle.promise().InjectStateOwner());
                return true;
            }

            T await_resume()
            {
                assert(handle && "Task::Awaiter requires non-null handle.");
                T out = handle.promise().TakeValue();
                if (owns_handle)
                {
                    // Rvalue co_await path: awaiter owns frame lifetime.
                    handle.destroy();
                    handle = {};
                }
                return out;
            }
        };

        auto operator co_await() & noexcept
        {
            // Lvalue task keeps owning the coroutine handle.
            return Awaiter{ handle_, false };
        }

        auto operator co_await() && noexcept
        {
            // Rvalue task transfers handle ownership to awaiter so temporary
            // Task destruction cannot invalidate scheduled coroutine handles.
            return Awaiter{ std::exchange(handle_, {}), true };
        }

    private:
        Handle handle_{};
    };

    template <typename T>
    class Task<T&>
    {
    public:
        using promise_type = TaskPromise<T&>;
        using Handle = std::coroutine_handle<promise_type>;

        Task() = default;

        explicit Task(Handle handle) noexcept
            : handle_(handle)
        {
        }

        ~Task()
        {
            if (handle_)
            {
                handle_.destroy();
            }
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& rhs) noexcept
            : handle_(std::exchange(rhs.handle_, {}))
        {
        }

        Task& operator=(Task&& rhs) noexcept
        {
            if (this != &rhs)
            {
                if (handle_)
                {
                    handle_.destroy();
                }
                handle_ = std::exchange(rhs.handle_, {});
            }
            return *this;
        }

        bool Done() const noexcept
        {
            return !handle_ || handle_.done();
        }

        explicit operator bool() const noexcept
        {
            return handle_ != nullptr;
        }

        void Resume()
        {
            if (!handle_ || handle_.done())
            {
                return;
            }
            auto guard = ActivateInjectStateFromLease(handle_.promise().InjectContext());
            handle_.resume();
        }

        void Schedule()
        {
            if (!handle_ || handle_.done())
            {
                return;
            }
            CurrentTaskScheduler().Enqueue(
                handle_,
                handle_.promise().InjectStateOwner());
        }

        T& Get()
        {
            // Same scheduler-driven completion path as Task<T>,
            // but returns borrowed reference captured by promise.
            Schedule();
            while (!Done())
            {
                if (!RunTaskSchedulerOnce())
                {
                    throw std::runtime_error("Task<T&>::Get deadlock: scheduler queue drained before completion.");
                }
            }
            return handle_.promise().TakeValue();
        }

        void SetInjectContext(InjectContextLeaseHandle lease)
        {
            if (handle_)
            {
                handle_.promise().SetInjectContext(std::move(lease));
            }
        }

        void BindInjectContext(InjectContextLeaseHandle lease)
        {
            SetInjectContext(std::move(lease));
        }

        struct Awaiter
        {
            Handle handle{};
            bool owns_handle = false;

            Awaiter(Handle h, bool own) noexcept
                : handle(h), owns_handle(own)
            {
            }

            ~Awaiter()
            {
                if (owns_handle && handle)
                {
                    handle.destroy();
                }
            }

            Awaiter(const Awaiter&) = delete;
            Awaiter& operator=(const Awaiter&) = delete;
            Awaiter(Awaiter&& rhs) noexcept
                : handle(std::exchange(rhs.handle, {})),
                owns_handle(rhs.owns_handle)
            {
                rhs.owns_handle = false;
            }
            Awaiter& operator=(Awaiter&&) = delete;

            bool await_ready() const noexcept
            {
                return !handle || handle.done();
            }

            bool await_suspend(std::coroutine_handle<> continuation)
            {
                if (!handle || handle.done())
                {
                    return false;
                }

                // Save parent continuation and parent DI context, then schedule child.
                // Parent resumes in final_suspend of the child.
                handle.promise().SetContinuation(
                    continuation,
                    CurrentInjectStateOwner());
                CurrentTaskScheduler().Enqueue(
                    handle,
                    handle.promise().InjectStateOwner());
                return true;
            }

            T& await_resume()
            {
                assert(handle && "Task<T&>::Awaiter requires non-null handle.");
                T& out = handle.promise().TakeValue();
                if (owns_handle)
                {
                    handle.destroy();
                    handle = {};
                }
                return out;
            }
        };

        auto operator co_await() & noexcept
        {
            return Awaiter{ handle_, false };
        }

        auto operator co_await() && noexcept
        {
            return Awaiter{ std::exchange(handle_, {}), true };
        }

    private:
        Handle handle_{};
    };

    template <>
    class Task<void>
    {
    public:
        using promise_type = TaskPromise<void>;
        using Handle = std::coroutine_handle<promise_type>;

        Task() = default;

        explicit Task(Handle handle) noexcept
            : handle_(handle)
        {
        }

        ~Task()
        {
            if (handle_)
            {
                handle_.destroy();
            }
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& rhs) noexcept
            : handle_(std::exchange(rhs.handle_, {}))
        {
        }

        Task& operator=(Task&& rhs) noexcept
        {
            if (this != &rhs)
            {
                if (handle_)
                {
                    handle_.destroy();
                }
                handle_ = std::exchange(rhs.handle_, {});
            }
            return *this;
        }

        bool Done() const noexcept
        {
            return !handle_ || handle_.done();
        }

        explicit operator bool() const noexcept
        {
            return handle_ != nullptr;
        }

        void Resume()
        {
            if (!handle_ || handle_.done())
            {
                return;
            }
            auto guard = ActivateInjectStateFromLease(handle_.promise().InjectContext());
            handle_.resume();
        }

        void Schedule()
        {
            if (!handle_ || handle_.done())
            {
                return;
            }
            CurrentTaskScheduler().Enqueue(
                handle_,
                handle_.promise().InjectStateOwner());
        }

        void Get()
        {
            // Synchronous bridge for async task:
            // pump the per-thread scheduler until this task reaches done().
            Schedule();
            while (!Done())
            {
                if (!RunTaskSchedulerOnce())
                {
                    throw std::runtime_error("Task<void>::Get deadlock: scheduler queue drained before completion.");
                }
            }
            handle_.promise().EnsureCompleted();
        }

        void SetInjectContext(InjectContextLeaseHandle lease)
        {
            if (handle_)
            {
                handle_.promise().SetInjectContext(std::move(lease));
            }
        }

        void BindInjectContext(InjectContextLeaseHandle lease)
        {
            SetInjectContext(std::move(lease));
        }

        struct Awaiter
        {
            Handle handle{};
            bool owns_handle = false;

            Awaiter(Handle h, bool own) noexcept
                : handle(h), owns_handle(own)
            {
            }

            ~Awaiter()
            {
                if (owns_handle && handle)
                {
                    handle.destroy();
                }
            }

            Awaiter(const Awaiter&) = delete;
            Awaiter& operator=(const Awaiter&) = delete;
            Awaiter(Awaiter&& rhs) noexcept
                : handle(std::exchange(rhs.handle, {})),
                owns_handle(rhs.owns_handle)
            {
                rhs.owns_handle = false;
            }
            Awaiter& operator=(Awaiter&&) = delete;

            bool await_ready() const noexcept
            {
                return !handle || handle.done();
            }

            bool await_suspend(std::coroutine_handle<> continuation)
            {
                if (!handle || handle.done())
                {
                    return false;
                }

                // Save parent continuation and parent DI context, then schedule child.
                // Parent resumes in final_suspend of the child.
                handle.promise().SetContinuation(
                    continuation,
                    CurrentInjectStateOwner());
                CurrentTaskScheduler().Enqueue(
                    handle,
                    handle.promise().InjectStateOwner());
                return true;
            }

            void await_resume()
            {
                assert(handle && "Task<void>::Awaiter requires non-null handle.");
                handle.promise().EnsureCompleted();
                if (owns_handle)
                {
                    handle.destroy();
                    handle = {};
                }
            }
        };

        auto operator co_await() & noexcept
        {
            return Awaiter{ handle_, false };
        }

        auto operator co_await() && noexcept
        {
            return Awaiter{ std::exchange(handle_, {}), true };
        }

    private:
        Handle handle_{};
    };

    template <typename T>
    Task<T> TaskPromise<T>::get_return_object()
    {
        using Handle = std::coroutine_handle<TaskPromise<T>>;
        return Task<T>{ Handle::from_promise(*this) };
    }

    template <typename T>
    Task<T&> TaskPromise<T&>::get_return_object()
    {
        using Handle = std::coroutine_handle<TaskPromise<T&>>;
        return Task<T&>{ Handle::from_promise(*this) };
    }

    inline Task<void> TaskPromise<void>::get_return_object()
    {
        using Handle = std::coroutine_handle<TaskPromise<void>>;
        return Task<void>{ Handle::from_promise(*this) };
    }
}

namespace cpp::blackmagic
{
    // User-facing task template:
    // - Task<>      == Task<void>
    // - Task<int>   returns int
    //
    // C++ language note:
    // - In type positions, class/alias templates still require template-id syntax.
    //   Use Task<> for void-returning coroutine signatures.
    template <typename T = void>
    using Task = depends::Task<T>;
}

#endif // __CPPBM_INTERNAL_DEPENDS_COROUTINE_H__
