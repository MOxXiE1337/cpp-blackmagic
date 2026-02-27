// File role:
// Asynchronous @inject argument resolution and invocation helpers.
//
// This layer mirrors sync resolver semantics, but runs placeholder resolution
// through coroutine-aware metadata APIs and returns Task-based results.

#ifndef __CPPBM_DEPENDS_INJECT_ASYNC_H__
#define __CPPBM_DEPENDS_INJECT_ASYNC_H__

#include <tuple>
#include <type_traits>
#include <utility>

#include "sync.h"
#include "../resolve/async.h"

namespace cpp::blackmagic::depends
{
    template <typename T>
    struct IsTaskReturn;

    // Async resolver extends sync resolver behavior for Task-returning targets.
    template <auto Target, typename... Args>
    struct InjectCallResolverAsync : InjectCallResolverSync<Target, Args...>
    {
        // Internal helper used by runtime fast path:
        // if no Depends placeholder exists in actual arguments, async resolver can
        // skip per-argument async pipeline entirely.
        template <std::size_t... I>
        static bool HasDependsPlaceholderInArgRefsImpl(
            std::tuple<Args&...>& arg_refs,
            std::index_sequence<I...>)
        {
            return (... || IsDependsPlaceholder<
                std::tuple_element_t<I, std::tuple<Args...>>>(std::get<I>(arg_refs)));
        }

        static bool HasDependsPlaceholderInArgRefs(std::tuple<Args&...>& arg_refs)
        {
            return HasDependsPlaceholderInArgRefsImpl(
                arg_refs,
                std::index_sequence_for<Args...>{});
        }

        // Async slow path for placeholder arguments only.
        // Caller guarantees arg is a Depends placeholder.
        template <std::size_t Index, typename A>
        static Task<std::tuple_element_t<Index, std::tuple<Args...>>> ResolveArgAsyncPlaceholder(A& arg)
        {
            using Declared = std::tuple_element_t<Index, std::tuple<Args...>>;
            using Raw = std::remove_cv_t<std::remove_reference_t<Declared>>;
            const void* target = TargetKeyOf<Target>();

            // Placeholder argument: resolve from default-arg metadata for this target/index.
            const void* resolved_factory = nullptr;
            if (co_await TryResolveDefaultArgForParamAsync<Declared>(target, Index, arg, &resolved_factory))
            {
                if constexpr (std::is_reference_v<Declared>)
                {
                    if (auto* resolved_ptr = TryResolveRawPtr<Raw>(target, resolved_factory))
                    {
                        co_return static_cast<Declared>(*resolved_ptr);
                    }
                }
                else
                {
                    co_return static_cast<Declared>(arg);
                }
            }

            if constexpr (std::is_reference_v<Declared>)
            {
                co_return FailInject<Declared>(InjectError{
                    InjectErrorCode::MissingDependency,
                    target,
                    Index,
                    typeid(Raw),
                    resolved_factory,
                    "Depends placeholder async resolution failed in @inject: missing slot(T&)."
                    });
            }
            else if constexpr (std::is_pointer_v<Declared>)
            {
                using Pointee = std::remove_cv_t<std::remove_pointer_t<Declared>>;
                co_return FailInject<Declared>(InjectError{
                    InjectErrorCode::MissingDependency,
                    target,
                    Index,
                    typeid(Pointee),
                    resolved_factory,
                    "Depends placeholder async resolution failed in @inject: missing slot(T*)."
                    });
            }

            co_return FailInject<Declared>(InjectError{
                InjectErrorCode::InvalidPlaceholder,
                target,
                Index,
                typeid(Raw),
                resolved_factory,
                "Depends placeholder is only supported for pointer/reference parameters in @inject."
                });
        }

        // Async general entry:
        // - non-placeholder: direct pass-through
        // - placeholder: delegate to slow path above
        template <std::size_t Index, typename A>
        static Task<std::tuple_element_t<Index, std::tuple<Args...>>> ResolveArgAsyncMaybe(A& arg)
        {
            using Declared = std::tuple_element_t<Index, std::tuple<Args...>>;
            if (!IsDependsPlaceholder<Declared>(arg))
            {
                co_return static_cast<Declared>(arg);
            }
            co_return co_await ResolveArgAsyncPlaceholder<Index>(arg);
        }

        template <typename RTask, typename Invoker, std::size_t... I>
            requires IsTaskReturn<RTask>::value
        static RTask InvokeAsync(
            Invoker invoker,
            std::tuple<Args...> arg_values,
            std::index_sequence<I...>)
        {
            // Arguments are stored by value in arg_values to avoid dangling stack refs
            // after outer call frame returns and coroutine resumes later.
            using TaskValue = typename IsTaskReturn<RTask>::ValueType;
            if constexpr (std::is_void_v<TaskValue>)
            {
                co_await std::forward<Invoker>(invoker)(
                    (co_await ResolveArgAsyncMaybe<I>(std::get<I>(arg_values)))...);
                co_return;
            }
            else
            {
                co_return co_await std::forward<Invoker>(invoker)(
                    (co_await ResolveArgAsyncMaybe<I>(std::get<I>(arg_values)))...);
            }
        }
    };
}

#endif // __CPPBM_DEPENDS_INJECT_ASYNC_H__
