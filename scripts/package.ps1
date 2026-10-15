param(
    [ValidateSet('Release', 'RelWithDebInfo', 'MinSizeRel')][string]$Configuration = 'Release',
    [string]$DestinationDir,
    [string]$VisualStudio,
    [string]$WindowsSdk,
    [string]$FFmpegRoot,
    [switch]$ForPublicRelease
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native-tools.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$ffmpegRootPath = if ($FFmpegRoot) {
    if ([System.IO.Path]::IsPathRooted($FFmpegRoot)) { [System.IO.Path]::GetFullPath($FFmpegRoot) }
    else { [System.IO.Path]::GetFullPath((Join-Path $projectRoot $FFmpegRoot)) }
} else { [System.IO.Path]::GetFullPath((Join-Path $projectRoot '.deps/ffmpeg')) }
$releaseSources = Join-Path $projectRoot '.deps/release-sources'
function Assert-FileHash([string]$Path, [string]$Algorithm, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing release input: $Path" }
    if ((Get-FileHash -LiteralPath $Path -Algorithm $Algorithm).Hash -ine $Expected) {
        throw "Release input hash mismatch: $Path"
    }
}
if ($ForPublicRelease) {
    $required = @(
        (Join-Path $releaseSources 'qt/qtbase-everywhere-src-6.8.3.tar.xz'),
        (Join-Path $releaseSources 'qt/qtbase-everywhere-src-6.8.3.tar.xz.sha256'),
        (Join-Path $releaseSources 'ffmpeg/ffmpeg-n8.1.3.tar.gz'),
        (Join-Path $releaseSources 'microsoft-vs2022-redist.txt'),
        (Join-Path $releaseSources 'microsoft-sdk-redist.txt'),
        (Join-Path $ffmpegRootPath 'BUILD-MANIFEST.json')
    )
    foreach ($path in $required) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Public binary material is missing: $path. Run scripts/fetch-release-materials.ps1 and build the minimal FFmpeg first."
        }
    }
    Assert-FileHash (Join-Path $releaseSources 'qt/qtbase-everywhere-src-6.8.3.tar.xz') SHA256 '56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80'
    $ffmpegSourceHash = (Get-Content -LiteralPath (Join-Path $projectRoot 'licenses/ffmpeg-minimal/SOURCE-SHA512.txt') -Raw).Trim().Split()[0]
    Assert-FileHash (Join-Path $releaseSources 'ffmpeg/ffmpeg-n8.1.3.tar.gz') SHA512 $ffmpegSourceHash
    $ffmpegManifest = Get-Content -LiteralPath (Join-Path $ffmpegRootPath 'BUILD-MANIFEST.json') -Raw | ConvertFrom-Json
    if ($ffmpegManifest.source_sha512 -ine $ffmpegSourceHash -or
        $ffmpegManifest.configure -notmatch '--disable-autodetect' -or
        $ffmpegManifest.configure -match '--enable-(gpl|nonfree|version3|lib\w+)(\s|$)') {
        throw 'Public packaging requires the source-built minimal FFmpeg configuration.'
    }
    foreach ($entry in $ffmpegManifest.files) {
        $entryPath = [System.IO.Path]::GetFullPath((Join-Path $ffmpegRootPath $entry.path))
        if (-not $entryPath.StartsWith($ffmpegRootPath.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'FFmpeg manifest contains an invalid path.'
        }
        Assert-FileHash $entryPath SHA256 $entry.sha256
    }
}
$binaryDir = Join-Path $projectRoot "build\$Configuration"
$destination = if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
    Join-Path $projectRoot 'dist\QueuePilot'
} elseif ([System.IO.Path]::IsPathRooted($DestinationDir)) {
    [System.IO.Path]::GetFullPath($DestinationDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $projectRoot $DestinationDir))
}
if ($ForPublicRelease -and (Test-Path -LiteralPath $destination)) {
    throw 'A public package must use a new destination directory, so no user data or stale DLLs can enter the archive.'
}
if (-not (Test-Path "$binaryDir\QueuePilot.exe")) { throw 'Build the application first.' }
if ($ForPublicRelease) {
    $cache = Get-Content -LiteralPath (Join-Path $projectRoot 'build/CMakeCache.txt')
    $configuredRoot = @($cache | Where-Object { $_ -match '^FFMPEG_ROOT:PATH=' })
    if ($configuredRoot.Count -ne 1 -or [System.IO.Path]::GetFullPath(($configuredRoot[0] -split '=',2)[1]) -ine $ffmpegRootPath) {
        throw 'Rebuild QueuePilot with the selected minimal -FFmpegRoot before public packaging.'
    }
    foreach ($dll in (Get-ChildItem -LiteralPath (Join-Path $ffmpegRootPath 'bin') -File -Filter '*.dll' |
        Where-Object { $_.Name -match '^(avcodec|avformat|avutil|swscale|swresample)-' })) {
        Assert-FileHash (Join-Path $binaryDir $dll.Name) SHA256 (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash
    }
}
$modelFiles = @('recognizer.onnx', 'detector.onnx', 'keys.txt', 'README.md')
foreach ($name in $modelFiles) {
    if (-not (Test-Path -LiteralPath "$projectRoot\models\$name" -PathType Leaf)) {
        throw "The production model asset is missing: $name"
    }
}
$targetExecutable = Join-Path $destination 'QueuePilot.exe'
$runningTarget = Get-Process -Name QueuePilot -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and [System.IO.Path]::GetFullPath($_.Path) -eq $targetExecutable }
if ($runningTarget) {
    throw "Close QueuePilot running from $destination, or package to another -DestinationDir."
}
if (Test-Path -LiteralPath $targetExecutable) {
    try {
        $lockCheck = [System.IO.File]::Open($targetExecutable, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        $lockCheck.Dispose()
    } catch {
        throw "The target executable is in use or not writable. Close it, or choose another -DestinationDir."
    }
}
$vsPath = Find-QueuePilotVisualStudio $VisualStudio
$sdk = Find-QueuePilotWindowsSdk $WindowsSdk
$d3dCompiler = Join-Path $sdk.Root 'Redist/D3D/x64/d3dcompiler_47.dll'
if (-not (Test-Path -LiteralPath $d3dCompiler -PathType Leaf)) {
    throw "Windows SDK redistributable d3dcompiler_47.dll was not found: $d3dCompiler"
}
$crtDir = Get-ChildItem "$vsPath\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" -Directory |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crtDir -or -not (Test-Path (Join-Path $crtDir.FullName 'vcruntime140.dll'))) {
    throw 'Microsoft VC143 x64 redistributable DLLs were not found.'
}
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item -LiteralPath "$binaryDir\QueuePilot.exe" -Destination $destination -Force
Copy-Item -Path "$binaryDir\*.dll" -Destination $destination -Force
$modelDestination = Join-Path $destination 'models'
New-Item -ItemType Directory -Force $modelDestination | Out-Null
foreach ($name in $modelFiles) {
    Copy-Item -LiteralPath "$projectRoot\models\$name" -Destination "$modelDestination\$name" -Force
}
$deployArguments = @('--release', '--no-translations', '--no-compiler-runtime', "$destination\QueuePilot.exe")
if ((Invoke-QueuePilotNative "$projectRoot\.deps\qt\bin\windeployqt.exe" $deployArguments) -ne 0) { throw 'Qt deployment failed.' }
Copy-Item -Path (Join-Path $crtDir.FullName '*.dll') -Destination $destination -Force
Remove-Item -LiteralPath (Join-Path $destination 'd3dcompiler_47.dll') -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $d3dCompiler -Destination $destination -Force
Copy-Item -LiteralPath "$projectRoot\README.md", "$projectRoot\THIRD_PARTY_NOTICES.md", "$projectRoot\LICENSE" -Destination $destination -Force
Copy-Item -LiteralPath "$projectRoot\licenses" -Destination $destination -Recurse -Force
Copy-Item -LiteralPath "$projectRoot\docs", "$projectRoot\assets" -Destination $destination -Recurse -Force
Copy-Item -LiteralPath "$projectRoot\assets\queuepilot.ico" -Destination $destination -Force
if ($ForPublicRelease) {
    $sourceDestination = Join-Path $destination 'sources'
    New-Item -ItemType Directory -Force $sourceDestination | Out-Null
    Copy-Item -LiteralPath (Join-Path $releaseSources 'qt/qtbase-everywhere-src-6.8.3.tar.xz'),
        (Join-Path $releaseSources 'qt/qtbase-everywhere-src-6.8.3.tar.xz.sha256'),
        (Join-Path $releaseSources 'ffmpeg/ffmpeg-n8.1.3.tar.gz'),
        (Join-Path $releaseSources 'microsoft-vs2022-redist.txt'),
        (Join-Path $releaseSources 'microsoft-sdk-redist.txt') -Destination $sourceDestination
    $evidenceDestination = Join-Path $sourceDestination 'sdk-evidence'
    & (Join-Path $projectRoot 'licenses/qt-release/collect-evidence.ps1') -VisualStudio $vsPath -WindowsSdk $sdk.Root `
        -Destination $evidenceDestination -QtSourceArchive (Join-Path $releaseSources 'qt/qtbase-everywhere-src-6.8.3.tar.xz') `
        -QtSourceChecksum (Join-Path $releaseSources 'qt/qtbase-everywhere-src-6.8.3.tar.xz.sha256') `
        -VisualStudioRedistList (Join-Path $releaseSources 'microsoft-vs2022-redist.txt') `
        -WindowsSdkRedistList (Join-Path $releaseSources 'microsoft-sdk-redist.txt') | Out-Host
    $ffmpegEvidence = Join-Path $sourceDestination 'ffmpeg-build'
    New-Item -ItemType Directory -Force $ffmpegEvidence | Out-Null
    foreach ($name in @('BUILD-MANIFEST.json','FFmpeg-VERSION.txt','FFmpeg-BUILD-CONFIG.txt','COPYING.LGPLv2.1','COPYING.LGPLv3','LICENSE.md','CREDITS')) {
        Copy-Item -LiteralPath (Join-Path $ffmpegRootPath $name) -Destination $ffmpegEvidence
    }
    Copy-Item -LiteralPath (Join-Path $ffmpegRootPath 'build-records') -Destination $ffmpegEvidence -Recurse
    Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts/build-ffmpeg.ps1'),
        (Join-Path $projectRoot 'scripts/fetch-release-materials.ps1') -Destination $sourceDestination
    Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts/native-tools.ps1') -Destination $sourceDestination
}
Write-Host "Package: $destination\QueuePilot.exe"
