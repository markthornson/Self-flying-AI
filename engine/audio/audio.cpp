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

    // One loaded copy of each sound. Voices are made by copying these, which
    // shares the decoded samples instead of decoding again.
    std::vector<std::unique_ptr<ma_sound>> sounds;
    // Every voice currently playing. unique_ptr because miniaudio keeps
    // pointers to them, so they must not move when the vector grows.
    std::vector<std::unique_ptr<ma_sound>> voices;
    std::unique_ptr<ma_sound> music;

    ma_sound_group* bus(Bus b) { return &buses[static_cast<int>(b)]; }

    // Starts a voice of `sound` on `bus`, or returns nullptr.
    std::unique_ptr<ma_sound> make_voice(SoundHandle sound, Bus b) {
        if (!ok || !sound.valid() || sound.index >= sounds.size()) return nullptr;
        auto voice = std::make_unique<ma_sound>();
        if (ma_sound_init_copy(&engine, sounds[sound.index].get(), 0, bus(b), voice.get()) != MA_SUCCESS) return nullptr;
        // We do our own positioning (spatial.h), so turn miniaudio's off.
        ma_sound_set_spatialization_enabled(voice.get(), MA_FALSE);
        return voice;
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
    for (auto& v : impl_->voices) ma_sound_uninit(v.get());
    if (impl_->music) ma_sound_uninit(impl_->music.get());
    for (auto& s : impl_->sounds) ma_sound_uninit(s.get());
    for (auto& group : impl_->buses) ma_sound_group_uninit(&group);
    ma_engine_uninit(&impl_->engine);
}

bool Audio::valid() const { return impl_->ok; }

SoundHandle Audio::load(const std::filesystem::path& path) {
    if (!impl_->ok) return {};
    auto sound = std::make_unique<ma_sound>();
    // DECODE: decode to raw samples now rather than while playing.
    ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    if (ma_sound_init_from_file(&impl_->engine, path.string().c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
        ENGINE_LOG_ERROR("Can't load sound %s", path.string().c_str());
        return {};
    }
    impl_->sounds.push_back(std::move(sound));
    return {static_cast<std::uint32_t>(impl_->sounds.size() - 1)};
}

void Audio::play(SoundHandle sound, Bus bus, float volume, float pitch) {
    auto voice = impl_->make_voice(sound, bus);
    if (!voice) return;
    ma_sound_set_volume(voice.get(), volume);
    ma_sound_set_pitch(voice.get(), pitch);
    ma_sound_start(voice.get());
    impl_->voices.push_back(std::move(voice));
}

void Audio::play_at(SoundHandle sound, Vec3 position, float volume) {
    Spatial s = spatialize(listener_, position);
    if (s.gain <= 0.0f) return; // too far away to hear
    auto voice = impl_->make_voice(sound, Bus::Effects);
    if (!voice) return;
    ma_sound_set_volume(voice.get(), volume * s.gain);
    ma_sound_set_pan(voice.get(), s.pan);
    ma_sound_start(voice.get());
    impl_->voices.push_back(std::move(voice));
}

void Audio::play_music(SoundHandle sound, float volume) {
    stop_music();
    impl_->music = impl_->make_voice(sound, Bus::Music);
    if (!impl_->music) return;
    ma_sound_set_looping(impl_->music.get(), MA_TRUE);
    ma_sound_set_volume(impl_->music.get(), volume);
    ma_sound_start(impl_->music.get());
}

void Audio::stop_music() {
    if (!impl_->music) return;
    ma_sound_uninit(impl_->music.get());
    impl_->music.reset();
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
    auto finished = std::remove_if(voices.begin(), voices.end(), [](const std::unique_ptr<ma_sound>& v) {
        if (!ma_sound_at_end(v.get())) return false;
        ma_sound_uninit(v.get());
        return true;
    });
    voices.erase(finished, voices.end());
}

int Audio::voices_playing() const { return static_cast<int>(impl_->voices.size()) + (impl_->music ? 1 : 0); }

} // namespace eng
