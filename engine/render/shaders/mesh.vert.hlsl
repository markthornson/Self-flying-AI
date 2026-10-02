// Vertex shader for lit, vertex-coloured meshes.
//
// Runs once per vertex. Moves the vertex from model space to clip space with
// the combined model-view-projection matrix, and passes the normal (turned to
// face the right way in the world) and colour on to the pixel shader.
//
// Compiled offline by tools/compile_shaders.py; the engine embeds the output.

// SDL_GPU's binding rule for SPIR-V: vertex-stage uniform buffers live in
// descriptor set 1, which HLSL spells "space1".
cbuffer DrawUniforms : register(b0, space1)
{
    float4x4 u_mvp;   // model -> clip space
    float4x4 u_model; // model -> world space, used for normals
    float4   u_tint;  // multiplies the vertex colour
};

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
    [[vk::location(2)]] float3 color    : COLOR;
};

struct VSOutput
{
    float4 clip_position : SV_Position;
    [[vk::location(0)]] float3 world_normal : NORMAL;
    [[vk::location(1)]] float3 color        : COLOR;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.clip_position = mul(u_mvp, float4(input.position, 1.0));
    // Rotating a normal uses only the upper 3x3 of the model matrix. This is
    // exact for rotation and uniform scale; non-uniform scale needs the
    // inverse-transpose, which arrives with proper materials in phase 4.
    output.world_normal = mul((float3x3)u_model, input.normal);
    output.color = input.color * u_tint.rgb;
    return output;
}
