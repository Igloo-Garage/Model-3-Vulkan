# Reports where the game window actually is, for the screen-capture harness.
param([string]$Rom = "E:/EMU/model-3-romset/daytona2.zip")

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$sig = @'
using System;
using System.Runtime.InteropServices;
public static class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
'@
if (-not ("Win" -as [type])) { Add-Type -TypeDefinition $sig }

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build-cmake\bin\Model3-Vulkan.exe"
$p = Start-Process -FilePath $exe -ArgumentList $Rom -WorkingDirectory (Join-Path $root "build-cmake\bin") -PassThru
Start-Sleep -Seconds 8
$p.Refresh()

foreach ($s in [System.Windows.Forms.Screen]::AllScreens) {
    Write-Output ("screen: primary=" + $s.Primary + " bounds=" + $s.Bounds)
}
Write-Output ("virtual: " + [System.Windows.Forms.SystemInformation]::VirtualScreen)

$h = [IntPtr]$p.MainWindowHandle.ToInt64()
Write-Output ("hwnd: " + $h.ToInt64() + "  visible: " + [Win]::IsWindowVisible($h) + "  foreground: " + [Win]::GetForegroundWindow().ToInt64())
$r = New-Object Win+RECT
if ([Win]::GetWindowRect($h, [ref]$r)) {
    Write-Output ("rect: " + $r.L + "," + $r.T + " - " + $r.R + "," + $r.B)
}

try { [Microsoft.VisualBasic.Interaction]::AppActivate($p.Id) | Out-Null; Write-Output "AppActivate ok" }
catch { Write-Output ("AppActivate failed: " + $_.Exception.Message) }

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
