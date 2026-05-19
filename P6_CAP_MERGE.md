# P6_CAP_MERGE.md

> **단계별 작업 매뉴얼.** 각 Phase 진입 시 해당 §만 정독하면 작업 가능.
> 진단 본문(왜 이 표가 이렇게 되었는가)은 `P6_DIAGNOSIS.md` 참조.
> 현재 진행 상태는 `MERGE_STATUS.md` 참조.
> Phase 6 종료 시 본 문서 삭제.

---

## 0. Final D Selection (2026-05-19)

- Selected branch: `phase6-d-tonemap`.
- Final policy: keep current NRD repo emissive values and apply shared `TONE_MAP_EXPOSURE=0.82` before ACES for both raw and denoised paths.
- `phase6-d-emissive` is no longer the default path; keep it only as a darker comparison branch.

---

## 0. 사전 합의 (한 번만 읽기)

### 0.1 공통 원칙

1. **NRD 통합 경로 (PT → NRD → Composite → ToneMap)는 절대 손대지 않는다.** cap_sharing은 누적 모델 + 단일 ToneMap이지만, NRD 레포의 per-frame overwrite 모델을 그대로 유지한다.
2. **`SampleDirectLight`가 albedo를 곱한다는 P3-2 결론은 유지.** cap_sharing 측 `SampleDirectLight`도 동일 패턴(BRDF 평가에 albedo 포함)이므로 Composite의 `diffuse + specular + emissive` 공식은 변경하지 않는다.
3. **HLSL `row_major` + C++ 측 전치 업로드 규칙 유지** (AGENTS.md §5).
4. **모든 단계는 한 PR/커밋 = 한 sub-phase 원칙** (AGENTS.md §8). 예외: B-1과 B-2는 데이터 의존성상 한 커밋이 자연스러움.

### 0.2 파일 prefix 변환

cap_sharing 측 파일은 프로젝트 지식에 prefix가 붙어 있다. 본 문서는 원본 경로로 표기.

| prefix | 실제 경로 |
|---|---|
| `cap_src__scene_desc.cpp` | `src/scene_desc.cpp` |
| `cap_shader__BRDF.hlsli` | `shader/BRDF.hlsli` |
| `cap_doc__*` | (이식 안 함) |

### 0.3 전체 흐름

```
A (진단 완료)
  ↓
B (sky) ────────── B-0 → B-1·2 → B-3 → B-4 → B-5
  ↓
C (BRDF) ───────── C-0 → C-1 → C-2 → C-3 → C-4
  ↓
D (exposure) ───── D-0 → D-1 → D-2 → D-3
  ↓
E (docs) ────────── E-1 → E-2
```

각 sub-phase = 한 PR. 진행 체크리스트는 `MERGE_STATUS.md` §2.

---

## 1. Phase A — diff (완료)

진단 결과는 `P6_DIAGNOSIS.md`에 동결. 본 문서는 작업 매뉴얼만.

---

## 2. Phase B — sky (환경맵 + 통합 광원)

### 2.1 목표

NRD 레포에 HDRI 환경맵 (importance sampling + 양방향 MIS) + 통합 광원 (sphere/triangle/quad)을 추가한다. 절차적 야간 하늘은 `g_hasEnvMap == 0` 폴백으로 유지.

### 2.2 슬롯·구조체 변경 (B-4 시점 적용)

```hlsl
// shader/PathTracer.hlsl - cbuffer GlobalUB 변경
cbuffer GlobalUB : register(b0) {
    float3 g_cameraPos;     float g_fov;
    float3 g_cameraFront;   float g_aspectRatio;
    float3 g_cameraUp;      float g_frameCount;
    float3 g_cameraRight;   uint  g_lightCount;
    // [B-4 신규]
    uint   g_envWidth;
    uint   g_envHeight;
    uint   g_hasEnvMap;
    float  _padB;
    // [NRD 레포 기존 유지]
    row_major float4x4 g_prevViewProj;
    row_major float4x4 g_currViewProj;
};
```

```cpp
// src/context.h - GlobalUniforms 변경 (cbuffer alignment 동일하게 유지)
struct GlobalUniforms {
    glm::vec3 cameraPos;    float    fov;
    glm::vec3 cameraFront;  float    aspectRatio;
    glm::vec3 cameraUp;     float    frameCount;
    glm::vec3 cameraRight;  uint32_t lightCount;
    // [B-4 신규]
    uint32_t envWidth;
    uint32_t envHeight;
    uint32_t hasEnvMap;
    float    _padB;
    // [NRD 레포 기존 유지]
    glm::mat4 prevViewProj;
    glm::mat4 currViewProj;
};
static_assert(sizeof(GlobalUniforms) % 16 == 0, "cbuffer alignment");
```

