#include "engine/app.h"

#include "engine/core/log.h"
#include "engine/core/profile.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

namespace eng {

App::SdlLifetime::SdlLifetime() {
    ok = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    if (!ok) ENGINE_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
}

App::SdlLifetime::~SdlLifetime() {
    SDL_Quit();
}

App::App(const AppConfig& config)
    : log_(1000, /*capture_sdl_log=*/true),
      window_(config.window),
      renderer_(window_),
      imgui_(window_, renderer_),
      assets_(&renderer_, audio_),
      fixed_step_(1.0 / config.simulation_hz) {}

int App::run(Game& game) {
    if (!sdl_.ok || !window_.valid() || !renderer_.valid()) {
        ENGINE_LOG_ERROR("Engine failed to start; see the errors above");
        return 1;
    }

    game.start(*this);
    running_ = true;

    // SDL's performance counter is the highest-resolution clock the OS offers.
    const double ticks_per_second = static_cast<double>(SDL_GetPerformanceFrequency());
    const std::uint64_t start = SDL_GetPerformanceCounter();
    std::uint64_t last = start;
    // Milliseconds since `from`, for timing each part of the frame.
    auto ms_since = [&](std::uint64_t from) {
        return static_cast<float>(static_cast<double>(SDL_GetPerformanceCounter() - from) * 1000.0 / ticks_per_second);
    };

    while (running_) {
        // Tracy shows each frame as the time between two FrameMarks.
        PROFILE_FRAME();
        FrameTimings timings;

        // --- 1. Events -----------------------------------------------------------
        std::uint64_t phase = SDL_GetPerformanceCounter();
        {
            PROFILE_SCOPE("Events");
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                imgui_.handle_event(event);
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running_ = false;
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F1 && !event.key.repeat)
                    debug_tools_.toggle();

                // While ImGui has keyboard focus (typing in a box), key presses are
                // its, not the game's. Releases always go through, so a key held
                // when focus moved can't get stuck down.
                bool is_key_down = event.type == SDL_EVENT_KEY_DOWN;
                if (!(is_key_down && imgui_.wants_keyboard())) input_.handle_event(event);
            }
        }
        timings.events = ms_since(phase);

        // --- 2. Hot reload -------------------------------------------------------
        phase = SDL_GetPerformanceCounter();
        assets_.update(static_cast<double>(phase - start) / ticks_per_second);
        timings.assets = ms_since(phase);

        // --- 3. Simulation at a fixed rate ---------------------------------------
        phase = SDL_GetPerformanceCounter();
        double frame_seconds = static_cast<double>(phase - last) / ticks_per_second;
        last = phase;

        int steps = fixed_step_.advance(frame_seconds);
        // Paused: throw the steps away, unless one was asked for.
        if (paused_) steps = step_requested_ ? 1 : 0;
        step_requested_ = false;
        const auto dt = static_cast<float>(fixed_step_.step_seconds());
        {
            PROFILE_SCOPE("Simulation");
            for (int i = 0; i < steps; ++i) {
                game.fixed_update(*this, dt);
                input_.consume_presses(); // each press is seen by exactly one step
            }
        }
        timings.simulation = ms_since(phase);

        stats_.frame_ms = static_cast<float>(frame_seconds * 1000.0);
        if (frame_seconds > 0.0) stats_.fps += (static_cast<float>(1.0 / frame_seconds) - stats_.fps) * 0.05f;
        stats_.steps_last_frame = steps;
        stats_.total_steps += static_cast<std::uint64_t>(steps);

        if (window_.minimized()) {
            SDL_Delay(10); // nothing to show; don't spin the CPU
            continue;
        }

        // --- 4. Debug UI ---------------------------------------------------------
        phase = SDL_GetPerformanceCounter();
        {
            PROFILE_SCOPE("Debug UI");
            imgui_.begin_frame();
            // The engine's tools first: their menu bar takes the top of the
            // screen, and the game's HUD lays itself out below it.
            if (debug_tools_.visible()) debug_tools_.draw(*this);
            game.debug_ui(*this);
            imgui_.end_frame();
        }
        timings.ui = ms_since(phase);

        // --- 5. Render -----------------------------------------------------------
        phase = SDL_GetPerformanceCounter();
        {
            PROFILE_SCOPE("Render");
            // While paused nothing moves, so draw the latest state as it is
            // rather than blending towards it.
            game.render(*this, renderer_, paused_ ? 1.0f : fixed_step_.alpha());
            renderer_.end_frame(&imgui_);
        }
        timings.render = ms_since(phase);

        // --- 6. Audio housekeeping ---------------------------------------------
        audio_.update(); // frees sounds that finished playing

        // This frame's total is only known when the next one starts, so the
        // history records the previous frame's total with this frame's parts;
        // close enough for a graph.
        timings.total = stats_.frame_ms;
        stats_.history[static_cast<std::size_t>(stats_.next)] = timings;
        stats_.next = (stats_.next + 1) % FrameStats::kHistory;
    }
    return 0;
}

} // namespace eng
