$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Remove-InRepo {
  param([string]$Path)
  if (-not (Test-Path $Path)) {
    return
  }
  $Resolved = (Resolve-Path $Path).Path
  if (-not $Resolved.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove path outside repository: $Resolved"
  }
  Remove-Item -LiteralPath $Resolved -Recurse -Force
}

Remove-InRepo (Join-Path $Root "build")
Get-ChildItem -LiteralPath $Root -Directory -Filter "build-*" | ForEach-Object {
  Remove-InRepo $_.FullName
}
Remove-InRepo (Join-Path $Root "data/generated")
Remove-InRepo (Join-Path $Root "benchmarks/history")
Remove-InRepo (Join-Path $Root "benchmarks/results")
Remove-InRepo (Join-Path $Root ".pytest_cache")
Get-ChildItem -LiteralPath $Root -Directory -Recurse -Filter "__pycache__" | ForEach-Object {
  Remove-InRepo $_.FullName
}
