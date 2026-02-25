#include "test.h"
#include <cppbm/depends.h>

using namespace cpp::blackmagic;

struct Config
{
	const char* path = "test!";

	~Config()
	{
		std::printf("%s Destroy: %p\n", path, this);
	}
};

decorator(@logger) 
int add(int a, int b)
{
	return a + b;
}

Config& get_config_borrowed()
{
	static Config instance{"BORROWED"};
	return instance;
}

Config* get_config_owned()
{
	return new Config{"OWNED"};
}

void middle();

decorator(@inject)
void test0(Config& cf0 = Depends(), Config* cf1 = Depends(get_config_borrowed))
{
	std::printf("TEST0\n");
	std::printf("cf0 %p: %s\n", &cf0, cf0.path);
	std::printf("cf1 %p: %s\n", cf1, cf1->path);

	middle();
}

decorator(@inject)
void test1(Config* cf0 = Depends(false),  Config& cf2 = Depends(get_config_owned), Config& cf1 = Depends(get_config_borrowed))
{
	std::printf("TEST1\n");
	std::printf("cf0 %p: %s\n", cf0, cf0->path);
	std::printf("cf1 %p: %s\n", &cf1, cf1.path);
	std::printf("cf2 %p: %s\n", &cf2, cf2.path);
}

class Test
{
public:
	decorator(@inject)
	void print(Config& cf = Depends(get_config_owned));
	
};

void Test::print(Config& cf)
{
	std::printf("Test::print: %s\n", cf.path);
}

void middle()
{
	test1();
}

int main()
{
	add(0x114514, 0xDEADBEEF);

	{
		Config config{"INJECTED"};
		auto guard = ScopeOverrideDependency<&test0>(&config);
		test0();
	}
	

	Test test;
	test.print();
	return 0;
}
