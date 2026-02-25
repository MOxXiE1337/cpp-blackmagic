#include <cppbm/depends.h>

#include <iostream>
#include <string>

using namespace cpp::blackmagic;

struct Config
{
    std::string env = "prod";
    int timeout_ms = 3000;
};

Config& DefaultConfigFactory()
{
    static Config cfg{ "prod", 3000 };
    return cfg;
}

decorator(@inject)
std::string ReadEnv(Config& cfg = Depends(DefaultConfigFactory))
{
    return cfg.env;
}

decorator(@inject)
int ReadTimeout(Config* cfg = Depends(DefaultConfigFactory))
{
    return cfg != nullptr ? cfg->timeout_ms : -1;
}

int main()
{
    (void)ClearDependencies();

    std::cout << "[inject] default env => " << ReadEnv() << "\n";
    std::cout << "[inject] default timeout => " << ReadTimeout() << "\n";

    Config target_only{ "staging", 1500 };
    {
        auto guard = ScopeOverrideDependency<&ReadEnv>(&target_only, DefaultConfigFactory);
        std::cout << "[inject] target override env => " << ReadEnv() << "\n";
        std::cout << "[inject] timeout (unchanged target) => " << ReadTimeout() << "\n";
    }

    Config global_override{ "local", 1000 };
    {
        auto guard = ScopeOverrideDependency(&global_override, DefaultConfigFactory);
        std::cout << "[inject] global override env => " << ReadEnv() << "\n";
        std::cout << "[inject] global override timeout => " << ReadTimeout() << "\n";
    }

    std::cout << "[inject] restored env => " << ReadEnv() << "\n";
    return 0;
}
