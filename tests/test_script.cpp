#include "approx.h"

#include "engine/assets/assets.h"
#include "engine/audio/audio.h"
#include "engine/physics/physics.h"
#include "engine/platform/input.h"
#include "engine/script/scripting.h"
#include "engine/world/components.h"

using namespace eng;

namespace {

// Everything a script can reach, with a silent audio device.
struct ScriptFixture {
    World world;
    Physics physics;
    Input input;
    Audio audio{/*open_device=*/false};
    Assets assets{/*renderer=*/nullptr, audio};
    Scripting scripting{ScriptServices{world, physics, input, audio, assets}};
};

} // namespace

TEST_CASE("vec3 does arithmetic in Lua") {
    ScriptFixture f;
    REQUIRE(f.scripting.run(R"lua(
        local v = vec3(1, 2, 3) + vec3(1, 1, 1) * 2
        assert(v.x == 3 and v.y == 4 and v.z == 5)
        assert(vec3(3, 0, 4):length() == 5)
        assert(tostring(-vec3(1, 2, 3)) == "vec3(-1, -2, -3)")
    )lua"));
}

TEST_CASE("world.spawn builds an entity from a table") {
    ScriptFixture f;
    REQUIRE(f.scripting.run(R"lua(
        coin = world.spawn{ name = "coin", position = vec3(1, 2, 3), scale = 2,
                            collider = "sphere", radius = 0.25, trigger = true }
        player = world.spawn{ name = "player", collider = "box", body = true, gravity = 0 }
        assert(coin:alive() and coin:name() == "coin")
        assert(world.find("player") == player)
        assert(#world.find_all("coin") == 1)
    )lua"));
    CHECK(f.world.entity_count() == 2);
    CHECK(f.world.count<Body>() == 1);

    bool checked = false;
    f.world.each<Collider, Transform>([&](Entity, Collider& c, Transform& t) {
        if (!c.is_trigger) return;
        check_near(t.position, {1, 2, 3});
        check_near(t.scale, {2, 2, 2});
        CHECK(c.shape == Collider::Shape::Sphere);
        CHECK(c.radius == doctest::Approx(0.25f));
        checked = true;
    });
    CHECK(checked);
}

TEST_CASE("the engine calls the script's update and on_trigger") {
    ScriptFixture f;
    REQUIRE(f.scripting.run(R"lua(
        steps, touched = 0, nil
        function update(dt) steps = steps + 1 end
        function on_trigger(trigger, other) touched = trigger:name() .. "/" .. other:name() end
        coin = world.spawn{ name = "coin", position = vec3(0, 0, 0), collider = "sphere", trigger = true }
        ball = world.spawn{ name = "ball", position = vec3(0, 0, 0), collider = "sphere", body = true, gravity = 0 }
    )lua"));
    f.scripting.update(1.0f / 60.0f);
    f.scripting.update(1.0f / 60.0f);
    f.physics.step(f.world, 1.0f / 60.0f);
    for (const TriggerEvent& e : f.physics.trigger_events()) f.scripting.on_trigger(e.trigger, e.other);
    CHECK(f.scripting.run("assert(steps == 2 and touched == 'coin/ball')"));
}

TEST_CASE("destroying from a script is deferred to the next flush") {
    ScriptFixture f;
    REQUIRE(f.scripting.run(R"lua(
        e = world.spawn{ name = "temp" }
        e:destroy()
        assert(e:alive()) -- still here until the step ends
    )lua"));
    f.world.flush();
    CHECK(f.world.entity_count() == 0);
    CHECK(f.scripting.run("assert(not e:alive())"));
}

TEST_CASE("script errors are caught and reported, not thrown") {
    ScriptFixture f;
    CHECK_FALSE(f.scripting.run("this is not lua"));
    CHECK_FALSE(f.scripting.error().empty());
}

TEST_CASE("a runtime error in a callback stops further calls until reload") {
    ScriptFixture f;
    REQUIRE(f.scripting.run(R"lua(
        calls = 0
        function update(dt) calls = calls + 1; error("boom") end
    )lua"));
    f.scripting.update(0.016f);
    f.scripting.update(0.016f);
    CHECK(f.scripting.error().find("boom") != std::string::npos);
    CHECK(f.scripting.run("assert(calls == 1)")); // the second update was skipped
}

TEST_CASE("scripts can't reach the file system") {
    ScriptFixture f;
    CHECK(f.scripting.run("assert(io == nil and os == nil)"));
}

TEST_CASE("physics.raycast is reachable from Lua") {
    ScriptFixture f;
    CHECK(f.scripting.run(R"lua(
        local floor = world.spawn{ name = "floor", position = vec3(0, -0.5, 0), scale = vec3(10, 1, 10), collider = "box" }
        local hit = physics.raycast(vec3(0, 5, 0), vec3(0, -1, 0), 20)
        assert(hit and hit.entity == floor and math.abs(hit.distance - 5) < 1e-4)
        assert(hit.normal == vec3(0, 1, 0))
        assert(physics.raycast(vec3(0, 5, 0), vec3(0, 1, 0), 20) == nil)
        assert(physics.raycast(vec3(0, 5, 0), vec3(0, -1, 0), 20, floor) == nil) -- ignored
    )lua"));
}

TEST_CASE("the console evaluates expressions and statements without stopping the script") {
    ScriptFixture f;
    REQUIRE(f.scripting.run("answer = 41"));

    // An expression shows its value, and several values are all shown.
    auto r = f.scripting.evaluate("answer + 1");
    CHECK(r.ok);
    CHECK(r.text == "42");
    CHECK(f.scripting.evaluate("1, 'two'").text == "1    two");

    // A statement runs and shows nothing.
    r = f.scripting.evaluate("answer = 7");
    CHECK(r.ok);
    CHECK(r.text.empty());
    CHECK(f.scripting.evaluate("answer").text == "7");

    // Mistakes come back as text, and the game's script keeps running.
    r = f.scripting.evaluate("nil + 1");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.text.empty());
    CHECK(f.scripting.evaluate("this is not lua").ok == false);
    CHECK(f.scripting.error().empty());
}
