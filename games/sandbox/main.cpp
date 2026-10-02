// Sandbox: the test bed for every new engine feature.
//
// Phase 1's goal: a cube that moves with the keyboard.
//   WASD or arrow keys   move          (or the left stick)
//   Space                jump          (or the A / cross button)
//   R                    reset position
//   Escape               quit
//
// The debug panel shows frame timings and lets you tune the movement live.

#include "engine/app.h"
#include "engine/core/math/math.h"

#include <imgui.h>

#include <SDL3/SDL_main.h> // lets SDL provide the right main()/WinMain per platform

namespace {

using namespace eng;

class Sandbox final : public Game {
public:
    void start(App& app) override {
        cube_mesh_ = app.renderer().create_mesh(make_cube_mesh());
        floor_mesh_ = app.renderer().create_mesh(make_cube_mesh({1.0f, 1.0f, 1.0f}));

        Input& in = app.input();
        in.bind_axis_keys("move_x", SDL_SCANCODE_A, SDL_SCANCODE_D);
        in.bind_axis_keys("move_x", SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
        in.bind_gamepad_axis("move_x", SDL_GAMEPAD_AXIS_LEFTX);
        in.bind_axis_keys("move_forward", SDL_SCANCODE_S, SDL_SCANCODE_W);
        in.bind_axis_keys("move_forward", SDL_SCANCODE_DOWN, SDL_SCANCODE_UP);
        in.bind_gamepad_axis("move_forward", SDL_GAMEPAD_AXIS_LEFTY, /*invert=*/true);
        in.bind_key("jump", SDL_SCANCODE_SPACE);
        in.bind_gamepad_button("jump", SDL_GAMEPAD_BUTTON_SOUTH);
        in.bind_key("reset", SDL_SCANCODE_R);
        in.bind_key("quit", SDL_SCANCODE_ESCAPE);

        reset();
    }

    void fixed_update(App& app, float dt) override {
        Input& in = app.input();
        if (in.pressed("quit")) app.quit();
        if (in.pressed("reset")) reset();

        // Remember where we were, so render() can blend towards where we are.
        previous_ = current_;

        // Horizontal movement. "Forward" is away from the camera, which looks
        // down -Z, so forward input moves the cube towards -Z.
        Vec3 wish{in.axis("move_x"), 0.0f, -in.axis("move_forward")};
        if (length(wish) > 1.0f) wish = normalize(wish); // diagonals aren't faster
        current_.position += wish * (move_speed_ * dt);

        // Jumping and gravity: velocity changes position, gravity changes velocity.
        bool on_ground = current_.position.y <= kRestHeight;
        if (on_ground && in.pressed("jump")) vertical_speed_ = jump_speed_;
        vertical_speed_ -= gravity_ * dt;
        current_.position.y += vertical_speed_ * dt;
        if (current_.position.y < kRestHeight) {
            current_.position.y = kRestHeight;
            vertical_speed_ = 0.0f;
        }

        // Stay on the floor.
        current_.position.x = clamp(current_.position.x, -kArenaHalfSize, kArenaHalfSize);
        current_.position.z = clamp(current_.position.z, -kArenaHalfSize, kArenaHalfSize);

        // Spin slowly so it's obviously a 3D object.
        Quat spin = quat_from_axis_angle({0.0f, 1.0f, 0.0f}, radians(spin_degrees_per_second_) * dt);
        current_.rotation = normalize(spin * current_.rotation);
    }

    void render(App&, Renderer& renderer, float alpha) override {
        renderer.begin_frame(camera_, clear_color_);

        // The floor is a white cube, squashed flat and tinted grey.
        Transform floor;
        floor.position = {0.0f, -0.05f, 0.0f};
        floor.scale = {kArenaHalfSize * 2.0f + 1.0f, 0.1f, kArenaHalfSize * 2.0f + 1.0f};
        renderer.draw(floor_mesh_, floor.to_matrix(), {0.45f, 0.47f, 0.5f, 1.0f});

        // Draw the cube part way between its last two simulation states.
        Transform shown = interpolate(previous_, current_, alpha);
        renderer.draw(cube_mesh_, shown.to_matrix());
    }

    void debug_ui(App& app) override {
        const FrameStats& s = app.stats();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Sandbox");
        ImGui::Text("%.1f fps (%.2f ms)  backend: %s", s.fps, s.frame_ms, app.renderer().backend_name());
        ImGui::Text("Simulation steps: %d this frame, %llu total", s.steps_last_frame,
                    static_cast<unsigned long long>(s.total_steps));
        ImGui::Text("Gamepad: %s", app.input().gamepad_connected() ? "connected" : "none");
        ImGui::SeparatorText("Cube");
        ImGui::Text("Position: %.2f, %.2f, %.2f", current_.position.x, current_.position.y, current_.position.z);
        ImGui::SliderFloat("Move speed", &move_speed_, 0.0f, 20.0f);
        ImGui::SliderFloat("Jump speed", &jump_speed_, 0.0f, 20.0f);
        ImGui::SliderFloat("Gravity", &gravity_, 0.0f, 50.0f);
        ImGui::SliderFloat("Spin (deg/s)", &spin_degrees_per_second_, -360.0f, 360.0f);
        if (ImGui::Button("Reset")) reset();
        ImGui::SeparatorText("Camera");
        ImGui::DragFloat3("Position", &camera_.position.x, 0.1f);
        ImGui::SliderFloat("Field of view", &camera_.fov_y_degrees, 20.0f, 120.0f);
        ImGui::ColorEdit3("Background", &clear_color_.x);
        ImGui::End();
    }

private:
    void reset() {
        current_ = Transform{};
        current_.position = {0.0f, kRestHeight, 0.0f};
        previous_ = current_;
        vertical_speed_ = 0.0f;
    }

    static constexpr float kRestHeight = 0.5f;     // a unit cube resting on y = 0
    static constexpr float kArenaHalfSize = 6.0f;

    MeshHandle cube_mesh_;
    MeshHandle floor_mesh_;
    Transform previous_, current_;
    float vertical_speed_ = 0.0f;

    // Tunables (editable in the debug panel).
    float move_speed_ = 5.0f;   // metres per second
    float jump_speed_ = 7.0f;   // metres per second, upwards
    float gravity_ = 20.0f;     // metres per second squared; a bit above real for snappy jumps
    float spin_degrees_per_second_ = 45.0f;

    Camera camera_{.position = {0.0f, 7.0f, 11.0f}, .target = {0.0f, 0.0f, 0.0f}};
    Vec4 clear_color_{0.10f, 0.12f, 0.16f, 1.0f};
};

} // namespace

int main(int, char**) {
    Sandbox game;
    eng::App app({.window = {.title = "Sandbox", .width = 1280, .height = 720}});
    return app.run(game);
}
