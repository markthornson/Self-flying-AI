#include "engine/render/mesh.h"

namespace eng {

namespace {

MeshData build_cube(const Vec3* face_colors) {
    struct Face {
        Vec3 normal, u, v, color; // u and v span the face; cross(u, v) == normal
    };
    // u x v points along the normal, so the corners below wind counter-clockwise
    // when you look at the face from outside the cube.
    const Face faces[6] = {
        {{+1, 0, 0}, {0, 0, -1}, {0, 1, 0}, face_colors[0]},
        {{-1, 0, 0}, {0, 0, +1}, {0, 1, 0}, face_colors[1]},
        {{0, +1, 0}, {1, 0, 0}, {0, 0, -1}, face_colors[2]},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, +1}, face_colors[3]},
        {{0, 0, +1}, {1, 0, 0}, {0, 1, 0}, face_colors[4]},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, face_colors[5]},
    };

    MeshData mesh;
    for (const Face& f : faces) {
        auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        Vec3 c = f.normal * 0.5f; // centre of the face
        Vec3 u = f.u * 0.5f, v = f.v * 0.5f;
        mesh.vertices.push_back({c - u - v, f.normal, f.color});
        mesh.vertices.push_back({c + u - v, f.normal, f.color});
        mesh.vertices.push_back({c + u + v, f.normal, f.color});
        mesh.vertices.push_back({c - u + v, f.normal, f.color});
        // Two triangles per face: 0-1-2 and 0-2-3.
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
    }
    return mesh;
}

} // namespace

MeshData make_cube_mesh() {
    const Vec3 colors[6] = {
        {0.90f, 0.30f, 0.25f}, // +X red
        {0.55f, 0.15f, 0.12f}, // -X dark red
        {0.35f, 0.80f, 0.35f}, // +Y green
        {0.15f, 0.40f, 0.15f}, // -Y dark green
        {0.30f, 0.50f, 0.95f}, // +Z blue
        {0.15f, 0.25f, 0.55f}, // -Z dark blue
    };
    return build_cube(colors);
}

MeshData make_cube_mesh(Vec3 color) {
    const Vec3 colors[6] = {color, color, color, color, color, color};
    return build_cube(colors);
}

} // namespace eng
