param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo', 'MinSizeRel')][string]$Configuration = 'Release',
    [switch]$SkipTests,
    [switch]$SkipDesktopTests,
    [string]$CMake = '',
    [string]$VisualStudio = '',
    [string]$WindowsSdk = '',
    [string]$FFmpegRoot = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native-tools.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $vsPath = Find-QueuePilotVisualStudio $VisualStudio
    $cmakePath = Find-QueuePilotNativeTool 'cmake.exe' $CMake
    $sdk = Find-QueuePilotWindowsSdk $WindowsSdk
    $sdkEnvironment = @{
        '_LatestWindowsTargetPlatformVersion' = $sdk.Version
        'TargetPlatformSdkPath' = $sdk.Root
        'TargetPlatformDisplayName' = 'Windows SDK'
    }
    $ffmpegRootPath = if ($FFmpegRoot) {
        if ([System.IO.Path]::IsPathRooted($FFmpegRoot)) {
            [System.IO.Path]::GetFullPath($FFmpegRoot)
        } else {
            [System.IO.Path]::GetFullPath((Join-Path $projectRoot $FFmpegRoot))
        }
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $projectRoot '.deps/ffmpeg'))
    }
    if (-not (Test-Path -LiteralPath (Join-Path $ffmpegRootPath 'include/libavformat/avformat.h'))) {
        throw "FFmpeg headers were not found under $ffmpegRootPath. Install the selected FFmpeg SDK first."
    }
    $configureArguments = @('-S', '.', '-B', 'build', '-G', 'Visual Studio 17 2022',
        "-DCMAKE_GENERATOR_INSTANCE=$vsPath", "-DCMAKE_PREFIX_PATH=$projectRoot/.deps/qt",
        "-DCMAKE_SYSTEM_VERSION=$($sdk.Version)",
        "-DFFMPEG_ROOT=$ffmpegRootPath",
        "-DCMAKE_VS_GLOBALS=_LatestWindowsTargetPlatformVersion=$($sdk.Version);TargetPlatformSdkPath=$($sdk.Root);TargetPlatformDisplayName=Windows SDK")
    if (-not (Test-Path -LiteralPath 'build/CMakeCache.txt')) { $configureArguments += @('-A', 'x64') }
    if ((Invoke-QueuePilotNative $cmakePath $configureArguments $sdkEnvironment) -ne 0) { throw 'CMake configuration failed.' }
    $buildArguments = @('--build', 'build', '--config', $Configuration, '--parallel', '4', '--', '/nr:false',
        "/p:_LatestWindowsTargetPlatformVersion=$($sdk.Version)", "/p:TargetPlatformSdkPath=$($sdk.Root)",
        '/p:TargetPlatformDisplayName=Windows SDK')
    if ((Invoke-QueuePilotNative $cmakePath $buildArguments $sdkEnvironment) -ne 0) { throw 'Compilation failed.' }
    if (-not $SkipTests) {
        $ctest = Find-QueuePilotNativeTool 'ctest.exe' (Join-Path (Split-Path $cmakePath) 'ctest.exe')
        $testArguments = @('--test-dir', 'build', '-C', $Configuration, '--output-on-failure')
        if ($SkipDesktopTests) { $testArguments += @('-LE', 'desktop') }
        if ((Invoke-QueuePilotNative $ctest $testArguments) -ne 0) { throw 'Verification failed.' }
    }
} finally { Pop-Location }
