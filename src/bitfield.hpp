#pragma once

#include <limits>
#include <climits>
#include <cstddef>
#include <type_traits>

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

template<typename base_type, size_t first_bit, size_t last_bit = first_bit>
class bitfield {
public:
    using value_type = std::remove_volatile_t<base_type>;
    using storage_type = base_type;

private:
    static_assert(std::is_integral_v<base_type>);
    static_assert(std::is_const_v<base_type> == false);
    static_assert(std::is_signed_v<value_type> == false);
    static_assert(first_bit <= last_bit);
    static_assert(last_bit < CHAR_BIT * sizeof(base_type));

    static constexpr value_type NUM_BITS = last_bit - first_bit + 1;
    static constexpr value_type MASK = (static_cast<value_type>(1) << NUM_BITS) - 1;

public:
    ALWAYS_INLINE constexpr bitfield() = default;
    ALWAYS_INLINE constexpr bitfield(value_type val)  {
        _assign(_raw, val);
    }

    ALWAYS_INLINE constexpr explicit operator bool() const {
        return (_raw & (MASK << first_bit)) != 0;
    }

    ALWAYS_INLINE constexpr operator value_type() const {
        return (_raw >> first_bit) & MASK;
    }

    ALWAYS_INLINE constexpr bitfield& operator=(value_type new_value) {
        _raw = _assign(_raw, new_value);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator+=(value_type rhs) {
        _raw = _add(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator-=(value_type rhs) {
        _raw = _sub(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator*=(value_type rhs) {
        _raw = _mul(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator/=(value_type rhs) {
        _raw = _div(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator%=(value_type rhs) {
        _raw = _mod(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator&=(value_type rhs) {
        _raw = _and(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator|=(value_type rhs) {
        _raw = _or(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator^=(value_type rhs) {
        _raw = _xor(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator<<=(value_type rhs) {
        _raw = _shl(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr bitfield& operator>>=(value_type rhs) {
        _raw = _shr(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr value_type operator++() { // pre-increment
        value_type tmp = _raw;
        value_type val = ((tmp >> first_bit) + 1) & MASK;
        _raw = _assign(tmp, val);
        return val;
    }

    ALWAYS_INLINE constexpr value_type operator++(int) { // post-increment
        value_type tmp = _raw;
        _raw = _add(tmp, 1);
        return (tmp >> first_bit) & MASK;
    }

    ALWAYS_INLINE constexpr value_type operator--() { // pre-decrement
        value_type tmp = _raw;
        value_type val = ((tmp >> first_bit) - 1) & MASK;
        _raw = _assign(tmp, val);
        return val;
    }

    ALWAYS_INLINE constexpr value_type operator--(int) { // post-decrement
        value_type tmp = _raw;
        _raw = _sub(tmp, 1);
        return (tmp >> first_bit) & MASK;
    }

    // Now overload all those operators on volatile. Without the non-volatile
    // versions compilers cannot delete calls to these operators, even if the
    // instance is not volatile.

    ALWAYS_INLINE constexpr explicit operator bool() const volatile {
        return (_raw & (MASK << first_bit)) != 0;
    }

    ALWAYS_INLINE constexpr operator value_type() const volatile {
        return (_raw >> first_bit) & MASK;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator=(value_type new_value) volatile {
        _raw = _assign(_raw, new_value);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator+=(value_type rhs) volatile {
        _raw = _add(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator-=(value_type rhs) volatile {
        _raw = _sub(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator*=(value_type rhs) volatile {
        _raw = _mul(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator/=(value_type rhs) volatile {
        _raw = _div(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator%=(value_type rhs) volatile {
        _raw = _mod(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator&=(value_type rhs) volatile {
        _raw = _and(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator|=(value_type rhs) volatile {
        _raw = _or(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator^=(value_type rhs) volatile {
        _raw = _xor(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator<<=(value_type rhs) volatile {
        _raw = _shl(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr volatile bitfield& operator>>=(value_type rhs) volatile {
        _raw = _shr(_raw, rhs);
        return *this;
    }

    ALWAYS_INLINE constexpr value_type operator++() volatile { // pre-increment
        value_type tmp = _raw;
        value_type val = ((tmp >> first_bit) + 1) & MASK;
        _raw = _assign(tmp, val);
        return val;
    }

    ALWAYS_INLINE constexpr value_type operator++(int) volatile { // post-increment
        value_type tmp = _raw;
        _raw = _add(tmp, 1);
        return (tmp >> first_bit) & MASK;
    }

    ALWAYS_INLINE constexpr value_type operator--() volatile { // pre-decrement
        value_type tmp = _raw;
        value_type val = ((tmp >> first_bit) - 1) & MASK;
        _raw = _assign(tmp, val);
        return val;
    }

    ALWAYS_INLINE constexpr value_type operator--(int) volatile { // post-decrement
        value_type tmp = _raw;
        _raw = _sub(tmp, 1);
        return (tmp >> first_bit) & MASK;
    }

private:
    ALWAYS_INLINE static constexpr value_type _assign(value_type lhs, value_type rhs) {
        return (lhs & ~(MASK << first_bit)) | ((rhs & MASK) << first_bit);
    }

    ALWAYS_INLINE static constexpr value_type _add(value_type lhs, value_type rhs) {
        return (lhs & ~(MASK << first_bit)) | ((lhs + (rhs << first_bit)) & (MASK << first_bit));
    }

    ALWAYS_INLINE static constexpr value_type _sub(value_type lhs, value_type rhs) {
        return (lhs & ~(MASK << first_bit)) | ((lhs - (rhs << first_bit)) & (MASK << first_bit));
    }

    ALWAYS_INLINE static constexpr value_type _mul(value_type lhs, value_type rhs) {
        return (lhs & ~(MASK << first_bit)) | (((lhs & (MASK << first_bit)) * rhs) & (MASK << first_bit));
    }

    ALWAYS_INLINE static constexpr value_type _div(value_type lhs, value_type rhs) {
        return (lhs & ~(MASK << first_bit)) | (((lhs & (MASK << first_bit)) / rhs) & (MASK << first_bit));
    }

    ALWAYS_INLINE static constexpr value_type _mod(value_type lhs, value_type rhs) {
        return (lhs & ~(MASK << first_bit)) | (lhs % ((rhs & MASK) << first_bit));
    }

    ALWAYS_INLINE static constexpr value_type _and(value_type lhs, value_type rhs) {
        return lhs & (~(MASK << first_bit) | (rhs << first_bit));
    }

    ALWAYS_INLINE static constexpr value_type _or(value_type lhs, value_type rhs) {
        return lhs | ((rhs & MASK) << first_bit);
    }

    ALWAYS_INLINE static constexpr value_type _xor(value_type lhs, value_type rhs) {
        return lhs ^ ((rhs & MASK) << first_bit);
    }

    ALWAYS_INLINE static constexpr value_type _shl(value_type lhs, value_type rhs) {
        return (lhs & ~(MASK << first_bit)) | (((lhs & (MASK << first_bit)) << rhs) & (MASK << first_bit));
    }

    ALWAYS_INLINE static constexpr value_type _shr(value_type lhs, value_type rhs) {
        return (lhs & ~(MASK << first_bit)) | (((lhs & (MASK << first_bit)) >> rhs) & (MASK << first_bit));
    }

private:
    base_type _raw;
};

static_assert(std::is_standard_layout_v<bitfield<unsigned, 0>>);
static_assert(std::is_pod_v<bitfield<unsigned, 0>>);

#if defined(_have_macro_ALWAYS_INLINE)
#undef _have_macro_ALWAYS_INLINE
#undef ALWAYS_INLINE
#pragma pop_macro("ALWAYS_INLINE")
#endif
