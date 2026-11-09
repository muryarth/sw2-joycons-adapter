# Renders the installer bitmaps from assets/app.ico (called by build.ps1).
#   banner.bmp 493x58   top strip of the inner pages (title text is drawn on it in black)
#   dialog.bmp 493x312  welcome/finish pages (left 164 px panel, text on the right)
param([string]$OutDir, [string]$IconPath)
Add-Type -AssemblyName System.Drawing

$icon  = (New-Object System.Drawing.Icon($IconPath, 256, 256)).ToBitmap()
$dark  = [System.Drawing.Color]::FromArgb(0x21, 0x22, 0x2B)
$light = [System.Drawing.Color]::White
$accent = [System.Drawing.Color]::FromArgb(0xFF, 0x5A, 0x5F)

function New-Canvas([int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
    return $bmp, $g
}

# Banner: plain white like the dialog body, app icon on the right.
$bmp, $g = New-Canvas 493 58
$g.Clear($light)
$g.DrawImage($icon, 493 - 50, 9, 40, 40)
$g.Dispose(); $bmp.Save((Join-Path $OutDir 'banner.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp); $bmp.Dispose()

# Welcome/finish: dark side panel with the icon and an accent line.
$bmp, $g = New-Canvas 493 312
$g.Clear($light)
$panel = New-Object System.Drawing.SolidBrush $dark
$g.FillRectangle($panel, 0, 0, 164, 312)
$g.DrawImage($icon, 34, 96, 96, 96)
$pen = New-Object System.Drawing.Pen $accent, 3
$g.DrawLine($pen, 62, 214, 102, 214)
$g.Dispose(); $bmp.Save((Join-Path $OutDir 'dialog.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp); $bmp.Dispose()
