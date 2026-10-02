#include "engine/audio/audio.h"

#include "engine/core/log.h"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <vector>

namespace eng {

struct Audio::Impl {
    ma_engine engine{};
    bool ok = false;
    std::array<ma_sound_group, kBusCount> buses{};
    std::array<float, kBusCount> bus_volumes{1.0f, 1.0f, 1.0f};
    float master_volume = 1.0f;

    // One loaded copy of each sound, in slots that unload() frees for reuse.
    // Voices are made by copying these, which shares the decoded samples
    // instead of decoding again.
    struct Slot {
        std::unique_ptr<ma_sound> sound; // null if the last reload failed
        std::uint32_t generation = 0;
        bool alive = false;
    };
    std::vector<Slot> sounds;
    std::vector<std::uint32_t> free_slots;

    // Every voice currently playing, and which sound it is a copy of.
    // unique_ptr because miniaudio keeps pointers to them, so they must not
    // move when the vector grows.
    struct Voice {
        std::unique_ptr<ma_sound> sound;
        SoundHandle source;
    };
    std::vector<Voice> voices;
    Voice music;
    float music_volume = 1.0f;

    ma_sound_group* bus(Bus b) { return &buses[static_cast<int>(b)]; }

    Slot* slot_of(SoundHandle sound) {
        if (!ok || !sound.valid() || sound.index >= sounds.size()) return nullptr;
        Slot& slot = sounds[sound.index];
        return slot.alive && slot.generation == sound.generation ? &slot : nullptr;
    }
    ma_sound* find(SoundHandle sound) {
        Slot* slot = slot_of(sound);
        return slot ? slot->sound.get() : nullptr;
    }

