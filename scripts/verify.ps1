param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo', 'MinSizeRel')][string]$Configuration = 'Release',
    [switch]$SkipDesktopTests,
    [string]$CMake = '',
    [string]$VisualStudio = '',
    [string]$WindowsSdk = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration -CMake $CMake `
    -VisualStudio $VisualStudio -WindowsSdk $WindowsSdk -SkipDesktopTests:$SkipDesktopTests
& (Join-Path $projectRoot 'tests/verify-reference.ps1') -Probe (Join-Path $projectRoot "build/$Configuration/ocr_probe.exe")
Write-Host 'Build, CTest and both screenshot reference checks passed.'
