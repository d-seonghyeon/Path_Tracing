cbuffer ToneMapCB : register(b0) {
    uint   g_toneMapFrameCount;
    uint3  _tmPad;
};

Texture2D<float4>         g_hdrInput  : register(t10);
RWTexture2D<unorm float4> g_ldrOutput : register(u1);

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

    float3 accum = g_hdrInput[id.xy].rgb;
    float3 hdr   = accum / max((float)(g_toneMapFrameCount + 1u), 1.0f);

    // ACES 톤맵핑 (Reinhard 대비 하이라이트 롤오프가 자연스럽고 색감 보존이 좋음)
    float3 ldr = ACESFilm(hdr);

    // 감마 보정
    ldr = pow(saturate(ldr), 1.0f / 2.2f);

    g_ldrOutput[id.xy] = float4(ldr, 1.0f);
}

