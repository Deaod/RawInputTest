#pragma once

#include <climits>
#include <cstddef>

namespace ctu {

namespace detail {
template<typename... types>
constexpr size_t size_of_impl_() {
    size_t result = 0;
    constexpr size_t sizes[]{ sizeof(types)... };
    for (auto size : sizes) {
        result += size;
    }
    return result;
}

template<>
constexpr size_t size_of_impl_<>() {
    return 0;
}

} // namespace detail

template<typename... types>
constexpr const auto size_of = detail::size_of_impl_<types...>();

template<typename... types>
constexpr const auto bits_of = size_of<types...> * CHAR_BIT;

template<typename type>
constexpr int log2(type val) {
    if (val < 0) {
        return int(bits_of<type> - 1);
    } else if (val == 0) {
        return int(bits_of<type>);
    } else if (val == 1) {
        return 0;
    } else {
        return 1 + log2(val >> 1);
    }
}

template<auto val>
constexpr int log2_v = log2(val);

template<typename type>
constexpr type round_up_bits(type val, int bits) {
    const type mask = type((type(1) << bits) - 1);
    return (val + mask) & ~mask;
}

template<typename real>
constexpr real deg_to_rad(real angle) {
    constexpr real pi = real(3.14159265358979323846);
    return angle * (pi / real(180.0));
}

template<typename type>
constexpr type clamp(type val, type from, type to) {
    if (val < from) return from;
    if (val > to) return to;
    return val;
}

} // namespace ctu
