// -------------------------------------------------------
// Minimal NRD front-end helpers used by the local path tracer.
// Formulas mirror NRD v4.14.3 front-end packing helpers.
// -------------------------------------------------------

static const float NRD_FP16_MAX = 65504.0f;
static const float NRD_EPS      = 1e-6f;

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
