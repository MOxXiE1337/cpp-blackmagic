#include <cppbm/depends.h>

#include <iostream>
#include <string>
#include <chrono>

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

// benchmark
std::vector<long long> prime_factorization(long long n) {
    std::vector<long long> factors;
    
    while (n % 2 == 0) {
        factors.push_back(2);
        n /= 2;
    }
    
    for (long long i = 3; i * i <= n; i += 2) {
        while (n % i == 0) {
            factors.push_back(i);
            n /= i;
        }
    }
   
    if (n > 2) {
        factors.push_back(n);
    }
    return factors;
}

decorator(@inject)
void benchmark(long long n, Config* cfg = Depends())
{
    prime_factorization(n);
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

    auto beg = std::chrono::high_resolution_clock::now();
    prime_factorization(1000000000000000000LL);
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Bench1: " << end - beg << std::endl;

     {
        Config test;
        auto guard = ScopeOverrideDependency<&benchmark>(&test);
        beg = std::chrono::high_resolution_clock::now();
        benchmark(1000000000000000000LL);
        end = std::chrono::high_resolution_clock::now();
        std::cout << "Bench2: " << end - beg << std::endl;
    }
   

    
    return 0;
}
