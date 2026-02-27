// File role:
// Aggregate @inject runtime decorator behavior.
//
// In this architecture:
// - hook::HookPipeline owns dispatch order and single original invocation
// - InjectDecorator only participates as a decorator node:
//   - BeforeCall: resolve Depends placeholders in arguments
//   - AfterCall: bind inject context to returned object/task when needed

#ifndef __CPPBM_DEPENDS_INJECT_H__
#define __CPPBM_DEPENDS_INJECT_H__

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "../../../decorator.h"
#include "inject/async.h"

namespace cpp::blackmagic::depends::detail
{
    template <auto Target, typename R, typename... Args>
    struct InjectDecoratorRuntime
    {
        using SyncResolver = InjectCallResolverSync<Target, Args...>;
        using AsyncResolver = InjectCallResolverAsync<Target, Args...>;

        struct InjectCallFrame
        {
            explicit InjectCallFrame(InjectContextLease&& in_lease)
                : lease(std::move(in_lease)),
                active(lease.StateOwner())
            {
            }

            InjectContextLease lease;
            ActiveInjectStateScope active;
        };

        static InjectCallFrame* InitCallFrame(hook::CallContext& ctx)
        {
            auto* slot = ctx.template As<InjectCallFrame>();
            if (slot == nullptr)
            {
                return nullptr;
            }
            return std::construct_at(slot, depends::AcquireInjectCallLease());
        }

        static InjectCallFrame* GetCallFrame(hook::CallContext& ctx)
        {
            return ctx.template As<InjectCallFrame>();
        }

        static void DestroyCallFrame(hook::CallContext& ctx)
        {
            if (auto* slot = ctx.template As<InjectCallFrame>(); slot != nullptr)
            {
                std::destroy_at(slot);
            }
        }

        template <typename Declared, typename Slot, typename Resolved>
        static void AssignResolved(Slot& slot, Resolved&& resolved)
        {
            if constexpr (std::is_lvalue_reference_v<Declared> || std::is_rvalue_reference_v<Declared>)
            {
                slot.Rebind(std::addressof(resolved));
            }
            else
            {
                slot.Assign(static_cast<Declared>(std::forward<Resolved>(resolved)));
            }
        }

        template <std::size_t I, typename Slot>
        static bool HasDependsPlaceholderOne(Slot& slot)
        {
            using Declared = std::tuple_element_t<I, std::tuple<Args...>>;
            return IsDependsPlaceholder<Declared>(slot.Get());
        }

        template <typename... Slots, std::size_t... I>
        static bool HasDependsPlaceholderInSlotsImpl(std::index_sequence<I...>, Slots&... slots)
        {
            return (... || HasDependsPlaceholderOne<I>(slots));
        }

        template <typename... Slots>
        static bool HasDependsPlaceholderInSlots(Slots&... slots)
        {
            return HasDependsPlaceholderInSlotsImpl(
                std::index_sequence_for<Args...>{},
                slots...);
        }

        template <std::size_t I, typename Slot>
        static void ResolveOne(Slot& slot)
        {
            using Declared = std::tuple_element_t<I, std::tuple<Args...>>;
            decltype(auto) current = slot.Get();

            if (!IsDependsPlaceholder<Declared>(current))
            {
                return;
            }

            if constexpr (IsTaskReturn<R>::value)
            {
                auto resolved_task = AsyncResolver::template ResolveArgAsyncPlaceholder<I>(current);
                decltype(auto) resolved = resolved_task.Get();
                AssignResolved<Declared>(slot, resolved);
            }
            else
            {
                decltype(auto) resolved = SyncResolver::template ResolveArg<I>(current);
                AssignResolved<Declared>(slot, resolved);
            }
        }

        template <std::size_t... I, typename... Slots>
        static void ResolveAllSlotsImpl(std::index_sequence<I...>, Slots&... slots)
        {
            (ResolveOne<I>(slots), ...);
        }

        template <typename... Slots>
        static void ResolveAllSlots(Slots&... slots)
        {
            static_assert(sizeof...(Slots) == sizeof...(Args),
                "ResolveAllSlots expects one slot for each function parameter.");

            // Async fast path:
            // when this call has no Depends placeholder at all, skip slow resolver loop.
            if constexpr (IsTaskReturn<R>::value)
            {
                if (!HasDependsPlaceholderInSlots(slots...))
                {
                    return;
                }
            }

            ResolveAllSlotsImpl(std::index_sequence_for<Args...>{}, slots...);
        }
    };
}

namespace cpp::blackmagic
{
    template <auto Target>
    class InjectDecorator;

