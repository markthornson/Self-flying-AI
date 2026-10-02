#include "engine/script/scripting.h"

#include "engine/assets/library.h"
#include "engine/audio/audio.h"
#include "engine/core/log.h"
#include "engine/core/math/math.h"
#include "engine/physics/physics.h"
#include "engine/platform/input.h"
#include "engine/world/components.h"
#include "engine/world/systems.h"

#include <imgui.h>
#include <sol/sol.hpp>

#include <cfloat>
#include <cmath>
#include <cstdio>

namespace eng {

struct Scripting::Impl {
    ScriptServices s;
    sol::state lua;
    std::string error;
    bool in_hud = false;

    // A fresh Lua state with the engine API bound.
    explicit Impl(ScriptServices services) : s(services) {
        // Only the safe standard libraries. A game script has no business
        // opening files (io) or running programs (os).
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::package);
        bind_math();
        bind_entity();
        bind_world();
        bind_systems();
    }

    void report(const std::string& message) {
        error = message;
        ENGINE_LOG_ERROR("Lua: %s", message.c_str());
    }

    // Calls a global Lua function if the script defines it. After an error we
    // stop calling into the script, so one mistake shows one message instead
    // of sixty a second.
    template <typename... Args>
    void call(const char* name, Args&&... args) {
        if (!error.empty()) return;
        sol::protected_function fn = lua[name];
        if (!fn.valid()) return;
        sol::protected_function_result result = fn(std::forward<Args>(args)...);
        if (!result.valid()) {
            sol::error err = result;
            report(err.what());
        }
    }

    void bind_math();
    void bind_entity();
    void bind_world();
    void bind_systems();
};

namespace {

// Reads a scale from a script: a single number for "the same on every axis",
// or a vec3.
Vec3 to_scale(const sol::object& value) {
    if (value.is<float>()) {
        float k = value.as<float>();
        return {k, k, k};
    }
    if (value.is<Vec3>()) return value.as<Vec3>();
    return {1.0f, 1.0f, 1.0f};
}

// The angle of a rotation about +Y. Only meaningful for rotations that are
// purely about Y, which is all the script API makes.
float yaw_of(Quat q) { return 2.0f * std::atan2(q.y, q.w); }

} // namespace

// --- vec3 ------------------------------------------------------------------------

void Scripting::Impl::bind_math() {
    lua.new_usertype<Vec3>(
        "vec3",
        // vec3(x, y, z) or vec3() for zero.
        sol::call_constructor,
        sol::factories([](float x, float y, float z) { return Vec3{x, y, z}; }, [] { return Vec3{}; }),
        // Fields as properties. (Binding &Vec3::x directly is shorter, but
        // trips a sol2 3.5 template bug on recent Clang.)
        "x", sol::property([](Vec3& v) { return v.x; }, [](Vec3& v, float x) { v.x = x; }),
        "y", sol::property([](Vec3& v) { return v.y; }, [](Vec3& v, float y) { v.y = y; }),
        "z", sol::property([](Vec3& v) { return v.z; }, [](Vec3& v, float z) { v.z = z; }),
        sol::meta_function::addition, [](Vec3 a, Vec3 b) { return a + b; },
        sol::meta_function::subtraction, [](Vec3 a, Vec3 b) { return a - b; },
        sol::meta_function::unary_minus, [](Vec3 v) { return -v; },
        sol::meta_function::multiplication,
        sol::overload([](Vec3 v, float k) { return v * k; }, [](float k, Vec3 v) { return v * k; }),
        sol::meta_function::division, [](Vec3 v, float k) { return v / k; },
        sol::meta_function::equal_to, [](Vec3 a, Vec3 b) { return a == b; },
        sol::meta_function::to_string,
        [](Vec3 v) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "vec3(%g, %g, %g)", v.x, v.y, v.z);
            return std::string(buf);
        },
        "length", [](Vec3 v) { return length(v); },
        "normalized", [](Vec3 v) { return normalize(v); },
        "dot", [](Vec3 a, Vec3 b) { return dot(a, b); },
        "cross", [](Vec3 a, Vec3 b) { return cross(a, b); },
        "lerp", [](Vec3 a, Vec3 b, float t) { return lerp(a, b, t); });
}

