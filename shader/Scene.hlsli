#ifndef __SCENE_HLSLI__
#define __SCENE_HLSLI__

#include "Intersection.hlsli"

#ifndef PI
#define PI 3.14159265359f
#endif

// -------------------------------------------------------
// 광원 타입
// -------------------------------------------------------
#define LIGHT_SPHERE   0
#define LIGHT_TRIANGLE 1
#define LIGHT_QUAD     2

// -------------------------------------------------------
// GPU 버퍼 구조체
// -------------------------------------------------------
struct ShaderVertex {
    float3 position; float pad0;
    float3 normal;   float pad1;
    float2 texCoord; float2 pad2;
    float3 tangent;  float pad3;
};

struct ShaderMaterial {
    float3 albedo;
    float  roughness;
    float3 emissive;
    float  metallic;
};

struct ShaderMeshInfo {
    uint vertexOffset;
    uint indexOffset;
    uint indexCount;
    uint materialIndex;
};

StructuredBuffer<ShaderVertex>   g_vertices  : register(t0);
StructuredBuffer<uint>           g_indices   : register(t1);
StructuredBuffer<ShaderMeshInfo> g_meshInfos : register(t2);
StructuredBuffer<ShaderMaterial> g_materials : register(t3);

struct ShaderBvhNode {
    float3 aabbMin;
    uint   leftFirst;
    float3 aabbMax;
    uint   primCount;
};

struct ShaderBvhPrim {
    uint triOffset;
    uint meshIdx;
    uint pad0, pad1;
};

StructuredBuffer<ShaderBvhNode> g_bvhNodes : register(t4);
StructuredBuffer<ShaderBvhPrim> g_bvhPrims : register(t5);

// -------------------------------------------------------
// 통합 광원 구조체 (80 bytes, C++의 LightDesc와 일치)
// -------------------------------------------------------
struct ShaderLight {
    float3 p0;         uint lightType;
    float3 p1;         float radius;
    float3 p2;         float area;
    float3 p3;         float _pad0;
    float3 emission;   float _pad1;
};
//구-> p0 = radius, 삼각형 -> p0,p1,p2= v0,v1,v2, 사각형 -> p0,p1,p2,p3 = v0,v1,v2,v3 

StructuredBuffer<ShaderLight> g_lights : register(t6);

// -------------------------------------------------------
// SurfaceHit
// -------------------------------------------------------
struct SurfaceHit {
    float          t;
    float3         p;
    float3         normal;
    float3         bary;
    ShaderMaterial material;
    bool           frontFace;
};

void SetFaceNormal(Ray ray, float3 outwardNormal, inout SurfaceHit hit) {
    hit.frontFace = dot(ray.direction, outwardNormal) < 0.0f;
    hit.normal    = hit.frontFace ? outwardNormal : -outwardNormal;
}

bool IsEmitter(ShaderMaterial mat) {
    return dot(mat.emissive, float3(1, 1, 1)) > 0.001f;
}

// -------------------------------------------------------
// 면적 광원 샘플링 헬퍼
// -------------------------------------------------------

// 삼각형 균일 샘플링 (Barycentric)
float3 SampleTrianglePoint(float3 v0, float3 v1, float3 v2, float2 xi) {
    float su = sqrt(xi.x);
    float u  = 1.0f - su;
    float v  = xi.y * su;
    return v0 * u + v1 * v + v2 * (1.0f - u - v);
}

float3 TriangleNormal(float3 v0, float3 v1, float3 v2) {
    return normalize(cross(v1 - v0, v2 - v0));
}

// 사각형 균일 샘플링 (Bilinear)
float3 SampleQuadPoint(float3 v0, float3 v1, float3 v2, float3 v3, float2 xi) {
    float3 a = lerp(v0, v1, xi.x);
    float3 b = lerp(v3, v2, xi.x);
    return lerp(a, b, xi.y);
}

float3 QuadNormal(float3 v0, float3 v1, float3 v3) {
    return normalize(cross(v1 - v0, v3 - v0));
}

