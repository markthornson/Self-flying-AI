#pragma once

// The engine's debug tools: a menu bar and four windows, shown with F1 in
// every game.
//
//   Frame     frame-time graph, where each frame's time went, pause and step
//   Entities  every entity in the world; pick one to see and edit its
//             components (see inspector.h)
//   Assets    loaded models and sounds, their reference counts and reload
//             status, with hot reload on or off
//   Console   the engine log, and a line to type Lua into
//
// App owns one and draws it every frame it's visible. A game tells it which
// World to show and what runs console lines:
//
//     app.debug_tools().set_world(&world_);
//     app.debug_tools().set_console([&](const std::string& line) { ... });

#include "engine/debug/inspector.h"
#include "engine/world/ecs.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eng {

class App;
class Assets;

// What a console line printed back: its result, or an error.
struct ConsoleReply {
    bool ok = true;
    std::string text;
};

class DebugTools {
public:
    DebugTools(); // registers the engine's own components with the inspector
    // The inspector's functions point back at this object, so it stays put.
    DebugTools(const DebugTools&) = delete;
    DebugTools& operator=(const DebugTools&) = delete;

    bool visible() const { return visible_; }
    void set_visible(bool visible) { visible_ = visible; }
    void toggle() { visible_ = !visible_; }

    void set_world(World* world) { world_ = world; }
    void set_console(std::function<ConsoleReply(const std::string&)> run) { run_console_ = std::move(run); }

    // Add the game's own component types here so the entity panel shows them.
    Inspector& inspector() { return inspector_; }

    // Builds the tools' ImGui windows. Call between ImGui frames.
    void draw(App& app);

private:
    void draw_menu(App& app);
    void draw_frame_window(App& app);
    void draw_entities_window();
    void draw_assets_window(App& app);
    void draw_console_window(App& app);
    void run_console_line(App& app, const std::string& line);

    Inspector inspector_;
    World* world_ = nullptr;
    Assets* app_assets_ = nullptr; // for the inspector's model picker
    std::function<ConsoleReply(const std::string&)> run_console_;
    bool visible_ = false;

    bool show_frame_ = true;
    bool show_entities_ = true;
    bool show_assets_ = false;
    bool show_console_ = true;

    Entity selected_ = kNullEntity;
    std::string entity_filter_;
    std::string console_input_;
    std::vector<std::string> console_history_; // lines typed, for up/down arrow
    int history_pos_ = -1;                     // -1: not browsing history
    std::uint64_t log_seen_ = 0;               // log lines when the console last scrolled
    bool focus_console_ = false;
};

} // namespace eng
