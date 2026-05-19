# MERGE_STATUS.md

> **Phase 6 (cap_sharing 병합) 진행 상태 추적.**
> 매 sub-phase 완료 시 갱신. 새 세션이 가장 먼저 읽는 `P6_HANDOFF.md` 다음 두 번째.
> Phase 6 종료 시 삭제 (NRD_BLACK_OUTPUT_DEBUG.md / P5_PBR_RECOVERY.md 패턴).

---

## 0. Final D Selection (2026-05-19)

- Selected branch: `phase6-d-tonemap`.
- Final policy: keep current NRD repo emissive values and apply shared `TONE_MAP_EXPOSURE=0.82` before ACES for both raw and denoised paths.
- Comparison branch `phase6-d-emissive` remains available only for the much darker cap_sharing-original night look.
- Verification passed: Debug `ALL_BUILD` and a 6s hidden runtime smoke test.
- Next action: cross-tool review, then merge Phase 6.

---

## 1. 현재 단계

```
A (diff) ──── ✅ 완료
B (sky) ───── ✅ 완료
C (BRDF) ──── ✅ 완료
D (exposure)  ⏳ 대기 (D-0 시작 가능, D-1 사용자 의사결정 필요)
E (docs) ──── ⏸ 대기 (D 완료 후)
```

**다음 단일 액션**: D-0 exposure 측정 — C-4 직후 4시점 F1 OFF/ON 캡처와 휘도 분포 비교. D-1에서 emissive/ToneMap 방침 사용자 선택 필요.

**상세 작업 매뉴얼**: `P6_CAP_MERGE.md` §B 참조.

---

## 2. 진행 체크리스트

### Phase B — sky

- [x] **B-0** 사전 검증: 베이스라인 캡처 2장 + Scene.hlsli 잔여 함수 본체 확인
- [x] **B-1·2** 광원 구조 통합 (`LightDesc` 80B + `ShaderLight` 80B + `SampleDirectLight` 통합 분기). B-0에서 현재 HEAD에 이미 반영된 상태로 확인.
- [x] **B-3** EnvMap 자원 추가 (셰이더 미사용 상태로 빌드 + 로그 확인)
- [x] **B-4** Miss 분기 환경맵 사용 + `GlobalUB` 환경맵 필드 + 슬롯 표 갱신 (STATUS.md §3 + AGENTS.md §4)
- [x] **B-5** 환경맵 NEE + 양방향 MIS + **lobe-split 머지 (P3-5 정책 준수)**

### Phase C — BRDF

- [x] **C-0** 사전 검증: `ComputeSpecularPDF`/`ComputeLobeWeights`/`EvaluateBRDF` 본체 view (cap_sharing의 VNDF/MS 적용 형태 확인)
- [x] **C-1** EnergyLUT 자원 추가 (셰이더 미사용 + t11/s1 바인딩만)
- [x] **C-2** VNDF 도입 (`ImportanceSampleVNDF` + `ComputeSpecularPDF` VNDF용 동시 교체)
- [x] **C-3** Kulla-Conty MS 보정 (`EvaluateBRDF` 본체 교체 + `SampleEnergyLUT` 사용)
- [x] **C-4** P5-3a FIREFLY_CLAMP 재검증

### Phase D — exposure

- [ ] **D-0** 측정: 4시점 캡처 + 휘도 분포 비교
- [ ] **D-1** 옵션 A/B 사용자 선택
- [ ] **D-2** 적용 (emissive 변경 또는 ToneMap 조정, raw/denoised 동일)
- [ ] **D-3** P4-7 finalization 절차 답습

### Phase E — docs

- [ ] **E-1** STATUS.md §3 §5 갱신 + AGENTS.md §4 §7 갱신
- [ ] **E-2** 임시 문서 4종 삭제 (`MERGE_STATUS.md`, `P6_CAP_MERGE.md`, `P6_DIAGNOSIS.md`, `P6_HANDOFF.md`) + `P5_PBR_RECOVERY.md` 닫힘 여부 판단

---

## 3. 슬롯 변경 계획

