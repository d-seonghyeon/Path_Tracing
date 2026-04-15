# 변경 이력 — 2026-04-07

## 커밋 목록

| 해시 | 제목 |
|---|---|
| `2638ac6` | PT_Object_Loading: BVH, SceneDesc 구조 추가 및 context 리팩토링 |
| `f23adfb` | perf: thread group 16x16, invD 호이스팅, SAH bins 8→16 |
| `2f5a5bd` | build: Debug 빌드 가능하도록 런타임 라이브러리 통일 |
| `23d00a9` | fix: Scene.hlsli t1s 중복 선언 제거 |

---

## 1. BVH / SceneDesc 구조 추가 (`2638ac6`)

### 신규 파일
- `src/bvh.h / bvh.cpp` — CPU SAH-BVH 빌더
  - SAH 8-bin 분할, 스택 기반 Subdivide
  - `BvhNode` (32B), `BvhPrim` (16B) — GPU 레이아웃과 static_assert 일치
- `src/scene_desc.h / scene_desc.cpp` — 씬 기술자 (GpuMeshInfo, GpuMaterial, LightDesc)
- `SCENE_GUIDE.md` — 씬 구조 가이드 문서
- `SCENE_REFACTOR.md` — 리팩토링 계획 문서

### 수정 파일
- `src/context.cpp / context.h` — 구조 정리 및 코드 축소 (266줄 → 리팩토링)
- `shader/Scene.hlsli` — BVH 순회(스택 기반, O(log N)) 추가, NEE 광원 동적 읽기
- `shader/PathTracer.hlsl` — Russian Roulette, NEE 루프 수정
- `CMakeLists.txt` — bvh.cpp, scene_desc.cpp 소스 등록

### GPU 버퍼 레지스터 (확장)
```
t0  g_vertices   StructuredBuffer<ShaderVertex>
t1  g_indices    StructuredBuffer<uint>
t2  g_meshInfos  StructuredBuffer<ShaderMeshInfo>
t3  g_materials  StructuredBuffer<ShaderMaterial>
t4  g_bvhNodes   StructuredBuffer<ShaderBvhNode>   ← 신규
t5  g_bvhPrims   StructuredBuffer<ShaderBvhPrim>   ← 신규
t6  g_lights     StructuredBuffer<ShaderLight>      ← 신규
```

---

## 2. 1순위 성능 개선 (`f23adfb`)

| 파일 | 변경 | 효과 |
|---|---|---|
| `PathTracer.hlsl:102` | `[numthreads(8,8,1)]` → `[numthreads(16,16,1)]` | GPU occupancy 향상 (64→256 threads/group) |
| `Scene.hlsli:129` | `rayInvD` BVH 루프 밖으로 이동 | 노드당 나눗셈 3회 제거 |
| `Scene.hlsli:208` | `srInvD` 섀도우 루프 밖으로 이동 | NEE 섀도우 레이마다 동일 효과 |
| `bvh.cpp:6` | `SAH_BINS 8 → 16` | BVH 품질 향상 → 런타임 순회 노드 수 감소 |

**실측**: Release 빌드 기준 렌더링 속도 및 카메라 이동 개선 확인

---

## 3. Debug 빌드 환경 수정 (`2f5a5bd`)

### 원인
assimp가 `/MD`(Release 런타임) + `_DEBUG=1` 모순 설정으로 사전 빌드되어
Debug/Release 모두에서 링크 실패.

### 수정 내용

**`CMakeLists.txt`**
- `cmake_minimum_required` 3.10 → 3.15
- `cmake_policy(SET CMP0091 NEW)` 추가 — `CMAKE_MSVC_RUNTIME_LIBRARY` 정책 활성화
- `MultiThreadedDebugDLL` → `MultiThreadedDLL`
- `_ITERATOR_DEBUG_LEVEL=0` 컴파일 정의 추가

**`Dependency.cmake`**
- assimp: `CMAKE_BUILD_TYPE=Debug` + `MDd` → Release + MD 로 수정
- assimp/spdlog: `BUILD_COMMAND/INSTALL_COMMAND --config Release` 명시
  (VS 멀티-config 제너레이터는 `CMAKE_BUILD_TYPE` 무시하므로 필수)
- `DEP_LIBS`: `zlibstaticd` → `zlibstatic`

### 결과
- Debug 빌드(`Ctrl+F5`) 정상 동작
- Release 빌드 유지

---

## 4. 셰이더 버그 수정 (`23d00a9`)

- `Scene.hlsli:141` — `float3 t1s` 중복 선언 제거
  - invD 호이스팅 Edit에서 old 라인이 잔존하여 `X3003: redefinition of 't1s'` 컴파일 오류 발생

---

## 다음 작업 예정

### 2순위
- BVH 자식 정렬 (Child Ordering) — 순회 노드 수 20~30% 감소 예상
- Cosine-weighted Diffuse IS — diffuse 재질 분산 감소
- MIS (Multiple Importance Sampling) — NEE + BSDF 결합

### 3순위
- 텍스처 지원 (albedo map, normal map)
- 공간 디노이즈 (SVGF)
- ReSTIR DI
