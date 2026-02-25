// File role:
// Runtime context/state and slot cache primitives for DI resolution.
//
// This layer does not decide high-level policy of parameter resolution;
// it only provides:
// - thread-local context stack
// - slot structures and storage helpers
// - low-level explicit-injection lookup and caching

#ifndef __CPPBM_DEPENDS_CONTEXT_H__
#define __CPPBM_DEPENDS_CONTEXT_H__

#include <cassert>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "meta.h"
#include "registry.h"

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

    struct ThreadInjectState
    {
        // Root node for current thread.
        InjectContext root{};

        // Active context stack (back() is current context).
        std::vector<InjectContext*> context_stack{};

        // Nesting depth for "execute Depends factories now" mode.
        int execute_depends_depth = 0;

        ThreadInjectState()
        {
            context_stack.push_back(&root);
        }
    };

    inline ThreadInjectState& GetThreadInjectState()
    {
        static thread_local ThreadInjectState state{};
        return state;
    }

    // Scope guard that enables factory-execution mode while alive.
    class DependsExecutionScope
    {
    public:
        DependsExecutionScope()
        {
            ++GetThreadInjectState().execute_depends_depth;
        }

        ~DependsExecutionScope()
        {
            auto& depth = GetThreadInjectState().execute_depends_depth;
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
        return GetThreadInjectState().execute_depends_depth > 0;
    }

    inline InjectContext& RootContext()
    {
        return GetThreadInjectState().root;
    }

    inline std::vector<InjectContext*>& ContextStack()
    {
        return GetThreadInjectState().context_stack;
    }

    inline InjectContext* CurrentContext()
    {
        auto& stack = ContextStack();
        assert(!stack.empty() && "Thread context stack should never be empty.");
        return stack.back();
    }

    // Push child context on enter, pop on exit.
    class ContextScope
    {
    public:
        ContextScope()
        {
            local_.parent = CurrentContext();
            ContextStack().push_back(&local_);
        }

        ~ContextScope()
        {
            auto& stack = ContextStack();
            assert(!stack.empty() && stack.back() == &local_
                && "ContextScope stack mismatch: destroying non-top scope.");
            if (!stack.empty() && stack.back() == &local_)
            {
                stack.pop_back();
            }
            if (stack.empty())
            {
                stack.push_back(&RootContext());
            }
        }

        ContextScope(const ContextScope&) = delete;
        ContextScope& operator=(const ContextScope&) = delete;
        ContextScope(ContextScope&&) = delete;
        ContextScope& operator=(ContextScope&&) = delete;

    private:
        InjectContext local_{};
    };

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
