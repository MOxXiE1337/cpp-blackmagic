#ifndef __CPPBM_DEPENDS_H__
#define __CPPBM_DEPENDS_H__

#include <tuple>
#include <utility>

// File role:
// Public dependency-injection API surface and @inject decorator binding.
//
// Key responsibilities:
// - Depends(...) placeholder and factory APIs
// - explicit injection/override registry APIs
// - runtime argument resolution for @inject wrappers
// - coroutine-aware invoke path for Task-returning targets

#include "decorator.h"
#include "task.h"
#include "internal/depends/runtime/error.h"
#include "internal/depends/compile/meta.h"
#include "internal/depends/runtime/placeholder.h"
#include "internal/depends/runtime/context.h"

namespace cpp::blackmagic
{
    template <typename T>
    inline constexpr bool kIsSupportedDependencyHandleV =
        std::is_pointer_v<std::remove_cvref_t<T>>
        || depends::IsReferenceWrapper<std::remove_cvref_t<T>>::value;

    namespace depends
    {
        template <typename>
        inline constexpr bool kAlwaysFalseV = false;

        // Return-type trait for coroutine-aware @inject and async metadata path.
        template <typename T>
        struct IsTaskReturn : std::false_type
        {
        };

        template <typename T>
        struct IsTaskReturn<Task<T>> : std::true_type
        {
            using ValueType = T;
        };
    }

    // Public Depends() entry:
    // returns a marker object used as a default argument expression.
    //
    // Behavior depends on execution phase:
    // - Normal call-site default expression: returns placeholder marker.
    // - @inject default-metadata evaluation: resolves real dependency.
    // cached:
    // - true  => may reuse a previously resolved slot in context chain
    // - false => force fresh resolve (factory/default-construct) into current slot
    inline depends::DependsMaker<> Depends(bool cached = true)
    {
        return depends::DependsMaker<>{ nullptr, cached };
    }

    // Depends(factory) entry:
    // factory must be no-arg and return:
    // - pointer/reference directly, or
    // - task-like object with Get() whose final result is pointer/reference.
    // The returned maker is converted later according to declared parameter type.
    template <typename T>
    constexpr depends::DependsMaker<T> Depends(T(*factory)(), bool cached = true)
    {
        static_assert(depends::kIsSupportedFactoryReturnV<T>,
            "Depends(factory): factory return type must be pointer/reference "
            "or task-like with Get() resolving to pointer/reference.");
        return { factory, cached };
    }

    namespace depends::detail
    {
        // Convert one factory result to target dependency type under coroutine context.
        // - direct pointer/reference results: convert immediately
        // - Task-like results: co_await and then convert
        template <typename To, typename From>
        Task<To> ConvertFactoryResultAsync(From&& from)
        {
            using FromValue = std::remove_cvref_t<From>;
            if constexpr (IsTaskReturn<FromValue>::value)
            {
                auto task = std::forward<From>(from);
                if constexpr (std::is_void_v<typename IsTaskReturn<FromValue>::ValueType>)
                {
                    co_await task;
                    static_assert(!std::is_same_v<To, To>,
                        "Depends(factory): Task<void> cannot be converted to dependency value.");
                }
                else
                {
                    co_return ConvertFactoryResult<To>(co_await task);
                }
            }
            else
            {
                co_return ConvertFactoryResult<To>(std::forward<From>(from));
            }
        }

        // Build async metadata for Depends(factory) path.
        template <typename Param, typename Meta, typename FactoryReturn>
        Task<Meta> BuildAsyncMetaFromFactory(FactoryReturn(*factory)(), bool cached)
        {
            if (factory == nullptr)
            {
                co_return FailInject<Meta>(InjectError{
                    InjectErrorCode::InvalidPlaceholder,
                    nullptr,
                    static_cast<std::size_t>(-1),
                    typeid(FactoryReturn),
                    nullptr,
                    "Depends(factory) metadata build failed: factory pointer is null."
                    });
            }

            using Raw = DependsRawFromParamT<Param>;
            decltype(auto) produced = InvokeFactory(factory);
            DependsPtrValue<Raw> out{};
            out.ptr = co_await ConvertFactoryResultAsync<Raw*>(
                std::forward<decltype(produced)>(produced));
            out.owned = kFactoryProducesPointerV<FactoryReturn>;
            out.factory = FactoryKeyOf(factory);
            out.cached = cached;
            co_return static_cast<Meta>(out);
        }
    }

