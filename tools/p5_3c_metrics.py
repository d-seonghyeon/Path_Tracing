"""P5-3c: average 64 clamped raw frames, compute metrics vs denoised.

Usage:
    python tools/p5_3c_metrics.py [build_dir]

Expects build_dir to contain:
    capture_0_raw.png ... capture_63_raw.png  (64 F1=OFF frames)
    capture_64_denoised.png                   (1 F1=ON frame)
"""
import sys, os, json, glob, shutil
import numpy as np
from PIL import Image

BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "build")
BUILD = os.path.normpath(BUILD)

# --- load raw frames ---
raw_files = sorted(glob.glob(os.path.join(BUILD, "capture_*_raw.png")),
                   key=lambda p: int(os.path.basename(p).split("_")[1]))
if not raw_files:
    sys.exit("ERROR: no capture_*_raw.png found in " + BUILD)
if len(raw_files) < 64:
    print(f"WARNING: only {len(raw_files)} raw frames (expected 64)")

frames = [np.array(Image.open(f), dtype=np.float64) for f in raw_files[:64]]
print(f"Loaded {len(frames)} raw frames")

# --- average ---
avg = np.mean(frames, axis=0)
avg_path = os.path.join(BUILD, "p5_3c_raw_clamped_avg64.png")
Image.fromarray(np.clip(np.round(avg), 0, 255).astype(np.uint8)).save(avg_path)
print(f"Saved avg: {avg_path}")

# copy with p5_3c_raw_NN.png names
for i, f in enumerate(raw_files[:64]):
    shutil.copy2(f, os.path.join(BUILD, f"p5_3c_raw_{i:02d}.png"))

# --- load denoised ---
den_files = sorted(glob.glob(os.path.join(BUILD, "capture_*_denoised.png")),
                   key=lambda p: int(os.path.basename(p).split("_")[1]))
if not den_files:
    sys.exit("ERROR: no capture_*_denoised.png found")
den_path = den_files[-1]
den = np.array(Image.open(den_path), dtype=np.float64)
shutil.copy2(den_path, os.path.join(BUILD, "p5_3c_denoised.png"))
print(f"Denoised: {den_path}")

ref = avg

# --- helpers ---
def mean_luma(img):
    rgb = img[..., :3] / 255.0
    return float(np.mean(0.2126*rgb[...,0] + 0.7152*rgb[...,1] + 0.0722*rgb[...,2]))

def global_ssim(a, b):
    a, b = a.ravel()/255.0, b.ravel()/255.0
    mu_a, mu_b = a.mean(), b.mean()
    cov = float(np.mean((a-mu_a)*(b-mu_b)))
    var_a = float(np.mean((a-mu_a)**2))
    var_b = float(np.mean((b-mu_b)**2))
    c1, c2 = 0.01**2, 0.03**2
    return float((2*mu_a*mu_b+c1)*(2*cov+c2) / ((mu_a**2+mu_b**2+c1)*(var_a+var_b+c2)))

# --- metrics ---
ref_luma = mean_luma(ref)
den_luma = mean_luma(den)
scale = ref_luma / den_luma if den_luma > 0 else 1.0
den_em = np.clip(den * scale, 0, 255)

diff    = den - ref
rmse    = float(np.sqrt(np.mean(diff**2)))
mae     = float(np.mean(np.abs(diff)))
psnr    = float(20*np.log10(255.0/rmse)) if rmse > 0 else 999.0
ssim    = global_ssim(ref, den)

diff_em = den_em - ref
rmse_em = float(np.sqrt(np.mean(diff_em**2)))
psnr_em = float(20*np.log10(255.0/rmse_em)) if rmse_em > 0 else 999.0
ssim_em = global_ssim(ref, den_em)

# --- result ---
metrics = {
    "note": "P5-3c: FIREFLY_CLAMP=20 raw clamped avg64 vs denoised",
    "ref_frames": len(frames),
    "ref_mean_luma": ref_luma,
    "denoised_mean_luma": den_luma,
    "exposure_scale": scale,
    "rmse": rmse,
    "mae": mae,
    "psnr_db": psnr,
    "global_ssim_style": ssim,
    "exposure_matched_rmse": rmse_em,
    "exposure_matched_psnr_db": psnr_em,
    "exposure_matched_ssim_style": ssim_em,
    "p4_3_baseline": {
        "global_ssim_style": 0.92003,
        "exposure_matched_ssim": 0.92879,
        "note": "reference was firefly-unclamped raw avg64"
    },
    "exit_criteria": "exposure_matched_ssim_style >= 0.93",
    "pass": bool(ssim_em >= 0.93),
}

out = os.path.join(BUILD, "p5_3c_metrics.json")
with open(out, "w") as f:
    json.dump(metrics, f, indent=2)

print(json.dumps(metrics, indent=2))
print(f"\nMetrics → {out}")
print(f"Exit criteria (ssim_em >= 0.93): {'PASS' if metrics['pass'] else 'FAIL'}")
