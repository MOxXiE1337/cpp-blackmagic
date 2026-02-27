#ifndef __CPPBM_HOOK_HOOK_H__
#define __CPPBM_HOOK_HOOK_H__

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
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

#include "arg_slot.h"
#include "error.h"
#include "hooker.h"
#include "registry.h"
#include "../utils/noncopyable.h"

#ifdef _WIN32
#ifndef _WIN64
#define _CPPBM_HOOK_WIN32
#endif // _WIN64
#endif // _WIN32

namespace cpp::blackmagic::hook
{
    // Convert a member-function pointer into a code address for MinHook.
    // Requires the member pointer representation to fit one machine pointer.
    template <typename MemFn>
        requires std::is_member_function_pointer_v<std::remove_cvref_t<MemFn>>
    inline void* MemberPointerToAddress(MemFn fn)
    {
        void* addr = nullptr;
        std::memcpy(&addr, &fn, sizeof(addr));
        return addr;
    }

    template <typename R, typename ThisPtr, typename... Args>
    struct MemberOriginalFnT
    {
#ifdef _CPPBM_HOOK_WIN32
        using type = R(__thiscall*)(ThisPtr, Args...);
#else
        using type = R(*)(ThisPtr, Args...);
#endif
    };

    template <typename R, typename ThisPtr, typename... Args>
    using MemberOriginalFn = typename MemberOriginalFnT<R, ThisPtr, Args...>::type;

    class CallContext
    {
    public:
        CallContext() = default;

        CallContext(void* memory, std::size_t bytes)
            : memory_(memory),
            bytes_(bytes)
        {
        }

        [[nodiscard]] void* Data() const
        {
            return memory_;
        }

        [[nodiscard]] std::size_t Size() const
        {
            return bytes_;
        }

        template <typename T>
        [[nodiscard]] T* As() const
        {
            if (memory_ == nullptr)
            {
                return nullptr;
            }
            if (sizeof(T) > bytes_)
            {
                return nullptr;
            }
            if (reinterpret_cast<std::uintptr_t>(memory_) % alignof(T) != 0)
            {
                return nullptr;
            }
            return reinterpret_cast<T*>(memory_);
        }

    private:
        void* memory_ = nullptr;
        std::size_t bytes_ = 0;
    };

    template <typename R, typename... Args>
    class DecoratorNode
    {
    public:
        virtual ~DecoratorNode() = default;

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