### 2.3 이식 파일 (전체)

| 파일 | 액션 | cap_sharing 원본 줄 범위 | 비고 |
|---|---|---|---|
| `src/env_map.h` | 신규 복사 | 전체 | t7/t8/t9 SRV + s0 sampler 멤버. AGENTS.md 파일 규약에 맞춰 snake_case 사용. |
| `src/env_map.cpp` | 신규 복사 | 전체 | `Init` HDRI 로드 + `BakeCDF` 4단계 |
| `src/scene_desc.h` | `LightDesc` 80B + `MakeSphere`/`MakeTriangle`/`MakeQuad` 추가 | cap_sharing `scene_desc.h` 전체 | 80B `static_assert` 유지 |
| `src/scene_desc.cpp` | NEE 광원 생성부를 `LightDesc::MakeSphere(...)` 호출로 변경 | cap_sharing `scene_desc.cpp` 가로등 생성부 | emissive 값은 **NRD 레포 측 값 그대로 유지** (Phase D 전까지 변경 금지) |
| `src/context.h` | `EnvMapUPtr m_envMap` 추가, `GlobalUniforms` env 4필드 추가 | cap_sharing `context.h` env 멤버 부분 | `prevViewProj/currViewProj` 그대로 유지. **cap_sharing은 두 행렬이 없으므로 병합 작업** |
| `src/context.cpp` | `Init`에서 `EnvMap::Load("hdri/moonless_golf_4k.hdr")`. `Render`에서 t7/t8/t9 SRV (10개로 확장), s0 sampler 바인딩, `GlobalUniforms` 채움. 패스 종료 후 t7~t9 + s0 null-clear | cap_sharing `context.cpp` `Init`/`Render` env 부분 | NRD 통합 경로 절대 손대지 않음 |
| `shader/PathTracer.hlsl` | cbuffer 4필드 + SRV/sampler 선언 + miss 분기 + 환경맵 NEE 블록 | L86 miss SampleEnvironmentMap, L181~196 환경맵 NEE 블록 | **G-buffer 7개 UAV (u0~u6) 출력 구조 보존** (u7 히스토그램은 별도, P5-3a). cap_sharing의 `totalRadiance += ...` 단순 합산을 lobe.pDiff/pSpec 분배로 변환 필요 |
| `shader/Scene.hlsli` | `ShaderLight` 80B + 환경맵 helper + 통합 NEE 이식 | 아래 줄별 표 참조 | NRD 레포의 t6 슬롯 유지. `UpdateRepresentativeHitDistance` 호출 보존 |

#### Scene.hlsli 이식 줄 범위 (사용자 grep 결과)

| 줄 | 함수/구조 | 설명 |
|---|---|---|
| L65~71 | `ShaderLight` 80B 구조체 | C++ `LightDesc`와 짝 |
| L84~91 | `DirToEnvUV` | 방향 벡터 → equirectangular UV |
| L93~96 | `SampleEnvironmentMap` | HDRI 텍스처 직접 샘플 |
| L98~108 | **미확정 헬퍼** | 후보: `CdfBinarySearch`. **B-0에서 view 필수** |
| L110~127 | `SampleEnvMapDir` | xi → 환경맵 방향 (margCDF + condCDF 이진 탐색) |
| L129~134 | `EnvMapPdf` | BSDF MIS 역방향 가중치용 PDF |
| L153~155 | `IsEmitter` | 광원 머티리얼 판정 헬퍼 (신규) |
| L160~175 | `GetSkyColor` | 절차적 야간 하늘 (`g_hasEnvMap == 0` 폴백) |
| L182~187 | `SampleTrianglePoint` | LightDesc 80B 통합 광원용 |
| L189~191 | `TriangleNormal` | |
| L194~198 | `SampleQuadPoint` | |
| L200~202 | `QuadNormal` | |
| L207~227 | `ComputeLightPdf` | 위치 기반 light PDF (신규) |
| L230~255 | `ComputeLightPdfDir` | 방향+거리 기반 light PDF (신규) |
| L260~392 | `SceneIntersect` (133줄) | 통합 광원(sphere/triangle/quad) hit + BVH |
| L397~439 | `IsOccluded` (43줄) | shadow ray |
| L444~557 | `SampleDirectLight` (114줄) | 통합 NEE. **`lightHitDistOut` 출력 인자 필수** (NRD 레포 P3-2 결정과 정합) |
| L560~584 | **미확정 함수** | 후보: `FindHitLight` 또는 두 번째 NEE 변형. **B-0에서 view 필수** |

---

### 2.4 Sub-phase 흐름

#### B-0: 사전 검증

