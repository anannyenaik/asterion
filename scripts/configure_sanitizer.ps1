param(
  [string]$BuildDir = "build-debug",
  [string]$PythonExe = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $PythonExe) {
  $PythonExe = (Get-Command python -ErrorAction Stop).Source
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -and (Test-Path "C:\msys64\ucrt64\bin")) {
  $env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
}

cmake -S $Root -B (Join-Path $Root $BuildDir) -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DASTERION_ENABLE_WARNINGS=ON `
  -DASTERION_ENABLE_SANITIZERS=ON `
  -DASTERION_BUILD_PYTHON=ON `
  "-DPython3_EXECUTABLE=$PythonExe"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
