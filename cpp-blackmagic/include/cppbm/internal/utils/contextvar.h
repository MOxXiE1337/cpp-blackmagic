// File role:
// Lightweight context-variable primitive (Python contextvars style).
//
// Design goals:
// 1) Keep API tiny and header-only.
// 2) Provide token-based restore semantics for nested overrides.
// 3) Stay implementation-agnostic: this class only models "current value".
//    It does not know anything about coroutine schedulers.
//
// Usage model:
// - In plain synchronous code, ContextVar naturally behaves like thread_local state.
// - In coroutine code, framework/runtime can restore the variable around resume points.

#ifndef __CPPBM_UTILS_CONTEXTVAR_H__
#define __CPPBM_UTILS_CONTEXTVAR_H__

#include <optional>
#include <unordered_map>
#include <utility>

namespace cpp::blackmagic::utils
{
    template <typename T>
    class ContextVar
    {
    public:
        class Token
        {
        public:
            Token() = default;

            ~Token()
            {
                Restore();
            }

            Token(const Token&) = delete;
            Token& operator=(const Token&) = delete;

            Token(Token&& rhs) noexcept
                : owner_(rhs.owner_),
                previous_(std::move(rhs.previous_)),
                had_previous_(rhs.had_previous_),
                active_(rhs.active_)
            {
                rhs.owner_ = nullptr;
                rhs.active_ = false;
            }

            Token& operator=(Token&& rhs) noexcept
            {
                if (this != &rhs)
                {
                    Restore();
                    owner_ = rhs.owner_;
                    previous_ = std::move(rhs.previous_);
                    had_previous_ = rhs.had_previous_;
                    active_ = rhs.active_;
                    rhs.owner_ = nullptr;
                    rhs.active_ = false;
                }
                return *this;
            }

            // Disable automatic restore.
            void Release() noexcept
            {
                active_ = false;
            }

            // Restore previous value immediately.
            void Restore() noexcept
            {
                if (!active_ || owner_ == nullptr)
                {
                    return;
                }
                owner_->RestoreFromToken(*this);
                active_ = false;
            }

        private:
            friend class ContextVar<T>;

            Token(ContextVar<T>* owner, std::optional<T> previous, bool had_previous)
                : owner_(owner),
                previous_(std::move(previous)),
                had_previous_(had_previous),
                active_(true)
            {
            }

            ContextVar<T>* owner_ = nullptr;
            std::optional<T> previous_{};
            bool had_previous_ = false;
            bool active_ = false;
        };

        using Value = T;

        // Returns current logical value if set for this execution context.
        std::optional<T> Get() const
        {
            auto it = slots_.find(this);
            if (it == slots_.end())
            {
                return std::nullopt;
            }
            return it->second;
        }

        bool HasValue() const
        {
            auto it = slots_.find(this);
            return it != slots_.end() && it->second.has_value();
        }

        // Set current value and return token that can restore previous value.
        Token Set(T value)
        {
            auto& slot = slots_[this];
            const bool had_previous = slot.has_value();
            std::optional<T> previous = slot;
            slot = std::move(value);
            return Token{ this, std::move(previous), had_previous };
        }

        // Clear current value for this execution context.
        void Clear()
        {
            slots_.erase(this);
        }

    private:
        // Important:
        // slots_ is keyed by ContextVar instance address, so multiple ContextVar<T>
        // objects do not overwrite each other's values even with same T.
        static inline thread_local std::unordered_map<const ContextVar<T>*, std::optional<T>> slots_{};

        void RestoreFromToken(const Token& token) noexcept
        {
            if (token.had_previous_)
            {
                slots_[this] = token.previous_;
            }
            else
            {
                slots_.erase(this);
            }
        }
    };
}

#endif // __CPPBM_UTILS_CONTEXTVAR_H__
