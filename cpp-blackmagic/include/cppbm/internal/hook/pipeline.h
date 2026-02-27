#ifndef __CPPBM_HOOK_PIPELINE_H__
#define __CPPBM_HOOK_PIPELINE_H__

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#include <malloc.h>
#define CPPBM_HOOK_ALLOCA _alloca
#else
#include <alloca.h>
#define CPPBM_HOOK_ALLOCA alloca
#endif

#include "node.h"
#include "state.h"

namespace cpp::blackmagic::hook
{
    template <typename R>
    R HookDefaultReturn()
    {
        if constexpr (std::is_void_v<R>)
        {
            return;
        }
        else if constexpr (std::is_reference_v<R>)
        {
            assert(false && "Hook default return failed: reference return has no default value.");
            std::terminate();
        }
        else if constexpr (std::is_default_constructible_v<R>)
        {
            return R{};
        }
        else
        {
            assert(false && "Hook default return failed: return type is not default-constructible.");
            std::terminate();
        }
    }

    // HookPipeline = one backend detour + one ordered decorator chain.
    //
    // Execution model:
    // 1) Iterate decorators in declaration order and call Before...
    // 2) If all returned true, invoke original once
    // 3) Walk invoked decorators in reverse order and call After...
    //
    // Context model:
    // - each decorator can request ContextSize()
    // - pipeline computes one per-node offset table
    // - each Dispatch allocates one contiguous arena (alloca)
    // - each invoked node receives a CallContext view for its slice
    //
    // Important behavior:
    // - nested calls are safe because each Dispatch has independent stack arena
    // - no heap allocation in context path
    template <typename OrigFn, typename R, typename... Args>
    class HookPipeline : private HookState<OrigFn>
    {
    public:
        using Core = HookState<OrigFn>;
        using Node = DecoratorNode<R, Args...>;

        struct DecoratorEntry
        {
            Node* node = nullptr;
            std::size_t offset = 0;
            std::size_t size = 0;
        };

        struct InvokedNodeState
        {
            Node* node = nullptr;
            CallContext ctx{};
        };

        HookPipeline(void* target, void* detour)
            : target_(target), detour_(detour)
        {
        }

        bool RegisterDecorator(Node* node)
        {
            if (node == nullptr)
            {
                return false;
            }

            {
                std::lock_guard<std::recursive_mutex> guard{ chain_mtx_ };
                const auto found = std::find_if(
                    chain_.begin(),
                    chain_.end(),
                    [node](const DecoratorEntry& e)
                    {
                        return e.node == node;
                    });
                if (found == chain_.end())
                {
                    DecoratorEntry entry{};
                    entry.node = node;
                    // Defer virtual ContextSize() query until layout rebuild.
                    // During base construction phase virtual dispatch is not stable.
                    entry.size = 0;
                    chain_.push_back(entry);
                    layout_dirty_ = true;
                }
            }

            if (Core::InstallAt(target_, detour_))
            {
                return true;
            }

            UnregisterDecorator(node);
            return false;
        }

        void UnregisterDecorator(Node* node)
        {
            if (node == nullptr)
            {
                return;
            }
            std::lock_guard<std::recursive_mutex> guard{ chain_mtx_ };
            const auto before = chain_.size();
            chain_.remove_if(
                [node](const DecoratorEntry& e)
                {
                    return e.node == node;
                });
            if (chain_.size() != before)
            {
                layout_dirty_ = true;
            }
        }

        [[nodiscard]] bool IsInstalled() const
        {
            return Core::IsInstalled();
        }

