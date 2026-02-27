#ifndef __CPPBM_HOOK_CALL_CONTEXT_H__
#define __CPPBM_HOOK_CALL_CONTEXT_H__

#include <cstddef>
#include <cstdint>

namespace cpp::blackmagic::hook
{
    // Lightweight view of per-decorator context storage.
    //
    // Important:
    // - CallContext does NOT own memory.
    // - Lifetime is managed by HookPipeline dispatch frame.
    // - Decorator code decides how to construct/destroy data inside this slot.
    //
    // Typical pattern in decorator:
    //   auto* frame = ctx.As<MyFrame>();
    //   std::construct_at(frame, ...);   // in BeforeCall
    //   std::destroy_at(frame);          // in AfterCall
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
            // Return nullptr when:
            // 1) this decorator has ContextSize()==0
            // 2) requested T does not fit in the reserved slot
            // 3) slot alignment is incompatible with T
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
}

#endif // __CPPBM_HOOK_CALL_CONTEXT_H__
