# P6_DIAGNOSIS.md

> **Phase A 진단 결과의 동결 사본.** 본 문서는 작성 후 수정하지 않는다.
> 새 세션이 "왜 이 단계 표가 이렇게 되었는가" 추적할 때 참조한다.
> 작성일: 2026-05-18. Phase 6 종료 시 삭제.

---

## 0. 진단 메타데이터

| 항목 | 값 |
|---|---|
| 진단 일자 | 2026-05-18 |
| 진단자 | Claude (Sonnet/Opus 단일 세션) |
| 소스 자료 | cap_sharing 프로젝트 지식 (`cap_src__*` / `cap_shader__*` / `cap_doc__*` prefix) + NRD 통합 레포 (작업 트리) |
| 결정타 자료 | 사용자 측 디스크의 `grep` 결과 (cap_sharing `shader/*.hlsl` `shader/*.hlsli` 함수 위치) |
| 진단 결과 정확도 | 초기 추론 1건 오류 → grep으로 정정. 잔여 미해결 1건 (emissive ×3~5 의도) |

---

## 1. 진단 표 (요청 형식)

> 사용자 요청: "결과는 표로: 파일 / 분류(동일/이식/무시) / 이식 시 핵심 줄 범위 / 비고"

| 파일 | 분류 | 이식 시 핵심 줄 범위 | 비고 |
|---|---|---|---|
| `cap_src__scene_desc.cpp` | **이식 (부분)** | matWinWarm/Cool1/Cool2/Head/Glass emissive 정의부 + NEE 광원 emission | emissive ×3~5 차이, Phase D와 결합. 지오메트리·albedo·roughness는 동일하므로 머티리얼 emissive와 LightDesc 80B 구조만 이식. |
| `cap_src__scene_desc.h` | **이식** | `LightDesc` 80바이트 정의 + `MakeSphere/MakeTriangle/MakeQuad` 정적 헬퍼 전체 | `static_assert(sizeof(LightDesc)==80)`. NRD 레포는 단순 sphere만 지원하므로 통합 광원으로 교체. |
| `cap_shader__PathTracer.hlsl` | **이식 (부분)** | `cbuffer GlobalUB` 환경맵 필드 (`g_envWidth/Height/hasEnvMap`), `TracePath` miss 분기 (env vs sky), "환경맵 NEE" 블록, 간접광 `ImportanceSampleVNDF` 호출 | NRD 레포 PathTracer의 G-buffer 출력 구조(`TraceResult`, `diffuseHitDist/specularHitDist`, octa-packed normal, viewZ, 7개 UAV)는 **반드시 보존**. B/C 단계 분리 이식. |
| `cap_shader__BRDF.hlsli` | **이식 (확정)** | L26~28 BalanceHeuristic / L70~112 ImportanceSampleVNDF / L115~127 ComputeSpecularPDF (VNDF용 여부 확인 필요) / L154~157 SampleEnergyLUT / L~167~181 ComputeLobeWeights / L~204~252 EvaluateBRDF (MS 보정 확장판) | 줄 범위는 2026-05-18 사용자 grep으로 확정. |
| `cap_shader__Scene.hlsli` | **이식** | L65~71 ShaderLight 80B / L84~91 DirToEnvUV / L93~96 SampleEnvironmentMap / L98~108 미확정 헬퍼 / L110~127 SampleEnvMapDir / L129~134 EnvMapPdf / L153~155 IsEmitter / L160~175 GetSkyColor / L182~202 Sample/TriangleNormal/Quad 헬퍼 / L207~227 ComputeLightPdf / L230~255 ComputeLightPdfDir / L260~392 SceneIntersect / L397~439 IsOccluded / L444~557 SampleDirectLight / L560~584 미확정 함수 | LightDesc 80B와 짝. NRD 레포 슬롯 맵(`t6: g_lights`)은 그대로 사용 가능. |
| `cap_shader__Utility.hlsli` | **무시** | — | `GetSkyColor`는 Scene.hlsli에 있음 (이전 진단의 Utility.hlsli 위치 표기는 정정됨). cap_sharing의 Utility.hlsli는 PCG hash / GetRandomFloat / GetRandomSamples만 보유. NRD 레포 측이 동등. |
| `cap_src__EnvMap.h` | **이식** | 클래스 전체 | t7/t8/t9 SRV + s0 sampler. NRD 레포에 신규 추가. |
| `cap_src__EnvMap.cpp` | **이식** | `Init` HDRI 로드 + 텍스처 생성, `BakeCDF` 4단계 (importance/conditional/marginal/GPU 업로드) | stbi_loadf + RGBA 패딩 + luminance·sin(θ) 가중. |
| `cap_src__EnergyLUT.h` | **이식** | 클래스 전체 | t11 SRV + s1 sampler, `LUT_SIZE=32`. NRD 레포에 신규 추가. |
| `cap_src__EnergyLUT.cpp` | **이식** | 전체 (Hammersley/G1/ComputeGGXEnergy/BakeLUT/Init) | Hammersley 1024 + RG32F (E, Eavg). HLSL 측 샘플링 코드는 BRDF.hlsli L154~157 `SampleEnergyLUT`. |
| `cap_src__context.h` | **이식 (부분)** | `EnergyLUTUPtr m_energyLUT`, `EnvMapUPtr m_envMap` 멤버 추가 | NRD 레포의 기존 멤버(NRD 관련, m_compositeTexture, m_denoisedDiffuse/Specular 등)는 보존. |
| `cap_src__context.cpp` | **이식 (부분)** | `Init`에서 `EnvMap::Load` + `EnergyLUT::Create` 호출, `Render`에서 t7/t8/t9/t11 SRV 바인딩 + GlobalUB 환경맵 필드 채우기 | NRD 통합 코드(PT→NRD→Composite→ToneMap 파이프라인)는 절대 손대지 말 것. 단순 추가만. |
| `cap_shader__Tonemap.hlsl` | **무시** | — | NRD 레포의 ToneMap이 더 발전된 형태 (Composite 분리 후 별도 패스). cap_sharing은 단순 ACES 단일 패스. Phase D에서 NRD 레포 톤맵 정책을 재검토할 때 같이 보면 됨. |
| `cap_src__bvh.{h,cpp}` | **무시 (동일)** | — | NRD 레포에 이미 존재. SAH 16-bin 동일. |
| `cap_src__model.cpp` `cap_src__gltf_loader.{h,cpp}` `cap_src__mesh.{h,cpp}` `cap_src__texture.{h,cpp}` `cap_src__image.{h,cpp}` `cap_src__buffer.{h,cpp}` `cap_src__shader.{h,cpp}` `cap_src__compute_program.{h,cpp}` `cap_src__common.h` | **무시 (동일 또는 무관)** | — | NRD 통합 레포에도 동일하게 존재할 인프라. 차이 있으면 NRD 레포 측 우선. |
| `cap_src__main.cpp` | **무시** | — | 입력/루프 관련. NRD 레포 main 우선. |
| `cap_src__generate_night_street.py` `cap_src__CMakeLists.txt` `cap_src___gitignore` | **무시** | — | OBJ 생성용 스탠드얼론 스크립트 / 빌드 시스템은 NRD 레포가 더 복잡 (NRD 패치 포함). |
| `cap_doc__SCENE_GUIDE.md` `cap_doc__SCENE_REFACTOR.md` `cap_doc__REFACTORING.md` `cap_doc__CHANGELOG_2026-04-07.md` | **무시** | — | cap_sharing 원본은 그대로 두고 NRD 레포는 별개 운영. Phase E에서도 통합 안 함. |

