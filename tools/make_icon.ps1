# Copyright (C) 2026 Mannai
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Draws the Shutterlink camera icon at the standard sizes and packs them into src\app\shutterlink.ico.
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot '..\src\app\shutterlink.ico'
$sizes = 16, 20, 24, 32, 40, 48, 64, 256

function New-RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

$pngs = foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    $u = $s / 32.0

    # Viewfinder hump + body
    $body = [System.Drawing.Color]::FromArgb(255, 38, 40, 46)
    $edge = [System.Drawing.Color]::FromArgb(255, 150, 155, 165)
    $brush = New-Object System.Drawing.SolidBrush $body
    $hump = New-RoundRect (11 * $u) (5 * $u) (10 * $u) (6 * $u) (1.5 * $u)
    $g.FillPath($brush, $hump)
    $rect = New-RoundRect (2 * $u) (9 * $u) (28 * $u) (18 * $u) (3.5 * $u)
    $g.FillPath($brush, $rect)
    $pen = New-Object System.Drawing.Pen $edge, ([Math]::Max(1.0, 1.1 * $u))
    $g.DrawPath($pen, $rect)

    # Lens: outer ring, glass with a blue sheen, highlight
    $lensR = 7.2 * $u; $cx = 16 * $u; $cy = 18 * $u
    $g.FillEllipse((New-Object System.Drawing.SolidBrush $edge), $cx - $lensR, $cy - $lensR, 2 * $lensR, 2 * $lensR)
    $glassR = 5.6 * $u
    $glass = New-Object System.Drawing.Drawing2D.LinearGradientBrush (
        (New-Object System.Drawing.PointF ($cx - $glassR), ($cy - $glassR)),
        (New-Object System.Drawing.PointF ($cx + $glassR), ($cy + $glassR)),
        [System.Drawing.Color]::FromArgb(255, 70, 140, 230), [System.Drawing.Color]::FromArgb(255, 10, 20, 45))
    $g.FillEllipse($glass, $cx - $glassR, $cy - $glassR, 2 * $glassR, 2 * $glassR)
    if ($s -ge 24) {
        $hl = 1.8 * $u
        $g.FillEllipse((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(200, 255, 255, 255))),
            $cx - 2.6 * $u - $hl / 2, $cy - 2.6 * $u - $hl / 2, $hl, $hl)
    }

    # Red "live" dot
    $dotR = 2.2 * $u
    $g.FillEllipse((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 230, 45, 45))),
        25 * $u - $dotR, 13 * $u - $dotR, 2 * $dotR, 2 * $dotR)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    if ($s -ge 256) {
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    } else {
        # Classic DIB entry: BITMAPINFOHEADER (double height), bottom-up BGRA, 1bpp AND mask.
        $bw = New-Object System.IO.BinaryWriter $ms
        $bw.Write([uint32]40); $bw.Write([int32]$s); $bw.Write([int32](2 * $s))
        $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]0)
        $bw.Write([uint32](4 * $s * $s)); $bw.Write([int32]0); $bw.Write([int32]0)
        $bw.Write([uint32]0); $bw.Write([uint32]0)
        for ($y = $s - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $s; $x++) {
                $c = $bmp.GetPixel($x, $y)
                $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A)
            }
        }
        $maskRow = [int]([Math]::Ceiling($s / 32.0) * 4)
        $bw.Write((New-Object byte[] ($maskRow * $s)))  # all zero: alpha channel decides
        $bw.Flush()
    }
    $bmp.Dispose()
    , $ms.ToArray()
}

# ICO container: header, directory, then PNG payloads.
$fs = [System.IO.File]::Create($out)
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
$w.Close()
"wrote $out"
