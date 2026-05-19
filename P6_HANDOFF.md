# P6_HANDOFF.md

> **새 세션 / 다른 LLM이 Phase 6 작업을 이어받을 때 가장 먼저 읽는 문서.**
> 이 문서가 30분 안에 컨텍스트 복원을 끝내게 해 준다.

---

## 0. Emissive Branch Snapshot (2026-05-19)

Start here for the emissive comparison branch.

- `master` has already received the selected `phase6-d-tonemap` path.
- Current branch: `phase6-d-emissive`, merged forward from `master`.
- Purpose: keep the much darker cap_sharing-original night look as a comparison branch at the user's request.
- User decision: keep `master` unchanged on the selected `phase6-d-tonemap` policy, and retain `phase6-d-emissive` as a comparison branch only.
- Policy on this branch: local `cap_sharing_for_upload` emissive values and ToneMap exposure `1.0`.
- Previous captures: `build/d_emissive_raw.png`, `build/d_emissive_denoised.png`.
- Previous metrics: raw luma `0.3192`, raw clip `0.0000`; denoised luma `0.3584`, denoised clip `0.0000`.
- Verification passed: Debug `ALL_BUILD` and a 6s hidden runtime smoke test.
- Post-merge recapture: `build/d_emissive_after_master_raw.png`, `build/d_emissive_after_master_denoised.png`.
- Post-merge metrics: raw luma `0.3189`, clip `0.0000`; denoised luma `0.3582`, clip `0.0000`.
- Visual pass: denoised image keeps the darker night look without black-output, obvious runaway, or measured clipping.
- Next action: do not merge this branch to `master`; keep it available for visual comparison, then continue Phase E cleanup from `master` when requested.

---

## 0. 한 줄 요약

**cap_sharing 프로젝트의 렌더 품질 기능(HDRI 환경맵 + VNDF + Kulla-Conty MS)을 NRD 통합 레포로 이식하는 작업의 Phase 6.** 진단(A)은 완료, 본 작업(B → C → D → E)이 남았다.

---

## 1. 읽기 순서 (30분 워크플로)

1. **본 문서 (`P6_HANDOFF.md`)** — 컨텍스트 회복 ─ 10분
2. **`MERGE_STATUS.md`** — 현재 어느 sub-phase인지, 다음 단일 액션이 무엇인지 ─ 5분
3. **`STATUS.md` §3 + §5** — NRD 통합 본진의 cross-session notes와 가장 최근 세션 로그 ─ 5분
4. **`AGENTS.md` §4 + §7 + §8** — 슬롯 맵, 고정 결정, 하면 안 되는 것 ─ 5분
5. **`P6_CAP_MERGE.md`** — 현재 진행할 sub-phase의 §만 정독 ─ 5분

진단 추론 근거나 정정 이력이 궁금하면 `P6_DIAGNOSIS.md`를 참고 (선택 사항).

---

## 2. 작업 컨텍스트 (왜 이 작업을 하는가)

### 2.1 두 프로젝트

