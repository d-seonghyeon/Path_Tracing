# AGENTS.md — Path Tracing (DX11 CS) 프로젝트 협업 규약

> **대상**: 이 리포지토리에서 일하는 모든 에이전트 (Codex, Claude Code, 기타 LLM 툴).
> **목적**: 두 개 이상의 툴이 세션을 넘나들며 작업해도 **코드 스타일과 아키텍처 결정이 분기(diverge)하지 않도록** 한다.
> **우선순위**: 이 문서 > 사용자의 세션 내 즉흥 지시 > 모델 기본값.
> 세션 시작 시 반드시 `STATUS.md` → `AGENTS.md` 순서로 읽는다.

---

## 1. 프로젝트 개요 (한 문단)

DirectX 11 compute-shader 기반 path tracer. 씬은 glTF/OBJ 로드 → SAH 16-bin BVH → CS 로 경로추적. NEE + MIS (Power Heuristic), GGX + 코사인 가중 로브 선택, RR (bounce ≥ 1). 출력은 HDR 누적 버퍼에 매 프레임 합산되고 ACES 톤맵 → R8G8B8A8 LDR 로 `Present`. 현재 목표는 **NVIDIA NRD (ReBLUR_DIFFUSE_SPECULAR) 통합**으로, 매 프레임 누적 모델을 per-frame 모델 + temporal denoise 로 교체하는 것.

---

## 2. 빌드 / 실행

- **제너레이터**: Visual Studio 2022 (MSVC v143), x64, Debug.
- **설정**: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
- **빌드**: `cmake --build build --config Debug --target ALL_BUILD -j 16`
- **런타임 라이브러리**: `MultiThreadedDebugDLL` + `_ITERATOR_DEBUG_LEVEL=2` (모든 외부 의존성도 동일해야 함 — `Dependency.cmake` 참조).
- **셸 인코딩**: `/utf-8` (MSVC), 소스는 UTF-8 (BOM 없음).
- **의존성**: `ExternalProject_Add` 로 `build/` 하위에 설치. 손대지 않는다. 강제 재빌드 시 폴더 전체 삭제.
- 셰이더는 빌드 시 `${CMAKE_BINARY_DIR}/shader/` 로 자동 복사된다. 런타임은 이 경로에서 읽음.

### assimp PDB 버그 기록 (해결됨)

- assimp 5.3.1 + `BUILD_SHARED_LIBS=OFF` + Debug 조합에서 존재하지 않는 linker PDB 를 install 하려다 실패.
- 수정: `Dependency.cmake` 의 `dep_assimp` 에 `-DASSIMP_INSTALL_PDB=OFF` 를 추가.
- **절대 되돌리지 말 것.**

---

## 3. 파일·네이밍 규약

| 대상 | 규칙 | 예 |
| --- | --- | --- |
| 소스 파일 | snake_case `.h` / `.cpp` | `nrd_denoiser.cpp` |
| 셰이더 파일 | PascalCase `.hlsl` / `.hlsli` | `PathTracer.hlsl`, `Utility.hlsli` |
| 클래스 | PascalCase + `CLASS_PTR(Name)` 매크로 사용 | `class NrdDenoiser` |
| 멤버 변수 | `m_` 접두사 + camelCase | `m_outputTexture`, `m_frameCount` |
| 전역/상수 | `g_` 접두사 (셰이더/C++ 공통) | `g_cameraPos`, `g_lightCount` |
| HLSL cbuffer | PascalCase + `UB` 접미사 | `GlobalUB`, `ToneMapCB` |
| 상수 | `SCREAMING_SNAKE` 또는 `static const` | `MAX_BOUNCES` |
| 함수 (C++) | PascalCase, 반환 위치는 다음 줄 금지 | `bool Init(...)` |
| 함수 (HLSL) | PascalCase | `GenerateCameraRay`, `TracePath` |

- 헤더가드는 `#ifndef __FOO_H__ / #define __FOO_H__ / #endif` 형식 유지. (기존 파일이 전부 이 스타일.)
- include 순서: (1) 같은 모듈 `.h` → (2) 프로젝트 다른 헤더 → (3) 서드파티 → (4) STL. 공백 줄로 분리.
- 한글 주석 OK. 단, 공개 API (`.h`) 에는 **영어 또는 한·영 병기**.

---

## 4. DX11 리소스 바인딩 맵 (현재 스냅샷)

> NRD 통합 중 슬롯이 바뀌면 아래 표 + `STATUS.md §3` 동시 갱신.

### PathTracer (CS)

