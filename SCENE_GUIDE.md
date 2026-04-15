# 씬(Scene) 추가 가이드

PT_Object_Loading 패스 트레이서에서 새로운 씬을 만들고 등록하는 방법을 단계별로 설명합니다.

---

## 1. 씬 파이프라인 전체 흐름

```
scene_desc.cpp
  MakeXxxScene() → SceneDesc { boxes, quads, lights }
        ↓
context.cpp
  FlattenScene() → allVertices / allIndices / meshInfos / materials (CPU)
        ↓
  Bvh::Build()  → BvhNode[] / BvhPrim[]
        ↓
  Buffer::CreateWithData() → GPU StructuredBuffer
        ↓
PathTracer.hlsl / Scene.hlsli
  t0: g_vertices   t1: g_indices   t2: g_meshInfos
  t3: g_materials  t4: g_bvhNodes  t5: g_bvhPrims
  t6: g_lights
```

**규칙**: 씬 데이터는 `SceneDesc`에만 기술합니다.  
`FlattenScene()`, BVH 빌드, GPU 업로드는 `BuildSceneBuffers()`가 자동 처리하므로 **건드릴 필요 없습니다.**

---

## 2. 씬 구성 요소

### 2-1. 재질 (`GpuMaterial`)

```cpp
// scene_desc.h
struct GpuMaterial {
    glm::vec3 albedo;     // 기본 색상 (선형 sRGB, 0~1)
    float     roughness;  // 0=거울, 1=완전 확산
    glm::vec3 emissive;   // 자체 발광 (0이면 꺼짐, >1이면 밝은 광원)
    float     metallic;   // 0=비금속, 1=금속
};
```

```cpp
// 예시: 붉은 벽돌
GpuMaterial matBrick{};
matBrick.albedo    = glm::vec3(0.65f, 0.30f, 0.20f);
matBrick.roughness = 0.85f;
// metallic, emissive 는 0 으로 자동 초기화

// 예시: 금속 기둥
GpuMaterial matMetal{};
matMetal.albedo    = glm::vec3(0.18f, 0.18f, 0.20f);
matMetal.roughness = 0.50f;
matMetal.metallic  = 0.80f;

// 예시: 빛을 내는 창문
GpuMaterial matWindow{};
matWindow.albedo    = glm::vec3(1.0f, 0.9f, 0.6f);
matWindow.roughness = 1.0f;
matWindow.emissive  = glm::vec3(2.0f, 1.5f, 0.6f);  // NEE와 무관한 면발광
```

### 2-2. 박스 (`BoxDesc`)

축정렬 직육면체(AABB). 가장 단순한 지오메트리.

```cpp
// scene_desc.h
struct BoxDesc {
    glm::vec3   lo;   // 최솟값 꼭짓점 (x,y,z)
    glm::vec3   hi;   // 최댓값 꼭짓점 (x,y,z)
    GpuMaterial mat;
};

// 추가 방법
desc.boxes.push_back({ lo, hi, mat });
```

```
       hi
        *-----*
       /|    /|
      * +---* |
      | *---|-*
      |/    |/
      *-----*
      lo
```

**좌표계**: Y축이 위, Z축이 전방.  
`lo.y = 0` 이면 바닥에 닿음.

### 2-3. 쿼드 (`QuadDesc`)

평면 사각형. 창문, 바닥 패널, 얇은 벽에 사용.

```cpp
// scene_desc.h
struct QuadDesc {
    glm::vec3   p0, p1, p2, p3;  // 꼭짓점 (반드시 CCW 순서)
    glm::vec3   n;                // 법선 벡터 (정규화)
    GpuMaterial mat;
};

// 추가 방법
desc.quads.push_back({ p0, p1, p2, p3, normal, mat });
```

**CCW 순서 (카메라에서 법선 방향으로 바라봤을 때 반시계)**:

```
   p3 ---- p2
   |        |
   p0 ---- p1
   법선 = 화면 밖
```

```cpp
// 예: XZ 평면 위의 수평 쿼드 (법선 = 위)
desc.quads.push_back({
    {-1.0f, 0.0f, -1.0f},   // p0
    { 1.0f, 0.0f, -1.0f},   // p1
    { 1.0f, 0.0f,  1.0f},   // p2
    {-1.0f, 0.0f,  1.0f},   // p3
    { 0.0f, 1.0f,  0.0f},   // 법선 (위)
    matFloor
});

// 예: YZ 평면의 수직 쿼드 (X+ 방향 법선, 건물 우측 벽면)
desc.quads.push_back({
    {6.0f, 0.0f,  0.0f},    // p0
    {6.0f, 0.0f, 12.0f},    // p1
    {6.0f, 5.0f, 12.0f},    // p2
    {6.0f, 5.0f,  0.0f},    // p3
    {1.0f, 0.0f,  0.0f},    // 법선
    matWall
});
```

### 2-4. 광원 (`LightDesc`)

NEE(Next Event Estimation) 직접 조명용 구형 광원.  
렌더러가 매 바운스마다 이 광원을 명시적으로 샘플링합니다.

