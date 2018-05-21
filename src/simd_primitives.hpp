#pragma once

#include <immintrin.h>
#include "types.hpp"

__m128 operator+(__m128 a, __m128 b) {
    return _mm_add_ps(a, b);
}

__m128 operator-(__m128 a, __m128 b) {
    return _mm_sub_ps(a, b);
}

__m128 operator*(__m128 a, __m128 b) {
    return _mm_mul_ps(a, b);
}

__m128 operator*(__m128 a, float b) {
    return a * _mm_set1_ps(b);
}

__m128 operator*(float a, __m128 b) {
    return _mm_set1_ps(a) * b;
}

__m128 operator/(__m128 a, __m128 b) {
    return _mm_div_ps(a, b);
}

__m128 operator/(__m128 a, float b) {
    return a / _mm_set1_ps(b);
}

__m128 operator/(float a, __m128 b) {
    return _mm_set1_ps(a) / b;
}

__m128 operator^(__m128 a, __m128 b) {
    return _mm_xor_ps(a, b);
}

bool operator==(__m128 a, __m128 b) {
    auto reg = _mm_cmpeq_ps(a, b);
    return reg.m128_i32[0] & reg.m128_i32[1] & reg.m128_i32[2] & reg.m128_i32[3];
}

float dotp(__m128 a, __m128 b) {
    return _mm_dp_ps(a, b, 0b1111'0001).m128_f32[0];
}

__m128 quat_mul(__m128 a, __m128 b) {
    __m128 p0 = _mm_set1_ps(a.m128_f32[0]) * b;
    __m128 p1 = _mm_fmaddsub_ps(_mm_set1_ps(a.m128_f32[1]), _mm_shuffle_ps(b, b, 0b0'10'11'00'01), p0);
    __m128 p2 = _mm_fmadd_ps(_mm_set1_ps(a.m128_f32[2]) ^ __m128({ -0.0f, 0.0f, 0.0f, -0.0f }), _mm_shuffle_ps(b, b, 0b0'01'00'11'10), p1);
    return _mm_fmadd_ps(_mm_set1_ps(a.m128_f32[3]) ^ __m128({ -0.0f, -0.0f, 0.0f, 0.0f }), _mm_shuffle_ps(b, b, 0b0'00'01'10'11), p2);
}

__m128 quat_reciprocal(__m128 a) {
    return (a ^ __m128({ 0.0f, -0.0f, -0.0f, -0.0f })) / _mm_dp_ps(a, a, 0b1111'1111);
}

__m128 vector_unit(__m128 a) {
    return _mm_div_ps(a, _mm_sqrt_ps(_mm_dp_ps(a, a, 0b1111'1111)));
}

__m128 vector_cross(__m128 a, __m128 b) {
    __m128 a1 = _mm_shuffle_ps(a, a, 0b0'01'11'10'00);
    __m128 a2 = _mm_shuffle_ps(a, a, 0b0'10'01'11'00);
    __m128 b1 = _mm_shuffle_ps(b, b, 0b0'01'11'10'00);
    __m128 b2 = _mm_shuffle_ps(b, b, 0b0'10'01'11'00);
    return _mm_fmsub_ps(a1, b2, a2*b1);
}

struct quaternion;

struct __declspec(align(sizeof(__m128))) vector3 {
    __m128 reg;

    vector3() : reg{ 0.0f } {}
    vector3(f32 x, f32 y, f32 z) : reg{ 0.0f, x, y, z } {}
    explicit vector3(__m128 reg) : reg(reg) {}

    f32 operator[](size_t index) const {
        return reg.m128_f32[index + 1];
    }

    f32 x() const {
        return reg.m128_f32[1];
    }

    f32 y() const {
        return reg.m128_f32[2];
    }

    f32 z() const {
        return reg.m128_f32[3];
    }

    __m128 simd_reg() const {
        return reg;
    }

    f32 length() const {
        return sqrt(dotp(reg, reg));
    }

    vector3& operator+=(const vector3& b) {
        reg = reg + b.reg;
        return *this;
    }

    vector3& operator-=(const vector3& b) {
        reg = reg - b.reg;
        return *this;
    }

    vector3& operator*=(f32 scale) {
        reg = reg * scale;
        return *this;
    }

    vector3& operator/=(f32 scale) {
        reg = reg / scale;
        return *this;
    }

    vector3 operator+(const vector3& other) const {
        return vector3{ reg + other.reg };
    }

    vector3 operator-(const vector3& other) const {
        return vector3{ reg - other.reg };
    }

    vector3 operator*(f32 b) const {
        return vector3{ reg * b };
    }

    vector3 operator/(f32 b) const {
        return vector3{ reg / b };
    }

    vector3 unit() const {
        return vector3{ vector_unit(reg) };
    }

    vector3 cross(const vector3& b) const {
        return vector3{ vector_cross(reg, b.reg) };
    }

    f32 dot(const vector3& b) const {
        return dotp(reg, b.reg);
    }

    f32 cos_theta(const vector3& b) const {
        return dot(b) / (length() * b.length());
    }

    f32 theta(const vector3& b) const {
        return acos(cos_theta(b));
    }

    bool operator==(const vector3& b) const {
        return (x() == b.x()) & (y() == b.y()) & (z() == b.z());
    }

    vector3 rotate(const quaternion& q) const;
};

struct __declspec(align(4 * sizeof(f32))) quaternion {
    __m128 reg;

    explicit quaternion(__m128 reg) : reg(reg) {}

    quaternion(f32 w, f32 x, f32 y, f32 z) : reg{ w, x, y, z } {}

    f32 w() const {
        return reg.m128_f32[0];
    }

    f32 x() const {
        return reg.m128_f32[1];
    }

    f32 y() const {
        return reg.m128_f32[2];
    }

    f32 z() const {
        return reg.m128_f32[3];
    }

    f32 norm() const {
        return dotp(reg, reg);
    }

    quaternion conjugate() const {
        return quaternion{ reg ^ __m128({ 0.0f, -0.0f, -0.0f, -0.0f }) };
    }

    quaternion reciprocal() const {
        return quaternion{ quat_reciprocal(reg) };
    }

    f32 length() const {
        return sqrt(norm());
    }

    quaternion operator+(const quaternion& other) const {
        return quaternion{ reg + other.reg };
    }

    quaternion operator*(f32 s) const {
        return quaternion{ reg * s };
    }

    quaternion operator/(f32 s) const {
        return quaternion{ reg / s };
    }

    quaternion operator*(const quaternion& q) const {
        return quaternion{ quat_mul(reg, q.reg) };
    }

    f32 dot(const quaternion& q) const {
        return dotp(reg, q.reg);
    }

    vector3 rotate(const vector3& p) const {
        auto pstar = *this * quaternion{ p.reg } *reciprocal();
        return vector3{ pstar.x(), pstar.y(), pstar.z() };
    }

    bool operator==(const quaternion& q) const {
        return reg == q.reg;
    }
};

vector3 vector3::rotate(const quaternion& q) const {
    return q.rotate(*this);
}