| 바인딩 | 리소스 | 설명 |
| --- | --- | --- |
| `b0` | `GlobalUB` | camera, fov, frameCount, lightCount (+ 추후 prev/curr viewProj) |
| `t0` | `g_vertices` (SRV, structured) | |
| `t1` | `g_indices` (SRV, raw/structured) | |
| `t2` | `g_meshInfos` (SRV) | |
| `t3` | `g_materials` (SRV) | |
| `t4` | `g_bvhNodes` (SRV) | |
| `t5` | `g_bvhPrims` (SRV) | |
| `t6` | `g_lights` (SRV) | |
| `u0` | `g_accum` (RWTex2D float4) | **Phase 0 에서 per-frame 출력으로 전환 예정** |

### ToneMap (CS)

| 바인딩 | 리소스 |
| --- | --- |
| `b0` | `ToneMapCB` (frameCount) |
| `t10` | `g_hdrInput` (Composite 결과로 교체 예정) |
| `u1` | `g_ldrOutput` (unorm R8G8B8A8) |

### Phase 0 목표 G-buffer (PathTracer 추가 UAV)

| 슬롯 | 이름 | 포맷 | 의미 |
| --- | --- | --- | --- |
| `u0` | `g_diffuseRadiance` | `R16G16B16A16_FLOAT` | .rgb = diffuse, .a = hitT |
| `u1` | `g_specularRadiance` | `R16G16B16A16_FLOAT` | .rgb = specular, .a = hitT |
| `u2` | `g_viewZ` | `R32_FLOAT` | linear view-space Z (양수, 전방) |
| `u3` | `g_normalRoughness` | `R10G10B10A2_UNORM` | octa-packed N + roughness |
| `u4` | `g_motionVector` | `R16G16_FLOAT` | 픽셀 단위 (prev − curr) |
| `u5` | `g_baseColorMetalness` | `R8G8B8A8_UNORM` | .rgb=albedo, .a=metalness |
| `u6` | `g_emissive` | `R11G11B10_FLOAT` | composite 단계에서 재사용 |

> **금지**: 이 맵을 말 없이 바꾸지 않는다. 바꿀 일이 있으면 `STATUS.md §3` 에 옛→새 매핑을 먼저 남기고 커밋한다.

---

## 5. 좌표계·수학적 결정

- **좌표계**: **Right-Handed**, `+Y` up, `-Z` forward (`glm` 기본).
- **행렬**: HLSL 에서 `row_major` 로 사용. C++ 측은 `glm` 기본(column-major) → GPU 업로드 시 **전치 없이** 그대로 올리면 `row_major` 어노테이션과 일치하지 않음. → 현재 `common.hlsli` 는 `row_major matrix model/view/projection` 선언. **업로드 시 전치하거나 HLSL 선언을 `column_major` 로 통일 — 이 결정은 Phase 0 중 Codex 가 확인 후 `STATUS.md §3` 에 기록한다.**
- **ViewZ 부호**: **양수 = 전방** (NRD 요구). `viewZ = length((viewPos - worldPos))` 의 단순 z 성분이 아닌 **선형 양수 거리에 가까운 값**. Camera-space z 에서 `-z.viewSpace` 로 얻는다 (RH 기준).
- **Motion vector**: `(prevPixelUV - currPixelUV) * screenSize`, **픽셀 단위**, world-space 가 아님. NRD 초기화 시 `isMotionVectorInWorldSpace=false`.
- **Octa 인코딩**: NRD 권장 octahedral 함수. helper 는 NRD 셰이더의 `NRD.hlsli` 내부 함수를 **C++ 측에서 재정의하지 않고** HLSL 에서만 사용.

---

## 6. 에러 처리·스타일

- `HRESULT` 는 매 호출 후 실패 시 `spdlog::error(...)` + early return.
- 래퍼 매크로 `DX_CHECK(...)` 가 이미 있다면 그것을 쓴다. 없으면 **추가하지 말고** 기존 패턴(`if (FAILED(hr)) { return false; }`) 을 따른다.
- 예외 throw 금지 (Windows/DX 경로).
- `assert` 대신 `SPDLOG_ERROR` + 조건부 반환. Release 빌드 차이에 의존하지 않는다.
- NaN/Inf 방어는 셰이더 측: `!any(isnan(x)) && !any(isinf(x))` — 기존 패턴 유지.

---

## 7. NRD 통합 — 고정된 결정

다음은 "재검토 금지" 항목. 다시 제안하려거든 사용자에게 먼저 물어본다.

