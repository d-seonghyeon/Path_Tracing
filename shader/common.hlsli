#ifndef __COMMON_HLSLI__
#define __COMMON_HLSLI__

// [Slot 0] 매 프레임, 매 오브젝트마다 바뀌는 데이터 (Transform)
cbuffer TransformUBO : register(b0) {
    row_major matrix model;
    row_major matrix view;
    row_major matrix projection;
};

// [Slot 1] 씬 전체의 공통 정보 (Light, CameraPos)
struct Light {
    float3 position;
    float  padding1;
    float3 color;
    float  padding2;
};

cbuffer SceneUBO : register(b1) {
    Light  g_lights[4];
    float3 g_viewPos;
    float  g_scenePadding;
};

// [Slot 2] 물체마다 다른 재질 정보 (PBR Parameters)
cbuffer MaterialUBO : register(b2) {
    float3 m_albedo;
    float  m_metallic;
    float  m_roughness;
    float  m_ao;
    float2 m_pad;
};

#endif