// --- Entity ------------------------------------------------------------------------
//
// Scripts hold Entity ids, never pointers into component arrays, so a script
// can keep an entity in a variable forever and safely ask e:alive() later.

void Scripting::Impl::bind_entity() {
    World& w = s.world;
    lua.new_usertype<Entity>(
        "Entity", sol::no_constructor,
        "alive", [&w](Entity e) { return w.alive(e); },
        "name",
        [&w](Entity e) {
            const Name* n = w.get<Name>(e);
            return n ? n->value : std::string();
        },
        "position",
        [&w](Entity e) {
            const Transform* t = w.get<Transform>(e);
            return t ? t->position : Vec3{};
        },
        "set_position",
        [&w](Entity e, Vec3 p) {
            if (Transform* t = w.get<Transform>(e)) t->position = p;
        },
        // Like set_position, but jumps instead of sliding there over a frame,
        // and stops the entity moving.
        "teleport",
        [&w](Entity e, Vec3 p) {
            if (Transform* t = w.get<Transform>(e)) t->position = p;
            if (Body* b = w.get<Body>(e)) b->velocity = {};
            skip_interpolation(w, e);
        },
        "scale",
        [&w](Entity e) {
            const Transform* t = w.get<Transform>(e);
            return t ? t->scale : Vec3{1.0f, 1.0f, 1.0f};
        },
        "set_scale",
        [&w](Entity e, sol::object scale) {
            if (Transform* t = w.get<Transform>(e)) t->scale = to_scale(scale);
        },
        // Facing, as an angle in radians about the up axis. 0 faces +Z.
        "yaw",
        [&w](Entity e) {
            const Transform* t = w.get<Transform>(e);
            return t ? yaw_of(t->rotation) : 0.0f;
        },
        "set_yaw",
        [&w](Entity e, float radians) {
            if (Transform* t = w.get<Transform>(e)) t->rotation = quat_from_axis_angle({0.0f, 1.0f, 0.0f}, radians);
        },
        "velocity",
        [&w](Entity e) {
            const Body* b = w.get<Body>(e);
            return b ? b->velocity : Vec3{};
        },
        "set_velocity",
        [&w](Entity e, Vec3 v) {
            if (Body* b = w.get<Body>(e)) b->velocity = v;
        },
        "on_ground",
        [&w](Entity e) {
            const Body* b = w.get<Body>(e);
            return b && b->on_ground;
        },
        "set_tint",
        [&w](Entity e, float r, float g, float b) {
            if (MeshRenderer* m = w.get<MeshRenderer>(e)) m->tint = {r, g, b, 1.0f};
        },
        // Destroyed at the end of this step, so it is safe from any callback.
        "destroy", [&w](Entity e) { w.destroy_later(e); },
        sol::meta_function::equal_to, [](Entity a, Entity b) { return a == b; },
        sol::meta_function::to_string,
        [](Entity e) { return "Entity(" + std::to_string(e.index) + "." + std::to_string(e.generation) + ")"; });
}

// --- world ---------------------------------------------------------------------------

