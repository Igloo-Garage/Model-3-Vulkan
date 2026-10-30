#Requires -Version 5.1
<#
.SYNOPSIS
    Manages the display resolution / refresh rate for Supermodel (Sega Model 3) so the
    arcade-native 57.524 Hz can run with frame rate == panel scan rate (no tearing, no
    cadence judder), restoring the previous desktop mode when the game exits.

.DESCRIPTION
    Why this exists:

    Model 3's Real3D hardware scans at 57.524160 Hz. On Windows, VRR (G-Sync/FreeSync) does not normally
    engage in windowed or borderless fullscreen because DWM keeps composing at the desktop refresh rate.
    So "borderless fullscreen + 57.524 fps" on a 60 Hz or 144 Hz desktop repeats some frames and skips
    the cadence of others (judder).

    "Normally" is load-bearing: NVIDIA offers to apply G-SYNC to windowed output as well as fullscreen
    ("Enable for windowed and full screen mode" in the control panel; the driver default is fullscreen
    only). With that selected, a borderless session can reach the same 1:1 mapping this script arranges
    by hand. Nothing can detect which is the case from inside an application - both present at the same
    rate and read the same frame rate - so the display's own live refresh readout is the only authority.

    The physically clean fix is to make the panel actually scan at 57/58 Hz, which requires exclusive
    fullscreen. Then Vulkan's FIFO present maps 1:1 frame-to-scanline.

    Prerequisite: the display must expose a 57 or 58 Hz mode. Usually this has to be created in
    the NVIDIA Control Panel first (see "Creating a custom 57.524 Hz resolution" below).
    -Action List reports whether one already exists.

.PARAMETER Action
    List     Show every resolution and its refresh rates, flagging arcade-capable ones.
    Save     Save the current display settings to arcade-display-state.json.
    Restore  Restore display settings from that JSON.
    Run      Save -> launch Supermodel -> restore when it exits.

.PARAMETER Game
    ROM zip path to launch in Run mode.

.PARAMETER Width / Height
    Target resolution. When omitted, prefers a 57/58 Hz mode at the current desktop resolution;
    otherwise picks the best available and reports what it chose.

.PARAMETER Monitor
    Which display to work on: the number Windows knows it by, which is the same number shown by
    "Identify" in Display Settings and by the "Output display" list in the emulator's settings window.
    0, the default, means whichever display this script would otherwise use.

    Worth setting on a machine with more than one monitor, and worth setting to the *same* number in
    the emulator's display setting. A 57.524 Hz custom resolution belongs to one output's mode list,
    so a mode created for one monitor and a session driven on another do not meet - and the symptom
    is not an error but a silent fall back to the desktop refresh rate.

    -Action List names every attached display with its number first, so the number to pass is the one
    printed there.

.PARAMETER Hz
    Target refresh rate, default 57.5242. The closest integer-rate mode (57 or 58) is chosen
    because SDL only reports integer refresh rates.

  .PARAMETER SupermodelExe
      Path to the emulator. When omitted, the program is looked for beside this script's parent
      folder (the distribution layout) and then under MSVC\x64\Release (a source checkout).

.PARAMETER ExtraArgs
    Extra arguments passed through to Supermodel (for example -vulkan3d).

.PARAMETER KeepMode
    In Run mode, do not restore the desktop display settings afterwards.

.EXAMPLE
    # Check whether the display has a usable 57/58 Hz mode
    .\ArcadeDisplay.ps1 -Action List

.EXAMPLE
    # Run Daytona 2 on Vulkan at the arcade rate; desktop is restored on exit
    .\ArcadeDisplay.ps1 -Action Run -Game 'E:\...\daytona2.zip' -Hz 57.5242 -ExtraArgs -vulkan3d

.EXAMPLE
    # Do the same on the second monitor, which is the one with a 57/58 Hz mode
    .\ArcadeDisplay.ps1 -Action Run -Game 'E:\...\daytona2.zip' -Monitor 5

