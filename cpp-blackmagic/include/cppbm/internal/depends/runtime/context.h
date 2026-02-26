// File role:
// Runtime context/state and slot cache primitives for DI resolution.
//
// This layer does not decide high-level policy of parameter resolution;
// it only provides:
// - execution-context aware current-state routing (contextvars style)
// - slot structures and storage helpers
// - low-level explicit-injection lookup and caching

#ifndef __CPPBM_DEPENDS_CONTEXT_H__
#define __CPPBM_DEPENDS_CONTEXT_H__

#include <cassert>
#include <algorithm>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../utils/contextvar.h"
#include "../compile/meta.h"
#include "../compile/registry.h"

namespace cpp::blackmagic::depends
{
    struct ContextSlot
    {
        // Raw address of resolved dependency object.
        // Type identity is tracked by SlotKey.type at lookup time.
        void* obj = nullptr;

        // Optional ownership holder for obj.
        // - empty holder  => borrowed pointer
        // - non-empty     => context owns object lifetime
        std::shared_ptr<void> holder{};
    };

    struct SlotKey
    {
        // Canonical object type key for slot identity.
        std::type_index type = typeid(void);

        // Optional factory key associated with this resolved value.
        // nullptr means plain Depends() without explicit factory.
        const void* factory = nullptr;

        bool operator==(const SlotKey& rhs) const
        {
            return type == rhs.type && factory == rhs.factory;
        }
    };

    struct SlotKeyHash
    {
        std::size_t operator()(const SlotKey& key) const
        {
            const auto h1 = std::hash<std::type_index>{}(key.type);
            const auto h2 = std::hash<const void*>{}(key.factory);
            return h1 ^ (h2 << 1);
        }
    };

    struct InjectContext
    {
        // Parent for nested @inject calls.
        InjectContext* parent = nullptr;

        // Resolved dependency cache for current scope only.
        std::unordered_map<SlotKey, ContextSlot, SlotKeyHash> slots{};
    };

    // One injectable call-chain state.
    // This can outlive the stack frame that created it (e.g. coroutine return object).
    struct InjectContextState
    {
        InjectContext root{};
        std::vector<InjectContext*> context_stack{};
        int execute_depends_depth = 0;
        int inject_call_depth = 0;

        InjectContextState()
        {
            context_stack.push_back(&root);
        }
    };

    inline void ResetInjectContextState(InjectContextState& state)
    {
        state.root.parent = nullptr;
        state.root.slots.clear();
        state.context_stack.clear();
        state.context_stack.push_back(&state.root);
        state.execute_depends_depth = 0;
        state.inject_call_depth = 0;
    }

    // Per-thread ambient state fallback.
    // This preserves legacy behavior when no coroutine/context binding is active.
    inline std::shared_ptr<InjectContextState> GetAmbientStateOwner()
    {
        static thread_local std::shared_ptr<InjectContextState> ambient = std::make_shared<InjectContextState>();
        return ambient;
    }

    // "Current active state" context variable.
    // - sync code: usually unset, so caller falls back to ambient state
    // - coroutine code: runtime can bind/unbind around resume points
    inline utils::ContextVar<std::shared_ptr<InjectContextState>>& ActiveInjectStateVar()
    {
        static utils::ContextVar<std::shared_ptr<InjectContextState>> var{};
        return var;
    }

    inline std::shared_ptr<InjectContextState> GetActiveStateOwner()
    {
        auto maybe = ActiveInjectStateVar().Get();
        if (!maybe || !(*maybe))
        {
            // No bound async state on this execution path:
            // degrade to thread-local ambient owner (sync-compatible behavior).
            return GetAmbientStateOwner();
        }
        return *maybe;
    }

    inline InjectContextState& GetActiveState()
    {
        return *GetActiveStateOwner();
    }

    // Temporarily switch thread-local active state.
    // Used by @inject entry and coroutine resume adapters.
    class ActiveInjectStateScope
    {
    public:
        explicit ActiveInjectStateScope(std::shared_ptr<InjectContextState> state)
            : token_(ActiveInjectStateVar().Set(state ? std::move(state) : GetAmbientStateOwner()))
        {
        }

        ~ActiveInjectStateScope()
        {
        }

        ActiveInjectStateScope(const ActiveInjectStateScope&) = delete;
        ActiveInjectStateScope& operator=(const ActiveInjectStateScope&) = delete;

        ActiveInjectStateScope(ActiveInjectStateScope&& rhs) noexcept
            : token_(std::move(rhs.token_))
        {
        }

        ActiveInjectStateScope& operator=(ActiveInjectStateScope&& rhs) noexcept = delete;

