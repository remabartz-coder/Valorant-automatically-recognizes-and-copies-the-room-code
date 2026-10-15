$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$suffix = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$destination = Join-Path $projectRoot "dist/QueuePilot-source-$suffix"
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$relativeFiles = @('CMakeLists.txt', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CONTRIBUTING.md', '.gitignore')
$extensions = @('.cpp', '.h', '.qrc', '.in', '.ps1', '.cmd', '.md', '.txt', '.json', '.yml', '.yaml', '.png', '.ico')
foreach ($directory in @('src', 'scripts', 'tests', 'licenses', 'assets', 'docs', '.github')) {
    $directoryPath = Join-Path $projectRoot $directory
    if (-not (Test-Path -LiteralPath $directoryPath)) { continue }
    $entries = @(Get-Item -LiteralPath $directoryPath) + @(Get-ChildItem -LiteralPath $directoryPath -Recurse -Force)
    foreach ($entry in $entries) {
        if ($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw "Source export refuses linked paths: $($entry.FullName)"
        }
        if (-not $entry.PSIsContainer -and $entry.Extension -in $extensions) {
            $relativeFiles += $entry.FullName.Substring($projectRoot.Length + 1)
        }
    }
}
$relativeFiles += @('models/recognizer.onnx', 'models/detector.onnx', 'models/keys.txt', 'models/README.md')
foreach ($relative in ($relativeFiles | Sort-Object -Unique)) {
    $source = Join-Path $projectRoot $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Source file is missing: $relative" }
    $target = Join-Path $destination $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
}
$manifest = foreach ($file in (Get-ChildItem -LiteralPath $destination -Recurse -File -Force | Sort-Object FullName)) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),
        $file.FullName.Substring($destination.Length + 1).Replace('\', '/')
}
$manifest | Set-Content -LiteralPath (Join-Path $destination 'SOURCE-MANIFEST.sha256') -Encoding ASCII
$archive = "$destination.zip"
# Explicit enumeration includes dotfiles such as .gitignore and .github.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($archive, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in (Get-ChildItem -LiteralPath $destination -Recurse -File -Force)) {
        $name = $file.FullName.Substring($destination.Length + 1).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $name) | Out-Null
    }
} finally { $zip.Dispose() }
Write-Host "Source directory: $destination"
Write-Host "Source archive: $archive"
