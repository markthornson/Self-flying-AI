#include "approx.h"

#include "engine/physics/collision.h"

using namespace eng;

TEST_CASE("sphere vs sphere") {
    CHECK_FALSE(collide(Sphere{{0, 0, 0}, 1}, Sphere{{3, 0, 0}, 1}));

    auto c = collide(Sphere{{1.5f, 0, 0}, 1}, Sphere{{0, 0, 0}, 1});
    REQUIRE(c);
    check_near(c->normal, {1, 0, 0}); // from b towards a
    CHECK(c->depth == doctest::Approx(0.5f));
}

TEST_CASE("sphere resting on a box is pushed straight up") {
    Aabb floor{{-5, -1, -5}, {5, 0, 5}};
    CHECK_FALSE(collide(Sphere{{0, 0.6f, 0}, 0.5f}, floor));

    auto c = collide(Sphere{{1, 0.4f, 2}, 0.5f}, floor);
    REQUIRE(c);
    check_near(c->normal, {0, 1, 0});
    CHECK(c->depth == doctest::Approx(0.1f));
}

TEST_CASE("sphere touching a box edge is pushed out diagonally") {
    Aabb box{{0, 0, 0}, {1, 1, 1}};
    auto c = collide(Sphere{{1.3f, 1.3f, 0.5f}, 0.5f}, box);
    REQUIRE(c);
    float d = 1.0f / std::sqrt(2.0f);
    check_near(c->normal, {d, d, 0});
}

TEST_CASE("a sphere whose centre is inside a box leaves by the nearest face") {
    Aabb box{{0, 0, 0}, {4, 1, 4}};
    auto c = collide(Sphere{{2, 0.8f, 2}, 0.5f}, box);
    REQUIRE(c);
    check_near(c->normal, {0, 1, 0});
    CHECK(c->depth == doctest::Approx(0.7f)); // 0.2 to the top face plus the radius
}

TEST_CASE("box vs sphere is sphere vs box reversed") {
    Aabb box{{0, 0, 0}, {1, 1, 1}};
    Sphere s{{0.5f, 1.3f, 0.5f}, 0.5f};
    auto a = collide(s, box);
    auto b = collide(box, s);
    REQUIRE(a);
    REQUIRE(b);
    check_near(b->normal, -a->normal);
    CHECK(b->depth == doctest::Approx(a->depth));
}

TEST_CASE("box vs box separates along the axis of least overlap") {
    Aabb a = Aabb::from_center({0.9f, 0.2f, 0}, {0.5f, 0.5f, 0.5f});
    Aabb b = Aabb::from_center({0, 0, 0}, {0.5f, 0.5f, 0.5f});
    auto c = collide(a, b);
    REQUIRE(c);
    check_near(c->normal, {1, 0, 0}); // overlap 0.1 on X beats 0.8 on Y
    CHECK(c->depth == doctest::Approx(0.1f));

    CHECK_FALSE(collide(Aabb::from_center({0, 2, 0}, {0.5f, 0.5f, 0.5f}), b)); // a gap on Y
}

TEST_CASE("ray vs sphere") {
    Sphere s{{0, 0, -5}, 1};
    auto hit = raycast(Ray{{0, 0, 0}, {0, 0, -1}}, s, 100);
    REQUIRE(hit);
    CHECK(hit->distance == doctest::Approx(4));
    check_near(hit->normal, {0, 0, 1});

    CHECK_FALSE(raycast(Ray{{0, 0, 0}, {0, 0, 1}}, s, 100));   // pointing away
    CHECK_FALSE(raycast(Ray{{0, 2, 0}, {0, 0, -1}}, s, 100));  // passes above
    CHECK_FALSE(raycast(Ray{{0, 0, 0}, {0, 0, -1}}, s, 3.9f)); // too short
    CHECK(raycast(Ray{{0, 0, -5}, {1, 0, 0}}, s, 1)->distance == 0); // starts inside
}

TEST_CASE("ray vs box, from each side") {
    Aabb box{{-1, -1, -1}, {1, 1, 1}};
    auto down = raycast(Ray{{0, 5, 0}, {0, -1, 0}}, box, 100);
    REQUIRE(down);
    CHECK(down->distance == doctest::Approx(4));
    check_near(down->normal, {0, 1, 0});

    auto from_left = raycast(Ray{{-3, 0.5f, 0}, {1, 0, 0}}, box, 100);
    REQUIRE(from_left);
    CHECK(from_left->distance == doctest::Approx(2));
    check_near(from_left->normal, {-1, 0, 0});

    CHECK_FALSE(raycast(Ray{{-3, 2, 0}, {1, 0, 0}}, box, 100)); // parallel, outside the slab
    CHECK_FALSE(raycast(Ray{{0, 5, 0}, {0, 1, 0}}, box, 100));  // pointing away
    CHECK_FALSE(raycast(Ray{{0, 5, 0}, {0, -1, 0}}, box, 3));   // too short
}

TEST_CASE("closest point on a box") {
    Aabb box{{0, 0, 0}, {2, 2, 2}};
    check_near(closest_point(box, {5, 1, -3}), {2, 1, 0});
    check_near(closest_point(box, {1, 1, 1}), {1, 1, 1}); // inside: the point itself
}
