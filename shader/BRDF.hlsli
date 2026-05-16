#ifndef __BRDF_HLSLI__
#define __BRDF_HLSLI__

// PI는 Utility.hlsli에서 이미 정의됨
#ifndef PI
#define PI 3.14159265359f
#endif

Texture2D<float2> g_energyLUT : register(t11);
SamplerState     g_energyLUTSamp : register(s1);

// -------------------------------------------------------
// 유틸리티
// -------------------------------------------------------
float Luminance(float3 c) {
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// Power Heuristic (β=2) for MIS
float PowerHeuristic(float pdfA, float pdfB) {
    float a2 = pdfA * pdfA;
    float b2 = pdfB * pdfB;
    return a2 / (a2 + b2 + 1e-10f);
}
// Balance Heuristic for MIS
float BalanceHeuristic(float pdfA, float pdfB){
    return pdfA / (pdfA + pdfB + 1e-10f);
}


// -------------------------------------------------------
// 1. NDF: GGX 분포
// -------------------------------------------------------
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float denom  = (NdotH2 * (a2 - 1.0f) + 1.0f);
    return a2 / max(PI * denom * denom, 0.000001f);
}

// -------------------------------------------------------
// 2. Geometry: Smith + Schlick-GGX
// -------------------------------------------------------
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    return GeometrySchlickGGX(NdotV, roughness) *
           GeometrySchlickGGX(NdotL, roughness);
}

// -------------------------------------------------------
// 3. Fresnel: Schlick
// -------------------------------------------------------
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

// -------------------------------------------------------
// 4-a. VNDF (Specular lobe)
// -------------------------------------------------------
// 기존 ImportanceSampleGGX 삭제하고 이걸로 교체
float3 ImportanceSampleVNDF(float2 xi, float3 N, float3 V, float roughness) {
    float a = roughness * roughness;

    // 접선 프레임
    float3 up        = abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    // V를 탄젠트 공간으로
    float3 Vlocal = float3(
        dot(V, tangent),
        dot(V, bitangent),
        dot(V, N)
    );

    // 1. 뷰 방향을 반구로 변환 (스트레칭)
    float3 Vh = normalize(float3(a * Vlocal.x, a * Vlocal.y, Vlocal.z));

    // 2. Vh 기준 접선 프레임
    float  lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1    = lensq > 0.0f
        ? float3(-Vh.y, Vh.x, 0.0f) / sqrt(lensq)
        : float3(1, 0, 0);
    float3 T2 = cross(Vh, T1);

    // 3. 균일 디스크 샘플 → 반구 샘플
    float r   = sqrt(xi.x);
    float phi = 2.0f * PI * xi.y;
    float t1  = r * cos(phi);
    float t2  = r * sin(phi);
    float s   = 0.5f * (1.0f + Vh.z);
    t2 = (1.0f - s) * sqrt(1.0f - t1 * t1) + s * t2;

    // 4. 법선 방향 계산
    float3 Nh = t1 * T1 + t2 * T2
              + sqrt(max(0.0f, 1.0f - t1*t1 - t2*t2)) * Vh;

    // 5. 타원체 → 원래 공간으로 (언스트레칭)
    float3 Hlocal = normalize(float3(a * Nh.x, a * Nh.y, max(0.0f, Nh.z)));

    // 접선 공간 → 월드 공간
    return normalize(tangent * Hlocal.x + bitangent * Hlocal.y + N * Hlocal.z);
}

// p(L) = D(H) * G1(V) / (4 * NdotV)
float ComputeSpecularPDF(float3 N, float3 V, float3 L, float roughness) {
    float3 H     = normalize(V + L);
    float  NdotV = max(dot(N, V), 0.0f);
    float  NdotH = max(dot(N, H), 0.0f);
    float  VdotH = max(dot(V, H), 0.0f);

    float D = DistributionGGX(N, H, roughness);

    // BRDF와 동일한 G1 함수 사용
    float G1 = GeometrySchlickGGX(NdotV, roughness);

    return D * G1 / (4.0f * NdotV + 0.0001f);
}

