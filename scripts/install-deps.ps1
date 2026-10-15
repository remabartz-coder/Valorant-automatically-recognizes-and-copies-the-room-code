param(
    [switch]$SkipQt,
    [switch]$SkipFFmpeg,
    [string]$VisualStudio,
    [string]$FFmpegArchive,
    [string]$FFmpegArchiveUrl
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native-tools.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$downloadRoot = Join-Path $projectRoot '.deps\downloads'
New-Item -ItemType Directory -Force $downloadRoot | Out-Null

function Fetch([string]$Url, [string]$Name, [string]$Sha256) {
    $target = Join-Path $downloadRoot $Name
    if ((Test-Path $target) -and (Get-FileHash $target -Algorithm SHA256).Hash -eq $Sha256) { return $target }
    Write-Host "Downloading $Name"
    & curl.exe -L --fail --proto '=https' --proto-redir '=https' --retry 2 --connect-timeout 15 --max-time 900 -o $target $Url
    if ($LASTEXITCODE) { throw "Download failed: $Name" }
    if ((Get-FileHash $target -Algorithm SHA256).Hash -ne $Sha256) { throw "Integrity check failed: $Name. The upstream release may have changed." }
    return $target
}
function Extract([string]$Archive, [string]$Destination) {
    New-Item -ItemType Directory -Force $Destination | Out-Null
    & tar.exe -xf $Archive -C $Destination
    if ($LASTEXITCODE) { throw "Extraction failed: $Archive" }
}

if (-not $SkipQt) {
    $qtArchive = Fetch 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.win64_msvc2022_64/6.8.3-0-202503201308qtbase-Windows-Windows_11_23H2-MSVC2022-Windows-Windows_11_23H2-X86_64.7z' 'qtbase.7z' '41688269FAC0565DB956C66D9EECAE777D16197E0C02CD81B88640C1F5D73D3F'
    Extract $qtArchive (Join-Path $projectRoot '.deps\qt')
}
if (-not $SkipFFmpeg) {
    $ffHash = '19D92DFB1481E72E4E8F94844BF609418297A646F33DEB6864C409CA752469C4'
    if ($FFmpegArchive) {
        $ffArchive = (Resolve-Path -LiteralPath $FFmpegArchive -ErrorAction Stop).Path
        if ((Get-FileHash -LiteralPath $ffArchive -Algorithm SHA256).Hash -ne $ffHash) {
            throw 'FFmpeg archive SHA-256 does not match the pinned build. See licenses/SOURCES.md.'
        }
    } else {
        $ffUrl = if ($FFmpegArchiveUrl) { $FFmpegArchiveUrl } else {
            'https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-10-13-13-05/ffmpeg-n8.1.3-18-g46d899d7a0-win64-lgpl-shared-8.1.zip'
        }
        $ffArchive = Fetch $ffUrl 'ffmpeg-20261013.zip' $ffHash
    }
    $ffExtract = Join-Path $projectRoot ('.deps\ffmpeg-extract-' + [Guid]::NewGuid().ToString('N'))
    Extract $ffArchive $ffExtract
    $ffPackage = Get-ChildItem $ffExtract -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'include\libavformat\avformat.h') } | Select-Object -First 1
    if (-not $ffPackage) { throw 'FFmpeg package has no development headers.' }
    New-Item -ItemType Directory -Force (Join-Path $projectRoot '.deps\ffmpeg') | Out-Null
    Copy-Item -Path (Join-Path $ffPackage.FullName '*') -Destination (Join-Path $projectRoot '.deps\ffmpeg') -Recurse -Force
}
# Only the native DLL is used from Microsoft's smaller official wheel; no Python runtime is shipped.
$ortWheel = Fetch 'https://files.pythonhosted.org/packages/37/de/6cab7e39917cc87728d2f00abe97c81fe86b29f9e1f758627864c28f0c21/onnxruntime-1.31.0-cp311-cp311-win_amd64.whl' 'onnxruntime-wheel.zip' '317608967B03807ED4661113B08293FAC02A1DB6496A6863A07D9F19232936AD'
$ortExtract = Join-Path $projectRoot '.deps\ort-wheel'
Extract $ortWheel $ortExtract
$ortRoot = Join-Path $projectRoot '.deps\onnxruntime'
New-Item -ItemType Directory -Force "$ortRoot\lib", "$ortRoot\include" | Out-Null
Copy-Item -LiteralPath "$ortExtract\onnxruntime\capi\onnxruntime.dll" -Destination "$ortRoot\lib\onnxruntime.dll" -Force
$headers = @{
    'onnxruntime_c_api.h' = '43D584F596EF66820421317DF32A79A95FC1D39EE46CB3971A870C08D8BA4BAA'
    'onnxruntime_cxx_api.h' = '1E80D494C47091B07D84DE60AD570326F82ED841C6DE66E5B3EB7DCE17BFC467'
    'onnxruntime_cxx_inline.h' = '908A50414C3F67F32FD556CD16F7779FA78380E12E9D08E44F5E6F66B965A7BE'
    'onnxruntime_float16.h' = '88B242845D25981633A0BBD1C148E273CF8BFB016EA3F57C4AF41A06530F72B0'
    'onnxruntime_error_code.h' = '5CE3B054E798ECED8D14F5B86E98692FD33470463F96194CE0700A2D53DD8721'
    'onnxruntime_ep_c_api.h' = 'B64036772115DBC6F06A900487CE22D34E862100DB03E2AD62210758E27A19B2'
}
foreach ($name in $headers.Keys) {
    $header = Fetch "https://raw.githubusercontent.com/microsoft/onnxruntime/v1.31.0/include/onnxruntime/core/session/$name" $name $headers[$name]
    Copy-Item -LiteralPath $header -Destination "$ortRoot\include\$name" -Force
}
$libTool = Get-Command lib.exe -ErrorAction SilentlyContinue
if ($libTool) { $libPath = $libTool.Source }
else {
    $vsPath = Find-QueuePilotVisualStudio $VisualStudio
    $libPath = Get-ChildItem "$vsPath\VC\Tools\MSVC\*\bin\Hostx64\x64\lib.exe" | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $libPath) { throw 'MSVC lib.exe is required to generate the ONNX import library.' }
