#include <benchmark/benchmark.h>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include <thread>

#include "my_vault.h"

struct Data {
    int         field_1 {0};
    std::string field_3;
};

std::ostream& operator<< (std::ostream& st, const Data& data)
{
    fmt::print(st, "s: {}  i: {}", data.field_3, data.field_1);
    return st;
}

template<size_t S>
void allocate_benchmark (benchmark::State& state)
{
    const size_t       tCount {static_cast<size_t>(state.range(0))};
    const size_t       count_per_thread = S / tCount;
    std::atomic_size_t allocations {0};
    std::atomic_size_t failures {0};

    std::vector<std::jthread> thr;
    thr.reserve(tCount);

    for ( auto _: state ) {
        std::unique_ptr<Vault<Data, S>> v = std::make_unique<Vault<Data, S>>( );
        for ( size_t i = 0; i < tCount; i++ ) {
            thr.emplace_back([&v, &allocations, &failures, &i, &count_per_thread] ( ) {
                for ( size_t n = 0; n < count_per_thread; n++ ) {
                    if ( auto [view, inserted] = v->allocate( ); inserted ) {
                        view( ).field_3.assign(fmt::format("{}_{}", i + 1, n + 1));
                        view( ).field_1 = 0;
                        allocations.fetch_add(1);
                        // long_lasting_op( );
                    } else {
                        failures.fetch_add(1);
                    }
                }
            });
        }
        for ( auto& t: thr )
            t.join( );
        thr.clear( );
    }

    state.counters["allocated"] = allocations.load( ) / state.iterations( );
    state.counters["failures"]  = failures.load( ) / state.iterations( );

    state.SetComplexityN(tCount);
}

BENCHMARK(allocate_benchmark<1024 * 2>)->Name("allocating  2K")->Unit(benchmark::kMillisecond)->RangeMultiplier(2)->Range(1, 128)->Complexity( )->UseRealTime( );
BENCHMARK(allocate_benchmark<1024 * 4>)->Name("allocating  4K")->Unit(benchmark::kMillisecond)->RangeMultiplier(2)->Range(1, 128)->Complexity( );
BENCHMARK(allocate_benchmark<1024 * 8>)->Name("allocating  8K")->Unit(benchmark::kMillisecond)->RangeMultiplier(2)->Range(1, 128)->Complexity( );
BENCHMARK(allocate_benchmark<1024 * 16>)->Name("allocating 16K")->Unit(benchmark::kMillisecond)->RangeMultiplier(2)->Range(1, 128)->Complexity( );
BENCHMARK(allocate_benchmark<1024 * 32>)->Name("allocating 32K")->Unit(benchmark::kMillisecond)->RangeMultiplier(2)->Range(1, 128)->Complexity( );
BENCHMARK(allocate_benchmark<1024 * 64>)->Name("allocating 64K")->Unit(benchmark::kMillisecond)->RangeMultiplier(2)->Range(1, 128)->Complexity( );
BENCHMARK(allocate_benchmark<1024 * 128>)->Name("allocating 128K")->Unit(benchmark::kMillisecond)->RangeMultiplier(2)->Range(1, 128)->Complexity( );
