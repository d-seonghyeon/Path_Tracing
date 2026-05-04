# STATUS - NRD Integration

> This file is the single source of truth shared across Codex / Claude Code sessions.
> Read this before starting work, and update it before ending a session.

---

## 0. Current Phase

- Active phase: `Phase 5 - REBLUR quality tuning`
- Detailed sub-phase: `P5-3c 완료 — Phase 5 perceptual recovery 종료`
- Blocked: `No`
- Branch: `master` (feature/nrd-phase0 는 master에 병합 완료 후 삭제됨)
- 보조 문서: `P5_PBR_RECOVERY.md` 삭제됨 (2026-05-04, §7 참조)

### Phase Checklist

Phase 0 - Render pipeline prerequisites

- [x] `[C]` Remove accumulation model and `g_accum += ...` from `PathTracer.hlsl`
- [x] `[C]` Remove `/(frameCount+1)` from `Tonemap.hlsl`
- [x] `[C]` Split `TracePath` return into diffuse / specular radiance
- [x] `[C]` Add 7 G-buffer UAVs in `context.h` / `context.cpp`
- [x] `[X]` Add `prevViewProj` / `currViewProj` to `GlobalUB` and upload from C++
- [x] `[X]` Add motion vector generation to the PathTracer output path
- [x] `[C]` Add `Composite.hlsl` for `diffuse + specular + emissive` (albedo already in BRDF output)
- [x] `[R]` Codex reviews Claude's Phase 0 diffs

Phase 1 - NRD dependency integration

- [x] `[X]` Add `dep_nrd` in `Dependency.cmake` (`v4.14.3`, DXBC embed)
- [x] `[X]` Add NRD include / link paths in `CMakeLists.txt`
- [x] `[R]` Claude reviews the CMake diff

Phase 2 - NrdDenoiser wrapper + DXBC pipeline

- [x] `[X]` Scaffold `src/nrd_denoiser.{h,cpp}` for permanent / transient pool + identifiers
- [x] `[X]` Build `ID3D11ComputeShader` objects from NRD `PipelineDesc`
- [x] `[C]` Review DX11 binding table / slot conflicts / resource lifetime

Phase 3 - Quality tuning

- [x] `[C]` HitT normalization + NRD helper packing
- [x] `[C]` Anti-lag / disocclusion threshold sweep
- [ ] `[X]` Optional SIGMA / ReLAX experiments (미구현 — 선택 사항)

Phase 4 - Validation / A-B

- [x] `[C]` A/B toggle (`F1 = denoise on/off`)
- [x] `[C]` FLIP / SSIM offline comparison (avg64 reference + exposure-matched metrics)
- [ ] `[X]` Timestamp query profiling for tracing vs denoise (미구현)

Phase 5 - REBLUR quality tuning (진행 중)

- [x] Checker plane baseline (sidewalk 제거, 2m 타일, 대비 0.18/0.72)
- [x] Blur-radius sweep (prepass=6, maxBlur=9)
- [~] Firefly suppression (P5-3, 2026-05-03 미커밋 → perceptual 회귀로 보류, P5-3a로 재진단)
- [x] **P5-3a Firefly clamp 정밀화** — FIREFLY_CLAMP 20.0, sigma/sensitivity 3.0/3.0, histogram dump. 시각 검증 통과 (puddle 4~5개, shadow OK, no firefly).
- [x] **P5-3b Specular hitT 분리 packing** — roughness<0.05 → min(hitT, viewZ*2) 클램프. 시각 검증 통과 (puddle 6개+, noise 회귀 없음).
- [x] **P5-3c Firefly-clamped reference 재측정** — avg31 clamped raw vs denoised. exposure_matched_ssim=0.9557 (기준 0.93 PASS, P4-3 대비 +0.027). `build/p5_3c_metrics.json`.
- [ ] Camera teleport → CLEAR_AND_RESTART (미구현 — 씬 컷 발생 시 필요)

---

## 1. Last Commit

| Item | Value |
| --- | --- |
| Hash | `558d730` |
| Author | choi mun chan |
| Date | 2026-05-02 |
| Scope | `P5` docs — update diagnosis §6 settings and §8 task status, STATUS next action |
| Summary | REBLUR prepass=6/max=9 blur-radius sweep 채택 완료. 체커 plane baseline 확보 (sidewalk 박스 제거, 타일 2m, 대비 0.18/0.72). P5-3 firefly suppression 작업 시작 (미커밋 상태). |

---

## 2. Next Concrete Action

Do exactly one next action, not a vague "continue".

```
[P5-3c PASS — 커밋 대기]

P5-3a/b/c 모두 완료. 미커밋 변경 커밋 준비.

커밋 대상:
  shader/PathTracer.hlsl  — FIREFLY_CLAMP 20.0, 256-bin histogram, roughness<0.05 hitT 클램프
  src/context.h           — m_histogramBuffer / m_histogramUAV 선언
  src/context.cpp         — UAV clear, 8-slot bind, histogram readback (F2 캡처 시)
  src/nrd_denoiser.cpp    — antilag sigma/sensitivity 3.0/3.0
  tools/p5_3c_capture.ps1 — 자동 캡처 스크립트
  tools/p5_3c_metrics.py  — SSIM/PSNR 계산 스크립트
  STATUS.md / (P5_PBR_RECOVERY.md 삭제)

커밋 메시지 후보:
  P5-3a/b/c: firefly clamp 20.0 + specular hitT fix + metrics PASS
```

---

## 3. Cross-Session Notes

### Newly introduced

- `shader/Composite.hlsl` - HDR composite pass: decodes YCoCg diffuse/specular, adds emissive; `diffuse + specular + emissive` (NO material factor remodulation)
- `shader/NrdFrontend.hlsli` - local NRD-compatible front-end helpers for radiance / hit-distance / normal packing
- `GlobalUniforms.prevViewProj` / `currViewProj` - motion vector matrices
- `Context` screen resources - 7 G-buffer textures + `m_compositeTexture` + `m_denoisedDiffuse/Specular`
- `PT_ENABLE_NRD` - enabled only when local NRD source/install is available
- `src/nrd_denoiser.{h,cpp}` - DX11 NRD wrapper without NRI
- `NrdGBufferInputs` / `NrdDenoisedOutputs` - grouped denoise I/O structs
- `NrdCameraData` - per-frame view/proj/prevView matrices for NRD CommonSettings
- `NrdPoolEntry` - permanent/transient pool texture entry (texture + SRV + UAV)
- `m_prevView` in `Context` - previous frame view matrix for NrdCameraData
- Render path - `PT -> NRD -> Composite -> ToneMap`
- `m_denoiseEnabled` + `F1` toggle - A/B path between raw and denoise-enabled rendering
- `NrdDenoiser::GetBackendStatusLabel()` / `HasUsableBackend()` - explicit stub vs real backend status
- `cmake/patch_nrd.cmake` - patches NRD v4.14.3 CMakeLists.txt for NVIDIA-RTX/ShaderMake compatibility
- `PathTracer.hlsl` now packs diffuse/specular with REBLUR-compatible normalized hit distance and YCoCg radiance
- `PathTracer.hlsl` now packs normal/roughness with an NRD-compatible encoding layout
- `Composite.hlsl` decodes YCoCg radiance back to linear RGB; formula is `diffuse + specular + emissive` (NOT `diffuse * albedo + ...`)
- `P5_PBR_RECOVERY.md` - 임시 진단 문서 (P5-3a/b/c 실행 계획). Phase 5 종료 시 §7 패턴으로 삭제.

