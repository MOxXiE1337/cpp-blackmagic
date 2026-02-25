// File role:
// Asynchronous @inject argument resolution and invocation helpers.

#ifndef __CPPBM_DEPENDS_INJECT_ASYNC_H__
#define __CPPBM_DEPENDS_INJECT_ASYNC_H__

#include <tuple>
#include <type_traits>
#include <utility>

#include "inject_sync.h"
#include "resolve_async.h"

namespace cpp::blackmagic::depends
{
    template <typename T>
    struct IsTaskReturn;

    template <auto Target, typename... Args>
    struct InjectCallResolverAsync : InjectCallResolverSync<Target, Args...>
    {
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

        template <std::size_t Index, typename A>
        static Task<std::tuple_element_t<Index, std::tuple<Args...>>> ResolveArgAsync(A& arg)
        {
            using Declared = std::tuple_element_t<Index, std::tuple<Args...>>;
            using Raw = std::remove_cv_t<std::remove_reference_t<Declared>>;
            const void* target = TargetKeyOf<Target>();
            const bool is_depends_param = IsDependsPlaceholder<Declared>(arg);

            if constexpr (std::is_reference_v<Declared>)
            {
                if (!is_depends_param)
                {
                    co_return static_cast<Declared>(arg);
                }

                const void* resolved_factory = nullptr;
                if (co_await TryResolveDefaultArgForParamAsync<Declared>(target, Index, arg, &resolved_factory))
                {
                    if (auto* resolved_ptr = TryResolveRawPtr<Raw>(target, resolved_factory))
                    {
                        co_return static_cast<Declared>(*resolved_ptr);
                    }
                }
                co_return FailInject<Declared>(InjectError{
                    InjectErrorCode::MissingDependency,
                    target,
                    Index,
                    typeid(Raw),
                    resolved_factory,
                    "Depends placeholder async resolution failed in @inject: missing slot(T&)."
                    });
            }
            else
            {
                if (!is_depends_param)
                {
                    co_return static_cast<Declared>(arg);
                }

                const void* resolved_factory = nullptr;
                if (co_await TryResolveDefaultArgForParamAsync<Declared>(target, Index, arg, &resolved_factory))
                {
                    co_return static_cast<Declared>(arg);
                }

                if constexpr (std::is_pointer_v<Declared>)
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
                else
                {
                    co_return FailInject<Declared>(InjectError{
                        InjectErrorCode::MissingDependency,
                        target,
                        Index,
                        typeid(Raw),
                        resolved_factory,
                        "Depends placeholder async resolution failed in @inject: missing slot(T)."
                        });
                }
            }
        }

        template <typename RTask, typename Invoker, std::size_t... I>
            requires IsTaskReturn<RTask>::value
        static RTask InvokeAsync(
            Invoker invoker,
            std::tuple<Args...> arg_values,
            std::index_sequence<I...>)
        {
            using TaskValue = typename IsTaskReturn<RTask>::ValueType;
            if constexpr (std::is_void_v<TaskValue>)
            {
                co_await std::forward<Invoker>(invoker)(
                    (co_await ResolveArgAsync<I>(std::get<I>(arg_values)))...);
                co_return;
            }
            else
            {
                co_return co_await std::forward<Invoker>(invoker)(
                    (co_await ResolveArgAsync<I>(std::get<I>(arg_values)))...);
            }
        }
    };
}

#endif // __CPPBM_DEPENDS_INJECT_ASYNC_H__
