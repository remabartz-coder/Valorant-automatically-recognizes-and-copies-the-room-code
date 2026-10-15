param(
    [string]$ModelsDirectory = '',
    [string]$Probe = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $ModelsDirectory) { $ModelsDirectory = Join-Path $projectRoot 'models' }
if (-not $Probe) { $Probe = Join-Path $projectRoot 'build/Release/ocr_probe.exe' }
if (-not (Test-Path -LiteralPath $Probe)) { throw 'Build ocr_probe before running reference checks.' }
$cases = @(
    @{ File = 'AZI973.png'; Code = 'AZI973'; Width = 155; Height = 48 },
    @{ File = 'DOB453.png'; Code = 'DOB453'; Width = 115; Height = 38 }
)
foreach ($case in $cases) {
    $fixture = Join-Path $PSScriptRoot "fixtures/$($case.File)"
    $outputDirectory = Join-Path $projectRoot "test-output/ocr-reference/regression-$($case.Code)"
    & $Probe $ModelsDirectory $fixture 0 0 $case.Width $case.Height $outputDirectory $case.Code
    if ($LASTEXITCODE) { throw "Raw OCR did not match the user-confirmed code $($case.Code)." }
}
Write-Host 'User screenshot reference checks passed (raw OCR only; confidence is printed above).'
