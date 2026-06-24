param(
    [string]$QtPrefix = "C:\Qt\5.12.12\msvc2017_64",
    [string]$BuildDir = "build-win-release",
    [string]$Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path "$PSScriptRoot\.."
$Dist = Join-Path $Root "dist\MultiMaterialSlicer-win64"
$Exe = Join-Path $Root "$BuildDir\Release\MultiMaterialSlicer.exe"

cmake -S $Root -B (Join-Path $Root $BuildDir) -G $Generator -A x64 -DCMAKE_PREFIX_PATH=$QtPrefix
cmake --build (Join-Path $Root $BuildDir) --config Release

if (Test-Path $Dist) {
    Remove-Item $Dist -Recurse -Force
}
New-Item -ItemType Directory -Path $Dist | Out-Null

Copy-Item $Exe $Dist
Copy-Item (Join-Path $Root "slice_1080p.py") $Dist
Copy-Item (Join-Path $Root "requirements.txt") $Dist

& (Join-Path $QtPrefix "bin\windeployqt.exe") (Join-Path $Dist "MultiMaterialSlicer.exe")

Compress-Archive -Path $Dist -DestinationPath (Join-Path $Root "dist\MultiMaterialSlicer-win64.zip") -Force

Write-Host "Package written to: $(Join-Path $Root 'dist\MultiMaterialSlicer-win64.zip')"
