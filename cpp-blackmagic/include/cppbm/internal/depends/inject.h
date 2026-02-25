// File role:
// Aggregate @inject runtime pipeline and decorator bindings.

#ifndef __CPPBM_DEPENDS_INJECT_H__
#define __CPPBM_DEPENDS_INJECT_H__

#include <tuple>
#include <type_traits>
#include <utility>

#include "../../decorator.h"
#include "inject_async.h"

namespace cpp::blackmagic::depends
{
    template <auto Target, typename R, typename... Args, typename Invoker>
    R InvokeInjectedCall(Invoker&& invoker, Args&&... args)
    {
        using SyncResolver = InjectCallResolverSync<Target, Args...>;
        using AsyncResolver = InjectCallResolverAsync<Target, Args...>;

        auto lease = AcquireInjectCallLease();
        ActiveInjectStateScope active_state{ lease.StateOwner() };
        auto arg_refs = std::forward_as_tuple(args...);

        if constexpr (std::is_void_v<R>)
        {
            SyncResolver::template Invoke<R>(
                std::forward<Invoker>(invoker),
                arg_refs,
                std::index_sequence_for<Args...>{});
            return;
        }
        else if constexpr (std::is_reference_v<R>)
        {
            return SyncResolver::template Invoke<R>(
                std::forward<Invoker>(invoker),
                arg_refs,
                std::index_sequence_for<Args...>{});
        }
        else if constexpr (IsTaskReturn<R>::value)
        {
            // Async fast path:
            // when all arguments are explicit values (no Depends placeholder),
            // skip ResolveArgAsync pipeline and call original directly.
            if (!AsyncResolver::HasDependsPlaceholderInArgRefs(arg_refs))
            {
                auto result = std::forward<Invoker>(invoker)(std::forward<Args>(args)...);
                return detail::AutoBindInjectContext(std::move(result), std::move(lease));
            }

            // Task-returning @inject uses coroutine-aware resolve/invoke path.
            // Important lifetime note:
            // - do not pass arg_refs (stack references) into coroutine frame.
            // - copy/move Args... into tuple<Args...> so async resume never touches
            //   invalid stack addresses after Call() returns.
            auto arg_values = std::tuple<Args...>{ std::forward<Args>(args)... };
            auto result = AsyncResolver::template InvokeAsync<R>(
                std::forward<Invoker>(invoker),
                std::move(arg_values),
                std::index_sequence_for<Args...>{});
            return detail::AutoBindInjectContext(std::move(result), std::move(lease));
        }
        else
        {
            auto result = SyncResolver::template Invoke<R>(
                std::forward<Invoker>(invoker),
                arg_refs,
                std::index_sequence_for<Args...>{});
            return detail::AutoBindInjectContext(std::move(result), std::move(lease));
        }
    }
}

namespace cpp::blackmagic
{
    template <auto Target>
    class decorator_class(inject);

    template <typename R, typename... Args, R(*Target)(Args...)>
    class decorator_class(inject)<Target> : public FunctionDecorator<Target>
    {
    public:
        R Call(Args... args)
        {
            auto invoker = [this](auto&&... resolved_args) -> R {
                if constexpr (std::is_void_v<R>)
                {
                    this->CallOriginal(std::forward<decltype(resolved_args)>(resolved_args)...);
                    return;
                }
                else
                {
                    return this->CallOriginal(std::forward<decltype(resolved_args)>(resolved_args)...);
                }
                };
            return depends::InvokeInjectedCall<Target, R, Args...>(
                std::move(invoker),
                std::forward<Args>(args)...);
        }
    };

    template <typename C, typename R, typename... Args, R(C::*Target)(Args...)>
    class decorator_class(inject)<Target> : public FunctionDecorator<Target>
    {
    public:
        R Call(C* thiz, Args... args)
        {
            auto invoker = [this, thiz](auto&&... resolved_args) -> R {
                if constexpr (std::is_void_v<R>)
                {
                    this->CallOriginal(thiz, std::forward<decltype(resolved_args)>(resolved_args)...);
                    return;
                }
                else
                {
                    return this->CallOriginal(thiz, std::forward<decltype(resolved_args)>(resolved_args)...);
                }
                };
            return depends::InvokeInjectedCall<Target, R, Args...>(
                std::move(invoker),
                std::forward<Args>(args)...);
        }
    };

    template <typename C, typename R, typename... Args, R(C::*Target)(Args...) const>
    class decorator_class(inject)<Target> : public FunctionDecorator<Target>
    {
    public:
        R Call(const C* thiz, Args... args)
        {
            auto invoker = [this, thiz](auto&&... resolved_args) -> R {
                if constexpr (std::is_void_v<R>)
                {
                    this->CallOriginal(thiz, std::forward<decltype(resolved_args)>(resolved_args)...);
                    return;
                }
                else
                {
                    return this->CallOriginal(thiz, std::forward<decltype(resolved_args)>(resolved_args)...);
                }
                };
            return depends::InvokeInjectedCall<Target, R, Args...>(
                std::move(invoker),
                std::forward<Args>(args)...);
        }
    };
}

#endif // __CPPBM_DEPENDS_INJECT_H__
