# P5-3c: auto-capture 64 raw (F1=OFF) + 1 denoised (F1=ON) frames
# Run from repo root: powershell -ExecutionPolicy Bypass -File tools\p5_3c_capture.ps1
param(
    [int]$RawFrames   = 64,
    [int]$WarmupSec   = 4,
    [int]$DenoiseSec  = 4,
    [int]$FrameDelayMs = 150
)

$buildDir = Join-Path $PSScriptRoot "..\build"
$exe      = Join-Path $buildDir "Debug\PT_Object_Loading.exe"

if (-not (Test-Path $exe)) {
    Write-Error "Exe not found: $exe"
    exit 1
}

# Back up any existing capture_*.png so fresh run starts at index 0
$backupDir = Join-Path $buildDir "p5_3c_backup_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
$existing = Get-ChildItem "$buildDir\capture_*.png" -ErrorAction SilentlyContinue
if ($existing) {
    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    $existing | Move-Item -Destination $backupDir -Force
    Write-Host "Backed up $($existing.Count) existing capture_*.png → $backupDir"
}
# Also backup histogram files
Get-ChildItem "$buildDir\histogram_*.txt" -ErrorAction SilentlyContinue |
    Move-Item -Destination $backupDir -ErrorAction SilentlyContinue

Write-Host "Launching $exe ..."
$proc = Start-Process -FilePath $exe -WorkingDirectory $buildDir -PassThru
Write-Host "PID=$($proc.Id) — warming up ${WarmupSec}s ..."
Start-Sleep -Seconds $WarmupSec

$wshell = New-Object -ComObject WScript.Shell
$activated = $wshell.AppActivate("PT_Object_Loading")
if (-not $activated) { $activated = $wshell.AppActivate($proc.Id) }
Start-Sleep -Milliseconds 400

Write-Host "Capturing $RawFrames raw frames (F1=OFF) ..."
for ($i = 0; $i -lt $RawFrames; $i++) {
    $wshell.SendKeys("{F2}")
    Start-Sleep -Milliseconds $FrameDelayMs
    if (($i+1) % 16 -eq 0) { Write-Host "  $($i+1)/$RawFrames" }
}

Write-Host "Switching to denoised (F1=ON) — settling ${DenoiseSec}s ..."
$wshell.SendKeys("{F1}")
Start-Sleep -Seconds $DenoiseSec

Write-Host "Capturing denoised frame ..."
$wshell.SendKeys("{F2}")
Start-Sleep -Milliseconds 600

$proc.Kill()
Write-Host "Done. Captures written to $buildDir"
Write-Host "Next: python tools\p5_3c_metrics.py"
