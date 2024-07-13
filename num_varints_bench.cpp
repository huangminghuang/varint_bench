#include <benchmark/benchmark.h>

#include <execution>
#include <experimental/simd>
#include <numeric>
#include <random>
#include <span>
#include <ranges>

std::size_t num_varints_simple_forloop(std::span<const char> range) {
  std::size_t r = 0;
  for (auto begin = range.begin(); begin != range.end(); ++begin) {
    auto v = *begin;
    r += (int8_t(v) >= 0);
  }
  return r;
}

std::size_t num_varints_unseq(std::span<const char> range) {
  return std::transform_reduce(std::execution::unseq, range.begin(), range.end(), 0U,
                               std::plus{},
                               [](char v) { return int8_t(v) >= 0; });
}

// std::size_t num_varints_popcount_unseq(const char *begin,
//                                              const char *end) {
//   return std::transform_reduce(
//       std::execution::unseq, begin, end, 0U, std::plus{}, [](char v) {
//         return std::popcount((uint16_t)(~uint8_t(v) & uint8_t(0x80)));
//       });
// }

// std::size_t num_varints_popcount(const char *begin, const char *end) {
//   return std::transform_reduce(begin, end, 0U, std::plus{}, [](char v) {
//     return std::popcount((uint16_t)(~uint8_t(v) & uint8_t(0x80)));
//   });
// }


struct dword_iterator {
  using value_type = int;

  const char *base;
  dword_iterator() : base(nullptr) {}
  explicit dword_iterator(const char *b) : base(b) {}
  dword_iterator(const dword_iterator &) = default;
  bool operator==(const dword_iterator &) const = default;

  dword_iterator operator++(int) const {
    return dword_iterator{base + sizeof(uint64_t)};
  }

  dword_iterator &operator++() {
    base += sizeof(uint64_t);
    return *this;
  }

  int operator*() const {
    uint64_t v;
    memcpy(&v, base, sizeof(v));
    return std::popcount(v);
  }
};

inline auto count_num_varints_by_dword(std::span<const char> range) {
  dword_iterator first{range.data()};
  dword_iterator last{range.data() + range.size() - (range.size() % sizeof(uint64_t))};

  int result = std::reduce(std::execution::seq, first, last, 0);
  if (range.size() % sizeof(uint64_t) == 0) return result;
  uint64_t v = -1;
  memcpy(&v, last.base, range.data() + range.size() - last.base);
  return result + std::popcount(~v & 0x8080808080808080ULL);
}

#if !defined(__clang__) || !defined(__APPLE_)

std::size_t num_varints_simd(std::span<const char> range) {
  namespace stdx = std::experimental::parallelism_v2;
  stdx::simd<int8_t> v, zeros{0};
  auto range1 = range.subspan(0, (range.size()/v.size())*v.size());
  auto range2 = range.subspan(range1.size());
  std::size_t result = 0;
  for (; range1.size() > 0; range1 = range1.subspan(v.size())) {
    v.copy_from(reinterpret_cast<const int8_t *>(range1.data()), stdx::element_aligned);
    result += stdx::popcount(v < zeros);
  }

  if (range2.empty()) return result;
  std::array<uint8_t, v.size()> remaining{255};
  auto it = std::copy(range2.begin(), range2.end(), remaining.data());
  v.copy_from(remaining.begin(), stdx::element_aligned);
  result += stdx::popcount(v < zeros);

  return result;
}

#endif

std::size_t num_varints_unroll1(std::span<const char> range) {
  std::size_t result = 0;
  uint64_t v;
  auto begin = range.data();
  auto end = range.data() + range.size();
  // for (; range.size() >=8; range = range.subspan( sizeof(v))) {
  for (; (end - begin) >= 8; begin+= sizeof(v)) {
    memcpy(&v,begin, sizeof(v));
    // memcpy(&v,range.data(), sizeof(v));
    result += std::popcount(~v & 0x8080808080808080ULL);
  }
  if (0 == end - begin) return result;
  v = UINT64_MAX;
  memcpy(&v,begin, end - begin);
  result += std::popcount(~v & 0x8080808080808080ULL);
  return result;
}