// -------------------------------------------------------
// 광원별 Solid-Angle PDF 계산 (MIS용)
// -------------------------------------------------------
float ComputeLightPdf(float3 hitP, int lightIdx) {
    ShaderLight light = g_lights[lightIdx];

    if (light.lightType == LIGHT_SPHERE) {
        // 원뿔 solid angle PDF
        float3 toCenter     = light.p0 - hitP;
        float  dist2        = dot(toCenter, toCenter);
        float  sinThetaMax2 = light.radius * light.radius / dist2;
        if (sinThetaMax2 >= 1.0f) return 0.0f;
        float cosThetaMax = sqrt(max(0.0f, 1.0f - sinThetaMax2));
        return 1.0f / (2.0f * PI * (1.0f - cosThetaMax + 1e-10f));
    }
    else {
        // 삼각형/사각형: 면적 PDF → solid angle PDF 변환
        // pdf_solidAngle = pdf_area * dist^2 / |cos(theta_light)|
        // pdf_area = 1 / area
        // 여기서는 방향을 모르므로 0 반환 → 호출자가 직접 계산
        // (SampleDirectLight 내부에서 정확한 값 계산)
        return 0.0f; // placeholder — 아래 ComputeLightPdfDir에서 처리
    }
}

// 방향이 주어졌을 때 정확한 solid angle PDF
float ComputeLightPdfDir(float3 hitP, float3 L, float hitDist, int lightIdx) {
    ShaderLight light = g_lights[lightIdx];

    if (light.lightType == LIGHT_SPHERE) {
        float3 toCenter     = light.p0 - hitP;
        float  dist2        = dot(toCenter, toCenter);
        float  sinThetaMax2 = light.radius * light.radius / dist2;
        if (sinThetaMax2 >= 1.0f) return 0.0f;
        float cosThetaMax = sqrt(max(0.0f, 1.0f - sinThetaMax2));
        return 1.0f / (2.0f * PI * (1.0f - cosThetaMax + 1e-10f));
    }
    else if (light.lightType == LIGHT_TRIANGLE) {
        float3 lN          = TriangleNormal(light.p0, light.p1, light.p2);
        float  cosAtLight  = abs(dot(lN, -L));
        if (cosAtLight < 1e-6f) return 0.0f;
        float  pdfArea     = 1.0f / max(light.area, 1e-6f);
        return pdfArea * (hitDist * hitDist) / cosAtLight;
    }
    else { // LIGHT_QUAD
        float3 lN          = QuadNormal(light.p0, light.p1, light.p3);
        float  cosAtLight  = abs(dot(lN, -L));
        if (cosAtLight < 1e-6f) return 0.0f;
        float  pdfArea     = 1.0f / max(light.area, 1e-6f);
        return pdfArea * (hitDist * hitDist) / cosAtLight;
    }
}

