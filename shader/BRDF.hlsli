#ifndef __BRDF_HLSLI__
#define __BRDF_HLSLI__

// PI는 Utility.hlsli에서 이미 정의됨
#ifndef PI
#define PI 3.14159265359f
#endif

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
// 4-a. GGX 중요도 샘플링 (Specular lobe)
// -------------------------------------------------------
float3 ImportanceSampleGGX(float2 xi, float3 N, float roughness) {
    float a   = roughness * roughness;   // alpha
    float phi = 2.0f * PI * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up        = abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// Specular PDF: D(H)*NdotH / (4*VdotH)
float ComputeSpecularPDF(float3 N, float3 V, float3 L, float roughness) {
    float3 H     = normalize(V + L);
    float  NDF   = DistributionGGX(N, H, roughness);
    float  NdotH = max(dot(N, H), 0.0f);
    float  VdotH = max(dot(V, H), 0.0f);
    return (NDF * NdotH) / (4.0f * VdotH + 0.0001f);
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

    float3 specular = (D * G * F) / (4.0f * NdotV * NdotL + 0.0001f);
    float3 kD       = (1.0f - F) * (1.0f - metallic);
    float3 diffuse  = kD * albedo / PI;

    res.value = (diffuse + specular) * NdotL;
    res.F     = F;
    return res;
}

#endif
