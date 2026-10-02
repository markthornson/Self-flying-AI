#include "approx.h"

#include "engine/render/mesh.h"

using namespace eng;

TEST_CASE("cube mesh triangles face outwards") {
    MeshData cube = make_cube_mesh();
    CHECK(cube.vertices.size() == 24);
    REQUIRE(cube.indices.size() == 36);
    for (size_t i = 0; i < cube.indices.size(); i += 3) {
        const Vertex& a = cube.vertices[cube.indices[i]];
        const Vertex& b = cube.vertices[cube.indices[i + 1]];
        const Vertex& c = cube.vertices[cube.indices[i + 2]];
        // Counter-clockwise winding: the edge cross product points along the
        // normal, and the normal points away from the centre.
        Vec3 winding = cross(b.position - a.position, c.position - a.position);
        CHECK(dot(winding, a.normal) > 0.0f);
        CHECK(dot(a.position, a.normal) > 0.0f);
    }
}

TEST_CASE("disc mesh faces up") {
    MeshData disc = make_disc_mesh({0.1f, 0.1f, 0.1f}, 16);
    CHECK(disc.vertices.size() == 17);
    REQUIRE(disc.indices.size() == 16 * 3);
    for (size_t i = 0; i < disc.indices.size(); i += 3) {
        Vec3 a = disc.vertices[disc.indices[i]].position;
        Vec3 b = disc.vertices[disc.indices[i + 1]].position;
        Vec3 c = disc.vertices[disc.indices[i + 2]].position;
        CHECK(cross(b - a, c - a).y > 0.0f);
    }
}
