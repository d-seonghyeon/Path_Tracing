# STATUS - NRD Integration

> This file is the single source of truth shared across Codex / Claude Code sessions.
> Read this before starting work, and update it before ending a session.

---

## 0. Current Phase

- Active phase: `Phase 3 - Quality tuning`
- Detailed sub-phase: `P3-3 - NRD black-output audit + follow-up patches landed; semantic mismatch still unresolved`
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
| Hash | `6187d9a` |
| Author | Claude Code |
| Date | 2026-04-18 |
| Scope | `P3` Fix double-albedo collapse: Composite formula + demodulation revert |
| Summary | Diagnosed that `result.diffuse` already contains BRDF albedo (`kD*albedo/PI` from `SampleDirectLight`); removed incorrect `diffuse/primaryAlbedo` demodulation from `PathTracer.hlsl`; changed `Composite.hlsl` formula from `diffuse*albedo+specular+emissive` to `diffuse+specular+emissive` |

---

## 2. Next Concrete Action

Do exactly one next action, not a vague "continue".

```
[1] Runtime test: build is clean. Run Debug\PT_Object_Loading.exe,
    press F1 to enable denoising, allow ~2s for accumulation, press F2
    to capture. Verify build/capture_1_denoised.png is NOT emissive-only
    black — expect a denoised scene with visible geometry.

    If it still looks black, the next audit target is IN_VIEWZ:
    verify that PathTracer.hlsl writes correct positive view-space Z
    to g_viewZ (dot product of worldPos with cameraFront, sign positive
    for forward). Check context.cpp that g_viewZ is bound as t3 for
    NRD and as the correct NRD resource type (IN_VIEWZ).
```

Owner: user (runtime capture) → Claude (follow-up if still black)

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
- Codex follow-up on 2026-04-18 added three more patches on top of the prior audit: `glm::perspectiveRH_ZO` for the NRD-facing projection, `IN_MV` UAV binding for REBLUR temporal stabilization, and a new demodulate/remodulate path (`PathTracer.hlsl` divides by NRD-style material factors, `Composite.hlsl` multiplies them back using `g_normalRoughness`). Debug build passed, automated `F1/F2` capture succeeded, but the newest pair (`build/capture_0_raw.png`, `build/capture_1_denoised.png`) still shows the denoised image collapsing to emissive-only black. Treat this as the current unresolved state.

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

1. Does the embedded NRD DXBC expect a stricter `IN_NORMAL_ROUGHNESS` resource format / semantic contract than our current `R16G16B16A16_FLOAT` + local oct-pack path?
2. Is the new demodulate/remodulate path actually aligned with NRD's intended "radiance without material information" contract for this tracer, or is it over-correcting an already-demodulated signal?
3. When the root cause is fixed, should `normalRoughness` stay as `R16G16B16A16_FLOAT`, or be tightened to a more storage-efficient format later?

---

## 5. Session Log

Newest entry goes on top.

```
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