    namespace depends
    {
        // Async metadata builder used by inject.py-generated registration.
        //
        // Why this exists:
        // - sync metadata path may call Get() for task-like factories
        // - async @inject parameter pipeline should await task-like factories instead
        //
        // Supported expression policy:
        // - Depends(factory): async path, await factory result if needed
        // - Depends(): build metadata from DependsMaker in coroutine return path
        // - non-Depends expressions are intentionally rejected
        template <typename Param, typename Expr>
        Task<DefaultArgMetadataTypeT<Param, Expr&&>> MakeDefaultArgMetadataAsync(Expr expr)
        {
            using E = RemoveCvRefT<Expr>;
            using Meta = DefaultArgMetadataTypeT<Param, Expr&&>;

            if constexpr (IsDependsFactoryMaker<E>::value)
            {
                // Important coroutine lifetime rule:
                // this coroutine starts suspended (initial_suspend == suspend_always),
                // so forwarding-reference parameters can dangle if caller passes a temporary.
                // Accept Expr by value and move here to keep maker stable in coroutine frame.
                E maker = std::move(expr);
                co_return co_await detail::BuildAsyncMetaFromFactory<Param, Meta>(
                    maker.factory,
                    maker.cached);
            }
            else if constexpr (IsDependsMaker<E>::value)
            {
                // Keep a concrete maker value to avoid conversion-probe surprises
                // from DependsMaker conversion operators in template deduction.
                E maker = std::move(expr);
                co_return static_cast<Meta>(MakeDefaultArgMetadata<Param>(std::move(maker)));
            }
            else
            {
                static_assert(
                    kAlwaysFalseV<E>,
                    "MakeDefaultArgMetadataAsync only accepts Depends() or Depends(factory) expressions.");
            }
        }

    }

    // Shared implementation for target-scoped and global injection APIs.
    //
    // Explicit injection policy:
    // - accepted: T* or std::reference_wrapper<T>
    // - treated as borrowed only
    template <typename T>
    bool InjectDependencyAt(const void* target, const void* factory, T&& value)
    {
        using U = std::remove_cvref_t<T>;
        constexpr bool kSupportedHandle =
            std::is_pointer_v<U>
            || depends::IsReferenceWrapper<U>::value;
        static_assert(kSupportedHandle,
            "InjectDependency(value) only accepts pointer/reference-wrapper values.");

        U captured = std::forward<T>(value);
        return depends::RegisterExplicitValue<U>(target, factory, std::move(captured));
    }

    template <typename T>
    bool InjectDependencyAt(const void* target, T&& value)
    {
        return InjectDependencyAt(target, nullptr, std::forward<T>(value));
    }

    template <typename T, typename FactoryReturn>
    bool InjectDependencyAt(const void* target, T&& value, FactoryReturn(*factory)())
    {
        static_assert(depends::kIsSupportedFactoryReturnV<FactoryReturn>,
            "InjectDependency(value, factory): factory return type must be pointer/reference "
            "or task-like with Get() resolving to pointer/reference.");
        return InjectDependencyAt(target, depends::FactoryKeyOf(factory), std::forward<T>(value));
    }

    // Target-scoped explicit injection:
    // applies only when resolver target matches the specified function.
    template <auto Target, typename T>
    bool InjectDependency(T&& value)
    {
        return InjectDependencyAt(depends::TargetKeyOf<Target>(), std::forward<T>(value));
    }

