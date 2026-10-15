[CmdletBinding()]
param(
    [string]$SourceArchive = '',
    [string]$OutputRoot = '',
    [string]$BuildRoot = '',
    [string]$SourceSha512 = '',
    [int]$Jobs = 0,
    [string]$VisualStudio = '',
    [string]$Bash = '',
    [string]$Make = '',
    [switch]$FinalizeOnly,
    [switch]$KeepBuild
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$SourceArchive = if ($SourceArchive) { $SourceArchive } else { Join-Path $PSScriptRoot '../.deps/downloads/ffmpeg-n8.1.3.tar.gz' }
$OutputRoot = if ($OutputRoot) { $OutputRoot } else { Join-Path $PSScriptRoot '../.deps/ffmpeg-minimal' }
$BuildRoot = if ($BuildRoot) { $BuildRoot } else { Join-Path $PSScriptRoot '../.deps/ffmpeg-minimal-build' }
$sourceUrl = 'https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n8.1.3.tar.gz'
$defaultSourceSha512 = '8c667090bcdc70796caaa8cdc2cf82e57d7c7f6feedcb0ec7251cefd6a0c63b7cad203a86c800d3e2f69ca91255167aa4e631701060b96d576d1a9666ca7aafe'
$hashFile = Join-Path $projectRoot 'licenses/ffmpeg-minimal/SOURCE-SHA512.txt'
if (-not $SourceSha512 -and (Test-Path -LiteralPath $hashFile)) {
    $SourceSha512 = (Get-Content -LiteralPath $hashFile -Raw).Trim().Split()[0]
}
if (-not $SourceSha512) { $SourceSha512 = $defaultSourceSha512 }
if ($SourceSha512.ToLowerInvariant() -ne $defaultSourceSha512) { throw "Only the fixed n8.1.3 source hash is accepted: $defaultSourceSha512" }

function Find-Exe([string]$Name, [string[]]$Candidates = @()) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    return $null
}

