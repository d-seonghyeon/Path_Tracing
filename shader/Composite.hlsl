// -------------------------------------------------------
// Composite.hlsl — Phase 0: diffuse*albedo + specular + emissive 합성
// NRD 디노이즈 후 이 패스에서 최종 HDR 합성.
// 현재는 디노이즈 없이 PathTracer 직출력을 합성 (Phase 2 이후 변경 예정).
// -------------------------------------------------------

Texture2D<float4>       g_diffuseRadiance    : register(t0); // .rgb=diffuse, .a=hitT
Texture2D<float4>       g_specularRadiance   : register(t1); // .rgb=specular
Texture2D<unorm float4> g_baseColorMetalness : register(t2); // .rgb=albedo, .a=metalness
Texture2D<float4>       g_emissive           : register(t3); // .rgb=emissive

RWTexture2D<float4>     g_compositeOutput    : register(u0); // HDR 합성 결과

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    g_compositeOutput.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;

    float3 diffuse  = g_diffuseRadiance[id.xy].rgb;
    float3 specular = g_specularRadiance[id.xy].rgb;
    float3 albedo   = g_baseColorMetalness[id.xy].rgb;
    float3 emissive = g_emissive[id.xy].rgb;

    // diffuse * albedo + specular + emissive
    float3 composite = diffuse * albedo + specular + emissive;

    g_compositeOutput[id.xy] = float4(composite, 1.0f);
}
