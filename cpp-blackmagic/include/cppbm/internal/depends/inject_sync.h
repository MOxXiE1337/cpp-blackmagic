// File role:
// Synchronous @inject argument resolution and invocation helpers.

#ifndef __CPPBM_DEPENDS_INJECT_SYNC_H__
#define __CPPBM_DEPENDS_INJECT_SYNC_H__

#include <tuple>
#include <type_traits>
#include <utility>

#include "resolve_sync.h"

namespace cpp::blackmagic::depends
{
    template <auto Target, typename... Args>
    struct InjectCallResolverSync
    {
        template <std::size_t Index, typename A>
        static std::tuple_element_t<Index, std::tuple<Args...>> ResolveArg(A& arg)
        {
            using Declared = std::tuple_element_t<Index, std::tuple<Args...>>;
            using Raw = std::remove_cv_t<std::remove_reference_t<Declared>>;
            const void* target = TargetKeyOf<Target>();
            const bool is_depends_param = IsDependsPlaceholder<Declared>(arg);

            if constexpr (std::is_reference_v<Declared>)
            {
                if (!is_depends_param)
                {
                    return static_cast<Declared>(arg);
                }

                const void* resolved_factory = nullptr;
                if (TryResolveDefaultArgForParam<Declared>(target, Index, arg, &resolved_factory))
                {
                    if (auto* resolved_ptr = TryResolveRawPtr<Raw>(target, resolved_factory))
                    {
                        return static_cast<Declared>(*resolved_ptr);
                    }
                }
                return FailInject<Declared>(InjectError{
                    InjectErrorCode::MissingDependency,
                    target,
                    Index,
                    typeid(Raw),
                    resolved_factory,
                    "Depends placeholder resolution failed in @inject: missing slot(T&)."
                    });
            }
            else
            {
                if (!is_depends_param)
                {
                    return static_cast<Declared>(arg);
                }

                const void* resolved_factory = nullptr;
                if (TryResolveDefaultArgForParam<Declared>(target, Index, arg, &resolved_factory))
                {
                    return static_cast<Declared>(arg);
                }

                if constexpr (std::is_pointer_v<Declared>)
                {
                    using Pointee = std::remove_cv_t<std::remove_pointer_t<Declared>>;
                    return FailInject<Declared>(InjectError{
                        InjectErrorCode::MissingDependency,
                        target,
                        Index,
                        typeid(Pointee),
                        resolved_factory,
                        "Depends placeholder resolution failed in @inject: missing slot(T*)."
                        });
                }
                else
                {
                    return FailInject<Declared>(InjectError{
                        InjectErrorCode::MissingDependency,
                        target,
                        Index,
                        typeid(Raw),
                        resolved_factory,
                        "Depends placeholder resolution failed in @inject: missing slot(T)."
                        });
                }
            }
        }

        template <typename R, typename Invoker, std::size_t... I>
        static R Invoke(
            Invoker&& invoker,
            std::tuple<Args&...>& arg_refs,
            std::index_sequence<I...>)
        {
            if constexpr (std::is_void_v<R>)
            {
                std::forward<Invoker>(invoker)(ResolveArg<I>(std::get<I>(arg_refs))...);
                return;
            }
            else
            {
                return std::forward<Invoker>(invoker)(ResolveArg<I>(std::get<I>(arg_refs))...);
            }
        }
    };
}

#endif // __CPPBM_DEPENDS_INJECT_SYNC_H__
