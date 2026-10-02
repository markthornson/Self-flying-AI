#pragma once

// Loading 3D models from glTF files.
//
// glTF ("GL Transmission Format") is the open standard for 3D models, often
// called "the JPEG of 3D". Blender exports it, and it comes in two flavours:
// .gltf (JSON plus separate .bin and image files) and .glb (the same packed
// into one binary file). Both load here.
//
// A glTF file is a tree of *nodes*, each with a transform; a node can point at
// a *mesh*, which is a list of *primitives*, each a vertex/index buffer pair
// with a material. Its data lives in *buffers*, sliced up by *accessors*
// ("floats 3 at a time, starting at byte 128, 24 of them").
//
// fastgltf parses the file and resolves accessors. Everything after that is
// ours: we walk the node tree, bake each node's transform into its vertices,
// and flatten every primitive into one MeshData the renderer can upload.
// Turning a general interchange format into the simple layout the engine
// wants is what a real engine's asset pipeline does too, usually offline.

#include "engine/render/mesh.h"

#include <filesystem>
#include <optional>
#include <string>

namespace eng {

// Loads every triangle in the file's default scene as one mesh, in the file's
// own units and axes (glTF is metres, +Y up, the same as the engine).
//
// Vertex colours come from the COLOR_0 attribute if the file has one,
// otherwise from the material's base colour, otherwise white. Missing normals
// are computed per triangle. Textures are not loaded yet (phase 4).
//
// Returns nothing and fills `error` (if given) when the file can't be read.
std::optional<MeshData> load_gltf_mesh(const std::filesystem::path& path, std::string* error = nullptr);

} // namespace eng
