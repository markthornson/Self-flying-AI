#include "engine/assets/library.h"

#include "engine/assets/gltf.h"
#include "engine/core/log.h"
#include "engine/render/renderer.h"

namespace eng {

bool AssetLibrary::load_model(Renderer& renderer, const std::string& name, const std::filesystem::path& path) {
    std::string error;
    auto mesh = load_gltf_mesh(path, &error);
    if (!mesh) {
        ENGINE_LOG_ERROR("Model '%s': %s", name.c_str(), error.c_str());
        return false;
    }
    models_[name] = renderer.create_mesh(*mesh);
    return true;
}

bool AssetLibrary::load_sound(Audio& audio, const std::string& name, const std::filesystem::path& path) {
    SoundHandle sound = audio.load(path);
    sounds_[name] = sound;
    // With no audio device every load "fails"; that's expected, not an error.
    return sound.valid() || !audio.valid();
}

MeshHandle AssetLibrary::model(std::string_view name) const {
    auto it = models_.find(name);
    return it != models_.end() ? it->second : MeshHandle{};
}

SoundHandle AssetLibrary::sound(std::string_view name) const {
    auto it = sounds_.find(name);
    return it != sounds_.end() ? it->second : SoundHandle{};
}

} // namespace eng
