#pragma once

// Input: turns raw keyboard and gamepad events into named actions.
//
// Game code never asks "is the W key down?". It asks "how much is the player
// pushing move_forward?" and the answer can come from W/S, the arrow keys or a
// gamepad stick. Rebinding controls then never touches gameplay code.
//
// Two kinds of binding:
//   * Buttons ("jump"): held() while down, pressed() once per press.
//   * Axes ("move_x"): a value from -1 to 1, from a key pair or a stick.
//
// The plan has bindings in a config file; phase 1 sets them in code and the
// file loader can be added on top of these same bind_* calls.

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

class Input {
public:
    Input() = default;
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // --- Setting up bindings -------------------------------------------------
    void bind_key(std::string_view action, SDL_Scancode key);
    void bind_gamepad_button(std::string_view action, SDL_GamepadButton button);
    void bind_axis_keys(std::string_view axis, SDL_Scancode negative, SDL_Scancode positive);
    // Sticks report +Y as "down"; pass invert = true to make "up" positive.
    void bind_gamepad_axis(std::string_view axis, SDL_GamepadAxis gamepad_axis, bool invert = false);

    // --- Queries for game code -------------------------------------------------
    bool held(std::string_view action) const;
    // True if the action was pressed since the last simulation step. Presses
    // are latched until consume_presses() so a quick tap that falls between
    // two fixed steps is never lost.
    bool pressed(std::string_view action) const;
    // -1..1. Keys give -1, 0 or 1; a stick gives anything in between. When
    // both are active the larger magnitude wins.
    float axis(std::string_view axis) const;

    // --- Called by the engine ----------------------------------------------------
    void handle_event(const SDL_Event& event);
    void consume_presses();   // after each simulation step
    void release_all();       // when the window loses focus, so no key sticks down

    bool gamepad_connected() const { return gamepad_ != nullptr; }

    // Stick values inside this radius count as zero; worn sticks never rest at
    // exactly 0.
    static constexpr float kStickDeadzone = 0.2f;

private:
    struct ButtonBinding {
        std::vector<SDL_Scancode> keys;
        std::vector<SDL_GamepadButton> buttons;
    };
    struct AxisBinding {
        std::vector<std::pair<SDL_Scancode, SDL_Scancode>> key_pairs; // (negative, positive)
        std::vector<std::pair<SDL_GamepadAxis, bool>> sticks;         // (axis, invert)
    };

    // std::less<> lets us look up with a string_view without building a string.
    std::map<std::string, ButtonBinding, std::less<>> buttons_;
    std::map<std::string, AxisBinding, std::less<>> axes_;

    std::array<bool, SDL_SCANCODE_COUNT> key_down_{};
    std::array<bool, SDL_SCANCODE_COUNT> key_pressed_{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> pad_down_{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> pad_pressed_{};
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> pad_axis_{};

    SDL_Gamepad* gamepad_ = nullptr; // the first connected gamepad; one player for now
};

} // namespace eng
