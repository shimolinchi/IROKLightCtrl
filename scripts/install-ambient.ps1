[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $repoRoot "build\$Configuration"
$exePath = Join-Path $outputDir 'LightController.exe'
$identityDir = Join-Path $repoRoot 'build\identity'
$stageDir = Join-Path $identityDir 'stage'
$appxPath = Join-Path $outputDir 'LightController.identity.msix'
$publisher = 'CN=LightController Local'
$packageName = 'shimolinchi.LightController'
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdministrator = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdministrator) {
    throw 'Run this script from an elevated PowerShell window (Run as administrator).'
}

& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exePath)) {
    throw 'LightController build failed.'
}

$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$sdkBin = Get-ChildItem -LiteralPath $sdkRoot -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64' } |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'makeappx.exe') } |
    Select-Object -First 1
if (-not $sdkBin) {
    throw 'Windows SDK packaging tools were not found.'
}
$makeAppx = Join-Path $sdkBin 'makeappx.exe'
$signTool = Join-Path $sdkBin 'signtool.exe'

Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stageDir,$identityDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'sparse\AppxManifest.xml') -Destination $stageDir

$assetDir = Join-Path $outputDir 'Assets'
$publicDir = Join-Path $outputDir 'Public'
New-Item -ItemType Directory -Path $assetDir,$publicDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'sparse\Public\README.txt') -Destination $publicDir -Force

Add-Type -AssemblyName System.Drawing
function Write-Logo([string]$Path, [int]$Size) {
    $bitmap = New-Object System.Drawing.Bitmap $Size,$Size
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.Clear([System.Drawing.Color]::FromArgb(245,245,247))
        $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(249,115,22))
        $font = New-Object System.Drawing.Font 'Segoe UI',($Size * 0.42),([System.Drawing.FontStyle]::Bold),([System.Drawing.GraphicsUnit]::Pixel)
        try {
            $format = New-Object System.Drawing.StringFormat
            $format.Alignment = [System.Drawing.StringAlignment]::Center
            $format.LineAlignment = [System.Drawing.StringAlignment]::Center
            $graphics.FillRectangle($brush,0,0,$Size,$Size)
            $graphics.DrawString('L',$font,[System.Drawing.Brushes]::White,(New-Object System.Drawing.RectangleF 0,0,$Size,$Size),$format)
        } finally {
            $font.Dispose()
            $brush.Dispose()
        }
        $bitmap.Save($Path,[System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}
Write-Logo (Join-Path $assetDir 'StoreLogo.png') 50
Write-Logo (Join-Path $assetDir 'Square44x44Logo.png') 44
Write-Logo (Join-Path $assetDir 'Square150x150Logo.png') 150

Remove-Item -LiteralPath $appxPath -Force -ErrorAction SilentlyContinue
& $makeAppx pack /d $stageDir /p $appxPath /o /nv | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw 'makeappx failed.'
}

$certificate = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $publisher -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(30) } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1
if (-not $certificate) {
    $certificate = New-SelfSignedCertificate -Type Custom -Subject $publisher `
        -FriendlyName 'LightController local package signing' `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 `
        -KeyUsage DigitalSignature `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
}
$cerPath = Join-Path $identityDir 'LightController.cer'
Export-Certificate -Cert $certificate -FilePath $cerPath -Force | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation 'Cert:\CurrentUser\TrustedPeople' | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null

& $signTool sign /fd SHA256 /sha1 $certificate.Thumbprint /s My $appxPath | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw 'signtool failed.'
}

$existing = Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue
if ($existing) {
    $existing | Remove-AppxPackage
}
Add-AppxPackage -Path $appxPath -ExternalLocation $outputDir

$registered = Get-AppxPackage -Name $packageName -ErrorAction Stop
$providerPaths = @('HKCU:\Software\Microsoft\Lighting\Providers')
$deviceKeys = @(Get-ChildItem 'HKCU:\Software\Microsoft\Lighting\Devices' -ErrorAction SilentlyContinue)
foreach ($deviceKey in $deviceKeys) {
    New-ItemProperty -LiteralPath $deviceKey.PSPath -Name ControlledByForegroundApp `
        -PropertyType DWord -Value 0 -Force | Out-Null
    $providerPaths += Join-Path $deviceKey.PSPath 'Providers'
}
New-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Lighting' -Name ControlledByForegroundApp `
    -PropertyType DWord -Value 0 -Force | Out-Null
foreach ($providerPath in $providerPaths) {
    if (-not (Test-Path -LiteralPath $providerPath)) {
        continue
    }
    $properties = (Get-ItemProperty -LiteralPath $providerPath).PSObject.Properties |
        Where-Object { $_.Name -match '^\d+$' } |
        Sort-Object { [int]$_.Name }
    $remaining = @($properties.Value | Where-Object {
        $_ -ne $registered.PackageFamilyName
    })
    foreach ($property in $properties) {
        Remove-ItemProperty -LiteralPath $providerPath -Name $property.Name -ErrorAction SilentlyContinue
    }
    $order = @($registered.PackageFamilyName) + $remaining
    for ($index = 0; $index -lt $order.Count; $index++) {
        New-ItemProperty -LiteralPath $providerPath -Name ($index + 1).ToString() `
            -PropertyType String -Value $order[$index] -Force | Out-Null
    }
}

Write-Host "Registered: $($registered.PackageFullName)"
Write-Host "External location: $outputDir"
Write-Host 'LightController is first in Windows background light control priority.'