// -------------------------------------------------------
// 4-b. Cosine-weighted 반구 샘플링 (Diffuse lobe)
// -------------------------------------------------------
float3 SampleCosineHemisphere(float2 xi, float3 N) {
    float phi      = 2.0f * PI * xi.x;
    float cosTheta = sqrt(1.0f - xi.y);
    float sinTheta = sqrt(xi.y);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up        = abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// Diffuse PDF: cos(θ) / π
float CosineHemispherePDF(float NdotL) {
    return max(NdotL, 0.0f) / PI;
}

// E(NdotV, roughness) 조회, 샘플링 함수
float2 SampleEnergyLUT(float NdotV, float roughness) {
    return g_energyLUT.SampleLevel(
        g_energyLUTSamp, float2(NdotV, roughness), 0).rg;
}

// -------------------------------------------------------
// 5. Lobe Selection 확률 계산
//    pSpec = Fresnel 기반 Specular 가중치 / 전체
//    pDiff = 1 - pSpec  (금속이면 diffuse 없음)
// -------------------------------------------------------
struct LobeWeights {
    float pSpec;
    float pDiff;
};

LobeWeights ComputeLobeWeights(float3 N, float3 V, float3 albedo, float metallic) {
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float  NdotV = max(dot(N, V), 0.0f);
    float3 F  = FresnelSchlick(NdotV, F0);
    float  specW = Luminance(F);
    float  diffW = (1.0f - specW) * (1.0f - metallic);

    LobeWeights w;
    float total = specW + diffW + 1e-8f;
    w.pSpec = specW / total;
    w.pDiff = diffW / total;
    return w;
}

// -------------------------------------------------------
// 6. Combined BRDF PDF (lobe selection 반영)
//    pCombined = pSpec * pdfSpec(L) + pDiff * pdfDiff(L)
// -------------------------------------------------------
float ComputeCombinedPDF(float3 N, float3 V, float3 L, float roughness,
                         LobeWeights lobe) {
    float pdfSpec = ComputeSpecularPDF(N, V, L, roughness);
    float NdotL   = max(dot(N, L), 0.0f);
    float pdfDiff = CosineHemispherePDF(NdotL);
    return lobe.pSpec * pdfSpec + lobe.pDiff * pdfDiff;
}

// -------------------------------------------------------
// 7. Full BRDF 평가 (diffuse + specular)
//    lobe selection과 무관하게 전체 BRDF를 반환
// -------------------------------------------------------
struct BRDFResult {
    float3 value;  // (kD * albedo/π + specular) * NdotL
    float3 F;      // 프레넬 (throughput 계산용)
};

BRDFResult EvaluateBRDF(float3 N, float3 V, float3 L, float3 albedo,
                        float roughness, float metallic) {
    BRDFResult res;
    res.value = float3(0, 0, 0);
    res.F     = float3(0, 0, 0);

    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);
    if (NdotL <= 0.0f) return res;

    float3 H  = normalize(V + L);
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 F  = FresnelSchlick(max(dot(H, V), 0.0f), F0);
    float  G  = GeometrySmith(N, V, L, roughness);
    float  D  = DistributionGGX(N, H, roughness);

    // 단일산란 specular
    float3 specular = (D * G * F) / (4.0f * NdotV * NdotL + 0.0001f);

    // Diffuse
    float3 kD      = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = kD * albedo / PI;

    // -------------------------------------------------------
    // Kulla-Conty 다중산란 보상
    // -------------------------------------------------------
    float2 lutO = SampleEnergyLUT(NdotV, roughness);
    float2 lutI = SampleEnergyLUT(NdotL, roughness);
    float  Eo   = lutO.r;                // 출사 방향 단일산란 에너지
    float  Ei   = lutI.r;                // 입사 방향 단일산란 에너지
    float  Eavg = lutO.g;                // 전 방향 평균 에너지 (roughness만의 함수)

    // 손실 에너지 분포
    float  fms = (1.0f - Eo) * (1.0f - Ei)
               / max(PI * (1.0f - Eavg), 1e-6f);

    // 다중산란 평균 Fresnel
    float3 Favg         = F0 + (1.0f - F0) / 21.0f;
    float3 Fms          = Favg * Favg * Eavg
                        / max(1.0f - Favg * (1.0f - Eavg), 1e-6f);
    float3 multiScatter = fms * Fms;
    multiScatter = min(multiScatter, float3(1.0f, 1.0f, 1.0f)); 
    //multiScatter = float3(0.0f, 0.0f, 0.0f); 


    res.value = (diffuse + specular + multiScatter) * NdotL;
    res.F     = F;
    return res;
}

#endif