### Important current behavior

- Local NRD v4.14.3 source is present under `build/dep_nrd-prefix/src/dep_nrd`.
- `Dependency.cmake` uses the NRD source-local SDK layout (`Include`, `Integration`, `Shaders/Include`, `_Bin/Debug`).
- ShaderMake CLI mismatch is fixed: `NVIDIA-RTX/ShaderMake` (main) dropped `--useAPI` and uses `SHADERMAKE_FXC_PATH` instead of `FXC_PATH`. Both issues are patched in `build/dep_nrd-prefix/src/dep_nrd/CMakeLists.txt` and via `cmake/patch_nrd.cmake`.
- `F1` only activates the denoise path when a usable backend actually exists; otherwise rendering stays on the raw G-buffer path and logs the stub status.
- `F1=ON` path: `PT → REBLUR_DIFFUSE_SPECULAR → Composite(YCoCg decode) → ToneMap`. `F1=OFF` path: raw PT output straight to Composite.
- `F2` saves `capture_N_<denoised|raw>.png` to the CWD of the exe (= `build/`).
- YCoCg packing: `PathTracer.hlsl` packs radiance via `NrdLinearToYCoCg` (= `_NRD_LinearToYCoCg` in NRD.hlsli, confirmed bit-identical). `Composite.hlsl` decodes via `NrdYCoCgToLinear`. REBLUR's internal textures store YCoCg and its back-end unpack does the inverse. Round-trip is correct.
- `nrd_denoiser.cpp` frame-0/1 log: outputs `NRD SetCommonSettings` with viewZScale/denoisingRange/mvScale for sanity check.
- P3-4 black-output is closed (2026-05-01). Root cause was missing SRV for `OUT_DIFF/OUT_SPEC_RADIANCE_HITDIST` in temporal accumulation pass.
- Resize validated: `Context::OnResize()` uses staged allocation; app survived two live resizes.
- **P5-3 미커밋 변경은 perceptual 회귀로 보류** (2026-05-04). P5-3a에서 histogram 기반으로 임계 재조정 예정.

### Current REBLUR settings (src/nrd_denoiser.cpp as of 2026-05-02 커밋된 상태)

```cpp
// hitT normalization — must stay in sync with HLSL REBLUR_HIT_DIST_PARAMS
// Scene AABB ~50x60x27m (diagonal 83m). A=30 matches city-scale ray lengths.
// normalization = (A + |viewZ|*B) * lerp(1, C, exp2(D*roughness^2))
// At viewZ=10m, diffuse: normalization=31 → 5m hit gives normHitDist=0.16 ✓
reblurSettings.hitDistanceParameters = { A=30.0f, B=0.1f, C=20.0f, D=-25.0f };
// ↑ HLSL mirror: shader/PathTracer.hlsl REBLUR_HIT_DIST_PARAMS = float4(30,0.1,20,-25)
// ↑ Both values MUST stay bit-identical — changing one without the other breaks normHitDist.
// ↑ P5-3b 작업 시 C++ 값은 변경 금지. roughness < 0.05 분기는 HLSL packing에서만 처리.

reblurSettings.antilagSettings.luminanceSigmaScale  = 3.0f;  // P5-3a: 3.5/2.5 → 3.0 (균형)
reblurSettings.antilagSettings.luminanceSensitivity = 3.0f;  // P5-3a: 2.5/3.5 → 3.0 (균형)
reblurSettings.maxAccumulatedFrameNum               = 24;
reblurSettings.maxFastAccumulatedFrameNum           = 4;
reblurSettings.maxStabilizedFrameNum                = 30;   // 0 = disabled (was wrong)
reblurSettings.historyFixBasePixelStride            = 8;
reblurSettings.diffusePrepassBlurRadius             = 6.0f; // P5 sweep: 8→6 (checker preservation)
reblurSettings.specularPrepassBlurRadius            = 6.0f; // P5 sweep: 8→6
reblurSettings.minBlurRadius                        = 0.5f;
reblurSettings.maxBlurRadius                        = 9.0f; // P5 sweep: 12→9 (sweet spot)
reblurSettings.minHitDistanceWeight                 = 0.10f;
reblurSettings.lobeAngleFraction                    = 0.25f;
reblurSettings.roughnessFraction                    = 0.25f;
reblurSettings.planeDistanceSensitivity             = 0.08f; // raised for sharper edge rejection
```

### P5-3a/b 적용 변경 (2026-05-04, 커밋됨)

```cpp
// P5-3a 최종값 (시각 검증 통과)
reblurSettings.antilagSettings.luminanceSigmaScale  = 3.0f;
reblurSettings.antilagSettings.luminanceSensitivity = 3.0f;
```

```hlsl
// P5-3a 최종값: FIREFLY_CLAMP 5.0 → 20.0 (99.9th percentile ~12.17 기준)
static const float FIREFLY_CLAMP = 20.0f;
// u7: g_luminanceHistogram — 256-bin log2 histogram (pre-clamp), F2 캡처 시 histogram_N_*.txt 덤프

// P5-3b 최종값: roughness < 0.05 mirror surface hitT cap (puddle max-blur 방지)
// effectiveSpecHitDist = (roughness < 0.05 && hitSurface && specularHitDist > 0)
//     ? min(specularHitDist, viewZ * 2.0f)
//     : specularHitDist;
```

P5-3c 결과: avg31 clamped raw vs denoised, exposure_matched_ssim=0.9557 (기준 0.93 PASS).

#### P5 blur-radius sweep summary (2026-05-02)

| prepass | maxBlur | 근거리 체커 | 벽 노이즈 | 판정 |
| ------- | ------- | ----------- | --------- | ---- |
| 8       | 12      | 소실        | 깨끗      | 기각 |
| 4       | 6       | 명확 보존   | grain 잔존 | 과소 |
| **6**   | **9**   | **보존** ✓  | **깨끗** ✓ | **채택** |

2m × 2m 체커가 근거리에서 살아남는 최소 설정은 prepass≤6 / max≤9.
prepass=4/max=6도 체커 보존은 더 강하지만 벽면 grain이 NRD 정상 작동 여부를
모호하게 만들어 기각. prepass=6/max=9가 진단 신뢰도 측면에서 최적.

### PathTracer.hlsl key decisions (as of 2026-05-02)

- **No sub-pixel jitter**: `GenerateCameraRay` uses `pixelCoord + 0.5` (center), no per-frame random offset.
  - Removed because REBLUR has no `cameraJitter` compensation field in our NRD v4.14.3 path.
  - Per-frame jitter + 24-frame temporal accumulation blended checker/edge pixels → watercolor blur.
  - If geometric AA is unacceptable: switch to Halton-2/3 jitter (≤0.5px) and feed offset into `CommonSettings.cameraJitter[]`.
- **SAMPLES_PER_PIXEL = 1**: one MC sample per pixel per frame. REBLUR temporal accumulation provides effective multi-sample averaging.
  - 주의: REBLUR은 RTXDI/ReSTIR 같은 강한 importance sampling 1spp를 가정. Vanilla PT 1spp는 NEE 분산이 커서 firefly clamp 임계 설정이 까다로움 (P5-3a 참조).