void Scripting::Impl::bind_world() {
    World& w = s.world;
    sol::table world = lua.create_named_table("world");

    // world.spawn{ name = "coin", model = "coin", position = vec3(0, 1, 0),
    //              scale = 1, yaw = 0, tint = vec3(1, 1, 1),
    //              collider = "box" | "sphere", trigger = false, body = false,
    //              gravity = 1 }
    // Every key is optional. Returns the new Entity.
    world["spawn"] = [this, &w](sol::table desc) {
        Entity e = w.create();

        Transform t;
        t.position = desc.get<sol::optional<Vec3>>("position").value_or(Vec3{});
        t.scale = to_scale(desc["scale"]);
        t.rotation = quat_from_axis_angle({0.0f, 1.0f, 0.0f}, desc.get_or("yaw", 0.0f));
        w.add<Transform>(e, t);
        w.add<PreviousTransform>(e, PreviousTransform{t});

        if (auto name = desc.get<sol::optional<std::string>>("name")) w.add<Name>(e, Name{*name});

        if (auto model = desc.get<sol::optional<std::string>>("model")) {
            MeshHandle mesh = s.assets.model(*model);
            if (!mesh.valid()) ENGINE_LOG_WARN("world.spawn: no model called '%s'", model->c_str());
            Vec3 tint = desc.get<sol::optional<Vec3>>("tint").value_or(Vec3{1.0f, 1.0f, 1.0f});
            w.add<MeshRenderer>(e, MeshRenderer{mesh, {tint.x, tint.y, tint.z, 1.0f}});
        }

        if (auto shape = desc.get<sol::optional<std::string>>("collider")) {
            Collider c = *shape == "sphere" ? Collider::sphere(desc.get_or("radius", 0.5f)) : Collider::box();
            c.is_trigger = desc.get_or("trigger", false);
            w.add<Collider>(e, c);
        }

        if (desc.get_or("body", false)) w.add<Body>(e, Body{.gravity_scale = desc.get_or("gravity", 1.0f)});
        return e;
    };

    // The first live entity with this name, or nil.
    world["find"] = [&w](const std::string& name) -> std::optional<Entity> {
        std::optional<Entity> found;
        w.each<Name>([&](Entity e, Name& n) {
            if (!found && n.value == name) found = e;
        });
        return found;
    };

    // Every entity with this name, as a list.
    world["find_all"] = [&w](const std::string& name, sol::this_state state) {
        sol::table list = sol::state_view(state).create_table();
        int i = 1; // Lua lists start at 1
        w.each<Name>([&](Entity e, Name& n) {
            if (n.value == name) list[i++] = e;
        });
        return list;
    };

    world["clear"] = [this, &w] {
        w.clear();
        s.physics.reset();
    };
    world["count"] = [&w] { return w.entity_count(); };
}

// --- input, audio, physics, ui, log ------------------------------------------------

