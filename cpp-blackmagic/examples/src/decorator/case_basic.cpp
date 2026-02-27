#include "cases.h"

#include <cppbm/decorator.h>

#include <cstdio>
#include <iostream>

namespace cppbm::examples::decorator
{
    using namespace cpp::blackmagic;

    template <auto Target>
    class BasicLoggerDecorator;

    template <typename R, typename... Args, R(*Target)(Args...)>
    class BasicLoggerDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        bool BeforeCall(Args&... /*args*/) override
        {
            std::printf("[basic.before] ");
            return true;
        }

        void AfterCall(R& /*result*/) override
        {
            std::printf("[basic.after] ");
        }
    };

    CPPBM_DECORATOR_BINDER(BasicLoggerDecorator, basic_logger);

    decorator(@basic_logger)
    int AddBasic(int a, int b);

    int AddBasic(int a, int b)
    {
        return a + b;
    }

    void RunDecoratorCaseBasic()
    {
        std::cout << "[basic] AddBasic(2, 3) => " << AddBasic(2, 3) << "\n";
    }
}
