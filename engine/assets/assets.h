#pragma once

// The asset system: loads models and sounds, hands out handles to them,
// counts who is using each one, and reloads them when their files change.
//
//     Handle<Model> coin = assets.load_model("coin", "models/coin.glb");
//     renderer.draw(assets.mesh(coin), matrix);   // or assets.mesh("coin")
//     ...
//     assets.release(coin);                       // when you're done with it
//
// Three ideas, each a step up from phase 2's simple name -> mesh table:
//
//   * Handles, not pointers (handle.h). A handle stays safe to hold even
//     after the asset is freed or replaced.
//   * Reference counting (asset_table.h). Loading a file that's already
//     loaded returns the same asset with one more reference; release() drops
//     one, and the last release frees the GPU memory or decoded audio.
//   * Hot reload (file_watcher.h). In development builds every loaded file is
//     watched. Save a model in Blender and update() reloads it into the same
//     handle, so the game shows the new version without restarting. A file
//     that fails to load keeps its last good version, and the error shows in
//     the debug panel until the next save fixes it.
//
// update() also tells you about other files you watch_file(), which is how a
// game reloads its scripts on save.

#include "engine/assets/asset_table.h"
#include "engine/assets/file_watcher.h"
#include "engine/audio/audio.h"
#include "engine/render/mesh.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

class Renderer;

// What the asset system keeps per model and per sound.
struct Model {
    MeshHandle mesh;         // on the GPU
    std::size_t vertices = 0;
    std::size_t triangles = 0;
};
struct Sound {
    SoundHandle sound;       // in the audio system
};

class Assets {
public:
    // Without a renderer (tests), models are still read and tracked but
    // nothing goes to the GPU, so their mesh handles are invalid.
    Assets(Renderer* renderer, Audio& audio);
    ~Assets();
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    // --- Loading and releasing -----------------------------------------------
    // Each load adds a reference; the same file loaded twice is one asset.
    // A file that can't be read still gets a handle. It draws (or plays)
    // nothing, the error is logged, and saving a fixed file loads it.
    Handle<Model> load_model(const std::string& name, const std::filesystem::path& path);
    // A model built in code (like make_disc_mesh()). It has no file, so it
    // never reloads.
    Handle<Model> add_model(const std::string& name, const MeshData& data);
    Handle<Sound> load_sound(const std::string& name, const std::filesystem::path& path);

    void acquire(Handle<Model> model) { models_.acquire(model); }
    void acquire(Handle<Sound> sound) { sounds_.acquire(sound); }
    // Drops a reference. The last one frees the asset and stops watching its file.
    void release(Handle<Model> model);
    void release(Handle<Sound> sound);

    // --- Using ---------------------------------------------------------------
    // By the name given when loading; an invalid handle if there's none.
    Handle<Model> find_model(std::string_view name) const { return models_.find_by_name(name); }
    Handle<Sound> find_sound(std::string_view name) const { return sounds_.find_by_name(name); }

    // What to pass to the renderer or audio system. Invalid (draws or plays
    // nothing) for a stale handle or an unknown name.
    MeshHandle mesh(Handle<Model> model) const;
    MeshHandle mesh(std::string_view name) const { return mesh(models_.find_by_name(name)); }
    SoundHandle sound(Handle<Sound> sound) const;
    SoundHandle sound(std::string_view name) const { return sound(sounds_.find_by_name(name)); }

    // --- Hot reload ----------------------------------------------------------
    // On by default in development builds (the ENGINE_HOT_RELOAD CMake option).
    void set_hot_reload(bool on) { hot_reload_ = on; }
    bool hot_reload() const { return hot_reload_; }

    // Calls on_change() when this file changes. For files that aren't
    // models or sounds, such as scripts.
    void watch_file(const std::filesystem::path& path, std::function<void()> on_change);

    // Checks watched files (at most a few times a second) and reloads or
    // reports the ones that changed. `now` is a clock in seconds. App calls
    // this once a frame.
    void update(double now);

    // Reload from disk right now. False, with the error recorded, on failure.
    bool reload(Handle<Model> model);
    bool reload(Handle<Sound> sound);

    // For the debug panel.
    AssetTable<Model>& models() { return models_; }
    AssetTable<Sound>& sounds() { return sounds_; }
    std::size_t watched_files() const { return watcher_.size(); }

private:
    struct WatchedFile {
        std::filesystem::path path;
        std::function<void()> on_change;
    };

    // Reads a glTF file into `model`, uploading it to the GPU (into the
    // existing mesh, if it has one). Fills `error` on failure.
    bool read_model(const std::filesystem::path& path, Model& model, std::string& error);

    Renderer* renderer_;
    Audio& audio_;
    AssetTable<Model> models_;
    AssetTable<Sound> sounds_;
    FileWatcher watcher_;
    std::vector<WatchedFile> other_files_;
    bool hot_reload_;
};

} // namespace eng
