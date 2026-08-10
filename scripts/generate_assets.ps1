<#
.SYNOPSIS
    generate_assets.ps1 - Render the Microsoft Store/MSIX logo set as PNGs from
    the app icon.

.DESCRIPTION
    Produces the tile logos required by a Windows.Store MSIX package
    (see packaging\AppxManifest.xml.in) into a target Assets folder. The files
    are named exactly as referenced by the manifest so makepri can index them
    without scale qualifiers.

.PARAMETER Source
    Source icon to render from (default: app\graph_studio\resources\icons\app_icon.ico).

.PARAMETER OutDir
    Output folder (default: app\graph_studio\packaging\Assets).

.EXAMPLE
    scripts\generate_assets.ps1
    scripts\generate_assets.ps1 -Source C:\icons\app.png -OutDir build\assets

.NOTES
    Requires the System.Drawing assembly (available in Windows PowerShell 5.1).
#>
[CmdletBinding()]
param(
    [string]$Source = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
if (-not $Source) {
    $Source = Join-Path $RootDir "app\graph_studio\resources\icons\app_icon.ico"
}
if (-not $OutDir) {
    $OutDir = Join-Path $RootDir "app\graph_studio\packaging\Assets"
}

if (-not (Test-Path $Source)) {
    Write-Host "==> Source icon not found: $Source" -ForegroundColor Red
    exit 1
}

Add-Type -AssemblyName System.Drawing

# name -> target size (single int for squares, array for rectangles)
$Targets = [ordered]@{
    "StoreLogo.png"           = 50
    "Square44x44Logo.png"     = 44
    "Square71x71Logo.png"     = 71
    "Square150x150Logo.png"   = 150
    "Square310x310Logo.png"   = 310
    "Wide310x150Logo.png"     = @(310, 150)
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Load the source icon once; prefer the largest embedded frame.
$icon = New-Object System.Drawing.Icon $Source
$maxFrame = 0
foreach ($frame in $icon.Frames) {
    if ($frame.Width -gt $maxFrame) { $maxFrame = $frame.Width; $frame.SelectActiveFrame([System.Drawing.Imaging.FrameDimension]::Page, 0) }
}
$sourceBitmap = $icon.ToBitmap()

function Render-Logo {
    param(
        [System.Drawing.Bitmap]$Src,
        [int]$Width,
        [int]$Height,
        [string]$Path
    )
    $bmp = New-Object System.Drawing.Bitmap $Width, $Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    # The source is square; keep its aspect ratio inside the canvas, centered.
    $shortSide = [Math]::Min($Width, $Height)
    $shortSide = [Math]::Max(1, [int]($shortSide * 0.8))   # small inset like real tiles
    $offX = ($Width - $shortSide) / 2
    $offY = ($Height - $shortSide) / 2
    $g.DrawImage($Src, [float]$offX, [float]$offY, [float]$shortSide, [float]$shortSide)
    $g.Dispose()
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

foreach ($name in $Targets.Keys) {
    $size = $Targets[$name]
    if ($size -is [array]) {
        Render-Logo -Src $sourceBitmap -Width $size[0] -Height $size[1] -Path (Join-Path $OutDir $name)
    }
    else {
        Render-Logo -Src $sourceBitmap -Width ([int]$size) -Height ([int]$size) -Path (Join-Path $OutDir $name)
    }
    Write-Host "==> $name" -ForegroundColor Green
}

$sourceBitmap.Dispose()
$icon.Dispose()
Write-Host "==> Assets written to $OutDir" -ForegroundColor Green