# Build the backend command-line tool (slice_merge_tool.exe) from slice_1080p.py
# using PyInstaller, so the App does not depend on a Python install at runtime.
param(
    [string]$Python = "py",
    [string]$VenvDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path "$PSScriptRoot\.."
if ([string]::IsNullOrWhiteSpace($VenvDir)) {
    $VenvDir = Join-Path $Root "backend_build\venv"
}

Push-Location $Root
& $Python -m venv $VenvDir
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
& $VenvPython -m pip install --upgrade pip
& $VenvPython -m pip install --upgrade pyinstaller opencv-python numpy pyyaml
& $VenvPython -m PyInstaller --clean --onefile --name slice_merge_tool `
    --distpath (Join-Path $Root "backend_dist") `
    --workpath (Join-Path $Root "backend_build") `
    --specpath (Join-Path $Root "backend_build") `
    slice_1080p.py
Pop-Location

Write-Host "Backend tool written to: $(Join-Path $Root 'backend_dist\slice_merge_tool.exe')"
Write-Host "It still supports:  slice_merge_tool.exe --config <config.yaml> --output <merged_dir>"