---

## 2. 진단 본문 (세 가지 질문에 대한 답)

### 2.1 씬 동일성

**지오메트리 100% 동일.** boxes/quads 좌표, 광원 위치/반지름, 건물 4채 [-13,-6,0~12], [-13,-6,14~26], [6,13,0~12], [6,13,14~26], 가로등 SL_Z=2,8,14,20,26, 좌우 5쌍 — 일치. 도로 매트(0.07/0.06/0.05), Puddle(0.03/0.04/0.06, r=0.02), Brick/Concrete/DarkBld/Beige/Side albedo, 나무 좌표/색까지 동일.

**차이는 광원/창문 emissive 강도와 NEE 광원 emission**에 집중:

| 재질 | NRD 레포 (현재) | cap_sharing | 비율 |
|---|---|---|---|
| `matWinWarm.emissive` | (2.0, 1.5, 0.6) | (6.0, 4.5, 1.8) | 3× |
| `matWinCool1.emissive` | (0.8, 1.0, 1.8) | (2.5, 3.0, 5.5) | ~3× |
| `matWinCool2.emissive` | (1.2, 1.5, 2.0) | (3.5, 4.5, 6.0) | ~3× |
| `matHead.emissive` | (3.5, 2.7, 1.3) | (18.0, 14.0, 6.5) | ~5× |
| `matGlass.emissive` | (2.5, 2.0, 1.0) | (12.0, 9.5, 4.5) | ~5× |
| NEE 가로등 emission | (3.5, 2.7, 1.3) | (18.0, 14.0, 6.5) | ~5× |

