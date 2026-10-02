#include "engine/platform/input.h"

#include "engine/core/log.h"

#include <cmath>

namespace eng {

Input::~Input() {
    if (gamepad_) SDL_CloseGamepad(gamepad_);
}

void Input::bind_key(std::string_view action, SDL_Scancode key) {
    buttons_[std::string(action)].keys.push_back(key);
}

void Input::bind_gamepad_button(std::string_view action, SDL_GamepadButton button) {
    buttons_[std::string(action)].buttons.push_back(button);
}

void Input::bind_axis_keys(std::string_view axis, SDL_Scancode negative, SDL_Scancode positive) {
    axes_[std::string(axis)].key_pairs.emplace_back(negative, positive);
}

void Input::bind_gamepad_axis(std::string_view axis, SDL_GamepadAxis gamepad_axis, bool invert) {
    axes_[std::string(axis)].sticks.emplace_back(gamepad_axis, invert);
}

bool Input::held(std::string_view action) const {
    auto it = buttons_.find(action);
    if (it == buttons_.end()) return false;
    for (SDL_Scancode k : it->second.keys)
        if (key_down_[k]) return true;
    for (SDL_GamepadButton b : it->second.buttons)
        if (pad_down_[b]) return true;
    return false;
}

bool Input::pressed(std::string_view action) const {
    auto it = buttons_.find(action);
    if (it == buttons_.end()) return false;
    for (SDL_Scancode k : it->second.keys)
        if (key_pressed_[k]) return true;
    for (SDL_GamepadButton b : it->second.buttons)
        if (pad_pressed_[b]) return true;
    return false;
}

float Input::axis(std::string_view axis) const {
    auto it = axes_.find(axis);
    if (it == axes_.end()) return 0.0f;

    float best = 0.0f;
    auto consider = [&best](float v) {
        if (std::fabs(v) > std::fabs(best)) best = v;
    };
    for (auto [neg, pos] : it->second.key_pairs)
        consider((key_down_[pos] ? 1.0f : 0.0f) - (key_down_[neg] ? 1.0f : 0.0f));
    for (auto [stick, invert] : it->second.sticks)
        consider(invert ? -pad_axis_[stick] : pad_axis_[stick]);
    return best;
}

void Input::handle_event(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        SDL_Scancode sc = event.key.scancode;
        if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_SCANCODE_COUNT) break;
        // Holding a key makes the OS send repeated KEY_DOWNs; those are not
        // new presses.
        if (event.key.down && !event.key.repeat && !key_down_[sc]) key_pressed_[sc] = true;
        key_down_[sc] = event.key.down;
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        auto b = static_cast<SDL_GamepadButton>(event.gbutton.button);
        if (b < 0 || b >= SDL_GAMEPAD_BUTTON_COUNT) break;
        if (event.gbutton.down && !pad_down_[b]) pad_pressed_[b] = true;
        pad_down_[b] = event.gbutton.down;
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        auto a = static_cast<SDL_GamepadAxis>(event.gaxis.axis);
        if (a < 0 || a >= SDL_GAMEPAD_AXIS_COUNT) break;
        // SDL gives -32768..32767; map to -1..1 and apply the deadzone,
        // rescaling so the output still reaches the full range smoothly.
        float v = static_cast<float>(event.gaxis.value) / 32767.0f;
        if (v < -1.0f) v = -1.0f;
        float mag = std::fabs(v);
        pad_axis_[a] = mag < kStickDeadzone ? 0.0f
                                            : std::copysign((mag - kStickDeadzone) / (1.0f - kStickDeadzone), v);
        break;
    }
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!gamepad_) {
            gamepad_ = SDL_OpenGamepad(event.gdevice.which);
            if (gamepad_) ENGINE_LOG_INFO("Gamepad connected: %s", SDL_GetGamepadName(gamepad_));
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad_ && SDL_GetGamepadID(gamepad_) == event.gdevice.which) {
            SDL_CloseGamepad(gamepad_);
            gamepad_ = nullptr;
            pad_down_.fill(false);
            pad_axis_.fill(0.0f);
            ENGINE_LOG_INFO("Gamepad disconnected");
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        release_all();
        break;
    default:
        break;
    }
}

void Input::consume_presses() {
    key_pressed_.fill(false);
    pad_pressed_.fill(false);
}

void Input::release_all() {
    key_down_.fill(false);
    pad_down_.fill(false);
    pad_axis_.fill(0.0f);
    consume_presses();
}

} // namespace eng