std::size_t num_varints_unroll2(std::span<const char> range) {
  std::size_t result = 0;
  uint64_t v;
  
  for (; range.size() >=8; range = range.subspan(sizeof(v))) {
    memcpy(&v,range.data(), sizeof(v));
    result += std::popcount(~v & 0x8080808080808080ULL);
  }

  if (range.empty()) return result;
  v = UINT64_MAX;
  memcpy(&v,range.data(), range.size());
  result += std::popcount(~v & 0x8080808080808080ULL);
  return result;
}

std::size_t num_varints_range_alg(std::span<const char> range) {
  auto dwords = range | std::views::chunk(sizeof(uint64_t)) | std::views::transform([](auto&& chunk) { 
    uint64_t v;
    memcpy(&v, chunk.data(), sizeof(v));
    return std::popcount(~v & 0x8080808080808080ULL);
  });
  
  return std::accumulate(dwords.begin(), dwords.end(), 0);
}


template <auto Fun> void BM_fun(benchmark::State &state) {
  auto count = static_cast<size_t>(state.range(0));

  std::vector<char> array(count);
  std::iota(array.begin(), array.end(), 1);

  // Create a random number generator
  std::random_device rd;  // Obtain a random number from hardware
  std::mt19937 gen(rd()); // Seed the generator

  // Shuffle the elements of the vector
  std::shuffle(array.begin(), array.end(), gen);

  for (auto _ : state) {
    auto r = Fun(array);
    //auto r = Fun(array.data(), array.data()+array.size());
    benchmark::DoNotOptimize(r);
  }
}
BENCHMARK(BM_fun<num_varints_simple_forloop>)
    ->Args({16})
    ->Args({32})
    ->Args({128})
    ->Args({200})
    ->Args({300})
    ->Args({1000})
    ->Args({10000});
BENCHMARK(BM_fun<num_varints_unseq>)
    ->Args({16})
    ->Args({32})
    ->Args({128})
    ->Args({200})
    ->Args({300})
    ->Args({1000})
    ->Args({10000});
// BENCHMARK(BM_fun<num_varints_popcount>)
//     ->Args({10})
//     ->Args({30})
//     ->Args({100})
//     ->Args({200})
//     ->Args({300})
//     ->Args({1000});
// BENCHMARK(BM_fun<num_varints_popcount_unseq>)
//     ->Args({10})
//     ->Args({30})
//     ->Args({100})
//     ->Args({200})
//     ->Args({300})
//     ->Args({1000});
#if !defined(__clang__) || !defined(__APPLE_)
BENCHMARK(BM_fun<num_varints_simd>)
    ->Args({16})
    ->Args({32})
    ->Args({128})
    ->Args({200})
    ->Args({300})
    ->Args({1000})
    ->Args({10000});
#endif
BENCHMARK(BM_fun<num_varints_unroll1>)
    ->Args({16})
    ->Args({32})
    ->Args({128})
    ->Args({200})
    ->Args({300})
    ->Args({1000})
    ->Args({10000});
BENCHMARK(BM_fun<num_varints_unroll2>)
    ->Args({16})
    ->Args({32})
    ->Args({128})
    ->Args({200})
    ->Args({300})
    ->Args({1000})
    ->Args({10000});

BENCHMARK(BM_fun<count_num_varints_by_dword>)
    ->Args({16})
    ->Args({32})
    ->Args({128})
    ->Args({200})
    ->Args({300})
    ->Args({1000})
    ->Args({10000});

BENCHMARK(BM_fun<num_varints_range_alg>)
    ->Args({16})
    ->Args({32})
    ->Args({128})
    ->Args({200})
    ->Args({300})
    ->Args({1000})
    ->Args({10000});

BENCHMARK_MAIN();