    // Target-scoped explicit injection bound to Depends(factory).
    // Only matches parameters declared with the same factory function pointer.
    template <auto Target, typename T, typename FactoryReturn>
    bool InjectDependency(T&& value, FactoryReturn(*factory)())
    {
        return InjectDependencyAt(depends::TargetKeyOf<Target>(), std::forward<T>(value), factory);
    }

    // Global explicit injection fallback (target == nullptr).
	template <typename T>
	bool InjectDependency(T&& value)
	{
		return InjectDependencyAt(nullptr, std::forward<T>(value));
	}

    // Global explicit injection bound to Depends(factory).
    template <typename T, typename FactoryReturn>
    bool InjectDependency(T&& value, FactoryReturn(*factory)())
    {
        return InjectDependencyAt(nullptr, std::forward<T>(value), factory);
    }

    // Clear all explicit injected values (all targets/factories/types).
    inline std::size_t ClearDependencies()
    {
        return depends::ClearExplicitValues();
    }

    // Clear explicit injected values for one target only.
    template <auto Target>
    std::size_t ClearDependencies()
    {
        return depends::ClearExplicitValuesForTarget(depends::TargetKeyOf<Target>());
    }

    // Remove one explicit injected value by exact key.
    inline bool RemoveDependencyAt(const void* target, const void* factory, std::type_index type)
    {
        return depends::RemoveExplicitValue(target, factory, type);
    }

    template <typename T>
    bool RemoveDependency()
    {
        using U = std::remove_cvref_t<T>;
        static_assert(kIsSupportedDependencyHandleV<U>,
            "RemoveDependency<T> requires T to be pointer or std::reference_wrapper.");
        return depends::RemoveExplicitValueTyped<U>(nullptr, nullptr);
    }

    template <typename T, typename FactoryReturn>
    bool RemoveDependency(FactoryReturn(*factory)())
    {
        using U = std::remove_cvref_t<T>;
        static_assert(kIsSupportedDependencyHandleV<U>,
            "RemoveDependency<T>(factory) requires T to be pointer or std::reference_wrapper.");
        static_assert(depends::kIsSupportedFactoryReturnV<FactoryReturn>,
            "RemoveDependency<T>(factory): factory return type must be pointer/reference "
            "or task-like with Get() resolving to pointer/reference.");
        return depends::RemoveExplicitValueTyped<U>(nullptr, depends::FactoryKeyOf(factory));
    }

    template <auto Target, typename T>
    bool RemoveDependency()
    {
        using U = std::remove_cvref_t<T>;
        static_assert(kIsSupportedDependencyHandleV<U>,
            "RemoveDependency<Target, T> requires T to be pointer or std::reference_wrapper.");
        return depends::RemoveExplicitValueTyped<U>(depends::TargetKeyOf<Target>(), nullptr);
    }

    template <auto Target, typename T, typename FactoryReturn>
    bool RemoveDependency(FactoryReturn(*factory)())
    {
        using U = std::remove_cvref_t<T>;
        static_assert(kIsSupportedDependencyHandleV<U>,
            "RemoveDependency<Target, T>(factory) requires T to be pointer or std::reference_wrapper.");
        static_assert(depends::kIsSupportedFactoryReturnV<FactoryReturn>,
            "RemoveDependency<Target, T>(factory): factory return type must be pointer/reference "
            "or task-like with Get() resolving to pointer/reference.");
        return depends::RemoveExplicitValueTyped<U>(
            depends::TargetKeyOf<Target>(),
            depends::FactoryKeyOf(factory));
    }

    template <typename U>
    class ScopedDependencyOverride
    {
    public:
        ScopedDependencyOverride(const void* target, const void* factory, U value)
            : target_(target), factory_(factory), had_previous_(false), active_(true)
        {
            static_assert(kIsSupportedDependencyHandleV<U>,
                "ScopedDependencyOverride<U> requires U to be pointer or std::reference_wrapper.");
            if (auto old = depends::FindExplicitValueExactTyped<U>(target_, factory_))
            {
                previous_ = std::move(*old);
                had_previous_ = true;
            }
            (void)depends::RegisterExplicitValue<U>(target_, factory_, std::move(value));
        }

