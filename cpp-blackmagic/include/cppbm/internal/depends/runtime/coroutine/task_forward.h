// File role:
// Forward declarations for coroutine Task and TaskPromise types.

#ifndef __CPPBM_INTERNAL_DEPENDS_COROUTINE_TASK_FORWARD_H__
#define __CPPBM_INTERNAL_DEPENDS_COROUTINE_TASK_FORWARD_H__

namespace cpp::blackmagic::depends
{
    template <typename T = void>
    class Task;

    template <typename T>
    class TaskPromise;

    class TaskPromiseBase;
    struct TaskFinalAwaiter;
}

#endif // __CPPBM_INTERNAL_DEPENDS_COROUTINE_TASK_FORWARD_H__
