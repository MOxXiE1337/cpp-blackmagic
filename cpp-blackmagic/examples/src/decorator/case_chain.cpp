#include "cases.h"

#include <cppbm/decorator.h>

#include <cstdio>
#include <iostream>

namespace cppbm::examples::decorator
{
    using namespace cpp::blackmagic;

    template <auto Target>
    class FirstChainDecorator;

    template <typename R, typename... Args, R(*Target)(Args...)>
    class FirstChainDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        bool BeforeCall(Args&... /*args*/) override
        {
            std::printf("[first.before] ");
            return true;
        }

        void AfterCall(R& /*result*/) override
        {
            std::printf("[first.after] ");
        }
    };

    template <auto Target>
    class SecondChainDecorator;

    template <typename R, typename... Args, R(*Target)(Args...)>
    class SecondChainDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        bool BeforeCall(Args&... /*args*/) override
        {
            std::printf("[second.before] ");
            return true;
        }

        void AfterCall(R& /*result*/) override
        {
            std::printf("[second.after] ");
        }
    };

    CPPBM_DECORATOR_BINDER(FirstChainDecorator, first_chain);
    CPPBM_DECORATOR_BINDER(SecondChainDecorator, second_chain);

    decorator(@first_chain, @second_chain)
    int AddChain(int a, int b);

    int AddChain(int a, int b)
    {
        return a + b;
    }

    void RunDecoratorCaseChain()
    {
        std::cout << "[chain] AddChain(5, 8) => " << AddChain(5, 8) << "\n";
    }
}
