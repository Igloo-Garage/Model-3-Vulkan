# Loads a save state and saves every frame it can grab for N seconds, as JPEGs,
# so a human can flip through them and point at the bad frame.
#
# Only the requested crop is captured from the screen: CopyFromScreen of the
# whole window is the bottleneck, and a small region gets us above the 115 Hz
# present rate, so no presented frame is missed.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/capture-burst.ps1 `
#       -Rom E:/EMU/model-3-romset/daytona2.zip -State Data/Saves/daytona2.zst6 `
#       -Seconds 5 -Crop "0,0.6,0.55,1.0" -Out sdl3_reports/images/burst
param(
    [string]$Rom = "E:/EMU/model-3-romset/daytona2.zip",
    [string]$State = "",
    [int]$Seconds = 5,
    [switch]$Drive,
    [string]$ExeName = "Model3-Vulkan.exe",
    [string]$Out = "sdl3_reports/images/burst",
    [int]$WaitSeconds = 7,
    # x0,y0,x1,y1 as fractions of the window; empty captures the whole window.
    [string]$Crop = "",
    # NAME=VALUE exported to the child process, for the SM3D_* debug hooks.
    [string]$SetEnv = ""
)

Add-Type -AssemblyName System.Drawing

$sig = @'
using System;
using System.Runtime.InteropServices;
public static class Burst {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(long h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    public const uint KEYUP = 0x0002;
    public static void Down(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); }
    public static void Up(byte vk)   { keybd_event(vk, 0, KEYUP, UIntPtr.Zero); }
}
'@
if (-not ("Burst" -as [type])) { Add-Type -TypeDefinition $sig }
[void][Burst]::SetProcessDPIAware()

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root ("build-cmake\bin\" + $ExeName)
$outDir = Join-Path $root $Out
if (Test-Path $outDir) { Get-ChildItem $outDir -Filter *.jpg -ErrorAction SilentlyContinue | Remove-Item -Force }
else { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

$argList = @()
if ($State -ne "") { $argList += ("-load-state=" + $State) }
$argList += $Rom
if ($SetEnv -ne "") {
    $kv = $SetEnv.Split('=', 2)
    [Environment]::SetEnvironmentVariable($kv[0], $kv[1], 'Process')
}
$p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory (Join-Path $root "build-cmake\bin") -PassThru

Start-Sleep -Seconds $WaitSeconds
$p.Refresh()
$hwnd = [IntPtr]$p.MainWindowHandle.ToInt64()
[void][Burst]::SetForegroundWindow($hwnd.ToInt64())
Start-Sleep -Milliseconds 600

$r = New-Object Burst+RECT
[void][Burst]::GetWindowRect($hwnd, [ref]$r)
$x = $r.L; $y = $r.T
$w = $r.R - $r.L; $h = $r.B - $r.T

# Crop first, then size the bitmap to the region actually captured.
$capX = $x; $capY = $y; $cw = $w; $ch = $h
if ($Crop -ne "") {
    $f = $Crop.Split(',')
    $cw = [int]($w * ([double]$f[2] - [double]$f[0]))
    $ch = [int]($h * ([double]$f[3] - [double]$f[1]))
    $capX = $x + [int]($w * [double]$f[0])
    $capY = $y + [int]($h * [double]$f[1])
}
Write-Output ("window " + $w + "x" + $h + "  capture " + $cw + "x" + $ch + " at " + $capX + "," + $capY)

$bmp = New-Object System.Drawing.Bitmap($cw, $ch)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$size = New-Object System.Drawing.Size($cw, $ch)

$enc = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
$params = New-Object System.Drawing.Imaging.EncoderParameters(1)
$params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]82)

$upHeld = $false
if ($Drive) { [Burst]::Down(0x26); $upHeld = $true }

$deadline = (Get-Date).AddSeconds($Seconds)
$n = 0
$t0 = Get-Date
while ((Get-Date) -lt $deadline) {
    $gfx.CopyFromScreen($capX, $capY, 0, 0, $size)
    $bmp.Save((Join-Path $outDir ("f{0:D4}.jpg" -f $n)), $enc, $params)
    $n++
}

if ($upHeld) { [Burst]::Up(0x26) }
$gfx.Dispose()
$bmp.Dispose()
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }

$elapsed = ((Get-Date) - $t0).TotalSeconds
$saved = (Get-ChildItem $outDir -Filter *.jpg | Measure-Object).Count
Write-Output ("frames: " + $n + " in " + [math]::Round($elapsed, 1) + " s  (" + [math]::Round($n / $elapsed, 1) + " fps), saved " + $saved)
