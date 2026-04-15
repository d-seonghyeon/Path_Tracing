#ifndef __UTILITY_HLSLI__
#define __UTILITY_HLSLI__

// -------------------------------------------------------
// 수학 상수
// -------------------------------------------------------
#ifndef PI
#define PI 3.14159265359f
#endif

// -------------------------------------------------------
// 고급 난수 생성 (Golden Ratio + PCG Hash)
// -------------------------------------------------------
uint PCGHash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

static const float GOLDEN_RATIO = 1.61803398875f;

float UintToFloat01(uint h) {
    return (float)(h & 0x00FFFFFFu) / (float)0x01000000u;
}

float GetRandomFloat(uint2 pixelCoord, uint bounce, uint frameCount) {
    uint baseSeed = pixelCoord.x * 1973u + pixelCoord.y * 9277u + bounce * 26699u;
    float h = UintToFloat01(PCGHash(baseSeed));
    return frac(h + (float)frameCount * GOLDEN_RATIO);
}

float2 GetRandomSamples(uint2 pixelCoord, uint bounce, uint frameCount) {
    uint baseSeed = pixelCoord.x * 1973u + pixelCoord.y * 9277u + bounce * 26699u;
    float2 h2 = float2(
        UintToFloat01(PCGHash(baseSeed)),
        UintToFloat01(PCGHash(baseSeed + 31337u))
    );
    const float2 alpha = float2(0.75487766f, 0.56984029f);
    return frac(h2 + (float)frameCount * alpha);
}

// -------------------------------------------------------
// 스카이 컬러 (야간 하늘 + 달)
// -------------------------------------------------------
float3 GetSkyColor(float3 direction) {
    float t = clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
    float3 horizon = float3(0.02f, 0.02f, 0.05f);
    float3 zenith  = float3(0.005f, 0.005f, 0.02f);
    float3 sky     = lerp(horizon, zenith, t);

    float3 moonDir = normalize(float3(-0.3f, 0.7f, 0.2f));
    float  moonDot = dot(normalize(direction), moonDir);
    if (moonDot > 0.9998f) {
        sky += float3(1.5f, 1.4f, 1.2f);
    } else if (moonDot > 0.990f) {
        float g = (moonDot - 0.990f) / (0.9998f - 0.990f);
        sky += lerp(float3(0,0,0), float3(0.04f, 0.04f, 0.03f), g);
    }
    return sky;
}

#endif