이건 P4-6 정책("denoised-only 노출 스칼라 금지")과 직접 충돌할 수 있는 항목. cap_sharing은 환경맵+VNDF+MS로 에너지가 빠지지 않는 파이프라인을 전제로 강한 emissive를 사용하고, NRD 레포는 단일산란 GGX의 에너지 손실을 emissive 축소로 보상한 셈. **Phase D 노출 작업의 결합 변수**.

또한 cap_sharing의 `LightDesc`는 **80바이트 통합 구조체** (sphere/triangle/quad 통합, `lightType`/`area`/`p0~p3` 필드 포함, `static_assert(sizeof(LightDesc)==80)`), NRD 레포는 sphere만 (center, radius, emission, _pad) 단순 구의 32바이트 추정. 셰이더 측 `ShaderLight` 구조체와 `SampleDirectLight`/`FindHitLight`도 함께 이식.

### 2.2 BRDF 변경 지점

cap_sharing의 BRDF는 **VNDF + Kulla-Conty MS 보정** 풀세트. 변경 지점:

| 위치 | 함수/구조 | NRD 레포 | cap_sharing | 줄 범위 |
|---|---|---|---|---|
| BRDF.hlsli 신규 | `BalanceHeuristic` | 없음 | MIS 대체 가중치 | L26~28 |
| BRDF.hlsli §4-a 교체 | specular sampler | `ImportanceSampleGGX` (NDF) | `ImportanceSampleVNDF` | L70~112 |
| BRDF.hlsli §4-b 확인 | `ComputeSpecularPDF` | NDF PDF | VNDF PDF 추정 (본체 확인 필요) | L115~127 |
| BRDF.hlsli 신규 | `SampleEnergyLUT` | 없음 | LUT 샘플 헬퍼 | L154~157 |
| BRDF.hlsli §5 교체 | `ComputeLobeWeights` | Fresnel only | MS 보정 specular 가중치 추정 | L~167~181 |
| BRDF.hlsli §7 교체 | `EvaluateBRDF` | 단일산란 (D·G·F)/(4 NdotV NdotL) | MS 보정 항 포함 (~49줄로 NRD 레포 ~46줄보다 김) | L~204~252 |
| EnergyLUT.cpp `BakeLUT` | LUT 생성 | 없음 | 32² RG32F | 전체 |
| EnergyLUT.cpp `Init` | t11 SRV/sampler | 없음 | DXGI_FORMAT_R32G32_FLOAT, IMMUTABLE | 전체 |
| context.h | LUT 멤버 | 없음 | `EnergyLUTUPtr m_energyLUT` 추가 | 헤더 일부 |
| context.cpp | t11 바인딩 | 없음 | `EnergyLUT::Create(device)` + Render에서 t11 바인드 | Render() 일부 |
| PathTracer.hlsl 간접광 | indirect sample | `ImportanceSampleGGX` | `ImportanceSampleVNDF` | L228 |
| Scene.hlsli `SampleDirectLight` | NEE BRDF | 단일산란 | MS 보정 적용 (추정, 확인 필요) | L444~557 |

