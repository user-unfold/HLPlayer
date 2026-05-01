# HLPlayer Smoke Test
# Basic smoke test to verify app launches and stays stable
# Run from project root: powershell -File tests/regression/smoke_test.ps1

$ErrorActionPreference = "Stop"
$ProjectDir = "D:\HLPlayer"
$BuildDir = "$ProjectDir\build"
$ExitCode = 0

Write-Host "=== HLPlayer Smoke Test ===" -ForegroundColor Cyan
Write-Host "Timestamp: $(Get-Date -Format 'o')"
Write-Host ""

# Step 1: Build the app
Write-Host "[1/4] Building app..." -ForegroundColor Yellow
$buildResult = cmake --build $BuildDir --config Release --target hlplayer_app 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "  PASS: App built successfully" -ForegroundColor Green
} else {
    Write-Host "  FAIL: Build failed" -ForegroundColor Red
    Write-Host $buildResult | Select-Object -Last 20
    $ExitCode = 1
    exit $ExitCode
}

# Step 2: Find the executable
Write-Host "[2/4] Locating executable..." -ForegroundColor Yellow
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "hlplayer_app.exe" | Select-Object -First 1
if (-not $exe) {
    Write-Host "  FAIL: hlplayer_app.exe not found" -ForegroundColor Red
    $ExitCode = 1
    exit $ExitCode
}
Write-Host "  Found: $($exe.FullName)" -ForegroundColor Green

# Step 3: Launch the app
Write-Host "[3/4] Launching app..." -ForegroundColor Yellow
$proc = Start-Process -FilePath $exe.FullName -PassThru -WindowStyle Normal
Write-Host "  PID: $($proc.Id)" -ForegroundColor Green

# Step 4: Wait and check stability
Write-Host "[4/4] Checking stability (3s)..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

if (-not $proc.HasExited) {
    Write-Host "  PASS: App running after 3s" -ForegroundColor Green
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Write-Host "  Terminated gracefully" -ForegroundColor Green
} else {
    Write-Host "  FAIL: App exited with code $($proc.ExitCode)" -ForegroundColor Red
    $ExitCode = 1
}

# Summary
Write-Host ""
Write-Host "=== Result ===" -ForegroundColor Cyan
if ($ExitCode -eq 0) {
    Write-Host "SMOKE TEST PASS" -ForegroundColor Green
} else {
    Write-Host "SMOKE TEST FAIL" -ForegroundColor Red
}

exit $ExitCode
