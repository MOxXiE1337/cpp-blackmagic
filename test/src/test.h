#include <iostream>
#include <cppbm/decorator.h>

template <typename... Args>
void print_args(Args&&... args) {
    bool is_first = true;
    (
        [&]() {
            if (!is_first) {
                std::cout << ", ";  
            }

            std::cout << std::forward<Args>(args);
            is_first = false;
        }(), 
            ...   
            );
    std::cout << std::endl; 
}

// Ez logger decorator, can decorate generic functions
template <auto Target>
class decorator_class(logger);

template <typename R, typename... Args, R(*Target)(Args...)>
class decorator_class(logger)<Target> : public cpp::blackmagic::FunctionDecorator<Target>
{
public:
	bool BeforeCall(Args... args) override
	{
		std::cout << "LoggerDecorator: " << std::hex << std::uppercase;
		print_args(args...);
		return true;
	}
};