function To-MsysPath([string]$Path) {
    $full = (Resolve-Path -LiteralPath $Path).Path.Replace('\', '/')
    if ($full -match '^([A-Za-z]):/(.*)$') { return '/' + $Matches[1].ToLowerInvariant() + '/' + $Matches[2] }
    return $full
}

if (-not (Test-Path -LiteralPath $SourceArchive)) {
    throw "FFmpeg source archive is missing: $SourceArchive`nDownload the fixed official archive manually:`n  $sourceUrl`nExpected SHA-512: $SourceSha512"
}
$actualSha512 = (Get-FileHash -LiteralPath $SourceArchive -Algorithm SHA512).Hash.ToLowerInvariant()
if ($actualSha512 -ne $SourceSha512.ToLowerInvariant()) {
    throw "FFmpeg source hash mismatch. Expected $SourceSha512, got $actualSha512"
}

$sevenZip = Find-Exe '7z.exe' @(
    'D:/vcpkg/downloads/tools/7zip-26.01-windows/7z.exe',
    'C:/Program Files/7-Zip/7z.exe'
)
if (-not $sevenZip) { throw '7z.exe is required to extract the fixed FFmpeg source archive.' }

if (-not $Bash) {
    $Bash = Find-Exe 'bash.exe' @(
        (Join-Path ${env:ProgramFiles} 'Git/bin/bash.exe'),
        'D:/Git/usr/bin/bash.exe',
        'C:/msys64/usr/bin/bash.exe',
        'C:/tools/msys64/usr/bin/bash.exe'
    )
}
if (-not $Bash) { throw 'A POSIX bash with make (MSYS2 or Git Bash) is required. Pass -Bash to its bash.exe.' }
$bashRoot = Split-Path (Split-Path $Bash -Parent) -Parent
$make = if ($Make) { (Resolve-Path -LiteralPath $Make -ErrorAction Stop).Path } else { Join-Path (Split-Path $Bash -Parent) 'make.exe' }
if (-not (Test-Path -LiteralPath $make)) {
    $make = Find-Exe 'make.exe' @(
        (Join-Path $bashRoot 'usr/bin/make.exe'),
        (Join-Path $projectRoot '.deps/make-tools/usr/bin/make.exe'),
        'C:/msys64/usr/bin/make.exe',
        'C:/tools/msys64/usr/bin/make.exe'
    )
}
if (-not $make) { throw "make.exe was not found beside $Bash. Install the MSYS2 make package and rerun." }

if ($Jobs -le 0) { $Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1) }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not $VisualStudio -and (Test-Path -LiteralPath $vswhere)) {
    $VisualStudio = (& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
}
if (-not $VisualStudio) {
    foreach ($candidate in @('D:/VS', 'C:/Program Files/Microsoft Visual Studio/2022/Community', 'C:/Program Files/Microsoft Visual Studio/2022/BuildTools')) {
        if (Test-Path (Join-Path $candidate 'Common7/Tools/VsDevCmd.bat')) { $VisualStudio = $candidate; break }
    }
}
if (-not $VisualStudio) { throw 'Visual Studio 2022 C++ tools were not found.' }
$vsDevCmd = Join-Path $VisualStudio 'Common7/Tools/VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) { throw "VsDevCmd.bat was not found under $VisualStudio" }

# Import the x64 MSVC environment into this PowerShell process before invoking bash.
$envLines = cmd.exe /d /s /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path ("Env:" + $Matches[1]) -Value $Matches[2] }
}
$nasm = Find-Exe 'nasm.exe' @(
    (Join-Path (Split-Path $Bash -Parent) 'nasm.exe'),
    (Join-Path $bashRoot 'usr/bin/nasm.exe'),
    'D:/vcpkg/downloads/tools/nasm/nasm-3.01/nasm.exe',
    'C:/msys64/usr/bin/nasm.exe'
)
if (-not $nasm) { throw 'nasm.exe is required for the x64 FFmpeg build.' }
$env:PATH = "$(Split-Path $nasm -Parent);$(Split-Path $make -Parent);$($env:PATH)"