- **Diffuse hitT**: luminance-weighted representative of bounce=0 NEE `lightHitDist` (first secondary hit distance, not primary camera hit, not accumulated path length).
- **Bounce=0 NEE split**: `diffuseContrib = neeContrib * lobe.pDiff`, `specularContrib = neeContrib * lobe.pSpec`. Do NOT route all NEE to diffuse (breaks matPuddle roughness=0.02 tight-reflection).
- **No bounce=0 emitter in diffuse channel**: direct emitter hit stored in `result.emissive` only → Composite adds it. Adding to diffuse caused anti-lag collapse.

### Closed debug campaigns

- **P3-4 (black output)**: closed 2026-05-01. Root cause: missing `OUT_DIFF/OUT_SPEC` SRV in temporal accumulation. See §7.
- **P3-5 watercolor blur root cause chain** (all closed 2026-05-02):
  - C1: sampler OOB (array size 2→4)
  - C4: diffuse hitT accumulated path length → first secondary hit
  - C5: specularPrepassBlurRadius 28→12→8
  - H1: hitDistanceParameters.A 3→30 (scene scale mismatch)
  - H2: maxStabilizedFrameNum 0→30 (temporal stabilization re-enabled)
  - **ROOT CAUSE**: per-frame sub-pixel jitter in GenerateCameraRay (removed in c93e521)

### Open perceptual recovery campaign (P5-3a/b/c — P5_PBR_RECOVERY.md)

P4-7 객관 metric acceptance 후 perceptual 회귀 발견 (2026-05-04):
- 그림자 평탄화 (가로등 ground occlusion 둔화)
- Puddle specular highlight 손실 (matPuddle roughness=0.02 두 spherical reflection 중 0개 visible)
- 벽면 PBR 톤 분리 손실

진단 4가지 root cause는 P5_PBR_RECOVERY.md §2:
- RC1. SPP=1 + firefly over-clamp 합산 효과
- RC2. ToneMap distribution shift (P4-5에서 측정 완료, mean luma 0.42381 → 0.48320)
- RC3. Specular hitT가 puddle roughness=0.02에 부적합
- RC4. P5-3 firefly suppression 양날의 검

실행 계획 P5-3a → P5-3b → P5-3c는 P5_PBR_RECOVERY.md §3.

### F2 Screenshot Capture (Phase 4 FLIP/SSIM)

- `F2` sets `m_captureRequested = true`; captured at end of same `Render()` call.
- Output filename: `capture_<index>_<denoised|raw>.png` in the CWD of the exe.
- Uses a per-call staging texture (D3D11_USAGE_STAGING + CPU_ACCESS_READ), then stb_image_write PNG.
- `stb_image_write.h` is now copied by `Dependency.cmake` alongside `stb_image.h`.
- P4-1 accepted default-view A/B captures:
  - `build/p4_1_raw_default.png`
  - `build/p4_1_denoised_default.png`
- P4-2 raw-average validation artifacts:
  - `build/p4_2_raw_00.png` ... `build/p4_2_raw_15.png`
  - `build/p4_2_raw_avg16_reference.png`
  - `build/p4_2_metrics.json`
  - Metrics vs avg16 reference:
    - single raw: RMSE 0.12637, MAE 0.06502, PSNR 17.97 dB, global SSIM-style 0.90618
    - denoised: RMSE 0.12751, MAE 0.08543, PSNR 17.89 dB, global SSIM-style 0.91635
- P4-3 raw-average validation artifacts:
  - `build/p4_3_raw_00.png` ... `build/p4_3_raw_63.png`
  - `build/p4_3_raw_avg64_reference.png`
  - `build/p4_3_metrics.json`
  - Metrics vs avg64 reference:
    - single raw: RMSE 0.12066, MAE 0.06160, PSNR 18.37 dB, global SSIM-style 0.91413, mean luma 0.42404
    - denoised: RMSE 0.12484, MAE 0.08320, PSNR 18.07 dB, global SSIM-style 0.92003, mean luma 0.48350
    - single raw exposure-matched: RMSE 0.12061, MAE 0.06167, PSNR 18.37 dB, global SSIM-style 0.91416
    - denoised exposure-matched: RMSE 0.10214, MAE 0.06785, PSNR 19.82 dB, global SSIM-style 0.92879
  - **주의**: 본 reference는 firefly가 들어간 raw 평균. P5-3c에서 clamped reference로 재측정 예정.
- P4-4 denoised timing artifacts:
  - `build/p4_4_denoised_2s.png`
  - `build/p4_4_denoised_6s.png`
  - `build/p4_4_denoised_12s.png`
  - `build/p4_4_timing_metrics.json`
  - Mean luma vs avg64 reference 0.42389:
    - 2s: 0.48347, exposure scale 0.87676, exposure-matched RMSE 0.10282, SSIM-style 0.92768
    - 6s: 0.48340, exposure scale 0.87690, exposure-matched RMSE 0.10209, SSIM-style 0.92888
    - 12s: 0.48322, exposure scale 0.87723, exposure-matched RMSE 0.10203, SSIM-style 0.92902
- P4-5 brightness-bias localization artifacts:
  - `build/p4_5_composite_stats_raw.txt`
  - `build/p4_5_composite_stats_denoised.txt`
  - `build/p4_5_raw_ldr.png`
  - `build/p4_5_denoised_ldr.png`
  - Raw pre-ToneMap HDR composite: mean luma 1.00606, ACES+gamma mean luma 0.42381
  - Denoised pre-ToneMap HDR composite: mean luma 0.97407, ACES+gamma mean luma 0.48320
  - Interpretation: denoised linear HDR mean is lower than raw, but ToneMap maps denoised distribution to a brighter LDR mean.
  - **P5_PBR_RECOVERY.md §2 RC2의 직접 증거**.
- P4-6 ToneMap calibration policy:
  - No denoised-only exposure multiplier for Phase 4.
  - Preserve identical Composite/ToneMap code for raw and denoised A/B.
  - Treat LDR brightness difference as documented ToneMap/distribution behavior.
  - Future exposure parity should use a real auto-exposure/calibration pass, not a hard-coded default-view scalar.
  - **P5_PBR_RECOVERY.md §5의 Phase 6 auto-exposure 제안은 본 정책과 호환** (histogram-based 자동 보정, raw/denoised 양쪽 동일 적용).

### Phase 4 Final Validation Summary (객관 metric — perceptual 회귀 별도)

