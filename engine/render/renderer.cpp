#include "engine/render/renderer.h"

#include "engine/core/log.h"
#include "engine/core/profile.h"
#include "engine/platform/window.h"
#include "engine/render/shaders/compiled/mesh.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace eng {

namespace {

// Matches the DrawUniforms cbuffer in mesh.vert.hlsl, byte for byte.
struct DrawUniforms {
    Mat4 mvp;
    Mat4 model;
    Vec4 tint;
};

// Loads one embedded shader in whichever format this GPU backend wants.
SDL_GPUShader* create_shader(SDL_GPUDevice* device, SDL_GPUShaderStage stage,
                             const unsigned char* spirv, size_t spirv_size,
                             const char* msl, std::uint32_t uniform_buffers) {
    SDL_GPUShaderCreateInfo info{};
    info.stage = stage;
    info.num_uniform_buffers = uniform_buffers;

    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {         // Vulkan
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = spirv;
        info.code_size = spirv_size;
        info.entrypoint = "main";
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {    // Metal
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.code = reinterpret_cast<const Uint8*>(msl);
        info.code_size = std::strlen(msl);
        info.entrypoint = "main0"; // spirv-cross renames main, a reserved word in Metal
    } else {
        ENGINE_LOG_ERROR("This GPU backend wants a shader format we don't ship yet");
        return nullptr;
    }

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    if (!shader) ENGINE_LOG_ERROR("SDL_CreateGPUShader failed: %s", SDL_GetError());
    return shader;
}

} // namespace

Renderer::Renderer(Window& window) : window_(window) {
    // Ask for a device that accepts SPIR-V (Vulkan) or MSL (Metal). SDL picks
    // the best backend on this machine that takes one of those. Debug mode
    // turns on the API's validation layers, which explain mistakes in detail.
#ifdef NDEBUG
    const bool debug_mode = false;
#else
    const bool debug_mode = true;
#endif
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, debug_mode, nullptr);
    if (!device_ && debug_mode) {
        // Validation layers aren't always installed; carry on without them.
        device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    }
    if (!device_) {
        ENGINE_LOG_ERROR("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return;
    }
    if (!SDL_ClaimWindowForGPUDevice(device_, window_.sdl())) {
        ENGINE_LOG_ERROR("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return;
    }
    // VSYNC: wait for the display refresh, so no tearing and no wasted frames.
    SDL_SetGPUSwapchainParameters(device_, window_.sdl(), SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  SDL_GPU_PRESENTMODE_VSYNC);

    // Pick a depth buffer format. 32-bit float is the most precise; not every
    // GPU can render to it, so fall back to 24-bit.
    depth_format_ = SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                                 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                        ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
                        : SDL_GPU_TEXTUREFORMAT_D24_UNORM;

    if (create_pipeline()) ENGINE_LOG_INFO("Renderer ready on the %s backend", backend_name());
}

Renderer::~Renderer() {
    if (!device_) return;
    SDL_WaitForGPUIdle(device_); // never free memory the GPU may still be reading
    for (GpuMesh& m : meshes_) release_buffers(m);
    if (depth_texture_) SDL_ReleaseGPUTexture(device_, depth_texture_);
    if (pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    SDL_ReleaseWindowFromGPUDevice(device_, window_.sdl());
    SDL_DestroyGPUDevice(device_);
}

const char* Renderer::backend_name() const {
    return device_ ? SDL_GetGPUDeviceDriver(device_) : "none";
}

SDL_GPUTextureFormat Renderer::color_format() const {
    return SDL_GetGPUSwapchainTextureFormat(device_, window_.sdl());
}

// A graphics pipeline bakes together everything about *how* to draw:
// shaders, vertex layout, culling, depth testing and the target formats.
// Creating one is slow, so it happens once at startup, never per frame.
bool Renderer::create_pipeline() {
    using namespace shaders;
    SDL_GPUShader* vs = create_shader(device_, SDL_GPU_SHADERSTAGE_VERTEX, mesh_vert_spv, sizeof(mesh_vert_spv),
                                      mesh_vert_msl, 1);
    SDL_GPUShader* fs = create_shader(device_, SDL_GPU_SHADERSTAGE_FRAGMENT, mesh_frag_spv, sizeof(mesh_frag_spv),
                                      mesh_frag_msl, 0);
    if (!vs || !fs) return false;

    // How to read a Vertex from the vertex buffer: one buffer, three float3s.
    SDL_GPUVertexBufferDescription buffer_desc{};
    buffer_desc.slot = 0;
    buffer_desc.pitch = sizeof(Vertex);
    buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, position)};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, normal)};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, color)};

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = color_format();

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vs;
    info.fragment_shader = fs;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer_desc;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    // Skip triangles facing away from the camera: on a closed mesh you can
    // never see them, and it halves the work.
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    // Depth test: keep a pixel only if it is nearer than what's already there.
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = depth_format_;
    info.target_info.has_depth_stencil_target = true;

    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &info);
    if (!pipeline_) ENGINE_LOG_ERROR("SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());

    // The pipeline keeps what it needs; the shader objects can go.
    SDL_ReleaseGPUShader(device_, vs);
    SDL_ReleaseGPUShader(device_, fs);
    return pipeline_ != nullptr;
}