**목적**: 작업 시작 전 베이스라인을 고정 + 진단 미해결 잔여 1건 해소.

1. `git status` 깨끗한지 확인.
2. F1 OFF / F1 ON 두 캡처. 기본 카메라, 정착 후 60프레임. 파일명: `build/b0_baseline_raw.png`, `build/b0_baseline_denoised.png`.
3. STATUS.md §3 슬롯 표와 코드 실제 상태 일치 확인.
4. **`Scene.hlsli` L98~108, L560~584 함수 본체 view.** 잔여 미해결 해소.

**통과 기준**: 베이스라인 캡처 2장 + Scene.hlsli 잔여 함수 본체 파악.

**소요**: 30분.

---

#### B-1·2: 광원 구조 통합 (C++ + HLSL 동시)

> **권장**: B-1 (C++ 측만) / B-2 (HLSL 측만) 두 sub-phase를 **한 PR로 묶는다**. 데이터 의존성상 별도 커밋이 빌드 깨짐을 보장.

**목적**: NRD 레포 sphere-only 광원을 80B `LightDesc` 통합 구조로 교체. 셰이더 측 `ShaderLight`도 동시 교체. **광원 emission 값은 NRD 레포 현재 값 그대로 유지** (Phase D 전까지).

**작업**:

- `src/scene_desc.h`: `LightDesc` 80B + `MakeSphere`/`MakeTriangle`/`MakeQuad` + `static_assert(sizeof(LightDesc)==80)`
- `src/scene_desc.cpp`: 가로등 생성부에서 `LightDesc::MakeSphere(center, radius, emission)` 호출로 교체
- `src/context.cpp::BuildSceneBuffers`: `LightDesc` 배열 GPU 업로드 (stride 80)
- `shader/Scene.hlsli`:
  - L65~71 `ShaderLight` 구조체 교체
  - L153~155 `IsEmitter` 추가
  - L182~202 `SampleTrianglePoint/TriangleNormal/SampleQuadPoint/QuadNormal` 4개 추가
  - L207~255 `ComputeLightPdf/ComputeLightPdfDir` 2개 추가
  - L444~557 `SampleDirectLight` 본체 교체 — **`lightHitDistOut` 출력 인자 유지** (NRD 레포 P3-2 결정과 정합)
  - L260~392 `SceneIntersect` 본체 교체 (통합 광원 hit 처리)
  - L560~584 미확정 함수도 이식 (B-0에서 본체 확인 후)

**통과 기준**: Debug ALL_BUILD 통과. F1 OFF/ON 캡처가 B-0 베이스라인과 **시각적으로 구분 불가**. SPDLOG `m_lightCount` 동일.

**막힐 가능성**:
- `LightDesc` 80B 패딩 ↔ GPU stride 불일치 → `static_assert`로 사전 차단
- `SampleDirectLight` 시그니처 (lightHitDistOut 누락) → cap_sharing 본체 가져오되 출력 인자 두 개 유지
- L98~108, L560~584 함수가 다른 함수에서 호출되는데 미이식 시 컴파일 에러

**소요**: 2~3시간.

---

#### B-3: EnvMap 자원 추가 (셰이더 미사용)

**목적**: HDRI 로드 + CDF 베이크 + GPU 자원 생성. 셰이더는 아직 t7/t8/t9 미사용.

**작업**:

- `src/env_map.h`, `src/env_map.cpp`: cap_sharing `EnvMap` 구현을 AGENTS.md 파일 규약에 맞춰 snake_case 파일명으로 신규 추가
- `src/context.h`: `EnvMapUPtr m_envMap` 멤버 추가
- `src/context.cpp::Init`: `m_envMap = EnvMap::Load(device, "hdri/moonless_golf_4k.hdr")`. **Render에서 아직 t7/t8/t9 바인딩 안 함**
- `CMakeLists.txt`:
  - `src/EnvMap.h src/EnvMap.cpp` 추가
  - `HDRI_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/hdri` 추가
  - `UpdateAssets`에 `copy_directory ${HDRI_SRC_DIR} ${CMAKE_BINARY_DIR}/hdri` 추가
- `hdri/moonless_golf_4k.hdr`: 사용자가 PolyHaven에서 받은 파일 배치

**통과 기준**: Debug ALL_BUILD 통과. 실행 시 stdout:
```
EnvMap: Loaded 4096x2048 HDRI [hdri/moonless_golf_4k.hdr]
EnvMap: CDF baked (4096x2048, totalSum=...)
```
렌더 결과는 B-0 베이스라인과 동일.

