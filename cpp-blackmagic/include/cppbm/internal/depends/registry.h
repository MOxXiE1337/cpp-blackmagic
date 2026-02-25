// File role:
// This header hosts process-wide registries used by dependency injection.
//
// There are two registries:
// 1) Explicit value registry:
//    Source: InjectDependency(...)
//    Key:    (target function pointer or nullptr, factory key or nullptr, requested type)
//    Value:  one injected value for that key (last registration wins)
//
// 2) Default-argument metadata registry:
//    Source: generated metadata from inject.py
//    Key:    (target function pointer, parameter index, registered metadata type)
//    Value:  one factory for that key
//
// NOTE:
// - Explicit injection can optionally bind to a factory key.
// - Factory keys are expected to come from Depends(factory) default expressions.

// ExplicitValueRegistry: value injected by InjectDependency
// DefaultArgRegistry: value injected by Depends


#ifndef __CPPBM_DEPENDS_REGISTRY_H__
#define __CPPBM_DEPENDS_REGISTRY_H__

#include <any>
#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cpp::blackmagic::depends
{
    // Pointer metadata payload generated for Depends(...) default arguments.
    // - ptr:   resolved raw pointer for the dependency object
    // - owned: true  => context takes ownership and deletes at scope end
    //          false => borrowed pointer, no deletion by context
    template <typename T>
    struct DependsPtrValue
    {
        T* ptr = nullptr;
        bool owned = false;
        // Optional factory key used for explicit-injection lookup.
        // nullptr means plain Depends() without factory.
        const void* factory = nullptr;
        // Whether resolver may reuse an existing slot for this dependency.
        // false means force fresh resolve and write into current context slot.
        bool cached = true;
    };

    // Erased callable for default-arg metadata table.
    using ErasedFactory = std::function<std::any()>;

    // Key for explicit injected values.
    // "target == nullptr" means global fallback value for all targets.
    struct ExplicitValueKey
    {
        const void* target = nullptr;
        const void* factory = nullptr;
        std::type_index type = typeid(void);

        bool operator==(const ExplicitValueKey& rhs) const
        {
            return target == rhs.target
                && factory == rhs.factory
                && type == rhs.type;
        }
    };

    struct ExplicitValueKeyHash
    {
        std::size_t operator()(const ExplicitValueKey& key) const
        {
            const auto h1 = std::hash<const void*>{}(key.target);
            const auto h2 = std::hash<const void*>{}(key.factory);
            const auto h3 = std::hash<std::type_index>{}(key.type);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    // Key for generated default-argument metadata.
    // "index" is the parameter index in target function signature.
    struct DefaultArgKey
    {
        const void* target = nullptr;
        std::size_t index = 0;
        std::type_index type = typeid(void);

        bool operator==(const DefaultArgKey& rhs) const
        {
            return target == rhs.target && index == rhs.index && type == rhs.type;
        }
    };

    struct DefaultArgKeyHash
    {
        std::size_t operator()(const DefaultArgKey& key) const
        {
            const auto h1 = std::hash<const void*>{}(key.target);
            const auto h2 = std::hash<std::size_t>{}(key.index);
            const auto h3 = std::hash<std::type_index>{}(key.type);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    // Explicit value registry:
    // wraps table + mutex in one object so lock discipline is internal.
    class ExplicitValueRegistry
    {
    public:
        using StoredAny = std::shared_ptr<const std::any>;

        bool Register(const void* target, const void* factory, std::type_index type, std::any value)
        {
            auto stored = std::make_shared<const std::any>(std::move(value));
            std::unique_lock<std::shared_mutex> lock{ mtx_ };
            table_[ExplicitValueKey{ target, factory, type }] = std::move(stored);
            return true;
        }

        [[nodiscard]] std::optional<StoredAny> Find(
            const void* target,
            const void* factory,
            std::type_index type) const
        {
            std::shared_lock<std::shared_mutex> lock{ mtx_ };

            const auto fetch_at = [&](const void* key_target, const void* key_factory) -> std::optional<StoredAny>
                {
                    auto it = table_.find(ExplicitValueKey{ key_target, key_factory, type });
                    if (it == table_.end())
                    {
                        return std::nullopt;
                    }
                    return it->second;
                };

            // Lookup policy:
            // - factory == nullptr: target-nullptr-factory, then global-nullptr-factory.
            // - factory != nullptr: target-exact-factory, then global-exact-factory.
            //   No fallback to nullptr-factory to avoid overriding Depends(factory)
            //   by plain InjectDependency(value).
            if (auto hit = fetch_at(target, factory))
            {
                return hit;
            }

            if (target != nullptr)
            {
                if (auto fallback = fetch_at(nullptr, factory))
                {
                    return fallback;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<StoredAny> FindExact(
            const void* target,
            const void* factory,
            std::type_index type) const
        {
            std::shared_lock<std::shared_mutex> lock{ mtx_ };
            auto it = table_.find(ExplicitValueKey{ target, factory, type });
            if (it == table_.end())
            {
                return std::nullopt;
            }
            return it->second;
        }

        bool Remove(const void* target, const void* factory, std::type_index type)
        {
            std::unique_lock<std::shared_mutex> lock{ mtx_ };
            return table_.erase(ExplicitValueKey{ target, factory, type }) > 0;
        }

        std::size_t Clear()
        {
            std::unique_lock<std::shared_mutex> lock{ mtx_ };
            const std::size_t removed = table_.size();
            table_.clear();
            return removed;
        }

        std::size_t ClearForTarget(const void* target)
        {
            std::unique_lock<std::shared_mutex> lock{ mtx_ };
            std::size_t removed = 0;
            for (auto it = table_.begin(); it != table_.end(); )
            {
                if (it->first.target == target)
                {
                    it = table_.erase(it);
                    ++removed;
                }
                else
                {
                    ++it;
                }
            }
            return removed;
        }

    private:
        mutable std::shared_mutex mtx_{};
        std::unordered_map<ExplicitValueKey, StoredAny, ExplicitValueKeyHash> table_{};
    };

    // Default-argument metadata registry:
    // one metadata factory per (target,index,type) key.
    class DefaultArgRegistry
    {
    public:
        using StoredFactory = std::shared_ptr<const ErasedFactory>;

        bool Register(const void* target, std::size_t index, std::type_index type, ErasedFactory factory)
        {
            auto stored = std::make_shared<const ErasedFactory>(std::move(factory));
            std::unique_lock<std::shared_mutex> lock{ mtx_ };
            table_[DefaultArgKey{ target, index, type }] = std::move(stored);
            return true;
        }

        [[nodiscard]] std::optional<StoredFactory> Find(
            const void* target,
            std::size_t index,
            std::type_index type) const
        {
            std::shared_lock<std::shared_mutex> lock{ mtx_ };
            auto it = table_.find(DefaultArgKey{ target, index, type });
            if (it == table_.end())
            {
                return std::nullopt;
            }
            return it->second;
        }

    private:
        mutable std::shared_mutex mtx_{};
        std::unordered_map<DefaultArgKey, StoredFactory, DefaultArgKeyHash> table_{};
    };

    inline ExplicitValueRegistry& GetExplicitValueRegistry()
    {
        static ExplicitValueRegistry registry{};
        return registry;
    }

    inline DefaultArgRegistry& GetDefaultArgRegistry()
    {
        static DefaultArgRegistry registry{};
        return registry;
    }

    // Lossless std::any extraction helper:
    // returns nullopt when dynamic type mismatch.
    template <typename U>
    std::optional<U> AnyTo(const std::any* value)
    {
        if (value == nullptr || !value->has_value())
        {
            return std::nullopt;
        }
        if (const auto* typed = std::any_cast<U>(value))
        {
            if constexpr (std::is_copy_constructible_v<U>)
            {
                return *typed;
            }
            else
            {
                // Const any cannot move out a move-only payload.
                return std::nullopt;
            }
        }
        if (const auto* boxed = std::any_cast<std::shared_ptr<U>>(value))
        {
            if (boxed->get() == nullptr)
            {
                return std::nullopt;
            }
            if constexpr (std::is_copy_constructible_v<U>)
            {
                return **boxed;
            }
            else
            {
                // Stored as shared_ptr<U> for move-only registration path.
                // Const extraction still cannot consume ownership.
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    template <typename U>
    std::optional<U> AnyTo(std::any* value)
    {
        if (value == nullptr || !value->has_value())
        {
            return std::nullopt;
        }
        // Non-const any path supports move-only payloads (e.g. Task<T> metadata).
        // Caller controls value lifetime and should pass rvalue any when moving out.
        if (auto* typed = std::any_cast<U>(value))
        {
            return std::optional<U>(std::move(*typed));
        }
        // Move-only metadata can be boxed as shared_ptr<U> in std::any
        // because std::any requires copy-constructible stored types.
        if (auto* boxed = std::any_cast<std::shared_ptr<U>>(value))
        {
            if (boxed->get() == nullptr)
            {
                return std::nullopt;
            }
            return std::optional<U>(std::move(**boxed));
        }
        return std::nullopt;
    }

    template <typename U>
    std::optional<U> AnyTo(const std::any& value)
    {
        return AnyTo<U>(std::addressof(value));
    }

    template <typename U>
    std::optional<U> AnyTo(std::any&& value)
    {
        return AnyTo<U>(std::addressof(value));
    }

    template <typename U>
    std::optional<U> AnyTo(const ExplicitValueRegistry::StoredAny& value)
    {
        if (!value)
        {
            return std::nullopt;
        }
        return AnyTo<U>(value.get());
    }

    inline std::optional<ExplicitValueRegistry::StoredAny> FindExplicitValue(
        const void* target,
        const void* factory,
        std::type_index type)
    {
        // Lookup policy:
        // - factory == nullptr: target + nullptr-factory, then global + nullptr-factory.
        // - factory != nullptr: target + exact-factory, then global + exact-factory.
        return GetExplicitValueRegistry().Find(target, factory, type);
    }

    inline std::optional<ExplicitValueRegistry::StoredAny> FindExplicitValueExact(
        const void* target,
        const void* factory,
        std::type_index type)
    {
        return GetExplicitValueRegistry().FindExact(target, factory, type);
    }

    inline std::optional<DefaultArgRegistry::StoredFactory> FindDefaultArgFactory(
        const void* target,
        std::size_t index,
        std::type_index type)
    {
        // Exact-key lookup only; no global fallback here.
        return GetDefaultArgRegistry().Find(target, index, type);
    }

    // Function identity key for factory matching.
    //
    // Why not reinterpret_cast function pointer to void*:
    // - conversion from function pointer to object pointer is not portable.
    // - instead we hash stable byte representation + function-pointer type.
    struct FactoryIdentityKey
    {
        std::type_index signature = typeid(void);
        std::vector<unsigned char> bytes{};

        bool operator==(const FactoryIdentityKey& rhs) const
        {
            return signature == rhs.signature && bytes == rhs.bytes;
        }
    };

    struct FactoryIdentityKeyHash
    {
        std::size_t operator()(const FactoryIdentityKey& key) const
        {
            std::size_t h = std::hash<std::type_index>{}(key.signature);
            for (unsigned char b : key.bytes)
            {
                h ^= static_cast<std::size_t>(b) + 0x9e3779b9u + (h << 6) + (h >> 2);
            }
            return h;
        }
    };

    class FactoryKeyRegistry
    {
    public:
        const void* GetOrCreate(
            std::type_index signature,
            const unsigned char* bytes,
            std::size_t size)
        {
            FactoryIdentityKey key{ signature, std::vector<unsigned char>(bytes, bytes + size) };

            {
                std::shared_lock<std::shared_mutex> read_lock{ mtx_ };
                if (auto it = table_.find(key); it != table_.end())
                {
                    return it->second.get();
                }
            }

            std::unique_lock<std::shared_mutex> write_lock{ mtx_ };
            if (auto it = table_.find(key); it != table_.end())
            {
                return it->second.get();
            }

            auto token = std::make_unique<unsigned char>(0);
            const void* out = token.get();
            table_.emplace(std::move(key), std::move(token));
            return out;
        }

    private:
        std::shared_mutex mtx_{};
        std::unordered_map<FactoryIdentityKey, std::unique_ptr<unsigned char>, FactoryIdentityKeyHash> table_{};
    };

    inline FactoryKeyRegistry& GetFactoryKeyRegistry()
    {
        static FactoryKeyRegistry registry{};
        return registry;
    }

    inline const void* FactoryKeyOf(std::nullptr_t)
    {
        return nullptr;
    }

    template <typename R>
    inline const void* FactoryKeyOf(R(*factory)())
    {
        if (factory == nullptr)
        {
            return nullptr;
        }

        std::array<unsigned char, sizeof(factory)> bytes{};
        std::memcpy(bytes.data(), &factory, bytes.size());
        return GetFactoryKeyRegistry().GetOrCreate(typeid(R(*)()), bytes.data(), bytes.size());
    }

    template <auto Target>
    inline const void* TargetKeyOf()
    {
        static int token = 0;
        return &token;
    }

    template <typename U>
    bool RegisterExplicitValue(const void* target, const void* factory, U&& value)
    {
        return GetExplicitValueRegistry().Register(
            target,
            factory,
            typeid(std::remove_cvref_t<U>),
            std::any(std::forward<U>(value)));
    }

    inline std::size_t ClearExplicitValues()
    {
        return GetExplicitValueRegistry().Clear();
    }

    inline std::size_t ClearExplicitValuesForTarget(const void* target)
    {
        return GetExplicitValueRegistry().ClearForTarget(target);
    }

    inline bool RemoveExplicitValue(const void* target, const void* factory, std::type_index type)
    {
        return GetExplicitValueRegistry().Remove(target, factory, type);
    }

    template <typename U>
    std::optional<U> FindExplicitValueExactTyped(const void* target, const void* factory)
    {
        auto value = FindExplicitValueExact(target, factory, typeid(U));
        if (!value)
        {
            return std::nullopt;
        }
        return AnyTo<U>(*value);
    }

    template <typename U>
    bool RemoveExplicitValueTyped(const void* target, const void* factory)
    {
        return RemoveExplicitValue(target, factory, typeid(U));
    }

    struct InjectRegistry
    {
        // Register generated metadata factory with deduced metadata type.
        template <auto Target, std::size_t Index, typename Factory>
        static bool Register(Factory&& factory)
        {
            using FactoryT = std::remove_reference_t<Factory>;
            using U = std::remove_cvref_t<decltype(std::declval<FactoryT&>()())>;
            return Register<Target, Index, U>(std::forward<Factory>(factory));
        }

        // Register generated metadata factory for one default parameter:
        // key = (Target function, parameter Index, metadata type U).
        template <auto Target, std::size_t Index, typename U, typename Factory>
        static bool Register(Factory&& factory)
        {
            static_assert(!std::is_reference_v<U>,
                "InjectRegistry::Register requires non-reference value type.");

            ErasedFactory erased = [fac = std::forward<Factory>(factory)]() mutable -> std::any {
                U produced = static_cast<U>(fac());
                if constexpr (std::is_copy_constructible_v<U>)
                {
                    return std::any(std::move(produced));
                }
                else
                {
                    // std::any cannot hold move-only U directly.
                    // Box it into shared_ptr<U> and let AnyTo<U>(std::any*) unbox by move.
                    return std::any(std::make_shared<U>(std::move(produced)));
                }
                };

            // Metadata factories are single-entry per key.
            // Last registration wins for identical key.
            return GetDefaultArgRegistry().Register(
                TargetKeyOf<Target>(),
                Index,
                typeid(U),
                std::move(erased));
        }

        // Resolve generated metadata by (target,index,type).
        template <typename U>
        static std::optional<U> Resolve(const void* target, std::size_t index)
        {
            auto erased = FindDefaultArgFactory(target, index, typeid(U));
            if (!erased)
            {
                return std::nullopt;
            }
            auto value = (*(*erased))();
            return AnyTo<U>(std::move(value));
        }
    };
}

#endif // __CPPBM_DEPENDS_REGISTRY_H__
