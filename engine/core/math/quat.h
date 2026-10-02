#pragma once

// Quaternions: how the engine stores rotations.
//
// A rotation of `angle` radians around a unit axis (ax, ay, az) is stored as
//     x, y, z = axis * sin(angle / 2)
//     w       = cos(angle / 2)
// Four numbers instead of a 3x3 matrix's nine, no gimbal lock like Euler
// angles, and two rotations blend smoothly with slerp. The price is that the
// numbers are not human readable, which is why the debug UI shows angles.

#include "engine/core/math/vec.h"

#include <cmath>

namespace eng {

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; // default is "no rotation"
};

inline Quat quat_from_axis_angle(Vec3 axis, float radians) {
    Vec3 a = normalize(axis);
    float s = std::sin(radians * 0.5f);
    return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
}

// Combining rotations: (a * b) applied to a vector means "rotate by b first,
// then by a", the same order as matrices.
constexpr Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

constexpr float dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

inline Quat normalize(Quat q) {
    float len = std::sqrt(dot(q, q));
    if (len <= 0.0f) return {};
    return {q.x / len, q.y / len, q.z / len, q.w / len};
}

// Rotates a vector. The textbook form is q * v * conjugate(q); this is the
// same thing expanded and simplified into two cross products.
constexpr Vec3 rotate(Quat q, Vec3 v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = 2.0f * cross(u, v);
    return v + q.w * t + cross(u, t);
}

// Spherical linear interpolation: walks along the shortest arc between two
// rotations at constant speed. Used to smooth rendering between fixed steps.
inline Quat slerp(Quat a, Quat b, float t) {
    float cos_theta = dot(a, b);
    // q and -q are the same rotation. Flip one so we take the short way round.
    if (cos_theta < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        cos_theta = -cos_theta;
    }
    // Nearly identical rotations: plain lerp is accurate and avoids dividing
    // by sin(theta), which is close to zero.
    if (cos_theta > 0.9995f) {
        return normalize(Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                              a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    float theta = std::acos(cos_theta);
    float sin_theta = std::sin(theta);
    float wa = std::sin((1.0f - t) * theta) / sin_theta;
    float wb = std::sin(t * theta) / sin_theta;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// Euler angles, for showing and editing rotations in the debug UI.
//
// The angles are a Vec3 of radians about each axis: x is pitch (nodding),
// y is yaw (turning), z is roll (tilting). They are applied roll first, then
// pitch, then yaw, so q = yaw * pitch * roll; that order is what makes yaw
// mean "which way it faces" no matter how it's pitched.
inline Quat quat_from_euler(Vec3 radians) {
    return quat_from_axis_angle({0.0f, 1.0f, 0.0f}, radians.y) * quat_from_axis_angle({1.0f, 0.0f, 0.0f}, radians.x) *
           quat_from_axis_angle({0.0f, 0.0f, 1.0f}, radians.z);
}

// The inverse of quat_from_euler. Read the angles back out of the rotation
// matrix q makes: for R = Ry * Rx * Rz, entry (1,2) is -sin(pitch), entries
// (0,2) and (2,2) are sin and cos of yaw scaled by cos(pitch), and (1,0) and
// (1,1) likewise give roll. Straight up or down (pitch = ±90°) yaw and roll
// turn about the same axis and can't be told apart: that's *gimbal lock*,
// the reason the engine stores quaternions and only shows angles.
inline Vec3 euler_from_quat(Quat q) {
    float m12 = 2.0f * (q.y * q.z - q.w * q.x);
    float m02 = 2.0f * (q.x * q.z + q.w * q.y);
    float m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    float m10 = 2.0f * (q.x * q.y + q.w * q.z);
    float m11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    float pitch = std::asin(m12 < -1.0f ? 1.0f : m12 > 1.0f ? -1.0f : -m12);
    return {pitch, std::atan2(m02, m22), std::atan2(m10, m11)};
}

} // namespace eng
