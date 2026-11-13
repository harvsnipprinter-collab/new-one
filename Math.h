// SculptForge 3D - core math types (header only)
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace sf {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit Vec3(float s) : x(s), y(s), z(s) {}
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(Vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
};

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length2(Vec3 a) { return dot(a, a); }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    float l = length(a);
    return l > 1e-20f ? a / l : Vec3(0, 0, 0);
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(Vec3 a, Vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline bool isFinite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

struct AABB {
    Vec3 mn{std::numeric_limits<float>::max()};
    Vec3 mx{-std::numeric_limits<float>::max()};
    void expand(Vec3 p) { mn = vmin(mn, p); mx = vmax(mx, p); }
    void expand(const AABB& b) { mn = vmin(mn, b.mn); mx = vmax(mx, b.mx); }
    bool valid() const { return mn.x <= mx.x; }
    Vec3 center() const { return (mn + mx) * 0.5f; }
    Vec3 size() const { return mx - mn; }
    // squared distance from point to box (0 if inside)
    float dist2(Vec3 p) const {
        float d = 0;
        for (int i = 0; i < 3; ++i) {
            float v = p[i];
            if (v < mn[i]) d += (mn[i] - v) * (mn[i] - v);
            else if (v > mx[i]) d += (v - mx[i]) * (v - mx[i]);
        }
        return d;
    }
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    static Quat axisAngle(Vec3 axis, float angle) {
        Vec3 a = normalize(axis);
        float s = std::sin(angle * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
    }
    Quat operator*(const Quat& o) const {
        return {w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w,
                w * o.w - x * o.x - y * o.y - z * o.z};
    }
    Vec3 rotate(Vec3 v) const {
        Vec3 q{x, y, z};
        Vec3 t = cross(q, v) * 2.0f;
        return v + t * w + cross(q, t);
    }
};

// Column-major 4x4 matrix (same memory layout as simd_float4x4 / Metal).
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    static Mat4 trs(Vec3 t, Quat q, Vec3 s) {
        Mat4 r;
        Vec3 cx = q.rotate({1, 0, 0}) * s.x, cy = q.rotate({0, 1, 0}) * s.y, cz = q.rotate({0, 0, 1}) * s.z;
        r.m[0] = cx.x; r.m[1] = cx.y; r.m[2] = cx.z;
        r.m[4] = cy.x; r.m[5] = cy.y; r.m[6] = cy.z;
        r.m[8] = cz.x; r.m[9] = cz.y; r.m[10] = cz.z;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rw = 0; rw < 4; ++rw) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += at(rw, k) * o.at(k, c);
                r.at(rw, c) = s;
            }
        return r;
    }
    Vec3 point(Vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    Vec3 dir(Vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z,
                m[1] * p.x + m[5] * p.y + m[9] * p.z,
                m[2] * p.x + m[6] * p.y + m[10] * p.z};
    }
    float det3() const {
        return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) +
               m[8] * (m[1] * m[6] - m[5] * m[2]);
    }
    // Inverse of an affine matrix (last row 0,0,0,1). Returns identity if singular.
    Mat4 affineInverse() const {
        float d = det3();
        Mat4 r;
        if (std::fabs(d) < 1e-30f) return r;
        float id = 1.0f / d;
        r.m[0] = (m[5] * m[10] - m[9] * m[6]) * id;
        r.m[1] = -(m[1] * m[10] - m[9] * m[2]) * id;
        r.m[2] = (m[1] * m[6] - m[5] * m[2]) * id;
        r.m[4] = -(m[4] * m[10] - m[8] * m[6]) * id;
        r.m[5] = (m[0] * m[10] - m[8] * m[2]) * id;
        r.m[6] = -(m[0] * m[6] - m[4] * m[2]) * id;
        r.m[8] = (m[4] * m[9] - m[8] * m[5]) * id;
        r.m[9] = -(m[0] * m[9] - m[8] * m[1]) * id;
        r.m[10] = (m[0] * m[5] - m[4] * m[1]) * id;
        Vec3 t{m[12], m[13], m[14]};
        Vec3 it = r.dir(t);
        r.m[12] = -it.x; r.m[13] = -it.y; r.m[14] = -it.z;
        return r;
    }
};

} // namespace sf
