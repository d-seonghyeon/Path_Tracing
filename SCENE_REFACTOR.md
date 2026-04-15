# 씬 구조 리팩토링 변경 기록

## 목적

맵(구조물)을 추가할 때 수정해야 하는 파일과 코드를 최소화하기 위한 구조 분리.

---

## 변경 전 구조

```
context.cpp
└── BuildCityGeometry()         ← 250줄, 재질/도로/건물/창문/가로등 전부 혼재
    ├── addBox() 람다            ← 지오메트리 생성과 씬 정의가 뒤섞임
    └── addWin() 람다

Scene.hlsli
└── GetLight(int idx)           ← 광원 위치 if/else 하드코딩
    static const int NUM_LIGHTS = 5
```

**문제점**
- 구조물 추가 = `BuildCityGeometry()` 250줄 함수 내부 수정
- 광원 추가 = C++ 수정 + 셰이더 `GetLight()` 수정 + `NUM_LIGHTS` 상수 수정 (2곳 동시)

---

## 변경 후 구조

```
scene_desc.h                    ← GpuMaterial, GpuMeshInfo, SceneDesc 정의
scene_desc.cpp
└── MakeCityScene()             ← 순수 데이터만 (지오메트리 생성 없음)

context.cpp
└── FlattenScene()              ← SceneDesc → GPU 버퍼 배열 변환
└── BuildSceneBuffers()         ← MakeCityScene() → FlattenScene() 호출

Scene.hlsli
└── StructuredBuffer<ShaderLight> g_lights : register(t6)  ← GPU 버퍼
    (GetLight() 하드코딩 제거)
```

**결과**
- 구조물 추가 = `MakeCityScene()` 에 `desc.boxes.push_back()` 한 줄
- 광원 추가 = `MakeCityScene()` 에 `desc.lights.push_back()` 한 줄 (셰이더 수정 불필요)

---

## 파일별 변경 내역

### 신규 생성

#### `src/scene_desc.h`
- `GpuMaterial` 구조체 (기존 `context.h`에서 이동)
- `GpuMeshInfo` 구조체 (기존 `context.h`에서 이동)
- `BoxDesc` — `lo`, `hi`, `mat` 필드
- `QuadDesc` — `p0~p3`, `n`, `mat` 필드
- `LightDesc` — `center`, `radius`, `emission` 필드
- `SceneDesc` — `boxes`, `quads`, `lights` 벡터
- `MakeCityScene()` 선언

#### `src/scene_desc.cpp`
- `MakeCityScene()` 구현
- 재질 정의 → `desc.boxes` / `desc.quads` / `desc.lights` 채우기만 수행
- 지오메트리 생성(삼각형 분할 등) 없음

---

### 수정

#### `src/context.h`
- `GpuMaterial`, `GpuMeshInfo` 정의 제거 → `scene_desc.h` include로 대체
- `GlobalUniforms::_pad` → `lightCount` (uint32_t) 로 변경
- 멤버 추가: `m_lightBuffer`, `m_lightSRV`, `m_lightCount`

#### `src/context.cpp`
- `BuildCityGeometry()` 함수 제거
- `FlattenScene(const SceneDesc&, ...)` 추가 (anonymous namespace)
  - `desc.boxes` → `AppendBox()` → `GpuMeshInfo` + `GpuMaterial` 배열
  - `desc.quads` → `AppendQuad()` → 동일
- `BuildSceneBuffers()` 변경
  - `BuildCityGeometry()` 호출 → `MakeCityScene()` + `FlattenScene()` 호출
  - `desc.lights` 로 `m_lightBuffer` (t6) 생성
- `Render()` 변경
  - `globalData.lightCount` 전달
  - SRV 배열 6개 → 7개 (t6: `m_lightSRV` 추가)

#### `shader/PathTracer.hlsl`
- `cbuffer GlobalUB` 를 `#include` 보다 앞으로 이동
  - 이유: `Scene.hlsli` 내부 함수에서 `g_lightCount` 참조 가능하려면 선언이 먼저여야 함
- `g_pad` (float) → `g_lightCount` (uint)
- NEE 루프: `NUM_LIGHTS` → `(int)g_lightCount`

#### `shader/Scene.hlsli`
- 제거: `SphereLight` 구조체, `NUM_LIGHTS` 상수, `GetLight()` 함수
- 추가: `ShaderLight` 구조체 + `StructuredBuffer<ShaderLight> g_lights : register(t6)`
- `SceneIntersect()` — 구형 광원 루프를 `g_lights[li]` + `g_lightCount` 기반으로 변경
- `SampleDirectLight()` — `GetLight(lightIdx)` → `g_lights[lightIdx]`

#### `CMakeLists.txt`
- `src/scene_desc.h`, `src/scene_desc.cpp` 추가

---

## GPU 레지스터 바인딩 (최종)

| 슬롯 | 타입 | 내용 |
|------|------|------|
| `t0` | `StructuredBuffer<ShaderVertex>` | 전체 버텍스 |
| `t1` | `StructuredBuffer<uint>` | 전체 인덱스 |
| `t2` | `StructuredBuffer<ShaderMeshInfo>` | 메시 오프셋/카운트 |
| `t3` | `StructuredBuffer<ShaderMaterial>` | 메시 재질 |
| `t4` | `StructuredBuffer<ShaderBvhNode>` | BVH 노드 |
| `t5` | `StructuredBuffer<ShaderBvhPrim>` | BVH 프리미티브 |
| `t6` | `StructuredBuffer<ShaderLight>` | NEE 광원 (신규) |
| `u0` | `RWTexture2D<float4>` | HDR 누적 버퍼 |
| `u1` | `RWTexture2D<unorm float4>` | LDR 출력 |
| `b0` | `cbuffer GlobalUB` | 카메라 + `lightCount` |

---

## 구조물 추가 방법 (변경 후)

`src/scene_desc.cpp` 의 `MakeCityScene()` 만 수정합니다.

```cpp
// 새 건물
desc.boxes.push_back({{20.f, 0.f, 0.f}, {28.f, 15.f, 10.f}, matBrick});

// 새 쿼드 (바닥, 창문 등)
desc.quads.push_back({{...}, {...}, {...}, {...}, {0,1,0}, matPuddle});

// 새 광원 (셰이더 수정 없이 즉시 반영)
desc.lights.push_back({{5.f, 8.f, 15.f}, 0.8f, {4.f, 3.f, 1.5f}});
```

`FlattenScene()`, BVH 빌드, GPU 버퍼 업로드는 `BuildSceneBuffers()` 가 자동 처리합니다.
