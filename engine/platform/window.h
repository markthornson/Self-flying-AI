#pragma once

// The OS window. A thin owner of an SDL_Window: creates it, destroys it, and
// answers questions about its size. Only the platform layer and the renderer
// ever see the SDL_Window pointer itself.

#include <SDL3/SDL_video.h>

namespace eng {

struct WindowConfig {
    const char* title = "Engine";
    int width = 1280;  // in screen coordinates; on a high-DPI display the
    int height = 720;  // framebuffer has more pixels than this
};

class Window {
public:
    explicit Window(const WindowConfig& config);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool valid() const { return window_ != nullptr; }
    SDL_Window* sdl() const { return window_; }

    // Size of the drawable area in actual pixels.
    void pixel_size(int& width, int& height) const;
    bool minimized() const;
    float content_scale() const; // 2.0 on a typical "Retina" display

private:
    SDL_Window* window_ = nullptr;
};

} // namespace eng
