# STATUS - NRD Integration

> This file is the single source of truth shared across Codex / Claude Code sessions.
> Read this before starting work, and update it before ending a session.

---

## 0. Current Phase

- Active phase: `Phase 2 - NrdDenoiser wrapper + DXBC pipeline`
- Detailed sub-phase: `P2-4 - NRD backend fully wired; REBLUR_DIFFUSE_SPECULAR active`
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
- [ ] `[C]` Add `Composite.hlsl` for `diffuse * albedo + specular + emissive`
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
| Hash | `7ce3894` |
| Author | Claude Code |
| Date | 2026-04-18 |
| Scope | `P4-0` A/B toggle (`F1 = denoise on/off`) |
| Summary | `m_denoiseEnabled` + F1 toggle, conditional NRD pass skip, Composite chooses raw G-buffer or denoised textures |

---

## 2. Next Concrete Action

Do exactly one next action, not a vague "continue".

```
[1] Run the app and verify NrdDenoiser logs "REBLUR_DIFFUSE_SPECULAR ready" on startup.
[2] Move camera (WASD) and toggle F1 — check that denoise path is visually different from raw.
    Note: output may look noisy/incorrect until Phase 3 HitT normalization is done.
[3] Phase 3: implement REBLUR_FrontEnd_GetNormHitDist in PathTracer.hlsl for proper hitT packing.
```

Owner: Claude (app run + Phase 3 quality)  
Claude completed: Phase 2 full wiring (NRD instance + pipelines + pools + dispatch loop)

---

## 3. Cross-Session Notes

### Newly introduced

- `shader/Composite.hlsl` - HDR composite pass for `diffuse * albedo + specular + emissive`
- `GlobalUniforms.prevViewProj` / `currViewProj` - motion vector matrices
- `Context` screen resources - 7 G-buffer textures + `m_compositeTexture` + `m_denoisedDiffuse/Specular`
- `PT_ENABLE_NRD` - enabled only when local NRD source/install is available
- `src/nrd_denoiser.{h,cpp}` - DX11 NRD wrapper without NRI
- `NrdGBufferInputs` / `NrdDenoisedOutputs` - grouped denoise I/O structs
- Render path - `PT -> NRD(stub) -> Composite -> ToneMap`
- `m_denoiseEnabled` + `F1` toggle - A/B path between raw and denoise-enabled rendering
- `NrdDenoiser::GetBackendStatusLabel()` / `HasUsableBackend()` - explicit stub vs real backend status
- `cmake/patch_nrd.cmake` - patches NRD v4.14.3 CMakeLists.txt for NVIDIA-RTX/ShaderMake compatibility

### Important current behavior

- Local NRD v4.14.3 source is now present under `build/dep_nrd-prefix/src/dep_nrd`.
- `Dependency.cmake` uses the NRD source-local SDK layout (`Include`, `Integration`, `Shaders/Include`, `_Bin/Debug`).
- **ShaderMake CLI mismatch fixed** (Claude P2-3): `NVIDIA-RTX/ShaderMake` (main) dropped `--useAPI` and uses `SHADERMAKE_FXC_PATH` not `FXC_PATH`. Both issues patched in `build/dep_nrd-prefix/src/dep_nrd/CMakeLists.txt` and via `cmake/patch_nrd.cmake` (PATCH_COMMAND in Dependency.cmake for future clones). Configure stamp deleted — next build will re-configure dep_nrd.
- `F1` only activates the denoise path when a usable backend actually exists; otherwise rendering stays on the raw G-buffer path and logs the stub status.

### Phase 2 [C] Review — Binding / Slot / Resource Lifetime (Claude P2-3)

No critical conflicts found. Details:
- SRV/UAV hazards: none — each pass null-clears before the next binds the same texture.
- CopyResource ref counting in stub Denoise(): `GetResource()` increments refcount; matching `Release()` calls present. ✓
- ToneMap binds u0=null, u1=outputUAV (slots 0..1 set in one call); null-cleared after. ✓
- b0 (GlobalUB) is never unbound after PathTracer — it remains bound in Composite/ToneMap. No behavioral impact (those passes don't use b0), but worth cleaning up before shipping.
- `OnResize` early-return on mid-sequence failure leaves partial resource state; `Render()` called with a partially initialized Context would crash. Low-priority risk — acceptable for now.
- Comment bug fixed: "패스 3: ToneMap" → "패스 4: ToneMap" in context.cpp.

### Fixed decisions

- Do not introduce NRI.
- First denoiser remains `REBLUR_DIFFUSE_SPECULAR`.
- Motion vectors stay in pixel units: `prev - curr`.
- HLSL keeps `row_major`; C++ uploads transposed `prev/currViewProj`.
- If local NRD source/install is missing, configure must gracefully fall back to `PT_ENABLE_NRD=OFF`.

---

## 4. Open Questions

1. Keep denoise input at `R16G16B16A16_FLOAT`, or promote specific signals later if quality requires it?
2. When the real backend is wired, should resize-triggered reset also be logged visibly for debugging?
3. If local NRD SDK is restored, should we immediately complete Phase 2 first, or keep the current A/B work in parallel?

---

4. ~~Should we patch upstream NRD's standalone CMake locally for this repo?~~ **Resolved**: `cmake/patch_nrd.cmake` + PATCH_COMMAND in dep_nrd. Future-safe for clean builds.

---

## 5. Session Log

Newest entry goes on top.

```
2026-04-18 | Claude Code | P2-4 | Full NRD backend wiring: nrd::CreateInstance, pipeline CreateComputeShader, permanent/transient pool, samplers, cbuffer, dispatch loop, ResolveSRV/UAV, NrdCameraData pass; build succeeded
2026-04-18 | Claude Code | P2-3 | Phase 2 [C] review (no slot conflicts); patched NRD CMakeLists.txt for NVIDIA-RTX/ShaderMake compat (--useAPI removal + FXC_PATH fix); added cmake/patch_nrd.cmake; fixed context.cpp comment (패스 3→4)
2026-04-18 | Codex       | P2-2 | Moved NRD target to v4.14.3, fetched local SDK source, fixed dependency path assumptions, and confirmed dep_nrd currently fails in NRDShaders due to ShaderMake CLI mismatch
2026-04-18 | Codex       | P2-1 | Confirmed local NRD SDK/NRD.lib missing, clarified stub backend status, and prevented F1 denoise toggle from pretending to use a real backend
2026-04-18 | Claude Code | P4-0 | Implemented F1 A/B toggle (`m_denoiseEnabled`), conditional NRD pass skip, Composite input selection, build succeeded
2026-04-18 | Claude Code | P2-0 | Review fixes (R10G10B10A2 -> R16F4, dead cbuffer), `NrdDenoiser::Denoise()` interface, 4-pass scaffold, build succeeded
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
