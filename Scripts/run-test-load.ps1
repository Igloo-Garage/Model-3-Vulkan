# Loads a save state and captures a burst of frames with the game's own
# screenshot key (Alt+S), for looking at an intermittent one-frame artifact.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/run-test-load.ps1 `
#       -Rom E:/EMU/model-3-romset/daytona2.zip `
#       -State "E:\...\Data\Saves\daytona2.zst6" -Seconds 20 -IntervalMs 90
param(
    [string]$Rom = "E:/EMU/model-3-romset/daytona2.zip",
    [string]$State = "",
    [int]$Seconds = 20,
    [int]$IntervalMs = 90
)

$sig = @'
using System;
using System.Runtime.InteropServices;
public static class Kb2 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    public const uint KEYUP = 0x0002;
    public static void Down(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); }
    public static void Up(byte vk)   { keybd_event(vk, 0, KEYUP, UIntPtr.Zero); }
    public static void Tap(byte vk, int ms) { Down(vk); System.Threading.Thread.Sleep(ms); Up(vk); }
}
'@
if (-not ("Kb2" -as [type])) { Add-Type -TypeDefinition $sig }

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build-cmake\bin\Model3-Vulkan.exe"
$log = Join-Path $root "build-cmake\bin\Data\Supermodel.log"
$dir = Join-Path $root "build-cmake\bin\Data\Screenshots"
if (Test-Path $log) { Remove-Item $log -Force }
if (Test-Path $dir) { Get-ChildItem $dir -Filter *.png | Remove-Item -Force }

$argList = @()
if ($State -ne "") { $argList += @("-load-state", $State) }
$argList += $Rom

$p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory (Join-Path $root "build-cmake\bin") -PassThru
Start-Sleep -Seconds 7
$p.Refresh()
$hwnd = [IntPtr]$p.MainWindowHandle.ToInt64()
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "no window"; Stop-Process -Id $p.Id -Force; exit 1 }

# Never send keys unless the game really is the foreground window: otherwise they
# land in whatever else is focused.
$focused = $false
for ($i = 0; $i -lt 8 -and -not $focused; $i++) {
    [void][Kb2]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 300
    $focused = ([Kb2]::GetForegroundWindow() -eq $hwnd)
}
if (-not $focused) {
    Write-Output "could not focus the game window; not sending keys"
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    exit 2
}
Start-Sleep -Milliseconds 700

$deadline = (Get-Date).AddSeconds($Seconds)
$count = 0
while ((Get-Date) -lt $deadline) {
    [Kb2]::Down(0x12)            # Alt
    [Kb2]::Tap(0x53, 45)         # S
    [Kb2]::Up(0x12)
    $count++
    Start-Sleep -Milliseconds $IntervalMs
}

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Milliseconds 500
Write-Output ("requests: " + $count + "  files: " + (Get-ChildItem $dir -Filter *.png | Measure-Object).Count)
