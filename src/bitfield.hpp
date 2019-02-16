#pragma once

#include <cstddef>
#include <type_traits>
#include "compile_time_utilities.hpp"

#if defined(ALWAYS_INLINE)
#define _have_macro_ALWAYS_INLINE
#pragma push_macro("ALWAYS_INLINE")
#undef ALWAYS_INLINE
#endif

#if defined(_MSC_VER) || defined(__ICC)
#define ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define ALWAYS_INLINE __attribute__((always_inline))
#else
#define ALWAYS_INLINE
#endif

template<typename _base_type, size_t _first_bit, size_t _last_bit = _first_bit>
struct bitfield {
    using value_type = std::remove_volatile_t<_base_type>;
    using storage_type = _base_type;

    static_assert(std::is_integral_v<_base_type>);
    static_assert(std::is_const_v<_base_type> == false);
    static_assert(_first_bit <= _last_bit);
    static_assert(_last_bit < ctu::bits_of<_base_type>);

    static constexpr value_type first_bit = _first_bit;
    static constexpr value_type last_bit = _last_bit;
    static constexpr value_type num_bits = _last_bit - _first_bit + 1;
    static constexpr value_type mask = ctu::bit_mask_v<value_type, num_bits>;

    ALWAYS_INLINE bitfield() = default;
    ALWAYS_INLINE bitfield(value_type val) : _raw(_assign(_raw, val)) {
    }

    ALWAYS_INLINE explicit operator bool() const {
        return (_raw & (mask << first_bit)) != 0;
    }

    ALWAYS_INLINE operator value_type() const {
        if constexpr (std::is_signed_v<value_type>) {
            return ((_raw << (ctu::bits_of<value_type> - last_bit - 1)) >> (ctu::bits_of<value_type> - num_bits));
        } else {
            return (_raw >> first_bit) & mask;
        }
    }

