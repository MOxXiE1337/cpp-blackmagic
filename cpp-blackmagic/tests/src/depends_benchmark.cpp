#include <cppbm/depends.h>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using namespace cpp::blackmagic;

namespace
{
    // Prevent optimizer from removing benchmark work.
    volatile std::size_t g_sink = 0;
}

struct Config
{
    std::string env = "prod";
    int timeout_ms = 3000;
};

std::vector<long long> prime_factorization(long long n)
{
    std::vector<long long> factors;

    while (n % 2 == 0)
    {
        factors.push_back(2);
        n /= 2;
    }

    for (long long i = 3; i * i <= n; i += 2)
    {
        while (n % i == 0)
        {
            factors.push_back(i);
            n /= i;
        }
    }

    if (n > 2)
    {
        factors.push_back(n);
    }

    return factors;
}

decorator(@inject)
void benchmark(long long n, Config* cfg = Depends())
{
    const auto factors = prime_factorization(n);
    g_sink += factors.size();

    if (cfg != nullptr && !factors.empty())
    {
        cfg->timeout_ms += static_cast<int>(factors.back() & 1LL);
    }
}

int main()
{
    (void)ClearDependencies();

    constexpr long long kInput = 1000000000000000000LL;

    auto beg = std::chrono::high_resolution_clock::now();
    const auto factors = prime_factorization(kInput);
    g_sink += factors.size();
    auto end = std::chrono::high_resolution_clock::now();

    const auto bench1_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - beg).count();
    std::cout << "Bench1: " << bench1_ns << " ns" << std::endl;

    {
        Config test{};
        auto guard = ScopeOverrideDependency<&benchmark>(&test);
        beg = std::chrono::high_resolution_clock::now();
        benchmark(kInput);
        end = std::chrono::high_resolution_clock::now();
        const auto bench2_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - beg).count();
        std::cout << "Bench2: " << bench2_ns << " ns" << std::endl;
    }

    std::cout << "Sink: " << g_sink << std::endl;
    return 0;
}
