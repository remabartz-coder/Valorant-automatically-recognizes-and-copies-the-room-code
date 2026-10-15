function Find-QueuePilotVisualStudio([string]$Installation = '') {
    if ($Installation) {
        $resolved = (Resolve-Path -LiteralPath $Installation -ErrorAction Stop).Path
        if (-not (Test-Path -LiteralPath (Join-Path $resolved 'VC/Tools/MSVC'))) {
            throw "The Visual Studio C++ tools were not found: $resolved"
        }
        return $resolved
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installationPath = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installationPath) { return $installationPath.Trim() }
    }
    throw 'Install Visual Studio 2022 C++ Desktop tools and a Windows SDK, or pass -VisualStudio.'
}

function Find-QueuePilotNativeTool([string]$Name, [string]$Executable = '') {
    if ($Executable) { return (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "$Name was not found. Install CMake 3.24 or newer and add its bin directory to PATH."
}

function Find-QueuePilotWindowsSdk([string]$Root = '') {
    if (-not $Root) {
        $Root = (Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots' `
            -ErrorAction SilentlyContinue).KitsRoot10
    }
    if (-not $Root) { $Root = $env:WindowsSdkDir }
    if (-not $Root) { throw 'A Windows 10/11 SDK was not found. Install it with Visual Studio.' }
    $Root = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path.TrimEnd('\', '/') + '/'
    $versions = Get-ChildItem -LiteralPath (Join-Path $Root 'Include') -Directory |
        Where-Object {
            $_.Name -match '^10\.0\.\d+\.\d+$' -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h')) -and
            (Test-Path -LiteralPath (Join-Path $Root "Lib/$($_.Name)/um/x64/kernel32.lib")) -and
            (Test-Path -LiteralPath (Join-Path $Root "Lib/$($_.Name)/ucrt/x64/ucrt.lib"))
        } | Sort-Object { [version]$_.Name } -Descending
    if (-not $versions) { throw "No complete x64 Windows SDK was found under $Root" }
    return @{ Root = $Root; Version = $versions[0].Name }
}

function ConvertTo-QueuePilotNativeArgument([string]$Value) {
    # ProcessStartInfo.Arguments also works in Windows PowerShell 5.1.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Invoke-QueuePilotNative([string]$Executable, [string[]]$Arguments, [hashtable]$Environment = @{}) {
    $processInfo = New-Object System.Diagnostics.ProcessStartInfo
    $processInfo.FileName = $Executable
    $processInfo.Arguments = ($Arguments | ForEach-Object { ConvertTo-QueuePilotNativeArgument $_ }) -join ' '
    $processInfo.WorkingDirectory = (Get-Location).Path
    $processInfo.UseShellExecute = $false
    # Windows environment names are case-insensitive. Rebuild the block so PATH/Path
    # duplicates inherited from a host cannot crash MSBuild's .NET child processes.
    $processInfo.EnvironmentVariables.Clear()
    foreach ($entry in (Get-ChildItem Env:)) {
        $processInfo.EnvironmentVariables[$entry.Name] = $entry.Value
    }
    $processInfo.EnvironmentVariables['MSBUILDDISABLENODEREUSE'] = '1'
    foreach ($key in $Environment.Keys) { $processInfo.EnvironmentVariables[$key] = $Environment[$key] }
    $process = [System.Diagnostics.Process]::Start($processInfo)
    try { $process.WaitForExit(); return $process.ExitCode } finally { $process.Dispose() }
}
