# Renders assets/app.ico (16..256 px, PNG-compressed entries) from the same
# geometry as assets/icon.svg. Windows PowerShell 5.1 / System.Drawing.
#   powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1
param([string]$Out = (Join-Path $PSScriptRoot '..\assets\app.ico'))

Add-Type -AssemblyName System.Drawing

function New-RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [Math]::Min($r * 2, [Math]::Min($w, $h))
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function Render([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $k = $size / 256.0
    $g.ScaleTransform($k, $k)

    $bg     = [System.Drawing.Color]::FromArgb(255, 0x14, 0x16, 0x1B)
    $fg     = [System.Drawing.Color]::FromArgb(255, 0xE8, 0xEA, 0xED)
    $accent = [System.Drawing.Color]::FromArgb(255, 0xFF, 0x5A, 0x5F)
    $bgBrush = New-Object System.Drawing.SolidBrush $bg
    $acBrush = New-Object System.Drawing.SolidBrush $accent

    # Small sizes: thicker strokes so the TV stays legible at 16 px.
    $stroke = if ($size -le 24) { 18 } elseif ($size -le 48) { 14 } else { 12 }
    $pen = New-Object System.Drawing.Pen $fg, $stroke
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'

    $g.FillPath($bgBrush, (New-RoundRect 0 0 256 256 56))
    $g.DrawPath($pen, (New-RoundRect 42 58 152 100 12))
    $g.DrawLine($pen, 118, 170, 118, 190)
    $g.DrawLine($pen, 92, 196, 144, 196)
    $g.FillPath($bgBrush, (New-RoundRect 158 94 72 132 36))
    $g.FillPath($acBrush, (New-RoundRect 168 104 52 112 26))
    $g.FillEllipse($bgBrush, 183, 127, 22, 22)
    if ($size -gt 24) { $g.FillEllipse($bgBrush, 188, 176, 12, 12) }

    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    if ($size -ge 256) {
        # 256 px: PNG-compressed entry (what Windows expects for large icons).
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    } else {
        # Smaller sizes: classic 32-bit DIB (BGRA bottom-up + empty AND mask),
        # readable by every Windows component.
        $bw = New-Object System.IO.BinaryWriter $ms
        $bw.Write([UInt32]40); $bw.Write([Int32]$size); $bw.Write([Int32]($size * 2))
        $bw.Write([UInt16]1); $bw.Write([UInt16]32); $bw.Write([UInt32]0)
        $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
        $bw.Write([UInt32]($size * $size * 4 + $maskStride * $size))
        $bw.Write([Int32]0); $bw.Write([Int32]0); $bw.Write([UInt32]0); $bw.Write([UInt32]0)
        for ($y = $size - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $size; $x++) {
                $c = $bmp.GetPixel($x, $y)
                $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A)
            }
        }
        $bw.Write((New-Object byte[] ($maskStride * $size)))
        $bw.Flush()
    }
    $bmp.Dispose()
    return ,$ms.ToArray()
}

$sizes = 16, 24, 32, 48, 64, 128, 256
$images = foreach ($s in $sizes) { ,(Render $s) }

$fs = [System.IO.File]::Create((Resolve-Path -LiteralPath (Split-Path $Out) | Join-Path -ChildPath (Split-Path $Out -Leaf)))
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $len = $images[$i].Length
    $w.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $w.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([UInt16]1); $w.Write([UInt16]32)
    $w.Write([UInt32]$len); $w.Write([UInt32]$offset)
    $offset += $len
}
foreach ($img in $images) { $w.Write($img) }
$w.Close()
Write-Host "Wrote $Out"
