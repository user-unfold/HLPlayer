#include <benchmark/benchmark.h>

static void BM_SimpleBenchmark(benchmark::State& state) {
    for (auto _ : state) {
        // Simple operation to benchmark
        int result = 0;
        for (int i = 0; i < 100; ++i) {
            result += i;
        }
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_SimpleBenchmark);