void Scripting::Impl::bind_systems() {
    Input& in = s.input;
    sol::table input = lua.create_named_table("input");
    input["axis"] = [&in](const std::string& name) { return in.axis(name); };
    input["held"] = [&in](const std::string& name) { return in.held(name); };
    input["pressed"] = [&in](const std::string& name) { return in.pressed(name); };

    Audio& au = s.audio;
    const AssetLibrary& assets = s.assets;
    sol::table audio = lua.create_named_table("audio");
    audio["play"] = [&au, &assets](const std::string& name, sol::optional<float> volume, sol::optional<float> pitch) {
        au.play(assets.sound(name), Bus::Effects, volume.value_or(1.0f), pitch.value_or(1.0f));
    };
    audio["play_at"] = [&au, &assets](const std::string& name, Vec3 position, sol::optional<float> volume) {
        au.play_at(assets.sound(name), position, volume.value_or(1.0f));
    };
    audio["play_ui"] = [&au, &assets](const std::string& name, sol::optional<float> volume) {
        au.play(assets.sound(name), Bus::Ui, volume.value_or(1.0f));
    };
    audio["music"] = [&au, &assets](const std::string& name, sol::optional<float> volume) {
        au.play_music(assets.sound(name), volume.value_or(1.0f));
    };
    audio["stop_music"] = [&au] { au.stop_music(); };

    sol::table physics = lua.create_named_table("physics");
    // physics.raycast(origin, direction, max_distance, ignore) returns
    // { entity, distance, point, normal } for the nearest solid hit, or nil.
    physics["raycast"] = [this](Vec3 origin, Vec3 direction, float max_distance, sol::optional<Entity> ignore,
                                sol::this_state state) -> sol::object {
        auto hit = s.physics.raycast(s.world, Ray{origin, normalize(direction)}, max_distance,
                                     ignore.value_or(kNullEntity));
        if (!hit) return sol::lua_nil;
        sol::table t = sol::state_view(state).create_table();
        t["entity"] = hit->entity;
        t["distance"] = hit->distance;
        t["point"] = hit->point;
        t["normal"] = hit->normal;
        return t;
    };
    physics["gravity"] = [this] { return s.physics.gravity; };
    physics["set_gravity"] = [this](Vec3 g) { s.physics.gravity = g; };

    // ui.text / ui.title only work inside hud(); elsewhere there is no frame
    // being built to put them in.
    sol::table ui = lua.create_named_table("ui");
    ui["text"] = [this](const std::string& text) {
        if (in_hud) ImGui::TextUnformatted(text.c_str());
    };
    ui["title"] = [this](const std::string& text) {
        if (!in_hud) return;
        // Big text in the middle of the screen, drawn straight onto ImGui's
        // foreground layer rather than inside a window.
        const float size = 64.0f;
        ImFont* font = ImGui::GetFont();
        ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImVec2 pos{(display.x - extent.x) * 0.5f, display.y * 0.38f - extent.y * 0.5f};
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        draw->AddText(font, size, {pos.x + 3, pos.y + 3}, IM_COL32(0, 0, 0, 160), text.c_str()); // shadow
        draw->AddText(font, size, pos, IM_COL32(255, 230, 120, 255), text.c_str());
    };

    // print() and log() both go to the engine log.
    auto log = [](sol::variadic_args args, sol::this_state state) {
        std::string line;
        sol::state_view lua_view(state);
        sol::protected_function tostring = lua_view["tostring"];
        for (auto arg : args) {
            if (!line.empty()) line += ' ';
            line += tostring(arg.get<sol::object>()).get<std::string>();
        }
        ENGINE_LOG_INFO("[lua] %s", line.c_str());
    };
    lua["print"] = log;
    lua["log"] = log;
}

// --- Scripting --------------------------------------------------------------------------

Scripting::Scripting(ScriptServices services) : impl_(std::make_unique<Impl>(services)) {}
Scripting::~Scripting() = default;

bool Scripting::load(const std::filesystem::path& path) {
    // A brand new state: nothing from the old script survives a reload.
    ScriptServices services = impl_->s;
    impl_ = std::make_unique<Impl>(services);
    Impl& self = *impl_;
    self.lua["package"]["path"] = (path.parent_path() / "?.lua").string();

    sol::protected_function_result result = self.lua.safe_script_file(path.string(), sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        self.report(err.what());
        return false;
    }
    ENGINE_LOG_INFO("Loaded script %s", path.string().c_str());
    return true;
}

void Scripting::start() { impl_->call("start"); }
void Scripting::update(float dt) { impl_->call("update", dt); }
void Scripting::on_trigger(Entity trigger, Entity other) { impl_->call("on_trigger", trigger, other); }

void Scripting::hud() {
    Impl& self = *impl_;
    // A see-through, borderless window in the top-left corner for ui.text().
    ImGui::SetNextWindowPos({16.0f, 16.0f});
    ImGui::SetNextWindowBgAlpha(0.35f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    ImGui::Begin("##hud", nullptr, flags);
    ImGui::PushFont(nullptr, 26.0f);
    self.in_hud = true;
    self.call("hud");
    self.in_hud = false;
    if (!self.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 110, 110, 255));
        ImGui::PushTextWrapPos(ImGui::GetIO().DisplaySize.x * 0.6f);
        ImGui::TextUnformatted(("Script error: " + self.error).c_str());
        ImGui::TextUnformatted("Fix the script and press F5 to reload.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    ImGui::End();
}

bool Scripting::run(const std::string& code) {
    sol::protected_function_result result = impl_->lua.safe_script(code, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        impl_->report(err.what());
        return false;
    }
    return true;
}

const std::string& Scripting::error() const { return impl_->error; }

} // namespace eng
