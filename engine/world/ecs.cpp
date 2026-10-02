#include "engine/world/ecs.h"

namespace eng {

Entity World::create() {
    std::uint32_t index;
    if (!free_slots_.empty()) {
        // Reuse a dead slot. Its generation was bumped when it died, so the
        // new id differs from every id that slot had before.
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(0);
        slot_alive_.push_back(false);
    }
    slot_alive_[index] = true;
    ++alive_count_;
    return {index, generations_[index]};
}

void World::destroy(Entity e) {
    if (!alive(e)) return; // already gone, e.g. destroy_later() twice
    for (auto& p : pools_)
        if (p) p->remove(e);
    slot_alive_[e.index] = false;
    ++generations_[e.index];
    free_slots_.push_back(e.index);
    --alive_count_;
}

void World::flush() {
    // destroy() may not add to pending_destroy_, but swap first anyway so the
    // loop never walks a vector that is changing underneath it.
    std::vector<Entity> pending;
    pending.swap(pending_destroy_);
    for (Entity e : pending) destroy(e);
}

void World::clear() {
    for (auto& p : pools_)
        if (p) p->clear();
    for (std::uint32_t i = 0; i < generations_.size(); ++i) {
        if (slot_alive_[i]) {
            slot_alive_[i] = false;
            ++generations_[i];
            free_slots_.push_back(i);
        }
    }
    pending_destroy_.clear();
    alive_count_ = 0;
}

} // namespace eng