**막힐 가능성**:
- HDR 파일이 빌드 디렉터리에 안 옮겨짐 → CMakeLists `UpdateAssets`에 HDRI copy 누락. 확인: `build/hdri/` 존재 여부.
- `stbi_loadf` 실패 → `IsLoaded() == false`. SPDLOG_WARN 확인.
- 4K HDRI는 메모리 ~128MB float RGB + GPU 텍스처 ~256MB RGBA32F.

**소요**: 1시간.

---

#### B-4: Miss 분기에 환경맵 사용

**목적**: PathTracer miss 분기에서 환경맵 사용. NEE는 아직 추가 안 함.

**작업**:

- `shader/PathTracer.hlsl`:
  - `cbuffer GlobalUB`에 env 4필드 추가
  - SRV 선언:
    ```hlsl
    Texture2D<float4> g_envMap      : register(t7);
    Texture2D<float>  g_envCondCDF  : register(t8);
    Texture2D<float>  g_envMargCDF  : register(t9);
    SamplerState      s_envSampler  : register(s0);
    ```
  - miss 분기:
    ```hlsl
    float3 skyRadiance = (g_hasEnvMap != 0u)
        ? SampleEnvironmentMap(ray.direction)
        : GetSkyColor(ray.direction);
    ```
- `shader/Scene.hlsli`: L84~96 `DirToEnvUV`/`SampleEnvironmentMap` + L98~108 미확정 헬퍼 + L160~175 `GetSkyColor` 이식
- `src/context.h`: `GlobalUniforms` env 4필드 추가 (cbuffer alignment 확인)
- `src/context.cpp::Render`:
  - SRV 배열을 7→10개로 확장: `CSSetShaderResources(0, 10, srvs)`
  - s0 sampler 바인딩
  - `globalData.envWidth/envHeight/hasEnvMap` 채움
  - 패스 종료 후 SRV 10개 + s0 null-clear

**슬롯 표 갱신**: STATUS.md §3 + AGENTS.md §4 동시 갱신 (B-4 커밋과 한 PR).

**통과 기준**: F1 OFF에서 하늘이 절차적 야간 → PolyHaven `moonless_golf_4k`로 전환. 가로등 NEE는 B-1·2와 동일.

**막힐 가능성**:
- **cbuffer alignment 깨짐** → GPU에서 카메라 행렬이 깨진 값 → 검은 화면 또는 카메라 폭주. `static_assert(sizeof(GlobalUniforms) % 16 == 0)` 추가 권장.
- **SRV null-clear 누락** → t7~t9이 stale로 다음 패스에 끌려감. cap_sharing은 10개 한 번에 null-clear.
- `s0` 슬롯 충돌: AGENTS.md §4 NRD Denoise 표 확인 → s0 미사용. 안전.

**소요**: 2시간.

---

#### B-5: 환경맵 NEE + 양방향 MIS

**목적**: 환경맵 밝은 영역 importance sample + specular/diffuse 채널 정상 분배.

**작업**:

- `shader/Scene.hlsli`: L110~127 `SampleEnvMapDir` + L129~134 `EnvMapPdf` 이식
- `shader/PathTracer.hlsl`: cap_sharing의 환경맵 NEE 블록(L181~196)을 BRDF NEE 다음에 삽입. **단, NRD 레포 G-buffer 출력에 맞춰 변환** — 가장 위험한 작업.

**올바른 변환 형태** (cap_sharing 단순 합산 → NRD 레포 lobe-split):