    template <typename... Args, void(*Target)(Args...)>
    class InjectDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        std::size_t ContextSize() const override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, void, Args...>;
            return sizeof(typename Runtime::InjectCallFrame);
        }

        bool BeforeCallSlot(hook::CallContext& ctx, hook::ArgSlot<Args>&... slots) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, void, Args...>;

            if (Runtime::InitCallFrame(ctx) == nullptr)
            {
                return false;
            }
            Runtime::ResolveAllSlots(slots...);
            return true;
        }

        void AfterCallSlot(hook::CallContext& ctx) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, void, Args...>;
            Runtime::DestroyCallFrame(ctx);
        }
    };

    template <typename R, typename... Args, R(*Target)(Args...)>
        requires (!std::is_void_v<R>)
    class InjectDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        std::size_t ContextSize() const override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, R, Args...>;
            return sizeof(typename Runtime::InjectCallFrame);
        }

        bool BeforeCallSlot(hook::CallContext& ctx, hook::ArgSlot<Args>&... slots) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, R, Args...>;

            if (Runtime::InitCallFrame(ctx) == nullptr)
            {
                return false;
            }
            Runtime::ResolveAllSlots(slots...);
            return true;
        }

        void AfterCallSlot(hook::CallContext& ctx, R& result) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, R, Args...>;
            auto* frame = Runtime::GetCallFrame(ctx);
            if (frame == nullptr)
            {
                return;
            }

            if constexpr (!std::is_reference_v<R> && std::is_move_assignable_v<R>)
            {
                result = depends::detail::AutoBindInjectContext(
                    std::move(result),
                    std::move(frame->lease));
            }
            Runtime::DestroyCallFrame(ctx);
        }
    };

    template <typename C, typename... Args, void(C::*Target)(Args...)>
    class InjectDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        std::size_t ContextSize() const override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, void, Args...>;
            return sizeof(typename Runtime::InjectCallFrame);
        }

        bool BeforeCallSlot(
            hook::CallContext& ctx,
            hook::ArgSlot<C*>& /*thiz_slot*/,
            hook::ArgSlot<Args>&... slots) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, void, Args...>;

            if (Runtime::InitCallFrame(ctx) == nullptr)
            {
                return false;
            }
            Runtime::ResolveAllSlots(slots...);
            return true;
        }

        void AfterCallSlot(hook::CallContext& ctx) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, void, Args...>;
            Runtime::DestroyCallFrame(ctx);
        }
    };

    template <typename C, typename R, typename... Args, R(C::*Target)(Args...)>
        requires (!std::is_void_v<R>)
    class InjectDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        std::size_t ContextSize() const override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, R, Args...>;
            return sizeof(typename Runtime::InjectCallFrame);
        }

        bool BeforeCallSlot(
            hook::CallContext& ctx,
            hook::ArgSlot<C*>& /*thiz_slot*/,
            hook::ArgSlot<Args>&... slots) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, R, Args...>;

            if (Runtime::InitCallFrame(ctx) == nullptr)
            {
                return false;
            }
            Runtime::ResolveAllSlots(slots...);
            return true;
        }

        void AfterCallSlot(hook::CallContext& ctx, R& result) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, R, Args...>;
            auto* frame = Runtime::GetCallFrame(ctx);
            if (frame == nullptr)
            {
                return;
            }

            if constexpr (!std::is_reference_v<R> && std::is_move_assignable_v<R>)
            {
                result = depends::detail::AutoBindInjectContext(
                    std::move(result),
                    std::move(frame->lease));
            }
            Runtime::DestroyCallFrame(ctx);
        }
    };

    template <typename C, typename... Args, void(C::*Target)(Args...) const>
    class InjectDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        std::size_t ContextSize() const override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, void, Args...>;
            return sizeof(typename Runtime::InjectCallFrame);
        }

        bool BeforeCallSlot(
            hook::CallContext& ctx,
            hook::ArgSlot<const C*>& /*thiz_slot*/,
            hook::ArgSlot<Args>&... slots) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, void, Args...>;

            if (Runtime::InitCallFrame(ctx) == nullptr)
            {
                return false;
            }
            Runtime::ResolveAllSlots(slots...);
            return true;
        }

        void AfterCallSlot(hook::CallContext& ctx) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, void, Args...>;
            Runtime::DestroyCallFrame(ctx);
        }
    };

    template <typename C, typename R, typename... Args, R(C::*Target)(Args...) const>
        requires (!std::is_void_v<R>)
    class InjectDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        std::size_t ContextSize() const override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, R, Args...>;
            return sizeof(typename Runtime::InjectCallFrame);
        }

        bool BeforeCallSlot(
            hook::CallContext& ctx,
            hook::ArgSlot<const C*>& /*thiz_slot*/,
            hook::ArgSlot<Args>&... slots) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, R, Args...>;

            if (Runtime::InitCallFrame(ctx) == nullptr)
            {
                return false;
            }
            Runtime::ResolveAllSlots(slots...);
            return true;
        }

        void AfterCallSlot(hook::CallContext& ctx, R& result) override
        {
            using Runtime = depends::detail::InjectDecoratorRuntime<Target, R, Args...>;
            auto* frame = Runtime::GetCallFrame(ctx);
            if (frame == nullptr)
            {
                return;
            }

            if constexpr (!std::is_reference_v<R> && std::is_move_assignable_v<R>)
            {
                result = depends::detail::AutoBindInjectContext(
                    std::move(result),
                    std::move(frame->lease));
            }
            Runtime::DestroyCallFrame(ctx);
        }
    };
}

#endif // __CPPBM_DEPENDS_INJECT_H__
