# Captures the game window repeatedly and keeps frames with a large near-white
# area, for tracking a one-frame white-flash artifact.
#
# Screen capture rather than the game's own Alt+S: it needs no window focus and
# photographs what is actually displayed. The process is made DPI-aware so the
# window rect and CopyFromScreen agree on pixels (the desktop runs at 150%).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/capture-flash.ps1 `
#       -Rom E:/EMU/model-3-romset/daytona2.zip -State Data/Saves/daytona2.zst6 -Seconds 20
param(
    [string]$Rom = "E:/EMU/model-3-romset/daytona2.zip",
    [string]$State = "",
    [int]$Seconds = 20,
    [int]$MaxShots = 30,
    [double]$WhiteThreshold = 0.25,
    [switch]$Drive,
    [string]$ExeName = "Model3-Vulkan.exe",
    [int]$WaitSeconds = 7,
    [string]$Out = "sdl3_reports/images/flash"
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$sig = @'
using System;
using System.Runtime.InteropServices;
public static class Cap {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(long h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    public const uint KEYUP = 0x0002;
    public static void Down(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); }
    public static void Up(byte vk)   { keybd_event(vk, 0, KEYUP, UIntPtr.Zero); }
}
'@
if (-not ("Cap" -as [type])) { Add-Type -TypeDefinition $sig }
[void][Cap]::SetProcessDPIAware()

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root ("build-cmake\bin\" + $ExeName)
$outDir = Join-Path $root $Out
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
Get-ChildItem $outDir -Filter *.png -ErrorAction SilentlyContinue | Remove-Item -Force

$argList = @()
if ($State -ne "") { $argList += ("-load-state=" + $State) }
$argList += $Rom
$p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory (Join-Path $root "build-cmake\bin") -PassThru

Start-Sleep -Seconds $WaitSeconds
$p.Refresh()
$hwnd = [IntPtr]$p.MainWindowHandle.ToInt64()
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "no window"; Stop-Process -Id $p.Id -Force; exit 1 }
[void][Cap]::SetForegroundWindow($hwnd.ToInt64())
Start-Sleep -Milliseconds 800

$r = New-Object Cap+RECT
if (-not [Cap]::GetWindowRect($hwnd, [ref]$r)) { Write-Output "no rect"; Stop-Process -Id $p.Id -Force; exit 1 }
$wx = $r.L; $wy = $r.T
$ww = $r.R - $r.L; $wh = $r.B - $r.T
Write-Output ("window " + $ww + "x" + $wh + " at " + $wx + "," + $wy)

$bmp = New-Object System.Drawing.Bitmap($ww, $wh)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$rect = New-Object System.Drawing.Rectangle(0, 0, $ww, $wh)
$buf = New-Object byte[] ($ww * $wh * 4)

$deadline = (Get-Date).AddSeconds($Seconds)
$shots = 0
$frames = 0
$peak = 0.0
$keptFirst = $false
$upHeld = $false

while ((Get-Date) -lt $deadline) {
    if ($Drive) {
        # The save sits on the start line; without throttle the scene never moves.
        if (-not $upHeld) { [Cap]::Down(0x26); $upHeld = $true }
        else { [Cap]::Up(0x26); [Cap]::Down(0x26) }
    }

    $gfx.CopyFromScreen($wx, $wy, 0, 0, (New-Object System.Drawing.Size($ww, $wh)))
    $frames++

    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
    $stride = $data.Stride
    $bmp.UnlockBits($data)

    # The artifact is on the track surface. Sample the lower-left half only
    # (the road in most corners) and measure mean luminance, so a white wall on
    # the right cannot trigger it.
    $sum = 0.0
    $total = 0
    $y0 = [int]($wh * 0.55)
    $y1 = [int]($wh * 0.97)
    $x0 = [int]($ww * 0.04)
    $x1 = [int]($ww * 0.55)
    for ($y = $y0; $y -lt $y1; $y += 6) {
        $row = $y * $stride
        for ($x = $x0; $x -lt $x1; $x += 6) {
            $i = $row + $x * 4
            $total++
            $sum += 0.114 * $buf[$i + 2] + 0.587 * $buf[$i + 1] + 0.299 * $buf[$i]
        }
    }
    $ratio = if ($total -gt 0) { $sum / $total } else { 0.0 }
    if ($ratio -gt $peak) { $peak = $ratio }

    if ((-not $keptFirst) -or ($ratio -ge $WhiteThreshold)) {
        $name = Join-Path $outDir ("frame_{0:D4}_{1:F3}.png" -f $frames, $ratio)
        $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png)
        $shots++
        $keptFirst = $true

        # Keep the whitest frames seen, not the first ones: the artifact is a spike
        # and a burst of ordinary frames would otherwise push it out of the set.
        if ($shots -gt $MaxShots) {
            $worst = Get-ChildItem $outDir -Filter *.png |
                Sort-Object { [double]($_.BaseName.Split('_')[2]) } |
                Select-Object -First 1
            if ($worst) { Remove-Item $worst.FullName -Force; $shots-- }
        }
    }
}

if ($upHeld) { [Cap]::Up(0x26) }

$gfx.Dispose()
$bmp.Dispose()
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Write-Output ("frames sampled: " + $frames + "  kept: " + $shots + "  peak white ratio: " + [math]::Round($peak, 3))
