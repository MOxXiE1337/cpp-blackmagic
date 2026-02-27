#ifndef __CPPBM_HOOK_REGISTRY_H__
#define __CPPBM_HOOK_REGISTRY_H__

#include <mutex>
#include <unordered_map>
#include <utility>

namespace cpp::blackmagic::hook
{
    class HookPipelineRegistry
    {
    public:
        static HookPipelineRegistry& GetInstance()
        {
            static HookPipelineRegistry registry{};
            return registry;
        }

        template <typename PipelineT, typename... CtorArgs>
        PipelineT& GetOrCreate(const void* target, CtorArgs&&... ctor_args)
        {
            std::lock_guard<std::mutex> guard{ mtx_ };
            auto it = pipelines_.find(target);
            if (it != pipelines_.end())
            {
                return *static_cast<PipelineT*>(it->second);
            }

            // Leak-on-exit by design:
            // pipelines are process-lifetime singletons keyed by target address.
            auto* created = new PipelineT(std::forward<CtorArgs>(ctor_args)...);
            pipelines_.emplace(target, created);
            return *created;
        }

    private:
        std::mutex mtx_{};
        std::unordered_map<const void*, void*> pipelines_{};
    };

    template <typename PipelineT, typename... CtorArgs>
    PipelineT& GetOrCreateHookPipeline(const void* target, CtorArgs&&... ctor_args)
    {
        return HookPipelineRegistry::GetInstance().GetOrCreate<PipelineT>(
            target,
            std::forward<CtorArgs>(ctor_args)...);
    }
}

#endif // __CPPBM_HOOK_REGISTRY_H__