        R Dispatch(Args... args)
        {
            std::lock_guard<std::recursive_mutex> guard{ chain_mtx_ };
            if (chain_.empty())
            {
                return CallOriginal(args...);
            }

            RebuildLayoutIfNeeded();

            auto states = std::tuple<ArgStorageT<Args>...>{ InitArgStorage<Args>(args)... };
            auto slots = MakeSlots(states, std::index_sequence_for<Args...>{});
            std::vector<InvokedNodeState> invoked{};
            invoked.reserve(chain_.size());

            // One stack arena for this dispatch frame.
            unsigned char* arena = nullptr;
            if (arena_bytes_ > 0)
            {
                const std::size_t space = arena_bytes_ + alignof(std::max_align_t);
                void* aligned = CPPBM_HOOK_ALLOCA(space);
                arena = static_cast<unsigned char*>(aligned);
            }

            bool proceed = true;
            for (auto it = chain_.begin(); it != chain_.end(); ++it)
            {
                auto* node = it->node;
                if (node == nullptr)
                {
                    continue;
                }

                invoked.emplace_back();
                auto& state = invoked.back();
                state.node = node;
                void* slot_mem = (arena == nullptr || it->size == 0)
                    ? nullptr
                    : static_cast<void*>(arena + it->offset);
                state.ctx = CallContext(slot_mem, it->size);

                if (!InvokeBefore(node, state.ctx, slots))
                {
                    proceed = false;
                    break;
                }
            }

            if constexpr (std::is_void_v<R>)
            {
                if (proceed)
                {
                    CallOriginalFromStates(states, std::index_sequence_for<Args...>{});
                }

                for (auto it = invoked.rbegin(); it != invoked.rend(); ++it)
                {
                    if (it->node != nullptr)
                    {
                        it->node->AfterCallSlot(it->ctx);
                    }
                }
                return;
            }
            else
            {
                R result = proceed
                    ? CallOriginalFromStates(states, std::index_sequence_for<Args...>{})
                    : HookDefaultReturn<R>();

                for (auto it = invoked.rbegin(); it != invoked.rend(); ++it)
                {
                    if (it->node != nullptr)
                    {
                        it->node->AfterCallSlot(it->ctx, result);
                    }
                }
                return result;
            }
        }

        R CallOriginal(Args... args) const
        {
            OrigFn original = Core::Original();
            if (original == nullptr)
            {
                return HookDefaultReturn<R>();
            }
            return std::invoke(original, args...);
        }

    private:
        template <std::size_t... I>
        static auto MakeSlots(
            std::tuple<ArgStorageT<Args>...>& states,
            std::index_sequence<I...>)
        {
            return std::tuple<ArgSlot<Args>...>{ ArgSlot<Args>(std::get<I>(states))... };
        }

        static bool InvokeBefore(
            Node* node,
            CallContext& call_ctx,
            std::tuple<ArgSlot<Args>...>& slots)
        {
            return std::apply(
                [node, &call_ctx](auto&... unpacked_slots) -> bool
                {
                    return node->BeforeCallSlot(call_ctx, unpacked_slots...);
                },
                slots);
        }

        template <std::size_t... I>
        R CallOriginalFromStates(
            std::tuple<ArgStorageT<Args>...>& states,
            std::index_sequence<I...>) const
        {
            return CallOriginal(ForwardCallArg<Args>(std::get<I>(states))...);
        }

        static std::size_t AlignUp(std::size_t value, std::size_t align)
        {
            if (align <= 1)
            {
                return value;
            }
            const std::size_t rem = value % align;
            return rem == 0 ? value : value + (align - rem);
        }

        void RebuildLayoutIfNeeded()
        {
            if (!layout_dirty_)
            {
                return;
            }

            std::size_t offset = 0;
            for (auto& entry : chain_)
            {
                entry.size = (entry.node == nullptr) ? 0 : entry.node->ContextSize();
                if (entry.size == 0)
                {
                    entry.offset = 0;
                    continue;
                }
                entry.offset = AlignUp(offset, alignof(std::max_align_t));
                offset = entry.offset + entry.size;
            }
            arena_bytes_ = offset;
            layout_dirty_ = false;
        }

    private:
        void* target_ = nullptr;
        void* detour_ = nullptr;
        mutable std::recursive_mutex chain_mtx_{};
        std::list<DecoratorEntry> chain_{};
        std::size_t arena_bytes_ = 0;
        bool layout_dirty_ = false;
    };
}

#endif // __CPPBM_HOOK_PIPELINE_H__