    private:
        utils::ContextVar<std::shared_ptr<InjectContextState>>::Token token_{};
    };

    // Scope guard that enables factory-execution mode while alive.
    class DependsExecutionScope
    {
    public:
        DependsExecutionScope()
        {
            ++GetActiveState().execute_depends_depth;
        }

        ~DependsExecutionScope()
        {
            auto& depth = GetActiveState().execute_depends_depth;
            assert(depth > 0 && "DependsExecutionScope underflow.");
            if (depth > 0)
            {
                --depth;
            }
        }

        DependsExecutionScope(const DependsExecutionScope&) = delete;
        DependsExecutionScope& operator=(const DependsExecutionScope&) = delete;
        DependsExecutionScope(DependsExecutionScope&&) = delete;
        DependsExecutionScope& operator=(DependsExecutionScope&&) = delete;
    };

    inline bool ShouldExecuteDependsFactories()
    {
        return GetActiveState().execute_depends_depth > 0;
    }

    inline InjectContext& RootContext()
    {
        return GetActiveState().root;
    }

    inline std::vector<InjectContext*>& ContextStack()
    {
        return GetActiveState().context_stack;
    }

    inline InjectContext* CurrentContext()
    {
        auto& stack = ContextStack();
        assert(!stack.empty() && "Thread context stack should never be empty.");
        return stack.back();
    }

    // One pushed inject-call frame that may outlive current stack frame.
    class InjectContextLease
    {
    public:
        InjectContextLease(std::shared_ptr<InjectContextState> state, bool track_inject_call_depth)
            : state_(state ? std::move(state) : std::make_shared<InjectContextState>()),
            track_inject_call_depth_(track_inject_call_depth)
        {
            auto& stack = state_->context_stack;
            assert(!stack.empty() && "InjectContextState stack should never be empty.");
            local_.parent = stack.back();
            stack.push_back(&local_);
            if (track_inject_call_depth_)
            {
                ++state_->inject_call_depth;
            }
        }

        ~InjectContextLease()
        {
            if (!active_ || !state_)
            {
                return;
            }

            auto& stack = state_->context_stack;
            // Coroutine scheduling can make lease destruction order non-LIFO.
            // Remove this scope frame defensively wherever it currently lives
            // to avoid leaving dangling InjectContext* in stack.
            if (!stack.empty())
            {
                auto it = std::find(stack.begin(), stack.end(), &local_);
                if (it != stack.end())
                {
                    stack.erase(it);
                }
            }
            if (stack.empty())
            {
                stack.push_back(&state_->root);
            }
            if (track_inject_call_depth_)
            {
                auto& depth = state_->inject_call_depth;
                assert(depth > 0 && "InjectContextLease call depth underflow.");
                if (depth > 0)
                {
                    --depth;
                }
            }
            active_ = false;
        }

        InjectContextLease(const InjectContextLease&) = delete;
        InjectContextLease& operator=(const InjectContextLease&) = delete;

        InjectContextLease(InjectContextLease&& rhs) noexcept
            : state_(std::move(rhs.state_)),
            local_(std::move(rhs.local_)),
            track_inject_call_depth_(rhs.track_inject_call_depth_),
            active_(rhs.active_)
        {
            if (active_ && state_)
            {
                auto& stack = state_->context_stack;
                auto it = std::find(stack.begin(), stack.end(), &rhs.local_);
                if (it != stack.end())
                {
                    *it = &local_;
                }

                for (InjectContext* ctx : stack)
                {
                    if (ctx != nullptr && ctx->parent == &rhs.local_)
                    {
                        ctx->parent = &local_;
                    }
                }
            }
            rhs.active_ = false;
            rhs.local_.parent = nullptr;
        }

        InjectContextLease& operator=(InjectContextLease&& rhs) noexcept = delete;

        std::shared_ptr<InjectContextState> StateOwner() const
        {
            return state_;
        }

    private:
        std::shared_ptr<InjectContextState> state_{};
        InjectContext local_{};
        bool track_inject_call_depth_ = false;
        bool active_ = true;
    };

    // Heap handle for carrying one inject-call lease across async boundaries.
    //
    // Why a shared_ptr wrapper:
    // - InjectContextLease is move-only.
    // - Many task implementations prefer storing copyable handles.
    // - shared_ptr keeps semantics explicit: once last owner is gone, lease is released.
    using InjectContextLeaseHandle = std::shared_ptr<InjectContextLease>;

    inline InjectContextLeaseHandle MakeInjectContextLeaseHandle(InjectContextLease&& lease)
    {
        return std::make_shared<InjectContextLease>(std::move(lease));
    }