$buildPath = (Resolve-Path (New-Item -ItemType Directory -Force -Path $BuildRoot)).Path
$outputPath = (Resolve-Path (New-Item -ItemType Directory -Force -Path $OutputRoot)).Path
foreach ($path in @($buildPath, $outputPath)) {
    if (-not $path.StartsWith($projectRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Build and output directories must be inside the project: $path"
    }
}
if ($buildPath -eq $outputPath -or $projectRoot -eq $buildPath) { throw 'BuildRoot and OutputRoot must be distinct subdirectories.' }
if (-not $FinalizeOnly -and (Get-ChildItem -LiteralPath $outputPath -Force | Measure-Object).Count -ne 0) {
    throw "The output directory must be empty to avoid stale DLLs: $outputPath"
}
$sourcePath = Join-Path $buildPath 'source'
$cl = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$clVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($cl).FileVersion
if (-not $FinalizeOnly) {
if (Test-Path $sourcePath) { Remove-Item -LiteralPath $sourcePath -Recurse -Force }
$extractPath = Join-Path $buildPath 'archive'
if (Test-Path $extractPath) { Remove-Item -LiteralPath $extractPath -Recurse -Force }
New-Item -ItemType Directory -Force -Path $extractPath | Out-Null
& $sevenZip x $SourceArchive "-o$extractPath" '-y' | Out-Host
if ($LASTEXITCODE -ne 0) { throw '7z failed to extract the FFmpeg source archive.' }
$tar = Get-ChildItem -LiteralPath $extractPath -File -Filter '*.tar' | Select-Object -First 1
if ($tar) {
    & $sevenZip x $tar.FullName "-o$extractPath" '-y' | Out-Host
    if ($LASTEXITCODE -ne 0) { throw '7z failed to extract the nested FFmpeg tar archive.' }
}
$nested = Get-ChildItem -LiteralPath $extractPath -Directory | Select-Object -First 1
if (-not $nested) { throw 'The FFmpeg source archive did not contain a top-level directory.' }
Move-Item -LiteralPath $nested.FullName -Destination $sourcePath

$sourceM = To-MsysPath $sourcePath
$outputM = To-MsysPath $outputPath
$makeM = To-MsysPath $make
$cl = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$clBinM = To-MsysPath (Split-Path $cl -Parent)
$clVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($cl).FileVersion
# A localized MSVC-only installation may ignore VSLANG. Match the vendor token
# anywhere on the banner, as the maintained vcpkg FFmpeg MSVC patch does.
$configurePath = Join-Path $sourcePath 'configure'
$configureText = [IO.File]::ReadAllText($configurePath)
$configureText = $configureText.Replace('grep -q ^Microsoft', 'grep -qi Microsoft').Replace('grep ^Microsoft | head', 'grep -i Microsoft | head')
[IO.File]::WriteAllText($configurePath, $configureText, (New-Object Text.UTF8Encoding($false)))
$clWrapper = Join-Path $buildPath 'cl-queuepilot'
@"
#!/bin/sh
case "`$1" in
  ''|-nologo-) echo 'Microsoft (R) C/C++ Optimizing Compiler $clVersion'; exit 0 ;;
esac
exec cl.exe "`$@"
"@.TrimStart() | Set-Content -LiteralPath $clWrapper -Encoding ASCII
$configure = @"
set -eux
export PATH='$(To-MsysPath $buildPath)':'$clBinM':`$PATH
export VSLANG=1033
cd '$sourceM'
./configure --prefix='$outputM' --toolchain=msvc --arch=x86_64 --target-os=win32 --cc=cl-queuepilot --cxx=cl-queuepilot --enable-shared --disable-static --disable-doc --disable-debug --disable-autodetect --disable-gpl --disable-nonfree --disable-version3 --enable-runtime-cpudetect --enable-network --enable-w32threads --enable-schannel --enable-protocol=http,https,tcp,tls,crypto,file,pipe --enable-demuxers --enable-decoders --enable-parsers --enable-muxers --enable-protocols --enable-filters --enable-encoders
"@
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $Bash -lc $configure 2>&1 | Tee-Object -FilePath (Join-Path $buildPath 'configure.log') | Out-Host
$configureExit = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
if ($configureExit -ne 0) { throw "FFmpeg configure failed (exit $configureExit)." }
$buildCmd = "export PATH='$(To-MsysPath $buildPath)':'$clBinM':`$PATH; export VSLANG=1033; cd '$sourceM' && '$makeM' -j$Jobs && '$makeM' install"
$ErrorActionPreference = 'Continue'
& $Bash -lc $buildCmd 2>&1 | Tee-Object -FilePath (Join-Path $buildPath 'build.log') | Out-Host
$buildExit = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
if ($buildExit -ne 0) { throw "FFmpeg build or install failed (exit $buildExit)." }
}