        ~ScopedDependencyOverride()
        {
            Restore();
        }

        ScopedDependencyOverride(const ScopedDependencyOverride&) = delete;
        ScopedDependencyOverride& operator=(const ScopedDependencyOverride&) = delete;

        ScopedDependencyOverride(ScopedDependencyOverride&& rhs) noexcept
            : target_(rhs.target_),
            factory_(rhs.factory_),
            had_previous_(rhs.had_previous_),
            previous_(std::move(rhs.previous_)),
            active_(rhs.active_)
        {
            rhs.active_ = false;
        }

        ScopedDependencyOverride& operator=(ScopedDependencyOverride&& rhs) noexcept
        {
            if (this != &rhs)
            {
                Restore();
                target_ = rhs.target_;
                factory_ = rhs.factory_;
                had_previous_ = rhs.had_previous_;
                previous_ = std::move(rhs.previous_);
                active_ = rhs.active_;
                rhs.active_ = false;
            }
            return *this;
        }

    private:
        void Restore() noexcept
        {
            if (!active_)
            {
                return;
            }
            if (had_previous_ && previous_.has_value())
            {
                (void)depends::RegisterExplicitValue<U>(target_, factory_, std::move(*previous_));
            }
            else
            {
                (void)depends::RemoveExplicitValueTyped<U>(target_, factory_);
            }
            active_ = false;
        }

        const void* target_ = nullptr;
        const void* factory_ = nullptr;
        bool had_previous_ = false;
        std::optional<U> previous_{};
        bool active_ = false;
    };

    template <typename T>
    auto ScopeOverrideDependency(T&& value)
    {
        using U = std::remove_cvref_t<T>;
        static_assert(kIsSupportedDependencyHandleV<U>,
            "ScopeOverrideDependency(value) requires pointer/reference-wrapper value.");
        return ScopedDependencyOverride<U>{ nullptr, nullptr, std::forward<T>(value) };
    }

    template <typename T, typename FactoryReturn>
    auto ScopeOverrideDependency(T&& value, FactoryReturn(*factory)())
    {
        using U = std::remove_cvref_t<T>;
        static_assert(kIsSupportedDependencyHandleV<U>,
            "ScopeOverrideDependency(value, factory) requires pointer/reference-wrapper value.");
        static_assert(depends::kIsSupportedFactoryReturnV<FactoryReturn>,
            "ScopeOverrideDependency(value, factory): factory return type must be pointer/reference "
            "or task-like with Get() resolving to pointer/reference.");
        return ScopedDependencyOverride<U>{ nullptr, depends::FactoryKeyOf(factory), std::forward<T>(value) };
    }

    template <auto Target, typename T>
    auto ScopeOverrideDependency(T&& value)
    {
        using U = std::remove_cvref_t<T>;
        static_assert(kIsSupportedDependencyHandleV<U>,
            "ScopeOverrideDependency<Target>(value) requires pointer/reference-wrapper value.");
        return ScopedDependencyOverride<U>{ depends::TargetKeyOf<Target>(), nullptr, std::forward<T>(value) };
    }

    template <auto Target, typename T, typename FactoryReturn>
    auto ScopeOverrideDependency(T&& value, FactoryReturn(*factory)())
    {
        using U = std::remove_cvref_t<T>;
        static_assert(kIsSupportedDependencyHandleV<U>,
            "ScopeOverrideDependency<Target>(value, factory) requires pointer/reference-wrapper value.");
        static_assert(depends::kIsSupportedFactoryReturnV<FactoryReturn>,
            "ScopeOverrideDependency<Target>(value, factory): factory return type must be pointer/reference "
            "or task-like with Get() resolving to pointer/reference.");
        return ScopedDependencyOverride<U>{
            depends::TargetKeyOf<Target>(),
            depends::FactoryKeyOf(factory),
            std::forward<T>(value) };
    }
}


#include "internal/depends/compile/inject.h"

#endif // __CPPBM_DEPENDS_H__
