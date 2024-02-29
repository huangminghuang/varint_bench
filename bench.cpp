#include <benchmark/benchmark.h>
#include "parse_varint.h"

constexpr std::size_t storage_size = 10000;
char storage[storage_size];

template <typename Type>
std::span<char> get_data()
{
    static std::span<char> data;
    if (data.empty())
    {
        std::span<char> rest = storage;
        Type value;
        do
        {
            value = static_cast<uint32_t>(std::rand());
        } while (pack_varint(value, rest));
        data = std::span{storage, storage_size - rest.size()};
    }
    return data;
}


template <typename Fun>
void BM_fun(benchmark::State &state)
{
    using value_type = typename Fun::type;
    auto data = get_data<value_type>();
    value_type value;
    auto d = data;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(
            Fun()(value, d));
        if (d.empty())
            d = data;
    }
}

BENCHMARK(BM_fun<parse_varint_loop<uint32_t>>);
BENCHMARK(BM_fun<parse_varint_unrolled<uint32_t>>);
BENCHMARK(BM_fun<shift_mix_parse_varint_op<uint32_t>>);

BENCHMARK_MAIN();
