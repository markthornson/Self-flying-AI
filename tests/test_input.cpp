#include "engine/platform/input.h"

#include <doctest/doctest.h>

using eng::Input;

namespace {

SDL_Event key_event(SDL_Scancode sc, bool down, bool repeat = false) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    e.key.down = down;
    e.key.repeat = repeat;
    return e;
}

SDL_Event stick_event(SDL_GamepadAxis axis, Sint16 value) {
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    e.gaxis.axis = static_cast<Uint8>(axis);
    e.gaxis.value = value;
    return e;
}

} // namespace

TEST_CASE("key pairs drive an axis") {
    Input in;
    in.bind_axis_keys("move_x", SDL_SCANCODE_A, SDL_SCANCODE_D);
    CHECK(in.axis("move_x") == 0.0f);
    in.handle_event(key_event(SDL_SCANCODE_D, true));
    CHECK(in.axis("move_x") == 1.0f);
    in.handle_event(key_event(SDL_SCANCODE_A, true));
    CHECK(in.axis("move_x") == 0.0f); // both held cancel out
    in.handle_event(key_event(SDL_SCANCODE_D, false));
    CHECK(in.axis("move_x") == -1.0f);
    CHECK(in.axis("unbound") == 0.0f);
}

TEST_CASE("pressed latches until consumed, held follows the key") {
    Input in;
    in.bind_key("jump", SDL_SCANCODE_SPACE);
    in.handle_event(key_event(SDL_SCANCODE_SPACE, true));
    in.handle_event(key_event(SDL_SCANCODE_SPACE, false));
    // Tapped and released before any simulation step: not held, but still pressed.
    CHECK_FALSE(in.held("jump"));
    CHECK(in.pressed("jump"));
    in.consume_presses();
    CHECK_FALSE(in.pressed("jump"));

    // OS key repeat is not a new press.
    in.handle_event(key_event(SDL_SCANCODE_SPACE, true));
    in.consume_presses();
    in.handle_event(key_event(SDL_SCANCODE_SPACE, true, /*repeat=*/true));
    CHECK(in.held("jump"));
    CHECK_FALSE(in.pressed("jump"));
}

TEST_CASE("stick axis applies the deadzone and inversion") {
    Input in;
    in.bind_gamepad_axis("move_forward", SDL_GAMEPAD_AXIS_LEFTY, /*invert=*/true);
    in.handle_event(stick_event(SDL_GAMEPAD_AXIS_LEFTY, 3000)); // inside the deadzone
    CHECK(in.axis("move_forward") == 0.0f);
    in.handle_event(stick_event(SDL_GAMEPAD_AXIS_LEFTY, -32768)); // fully up
    CHECK(in.axis("move_forward") == doctest::Approx(1.0f));
}

TEST_CASE("release_all clears everything") {
    Input in;
    in.bind_key("fire", SDL_SCANCODE_F);
    in.handle_event(key_event(SDL_SCANCODE_F, true));
    in.release_all();
    CHECK_FALSE(in.held("fire"));
    CHECK_FALSE(in.pressed("fire"));
}