| 슬롯 | 현재 (NRD 레포) | Phase 6 완료 후 | 추가 시점 |
|---|---|---|---|
| `t7` | `g_envMap` (HDRI RGBA32F) | `g_envMap` (HDRI RGBA32F) | B-4 완료 |
| `t8` | `g_envCondCDF` (R32F) | `g_envCondCDF` (R32F) | B-4 바인딩 완료, B-5 사용 |
| `t9` | `g_envMargCDF` (R32F) | `g_envMargCDF` (R32F) | B-4 바인딩 완료, B-5 사용 |
| `t11` | `g_energyLUT` (RG32F, 32×32) | `g_energyLUT` (RG32F, 32×32) | C-1 완료 |
| `s0` | `s_envSampler` | env sampler | B-4 완료 |
| `s1` | `s_energyLUTSampler` | LUT sampler | C-1 완료 |
| `b0` | `GlobalUB` + 4필드 (env 정보) | 유지 | B-4 완료 |

**B-4 / C-1 완료 시 STATUS.md §3 + AGENTS.md §4 동시 갱신.**

---

## 4. 사용자 의사결정 대기 항목

| 항목 | 결정 시점 | 옵션 |
|---|---|---|
| **emissive 처리 방침** (D-1) | B/C 완료 후 raw 캡처 측정 직후 | A: NRD 레포 값 유지 + ACES 재조정 / B: cap_sharing 값(×3~5) 이식 + ACES 그대로 |
| **B-1·2 통합 PR** (B-1 직전) | B-0 직후 | 해결됨: 현재 HEAD에 이미 통합 반영됨 |
| **`ComputeSpecularPDF` 교체 여부** (C-2 직전) | C-0 본체 view 직후 | 해결됨: VNDF용 PDF(`D * G1(V) / (4 * NdotV)`)로 판정, C-2에서 그대로 교체 |
| **환경맵 NEE 차폐 없음 hitT 정의** (B-5 작업 중) | B-5 빌드 후 raw/denoised 캡처 비교 | A: `1e4f` 그대로 → max blur / B: viewZ 거리 / C: 0 (위험) — 상세는 `P6_CAP_MERGE.md` §B-5 |

---

## 5. 진행 로그

최신 항목이 위로 온다.

