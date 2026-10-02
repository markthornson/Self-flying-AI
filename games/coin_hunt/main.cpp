// Coin Hunt: phase 2's small playable game, and the first real user of the
// ECS, collisions, glTF models, Lua scripting and audio.
//
// Collect every coin before the clock runs out. Diamonds and falling off the
// edge send you back to the start and cost five seconds.
//
//   WASD or arrow keys   move          (or the left stick)
//   Space                jump          (or the A / cross button)
//   R                    restart
//   F1                   debug panel
//   F5                   reload the Lua script (edit it while the game runs)
//   Escape               quit
//
// The split between C++ and Lua is the point of this file. C++ (here) owns
// the frame: it loads assets, runs the systems in a fixed order each step,
// moves the camera and draws. Lua (scripts/game.lua) owns the rules: what the
// level looks like, how the player moves, what a coin does when touched,
// and what the HUD says.

#include "engine/app.h"
#include "engine/assets/library.h"
#include "engine/core/log.h"
#include "engine/core/math/math.h"
#include "engine/physics/physics.h"
#include "engine/script/scripting.h"
#include "engine/world/components.h"
#include "engine/world/systems.h"

#include <imgui.h>

#include <SDL3/SDL_main.h> // lets SDL provide the right main()/WinMain per platform

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace {

using namespace eng;

const std::filesystem::path kGameDir = COIN_HUNT_DIR;

class CoinHunt final : public Game {
public:
    void start(App& app) override {
        // --- Assets -----------------------------------------------------------
        const auto models = kGameDir / "assets" / "models";
        for (const char* name : {"player", "coin", "crate", "platform", "enemy"})
            assets_.load_model(app.renderer(), name, models / (std::string(name) + ".glb"));
        shadow_mesh_ = app.renderer().create_mesh(make_disc_mesh({0.08f, 0.09f, 0.1f}));

        const auto sounds = kGameDir / "assets" / "sounds";
        for (const char* name : {"coin", "jump", "hurt", "win", "lose", "music"})
            assets_.load_sound(app.audio(), name, sounds / (std::string(name) + ".wav"));

        // --- Controls ---------------------------------------------------------
        Input& in = app.input();
        in.bind_axis_keys("move_x", SDL_SCANCODE_A, SDL_SCANCODE_D);
        in.bind_axis_keys("move_x", SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
        in.bind_gamepad_axis("move_x", SDL_GAMEPAD_AXIS_LEFTX);
        in.bind_axis_keys("move_forward", SDL_SCANCODE_S, SDL_SCANCODE_W);
        in.bind_axis_keys("move_forward", SDL_SCANCODE_DOWN, SDL_SCANCODE_UP);
        in.bind_gamepad_axis("move_forward", SDL_GAMEPAD_AXIS_LEFTY, /*invert=*/true);
        in.bind_key("jump", SDL_SCANCODE_SPACE);
        in.bind_gamepad_button("jump", SDL_GAMEPAD_BUTTON_SOUTH);
        in.bind_key("restart", SDL_SCANCODE_R);
        in.bind_gamepad_button("restart", SDL_GAMEPAD_BUTTON_START);
        in.bind_key("debug", SDL_SCANCODE_F1);
        in.bind_key("reload", SDL_SCANCODE_F5);
        in.bind_key("quit", SDL_SCANCODE_ESCAPE);

        // --- Script -----------------------------------------------------------
        scripting_ = std::make_unique<Scripting>(ScriptServices{world_, physics_, in, app.audio(), assets_});
        load_script();
    }

    void fixed_update(App& app, float dt) override {
        Input& in = app.input();
        if (in.pressed("quit")) app.quit();
        if (in.pressed("debug")) show_debug_ = !show_debug_;
        if (in.pressed("reload")) load_script();

        // The order of systems in a step matters, and here it is explicit:
        save_previous_transforms(world_); // 1. remember where things were, for drawing
        scripting_->update(dt);           // 2. game rules: read input, set velocities
        physics_.step(world_, dt);        // 3. move bodies and resolve collisions
        // 4. tell the script what touched what. Copy the events first: the
        //    script may restart the level, which clears the physics state.
        std::vector<TriggerEvent> events = physics_.trigger_events();
        for (const TriggerEvent& e : events) scripting_->on_trigger(e.trigger, e.other);
        world_.flush();                   // 5. destroy what the script asked to
        update_camera(dt);                // 6. follow the player
    }

    void render(App& app, Renderer& renderer, float alpha) override {
        Camera camera = camera_;
        camera.position = lerp(camera_previous_.position, camera_.position, alpha);
        camera.target = lerp(camera_previous_.target, camera_.target, alpha);
        renderer.begin_frame(camera, sky_color_);

        draw_meshes(world_, renderer, alpha);
        draw_blob_shadows(renderer, alpha);

        // The listener is the camera, so sounds pan with what you see.
        app.audio().set_listener({camera.position, normalize(camera.target - camera.position), {0.0f, 1.0f, 0.0f}});
    }

    void debug_ui(App& app) override {
        scripting_->hud();
        if (!show_debug_) return;

        const FrameStats& s = app.stats();
        Audio& audio = app.audio();
        ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x - 330.0f, 10.0f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({320.0f, 0.0f}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Debug (F1)");
        ImGui::Text("%.1f fps (%.2f ms)  %s", s.fps, s.frame_ms, app.renderer().backend_name());
        ImGui::Text("Entities: %zu  bodies: %zu  colliders: %zu", world_.entity_count(), world_.count<Body>(),
                    world_.count<Collider>());
        ImGui::Text("Draw calls: %zu", app.renderer().draws_last_frame());
        if (ImGui::Button("Reload script (F5)")) load_script();

        ImGui::SeparatorText("Audio");
        ImGui::Text("Device: %s   voices: %d", audio.valid() ? "open" : "none", audio.voices_playing());
        float master = audio.master_volume();
        if (ImGui::SliderFloat("Master", &master, 0.0f, 1.0f)) audio.set_master_volume(master);
        const char* bus_names[kBusCount] = {"Music", "Effects", "UI"};
        for (int i = 0; i < kBusCount; ++i) {
            auto bus = static_cast<Bus>(i);
            float v = audio.bus_volume(bus);
            if (ImGui::SliderFloat(bus_names[i], &v, 0.0f, 1.0f)) audio.set_bus_volume(bus, v);
        }

        ImGui::SeparatorText("World");
        ImGui::DragFloat3("Gravity", &physics_.gravity.x, 0.1f);
        ImGui::DragFloat3("Camera offset", &camera_offset_.x, 0.1f);
        ImGui::SliderFloat("Camera stiffness", &camera_stiffness_, 1.0f, 20.0f);
        ImGui::End();
    }

private:
    void load_script() {
        // A clean slate: the script builds the whole level in start().
        world_.clear();
        physics_.reset();
        if (scripting_->load(kGameDir / "scripts" / "game.lua")) scripting_->start();
        update_camera(0.0f);
        camera_previous_ = camera_;
    }

    void update_camera(float dt) {
        camera_previous_ = camera_;
        std::optional<Entity> player;
        world_.each<Name>([&](Entity e, Name& n) {
            if (!player && n.value == "player") player = e;
        });
        if (!player) return;
        const Transform* t = world_.get<Transform>(*player);
        if (!t) return;

        // Look a little above the player, but don't follow them far down:
        // falling off the world should look like falling.
        Vec3 target = t->position + Vec3{0.0f, 0.5f, 0.0f};
        target.y = std::max(target.y, -1.0f);
        Vec3 desired = target + camera_offset_;

        // Ease towards where the camera wants to be. 1 - e^(-k dt) is the
        // frame-rate independent form of "move a fraction of the way there":
        // the same stiffness feels the same at any step length. A player who
        // jumped several metres in one step was teleported (a respawn), so
        // cut straight there instead.
        bool teleported = length(t->position - last_player_position_) > 3.0f;
        last_player_position_ = t->position;
        float blend = dt > 0.0f && !teleported ? 1.0f - std::exp(-camera_stiffness_ * dt) : 1.0f;
        camera_.position = lerp(camera_.position, desired, blend);
        camera_.target = lerp(camera_.target, target, blend);
        if (teleported) camera_previous_ = camera_;
    }

    // A dark disc on the ground under everything that moves. Real shadows come
    // in phase 4; a blob shadow is what many games shipped with for years, and
    // it matters more than you'd think for judging where a jump will land.
    void draw_blob_shadows(Renderer& renderer, float alpha) {
        world_.each<Body, Transform>([&](Entity e, Body&, Transform& current) {
            const PreviousTransform* previous = world_.get<PreviousTransform>(e);
            Vec3 pos = previous ? lerp(previous->value.position, current.position, alpha) : current.position;
            auto hit = physics_.raycast(world_, Ray{pos, {0.0f, -1.0f, 0.0f}}, 30.0f, e);
            if (!hit) return; // over the void: no shadow
            // Shrink the shadow as the object rises.
            float size = clamp(1.0f - hit->distance * 0.08f, 0.3f, 1.0f) * 0.9f;
            Transform shadow;
            shadow.position = hit->point + Vec3{0.0f, 0.02f, 0.0f}; // just above, so it doesn't flicker
            shadow.scale = {size, 1.0f, size};
            renderer.draw(shadow_mesh_, shadow.to_matrix());
        });
    }

    World world_;
    Physics physics_;
    AssetLibrary assets_;
    std::unique_ptr<Scripting> scripting_;
    MeshHandle shadow_mesh_;

    Camera camera_{.position = {0.0f, 7.0f, 10.0f}, .target = {0.0f, 0.0f, 0.0f}, .fov_y_degrees = 55.0f};
    Camera camera_previous_ = camera_;
    Vec3 camera_offset_{0.0f, 6.5f, 9.0f};
    Vec3 last_player_position_{};
    float camera_stiffness_ = 6.0f;
    Vec4 sky_color_{0.45f, 0.65f, 0.9f, 1.0f};
    bool show_debug_ = false;
};

} // namespace

int main(int, char**) {
    CoinHunt game;
    eng::App app({.window = {.title = "Coin Hunt", .width = 1280, .height = 720}});
    return app.run(game);
}
