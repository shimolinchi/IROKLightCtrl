[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path $env:TEMP 'LightController-diagnostic.json'),
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repoRoot 'build\Release\LightController.exe'

if (-not $SkipBuild -or -not (Test-Path -LiteralPath $executable)) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release
}

$process = Start-Process -FilePath $executable `
    -ArgumentList @('--diagnose', ('"{0}"' -f $OutputPath)) `
    -WindowStyle Hidden `
    -Wait `
    -PassThru

if ($process.ExitCode -ne 0) {
    throw "LightController diagnostics failed with exit code $($process.ExitCode)."
}

Get-Content -LiteralPath $OutputPath -Raw
