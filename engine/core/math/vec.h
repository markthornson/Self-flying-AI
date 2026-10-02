#pragma once

// Vectors: the basic building block of 3D math.
//
// A vector is just a few floats. We use it both for points ("the cube is at
// (1, 0, 3)") and for directions ("move along +X"). The engine's coordinate
// system is right-handed with +Y up and the camera looking down -Z, the same
// convention glTF and Blender's glTF exporter use.
//
// Everything here is constexpr or inline so the compiler can fold it away;
// there is no hidden cost to writing a + b instead of three float adds.

#include <cmath>

namespace eng {

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

// --- Vec3 operators ----------------------------------------------------------

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }
constexpr Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }
constexpr Vec3 operator/(Vec3 v, float s) { return {v.x / s, v.y / s, v.z / s}; }
constexpr bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// Dot product: |a| |b| cos(angle). Zero means the vectors are perpendicular,
// positive means they point roughly the same way. Lighting uses this.
constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Cross product: a vector perpendicular to both a and b, following the
// right-hand rule (cross(+X, +Y) = +Z). Used to build camera axes.
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

// Returns a vector of length 1 pointing the same way. A zero vector stays zero
// instead of turning into NaNs, which is the friendlier failure in a game.
inline Vec3 normalize(Vec3 v) {
    float len = length(v);
    return len > 0.0f ? v / len : Vec3{};
}

// Linear interpolation: t = 0 gives a, t = 1 gives b.
constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// --- Vec4 helpers ----------------------------------------------------------------

constexpr Vec4 to_vec4(Vec3 v, float w) { return {v.x, v.y, v.z, w}; }
constexpr Vec3 xyz(Vec4 v) { return {v.x, v.y, v.z}; }
constexpr float dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
constexpr bool operator==(Vec4 a, Vec4 b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

} // namespace eng
