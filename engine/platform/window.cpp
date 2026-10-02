#include "engine/platform/window.h"

#include "engine/core/log.h"

#include <SDL3/SDL_error.h>

namespace eng {

Window::Window(const WindowConfig& config) {
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow(config.title, config.width, config.height, flags);
    if (!window_) ENGINE_LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
}

Window::~Window() {
    if (window_) SDL_DestroyWindow(window_);
}

void Window::pixel_size(int& width, int& height) const {
    SDL_GetWindowSizeInPixels(window_, &width, &height);
}

bool Window::minimized() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
}

float Window::content_scale() const {
    float scale = SDL_GetWindowDisplayScale(window_);
    return scale > 0.0f ? scale : 1.0f;
}

} // namespace eng