    ALWAYS_INLINE bitfield& operator=(value_type new_value) {
        _raw = _assign(_raw, new_value);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator+=(value_type rhs) {
        _raw = _add(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator-=(value_type rhs) {
        _raw = _sub(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator*=(value_type rhs) {
        _raw = _mul(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator/=(value_type rhs) {
        _raw = _div(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator%=(value_type rhs) {
        _raw = _mod(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator&=(value_type rhs) {
        _raw = _and(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator|=(value_type rhs) {
        _raw = _or(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator^=(value_type rhs) {
        _raw = _xor(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator<<=(value_type rhs) {
        _raw = _shl(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE bitfield& operator>>=(value_type rhs) {
        _raw = _shr(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE value_type operator++() { // pre-increment
        value_type tmp = _raw;
        value_type val = ((tmp >> first_bit) + 1) & mask;
        _raw = _assign(tmp, val);
        if constexpr (std::is_signed_v<value_type>) {
            val <<= ctu::bits_of<value_type> - num_bits;
            val >>= ctu::bits_of<value_type> - num_bits;
        }
        return val;
    }

    ALWAYS_INLINE value_type operator++(int) { // post-increment
        value_type tmp = _raw;
        _raw = _add(tmp, 1);
        if constexpr (std::is_signed_v<value_type>) {
            return ((tmp << (ctu::bits_of<value_type> - last_bit - 1)) >> (ctu::bits_of<value_type> - num_bits));
        } else {
            return (tmp >> first_bit) & mask;
        }
    }

    ALWAYS_INLINE value_type operator--() { // pre-decrement
        value_type tmp = _raw;
        value_type val = ((tmp >> first_bit) - 1) & mask;
        _raw = _assign(tmp, val);
        if constexpr (std::is_signed_v<value_type>) {
            val <<= ctu::bits_of<value_type> - num_bits;
            val >>= ctu::bits_of<value_type> - num_bits;
        }
        return val;
    }

    ALWAYS_INLINE value_type operator--(int) { // post-decrement
        value_type tmp = _raw;
        _raw = _sub(tmp, 1);
        if constexpr (std::is_signed_v<value_type>) {
            return ((tmp << (ctu::bits_of<value_type> - last_bit - 1)) >> (ctu::bits_of<value_type> - num_bits));
        } else {
            return (tmp >> first_bit) & mask;
        }
    }

    // Now overload all those operators on volatile. Without the non-volatile
    // versions compilers cannot delete calls to these operators, even if the
    // instance is not volatile.

    ALWAYS_INLINE explicit operator bool() const volatile {
        return (_raw & (mask << first_bit)) != 0;
    }

    ALWAYS_INLINE operator value_type() const volatile {
        if constexpr (std::is_signed_v<value_type>) {
            return ((_raw << (ctu::bits_of<value_type> - last_bit - 1)) >> (ctu::bits_of<value_type> - num_bits));
        } else {
            return (_raw >> first_bit) & mask;
        }
    }

    ALWAYS_INLINE volatile bitfield& operator=(value_type new_value) volatile {
        _raw = _assign(_raw, new_value);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator+=(value_type rhs) volatile {
        _raw = _add(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator-=(value_type rhs) volatile {
        _raw = _sub(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator*=(value_type rhs) volatile {
        _raw = _mul(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator/=(value_type rhs) volatile {
        _raw = _div(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator%=(value_type rhs) volatile {
        _raw = _mod(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator&=(value_type rhs) volatile {
        _raw = _and(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator|=(value_type rhs) volatile {
        _raw = _or(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator^=(value_type rhs) volatile {
        _raw = _xor(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator<<=(value_type rhs) volatile {
        _raw = _shl(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE volatile bitfield& operator>>=(value_type rhs) volatile {
        _raw = _shr(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE value_type operator++() volatile { // pre-increment
        value_type tmp = _raw;
        value_type val = ((tmp >> first_bit) + 1) & mask;
        _raw = _assign(tmp, val);
        if constexpr (std::is_signed_v<value_type>) {
            val <<= ctu::bits_of<value_type> - num_bits;
            val >>= ctu::bits_of<value_type> - num_bits;
        }
        return val;
    }

    ALWAYS_INLINE value_type operator++(int) volatile { // post-increment
        value_type tmp = _raw;
        _raw = _add(tmp, 1);
        if constexpr (std::is_signed_v<value_type>) {
            return ((tmp << (ctu::bits_of<value_type> - last_bit - 1)) >> (ctu::bits_of<value_type> - num_bits));
        } else {
            return (tmp >> first_bit) & mask;
        }
    }

    ALWAYS_INLINE value_type operator--() volatile { // pre-decrement
        value_type tmp = _raw;
        value_type val = ((tmp >> first_bit) - 1) & mask;
        _raw = _assign(tmp, val);
        if constexpr (std::is_signed_v<value_type>) {
            val <<= ctu::bits_of<value_type> - num_bits;
            val >>= ctu::bits_of<value_type> - num_bits;
        }
        return val;
    }

    ALWAYS_INLINE value_type operator--(int) volatile { // post-decrement
        value_type tmp = _raw;
        _raw = _sub(tmp, 1);
        if constexpr (std::is_signed_v<value_type>) {
            return ((tmp << (ctu::bits_of<value_type> - last_bit - 1)) >> (ctu::bits_of<value_type> - num_bits));
        } else {
            return (tmp >> first_bit) & mask;
        }
    }

private:
    ALWAYS_INLINE static value_type _assign(value_type lhs, value_type rhs) {
        return (lhs & ~(mask << first_bit)) | ((rhs & mask) << first_bit);
    }

    ALWAYS_INLINE static value_type _add(value_type lhs, value_type rhs) {
        return (lhs & ~(mask << first_bit)) | ((lhs + (rhs << first_bit)) & (mask << first_bit));
    }

    ALWAYS_INLINE static value_type _sub(value_type lhs, value_type rhs) {
        return (lhs & ~(mask << first_bit)) | ((lhs - (rhs << first_bit)) & (mask << first_bit));
    }

    ALWAYS_INLINE static value_type _mul(value_type lhs, value_type rhs) {
        return (lhs & ~(mask << first_bit)) | (((lhs & (mask << first_bit)) * rhs) & (mask << first_bit));
    }

    ALWAYS_INLINE static value_type _div(value_type lhs, value_type rhs) {
        return (lhs & ~(mask << first_bit)) | (((lhs & (mask << first_bit)) / rhs) & (mask << first_bit));
    }

    ALWAYS_INLINE static value_type _mod(value_type lhs, value_type rhs) {
        return (lhs & ~(mask << first_bit)) | (lhs % ((rhs & mask) << first_bit));
    }

    ALWAYS_INLINE static value_type _and(value_type lhs, value_type rhs) {
        return lhs & (~(mask << first_bit) | (rhs << first_bit));
    }

    ALWAYS_INLINE static value_type _or(value_type lhs, value_type rhs) {
        return lhs | ((rhs & mask) << first_bit);
    }

    ALWAYS_INLINE static value_type _xor(value_type lhs, value_type rhs) {
        return lhs ^ ((rhs & mask) << first_bit);
    }

    ALWAYS_INLINE static value_type _shl(value_type lhs, value_type rhs) {
        return (lhs & ~(mask << first_bit)) | (((lhs & (mask << first_bit)) << rhs) & (mask << first_bit));
    }

    ALWAYS_INLINE static value_type _shr(value_type lhs, value_type rhs) {
        return (lhs & ~(mask << first_bit)) | (((lhs & (mask << first_bit)) >> rhs) & (mask << first_bit));
    }

private:
    _base_type _raw;
};

static_assert(std::is_standard_layout_v<bitfield<unsigned, 0>>);
static_assert(std::is_trivial_v<bitfield<unsigned, 0>>);

#if defined(_have_macro_ALWAYS_INLINE)
#undef _have_macro_ALWAYS_INLINE
#undef ALWAYS_INLINE
#pragma pop_macro("ALWAYS_INLINE")
#endif
