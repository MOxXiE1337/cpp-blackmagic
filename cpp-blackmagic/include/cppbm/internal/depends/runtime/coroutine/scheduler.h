// File role:
// Per-thread coroutine scheduler primitives for Depends/@inject integration.

#ifndef __CPPBM_INTERNAL_DEPENDS_COROUTINE_SCHEDULER_H__
#define __CPPBM_INTERNAL_DEPENDS_COROUTINE_SCHEDULER_H__

#include <coroutine>
#include <deque>
#include <memory>

#include "../context.h"

namespace cpp::blackmagic::depends
{
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

    inline bool RunTaskSchedulerOnce()
    {
        return CurrentTaskScheduler().RunOne();
    }

    inline void RunTaskSchedulerUntilIdle()
    {
        CurrentTaskScheduler().RunUntilIdle();
    }
}

#endif // __CPPBM_INTERNAL_DEPENDS_COROUTINE_SCHEDULER_H__