// -------------------------------------------------------
// 씬 교차 검사 (BVH)
// -------------------------------------------------------
bool SceneIntersect(Ray ray, out SurfaceHit hit) {
    hit = (SurfaceHit)0;
    float tClosest = 1e30f;
    bool  hitAny   = false;

    // 바닥 평면 (y = 0)
    float3 planeN = float3(0, 1, 0);
    float  denom  = dot(ray.direction, planeN);
    if (abs(denom) > 0.0001f) {
        float tPlane = -dot(ray.origin, planeN) / denom;
        if (tPlane > 0.0001f && tPlane < tClosest) {
            tClosest               = tPlane;
            hitAny                 = true;
            hit.t                  = tPlane;
            hit.p                  = ray.origin + tPlane * ray.direction;
            hit.normal             = planeN;
            hit.frontFace          = true;
            float2 uv              = hit.p.xz;
            float checker          = (fmod(abs(floor(uv.x) + floor(uv.y)), 2.0f) < 1.0f) ? 1.0f : 0.0f;
            hit.material.albedo    = lerp(float3(0.3f,0.3f,0.3f), float3(0.9f,0.9f,0.9f), checker);
            hit.material.roughness = 0.8f;
            hit.material.metallic  = 0.0f;
            hit.material.emissive  = float3(0, 0, 0);
        }
    }

    // 광원 (구/삼각형/사각형)
    for (int li = 0; li < (int)g_lightCount; ++li) {
        ShaderLight sl = g_lights[li];

        if (sl.lightType == LIGHT_SPHERE) {
            HitRecord recL;
            if (IntersectSphere(ray, sl.p0, sl.radius, recL)) {
                if (recL.t > 0.0001f && recL.t < tClosest) {
                    tClosest               = recL.t;
                    hitAny                 = true;
                    hit.t                  = recL.t;
                    hit.p                  = recL.p;
                    hit.normal             = recL.normal;
                    hit.frontFace          = true;
                    hit.material.albedo    = float3(1, 1, 1);
                    hit.material.roughness = 1.0f;
                    hit.material.metallic  = 0.0f;
                    hit.material.emissive  = sl.emission;
                }
            }
        }
        else if (sl.lightType == LIGHT_TRIANGLE) {
            HitRecord recL;
            if (IntersectTriangle(ray, sl.p0, sl.p1, sl.p2, recL)) {
                if (recL.t > 0.0001f && recL.t < tClosest) {
                    tClosest               = recL.t;
                    hitAny                 = true;
                    hit.t                  = recL.t;
                    hit.p                  = recL.p;
                    hit.normal             = TriangleNormal(sl.p0, sl.p1, sl.p2);
                    hit.frontFace          = dot(ray.direction, hit.normal) < 0.0f;
                    if (!hit.frontFace) hit.normal = -hit.normal;
                    hit.material.albedo    = float3(1, 1, 1);
                    hit.material.roughness = 1.0f;
                    hit.material.metallic  = 0.0f;
                    hit.material.emissive  = sl.emission;
                }
            }
        }
        else { // LIGHT_QUAD → 두 삼각형으로 교차
            HitRecord recL;
            if (IntersectTriangle(ray, sl.p0, sl.p1, sl.p2, recL)) {
                if (recL.t > 0.0001f && recL.t < tClosest) {
                    tClosest               = recL.t;
                    hitAny                 = true;
                    hit.t                  = recL.t;
                    hit.p                  = recL.p;
                    hit.normal             = QuadNormal(sl.p0, sl.p1, sl.p3);
                    hit.frontFace          = dot(ray.direction, hit.normal) < 0.0f;
                    if (!hit.frontFace) hit.normal = -hit.normal;
                    hit.material.albedo    = float3(1, 1, 1);
                    hit.material.roughness = 1.0f;
                    hit.material.metallic  = 0.0f;
                    hit.material.emissive  = sl.emission;
                }
            }
            if (IntersectTriangle(ray, sl.p0, sl.p2, sl.p3, recL)) {
                if (recL.t > 0.0001f && recL.t < tClosest) {
                    tClosest               = recL.t;
                    hitAny                 = true;
                    hit.t                  = recL.t;
                    hit.p                  = recL.p;
                    hit.normal             = QuadNormal(sl.p0, sl.p1, sl.p3);
                    hit.frontFace          = dot(ray.direction, hit.normal) < 0.0f;
                    if (!hit.frontFace) hit.normal = -hit.normal;
                    hit.material.albedo    = float3(1, 1, 1);
                    hit.material.roughness = 1.0f;
                    hit.material.metallic  = 0.0f;
                    hit.material.emissive  = sl.emission;
                }
            }
        }
    }

    // BVH 순회
    float3 rayInvD = 1.0f / ray.direction;
    uint bvhStack[64];
    int  bvhTop = 0;
    bvhStack[bvhTop++] = 0u;

    while (bvhTop > 0) {
        ShaderBvhNode node = g_bvhNodes[bvhStack[--bvhTop]];

        float3 t0s    = (node.aabbMin - ray.origin) * rayInvD;
        float3 t1s    = (node.aabbMax - ray.origin) * rayInvD;
        float3 tMn    = min(t0s, t1s);
        float3 tMx    = max(t0s, t1s);
        float  tEnter = max(max(tMn.x, tMn.y), tMn.z);
        float  tExit  = min(min(tMx.x, tMx.y), tMx.z);
        if (tExit < 0.0001f || tEnter > tExit || tEnter > tClosest)
            continue;

        if (node.primCount > 0u) {
            for (uint p = 0u; p < node.primCount; ++p) {
                ShaderBvhPrim prim = g_bvhPrims[node.leftFirst + p];
                uint i0 = g_indices[prim.triOffset + 0];
                uint i1 = g_indices[prim.triOffset + 1];
                uint i2 = g_indices[prim.triOffset + 2];
                HitRecord rec;
                if (IntersectTriangle(ray,
                    g_vertices[i0].position,
                    g_vertices[i1].position,
                    g_vertices[i2].position, rec))
                {
                    if (rec.t > 0.0001f && rec.t < tClosest) {
                        tClosest = rec.t;
                        hitAny   = true;
                        hit.t    = rec.t;
                        hit.p    = rec.p;
                        hit.bary = rec.bary;
                        float3 N = normalize(
                            g_vertices[i0].normal * rec.bary.x +
                            g_vertices[i1].normal * rec.bary.y +
                            g_vertices[i2].normal * rec.bary.z);
                        SetFaceNormal(ray, N, hit);
                        hit.material = g_materials[prim.meshIdx];
                    }
                }
            }
        } else {
            if (bvhTop + 1 < 64) {
                bvhStack[bvhTop++] = node.leftFirst;
                bvhStack[bvhTop++] = node.leftFirst + 1u;
            }
        }
    }
    return hitAny;
}