.NOTES
    Creating a custom 57.524 Hz resolution (one-off, manual; NVIDIA exposes no public API):

      1. Right-click desktop -> NVIDIA Control Panel -> "Change resolution"
      2. "Customize..." -> tick "Enable resolutions not exposed by the display" ->
         "Create Custom Resolution"
      3. Set Horizontal pixels / Vertical lines to the target resolution (the display's native
         resolution is a good choice)
      4. Set "Refresh rate" to 57 (or 58)
      5. Choose "CVT" for Timing, save, then select it back in "Change resolution" and confirm
         the image is stable. If the display rejects the timing, retry with "CVT - Reduced
         blanking" or a lower resolution.
#>
[CmdletBinding()]
param(
    [ValidateSet('List', 'Save', 'Restore', 'Run')]
    [string]$Action = 'List',

    [string]$Game,
    [int]$Width = 0,
    [int]$Height = 0,
    [int]$Monitor = 0,
    [double]$Hz = 57.5242,
    [string]$SupermodelExe,
    [string[]]$ExtraArgs = @(),
    [switch]$KeepMode,

    # Language for the preamble this script prints itself. The two .bat launchers pass this in, and
    # it exists because of a cmd limitation rather than for tidiness: a .bat that changes the console
    # code page part-way through is re-read by cmd at a byte offset that no longer lines up, which
    # loses the 'rem' keyword on following lines and makes cmd try to execute comments. Keeping the
    # launchers pure ASCII avoids that entirely, so anything non-ASCII has to come from here - and
    # this file carries a UTF-8 BOM so PowerShell reads it correctly whatever the system code page is.
    [ValidateSet('en', 'zh')]
    [string]$Lang = 'en'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$StatePath = Join-Path $PSScriptRoot 'arcade-display-state.json'

# ---------------------------------------------------------------------------
# Localised preamble
# ---------------------------------------------------------------------------
# Printed by the script rather than by the .bat launchers, for the reason given on the -Lang
# parameter: a .bat cannot safely carry non-ASCII text. Both halves are kept here so the two
# languages stay side by side and cannot drift apart.
#
# Write-Host is used throughout rather than Write-Output, so the text reaches the console without
# joining the pipeline and being captured by a caller doing something with the results.
# ---------------------------------------------------------------------------
$Text = if ($Lang -eq 'zh') {
    @{
        Title    = 'Model 3-Vulkan - 街机刷新率显示助手'
        Intro    = @(
            '本工具用于把显示器切到 Model 3 原生的 57.524Hz，并在退出后恢复桌面。',
            '下面列出所有可用模式，标绿的是能承载 57.524Hz 的（57 或 58Hz）。',
            '若一个都没有，需要在显卡控制面板里创建 57.524Hz 自定义分辨率。'
        )
        Current  = '当前模式'
        Avail    = '可用分辨率与刷新率'
        Usable   = '可用于街机 57.524Hz'
        None     = '没有找到 57/58Hz 模式。'
        Hint     = '提示'
        HintBody = '可在「常用」页的「固定街机刷新率」选项下看到本机显示器 EDID 报告的时序范围。'
        Monitors = '已连接的显示器'
        Using    = '本次操作的显示器'
        NoSuchMonitor = '没有编号为 {0} 的显示器。'
        MonitorsHint  = '用 -Action List 可以看到各显示器的编号；该编号与「显示设置 → 标识」以及模拟器设置里的编号一致。'
    }
} else {
    @{
        Title    = 'Model 3-Vulkan - arcade display helper'
        Intro    = @(
            'Switches the display to the Model 3 native 57.524 Hz and restores the desktop afterwards.',
            'Below is every available mode; the ones marked green can carry 57.524 Hz (57 or 58 Hz).',
            'If none is listed, a custom 57.524 Hz resolution has to be created in the GPU control panel.'
        )
        Current  = 'Current mode'
        Avail    = 'Available resolutions and refresh rates'
        Usable   = 'usable for arcade 57.524 Hz'
        None     = 'No 57/58 Hz mode found.'
        Hint     = 'Hint'
        HintBody = 'The display timing range read from this machine''s EDID is shown under the fixed arcade rate option on the Common tab.'
        Monitors = 'Attached displays'
        Using    = 'Display in use'
        NoSuchMonitor = 'There is no display numbered {0}.'
        MonitorsHint  = 'Run -Action List to see the numbers; they match Display Settings > Identify and the emulator''s own display setting.'
    }
}

Write-Host ''
Write-Host ('  ' + $Text.Title) -ForegroundColor Cyan
Write-Host '  ==================================================================' -ForegroundColor Cyan
foreach ($line in $Text.Intro) { Write-Host ('  ' + $line) -ForegroundColor DarkGray }
Write-Host ''

# ---------------------------------------------------------------------------
# Win32 display settings interop
# ---------------------------------------------------------------------------
if (-not ('ArcadeDisplay.Native' -as [type]))
{
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace ArcadeDisplay
{
    public static class Native
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct DEVMODE
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
            public short dmSpecVersion;
            public short dmDriverVersion;
            public short dmSize;
            public short dmDriverExtra;
            public int   dmFields;
            public int   dmPositionX;
            public int   dmPositionY;
            public int   dmDisplayOrientation;
            public int   dmDisplayFixedOutput;
            public short dmColor;
            public short dmDuplex;
            public short dmYResolution;
            public short dmTTOption;
            public short dmCollate;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
            public short dmLogPixels;
            public int   dmBitsPerPel;
            public int   dmPelsWidth;
            public int   dmPelsHeight;
            public int   dmDisplayFlags;
            public int   dmDisplayFrequency;
            public int   dmICMMethod;
            public int   dmICMIntent;
            public int   dmMediaType;
            public int   dmDitherType;
            public int   dmReserved1;
            public int   dmReserved2;
            public int   dmPanningWidth;
            public int   dmPanningHeight;
        }

        public const int ENUM_CURRENT_SETTINGS = -1;

        public const uint DM_BITSPERPEL       = 0x00040000;
        public const uint DM_PELSWIDTH        = 0x00080000;
        public const uint DM_PELSHEIGHT       = 0x00100000;
        public const uint DM_DISPLAYFREQUENCY = 0x00400000;

        public const uint CDS_UPDATEREGISTRY = 0x00000001;
        public const uint CDS_TEST           = 0x00000002;
        public const uint CDS_FULLSCREEN     = 0x00000004;

        [DllImport("user32.dll", CharSet = CharSet.Ansi)]
        public static extern bool EnumDisplaySettings(string deviceName, int modeNum, ref DEVMODE devMode);

        [DllImport("user32.dll", CharSet = CharSet.Ansi)]
        public static extern int ChangeDisplaySettingsEx(string deviceName, ref DEVMODE devMode, IntPtr hwnd, uint flags, IntPtr param);

        [DllImport("user32.dll", CharSet = CharSet.Ansi)]
        public static extern int ChangeDisplaySettingsEx(string deviceName, IntPtr devMode, IntPtr hwnd, uint flags, IntPtr param);

        public static DEVMODE NewMode()
        {
            DEVMODE dm = new DEVMODE();
            dm.dmDeviceName = new string('\0', 32);
            dm.dmFormName   = new string('\0', 32);
            dm.dmSize       = (short)Marshal.SizeOf(typeof(DEVMODE));
            return dm;
        }
    }
}
'@
}