### 2.3 Sky 로딩 구조

cap_sharing은 **HDRI + importance sampling (Equirectangular CDF) + MIS** 풀 파이프라인:

| 단계 | 파일/함수 | 동작 |
|---|---|---|
| 로드 | `EnvMap::Load` → `EnvMap::Init` | `stbi_loadf` (RGB float) → RGBA로 패딩 |
| 텍스처 | `EnvMap::Init` body | t7, R32G32B32A32_FLOAT, IMMUTABLE, Linear Wrap-U/Clamp-V |
| CDF 베이킹 | `EnvMap::BakeCDF` | luminance×sin(θ) 가중 → 행별 conditional CDF (t8 R32F) + 행 선택 marginal CDF (t9, 1×h R32F) |
| 컨텍스트 | `Context::Init` | `m_envMap = EnvMap::Load(device, "hdri/moonless_golf_4k.hdr")` 호출 (실패 시 nullptr 반환 → 폴백) |
| GlobalUB 확장 | `cbuffer GlobalUB` | `g_envWidth`, `g_envHeight`, `g_hasEnvMap`, `_pad` 추가 |
| Miss 분기 | `PathTracer.hlsl::TracePath` | env 있으면 `SampleEnvironmentMap(ray.dir)`, 없으면 `GetSkyColor()` (절차적 야간 하늘, `Scene.hlsli` L160) |
| NEE | `PathTracer.hlsl::TracePath` "환경맵 NEE" 블록 (L181~196) | `SampleEnvMapDir(xi, w, h, pdf)` → shadow ray (1e4f) → `EvaluateBRDF` → `PowerHeuristic(envPdf, brdfPdf)` |
| BSDF 측 MIS | miss 시 | `EnvMapPdf(ray.dir, w, h)` 로 역방향 MIS 가중치 산출 (bounce≥1 + !prevSpecular 일 때만) |

**판정**: BSDF sampling만이 아니라 **NEE도 함께 importance sample**. MIS는 양방향. 폴백 경로(`g_hasEnvMap == 0`)는 cap_sharing `Scene.hlsli` L160~175의 `GetSkyColor()`가 절차적 야간 하늘(달 포함, 어두운 zenith/horizon).

---

## 3. 정정 이력

### 정정 #1 — `GetSkyColor` 위치

- **초기 진단 (잘못)**: `Utility.hlsli`에 있다고 추정.
- **정정 사유**: 사용자 grep 결과 `Scene.hlsli` L160~175에 있음.
- **확인 자료**: 2026-05-18 사용자 PowerShell `Select-String` 결과.
- **영향**: 이식 시 `Utility.hlsli`가 아닌 `Scene.hlsli`에서 함수 단위로 가져온다.

### 정정 #2 — EnergyLUT "dead resource" 추정

- **초기 진단 (잘못)**: cap_sharing이 EnergyLUT를 베이크하고 t11 바인딩까지 하지만 셰이더에서 사용 흔적이 검색에 안 잡히므로 "dead resource"로 추정.
- **정정 사유**: 사용자 grep 결과 `BRDF.hlsli` L154~157에 `SampleEnergyLUT(NdotV, roughness)` 헬퍼가 명확히 존재. EvaluateBRDF 본체(L~204~252, 49줄)가 NRD 레포 단일산란판(46줄)보다 김 → MS 보정 항 내장 확장판.
- **확인 자료**: 동일 grep.
- **영향**: Phase C-3에서 LUT는 살아있는 자원으로 이식. C-3 위험 §2 "LUT 좌표 transpose"가 작업 핵심.

### 정정 #3 — `EnvMap.hlsli` 별도 파일 가정

