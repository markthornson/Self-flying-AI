#pragma once

// Collision tests between the three shapes phase 2 needs: spheres, axis-aligned
// boxes and rays. Pure functions with no engine state, so each one is easy to
// unit test and to read on its own.
//
// A *contact* answers "these two shapes overlap; how do I separate them?":
// move shape `a` by `normal * depth` and they just touch. Every collide()
// returns its contact with the normal pointing from b towards a.
//
// Boxes are axis aligned (AABBs): their edges always run along X, Y and Z, so
// a box is just a min and max corner. Rotated boxes need the separating axis
// test, which arrives with Jolt in phase 4 instead.

#include "engine/core/math/vec.h"

#include <optional>

namespace eng {

struct Sphere {
    Vec3 center;
    float radius = 0.5f;
};

struct Aabb {
    Vec3 min, max;

    static constexpr Aabb from_center(Vec3 center, Vec3 half_extents) {
        return {center - half_extents, center + half_extents};
    }
    constexpr Vec3 center() const { return (min + max) * 0.5f; }
    constexpr Vec3 half_extents() const { return (max - min) * 0.5f; }
};

// A half-line from origin along direction. direction must be unit length, so
// distances along the ray are in world units.
struct Ray {
    Vec3 origin;
    Vec3 direction{0.0f, 0.0f, -1.0f};

    constexpr Vec3 at(float distance) const { return origin + direction * distance; }
};

struct Contact {
    Vec3 normal;       // unit length, from b towards a
    float depth = 0;   // how far they overlap along normal
};

struct RayHit {
    float distance = 0; // along the ray
    Vec3 normal;        // surface normal at the hit, facing the ray
};

// The point inside (or on) the box nearest to p. If p is inside, p itself.
Vec3 closest_point(const Aabb& box, Vec3 p);

std::optional<Contact> collide(const Sphere& a, const Sphere& b);
std::optional<Contact> collide(const Sphere& a, const Aabb& b);
std::optional<Contact> collide(const Aabb& a, const Sphere& b);
std::optional<Contact> collide(const Aabb& a, const Aabb& b);

// The first point where the ray enters the shape, within max_distance. A ray
// starting inside a shape hits it at distance 0.
std::optional<RayHit> raycast(const Ray& ray, const Sphere& sphere, float max_distance);
std::optional<RayHit> raycast(const Ray& ray, const Aabb& box, float max_distance);

} // namespace eng
