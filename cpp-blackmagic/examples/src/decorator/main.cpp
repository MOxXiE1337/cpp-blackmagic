#include "cases.h"

#include <iostream>

int main()
{
    using namespace cppbm::examples::decorator;

    std::cout << "== Decorator Examples ==\n";
    RunDecoratorCaseBasic();
    RunDecoratorCaseFixedTarget();
    RunDecoratorCaseExpression();
    RunDecoratorCaseContext();
    RunDecoratorCaseChain();
    RunDecoratorCaseMember();
    return 0;
}
