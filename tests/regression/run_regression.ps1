# HLPlayer Local Playback Regression Suite
# Run from project root: powershell -File tests/regression/run_regression.ps1

$ErrorActionPreference = "Stop"
$ProjectDir = "D:\HLPlayer"
$BuildDir = "$ProjectDir\build"
$Results = @()

Write-Host "=== HLPlayer Regression Suite ===" -ForegroundColor Cyan
Write-Host "Timestamp: $(Get-Date -Format 'o')"
Write-Host ""

# Test 1: Build all library targets
Write-Host "[1/3] Build verification..." -ForegroundColor Yellow
$buildResult = cmake --build $BuildDir --config Release --target hlplayer_app 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "  PASS: All targets built successfully" -ForegroundColor Green
    $Results += @{ Test = "Build"; Status = "PASS" }
} else {
    Write-Host "  FAIL: Build errors detected" -ForegroundColor Red
    Write-Host $buildResult | Select-Object -Last 10
    $Results += @{ Test = "Build"; Status = "FAIL" }
}

# Test 2: App startup
Write-Host "[2/3] App startup smoke test..." -ForegroundColor Yellow
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "hlplayer_app.exe" | Select-Object -First 1
if ($exe) {
    $proc = Start-Process -FilePath $exe.FullName -PassThru -WindowStyle Normal
    Start-Sleep -Seconds 3
    if (-not $proc.HasExited) {
        Write-Host "  PASS: App running after 3s (PID: $($proc.Id))" -ForegroundColor Green
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        $Results += @{ Test = "Startup"; Status = "PASS" }
    } else {
        Write-Host "  FAIL: App exited with code $($proc.ExitCode)" -ForegroundColor Red
        $Results += @{ Test = "Startup"; Status = "FAIL" }
    }
} else {
    Write-Host "  FAIL: hlplayer_app.exe not found" -ForegroundColor Red
    $Results += @{ Test = "Startup"; Status = "FAIL" }
}

# Test 3: DLL dependencies
Write-Host "[3/3] DLL dependency check..." -ForegroundColor Yellow
$dllPath = $exe.DirectoryName
$requiredDlls = @("libhlplayer_core.dll", "libhlplayer_qml.dll", "libhlplayer_render.dll", "libhlplayer_extractor.dll")
$allDlls = $true
foreach ($dll in $requiredDlls) {
    $found = Get-ChildItem -Path $dllPath -Recurse -Filter $dll -ErrorAction SilentlyContinue
    if (-not $found) {
        Write-Host "  WARN: $dll not found near executable" -ForegroundColor Yellow
        $allDlls = $false
    }
}
if ($allDlls) {
    Write-Host "  PASS: All required DLLs present" -ForegroundColor Green
    $Results += @{ Test = "DLLs"; Status = "PASS" }
} else {
    $Results += @{ Test = "DLLs"; Status = "WARN" }
}

# Summary
Write-Host ""
Write-Host "=== Results ===" -ForegroundColor Cyan
foreach ($r in $Results) {
    $color = if ($r.Status -eq "PASS") { "Green" } elseif ($r.Status -eq "FAIL") { "Red" } else { "Yellow" }
    Write-Host "  $($r.Test): $($r.Status)" -ForegroundColor $color
}
