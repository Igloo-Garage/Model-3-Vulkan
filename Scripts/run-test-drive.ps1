# Drives a Model 3 game with synthetic keyboard input so the 3D track is reached,
# then optionally captures frames with the game's own screenshot key (Alt+S).
#
# Coin = 3, Start = 1, Accelerate = Up arrow, Steer = Left/Right (see InputAccelerator
# and InputStart1 in Model3-Vulkan.ini).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/run-test-drive.ps1 `
#       -Rom E:/EMU/model-3-romset/daytona2.zip -Seconds 60 -Shots 6
param(
    [string]$Rom = "E:/EMU/model-3-romset/daytona2.zip",
    [int]$Seconds = 60,
    [int]$Shots = 0,
    [int]$CoinCount = 3
)

Add-Type -AssemblyName System.Windows.Forms

$sig = @'
using System;
using System.Runtime.InteropServices;
public static class Kb {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    public const uint KEYUP = 0x0002;
    public static void Down(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); }
    public static void Up(byte vk)   { keybd_event(vk, 0, KEYUP, UIntPtr.Zero); }
    public static void Tap(byte vk, int ms) { Down(vk); System.Threading.Thread.Sleep(ms); Up(vk); }
}
'@
if (-not ("Kb" -as [type])) { Add-Type -TypeDefinition $sig }

$VK = @{ '1' = 0x31; '3' = 0x33; 'UP' = 0x26; 'DOWN' = 0x28; 'LEFT' = 0x25; 'RIGHT' = 0x27; 'ALT' = 0x12; 'S' = 0x53 }

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build-cmake\bin\Model3-Vulkan.exe"
$log = Join-Path $root "build-cmake\bin\Data\Supermodel.log"
$dir = Join-Path $root "build-cmake\bin\Data\Screenshots"
if (Test-Path $log) { Remove-Item $log -Force }
if (Test-Path $dir) { Get-ChildItem $dir -Filter *.png | Remove-Item -Force }

$p = Start-Process -FilePath $exe -ArgumentList $Rom -WorkingDirectory (Join-Path $root "build-cmake\bin") -PassThru
$deadline = (Get-Date).AddSeconds($Seconds)
$upHeld = $false
$shotTimes = @()
if ($Shots -gt 0) {
    for ($i = 1; $i -le $Shots; $i++) { $shotTimes += 8 + ($i * [math]::Floor(($Seconds - 10) / $Shots)) }
}

Start-Sleep -Seconds 7
$p.Refresh()
if ($p.MainWindowHandle -eq 0) { Write-Output "no window"; Stop-Process -Id $p.Id -Force; exit 1 }
[void][Kb]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 800

# Credits, then start. Repeated because the attract loop ignores input at first.
for ($i = 0; $i -lt $CoinCount; $i++) { [Kb]::Tap($VK['3'], 90); Start-Sleep -Milliseconds 250 }
Start-Sleep -Milliseconds 600
[Kb]::Tap($VK['1'], 120); Start-Sleep -Milliseconds 1200
[Kb]::Tap($VK['1'], 120); Start-Sleep -Milliseconds 1200
[Kb]::Tap($VK['1'], 120)

$start = Get-Date
while ((Get-Date) -lt $deadline) {
    $elapsed = ((Get-Date) - $start).TotalSeconds

    # Hold the accelerator by re-pressing it: SDL only sees the state its own events imply.
    if (-not $upHeld) { [Kb]::Down($VK['UP']); $upHeld = $true }
    else { [Kb]::Up($VK['UP']); Start-Sleep -Milliseconds 30; [Kb]::Down($VK['UP']) }

    # Weave a little so the car does not sit against a wall for the whole run.
    if (($elapsed % 12) -lt 1) { [Kb]::Tap($VK['LEFT'], 200) }
    elseif (($elapsed % 12) -gt 6 -and ($elapsed % 12) -lt 7) { [Kb]::Tap($VK['RIGHT'], 200) }

    foreach ($t in $shotTimes) {
        if ([math]::Abs($elapsed - $t) -lt 0.6) {
            [Kb]::Down($VK['ALT']); [Kb]::Tap($VK['S'], 60); [Kb]::Up($VK['ALT'])
            Start-Sleep -Milliseconds 400
        }
    }

    Start-Sleep -Milliseconds 200
}

if ($upHeld) { [Kb]::Up($VK['UP']) }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Milliseconds 500
Write-Output "done"