1. **NRI 는 도입하지 않는다.** NRD 의 DXBC embed (`NRD_EMBEDS_DXBC_SHADERS=ON`, `NRD_USE_PRECOMPILED_SHADERS=ON`) 로 얻은 바이트코드를 `ID3D11Device::CreateComputeShader` 로 직접 생성해 파이프라인을 구성한다.
2. **첫 디노이저**: `REBLUR_DIFFUSE_SPECULAR`. SIGMA (그림자) · ReLAX · REFERENCE 는 Phase 3 옵션.
3. **누적 모델 폐기**: `g_accum += ...` 방식은 Phase 0 에서 **per-frame overwrite** 로 완전히 교체. ToneMap 의 `/(frameCount+1)` 도 동시에 제거.
4. **Composite 는 별도 CS 패스**. Denoise 후 `diffuse * baseColor + specular + emissive` 를 합성한다.
5. **리사이즈·카메라 텔레포트 시** NRD `AccumulationMode::CLEAR_AND_RESTART` 를 호출. `OnResize` 와 "카메라 점프 감지" 분기에 연결.
6. **셰이더 리소스 수명**: NRD transient pool 은 매 프레임 재사용 가능 (해제 금지). permanent pool 은 리사이즈 시에만 재생성.
7. **NRD 버전 고정**: `v4.13.3`. 버전업은 별도 PR 로만 한다.

---

## 8. 하면 안 되는 것

- `Dependency.cmake` 에서 `-DASSIMP_INSTALL_PDB=OFF` 제거. (assimp 5.3.1 + Debug 버그 회피)
- `_ITERATOR_DEBUG_LEVEL` / 런타임 라이브러리 변경. (`MultiThreadedDebugDLL` + IDL=2 고정)
- `build/` 폴더를 직접 수정하거나, 한 세션이 빌드 중인데 다른 세션이 재설정. (Visual Studio 제너레이터는 `ALL_BUILD.vcxproj` 에 락을 건다.)
- NRI 재도입. (위 §7.1 참조)
- 누적 모델 복원. (NRD 와 양립 불가)
- 슬롯 맵을 **말 없이** 변경. 바뀔 땐 `STATUS.md §3` 먼저.
- 한 번에 여러 Phase 를 한 커밋에 묶기. Phase 경계는 커밋 경계.
- MCP/네트워크 의존성 추가. 현재 프로젝트는 완전 오프라인 빌드.

---

## 9. 커밋·PR 규약

- **커밋 단위**: Phase 의 체크박스 1개 또는 논리적으로 묶이는 몇 개. "`P0`: remove accumulation" 처럼 접두사에 phase 태그.
- **메시지 형식** (예):
  ```
  P0: pathtracer/tonemap — drop accumulate-forever model

  - remove g_accum += ... in PathTracer.hlsl
  - remove /(frameCount+1) in Tonemap.hlsl
  - tonemap input switched to Composite output (stub for now)
  ```
- **머지 전 확인**: 빌드(ALL_BUILD Debug), 실행 시 첫 프레임 크래시 없음, `STATUS.md` 갱신.
- **리뷰 원칙**: 작성한 툴과 **다른 툴**이 diff 를 리뷰한다 (Codex 가 만든 건 Claude 가, 반대도 동일).

---

## 10. 툴별 주의사항

### Claude Code

- CLAUDE.md 를 별도로 만들지 않고 이 `AGENTS.md` 를 읽는다. 필요시 `CLAUDE.md` 는 "see AGENTS.md" 한 줄로 둔다.
- 장시간 탐색이 필요하면 subagent/Task 를 사용해 문맥 창을 아낀다.
- 파일 편집 전 Read 필수.

### Codex

- `AGENTS.md` 를 자동 로드한다 — 이 문서가 곧 Codex 의 "project rules".
- 클라우드 에이전트 / 백그라운드 작업은 단일 phase 안에서만. 여러 phase 를 한 번에 돌리지 않는다.
- ChatGPT 와 IDE 확장이 같은 리포지토리에 동시에 작업하지 않도록 한다 (파일 락 충돌 방지).

---

## 11. 참고 문서

- `NRD_INTEGRATION_PLAN.md` — Phase 0~4 전체 로드맵 (본 문서의 근거 문서).
- `STATUS.md` — 현재 진행 상태, 다음 행동, 세션 로그.
- `CHANGELOG_2026-04-07.md` — 런타임 라이브러리 관련 히스토리.
- NRD 공식: `https://github.com/NVIDIAGameWorks/RayTracingDenoiser`
- ReBLUR 논문: *"ReBLUR: A Hierarchical Recurrent Denoiser"* (NVIDIA).
