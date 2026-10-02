#pragma once

// Audio: plays sounds through mixer buses.
//
// miniaudio does the plumbing (talking to WASAPI, Core Audio, ALSA or
// PulseAudio, decoding WAV files and mixing voices). On top of it this class
// adds the parts a game cares about:
//   * Buses. Every sound plays through one of three groups (music, effects,
//     UI), each with its own volume, so a settings menu can turn the music
//     down without touching anything else. This is what a mixing desk does.
//   * Fire and forget. play() starts a new *voice* (one playing copy of a
//     sound) and the class cleans it up when it ends, so the same coin sound
//     can overlap itself.
//   * 3D positioning with our own falloff and panning (see spatial.h).
//
// If no audio device can be opened (a CI machine, say) every call quietly
// does nothing, and the game still runs.

#include "engine/audio/spatial.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace eng {

enum class Bus { Music, Effects, Ui };
inline constexpr int kBusCount = 3;

// A loaded sound: a slot plus its generation, like MeshHandle, so a handle to
// an unloaded sound plays nothing instead of whatever took its slot.
struct SoundHandle {
    std::uint32_t index = UINT32_MAX;
    std::uint32_t generation = 0;
    bool valid() const { return index != UINT32_MAX; }
    friend bool operator==(SoundHandle, SoundHandle) = default;
};

class Audio {
public:
    // open_device = false makes a silent Audio that does nothing, for tests.
    explicit Audio(bool open_device = true);
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool valid() const;

    // Decodes the whole file into memory now, so playing it later never waits
    // on the disk. Fine for short effects; long music would stream instead.
    SoundHandle load(const std::filesystem::path& path);
    // Decodes the file again into the same handle, for hot reload. Voices of
    // the old version stop; music that was playing it restarts. On failure
    // the old version stays and false is returned.
    bool reload(SoundHandle sound, const std::filesystem::path& path);
    // Stops every voice of the sound and frees its samples.
    void unload(SoundHandle sound);

    void play(SoundHandle sound, Bus bus = Bus::Effects, float volume = 1.0f, float pitch = 1.0f);
    // Plays on the effects bus, louder or quieter and panned by where it is
    // relative to the listener. The position is fixed when the sound starts.
    void play_at(SoundHandle sound, Vec3 position, float volume = 1.0f);
    // Loops on the music bus, replacing whatever music was playing.
    void play_music(SoundHandle sound, float volume = 1.0f);
    void stop_music();

    void set_bus_volume(Bus bus, float volume);
    float bus_volume(Bus bus) const;
    void set_master_volume(float volume);
    float master_volume() const;

    void set_listener(const Listener& listener) { listener_ = listener; }
    const Listener& listener() const { return listener_; }

    // Frees voices that have finished. App calls this once a frame.
    void update();
    int voices_playing() const;

private:
    struct Impl; // keeps miniaudio's very large header out of everyone else's build
    std::unique_ptr<Impl> impl_;
    Listener listener_;
};

} // namespace eng