// -------------------------------------------------------
// 섀도우 레이
// -------------------------------------------------------
bool IsOccluded(float3 origin, float3 target) {
    float3 toTarget = target - origin;
    float  dist     = length(toTarget);
    Ray    sr;
    sr.origin    = origin;
    sr.direction = toTarget / dist;

    // 바닥 평면
    float pd = dot(sr.direction, float3(0, 1, 0));
    if (abs(pd) > 0.0001f) {
        float tp = -dot(sr.origin, float3(0, 1, 0)) / pd;
        if (tp > 0.001f && tp < dist - 0.001f) return true;
    }

    // BVH 섀도우 순회
    float3 srInvD = 1.0f / sr.direction;
    uint sStack[64];
    int  sTop = 0;
    sStack[sTop++] = 0u;

    while (sTop > 0) {
        ShaderBvhNode node = g_bvhNodes[sStack[--sTop]];
        float3 t0s    = (node.aabbMin - sr.origin) * srInvD;
        float3 t1s    = (node.aabbMax - sr.origin) * srInvD;
        float3 tMn    = min(t0s, t1s);
        float3 tMx    = max(t0s, t1s);
        float  tEnter = max(max(tMn.x, tMn.y), tMn.z);
        float  tExit  = min(min(tMx.x, tMx.y), tMx.z);
        if (tExit < 0.001f || tEnter > tExit || tEnter > dist - 0.001f)
            continue;

        if (node.primCount > 0u) {
            for (uint p = 0u; p < node.primCount; ++p) {
                ShaderBvhPrim prim = g_bvhPrims[node.leftFirst + p];
                HitRecord tr;
                if (IntersectTriangle(sr,
                    g_vertices[g_indices[prim.triOffset + 0]].position,
                    g_vertices[g_indices[prim.triOffset + 1]].position,
                    g_vertices[g_indices[prim.triOffset + 2]].position, tr))
                    if (tr.t > 0.001f && tr.t < dist - 0.001f) return true;
            }
        } else {
            if (sTop + 1 < 64) {
                sStack[sTop++] = node.leftFirst;
                sStack[sTop++] = node.leftFirst + 1u;
            }
        }
    }
    return false;
}

