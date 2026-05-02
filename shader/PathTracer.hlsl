// -------------------------------------------------------
// PathTracer.hlsl — Phase 0: per-frame G-buffer output (NRD 준비)
//
// 주요 변경 (Phase 0):
//   1. g_accum 누적 모델 제거 — per-frame overwrite로 전환
//   2. TracePath → diffuse / specular 분리 반환 (TraceResult)
//   3. 7개 G-buffer UAV (u0~u6) 출력: diffuse, specular, viewZ,
//      normalRoughness, motionVector, baseColorMetalness, emissive
//   4. motionVector는 first-hit worldPos + prev/curr viewProj로 계산
// -------------------------------------------------------

cbuffer GlobalUB : register(b0) {
    float3 g_cameraPos;
    float  g_fov;
    float3 g_cameraFront;
    float  g_aspectRatio;   // 폴백용 (미사용)
    float3 g_cameraUp;
    float  g_frameCount;
    float3 g_cameraRight;
    uint   g_lightCount;
    row_major float4x4 g_prevViewProj;
    row_major float4x4 g_currViewProj;
};
#include "Utility.hlsli"
#include "BRDF.hlsli"
#include "NrdFrontend.hlsli"
#include "Scene.hlsli"

// -------------------------------------------------------
// G-buffer UAV (u0~u6) — AGENTS.md §4 Phase 0 목표 G-buffer
// -------------------------------------------------------
RWTexture2D<float4>       g_diffuseRadiance    : register(u0); // .rgb=diffuse, .a=hitT
RWTexture2D<float4>       g_specularRadiance   : register(u1); // .rgb=specular, .a=hitT
RWTexture2D<float>        g_viewZ              : register(u2); // linear view-space Z (양수, 전방)
RWTexture2D<float4>       g_normalRoughness    : register(u3); // .rg=octa-packed N, .b=roughness
RWTexture2D<float2>       g_motionVector       : register(u4); // 픽셀 단위 (prev-curr)
RWTexture2D<unorm float4> g_baseColorMetalness : register(u5); // .rgb=albedo, .a=metalness
RWTexture2D<float4>       g_emissive           : register(u6); // .rgb=emissive

static const int   MAX_BOUNCES       = 6;
static const int   SAMPLES_PER_PIXEL = 1;
static const float4 REBLUR_HIT_DIST_PARAMS = float4(30.0f, 0.1f, 20.0f, -25.0f);
static const float NRD_HIT_T_MAX     = 1e16f; // 하늘/미스 센티널

// -------------------------------------------------------
// TracePath 반환 구조체
// -------------------------------------------------------
struct TraceResult {
    float3 diffuse;     // diffuse 방사휘도 (per-frame)
    float3 specular;    // specular 방사휘도 (per-frame)
    float  hitT;        // 첫 번째 히트 거리 (NRD .a 채널)
    // 첫 번째 히트의 G-buffer 데이터
    float3 normal;
    float  roughness;
    float3 albedo;
    float  metalness;
    float3 emissive;
    float  viewZ;       // linear view-space Z (양수)
    bool   hitSurface;  // 카메라 레이가 표면에 맞았는지
    float3 worldPos;    // 첫 번째 히트 지점 (motion vector 계산용)
};

float RadianceWeight(float3 radiance) {
    return max(0.0f, dot(radiance, float3(0.2126f, 0.7152f, 0.0722f)));
}

void UpdateRepresentativeHitDistance(
    inout float representativeHitDist,
    inout float representativeWeight,
    float3 radiance,
    float candidateHitDist)
{
    float weight = RadianceWeight(radiance);
    if (weight > representativeWeight) {
        representativeWeight = weight;
        representativeHitDist = candidateHitDist;
    }
}

// -------------------------------------------------------
// 법선 옥타헤드럴 인코딩 (NRD 권장)
// 단위 구 벡터 → [0,1]^2 2D UV
// -------------------------------------------------------
float2 ClipToPixel(float4 clipPos, uint2 screenSize) {
    float2 ndc = clipPos.xy / clipPos.w;
    float2 uv  = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    return uv * float2(screenSize);
}

