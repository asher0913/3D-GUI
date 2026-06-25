param(
    [string]$QtPrefix = "C:\Qt\5.12.12\msvc2017_64",
    [string]$BuildDir = "build-win-release",
    [string]$Generator = "Visual Studio 17 2022",
    [bool]$BuildBackend = $true
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path "$PSScriptRoot\.."
$Dist = Join-Path $Root "dist\MultiMaterialSlicer-win64"
$Exe = Join-Path $Root "$BuildDir\Release\MultiMaterialSlicer.exe"

if ($BuildBackend) {
    & (Join-Path $Root "scripts\package_backend_windows.ps1")
}

cmake -S $Root -B (Join-Path $Root $BuildDir) -G $Generator -A x64 -DCMAKE_PREFIX_PATH=$QtPrefix
cmake --build (Join-Path $Root $BuildDir) --config Release

if (Test-Path $Dist) {
    Remove-Item $Dist -Recurse -Force
}
New-Item -ItemType Directory -Path $Dist | Out-Null

Copy-Item $Exe $Dist
Copy-Item (Join-Path $Root "slice_1080p.py") $Dist
Copy-Item (Join-Path $Root "requirements.txt") $Dist
Copy-Item (Join-Path $Root "config\machine_material_presets.yaml") $Dist

# Production backend tool, if built with package_backend_windows.ps1.
$Backend = Join-Path $Root "backend_dist\slice_merge_tool.exe"
if (Test-Path $Backend) {
    Copy-Item $Backend $Dist
    Write-Host "Bundled backend tool: slice_merge_tool.exe"
} else {
    Write-Host "WARNING: backend_dist\slice_merge_tool.exe not found; bundling slice_1080p.py for dev mode only."
    Write-Host "         Run scripts\package_backend_windows.ps1 first for a Python-free release."
}

& (Join-Path $QtPrefix "bin\windeployqt.exe") (Join-Path $Dist "MultiMaterialSlicer.exe")

Compress-Archive -Path $Dist -DestinationPath (Join-Path $Root "dist\MultiMaterialSlicer-win64.zip") -Force

Write-Host "Package written to: $(Join-Path $Root 'dist\MultiMaterialSlicer-win64.zip')"