MeshHandle Renderer::create_mesh(const MeshData& data) {
    if (!device_) return {};
    GpuMesh uploaded;
    if (!upload(uploaded, data)) return {};

    // Reuse a slot freed by destroy_mesh() if there is one. Its generation
    // was bumped when it was freed, so old handles to it stay dead.
    std::uint32_t index;
    if (!free_meshes_.empty()) {
        index = free_meshes_.back();
        free_meshes_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(meshes_.size());
        meshes_.emplace_back();
    }
    GpuMesh& slot = meshes_[index];
    uploaded.generation = slot.generation;
    uploaded.alive = true;
    slot = uploaded;
    ++mesh_count_;
    return MeshHandle{index, slot.generation};
}

bool Renderer::update_mesh(MeshHandle handle, const MeshData& data) {
    GpuMesh* mesh = find(handle);
    if (!mesh) return false;
    GpuMesh replacement;
    if (!upload(replacement, data)) return false;
    // The old buffers may still be in use by a frame the GPU hasn't finished.
    // That's fine: SDL only frees a released buffer once the GPU is done
    // with it, so we can let go of them straight away.
    release_buffers(*mesh);
    mesh->vertex_buffer = replacement.vertex_buffer;
    mesh->index_buffer = replacement.index_buffer;
    mesh->index_count = replacement.index_count;
    return true;
}

void Renderer::destroy_mesh(MeshHandle handle) {
    GpuMesh* mesh = find(handle);
    if (!mesh) return;
    release_buffers(*mesh);
    mesh->alive = false;
    ++mesh->generation;
    free_meshes_.push_back(handle.index);
    --mesh_count_;
}

Renderer::GpuMesh* Renderer::find(MeshHandle handle) {
    if (!handle.valid() || handle.index >= meshes_.size()) return nullptr;
    GpuMesh& mesh = meshes_[handle.index];
    return mesh.alive && mesh.generation == handle.generation ? &mesh : nullptr;
}

void Renderer::release_buffers(GpuMesh& mesh) {
    if (mesh.vertex_buffer) SDL_ReleaseGPUBuffer(device_, mesh.vertex_buffer);
    if (mesh.index_buffer) SDL_ReleaseGPUBuffer(device_, mesh.index_buffer);
    mesh.vertex_buffer = nullptr;
    mesh.index_buffer = nullptr;
    mesh.index_count = 0;
}

bool Renderer::upload(GpuMesh& mesh, const MeshData& data) {
    if (data.vertices.empty() || data.indices.empty()) return false;

    const auto vertex_bytes = static_cast<std::uint32_t>(data.vertices.size() * sizeof(Vertex));
    const auto index_bytes = static_cast<std::uint32_t>(data.indices.size() * sizeof(std::uint32_t));

    mesh.index_count = static_cast<std::uint32_t>(data.indices.size());
    SDL_GPUBufferCreateInfo vb_info{SDL_GPU_BUFFERUSAGE_VERTEX, vertex_bytes, 0};
    SDL_GPUBufferCreateInfo ib_info{SDL_GPU_BUFFERUSAGE_INDEX, index_bytes, 0};
    mesh.vertex_buffer = SDL_CreateGPUBuffer(device_, &vb_info);
    mesh.index_buffer = SDL_CreateGPUBuffer(device_, &ib_info);

    // GPU-only memory can't be written by the CPU directly. The usual route:
    // write into a CPU-visible "transfer buffer", then record a copy pass that
    // has the GPU copy from there into the real buffers.
    SDL_GPUTransferBufferCreateInfo tb_info{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, vertex_bytes + index_bytes, 0};
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tb_info);
    auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(device_, transfer, false));
    std::memcpy(mapped, data.vertices.data(), vertex_bytes);
    std::memcpy(mapped + vertex_bytes, data.indices.data(), index_bytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{transfer, 0};
    SDL_GPUBufferRegion dst{mesh.vertex_buffer, 0, vertex_bytes};
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    src.offset = vertex_bytes;
    dst = {mesh.index_buffer, 0, index_bytes};
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer); // SDL frees it once the copy has run
    return true;
}

