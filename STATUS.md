# STATUS - NRD Integration

> This file is the single source of truth shared across Codex / Claude Code sessions.
> Read this before starting work, and update it before ending a session.

---

## 0. Current Phase

- Active phase: `Phase 3 - Quality tuning`
- Detailed sub-phase: `P3-5 - REBLUR quality tuning after black-output fix`
- Blocked: `No`
- Branch: `feature/nrd-phase0`

### Phase Checklist

Phase 0 - Render pipeline prerequisites

- [ ] `[C]` Remove accumulation model and `g_accum += ...` from `PathTracer.hlsl`
- [ ] `[C]` Remove `/(frameCount+1)` from `Tonemap.hlsl`
- [ ] `[C]` Split `TracePath` return into diffuse / specular radiance
- [ ] `[C]` Add 7 G-buffer UAVs in `context.h` / `context.cpp`
- [ ] `[X]` Add `prevViewProj` / `currViewProj` to `GlobalUB` and upload from C++
- [ ] `[X]` Add motion vector generation to the PathTracer output path
- [ ] `[C]` Add `Composite.hlsl` for `diffuse + specular + emissive` (albedo already in BRDF output)
- [ ] `[R]` Codex reviews Claude's Phase 0 diffs

Phase 1 - NRD dependency integration

- [ ] `[X]` Add `dep_nrd` in `Dependency.cmake` (`v4.14.3`, DXBC embed)
- [ ] `[X]` Add NRD include / link paths in `CMakeLists.txt`
- [ ] `[R]` Claude reviews the CMake diff

Phase 2 - NrdDenoiser wrapper + DXBC pipeline

- [ ] `[X]` Scaffold `src/nrd_denoiser.{h,cpp}` for permanent / transient pool + identifiers
- [ ] `[X]` Build `ID3D11ComputeShader` objects from NRD `PipelineDesc`
- [ ] `[C]` Review DX11 binding table / slot conflicts / resource lifetime

Phase 3 - Quality tuning

- [ ] `[C]` HitT normalization + NRD helper packing
- [ ] `[C]` Anti-lag / disocclusion threshold sweep
- [ ] `[X]` Optional SIGMA / ReLAX experiments

Phase 4 - Validation / A-B

- [ ] `[C]` A/B toggle (`F1 = denoise on/off`)
- [ ] `[C]` FLIP / SSIM offline comparison
- [ ] `[X]` Timestamp query profiling for tracing vs denoise

---

## 1. Last Commit

| Item | Value |
| --- | --- |
| Hash | `99d1293` |
| Author | choi mun chan |
| Date | 2026-05-02 |
| Scope | `P3` PathTracer — revert C2, restore lobe-weighted NEE split at bounce=0 |
| Summary | C2 (all bounce=0 NEE to diffuse) reverted. matPuddle (roughness=0.02, pSpec≈1.0) requires specular-channel NEE for tight-blur treatment; routing to diffuse produced large soft blobs. Restored lobe-weighted split: diffuseContrib=neeContrib*pDiff, specularContrib=neeContrib*pSpec, hitT tracked per-channel. Horizontal smearing fix came from C5 (specularPrepassBlurRadius 28→12), not from NEE routing. |

---

## 2. Next Concrete Action

Do exactly one next action, not a vague "continue".

