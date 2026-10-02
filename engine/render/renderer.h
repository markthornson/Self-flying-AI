#pragma once

// The renderer: the only code in the engine that talks to the GPU.
//
// Game code never calls SDL_GPU. Each frame it describes what it wants drawn:
//
//     renderer.begin_frame(camera, clear_color);
//     renderer.draw(cube_mesh, model_matrix);   // as many as you like
//     renderer.end_frame(&overlay);             // records and submits GPU work
//
// draw() only appends to a list; all the real work happens in end_frame(),
// where the list can be sorted to cut down on GPU state changes. Keeping the
// GPU behind this small interface is what lets the renderer grow (materials,
// shadows, batching) without game code changing.
//
// SDL_GPU itself is a thin layer over Vulkan, Metal and Direct3D 12. It gives
// us the modern GPU model, without writing three backends:
//   * a *device* (the GPU),
//   * *buffers* and *textures* in GPU memory,
//   * a *graphics pipeline* (shaders + fixed-function state, baked together),
//   * *command buffers* we record work into and then submit,
//   * *render passes* that draw into a set of target textures.

#include "engine/core/math/math.h"
#include "engine/render/mesh.h"

#include <SDL3/SDL_gpu.h>

#include <vector>

namespace eng {

class Window;

struct Camera {
    Vec3 position{0.0f, 2.0f, 5.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fov_y_degrees = 60.0f;
    float near_z = 0.1f;
    float far_z = 200.0f;
};

// Anything drawn on top of the 3D scene after it, with no depth test (the
// debug UI). The renderer calls prepare() before any render pass, so the
// overlay can upload its vertex data, then draw() inside its own pass.
class Overlay {
public:
    virtual ~Overlay() = default;
    virtual void prepare(SDL_GPUCommandBuffer* cmd) = 0;
    virtual void draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) = 0;
};

class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool valid() const { return pipeline_ != nullptr; }

    // Copies a mesh into GPU memory. Meshes live until the renderer is destroyed.
    MeshHandle create_mesh(const MeshData& data);

    void begin_frame(const Camera& camera, Vec4 clear_color);
    void draw(MeshHandle mesh, const Mat4& model, Vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});
    void end_frame(Overlay* overlay = nullptr);

    // Width / height of the last frame, for anything that needs the aspect ratio.
    float aspect_ratio() const { return aspect_; }
    const char* backend_name() const;
    size_t draws_last_frame() const { return draws_last_frame_; }

    // For the ImGui backend, which needs to create its own pipeline.
    SDL_GPUDevice* device() const { return device_; }
    SDL_GPUTextureFormat color_format() const;

private:
    struct GpuMesh {
        SDL_GPUBuffer* vertex_buffer = nullptr;
        SDL_GPUBuffer* index_buffer = nullptr;
        std::uint32_t index_count = 0;
    };
    struct DrawItem {
        MeshHandle mesh;
        Mat4 model;
        Vec4 tint;
    };

    bool create_pipeline();
    void ensure_depth_texture(std::uint32_t width, std::uint32_t height);

    Window& window_;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUTexture* depth_texture_ = nullptr;
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    std::uint32_t depth_width_ = 0, depth_height_ = 0;

    std::vector<GpuMesh> meshes_;
    std::vector<DrawItem> draw_list_;
    Camera camera_;
    Vec4 clear_color_{};
    float aspect_ = 16.0f / 9.0f;
    size_t draws_last_frame_ = 0;
};

} // namespace eng
