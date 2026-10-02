#pragma once

// The bookkeeping behind every kind of asset: a table of slots, each holding
// one loaded asset with its name, file path and reference count.
//
// Reference counting is how the asset system knows when it may free
// something. Each load_*() of an asset adds a reference and each release()
// removes one; when the count reaches zero nobody is using it any more, and
// the asset's memory (GPU buffers, decoded audio) can go. Loading the same
// file twice doesn't load it twice: the second load finds it by path and
// just adds a reference.
//
// Slots are reused after an asset is freed, so each slot keeps a generation
// that goes up on every free; a Handle only matches while its generation does
// (see handle.h). The table itself doesn't know how to load or free anything:
// that's the Assets class's job. It only tracks who is using what.
//
// Header only and free of SDL, the GPU and audio, so it is easy to unit test.

#include "engine/assets/handle.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eng {

template <typename T>
class AssetTable {
public:
    struct Entry {
        T value{};
        std::string name;            // what scripts call it, e.g. "coin"
        std::filesystem::path path;  // empty for assets built in code
        std::uint32_t refs = 0;
        int reloads = 0;             // times hot reload replaced it, for the debug panel
        std::string error;           // why the last reload failed, if it did
    };

    // Adds a newly loaded asset with one reference. A name already in use
    // moves to the new asset.
    Handle<T> add(std::string name, std::filesystem::path path, T value) {
        std::uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        Slot& slot = slots_[index];
        slot.alive = true;
        slot.entry = Entry{std::move(value), std::move(name), std::move(path), 1, 0, {}};
        Handle<T> handle{index, slot.generation};
        if (!slot.entry.name.empty()) by_name_[slot.entry.name] = handle;
        if (!slot.entry.path.empty()) by_path_[key(slot.entry.path)] = handle;
        return handle;
    }

    // One more user of an asset that's already loaded.
    void acquire(Handle<T> handle) {
        if (Entry* e = get(handle)) ++e->refs;
    }

    // One user fewer. When that was the last one the asset leaves the table
    // and its value is returned, so the caller can free what it holds.
    std::optional<T> release(Handle<T> handle) {
        Entry* e = get(handle);
        if (!e || --e->refs > 0) return std::nullopt;

        Slot& slot = slots_[handle.index];
        T value = std::move(e->value);
        erase_lookup(by_name_, e->name, handle);
        if (!e->path.empty()) erase_lookup(by_path_, key(e->path), handle);
        slot.alive = false;
        slot.entry = Entry{};
        ++slot.generation; // every existing handle to this slot is now stale
        free_.push_back(handle.index);
        return value;
    }

    // The asset's entry, or nullptr if the handle is stale or was never valid.
    Entry* get(Handle<T> handle) {
        if (!handle.valid() || handle.index >= slots_.size()) return nullptr;
        Slot& slot = slots_[handle.index];
        return slot.alive && slot.generation == handle.generation ? &slot.entry : nullptr;
    }
    const Entry* get(Handle<T> handle) const { return const_cast<AssetTable*>(this)->get(handle); }

    // Lookups; an invalid handle when there is no such asset.
    Handle<T> find_by_name(std::string_view name) const { return find(by_name_, name); }
    Handle<T> find_by_path(const std::filesystem::path& path) const { return find(by_path_, key(path)); }

    // Calls fn(handle, entry) for every loaded asset.
    template <typename Fn>
    void each(Fn&& fn) {
        for (std::uint32_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].alive) fn(Handle<T>{i, slots_[i].generation}, slots_[i].entry);
    }

    std::size_t size() const { return slots_.size() - free_.size(); }

private:
    struct Slot {
        Entry entry;
        std::uint32_t generation = 0;
        bool alive = false;
    };
    using Lookup = std::map<std::string, Handle<T>, std::less<>>;

    // Paths are compared in a normal form, so "models/../models/coin.glb"
    // and "models/coin.glb" are the same file.
    static std::string key(const std::filesystem::path& path) {
        return path.lexically_normal().generic_string();
    }

    static Handle<T> find(const Lookup& lookup, std::string_view k) {
        auto it = lookup.find(k);
        return it != lookup.end() ? it->second : Handle<T>{};
    }

    // Only removes the lookup if it still points at this asset; a newer
    // asset may have taken the name since.
    static void erase_lookup(Lookup& lookup, const std::string& k, Handle<T> handle) {
        auto it = lookup.find(k);
        if (it != lookup.end() && it->second == handle) lookup.erase(it);
    }

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    Lookup by_name_;
    Lookup by_path_;
};

} // namespace eng