    // Runtime helper for task resume path:
    // activate state from bound lease for current thread, then rely on RAII to restore.
    //
    // Typical coroutine runtime usage:
    //   auto guard = depends::ActivateInjectStateFromLease(bound_lease_handle);
    //   resume_underlying_coroutine();
    inline ActiveInjectStateScope ActivateInjectStateFromLease(const InjectContextLeaseHandle& lease)
    {
        if (lease)
        {
            return ActiveInjectStateScope{ lease->StateOwner() };
        }
        return ActiveInjectStateScope{ GetActiveStateOwner() };
    }

    // Extract state owner from lease handle.
    // When lease is missing, fall back to current active state.
    inline std::shared_ptr<InjectContextState> InjectStateFromLease(const InjectContextLeaseHandle& lease)
    {
        if (lease)
        {
            return lease->StateOwner();
        }
        return GetActiveStateOwner();
    }

    inline std::shared_ptr<InjectContextState> AcquireReusableTopLevelInjectStateOwner()
    {
        // Reuse one isolated top-level state per thread to avoid allocating
        // a fresh state owner on every synchronous @inject call.
        static thread_local std::shared_ptr<InjectContextState> reusable =
            std::make_shared<InjectContextState>();

        if (!reusable || reusable.use_count() != 1)
        {
            return std::make_shared<InjectContextState>();
        }

        ResetInjectContextState(*reusable);
        return reusable;
    }

    inline std::shared_ptr<InjectContextState> AcquireInjectCallStateOwner()
    {
        auto state = GetActiveStateOwner();
        if (!state || state->inject_call_depth <= 0)
        {
            // Top-level @inject call (not inside another @inject scope):
            // use isolated state root so sibling requests do not share caches.
            state = AcquireReusableTopLevelInjectStateOwner();
        }
        return state;
    }

    inline InjectContextLease AcquireInjectCallLease()
    {
        auto state = AcquireInjectCallStateOwner();
        // Nested @inject call reuses existing state and pushes one child context frame.
        return InjectContextLease{ std::move(state), true };
    }

    // Backward-compatible scoped child context on current active state.
    class ContextScope
    {
    public:
        ContextScope()
            : lease_(GetActiveStateOwner(), false)
        {
        }

        ContextScope(const ContextScope&) = delete;
        ContextScope& operator=(const ContextScope&) = delete;
        ContextScope(ContextScope&&) = delete;
        ContextScope& operator=(ContextScope&&) = delete;

    private:
        InjectContextLease lease_;
    };

    inline std::shared_ptr<InjectContextState> CurrentInjectStateOwner()
    {
        return GetActiveStateOwner();
    }

    namespace detail
    {
        template <typename R>
        concept HasMemberBindInjectContext = requires(R & value, InjectContextLease && lease)
        {
            value.BindInjectContext(std::move(lease));
        };

        template <typename R>
        concept HasMemberBindInjectContextHandle = requires(R & value, InjectContextLeaseHandle lease)
        {
            value.BindInjectContext(std::move(lease));
        };

        template <typename R>
        concept HasMemberSetInjectContextHandle = requires(R & value, InjectContextLeaseHandle lease)
        {
            value.SetInjectContext(std::move(lease));
        };

        template <typename R>
        concept HasAdlBindInjectContext = requires(R && value, InjectContextLease && lease)
        {
            BindInjectContext(std::forward<R>(value), std::move(lease));
        };

        template <typename R>
        concept HasAdlBindInjectContextHandle = requires(R && value, InjectContextLeaseHandle lease)
        {
            BindInjectContext(std::forward<R>(value), std::move(lease));
        };

        template <typename R>
        concept HasAdlSetInjectContextHandle = requires(R && value, InjectContextLeaseHandle lease)
        {
            SetInjectContext(std::forward<R>(value), std::move(lease));
        };

