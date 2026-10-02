#include "engine/physics/physics.h"

#include "engine/core/math/math.h"
#include "engine/core/profile.h"

#include <algorithm>
#include <cmath>

namespace eng {

namespace {

std::uint64_t pack(Entity e) { return (static_cast<std::uint64_t>(e.generation) << 32) | e.index; }

} // namespace

WorldShape world_shape(const Collider& collider, const Transform& transform) {
    WorldShape s{};
    s.shape = collider.shape;
    Vec3 center = transform.position + collider.offset;
    Vec3 scale = transform.scale;
    if (collider.shape == Collider::Shape::Sphere) {
        float largest = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
        s.sphere = {center, collider.radius * largest};
    } else {
        Vec3 h = collider.half_extents;
        s.box = Aabb::from_center(center, {h.x * std::abs(scale.x), h.y * std::abs(scale.y), h.z * std::abs(scale.z)});
    }
    return s;
}

std::optional<Contact> collide(const WorldShape& a, const WorldShape& b) {
    using S = Collider::Shape;
    if (a.shape == S::Sphere) return b.shape == S::Sphere ? collide(a.sphere, b.sphere) : collide(a.sphere, b.box);
    return b.shape == S::Sphere ? collide(a.box, b.sphere) : collide(a.box, b.box);
}

void Physics::step(World& world, float dt) {
    PROFILE_SCOPE("Physics::step");
    events_.clear();

    // Gather the static solid colliders once; every body is tested against
    // them. Checking every body against every collider is O(bodies x
    // colliders), fine for the few hundred objects of a small level. A larger
    // world would first sort colliders into a grid or tree ("broad phase").
    struct Solid {
        Entity entity;
        WorldShape shape;
    };
    std::vector<Solid> solids;
    world.each<Collider, Transform>([&](Entity e, Collider& c, Transform& t) {
        if (!c.is_trigger && !world.has<Body>(e)) solids.push_back({e, world_shape(c, t)});
    });

    world.each<Body, Collider, Transform>([&](Entity, Body& body, Collider& collider, Transform& t) {
        // 1. Integrate: gravity changes velocity, velocity changes position.
        body.velocity += gravity * (body.gravity_scale * dt);
        t.position += body.velocity * dt;
        body.on_ground = false;
        if (collider.is_trigger) return; // a moving trigger passes through things

        // 2. Resolve. Pushing out of one box can push into another, so make a
        // few passes; three settles every corner case a small level has.
        for (int pass = 0; pass < 3; ++pass) {
            bool moved = false;
            for (const Solid& solid : solids) {
                auto contact = collide(world_shape(collider, t), solid.shape);
                if (!contact) continue;
                t.position += contact->normal * contact->depth;
                // Remove the part of the velocity heading into the surface, so
                // we slide along walls and stop falling on floors.
                float into = dot(body.velocity, contact->normal);
                if (into < 0.0f) body.velocity -= contact->normal * into;
                if (contact->normal.y >= kFloorNormalY) body.on_ground = true;
                moved = true;
            }
            if (!moved) break;
        }
    });

    // 3. Triggers: which bodies overlap which triggers now?
    std::set<Pair> touching_now;
    world.each<Collider, Transform>([&](Entity trigger, Collider& tc, Transform& tt) {
        if (!tc.is_trigger) return;
        WorldShape trigger_shape = world_shape(tc, tt);
        world.each<Body, Collider, Transform>([&](Entity other, Body&, Collider& oc, Transform& ot) {
            if (other == trigger || !collide(world_shape(oc, ot), trigger_shape)) return;
            Pair pair{pack(trigger), pack(other)};
            touching_now.insert(pair);
            if (!touching_.contains(pair)) events_.push_back({trigger, other}); // new this step
        });
    });
    touching_ = std::move(touching_now);
}

std::optional<PhysicsHit> Physics::raycast(World& world, const Ray& ray, float max_distance, Entity ignore) const {
    std::optional<PhysicsHit> best;
    world.each<Collider, Transform>([&](Entity e, Collider& c, Transform& t) {
        if (c.is_trigger || e == ignore) return;
        WorldShape s = world_shape(c, t);
        float limit = best ? best->distance : max_distance;
        auto hit = s.shape == Collider::Shape::Sphere ? eng::raycast(ray, s.sphere, limit) : eng::raycast(ray, s.box, limit);
        if (hit) best = PhysicsHit{e, hit->distance, ray.at(hit->distance), hit->normal};
    });
    return best;
}

} // namespace eng