# NOTE: PowerShell variable names are case-insensitive, so the type reference must not be called
# $dm - a "$dm = ..." assignment would silently overwrite it.
$Native = [ArcadeDisplay.Native]

# Passing $null to a [string] P/Invoke parameter does NOT marshal as a NULL pointer in PowerShell:
# it is converted to an empty string, and EnumDisplaySettings/ChangeDisplaySettingsEx reject that
# with ERROR_INVALID_PARAMETER (87). [NullString]::Value is the way to pass a genuine NULL, which
# is what "use the current display" requires.
$NullDevice = [NullString]::Value

# ---------------------------------------------------------------------------
# Which display to work on
# ---------------------------------------------------------------------------
# Windows names displays \\.\DISPLAYn, and that n is the number the user sees in Display Settings and
# in the emulator's own display list. The interop calls above all take that name, with NULL meaning
# "the current one" - which is why a script that only ever passed NULL could only ever reach the
# primary display, whatever monitor the user had in mind.
#
# The enumeration goes through System.Windows.Forms.Screen because it needs no interop of its own:
# its DeviceName is exactly that \\.\DISPLAYn string, and Primary identifies the main display.
#
# Returns a collection, and - like any PowerShell function returning one - a single display comes back
# unrolled as the object itself rather than a one-element array. Callers that need .Count or indexing
# must wrap the call in @(); passing it straight into a pipeline is fine either way. This has already
# caught one caller out (see the note on the monitor list in the List action), and it fails only on
# machines with exactly one display, so it goes unnoticed on the multi-monitor setup it was written on.
# ---------------------------------------------------------------------------
function Get-Monitors
{
    Add-Type -AssemblyName System.Windows.Forms -ErrorAction SilentlyContinue

    $monitors = New-Object System.Collections.Generic.List[object]
    foreach ($screen in [System.Windows.Forms.Screen]::AllScreens)
    {
        # "\\.\DISPLAY12" -> 12. The prefix holds no digits, so the digits are the number.
        $number = 0
        foreach ($character in $screen.DeviceName.ToCharArray())
        {
            if ([char]::IsDigit($character))
            {
                $number = $number * 10 + [int][string]$character
            }
        }

        $monitors.Add([pscustomobject]@{
            Number  = $number
            Device  = $screen.DeviceName
            Primary = $screen.Primary
        })
    }
    return $monitors
}