```cpp
// scene_desc.h
struct LightDesc {
    glm::vec3 center;    // 구의 중심
    float     radius;    // 구의 반지름
    glm::vec3 emission;  // 방사 휘도 (RGB, 단위 없음 — 클수록 밝음)
    float     _pad;      // GPU 패딩 (자동 0)
};

// 추가 방법
desc.lights.push_back({ center, radius, emission });
```

```cpp
// 예: 천장 전구
desc.lights.push_back({
    {0.0f, 4.8f, 5.0f},   // center
    0.3f,                   // radius
    {5.0f, 4.0f, 2.5f}    // 따뜻한 흰빛
});
```

> **주의**: `emissive`가 있는 `GpuMaterial`은 **면발광** (간접 기여만),  
> `LightDesc`는 **NEE 직접 샘플링** 대상. 밝은 광원은 둘 다 설정하세요.

---

## 3. 새 씬 추가 — 단계별 절차

### Step 1 — `scene_desc.h`에 팩토리 선언 추가

```cpp
// scene_desc.h 하단
SceneDesc MakeCityScene();
SceneDesc MakeMyScene();   // ← 추가
```

### Step 2 — `scene_desc.cpp`에 팩토리 구현

```cpp
// scene_desc.cpp
SceneDesc MakeMyScene() {
    SceneDesc desc;

    // --- 재질 ---
    GpuMaterial matFloor{};
    matFloor.albedo    = glm::vec3(0.8f, 0.8f, 0.8f);
    matFloor.roughness = 0.9f;

    GpuMaterial matWall{};
    matWall.albedo    = glm::vec3(0.9f, 0.85f, 0.75f);
    matWall.roughness = 0.85f;

    GpuMaterial matLight{};
    matLight.albedo    = glm::vec3(1.0f, 1.0f, 0.9f);
    matLight.roughness = 1.0f;
    matLight.emissive  = glm::vec3(4.0f, 3.5f, 2.5f);

    // --- 지오메트리 ---
    // 바닥
    desc.boxes.push_back({{-5.0f, -0.1f, -5.0f}, {5.0f, 0.0f, 5.0f}, matFloor});
    // 벽
    desc.boxes.push_back({{-5.0f, 0.0f, -5.1f}, {5.0f, 4.0f, -5.0f}, matWall});
    // 광원 박스 (면발광)
    desc.boxes.push_back({{-0.3f, 3.9f, -0.3f}, {0.3f, 4.0f, 0.3f}, matLight});

    // --- NEE 광원 ---
    desc.lights.push_back({
        {0.0f, 3.95f, 0.0f},
        0.3f,
        {4.0f, 3.5f, 2.5f}
    });

    return desc;
}
```

### Step 3 — `context.cpp`에서 씬 교체

`BuildSceneBuffers()` 안의 폴백 분기에서 호출 함수를 바꿉니다.

```cpp
// context.cpp : BuildSceneBuffers() 내부 (~160번째 줄)
if (meshInfos.empty()) {
    // desc = MakeCityScene();  ← 기존
    desc = MakeMyScene();       // ← 교체
    FlattenScene(desc, allVertices, allIndices, meshInfos, materials);
}
```

이후 빌드하면 새 씬이 렌더링됩니다. 셰이더, BVH, GPU 버퍼 코드는 **수정 불필요**.

---

## 4. 샘플 씬: 코넬 박스 (Cornell Box)

```cpp
SceneDesc MakeCornellBox() {
    SceneDesc desc;

    // 재질
    GpuMaterial matWhite{};
    matWhite.albedo = glm::vec3(0.73f); matWhite.roughness = 1.0f;

    GpuMaterial matRed{};
    matRed.albedo = glm::vec3(0.65f, 0.05f, 0.05f); matRed.roughness = 1.0f;

    GpuMaterial matGreen{};
    matGreen.albedo = glm::vec3(0.12f, 0.45f, 0.15f); matGreen.roughness = 1.0f;

    GpuMaterial matLight{};
    matLight.albedo    = glm::vec3(1.0f);
    matLight.roughness = 1.0f;
    matLight.emissive  = glm::vec3(15.0f, 12.0f, 8.0f);

    GpuMaterial matMirror{};
    matMirror.albedo    = glm::vec3(0.8f);
    matMirror.roughness = 0.02f;
    matMirror.metallic  = 1.0f;

    const float S = 2.75f;  // 박스 반폭

    // 바닥, 천장, 뒷벽 (흰색)
    desc.quads.push_back({
        {-S, 0.0f,-S},{S, 0.0f,-S},{S, 0.0f, S},{-S, 0.0f, S},
        {0,1,0}, matWhite});
    desc.quads.push_back({
        {-S, S*2,  S},{S, S*2,  S},{S, S*2,-S},{-S, S*2,-S},
        {0,-1,0}, matWhite});
    desc.quads.push_back({
        {-S, 0.0f,-S},{-S, S*2,-S},{S, S*2,-S},{S, 0.0f,-S},
        {0,0,1}, matWhite});

    // 왼쪽 벽 (빨간색)
    desc.quads.push_back({
        {-S, 0.0f, S},{-S, 0.0f,-S},{-S, S*2,-S},{-S, S*2, S},
        {1,0,0}, matRed});

    // 오른쪽 벽 (초록색)
    desc.quads.push_back({
        {S, 0.0f,-S},{S, 0.0f, S},{S, S*2, S},{S, S*2,-S},
        {-1,0,0}, matGreen});

    // 천장 광원 패널
    desc.quads.push_back({
        {-0.6f, S*2-0.01f,-0.6f},
        { 0.6f, S*2-0.01f,-0.6f},
        { 0.6f, S*2-0.01f, 0.6f},
        {-0.6f, S*2-0.01f, 0.6f},
        {0,-1,0}, matLight});

    // 작은 상자 (흰색)
    desc.boxes.push_back({{-2.0f, 0.0f,-1.2f},{-0.7f, 1.65f, 0.3f}, matWhite});

    // 큰 거울 상자
    desc.boxes.push_back({{0.4f, 0.0f,-2.2f},{1.8f, 3.3f,-0.8f}, matMirror});

    // NEE 광원
    desc.lights.push_back({
        {0.0f, S*2 - 0.3f, 0.0f},
        0.6f,
        {15.0f, 12.0f, 8.0f}
    });

    return desc;
}
```

