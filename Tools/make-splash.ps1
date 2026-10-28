<#
  BUSMASTER-NG: draws the splash screen (Sources\BUSMASTER\Application\Res\Splsh16.bmp and Splsh16_2x.bmp) with the
  version number. Called by set-version.ps1; can also be run alone.

  Usage (Windows PowerShell):
    .\Tools\make-splash.ps1 -Version 3.4.0 [-CodeName Overdrive]
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$CodeName = "Overdrive"
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$res = Join-Path (Split-Path $PSScriptRoot -Parent) "Sources\BUSMASTER\Application\Res"

function New-StepPoints([int]$y0, [int]$amp, [int[]]$bits) {
    $pts = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    $x = 0.0; $step = 38.0
    $pts.Add((New-Object System.Drawing.PointF($x, $y0)))
    foreach ($b in $bits) {
        $y = $y0 - ($b * $amp)
        $pts.Add((New-Object System.Drawing.PointF($x, $y)))
        $x += $step
        $pts.Add((New-Object System.Drawing.PointF($x, $y)))
    }
    return $pts.ToArray()
}

function Save-Splash([string]$OutBmp, [int]$Scale) {
    $W = 599; $H = 364
    $bmp = New-Object System.Drawing.Bitmap(($W * $Scale), ($H * $Scale), [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.ScaleTransform($Scale, $Scale)

    # Background: dark slate to deep teal diagonal gradient
    $rect = New-Object System.Drawing.Rectangle(0, 0, $W, $H)
    $c1 = [System.Drawing.Color]::FromArgb(14, 26, 38)
    $c2 = [System.Drawing.Color]::FromArgb(10, 78, 82)
    $bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $c1, $c2, 35.0)
    $g.FillRectangle($bg, $rect)

    # CAN bus: the two differential lines (CAN_H / CAN_L) of a bit sequence
    $bits = 1,0,1,1,0,0,1,0,1,0,0,1,1,0,1,1
    $bitsInv = $bits | ForEach-Object { 1 - $_ }
    $penH = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(150, 46, 204, 160), 3)
    $penL = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(110, 120, 200, 235), 3)
    $penH.LineJoin = 'Miter'; $penL.LineJoin = 'Miter'
    $g.DrawLines($penH, (New-StepPoints 278 26 $bits))
    $g.DrawLines($penL, (New-StepPoints 300 26 $bitsInv))

    # Accent bar
    $accent = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(46, 204, 160))
    $g.FillRectangle($accent, 40, 70, 6, 112)

    # Title, code name, version
    $fTitle = New-Object System.Drawing.Font('Segoe UI Semibold', 46, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $fSub   = New-Object System.Drawing.Font('Segoe UI Light', 26, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $fSmall = New-Object System.Drawing.Font('Segoe UI', 13, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $white  = [System.Drawing.Brushes]::White
    $mint   = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(46, 204, 160))
    $soft   = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(190, 215, 220))

    $g.DrawString('BUS', $fTitle, $white, 56, 64)
    $busW = $g.MeasureString('BUS', $fTitle, 1000, [System.Drawing.StringFormat]::GenericTypographic).Width
    $g.DrawString('MASTER', $fTitle, $mint, 56 + $busW + 2, 64)
    $masterW = $g.MeasureString('MASTER', $fTitle, 1000, [System.Drawing.StringFormat]::GenericTypographic).Width
    $g.DrawString('-NG', $fTitle, $white, 56 + $busW + $masterW + 4, 64)

    $g.DrawString($CodeName, $fSub, $soft, 58, 128)
    $g.DrawString("Ver $Version   -   by bigt92", $fSmall, $soft, 60, 168)

    # Credit line
    $g.DrawString('Community fork of BUSMASTER (RBEI-ETAS) - LGPL v3 - not affiliated with ETAS / Bosch', $fSmall, $soft, 20, 336)

    $g.Dispose()
    $bmp.Save($OutBmp, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
}

Save-Splash (Join-Path $res "Splsh16.bmp") 1
Save-Splash (Join-Path $res "Splsh16_2x.bmp") 2
Write-Host "Splash screen drawn for version $Version"
