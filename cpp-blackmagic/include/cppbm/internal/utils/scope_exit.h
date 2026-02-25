#ifndef __CPPBM_UTILS_SCOPE_EXIT_H__
#define __CPPBM_UTILS_SCOPE_EXIT_H__

#include <utility>

namespace cpp::blackmagic::utils
{
    template <typename F>
    class ScopeExit
    {
    public:
        explicit ScopeExit(F f) : f_(std::move(f)) {}
        ~ScopeExit()
        {
            if (active_)
                f_();
        }

        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;

        ScopeExit(ScopeExit&& other) noexcept
            : f_(std::move(other.f_)), active_(other.active_)
        {
            other.active_ = false;
        }

        void Release() noexcept { active_ = false; }

    private:
        F f_;
        bool active_ = true;
    };

    template <typename F>
    ScopeExit(F) -> ScopeExit<F>;
}

#endif // __CPPBM_UTILS_SCOPE_EXIT_H__