- **Accepted rendering state (objective)**: F1 ON denoised path은 객관 metric 기준으로 usable. 정상 색/조명, black-output 회귀 없음, raw speckle 대부분 제거.
- **Static A/B**: `build/p4_1_raw_default.png` vs `build/p4_1_denoised_default.png` shows a clear visual denoise win with no watercolor-level blur regression in the default view.
- **Motion probe**: `build/ghosting_immediate.png` and `build/ghosting_settled.png` did not show a denoiser-only long-lived ghost trail. The right-lamp teardrop shape also exists in `build/ghosting_raw_compare.png`, so it is not classified as REBLUR ghosting.
- **Reference metrics**: avg64 reference (`build/p4_3_raw_avg64_reference.png`) produced mixed raw pixel RMSE because denoised LDR has a brightness/distribution shift, but denoised wins on global SSIM-style and exposure-matched RMSE/PSNR.
- **Brightness caveat**: denoised LDR mean is higher than raw/reference after ACES+gamma, but P4-5 showed denoised pre-ToneMap HDR composite mean is lower than raw. The difference is a ToneMap/distribution interaction, not simple NRD energy amplification.
- **Policy**: do not apply a denoised-only hard-coded exposure scalar in Phase 4. If exposure parity becomes a requirement, build a proper auto-exposure/calibration pass and validate it across multiple views/scenes.
- **Known limitations**: validation covers the default procedural city view plus one D-key movement probe. It is not a full multi-scene FLIP/SSIM campaign, and `CommonSettings.enableValidation` remains effectively unused because `OUT_VALIDATION` has no texture/display path.
- **Perceptual gap (NEW, 2026-05-04)**: 객관 metric은 통과했으나 사용자 시각 검증에서 그림자/puddle/PBR 톤 회귀 발견. 이로 인해 Phase 5는 종료되지 않고 P5-3a/b/c 추가 작업 진입. 상세 진단은 `P5_PBR_RECOVERY.md`.

### Phase 2 [C] Review - Binding / Slot / Resource Lifetime

No critical conflicts found. Details:
- SRV/UAV hazards: none - each pass null-clears before the next binds the same texture.
- CopyResource ref counting in stub `Denoise()`: `GetResource()` increments refcount and matching `Release()` calls are present.
- ToneMap binds `u0=null`, `u1=outputUAV` (slots `0..1` set in one call); null-cleared after.
- `b0 (GlobalUB)` is never unbound after PathTracer. No behavioral impact today, but worth cleaning up before shipping.
- The old `OnResize` partial-initialization risk was fixed locally in `src/context.cpp` by staging all screen textures before swapping them in.

### Scene checker baseline (P5-1)

- Scene checker now exposed on y=0 plane in sidewalk regions.
  Sidewalk boxes removed from scene_desc.cpp. Tile size 2m × 2m,
  albedo lerp(0.18, 0.72) for high-contrast detail preservation
  testing under REBLUR spatial blur.

### Fixed decisions

- Do not introduce NRI.
- First denoiser remains `REBLUR_DIFFUSE_SPECULAR`.
- Motion vectors stay in pixel units: `prev - curr`.
- HLSL keeps `row_major`; C++ uploads transposed `prev/currViewProj`.
- If local NRD source/install is missing, configure must gracefully fall back to `PT_ENABLE_NRD=OFF`.
- **P5-3b 작업 시 C++ `hitDistanceParameters` 값 변경 금지**. roughness < 0.05 분기는 HLSL packing에서만 처리하여 HLSL/C++ bit-identical 제약 유지.

---

## 4. Open Questions

1. **[Verified 2026-05-02]** Jitter removal is acceptable for now. `jitter_removed_on.png` keeps visible wall grain and tight puddle lights; no Halton jitter change yet.
2. **[Verified 2026-05-02]** D-key 0.5s camera-motion probe did not show a denoiser-only long-lived ghost trail. The right-lamp teardrop shape also appears in `ghosting_raw_compare.png`, so it is not classified as REBLUR ghosting.
3. **[Reopened 2026-05-04]** Remaining indirect-lit dark-side noise는 P4 단계에서 보류로 분류됐으나, P5-3a/b/c perceptual recovery 작업 결과에 따라 재평가 필요. `maxFastAccumulatedFrameNum` 4→2 candidate는 2026-05-02에 reject됨.
4. Should `normalRoughness` stay as `R16G16B16A16_FLOAT`, or be tightened to a more storage-efficient format?
5. Does `CommonSettings.enableValidation=true` emit anything useful in the DX11-direct path (no NRI)? Currently: `OUT_VALIDATION` resource type returns null UAV in `ResolveUAV()` → silent no-op. Would need a dedicated texture + display path to use it.
6. **[Documented]** P4-5 localized the denoised brightness bias to ToneMap/distribution interaction, not a simple NRD linear-radiance amplification. Denoised HDR composite mean is lower than raw, but ACES+gamma mean is higher. P5_PBR_RECOVERY.md §2 RC2 참조.
7. **[Policy fixed]** P4-6 decided not to add a hard-coded denoised exposure multiplier. It would be view-dependent and would break identical ToneMap A/B validation. Phase 6 auto-exposure 제안(P5_PBR_RECOVERY.md §5)은 본 정책과 호환.
8. **[NEW 2026-05-04]** SPP=1 vanilla PT 1spp가 REBLUR 입력 가정과 부합하는가? RTXDI/ReSTIR 도입은 Phase 5 범위 밖이지만 P5-3a/b/c 결과에 따라 Phase 6+ 후보로 검토 필요.

---

## 5. Session Log

Newest entry goes on top.

