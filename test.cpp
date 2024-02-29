#include "parse_varint.h"

#include <boost/ut.hpp>

using namespace boost::ut;

suite varint_test = []
{
    auto verify = [](auto arg)
    {
        using arg_type = decltype(arg);
        std::array<char, 10> storage;
        std::span<char> data{storage}, data1{storage}, data2{storage};
        const parse_varint_unrolled<arg_type> p1;
        const shift_mix_parse_varint_op<arg_type> p2;

        pack_varint(arg, data);

        decltype(arg) x1, x2;
        expect(p1(x1, data1) == std::errc{});
        expect(p2(x2, data2) == std::errc{});
        expect(data1.size() == data2.size());
        expect(data1.data() == data2.data());
        expect(x1 == arg);
        expect(x2 == arg);
    };

    "uint64"_test = verify | std::vector<uint64_t>{100, 2000, 450000, 450000000, 450000000000ULL, 4500000000000000ULL, 4500000000000000000ULL};
    "int64"_test = verify | std::vector<int64_t>{-1, -100, -2000, -450000, -450000000, -450000000000LL, -4500000000000000LL, 4500000000000000000LL};

};

int main() {}