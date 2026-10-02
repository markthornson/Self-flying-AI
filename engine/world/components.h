#pragma once

// The general-purpose components every game uses. Components are plain data:
// no behaviour, no pointers to other objects. Systems give them meaning.
//
// Physics components (Collider, Body) live in engine/physics/physics.h, next
// to the system that reads them.

#include "engine/assets/handle.h"
#include "engine/core/math/math.h"

#include <string>

namespace eng {

// Where the entity is. Transform itself (position, rotation, scale) is the
// struct from the math library; it is used directly as a component.

// The entity's Transform at the end of the previous simulation step. Drawing
// blends from this to the current Transform, which is what keeps motion
// smooth when frames and fixed steps don't line up (see App::run()).
struct PreviousTransform {
    Transform value;
};

struct Model; // engine/assets/assets.h

// Draw this model at the entity's Transform. It holds the asset handle, not
// the GPU mesh, so when hot reload replaces the model (or loads it for the
// first time after a broken save) the entity draws the new one.
struct MeshRenderer {
    Handle<Model> model;
    Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f}; // multiplies the mesh's vertex colours
};

// A label scripts can find entities by ("player", "coin"). Not unique.
struct Name {
    std::string value;
};

} // namespace eng
