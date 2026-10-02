#include "approx.h"

#include "engine/physics/physics.h"

using namespace eng;

namespace {

Entity add_floor(World& world) {
    Entity floor = world.create();
    world.add<Transform>(floor, Transform{.position = {0, -0.5f, 0}, .scale = {20, 1, 20}});
    world.add<Collider>(floor, Collider::box());
    return floor;
}

Entity add_ball(World& world, Vec3 position) {
    Entity ball = world.create();
    world.add<Transform>(ball, Transform{.position = position});
    world.add<Collider>(ball, Collider::sphere(0.5f));
    world.add<Body>(ball);
    return ball;
}

} // namespace

TEST_CASE("a body falls under gravity and comes to rest on the floor") {
    World world;
    Physics physics;
    add_floor(world);
    Entity ball = add_ball(world, {0, 3, 0});

    for (int i = 0; i < 120; ++i) physics.step(world, 1.0f / 60.0f);

    CHECK(world.get<Transform>(ball)->position.y == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(world.get<Body>(ball)->on_ground);
    CHECK(world.get<Body>(ball)->velocity.y == doctest::Approx(0.0f));
}

TEST_CASE("a body slides along a wall instead of stopping dead") {
    World world;
    Physics physics;
    physics.gravity = {};
    Entity wall = world.create();
    world.add<Transform>(wall, Transform{.position = {2, 0, 0}, .scale = {1, 4, 20}});
    world.add<Collider>(wall, Collider::box());
    Entity ball = add_ball(world, {0, 0, 0});
    world.get<Body>(ball)->velocity = {5, 0, -5}; // into the wall and along it

    for (int i = 0; i < 60; ++i) physics.step(world, 1.0f / 60.0f);

    const Transform& t = *world.get<Transform>(ball);
    CHECK(t.position.x == doctest::Approx(1.0f).epsilon(0.01)); // stopped at the wall face (1.5) minus radius
    CHECK(t.position.z < -4.0f);                                // but kept moving along it
    CHECK_FALSE(world.get<Body>(ball)->on_ground);              // a wall is not a floor
}

TEST_CASE("triggers report each entry once") {
    World world;
    Physics physics;
    physics.gravity = {};
    Entity coin = world.create();
    world.add<Transform>(coin, Transform{.position = {2, 0, 0}});
    world.add<Collider>(coin, Collider{.shape = Collider::Shape::Sphere, .radius = 0.5f, .is_trigger = true});
    Entity ball = add_ball(world, {0, 0, 0});
    world.get<Body>(ball)->velocity = {6, 0, 0};

    int entries = 0;
    for (int i = 0; i < 30; ++i) {
        physics.step(world, 1.0f / 60.0f);
        for (const TriggerEvent& e : physics.trigger_events()) {
            CHECK(e.trigger == coin);
            CHECK(e.other == ball);
            ++entries;
        }
    }
    CHECK(entries == 1);
    // Triggers never push anything: the ball sailed straight through.
    CHECK(world.get<Transform>(ball)->position.x > 2.5f);
}

TEST_CASE("raycast finds the nearest solid collider and skips triggers and the ignored entity") {
    World world;
    Physics physics;
    Entity floor = add_floor(world);
    Entity ball = add_ball(world, {0, 2, 0});
    Entity trigger = world.create();
    world.add<Transform>(trigger, Transform{.position = {0, 1, 0}});
    world.add<Collider>(trigger, Collider{.shape = Collider::Shape::Box, .is_trigger = true});

    auto hit = physics.raycast(world, Ray{{0, 2, 0}, {0, -1, 0}}, 10, ball);
    REQUIRE(hit);
    CHECK(hit->entity == floor);
    CHECK(hit->distance == doctest::Approx(2));
    check_near(hit->point, {0, 0, 0});
    (void)trigger;
}

TEST_CASE("collider shapes follow the transform's position and scale") {
    WorldShape box = world_shape(Collider::box(), Transform{.position = {1, 2, 3}, .scale = {2, 4, 6}});
    check_near(box.box.min, {0, 0, 0});
    check_near(box.box.max, {2, 4, 6});

    WorldShape sphere = world_shape(Collider::sphere(0.5f), Transform{.position = {0, 1, 0}, .scale = {1, 3, 1}});
    CHECK(sphere.sphere.radius == doctest::Approx(1.5f)); // the largest axis wins
}