```hlsl
// 환경맵 NEE — CDF 중요도 샘플링 + MIS
if (g_hasEnvMap != 0u && g_envWidth > 0u) {
    float2 xiEnv = GetRandomSamples(pixelCoord, (uint)(bounce * 10 + 99), frameCount);
    float  envPdf;
    float3 Lenv = SampleEnvMapDir(xiEnv, g_envWidth, g_envHeight, envPdf);
    float  NdotL = dot(N, Lenv);
    if (NdotL > 0.0f && envPdf > 0.0f) {
        float3 shadowTarget = hit.p + Lenv * 1e4f;
        if (!IsOccluded(hit.p + N * 0.005f, shadowTarget)) {  // P3-5 epsilon 0.005 준수
            BRDFResult envBrdf = EvaluateBRDF(N, V, Lenv,
                hit.material.albedo, hit.material.roughness, hit.material.metallic);

            float3 Le = SampleEnvironmentMap(Lenv);
            LobeWeights lobe2 = ComputeLobeWeights(N, V, hit.material.albedo, hit.material.metallic);
            float brdfPdf = ComputeCombinedPDF(N, V, Lenv, hit.material.roughness, lobe2);
            float w = PowerHeuristic(envPdf, brdfPdf);

            // FIREFLY_CLAMP는 NRD 레포 정책 (P5-3a 보류 중 — C-4 재검증)
            float3 envContrib = clamp(envBrdf.value * Le * w / (envPdf + 1e-10f) * throughput,
                                      0.0f, FIREFLY_CLAMP);

            // NRD 레포의 lobe-weighted split (P3-5 정책 준수)
            if (bounce == 0) {
                float3 diffuseContrib  = envContrib * lobe2.pDiff;
                float3 specularContrib = envContrib * lobe2.pSpec;
                result.diffuse  += diffuseContrib;
                result.specular += specularContrib;
                UpdateRepresentativeHitDistance(diffuseHitDist, diffuseHitWeight,
                                                diffuseContrib, /* env hitT */);
                UpdateRepresentativeHitDistance(specularHitDist, specularHitWeight,
                                                specularContrib, /* env hitT */);
            } else {
                if (pathTypeSet && pathIsSpecular) {
                    result.specular += envContrib;
                    UpdateRepresentativeHitDistance(specularHitDist, specularHitWeight,
                                                    envContrib,
                                                    specularFirstHitDist > 0.0f
                                                        ? specularFirstHitDist : /* env hitT */);
                } else {
                    result.diffuse += envContrib;
                    UpdateRepresentativeHitDistance(diffuseHitDist, diffuseHitWeight,
                                                    envContrib,
                                                    diffuseFirstHitDist > 0.0f
                                                        ? diffuseFirstHitDist : /* env hitT */);
                }
            }
        }
    }
}
```

**환경맵 NEE hitT 정의 — 미결정**:
shadow ray 차폐 없음 케이스의 hitT를 `1e4f`로 그대로 두면 REBLUR `hitDistanceParameters` 정규화에서 normHitDist=1.0(=max blur)로 매핑됨. 옵션:
- (a) `1e4f` 그대로 사용 → max blur 의도. 환경광의 부드러운 음영에 적합.
- (b) viewZ 거리 그대로 사용 → 표면 hit distance처럼 처리.
- (c) `0`으로 → REBLUR가 hit가 없는 픽셀로 간주 → 무시. 위험.

**B-5 작업 시 (a)로 시작, 필요 시 (b) 검증**. cap_sharing은 G-buffer 출력 자체가 없으므로 답이 없음 — 새로 정해야 함.

**FIREFLY_CLAMP**: NRD 레포 측 값을 사용. P5-3a로 20.0이면 20.0, 기본 5.0이면 5.0. cap_sharing의 50은 무시.

**BSDF 측 MIS (miss 분기)**:
```hlsl
// bounce>=1 이고 직전 샘플링이 specular가 아닐 때만 환경맵 MIS 적용
if (bounce >= 1 && !prevSpecular && g_hasEnvMap != 0u) {
    float envPdfRev = EnvMapPdf(ray.direction, g_envWidth, g_envHeight);
    float w = PowerHeuristic(prevBrdfPdf, envPdfRev);
    skyRadiance *= w;
}
```

**통과 기준**:
- F1 OFF: 음영부(건물 뒷면, 가로등 그늘)가 환경광 수렴 가속으로 정착 가속. 휘도 평균 증가.
- F1 ON: matPuddle(r=0.02) 반사가 환경맵 밝은 영역(달 등)을 잡아냄. specular 채널에 정상 분배.
- 회귀 없음: 직사 가로등 음영 패턴이 베이스라인과 동일.

**막힐 가능성**:
- shadow ray epsilon 0.001 vs 0.005 → P3-5 결정 0.005 준수
- 환경맵 NEE hitT 정의 → 위 (a)/(b)/(c) 결정
- lobe-split 누락 → REBLUR specular 채널이 diffuse로 새거나 그 반대

**B 완료 직후**: F1 ON/OFF 각각 8초 정착 캡처 → `build/b5_final_raw.png`, `build/b5_final_denoised.png`. B-0 베이스라인과 휘도/RMSE/SSIM 비교.

**소요**: 3~4시간 (가장 위험).

---

## 3. Phase C — BRDF (VNDF + Kulla-Conty MS)

### 3.1 목표

specular sampling NDF → VNDF (low-roughness 분산 감소). Kulla-Conty MS로 거친 표면 에너지 회복.

### 3.2 이식 파일

> **정정**: 이전 진단에서 "EnergyLUT는 dead resource"라고 추정했으나 **오류**. `BRDF.hlsli` L154~157 `SampleEnergyLUT` 헬퍼 존재. `EvaluateBRDF` 본체(L~204~252, 49줄)가 MS 보정 항 포함된 확장판.

