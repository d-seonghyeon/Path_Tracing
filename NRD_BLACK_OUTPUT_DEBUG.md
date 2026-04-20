# NRD Black-Output Debug Plan (P3-4)

> **Scope.** This document covers exactly one bug: with `F1=ON`, the
> denoised capture (`build/capture_*_denoised.png`) shows only emissive
> sources on a black background, while the raw capture
> (`build/capture_*_raw.png`) is correct (noisy but visible).
>
> **Lifetime.** Temporary. Delete this file when STATUS.md `Phase 3`
> sub-phase advances past `P3-4` (i.e. when the exit criteria below
> are met).
>
> **Owner of next action.** See `STATUS.md §2`. Never duplicate the
> "next concrete action" here — it lives in STATUS.md.

---

## Why this plan exists

Between 2026-04-18 and 2026-04-20 we landed ~10 patches against the
"denoised image is emissive-only black" bug:

- column-major `CommonSettings` upload audit
- YCoCg radiance pack
- secondary-only hit distance
- luminance-weighted NEE hit distance
- `IN_MV` UAV binding
- `glm::perspectiveRH_ZO` for the NRD-facing projection
- demodulate/remodulate path (added, then removed)
- double-albedo `Composite` formula fix
- `denoisingRange = 500000.0f` for large worlds
- debug `Composite` overlay (raw Y × 20)

None of them resolved the symptom. The pattern — guess → patch →
capture → still black — is failing, because **we have never isolated
which texture in the chain first goes black**. This plan replaces
guessing with localization: each phase is gated by a capture, and
we only enter the next phase after the current capture classifies
the failure.

---

## Ground rules

1. **No speculative fixes.** Every patch in P3-4 must be justified by
   a capture or a log line that names a specific failing input/stage.
2. **One change per capture.** If a phase needs two changes (e.g.
   enable validation + add a UAV capture), commit/test them
   separately so the responsible change is unambiguous.
3. **Record every verdict in `STATUS.md §5` Session Log.** Future
   sessions must be able to see which phases passed and which failed
   without re-running the captures.
4. **A phase that "passes" is just as informative as one that fails.**
   Passing = the stage is innocent → the search space narrows.

---

## Phase A — Localize the failing stage (mandatory entry point)

Goal: classify the bug as **upstream (PathTracer)** vs
**NRD-internal** vs **downstream (Composite/ToneMap)**.

- `[A1]` Use the temporary debug-Composite (Y × 20 of raw
  `g_diffuseRadiance` / `g_specularRadiance`, added in `f696630`) to
  decide whether the *raw* diff/spec textures carry signal before
  NRD. Output: `build/debug_raw_overlay.png`.
- `[A2]` Add per-stage F-key captures so we can compare the same
  frame at three points: raw G-buffer (post-PathTracer, pre-NRD),
  denoised G-buffer (post-NRD, pre-Composite), and final composite.
  Suggested mapping: `F3 = raw diff/spec`, `F4 = denoised diff/spec`.
  Reuse the existing staging-readback path from `F2`.
- `[A3]` Verdict (write into `STATUS.md §5` Session Log):
  - Raw OK, denoised black → **NRD-internal** → go to Phase B.
  - Raw OK, denoised OK, composite black → **Downstream** → go to
    Phase D.
  - Raw already black → **Upstream** → go to Phase C and skip B/D.

---

## Phase B — NRD-internal isolation

Entry condition: A3 = NRD-internal.

- `[B1]` Set `CommonSettings.enableValidation = true` and route the
  validation output texture through the debug composite (or save it
  to `build/nrd_validation.png`). NRD draws red overlays for
  malformed inputs (bad viewZ sign, NaN, wrong octa pack, etc.).
- `[B2]` One-shot log: dump the full `CommonSettings` +
  `ReblurSettings` on the first frame after `F1=ON` so we can verify
  the values that actually reached NRD this run (matrices,
  motionVectorScale, hitDistanceParameters, denoisingRange,
  accumulationMode).
- `[B3]` Force `accumulationMode = CLEAR_AND_RESTART` for the very
  first denoise frame after `F1=ON` (currently we never call this on
  toggle); confirm whether the first post-toggle frame is already
  black or only black after temporal accumulation.
- `[B4]` Swap `REBLUR_DIFFUSE_SPECULAR` for `REFERENCE` (NRD's
  passthrough denoiser) as a sanity check.
  - If REFERENCE also produces black → the bug is in our pipeline
    plumbing (instance creation, Resolve, SRV/UAV binding, dispatch
    order), **not** in REBLUR's logic.
  - If REFERENCE produces a noisy-but-visible image → REBLUR's
    input-format expectations are wrong → go to Phase C limited to
    REBLUR-specific inputs (radiance/hitdist YCoCg + normal/roughness).

