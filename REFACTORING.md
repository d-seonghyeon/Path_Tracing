# PT_Object_Loading 리팩토링 보고서

**날짜**: 2026-04-02  
**대상 프로젝트**: `PT_Object_Loading` (DX11 GPU 패스 트레이서)  
**작업 범위**: 코드 품질 개선 + glTF 도입(Phase 1~2) + 씬 소재 개선

---

## 목차

1. [BuildSceneBuffers — reserve() 추가](#1-buildsceenbuffers--reserve-추가)
2. [OnResize — HRESULT 검사 추가](#2-onresize--hresult-검사-추가)
3. [model.cpp — Shininess→Roughness 변환 개선](#3-modelcpp--shininesssroughness-변환-개선)
4. [Dependency.cmake — tinygltf 의존성 추가](#4-dependencycmake--tinygltf-의존성-추가)
5. [CMakeLists.txt — gltf_loader 빌드 대상 추가](#5-cmakeliststxt--gltf_loader-빌드-대상-추가)
6. [GltfLoader 클래스 신규 구현](#6-gltfloader-클래스-신규-구현)
7. [씬 소재 개선 — 아스팔트 + 물웅덩이](#7-씬-소재-개선--아스팔트--물웅덩이)

---

## 1. BuildSceneBuffers — reserve() 추가

### 문제

`BuildSceneBuffers`에서 모델의 각 메시를 순회할 때 `push_back`을 반복 호출하고 있었습니다.  
STL 벡터는 용량을 초과할 때마다 내부적으로 힙 재할당 + 전체 복사를 수행하므로,  
메시 수가 많을수록 불필요한 메모리 할당이 반복됩니다.

### Before

```cpp
// context.cpp — BuildSceneBuffers
std::vector<Vertex>      allVertices;
std::vector<uint32_t>    allIndices;
std::vector<GpuMeshInfo> meshInfos;
std::vector<GpuMaterial> materials;

if (m_model) {
    for (const auto &mesh : m_model->GetMeshes()) {
        GpuMeshInfo info;
        info.vertexOffset  = (uint32_t)allVertices.size();
        info.indexOffset   = (uint32_t)allIndices.size();
        info.indexCount    = mesh->GetIndexCount();
        info.materialIndex = (uint32_t)materials.size();
        meshInfos.push_back(info);

        // ...재질 push_back...

        uint32_t vOffset = (uint32_t)allVertices.size(); // info.vertexOffset과 중복
        for (const auto &v : mesh->GetVertices())
            allVertices.push_back(v);          // ← 반복 재할당 위험

        for (const auto &idx : mesh->GetIndices())
            allIndices.push_back(vOffset + idx);
    }
}
```

### After

```cpp
if (m_model) {
    // 전체 크기를 먼저 계산해 한 번만 할당
    size_t totalVerts = 0, totalInds = 0;
    for (const auto& m : m_model->GetMeshes()) {
        totalVerts += m->GetVertexCount();
        totalInds  += m->GetIndexCount();
    }
    allVertices.reserve(totalVerts);
    allIndices.reserve(totalInds);
    meshInfos.reserve(m_model->GetMeshes().size());
    materials.reserve(m_model->GetMeshes().size());

    for (const auto &mesh : m_model->GetMeshes()) {
        GpuMeshInfo info;
        info.vertexOffset  = (uint32_t)allVertices.size();
        info.indexOffset   = (uint32_t)allIndices.size();
        info.indexCount    = mesh->GetIndexCount();
        info.materialIndex = (uint32_t)materials.size();
        meshInfos.push_back(info);

        // ...재질 push_back...

        uint32_t vOffset = info.vertexOffset; // 중복 계산 제거
        const auto& verts = mesh->GetVertices();
        allVertices.insert(allVertices.end(), verts.begin(), verts.end()); // 배치 복사

        for (uint32_t idx : mesh->GetIndices())
            allIndices.push_back(vOffset + idx);
    }
}
```

### 개선 효과

| 항목 | Before | After |
|------|--------|-------|
| 힙 재할당 횟수 | O(log n) 회 | 0회 (1회 선할당) |
| 버텍스 복사 방식 | 개별 `push_back` | `insert(end, …)` 배치 복사 |
| 코드 명확성 | `vOffset` 중복 계산 | `info.vertexOffset` 재사용 |

---

## 2. OnResize — HRESULT 검사 추가

### 문제

`OnResize`에서 DX11 리소스 6개(`CreateTexture2D`, `CreateUnorderedAccessView`, `CreateShaderResourceView` × 2벌)를 생성하면서  
반환값을 전혀 확인하지 않았습니다. 실패 시 nullptr 상태로 이후 렌더링이 진행되어 크래시 원인 파악이 어렵습니다.

### Before

```cpp
// context.cpp — OnResize
device->CreateTexture2D(&desc, nullptr,
                        m_outputTexture.ReleaseAndGetAddressOf());
device->CreateUnorderedAccessView(m_outputTexture.Get(), nullptr,
                                  m_outputUAV.ReleaseAndGetAddressOf());
device->CreateShaderResourceView(m_outputTexture.Get(), nullptr,
                                 m_outputSRV.ReleaseAndGetAddressOf());

desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
device->CreateTexture2D(&desc, nullptr,
                        m_accumTexture.ReleaseAndGetAddressOf());
device->CreateUnorderedAccessView(m_accumTexture.Get(), nullptr,
                                  m_accumUAV.ReleaseAndGetAddressOf());
device->CreateShaderResourceView(m_accumTexture.Get(), nullptr,
                                 m_accumSRV.ReleaseAndGetAddressOf());
```

### After

```cpp
HRESULT hr = device->CreateTexture2D(&desc, nullptr,
                                     m_outputTexture.ReleaseAndGetAddressOf());
if (FAILED(hr)) {
    SPDLOG_ERROR("Failed to create output texture. HRESULT: 0x{:08x}", (uint32_t)hr);
    return;
}
hr = device->CreateUnorderedAccessView(m_outputTexture.Get(), nullptr,
                                       m_outputUAV.ReleaseAndGetAddressOf());
if (FAILED(hr)) {
    SPDLOG_ERROR("Failed to create output UAV. HRESULT: 0x{:08x}", (uint32_t)hr);
    return;
}
hr = device->CreateShaderResourceView(m_outputTexture.Get(), nullptr,
                                      m_outputSRV.ReleaseAndGetAddressOf());
if (FAILED(hr)) {
    SPDLOG_ERROR("Failed to create output SRV. HRESULT: 0x{:08x}", (uint32_t)hr);
    return;
}
// HDR 누적 버퍼도 동일하게 3회 검사...
```

### 개선 효과

- DX11 리소스 생성 실패 시 HRESULT 코드와 함께 즉시 에러 로그 출력
- 잘못된 상태로 렌더링이 진행되지 않도록 조기 종료
- CLAUDE.md에 명시된 `FAILED(hr) → SPDLOG_ERROR + return` 패턴 준수

---

## 3. model.cpp — Shininess→Roughness 변환 개선

### 문제

Assimp가 OBJ/MTL의 `Ns`(Shininess) 값을 읽어올 때 이를 PBR roughness로 변환하는 과정에서  
단순 선형 매핑을 사용하고 있었습니다. 이는 Blinn-Phong과 GGX 분포의 물리적 관계를 반영하지 못합니다.

### 이론적 배경

Blinn-Phong specular exponent *n* 과 GGX roughness *α* 의 관계:

```
α ≈ sqrt(2 / (n + 2))
```

이 식은 Blinn-Phong NDF와 GGX NDF의 분포가 통계적으로 동등한 조건에서 유도됩니다.  
*n* = 0 → *α* = 1.0 (완전 난반사), *n* = ∞ → *α* → 0 (완전 경면반사).

### Before

```cpp
// model.cpp — ProcessMesh
float shininess = 32.0f;
material->Get(AI_MATKEY_SHININESS, shininess);

// 선형 매핑: 물리적 근거 없음
shininess = glm::clamp(shininess, 0.0f, 1000.0f);
matData.roughness = glm::clamp(1.0f - (shininess / 1000.0f), 0.05f, 1.0f);
```

### After

```cpp
float shininess = 32.0f;
material->Get(AI_MATKEY_SHININESS, shininess);

// Blinn-Phong → GGX perceptual 변환: α ≈ sqrt(2/(n+2))
shininess         = glm::max(shininess, 0.0f);
matData.roughness = glm::clamp(sqrtf(2.0f / (shininess + 2.0f)), 0.05f, 1.0f);
```

### 변환 값 비교

| Shininess (n) | 이전 (선형) | 이후 (sqrt) | 실제 체감 |
|:---:|:---:|:---:|:---|
| 0 | 1.00 | 1.00 | 완전 난반사 — 동일 |
| 32 | 0.97 | 0.24 | 중간 광택 — 훨씬 그럴듯함 |
| 100 | 0.90 | 0.14 | 고광택 금속 |
| 500 | 0.50 | 0.06 | 거울에 가까운 표면 |
| 1000 | 0.05 | 0.05 (clamp) | 완전 경면 |

선형 변환은 중간 Shininess 값에서 roughness를 과도하게 크게 추정하는 경향이 있습니다.

---

## 4. Dependency.cmake — tinygltf 의존성 추가

### 목적

glTF/GLB 포맷 로딩을 위해 헤더 전용 라이브러리 **tinygltf v2.8.21**을 추가합니다.  
tinygltf는 빌드가 필요 없고 `tiny_gltf.h` 단일 파일로 구성되므로 ExternalProject로 헤더만 복사합니다.

### Before

```cmake
# Dependency.cmake — 기존 마지막 줄
set(DEP_LIBS ${DEP_LIBS} assimp-vc143-mt zlibstaticd)
set(DEP_LIST ${DEP_LIST} dep_assimp)

add_dependencies(${PROJECT_NAME} ${DEP_LIST})
```

### After

```cmake
set(DEP_LIBS ${DEP_LIBS} assimp-vc143-mt zlibstaticd)
set(DEP_LIST ${DEP_LIST} dep_assimp)

# 5. tinygltf (헤더 전용)
ExternalProject_Add(
    dep_tinygltf
    GIT_REPOSITORY "https://github.com/syoyo/tinygltf.git"
    GIT_TAG        "v2.8.21"
    GIT_SHALLOW    1
    UPDATE_DISCONNECTED 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ""
    TEST_COMMAND      ""
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E copy
        ${PROJECT_BINARY_DIR}/dep_tinygltf-prefix/src/dep_tinygltf/tiny_gltf.h
        ${DEP_INSTALL_DIR}/include/tiny_gltf.h
        COMMAND ${CMAKE_COMMAND} -E copy
        ${PROJECT_BINARY_DIR}/dep_tinygltf-prefix/src/dep_tinygltf/json.hpp
        ${DEP_INSTALL_DIR}/include/json.hpp  # tinygltf 내부 의존성
)
set(DEP_LIST ${DEP_LIST} dep_tinygltf)

add_dependencies(${PROJECT_NAME} ${DEP_LIST})
```

### 주의 사항

`tinygltf`는 내부적으로 `json.hpp`(nlohmann/json)와 `stb_image.h`를 사용합니다.  
`stb_image.h`는 이미 `image.cpp`에서 `STB_IMAGE_IMPLEMENTATION`으로 구현체가 생성되므로,  
gltf_loader.cpp에서 `TINYGLTF_NO_STB_IMAGE` 플래그로 중복 정의를 방지했습니다.

---

## 5. CMakeLists.txt — gltf_loader 빌드 대상 추가

### Before

```cmake
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/buffer.h           src/buffer.cpp
    src/common.h
    src/context.h          src/context.cpp
    src/compute_program.h  src/compute_program.cpp
    src/shader.h           src/shader.cpp
    src/image.h            src/image.cpp
    src/texture.h          src/texture.cpp
    src/mesh.h             src/mesh.cpp
    src/model.h            src/model.cpp
    ${SHADER_SOURCES}
)
```

### After

```cmake
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/buffer.h           src/buffer.cpp
    src/common.h
    src/context.h          src/context.cpp
    src/compute_program.h  src/compute_program.cpp
    src/shader.h           src/shader.cpp
    src/image.h            src/image.cpp
    src/texture.h          src/texture.cpp
    src/mesh.h             src/mesh.cpp
    src/model.h            src/model.cpp
    src/gltf_loader.h      src/gltf_loader.cpp   # [추가]
    ${SHADER_SOURCES}
)
```

---

## 6. GltfLoader 클래스 신규 구현

### 설계 원칙

- **인터페이스 호환**: `Model::Load`와 동일하게 `GetMeshes()` 반환 → `BuildSceneBuffers` 수정 없이 교체 가능
- **헤더 오염 방지**: `tiny_gltf.h`(대형 헤더)를 `.cpp`에서만 include
- **데이터 복사 최소화**: accessor → bufferView → buffer 직접 포인터 접근

### gltf_loader.h

```cpp
#ifndef __GLTF_LOADER_H__
#define __GLTF_LOADER_H__
#include "common.h"
#include "mesh.h"

CLASS_PTR(GltfLoader)
class GltfLoader {
public:
    // Model::Load와 동일한 시그니처
    static GltfLoaderUPtr Load(ID3D11Device* device, const std::string& filepath);
    const std::vector<MeshUPtr>& GetMeshes() const { return m_meshes; }

private:
    GltfLoader() {}
    bool LoadGltf(ID3D11Device* device, const std::string& filepath);
    std::vector<MeshUPtr> m_meshes;
};
#endif
```

### gltf_loader.cpp — 주요 구조

```cpp
// 구현체는 이 파일에서만 생성
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE        // image.cpp와 중복 방지
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>
#include "gltf_loader.h"

namespace {

// ── 1. accessor 직접 포인터 접근 ──────────────────────────
template<typename T>
const T* AccessorPtr(const tinygltf::Model& model, int idx) {
    const auto& acc  = model.accessors[idx];
    const auto& view = model.bufferViews[acc.bufferView];
    const auto& buf  = model.buffers[view.buffer];
    return reinterpret_cast<const T*>(
        buf.data.data() + view.byteOffset + acc.byteOffset);
}

// ── 2. 재질 추출 ──────────────────────────────────────────
MaterialData ExtractMaterial(const tinygltf::Model& model, int matIndex) {
    MaterialData mat;
    if (matIndex < 0) return mat;
    const auto& pbr = model.materials[matIndex].pbrMetallicRoughness;
    mat.albedo    = glm::vec3(pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2]);
    mat.metallic  = (float)pbr.metallicFactor;
    mat.roughness = (float)pbr.roughnessFactor;
    const auto& src = model.materials[matIndex];
    mat.emissive  = glm::vec3(src.emissiveFactor[0], src.emissiveFactor[1], src.emissiveFactor[2]);
    return mat;
}

// ── 3. 인덱스 추출 (uint8/16/32 → uint32) ─────────────────
bool ExtractIndices(const tinygltf::Model& model, int idx, std::vector<uint32_t>& out) {
    const auto& acc = model.accessors[idx];
    switch (acc.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { /* uint16 → uint32 */ break; }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   { /* uint32 직접 복사 */ break; }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  { /* uint8  → uint32 */ break; }
    default: return false;
    }
    return true;
}

// ── 4. Primitive 처리 ─────────────────────────────────────
MeshUPtr ProcessPrimitive(ID3D11Device* device,
                           const tinygltf::Model& model,
                           const tinygltf::Primitive& prim)
{
    // POSITION → NORMAL → TEXCOORD_0 → TANGENT 순서로 Vertex 합성
    // 인덱스 없는 primitive는 순서형 인덱스 자동 생성
    // Mesh::Create → mesh->SetMaterial 순으로 처리
}

} // namespace

// ── 공개 인터페이스 ────────────────────────────────────────
bool GltfLoader::LoadGltf(ID3D11Device* device, const std::string& filepath) {
    tinygltf::TinyGLTF loader;
    // .glb / .gltf 자동 구분
    bool ok = isBinary
        ? loader.LoadBinaryFromFile(...)
        : loader.LoadASCIIFromFile(...);
    // 삼각형 primitive만 처리 (prim.mode == TINYGLTF_MODE_TRIANGLES)
}
```

### 데이터 흐름

```
.gltf / .glb
    └─ TinyGLTF::LoadASCIIFromFile / LoadBinaryFromFile
         └─ tinygltf::Model
              ├─ mesh[].primitive[]
              │    ├─ attributes["POSITION"]  → accessor → float3[]
              │    ├─ attributes["NORMAL"]    → accessor → float3[]
              │    ├─ attributes["TEXCOORD_0"]→ accessor → float2[]
              │    ├─ attributes["TANGENT"]   → accessor → float4[] (w=handedness)
              │    └─ indices                 → accessor → uint32[]
              └─ materials[].pbrMetallicRoughness → MaterialData
                   ├─ baseColorFactor  → albedo
                   ├─ metallicFactor   → metallic
                   ├─ roughnessFactor  → roughness
                   └─ emissiveFactor   → emissive
```

### Phase 3 사용 방법 (미완성, 향후 전환 시)

```cpp
// context.h에 추가
#include "gltf_loader.h"

// context.cpp — Init() 내부
// 기존: m_model = Model::Load(device, "model/backpack.obj");
// 변경:
m_model = GltfLoader::Load(device, "model/scene.glb");
// BuildSceneBuffers는 GetMeshes() 인터페이스가 동일하므로 수정 없음
```

---

## 7. 씬 소재 개선 — 아스팔트 + 물웅덩이

### 7-1. 아스팔트 재질

도로 표면을 기존의 단순한 어두운 회색에서 실제 아스팔트에 가깝게 조정했습니다.

#### Before

```cpp
GpuMaterial matRoad{};
matRoad.albedo    = glm::vec3(0.20f, 0.20f, 0.20f); // 밝은 회색
matRoad.roughness = 0.95f;
```

#### After

```cpp
// 아스팔트: 더 어둡고 거친 표면, 약간의 갈색 기운
GpuMaterial matRoad{};
matRoad.albedo    = glm::vec3(0.12f, 0.11f, 0.10f); // 어두운 갈색 회색
matRoad.roughness = 0.97f;
```

| 속성 | Before | After | 의도 |
|------|--------|-------|------|
| albedo R | 0.20 | 0.12 | 아스팔트는 매우 어두움 |
| albedo G | 0.20 | 0.11 | 약간의 갈색 기운 |
| albedo B | 0.20 | 0.10 | 청색 성분 억제 |
| roughness | 0.95 | 0.97 | 타르의 거친 표면 |

### 7-2. 물웅덩이

도로 위에 반사성 물웅덩이를 쿼드로 추가했습니다.  
패스 트레이서의 GGX BRDF가 roughness ≈ 0에 가까울수록 거울 반사를 시뮬레이션합니다.

#### 재질

```cpp
// 물웅덩이: 거의 완전 반사 + 어두운 청색
GpuMaterial matPuddle{};
matPuddle.albedo    = glm::vec3(0.03f, 0.04f, 0.06f); // 아주 어두운 청색
matPuddle.roughness = 0.02f;  // 거울에 가까운 반사
matPuddle.metallic  = 0.0f;   // 물은 비전도체
```

#### 배치

```cpp
// 도로 중앙 (x: -1.5~1.5, z: 5~9m), 도로면 위 0.5mm
addWin(
    {-1.5f, 0.0005f,  5.0f},
    { 1.5f, 0.0005f,  5.0f},
    { 1.5f, 0.0005f,  9.0f},
    {-1.5f, 0.0005f,  9.0f},
    {0.0f, 1.0f, 0.0f},   // 법선: 위쪽
    matPuddle
);
```

**y = 0.0005f** (0.5mm 띄우기): 도로 박스의 상단면(y=0)과 완전히 겹치면  
두 삼각형이 동일한 깊이를 가져 Z-fighting이 발생합니다. 미세하게 띄워 해결합니다.

#### 렌더링 원리

물웅덩이의 낮은 roughness(`0.02`)는 GGX NDF를 매우 좁게 만들어  
반사 방향 주변의 좁은 고체각에만 빛을 집중시킵니다.  
프레임이 누적될수록(progressive accumulation) 가로등과 창문 빛이  
물웅덩이에 또렷하게 반사되는 것을 확인할 수 있습니다.

---

## 빌드 및 실행

```bash
# 빌드 디렉터리에서
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
./Release/PT_Object_Loading.exe
```

> **Debug 빌드 불가 원인**: dep_assimp가 Release 런타임(`/MD`)으로 컴파일되어 있어  
> Debug(`/MDd`)와 런타임 라이브러리 불일치(LNK2038) 발생. Release 빌드로 우회.  
> 근본 해결책: dep_assimp를 `cmake --build . --config Debug`로 재빌드하거나  
> Dependency.cmake의 `-DCMAKE_BUILD_TYPE`을 제거하고 상위 빌드 구성을 따르도록 수정.

---

## 전체 변경 파일 목록

| 파일 | 변경 유형 | 내용 |
|------|-----------|------|
| `src/context.cpp` | 수정 | reserve(), HRESULT 검사, 아스팔트 재질, 물웅덩이 |
| `src/model.cpp` | 수정 | Shininess→Roughness sqrt 변환 |
| `Dependency.cmake` | 수정 | tinygltf + json.hpp 추가 |
| `CMakeLists.txt` | 수정 | gltf_loader 빌드 대상 추가 |
| `src/gltf_loader.h` | **신규** | GltfLoader 클래스 선언 |
| `src/gltf_loader.cpp` | **신규** | GltfLoader 구현 (tinygltf 기반) |
