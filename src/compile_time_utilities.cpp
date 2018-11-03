#include "compile_time_utilities.hpp"

static_assert(ctu::size_of<> == 0);
static_assert(ctu::size_of<int> == sizeof(int));
static_assert(ctu::size_of<int, char> == sizeof(int) + sizeof(char));

static_assert(ctu::bits_of<> == 0);
static_assert(ctu::bits_of<char> == CHAR_BIT);
static_assert(ctu::bits_of<int> == sizeof(int) * CHAR_BIT);
static_assert(ctu::bits_of<int, char> == (sizeof(int) + sizeof(char)) * CHAR_BIT);

static_assert(ctu::bit_mask_v<int, 32> == 0xFFFFFFFF);
static_assert(ctu::bit_mask_v<int, 16> == 0xFFFF);
static_assert(ctu::bit_mask_v<int, 8> == 0xFF);
static_assert(ctu::bit_mask_v<int, 4> == 0xF);
static_assert(ctu::bit_mask_v<int, 2> == 3);
static_assert(ctu::bit_mask_v<int, 1> == 1);
static_assert(ctu::bit_mask_v<int, 0> == 0);

static_assert(ctu::log2(0) == ctu::bits_of<int>);
static_assert(ctu::log2(-1) == ctu::bits_of<int> -1);

static_assert(ctu::log2(1ull) == 0);
static_assert(ctu::log2(2ull) == 1);
static_assert(ctu::log2(3ull) == 1);
static_assert(ctu::log2(4ull) == 2);
static_assert(ctu::log2(7ull) == 2);
static_assert(ctu::log2(8ull) == 3);
static_assert(ctu::log2(SIZE_MAX) == (ctu::bits_of<size_t> -1));

static_assert(ctu::round_up_bits(0, 0) == 0);
static_assert(ctu::round_up_bits(1, 0) == 1);
static_assert(ctu::round_up_bits(2, 0) == 2);
static_assert(ctu::round_up_bits(0, 1) == 0);
static_assert(ctu::round_up_bits(1, 1) == 2);
static_assert(ctu::round_up_bits(2, 1) == 2);
static_assert(ctu::round_up_bits(3, 1) == 4);
static_assert(ctu::round_up_bits(0, 2) == 0);
static_assert(ctu::round_up_bits(1, 2) == 4);
static_assert(ctu::round_up_bits(2, 2) == 4);
static_assert(ctu::round_up_bits(3, 2) == 4);
static_assert(ctu::round_up_bits(4, 2) == 4);
static_assert(ctu::round_up_bits(7, 2) == 8);
