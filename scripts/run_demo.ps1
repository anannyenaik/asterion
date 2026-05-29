param(
  [string]$BuildDir = "build",
  [switch]$SkipBuild,
  [string]$PythonExe = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $PythonExe) {
  $PythonExe = (Get-Command python -ErrorAction Stop).Source
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -and (Test-Path "C:\msys64\ucrt64\bin")) {
  $env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
}

$BuildPath = Join-Path $Root $BuildDir
if (-not $SkipBuild) {
  cmake -S $Root -B $BuildPath -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DASTERION_ENABLE_WARNINGS=ON `
    -DASTERION_BUILD_PYTHON=ON `
    "-DPython3_EXECUTABLE=$PythonExe"
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
  cmake --build $BuildPath --target asterion_replay asterion_latency_budget asterion_benchmarks asterion_python_package
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

$env:PYTHONPATH = "$(Join-Path $BuildPath "python")$([IO.Path]::PathSeparator)$env:PYTHONPATH"
$OutDir = Join-Path $BuildPath "demo"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Invoke-Native {
  param(
    [string]$FilePath,
    [string[]]$Arguments
  )
  & $FilePath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "command failed ($LASTEXITCODE): $FilePath $($Arguments -join ' ')"
  }
}

function Invoke-NativeQuiet {
  param(
    [string]$FilePath,
    [string[]]$Arguments
  )
  & $FilePath @Arguments *> $null
  if ($LASTEXITCODE -ne 0) {
    throw "command failed ($LASTEXITCODE): $FilePath $($Arguments -join ' ')"
  }
}

function Get-ToolPath {
  param([string]$Name)
  $Plain = Join-Path $BuildPath $Name
  $Exe = Join-Path $BuildPath "$Name.exe"
  if (Test-Path $Plain) {
    return $Plain
  }
  if (Test-Path $Exe) {
    return $Exe
  }
  throw "missing built tool: $Name in $BuildPath"
}

function Section {
  param([string]$Name)
  Write-Host ""
  Write-Host "== $Name =="
}

$Replay = Get-ToolPath "asterion_replay"
$Latency = Get-ToolPath "asterion_latency_budget"
$Bench = Get-ToolPath "asterion_benchmarks"
$Cli = Join-Path $Root "scripts/asterion_inspect.py"

Section "CSV Replay"
Invoke-Native $Replay @("--input", (Join-Path $Root "data/samples/sample_replay.csv"), "--no-diagnostics")

Section "Binary Replay"
Invoke-Native $Replay @("--input", (Join-Path $Root "data/samples/sample_replay.bin"), "--format", "binary", "--no-diagnostics")

Section "Shared Vs Grouped Replay Parity"
Invoke-Native $PythonExe @($Cli, "replay-parity", "--input", (Join-Path $Root "data/samples/sample_replay.csv"), "--json")

Section "Diagnostics Summary"
Invoke-Native $PythonExe @($Cli, "diagnostics", "--input", (Join-Path $Root "data/samples/sample_replay.csv"))

Section "Risk Audit Summary"
Invoke-Native $PythonExe @($Cli, "audit-summary", "--input", (Join-Path $Root "data/samples/sample_risk_audit.jsonl"))
Invoke-Native $PythonExe @($Cli, "audit-verify", "--input", (Join-Path $Root "data/samples/sample_risk_audit.jsonl"))

Section "Audit Manifest Verification"
$Manifest = Join-Path $OutDir "sample_risk_audit.manifest.jsonl"
Invoke-Native $PythonExe @(
  $Cli, "audit-manifest",
  "--input", (Join-Path $Root "data/samples/sample_risk_audit.jsonl"),
  "--output", $Manifest,
  "--json"
)
Invoke-Native $PythonExe @(
  $Cli, "audit-manifest-verify",
  "--manifest", $Manifest,
  "--base-dir", (Join-Path $Root "data/samples"),
  "--json"
)

Section "Portfolio Risk Snapshot"
Invoke-Native $PythonExe @($Cli, "portfolio-risk", "--input", (Join-Path $Root "data/samples/sample_portfolio_risk.json"))

Section "Latency-Budget Summary"
$LatencyJson = Join-Path $OutDir "latency_budget.json"
Invoke-Native $Latency @("--iterations", "200", "--json", $LatencyJson, "--no-text")
Invoke-NativeQuiet $PythonExe @("-m", "json.tool", $LatencyJson)
$LatencyPayload = Get-Content -LiteralPath $LatencyJson -Raw | ConvertFrom-Json
Write-Host "latency_budget_json=$LatencyJson"
Write-Host "stage_count=$($LatencyPayload.stages.Count)"
Write-Host "exceeded_count=$($LatencyPayload.exceeded_count)"
Write-Host "config_checksum=$($LatencyPayload.config_checksum)"

Section "Benchmark JSON Generation"
$BenchJson = Join-Path $OutDir "asterion_benchmark.json"
Invoke-Native $Bench @("--json", $BenchJson, "--no-text")
Invoke-NativeQuiet $PythonExe @("-m", "json.tool", $BenchJson)
$BenchmarkPayload = Get-Content -LiteralPath $BenchJson -Raw | ConvertFrom-Json
Write-Host "benchmark_json=$BenchJson"
Write-Host "benchmark_count=$($BenchmarkPayload.benchmarks.Count)"
Write-Host "benchmark_results_committed=false"

Section "Demo Complete"
Write-Host "generated_outputs=$OutDir"
