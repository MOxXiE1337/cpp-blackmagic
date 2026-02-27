#include "cases.h"

#include <cppbm/decorator.h>

#include <cstdio>
#include <iostream>

namespace cppbm::examples::decorator
{
    using namespace cpp::blackmagic;

    int AddFixedTarget(int a, int b);

    class FixedTargetDecorator : public FunctionDecorator<&AddFixedTarget>
    {
    public:
        bool BeforeCall(int& a, int& b) override
        {
            std::printf("[fixed.before] ");
            // Demonstrate argument rewrite before original call.
            a += 1;
            b += 1;
            return true;
        }

        void AfterCall(int& result) override
        {
            std::printf("[fixed.after] ");
            // Demonstrate return value rewrite after original call.
            result += 100;
        }
    };

    inline FixedTargetDecorator fixed_target_decorator{};

    int AddFixedTarget(int a, int b)
    {
        return a + b;
    }

    void RunDecoratorCaseFixedTarget()
    {
        std::cout << "[fixed-target] AddFixedTarget(2, 3) => " << AddFixedTarget(2, 3) << "\n";
    }
}