- **초기 진단 (잘못)**: cap_sharing 측 환경맵 helper들이 별도 `EnvMap.hlsli`에 있다고 추정.
- **정정 사유**: 사용자 grep 결과 모든 EnvMap helper는 `Scene.hlsli` L84~134에 내장.
- **확인 자료**: 동일 grep.
- **영향**: Phase B-4 이식 시 `EnvMap.hlsli` 신규 작성 불필요. `Scene.hlsli` 확장만으로 충분.

### 추론 오류의 메타 원인

세 정정 모두 **"프로젝트 지식 검색이 안 잡혔다 = 파일에 없다"**는 잘못된 등치 추론에서 발생. 검색 토큰화가 BRDF.hlsli §4-b 이후 청크와 Scene.hlsli L84~134 청크를 일관되게 노출하지 않는 한계가 있었다.

**교훈**: 사용자 측 디스크가 진실의 출처. 검색이 못 잡으면 사용자 grep을 일찍 요청.

---

## 4. 잔여 미해결 (1건)

### 미해결 #4 — emissive ×3~5 단위 차이의 의도

- **현상**: matWinWarm/Cool1/Cool2/Head/Glass + NEE 가로등 emission 전부 cap_sharing이 NRD 레포보다 3~5배 큼.
- **가설 A**: cap_sharing이 환경맵 추가로 빛이 분산되니 보상 차원에서 광원을 강화함.
- **가설 B**: Kulla-Conty MS 보정으로 회수된 에너지에 맞춰 광원도 비례 증가시킴.
- **가설 C**: 의도 없이 시각적 튜닝의 결과. cap_sharing 측 P3-2 commit log의 "Increased street lamp emission 5×, window emissives 3×" 가 NRD 레포에 같은 시점에 작성되어 있음 — 두 프로젝트가 한때 동일 트리였을 가능성.
- **해소 방법**: Phase D 진입 전 사용자 회상 또는 cap_sharing 측 git log 확인.
- **영향**: Phase D 옵션 A/B 결정에 직결. C 미해결인 채로도 B/C 진행은 가능.

---

## 5. 진단의 한계 (새 세션이 알아야 할 것)

1. **`Scene.hlsli` L98~108 / L560~584의 함수 본체**는 grep 패턴 한계로 이름이 미확정. Phase B 진입 직전 사용자 view 또는 추가 grep 필요.
2. **`ComputeSpecularPDF` (BRDF.hlsli L115~127) 본체**는 줄수가 NRD 레포 단일판과 동일하나 VNDF용 PDF로 변경되었는지 미확인. Phase C-2 진입 직전 본체 view 필요.
3. **`ComputeLobeWeights` (BRDF.hlsli L~167~181)** 본체도 MS 보정이 반영되었는지 미확인. Phase C-3 진입 직전 본체 view 필요.
4. **`BalanceHeuristic` 호출처**는 PathTracer.hlsl 또는 Scene.hlsli 어디인지 미확인. PowerHeuristic과의 사용 분기 패턴이 cap_sharing에 있는지 확인 필요.

이 4건은 진단을 막지 않지만, 이식 작업 시 본체 view가 필요한 시점을 명시.

---

## 6. 자료 출처 일람

| 출처 | 활용 |
|---|---|
| 프로젝트 지식 `cap_src__*` `cap_shader__*` `cap_doc__*` | 초기 진단의 대부분 |
| NRD 통합 레포 작업 트리 (현재 워킹 디렉터리) | 비교 기준 |
| 사용자 grep 결과 (2026-05-18) | 미해결 #1·#2·#3 해소의 결정타 |
| `STATUS.md` §3 §5 | P3-2, P3-5, P4-6, P5-3a 정책 추적 |
| `AGENTS.md` §4 §7 §8 | 슬롯 표 / 고정 결정 / 금지 항목 |
| `P5_PBR_RECOVERY.md` | 동시 진행 작업 충돌 검증 |
| `cap_doc__CHANGELOG_2026-04-07.md` | cap_sharing의 BVH/씬 리팩토링 배경 |