---

## Phase C — Upstream (PathTracer output) audit

Entry condition: A3 = Upstream, OR B4 says REBLUR-specific input is
malformed.

- `[C1]` Diff our `NrdFrontend.hlsli` packing against
  `build/dep_nrd-prefix/src/dep_nrd/Shaders/Include/NRD.hlsli`
  byte-for-byte, specifically:
  - `NRD_FrontEnd_PackNormalAndRoughness`
  - `REBLUR_FrontEnd_PackRadianceAndNormHitDist`
  - YCoCg encode/decode helpers

  Anything we re-implemented locally must produce bit-identical
  output to NRD's own helpers. If not bit-identical, replace local
  helpers with `#include "NRD.hlsli"` directly.
- `[C2]` Visualize `g_viewZ` as grayscale through a one-shot debug
  pass; expect bright = far, dark = near, all positive. NaN/Inf or
  negative values are the bug.
- `[C3]` Visualize `g_motionVector` as RG; expect near-zero
  magnitude on a static camera and a small linear gradient when
  panning.
- `[C4]` Visualize `g_baseColorMetalness` and `g_emissive`
  separately. If `g_baseColorMetalness` is all-zero where the scene
  is dark, the upstream bug is in PathTracer's primary-hit shading,
  not NRD.
- `[C5]` After a fix, re-run Phase A1 — the raw overlay must now
  show signal before claiming the upstream bug is resolved.

---

## Phase D — Downstream (Composite / ToneMap) audit

Entry condition: A3 = Downstream.

- `[D1]` Bypass NRD entirely while keeping `m_denoiseEnabled = true`:
  bind raw `g_diffuseRadiance`/`g_specularRadiance` (instead of the
  denoised pair) into the Composite SRV slots. Result must be a
  noisy-but-visible image. If it is still black, the bug is in
  Composite/ToneMap, not NRD.
- `[D2]` Confirm the `g_denoisedDiffuse`/`g_denoisedSpecular` SRVs
  Composite reads are bound to the textures NRD's last dispatch
  actually wrote (not stale UAV-only handles, not the wrong slot).
- `[D3]` Round-trip unit test for the YCoCg→Linear path: encode a
  known RGB triplet, run it through `NrdYCoCgToLinear`, compare
  against the input within 1e-3. The `max(.., 0)` clamp added in
  `f696630` should not change the result for valid inputs.
- `[D4]` Inspect `Tonemap.hlsl` for any leftover `/(frameCount+1)`
  divisor or assumption that the input is an accumulated buffer.

---

## Phase E — Reference comparison and fallback

Entry condition: Phases A–D all pass without resolving the symptom.

- `[E1]` Build NVIDIA's official NRD DX11 sample against the same
  `v4.14.3` source under `build/dep_nrd-prefix/src/dep_nrd` and
  confirm `REBLUR_DIFFUSE_SPECULAR` produces a non-black image
  out-of-the-box. This isolates "our usage" vs "the DXBC we built".
- `[E2]` Diff our `NrdDenoiser::Denoise()` dispatch loop and
  resource-binding code against the sample's equivalent path.
- `[E3]` Decision branch:
  - If E1 fails → escalate to NVIDIA NRD GitHub with a minimal repro
    of the bad DXBC build.
  - If E1 passes but our path still fails → file an internal task to
    rewrite `NrdDenoiser` against the sample's structure.
- `[E4]` Last resort to unblock Phase 4: ship `REFERENCE` as the
  default denoiser so the F1 A/B path, FLIP/SSIM tooling, and
  capture/profiling pipelines can still be validated end-to-end
  while the REBLUR semantic mismatch is investigated separately.

---

## Exit criteria for P3-4

P3-4 is done when **all** of the following hold:

1. Running `Debug\PT_Object_Loading.exe`, pressing `F1` to enable
   denoise, then `F2` produces a `capture_*_denoised.png` that shows
   the scene geometry (not emissive-only).
2. The session log records which phase identified the root cause and
   why each prior patch did not.
3. Any temporary debug paths added during P3-4 (debug Composite
   Y × 20 overlay, F3/F4 captures, validation overlay routing) are
   either removed or guarded behind a compile-time `PT_NRD_DEBUG`
   flag.
4. This file (`NRD_BLACK_OUTPUT_DEBUG.md`) is deleted in the same
   commit that flips `STATUS.md §0` sub-phase past `P3-4`.
