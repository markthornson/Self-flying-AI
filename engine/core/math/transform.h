#pragma once

// Where an object is, which way it faces, and how big it is.
//
// Storing these three separately (rather than one matrix) keeps them easy to
// edit and to interpolate; the matrix is built only when drawing.

#include "engine/core/math/mat4.h"
#include "engine/core/math/quat.h"
#include "engine/core/math/vec.h"

namespace eng {

struct Transform {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    // Scale first, then rotate, then move into place.
    Mat4 to_matrix() const {
        return mat4_translation(position) * mat4_rotation(rotation) * mat4_scale(scale);
    }
};

// Blends two transforms. The game loop uses this to draw objects part way
// between their last two simulation states, so motion looks smooth even when
// the screen refreshes faster or slower than the simulation ticks.
inline Transform interpolate(const Transform& a, const Transform& b, float t) {
    return {lerp(a.position, b.position, t), slerp(a.rotation, b.rotation, t), lerp(a.scale, b.scale, t)};
}

} // namespace eng