| 파일 | 액션 | cap_sharing 줄 범위 | 비고 |
|---|---|---|---|
| `src/EnergyLUT.h` | 신규 복사 | 전체 | LUT_SIZE=32, t11/s1 사용 |
| `src/EnergyLUT.cpp` | 신규 복사 | 전체 | Hammersley 1024 + RG32F (E, Eavg) 베이크 |
| `shader/BRDF.hlsli` | 확장 + 교체 | L26~28 `BalanceHeuristic` (신규)<br>L70~112 `ImportanceSampleVNDF` (43줄)<br>L115~127 `ComputeSpecularPDF` (VNDF용인지 C-0 확인)<br>L154~157 `SampleEnergyLUT` (신규)<br>L~167~181 `ComputeLobeWeights` (MS 보정 가능성 — C-0 확인)<br>L~204~252 `EvaluateBRDF` (MS 보정 내장) | NRD 레포 기존 함수(`DistributionGGX`, `GeometrySmith`, `FresnelSchlick`, `ImportanceSampleGGX`) 보존 |
| `src/context.h` | `EnergyLUTUPtr m_energyLUT` 추가 | cap_sharing 부분 | |
| `src/context.cpp` | `Init`에서 `EnergyLUT::Create(device)`. `Render`에서 t11 SRV + s1 sampler 바인딩. 패스 종료 후 null-clear | cap_sharing 부분 | B-4에서 s0 바인딩과 합쳐 `CSSetSamplers(0, 2, samplers)` 한 줄. |
| `shader/PathTracer.hlsl` | L228 한 줄 교체: `ImportanceSampleGGX` → `ImportanceSampleVNDF` | L228 | |

### 3.3 Sub-phase 흐름

#### C-0: 사전 검증

**목적**: B-5 완료 캡처를 C 베이스라인으로 고정 + 본체 view 3건.

1. B-5 완료 캡처를 `build/c0_baseline_raw.png`, `build/c0_baseline_denoised.png`로 복사.
2. cap_sharing `BRDF.hlsli` L115~127 `ComputeSpecularPDF` 본체 view. VNDF용 PDF인지 NDF용인지 판정. 표준 VNDF PDF 형식: `D(H) * G1(V) / (4 * NdotV)`.
3. cap_sharing `BRDF.hlsli` L~167~181 `ComputeLobeWeights` 본체 view. MS 보정 specular 가중치 변경 여부 확인.
4. cap_sharing `BRDF.hlsli` L~204~252 `EvaluateBRDF` 본체 view. MS 보정 형태 (Kulla-Conty / Turquin / 기타) 및 LUT 좌표 순서 확인.

**통과 기준**: 세 함수 본체 파악 + C-2의 `ComputeSpecularPDF` 처리 방침 결정 (그대로 교체 / 새로 작성).

**소요**: 1시간 (PDF 본체 확인이 핵심).

---

#### C-1: EnergyLUT 자원 추가 (셰이더 미사용)

**작업**:

- `src/EnergyLUT.h`, `src/EnergyLUT.cpp`: cap_sharing 그대로
- `src/context.h`: `EnergyLUTUPtr m_energyLUT` 멤버 추가
- `src/context.cpp::Init`: `m_energyLUT = EnergyLUT::Create(device)`
- `src/context.cpp::Render`:
  - t11 SRV 바인딩: `CSSetShaderResources(11, 1, lutSRV)`
  - s1 sampler 추가: **B-4에서 이미 s0 바인딩되어 있으므로 `CSSetSamplers(0, 2, samplers)` 한 줄로 s0/s1 동시 설정**
  - 패스 종료 후 t11 + s1 null-clear
- `CMakeLists.txt`: `src/EnergyLUT.h src/EnergyLUT.cpp` 추가

**슬롯 표 갱신**: STATUS.md §3 + AGENTS.md §4 동시 갱신.

**통과 기준**: Debug ALL_BUILD 통과. stdout: `EnergyLUT: Bake complete (32x32)`. 렌더 결과는 C-0 베이스라인과 동일.

**소요**: 1시간.

---

#### C-2: VNDF 도입 (MS 보정 없이)

**작업**:

- `shader/BRDF.hlsli`:
  - L70~112 `ImportanceSampleVNDF` 추가
  - L115~127 `ComputeSpecularPDF` 교체 (C-0 결정에 따라)
- `shader/PathTracer.hlsl` L228: `ImportanceSampleGGX` → `ImportanceSampleVNDF` 한 줄 교체

**리스크 최대 지점**: VNDF/PDF 미스매치. VNDF는 V-projected halfvector를 샘플하므로 PDF가 NDF와 다름. 어긋나면 `combinedPdf` 잘못 계산 → throughput 발산 또는 표면 검어짐.

