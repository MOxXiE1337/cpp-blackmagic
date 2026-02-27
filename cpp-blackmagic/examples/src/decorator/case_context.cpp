#include "cases.h"

#include <cppbm/decorator.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>

namespace cppbm::examples::decorator
{
    using namespace cpp::blackmagic;

    template <auto Target>
    class TimingDecorator;

    template <typename R, typename... Args, R(*Target)(Args...)>
    class TimingDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        struct Frame
        {
            std::chrono::steady_clock::time_point start{};
        };

        std::size_t ContextSize() const override
        {
            return sizeof(Frame);
        }

        bool BeforeCall(hook::CallContext& ctx, Args&... /*args*/) override
        {
            auto* frame = ctx.As<Frame>();
            if (frame == nullptr)
            {
                return false;
            }
            std::construct_at(frame, Frame{ std::chrono::steady_clock::now() });
            return true;
        }

        void AfterCall(hook::CallContext& ctx, R& /*result*/) override
        {
            auto* frame = ctx.As<Frame>();
            if (frame == nullptr)
            {
                return;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - frame->start).count();
            std::destroy_at(frame);
            std::printf("[context.elapsed=%lldns] ", static_cast<long long>(elapsed));
        }
    };

    CPPBM_DECORATOR_BINDER(TimingDecorator, timing);

    decorator(@timing)
    int Multiply(int a, int b);

    int Multiply(int a, int b)
    {
        return a * b;
    }

    void RunDecoratorCaseContext()
    {
        std::cout << "[context] Multiply(6, 7) => " << Multiply(6, 7) << "\n";
    }
}