| 이름 | 역할 | 위치 |
|---|---|---|
| **NRD 통합 레포** (현재 작업 트리) | NVIDIA NRD `REBLUR_DIFFUSE_SPECULAR` denoiser 통합 중. 기능은 적지만 NRD 파이프라인 완성. | 본 레포 |
| **cap_sharing** | NRD 없음. 그러나 렌더 품질 기능(HDRI 환경맵 importance sampling, VNDF, Kulla-Conty multi-scatter)이 풍부. | `C:\Users\cmc46\git\cap_sharing_for_upload\` (사용자 로컬). 프로젝트 지식에 `cap_src__*` / `cap_shader__*` / `cap_doc__*` prefix로 업로드되어 있음. HDRI 두 장(`hdri/moonless_golf_4k.hdr`, `hdri/qwantani_night_puresky_4k.hdr`)은 용량으로 미업로드 — PolyHaven에서 받은 4K. |

### 2.2 왜 합치는가

NRD 통합 작업 자체는 P0~P5 완료까지 진행되어 있으나, 렌더 품질은 cap_sharing이 우월. 합치면:
- HDRI로 환경광 수렴 가속
- VNDF로 low-roughness specular(matPuddle r=0.02) 분산 감소
- Kulla-Conty MS로 거친 표면 에너지 손실 회복

P5 단계에서 발견된 `P5_PBR_RECOVERY.md`의 PBR 회복 작업과 일부 목표가 겹친다. C-4에서 P5-3a (FIREFLY_CLAMP) 재검증으로 동기화.

### 2.3 합의된 단계

- **A (diff)** — 진단. **완료**.
- **B (sky)** — HDRI 환경맵 + 통합 광원(sphere/triangle/quad).
- **C (BRDF)** — VNDF + Kulla-Conty MS.
- **D (exposure)** — emissive 단위 재정합 + ToneMap 정책 재검토. **사용자 의사결정 필요**.
- **E (docs)** — 정리. STATUS.md / AGENTS.md 갱신 + 임시 문서(`MERGE_STATUS.md`, `P6_CAP_MERGE.md`, `P6_DIAGNOSIS.md`, `P6_HANDOFF.md`) 삭제.

---

## 3. 가장 중요한 4가지 약속

이 4개를 위반하면 회귀가 보장된다. 작업 중 끝까지 의식.

1. **NRD 통합 경로(PT → NRD → Composite → ToneMap)는 절대 손대지 않는다.**
   cap_sharing은 누적(`g_accum +=`) + 단일 ToneMap 모델이지만, NRD 레포는 per-frame overwrite + 4패스. cap_sharing의 누적 모델을 복원하려 하지 말 것.

2. **`SampleDirectLight`는 BRDF 평가에 albedo를 baked한다 (P3-2 결정).**
   Composite 공식 `diffuse + specular + emissive`는 변경 금지. `diffuse * albedo`로 되돌리면 double-albedo 발생.

3. **HLSL/C++ bit-identical 유지** (AGENTS.md §5/§7).
   특히 `hitDistanceParameters` (A=30 등), `EnergyLUT.cpp`의 `ImportanceSampleGGX` α 매핑은 HLSL과 정확히 일치해야 함.

4. **한 sub-phase = 한 PR/커밋.** (AGENTS.md §8)
   여러 sub-phase를 한 PR에 묶지 말 것. B-1과 B-2는 작업 의존성이 강하므로 예외적으로 통합 가능 (P6_CAP_MERGE.md §B 참조).

---

## 4. 정책 영향 매트릭스

이미 굳어진 결정과 진행 중 작업의 충돌 가능성. 각 단계 시작 전 한 번 확인.

| 정책 / 작업 | 출처 | Phase 6 영향 단계 | 처리 방침 |
|---|---|---|---|
| **P3-2** SampleDirectLight albedo bake | STATUS.md | B-2, C-3 | cap_sharing 코드 그대로 머지 가능 (cap_sharing도 같은 패턴) |
| **P3-5** jitter 제거 | STATUS.md | B-4 | 환경맵 NEE는 jitter와 무관. 영향 없음. |
| **P3-5** hitDistanceParameters A=30 | STATUS.md / nrd_denoiser.cpp | B-5, C-2, C-3 | VNDF 도입은 hitT 분포에 영향 없음. MS 보정으로 specular 강도가 변하면 재검토 필요. |
| **P3-5** SAMPLES_PER_PIXEL=1 | STATUS.md | 전체 | 변경 금지. REBLUR temporal이 multi-sample 역할. |
| **P3-5** lobe-weighted NEE split | STATUS.md | B-5 | **환경맵 NEE 결과도 `result.diffuse * lobe.pDiff` / `result.specular * lobe.pSpec`로 분배 필요**. cap_sharing의 `totalRadiance += ...` 단순 합산을 그대로 옮기면 위반. |
| **P3-5** representative hitT (luminance-weighted) | STATUS.md / PathTracer.hlsl | B-5 | `UpdateRepresentativeHitDistance` 호출이 환경맵 NEE에도 필요. shadow ray가 차폐 없음일 때의 hitT 정의가 모호 — B-5 위험 요소 §1. |
| **P4-6** denoised-only 노출 스칼라 금지 | STATUS.md | D | raw/denoised 양쪽 동일 변경만 허용. 한쪽만 곱하기 금지. |
| **P5-3a** FIREFLY_CLAMP=20 (보류) | P5_PBR_RECOVERY.md | C-4 | VNDF로 specular 분산 감소 시 임계값 낮춰도 무방. C-4에서 재측정. |
| **P5-3b** HLSL specular hitT 브랜치 r<0.05 (보류) | P5_PBR_RECOVERY.md | C-3 | MS 보정으로 specular 강도가 변하면 hitT 정규화 임계값 재검토. |

---

## 5. 슬롯 변경 예고 (B/C 완료 후 최종 상태)

`AGENTS.md §4 PathTracer (CS)` 표가 다음과 같이 확장된다. **변경은 B-4 / B-5 / C-1 시점에 STATUS.md §3 + AGENTS.md §4 동시 갱신**.

| 슬롯 | 현재 (NRD 레포) | Phase 6 완료 후 | 추가 시점 |
|---|---|---|---|
| `t0`~`t6` | 씬 데이터 SRV | **동일 유지** | — |
| `t7` | — | `g_envMap` (HDRI RGBA32F) | B-4 |
| `t8` | — | `g_envCondCDF` (R32F, w×h) | B-4 |
| `t9` | — | `g_envMargCDF` (R32F, 1×h) | B-4 |
| `t10` | ToneMap 입력 (별 패스) | **동일 유지** | — |
| `t11` | — | `g_energyLUT` (RG32F, 32×32) | C-1 |
| `s0` | — | env sampler (Wrap-U / Clamp-V) | B-4 |
| `s1` | — | LUT sampler (Linear Clamp) | C-1 |
| `u0`~`u6` | G-buffer 7장 | **동일 유지** | — |
| `b0` | `GlobalUB` (11 필드) | + `envWidth/envHeight/hasEnvMap/_pad` 4필드 | B-4 |

**충돌 없음.** AGENTS.md §4 NRD Denoise / Composite / ToneMap 표의 슬롯 번호와 비교 확인 완료.

---

## 6. 작업 시작 직전 체크리스트

매 sub-phase 시작 시 한 번 실행:

- [ ] `git status` 깨끗한가? (P5-3 stash가 끼어있지 않은가)
- [ ] 직전 sub-phase의 통과 기준이 실제로 만족되었나? (캡처 확인)
- [ ] `STATUS.md` §3의 슬롯 표가 코드와 일치하나?
- [ ] 이번 sub-phase가 §4 정책 매트릭스의 어떤 항목과 부딪치나?
- [ ] 이번 sub-phase의 "통과 기준"이 무엇인지 작업 시작 전에 글로 적어두었나?

---

## 7. 막혔을 때

다음 순서로 점검:

1. **빌드 실패** → 컴파일 에러 위치 확인. 셰이더 컴파일이면 `D3DCOMPILE_DEBUG` 활성 상태에서 줄 번호와 에러 메시지 모두 사용. cbuffer alignment 깨지면 GPU에서 카메라 폭주 — 검은 화면.

2. **화면이 검다** →
   - cbuffer 필드 순서 / alignment 점검
   - SRV 바인딩 카운트 (7 vs 10) 확인. null-clear 누락 점검
   - `D3D11_DEBUG_LAYER`가 켜져 있다면 stderr에 binding warning이 떠야 함

3. **시각적 회귀 (베이스라인과 다름)** →
   - F1 OFF/ON 둘 다 회귀인지, 한쪽만인지 분리
   - 직전 sub-phase 캡처와 픽셀 단위 비교 (`build/p4_2_metrics.json` 패턴)
   - 가장 흔한 원인: `SampleDirectLight` 시그니처 불일치 (`lightHitDistOut` 누락), lobe-split 깨짐, throughput clamp

4. **수치 폭주 (NaN/Inf)** →
   - VNDF의 grazing angle 클램프 누락 (C-2 위험 §1)
   - LUT 좌표 transpose (C-3 위험)
   - shadow ray 자기교차 (epsilon 0.001 vs 0.005 — P3-5는 0.005)

5. **NRD 자체 회귀 (denoise만 깨짐)** →
   - `hitDistanceParameters` HLSL↔C++ 불일치 점검
   - `g_diffuseRadiance` / `g_specularRadiance` UAV 출력 형식(YCoCg, normHitDist) 점검
   - cap_sharing 본체의 `totalRadiance += ...`가 G-buffer 출력으로 정확히 분배되었는지

---

## 8. 가장 위험한 두 곳

### 8.1 B-5: 환경맵 NEE의 lobe-split 머지

cap_sharing 원본은 `totalRadiance += clamp(envBrdf.value * Le * w / envPdf * throughput, 0, 50)`로 **단일 합산**. NRD 레포는 lobe.pDiff/pSpec 분배 + `UpdateRepresentativeHitDistance` 호출이 모두 필요. **cap_sharing 코드를 그대로 복붙하면 P3-5 정책 위반 + REBLUR 채널 입력 비정상**.

올바른 형태 (예시):
```hlsl
float3 envContrib = clamp(envBrdf.value * Le * w / (envPdf + 1e-10f) * throughput, 0, 50);
if (bounce == 0) {
    float3 diffuseContrib  = envContrib * lobe.pDiff;
    float3 specularContrib = envContrib * lobe.pSpec;
    result.diffuse  += diffuseContrib;
    result.specular += specularContrib;
    UpdateRepresentativeHitDistance(diffuseHitDist,  diffuseHitWeight,  diffuseContrib,  /* env hitT 정의 */);
    UpdateRepresentativeHitDistance(specularHitDist, specularHitWeight, specularContrib, /* env hitT 정의 */);
} else {
    // pathIsSpecular 분기 + UpdateRepresentativeHitDistance — P3-5의 NEE 처리 답습
}
```

shadow ray가 차폐 없음으로 환경맵에 도달했을 때의 hitT 정의는 **단순 가정 금지**. `1e4f`는 cap_sharing 가정이지만, REBLUR `hitDistanceParameters` 정규화에서 NormHitDist=1.0(=max blur)로 매핑됨. specular 채널은 `prevSpecular && roughness<0.05`일 때만 의미가 있고, 그 외에는 normHitDist를 다르게 정해야 할 수 있음. **B-5 작업 시 별도 검증 필요 항목**.

### 8.2 C-3: EvaluateBRDF MS 보정의 LUT 좌표 transpose

cap_sharing `EnergyLUT::BakeLUT`는 `(y=roughness, x=NdotV)`로 베이크. HLSL `SampleEnergyLUT(NdotV, roughness)` 호출에서 텍스처 좌표가 `float2(NdotV, roughness)`인지 `float2(roughness, NdotV)`인지가 **한 줄 결정**. 잘못 쓰면 표면이 검어지거나 폭주.

확인 방법: `EnergyLUT.cpp` `BakeLUT`의 인덱싱 `outData[(y * LUT_SIZE + x) * 2 + ...]`에서 y가 roughness 축, x가 NdotV 축. HLSL `Texture2D.SampleLevel(sampler, uv, 0)`의 uv는 `(x_axis, y_axis)` 순서. 따라서 **`float2(NdotV, roughness)` 가 정확**.

cap_sharing `BRDF.hlsli` L154~157의 `SampleEnergyLUT` 본체가 이 순서를 어떻게 두는지 확인 후 그대로 옮길 것.

---

## 9. 의사결정 대기 항목

다음은 본 LLM이 단독으로 결정하지 않고 사용자에게 묻는다:

1. **Phase D 옵션 선택** (D-1)
   - 옵션 A: NRD 레포 현재 emissive 유지, ACES만 재조정
   - 옵션 B: cap_sharing emissive (×3~5) 이식, ACES 그대로
   - **선택 시점**: B와 C가 끝나고 raw 캡처로 휘도 분포를 측정한 후. 추측으로 미리 결정하지 말 것.

2. **B-1과 B-2의 통합 PR 처리 여부** (B-1 직전)
   - 별도로 나누는 게 안전하나, 데이터 의존성상 둘이 한 커밋이 되는 게 더 자연스러움
   - 사용자 또는 다음 LLM이 일관성 있게 결정할 것

3. **C-2의 ComputeSpecularPDF 교체 여부** (C-2 직전)
   - cap_sharing L115~127의 `ComputeSpecularPDF` 본체를 확인하여 VNDF용인지 NDF용인지 판정
   - VNDF용이면 그대로 교체. NDF용이면 cap_sharing 자체가 잘못 — 표준 VNDF PDF로 새로 작성

4. **환경맵 NEE 차폐 없음 hitT 정의** (B-5 작업 중)
   - shadow ray가 어디에도 안 부딪힐 때 `UpdateRepresentativeHitDistance`에 넣을 hitT 값
   - 옵션 A: `1e4f` 그대로 → REBLUR가 max blur로 처리. 환경광 부드러운 음영에 적합.
   - 옵션 B: viewZ 거리 → 표면 hit처럼 처리. 잘못된 정규화 위험.
   - 옵션 C: `0` → 무효 픽셀. 위험.
   - **결정 방법**: B-5 첫 빌드 후 옵션 A로 시작하여 F1 ON 캡처를 보고 회귀 확인. 회귀 있으면 옵션 B로 비교.

---

## 10. 임시 문서 4종 (Phase 6 종료 시 일괄 삭제)

| 파일 | 목적 | 갱신 빈도 |
|---|---|---|
| `MERGE_STATUS.md` | 현재 진행 상태 + 진행 로그 | 매 sub-phase 완료 |
| `P6_CAP_MERGE.md` | 단계별 작업 매뉴얼 | Phase 진입 시 (B/C/D/E) |
| `P6_DIAGNOSIS.md` | Phase A 진단 결과 + 정정 이력 (참고 자료) | **동결** (수정 안 함) |
| `P6_HANDOFF.md` | 본 문서. 새 세션 진입 안내 | Phase 전환 시 |

NRD 본진 문서(`STATUS.md`, `AGENTS.md`, `CLAUDE.md`, `NRD_INTEGRATION_PLAN.md`)는 **Phase 6 종료 시 §E 갱신**으로 통합되고, 그 외 임시 문서 4개는 **모두 삭제**한다 (NRD_BLACK_OUTPUT_DEBUG.md / P5_PBR_RECOVERY.md 패턴 답습).

---

## 11. 종료 조건

- [ ] B/C/D 모든 sub-phase 통과
- [ ] AGENTS.md §4 슬롯 표 ↔ 코드 일치 확인
- [ ] STATUS.md §3에 Phase 6 변경 사항 정리
- [ ] HDRI 누락 fallback (`g_hasEnvMap == 0`) 경로 정상
- [ ] P4-6 정책 미위배 (raw/denoised 동일 ToneMap)
- [ ] 임시 문서 4종 삭제
- [ ] `P5_PBR_RECOVERY.md` 잔여 항목 처리 (C-4에서 P5-3a 닫혔다면 삭제)

---

**다음 액션**: `MERGE_STATUS.md` §2를 읽고 현재 sub-phase를 확인한 뒤, `P6_CAP_MERGE.md`에서 해당 §만 정독.
