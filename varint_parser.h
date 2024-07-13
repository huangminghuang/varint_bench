
#include <bit>
#include <cstdint>
#include <cstring>
#include <immintrin.h>

template <int MaskLength, typename T>
struct bmi_varint_parser {
  T *res;
  int shift_bits = 0;
  uint64_t pt_val = 0;

  static consteval int calc_shift_bits(unsigned sign_bits) {
    unsigned mask = 1 << (MaskLength - 1);
    int result = 0;
    for (; mask != 0 && (sign_bits & mask); mask >>= 1) {
      result += 1;
    }
    return result * 7;
  }

  static consteval uint64_t calc_word_mask() {
    uint64_t result = 0x80ULL;
    for (int i = 0; i < MaskLength - 1; ++i)
      result = (result << CHAR_BIT | 0x80ULL);
    return result;
  }

  static constexpr auto word_mask = calc_word_mask();
  static consteval uint64_t calc_extract_mask(uint64_t sign_bits) {
    int64_t extract_mask = 0x7fULL;
    for (int i = 0; i < std::countr_one(sign_bits); ++i) {
      extract_mask <<= CHAR_BIT;
      extract_mask |= 0x7fULL;
    }
    return extract_mask;
  }

  static uint64_t pext_u64(uint64_t a, uint64_t mask) {
#if defined(__GNUC__) || defined(__clang__)
    uint64_t result;
    asm("pext %2, %1, %0" : "=r"(result) : "r"(a), "r"(mask));
    return result;
#elif defined(_MSC_VER)
    return _pext_u64(a, mask);
#endif
  }

  __attribute__((always_inline)) void output(uint64_t v) { *res++ = static_cast<T>(v); }

  template <uint64_t SignBits, int I>
  inline void output(uint64_t word, uint64_t &extract_mask) {
    if constexpr (I < MaskLength) {
      extract_mask |= 0x7fULL << (CHAR_BIT * I);
      if ((SignBits & (0x01ULL << I)) == 0) {
        output(pext_u64(word, extract_mask));
        extract_mask = 0;
      }
      output<SignBits, I + 1>(word, extract_mask);
    }
  }

  template <uint64_t SignBits>
  __attribute__((always_inline)) void fixed_masked_parse(uint64_t word) {
    uint64_t extract_mask = calc_extract_mask(SignBits);
    if constexpr (std::countr_one(SignBits) < MaskLength) {
      output((pext_u64(word, extract_mask) << shift_bits) | pt_val);
      constexpr unsigned bytes_processed = std::countr_one(SignBits) + 1;
      extract_mask = 0x7fULL << (CHAR_BIT * bytes_processed);
      output<SignBits, bytes_processed>(word, extract_mask);
      pt_val = 0;
      shift_bits = 0;
    }

    if constexpr (SignBits & (0x01ULL << (MaskLength - 1))) {
      pt_val |= pext_u64(word, extract_mask) << shift_bits;
    }

    shift_bits += calc_shift_bits(SignBits);
  }

  template <std::size_t... I>
  __attribute__((always_inline)) void parse_word(uint64_t masked_bits, uint64_t word, std::index_sequence<I...>) {
    (void)((masked_bits == I && (fixed_masked_parse<I>(word), true)) || ...);
  }

  const char *parse_partial(const char *begin, const char *end) {
    for (; end - begin >= MaskLength; begin += MaskLength) {
      uint64_t word;
      memcpy(&word, begin, sizeof(word));
      auto mval = pext_u64(word, word_mask);
      parse_word(mval, word, std::make_index_sequence<1 << MaskLength>());
    }
    return begin;
  }