function Resolve-MonitorDevice
{
    param([int]$Monitor)

    # 0 keeps the old behaviour: whatever display the API calls consider current, which is the
    # primary one. Kept as the default so an existing shortcut or command line does not change
    # meaning when this parameter is added.
    #
    # An empty string, not [NullString]::Value, is returned here on purpose. The callers pass the
    # result through [string]-typed parameters, and PowerShell coerces a NullString to "" on the way;
    # doing the conversion back to a genuine NULL at the P/Invoke itself is the only place it
    # survives. See Get-DisplayModes.
    if ($Monitor -le 0)
    {
        return ''
    }

    $match = @(Get-Monitors | Where-Object { $_.Number -eq $Monitor })
    if ($match.Count -eq 0)
    {
        # Every mode query below is per-display, so a number that does not exist produces an empty
        # mode list rather than an error - and "no 57/58 Hz mode found" is the wrong thing to tell
        # someone who simply mistyped the number of a monitor that is not plugged in.
        Write-Host ''
        Write-Host ('  [ERROR] ' + ($Text.NoSuchMonitor -f $Monitor)) -ForegroundColor Red
        Write-Host ('  ' + $Text.MonitorsHint) -ForegroundColor DarkGray
        Write-Host ''
        exit 1
    }

    return $match[0].Device
}

function Get-DisplayModes
{
    param([string]$Device = '')

    # "" is not the same as no device: EnumDisplaySettings rejects it with ERROR_INVALID_PARAMETER,
    # while a genuine NULL means "the current display". [NullString]::Value is the way to pass a
    # genuine NULL, and this is the last point before the call where it can be substituted - a
    # [string]-typed parameter or a function return would have coerced it to "" already.
    $target = if ($Device) { $Device } else { $NullDevice }

    $modes = New-Object System.Collections.Generic.List[object]
    $dm = $Native::NewMode()
    $i = 0
    while ($Native::EnumDisplaySettings($target, $i, [ref]$dm))
    {
        $modes.Add([pscustomobject]@{
            Width   = $dm.dmPelsWidth
            Height  = $dm.dmPelsHeight
            Hz      = $dm.dmDisplayFrequency
            Bpp     = $dm.dmBitsPerPel
            DevMode = $dm
        })
        $i++
    }
    return $modes
}

function Get-CurrentMode
{
    param([string]$Device = '')

    $target = if ($Device) { $Device } else { $NullDevice }

    $dm = $Native::NewMode()
    if (-not $Native::EnumDisplaySettings($target, $Native::ENUM_CURRENT_SETTINGS, [ref]$dm))
    {
        throw 'Unable to read the current display settings.'
    }
    return [pscustomobject]@{
        Width  = $dm.dmPelsWidth
        Height = $dm.dmPelsHeight
        Hz     = $dm.dmDisplayFrequency
        Bpp    = $dm.dmBitsPerPel
    }
}

