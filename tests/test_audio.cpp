#include <doctest/doctest.h>

#include "engine/audio/audio.h"
#include "engine/audio/spatial.h"

using namespace eng;

TEST_CASE("sounds get quieter with distance") {
    Listener listener{.position = {0, 0, 0}};
    Falloff falloff{.min_distance = 2, .max_distance = 50};
    CHECK(spatialize(listener, {0, 0, -1}, falloff).gain == doctest::Approx(1.0f));  // inside min_distance
    CHECK(spatialize(listener, {0, 0, -4}, falloff).gain == doctest::Approx(0.5f));  // twice as far: half
    CHECK(spatialize(listener, {0, 0, -8}, falloff).gain == doctest::Approx(0.25f));
    CHECK(spatialize(listener, {0, 0, -60}, falloff).gain == 0.0f);                  // past max_distance
}

TEST_CASE("sounds pan towards the side they come from") {
    Listener listener{.position = {0, 0, 0}, .forward = {0, 0, -1}};
    CHECK(spatialize(listener, {5, 0, 0}).pan == doctest::Approx(1.0f));   // right
    CHECK(spatialize(listener, {-5, 0, 0}).pan == doctest::Approx(-1.0f)); // left
    CHECK(spatialize(listener, {0, 0, -5}).pan == doctest::Approx(0.0f));  // straight ahead

    // Turn the listener to face +X: what was on the right is now ahead.
    listener.forward = {1, 0, 0};
    CHECK(spatialize(listener, {5, 0, 0}).pan == doctest::Approx(0.0f));
    CHECK(spatialize(listener, {0, 0, 5}).pan == doctest::Approx(1.0f));
}

TEST_CASE("audio without a device is silent but safe to use") {
    Audio audio(/*open_device=*/false);
    CHECK_FALSE(audio.valid());
    SoundHandle sound = audio.load("does-not-exist.wav");
    CHECK_FALSE(sound.valid());
    audio.play(sound);
    audio.play_music(sound);
    audio.set_bus_volume(Bus::Music, 0.25f);
    CHECK(audio.bus_volume(Bus::Music) == doctest::Approx(0.25f));
    audio.update();
    CHECK(audio.voices_playing() == 0);
}
