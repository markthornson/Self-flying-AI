#pragma once

// Dear ImGui, wired into the engine as a renderer overlay.
//
// ImGui is "immediate mode": every frame you call ImGui::Begin/Slider/Text and
// it rebuilds the whole UI from scratch. No widget objects to keep in sync with
// game state, which is exactly what a debug UI wants.

#include "engine/render/renderer.h"

#include <SDL3/SDL_events.h>

namespace eng {

class Window;

class ImGuiLayer final : public Overlay {
public:
    ImGuiLayer(Window& window, Renderer& renderer);
    ~ImGuiLayer() override;
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void handle_event(const SDL_Event& event);
    void begin_frame(); // before any ImGui:: calls this frame
    void end_frame();   // after the last one; builds the draw data

    // True while ImGui is using the keyboard or mouse (typing in a text box,
    // dragging a slider), so the game should ignore that input.
    bool wants_keyboard() const;
    bool wants_mouse() const;

    void prepare(SDL_GPUCommandBuffer* cmd) override;
    void draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) override;

private:
    bool initialized_ = false;
};

} // namespace eng