```
[P3-5 quality-fix round] hitDistanceParameters.A 3→30 + maxStabilizedFrameNum 0→30.

Scene AABB ~50x60x27m (diagonal 83m). With A=3.0 (NRD indoor default),
normalization=(3+viewZ*0.1) at viewZ=10m was ~4, saturating any secondary hit
>4m to normHitDist=1.0 ("far/uncertain"). REBLUR applied max spatial blur to
essentially the entire street scene. Fixed: A=30 raises normalization to ~31 at
viewZ=10m, giving 5-25m secondary hits a proper [0,1] range. Also updated
HLSL REBLUR_HIT_DIST_PARAMS.x=30 to stay bit-identical with C++ side.

maxStabilizedFrameNum restored to 30 (was 0 = disabled). Re-enables temporal
stabilization pass, which reduces residual flicker on static camera and lowers
spatial blur dependency.

Commits: 9352bdf (A 3→30), afebcca (stabilized 0→30)

Next: F2 capture F1 OFF/ON to judge remaining texture blur. If still excessive,
try maxBlurRadius 18→12 (Step 2-3).
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

### Important current behavior

- Local NRD v4.14.3 source is present under `build/dep_nrd-prefix/src/dep_nrd`.
- `Dependency.cmake` uses the NRD source-local SDK layout (`Include`, `Integration`, `Shaders/Include`, `_Bin/Debug`).
- ShaderMake CLI mismatch is fixed: `NVIDIA-RTX/ShaderMake` (main) dropped `--useAPI` and uses `SHADERMAKE_FXC_PATH` instead of `FXC_PATH`. Both issues are patched in `build/dep_nrd-prefix/src/dep_nrd/CMakeLists.txt` and via `cmake/patch_nrd.cmake`.
- `F1` only activates the denoise path when a usable backend actually exists; otherwise rendering stays on the raw G-buffer path and logs the stub status.
- `nrd_denoiser.cpp` now sets `ReblurSettings.hitDistanceParameters` explicitly to match shader-side packing constants (`A=3, B=0.1, C=20, D=-25`).
- Codex audited the black-output suspects on 2026-04-18: NRD v4.14.3 docs/source confirmed that column-major `CommonSettings` upload is correct, REBLUR does expect pre-packed YCoCg radiance, and `motionVectorScale = 1 / screenSize` is correct for 2D screen-space MVs. The one concrete integration bug found was elsewhere: REBLUR temporal stabilization writes `IN_MV` as a UAV, and the DX11 wrapper now binds `m_motionVectorUAV` for that path. Debug build passed after the patch, but a local PowerShell `Start-Process` `Path`/`PATH` collision blocked the short runtime smoke test.
- First quality-tuning pass in `nrd_denoiser.cpp` biases REBLUR toward faster history rejection: `antilagSettings = { sigmaScale=3.0, sensitivity=2.0 }`, `maxAccumulatedFrameNum = 24`, `maxFastAccumulatedFrameNum = 4`, `diffuse/specularPrepassBlurRadius = 20/35`.
- Runtime checks completed on 2026-04-18: Debug build passed, app stayed alive, `REBLUR_DIFFUSE_SPECULAR ready` logged, and `F1` still logged `Denoise OFF` / `Denoise ON` after the Phase 3 packing change.
- Manual quality comparison completed on 2026-04-18: after camera movement, `F1=ON` produced a much cleaner image while `F1=OFF` remained heavily speckled on dark building faces. The current denoiser path is active and materially affecting output.
- Resize validation repeated on 2026-04-18 with the staged-allocation `Context::OnResize()` path: Debug build passed, the app survived two live window resizes, and stdout logged `Window Resized: 1088x642` then `Window Resized: 828x422` without crashing or leaving the render path unusable.
- Anti-lag tuning smoke test completed on 2026-04-18: Debug build passed after the new REBLUR settings, the app stayed alive for 6 seconds, and startup again logged `REBLUR_DIFFUSE_SPECULAR ready` with no stderr output.
- Manual camera-motion probe completed on 2026-04-18 with `quality_tuned_denoise_on/off.png` plus `ghosting_probe_immediate/settled.png`: the new settings did not show an obvious long-lived ghost trail in the tested move, but dark regions still collapsed too aggressively, so the next pass should focus on restoring detail rather than pushing anti-lag harder.
- Second REBLUR sweep completed on 2026-04-18 with a partial rollback toward defaults (`antilag` 3.5/2.5, history 28/5, prepass blur 24/42). The build and runtime capture path both succeeded, but `quality_retuned_denoise_on.png` still did not recover dark-scene detail enough to call the issue solved.
- Third REBLUR sweep completed on 2026-04-18 with looser spatial rejection (`minHitDistanceWeight = 0.18`, `lobeAngleFraction = 0.20`, `roughnessFraction = 0.20`, `planeDistanceSensitivity = 0.03`) while keeping the moderated anti-lag/history values. Build and capture succeeded, but `quality_third_sweep_on.png` still looked materially similar to the second sweep, which suggests the remaining dark-scene collapse may not be fixable by parameter tuning alone.
- Input audit on 2026-04-18 found a real REBLUR semantic mismatch: `PathTracer.hlsl` had been feeding the primary camera-hit distance into `IN_DIFF_RADIANCE_HITDIST` / `IN_SPEC_RADIANCE_HITDIST`, even though NRD's own `NRD.hlsli` explicitly says primary hit distance must be ignored. A first fix now feeds secondary-path hit distance estimates instead, which improved the fully-collapsed static image somewhat, but the denoised image still spends time near-black before settling.
- A targeted startup-response tuning pass on 2026-04-18 (`maxStabilizedFrameNum = 0`, `historyFixBasePixelStride = 8`) did not materially improve the early near-black warm-up compared with the post-hit-distance-fix baseline.
- A follow-up NEE audit on 2026-04-18 found that primary and indirect direct-light contributions were still carrying little or no representative hit distance. Local shader edits now pass `lightHitDist` out of `SampleDirectLight(...)` and choose a luminance-weighted representative hit distance for the diffuse/specular channels, which produced a visibly brighter settled static frame than the previous primary-hit-distance-only fix.
- Root cause of persistent dark collapse identified on 2026-04-18: `SampleDirectLight` returns `(kD * albedo/PI + specular) * emission * NdotL / pdf`, so `result.diffuse` already contains albedo from the Lambertian BRDF. The old Composite formula `diffuse * albedo + specular + emissive` was double-multiplying albedo, making dark materials (albedo ~0.1) appear ~10x too dark. Fixed by changing Composite to `diffuse + specular + emissive` and removing the incorrect `diffuse / primaryAlbedo` demodulation from PathTracer.
- P3-4 black-output debug is closed as of 2026-05-01. The root cause was missing SRV resolution for REBLUR internal `OUT_DIFF_RADIANCE_HITDIST` / `OUT_SPEC_RADIANCE_HITDIST` read-after-write passes; F1 ON now produces a visible normal-color denoised image.
- B11 detail-retention sweep reduced REBLUR blur/history settings (`diffuse/specularPrepassBlurRadius=16/28`, `maxBlurRadius=18`, `maxAccumulatedFrameNum=24`, `maxFastAccumulatedFrameNum=4`). Static capture looks slightly less smeared than B10.
- B12 automated D-key camera-motion probe did not show an obvious long-lived ghost trail between the immediate and settled denoised captures. The sweep is a reasonable keep candidate, pending user visual review.

### F2 Screenshot Capture (Phase 4 FLIP/SSIM)

- `F2` sets `m_captureRequested = true`; captured at end of same `Render()` call.
- Output filename: `capture_<index>_<denoised|raw>.png` in the CWD of the exe.
- Uses a per-call staging texture (D3D11_USAGE_STAGING + CPU_ACCESS_READ), then stb_image_write PNG.
- `stb_image_write.h` is now copied by `Dependency.cmake` alongside `stb_image.h`.

### Phase 2 [C] Review - Binding / Slot / Resource Lifetime

No critical conflicts found. Details:
- SRV/UAV hazards: none - each pass null-clears before the next binds the same texture.
- CopyResource ref counting in stub `Denoise()`: `GetResource()` increments refcount and matching `Release()` calls are present.
- ToneMap binds `u0=null`, `u1=outputUAV` (slots `0..1` set in one call); null-cleared after.
- `b0 (GlobalUB)` is never unbound after PathTracer. No behavioral impact today, but worth cleaning up before shipping.
- The old `OnResize` partial-initialization risk was fixed locally in `src/context.cpp` by staging all screen textures before swapping them in.

### Fixed decisions

- Do not introduce NRI.
- First denoiser remains `REBLUR_DIFFUSE_SPECULAR`.
- Motion vectors stay in pixel units: `prev - curr`.
- HLSL keeps `row_major`; C++ uploads transposed `prev/currViewProj`.
- If local NRD source/install is missing, configure must gracefully fall back to `PT_ENABLE_NRD=OFF`.

---

## 4. Open Questions

1. Does the B11 lower-blur sweep introduce ghost trails or unacceptable noise during camera motion?
2. Can the remaining side-wall dark smearing be improved with REBLUR parameters alone, or does it require another G-buffer / hit-distance semantic audit?
3. Should `normalRoughness` stay as `R16G16B16A16_FLOAT`, or be tightened to a more storage-efficient format later?
4. Does the NRD validation layer (`CommonSettings.enableValidation`) emit anything useful in the DX11-direct path (no NRI), or would validation need a custom hook?

---

## 5. Session Log

Newest entry goes on top.

```
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

---

## 7. Closed Black-Output Debug Plan (P3-4)

P3-4 closed on 2026-05-01. B10 verified that the normal Composite path
produces visible, normally colored F1 ON output after the OUT_DIFF /
OUT_SPEC SRV fix.

The temporary `NRD_BLACK_OUTPUT_DEBUG.md` plan was removed when advancing
the active sub-phase past `P3-4`.