function Apply-Mode
{
    param(
        [int]$Width,
        [int]$Height,
        [int]$Hz,
        [int]$Bpp = 32,
        [string]$Device = '',
        [switch]$TestOnly
    )

    $target = if ($Device) { $Device } else { $NullDevice }

    $match = Get-DisplayModes -Device $Device |
        Where-Object { $_.Width -eq $Width -and $_.Height -eq $Height -and $_.Hz -eq $Hz -and $_.Bpp -ge 24 } |
        Select-Object -First 1

    if (-not $match)
    {
        return $false
    }

    $dm = $match.DevMode
    $dm.dmFields = $Native::DM_PELSWIDTH -bor $Native::DM_PELSHEIGHT -bor $Native::DM_BITSPERPEL

    $flags = $Native::CDS_FULLSCREEN
    if ($TestOnly)
    {
        $flags = $flags -bor $Native::CDS_TEST
    }
    else
    {
        $flags = $flags -bor $Native::CDS_UPDATEREGISTRY
    }

    $result = $Native::ChangeDisplaySettingsEx($target, [ref]$dm, [IntPtr]::Zero, $flags, [IntPtr]::Zero)
    return ($result -eq 0)     # DISP_CHANGE_SUCCESSFUL
}

function Restore-Mode
{
    param([object]$Saved, [string]$Device = '')

    if ($Saved)
    {
        # The display is taken from the saved state when it recorded one, so that restoring after the
        # number changed - or after being run without -Monitor - still goes back to the monitor the
        # mode was taken from. Older state files have no Device field, hence the property check rather
        # than a plain read: Set-StrictMode makes reading a missing property an error.
        $targetDevice = $Device
        if ($Saved.PSObject.Properties.Name -contains 'Device' -and $Saved.Device)
        {
            $targetDevice = $Saved.Device
        }

        if (Apply-Mode -Width $Saved.Width -Height $Saved.Height -Hz $Saved.Hz -Bpp $Saved.Bpp -Device $targetDevice)
        {
            return $true
        }
        Write-Warning ('Could not restore {0}x{1}@{2}Hz; falling back to the driver default mode.' -f $Saved.Width, $Saved.Height, $Saved.Hz)
    }

    # Last resort: let the driver return to the mode recorded in the registry
    $result = $Native::ChangeDisplaySettingsEx($NullDevice, [IntPtr]::Zero, [IntPtr]::Zero, 0, [IntPtr]::Zero)
    return ($result -eq 0)
}

function Save-DisplayState
{
    param([string]$Device = '', [int]$Number = 0)

    $cur = Get-CurrentMode -Device $Device
    $state = [pscustomobject]@{
        Number = $Number
        Device = $Device
        Width  = $cur.Width
        Height = $cur.Height
        Hz     = $cur.Hz
        Bpp    = $cur.Bpp
    }
    $state | ConvertTo-Json | Set-Content -Path $StatePath -Encoding UTF8
    Write-Host ('Saved current display settings: {0}x{1}@{2}Hz -> {3}' -f $cur.Width, $cur.Height, $cur.Hz, $StatePath)
}

function Read-DisplayState
{
    if (-not (Test-Path $StatePath))
    {
        return $null
    }
    return (Get-Content -Path $StatePath -Raw | ConvertFrom-Json)
}