// -------------------------------------------------------
// 카메라 레이 생성 (종횡비를 스크린 실제 크기에서 계산)
// -------------------------------------------------------
Ray GenerateCameraRay(uint2 pixelCoord, uint2 screenSize, uint frameCount) {
    float2 jitter = GetRandomSamples(pixelCoord, 0, frameCount) - 0.5f;
    float2 uv     = ((float2)pixelCoord + 0.5f + jitter) / (float2)screenSize;
    float2 ndc    = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float aspectRatio = (float)screenSize.x / (float)screenSize.y;
    float halfH = tan(g_fov * 0.5f);
    float halfW = halfH * aspectRatio;

    float3 dir = normalize(
        g_cameraFront +
        g_cameraRight * (ndc.x * halfW) +
        g_cameraUp    * (ndc.y * halfH));

    Ray ray;
    ray.origin    = g_cameraPos;
    ray.direction = dir;
    return ray;
}

// -------------------------------------------------------
// 경로 추적 — diffuse/specular 분리 반환
//
// 분리 전략:
//   - 첫 히트 NEE: lobe.pDiff → diffuse, lobe.pSpec → specular
//   - 첫 간접 바운스의 로브 타입으로 이후 경로 채널 결정
//     (diffuse 바운스로 시작 → 이후 기여 모두 diffuse, 반대 동일)
// -------------------------------------------------------
TraceResult TracePath(Ray ray, uint2 pixelCoord, uint frameCount, out float diffuseHitDist, out float specularHitDist) {
    TraceResult result;
    diffuseHitDist = 0.0f;
    specularHitDist = 0.0f;
    result.diffuse    = float3(0, 0, 0);
    result.specular   = float3(0, 0, 0);
    result.hitT       = NRD_HIT_T_MAX;
    result.normal     = float3(0, 1, 0);
    result.roughness  = 1.0f;
    result.albedo     = float3(0, 0, 0);
    result.metalness  = 0.0f;
    result.emissive   = float3(0, 0, 0);
    result.viewZ      = NRD_HIT_T_MAX;
    result.hitSurface = false;
    result.worldPos   = float3(0, 0, 0);

    float3 throughput    = float3(1, 1, 1);
    float  prevBrdfPdf   = 0.0f;
    bool   prevSpecular  = true;
    bool   pathTypeSet   = false;   // 첫 간접 바운스 로브가 결정됐는지
    bool   pathIsSpecular = false;  // true = specular 경로, false = diffuse 경로
    float  diffuseFirstHitDist = 0.0f;
    float  specularFirstHitDist = 0.0f;
    float  diffuseHitWeight = 0.0f;
    float  specularHitWeight = 0.0f;

    for (int bounce = 0; bounce < MAX_BOUNCES; ++bounce) {
        SurfaceHit hit;
        if (!SceneIntersect(ray, hit)) {
            // 하늘 히트 — MIS 없음
            float3 sky = GetSkyColor(ray.direction) * throughput;
            if (any(sky > 0.0f)) {
                if (bounce == 0 || !pathTypeSet || !pathIsSpecular) {
                    result.diffuse  += sky;
                    if (bounce > 0)
                        UpdateRepresentativeHitDistance(diffuseHitDist, diffuseHitWeight, sky, NRD_HIT_T_MAX);
                }
                else {
                    result.specular += sky;
                    float candidateSpecHitDist = specularFirstHitDist > 0.0f ? specularFirstHitDist : NRD_HIT_T_MAX;
                    UpdateRepresentativeHitDistance(specularHitDist, specularHitWeight, sky, candidateSpecHitDist);
                }
            }
            break;
        }

        if (bounce > 0) {
            if (pathTypeSet && pathIsSpecular) {
                if (specularFirstHitDist == 0.0f)
                    specularFirstHitDist = hit.t;
            } else {
                if (diffuseFirstHitDist == 0.0f)
                    diffuseFirstHitDist = hit.t;
            }
        }

        // -------------------------------------------------------
        // bounce 0: G-buffer 수집
        // -------------------------------------------------------
        if (bounce == 0) {
            result.hitT       = hit.t;
            result.normal     = hit.normal;
            result.roughness  = hit.material.roughness;
            result.albedo     = hit.material.albedo;
            result.metalness  = hit.material.metallic;
            result.emissive   = hit.material.emissive;
            result.worldPos   = hit.p;
            // viewZ: 카메라 전방으로의 선형 투영 (RH, 양수 = 전방)
            result.viewZ      = dot(hit.p - g_cameraPos, g_cameraFront);
            result.hitSurface = true;
        }

        // -------------------------------------------------------
        // Emitter 히트
        // -------------------------------------------------------
        if (IsEmitter(hit.material)) {
            if (bounce == 0) {
                // bounce=0 직접 emitter 히트: result.emissive에 이미 저장됨 (→ g_emissive → Composite).
                // NRD diffuse 채널에 넣으면 dynamic range가 18:1 이상이 되어
                // REBLUR anti-lag이 indirect diffuse를 0으로 collapse함 → 검정 버그.
                // 따라서 여기서는 diffuse에 추가하지 않음.
            } else {
                int hitLightIdx = FindHitLight(hit.p, hit.normal);
                float w = 1.0f;
                if (hitLightIdx >= 0) {
                    float lightPdf = ComputeLightPdf(ray.origin, hitLightIdx);
                    w = (prevSpecular || lightPdf <= 0.0f)
                        ? 1.0f
                        : PowerHeuristic(prevBrdfPdf, lightPdf);
                }
                float3 emitContrib = hit.material.emissive * throughput * w;
                if (pathTypeSet && pathIsSpecular) {
                    result.specular += emitContrib;
                    float candidateSpecHitDist = specularFirstHitDist > 0.0f ? specularFirstHitDist : hit.t;
                    UpdateRepresentativeHitDistance(specularHitDist, specularHitWeight, emitContrib, candidateSpecHitDist);
                }
                else {
                    result.diffuse  += emitContrib;
                    UpdateRepresentativeHitDistance(diffuseHitDist, diffuseHitWeight, emitContrib, diffuseFirstHitDist > 0.0f ? diffuseFirstHitDist : hit.t);
                }
            }
            break;
        }

        float3 N = hit.normal;
        float3 V = -ray.direction;
        LobeWeights lobe = ComputeLobeWeights(N, V, hit.material.albedo, hit.material.metallic);

        // -------------------------------------------------------
        // NEE (Next Event Estimation) + MIS
        // -------------------------------------------------------
        for (int li = 0; li < (int)g_lightCount; ++li) {
            float2 xiNEE = GetRandomSamples(
                pixelCoord, (uint)(bounce * 10 + li + 50), frameCount);

            float  lightPdf;
            float  lightHitDist;
            float3 neeRaw = SampleDirectLight(
                hit.p, N, V, hit.material, xiNEE, li, lightPdf, lightHitDist);

            if (lightPdf > 0.0f && any(neeRaw > 0.0f)) {
                // MIS 가중치 계산 (구형 광원 기준으로 방향 복원)
                ShaderLight sl    = g_lights[li];
                float3 toCenter   = sl.p0 - hit.p;
                float  d2         = dot(toCenter, toCenter);
                float  d          = sqrt(d2);
                float3 wc         = toCenter / d;
                float  sinTMax2   = sl.radius * sl.radius / d2;
                float  cosTMax    = sqrt(max(0.0f, 1.0f - sinTMax2));
                float  cosT       = 1.0f - xiNEE.y * (1.0f - cosTMax);
                float  sinT       = sqrt(max(0.0f, 1.0f - cosT * cosT));
                float  phi        = 2.0f * PI * xiNEE.x;
                float3 up         = abs(wc.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
                float3 T          = normalize(cross(up, wc));
                float3 B          = cross(wc, T);
                float3 Lnee       = normalize(T*(cos(phi)*sinT) + B*(sin(phi)*sinT) + wc*cosT);

                float  brdfPdf    = ComputeCombinedPDF(N, V, Lnee, hit.material.roughness, lobe);
                float  w          = PowerHeuristic(lightPdf, brdfPdf);
                float3 neeContrib = clamp(neeRaw * w * throughput, 0.0f, 50.0f);

                if (bounce == 0) {
                    float3 diffuseContrib  = neeContrib * lobe.pDiff;
                    float3 specularContrib = neeContrib * lobe.pSpec;
                    result.diffuse  += diffuseContrib;
                    result.specular += specularContrib;
                    UpdateRepresentativeHitDistance(diffuseHitDist,  diffuseHitWeight,  diffuseContrib,  lightHitDist);
                    UpdateRepresentativeHitDistance(specularHitDist, specularHitWeight, specularContrib, lightHitDist);
                } else {
                    if (pathTypeSet && pathIsSpecular) {
                        result.specular += neeContrib;
                        float candidateSpecHitDist = specularFirstHitDist > 0.0f ? specularFirstHitDist : lightHitDist;
                        UpdateRepresentativeHitDistance(specularHitDist, specularHitWeight, neeContrib, candidateSpecHitDist);
                    }
                    else {
                        result.diffuse  += neeContrib;
                        float candidateDiffHitDist = diffuseFirstHitDist > 0.0f ? diffuseFirstHitDist : lightHitDist;
                        UpdateRepresentativeHitDistance(diffuseHitDist, diffuseHitWeight, neeContrib, candidateDiffHitDist);
                    }
                }
            }
        }

        // -------------------------------------------------------
        // 간접광: Lobe Selection
        // -------------------------------------------------------
        float  xiLobe = GetRandomFloat(pixelCoord, (uint)bounce + 200u, frameCount);
        float3 L;
        bool   sampledSpecular;

        if (xiLobe < lobe.pSpec) {
            float2 xiSpec = GetRandomSamples(pixelCoord, (uint)bounce, frameCount);
            float3 H = ImportanceSampleGGX(xiSpec, N, hit.material.roughness);
            L = reflect(-V, H);
            sampledSpecular = true;
        } else {
            float2 xiDiff = GetRandomSamples(pixelCoord, (uint)bounce + 500u, frameCount);
            L = SampleCosineHemisphere(xiDiff, N);
            sampledSpecular = false;
        }

        float NdotL = dot(N, L);
        if (NdotL <= 0.0f) break;

        BRDFResult brdfRes = EvaluateBRDF(N, V, L,
            hit.material.albedo, hit.material.roughness, hit.material.metallic);
        if (all(brdfRes.value <= 0.0f)) break;

        float combinedPdf = ComputeCombinedPDF(N, V, L, hit.material.roughness, lobe);
        if (combinedPdf <= 0.0f) break;

        throughput *= brdfRes.value / combinedPdf;
        throughput  = min(throughput, float3(10.0f, 10.0f, 10.0f));

        prevBrdfPdf  = combinedPdf;
        prevSpecular = sampledSpecular && (hit.material.roughness < 0.05f);

        // 첫 간접 바운스에서 경로 채널 결정
        if (!pathTypeSet) {
            pathIsSpecular = sampledSpecular;
            pathTypeSet    = true;
        }

        // -------------------------------------------------------
        // Russian Roulette (bounce >= 1)
        // -------------------------------------------------------
        if (bounce >= 1) {
            float pSurvive = clamp(
                max(throughput.r, max(throughput.g, throughput.b)),
                0.05f, 0.95f);
            if (GetRandomFloat(pixelCoord, (uint)bounce + 100u, frameCount) > pSurvive)
                break;
            throughput /= pSurvive;
        }

        ray.origin    = hit.p + N * 0.001f;
        ray.direction = L;
    }

    return result;
}

// -------------------------------------------------------
// 엔트리 포인트 — per-frame overwrite (누적 없음)
// -------------------------------------------------------
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    uint  screenW, screenH;
    g_diffuseRadiance.GetDimensions(screenW, screenH);
    if (pixelCoord.x >= screenW || pixelCoord.y >= screenH) return;

    uint frameCount = (uint)g_frameCount;

    float3 diffuse  = float3(0, 0, 0);
    float3 specular = float3(0, 0, 0);

    TraceResult res;
    float resDiffuseHitDist = 0.0f;
    float resSpecularHitDist = 0.0f;
    res.diffuse    = float3(0, 0, 0);
    res.specular   = float3(0, 0, 0);
    res.hitT       = NRD_HIT_T_MAX;
    res.normal     = float3(0, 1, 0);
    res.roughness  = 1.0f;
    res.albedo     = float3(0, 0, 0);
    res.metalness  = 0.0f;
    res.emissive   = float3(0, 0, 0);
    res.viewZ      = NRD_HIT_T_MAX;
    res.hitSurface = false;

    for (int s = 0; s < SAMPLES_PER_PIXEL; ++s) {
        uint sampleSeed = frameCount * (uint)SAMPLES_PER_PIXEL + (uint)s;
        Ray  ray        = GenerateCameraRay(pixelCoord, uint2(screenW, screenH), sampleSeed);
        float traceDiffuseHitDist = 0.0f;
        float traceSpecularHitDist = 0.0f;
        TraceResult r   = TracePath(ray, pixelCoord, sampleSeed, traceDiffuseHitDist, traceSpecularHitDist);
        res = r;
        resDiffuseHitDist = traceDiffuseHitDist;
        resSpecularHitDist = traceSpecularHitDist;

        if (!any(isnan(r.diffuse))  && !any(isinf(r.diffuse)))
            diffuse  += clamp(r.diffuse,  0.0f, 10.0f);
        if (!any(isnan(r.specular)) && !any(isinf(r.specular)))
            specular += clamp(r.specular, 0.0f, 10.0f);

        if (s == 0) res = r; // G-buffer는 첫 번째 샘플 기준
    }
    diffuse  /= (float)SAMPLES_PER_PIXEL;
    specular /= (float)SAMPLES_PER_PIXEL;

    // -------------------------------------------------------
    // G-buffer 출력 (per-frame overwrite)
    // -------------------------------------------------------
    float diffuseHitDist = res.hitSurface ? resDiffuseHitDist : 0.0f;
    float specularHitDist = res.hitSurface ? resSpecularHitDist : 0.0f;
    float diffuseNormHitDist = NrdReblurGetNormHitDist(diffuseHitDist, res.viewZ, REBLUR_HIT_DIST_PARAMS, 1.0f);
    float specularNormHitDist = NrdReblurGetNormHitDist(specularHitDist, res.viewZ, REBLUR_HIT_DIST_PARAMS, res.roughness);

    g_diffuseRadiance[pixelCoord]  = NrdPackReblurRadianceAndNormHitDist(diffuse, diffuseNormHitDist);
    g_specularRadiance[pixelCoord] = NrdPackReblurRadianceAndNormHitDist(specular, specularNormHitDist);

    g_viewZ[pixelCoord] = res.hitSurface ? res.viewZ : NRD_HIT_T_MAX;

    g_normalRoughness[pixelCoord] = NrdPackNormalAndRoughness(res.normal, res.roughness);

    float2 motionVector = float2(0.0f, 0.0f);
    if (res.hitSurface) {
        float4 currClip = mul(g_currViewProj, float4(res.worldPos, 1.0f));
        float4 prevClip = mul(g_prevViewProj, float4(res.worldPos, 1.0f));
        if (currClip.w > 1e-6f && prevClip.w > 1e-6f) {
            float2 currPixel = ClipToPixel(currClip, uint2(screenW, screenH));
            float2 prevPixel = ClipToPixel(prevClip, uint2(screenW, screenH));
            motionVector = prevPixel - currPixel;
        }
    }
    g_motionVector[pixelCoord] = motionVector;

    g_baseColorMetalness[pixelCoord] = float4(res.albedo, res.metalness);
    g_emissive[pixelCoord]           = float4(res.emissive, 1.0f);
}
