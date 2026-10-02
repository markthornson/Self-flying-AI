#include "approx.h"

#include "engine/assets/gltf.h"

#include <filesystem>
#include <string>

using namespace eng;

namespace {
const std::filesystem::path kModels = std::filesystem::path(ENGINE_SOURCE_DIR) / "games/coin_hunt/assets/models";
}

TEST_CASE("a glTF model loads with sensible normals, colours and indices") {
    std::string error;
    auto mesh = load_gltf_mesh(kModels / "crate.glb", &error);
    REQUIRE_MESSAGE(mesh, error);
    CHECK_FALSE(mesh->vertices.empty());
    REQUIRE(mesh->indices.size() % 3 == 0);

    for (std::uint32_t i : mesh->indices) REQUIRE(i < mesh->vertices.size());
    for (const Vertex& v : mesh->vertices) {
        CHECK(length(v.normal) == doctest::Approx(1.0f));
        // The crate is a 1 m cube centred on the origin.
        CHECK(std::abs(v.position.x) <= 0.5001f);
        CHECK(std::abs(v.position.y) <= 0.5001f);
        // Brown, from the file's COLOR_0 attribute: more red than blue.
        CHECK(v.color.x > v.color.z);
    }
}

TEST_CASE("glTF triangles wind counter-clockwise, matching their normals") {
    for (const char* name : {"coin.glb", "enemy.glb", "platform.glb", "player.glb"}) {
        CAPTURE(name);
        auto mesh = load_gltf_mesh(kModels / name);
        REQUIRE(mesh);
        for (size_t i = 0; i < mesh->indices.size(); i += 3) {
            const Vertex& a = mesh->vertices[mesh->indices[i]];
            const Vertex& b = mesh->vertices[mesh->indices[i + 1]];
            const Vertex& c = mesh->vertices[mesh->indices[i + 2]];
            CHECK(dot(cross(b.position - a.position, c.position - a.position), a.normal) > 0.0f);
        }
    }
}

TEST_CASE("a missing file is reported, not a crash") {
    std::string error;
    CHECK_FALSE(load_gltf_mesh(kModels / "no-such-model.glb", &error));
    CHECK(error.find("no-such-model") != std::string::npos);
}