  const char *parse(const char *begin, const char *end, T *result) {
    res = result;

    begin = parse_partial(begin, end);

    int bytes_left = end - begin;
    uint64_t word = 0;
    memcpy(&word, begin, bytes_left);
    for (; bytes_left > 0; --bytes_left, word >>= CHAR_BIT) {
      pt_val |= ((word & 0x7fULL) << shift_bits);
      if (word & 0x80ULL) {
        shift_bits += (CHAR_BIT - 1);
      } else {
        output(pt_val);
        pt_val = 0;
        shift_bits = 0;
      }
    }
    return end;
  }
};

struct ubfx_varint_parser {

  template <uint32_t lsb, uint32_t width>
  inline static constexpr uint64_t ubfx(uint64_t src) {
    return (src >> lsb) & ((1 << width) - 1);
  }

  template <uint32_t lsb>
  inline static constexpr uint64_t extract_2_bytes(uint64_t src) {
    uint64_t b0 = ubfx<lsb, 7>(src);
    uint64_t b1 = ubfx<lsb + 8, 7>(src);
    return (b1 << 7 | b0) << 7 * (lsb / 8);
  }

  inline static constexpr uint64_t extract_bytes(uint64_t word, int n) {
    if (!std::is_constant_evaluated()) {
#ifdef __BMI2__
      constexpr std::array<uint64_t, 8> masks = {0ULL,
                                                 0x7f7fULL,
                                                 0x7f7f7fULL,
                                                 0x7f7f7f7fULL,
                                                 0x7f7f7f7f7fULL,
                                                 0x7f7f7f7f7f7fULL,
                                                 0x7f7f7f7f7f7f7fULL,
                                                 0x7f7f7f7f7f7f7f7fULL};
      return _pext_u64(word, masks[n - 1]);
#endif
    }

    uint64_t byte_zero = word & 0x7fULL;
    if (n == 2)
      return extract_2_bytes<0>(word);
    else if (n == 3)
      return extract_2_bytes<8>(word) | byte_zero;
    else if (n == 4)
      return extract_2_bytes<16>(word) | extract_2_bytes<0>(word);
    else if (n == 5)
      return extract_2_bytes<24>(word) | extract_2_bytes<8>(word) | byte_zero;
    else if (n == 6)
      return extract_2_bytes<32>(word) | extract_2_bytes<16>(word) |
             extract_2_bytes<0>(word);
    else if (n == 7)
      return extract_2_bytes<40>(word) | extract_2_bytes<24>(word) |
             extract_2_bytes<8>(word) | byte_zero;
    else if (n == 8)
      return extract_2_bytes<48>(word) | extract_2_bytes<32>(word) |
             extract_2_bytes<16>(word) | extract_2_bytes<0>(word);
#if defined(_MSC_VER) && !defined(__clang__) // MSVC
    __assume(false);
#else // GCC, Clang
    __builtin_unreachable();
#endif
  }

  template <typename T>
  static inline const char *parse(const char *begin, const char *end,
                                  T *result) {
    while (end - begin >= 8) {
      uint64_t word;
      memcpy(&word, begin, sizeof(word));

      int width = 1 + std::countr_one(word | 0x7f7f7f7f7f7f7f7fULL) / 8;
      if (width == 1) {
        auto x = std::bit_cast<std::array<int8_t, 8>>(word);
        int i;
        for (i = 0; x[i] >= 0 && i < 8; ++i) {
          *result++ = static_cast<T>(x[i]);
        }
        begin += i;
      } else if (width == 9) {
        int8_t next_byte = static_cast<int8_t>(*(begin + 8));
        *result++ =
            extract_bytes(word, 8) | (static_cast<uint64_t>(next_byte) << 56);
        if (next_byte > 0) [[likely]] {
          begin += 9;
        } else {
          if (static_cast<int8_t>(*(begin + 9)) == -1) [[likely]]
            begin += 10;
          else
            return end + 1; // error
        }
      } else {
        *result++ = extract_bytes(word, width);
        begin += width;
      }
    }

    while (begin < end) {
      int64_t v;
      begin = shift_mix_parse_varint<T>(begin, v);
      *result++ = static_cast<T>(v);
    }
    return begin;
  }
};
