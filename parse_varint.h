
#pragma once
#include <climits>
#include <system_error>
#include <span>
#include <cstdint>

template <typename Type>
constexpr auto varint_max_size = sizeof(Type) * CHAR_BIT / (CHAR_BIT - 1) + 1;

template <typename Type>
struct parse_varint_loop
{
    using type = Type;
    std::errc operator()(Type &v, std::span<char> &data) const
    {
        std::size_t shift = 0;
        using value_type = std::make_unsigned_t<Type>;
        value_type value = 0;
        auto limit = std::min(data.size(), varint_max_size<Type>);
        for (unsigned i = 0; i < limit; ++i)
        {
            auto next_byte = value_type(data[i]);
            value |= (next_byte & 0x7f) << shift;
            if (next_byte >= 0x80) [[unlikely]]
            {
                shift += CHAR_BIT - 1;
                continue;
            }
            data = data.subspan(i + 1);
            v = static_cast<Type>(value);
            return {};
        }
        return std::errc::value_too_large;
    }
};

template <typename Type>
struct parse_varint_unrolled
{
    using type = Type;
    std::errc operator()(Type &v, std::span<char> &data) const
    {
        using value_type = std::make_unsigned_t<Type>;
        value_type value = 0;
        auto p = data.data();
        do
        {
            // clang-format off
      value_type next_byte; 
      next_byte = value_type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 0)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = value_type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 1)); if (next_byte < 0x80) [[likely]] { break; }
      if constexpr (varint_max_size<Type> > 2) {
      next_byte = value_type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 2)); if (next_byte < 0x80) [[likely]] { break; }
      if constexpr (varint_max_size<Type> > 3) {
      next_byte = value_type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 3)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = value_type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 4)); if (next_byte < 0x80) [[likely]] { break; }
      if constexpr (varint_max_size<Type> > 5) {
      next_byte = value_type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 5)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = value_type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 6)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = value_type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 7)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = value_type(*p++); value |= ((next_byte & 0x7f) << ((CHAR_BIT - 1) * 8)); if (next_byte < 0x80) [[likely]] { break; }
      next_byte = value_type(*p++); value |= ((next_byte & 0x01) << ((CHAR_BIT - 1) * 9)); if (next_byte < 0x80) [[likely]] { break; } }}}
      return std::errc::value_too_large;
            // clang-format on
        } while (false);
        data = data.subspan(std::distance(data.data(), p));
        v = static_cast<Type>(value);
        return {};
    }
};

// Shifts "byte" left by n * 7 bits, filling vacated bits from `ones`.
// template <int n>
constexpr inline int64_t VarintShlByte(int n, int8_t byte, int64_t ones)
{
    return static_cast<int64_t>((static_cast<uint64_t>(byte) << n * 7) |
                                (static_cast<uint64_t>(ones) >> (64 - n * 7)));
}