function Find-Supermodel
{
    if ($SupermodelExe) { return (Resolve-Path $SupermodelExe).Path }

    # The script is written to work both from a source checkout and from the assembled distribution,
    # because it ships in the latter - the interface and the README both tell the reader to run it, and
    # a script that only exists in the repository makes that an instruction nobody can follow.
    #
    # Distribution layout nests it one level down, beside the program:
    #     Model-3-Vulkan\Model3-Vulkan.exe
    #     Model-3-Vulkan\Scripts\ArcadeDisplay.ps1
    # A checkout puts the executable under MSVC\ instead, which is the fallback below.
    $parent = Resolve-Path (Join-Path $PSScriptRoot '..')

    # The Vulkan build first: it is the one the manual recommends and the one the display helper is
    # most often used with. LegacyOG and the pre-rename names stay in the list, so a distribution
    # assembled before the two-executable layout (or one that has been trimmed to a single exe) still
    # resolves.
    foreach ($name in @('Model3-Vulkan.exe', 'Supermodel-LegacyOG.exe', 'Supermodel.exe'))
    {
        $candidate = Join-Path $parent $name
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }

    $candidate = Join-Path $parent 'MSVC\x64\Release\Supermodel-LegacyOG.exe'
    if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }

    $candidate = Join-Path $parent 'MSVC\x64\Release\Supermodel.exe'
    if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }

    throw 'Model 3-Vulkan / Supermodel-LegacyOG was not found next to this script or under MSVC\x64\Release; pass -SupermodelExe <path>.'
}

# ---------------------------------------------------------------------------
# Pick the target mode: prefer 57/58 Hz at the current desktop resolution
# ---------------------------------------------------------------------------
function Select-ArcadeMode
{
    param([int]$Width, [int]$Height, [double]$Hz, [string]$Device = '')

    $arcade = Get-DisplayModes -Device $Device | Where-Object { $_.Bpp -ge 24 -and ($_.Hz -eq 57 -or $_.Hz -eq 58) }
    if (-not $arcade) { return $null }

    if ($Width -gt 0 -and $Height -gt 0)
    {
        $arcade = $arcade | Where-Object { $_.Width -eq $Width -and $_.Height -eq $Height }
        if (-not $arcade) { return $null }
    }
    else
    {
        $cur = Get-CurrentMode -Device $Device
        $preferred = $arcade | Where-Object { $_.Width -eq $cur.Width -and $_.Height -eq $cur.Height }
        if ($preferred) { $arcade = $preferred }
    }

    return ($arcade | Sort-Object { [Math]::Abs($_.Hz - $Hz) }, Width, Height | Select-Object -First 1)
}

