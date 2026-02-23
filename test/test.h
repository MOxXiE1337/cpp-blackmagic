#include <iostream>
#include <type_traits>
#include <utility>
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
class decorator_class(logger)<Target> : public cpp::blackmagic::AutoDecorator<Target>
{
public:
	bool BeforeCall(Args... args) override
	{
		std::cout << "LoggerDecorator: " << std::hex << std::uppercase;
		print_args(args...);
		return true;
	}
};


// Namespaced decorator
// Decorated funciton will only run once
namespace test
{
	template <auto Target>
	class decorator_class(once);

	template <typename R, typename... Args, R(*Target)(Args...)>
	class decorator_class(once) < Target > : public cpp::blackmagic::AutoDecorator<Target>
	{
	public:
		bool BeforeCall(Args... args) override
		{
			if (flag_)
				return false;
			return true;
		}

		void AfterCall() override
		{
			flag_ = true;
		}
	private:
		bool flag_ = false;
	};
}





class TestClass
{
public:
	int Add(int a, int b)
	{
		return a + b;
	}
};

// Member function decorator
class MemberFunctionDecorator : public cpp::blackmagic::AutoDecorator<&TestClass::Add>
{
public:
	bool BeforeCall(TestClass* thiz, int a, int b) override
	{
		std::printf("MemberFunctionDecorator::Add: %d + %d\n", a, b);
		return true;
	}
};
