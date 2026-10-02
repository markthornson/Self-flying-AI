#pragma once

// The application shell: owns the window, renderer, input and debug UI, and
// runs the game loop. A game is a class deriving from eng::Game; main() is
//
//     MyGame game;
//     eng::App app({.window = {.title = "My game"}});
//     return app.run(game);
//
// Each frame, App::run():
//   1. pumps OS events into Input and ImGui,
//   2. runs Game::fixed_update() zero or more times at exactly 1/60 s each,
//   3. calls Game::debug_ui() to build the ImGui panels,
//   4. calls Game::render() with alpha, how far we are between the last two
//      simulation steps, then has the renderer draw and present the frame.

#include "engine/core/fixed_step.h"
#include "engine/debug/imgui_layer.h"
#include "engine/platform/input.h"
#include "engine/platform/window.h"
#include "engine/render/renderer.h"

#include <cstdint>

namespace eng {

class App;

class Game {
public:
    virtual ~Game() = default;
    virtual void start(App&) {}
    // Advance the simulation by exactly dt seconds. All gameplay goes here.
    virtual void fixed_update(App& app, float dt) = 0;
    // Describe the frame to draw. Must not change game state: it may run more
    // or fewer times than fixed_update.
    virtual void render(App& app, Renderer& renderer, float alpha) = 0;
    // Build debug panels with ImGui:: calls.
    virtual void debug_ui(App&) {}
};

struct AppConfig {
    WindowConfig window;
    double simulation_hz = 60.0;
};

struct FrameStats {
    float frame_ms = 0.0f;          // wall time of the last frame
    float fps = 0.0f;               // smoothed
    int steps_last_frame = 0;       // simulation steps run in the last frame
    std::uint64_t total_steps = 0;  // simulation steps since start
};

class App {
public:
    explicit App(const AppConfig& config);
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs until the window closes or quit() is called. Returns the exit code.
    int run(Game& game);
    void quit() { running_ = false; }

    Input& input() { return input_; }
    Renderer& renderer() { return renderer_; }
    Window& window() { return window_; }
    const FrameStats& stats() const { return stats_; }

private:
    // Declared first so it is destroyed last: SDL_Quit must run after every
    // other SDL object is gone.
    struct SdlLifetime {
        bool ok = false;
        SdlLifetime();
        ~SdlLifetime();
    };

    SdlLifetime sdl_;
    Window window_;
    Renderer renderer_;
    ImGuiLayer imgui_;
    Input input_;
    FixedStep fixed_step_;
    FrameStats stats_;
    bool running_ = false;
};

} // namespace eng
