#pragma once

// The application shell: owns the window, renderer, input, audio and debug UI, and
// runs the game loop. A game is a class deriving from eng::Game; main() is
//
//     MyGame game;
//     eng::App app({.window = {.title = "My game"}});
//     return app.run(game);
//
// Each frame, App::run():
//   1. pumps OS events into Input and ImGui,
//   2. reloads any asset whose file changed on disk,
//   3. runs Game::fixed_update() zero or more times at exactly 1/60 s each,
//   4. builds the debug UI: the engine's DebugTools (F1), then
//      Game::debug_ui() for the game's own panels and HUD,
//   5. calls Game::render() with alpha, how far we are between the last two
//      simulation steps, then has the renderer draw and present the frame.
//
// F1 is the engine's: it shows and hides the debug tools in every game.

#include "engine/assets/assets.h"
#include "engine/audio/audio.h"
#include "engine/core/fixed_step.h"
#include "engine/debug/debug_tools.h"
#include "engine/debug/imgui_layer.h"
#include "engine/debug/log_history.h"
#include "engine/platform/input.h"
#include "engine/platform/window.h"
#include "engine/render/renderer.h"

#include <array>
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

// Where one frame's time went, in milliseconds.
struct FrameTimings {
    float total = 0.0f;      // start of this frame to start of the next
    float events = 0.0f;     // OS events and input
    float assets = 0.0f;     // checking files, hot reloading
    float simulation = 0.0f; // every fixed_update() this frame
    float ui = 0.0f;         // building the debug UI
    float render = 0.0f;     // drawing, submitting and presenting; includes
                             // waiting for the display (vsync)
};

struct FrameStats {
    float frame_ms = 0.0f;          // wall time of the last frame
    float fps = 0.0f;               // smoothed
    int steps_last_frame = 0;       // simulation steps run in the last frame
    std::uint64_t total_steps = 0;  // simulation steps since start

    // The last kHistory frames' timings, as a ring: the newest is at
    // history[(next - 1) % kHistory]. For the frame-time graph.
    static constexpr int kHistory = 240;
    std::array<FrameTimings, kHistory> history{};
    int next = 0;
};

class App {
public:
    explicit App(const AppConfig& config);
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs until the window closes or quit() is called. Returns the exit code.
    int run(Game& game);
    void quit() { running_ = false; }

    // Pausing stops fixed_update() but keeps drawing and the debug UI live,
    // so you can look around a frozen moment. step_once() runs exactly one
    // simulation step while paused.
    void set_paused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }
    void step_once() { step_requested_ = true; }

    Input& input() { return input_; }
    Audio& audio() { return audio_; }
    Assets& assets() { return assets_; }
    Renderer& renderer() { return renderer_; }
    Window& window() { return window_; }
    DebugTools& debug_tools() { return debug_tools_; }
    LogHistory& log() { return log_; }
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
    LogHistory log_; // early, so it captures what the others log as they start
    Window window_;
    Renderer renderer_;
    ImGuiLayer imgui_;
    Input input_;
    Audio audio_;
    Assets assets_; // after the renderer and audio, which it loads into
    DebugTools debug_tools_;
    FixedStep fixed_step_;
    FrameStats stats_;
    bool running_ = false;
    bool paused_ = false;
    bool step_requested_ = false;
};

} // namespace eng
