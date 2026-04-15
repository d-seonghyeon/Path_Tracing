// -------------------------------------------------------
// PathTracer.hlsl — 개선된 경로 추적 컴퓨트 셰이더
//
// 주요 변경:
//   1. 종횡비를 스크린 실제 크기에서 계산
//   2. Diffuse cosine-weighted 중요도 샘플링 추가
//   3. Lobe selection (specular/diffuse) + combined PDF
//   4. NEE 원뿔각 샘플링 + MIS (Power Heuristic)
//   5. 간접광이 광원 히트 시에도 MIS 적용
//   6. Russian Roulette 개선 (bounce >= 1)
//   7. 톤맵핑 제거 — 누적만 수행, ToneMapCS에서 처리
// -------------------------------------------------------

// cbuffer를 includes 앞에 선언 (Scene.hlsli에서 g_lightCount 참조)
cbuffer GlobalUB : register(b0) {
    float3 g_cameraPos;
    float  g_fov;
    float3 g_cameraFront;
    float  g_aspectRatio;   // C++ 폴백용 (사용하지 않음)
    float3 g_cameraUp;
    float  g_frameCount;
    float3 g_cameraRight;
    uint   g_lightCount;
};
#include "Utility.hlsli"
#include "BRDF.hlsli"
#include "Scene.hlsli"

RWTexture2D<float4> g_accum : register(u0);

static const int MAX_BOUNCES       = 6;
static const int SAMPLES_PER_PIXEL = 1;

