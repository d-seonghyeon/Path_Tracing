// -------------------------------------------------------
// Composite.hlsl - re-apply NRD material factors and composite
// demodulated diffuse/specular radiance back into the final HDR output.
// -------------------------------------------------------

#include "NrdFrontend.hlsli"

Texture2D<float4>       g_diffuseRadiance    : register(t0); // .rgb=YCoCg radiance, .a=normHitDist
Texture2D<float4>       g_specularRadiance   : register(t1); // .rgb=YCoCg radiance, .a=normHitDist
Texture2D<unorm float4> g_baseColorMetalness : register(t2); // .rgb=albedo, .a=metalness
Texture2D<float4>       g_emissive           : register(t3); // .rgb=emissive
Texture2D<float4>       g_normalRoughness    : register(t4); // .xy=oct normal, .z=roughness

RWTexture2D<float4>     g_compositeOutput    : register(u0); // HDR composite result

cbuffer GlobalUB : register(b0) {
    float3 g_cameraPos;
    float  g_fov;
    float3 g_cameraFront;
    float  g_aspectRatio;
    float3 g_cameraUp;
    float  g_frameCount;
    float3 g_cameraRight;
    uint   g_lightCount;
    row_major float4x4 g_prevViewProj;
    row_major float4x4 g_currViewProj;
};

float3 GenerateCameraViewDir(uint2 pixelCoord, uint2 screenSize)
{
    float2 uv = ((float2)pixelCoord + 0.5f) / (float2)screenSize;
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float halfH = tan(g_fov * 0.5f);
    float halfW = halfH * g_aspectRatio;

    return normalize(
        g_cameraFront +
        g_cameraRight * (ndc.x * halfW) +
        g_cameraUp    * (ndc.y * halfH));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    g_compositeOutput.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;

    float3 diffuse  = NrdYCoCgToLinear(g_diffuseRadiance[id.xy].rgb);
    float3 specular = NrdYCoCgToLinear(g_specularRadiance[id.xy].rgb);
    float4 normalRoughness = NrdUnpackNormalAndRoughness(g_normalRoughness[id.xy]);
    float3 normal = normalRoughness.xyz;
    float roughness = normalRoughness.w;
    float4 baseColorMetalness = g_baseColorMetalness[id.xy];
    float3 albedo = baseColorMetalness.rgb;
    float metalness = baseColorMetalness.a;
    float3 emissive = g_emissive[id.xy].rgb;
    float3 viewDir = -GenerateCameraViewDir(id.xy, uint2(w, h));

    float3 diffuseFactor;
    float3 specularFactor;
    NrdMaterialFactors(normal, viewDir, albedo, metalness, roughness, diffuseFactor, specularFactor);

    diffuse *= diffuseFactor;
    specular *= specularFactor;

    float3 composite = diffuse + specular + emissive;
    g_compositeOutput[id.xy] = float4(composite, 1.0f);
}
