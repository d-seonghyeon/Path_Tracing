// -------------------------------------------------------
// PathTracer.hlsl — 개선된 경로 추적 컴퓨트 셰이더
//
// 주요 기능:
//   1. 종횡비를 스크린 실제 크기에서 계산
//   2. Diffuse cosine-weighted 중요도 샘플링
//   3. Lobe selection (specular/diffuse) + combined PDF
//   4. NEE 원뿔각/면적 샘플링 + MIS (Power Heuristic)
//   5. 간접광이 광원 히트 시에도 MIS 적용
//   6. Russian Roulette (bounce >= 1)
//   7. 톤맵핑 분리 — 누적만 수행
//   8. 환경맵 NEE + CDF 중요도 샘플링 + MIS
// -------------------------------------------------------

// cbuffer를 includes 앞에 선언 (Scene.hlsli에서 g_lightCount 참조)
cbuffer GlobalUB : register(b0) {
    float3 g_cameraPos;
    float  g_fov;
    float3 g_cameraFront;
    float  g_aspectRatio;   // C++ 폴백용 (셰이더에서는 스크린 크기로 직접 계산)
    float3 g_cameraUp;
    float  g_frameCount;
    float3 g_cameraRight;
    uint   g_lightCount;
    // 환경맵 정보
    uint   g_envWidth;      // HDRI 텍스처 가로 해상도
    uint   g_envHeight;     // HDRI 텍스처 세로 해상도
    uint   g_hasEnvMap;     // 환경맵 로드 여부 (0 or 1)
    float  _pad;
};

#include "Utility.hlsli"
#include "BRDF.hlsli"
#include "Scene.hlsli"

RWTexture2D<float4> g_accum : register(u0);

static const int MAX_BOUNCES       = 6;
static const int SAMPLES_PER_PIXEL = 1;

