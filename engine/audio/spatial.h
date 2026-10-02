#pragma once

// 3D sound, worked out by hand: how loud a sound at some position should be,
// and how far left or right it should sit, for a listener (the camera).
//
// Real ears hear far sounds quieter and use the difference between the two
// ears to tell direction. Games approximate both with two numbers per sound:
//   * gain: volume falls off with distance, following the inverse distance
//     law (twice as far, half as loud) beyond min_distance,
//   * pan: -1 is hard left, +1 hard right, from how much the direction to
//     the sound lines up with the listener's right-hand side.
// This is the same model OpenAL's and miniaudio's built-in spatialisers use;
// writing it out shows there is no magic in it.

#include "engine/core/math/vec.h"

namespace eng {

struct Listener {
    Vec3 position;
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
};

struct Falloff {
    float min_distance = 4.0f;  // full volume inside this radius
    float max_distance = 40.0f; // silent beyond this
};

struct Spatial {
    float gain = 1.0f; // 0..1
    float pan = 0.0f;  // -1 (left) .. 1 (right)
};

inline Spatial spatialize(const Listener& listener, Vec3 source, const Falloff& falloff = {}) {
    Vec3 to_source = source - listener.position;
    float distance = length(to_source);
    if (distance >= falloff.max_distance) return {0.0f, 0.0f};

    Spatial s;
    s.gain = distance <= falloff.min_distance ? 1.0f : falloff.min_distance / distance;
    if (distance > 0.0f) {
        // The listener's right-hand side: forward x up, by the right-hand rule.
        Vec3 right = normalize(cross(listener.forward, listener.up));
        s.pan = dot(to_source / distance, right);
    }
    return s;
}

} // namespace eng
