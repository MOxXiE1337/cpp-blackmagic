#include "test.h"
#include <map>
#include <cppbm/depends.h>

using namespace cpp::blackmagic;


decorator(@inject)
void test0(std::map<std::string, int>& map = Depends())
{
	for(auto& [key, val] : map)
	{
		std::printf("%s: %d\n", key.c_str(), val);
	}

}


int main()
{

	{
		std::map<std::string, int> test;
		test["TEST"] = 114514;
		auto guard = ScopeOverrideDependency<&test0>(&test);
		test0();
	}
	
	return 0;
}
