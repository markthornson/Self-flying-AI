#pragma once

// 4x4 matrices: how positions move from a model, into the world, into the
// camera's view, and finally onto the screen.
//
// Conventions (worth memorising, every bug in this area is one of these):
//   * Column vectors: a point is transformed as  p' = M * p.
//   * Column-major storage: cols[0] is the first column. This matches glTF,
//     GLSL, and what the HLSL shaders expect, so the bytes go to the GPU as is.
//   * Composition reads right to left: (A * B) * p applies B first, then A.
//     A model matrix is translation * rotation * scale.
//   * Clip space is SDL_GPU's: Y up, depth from 0 (near) to 1 (far).

#include "engine/core/math/quat.h"
#include "engine/core/math/vec.h"

#include <cmath>

namespace eng {

struct Mat4 {
    Vec4 cols[4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}; // identity

    // Element at (row, col), the way it is written on paper.
    constexpr float at(int row, int col) const {
        const Vec4& c = cols[col];
        return row == 0 ? c.x : row == 1 ? c.y : row == 2 ? c.z : c.w;
    }

    // Pointer to 16 floats in column-major order, ready for the GPU.
    const float* data() const { return &cols[0].x; }
};

static_assert(sizeof(Mat4) == 16 * sizeof(float), "Mat4 must be tightly packed for GPU upload");

constexpr Mat4 mat4_identity() { return {}; }

// Matrix times column vector: a weighted sum of the matrix's columns.
constexpr Vec4 operator*(const Mat4& m, Vec4 v) {
    const Vec4* c = m.cols;
    return {c[0].x * v.x + c[1].x * v.y + c[2].x * v.z + c[3].x * v.w,
            c[0].y * v.x + c[1].y * v.y + c[2].y * v.z + c[3].y * v.w,
            c[0].z * v.x + c[1].z * v.y + c[2].z * v.z + c[3].z * v.w,
            c[0].w * v.x + c[1].w * v.y + c[2].w * v.z + c[3].w * v.w};
}

// Matrix times matrix: each column of the result is a times that column of b.
constexpr Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) r.cols[i] = a * b.cols[i];
    return r;
}

constexpr Vec3 transform_point(const Mat4& m, Vec3 p) { return xyz(m * to_vec4(p, 1.0f)); }     // w = 1: translation applies
constexpr Vec3 transform_direction(const Mat4& m, Vec3 d) { return xyz(m * to_vec4(d, 0.0f)); } // w = 0: it doesn't

constexpr Mat4 transpose(const Mat4& m) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        r.cols[c] = {m.at(c, 0), m.at(c, 1), m.at(c, 2), m.at(c, 3)};
    return r;
}

// --- Building blocks ----------------------------------------------------------

constexpr Mat4 mat4_translation(Vec3 t) {
    Mat4 m;
    m.cols[3] = {t.x, t.y, t.z, 1.0f};
    return m;
}

constexpr Mat4 mat4_scale(Vec3 s) {
    Mat4 m;
    m.cols[0].x = s.x;
    m.cols[1].y = s.y;
    m.cols[2].z = s.z;
    return m;
}

// The rotation matrix for a unit quaternion. Each column is where that axis
// (X, Y, Z) ends up after the rotation.
constexpr Mat4 mat4_rotation(Quat q) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat4 m;
    m.cols[0] = {1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0};
    m.cols[1] = {2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0};
    m.cols[2] = {2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0};
    return m;
}

// --- Camera matrices ------------------------------------------------------------

// View matrix: moves the world so the camera sits at the origin looking down
// -Z with +Y up. It is the inverse of the camera's own placement.
inline Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = normalize(target - eye); // forward
    Vec3 s = normalize(cross(f, up)); // right ("side")
    Vec3 u = cross(s, f);             // true up, perpendicular to both
    Mat4 m;
    // The rotation part is the camera's axes written as rows (a rotation's
    // inverse is its transpose)...
    m.cols[0] = {s.x, u.x, -f.x, 0};
    m.cols[1] = {s.y, u.y, -f.y, 0};
    m.cols[2] = {s.z, u.z, -f.z, 0};
    // ...and the translation undoes the camera's position along those axes.
    m.cols[3] = {-dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    return m;
}

// Perspective projection: things further away get smaller. Squeezes the
// visible pyramid (the "frustum") into SDL_GPU's clip space, where a point at
// distance `near_z` lands on depth 0 and one at `far_z` lands on depth 1.
// The GPU then divides x, y, z by w, and w here is the distance from the camera.
inline Mat4 mat4_perspective(float fov_y_radians, float aspect, float near_z, float far_z) {
    float f = 1.0f / std::tan(fov_y_radians * 0.5f);
    Mat4 m;
    m.cols[0] = {f / aspect, 0, 0, 0};
    m.cols[1] = {0, f, 0, 0};
    m.cols[2] = {0, 0, far_z / (near_z - far_z), -1};
    m.cols[3] = {0, 0, -(far_z * near_z) / (far_z - near_z), 0};
    return m;
}

} // namespace eng