// -------------------------------------------------------
// 카메라 레이 생성 (종횡비를 스크린 실제 크기에서 계산)
// -------------------------------------------------------
Ray GenerateCameraRay(uint2 pixelCoord, uint2 screenSize, uint frameCount) {
    float2 jitter = GetRandomSamples(pixelCoord, 0, frameCount) - 0.5f;
    float2 uv     = ((float2)pixelCoord + 0.5f + jitter) / (float2)screenSize;
    float2 ndc    = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    // [FIX] 스크린 실제 크기에서 종횡비를 계산하여 리사이즈/회전에 대응
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
// 경로 추적 (Lobe Selection + MIS + 개선된 RR)
// -------------------------------------------------------
float3 TracePath(Ray ray, uint2 pixelCoord, uint frameCount) {
    float3 totalRadiance = float3(0, 0, 0);
    float3 throughput    = float3(1, 1, 1);
    float  prevBrdfPdf   = 0.0f;   // 이전 바운스의 BRDF PDF (MIS용)
    bool   prevSpecular  = true;    // 이전 바운스가 specular인지 (MIS 판단용)

    for (int bounce = 0; bounce < MAX_BOUNCES; ++bounce) {
        SurfaceHit hit;
        if (!SceneIntersect(ray, hit)) {
            totalRadiance += GetSkyColor(ray.direction) * throughput;
            break;
        }

        // -------------------------------------------------------
        // Emitter 히트: MIS 간접광 경로
        // -------------------------------------------------------
        if (IsEmitter(hit.material)) {
            if (bounce == 0) {
                // 카메라에서 직접 본 광원 — MIS 불필요
                totalRadiance += hit.material.emissive * throughput;
            } else {
                // BRDF 샘플링으로 광원에 맞음 → MIS 가중치 적용
                // prevBrdfPdf : 이전 바운스에서 이 방향을 샘플링한 PDF
                // lightPdf    : 이 방향이 NEE에서 샘플링될 확률
                float3 prevHitP = ray.origin; // offset 포함된 이전 히트점
                int hitLightIdx = FindHitLight(hit.p, hit.normal);
                if (hitLightIdx >= 0) {
                    float lightPdf = ComputeLightPdf(prevHitP, hitLightIdx);
                    float w = (prevSpecular || lightPdf <= 0.0f)
                            ? 1.0f
                            : PowerHeuristic(prevBrdfPdf, lightPdf);
                    totalRadiance += hit.material.emissive * throughput * w;
                } else {
                    // 광원 목록에 없는 emissive (예: emissive 메시)
                    totalRadiance += hit.material.emissive * throughput;
                }
            }
            break;
        }

        float3 N = hit.normal;
        float3 V = -ray.direction;

        // -------------------------------------------------------
        // NEE (Next Event Estimation) — 원뿔 샘플링 + MIS
        // -------------------------------------------------------
        for (int li = 0; li < (int)g_lightCount; ++li) {
            float2 xiNEE = GetRandomSamples(
                pixelCoord, (uint)(bounce * 10 + li + 50), frameCount);

            float lightPdf;
            float3 neeRaw = SampleDirectLight(
                hit.p, N, V, hit.material, xiNEE, li, lightPdf);

            if (lightPdf > 0.0f && any(neeRaw > 0.0f)) {
                // BRDF 의 combined PDF (이 방향에 대해)
                // SampleDirectLight 내부에서 L을 복원해야 하므로 여기서 재계산
                // → 원뿔 방향을 다시 만들기보다, neeRaw에 이미 brdf*Le*NdotL/pdfLight가 들어있으므로
                //   MIS weight만 곱하면 됨
                LobeWeights lobe = ComputeLobeWeights(N, V, hit.material.albedo, hit.material.metallic);

                // NEE 방향 복원 (SampleDirectLight와 동일한 시드 → 동일한 방향)
                ShaderLight sl = g_lights[li];
                float3 toCenter = sl.p0 - hit.p;
                float  d2       = dot(toCenter, toCenter);
                float  d        = sqrt(d2);
                float3 wc       = toCenter / d;
                float  sinTMax2 = sl.radius * sl.radius / d2;
                float  cosTMax  = sqrt(max(0.0f, 1.0f - sinTMax2));
                float  cosT     = 1.0f - xiNEE.y * (1.0f - cosTMax);
                float  sinT     = sqrt(max(0.0f, 1.0f - cosT * cosT));
                float  phi      = 2.0f * PI * xiNEE.x;
                float3 up       = abs(wc.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
                float3 T        = normalize(cross(up, wc));
                float3 B        = cross(wc, T);
                float3 Lnee     = normalize(T*(cos(phi)*sinT) + B*(sin(phi)*sinT) + wc*cosT);

                float brdfPdf = ComputeCombinedPDF(N, V, Lnee, hit.material.roughness, lobe);
                float w       = PowerHeuristic(lightPdf, brdfPdf);

                totalRadiance += clamp(neeRaw * w * throughput, 0.0f, 50.0f);
            }
        }

        // -------------------------------------------------------
        // 간접광: Lobe Selection (Specular / Diffuse)
        // -------------------------------------------------------
        LobeWeights lobe = ComputeLobeWeights(N, V, hit.material.albedo, hit.material.metallic);

        // 로브 선택용 난수
        float xiLobe = GetRandomFloat(pixelCoord, (uint)bounce + 200u, frameCount);

        float3 L;
        bool   sampledSpecular;

        if (xiLobe < lobe.pSpec) {
            // === Specular Lobe: GGX 중요도 샘플링 ===
            float2 xiSpec = GetRandomSamples(pixelCoord, (uint)bounce, frameCount);
            float3 H = ImportanceSampleGGX(xiSpec, N, hit.material.roughness);
            L = reflect(-V, H);
            sampledSpecular = true;
        } else {
            // === Diffuse Lobe: Cosine-weighted 반구 샘플링 ===
            float2 xiDiff = GetRandomSamples(pixelCoord, (uint)bounce + 500u, frameCount);
            L = SampleCosineHemisphere(xiDiff, N);
            sampledSpecular = false;
        }

        float NdotL = dot(N, L);
        if (NdotL <= 0.0f) break;

        // 전체 BRDF 평가 (lobe selection과 무관하게 항상 full BRDF)
        BRDFResult brdfRes = EvaluateBRDF(N, V, L,
            hit.material.albedo, hit.material.roughness, hit.material.metallic);
        if (all(brdfRes.value <= 0.0f)) break;

        // Combined PDF = pSpec * pdfSpec + pDiff * pdfDiff
        float combinedPdf = ComputeCombinedPDF(N, V, L, hit.material.roughness, lobe);
        if (combinedPdf <= 0.0f) break;

        // Throughput 갱신
        throughput *= brdfRes.value / combinedPdf;
        throughput  = min(throughput, float3(10.0f, 10.0f, 10.0f));

        // 다음 바운스 MIS용 정보 저장
        prevBrdfPdf  = combinedPdf;
        prevSpecular = sampledSpecular && (hit.material.roughness < 0.05f);

        // -------------------------------------------------------
        // Russian Roulette (bounce >= 1부터 적용)
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

    return totalRadiance;
}

// -------------------------------------------------------
// 엔트리 포인트
// -------------------------------------------------------
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    uint screenW, screenH;
    g_accum.GetDimensions(screenW, screenH);
    if (pixelCoord.x >= screenW || pixelCoord.y >= screenH) return;

    uint frameCount = (uint)g_frameCount;

    float3 newSample = float3(0, 0, 0);
    for (int s = 0; s < SAMPLES_PER_PIXEL; ++s) {
        uint sampleSeed = frameCount * (uint)SAMPLES_PER_PIXEL + (uint)s;
        Ray  ray        = GenerateCameraRay(pixelCoord, uint2(screenW, screenH), sampleSeed);
        float3 s_val    = TracePath(ray, pixelCoord, sampleSeed);
        if (!any(isnan(s_val)) && !any(isinf(s_val)))
            newSample += clamp(s_val, 0.0f, 10.0f);
    }
    newSample /= (float)SAMPLES_PER_PIXEL;

    // 누적만 수행 — 톤맵핑은 ToneMapCS에서
    if (frameCount == 0u) {
        g_accum[pixelCoord] = float4(newSample, 1.0f);
    } else {
        g_accum[pixelCoord] += float4(newSample, 0.0f);
    }
}
