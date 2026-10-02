#include <doctest/doctest.h>

#include "engine/world/ecs.h"

#include <string>
#include <vector>

using namespace eng;

namespace {
struct Position {
    float x = 0, y = 0;
};
struct Health {
    int value = 100;
};
struct Label {
    std::string text;
};
} // namespace

TEST_CASE("entities are created alive and die when destroyed") {
    World world;
    Entity a = world.create();
    Entity b = world.create();
    CHECK(a != b);
    CHECK(world.alive(a));
    CHECK(world.entity_count() == 2);

    world.destroy(a);
    CHECK_FALSE(world.alive(a));
    CHECK(world.alive(b));
    CHECK(world.entity_count() == 1);
}

TEST_CASE("a reused slot gets a new generation, so stale ids stay dead") {
    World world;
    Entity old_id = world.create();
    world.add<Health>(old_id, Health{5});
    world.destroy(old_id);

    Entity new_id = world.create();
    CHECK(new_id.index == old_id.index);           // the slot was reused...
    CHECK(new_id.generation != old_id.generation); // ...under a new generation
    CHECK_FALSE(world.alive(old_id));
    CHECK(world.get<Health>(old_id) == nullptr);   // the old id can't see the new entity's data
    CHECK_FALSE(world.has<Health>(new_id));        // and the dead entity's components are gone
}

TEST_CASE("components can be added, read, replaced and removed") {
    World world;
    Entity e = world.create();
    CHECK_FALSE(world.has<Position>(e));
    CHECK(world.get<Position>(e) == nullptr);

    world.add<Position>(e, Position{1, 2});
    REQUIRE(world.has<Position>(e));
    CHECK(world.get<Position>(e)->y == 2);

    world.add<Position>(e, Position{3, 4}); // replaces
    CHECK(world.get<Position>(e)->x == 3);
    CHECK(world.count<Position>() == 1);

    world.remove<Position>(e);
    CHECK_FALSE(world.has<Position>(e));
    CHECK(world.alive(e)); // removing a component doesn't kill the entity
}

TEST_CASE("removing from the middle keeps every other component findable") {
    // Swap-and-pop moves the last element into the hole; check the moved
    // entity's lookup was updated.
    World world;
    std::vector<Entity> es;
    for (int i = 0; i < 5; ++i) {
        es.push_back(world.create());
        world.add<Health>(es.back(), Health{i});
    }
    world.remove<Health>(es[1]);
    world.destroy(es[2]);
    CHECK(world.count<Health>() == 3);
    CHECK(world.get<Health>(es[0])->value == 0);
    CHECK(world.get<Health>(es[3])->value == 3);
    CHECK(world.get<Health>(es[4])->value == 4);
}

TEST_CASE("each visits exactly the entities with every listed component") {
    World world;
    Entity both = world.create();
    world.add<Position>(both);
    world.add<Health>(both, Health{7});
    Entity only_position = world.create();
    world.add<Position>(only_position);
    Entity only_health = world.create();
    world.add<Health>(only_health);

    std::vector<Entity> seen;
    world.each<Position, Health>([&](Entity e, Position& p, Health& h) {
        seen.push_back(e);
        p.x = static_cast<float>(h.value); // components are writable
    });
    REQUIRE(seen.size() == 1);
    CHECK(seen[0] == both);
    CHECK(world.get<Position>(both)->x == 7);

    int positions = 0;
    world.each<Position>([&](Entity, Position&) { ++positions; });
    CHECK(positions == 2);
}

TEST_CASE("destroy_later is safe while iterating and applies on flush") {
    World world;
    for (int i = 0; i < 4; ++i) world.add<Label>(world.create(), Label{i % 2 ? "odd" : "even"});

    world.each<Label>([&](Entity e, Label& l) {
        if (l.text == "odd") world.destroy_later(e);
    });
    CHECK(world.entity_count() == 4); // nothing happens until flush
    world.flush();
    CHECK(world.entity_count() == 2);
    world.each<Label>([](Entity, Label& l) { CHECK(l.text == "even"); });
}

TEST_CASE("clear destroys everything") {
    World world;
    Entity e = world.create();
    world.add<Health>(e);
    world.clear();
    CHECK(world.entity_count() == 0);
    CHECK_FALSE(world.alive(e));
    CHECK(world.count<Health>() == 0);
    CHECK(world.alive(world.create()));
}

TEST_CASE("each_entity visits every live entity, with or without components") {
    World world;
    Entity a = world.create();
    Entity b = world.create();
    Entity c = world.create();
    world.add<Health>(a);
    world.destroy(b);
    std::vector<Entity> seen;
    world.each_entity([&](Entity e) { seen.push_back(e); });
    REQUIRE(seen.size() == 2);
    CHECK(seen[0] == a);
    CHECK(seen[1] == c);
}
