#pragma once

// Mesh data as the renderer sees it: vertices and the triangles between them.

#include "engine/core/math/vec.h"

#include <cstdint>
#include <vector>

namespace eng {

// One vertex. The layout must match the VSInput struct in mesh.vert.hlsl and
// the vertex attributes declared in Renderer::create_pipeline().
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color;
};

// CPU-side mesh: plain arrays you can build or load, then hand to
// Renderer::create_mesh() to copy onto the GPU.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices; // three per triangle, counter-clockwise when seen from outside
};

// A handle to a mesh living on the GPU. Just an index into the renderer's
// table, so game code can copy it around freely and never owns GPU memory.
struct MeshHandle {
    std::uint32_t index = UINT32_MAX;
    bool valid() const { return index != UINT32_MAX; }
};

// A 1x1x1 cube centred on the origin, each face a different colour so you can
// see it turn. 24 vertices rather than 8 because each face needs its own normal.
MeshData make_cube_mesh();
// The same cube in a single colour, e.g. white to be tinted per draw.
MeshData make_cube_mesh(Vec3 color);
// A flat disc of radius 0.5 in the XZ plane, facing up (+Y). Handy for blob
// shadows and markers on the ground.
MeshData make_disc_mesh(Vec3 color, int segments = 24);

} // namespace eng