---

## 5. 샘플 씬: 단순 방 (Simple Room)

```cpp
SceneDesc MakeSimpleRoom() {
    SceneDesc desc;

    GpuMaterial matFloor{};
    matFloor.albedo = glm::vec3(0.60f, 0.50f, 0.35f);
    matFloor.roughness = 0.9f;

    GpuMaterial matCeiling{};
    matCeiling.albedo = glm::vec3(0.95f); matCeiling.roughness = 1.0f;

    GpuMaterial matWall{};
    matWall.albedo = glm::vec3(0.88f, 0.82f, 0.70f); matWall.roughness = 0.92f;

    GpuMaterial matTable{};
    matTable.albedo = glm::vec3(0.40f, 0.22f, 0.10f); matTable.roughness = 0.4f;

    GpuMaterial matBulb{};
    matBulb.albedo    = glm::vec3(1.0f, 0.95f, 0.8f);
    matBulb.roughness = 1.0f;
    matBulb.emissive  = glm::vec3(6.0f, 5.0f, 3.0f);

    // 방 (5m × 3m × 5m)
    desc.boxes.push_back({{-2.5f,-0.1f,-2.5f},{ 2.5f, 0.0f, 2.5f}, matFloor});
    desc.boxes.push_back({{-2.5f, 3.0f,-2.5f},{ 2.5f, 3.1f, 2.5f}, matCeiling});
    desc.boxes.push_back({{-2.6f, 0.0f,-2.5f},{-2.5f, 3.0f, 2.5f}, matWall}); // 왼벽
    desc.boxes.push_back({{ 2.5f, 0.0f,-2.5f},{ 2.6f, 3.0f, 2.5f}, matWall}); // 오른벽
    desc.boxes.push_back({{-2.5f, 0.0f,-2.6f},{ 2.5f, 3.0f,-2.5f}, matWall}); // 앞벽
    desc.boxes.push_back({{-2.5f, 0.0f, 2.5f},{ 2.5f, 3.0f, 2.6f}, matWall}); // 뒷벽

    // 테이블 (상판 + 다리 4개)
    desc.boxes.push_back({{-0.7f, 0.7f,-0.4f},{ 0.7f, 0.75f, 0.4f}, matTable}); // 상판
    desc.boxes.push_back({{-0.65f,0.0f,-0.35f},{-0.60f,0.7f,-0.30f}, matTable});
    desc.boxes.push_back({{ 0.60f,0.0f,-0.35f},{ 0.65f,0.7f,-0.30f}, matTable});
    desc.boxes.push_back({{-0.65f,0.0f, 0.30f},{-0.60f,0.7f, 0.35f}, matTable});
    desc.boxes.push_back({{ 0.60f,0.0f, 0.30f},{ 0.65f,0.7f, 0.35f}, matTable});

    // 천장 전구
    desc.boxes.push_back({{-0.08f,2.8f,-0.08f},{ 0.08f,2.95f, 0.08f}, matBulb});
    desc.lights.push_back({{0.0f, 2.88f, 0.0f}, 0.08f, {6.0f, 5.0f, 3.0f}});

    return desc;
}
```

---


## 6. GPU 버퍼 레지스터 (변경 금지)

```
t0  g_vertices   StructuredBuffer<ShaderVertex>
t1  g_indices    StructuredBuffer<uint>
t2  g_meshInfos  StructuredBuffer<ShaderMeshInfo>
t3  g_materials  StructuredBuffer<ShaderMaterial>
t4  g_bvhNodes   StructuredBuffer<ShaderBvhNode>
t5  g_bvhPrims   StructuredBuffer<ShaderBvhPrim>
t6  g_lights     StructuredBuffer<ShaderLight>
u0  g_accum      RWTexture2D<float4>   (HDR 누적)
u1  g_output     RWTexture2D<unorm float4> (LDR 출력)
b0  GlobalUB     cbuffer (카메라 파라미터 + lightCount)
```
