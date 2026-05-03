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
    // 기존 유지 — 하지만 PCG를 2회 체이닝해서 시드 품질만 올림
    uint baseSeed = pixelCoord.x * 1973u + pixelCoord.y * 9277u + bounce * 26699u;
    uint s1 = PCGHash(baseSeed);
    uint s2 = PCGHash(s1);  // 2회차: 상관성 제거

    float2 h2 = float2(UintToFloat01(s1), UintToFloat01(s2));
    const float2 alpha = float2(0.75487766f, 0.56984029f);
    return frac(h2 + (float)frameCount * alpha);
}



#endif