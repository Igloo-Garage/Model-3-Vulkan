# Runs one game for N seconds and closes the window politely, so Shutdown() runs and the log is
# flushed. Prints the log lines the caller asked for.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/run-log.ps1 `
#       -Rom E:/EMU/model-3-romset/daytona2.zip -State Data/Saves/daytona2.zst6 `
#       -Seconds 12 -SetEnv "SM3D_FRAME_TIMING=1"
param(
    [string]$Rom = "E:/EMU/model-3-romset/daytona2.zip",
    [string]$State = "",
    [int]$Seconds = 12,
    [switch]$Drive,
    [string]$SetEnv = "",
    [string]$Log = "",
    [string]$Exe = "Model3-Vulkan.exe",
    [switch]$Shot,
    [string]$Grep = "Interp:|PACE:|present_wait|Timing:|Sleep timing|Vulkan:|Pace:"
)

$sig = @'
using System;
using System.Runtime.InteropServices;
public static class Rl {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool attach);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    public const uint KEYUP = 0x0002;
    public static void Down(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); }
    public static void Up(byte vk)   { keybd_event(vk, 0, KEYUP, UIntPtr.Zero); }
    public static bool ForceForeground(IntPtr h) {
        uint pid; uint target = GetWindowThreadProcessId(h, out pid);
        uint self = GetCurrentThreadId();
        if (target != self) AttachThreadInput(self, target, true);
        bool ok = SetForegroundWindow(h);
        if (target != self) AttachThreadInput(self, target, false);
        return ok;
    }
}
'@
if (-not ("Rl" -as [type])) { Add-Type -TypeDefinition $sig }

$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "build-cmake\bin"
$exe = Join-Path $bin $Exe
if (-not (Test-Path $exe)) { Write-Output ("missing: " + $exe); exit 1 }
if ($Log -eq "") { $Log = Join-Path $bin "Data\Supermodel.log" }
if (Test-Path $Log) { Remove-Item $Log -Force }

if ($SetEnv -ne "") {
    $kv = $SetEnv.Split('=', 2)
    [Environment]::SetEnvironmentVariable($kv[0], $kv[1], 'Process')
}

$argList = @()
if ($State -ne "") { $argList += ('-load-state="' + $State + '"') }
$argList += ('"' + $Rom + '"')

$p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory $bin -PassThru
Start-Sleep -Seconds 8

$upHeld = $false
if ($Drive) { [Rl]::Down(0x26); $upHeld = $true }

Start-Sleep -Seconds $Seconds
if ($upHeld) { [Rl]::Up(0x26) }

if ($Shot) {
    # The screenshot has to come from the emulator itself: the game runs in exclusive fullscreen,
    # where CopyFromScreen returns a black frame. Alt+S is uiScreenshot; the file lands in
    # Data\Screenshots with a timestamp name.
    $since = Get-Date
    $p.Refresh()
    if (-not $p.HasExited) {
        $gameWindow = [IntPtr]$p.MainWindowHandle.ToInt64()
        if ($gameWindow -ne [IntPtr]::Zero) {
            $fg = [Rl]::ForceForeground($gameWindow)
            Write-Output ("foreground: " + $fg)
        }
    }
    Start-Sleep -Milliseconds 500
    [Rl]::Down(0x12)
    [Rl]::Down(0x53)
    [Rl]::Up(0x53)
    [Rl]::Up(0x12)
    Start-Sleep -Seconds 2
    $shotDir = Join-Path $bin "Data\Screenshots"
    $shotFile = Get-ChildItem $shotDir -Filter *.png -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $since } | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($shotFile) { Write-Output ("shot: " + $shotFile.FullName) } else { Write-Output "shot: none written" }
}

$p.Refresh()
if (-not $p.HasExited) {
    $hwnd = [IntPtr]$p.MainWindowHandle.ToInt64()
    if ($hwnd -ne [IntPtr]::Zero) { [void][Rl]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
}
for ($i = 0; $i -lt 40 -and -not $p.HasExited; $i++) { Start-Sleep -Milliseconds 250 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "forced kill" }

Start-Sleep -Milliseconds 400
if (-not (Test-Path $Log)) { Write-Output "no log"; exit 1 }
Select-String -Path $Log -Pattern $Grep | ForEach-Object { $_.Line }