// -------------------------------------------------------
// 카메라 레이 생성
//   종횡비를 스크린 실제 크기에서 계산하여 리사이즈/회전에 대응
// -------------------------------------------------------
Ray GenerateCameraRay(uint2 pixelCoord, uint2 screenSize, uint frameCount) {
    // 서브픽셀 지터 (안티앨리어싱)
    float2 jitter = GetRandomSamples(pixelCoord, 0, frameCount) - 0.5f;
    float2 uv     = ((float2)pixelCoord + 0.5f + jitter) / (float2)screenSize;
    float2 ndc    = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    // 스크린 실제 크기에서 종횡비 계산
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
// 경로 추적
//   Lobe Selection + MIS + 환경맵 NEE + Russian Roulette
// -------------------------------------------------------
float3 TracePath(Ray ray, uint2 pixelCoord, uint frameCount) {
    float3 totalRadiance = float3(0, 0, 0);
    float3 throughput    = float3(1, 1, 1);
    float  prevBrdfPdf   = 0.0f;    // 이전 바운스의 BRDF PDF (MIS용)
    bool   prevSpecular  = true;     // 이전 바운스가 specular인지 (MIS 판단용)

    for (int bounce = 0; bounce < MAX_BOUNCES; ++bounce) {
        SurfaceHit hit;

        // -------------------------------------------------------
        // miss: 씬에 아무것도 안 맞음 → 배경(환경맵 or 하늘)
        // -------------------------------------------------------
        if (!SceneIntersect(ray, hit)) {
            if (g_hasEnvMap != 0u) {
                // 환경맵 있음 → 환경맵 NEE가 존재하므로 MIS 적용
                float3 envColor = SampleEnvironmentMap(ray.direction);

                if (bounce == 0 || prevSpecular) {
                    // 카메라 직접 시선 or 거울 반사 → MIS 불필요 (가중치 1.0)
                    totalRadiance += envColor * throughput;
                } else {
                    // BRDF 샘플링으로 환경에 맞음 → 환경맵 NEE pdf와 비교
                    float envPdf = EnvMapPdf(ray.direction, g_envWidth, g_envHeight);
                    float w = (envPdf > 0.0f)
                        ? PowerHeuristic(prevBrdfPdf, envPdf)
                        : 1.0f;
                    totalRadiance += envColor * throughput * w;
                }
            } else {
                // 환경맵 없음 → NEE 없으므로 MIS 불필요, 전부 BRDF 기여
                totalRadiance += GetSkyColor(ray.direction) * throughput;
            }
            break;
        }

        // -------------------------------------------------------
        // Emitter 히트: 광원에 직접 맞음 → MIS 간접광 경로
        // -------------------------------------------------------
        if (IsEmitter(hit.material)) {
            if (bounce == 0) {
                // 카메라에서 직접 본 광원 → MIS 불필요
                totalRadiance += hit.material.emissive * throughput;
            } else {
                // BRDF 샘플링으로 광원에 맞음 → NEE pdf와 비교해서 MIS 가중치 적용
                float3 prevHitP = ray.origin; // offset 포함된 이전 히트점
                int hitLightIdx = FindHitLight(hit.p, hit.normal);
                if (hitLightIdx >= 0) {
                    float lightPdf = ComputeLightPdf(prevHitP, hitLightIdx);
                    float w = (prevSpecular || lightPdf <= 0.0f)
                            ? 1.0f  // 거울 반사이거나 NEE 확률 0 → 가중치 1.0
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
        // NEE (Next Event Estimation) — 면적/원뿔 광원 + MIS
        // -------------------------------------------------------
        for (int li = 0; li < (int)g_lightCount; ++li) {
            float2 xiNEE = GetRandomSamples(
                pixelCoord, (uint)(bounce * 10 + li + 50), frameCount);

            float lightPdf;
            float3 neeRaw = SampleDirectLight(
                hit.p, N, V, hit.material, xiNEE, li, lightPdf);

            if (lightPdf > 0.0f && any(neeRaw > 0.0f)) {
                // BRDF combined PDF 계산 (MIS 가중치용)
                LobeWeights lobe = ComputeLobeWeights(N, V, hit.material.albedo, hit.material.metallic);

                // NEE 샘플링 방향 복원 (SampleDirectLight와 동일한 시드 → 동일한 방향)
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
        // 환경맵 NEE — CDF 중요도 샘플링 + MIS
        //   환경맵의 밝은 영역을 집중 샘플링하여 환경광 수렴 가속
        // -------------------------------------------------------
        if (g_hasEnvMap != 0u && g_envWidth > 0u) {
            float2 xiEnv = GetRandomSamples(
                pixelCoord, (uint)(bounce * 10 + 99), frameCount);

            float  envPdf;
            float3 Lenv = SampleEnvMapDir(xiEnv, g_envWidth, g_envHeight, envPdf);

            float NdotL = dot(N, Lenv);
            if (NdotL > 0.0f && envPdf > 0.0f) {
                // 섀도우 레이 — 환경맵까지 차폐 없는지 확인 (먼 거리로 쏨)
                float3 shadowTarget = hit.p + Lenv * 1e4f;
                if (!IsOccluded(hit.p + N * 0.005f, shadowTarget)) { //shadowacne ->0.001f-> 0.005f
                    // 전체 BRDF 평가
                    BRDFResult envBrdf = EvaluateBRDF(
                        N, V, Lenv,
                        hit.material.albedo,
                        hit.material.roughness,
                        hit.material.metallic);

                    // 환경맵에서 실제 radiance 읽기
                    float3 Le = SampleEnvironmentMap(Lenv);

                    // MIS: 환경맵 pdf vs BRDF combined pdf
                    LobeWeights lobe = ComputeLobeWeights(
                        N, V, hit.material.albedo, hit.material.metallic);
                    float brdfPdf = ComputeCombinedPDF(
                        N, V, Lenv, hit.material.roughness, lobe);
                    float w = PowerHeuristic(envPdf, brdfPdf);

                    totalRadiance += clamp(
                        envBrdf.value * Le * w / (envPdf + 1e-10f) * throughput,
                        0.0f, 50.0f);
                }
            }
        }

        // -------------------------------------------------------
        // 간접광: Lobe Selection (Specular / Diffuse)
        //   Fresnel 기반 확률로 로브를 선택하고, 해당 분포에서 방향 샘플링
        //   BRDF 평가는 항상 full BRDF (combined PDF로 나눔)
        // -------------------------------------------------------
        LobeWeights lobe = ComputeLobeWeights(N, V, hit.material.albedo, hit.material.metallic);

        // 로브 선택용 난수
        float xiLobe = GetRandomFloat(pixelCoord, (uint)bounce + 200u, frameCount);

        float3 L;
        bool   sampledSpecular;

        if (xiLobe < lobe.pSpec) {
            // === Specular Lobe: GGX 중요도 샘플링 ===
            float2 xiSpec = GetRandomSamples(pixelCoord, (uint)bounce, frameCount);
            float3 H = ImportanceSampleVNDF(xiSpec, N, V, hit.material.roughness);
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

        // Throughput 갱신: brdf(L) * NdotL / pdf(L)
        throughput *= brdfRes.value / combinedPdf;
        throughput  = min(throughput, float3(10.0f, 10.0f, 10.0f));  // Firefly 억제

        // 다음 바운스 MIS용 정보 저장
        prevBrdfPdf  = combinedPdf;
        prevSpecular = sampledSpecular && (hit.material.roughness < 0.05f);

        // -------------------------------------------------------
        // Russian Roulette (bounce >= 1부터 적용)
        //   throughput이 낮으면 확률적으로 경로 종료 → 편향 없이 비용 절감
        // -------------------------------------------------------
        if (bounce >= 1) {
            float pSurvive = clamp(
                max(throughput.r, max(throughput.g, throughput.b)),
                0.05f, 0.75f);
            if (GetRandomFloat(pixelCoord, (uint)bounce + 100u, frameCount) > pSurvive)
                break;
            throughput /= pSurvive;
        }

        // 다음 바운스 레이 설정 (법선 방향으로 약간 오프셋)
        ray.origin    = hit.p + N * 0.005f; //shadow acne ->0.001f->0.005f
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

    // 멀티 샘플링 루프
    float3 newSample = float3(0, 0, 0);
    for (int s = 0; s < SAMPLES_PER_PIXEL; ++s) {
        uint sampleSeed = frameCount * (uint)SAMPLES_PER_PIXEL + (uint)s;
        Ray  ray        = GenerateCameraRay(pixelCoord, uint2(screenW, screenH), sampleSeed);
        float3 s_val    = TracePath(ray, pixelCoord, sampleSeed);

        // NaN/Inf 방어 + Firefly 클램핑
        if (!any(isnan(s_val)) && !any(isinf(s_val)))
            newSample += clamp(s_val, 0.0f, 10.0f);
    }
    newSample /= (float)SAMPLES_PER_PIXEL;

    // 누적만 수행 — 톤맵핑은 ToneMapCS에서 별도 처리
    if (frameCount == 0u) {
        g_accum[pixelCoord] = float4(newSample, 1.0f);
    } else {
        g_accum[pixelCoord] += float4(newSample, 0.0f);
    }
}