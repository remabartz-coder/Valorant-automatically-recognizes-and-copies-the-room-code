param(
    [ValidateSet('Release', 'RelWithDebInfo', 'MinSizeRel')][string]$Configuration = 'Release',
    [string]$DestinationDir,
    [string]$VisualStudio,
    [switch]$ForPublicRelease
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native-tools.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($ForPublicRelease) {
    throw 'Public binary packaging is pending the corresponding FFmpeg dependency sources and notices. See docs/RELEASING.md. Local packaging is available without -ForPublicRelease.'
}
$binaryDir = Join-Path $projectRoot "build\$Configuration"
$destination = if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
    Join-Path $projectRoot 'dist\QueuePilot'
} elseif ([System.IO.Path]::IsPathRooted($DestinationDir)) {
    [System.IO.Path]::GetFullPath($DestinationDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $projectRoot $DestinationDir))
}
if (-not (Test-Path "$binaryDir\QueuePilot.exe")) { throw 'Build the application first.' }
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
Copy-Item -LiteralPath "$projectRoot\README.md", "$projectRoot\THIRD_PARTY_NOTICES.md", "$projectRoot\LICENSE" -Destination $destination -Force
Copy-Item -LiteralPath "$projectRoot\licenses" -Destination $destination -Recurse -Force
Copy-Item -LiteralPath "$projectRoot\docs", "$projectRoot\assets" -Destination $destination -Recurse -Force
Copy-Item -LiteralPath "$projectRoot\assets\queuepilot.ico" -Destination $destination -Force
Write-Host "Local package: $destination\QueuePilot.exe (read docs/RELEASING.md before public binary distribution)"