$installedExe = Join-Path $outputPath 'bin/ffmpeg.exe'
if (-not (Test-Path -LiteralPath $installedExe)) { throw 'The FFmpeg build did not install ffmpeg.exe.' }
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$versionText = (& $installedExe -version 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'The installed FFmpeg executable could not start.' }
$versionText | Set-Content -LiteralPath (Join-Path $outputPath 'FFmpeg-VERSION.txt') -Encoding UTF8
if ($versionText -match '--enable-(gpl|nonfree|version3)(\s|$)') { throw 'Unexpected restricted component in the FFmpeg build configuration.' }
$configText = (& $installedExe -buildconf 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Reading the FFmpeg build configuration failed.' }
$configText | Set-Content -LiteralPath (Join-Path $outputPath 'FFmpeg-BUILD-CONFIG.txt') -Encoding UTF8
$ErrorActionPreference = $previousErrorAction
New-Item -ItemType Directory -Force -Path (Join-Path $outputPath 'lib') | Out-Null
Get-ChildItem -LiteralPath (Join-Path $outputPath 'bin') -Filter '*.lib' -File | Copy-Item -Destination (Join-Path $outputPath 'lib')
foreach ($name in @('COPYING.LGPLv2.1', 'COPYING.LGPLv3', 'LICENSE.md', 'CREDITS')) {
    $licensePath = Join-Path $sourcePath $name
    if (Test-Path -LiteralPath $licensePath) { Copy-Item -LiteralPath $licensePath -Destination $outputPath }
}

$recordsPath = Join-Path $outputPath 'build-records'
$recordFiles = @(
    'configure.log', 'build.log', 'cl-queuepilot',
    'source/configure', 'source/config.h', 'source/config_components.h',
    'source/ffbuild/config.mak', 'source/ffbuild/config.log'
)
foreach ($relative in $recordFiles) {
    $recordSource = Join-Path $buildPath $relative
    if (-not (Test-Path -LiteralPath $recordSource -PathType Leaf)) { throw "Missing FFmpeg build record: $recordSource" }
    $recordDestination = Join-Path $recordsPath $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $recordDestination -Parent) | Out-Null
    Copy-Item -LiteralPath $recordSource -Destination $recordDestination -Force
}
$tools = [ordered]@{}
$toolPaths = [ordered]@{
    cl = $cl
    rc = (Get-Command rc.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    link = (Get-Command link.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    nasm = $nasm
    make = $make
}
foreach ($toolName in $toolPaths.Keys) {
    $toolPath = $toolPaths[$toolName]
    $toolVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($toolPath)
    $tools[$toolName] = [ordered]@{
        path = $toolPath
        file_version = $toolVersion.FileVersion
        product_version = $toolVersion.ProductVersion
        sha256 = (Get-FileHash -LiteralPath $toolPath -Algorithm SHA256).Hash
    }
}
$ErrorActionPreference = 'Continue'
$tools['nasm']['version_output'] = ((& $nasm -v 2>&1) -join "`n").Trim()
$tools['make']['version_output'] = ((& $Bash -lc "'$(To-MsysPath $make)' --version" 2>&1) -join "`n").Trim()
$ErrorActionPreference = $previousErrorAction
$tools | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $recordsPath 'TOOLS.json') -Encoding UTF8
$actualConfiguration = (Get-Content -LiteralPath (Join-Path $sourcePath 'ffbuild/config.mak') | Where-Object { $_.StartsWith('FFMPEG_CONFIGURATION=') } | Select-Object -First 1).Substring('FFMPEG_CONFIGURATION='.Length)

$manifest = [ordered]@{
    project = 'QueuePilot'
    source_url = $sourceUrl
    source_sha512 = $actualSha512
    version = 'n8.1.3'
    configure = $actualConfiguration
    visual_studio = $VisualStudio
    msvc_compiler = $cl
    msvc_compiler_version = $clVersion
    bash = $Bash
    make = $make
    nasm = $nasm
    jobs = $Jobs
    generated_utc = [DateTime]::UtcNow.ToString('o')
    files = @()
}
foreach ($file in Get-ChildItem -LiteralPath $outputPath -Recurse -File | Where-Object { $_.Name -ne 'BUILD-MANIFEST.json' } | Sort-Object FullName) {
    $manifest.files += [ordered]@{ path = $file.FullName.Substring($outputPath.Length + 1); sha256 = (Get-FileHash $file.FullName -Algorithm SHA256).Hash }
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'BUILD-MANIFEST.json') -Encoding UTF8
if (-not $KeepBuild -and -not $FinalizeOnly) { Remove-Item -LiteralPath $buildPath -Recurse -Force }
Write-Host "FFmpeg minimal build installed at $outputPath"
