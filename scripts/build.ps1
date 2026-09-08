[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $installationPath) {
    throw 'A Visual Studio installation with MSBuild was not found.'
}

$msbuild = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $repoRoot 'IROKLightCtrl.sln') /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 /nologo
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Built: $repoRoot\build\$Configuration\IROKLightCtrl.exe"
