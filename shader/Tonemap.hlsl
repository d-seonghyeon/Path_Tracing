// Tonemap.hlsl — Phase 0: /(frameCount+1) 나눗셈 제거
// 입력은 Composite.hlsl 의 per-frame HDR 출력 (t10)

Texture2D<float4>         g_hdrInput  : register(t10);
RWTexture2D<unorm float4> g_ldrOutput : register(u1);

static const float TONE_MAP_EXPOSURE = 0.82f;

// ACES Filmic Tone Mapping
float3 ACESFilm(float3 x) {
    float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    g_ldrOutput.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;

    float3 hdr = g_hdrInput[id.xy].rgb * TONE_MAP_EXPOSURE; // Composite 출력 (per-frame, 나눗셈 불필요)

    float3 ldr = ACESFilm(hdr);
    ldr = pow(saturate(ldr), 1.0f / 2.2f);

    g_ldrOutput[id.xy] = float4(ldr, 1.0f);
}
