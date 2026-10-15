[CmdletBinding()]
param(
    [string]$Destination = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$root = if ($Destination) {
    if ([System.IO.Path]::IsPathRooted($Destination)) { [System.IO.Path]::GetFullPath($Destination) }
    else { [System.IO.Path]::GetFullPath((Join-Path $projectRoot $Destination)) }
} else { Join-Path $projectRoot '.deps/release-sources' }
$qtRoot = Join-Path $root 'qt'
$ffmpegRoot = Join-Path $root 'ffmpeg'
New-Item -ItemType Directory -Force $qtRoot | Out-Null
New-Item -ItemType Directory -Force $ffmpegRoot | Out-Null
New-Item -ItemType Directory -Force $root | Out-Null

function Hash-File([string]$Path, [string]$Algorithm) {
    $hash = [System.Security.Cryptography.HashAlgorithm]::Create($Algorithm)
    $stream = [System.IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
}

function Download-Verified([string]$Url, [string]$Path, [string]$Algorithm, [string]$Expected) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Hash-File $Path $Algorithm) -ne $Expected.ToLowerInvariant()) {
        Write-Host "Downloading $Url"
        & curl.exe -sS -L --fail --proto '=https' --proto-redir '=https' --retry 2 --connect-timeout 20 --max-time 1800 -o $Path $Url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
    }
    $actual = Hash-File $Path $Algorithm
    if ($actual -ne $Expected.ToLowerInvariant()) { throw "Hash mismatch for $Path. Expected $Expected, got $actual" }
}

$qtUrl = 'https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz'
$qtHash = '56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80'
Download-Verified $qtUrl (Join-Path $qtRoot 'qtbase-everywhere-src-6.8.3.tar.xz') 'SHA256' $qtHash
$qtChecksumPath = Join-Path $qtRoot 'qtbase-everywhere-src-6.8.3.tar.xz.sha256'
# The checksum list is itself a corresponding-source record. Pin its hash so a
# stale or substituted mirror cannot silently change the evidence we ship.
$qtChecksumHash = '0576c7c6f57df11a0f4fa4875e88c08f6ae8652fa9f2cf2d7be3c421b94d927c'
Download-Verified ($qtUrl + '.sha256') $qtChecksumPath 'SHA256' $qtChecksumHash
$qtChecksumText = Get-Content -LiteralPath (Join-Path $qtRoot 'qtbase-everywhere-src-6.8.3.tar.xz.sha256') -Raw
if ($qtChecksumText -notmatch $qtHash) { throw 'The downloaded Qt checksum file does not contain the verified archive hash.' }

$ffmpegUrl = 'https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n8.1.3.tar.gz'
$ffmpegHash = '8c667090bcdc70796caaa8cdc2cf82e57d7c7f6feedcb0ec7251cefd6a0c63b7cad203a86c800d3e2f69ca91255167aa4e631701060b96d576d1a9666ca7aafe'
Download-Verified $ffmpegUrl (Join-Path $ffmpegRoot 'ffmpeg-n8.1.3.tar.gz') 'SHA512' $ffmpegHash
@{
    url = $ffmpegUrl
    sha512 = $ffmpegHash
    version = 'n8.1.3'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'ffmpeg-source.json') -Encoding UTF8

foreach ($item in @(
    @{ name = 'microsoft-vs2022-redist.txt'; url = 'https://aka.ms/vs/17/redist.txt' },
    @{ name = 'microsoft-sdk-redist.txt'; url = 'https://go.microsoft.com/fwlink/?LinkId=524842' }
)) {
    $target = Join-Path $root $item.name
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        & curl.exe -sS -L --fail --proto '=https' --proto-redir '=https' --retry 2 --connect-timeout 20 --max-time 180 -o $target $item.url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($item.url)" }
    }
    if ((Get-Item -LiteralPath $target).Length -lt 1024) { throw "Microsoft redistribution list is unexpectedly small: $target" }
}

@{
    qt_source_url = $qtUrl
    qt_source_sha256 = $qtHash
    ffmpeg_source_url = $ffmpegUrl
    ffmpeg_source_sha512 = $ffmpegHash
    microsoft_vs_url = 'https://aka.ms/vs/17/redist.txt'
    microsoft_sdk_url = 'https://go.microsoft.com/fwlink/?LinkId=524842'
    generated_utc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'MATERIALS-MANIFEST.json') -Encoding UTF8

Write-Host "Verified release materials under $root"
