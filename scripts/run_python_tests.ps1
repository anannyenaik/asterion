param(
  [string]$BuildDir = "build",
  [string]$PythonExe = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $PythonExe) {
  $PythonExe = (Get-Command python -ErrorAction Stop).Source
}

$BuildPath = Join-Path $Root $BuildDir
$env:PYTHONPATH = "$(Join-Path $BuildPath "python")$([IO.Path]::PathSeparator)$env:PYTHONPATH"
& $PythonExe -m pytest (Join-Path $Root "python/tests")
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
