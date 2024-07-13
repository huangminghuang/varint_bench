#include "parse_varint.h"
#include "varint_parser.h"

#include <benchmark/benchmark.h>
#include <map>
#include <random>
#include <vector>

static std::vector<char> data;

template <typename Type> char *pack_varint(Type orig_value, char *data) {
  auto value = std::make_unsigned_t<Type>(orig_value);

  while (value >= 0x80) {
    *data++ = char((value & 0x7f) | 0x80);
    value >>= (CHAR_BIT - 1);
  }
  *data++ = char(value);
  return data;
}

const std::vector<char> get_data(std::size_t len) {
  static std::map<std::size_t, std::vector<char>> all_data;
  auto &data = all_data[len];
  if (data.size() == 0) {
    std::random_device rd;
    std::mt19937 engine(rd());
    constexpr uint64_t level[] = {
      0x00ULL,
      0x80ULL,
      0x4000ULL,
      0x200000ULL,
      0x10000000ULL,
      0x0800000000ULL,
      0x040000000000ULL,
      0x02000000000000ULL,
      0x0100000000000000ULL,
    };
    std::uniform_int_distribution<unsigned long long> dis(
        level[0],
        level[4]-1);

    data.resize(len * 10);
    auto buf = data.data();
    for (auto i = 0U; i < len; ++i) {
      buf = pack_varint(dis(engine), buf);
    }
    std::size_t s = buf - data.data();
    data.resize(buf - data.data());
  }
  return data;
}

template <auto Fun> void BM_fun(benchmark::State &state) {
  auto count = static_cast<size_t>(state.range(0));
  auto &data = get_data(count);
  std::vector<uint64_t> result;
  result.resize(count);

  for (auto _ : state) {
    auto r = Fun(data.data(), data.data() + data.size(), result.data());
    benchmark::DoNotOptimize(r);
  }
}

auto bulk_bmi_parse(const char *begin, const char *end, uint64_t *res) {
  bmi_varint_parser<6, uint64_t> parser;
  return parser.parse(begin, end, res);
}

auto bulk_shift_mix_parse(const char *begin, const char *end, uint64_t *res) {
  while (begin < end) {
    
    begin = shift_mix_parse_varint<uint64_t>(begin, reinterpret_cast<int64_t&>(*res++));
  }
  return begin;
}

template <typename Byte, typename Type, int MAX_BYTES = ((sizeof(Type) * 8 + 6) / 7)>
constexpr inline const Byte *unrolled_parse_varint(const Byte *p, const Byte *end, Type &value) {
  value = 0;
  do {
    // clang-format off
      Type next_byte; 
      next_byte = Type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 0)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = Type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 1)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = Type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 2)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = Type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 3)); if (next_byte < 0x80) [[likely]] { break; }
      if constexpr (MAX_BYTES > 4) {
      next_byte = Type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 4)); if (next_byte < 0x80) [[likely]] { break; }
      if constexpr (MAX_BYTES > 5) {
      next_byte = Type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 5)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = Type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 6)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = Type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 7)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = Type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 8)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = Type(*p++); value |= ((next_byte & 0x01) << ((CHAR_BIT - 1) * 9)); if (next_byte < 0x80) [[likely]] { break; } } }
      return end;
    // clang-format on
  } while (false);
  return p;
}

auto bulk_unroll_parse(const char *begin, const char *end, uint64_t *res) {
  while (begin < end) {
    begin = unrolled_parse_varint(begin, end+1, *res++);
  }
  return begin;
}

auto bulk_ubfx_parse(const char *begin, const char *end, uint64_t *res) {
  return ubfx_varint_parser::parse(begin, end, res);
}

#ifdef __x86_64__
BENCHMARK(BM_fun<bulk_bmi_parse>)->Args({10})->Args({100})->Args({300})->Args({1000});
#endif

BENCHMARK(BM_fun<bulk_shift_mix_parse>)->Args({10})->Args({100})->Args({300})->Args({1000});
BENCHMARK(BM_fun<bulk_unroll_parse>)->Args({10})->Args({100})->Args({300})->Args({1000});
BENCHMARK(BM_fun<bulk_ubfx_parse>)->Args({10})->Args({100})->Args({300})->Args({1000});

BENCHMARK_MAIN();
