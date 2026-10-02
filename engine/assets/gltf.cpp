#include "engine/assets/gltf.h"

#include "engine/core/math/math.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <cstring>

namespace eng {

namespace {

// glTF and our Mat4 both store matrices as 16 floats, column by column, so
// converting is a straight copy.
Mat4 to_mat4(const fastgltf::math::fmat4x4& m) {
    Mat4 out;
    std::memcpy(&out.cols[0].x, m.data(), sizeof(float) * 16);
    return out;
}

Vec3 to_vec3(const fastgltf::math::fvec3& v) { return {v.x(), v.y(), v.z()}; }

// Multiplies colours channel by channel, the way a tint works.
Vec3 tinted(Vec3 color, Vec3 tint) { return {color.x * tint.x, color.y * tint.y, color.z * tint.z}; }

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

// Appends one primitive's triangles to `out`, transformed by `node_matrix`.
bool append_primitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive, const Mat4& node_matrix,
                      MeshData& out, std::string* error) {
    if (primitive.type != fastgltf::PrimitiveType::Triangles) return true; // skip lines and points

    auto position_attr = primitive.findAttribute("POSITION");
    if (position_attr == primitive.attributes.end()) return fail(error, "a primitive has no POSITION attribute");

    const auto first_vertex = static_cast<std::uint32_t>(out.vertices.size());
    const fastgltf::Accessor& positions = asset.accessors[position_attr->accessorIndex];
    out.vertices.resize(first_vertex + positions.count);

    // Default colour: the material's base colour, or white.
    Vec3 base_color{1.0f, 1.0f, 1.0f};
    if (primitive.materialIndex) {
        const auto& factor = asset.materials[*primitive.materialIndex].pbrData.baseColorFactor;
        base_color = {factor.x(), factor.y(), factor.z()};
    }

    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, positions, [&](fastgltf::math::fvec3 p, size_t i) {
        Vertex& v = out.vertices[first_vertex + i];
        v.position = transform_point(node_matrix, to_vec3(p));
        v.color = base_color;
    });

    bool has_normals = false;
    if (auto attr = primitive.findAttribute("NORMAL"); attr != primitive.attributes.end()) {
        has_normals = true;
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset, asset.accessors[attr->accessorIndex], [&](fastgltf::math::fvec3 n, size_t i) {
                // Directions ignore translation (w = 0). Renormalise because
                // the node might be scaled.
                out.vertices[first_vertex + i].normal = normalize(transform_direction(node_matrix, to_vec3(n)));
            });
    }

    if (auto attr = primitive.findAttribute("COLOR_0"); attr != primitive.attributes.end()) {
        // Colours may be RGB or RGBA; we keep RGB. fastgltf converts 8- and
        // 16-bit normalised integers to 0..1 floats for us.
        const fastgltf::Accessor& colors = asset.accessors[attr->accessorIndex];
        if (colors.type == fastgltf::AccessorType::Vec4) {
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, colors, [&](fastgltf::math::fvec4 c, size_t i) {
                out.vertices[first_vertex + i].color = tinted({c.x(), c.y(), c.z()}, base_color);
            });
        } else {
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, colors, [&](fastgltf::math::fvec3 c, size_t i) {
                out.vertices[first_vertex + i].color = tinted(to_vec3(c), base_color);
            });
        }
    }

    // Indices: three per triangle. A primitive without an index buffer just
    // uses its vertices in order.
    const auto first_index = out.indices.size();
    if (primitive.indicesAccessor) {
        const fastgltf::Accessor& indices = asset.accessors[*primitive.indicesAccessor];
        out.indices.reserve(first_index + indices.count);
        fastgltf::iterateAccessor<std::uint32_t>(asset, indices,
                                                 [&](std::uint32_t index) { out.indices.push_back(first_vertex + index); });
    } else {
        for (std::uint32_t i = 0; i < positions.count; ++i) out.indices.push_back(first_vertex + i);
    }

    // A mirrored node (negative scale) turns triangles inside out. Swap two
    // corners of each triangle so they wind counter-clockwise again.
    const Mat4& m = node_matrix;
    float det = dot(xyz(m.cols[0]), cross(xyz(m.cols[1]), xyz(m.cols[2])));
    if (det < 0.0f)
        for (size_t i = first_index; i + 2 < out.indices.size(); i += 3) std::swap(out.indices[i + 1], out.indices[i + 2]);

    if (!has_normals) {
        // Flat shading: each triangle's normal is the cross product of two of
        // its edges. Shared vertices end up with the last triangle's normal,
        // which is fine for the hard-edged shapes that skip normals.
        for (size_t i = first_index; i + 2 < out.indices.size(); i += 3) {
            Vertex& a = out.vertices[out.indices[i]];
            Vertex& b = out.vertices[out.indices[i + 1]];
            Vertex& c = out.vertices[out.indices[i + 2]];
            Vec3 n = normalize(cross(b.position - a.position, c.position - a.position));
            a.normal = b.normal = c.normal = n;
        }
    }
    return true;
}

} // namespace

std::optional<MeshData> load_gltf_mesh(const std::filesystem::path& path, std::string* error) {
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        fail(error, "can't read " + path.string());
        return std::nullopt;
    }

    fastgltf::Parser parser;
    auto asset = parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::LoadExternalBuffers);
    if (asset.error() != fastgltf::Error::None) {
        fail(error, path.string() + ": " + std::string(fastgltf::getErrorMessage(asset.error())));
        return std::nullopt;
    }

    const fastgltf::Asset& a = asset.get();
    if (a.scenes.empty()) {
        fail(error, path.string() + " has no scene");
        return std::nullopt;
    }

    // Walk the node tree. fastgltf hands each node its world matrix (its own
    // transform times all its parents'), which we bake into the vertices.
    MeshData mesh;
    bool ok = true;
    std::size_t scene = a.defaultScene.value_or(0);
    fastgltf::iterateSceneNodes(a, scene, fastgltf::math::fmat4x4(),
                                [&](const fastgltf::Node& node, const fastgltf::math::fmat4x4& matrix) {
                                    if (!ok || !node.meshIndex) return;
                                    Mat4 m = to_mat4(matrix);
                                    for (const auto& primitive : a.meshes[*node.meshIndex].primitives)
                                        ok = ok && append_primitive(a, primitive, m, mesh, error);
                                });
    if (!ok) return std::nullopt;
    if (mesh.indices.empty()) {
        fail(error, path.string() + " has no triangles");
        return std::nullopt;
    }
    return mesh;
}

} // namespace eng