```
2026-05-18 | Codex | C-4 | FIREFLY_CLAMP 재검증 완료. C-3 이후 F1 OFF 30초 정착
                              raw 캡처 `build/c4_firefly_raw.png` 및 `build/c4_firefly_histogram.txt` 생성.
                              stdout histogram: 99th=2.36, 99.9th=4.66, stderr 비어 있음.
                              시각 확인상 새 firefly 패턴/폭주 없음. P5-3a에서 20.0으로 완화한 목적이
                              valid puddle/lamp highlight 보존이었고 현 캡처에서 회귀 증거가 없어
                              `FIREFLY_CLAMP=20.0` 유지로 결정. Phase C 완료. 다음은 D-0 exposure 측정.
2026-05-18 | Codex | C-3 | Kulla-Conty MS 보정 적용 완료. `BRDF.hlsli`에
                              `g_energyLUT`/`s_energyLUTSampler` 및 `SampleEnergyLUT` 추가.
                              `EvaluateBRDF`를 MS 보정 포함 확장판으로 교체했고 LUT 좌표는 C-0에서
                              확인한 `float2(NdotV, roughness)` 유지. Debug ALL_BUILD 통과.
                              캡처: `build/c3_ms_raw.png`, `build/c3_ms_denoised.png`; stderr 비어 있음.
                              시각 확인상 표면 폭주/검어짐 없음, histogram 99.9th=4.66. 다음은 C-4
                              FIREFLY_CLAMP 재검증.
2026-05-18 | Codex | C-2 | VNDF 도입 완료. `BRDF.hlsli`에 `ImportanceSampleVNDF` 추가,
                              `ComputeSpecularPDF`를 C-0에서 확인한 VNDF용 PDF
                              `D * G1(V) / (4 * NdotV)`로 교체. PathTracer specular sampling 호출을
                              `ImportanceSampleGGX`에서 `ImportanceSampleVNDF(xi, N, V, roughness)`로 전환.
                              Debug ALL_BUILD 통과. 캡처: `build/c2_vndf_raw.png`,
                              `build/c2_vndf_denoised.png`; stderr 비어 있음. 시각 확인상 검은 화면,
                              폭주, obvious puddle/specular 회귀 없음. 다음은 C-3 Kulla-Conty MS 보정.
2026-05-18 | Codex | C-1 | EnergyLUT 자원 추가 완료. AGENTS 파일 규약에 맞춰
                              `src/energy_lut.h`, `src/energy_lut.cpp` 추가. `Context`에
                              `m_energyLUT` 생성 경로 연결, PathTracer pass에서 `t11` SRV와
                              `s1` sampler를 바인딩/해제. 아직 셰이더가 LUT를 사용하지 않으므로
                              렌더 결과는 C-0 baseline 유지가 기대값. Debug ALL_BUILD 통과.
                              실행 stdout에서 `EnergyLUT: Bake complete (32x32)` 확인,
                              `build/c1_lut_raw.png`, `build/c1_lut_denoised.png` 캡처, stderr 비어 있음.
                              AGENTS.md §4 슬롯 표 갱신 완료. 다음은 C-2 VNDF 도입.
2026-05-18 | Codex | C-0 | BRDF 사전 검증 완료. B-5 완료 캡처를
                              `build/c0_baseline_raw.png`, `build/c0_baseline_denoised.png`로 복사.
                              cap_sharing `BRDF.hlsli` 본체 확인: `ComputeSpecularPDF`는
                              `D(H) * G1(V) / (4 * NdotV)` 형태의 VNDF용 PDF로 판정되어 C-2에서
                              그대로 교체 가능. `ComputeLobeWeights`는 현재 NRD 레포와 동일.
                              `EvaluateBRDF`는 `SampleEnergyLUT(NdotV, roughness)` / `SampleEnergyLUT(NdotL, roughness)`
                              좌표를 쓰는 Kulla-Conty MS 보정 포함 확장판. LUT 좌표 순서는
                              `float2(NdotV, roughness)`로 확인. 다음은 C-1 EnergyLUT 자원 추가.
2026-05-18 | Codex | B-5 | 환경맵 NEE + 양방향 MIS 완료. `Scene.hlsli`에 CDF binary
                              search, `SampleEnvMapDir`, `EnvMapPdf` 추가. PathTracer direct-light
                              NEE 뒤에 environment NEE 추가, P3-5 정책대로 bounce0에서
                              lobe.pDiff/pSpec 분배 및 representative hitT 업데이트. 차폐 없음
                              env hitT는 계획 옵션 A인 `1e4f`로 시작. Miss 경로에는
                              `EnvMapPdf` reverse MIS를 적용하되 primary sky emissive routing은 유지.
                              Debug ALL_BUILD 통과. 8초 정착 캡처:
                              `build/b5_final_raw.png`, `build/b5_final_denoised.png`; stderr 비어 있음.
                              시각 확인: HDRI background 유지, puddle reflection 정상 범위, 검은 화면/폭주 없음.
                              Phase B 완료. 다음은 C-0 BRDF 사전 검증.
2026-05-18 | Codex | B-4 | Miss 분기 환경맵 사용 완료. `GlobalUniforms`/`GlobalUB`에
                              envWidth/envHeight/hasEnvMap/_padB 추가, PathTracer pass SRV 바인딩을
                              t0~t9로 확장하고 s0 sampler 바인딩/해제 추가. `Scene.hlsli`에
                              DirToEnvUV/SampleEnvironmentMap 및 t7~t9/s0 선언 추가.
                              Primary camera miss는 denoisable surface가 아니므로 emissive로 routing해
                              raw/denoised Composite 양쪽에서 HDRI background가 동일하게 보이게 함.
                              Debug ALL_BUILD 통과. 캡처: `build/b4_env_raw.png`,
                              `build/b4_env_denoised.png`; stderr 비어 있음. 다음은 B-5 환경맵 NEE.
2026-05-18 | Codex | B-3 | HDRI 에셋 배치 및 실제 로드 검증 완료.
                              Poly Haven `moonless_golf_4k.hdr`를 `hdri/`에 배치했고
                              CMake `UpdateAssets`가 `build/hdri/`로 복사하는 것 확인.
                              Debug ALL_BUILD 통과. 실행 stdout에서
                              `EnvMap: CDF baked (4096x2048, totalSum=890389.00)` 및
                              `EnvMap: Loaded 4096x2048 HDRI [hdri/moonless_golf_4k.hdr]` 확인,
                              stderr 비어 있음. 다음 단일 액션은 B-4 miss 분기 환경맵 사용.
2026-05-18 | Codex | B-3 | EnvMap 자원 추가 코드 완료. AGENTS 파일 규약에 맞춰
                              `src/env_map.h`, `src/env_map.cpp`로 추가하고 `Context`에 optional
                              `m_envMap` 로드 경로를 연결. CMake `UpdateAssets`가 `hdri/`를
                              `build/hdri/`로 복사하도록 확장. Debug ALL_BUILD 통과.
                              실행 검증은 `hdri/moonless_golf_4k.hdr`가 없어
                              `EnvMap: HDRI not found [...] - using procedural sky` 폴백 로그 확인까지 완료.
                              실제 `Loaded`/`CDF baked` 검증은 HDRI 에셋 배치 후 재실행 필요.
2026-05-18 | Codex | B-0 | 사전 검증 완료. Debug ALL_BUILD 통과. 기본 카메라에서
                              `build/b0_baseline_raw.png`, `build/b0_baseline_denoised.png` 생성.
                              런타임 stderr 비어 있음, NRD backend ready, m_lightCount=10.
                              STATUS/AGENTS 슬롯 표와 현재 PathTracer 바인딩(u0~u7, t0~t6, ToneMap t10) 일치 확인.
                              Scene.hlsli는 총 541줄이라 L560~584는 존재하지 않음. L98~108은
                              `SampleTrianglePoint`, 후반 미확정 후보는 L517~539 `FindHitLight`.
                              추가 발견: B-1·2 범위(80B LightDesc/ShaderLight, 통합 SampleDirectLight)는
                              이미 현재 HEAD에 반영되어 있어 B-3부터 진행 가능.
2026-05-18 | Claude | Phase A | 문서 4종 체제로 재정리. P6_HANDOFF.md / P6_DIAGNOSIS.md 신규 작성.
                                MERGE_STATUS.md / P6_CAP_MERGE.md 슬림화. 새 세션/LLM이 30분 안에
                                컨텍스트 회복 가능하게 진입점 + 작업 매뉴얼 + 진단 동결판 + 상태 추적 분리.
2026-05-18 | Claude | Phase A | 사용자 grep 결과로 미해결 #1·#2·#3 해소.
                                ① BRDF.hlsli L70~112 ImportanceSampleVNDF / L154~157 SampleEnergyLUT /
                                  ~L204~252 EvaluateBRDF (MS 보정 내장 확장판) 위치 확정.
                                ② Scene.hlsli L84~134에 EnvMap helpers 전부 (DirToEnvUV / SampleEnvironmentMap
                                  / SampleEnvMapDir / EnvMapPdf). 별도 EnvMap.hlsli 없음.
                                ③ EnergyLUT는 dead resource 아님 — SampleEnergyLUT 헬퍼 존재.
                                추가 발견: BalanceHeuristic / IsEmitter / ComputeLightPdf / ComputeLightPdfDir 신규.
2026-05-18 | Claude | Phase A | 진단 완료. 씬 동일성/BRDF 변경 지점/Sky 로딩 구조 표 작성.
                                미해결 4건 추적 (#1 BRDF.hlsli 후반부 / #2 EnvMap HLSL helper /
                                #3 EnergyLUT.cpp + HLSL 측 / #4 emissive ×3~5 의도).
2026-05-18 | Claude | Phase A | MERGE_STATUS.md / P6_CAP_MERGE.md 초안 작성.
```

