#pragma once

// The entity-component system (ECS): how the engine stores everything in a
// game world.
//
// Three ideas:
//   * An *entity* is only an id. It has no data and no behaviour of its own.
//   * A *component* is a plain struct of data (a Transform, a Collider, a
//     MeshRenderer). An entity "has" a component if one is stored for its id.
//   * A *system* is a free function that loops over every entity with a given
//     set of components and does one job, e.g. the physics step or drawing.
//
// Why bother, instead of a GameObject class with virtual update()? Because
// each component type lives in its own tightly packed array. A system that
// only needs positions and velocities walks two arrays front to back, which is
// exactly the access pattern CPU caches are built for, and adding a new kind
// of behaviour is a new component plus a new system rather than a change to a
// class hierarchy.
//
// Storage: one *sparse set* per component type. EnTT, the best known C++ ECS,
// uses the same structure, and its author's blog series "ECS back and forth"
// is the reference worth reading next. A sparse set is two arrays:
//
//     sparse:  entity index  ->  position in dense      (big, mostly empty)
//     dense:   position      ->  entity + component     (packed, no holes)
//
// Lookup is two array reads, iteration walks only the dense array, and
// removal moves the last element into the hole so dense never has gaps.
//
// Usage:
//
//     World world;
//     Entity e = world.create();
//     world.add<Transform>(e, Transform{.position = {0, 1, 0}});
//     world.add<Velocity>(e, Velocity{{1, 0, 0}});
//
//     world.each<Transform, Velocity>([&](Entity, Transform& t, Velocity& v) {
//         t.position += v.value * dt;
//     });

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace eng {

// An entity id. `index` picks a slot in the world; `generation` counts how
// many times that slot has been reused. When an entity is destroyed its slot's
// generation goes up, so any old copy of the id stops matching and
// World::alive() returns false for it, instead of the stale id silently
// pointing at whatever entity got the slot next.
struct Entity {
    std::uint32_t index = UINT32_MAX;
    std::uint32_t generation = 0;

    // True for any id handed out by World::create(). It does not mean the
    // entity is still alive; ask the world for that.
    constexpr bool valid() const { return index != UINT32_MAX; }
    friend constexpr bool operator==(Entity, Entity) = default;
};

inline constexpr Entity kNullEntity{};

namespace detail {

// The part of a component pool that doesn't depend on the component's type,
// so the World can remove all of a dead entity's components without knowing
// what types they are.
class PoolBase {
public:
    virtual ~PoolBase() = default;
    virtual bool contains(Entity e) const = 0;
    virtual void remove(Entity e) = 0;
    virtual void clear() = 0;
};

// The sparse set for one component type T.
template <typename T>
class Pool final : public PoolBase {
public:
    bool contains(Entity e) const override {
        return e.index < sparse_.size() && sparse_[e.index] != kEmpty && entities_[sparse_[e.index]] == e;
    }

    template <typename... Args>
    T& emplace(Entity e, Args&&... args) {
        if (contains(e)) return components_[sparse_[e.index]] = T{std::forward<Args>(args)...};
        if (e.index >= sparse_.size()) sparse_.resize(e.index + 1, kEmpty);
        sparse_[e.index] = static_cast<std::uint32_t>(entities_.size());
        entities_.push_back(e);
        components_.push_back(T{std::forward<Args>(args)...});
        return components_.back();
    }

    void remove(Entity e) override {
        if (!contains(e)) return;
        // Swap and pop: move the last element into the hole, then shrink by
        // one. Order isn't preserved, but dense stays packed and removal is O(1).
        std::uint32_t hole = sparse_[e.index];
        std::uint32_t last = static_cast<std::uint32_t>(entities_.size() - 1);
        if (hole != last) {
            entities_[hole] = entities_[last];
            components_[hole] = std::move(components_[last]);
            sparse_[entities_[hole].index] = hole;
        }
        entities_.pop_back();
        components_.pop_back();
        sparse_[e.index] = kEmpty;
    }

    void clear() override {
        sparse_.clear();
        entities_.clear();
        components_.clear();
    }

    T* get(Entity e) { return contains(e) ? &components_[sparse_[e.index]] : nullptr; }
    const T* get(Entity e) const { return contains(e) ? &components_[sparse_[e.index]] : nullptr; }

    std::size_t size() const { return entities_.size(); }
    Entity entity_at(std::size_t i) const { return entities_[i]; }
    T& component_at(std::size_t i) { return components_[i]; }

private:
    static constexpr std::uint32_t kEmpty = UINT32_MAX;

    std::vector<std::uint32_t> sparse_; // entity index -> position in the arrays below
    std::vector<Entity> entities_;      // dense: which entity owns each component
    std::vector<T> components_;         // dense: the components themselves
};

} // namespace detail

class World {
public:
    // --- Entities ------------------------------------------------------------
    Entity create();
    // Removes the entity and all its components at once. Don't call this from
    // inside each(); use destroy_later() there.
    void destroy(Entity e);
    // Queues the entity for destruction by the next flush(). Safe at any time,
    // including while iterating. The game loop flushes after each step.
    void destroy_later(Entity e) { pending_destroy_.push_back(e); }
    void flush();
    bool alive(Entity e) const {
        return e.valid() && e.index < generations_.size() && generations_[e.index] == e.generation &&
               slot_alive_[e.index];
    }
    std::size_t entity_count() const { return alive_count_; }
    // Destroys every entity. Ids handed out before stay dead.
    void clear();

    // --- Components ---------------------------------------------------------
    // Adds (or replaces) a component. The returned reference, like every
    // pointer from get(), stays valid only until the next add or remove of the
    // same component type, which may move the array.
    template <typename T, typename... Args>
    T& add(Entity e, Args&&... args) {
        return pool<T>(true)->emplace(e, std::forward<Args>(args)...);
    }

    template <typename T>
    void remove(Entity e) {
        if (auto* p = pool<T>(false)) p->remove(e);
    }

    template <typename T>
    bool has(Entity e) const {
        auto* p = pool<T>();
        return p && p->contains(e);
    }

    // The component, or nullptr if the entity doesn't have one.
    template <typename T>
    T* get(Entity e) {
        auto* p = pool<T>(false);
        return p ? p->get(e) : nullptr;
    }

    // Calls fn(entity, first&, rest&...) for every entity that has all of the
    // listed components. It walks First's dense array and skips entities
    // missing any of the others, so put the rarest component first.
    //
    // fn may change component values and call destroy_later(), but must not
    // add or remove components of the types being iterated.
    template <typename First, typename... Rest, typename Fn>
    void each(Fn&& fn) {
        auto* first = pool<First>(false);
        if (!first) return;
        for (std::size_t i = 0; i < first->size(); ++i) {
            Entity e = first->entity_at(i);
            if ((has<Rest>(e) && ...)) fn(e, first->component_at(i), *get<Rest>(e)...);
        }
    }

    // How many entities have component T.
    template <typename T>
    std::size_t count() const {
        auto* p = pool<T>();
        return p ? p->size() : 0;
    }

private:
    // Every component type gets a small number the first time it is used,
    // which indexes pools_. A function-local static per template instance
    // is the usual trick for this; no RTTI needed.
    static std::size_t next_type_id() {
        static std::size_t counter = 0;
        return counter++;
    }
    template <typename T>
    static std::size_t type_id() {
        static const std::size_t id = next_type_id();
        return id;
    }

    template <typename T>
    detail::Pool<T>* pool(bool create_if_missing) {
        std::size_t id = type_id<T>();
        if (id >= pools_.size()) {
            if (!create_if_missing) return nullptr;
            pools_.resize(id + 1);
        }
        if (!pools_[id]) {
            if (!create_if_missing) return nullptr;
            pools_[id] = std::make_unique<detail::Pool<T>>();
        }
        return static_cast<detail::Pool<T>*>(pools_[id].get());
    }
    template <typename T>
    const detail::Pool<T>* pool() const {
        std::size_t id = type_id<T>();
        return id < pools_.size() ? static_cast<const detail::Pool<T>*>(pools_[id].get()) : nullptr;
    }

    std::vector<std::unique_ptr<detail::PoolBase>> pools_; // indexed by type_id
    std::vector<std::uint32_t> generations_;               // per slot
    std::vector<bool> slot_alive_;                         // per slot
    std::vector<std::uint32_t> free_slots_;                // dead slots ready for reuse
    std::vector<Entity> pending_destroy_;
    std::size_t alive_count_ = 0;
};

} // namespace eng
