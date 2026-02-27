#ifndef __CPPBM_TASK_H__
#define __CPPBM_TASK_H__

// Public coroutine task API.
//
// Users should include this header when they only need Task<T>
// and do not need dependency-injection APIs from depends.h.

#include "internal/depends/runtime/coroutine/task.h"

namespace cpp::blackmagic
{
    template <typename T = void>
    using Task = depends::Task<T>;
}

#endif // __CPPBM_TASK_H__

