#include <cppbm/decorator.h>

#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

using namespace cpp::blackmagic;

template <auto Target>
class LoggerDecorator;

template <typename R, typename... Args, R(*Target)(Args...)>
class LoggerDecorator<Target> : public FunctionDecorator<Target>
{
public:
    R Call(Args... args) override
    {
        std::printf("HELLO! ");
        if constexpr (std::is_void_v<R>)
        {
            this->CallOriginal(args...);
            return;
        }
        else
        {
            return this->CallOriginal(args...);
        }
    }
};

CPPBM_DECORATOR_BINDER(LoggerDecorator, logger);

decorator(@logger)
int add(int a, int b)
{
    return a + b;
}

int main()
{
    std::cout << "[decorator] add(2, 3) => " << add(2, 3) << "\n";
    return 0;
}
