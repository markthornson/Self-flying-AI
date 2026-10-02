#include "engine/app.h"

#include "engine/core/log.h"

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
    : window_(config.window),
      renderer_(window_),
      imgui_(window_, renderer_),
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
    std::uint64_t last = SDL_GetPerformanceCounter();

    while (running_) {
        // --- 1. Events -----------------------------------------------------------
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            imgui_.handle_event(event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running_ = false;

            // While ImGui has keyboard focus (typing in a box), key presses are
            // its, not the game's. Releases always go through, so a key held
            // when focus moved can't get stuck down.
            bool is_key_down = event.type == SDL_EVENT_KEY_DOWN;
            if (!(is_key_down && imgui_.wants_keyboard())) input_.handle_event(event);
        }

        // --- 2. Simulation at a fixed rate ---------------------------------------
        std::uint64_t now = SDL_GetPerformanceCounter();
        double frame_seconds = static_cast<double>(now - last) / ticks_per_second;
        last = now;

        int steps = fixed_step_.advance(frame_seconds);
        const auto dt = static_cast<float>(fixed_step_.step_seconds());
        for (int i = 0; i < steps; ++i) {
            game.fixed_update(*this, dt);
            input_.consume_presses(); // each press is seen by exactly one step
        }

        stats_.frame_ms = static_cast<float>(frame_seconds * 1000.0);
        if (frame_seconds > 0.0) stats_.fps += (static_cast<float>(1.0 / frame_seconds) - stats_.fps) * 0.05f;
        stats_.steps_last_frame = steps;
        stats_.total_steps += static_cast<std::uint64_t>(steps);

        if (window_.minimized()) {
            SDL_Delay(10); // nothing to show; don't spin the CPU
            continue;
        }

        // --- 3. Debug UI ---------------------------------------------------------
        imgui_.begin_frame();
        game.debug_ui(*this);
        imgui_.end_frame();

        // --- 4. Render -----------------------------------------------------------
        game.render(*this, renderer_, fixed_step_.alpha());
        renderer_.end_frame(&imgui_);
    }
    return 0;
}

} // namespace eng
