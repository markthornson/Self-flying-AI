#include "engine/assets/assets.h"

#include "engine/assets/gltf.h"
#include "engine/core/log.h"
#include "engine/core/profile.h"
#include "engine/render/renderer.h"

namespace eng {

namespace {

#ifdef ENGINE_HOT_RELOAD
constexpr bool kHotReloadDefault = true;
#else
constexpr bool kHotReloadDefault = false;
#endif

// One spelling per file, so the watcher, the table and the caller agree.
std::filesystem::path normal(const std::filesystem::path& path) { return path.lexically_normal(); }

} // namespace

Assets::Assets(Renderer* renderer, Audio& audio)
    : renderer_(renderer), audio_(audio), hot_reload_(kHotReloadDefault) {}

Assets::~Assets() {
    // Free whatever is still loaded. The renderer and audio system would free
    // it all anyway when they go, but an asset system that cleans up after
    // itself is easier to trust.
    models_.each([&](Handle<Model>, AssetTable<Model>::Entry& e) {
        if (renderer_) renderer_->destroy_mesh(e.value.mesh);
    });
    sounds_.each([&](Handle<Sound>, AssetTable<Sound>::Entry& e) { audio_.unload(e.value.sound); });
}

// --- Models -------------------------------------------------------------------------

bool Assets::read_model(const std::filesystem::path& path, Model& model, std::string& error) {
    auto data = load_gltf_mesh(path, &error);
    if (!data) return false;
    model.vertices = data->vertices.size();
    model.triangles = data->indices.size() / 3;
    if (!renderer_) return true;
    // Reuse the GPU slot if there is one, so every MeshRenderer that holds
    // this mesh's handle draws the new version.
    if (renderer_->update_mesh(model.mesh, *data)) return true;
    model.mesh = renderer_->create_mesh(*data);
    return true;
}

Handle<Model> Assets::load_model(const std::string& name, const std::filesystem::path& path) {
    const std::filesystem::path file = normal(path);
    if (Handle<Model> existing = models_.find_by_path(file); models_.get(existing)) {
        models_.acquire(existing);
        return existing;
    }

    Model model;
    std::string error;
    if (!read_model(file, model, error)) ENGINE_LOG_ERROR("Model '%s': %s", name.c_str(), error.c_str());
    Handle<Model> handle = models_.add(name, file, model);
    models_.get(handle)->error = error;
    watcher_.watch(file);
    return handle;
}

Handle<Model> Assets::add_model(const std::string& name, const MeshData& data) {
    Model model;
    model.vertices = data.vertices.size();
    model.triangles = data.indices.size() / 3;
    if (renderer_) model.mesh = renderer_->create_mesh(data);
    return models_.add(name, {}, model);
}

void Assets::release(Handle<Model> handle) {
    const AssetTable<Model>::Entry* e = models_.get(handle);
    if (!e) return;
    std::filesystem::path file = e->path;
    if (auto freed = models_.release(handle)) {
        if (renderer_) renderer_->destroy_mesh(freed->mesh);
        if (!file.empty()) watcher_.unwatch(file);
    }
}

MeshHandle Assets::mesh(Handle<Model> handle) const {
    const auto* e = models_.get(handle);
    return e ? e->value.mesh : MeshHandle{};
}

bool Assets::reload(Handle<Model> handle) {
    AssetTable<Model>::Entry* e = models_.get(handle);
    if (!e || e->path.empty()) return false;
    std::string error;
    if (!read_model(e->path, e->value, error)) {
        // Keep the last good version on screen; show what went wrong.
        e->error = error;
        ENGINE_LOG_ERROR("Reloading model '%s' failed, keeping the old one: %s", e->name.c_str(), error.c_str());
        return false;
    }
    e->error.clear();
    ++e->reloads;
    ENGINE_LOG_INFO("Reloaded model '%s' (%zu triangles)", e->name.c_str(), e->value.triangles);
    return true;
}

// --- Sounds -------------------------------------------------------------------------

Handle<Sound> Assets::load_sound(const std::string& name, const std::filesystem::path& path) {
    const std::filesystem::path file = normal(path);
    if (Handle<Sound> existing = sounds_.find_by_path(file); sounds_.get(existing)) {
        sounds_.acquire(existing);
        return existing;
    }
    // With no audio device every load "fails"; that's expected, not an error.
    SoundHandle sound = audio_.load(file);
    Handle<Sound> handle = sounds_.add(name, file, Sound{sound});
    if (!sound.valid() && audio_.valid()) sounds_.get(handle)->error = "can't load " + file.string();
    watcher_.watch(file);
    return handle;
}

void Assets::release(Handle<Sound> handle) {
    const AssetTable<Sound>::Entry* e = sounds_.get(handle);
    if (!e) return;
    std::filesystem::path file = e->path;
    if (auto freed = sounds_.release(handle)) {
        audio_.unload(freed->sound);
        watcher_.unwatch(file);
    }
}

SoundHandle Assets::sound(Handle<Sound> handle) const {
    const auto* e = sounds_.get(handle);
    return e ? e->value.sound : SoundHandle{};
}

bool Assets::reload(Handle<Sound> handle) {
    AssetTable<Sound>::Entry* e = sounds_.get(handle);
    if (!e || !audio_.valid()) return false;
    // A sound whose first load failed has no slot in the audio system yet.
    bool ok = e->value.sound.valid() ? audio_.reload(e->value.sound, e->path)
                                     : (e->value.sound = audio_.load(e->path)).valid();
    if (!ok) {
        e->error = "can't load " + e->path.string();
        return false;
    }
    e->error.clear();
    ++e->reloads;
    ENGINE_LOG_INFO("Reloaded sound '%s'", e->name.c_str());
    return true;
}

// --- Watching -----------------------------------------------------------------------

void Assets::watch_file(const std::filesystem::path& path, std::function<void()> on_change) {
    const std::filesystem::path file = normal(path);
    other_files_.push_back({file, std::move(on_change)});
    watcher_.watch(file);
}

void Assets::update(double now) {
    if (!hot_reload_) return;
    PROFILE_SCOPE("Assets::update");
    for (const std::filesystem::path& file : watcher_.poll(now)) {
        if (Handle<Model> m = models_.find_by_path(file); models_.get(m)) reload(m);
        if (Handle<Sound> s = sounds_.find_by_path(file); sounds_.get(s)) reload(s);
        // Copy the callbacks first: one may watch more files, which would
        // change other_files_ under the loop.
        std::vector<std::function<void()>> callbacks;
        for (const WatchedFile& w : other_files_)
            if (w.path == file) callbacks.push_back(w.on_change);
        if (!callbacks.empty()) ENGINE_LOG_INFO("%s changed", file.string().c_str());
        for (auto& callback : callbacks) callback();
    }
}

} // namespace eng
