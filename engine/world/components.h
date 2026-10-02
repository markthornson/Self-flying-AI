#pragma once

// The general-purpose components every game uses. Components are plain data:
// no behaviour, no pointers to other objects. Systems give them meaning.
//
// Physics components (Collider, Body) live in engine/physics/physics.h, next
// to the system that reads them.

#include "engine/core/math/math.h"
#include "engine/render/mesh.h"

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

// Draw this mesh at the entity's Transform.
struct MeshRenderer {
    MeshHandle mesh;
    Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f}; // multiplies the mesh's vertex colours
};

// A label scripts can find entities by ("player", "coin"). Not unique.
struct Name {
    std::string value;
};

} // namespace eng