    // Decodes a file into a new ma_sound, or returns nullptr.
    std::unique_ptr<ma_sound> decode(const std::filesystem::path& path) {
        auto sound = std::make_unique<ma_sound>();
        // DECODE: decode to raw samples now rather than while playing.
        ma_uint32 flags = MA_SOUND_FLAG_DECODE;
        if (ma_sound_init_from_file(&engine, path.string().c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
            ENGINE_LOG_ERROR("Can't load sound %s", path.string().c_str());
            return nullptr;
        }
        return sound;
    }

    // Starts a voice of `sound` on `bus`, or returns an empty Voice.
    Voice make_voice(SoundHandle sound, Bus b) {
        ma_sound* source = find(sound);
        if (!source) return {};
        auto voice = std::make_unique<ma_sound>();
        if (ma_sound_init_copy(&engine, source, 0, bus(b), voice.get()) != MA_SUCCESS) return {};
        // We do our own positioning (spatial.h), so turn miniaudio's off.
        ma_sound_set_spatialization_enabled(voice.get(), MA_FALSE);
        return {std::move(voice), sound};
    }

    // Stops and frees every voice copied from `sound`, which must happen
    // before the sound itself is freed.
    void stop_voices_of(SoundHandle sound) {
        std::erase_if(voices, [&](Voice& v) {
            if (v.source != sound) return false;
            ma_sound_uninit(v.sound.get());
            return true;
        });
    }
};

Audio::Audio(bool open_device) : impl_(std::make_unique<Impl>()) {
    if (!open_device) return;
    if (ma_engine_init(nullptr, &impl_->engine) != MA_SUCCESS) {
        ENGINE_LOG_WARN("No audio device; the game will be silent");
        return;
    }
    impl_->ok = true;
    for (auto& group : impl_->buses) ma_sound_group_init(&impl_->engine, 0, nullptr, &group);
}

Audio::~Audio() {
    if (!impl_->ok) return;
    // Voices first, then the sounds they copy, then the buses they play
    // through, then the engine: the reverse of creation.
    for (auto& v : impl_->voices) ma_sound_uninit(v.sound.get());
    stop_music();
    for (auto& slot : impl_->sounds)
        if (slot.sound) ma_sound_uninit(slot.sound.get());
    for (auto& group : impl_->buses) ma_sound_group_uninit(&group);
    ma_engine_uninit(&impl_->engine);
}

bool Audio::valid() const { return impl_->ok; }

SoundHandle Audio::load(const std::filesystem::path& path) {
    if (!impl_->ok) return {};
    auto sound = impl_->decode(path);
    if (!sound) return {};
    std::uint32_t index;
    if (!impl_->free_slots.empty()) {
        index = impl_->free_slots.back();
        impl_->free_slots.pop_back();
    } else {
        index = static_cast<std::uint32_t>(impl_->sounds.size());
        impl_->sounds.emplace_back();
    }
    impl_->sounds[index].sound = std::move(sound);
    impl_->sounds[index].alive = true;
    return {index, impl_->sounds[index].generation};
}

bool Audio::reload(SoundHandle handle, const std::filesystem::path& path) {
    Impl::Slot* slot = impl_->slot_of(handle);
    if (!slot) return false;
    // miniaudio's resource manager shares decoded files between sounds that
    // load the same path, so while the old version exists, loading the path
    // again would hand back the old samples. Free every user of the old one
    // first, then decode afresh.
    bool music_was_playing = impl_->music.sound && impl_->music.source == handle;
    impl_->stop_voices_of(handle);
    if (music_was_playing) stop_music();
    if (slot->sound) ma_sound_uninit(slot->sound.get());

    // A broken file leaves the slot empty but its handle valid: playing it is
    // silent until a later save fixes the file and the next reload fills it.
    slot->sound = impl_->decode(path);
    if (!slot->sound) return false;
    if (music_was_playing) play_music(handle, impl_->music_volume);
    return true;
}

void Audio::unload(SoundHandle handle) {
    Impl::Slot* slot = impl_->slot_of(handle);
    if (!slot) return;
    impl_->stop_voices_of(handle);
    if (impl_->music.sound && impl_->music.source == handle) stop_music();
    if (slot->sound) ma_sound_uninit(slot->sound.get());
    slot->sound.reset();
    slot->alive = false;
    ++slot->generation;
    impl_->free_slots.push_back(handle.index);
}

void Audio::play(SoundHandle sound, Bus bus, float volume, float pitch) {
    auto voice = impl_->make_voice(sound, bus);
    if (!voice.sound) return;
    ma_sound_set_volume(voice.sound.get(), volume);
    ma_sound_set_pitch(voice.sound.get(), pitch);
    ma_sound_start(voice.sound.get());
    impl_->voices.push_back(std::move(voice));
}

void Audio::play_at(SoundHandle sound, Vec3 position, float volume) {
    Spatial s = spatialize(listener_, position);
    if (s.gain <= 0.0f) return; // too far away to hear
    auto voice = impl_->make_voice(sound, Bus::Effects);
    if (!voice.sound) return;
    ma_sound_set_volume(voice.sound.get(), volume * s.gain);
    ma_sound_set_pan(voice.sound.get(), s.pan);
    ma_sound_start(voice.sound.get());
    impl_->voices.push_back(std::move(voice));
}

void Audio::play_music(SoundHandle sound, float volume) {
    stop_music();
    impl_->music = impl_->make_voice(sound, Bus::Music);
    impl_->music_volume = volume;
    if (!impl_->music.sound) return;
    ma_sound_set_looping(impl_->music.sound.get(), MA_TRUE);
    ma_sound_set_volume(impl_->music.sound.get(), volume);
    ma_sound_start(impl_->music.sound.get());
}

void Audio::stop_music() {
    if (!impl_->music.sound) return;
    ma_sound_uninit(impl_->music.sound.get());
    impl_->music = {};
}

void Audio::set_bus_volume(Bus bus, float volume) {
    impl_->bus_volumes[static_cast<int>(bus)] = volume;
    if (impl_->ok) ma_sound_group_set_volume(impl_->bus(bus), volume);
}

float Audio::bus_volume(Bus bus) const { return impl_->bus_volumes[static_cast<int>(bus)]; }

void Audio::set_master_volume(float volume) {
    impl_->master_volume = volume;
    if (impl_->ok) ma_engine_set_volume(&impl_->engine, volume);
}

float Audio::master_volume() const { return impl_->master_volume; }

void Audio::update() {
    auto& voices = impl_->voices;
    std::erase_if(voices, [](Impl::Voice& v) {
        if (!ma_sound_at_end(v.sound.get())) return false;
        ma_sound_uninit(v.sound.get());
        return true;
    });
}

int Audio::voices_playing() const { return static_cast<int>(impl_->voices.size()) + (impl_->music.sound ? 1 : 0); }

} // namespace eng
