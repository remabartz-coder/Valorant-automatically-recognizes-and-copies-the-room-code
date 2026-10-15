param(
    [string]$VisualStudio,
    [string]$WindowsSdk,
    [string]$SdkVersion,
    [string]$RuntimeVersion,
    [string]$QtSdk,
    [string]$Destination,
    [string]$VisualStudioRedistList,
    [string]$WindowsSdkRedistList,
    [string]$QtSourceArchive,
    [string]$QtSourceChecksum
)
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $QtSdk) { $QtSdk = Join-Path $projectRoot '.deps/qt' }
if (-not $Destination) { $Destination = Join-Path $projectRoot '.deps/release-sources/qt' }
function Resolve-ExistingDirectory([string[]]$Candidates, [string]$Description) {
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Container)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    throw "$Description was not found. Pass an explicit path."
}
if (-not $VisualStudio) {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vswhereInstall = if (Test-Path -LiteralPath $vswherePath) {
        & $vswherePath -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    $VisualStudio = Resolve-ExistingDirectory @(
        $env:VSINSTALLDIR,
        $vswhereInstall,
        'C:/Program Files/Microsoft Visual Studio/2022/Community',
        'C:/Program Files/Microsoft Visual Studio/2022/Professional',
        'C:/Program Files/Microsoft Visual Studio/2022/Enterprise'
    ) 'Visual Studio installation'
}
if (-not $WindowsSdk) {
    $registrySdkRoot = (Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
    $WindowsSdk = Resolve-ExistingDirectory @($env:WindowsSdkDir, $registrySdkRoot, 'C:/Program Files (x86)/Windows Kits/10') 'Windows SDK installation'
}
$qtRoot = [System.IO.Path]::GetFullPath($QtSdk)
$outputRoot = [System.IO.Path]::GetFullPath($Destination)
$qtSourceRoot = Join-Path $projectRoot '.deps/release-sources/qt'
if (-not $QtSourceArchive) { $QtSourceArchive = Join-Path $qtSourceRoot 'qtbase-everywhere-src-6.8.3.tar.xz' }
if (-not $QtSourceChecksum) { $QtSourceChecksum = Join-Path $qtSourceRoot 'qtbase-everywhere-src-6.8.3.tar.xz.sha256' }
$evidenceRoot = Join-Path $outputRoot 'queuepilot-qt-sdk-evidence'
$sbomPath = Join-Path $qtRoot 'sbom/qtbase-6.8.3.spdx.json'
if (-not (Test-Path -LiteralPath $sbomPath -PathType Leaf)) { throw 'Qt 6.8.3 official SDK SPDX was not found.' }
$sbom = Get-Content -LiteralPath $sbomPath -Raw | ConvertFrom-Json
$qtPackage = @($sbom.packages | Where-Object { $_.name -eq 'qtbase' })
if ($qtPackage.Count -ne 1 -or $qtPackage[0].downloadLocation -notmatch 'c07c2d5a527a644d36e7853d55132ae38921682f$') {
    throw 'The installed Qt SDK does not identify the expected Qt Base source commit.'
}
if (Test-Path -LiteralPath $evidenceRoot) {
    throw 'Evidence directory already exists. Select a new -Destination for a fresh inventory.'
}
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
Copy-Item -LiteralPath $sbomPath -Destination (Join-Path $evidenceRoot 'qtbase-6.8.3.spdx.json')
$qtPackage[0].comment | Set-Content -LiteralPath (Join-Path $evidenceRoot 'original-qtbase-build-record.txt') -Encoding UTF8
$sourceRecord = [ordered]@{
    archive = [System.IO.Path]::GetFileName($QtSourceArchive)
    archiveUrl = 'https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz'
    checksumFile = [System.IO.Path]::GetFileName($QtSourceChecksum)
    checksumUrl = 'https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz.sha256'
    expectedSha256 = $null
    actualSha256 = $null
    hashMatches = $false
    topLevelTag = $null
    topLevelCmakeVersion = $null
    archiveEntries = $null
    thirdPartyEntries = $null
    sourceFileEntries = $null
}
if (-not (Test-Path -LiteralPath $QtSourceArchive -PathType Leaf)) { throw "Qt source archive was not found: $QtSourceArchive" }
if (-not (Test-Path -LiteralPath $QtSourceChecksum -PathType Leaf)) { throw "Qt source checksum was not found: $QtSourceChecksum" }
$checksumLine = Get-Content -LiteralPath $QtSourceChecksum | Where-Object { $_ -match 'qtbase-everywhere-src-6\.8\.3\.tar\.xz' } | Select-Object -First 1
if (-not $checksumLine -or $checksumLine -notmatch '^\s*([0-9a-fA-F]{64})\s+') { throw 'Qt source checksum file has no usable SHA-256 entry.' }
$sourceRecord.expectedSha256 = $Matches[1].ToUpperInvariant()
$sourceRecord.actualSha256 = (Get-FileHash -LiteralPath $QtSourceArchive -Algorithm SHA256).Hash
$sourceRecord.hashMatches = $sourceRecord.expectedSha256 -eq $sourceRecord.actualSha256
if (-not $sourceRecord.hashMatches) { throw "Qt source archive hash mismatch: expected $($sourceRecord.expectedSha256), got $($sourceRecord.actualSha256)" }
$archiveListing = @(tar -tf $QtSourceArchive)
if ($LASTEXITCODE -ne 0 -or $archiveListing.Count -lt 1000) { throw 'Qt source archive listing failed or is unexpectedly incomplete.' }
$sourceRecord.archiveEntries = $archiveListing.Count
$sourceRecord.thirdPartyEntries = @($archiveListing | Where-Object { $_ -like 'qtbase-everywhere-src-6.8.3/src/3rdparty/*' }).Count
$sourceRecord.sourceFileEntries = @($archiveListing | Where-Object { $_ -match 'qtbase-everywhere-src-6.8.3/src/3rdparty/.*\.(c|cc|cpp|h|hpp)$' }).Count
$tagEntry = $archiveListing | Where-Object { $_ -eq 'qtbase-everywhere-src-6.8.3/.tag' }
$cmakeEntry = $archiveListing | Where-Object { $_ -eq 'qtbase-everywhere-src-6.8.3/.cmake.conf' }
if (-not $tagEntry -or -not $cmakeEntry) { throw 'Qt source archive is missing .tag or .cmake.conf.' }
$sourceRecord.topLevelTag = ((tar -xOf $QtSourceArchive 'qtbase-everywhere-src-6.8.3/.tag') -join '').Trim()
$sourceRecord.topLevelCmakeVersion = ((tar -xOf $QtSourceArchive 'qtbase-everywhere-src-6.8.3/.cmake.conf') -join '') -match 'QT_REPO_MODULE_VERSION\s+"6\.8\.3"'
if ($sourceRecord.topLevelTag -ne 'c07c2d5a527a644d36e7853d55132ae38921682f' -or -not $sourceRecord.topLevelCmakeVersion) {
    throw 'Qt source archive version or commit does not match the installed SDK.'
}
foreach ($requiredEntry in @('configure.bat', 'configure', 'CMakeLists.txt', 'LICENSES/LGPL-3.0-only.txt', 'LICENSES/GPL-3.0-only.txt',
        'src/3rdparty/freetype/qt_attribution.json', 'src/3rdparty/harfbuzz-ng/qt_attribution.json',
        'src/3rdparty/libpng/qt_attribution.json', 'src/3rdparty/libjpeg/qt_attribution.json',
        'src/3rdparty/pcre2/qt_attribution.json', 'src/3rdparty/zlib/qt_attribution.json')) {
    if ($archiveListing -notcontains ('qtbase-everywhere-src-6.8.3/' + $requiredEntry)) {
        throw "Qt source archive is missing a required build/license/third-party input: $requiredEntry"
    }
}
$sourceRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'qt-source-archive-verification.json') -Encoding UTF8
Copy-Item -LiteralPath $QtSourceChecksum -Destination (Join-Path $evidenceRoot 'qtbase-everywhere-src-6.8.3.tar.xz.sha256')
foreach ($relativeDirectory in @('lib/cmake', 'mkspecs')) {
    $targetParent = Join-Path $evidenceRoot (Split-Path $relativeDirectory -Parent)
    New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $qtRoot $relativeDirectory) -Destination $targetParent -Recurse
}
$configurationFiles = @(Get-ChildItem -LiteralPath (Join-Path $qtRoot 'include') -Recurse -File -Filter '*config*.h')
$configurationFiles += @(Get-ChildItem -LiteralPath (Join-Path $qtRoot 'lib') -File -Filter '*.prl')
foreach ($inputFile in $configurationFiles) {
    $relativePath = $inputFile.FullName.Substring($qtRoot.Length).TrimStart('\', '/')
    $targetPath = Join-Path $evidenceRoot $relativePath
    New-Item -ItemType Directory -Path (Split-Path $targetPath -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $inputFile.FullName -Destination $targetPath
}
$msRoot = Join-Path $evidenceRoot 'microsoft'
New-Item -ItemType Directory -Path $msRoot -Force | Out-Null
if (-not $SdkVersion) {
    $SdkVersion = (Get-ChildItem -LiteralPath (Join-Path $WindowsSdk 'Include') -Directory |
        Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
}
$sdkLicense = Join-Path $WindowsSdk "Licenses/$SdkVersion/sdk_license.rtf"
if (-not $RuntimeVersion) {
    $RuntimeVersion = (Get-ChildItem -LiteralPath (Join-Path $VisualStudio 'VC/Redist/MSVC') -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'x64/Microsoft.VC143.CRT')) } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
}
$vsRedist = Get-ChildItem -LiteralPath (Join-Path $VisualStudio 'Licenses') -Filter 'Redist.txt' -Recurse -File |
    Sort-Object FullName | Select-Object -First 1 -ExpandProperty FullName
if (-not $vsRedist) { throw 'The installed Visual Studio redistribution notice was not found.' }
Copy-Item -LiteralPath $sdkLicense -Destination (Join-Path $msRoot 'windows-sdk-license.rtf')
Copy-Item -LiteralPath $vsRedist -Destination (Join-Path $msRoot 'visual-studio-installed-redist-pointer.txt')
$runtimeRoot = Join-Path $VisualStudio "VC/Redist/MSVC/$RuntimeVersion/x64/Microsoft.VC143.CRT"
$microsoftFiles = @(Get-ChildItem -LiteralPath $runtimeRoot -File -Filter '*.dll' | Sort-Object Name)
$microsoftFiles += Get-Item -LiteralPath (Join-Path $WindowsSdk 'Redist/D3D/x64/d3dcompiler_47.dll')
$inventory = foreach ($inputFile in $microsoftFiles) {
    $signature = Get-AuthenticodeSignature -LiteralPath $inputFile.FullName
    if ($signature.Status -ne 'Valid') { throw "Microsoft input signature is not valid: $($inputFile.Name)" }
    [ordered]@{
        file = $inputFile.Name
        source = if ($inputFile.Name -ieq 'd3dcompiler_47.dll') { 'WindowsSDK/Redist/D3D/x64' } else { "VisualStudio/VC/Redist/MSVC/$RuntimeVersion/x64/Microsoft.VC143.CRT" }
        fileVersion = $inputFile.VersionInfo.FileVersion
        bytes = $inputFile.Length
        sha256 = (Get-FileHash -LiteralPath $inputFile.FullName -Algorithm SHA256).Hash
        authenticodeStatus = [string]$signature.Status
        signer = $signature.SignerCertificate.Subject
    }
}
$inventory | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $msRoot 'redist-inputs.json') -Encoding UTF8
if (-not $VisualStudioRedistList) {
    $VisualStudioRedistList = Join-Path $projectRoot '.deps/release-sources/microsoft-vs2022-redist.txt'
}
if (-not $WindowsSdkRedistList) {
    $WindowsSdkRedistList = Join-Path $projectRoot '.deps/release-sources/microsoft-sdk-redist.txt'
}
$officialLists = @(
    [ordered]@{
        name = 'visual-studio-2022-redist.html'
        path = $VisualStudioRedistList
        canonicalUrl = 'https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution'
    },
    [ordered]@{
        name = 'windows-sdk-redist.html'
        path = $WindowsSdkRedistList
        canonicalUrl = 'https://learn.microsoft.com/en-us/legal/windows-sdk/redist'
    }
)
$officialListMetadata = foreach ($officialList in $officialLists) {
    if (-not (Test-Path -LiteralPath $officialList.path -PathType Leaf)) {
        throw "Official Microsoft redistribution list was not found: $($officialList.path)"
    }
    $sourceFile = Get-Item -LiteralPath $officialList.path
    Copy-Item -LiteralPath $sourceFile.FullName -Destination (Join-Path $msRoot $officialList.name)
    [ordered]@{
        name = $officialList.name
        canonicalUrl = $officialList.canonicalUrl
        sourceFile = $sourceFile.Name
        bytes = $sourceFile.Length
        sha256 = (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash
        capturedUtc = (Get-Date).ToUniversalTime().ToString('o')
        contentType = 'text/html'
    }
}
$officialListMetadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $msRoot 'official-redist-sources.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md'), (Join-Path $PSScriptRoot 'MICROSOFT.md') -Destination $evidenceRoot
$fileInventory = foreach ($inputFile in (Get-ChildItem -LiteralPath $evidenceRoot -Recurse -File | Sort-Object FullName)) {
    [ordered]@{
        path = $inputFile.FullName.Substring($evidenceRoot.Length).TrimStart('\', '/').Replace('\', '/')
        bytes = $inputFile.Length
        sha256 = (Get-FileHash -LiteralPath $inputFile.FullName -Algorithm SHA256).Hash
    }
}
$fileInventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'evidence-manifest.json') -Encoding UTF8
$zipPath = Join-Path $outputRoot 'queuepilot-qt-sdk-evidence.zip'
if (Test-Path -LiteralPath $zipPath) { throw 'Evidence ZIP already exists; select a fresh destination.' }
Compress-Archive -LiteralPath $evidenceRoot -DestinationPath $zipPath
Write-Host "Collected SDK build and Microsoft input evidence: $zipPath"
Write-Host 'This archive does not contain the Qt source archive; Microsoft redistribution HTML and source hashes are included when available.'
Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
