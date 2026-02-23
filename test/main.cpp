#include "test.h"
#include "inlinehook.h"

decorator(@logger)
int add(int a, int b)
{
	return a + b;
}

decorator(test::@once)
void print()
{
	std::cout << "This function will only run once!" << std::endl;
}

int main()
{
	// Will print args with @logger decorator
	int result = add(0x114514, 0xDEADBEEF);

	// Will only run once with @once decorator
	print();
	print();
	print();

	MemberFunctionDecorator t{};

	TestClass cls;
	cls.Add(123, 456);

	// Inline hook test
	// If you use AutoDecorator, you don't need to pass in the func address
	MessageBoxAHooker hooker{};

	MessageBoxA(NULL, "HELLO WORLD!", "TESTA", MB_OK);

	return 0;
}
