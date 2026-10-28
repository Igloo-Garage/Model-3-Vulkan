# Drives the settings window for scripted checks: starts it (or attaches to a running instance) and
# executes a list of actions. Exists because the settings UI is drawn with ImGui - no accessibility
# tree - so the only way to reach "two game sessions in the same process" is the real mouse path.
#
# Usage:
#   # Start, click Load in the open Manage-saves dialog, then leave the game with Esc.
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/gui-drive.ps1 `
#       -SetEnv "SM3D_SAVE_MANAGER=dayto2pe;SM3D_AUDIO_LEVEL=1" `
#       -Actions "wait:10;click:357,761;wait:14;close:Daytona;wait:4;shot:after.png" -Keep
#
#   # Then attach to the same process and start a second session.
#   powershell ... -ProcessId 1234 -Actions "dblclick:300,867;wait:14;shot:second.png"
#
# Actions (separated by ';'):
#   wait:N              sleep N seconds
#   click:x,y           left click at window-relative physical pixels
#   dblclick:x,y        double click at window-relative physical pixels
#   close:Substring     bring the window whose title contains Substring to the front and press Esc
#                       (the player's way out of a game; WM_CLOSE would post SDL_EVENT_QUIT, which the
#                       settings loop then reads and the whole program exits)
#   tab                 press Tab in the current window (opens the in-game panel, which pauses)
#   shot:path.png       capture the current window, scaled to 1400 px wide
#   kill                close the emulator process
#
# Click coordinates are relative to the emulator window's top-left corner, in physical pixels, which
# is exactly the coordinate system of the screenshots this script writes.
param(
    [string]$SetEnv = "",
    [int]$ProcessId = 0,
    [string]$Actions = "wait:8;shot:shot.png",
    [switch]$Keep,
    [switch]$FullScreen
)

$sig = @'
using System;
using System.Runtime.InteropServices;
public static class Gd {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public const uint LEFTDOWN = 0x0002, LEFTUP = 0x0004, KEYUP = 0x0002;

    public static string Title(IntPtr h) {
        var sb = new System.Text.StringBuilder(512);
        GetWindowText(h, sb, sb.Capacity);
        return sb.ToString();
    }