# ---------------------------------------------------------------------------
# Actions
# ---------------------------------------------------------------------------
switch ($Action)
{
    'List'
    {
        # Resolved before anything is queried, because every query below is per-display. A number the
        # user mistyped has to be reported as such rather than surfacing as "no 57/58 Hz mode", which
        # would send them off to create a custom resolution on the wrong monitor.
        $device = Resolve-MonitorDevice -Monitor $Monitor

        # @() is not decoration. Get-Monitors returns a collection, and PowerShell unrolls a
        # one-element collection on its way out of a function - so on a machine with a single display
        # $monitors would be the object itself rather than an array, and .Count would throw under
        # Set-StrictMode. That is exactly the sign-in of the bug this fixes: the helper worked on a
        # two-monitor desktop and failed on a laptop, which is the machine least able to shrug it off.
        $monitors = @(Get-Monitors | Sort-Object Number)
        if ($monitors.Count -gt 1)
        {
            Write-Host ('  ' + $Text.Monitors + ':') -ForegroundColor Cyan
            foreach ($screen in $monitors)
            {
                $kind = if ($screen.Primary) { '  (primary)' } else { '' }
                $mark = if ($Monitor -gt 0 -and $screen.Number -eq $Monitor) { '  <== ' + $Text.Using } else { '' }
                Write-Host ('    {0}{1}{2}' -f $screen.Number, $kind, $mark) -ForegroundColor Gray
            }
            Write-Host ''
        }

        $all = Get-DisplayModes -Device $device | Where-Object { $_.Bpp -ge 24 }
        $cur = Get-CurrentMode -Device $device

        Write-Host ''
        Write-Host ('Current mode: {0}x{1} @ {2} Hz' -f $cur.Width, $cur.Height, $cur.Hz) -ForegroundColor Cyan
        Write-Host ('  ' + $Text.Avail + ':') -ForegroundColor Cyan

        $all |
            Group-Object { '{0}x{1}' -f $_.Width, $_.Height } |
            Sort-Object { [int]($_.Name -split 'x')[0] }, { [int]($_.Name -split 'x')[1] } |
            ForEach-Object {
                $rates = ($_.Group | Sort-Object Hz -Unique | ForEach-Object { $_.Hz }) -join ', '
                $hasArcade = $null -ne ($_.Group | Where-Object { $_.Hz -eq 57 -or $_.Hz -eq 58 } | Select-Object -First 1)
                $mark  = if ($hasArcade) { '  <== ' + $Text.Usable } else { '' }
                $color = if ($hasArcade) { 'Green' } else { 'Gray' }
                Write-Host ('  {0,-12} {1,4} Hz{2}' -f $_.Name, $rates, $mark) -ForegroundColor $color
            }

        Write-Host ''
        $pick = Select-ArcadeMode -Width $Width -Height $Height -Hz $Hz -Device $device
        if ($pick)
        {
            Write-Host ('Arcade mode available: {0}x{1} @ {2} Hz' -f $pick.Width, $pick.Height, $pick.Hz) -ForegroundColor Green
            Write-Host '  Use -Action Run to launch with it (the desktop is restored on exit).'
        }
        else
        {
            Write-Host $Text.None -ForegroundColor Yellow
            Write-Host '  Create one in the GPU control panel first (see the script header), then rerun -Action List.'
        }
        Write-Host ''
        Write-Host ('  ' + $Text.Hint + ': ' + $Text.HintBody) -ForegroundColor DarkGray
        Write-Host ''
    }

    'Save'
    {
        $device = Resolve-MonitorDevice -Monitor $Monitor
        Save-DisplayState -Device $device -Number $Monitor
    }

    'Restore'
    {
        $saved = Read-DisplayState
        if (Restore-Mode -Saved $saved -Device (Resolve-MonitorDevice -Monitor $Monitor))
        {
            Write-Host 'Display settings restored.'
        }
        else
        {
            Write-Warning 'Failed to restore the display settings.'
        }
    }

    'Run'
    {
        if (-not $Game) { throw 'Run mode needs -Game <rom zip path>.' }
        $exe = Find-Supermodel

        $device = Resolve-MonitorDevice -Monitor $Monitor

        $pick = Select-ArcadeMode -Width $Width -Height $Height -Hz $Hz -Device $device
        if (-not $pick)
        {
            $where = if ($Monitor -gt 0) { ' on display {0}' -f $Monitor } else { '' }
            Write-Warning ('No 57/58 Hz display mode{0}; running normally (borderless fullscreen at the desktop rate).' -f $where)
            Write-Warning 'Run -Action List to see what is available and create a custom resolution if needed.'
        }

        $smArgs = @(
            $Game,
            '-fullscreen',
            ('-exclusive-refresh-rate={0}' -f $Hz)
        )
        # The emulator has to be told which display to drive as well, and told the same number: the
        # mode is switched here on one monitor, so a session that opens fullscreen on another finds no
        # such timing and quietly falls back to the desktop rate. Passing it only when one was named
        # keeps the default behaviour - the primary display - unchanged.
        if ($Monitor -gt 0)
        {
            $smArgs += ('-display={0}' -f $Monitor)
        }
        $smArgs += $ExtraArgs

        $saved = Get-CurrentMode -Device $device

        try
        {
            if ($pick)
            {
                Write-Host ('Switching to {0}x{1}@{2}Hz ...' -f $pick.Width, $pick.Height, $pick.Hz)
                if (-not (Apply-Mode -Width $pick.Width -Height $pick.Height -Hz $pick.Hz -Device $device))
                {
                    Write-Warning 'Could not switch the display mode; continuing in normal mode.'
                }
                Start-Sleep -Milliseconds 800
            }

            Write-Host ('Launching: {0}' -f $exe)
            Write-Host ('Arguments: {0}' -f ($smArgs -join ' '))
            $proc = Start-Process -FilePath $exe -ArgumentList $smArgs -PassThru -Wait
            Write-Host ('Supermodel exited with code {0}.' -f $proc.ExitCode)
        }
        finally
        {
            if (-not $KeepMode)
            {
                Write-Host 'Restoring the desktop display settings ...'
                Restore-Mode -Saved $saved -Device $device | Out-Null
            }
        }
    }
}


