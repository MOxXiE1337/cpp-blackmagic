// File role:
// Aggregate @inject runtime pipeline and decorator bindings.
//
// This header is the runtime entry for @inject call dispatch.
// It wires together:
// - call-scope context setup/teardown
// - sync vs async argument resolver selection
// - concrete decorator wrappers for free/member functions

#ifndef __CPPBM_DEPENDS_INJECT_H__
#define __CPPBM_DEPENDS_INJECT_H__

#include <tuple>
#include <type_traits>
#include <utility>

#include "../../../decorator.h"
#include "inject/async.h"

namespace cpp::blackmagic::depends
{
    // Unified runtime entry used by InjectDecorator<Target>::Call.
    //
    // Dispatch policy by target return type R:
    // - R is void/reference: sync resolver path
    // - R is Task<T>: async resolver path, with fast path when no placeholder
    // - otherwise: sync resolver + context binding on returned value
    //
    // Why lease/state is created here:
    // - all parameter resolution and nested @inject calls must share one call scope
    // - task return types may outlive current stack frame; lease is rebound to task
    template <auto Target, typename R, typename... Args, typename Invoker>
    R InvokeInjectedCall(Invoker&& invoker, Args&&... args)
    {
        using SyncResolver = InjectCallResolverSync<Target, Args...>;
        using AsyncResolver = InjectCallResolverAsync<Target, Args...>;

        // Create one call lease and make it current for this invocation.
        auto lease = AcquireInjectCallLease();
        ActiveInjectStateScope active_state{ lease.StateOwner() };
        auto arg_refs = std::forward_as_tuple(args...);

        // Void/reference returns are never context-rebound values.
        // Resolve parameters synchronously and call through directly.
        if constexpr (std::is_void_v<R> || std::is_reference_v<R>)
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
            // Non-task value return:
            // resolve args synchronously, invoke, then bind context to return object
            // when return type supports binding adapters.
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
    class InjectDecorator;

    template <typename R, typename... Args, R(*Target)(Args...)>
    class InjectDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        R Call(Args... args)
        {
            // Invoker always calls original function.
            // Parameter transformation happens in InvokeInjectedCall.
            auto invoker = [this](auto&&... resolved_args) -> R {
                return this->CallOriginal(std::forward<decltype(resolved_args)>(resolved_args)...);
                };
            return depends::InvokeInjectedCall<Target, R, Args...>(
                std::move(invoker),
                std::forward<Args>(args)...);
        }
    };

    template <typename C, typename R, typename... Args, R(C::*Target)(Args...)>
    class InjectDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        R Call(C* thiz, Args... args)
        {
            // Member function wrapper keeps object pointer and delegates argument
            // resolution/lifetime handling to InvokeInjectedCall.
            auto invoker = [this, thiz](auto&&... resolved_args) -> R {
                return this->CallOriginal(thiz, std::forward<decltype(resolved_args)>(resolved_args)...);
                };
            return depends::InvokeInjectedCall<Target, R, Args...>(
                std::move(invoker),
                std::forward<Args>(args)...);
        }
    };

    template <typename C, typename R, typename... Args, R(C::*Target)(Args...) const>
    class InjectDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        R Call(const C* thiz, Args... args)
        {
            // Const member specialization mirrors mutable-member behavior.
            auto invoker = [this, thiz](auto&&... resolved_args) -> R {
                return this->CallOriginal(thiz, std::forward<decltype(resolved_args)>(resolved_args)...);
                };
            return depends::InvokeInjectedCall<Target, R, Args...>(
                std::move(invoker),
                std::forward<Args>(args)...);
        }
    };

}

#endif // __CPPBM_DEPENDS_INJECT_H__