    // Process.MainWindowHandle is unreliable here (it is cached per Process object and comes back
    // null while the settings window is recreated after a session), so enumerate instead.
    public static IntPtr FindWindow(int pid, string match) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p != (uint)pid || !IsWindowVisible(h) || GetWindowTextLength(h) == 0) return true;
            if (match != null && Title(h).IndexOf(match, StringComparison.OrdinalIgnoreCase) < 0) return true;
            found = h; return false;
        }, IntPtr.Zero);
        return found;
    }

    public static void Click(int x, int y, bool twice) {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(150);
        int clicks = twice ? 2 : 1;
        for (int i = 0; i < clicks; i++) {
            mouse_event(LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
            System.Threading.Thread.Sleep(40);
            mouse_event(LEFTUP, 0, 0, 0, UIntPtr.Zero);
            if (twice) System.Threading.Thread.Sleep(90);
        }
    }

    public static void Esc() {
        keybd_event(0x1B, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(60);
        keybd_event(0x1B, 0, KEYUP, UIntPtr.Zero);
    }

    public static void Key(byte vk) {
        keybd_event(vk, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(60);
        keybd_event(vk, 0, KEYUP, UIntPtr.Zero);
    }
}
'@
if (-not ("Gd" -as [type])) { Add-Type -TypeDefinition $sig }

# Without this the script gets virtualised (scaled) window rects while CopyFromScreen and the mouse
# work in physical pixels, so screenshots and clicks land at 1/1.5 of the intended place.
[void][Gd]::SetProcessDPIAware()

Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "build-cmake\bin"
$exe = Join-Path $bin "Model3-Vulkan.exe"

if ($SetEnv -ne "") {
    foreach ($pair in $SetEnv.Split(';')) {
        if ($pair -eq "") { continue }
        $kv = $pair.Split('=', 2)
        [Environment]::SetEnvironmentVariable($kv[0], $kv[1], 'Process')
    }
}

if ($ProcessId -ne 0) {
    $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $proc) { Write-Output "pid $ProcessId is not running"; exit 1 }
} else {
    $proc = Start-Process -FilePath $exe -ArgumentList '-gui' -WorkingDirectory $bin -PassThru
    Write-Output ("pid {0}" -f $proc.Id)
}

function Get-EmuWindow([string]$Match) {
    if ($null -eq $proc) { return [IntPtr]::Zero }
    $proc.Refresh()
    if ($proc.HasExited) { return [IntPtr]::Zero }
    return [Gd]::FindWindow($proc.Id, $Match)
}

function Save-WindowShot([string]$Path, [IntPtr]$Hwnd) {
    if ($Hwnd -eq [IntPtr]::Zero) { Write-Output "shot: no window"; return }
    $r = New-Object Gd+RECT
    if (-not [Gd]::GetWindowRect($Hwnd, [ref]$r)) { Write-Output "shot: no window rect"; return }
    $w = $r.Right - $r.Left
    $h = $r.Bottom - $r.Top
    if ($w -le 0 -or $h -le 0) { Write-Output "shot: bad window rect"; return }

    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $g.Dispose()

    $maxWidth = 1400
    if ($w -gt $maxWidth) {
        $nh = [int]($h * $maxWidth / $w)
        $small = New-Object System.Drawing.Bitmap $maxWidth, $nh
        $g2 = [System.Drawing.Graphics]::FromImage($small)
        $g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g2.DrawImage($bmp, 0, 0, $maxWidth, $nh)
        $g2.Dispose()
        $bmp.Dispose()
        $bmp = $small
    }
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("shot {0} ({1}x{2} at {3},{4})" -f $Path, $w, $h, $r.Left, $r.Top)
}

foreach ($action in $Actions.Split(';')) {
    if ($action -eq "" ) { continue }
    $name, $arg = $action.Split(':', 2)
    switch ($name) {
        "wait" { Start-Sleep -Seconds ([int]$arg) }

        "click" {
            $hwnd = Get-EmuWindow $null
            if ($hwnd -eq [IntPtr]::Zero) { Write-Output "click: no window"; break }
            $r = New-Object Gd+RECT
            [void][Gd]::GetWindowRect($hwnd, [ref]$r)
            [void][Gd]::SetForegroundWindow($hwnd)
            Start-Sleep -Milliseconds 400
            $xy = $arg.Split(',')
            $sx = $r.Left + [int]$xy[0]
            $sy = $r.Top + [int]$xy[1]
            [Gd]::Click($sx, $sy, $false)
            Write-Output ("click {0} -> {1},{2}" -f $arg, $sx, $sy)
        }

        "dblclick" {
            $hwnd = Get-EmuWindow $null
            if ($hwnd -eq [IntPtr]::Zero) { Write-Output "dblclick: no window"; break }
            $r = New-Object Gd+RECT
            [void][Gd]::GetWindowRect($hwnd, [ref]$r)
            [void][Gd]::SetForegroundWindow($hwnd)
            Start-Sleep -Milliseconds 400
            $xy = $arg.Split(',')
            $sx = $r.Left + [int]$xy[0]
            $sy = $r.Top + [int]$xy[1]
            [Gd]::Click($sx, $sy, $true)
            Write-Output ("dblclick {0} -> {1},{2}" -f $arg, $sx, $sy)
        }

        "close" {
            $hwnd = Get-EmuWindow $arg
            if ($hwnd -eq [IntPtr]::Zero) {
                Write-Output ("close: no window matching '{0}'" -f $arg)
            } else {
                Write-Output ("close '{0}' with Esc" -f [Gd]::Title($hwnd))
                [void][Gd]::SetForegroundWindow($hwnd)
                Start-Sleep -Milliseconds 500
                [Gd]::Esc()
            }
        }

        "tab" {
            $hwnd = Get-EmuWindow $null
            if ($hwnd -eq [IntPtr]::Zero) { Write-Output "tab: no window"; break }
            [void][Gd]::SetForegroundWindow($hwnd)
            Start-Sleep -Milliseconds 400
            [Gd]::Key(0x09)
            Write-Output "tab"
        }

        "shot" { Save-WindowShot $arg (Get-EmuWindow $null) }

        "kill" {
            if ($null -ne $proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Output "killed" }
        }

        default { Write-Output ("unknown action: {0}" -f $action) }
    }
}

if (-not $Keep -and $ProcessId -eq 0) {
    if ($null -ne $proc -and -not $proc.HasExited) {
        $hwnd = Get-EmuWindow $null
        if ($hwnd -ne [IntPtr]::Zero) { [void][Gd]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
        for ($i = 0; $i -lt 40 -and -not $proc.HasExited; $i++) { Start-Sleep -Milliseconds 250 }
    }
    if ($null -ne $proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Output "forced kill" }
}
