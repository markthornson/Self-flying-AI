#include "engine/physics/collision.h"

#include "engine/core/math/math.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eng {

namespace {

float axis(Vec3 v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

Vec3 unit_axis(int i, float sign) {
    Vec3 v{};
    (i == 0 ? v.x : i == 1 ? v.y : v.z) = sign;
    return v;
}

Contact flipped(Contact c) { return {-c.normal, c.depth}; }

} // namespace

Vec3 closest_point(const Aabb& box, Vec3 p) {
    // Clamp each coordinate into the box's range, independently. That is all
    // "closest point on an axis-aligned box" takes.
    return {clamp(p.x, box.min.x, box.max.x), clamp(p.y, box.min.y, box.max.y), clamp(p.z, box.min.z, box.max.z)};
}

std::optional<Contact> collide(const Sphere& a, const Sphere& b) {
    // Two spheres overlap when their centres are closer than the sum of their
    // radii. Compare squared lengths first to skip the square root in the
    // common case where they're far apart.
    Vec3 d = a.center - b.center;
    float r = a.radius + b.radius;
    float dist_sq = dot(d, d);
    if (dist_sq >= r * r) return std::nullopt;
    float dist = std::sqrt(dist_sq);
    // Exactly the same centre: any direction separates them; pick up.
    Vec3 normal = dist > 0.0f ? d / dist : Vec3{0.0f, 1.0f, 0.0f};
    return Contact{normal, r - dist};
}

std::optional<Contact> collide(const Sphere& a, const Aabb& b) {
    Vec3 p = closest_point(b, a.center);
    Vec3 d = a.center - p;
    float dist_sq = dot(d, d);
    if (dist_sq >= a.radius * a.radius) return std::nullopt;

    if (dist_sq > 0.0f) {
        // The usual case: the centre is outside the box, and the closest point
        // on the box is where they touch.
        float dist = std::sqrt(dist_sq);
        return Contact{d / dist, a.radius - dist};
    }

    // The centre is inside the box (the sphere moved fast, or started there).
    // Push out through whichever face is nearest.
    Vec3 to_min = a.center - b.min, to_max = b.max - a.center;
    int best_axis = 0;
    float best_dist = INFINITY, best_sign = 1.0f;
    for (int i = 0; i < 3; ++i) {
        if (axis(to_min, i) < best_dist) {
            best_dist = axis(to_min, i);
            best_axis = i;
            best_sign = -1.0f;
        }
        if (axis(to_max, i) < best_dist) {
            best_dist = axis(to_max, i);
            best_axis = i;
            best_sign = 1.0f;
        }
    }
    return Contact{unit_axis(best_axis, best_sign), best_dist + a.radius};
}

std::optional<Contact> collide(const Aabb& a, const Sphere& b) {
    auto c = collide(b, a);
    return c ? std::optional(flipped(*c)) : std::nullopt;
}

std::optional<Contact> collide(const Aabb& a, const Aabb& b) {
    // Boxes overlap only if their ranges overlap on all three axes. The axis
    // with the smallest overlap is the cheapest way to pull them apart.
    Contact best{{}, INFINITY};
    for (int i = 0; i < 3; ++i) {
        float overlap = std::min(axis(a.max, i), axis(b.max, i)) - std::max(axis(a.min, i), axis(b.min, i));
        if (overlap <= 0.0f) return std::nullopt; // a gap on this axis: no collision
        if (overlap < best.depth) {
            float sign = axis(a.center(), i) >= axis(b.center(), i) ? 1.0f : -1.0f;
            best = {unit_axis(i, sign), overlap};
        }
    }
    return best;
}

std::optional<RayHit> raycast(const Ray& ray, const Sphere& sphere, float max_distance) {
    // Points on the ray are origin + t * dir. Setting |point - centre| = r
    // gives a quadratic in t (with a = 1 because dir is unit length):
    //     t^2 + 2 b t + c = 0,   b = dot(m, dir),  c = dot(m, m) - r^2
    Vec3 m = ray.origin - sphere.center;
    float b = dot(m, ray.direction);
    float c = dot(m, m) - sphere.radius * sphere.radius;
    if (c <= 0.0f) return RayHit{0.0f, -ray.direction}; // starts inside
    if (b > 0.0f) return std::nullopt;                  // outside and pointing away
    float discriminant = b * b - c;
    if (discriminant < 0.0f) return std::nullopt;       // misses
    float t = -b - std::sqrt(discriminant);             // the nearer root
    if (t > max_distance) return std::nullopt;
    return RayHit{t, normalize(ray.at(t) - sphere.center)};
}

std::optional<RayHit> raycast(const Ray& ray, const Aabb& box, float max_distance) {
    // The "slab" method. On each axis the box is the slab between two planes;
    // the ray is inside that slab for some range of t. It is inside the box
    // where all three ranges overlap: from the latest entry to the earliest exit.
    float t_enter = 0.0f, t_exit = max_distance;
    Vec3 enter_normal = -ray.direction; // used if the ray starts inside
    for (int i = 0; i < 3; ++i) {
        float o = axis(ray.origin, i), d = axis(ray.direction, i);
        float lo = axis(box.min, i), hi = axis(box.max, i);
        if (std::abs(d) < 1e-8f) {
            // Parallel to this slab: either always inside it or never.
            if (o < lo || o > hi) return std::nullopt;
            continue;
        }
        float t0 = (lo - o) / d, t1 = (hi - o) / d;
        float near_sign = -1.0f; // entering through the min face, whose normal points -axis
        if (t0 > t1) {
            std::swap(t0, t1); // travelling towards -axis: we enter through the max face
            near_sign = 1.0f;
        }
        if (t0 > t_enter) {
            t_enter = t0;
            enter_normal = unit_axis(i, near_sign);
        }
        t_exit = std::min(t_exit, t1);
        if (t_enter > t_exit) return std::nullopt;
    }
    return RayHit{t_enter, enter_normal};
}

} // namespace eng