        template <typename R>
        std::remove_cvref_t<R> AutoBindInjectContext(R&& value, InjectContextLease&& lease)
        {
            using V = std::remove_cvref_t<R>;
            static_assert(!std::is_reference_v<R>,
                "AutoBindInjectContext expects non-reference return types.");

            if constexpr (HasMemberBindInjectContext<V>)
            {
                V out = std::forward<R>(value);
                out.BindInjectContext(std::move(lease));
                return out;
            }
            else if constexpr (HasMemberBindInjectContextHandle<V>)
            {
                auto lease_handle = MakeInjectContextLeaseHandle(std::move(lease));
                V out = std::forward<R>(value);
                out.BindInjectContext(lease_handle);
                return out;
            }
            else if constexpr (HasMemberSetInjectContextHandle<V>)
            {
                auto lease_handle = MakeInjectContextLeaseHandle(std::move(lease));
                V out = std::forward<R>(value);
                out.SetInjectContext(lease_handle);
                return out;
            }
            else if constexpr (HasAdlBindInjectContext<R>)
            {
                return BindInjectContext(std::forward<R>(value), std::move(lease));
            }
            else if constexpr (HasAdlBindInjectContextHandle<R>)
            {
                auto lease_handle = MakeInjectContextLeaseHandle(std::move(lease));
                return BindInjectContext(std::forward<R>(value), lease_handle);
            }
            else if constexpr (HasAdlSetInjectContextHandle<R>)
            {
                auto lease_handle = MakeInjectContextLeaseHandle(std::move(lease));
                return SetInjectContext(std::forward<R>(value), lease_handle);
            }
            else
            {
                // No adapter found:
                // returning value as-is keeps backward compatibility for sync return types.
                // For coroutine return types, user should implement one supported adapter.
                (void)lease;
                return std::forward<R>(value);
            }
        }
    }

    [[nodiscard]] inline ContextSlot* FindSlotInChain(std::type_index key, const void* factory = nullptr)
    {
        for (InjectContext* ctx = CurrentContext(); ctx != nullptr; ctx = ctx->parent)
        {
            const auto it = ctx->slots.find(SlotKey{ key, factory });
            if (it != ctx->slots.end())
            {
                return &it->second;
            }
        }
        return nullptr;
    }

    inline ContextSlot& UpsertLocalSlot(std::type_index key, const void* factory, ContextSlot slot)
    {
        auto& out = CurrentContext()->slots[SlotKey{ key, factory }];
        out = std::move(slot);
        return out;
    }

    template <typename T>
    void CacheRawSlot(T* ptr, std::shared_ptr<void> holder, const void* factory = nullptr)
    {
        UpsertLocalSlot(typeid(T), factory, ContextSlot{
            const_cast<void*>(static_cast<const void*>(ptr)),
            std::move(holder) });
    }

    template <typename T>
    void CacheOwnedValue(T value, const void* factory = nullptr)
    {
        T* raw = new T(std::move(value));
        std::shared_ptr<void> holder{
            static_cast<void*>(raw),
            [](void* p) { delete static_cast<T*>(p); }
        };
        CacheRawSlot<T>(raw, std::move(holder), factory);
    }

    template <typename T>
    void CacheOwnedDefault(const void* factory = nullptr)
    {
        T* raw = new T{};
        std::shared_ptr<void> holder{
            static_cast<void*>(raw),
            [](void* p) { delete static_cast<T*>(p); }
        };
        CacheRawSlot<T>(raw, std::move(holder), factory);
    }

    template <typename T>
    void CacheBorrowedRaw(T* ptr, const void* factory = nullptr)
    {
        if (ptr == nullptr)
        {
            return;
        }
        CacheRawSlot<T>(ptr, {}, factory);
    }

    template <typename T>
    void CacheOwnedRaw(T* ptr, const void* factory = nullptr)
    {
        if (ptr == nullptr)
        {
            return;
        }
        std::shared_ptr<void> holder{
            const_cast<void*>(static_cast<const void*>(ptr)),
            [](void* p) { delete static_cast<T*>(p); }
        };
        CacheRawSlot<T>(ptr, std::move(holder), factory);
    }

    template <typename U>
    [[nodiscard]] std::optional<U> TryResolveExplicitValue(const void* target, const void* factory = nullptr)
    {
        auto value = FindExplicitValue(target, factory, typeid(U));
        if (!value)
        {
            return std::nullopt;
        }
        return AnyTo<U>(*value);
    }

    template <typename T>
    bool TryPopulateRawSlotFromExplicit(const void* target, const void* factory = nullptr)
    {
        // Explicit injection is borrowed-only:
        // 1) std::reference_wrapper<T>
        // 2) T*
        if (auto ref = TryResolveExplicitValue<std::reference_wrapper<T>>(target, factory))
        {
            CacheBorrowedRaw<T>(std::addressof(ref->get()), factory);
            return true;
        }

        if constexpr (!std::is_pointer_v<T>)
        {
            if (auto raw = TryResolveExplicitValue<T*>(target, factory); raw && *raw)
            {
                CacheBorrowedRaw<T>(*raw, factory);
                return true;
            }
        }

        return false;
    }
}

#endif // __CPPBM_DEPENDS_CONTEXT_H__