template <typename VarintType>
constexpr inline const char *shift_mix_parse_varint(const char *p,
                                                 int64_t &res1)
{
    using Signed = std::make_signed_t<VarintType>;
    constexpr bool kIs64BitVarint = std::is_same<Signed, int64_t>::value;
    constexpr bool kIs32BitVarint = std::is_same<Signed, int32_t>::value;
    static_assert(kIs64BitVarint || kIs32BitVarint, "");

    // The algorithm relies on sign extension for each byte to set all high bits
    // when the varint continues. It also relies on asserting all of the lower
    // bits for each successive byte read. This allows the result to be aggregated
    // using a bitwise AND. For example:
    //
    //          8       1          64     57 ... 24     17  16      9  8       1
    // ptr[0] = 1aaa aaaa ; res1 = 1111 1111 ... 1111 1111  1111 1111  1aaa aaaa
    // ptr[1] = 1bbb bbbb ; res2 = 1111 1111 ... 1111 1111  11bb bbbb  b111 1111
    // ptr[2] = 0ccc cccc ; res3 = 0000 0000 ... 000c cccc  cc11 1111  1111 1111
    //                             ---------------------------------------------
    //        res1 & res2 & res3 = 0000 0000 ... 000c cccc  ccbb bbbb  baaa aaaa
    //
    // On x86-64, a shld from a single register filled with enough 1s in the high
    // bits can accomplish all this in one instruction. It so happens that res1
    // has 57 high bits of ones, which is enough for the largest shift done.
    //
    // Just as importantly, by keeping results in res1, res2, and res3, we take
    // advantage of the superscalar abilities of the CPU.
    const auto next = [&p]
    { return static_cast<const int8_t>(*p++); };
    const auto last = [&p]
    { return static_cast<const int8_t>(p[-1]); };

    // Shifts "byte" left by n * 7 bits, filling vacated bits from `ones`.
  constexpr auto shl_byte = [](int n, int8_t byte, int64_t ones) constexpr -> int64_t {
    return static_cast<int64_t>((static_cast<uint64_t>(byte) << n * 7) | (static_cast<uint64_t>(ones) >> (64 - n * 7)));
  };

  constexpr auto shl_and = [shl_byte](int n, int8_t byte, int64_t ones, int64_t &res) {
    res &= shl_byte(n, byte, ones);
    return res >= 0;
  };

  constexpr auto shl = [shl_byte](int n, int8_t byte, int64_t ones, int64_t &res) {
    res = shl_byte(n, byte, ones);
    return res >= 0;
  };


    int64_t res2, res3; // accumulated result chunks

    const auto done1 = [&]
    {
        res1 &= res2;
        // __builtin_assume(p != nullptr);
        return p;
    };

    const auto done2 = [&]
    {
        res2 &= res3;
        return done1();
    };

    res1 = next();
    if (res1 >= 0) [[likely]]
        return p;

    // Densify all ops with explicit FALSE predictions from here on, except that
    // we predict length = 5 as a common length for fields like timestamp.
    if (shl(1, next(), res1, res2)) [[unlikely]]
        return done1();

    if (shl(2, next(), res1, res3)) [[unlikely]]
        return done2();

    if (shl_and(3, next(), res1, res2)) [[unlikely]]
        return done2();

    if (shl_and(4, next(), res1, res3)) [[likely]]
        return done2();

    if (kIs64BitVarint)
    {
        if (shl_and(5, next(), res1, res2)) [[unlikely]]
            return done2();

        if (shl_and(6, next(), res1, res3)) [[unlikely]]
            return done2();

        if (shl_and(7, next(), res1, res2)) [[unlikely]]
            return done2();

        if (shl_and(8, next(), res1, res3)) [[unlikely]]
            return done2();
    }
    else
    {
        // An overlong int32 is expected to span the full 10 bytes
        if (!(next() & 0x80)) [[unlikely]]
            return done2();

        if (!(next() & 0x80)) [[unlikely]]
            return done2();

        if (!(next() & 0x80)) [[unlikely]]
            return done2();

        if (!(next() & 0x80)) [[unlikely]]
            return done2();
    }

    // For valid 64bit varints, the 10th byte/ptr[9] should be exactly 1. In this
    // case, the continuation bit of ptr[8] already set the top bit of res3
    // correctly, so all we have to do is check that the expected case is true.
    if (next() == 1) [[likely]]
        return done2();

    if (last() & 0x80) [[likely]]
    {
        // If the continue bit is set, it is an unterminated varint.
        return nullptr;
    }

    // A zero value of the first bit of the 10th byte represents an
    // over-serialized varint. This case should not happen, but if does (say, due
    // to a nonconforming serializer), deassert the continuation bit that came
    // from ptr[8].
    if (kIs64BitVarint && (last() & 1) == 0)
    {
        constexpr int bits = 64 - 1;
#if defined(__GCC_ASM_FLAG_OUTPUTS__) && defined(__x86_64__)
        // Use a small instruction since this is an uncommon code path.
        asm("btc %[bits], %[res3]" : [res3] "+r"(res3) : [bits] "i"(bits));
#else
        res3 ^= int64_t{1} << bits;
#endif
    }
    return done2();
}

template <typename Type>
struct shift_mix_parse_varint_op
{
    using type = Type;
    std::errc operator()(Type &value, std::span<char> &data) const
    {
        auto end = data.data() + data.size();
        int64_t v;
        auto p = shift_mix_parse_varint<Type>(data.data(), v);
        value = v;
        if (p == nullptr) [[unlikely]]
            return std::errc::value_too_large;
        data = std::span<char>(const_cast<char *>(p), end);
        return {};
    }
};

template <typename Type>
std::size_t pack_varint(Type orig_value, std::span<char> &data)
{
    auto value = std::make_unsigned_t<Type>(orig_value);
    if (data.size() >= varint_max_size<Type>)
    {
        std::size_t position = 0;
        while (value >= 0x80)
        {
            data[position++] = char((value & 0x7f) | 0x80);
            value >>= (CHAR_BIT - 1);
        }
        data[position++] = char(value);
        data = data.subspan(position);
        return position;
    }
    return 0;
}