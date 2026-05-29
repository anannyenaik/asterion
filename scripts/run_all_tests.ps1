param(
  [string]$BuildDir = "build",
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

& (Join-Path $PSScriptRoot "configure_release.ps1") -BuildDir $BuildDir -PythonExe $PythonExe
$BuildPath = Join-Path $Root $BuildDir
cmake --build $BuildPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
ctest --test-dir $BuildPath --output-on-failure
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
& (Join-Path $PSScriptRoot "run_python_tests.ps1") -BuildDir $BuildDir -PythonExe $PythonExe
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
