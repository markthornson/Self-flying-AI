#pragma once

// The physics system: moves bodies, keeps them out of solid things, and
// reports when something walks into a trigger.
//
// This is deliberately a *character* physics step, not a rigid-body solver.
// There is no mass, friction, bouncing or rotation, which is exactly what a
// platformer wants and keeps the code short. Jolt replaces it in phase 4.
//
// Entities take part by having a Transform and a Collider:
//   * Collider only             -> static scenery: floors, walls, crates.
//   * Collider + Body           -> moves with its velocity and gravity, and is
//                                  pushed out of static scenery.
//   * Collider with is_trigger  -> solid for nothing; reports overlaps with
//                                  bodies instead (coins, hazards, goals).
//
// Each step, for every body:
//   1. velocity += gravity * dt;  position += velocity * dt   (Euler integration)
//   2. for each solid collider it overlaps, push it out along the contact
//      normal and cancel the velocity going into that surface,
//   3. remember whether any of those surfaces was floor-like (on_ground).
// Then every body is tested against every trigger, and pairs that started
// overlapping this step become TriggerEvents.
//
// Shapes follow the Transform's position and scale, but not its rotation:
// boxes stay axis aligned (see collision.h).

#include "engine/core/math/transform.h"
#include "engine/core/math/vec.h"
#include "engine/physics/collision.h"
#include "engine/world/ecs.h"

#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace eng {

struct Collider {
    enum class Shape { Sphere, Box };

    Shape shape = Shape::Box;
    Vec3 half_extents{0.5f, 0.5f, 0.5f}; // Box: multiplied by Transform::scale
    float radius = 0.5f;                 // Sphere: multiplied by the largest scale axis
    Vec3 offset{};                       // from the Transform's position
    bool is_trigger = false;

    static Collider box(Vec3 half_extents = {0.5f, 0.5f, 0.5f}) {
        return {.shape = Shape::Box, .half_extents = half_extents};
    }
    static Collider sphere(float radius = 0.5f) { return {.shape = Shape::Sphere, .radius = radius}; }
};

// Makes a collider move. Velocity is in metres per second.
struct Body {
    Vec3 velocity{};
    float gravity_scale = 1.0f; // 0 for things that fly
    bool on_ground = false;     // set by the physics step: touching a floor-like surface
};

// "other walked into trigger" this step. Only reported once per contact: the
// pair must separate before it is reported again.
struct TriggerEvent {
    Entity trigger;
    Entity other;
};

struct PhysicsHit {
    Entity entity;
    float distance = 0;
    Vec3 point;
    Vec3 normal;
};

// A collider's shape in world space, after applying the entity's Transform.
struct WorldShape {
    Collider::Shape shape;
    Sphere sphere;
    Aabb box;
};

class Physics {
public:
    Vec3 gravity{0.0f, -20.0f, 0.0f}; // a bit above Earth's 9.8 for snappy jumps

    // Surfaces whose normal points up at least this much (cos 45°) count as
    // floor for on_ground. Steeper ones are walls.
    static constexpr float kFloorNormalY = 0.7f;

    // Runs one fixed step. Trigger events from the previous step are cleared.
    void step(World& world, float dt);

    const std::vector<TriggerEvent>& trigger_events() const { return events_; }

    // The nearest solid (non-trigger) collider along the ray, ignoring
    // `ignore`, e.g. the entity casting it.
    std::optional<PhysicsHit> raycast(World& world, const Ray& ray, float max_distance,
                                      Entity ignore = kNullEntity) const;

    // Forgets which pairs are touching, e.g. when a level restarts.
    void reset() {
        events_.clear();
        touching_.clear();
    }

private:
    using Pair = std::pair<std::uint64_t, std::uint64_t>; // (trigger, other) as packed ids

    std::vector<TriggerEvent> events_;
    std::set<Pair> touching_; // trigger pairs overlapping at the end of the last step
};

// Builds the world-space shape for a collider on an entity with this transform.
WorldShape world_shape(const Collider& collider, const Transform& transform);

// Contact between two world shapes, normal pointing from b towards a.
std::optional<Contact> collide(const WorldShape& a, const WorldShape& b);

} // namespace eng