---

## 6. 외부 자산

| 경로 | 비고 |
|---|---|
| `hdri/moonless_golf_4k.hdr` | PolyHaven 4K HDRI. cap_sharing이 기본값으로 로드. `EnvMap::Load` 인자 하드코딩. |
| `hdri/qwantani_night_puresky_4k.hdr` | PolyHaven 4K HDRI. 야간 시나리오 A/B 테스트용 후보. |

용량 제약으로 본 문서/프로젝트 지식에는 미업로드. 실제 빌드 시 `hdri/` 디렉터리에 위치해야 한다 (CMakeLists.txt가 `${CMAKE_BINARY_DIR}/hdri/`로 동기화).

---

## 7. 임시 문서 4종

| 파일 | 목적 | 갱신 빈도 |
|---|---|---|
| `P6_HANDOFF.md` | 새 세션 진입 안내 (가장 먼저 읽음) | Phase 전환 시 |
| `MERGE_STATUS.md` | 본 문서. 현재 진행 상태 | 매 sub-phase 완료 |
| `P6_CAP_MERGE.md` | 단계별 작업 매뉴얼 | Phase 진입 시 |
| `P6_DIAGNOSIS.md` | Phase A 진단 동결판 | **동결** |

Phase 6 종료 시 4종 모두 삭제.