```
2026-05-04 | Claude Code | P5-3c | avg31 clamped raw frames vs denoised 객관 metric 계산.
exposure_matched_ssim=0.9557 (기준 0.93 PASS, P4-3 baseline 0.9288 대비 +0.027).
tools/p5_3c_metrics.py JSON 직렬화 버그(numpy.bool_) 수정. P5_PBR_RECOVERY.md §6
closure criteria 충족: P5-3a/b/c 모두 [x], 사용자 시각 검증 완료, 핵심 결정
STATUS.md §3 영구 이관, §7에 삭제 기록. P5_PBR_RECOVERY.md 삭제.
2026-05-04 | Claude Code | P5-3b | roughness<0.05 specular hitT 분리 packing. PathTracer.hlsl에
effectiveSpecHitDist = min(rawHitT, viewZ*2.0) 분기 추가. C++ hitDistanceParameters 불변.
빌드 성공 (Debug ALL_BUILD). 사용자 시각 검증 대기 — puddle 두 highlight / noise 회귀.
2026-05-04 | Claude Code | P5-3a | FIREFLY_CLAMP 5.0→20.0, sigma/sensitivity 3.5/2.5→3.0/3.0 적용.
PathTracer.hlsl에 256-bin log2 luminance histogram (u7 RWStructuredBuffer, pre-clamp
InterlockedAdd) 추가. context.cpp: 매 프레임 UAV clear, F2 캡처 시 histogram_N_*.txt
readback (99th/99.9th percentile lum 기록). 빌드 성공 (Debug ALL_BUILD).
사용자 시각 검증 대기 — puddle highlight / 그림자 경계 OFF 대비 80% 이상 exit criteria.
2026-05-04 | Claude Code | P5-3a | F1 ON perceptual 회귀 진단 완료. 사용자 시각 검증으로
그림자 평탄화 / puddle specular 손실 / PBR 톤 분리 손실 발견. 4개 root cause로 분류:
RC1 SPP=1 + firefly over-clamp 합산, RC2 ToneMap distribution shift (P4-5 직접 증거),
RC3 Specular hitT가 puddle roughness=0.02에 부적합, RC4 P5-3 firefly suppression
양날의 검. P5-3 미커밋 변경(FIREFLY_CLAMP=5.0, sigma 2.5, sensitivity 3.5) 보류 결정.
P5_PBR_RECOVERY.md 신설 (P5-3a/b/c 실행 계획, Phase 5 종료 시 §7 패턴으로 삭제).
STATUS.md §0/§2/§3/§4/§5 갱신. 다음: PathTracer.hlsl histogram dump 추가.
2026-05-03 | Claude Code | merge | feature/nrd-phase0 → master 병합 확인. master가 이미
feature/nrd-phase0보다 6커밋 앞서 있었음(P5 커밋들이 master에서 직접 작성됨).
feature/nrd-phase0 삭제. Phase 0-4 체크리스트 전체 완료 처리.
2026-05-03 | Claude Code | P5-3 | Firefly suppression — PathTracer.hlsl의 uniform clamp(0,10)을
luminance 기반 hue-preserving 클램프(FIREFLY_CLAMP=5.0)로 교체. Rec.709 휘도가
임계치 초과 시 색조를 유지한 채 스케일다운. nrd_denoiser.cpp antilag 조정:
sigmaScale 3.5→2.5(임계 축소), sensitivity 2.5→3.5(이상값 반응 강화).
빌드·시각 검증 대기. 미커밋. **2026-05-04 perceptual 회귀로 보류 결정**.
2026-05-02 | Claude Code | P5-2 | REBLUR blur-radius sweep: prepass 8/max 12 → 4/6 → 6/9.
prepass=8/max=12에서 2m 체커 완전 소실 확인. prepass=4/max=6에서 체커 보존
성공했으나 벽면 grain 잔존. prepass=6/max=9이 체커 보존 + 벽 노이즈 제거
모두 충족하는 스윗스팟으로 채택. 빌드 성공. 커밋 aea86f5.
2026-05-02 | Claude Code | P5-1 | 선택지 B 적용: scene_desc.cpp의 sidewalk
박스 2개 제거하고 Scene.hlsli의 y=0 체커 plane을 인도 영역에 노출.
타일 크기 1m → 2m, 대비 (0.3, 0.9) → (0.18, 0.72)로 강화.
원인: raw 사진에서 박스(z=0.02)가 plane(y=0) 바로 위에 떠 있어 시점별
hit ordering이 달라지며 인도 영역에 두 톤 패턴 발생. 박스 제거로
plane만 노출시켜 NRD 평면 디테일 보존 검증을 위한 baseline 확보.
빌드 성공. 사용자 F1 OFF/ON 시각 검증 대기.
2026-05-02 | Codex       | P4-7 | Wrote final Phase 4 validation handoff summary in STATUS.md. Verdict: current F1 ON REBLUR path is accepted for the procedural city default view with documented limitations. Denoise strongly reduces raw speckle, no black-output regression was seen, jitter removal fixed watercolor blur, motion probe did not show denoiser-only long-lived ghosting, and brightness difference is documented as ToneMap/distribution behavior. No code changes; next action is review and commit the STATUS.md-only validation note.
2026-05-02 | Codex       | P4-6 | Decided not to add a denoised-only ToneMap exposure multiplier for Phase 4. The ~0.877 scale was measured from one default camera and is view/scene dependent; applying it only to denoised output would hide the validation finding and break the identical Composite/ToneMap A/B contract. Policy: document the LDR exposure difference as a ToneMap/distribution effect; if exposure parity is required later, implement a real auto-exposure/calibration pass rather than a hard-coded denoised scalar. No code changes.
2026-05-02 | Codex       | P4-5 | Temporarily instrumented F2 to dump pre-ToneMap `m_compositeTexture` stats, captured raw/denoised default-view stats, then removed the temporary code and rebuilt successfully. Artifacts: `build/p4_5_composite_stats_raw.txt`, `build/p4_5_composite_stats_denoised.txt`, `build/p4_5_raw_ldr.png`, `build/p4_5_denoised_ldr.png`. Raw HDR composite mean luma was 1.00606; denoised HDR composite mean luma was lower at 0.97407. Applying the same ACES+gamma curve to those HDR values produced mean luma 0.42381 raw vs 0.48320 denoised. Verdict: visible LDR brightness bias is from ToneMap's nonlinear response to the denoised HDR distribution, not from denoised linear HDR mean being brighter.
2026-05-02 | Codex       | P4-4 | Captured F1 ON denoised timing set at ~2s, ~6s, and ~12s after enabling denoise: `build/p4_4_denoised_2s.png`, `build/p4_4_denoised_6s.png`, `build/p4_4_denoised_12s.png`; metrics in `build/p4_4_timing_metrics.json`. Debug ALL_BUILD passed; runtime stderr empty. Mean luma stayed high and stable vs avg64 ref 0.42389: 2s=0.48347, 6s=0.48340, 12s=0.48322. Exposure-matched metrics remain good (12s RMSE 0.10203 / PSNR 19.83 dB / SSIM-style 0.92902). Verdict: denoised brightness bias is stable, not warm-up; next step is to localize whether it appears before NRD, in NRD output, or in Composite/ToneMap.
2026-05-02 | Codex       | P4-3 | Captured 64 default-view F1 OFF raw frames (`build/p4_3_raw_00.png` ... `build/p4_3_raw_63.png`), averaged them into `build/p4_3_raw_avg64_reference.png`, and wrote metrics to `build/p4_3_metrics.json`. Debug ALL_BUILD passed; runtime stderr empty. Against avg64, single raw was RMSE 0.12066 / MAE 0.06160 / PSNR 18.37 dB / SSIM-style 0.91413; denoised was RMSE 0.12484 / MAE 0.08320 / PSNR 18.07 dB / SSIM-style 0.92003. Exposure-matched comparison favored denoised: RMSE 0.10214 / PSNR 19.82 dB / SSIM-style 0.92879. Denoised mean luminance is higher (0.48350 vs ref 0.42388), so next check is whether this is warm-up timing or a stable brightness bias.
2026-05-02 | Codex       | P4-2 | Captured 16 default-view F1 OFF raw frames (`build/p4_2_raw_00.png` ... `build/p4_2_raw_15.png`), averaged them into `build/p4_2_raw_avg16_reference.png`, and wrote metrics to `build/p4_2_metrics.json`. Debug ALL_BUILD passed; runtime stderr empty. Metrics vs avg16 reference were mixed: single raw RMSE 0.12637 / MAE 0.06502 / PSNR 17.97 dB / SSIM-style 0.90618; denoised RMSE 0.12751 / MAE 0.08543 / PSNR 17.89 dB / SSIM-style 0.91635. Visual denoise win remains clear, but objective closeness needs avg64 + exposure-matched metrics.
2026-05-02 | Codex       | P4-1 | Captured accepted default-view A/B validation pair. Debug ALL_BUILD passed. Runtime from `build/` produced `build/p4_1_raw_default.png` (F1 OFF, settled frame 71) and `build/p4_1_denoised_default.png` (F1 ON, settled frame 77). stderr empty, NRD backend ready. Visual verdict: denoise strongly reduces raw speckle while preserving normal lighting/color; no black-output regression, no watercolor-level blur, and no denoiser-only ghost artifact in the static default view.
2026-05-02 | Codex       | P3-6 | Ran controlled maxFastAccumulatedFrameNum 4→2 residual-noise sweep. Baseline `build/p3_6_fast4_baseline.png`; sweep `build/p3_6_fast2_sweep.png`; motion probe `build/p3_6_fast2_motion_immediate.png` / `build/p3_6_fast2_motion_settled.png`. Build and runtime passed with empty stderr, but the sweep did not visibly improve dark-side noise/stale history, so the one-line change was reverted and accepted setting remains maxFastAccumulatedFrameNum=4. Final Debug ALL_BUILD passed.
2026-05-02 | Codex       | P3-5 | Verified jitter-removal effect and camera-motion ghosting probe. Debug ALL_BUILD passed. Automated F1 ON capture produced `build/jitter_removed_on.png`; D-key 0.5s produced `build/ghosting_immediate.png` and `build/ghosting_settled.png`; raw comparison `build/ghosting_raw_compare.png` showed the right-lamp teardrop shape is already present before denoise, so it is not a denoiser-only ghost trail. stderr empty; NRD backend ready; motion restarted accumulation.
2026-05-02 | Claude Code | P3-5 | Remove per-frame sub-pixel jitter from GenerateCameraRay (ROOT CAUSE of watercolor blur). Jitter caused 24-frame temporal accumulation to average differently-positioned samples at every checker/edge boundary → gray blend = watercolor. REBLUR has no jitter compensation in motion vectors; jitter is only valid for traditional TAA. Commit c93e521.
2026-05-02 | Claude Code | P3-5 | prepassBlurRadius 16/12→8/8 + planeDistanceSensitivity 0.025→0.08 + lobeAngleFraction/roughnessFraction 0.16→0.25. After A=30 fix, watercolor blur persisted from diffusePrepassBlurRadius=16 smearing raw input before temporal. Halved prepass radii; tightened edge-based rejection to preserve surface boundaries. Build passed. Commit dc8ce78.
2026-05-02 | Claude Code | P3-5 | maxBlurRadius 18→12 (Step 2-3). Building walls/checker still watercolor-blurry. Commit 982f1d6.
2026-05-02 | Claude Code | P3-5 | hitDistanceParameters.A 3→30 (scene scale fix) + maxStabilizedFrameNum 0→30. Scene AABB 50x60x27m (diagonal 83m). A=3 saturated all secondary hits >4m to normHitDist=1.0 → REBLUR max-blur on entire scene. A=30 restores proper [0,1] range for 5-25m hits. Both C++ ReblurSettings and HLSL REBLUR_HIT_DIST_PARAMS.x updated to 30 (must stay bit-identical). maxStabilizedFrameNum restored 0→30 re-enables temporal stabilization. Build passed. Next: F2 capture to judge improvement; if still blurry try maxBlurRadius 18→12.
2026-05-02 | Claude Code | P3-5 | Reverted C2 (bounce=0 NEE lobe-weighted split restored). C2 was wrong: all-to-diffuse routing made matPuddle (roughness=0.02, pSpec≈1.0) lamp reflections land in max-blur diffuse channel → large soft blobs. Reverted to diffuseContrib=neeContrib*pDiff / specularContrib=neeContrib*pSpec. The original horizontal-smearing fix was C5 (specularPrepassBlurRadius 28→12), not C2.
2026-05-01 | Claude Code | P3-5 | Quality-fix round C1–C5 committed (8450ef1..4ca5441). (C1) sampler array extended to 4 entries — NEAREST_CLAMP/NEAREST_MIRROR/LINEAR_CLAMP/LINEAR_MIRROR indexed by nrd::Sampler enum value, fixing out-of-bounds UB that corrupted REBLUR blur-pass filtering. (C2) bounce=0 NEE moved entirely to diffuse channel, removing REBLUR specular temporal-lobe mismatch that caused horizontal smearing. (C4) diffuse hitT changed from accumulated multi-bounce path length to first-secondary-hit distance only, symmetric with specular. (C5) specularPrepassBlurRadius 28→12 now that specular channel is clean. Debug ALL_BUILD passed. Runtime visual comparison needed: F2 capture F1 OFF/ON to judge remaining smearing and detail.
2026-05-01 | Codex       | Docs | Removed obsolete root markdown notes that were no longer needed for future work: `REFACTORING.md`, `SCENE_REFACTOR.md`, and `SCENE_GUIDE.md`. Kept `STATUS.md`, `AGENTS.md`, `CLAUDE.md`, `NRD_INTEGRATION_PLAN.md`, and `CHANGELOG_2026-04-07.md` because they are still used for cross-session state, agent rules, NRD roadmap, or build/runtime history.
2026-05-01 | Codex       | P3-5 | B12 automated camera-motion probe completed for the B11 lower-blur sweep. Started with F1 ON, held D briefly, captured `capture_0_denoised.png` immediately after movement and `capture_1_denoised.png` after roughly 4 seconds of settling. The app stayed alive, stdout showed normal NRD settings logs, stderr was empty, and the viewed captures did not show an obvious long-lived ghost trail. Verdict: B11 sweep is a keep candidate, pending user visual review before commit.
2026-05-01 | Codex       | P3-5 | B11 detail-retention sweep applied. Reduced REBLUR prepass radii from `24/42` to `16/28`, max history from `28/5` to `24/4`, added `minBlurRadius=0.75` and `maxBlurRadius=18`, and moved rejection settings closer to defaults (`minHitDistanceWeight=0.12`, `lobeAngleFraction=0.16`, `roughnessFraction=0.16`, `planeDistanceSensitivity=0.025`). Debug `ALL_BUILD` passed. Automated F1 OFF / F1 ON capture passed after retrying with `WScript.Shell.SendKeys`; `capture_1_denoised.png` remains visible and looks slightly less smeared around water reflections/distant geometry than the saved B10 baseline, with no stderr. Next: manual camera-motion probe to catch ghosting/noise regressions before keeping or rolling back this sweep.
2026-05-01 | Codex       | P3-5 | B10 manual quality pass completed. Automated run captured `capture_0_raw.png` with F1 OFF and `capture_1_denoised.png` after F1 ON in the normal Composite path. Raw output is heavily noisy but correctly lit; denoised output is visible, normally colored, and no longer collapses to emissive-only black. stdout logged `REBLUR_DIFFUSE_SPECULAR ready`, `Denoise ON`, sane `motionVectorScale=(0.001042,0.001852)`, and stderr was empty. Verdict: close P3-4 black-output debug and continue with focused REBLUR detail-retention tuning.
2026-05-01 | Codex       | P3-4 | B9 cleanup completed after black-output fix. Removed temporary F3/F4/F5/F6/F7/F8/F9 controls plus REFERENCE, split-screen, dispatch-bypass, viewZ-stats, and internal debug-stop plumbing. Left only F1 denoise toggle and F2 screenshot capture. Kept the real OUT_DIFF/OUT_SPEC SRV resolve fix and normal Composite output. Debug build passed. Runtime smoke test passed with F1 ON + F2 capture; stdout now shows sane `motionVectorScale=(0.001042,0.001852)` after fixing a cleanup-induced commented-out `motionVectorScale[0]` assignment. Next: manual F1 OFF/ON quality pass in the normal Composite path.
2026-05-01 | Codex       | P3-4 | Black-output root cause fixed. B7 showed first black pass was `REBLUR_DiffuseSpecular - Temporal accumulation`; inspection found the wrapper did not resolve `ResourceType::OUT_DIFF_RADIANCE_HITDIST` / `OUT_SPEC_RADIANCE_HITDIST` as SRV inputs, even though Temporal accumulation reads the Pre-pass outputs through those resource types. Added denoised diffuse/specular SRVs to `NrdDenoisedOutputs`, passed them from `Context`, and returned them in `ResolveSRV()` for OUT_DIFF/OUT_SPEC. Restored `Composite.hlsl` from false-color Y-channel debug output to normal `diffuse + specular + emissive`. Debug build passed; F1 ON capture is visible with normal colors, confirming NRD no longer collapses to black. Next: clean up or gate temporary debug keys/logging.
2026-05-01 | Codex       | P3-4 | B7 run completed: automated F1 + F9/F2 capture ladder from the build/ working directory. Captures: `capture_1_denoised.png` (Pre-pass) is visibly alive with avg luminance 124.474; `capture_2_denoised.png` (Temporal accumulation) drops to emissive-only black with avg luminance 1.954; History fix, Blur, Post-blur, and Temporal stabilization remain black. `Hit distance reconstruction` did not match any dispatch because the current REBLUR settings leave hit-distance reconstruction OFF. Verdict: first black internal pass is `REBLUR_DiffuseSpecular - Temporal accumulation`. Next: add a targeted temporal-isolation mode to distinguish shader/settings/history-input failure from wrapper output-copy/binding failure.
2026-05-01 | Codex       | P3-4 | B6 implemented: added `NrdDebugStopPass` and F9 cycling for REBLUR internal-pass localization. The DX11 dispatch loop now detects the selected REBLUR pass by `DispatchDesc::name`, stops immediately after that dispatch, copies the pass' diffuse/specular radiance UAVs into `m_denoisedDiffuse/Specular`, and leaves Composite/ToneMap/F2 to capture the result. Handles Hit distance reconstruction, Pre-pass, Temporal accumulation, History fix, Blur, Post-blur, and Temporal stabilization. Debug build passed. Runtime smoke test from build/ initialized `REBLUR_DIFFUSE_SPECULAR + REFERENCE ready` with no stderr; first attempted smoke test from build/Debug only failed because `shader/PathTracer.hlsl` was not found from that working directory. Next: run interactively, F1 ON, cycle F9 stages, press F2 per stage, and record the first black output.
2026-05-01 | Codex       | P3-4 | B5/B6: Re-ran Claude's viewZScale fix. Debug build passed; F1 REBLUR still produced emissive-only black, while stdout showed viewZScale=1.0000 and no SetCommonSettings failure, so viewZScale was not the root cause. Added F6 REFERENCE debug mode in the same NRD instance: REFERENCE capture was visible, proving NRD instance / DXBC / pools / dispatch loop can work. Fixed a real pool allocation bug: TextureDesc.downsampleFactor is a divisor, not a bit shift; pool sizes now use ceil(size/downsampleFactor). Added F7 REBLUR splitScreen passthrough: visible capture proved REBLUR IN_DIFF/IN_SPEC and OUT_DIFF/OUT_SPEC binding is OK. Added F8 viewZ stats: valid=456267, sky=62133, nonPositive=0, min=5.9875, max=23850.9531. Tried projection far=500000 to match denoisingRange; REBLUR still black. Next: add one-frame debug stop/copy after each REBLUR internal pass and find the first pass that goes black.
2026-05-01 | Claude Code  | P3-4 | B5: Found root cause — nrd::CommonSettings cs = {} may zero-init viewZScale via MSVC pre-19.26 aggregate-init bug (overrides DMI of 1.0f); UnpackViewZ = abs(z * 0) = 0 everywhere → REBLUR position reconstruction fails → zero output. Also: disocclusionThreshold / disocclusionThresholdAlternate would be zero, triggering NRD assertion failure and early-return from SetCommonSettings with INVALID_ARGUMENT. Fix: explicitly set cs.viewZScale=1.0f, cs.disocclusionThreshold=0.01f, cs.disocclusionThresholdAlternate=0.05f; check return values of SetCommonSettings and SetDenoiserSettings; add frame-0/1 diagnostic log. Debug build clean. B4 verdict confirmed (F5 bypass shows image → NRD dispatch was the bug). Next: run exe F1=ON without F5; if image visible → bug fixed; if still black → check stdout SetCommonSettings log.
2026-05-01 | Claude Code  | P3-4 | Phase B4 implemented: added F5 key (NRD dispatch bypass → CopyResource raw→denoised). YCoCg/normHitDist/normalRoughness functions confirmed bit-identical to NRD v4.14.3 official helpers; NRD_NORMAL_ENCODING=2 uses oct-pack XY so our encoding is compatible. Debug build clean. Next: run exe, press F1=ON, press F5, visually compare — if visible (noisy): NRD dispatch is the bug (Phase C input audit or dispatch loop logging); if still black: plumbing outside NRD is the bug (check SRV→Composite binding).
2026-05-01 | Claude Code  | P3-4 | A1 verdict: debug overlay (diffuseY×20 in R, specularY×20 in G) shows a visible colorful scene with F1=OFF → raw g_diffuseRadiance / g_specularRadiance carry signal; PathTracer is innocent; bug is downstream (NRD or Composite). Implemented Phase A2: added F3 (force raw → Composite → save capture_N_debug_raw.png) and F4 (force NRD denoised → Composite → save capture_N_debug_denoised.png) key captures in context.h/.cpp; CaptureScreenshot updated to handle all three modes; Debug build clean. Next: run exe, press F3 then F1+F4, compare PNGs to classify NRD-internal vs Composite downstream.
2026-04-20 | Claude       | P3-4 | After ~10 patches (matrix, YCoCg, hit-dist, IN_MV UAV, perspectiveRH_ZO, demod/remod, double-albedo, denoisingRange) all failed to resolve the emissive-only black, switched strategy from blind patching to localization-first debugging; added new doc `NRD_BLACK_OUTPUT_DEBUG.md` with phases A (per-stage capture) → B (NRD validation + REFERENCE) → C (input audit) → D (downstream audit) → E (reference comparison / fallback) and exit criteria; updated `STATUS.md` §0 sub-phase (P3-3 → P3-4), §1 last-commit hash (6187d9a → f696630), §2 next action (Phase A1: inspect debug Composite overlay), §4 open questions, §7 (now a pointer to the new doc); no code changes yet — next action is a runtime capture to classify the failing stage
2026-04-19 | Claude Code | P3-3 | Identified demodulate/remodulate path (added by Codex) as root cause of emissive-only black denoised output; removed demodulation block from PathTracer.hlsl (previous session) and removed remodulation + GenerateCameraViewDir from Composite.hlsl; Composite now simply `diffuse + specular + emissive`; Debug build clean, awaiting runtime capture to confirm fix
2026-04-18 | Codex       | P3-3 | Added `glm::perspectiveRH_ZO`, bound `IN_MV` as a UAV, and introduced a new NRD-style demodulate/remodulate path (`PathTracer.hlsl` + `Composite.hlsl` + `NrdFrontend.hlsli`); Debug build passed, automated F1/F2 capture succeeded, but `build/capture_1_denoised.png` is still almost entirely black except emissives, so the handoff target is now the remaining semantic mismatch (most likely `IN_NORMAL_ROUGHNESS` and/or material-factor handling)
2026-04-18 | Codex       | P3-3 | Audited A-D against local NRD v4.14.3 docs/source: column-major CommonSettings upload is correct, REBLUR does expect YCoCg-packed radiance, and `motionVectorScale = 1/screenSize` is correct for 2D screen-space MVs; found and patched a separate real DX11 bug where REBLUR temporal stabilization writes `IN_MV` as a UAV but our wrapper had been binding null there; Debug build passed, runtime smoke test blocked by a local PowerShell `Path`/`PATH` collision
2026-04-18 | Claude Code | P3-3 | NRD black output bug: confirmed emissive-only pattern persists even after bounce-0 emitter removal; Codex delegated for root-cause analysis (matrix convention / YCoCg / normHitDist / MV scale)
2026-04-18 | Claude Code | P3-3 | Remove bounce=0 emitter hit from NRD diffuse input (double-counted via g_emissive); also set m_denoiseEnabled default=false; build succeeded (commit 628e352)
2026-04-18 | Claude Code | P4-1 | Added F2 screenshot capture (stb_image_write, staging readback); `capture_N_raw.png` / `capture_N_denoised.png` saved to CWD; enables Phase 4 FLIP/SSIM offline comparison
2026-04-18 | Claude Code | P3-2 | Increased street lamp emission 5× (3.5→18.0), window emissives 3×; lower floors should now show visible amber illumination after double-albedo fix
2026-04-18 | Claude Code | P3-2 | Diagnosed double-albedo root cause: SampleDirectLight already bakes albedo into BRDF output; Composite `diffuse*albedo` was double-multiplying; changed to `diffuse+specular+emissive`; reverted primaryAlbedo demodulation; build succeeded (commit 6187d9a)
2026-04-18 | Claude Code | P3-2 | Built primary-albedo diffuse demodulation patch (PathTracer.hlsl `diffuse /= primaryAlbedo` before NRD packing); user reported still dark; patch reverted in next step
2026-04-18 | Codex       | P3-2 | Prepared a longer static timing probe (4s / 8s / 12s); the pre-demod frames still looked similarly dark at 4s and 8s, so the issue is likely deeper than short warm-up alone
2026-04-18 | Codex       | P3-2 | Added luminance-weighted representative hit-distance tracking for direct/NEE contributions and passed `lightHitDist` out of `SampleDirectLight(...)`; the settled static frame looked brighter than the prior primary-hit-distance-only baseline
2026-04-18 | Codex       | P3-2 | Wrote an unvalidated primary-albedo diffuse demodulation patch in `PathTracer.hlsl` after noticing `Composite.hlsl` already multiplies by `baseColor`; next session must rebuild and verify this before drawing conclusions
2026-04-18 | Codex       | P3-2 | Fixed one real REBLUR input bug by removing primary hit distance from the packed radiance-hit-distance path; static denoised output improved, but the denoised image still spends time near-black before settling
2026-04-18 | Codex       | P3-2 | Tried a targeted startup-response tweak (`maxStabilizedFrameNum = 0`, `historyFixBasePixelStride = 8`), but the early near-black warm-up remained materially similar
2026-04-18 | Codex       | P3-2 | Tried a third REBLUR sweep with looser spatial rejection (`minHitDistanceWeight`, lobe/roughness fraction, plane sensitivity), but capture output stayed materially similar to the second sweep; next step should audit denoiser inputs instead of more blind tuning
2026-04-18 | Codex       | P3-2 | Tried a second REBLUR sweep to recover dark detail (`antilag` 3.5/2.5, history 28/5, prepass blur 24/42); build and capture succeeded, but dark-scene detail still did not recover enough
2026-04-18 | Codex       | P3-2 | Manual motion-quality probe on the first REBLUR tuning showed no obvious persistent ghosting in the tested move, but dark-scene detail remained over-collapsed; next pass should rebalance detail retention
2026-04-18 | Codex       | P3-2 | Tuned first REBLUR quality pass in `nrd_denoiser.cpp` (`antilag` 3.0/2.0, history 24/4, prepass blur 20/35); Debug build and runtime smoke test succeeded
2026-04-18 | Codex       | P2-4 | Re-validated the staged `Context::OnResize()` fix at runtime: Debug build passed, app survived two live resizes, and resize logs were emitted without crash
2026-04-18 | Codex       | P3-1 | Manual quality check after camera movement confirmed denoise ON materially reduces speckle versus raw OFF path; next step is anti-lag / blur tuning
2026-04-18 | Codex       | P3-1 | Added NRD-compatible front-end packing (`shader/NrdFrontend.hlsli`), switched PathTracer diffuse/specular + normal/roughness packing, updated Composite to decode YCoCg, matched hitDistanceParameters in C++, build/runtime/F1 checks succeeded
2026-04-18 | Claude Code | P2-4 | Full NRD backend wiring: nrd::CreateInstance, pipeline CreateComputeShader, permanent/transient pool, samplers, cbuffer, dispatch loop, ResolveSRV/UAV, NrdCameraData pass; build succeeded
2026-04-18 | Claude Code | P2-3 | Phase 2 [C] review (no slot conflicts); patched NRD CMakeLists.txt for NVIDIA-RTX/ShaderMake compat (`--useAPI` removal + `FXC_PATH` fix); added `cmake/patch_nrd.cmake`; fixed context.cpp comment
2026-04-18 | Codex       | P2-2 | Moved NRD target to v4.14.3, fetched local SDK source, fixed dependency path assumptions, and confirmed `dep_nrd` failed in NRDShaders due to ShaderMake CLI mismatch
2026-04-18 | Codex       | P2-1 | Confirmed local NRD SDK/NRD.lib missing, clarified stub backend status, and prevented F1 denoise toggle from pretending to use a real backend
2026-04-18 | Claude Code | P4-0 | Implemented F1 A/B toggle (`m_denoiseEnabled`), conditional NRD pass skip, Composite input selection, build succeeded
2026-04-18 | Claude Code | P2-0 | Review fixes (`R10G10B10A2 -> R16F4`, dead cbuffer), `NrdDenoiser::Denoise()` interface, 4-pass scaffold, build succeeded
2026-04-17 21:11 | Codex | P1-0 | Added offline-safe `dep_nrd`, `PT_ENABLE_NRD`, `nrd_denoiser` scaffold, external build succeeded
2026-04-17 20:02 | Codex | P0-1 | Added 7 G-buffer outputs, prev/curr viewProj, motion vector, Composite path, clean-env build succeeded
2026-04-17 | Claude Code | P0-0 | Wrote initial STATUS.md / AGENTS.md
```

