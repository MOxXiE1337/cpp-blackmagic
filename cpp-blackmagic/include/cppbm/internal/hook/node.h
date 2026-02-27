#ifndef __CPPBM_HOOK_NODE_H__
#define __CPPBM_HOOK_NODE_H__

#include <cstddef>

#include "arg_slot.h"
#include "call_context.h"

namespace cpp::blackmagic::hook
{
    // DecoratorNode defines extension points seen by HookPipeline.
    //
    // Two API layers:
    // 1) "simple" callbacks (BeforeCall / AfterCall) for common decorators
    // 2) slot-based callbacks (BeforeCallSlot / AfterCallSlot) used by pipeline
    //
    // Default implementation of slot-based callbacks forwards to simple callbacks.
    // This keeps user decorators concise while still allowing argument rebinding via ArgSlot.
    template <typename R, typename... Args>
    class DecoratorNode
    {
    public:
        virtual ~DecoratorNode() = default;

        // Requested bytes for this decorator's per-call context slot.
        // Return 0 when no context storage is needed.
        virtual std::size_t ContextSize() const { return 0; }

        virtual bool BeforeCall(Args&... /*args*/) { return true; }

        virtual bool BeforeCall(CallContext& ctx, Args&... args)
        {
            (void)ctx;
            return BeforeCall(args...);
        }

        virtual bool BeforeCallSlot(CallContext& ctx, ArgSlot<Args>&... slots)
        {
            return BeforeCall(ctx, slots.BeforeArg()...);
        }

        virtual void AfterCallSlot(CallContext& ctx, R& result)
        {
            AfterCall(ctx, result);
        }

        virtual void AfterCall(CallContext& ctx, R& result)
        {
            (void)ctx;
            AfterCall(result);
        }

        virtual void AfterCall(R& /*result*/) {}
    };

    template <typename... Args>
    class DecoratorNode<void, Args...>
    {
    public:
        virtual ~DecoratorNode() = default;

        // Requested bytes for this decorator's per-call context slot.
        // Return 0 when no context storage is needed.
        virtual std::size_t ContextSize() const { return 0; }

        virtual bool BeforeCall(Args&... /*args*/) { return true; }

        virtual bool BeforeCall(CallContext& ctx, Args&... args)
        {
            (void)ctx;
            return BeforeCall(args...);
        }

        virtual bool BeforeCallSlot(CallContext& ctx, ArgSlot<Args>&... slots)
        {
            return BeforeCall(ctx, slots.BeforeArg()...);
        }

        virtual void AfterCallSlot(CallContext& ctx)
        {
            AfterCall(ctx);
        }

        virtual void AfterCall(CallContext& ctx)
        {
            (void)ctx;
            AfterCall();
        }

        virtual void AfterCall() {}
    };
}

#endif // __CPPBM_HOOK_NODE_H__
