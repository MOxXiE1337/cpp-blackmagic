#include <cppbm/depends.h>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>
#include <algorithm>

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

Config& DefaultConfigFactory()
{
    static Config cfg{ "prod", 3000 };
    return cfg;
}

Config& DefaultConfigFactoryRef()
{
    static Config cfg{ "prod-ref", 3500 };
    return cfg;
}

Task<Config&> AsyncConfigFactoryRef()
{
    static Config cfg{ "prod-async-ref", 3600 };
    co_return cfg;
}

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

void BenchmarkCore(long long n, Config* cfg)
{
    const auto factors = prime_factorization(n);
    g_sink += factors.size();

    if (cfg != nullptr && !factors.empty())
    {
        cfg->timeout_ms += static_cast<int>(factors.back() & 1LL);
    }
}

decorator(@inject)
void benchmark_depends_plain(long long n, Config* cfg = Depends())
{
    BenchmarkCore(n, cfg);
}

decorator(@inject)
void benchmark_depends_plain_nocache(long long n, Config* cfg = Depends(false))
{
    BenchmarkCore(n, cfg);
}

decorator(@inject)
void benchmark_depends_factory_ptr(long long n, Config* cfg = Depends(DefaultConfigFactory))
{
    BenchmarkCore(n, cfg);
}

decorator(@inject)
void benchmark_depends_factory_ref(long long n, Config& cfg = Depends(DefaultConfigFactoryRef))
{
    BenchmarkCore(n, &cfg);
}

decorator(@inject)
void benchmark_explicit_arg_bypass(long long n, Config* cfg = Depends())
{
    BenchmarkCore(n, cfg);
}

Task<> benchmark_async_direct(long long n, Config* cfg)
{
    BenchmarkCore(n, cfg);
    co_return;
}

decorator(@inject)
Task<> benchmark_async_depends_plain(long long n, Config* cfg = Depends())
{
    BenchmarkCore(n, cfg);
    co_return;
}

decorator(@inject)
Task<> benchmark_async_depends_factory_ref(long long n, Config& cfg = Depends(AsyncConfigFactoryRef))
{
    BenchmarkCore(n, &cfg);
    co_return;
}

decorator(@inject)
Task<> benchmark_async_explicit_arg_bypass(long long n, Config* cfg = Depends())
{
    BenchmarkCore(n, cfg);
    co_return;
}

struct BenchStats
{
    std::int64_t min_ns = 0;
    std::int64_t p50_ns = 0;
    std::int64_t p95_ns = 0;
    std::int64_t p99_ns = 0;
    std::int64_t max_ns = 0;
    double avg_ns = 0.0;
};

std::int64_t PercentileFromSorted(const std::vector<std::int64_t>& sorted, int percentile)
{
    if (sorted.empty())
    {
        return 0;
    }

    const std::size_t n = sorted.size();
    const std::size_t idx = static_cast<std::size_t>((static_cast<double>(percentile) / 100.0) * static_cast<double>(n - 1));
    return sorted[idx];
}

BenchStats ComputeStats(std::vector<std::int64_t> samples)
{
    BenchStats stats{};
    if (samples.empty())
    {
        return stats;
    }

    std::sort(samples.begin(), samples.end());

    stats.min_ns = samples.front();
    stats.p50_ns = PercentileFromSorted(samples, 50);
    stats.p95_ns = PercentileFromSorted(samples, 95);
    stats.p99_ns = PercentileFromSorted(samples, 99);
    stats.max_ns = samples.back();

    const auto sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    stats.avg_ns = sum / static_cast<double>(samples.size());
    return stats;
}

template <typename F>
std::vector<std::int64_t> CollectSamples(F&& fn, int warmup_iters, int measure_iters)
{
    using Clock = std::chrono::high_resolution_clock;
    std::vector<std::int64_t> samples;
    samples.reserve(static_cast<std::size_t>(measure_iters));

    for (int i = 0; i < warmup_iters; ++i)
    {
        fn();
    }

    for (int i = 0; i < measure_iters; ++i)
    {
        const auto beg = Clock::now();
        fn();
        const auto end = Clock::now();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - beg).count();
        samples.push_back(static_cast<std::int64_t>(ns));
    }

    return samples;
}

void PrintStats(const char* label, const BenchStats& stats)
{
    std::cout << label
              << " avg=" << std::fixed << std::setprecision(1) << stats.avg_ns << " ns"
              << " min=" << stats.min_ns << " ns"
              << " p50=" << stats.p50_ns << " ns"
              << " p95=" << stats.p95_ns << " ns"
              << " p99=" << stats.p99_ns << " ns"
              << " max=" << stats.max_ns << " ns"
              << std::endl;
}

template <typename FBase, typename FTarget>
std::vector<std::int64_t> CollectOverheadSamples(
    FBase&& base_fn,
    FTarget&& target_fn,
    int warmup_iters,
    int measure_iters)
{
    using Clock = std::chrono::high_resolution_clock;
    std::vector<std::int64_t> samples;
    samples.reserve(static_cast<std::size_t>(measure_iters));

    for (int i = 0; i < warmup_iters; ++i)
    {
        base_fn();
        target_fn();
    }

    for (int i = 0; i < measure_iters; ++i)
    {
        const auto b0 = Clock::now();
        base_fn();
        const auto b1 = Clock::now();
        target_fn();
        const auto b2 = Clock::now();

        const auto base_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(b1 - b0).count();
        const auto target_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();
        samples.push_back(static_cast<std::int64_t>(target_ns - base_ns));
    }

    return samples;
}

