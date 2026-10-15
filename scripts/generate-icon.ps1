param([string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'assets'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

# Draw at 4x resolution, then downsample each ICO entry independently.
$master = New-Object System.Drawing.Bitmap 1024, 1024
$graphics = [System.Drawing.Graphics]::FromImage($master)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.ScaleTransform(4, 4)
$background = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#17221C'))
$foreground = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#F6F8F7'))
$accent = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml('#51D49B')), 26
$accent.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$accent.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$outline = New-Object System.Drawing.Drawing2D.GraphicsPath
$ring = New-Object System.Drawing.Drawing2D.GraphicsPath
try {
    foreach ($corner in @(@(12,12,56,56,180), @(188,12,56,56,270), @(188,188,56,56,0), @(12,188,56,56,90))) {
        $outline.AddArc($corner[0], $corner[1], $corner[2], $corner[3], $corner[4], 90)
    }
    $outline.CloseFigure()
    $graphics.FillPath($background, $outline)
    $ring.FillMode = [System.Drawing.Drawing2D.FillMode]::Alternate
    $ring.AddEllipse(56, 50, 140, 140)
    $ring.AddEllipse(83, 77, 86, 86)
    $graphics.FillPath($foreground, $ring)
    $graphics.DrawLine($accent, 148, 145, 193, 193)
    $sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
    $images = @()
    foreach ($size in $sizes) {
        $bitmap = New-Object System.Drawing.Bitmap $size, $size
        $resampler = [System.Drawing.Graphics]::FromImage($bitmap)
        $stream = New-Object System.IO.MemoryStream
        try {
            $resampler.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $resampler.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $resampler.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $resampler.DrawImage($master, (New-Object System.Drawing.Rectangle 0, 0, $size, $size))
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $images += ,$stream.ToArray()
            if ($size -eq 256) { $bitmap.Save((Join-Path $OutputDirectory 'queuepilot.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
        } finally { $stream.Dispose(); $resampler.Dispose(); $bitmap.Dispose() }
    }
    $file = [System.IO.File]::Create((Join-Path $OutputDirectory 'queuepilot.ico'))
    $writer = New-Object System.IO.BinaryWriter $file
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($index = 0; $index -lt $sizes.Count; ++$index) {
            $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
            $writer.Write([byte]$dimension)
            $writer.Write([byte]$dimension)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$images[$index].Length)
            $writer.Write([uint32]$offset)
            $offset += $images[$index].Length
        }
        foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
    } finally { $writer.Dispose() }
} finally {
    $ring.Dispose(); $outline.Dispose(); $accent.Dispose()
    $foreground.Dispose(); $background.Dispose(); $graphics.Dispose(); $master.Dispose()
}
Write-Host "Generated QueuePilot PNG and multi-resolution ICO in $OutputDirectory"
