#include "cases.h"

#include <cppbm/decorator.h>

#include <cstdio>
#include <iostream>

namespace cppbm::examples::decorator
{
    using namespace cpp::blackmagic;

    template <auto Target>
    class MemberTraceDecorator;

    template <typename C, typename R, typename... Args, R(C::*Target)(Args...)>
    class MemberTraceDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        bool BeforeCall(C*& /*thiz*/, Args&... /*args*/) override
        {
            std::printf("[member.before] ");
            return true;
        }

        void AfterCall(R& /*result*/) override
        {
            std::printf("[member.after] ");
        }
    };

    CPPBM_DECORATOR_BINDER(MemberTraceDecorator, member_logger);

    class Counter
    {
    public:
        decorator(@member_logger)
        int Add(int delta);

    private:
        int value_ = 10;
    };

    int Counter::Add(int delta)
    {
        value_ += delta;
        return value_;
    }

    void RunDecoratorCaseMember()
    {
        Counter c{};
        std::cout << "[member] Counter::Add(5) => " << c.Add(5) << "\n";
    }
}