template <typename FBase, typename FTarget>
void RunCase(
    const char* case_name,
    FBase&& base_fn,
    FTarget&& target_fn,
    int warmup_iters,
    int measure_iters)
{
    const auto target_samples = CollectSamples(
        std::forward<FTarget>(target_fn),
        warmup_iters,
        measure_iters);
    const auto target_stats = ComputeStats(target_samples);

    const auto overhead_samples = CollectOverheadSamples(
        std::forward<FBase>(base_fn),
        std::forward<FTarget>(target_fn),
        warmup_iters,
        measure_iters);
    const auto overhead_stats = ComputeStats(overhead_samples);

    PrintStats(case_name, target_stats);
    PrintStats("  Overhead(vs direct)", overhead_stats);
}

int main()
{
    (void)ClearDependencies();

    constexpr long long kInput = 1000000000000000000LL;
    constexpr int kWarmupIters = 12;
    constexpr int kMeasureIters = 240;

    Config base_cfg{};
    auto direct_fn = [&]() {
        BenchmarkCore(kInput, &base_cfg);
    };

    // Baseline: direct computation without @inject.
    const auto direct_samples = CollectSamples(
        direct_fn,
        kWarmupIters,
        kMeasureIters);
    const auto direct_stats = ComputeStats(direct_samples);
    PrintStats("Bench0 (direct baseline)", direct_stats);

    RunCase(
        "Bench1 (@inject Depends())",
        direct_fn,
        [&]() { benchmark_depends_plain(kInput); },
        kWarmupIters,
        kMeasureIters);

    {
        Config target_cfg{};
        auto guard = ScopeOverrideDependency<&benchmark_depends_plain>(&target_cfg);
        RunCase(
            "Bench2 (@inject Depends() + target override)",
            direct_fn,
            [&]() { benchmark_depends_plain(kInput); },
            kWarmupIters,
            kMeasureIters);
    }

    {
        Config global_cfg{};
        auto guard = ScopeOverrideDependency(&global_cfg);
        RunCase(
            "Bench3 (@inject Depends() + global override)",
            direct_fn,
            [&]() { benchmark_depends_plain(kInput); },
            kWarmupIters,
            kMeasureIters);
    }

    RunCase(
        "Bench4 (@inject Depends(false))",
        direct_fn,
        [&]() { benchmark_depends_plain_nocache(kInput); },
        kWarmupIters,
        kMeasureIters);

    RunCase(
        "Bench5 (@inject Depends(factory ptr))",
        direct_fn,
        [&]() { benchmark_depends_factory_ptr(kInput); },
        kWarmupIters,
        kMeasureIters);

    {
        Config global_factory_cfg{};
        auto guard = ScopeOverrideDependency(&global_factory_cfg, DefaultConfigFactory);
        RunCase(
            "Bench6 (@inject Depends(factory ptr) + global factory override)",
            direct_fn,
            [&]() { benchmark_depends_factory_ptr(kInput); },
            kWarmupIters,
            kMeasureIters);
    }

    RunCase(
        "Bench7 (@inject Depends(factory ref))",
        direct_fn,
        [&]() { benchmark_depends_factory_ref(kInput); },
        kWarmupIters,
        kMeasureIters);

    RunCase(
        "Bench8 (@inject explicit arg bypass)",
        direct_fn,
        [&]() { benchmark_explicit_arg_bypass(kInput, &base_cfg); },
        kWarmupIters,
        kMeasureIters);

    std::cout << "---- Async Cases ----" << std::endl;

    auto direct_async_fn = [&]() {
        benchmark_async_direct(kInput, &base_cfg).Get();
    };

    const auto direct_async_samples = CollectSamples(
        direct_async_fn,
        kWarmupIters,
        kMeasureIters);
    const auto direct_async_stats = ComputeStats(direct_async_samples);
    PrintStats("Bench9 (async direct baseline)", direct_async_stats);

    RunCase(
        "Bench10 (@inject async Depends())",
        direct_async_fn,
        [&]() { benchmark_async_depends_plain(kInput).Get(); },
        kWarmupIters,
        kMeasureIters);

    {
        Config target_cfg_async{};
        auto guard = ScopeOverrideDependency<&benchmark_async_depends_plain>(&target_cfg_async);
        RunCase(
            "Bench11 (@inject async Depends() + target override)",
            direct_async_fn,
            [&]() { benchmark_async_depends_plain(kInput).Get(); },
            kWarmupIters,
            kMeasureIters);
    }

    RunCase(
        "Bench12 (@inject async Depends(async factory ref))",
        direct_async_fn,
        [&]() { benchmark_async_depends_factory_ref(kInput).Get(); },
        kWarmupIters,
        kMeasureIters);

    RunCase(
        "Bench13 (@inject async explicit arg bypass)",
        direct_async_fn,
        [&]() { benchmark_async_explicit_arg_bypass(kInput, &base_cfg).Get(); },
        kWarmupIters,
        kMeasureIters);

    std::cout << "Sink: " << g_sink << std::endl;
    return 0;
}