    template <typename OrigFn>
    class HookState : private utils::NonCopyable
    {
    public:
        bool InstallAt(void* target, void* detour)
        {
            std::lock_guard<std::mutex> guard{ mtx_ };
            if (installed_.load(std::memory_order_acquire))
            {
                return true;
            }

            ClearLastHookError();
            if (target == nullptr || detour == nullptr)
            {
                return HandleHookFailure(HookError{
                    HookErrorCode::InvalidInstallArgument,
                    target,
                    detour,
                    "Hook install failed: target/detour cannot be null."
                    });
            }

            Hooker& hooker = Hooker::GetInstance();
            OrigFn original = nullptr;
            if (!hooker.CreateHook(target, detour, reinterpret_cast<void**>(&original)))
            {
                return HandleHookFailure(HookError{
                    HookErrorCode::CreateHookFailed,
                    target,
                    detour,
                    "Hook install failed: backend CreateHook() returned false."
                    });
            }

            if (!hooker.EnableHook(target))
            {
                hooker.RemoveHook(target);
                return HandleHookFailure(HookError{
                    HookErrorCode::EnableHookFailed,
                    target,
                    detour,
                    "Hook install failed: backend EnableHook() returned false."
                    });
            }

            original_.store(original, std::memory_order_release);
            installed_.store(true, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool IsInstalled() const
        {
            return installed_.load(std::memory_order_acquire);
        }

    protected:
        OrigFn Original() const
        {
            return original_.load(std::memory_order_acquire);
        }

    private:
        std::atomic<OrigFn> original_{ nullptr };
        std::atomic_bool installed_{ false };
        mutable std::mutex mtx_{};
    };

    // One backend hook + one decorator chain per target.
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
                    // Do not query virtual context size in base constructor phase.
                    // Actual size is refreshed in RebuildLayoutIfNeeded().
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

            // CallContext mem space allocation
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

                if (invoked.empty())
                {
                    return;
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

                if (invoked.empty())
                {
                    return result;
                }

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
                return;

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

    template <typename Derived, typename Node>
    class HookPipelineNodeBase : public Node, private utils::NonCopyable
    {
    protected:
        bool RegisterDecoratorNode()
        {
            return Derived::GetPipeline().RegisterDecorator(static_cast<Node*>(this));
        }

        void UnregisterDecoratorNode()
        {
            Derived::GetPipeline().UnregisterDecorator(static_cast<Node*>(this));
        }
    };

    template <auto Target, typename R, typename... Args>
    class FreeHookBase
        : public HookPipelineNodeBase<FreeHookBase<Target, R, Args...>, DecoratorNode<R, Args...>>
    {
    public:
        using Fn = R(*)(Args...);
        using Pipeline = HookPipeline<Fn, R, Args...>;

    protected:
        static R CallOriginal(Args... args)
        {
            return GetPipeline().CallOriginal(std::forward<Args>(args)...);
        }

    public:
        static Pipeline& GetPipeline()
        {
            return GetOrCreateHookPipeline<Pipeline>(
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(&Detour));
        }

    private:
        static R Detour(Args... args)
        {
            return GetPipeline().Dispatch(args...);
        }
    };

    template <auto Target, typename MemberFn, typename ThisPtr, typename R, typename... Args>
    class MemberHookBase
        : public HookPipelineNodeBase<
        MemberHookBase<Target, MemberFn, ThisPtr, R, Args...>,
        DecoratorNode<R, ThisPtr, Args...>>
    {
    public:
        using OrigFn = MemberOriginalFn<R, ThisPtr, Args...>;
        using Pipeline = HookPipeline<OrigFn, R, ThisPtr, Args...>;

    protected:
        static R CallOriginal(ThisPtr thiz, Args... args)
        {
            return GetPipeline().CallOriginal(
                std::forward<ThisPtr>(thiz),
                std::forward<Args>(args)...);
        }

    public:
        static Pipeline& GetPipeline()
        {
            return GetOrCreateHookPipeline<Pipeline>(
                MemberPointerToAddress(Target),
                MemberPointerToAddress(Target),
                reinterpret_cast<void*>(&Detour));
        }

    private:
#ifdef _CPPBM_HOOK_WIN32
        static R __fastcall Detour(ThisPtr thiz, void* /*edx*/, Args... args)
        {
            return GetPipeline().Dispatch(thiz, args...);
        }
#else
        static R Detour(ThisPtr thiz, Args... args)
        {
            return GetPipeline().Dispatch(thiz, args...);
        }
#endif
    };

#ifdef _CPPBM_HOOK_WIN32
    template <auto Target, typename R, typename... Args>
    class FreeHookBaseStdcall
        : public HookPipelineNodeBase<FreeHookBaseStdcall<Target, R, Args...>, DecoratorNode<R, Args...>>
    {
    public:
        using Fn = R(__stdcall*)(Args...);
        using Pipeline = HookPipeline<Fn, R, Args...>;

    protected:
        static R CallOriginal(Args... args)
        {
            return GetPipeline().CallOriginal(std::forward<Args>(args)...);
        }

    public:
        static Pipeline& GetPipeline()
        {
            return GetOrCreateHookPipeline<Pipeline>(
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(&Detour));
        }

    private:
        static R __stdcall Detour(Args... args)
        {
            return GetPipeline().Dispatch(args...);
        }
    };

    template <auto Target, typename R, typename... Args>
    class FreeHookBaseFastcall
        : public HookPipelineNodeBase<FreeHookBaseFastcall<Target, R, Args...>, DecoratorNode<R, Args...>>
    {
    public:
        using Fn = R(__fastcall*)(Args...);
        using Pipeline = HookPipeline<Fn, R, Args...>;

    protected:
        static R CallOriginal(Args... args)
        {
            return GetPipeline().CallOriginal(std::forward<Args>(args)...);
        }

    public:
        static Pipeline& GetPipeline()
        {
            return GetOrCreateHookPipeline<Pipeline>(
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(Target),
                reinterpret_cast<void*>(&Detour));
        }

    private:
        static R __fastcall Detour(Args... args)
        {
            return GetPipeline().Dispatch(args...);
        }
    };
#endif // _CPPBM_HOOK_WIN32
}

#endif // __CPPBM_HOOK_HOOK_H__