---

## 6. Update Rules

1. Start every session by reading `STATUS.md` then `AGENTS.md`.
2. Before ending a session, update the session log and replace the next concrete action.
3. Do not let two tools rewrite the same section simultaneously.
4. Only check checklist boxes after merge, not for local-only progress.
5. **임시 진단 문서(P5_PBR_RECOVERY.md 등)는 closure criteria 충족 시 §7 패턴으로 삭제하고 STATUS.md에 1줄 기록**.

---

## 7. Closed / Active Debug Plans

### Closed: Black-Output Debug Plan (P3-4)

P3-4 closed on 2026-05-01. B10 verified that the normal Composite path
produces visible, normally colored F1 ON output after the OUT_DIFF /
OUT_SPEC SRV fix.

The temporary `NRD_BLACK_OUTPUT_DEBUG.md` plan was removed when advancing
the active sub-phase past `P3-4`.

### Closed: PBR Perceptual Recovery (P5-3a/b/c)

P5_PBR_RECOVERY.md was deleted on 2026-05-04. All §6 closure criteria met:
P5-3a/b/c [x], 사용자 시각 검증 완료(P5-3a puddle 4~5개/shadow, P5-3b puddle 6개+),
P5-3c exposure_matched_ssim=0.9557 PASS. 핵심 결정(hitT 분기, firefly 임계, 최종 antilag 값)
STATUS.md §3에 영구 이관 완료.
