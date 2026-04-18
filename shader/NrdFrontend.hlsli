// -------------------------------------------------------
// Minimal NRD front-end helpers used by the local path tracer.
// Formulas mirror NRD v4.14.3 front-end packing helpers.
// -------------------------------------------------------

static const float NRD_FP16_MAX = 65504.0f;
static const float NRD_EPS      = 1e-6f;
static const float NRD_MATERIAL_FACTOR_MIN_SCALE = 0.02f;
static const float NRD_ROUGHNESS_FACTOR_MIN_SCALE = 0.1f;

float3 NrdSafeNormalize(float3 v)
{
    return v * rsqrt(dot(v, v) + 1e-9f);
}

float2 NrdEncodeUnitVector(float3 v)
{
    v /= dot(abs(v), float3(1.0f, 1.0f, 1.0f));

    float2 octWrap = (1.0f - abs(v.yx)) * (step(0.0f, v.xy) * 2.0f - 1.0f);
    v.xy = v.z >= 0.0f ? v.xy : octWrap;

    return v.xy * 0.5f + 0.5f;
}

float3 NrdDecodeUnitVector(float2 p)
{
    p = p * 2.0f - 1.0f;

    float3 n = float3(p.xy, 1.0f - abs(p.x) - abs(p.y));
    float t = saturate(-n.z);
    n.xy -= t * (step(0.0f, n.xy) * 2.0f - 1.0f);

    return NrdSafeNormalize(n);
}

float3 NrdLinearToYCoCg(float3 color)
{
    float y  = dot(color, float3(0.25f, 0.5f, 0.25f));
    float co = dot(color, float3(0.5f, 0.0f, -0.5f));
    float cg = dot(color, float3(-0.25f, 0.5f, -0.25f));

    return float3(y, co, cg);
}

float3 NrdYCoCgToLinear(float3 color)
{
    float t = color.x - color.z;

    float3 result;
    result.y = color.x + color.z;
    result.x = t + color.y;
    result.z = t - color.y;
    return result;
}

float4 NrdPackNormalAndRoughness(float3 normal, float roughness)
{
    float4 packed;
    packed.xy = NrdEncodeUnitVector(NrdSafeNormalize(normal));
    packed.z  = saturate(roughness);
    packed.w  = 0.0f;
    return packed;
}

float4 NrdUnpackNormalAndRoughness(float4 packed)
{
    float4 result;
    result.xyz = NrdDecodeUnitVector(packed.xy);
    result.w = saturate(packed.z);
    return result;
}

float3 NrdEnvironmentTerm(float3 rf0, float NoV, float roughness)
{
    float m = saturate(roughness * roughness);

    float4 X;
    X.x = 1.0f;
    X.y = NoV;
    X.z = NoV * NoV;
    X.w = NoV * X.z;

    float4 Y;
    Y.x = 1.0f;
    Y.y = m;
    Y.z = m * m;
    Y.w = m * Y.z;

    const float2x2 M1 = float2x2(0.99044f, -1.28514f, 1.29678f, -0.755907f);
    const float3x3 M2 = float3x3(1.0f, 2.92338f, 59.4188f, 20.3225f, -27.0302f, 222.592f, 121.563f, 626.13f, 316.627f);
    const float2x2 M3 = float2x2(0.0365463f, 3.32707f, 9.0632f, -9.04756f);
    const float3x3 M4 = float3x3(1.0f, 3.59685f, -1.36772f, 9.04401f, -16.3174f, 9.22949f, 5.56589f, 19.7886f, -20.2123f);

    float bias = dot(mul(M1, X.xy), Y.xy) / max(dot(mul(M2, X.xyw), Y.xyw), NRD_EPS);
    float scale = dot(mul(M3, X.xy), Y.xy) / max(dot(mul(M4, X.xzw), Y.xyw), NRD_EPS);

    return saturate(rf0 * scale + bias);
}

void NrdMaterialFactors(
    float3 normal,
    float3 viewDir,
    float3 albedo,
    float metalness,
    float roughness,
    out float3 diffuseFactor,
    out float3 specularFactor)
{
    float3 rf0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, saturate(metalness));
    float NoV = abs(dot(NrdSafeNormalize(normal), NrdSafeNormalize(viewDir)));
    float3 fEnv = NrdEnvironmentTerm(rf0, NoV, roughness);

    diffuseFactor = (1.0f - fEnv) * albedo;
    diffuseFactor = lerp(NRD_MATERIAL_FACTOR_MIN_SCALE.xxx, float3(1.0f, 1.0f, 1.0f), diffuseFactor);

    specularFactor = fEnv;
    specularFactor *= lerp(NRD_ROUGHNESS_FACTOR_MIN_SCALE.xxx, float3(1.0f, 1.0f, 1.0f), roughness);
    specularFactor = lerp(NRD_MATERIAL_FACTOR_MIN_SCALE.xxx, float3(1.0f, 1.0f, 1.0f), specularFactor);
}

float NrdReblurGetHitDistanceNormalization(float viewZ, float4 hitDistParams, float roughness)
{
    float roughnessScale = saturate(exp2(hitDistParams.w * roughness * roughness));
    return (hitDistParams.x + abs(viewZ) * hitDistParams.y) * lerp(1.0f, hitDistParams.z, roughnessScale);
}

float NrdReblurGetNormHitDist(float hitDist, float viewZ, float4 hitDistParams, float roughness)
{
    float normalization = NrdReblurGetHitDistanceNormalization(viewZ, hitDistParams, roughness);
    return saturate(hitDist / normalization);
}

float4 NrdPackReblurRadianceAndNormHitDist(float3 radiance, float normHitDist)
{
    float3 safeRadiance = any(isnan(radiance)) || any(isinf(radiance))
        ? float3(0.0f, 0.0f, 0.0f)
        : clamp(radiance, 0.0f, NRD_FP16_MAX);

    float safeNormHitDist = (isnan(normHitDist) || isinf(normHitDist))
        ? 0.0f
        : saturate(normHitDist);

    return float4(NrdLinearToYCoCg(safeRadiance), safeNormHitDist);
}