**통과 기준**:
- F1 OFF에서 matPuddle(r=0.02) 반사가 더 안정 (분산 감소, 정착 가속)
- F1 ON에서 specular 채널 더 정확. P3-5의 `prevSpecular = sampledSpecular && roughness < 0.05` 분기와 충돌 없음
- P5-3a FIREFLY_CLAMP 발화 빈도 측정 (C-4 입력)

**막힐 가능성**:
- VNDF grazing angle (NdotV < 0.1) 수치 불안정 → cap_sharing L70~112에 `max(NdotV, 0.01)` 클램프 있는지 확인
- `combinedPdf <= 0.0f` early exit 빈도 증가 → 일부 픽셀 무광 가능성

**소요**: 2시간.

---

#### C-3: Kulla-Conty MS 보정

**작업**:

- `shader/BRDF.hlsli`:
  - SRV 선언 추가:
    ```hlsl
    Texture2D<float2> g_energyLUT  : register(t11);
    SamplerState      s_lutSampler : register(s1);
    ```
  - L154~157 `SampleEnergyLUT` 헬퍼 추가
  - L~167~181 `ComputeLobeWeights` 교체 (C-0에서 MS 보정 확인됐다면)
  - L~204~252 `EvaluateBRDF` 교체 — MS 보정 항 포함된 49줄짜리 확장판
  - L26~28 `BalanceHeuristic` 추가 (호출처 확인 후 PathTracer.hlsl / Scene.hlsli 갱신 결정)

**리스크 최대 지점**: **LUT 좌표 transpose** — `P6_HANDOFF.md` §8.2 참조.
- `EnergyLUT::BakeLUT`는 `(y=roughness, x=NdotV)`로 베이크
- HLSL 호출은 `g_energyLUT.SampleLevel(s_lutSampler, float2(NdotV, roughness), 0)` 가 정확
- 한 줄 잘못 쓰면 LUT 전치 → 표면 폭주 또는 검어짐

**통과 기준**:
- F1 OFF에서 거친 표면(matBeige, matBrick 등 r>0.5) 휘도 증가. 어두운 곳이 덜 어두워짐
- 금속 표면(matHead headlight, matGlass) 반사 풍부. 발산 없음
- floor checker LDR mean ±5% 안정

**막힐 가능성**:
- LUT 좌표 transpose (위)
- F0=1 베이크 가정 ↔ 실제 F0 결합 공식 잘못 이식
- throughput 클램프(`min(throughput, 10)`)가 MS 보정 효과 상쇄 가능 → P5-3a와 함께 재검토

**소요**: 2~3시간.

---

#### C-4: P5-3a FIREFLY_CLAMP 재검증

**목적**: VNDF + MS 후 firefly 빈도 변화 측정 + 임계값 재튜닝.

**작업**:
1. F1 OFF 30초 정착 캡처. firefly 패턴 시각 분석.
2. P5-3a exit criteria(`P5_PBR_RECOVERY.md` 참조) 재측정. firefly 빈도 감소했다면 FIREFLY_CLAMP 5.0 환원 검토. 변화 없으면 20.0 유지.
3. matPuddle NEE 강도 측정. C-3 적용 후 r=0.02 specular 휘도 평균 추적.

**통과 기준**: 임계값 결정 + STATUS.md 업데이트 + P5_PBR_RECOVERY.md P5-3a 닫힘 여부 판단.

**막힐 가능성**: VNDF + MS로 specular 강화되어 firefly **증가** 가능성. matPuddle 근처 가로등. 별도 sub-phase 필요할 수도.

**소요**: 1~2시간.

---

## 4. Phase D — exposure

### 4.1 목표

cap_sharing emissive ×3~5 강세를 NRD 레포 ToneMap 정책과 정합. P4-6 정책 위반 없이 raw/denoised 양쪽 동시 적용.

### 4.2 사용자 의사결정 (D-1)

| 항목 | 옵션 A | 옵션 B |
|---|---|---|
| **emissive** | NRD 레포 현재 값 유지 + ACES 곡선 재조정 | cap_sharing 값(×3~5) 이식 + ACES 그대로 |
| **노출 매칭** | 자동 노출 패스 (별도 PR) | 사용자 정한 고정 스칼라 (raw/denoised 동일) |

**선택 시점**: D-0 측정 후. 추측으로 미리 결정 금지.

### 4.3 Sub-phase

| Sub-phase | 작업 | 통과 기준 |
|---|---|---|
| **D-0** | C-4 직후 4시점 캡처 (기본 + 3 추가) F1 OFF/ON. LDR mean, 채널별 평균, 클립 빈도, P4-5 distribution shift 재현 여부 측정 | 측정 데이터 + STATUS.md §4 Open Questions 갱신 |
| **D-1** | 사용자 옵션 A/B 선택 | 결정 확정 |
| **D-2** | 옵션 적용 (emissive 변경 또는 ACES 곡선 조정). raw/denoised 동일 패스이므로 P4-6 위배 아님 | LDR mean이 P4 reference 대역(±5%) 안으로 |
| **D-3** | P4-7 finalization 절차 답습 (정착 캡처 + 카메라 모션 probe) | 회귀 없음 |

