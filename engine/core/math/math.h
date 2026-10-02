#pragma once

// Include this to get the whole math library.

#include "engine/core/math/mat4.h"
#include "engine/core/math/quat.h"
#include "engine/core/math/transform.h"
#include "engine/core/math/vec.h"

namespace eng {

inline constexpr float kPi = 3.14159265358979323846f;

constexpr float radians(float degrees) { return degrees * (kPi / 180.0f); }
constexpr float degrees(float radians) { return radians * (180.0f / kPi); }

template <typename T>
constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace eng
