// The asset system: handles, reference counting, file watching and hot
// reload. None of it needs a GPU: the Assets tests run without a renderer,
// so models are read from disk and tracked but never uploaded.

#include <doctest/doctest.h>

#include "engine/assets/asset_table.h"
#include "engine/assets/assets.h"
#include "engine/assets/file_watcher.h"
#include "engine/audio/audio.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace eng;
namespace fs = std::filesystem;

namespace {

const fs::path kModels = fs::path(ENGINE_SOURCE_DIR) / "games/coin_hunt/assets/models";

struct Thing {
    int value = 0;
};

// A fresh, empty folder for one test, deleted afterwards.
struct TempDir {
    fs::path path;
    explicit TempDir(const char* name) : path(fs::temp_directory_path() / name) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write_file(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

// Makes a file look modified, even on file systems whose clocks only tick
// once a second: push its modification time forward.
void touch(const fs::path& path, int seconds) {
    fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(seconds));
}

} // namespace

// --- AssetTable ----------------------------------------------------------------------

TEST_CASE("an asset table counts references and frees on the last release") {
    AssetTable<Thing> table;
    Handle<Thing> h = table.add("thing", "files/thing.txt", Thing{7});
    REQUIRE(table.get(h));
    CHECK(table.get(h)->value.value == 7);
    CHECK(table.get(h)->refs == 1);

    table.acquire(h);
    CHECK(table.get(h)->refs == 2);
    CHECK_FALSE(table.release(h)); // one user left
    REQUIRE(table.get(h));

    auto freed = table.release(h); // the last one
    REQUIRE(freed);
    CHECK(freed->value == 7);
    CHECK_FALSE(table.get(h));
    CHECK(table.size() == 0);
    CHECK_FALSE(table.find_by_name("thing").valid());
}

TEST_CASE("an asset table finds assets by name and by normalised path") {
    AssetTable<Thing> table;
    Handle<Thing> h = table.add("thing", "files/thing.txt", Thing{1});
    CHECK(table.find_by_name("thing") == h);
    CHECK(table.find_by_path("files/thing.txt") == h);
    CHECK(table.find_by_path("files/../files/./thing.txt") == h);
    CHECK_FALSE(table.find_by_name("other").valid());
}

TEST_CASE("a freed slot is reused, and old handles to it stay dead") {
    AssetTable<Thing> table;
    Handle<Thing> old = table.add("a", "", Thing{1});
    table.release(old);
    Handle<Thing> reused = table.add("b", "", Thing{2});
    CHECK(reused.index == old.index);
    CHECK(reused.generation != old.generation);
    CHECK_FALSE(table.get(old));
    // Releasing the stale handle again must not touch the new asset.
    CHECK_FALSE(table.release(old));
    REQUIRE(table.get(reused));
    CHECK(table.get(reused)->refs == 1);
}

// --- FileWatcher ----------------------------------------------------------------------

TEST_CASE("the file watcher reports a change once the file has settled") {
    TempDir dir("engine_test_watcher");
    fs::path file = dir.path / "model.glb";
    write_file(file, "version 1");

    FileWatcher watcher(/*poll_seconds=*/0.25, /*settle_seconds=*/0.3);
    watcher.watch(file);
    watcher.watch(file); // twice is the same as once
    CHECK(watcher.size() == 1);
    CHECK(watcher.poll(0.0).empty()); // nothing changed yet

    write_file(file, "version 2, longer");
    touch(file, 2);
    CHECK(watcher.poll(1.0).empty()); // the change is seen, but may still be in progress
    CHECK(watcher.poll(1.1).empty()); // too soon to poll again at all
    auto changed = watcher.poll(1.5); // unchanged for long enough: report it
    REQUIRE(changed.size() == 1);
    CHECK(changed[0] == file);
    CHECK(watcher.poll(2.0).empty()); // and only once
}

TEST_CASE("the file watcher waits for a file being rewritten to come back") {
    TempDir dir("engine_test_watcher_missing");
    fs::path file = dir.path / "script.lua";
    write_file(file, "x = 1");
    FileWatcher watcher(0.0, 0.0);
    watcher.watch(file);

    fs::remove(file); // an editor saving via a temporary file
    CHECK(watcher.poll(1.0).empty());
    CHECK(watcher.poll(2.0).empty()); // still missing: nothing to reload yet
    write_file(file, "x = 2");
    CHECK(watcher.poll(3.0).empty()); // back, but just changed
    CHECK(watcher.poll(4.0).size() == 1);
}

// --- Assets ------------------------------------------------------------------------------

TEST_CASE("loading the same model twice shares one asset") {
    Audio audio(/*open_device=*/false);
    Assets assets(/*renderer=*/nullptr, audio);
    Handle<Model> a = assets.load_model("coin", kModels / "coin.glb");
    Handle<Model> b = assets.load_model("coin", kModels / "." / "coin.glb"); // same file, spelled differently
    CHECK(b == a);
    REQUIRE(assets.models().get(a));
    CHECK(assets.models().get(a)->refs == 2);
    CHECK(assets.models().get(a)->value.triangles > 0);
    CHECK(assets.models().get(a)->error.empty());
    CHECK(assets.find_model("coin") == a);
    CHECK(assets.watched_files() == 1);

    assets.release(a);
    CHECK(assets.models().get(a)); // still one reference
    assets.release(b);
    CHECK_FALSE(assets.models().get(a));
    CHECK_FALSE(assets.find_model("coin").valid());
    CHECK(assets.watched_files() == 0); // nobody uses it, so stop watching
}

TEST_CASE("saving a model reloads it into the same handle") {
    TempDir dir("engine_test_hot_reload");
    fs::path file = dir.path / "thing.glb";
    fs::copy_file(kModels / "crate.glb", file);

    Audio audio(/*open_device=*/false);
    Assets assets(nullptr, audio);
    assets.set_hot_reload(true);
    Handle<Model> h = assets.load_model("thing", file);
    std::size_t crate_triangles = assets.models().get(h)->value.triangles;
    assets.update(0.0);

    // "Edit the model in Blender": replace the file with a different model.
    fs::copy_file(kModels / "coin.glb", file, fs::copy_options::overwrite_existing);
    touch(file, 2);
    assets.update(10.0); // sees the change
    assets.update(20.0); // settled: reloads

    const auto* e = assets.models().get(h);
    REQUIRE(e);
    CHECK(e->reloads == 1);
    CHECK(e->value.triangles != crate_triangles);
    CHECK(e->error.empty());

    SUBCASE("a broken save keeps the last good version and reports why") {
        std::size_t good = e->value.triangles;
        write_file(file, "this is not a glTF file");
        touch(file, 4);
        assets.update(30.0);
        assets.update(40.0);
        CHECK(e->value.triangles == good);
        CHECK(e->reloads == 1);
        CHECK_FALSE(e->error.empty());
    }

    SUBCASE("nothing reloads with hot reload off") {
        assets.set_hot_reload(false);
        touch(file, 4);
        assets.update(30.0);
        assets.update(40.0);
        CHECK(e->reloads == 1);
    }
}

TEST_CASE("a missing model still gets a handle, and loads once the file appears") {
    TempDir dir("engine_test_missing_model");
    fs::path file = dir.path / "later.glb";
    Audio audio(/*open_device=*/false);
    Assets assets(nullptr, audio);
    assets.set_hot_reload(true);

    Handle<Model> h = assets.load_model("later", file);
    REQUIRE(assets.models().get(h));
    CHECK_FALSE(assets.models().get(h)->error.empty());

    assets.update(0.0);
    fs::copy_file(kModels / "crate.glb", file);
    assets.update(10.0);
    assets.update(20.0);
    CHECK(assets.models().get(h)->error.empty());
    CHECK(assets.models().get(h)->value.triangles > 0);
}

TEST_CASE("watch_file calls back when any watched file changes") {
    TempDir dir("engine_test_watch_file");
    fs::path file = dir.path / "game.lua";
    write_file(file, "-- v1");
    Audio audio(/*open_device=*/false);
    Assets assets(nullptr, audio);
    assets.set_hot_reload(true);

    int calls = 0;
    assets.watch_file(file, [&] { ++calls; });
    assets.update(0.0);
    write_file(file, "-- version two");
    touch(file, 2);
    assets.update(10.0);
    assets.update(20.0);
    CHECK(calls == 1);
}