void Renderer::ensure_depth_texture(std::uint32_t width, std::uint32_t height) {
    if (depth_texture_ && width == depth_width_ && height == depth_height_) return;
    if (depth_texture_) SDL_ReleaseGPUTexture(device_, depth_texture_);

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = depth_format_;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    depth_texture_ = SDL_CreateGPUTexture(device_, &info);
    depth_width_ = width;
    depth_height_ = height;
}

void Renderer::begin_frame(const Camera& camera, Vec4 clear_color) {
    camera_ = camera;
    clear_color_ = clear_color;
    draw_list_.clear();
}

void Renderer::draw(MeshHandle mesh, const Mat4& model, Vec4 tint) {
    if (find(mesh)) draw_list_.push_back({mesh, model, tint});
}

void Renderer::end_frame(Overlay* overlay) {
    PROFILE_SCOPE("Renderer::end_frame");
    if (!valid()) return;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUTexture* swapchain = nullptr;
    std::uint32_t width = 0, height = 0;
    // Blocks until the display has a free image for us (this is where VSYNC waits).
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_.sdl(), &swapchain, &width, &height) || !swapchain) {
        // No image, e.g. the window is minimized. Nothing to draw into.
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }
    ensure_depth_texture(width, height);
    aspect_ = static_cast<float>(width) / static_cast<float>(height);

    // The overlay uploads its vertices now: copies can't happen inside a render pass.
    if (overlay) overlay->prepare(cmd);

    // Sort so draws of the same mesh sit together and we rebind buffers less.
    // (With one mesh this does nothing; it's the hook for batching later.)
    std::stable_sort(draw_list_.begin(), draw_list_.end(),
                     [](const DrawItem& a, const DrawItem& b) { return a.mesh.index < b.mesh.index; });

    const Mat4 view = mat4_look_at(camera_.position, camera_.target, camera_.up);
    const Mat4 projection = mat4_perspective(radians(camera_.fov_y_degrees), aspect_, camera_.near_z, camera_.far_z);
    const Mat4 view_projection = projection * view;

    // --- Pass 1: the 3D scene, into the swapchain image plus a depth buffer.
    SDL_GPUColorTargetInfo color{};
    color.texture = swapchain;
    color.clear_color = {clear_color_.x, clear_color_.y, clear_color_.z, clear_color_.w};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = depth_texture_;
    depth.clear_depth = 1.0f; // 1 = as far away as possible
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE; // nobody reads depth after this pass
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);
    std::uint32_t bound_mesh = UINT32_MAX;
    for (const DrawItem& item : draw_list_) {
        const GpuMesh& mesh = meshes_[item.mesh.index];
        if (item.mesh.index != bound_mesh) {
            SDL_GPUBufferBinding vb{mesh.vertex_buffer, 0};
            SDL_GPUBufferBinding ib{mesh.index_buffer, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            bound_mesh = item.mesh.index;
        }
        DrawUniforms uniforms{view_projection * item.model, item.model, item.tint};
        SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count, 1, 0, 0, 0);
    }
    SDL_EndGPURenderPass(pass);

    // --- Pass 2: the overlay, drawn over the scene without depth testing.
    if (overlay) {
        SDL_GPUColorTargetInfo ui{};
        ui.texture = swapchain;
        ui.load_op = SDL_GPU_LOADOP_LOAD; // keep the scene we just drew
        ui.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* ui_pass = SDL_BeginGPURenderPass(cmd, &ui, 1, nullptr);
        overlay->draw(cmd, ui_pass);
        SDL_EndGPURenderPass(ui_pass);
    }

    // Hand everything to the GPU; the swapchain image is shown when it finishes.
    SDL_SubmitGPUCommandBuffer(cmd);
    draws_last_frame_ = draw_list_.size();
}

} // namespace eng