---

## 5. Phase E — close-out

### 5.1 NRD 본진 문서 갱신

| 문서 | 갱신 내용 |
|---|---|
| `STATUS.md` §3 (Cross-Session Notes) | "Phase 6 — cap_sharing merge" 항목 추가. 슬롯 변경 (t7~t11, s0/s1, GlobalUB 4필드) 기록 |
| `STATUS.md` §5 (Session Log) | 각 sub-phase 완료 기록 |
| `STATUS.md` §4 (Open Questions) | D-0 측정 결과 반영 |
| `AGENTS.md` §4 (DX11 리소스 바인딩 맵) | PathTracer (CS) 표에 t7~t11, s0/s1 추가 |
| `AGENTS.md` §7 (Fixed decisions) | "HDRI 환경맵 + CDF importance sampling 도입", "VNDF specular sampling 도입", "Kulla-Conty MS 보정 도입" 3건 추가 |
| `NRD_INTEGRATION_PLAN.md` | "Phase 6 — cap_sharing merge" 줄 1개 추가 |

### 5.2 임시 문서 삭제

| 파일 | 액션 |
|---|---|
| `MERGE_STATUS.md` | 삭제 |
| `P6_CAP_MERGE.md` | 삭제 |
| `P6_DIAGNOSIS.md` | 삭제 |
| `P6_HANDOFF.md` | 삭제 |
| `P5_PBR_RECOVERY.md` | C-4에서 P5-3a 닫혔다면 삭제. 아니면 별도 판단 |

### 5.3 종료 체크

- [ ] B/C/D 모든 sub-phase 통과
- [ ] AGENTS.md §4 슬롯 표 ↔ 코드 일치 확인
- [ ] HDRI 누락 fallback (`g_hasEnvMap == 0`) 정상
- [ ] P4-6 정책 미위배 (raw/denoised 동일 ToneMap)
- [ ] 임시 문서 4종 삭제
- [ ] cap_sharing 측 원본 코드는 손대지 않음 (참고용으로 그대로 둠)

---

## 6. 비변경 영역 (Phase 6 범위 밖)

cap_sharing이 더 발전한 경우라도 NRD 레포 측 우선.

- **누적 모델**: NRD 레포는 per-frame overwrite. cap_sharing의 `g_accum +=` 복원 금지.
- **ToneMap 단일 패스**: NRD 레포는 4-pass (PathTracer → NRD → Composite → ToneMap). cap_sharing의 단일 ToneMap 복원 금지.
- **BVH 빌더**: 양쪽 동일 (SAH 16-bin). 이식 불필요.
- **Model/Mesh/Texture/Image/Buffer/Shader/ComputeProgram 인프라**: NRD 레포 측이 동등하거나 발전. 변경 없음.
- **`main.cpp`**: NRD 레포 우선. F1/F2 키 바인딩 보존.

---

## 7. 시간 추정

| Phase | sub-phase | 추정 |
|---|---|---|
| B | B-0 | 30분 |
| B | B-1·2 (통합) | 2~3시간 |
| B | B-3 | 1시간 |
| B | B-4 | 2시간 |
| B | B-5 | 3~4시간 (위험 최고) |
| C | C-0 | 1시간 |
| C | C-1 | 1시간 |
| C | C-2 | 2시간 |
| C | C-3 | 2~3시간 |
| C | C-4 | 1~2시간 |
| D | D-0~D-3 | 사용자 결정 + 1~2시간 |
| E | E-1~E-2 | 1시간 |
| **총** | | **~20~25시간** |

한 sub-phase = 한 PR이므로 1일 1~2 sub-phase가 안전한 페이스.

---

## 8. 참조

- `P6_HANDOFF.md` — 새 세션 진입 안내 (가장 먼저)
- `P6_DIAGNOSIS.md` — Phase A 진단 동결판 (참고 자료)
- `MERGE_STATUS.md` — 현재 진행 상태
- `STATUS.md` §3 §5 — NRD 본진 cross-session notes / session log
- `AGENTS.md` §4 §7 §8 — 슬롯 맵 / 고정 결정 / 금지 항목
- `P5_PBR_RECOVERY.md` — P5-3a/b/c 동시 진행 작업
- `cap_doc__CHANGELOG_2026-04-07.md` — cap_sharing 리팩토링 배경 (참고)
