#pragma once

// Asset handles: how game code refers to a loaded model or sound.
//
// A handle is an id, not a pointer. It names a slot in the asset system's
// table plus the slot's *generation*, the same trick the ECS uses for
// entities (see Entity in engine/world/ecs.h). Ids instead of pointers buy
// three things:
//
//   * The asset system can move, replace or reload what's behind a handle
//     (hot reload swaps in a new mesh) and every holder sees the new one.
//   * A handle to an asset that has been freed doesn't dangle. Its
//     generation no longer matches the slot's, so looking it up returns
//     nothing instead of whatever was loaded into the slot next.
//   * Handles are small plain values, so they can be stored in components,
//     copied, compared and passed to scripts without any ownership rules.
//
// The tag type T only keeps handles of different kinds apart: a
// Handle<Model> can't be passed where a Handle<Sound> is expected.

#include <cstdint>

namespace eng {

template <typename T>
struct Handle {
    std::uint32_t index = UINT32_MAX;
    std::uint32_t generation = 0;

    // True for any handle the asset system handed out. It does not mean the
    // asset is still loaded; ask the asset system for that.
    constexpr bool valid() const { return index != UINT32_MAX; }
    friend constexpr bool operator==(Handle, Handle) = default;
};

} // namespace eng
