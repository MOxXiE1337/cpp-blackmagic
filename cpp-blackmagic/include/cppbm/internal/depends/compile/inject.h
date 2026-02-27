// File role:
// Compile-time/bind-time inject registration utilities.
//
// Responsibilities:
// - consume generated InjectArgMeta<...> objects at Bind<&Target>(...)
// - choose sync/async metadata registration by target return type
// - expose default binder object `inject`

#ifndef __CPPBM_DEPENDS_COMPILE_INJECT_H__
#define __CPPBM_DEPENDS_COMPILE_INJECT_H__

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "meta.h"
#include "../runtime/inject.h"

namespace cpp::blackmagic::depends
{
    // Lightweight data carrier for one @inject default-arg metadata entry.
    //
    // Generated code shape:
    //   InjectArgMeta<Index, Param>([] { return Depends(...); })
    //
    // This object stores sync/async metadata registration closures.
    // InjectBinder::Bind decides which one to invoke according to target
    // return type (Task-returning function => async metadata).
    template <std::size_t Index, typename Param>
    class InjectArgMeta
    {
    public:
        static constexpr std::size_t kIndex = Index;
        using ParamType = Param;

        InjectArgMeta() = delete;

        template <typename Factory>
        explicit InjectArgMeta(Factory&& factory)
        {
            using FactoryT = RemoveCvRefT<Factory>;
            auto holder = std::make_shared<FactoryT>(std::forward<Factory>(factory));

            register_sync_at_ = [holder](const void* target) -> bool {
                return InjectRegistry::RegisterAt<Index>(target, [holder]() mutable {
                    return MakeDefaultArgMetadata<Param>((*holder)());
                    });
            };

            register_async_at_ = [holder](const void* target) -> bool {
                return InjectRegistry::RegisterAt<Index>(target, [holder]() mutable {
                    return MakeDefaultArgMetadataAsync<Param>((*holder)());
                    });
            };
        }

        bool RegisterSyncAt(const void* target) const
        {
            if (!register_sync_at_) return false;
            return register_sync_at_(target);
        }

        bool RegisterAsyncAt(const void* target) const
        {
            if (!register_async_at_) return false;
            return register_async_at_(target);
        }

    private:
        std::function<bool(const void*)> register_sync_at_{};
        std::function<bool(const void*)> register_async_at_{};
    };

    template <typename T>
    struct IsInjectArgMeta : std::false_type
    {
    };

    template <std::size_t Index, typename Param>
    struct IsInjectArgMeta<InjectArgMeta<Index, Param>> : std::true_type
    {
    };
}

namespace cpp::blackmagic::depends::detail
{
    template <typename T>
    struct FunctionSignatureTraits;

    template <typename R, typename... Args>
    struct FunctionSignatureTraits<R(*)(Args...)>
    {
        using ReturnType = R;
    };

    template <typename C, typename R, typename... Args>
    struct FunctionSignatureTraits<R(C::*)(Args...)>
    {
        using ReturnType = R;
    };

    template <typename C, typename R, typename... Args>
    struct FunctionSignatureTraits<R(C::*)(Args...) const>
    {
        using ReturnType = R;
    };

    template <auto Target, std::size_t Index, typename Param>
    bool ApplyMeta(const InjectArgMeta<Index, Param>& meta)
    {
        using FnTraits = FunctionSignatureTraits<decltype(Target)>;
        constexpr bool kUseAsyncMetadata =
            IsTaskReturn<typename FnTraits::ReturnType>::value;

        if constexpr (kUseAsyncMetadata)
        {
            return meta.RegisterAsyncAt(TargetKeyOf<Target>());
        }
        else
        {
            return meta.RegisterSyncAt(TargetKeyOf<Target>());
        }
    }

    template <auto Target, typename Meta>
    bool ApplyMeta(Meta&&)
    {
        // Unknown metadata is intentionally ignored by InjectBinder.
        return true;
    }
}

namespace cpp::blackmagic
{
    class InjectBinder
    {
    public:
        template <auto Target, typename... Metas>
        auto Bind(Metas&&... metas) const
        {
            if constexpr (sizeof...(Metas) > 0)
            {
                const bool applied_all = (
                    depends::detail::ApplyMeta<Target>(
                        std::forward<Metas>(metas)
                    ) && ...
                );
                (void)applied_all;
            }

            return DecoratorBinding<Target, InjectDecorator>{};
        }
    };

    // Default binder object used by preprocess-generated code:
    //   (inject).Bind<&Target>(...)
    inline constexpr InjectBinder inject{};
}

#endif // __CPPBM_DEPENDS_COMPILE_INJECT_H__