& $libPath "/def:$PSScriptRoot\onnxruntime.def" /machine:x64 "/out:$ortRoot\lib\onnxruntime.lib"
if ($LASTEXITCODE) { throw 'ONNX import library creation failed.' }
$modelDir = Join-Path $projectRoot 'models'
New-Item -ItemType Directory -Force $modelDir | Out-Null
$model = Fetch 'https://huggingface.co/SWHL/RapidOCR/resolve/1cfba2e90fc938db55889873735088de210cc173/PP-OCRv3/en_PP-OCRv3_rec_infer.onnx' 'latin-recognizer.onnx' 'EF7ABD8BD3629AE57EA2C28B425C1BD258A871B93FD2FE7C433946ADE9B5D9EA'
$detector = Fetch 'https://huggingface.co/SWHL/RapidOCR/resolve/1cfba2e90fc938db55889873735088de210cc173/PP-OCRv4/ch_PP-OCRv4_det_infer.onnx' 'detector.onnx' 'D2A7720D45A54257208B1E13E36A8479894CB74155A5EFE29462512D42F49DA9'
$keys = Fetch 'https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/8cce9b6fd7ccb50226d0c38f94054d81c29b8184/ppocr/utils/en_dict.txt' 'latin-keys.txt' '5662DF9D2D03F0E8CA0D3B0649D6ACBAB904B6A14B3D3521463C71C37C668CE3'
Copy-Item -LiteralPath $model -Destination "$modelDir\recognizer.onnx" -Force
Copy-Item -LiteralPath $detector -Destination "$modelDir\detector.onnx" -Force
Copy-Item -LiteralPath $keys -Destination "$modelDir\keys.txt" -Force
Write-Host 'Native dependencies ready. Run scripts/build.ps1.'
