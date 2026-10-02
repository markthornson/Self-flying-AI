// Pixel shader for lit, vertex-coloured meshes.
//
// Runs once per covered pixel. Simple "Lambert" lighting: a surface is as
// bright as it faces the light (the dot product of its normal and the light
// direction), plus a little ambient light so the shadowed sides aren't black.

struct PSInput
{
    [[vk::location(0)]] float3 world_normal : NORMAL;
    [[vk::location(1)]] float3 color        : COLOR;
};

float4 main(PSInput input) : SV_Target0
{
    const float3 light_dir = normalize(float3(0.4, 1.0, 0.6)); // towards the light
    const float ambient = 0.25;

    float3 n = normalize(input.world_normal);
    float diffuse = saturate(dot(n, light_dir));
    float3 lit = input.color * (ambient + (1.0 - ambient) * diffuse);
    return float4(lit, 1.0);
}
