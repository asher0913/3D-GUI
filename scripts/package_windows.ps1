# =====================================================================
# MultiMaterialSlicer - Windows packaging (one stop)
#
# Builds:
#   1. The backend tool  slice_merge_tool.exe  (PyInstaller, no Python needed at runtime)
#   2. The Qt application MultiMaterialSlicer.exe
#   3. Collects Qt DLLs (windeployqt), embeds the backend + config
#   4. Produces dist\MultiMaterialSlicer-win64.zip  (give ONLY this to customers)
#
# Usage (or just double-click build_windows.bat):
#   powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1
#   ...optional overrides:
#   -QtPrefix C:\Qt\5.12.12\msvc2017_64  -Python py  -SkipBackend
# =====================================================================
param(
    [string]$QtPrefix = "",
    [string]$Generator = "",
    [string]$Python = "",
    [switch]$SkipBackend
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$BuildDir = Join-Path $Root "build-win-release"
$DistRoot = Join-Path $Root "dist"
$Stage = Join-Path $DistRoot "MultiMaterialSlicer-win64"
$Zip = Join-Path $DistRoot "MultiMaterialSlicer-win64.zip"

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Info($msg) { Write-Host $msg -ForegroundColor Cyan }

# ---- Locate Qt -------------------------------------------------------
function Resolve-Qt {
    param([string]$hint)
    if ($hint -and (Test-Path (Join-Path $hint "bin\windeployqt.exe"))) { return $hint }
    $cands = @(
        "C:\Qt\5.12.12\msvc2017_64",
        "C:\Qt\5.12.12\msvc2019_64",
        "C:\Qt\5.12.12\msvc2017",
        "C:\Qt5.12.12\5.12.12\msvc2017_64"
    )
    foreach ($c in $cands) { if (Test-Path (Join-Path $c "bin\windeployqt.exe")) { return $c } }
    if (Test-Path "C:\Qt\5.12.12") {
        $f = Get-ChildItem "C:\Qt\5.12.12" -Directory -ErrorAction SilentlyContinue |
             Where-Object { Test-Path (Join-Path $_.FullName "bin\windeployqt.exe") } |
             Select-Object -First 1
        if ($f) { return $f.FullName }
    }
    return $null
}

$QtPrefix = Resolve-Qt $QtPrefix
if (-not $QtPrefix) {
    Fail "Could not find Qt 5.12.12 MSVC. Install it (e.g. C:\Qt\5.12.12\msvc2017_64) or pass -QtPrefix <path>."
}
Info "Qt:        $QtPrefix"

# ---- Locate CMake ----------------------------------------------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Fail "cmake not found on PATH. Install CMake and reopen the terminal."
}

# ---- Pick a Visual Studio generator ---------------------------------
if (-not $Generator) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $ver = & $vswhere -latest -property catalog_productLineVersion 2>$null
        if ($ver -eq "2022") { $Generator = "Visual Studio 17 2022" }
        elseif ($ver -eq "2019") { $Generator = "Visual Studio 16 2019" }
    }
    if (-not $Generator) { $Generator = "Visual Studio 17 2022" }  # sensible default
}
Info "Generator: $Generator"

# ---- 1. Backend tool (PyInstaller) ----------------------------------
$BackendExe = Join-Path $Root "backend_dist\slice_merge_tool.exe"
if ($SkipBackend) {
    Info "Skipping backend build (-SkipBackend)."
} else {
    Info "==> Building backend tool (slice_merge_tool.exe) ..."
    $bargs = @()
    if ($Python) { $bargs += @("-Python", $Python) }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "package_backend_windows.ps1") @bargs
    if ($LASTEXITCODE -ne 0) { Fail "Backend build failed. Is Python 3 installed and on PATH?" }
}
if (-not (Test-Path $BackendExe)) {
    Write-Host "WARNING: $BackendExe not found." -ForegroundColor Yellow
    Write-Host "         The app will be built without an embedded backend (dev mode only)." -ForegroundColor Yellow
}

# ---- 2. Configure + build the app -----------------------------------
# Configure AFTER the backend exists so CMake embeds it next to the exe.
Info "==> Configuring (CMake) ..."
cmake -S $Root -B $BuildDir -G $Generator -A x64 -DCMAKE_PREFIX_PATH=$QtPrefix
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed." }

Info "==> Building (Release) ..."
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { Fail "Build failed." }

$ExeDir = Join-Path $BuildDir "Release"
$Exe = Join-Path $ExeDir "MultiMaterialSlicer.exe"
if (-not (Test-Path $Exe)) { Fail "Built exe not found at $Exe" }

# ---- 3. Assemble the distributable folder ---------------------------
Info "==> Assembling package ..."
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage | Out-Null

# Copy the exe and everything CMake placed next to it (backend exe, config, script).
Copy-Item (Join-Path $ExeDir "*") $Stage -Recurse -Force

# Collect Qt runtime DLLs and plugins into the package folder.
& (Join-Path $QtPrefix "bin\windeployqt.exe") --release --no-translations (Join-Path $Stage "MultiMaterialSlicer.exe")
if ($LASTEXITCODE -ne 0) { Fail "windeployqt failed." }

# Make sure backend + config are present (defensive; CMake already copies them).
if (Test-Path $BackendExe) { Copy-Item $BackendExe (Join-Path $Stage "slice_merge_tool.exe") -Force }
Copy-Item (Join-Path $Root "config\machine_material_presets.yaml") $Stage -Force

# ---- 4. Zip ----------------------------------------------------------
Info "==> Creating zip ..."
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path $Stage -DestinationPath $Zip -Force

Write-Host ""
Write-Host "Package: $Zip" -ForegroundColor Green
Write-Host "Folder : $Stage" -ForegroundColor Green
Write-Host "Distribute the zip; the customer just unzips and runs MultiMaterialSlicer.exe (no Qt/Python needed)."