// -------------------------------------------------------
// NEE: 통합 직접 광원 샘플링 (구/삼각형/사각형)
// -------------------------------------------------------
float3 SampleDirectLight(
    float3 hitP, float3 hitN, float3 V,
    ShaderMaterial mat, float2 xi, int lightIdx,
    out float lightPdfOut)
{
    lightPdfOut = 0.0f;
    ShaderLight light = g_lights[lightIdx];
    if (light.area <= 0.0f) return float3(0, 0, 0);

    float3 lPos;       // 광원 위의 샘플점
    float3 lNormal;    // 광원 표면 법선
    float  pdfLight;   // solid angle PDF

    // -------------------------------------------------------
    // 광원 타입별 샘플링
    // -------------------------------------------------------
    if (light.lightType == LIGHT_SPHERE) {
        // 원뿔각 샘플링 (기존)
        float3 toCenter = light.p0 - hitP;
        float  dist2    = dot(toCenter, toCenter);
        float  dist     = sqrt(dist2);
        float3 wc       = toCenter / dist;

        float sinThetaMax2 = light.radius * light.radius / dist2;
        if (sinThetaMax2 >= 1.0f) return float3(0, 0, 0);
        float cosThetaMax = sqrt(max(0.0f, 1.0f - sinThetaMax2));

        float cosTheta = 1.0f - xi.y * (1.0f - cosThetaMax);
        float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
        float phi      = 2.0f * PI * xi.x;

        float3 up  = abs(wc.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 T   = normalize(cross(up, wc));
        float3 B   = cross(wc, T);
        float3 L   = normalize(T*(cos(phi)*sinTheta) + B*(sin(phi)*sinTheta) + wc*cosTheta);

        float NdotL = dot(hitN, L);
        if (NdotL <= 0.0f) return float3(0, 0, 0);

        Ray shadowRay;
        shadowRay.origin    = hitP + hitN * 0.001f;
        shadowRay.direction = L;
        HitRecord lightRec;
        if (!IntersectSphere(shadowRay, light.p0, light.radius, lightRec))
            return float3(0, 0, 0);

        lPos    = shadowRay.origin + lightRec.t * L;
        lNormal = normalize(lPos - light.p0);
        pdfLight = 1.0f / (2.0f * PI * (1.0f - cosThetaMax + 1e-10f));
    }
    else if (light.lightType == LIGHT_TRIANGLE) {
        // 삼각형 균일 면적 샘플링
        lPos    = SampleTrianglePoint(light.p0, light.p1, light.p2, xi);
        lNormal = TriangleNormal(light.p0, light.p1, light.p2);

        float3 toLight = lPos - hitP;
        float  distSq  = dot(toLight, toLight);
        float  dist    = sqrt(distSq);
        float3 L       = toLight / dist;

        float NdotL = dot(hitN, L);
        if (NdotL <= 0.0f) return float3(0, 0, 0);

        float cosAtLight = abs(dot(lNormal, -L));
        if (cosAtLight < 1e-6f) return float3(0, 0, 0);

        // 면적 PDF → solid angle PDF
        pdfLight = distSq / (light.area * cosAtLight);
    }
    else { // LIGHT_QUAD
        // 사각형 균일 면적 샘플링
        lPos    = SampleQuadPoint(light.p0, light.p1, light.p2, light.p3, xi);
        lNormal = QuadNormal(light.p0, light.p1, light.p3);

        float3 toLight = lPos - hitP;
        float  distSq  = dot(toLight, toLight);
        float  dist    = sqrt(distSq);
        float3 L       = toLight / dist;

        float NdotL = dot(hitN, L);
        if (NdotL <= 0.0f) return float3(0, 0, 0);

        float cosAtLight = abs(dot(lNormal, -L));
        if (cosAtLight < 1e-6f) return float3(0, 0, 0);

        pdfLight = distSq / (light.area * cosAtLight);
    }

    // -------------------------------------------------------
    // 공통: 차폐 검사 + BRDF 평가
    // -------------------------------------------------------
    if (IsOccluded(hitP + hitN * 0.001f, lPos))
        return float3(0, 0, 0);

    lightPdfOut = pdfLight;

    float3 toLight = lPos - hitP;
    float  dist    = length(toLight);
    float3 L       = toLight / dist;
    float  NdotL   = dot(hitN, L);
    float  NdotV   = max(dot(hitN, V), 0.0f);

    float3 H  = normalize(V + L);
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), mat.albedo, mat.metallic);
    float3 F  = FresnelSchlick(max(dot(H, V), 0.0f), F0);
    float  D  = DistributionGGX(hitN, H, mat.roughness);
    float  G  = GeometrySmith(hitN, V, L, mat.roughness);

    float3 specular = (D * G * F) / (4.0f * NdotV * NdotL + 0.0001f);
    float3 kD       = (1.0f - F) * (1.0f - mat.metallic);
    float3 brdf     = kD * mat.albedo / PI + specular;

    return brdf * light.emission * NdotL / (pdfLight + 0.0001f);
}

// -------------------------------------------------------
// 히트 포인트가 어떤 광원에 맞았는지 확인 (MIS용)
// -------------------------------------------------------
int FindHitLight(float3 hitP, float3 hitNormal) {
    for (int li = 0; li < (int)g_lightCount; ++li) {
        ShaderLight sl = g_lights[li];

        if (sl.lightType == LIGHT_SPHERE) {
            float3 diff = hitP - sl.p0;
            float  d2   = dot(diff, diff);
            if (d2 < sl.radius * sl.radius * 1.21f) return li;
        }
        else if (sl.lightType == LIGHT_TRIANGLE) {
            // 삼각형 평면과의 거리 확인
            float3 lN   = TriangleNormal(sl.p0, sl.p1, sl.p2);
            float  dist = abs(dot(hitP - sl.p0, lN));
            if (dist < 0.05f) return li;
        }
        else { // LIGHT_QUAD
            float3 lN   = QuadNormal(sl.p0, sl.p1, sl.p3);
            float  dist = abs(dot(hitP - sl.p0, lN));
            if (dist < 0.05f) return li;
        }
    }
    return -1;
}

#endif