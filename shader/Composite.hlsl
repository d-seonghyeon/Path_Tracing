// -------------------------------------------------------
// Composite.hlsl - composite denoised diffuse/specular radiance
// into the final HDR output.
// -------------------------------------------------------

#include "NrdFrontend.hlsli"

Texture2D<float4>       g_diffuseRadiance    : register(t0); // .rgb=YCoCg radiance, .a=normHitDist
Texture2D<float4>       g_specularRadiance   : register(t1); // .rgb=YCoCg radiance, .a=normHitDist
Texture2D<unorm float4> g_baseColorMetalness : register(t2); // unused (kept for slot continuity)
Texture2D<float4>       g_emissive           : register(t3); // .rgb=emissive
Texture2D<float4>       g_normalRoughness    : register(t4); // unused (kept for slot continuity)

RWTexture2D<float4>     g_compositeOutput    : register(u0); // HDR composite result

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    g_compositeOutput.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;

    float3 diffuse  = NrdYCoCgToLinear(g_diffuseRadiance[id.xy].rgb);
    float3 specular = NrdYCoCgToLinear(g_specularRadiance[id.xy].rgb);
    float3 emissive = g_emissive[id.xy].rgb;

    // DEBUG: show raw diffuse YCoCg Y (luminance) × 20 as green channel
    // to diagnose whether diffuse buffer has any signal.
    // Remove this block once confirmed.
    float diffuseY = g_diffuseRadiance[id.xy].r; // Y channel of YCoCg
    float specularY = g_specularRadiance[id.xy].r;
    g_compositeOutput[id.xy] = float4(diffuseY * 20.0f, specularY * 20.0f, emissive.r, 1.0f);
}
