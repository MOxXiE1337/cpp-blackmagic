// File role:
// Task-layer coroutine holders for Depends/@inject integration.
//
// This header contains only Task<T> runtime behavior.
// Promise-layer types live in task_promise.h.

#ifndef __CPPBM_INTERNAL_DEPENDS_COROUTINE_TASK_H__
#define __CPPBM_INTERNAL_DEPENDS_COROUTINE_TASK_H__

#include <cassert>
#include <coroutine>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "task_promise.h"

namespace cpp::blackmagic::depends
{
    namespace detail
    {
        template <typename T>
        constexpr const char* TaskGetDeadlockMessage() noexcept
        {
            if constexpr (std::is_void_v<T>)
            {
                return "Task<void>::Get deadlock: scheduler queue drained before completion.";
            }
            else if constexpr (std::is_reference_v<T>)
            {
                return "Task<T&>::Get deadlock: scheduler queue drained before completion.";
            }
            else
            {
                return "Task::Get deadlock: scheduler queue drained before completion.";
            }
        }

        template <typename T>
        constexpr const char* TaskAwaiterAssertMessage() noexcept
        {
            if constexpr (std::is_void_v<T>)
            {
                return "Task<void>::Awaiter requires non-null handle.";
            }
            else if constexpr (std::is_reference_v<T>)
            {
                return "Task<T&>::Awaiter requires non-null handle.";
            }
            else
            {
                return "Task::Awaiter requires non-null handle.";
            }
        }
    }

    template <typename T>
    class Task
    {
    public:
        using promise_type = TaskPromise<T>;
        using Handle = std::coroutine_handle<promise_type>;
        using ReturnType = std::conditional_t<std::is_void_v<T>, void, T>;

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

        ReturnType Get()
        {
            // Synchronous bridge for async task:
            // pump the per-thread scheduler until this task reaches done().
            Schedule();
            while (!Done())
            {
                if (!RunTaskSchedulerOnce())
                {
                    throw std::runtime_error(detail::TaskGetDeadlockMessage<T>());
                }
            }

            if constexpr (std::is_void_v<T>)
            {
                handle_.promise().EnsureCompleted();
                return;
            }
            else
            {
                return handle_.promise().TakeValue();
            }
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

            ReturnType await_resume()
            {
                assert(handle && detail::TaskAwaiterAssertMessage<T>());

                if constexpr (std::is_void_v<T>)
                {
                    handle.promise().EnsureCompleted();
                    if (owns_handle)
                    {
                        handle.destroy();
                        handle = {};
                    }
                    return;
                }
                else
                {
                    if constexpr (std::is_reference_v<T>)
                    {
                        auto& out = handle.promise().TakeValue();
                        if (owns_handle)
                        {
                            // Rvalue co_await path: awaiter owns frame lifetime.
                            handle.destroy();
                            handle = {};
                        }
                        return out;
                    }
                    else
                    {
                        auto out = handle.promise().TakeValue();
                        if (owns_handle)
                        {
                            // Rvalue co_await path: awaiter owns frame lifetime.
                            handle.destroy();
                            handle = {};
                        }
                        return out;
                    }
                }
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

#endif // __CPPBM_INTERNAL_DEPENDS_COROUTINE_TASK_H__
