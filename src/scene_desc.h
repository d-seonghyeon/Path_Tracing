#ifndef __SCENE_DESC_H__
#define __SCENE_DESC_H__
#include "common.h"
#include <vector>

// -------------------------------------------------------
// GPU StructuredBuffer 레이아웃
// -------------------------------------------------------
struct GpuMaterial {
    glm::vec3 albedo;
    float     roughness;
    glm::vec3 emissive;
    float     metallic;
};

struct GpuMeshInfo {
    uint32_t vertexOffset;
    uint32_t indexOffset;
    uint32_t indexCount;
    uint32_t materialIndex;
};

// -------------------------------------------------------
// 광원 타입
// -------------------------------------------------------
enum LightType : uint32_t {
    LIGHT_SPHERE   = 0,
    LIGHT_TRIANGLE = 1,
    LIGHT_QUAD     = 2,
};

// -------------------------------------------------------
// 통합 광원 디스크립터 (80 bytes, 16-byte aligned)
//   셰이더의 ShaderLight와 메모리 레이아웃 일치 필수
// -------------------------------------------------------
struct LightDesc {
    glm::vec3 p0;                          // sphere: center, tri/quad: v0
    uint32_t  lightType { LIGHT_SPHERE };
    glm::vec3 p1;                          // tri/quad: v1
    float     radius    { 0.f };           // sphere only
    glm::vec3 p2;                          // tri/quad: v2
    float     area      { 0.f };           // 사전 계산된 면적
    glm::vec3 p3;                          // quad: v3
    float     _pad0     { 0.f };
    glm::vec3 emission;
    float     _pad1     { 0.f };

    // --- 팩토리 ---
    static LightDesc MakeSphere(glm::vec3 center, float radius, glm::vec3 emission) {
        LightDesc l{};
        l.p0        = center;
        l.lightType = LIGHT_SPHERE;
        l.radius    = radius;
        l.area      = 4.0f * glm::pi<float>() * radius * radius;
        l.emission  = emission;
        return l;
    }

    static LightDesc MakeTriangle(glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 emission) {
        LightDesc l{};
        l.p0        = v0;
        l.lightType = LIGHT_TRIANGLE;
        l.p1        = v1;
        l.p2        = v2;
        l.area      = 0.5f * glm::length(glm::cross(v1 - v0, v2 - v0));
        l.emission  = emission;
        return l;
    }

    static LightDesc MakeQuad(glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, glm::vec3 emission) {
        LightDesc l{};
        l.p0        = v0;
        l.lightType = LIGHT_QUAD;
        l.p1        = v1;
        l.p2        = v2;
        l.p3        = v3;
        // 두 삼각형 면적의 합
        float a1 = 0.5f * glm::length(glm::cross(v1 - v0, v2 - v0));
        float a2 = 0.5f * glm::length(glm::cross(v2 - v0, v3 - v0));
        l.area      = a1 + a2;
        l.emission  = emission;
        return l;
    }
};
static_assert(sizeof(LightDesc) == 80, "LightDesc must be 80 bytes");

// -------------------------------------------------------
// 씬 디스크립터
// -------------------------------------------------------
struct BoxDesc {
    glm::vec3   lo, hi;
    GpuMaterial mat;
};

struct QuadDesc {
    glm::vec3   p0, p1, p2, p3;
    glm::vec3   n;
    GpuMaterial mat;
};

struct SceneDesc {
    std::vector<BoxDesc>   boxes;
    std::vector<QuadDesc>  quads;
    std::vector<LightDesc> lights;
};

SceneDesc MakeCityScene();

#endif
