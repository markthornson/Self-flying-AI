#include "engine/debug/imgui_layer.h"

#include "engine/platform/window.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace eng {

ImGuiLayer::ImGuiLayer(Window& window, Renderer& renderer) {
    if (!renderer.valid()) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr; // don't write imgui.ini next to the executable

    ImGui::StyleColorsDark();
    // Scale the UI on high-DPI screens so text isn't tiny.
    float scale = window.content_scale();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;

    // Two backends: SDL3 feeds ImGui mouse and keyboard events; SDL_GPU draws it.
    ImGui_ImplSDL3_InitForSDLGPU(window.sdl());
    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = renderer.device();
    info.ColorTargetFormat = renderer.color_format();
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&info);
    initialized_ = true;
}

ImGuiLayer::~ImGuiLayer() {
    if (!initialized_) return;
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::handle_event(const SDL_Event& event) {
    if (initialized_) ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiLayer::begin_frame() {
    if (!initialized_) return;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::end_frame() {
    if (initialized_) ImGui::Render();
}

bool ImGuiLayer::wants_keyboard() const { return initialized_ && ImGui::GetIO().WantCaptureKeyboard; }
bool ImGuiLayer::wants_mouse() const { return initialized_ && ImGui::GetIO().WantCaptureMouse; }

void ImGuiLayer::prepare(SDL_GPUCommandBuffer* cmd) {
    if (!initialized_) return;
    // Uploads this frame's UI vertices and font texture changes. Must run
    // before the render pass that draws them.
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
}

void ImGuiLayer::draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) {
    if (initialized_) ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, pass);
}

} // namespace eng
