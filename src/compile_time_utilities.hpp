#pragma once

#include <climits>
#include <cstddef>
#include <type_traits>

namespace ctu {

namespace _detail {
template<typename... types>
constexpr size_t _size_of_impl() {
    size_t result = 0;
    constexpr size_t sizes[]{ sizeof(types)... };
    for (auto size : sizes) {
        result += size;
    }
    return result;
}

template<>
constexpr size_t _size_of_impl<>() {
    return 0;
}

template<typename type>
struct _bit_mask_all {
    static constexpr type value = type(~type(0));
};

template<typename type, int width>
struct _bit_mask_partial {
    static constexpr type value = type((type(1) << width) - 1);
};

} // namespace detail

template<typename... types>
constexpr const size_t size_of = _detail::_size_of_impl<types...>();

template<typename... types>
constexpr const size_t bits_of = size_of<types...> * CHAR_BIT;

template<typename type, int width>
constexpr type bit_mask_v = std::conditional_t<
    (width > bits_of<type>),
    void,
    std::conditional_t<
        width == bits_of<type>,
        _detail::_bit_mask_all<type>,
        _detail::_bit_mask_partial<type, width>
    >
>::value;

template<typename type>
constexpr type bit_mask(int width) {
    return type((type(1) << width) - 1);
}

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
    const type mask = bit_mask<type>(bits);
    return (val + mask) & ~mask;
}

template<typename real>
constexpr real deg_to_rad(real angle) {
    constexpr real pi = real(3.14159265358979323846);
    return angle * (pi / real(180.0));
}

template<typename type>
constexpr type clamp(type val, type from, type to) {
    return std::clamp(val, from, to);
}

} // namespace ctu
