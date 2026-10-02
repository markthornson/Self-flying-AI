#pragma once

// A name -> asset table, so scripts and game code can say "coin" instead of
// holding handles. Phase 3 turns this into the plan's reference-counted,
// hot-reloading asset system; for now it loads everything up front.

#include "engine/audio/audio.h"
#include "engine/render/mesh.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace eng {

class Renderer;

class AssetLibrary {
public:
    // Loads a glTF model onto the GPU under `name`. Logs and returns false on failure.
    bool load_model(Renderer& renderer, const std::string& name, const std::filesystem::path& path);
    // Registers a mesh built in code (like make_cube_mesh()).
    void add_model(const std::string& name, MeshHandle mesh) { models_[name] = mesh; }
    bool load_sound(Audio& audio, const std::string& name, const std::filesystem::path& path);

    // An invalid handle if there is no such asset; drawing or playing it then does nothing.
    MeshHandle model(std::string_view name) const;
    SoundHandle sound(std::string_view name) const;

    const std::map<std::string, MeshHandle, std::less<>>& models() const { return models_; }
    const std::map<std::string, SoundHandle, std::less<>>& sounds() const { return sounds_; }

private:
    std::map<std::string, MeshHandle, std::less<>> models_;
    std::map<std::string, SoundHandle, std::less<>> sounds_;
};

} // namespace eng
