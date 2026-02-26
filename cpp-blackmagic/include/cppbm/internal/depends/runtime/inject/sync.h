// File role:
// Synchronous @inject argument resolution and invocation helpers.
//
// This layer performs per-argument runtime behavior for non-task call paths:
// - detect whether caller passed a Depends placeholder
// - resolve placeholder via default-arg metadata + explicit overrides
// - pass through explicit caller arguments unchanged

#ifndef __CPPBM_DEPENDS_INJECT_SYNC_H__
#define __CPPBM_DEPENDS_INJECT_SYNC_H__

#include <tuple>
#include <type_traits>
#include <utility>

#include "../resolve/sync.h"

namespace cpp::blackmagic::depends
{
    // Sync resolver parameterized by one concrete target function signature.
    template <auto Target, typename... Args>
    struct InjectCallResolverSync
    {
        // Resolve one argument at parameter Index.
        //
        // Contract:
        // - if arg is not placeholder: return arg as-is
        // - if arg is placeholder: resolve via TryResolveDefaultArgForParam
        // - on failure:
        //   - pointer/reference params -> MissingDependency
        //   - value params             -> InvalidPlaceholder
        template <std::size_t Index, typename A>
        static std::tuple_element_t<Index, std::tuple<Args...>> ResolveArg(A& arg)
        {
            using Declared = std::tuple_element_t<Index, std::tuple<Args...>>;
            using Raw = std::remove_cv_t<std::remove_reference_t<Declared>>;
            const void* target = TargetKeyOf<Target>();
            if (!IsDependsPlaceholder<Declared>(arg))
            {
                // Non-placeholder argument: caller provided explicit value.
                return static_cast<Declared>(arg);
            }

            // Placeholder argument: resolve from default-arg metadata for this target/index.
            const void* resolved_factory = nullptr;
            if (TryResolveDefaultArgForParam<Declared>(target, Index, arg, &resolved_factory))
            {
                if constexpr (std::is_reference_v<Declared>)
                {
                    if (auto* resolved_ptr = TryResolveRawPtr<Raw>(target, resolved_factory))
                    {
                        return static_cast<Declared>(*resolved_ptr);
                    }
                }
                else
                {
                    // Pointer/value declared type returns resolved argument as-is.
                    return static_cast<Declared>(arg);
                }
            }

            if constexpr (std::is_reference_v<Declared>)
            {
                return FailInject<Declared>(InjectError{
                    InjectErrorCode::MissingDependency,
                    target,
                    Index,
                    typeid(Raw),
                    resolved_factory,
                    "Depends placeholder resolution failed in @inject: missing slot(T&)."
                    });
            }
            else if constexpr (std::is_pointer_v<Declared>)
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

            return FailInject<Declared>(InjectError{
                InjectErrorCode::InvalidPlaceholder,
                target,
                Index,
                typeid(Raw),
                resolved_factory,
                "Depends placeholder is only supported for pointer/reference parameters in @inject."
                });
        }

        template <typename R, typename Invoker, std::size_t... I>
        static R Invoke(
            Invoker&& invoker,
            std::tuple<Args&...>& arg_refs,
            std::index_sequence<I...>)
        {
            // Expand parameter pack in declaration order:
            // each argument is resolved independently before invoking original target.
            return std::forward<Invoker>(invoker)(ResolveArg<I>(std::get<I>(arg_refs))...);
        }
    };
}

#endif // __CPPBM_DEPENDS_INJECT_SYNC_H__
