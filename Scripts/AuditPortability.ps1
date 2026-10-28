<#
  AuditPortability.ps1

  Finds Win32-only identifiers used outside an #ifdef _WIN32 guard, in the files that a Linux or
  macOS build compiles.

  Why this exists: CI failed at Src/OSD/SDL/Main.cpp with

      error: 'UINT_PTR' does not name a type
      error: 'HWND' was not declared in this scope

  The code was correct on Windows and simply did not exist anywhere else, because the #ifdef
  covered the function bodies but not the declarations at file scope. MSVC never notices: those
  identifiers come from <windows.h> and are always there. Only gcc and clang see the fault.

  This is a static scan, not a compile. It cannot prove a translation unit builds, but it catches
  the whole "identifier only exists on Windows" family in one pass instead of one file per CI run.

  Soundness note: the report is deliberately narrow. It only looks at files that differ from
  upstream - a regression can only be something this fork introduced, and upstream's untouched
  files build on Linux today. It also ignores identifiers the project defines itself (UINT8,
  UINT32, BOOL is Win32 but BYTE and WORD are used by the 68K/Z80 cores as plain typedefs), so the
  output stays short enough to act on.

  Usage:
    powershell -NoProfile -File Scripts/AuditPortability.ps1
    powershell -NoProfile -File Scripts/AuditPortability.ps1 -All
#>

[CmdletBinding()]
param(
    # Root to audit. The main tree and the upload copy are kept in step, so either can be checked.
    [string] $Root = '202610_archived/UploadToGithub_20261017',

    # Compare against this upstream tree and only report files that differ. Set to '' to scan
    # every file the platform builds, which produces a lot of pre-existing noise.
    [string] $Upstream = 'E:\EMU\Supermodel-0.3a-20261017-git-b05ef21',

    # Ignore the diff and scan everything.
    [switch] $All
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------------------------
# What counts as Win32-only.
#
# Excluded on purpose:
#   bool, BYTE, WORD, LONG, UINT8..UINT64  - the project's own Types.h, or C/C++ built-ins
#   BOOL, TRUE, FALSE                      - defined by the SDL headers on every platform
#   DWORD                                  - SCSP.cpp defines its own fallback (see line ~120)
# ---------------------------------------------------------------------------------------------
$tokens = @(
    # --- types that come only from <windows.h> -------------------------------------------------
    'HWND', 'HDC', 'HINSTANCE', 'HICON', 'HMONITOR', 'HRAWINPUT', 'HMENU', 'HGDIOBJ',
    'LRESULT', 'WPARAM', 'LPARAM', 'LPBYTE', 'LPRECT', 'LPSTR', 'LPCSTR', 'LPWSTR', 'LPCWSTR',
    'UINT_PTR', 'INT_PTR', 'LONG_PTR', 'ULONG_PTR', 'DWORD_PTR', 'ATOM', 'COLORREF', 'HKL',
    'WIN32_FIND_DATAA', 'WIN32_FIND_DATAW', 'COPYDATASTRUCT', 'RECT', 'POINT', 'MSG',
    'HINSTANCE__', 'HKEY', 'PHKEY', 'LPDWORD', 'LPOVERLAPPED', 'SECURITY_ATTRIBUTES',

    # --- window management --------------------------------------------------------------------
    'RegisterClass', 'RegisterClassEx', 'UnregisterClass', 'CreateWindow', 'CreateWindowEx',
    'DestroyWindow', 'ShowWindow', 'UpdateWindow', 'DefWindowProc', 'GetWindowLong',
    'GetWindowLongPtr', 'SetWindowLong', 'SetWindowLongPtr', 'GetClientRect', 'GetWindowRect',
    'ScreenToClient', 'ClientToScreen', 'GetParent', 'SetWindowPos', 'SetForegroundWindow',
    'LoadImage', 'LoadIcon', 'LoadCursor', 'MAKEINTRESOURCE', 'MAKELONG', 'MAKELPARAM',
    'SendMessage', 'PostMessage', 'SendMessageTimeout', 'GetDC', 'ReleaseDC', 'GetSystemMetrics',
    'EnumWindows', 'IsWindowVisible', 'MoveWindow', 'DrawMenuBar',

    # --- window messages and constants -------------------------------------------------------
    'WM_SETICON', 'WM_ENTERSIZEMOVE', 'WM_ENTERMENULOOP', 'WM_EXITSIZEMOVE', 'WM_EXITMENULOOP',
    'WM_TIMER', 'WM_INPUT', 'WM_COPYDATA', 'WM_DISPLAYCHANGE', 'WM_SIZE', 'WM_MOVE',
    'WM_WINDOWPOSCHANGED', 'WM_NCHITTEST', 'WM_SYSCOMMAND', 'WM_ACTIVATE', 'WM_CLOSE',
    'WM_DESTROY', 'WM_PAINT', 'HWND_BROADCAST', 'HWND_TOPMOST', 'HWND_NOTOPMOST',
    'ICON_BIG', 'ICON_SMALL', 'SW_HIDE', 'SW_SHOW', 'SW_SHOWNORMAL', 'CW_USEDEFAULT',
    'SetTimer', 'KillTimer', 'USER_TIMER_MINIMUM', 'SC_RESTORE', 'SC_MINIMIZE',

    # --- console, process, priority ----------------------------------------------------------
    'GetModuleHandle', 'GetModuleHandleA', 'GetModuleHandleW', 'GetConsoleWindow', 'AllocConsole',
    'FreeConsole', 'AttachConsole', 'GetStdHandle', 'SetConsoleTitleA',
    'SetPriorityClass', 'GetPriorityClass', 'ABOVE_NORMAL_PRIORITY_CLASS', 'HIGH_PRIORITY_CLASS',
    'NORMAL_PRIORITY_CLASS', 'BELOW_NORMAL_PRIORITY_CLASS', 'REALTIME_PRIORITY_CLASS',
    'ATTACH_PARENT_PROCESS', 'STD_OUTPUT_HANDLE', 'STD_ERROR_HANDLE', 'STD_INPUT_HANDLE',
    'INVALID_HANDLE_VALUE', 'MAX_PATH', 'GetCurrentProcess', 'OpenProcess', 'ExitProcess',
    'MessageBoxA', 'MessageBoxW', 'SetErrorMode', 'SEM_FAILCRITICALERRORS',

    # --- displays, monitors, DPI -------------------------------------------------------------
    'EnumDisplayMonitors', 'EnumDisplaySettings', 'EnumDisplaySettingsA', 'EnumDisplaySettingsW',
    'GetMonitorInfo', 'GetMonitorInfoA', 'GetMonitorInfoW', 'MonitorFromWindow', 'MONITORINFO',
    'DEVMODE', 'GetDeviceCaps', 'LOGPIXELSX', 'SetProcessDPIAware',
    'SetProcessDpiAwarenessContext', 'GetDpiForWindow',

    # --- strings and encoding ----------------------------------------------------------------
    'MultiByteToWideChar', 'WideCharToMultiByte', 'CP_UTF8', 'CP_ACP', 'CHAR', 'WCHAR',

    # --- file system -------------------------------------------------------------------------
    'FindFirstFileA', 'FindFirstFileW', 'FindNextFileA', 'FindNextFileW', 'FindClose',
    'GetFileAttributesA', 'GetFileAttributesW', 'INVALID_FILE_ATTRIBUTES',
    'CreateFileA', 'CreateFileW', 'CloseHandle', 'SetFilePointer', 'ReadFile', 'WriteFile',

    # --- COM / shell -------------------------------------------------------------------------
    'CoInitializeEx', 'CoInitialize', 'CoCreateInstance', 'CoUninitialize', 'CoTaskMemFree',
    'IFileDialog', 'IShellItem', 'CLSID_FileOpenDialog', 'IID_PPV_ARGS', 'InFileDialogOk',
    'SHGetFolderPath', 'SHGetFolderPathA', 'SHGetFolderPathW', 'SHGetKnownFolderPath',
    'FOLDERID_', 'CSIDL_', 'SIGDN_FILESYSPATH', 'COINIT_APARTMENTTHREADED',
    'COINIT_DISABLE_OLE1DDE', 'CLSCTX_INPROC_SERVER', 'SUCCEEDED', 'FAILED_HR',

    # --- keyboard and mouse --------------------------------------------------------------------
    # Overlay.cpp reads the physical key state rather than SDL's, because SDL's goes stale while a
    # game runs. All of this is Win32-only: there is no VK_* namespace anywhere else.
    'GetAsyncKeyState', 'GetCursorPos', 'GetKeyState', 'MapVirtualKey', 'VkKeyScan',
    'VK_TAB', 'VK_LEFT', 'VK_RIGHT', 'VK_UP', 'VK_DOWN', 'VK_PRIOR', 'VK_NEXT', 'VK_HOME',
    'VK_END', 'VK_INSERT', 'VK_DELETE', 'VK_BACK', 'VK_SPACE', 'VK_RETURN', 'VK_ESCAPE',
    'VK_CONTROL', 'VK_SHIFT', 'VK_MENU', 'VK_LBUTTON', 'VK_RBUTTON', 'VK_MBUTTON',
    'VK_NUMPAD', 'VK_DECIMAL', 'VK_SUBTRACT', 'VK_OEM_MINUS', 'VK_OEM_PERIOD', 'VK_OEM_COMMA',

    # --- MSVC-only ---------------------------------------------------------------------------
    # GetLastError() is listed with a negative lookbehind at match time rather than here: the
    # project has its own CThread::GetLastError(), which exists on every platform.
    '__declspec', 'freopen_s', '_wfopen_s', 'WaitForSingleObject',

    # --- SDL's own Windows-specific entry points ---------------------------------------------
    'SDL_SetWindowsMessageHook', 'SDL_SysWMinfo', 'SDL_GetWindowWMInfo'
)

# Explicitly case-sensitive: without RegexOptions.None spelled out, PowerShell's -match is
# case-insensitive, so 'CHAR' would also match the letter sequence in 'char buf[16]' and every
# plain char array in the codebase would be reported. That mistake produced 453 false positives
# on the first run - 'char', 'LONG', 'WORD' and 'BOOL' are all ordinary C identifiers here.
$tokenPattern = [regex]::new(
    '\b(' + (($tokens | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')\b',
    [System.Text.RegularExpressions.RegexOptions]::None)

# Files a Linux/macOS build compiles, minus the Windows-only back ends and vendored packages that
# bring their own guards.
$projectRoot = (Resolve-Path $Root).Path
$files = Get-ChildItem -Path (Join-Path $projectRoot 'Src') -Recurse -File -Include '*.cpp', '*.c', '*.h' |
    Where-Object {
        $_.FullName -notmatch '\\OSD\\Windows\\' -and
        $_.FullName -notmatch '\\Pkgs\\imgui\\' -and
        $_.FullName -notmatch '\\Pkgs\\volk\\' -and
        $_.FullName -notmatch '\\Pkgs\\glew\.c$' -and
        $_.FullName -notmatch '\\Pkgs\\unzip\.c$' -and
        $_.FullName -notmatch '\\Pkgs\\ioapi\.c$' -and
        $_.FullName -notmatch '\\CPU\\68K\\Musashi\\' -and
        $_.FullName -notmatch '\\Debugger\\CPU\\Musashi'
    }

if (-not $All -and $Upstream -and (Test-Path -LiteralPath $Upstream))
{
    $upstreamRoot = (Resolve-Path $Upstream).Path
    $files = $files | Where-Object {
        $rel = $_.FullName.Substring($projectRoot.Length + 1)
        $candidate = Join-Path $upstreamRoot $rel
        if (-not (Test-Path -LiteralPath $candidate)) { return $true }        # new file
        (Get-FileHash -LiteralPath $_.FullName).Hash -ne
            (Get-FileHash -LiteralPath $candidate).Hash                        # modified file
    }
}

$findings = New-Object System.Collections.Generic.List[string]

foreach ($file in $files)
{
    $rel = $file.FullName.Substring($projectRoot.Length + 1)
    $lines = Get-Content -LiteralPath $file.FullName
    $stack = New-Object System.Collections.Generic.List[object]
    $inBlockComment = $false

    for ($i = 0; $i -lt $lines.Count; $i++)
    {
        $line = $lines[$i]
        $trimmed = $line.Trim()

        # --- preprocessor bookkeeping ---------------------------------------------------------
        if ($trimmed -match '^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b')
        {
            $directive = $Matches[1]

            switch -Regex ($directive)
            {
                '^(if|ifdef|ifndef)$' {
                    $isWin = $trimmed -match '\b_WIN32\b' -or $trimmed -match '\bSUPERMODEL_WIN32\b'
                    # An "if not Windows" region is the opposite of a Windows guard: text in it is
                    # exactly what the other platforms compile, so it must never be skipped.
                    $inverted = ($directive -eq 'ifndef' -and $isWin) -or
                                ($trimmed -match '!\s*defined\s*\(?\s*_WIN32')
                    $stack.Add([pscustomobject]@{ Win = $isWin; Inverted = $inverted; Live = -not $inverted })
                    break
                }
                '^elif$' {
                    if ($stack.Count -gt 0 -and $stack[$stack.Count - 1].Win)
                    {
                        $stack[$stack.Count - 1].Live = -not $stack[$stack.Count - 1].Inverted
                    }
                    break
                }
                '^else$' {
                    if ($stack.Count -gt 0 -and $stack[$stack.Count - 1].Win)
                    {
                        $stack[$stack.Count - 1].Live = $stack[$stack.Count - 1].Inverted
                    }
                    break
                }
                '^endif$' {
                    if ($stack.Count -gt 0) { $stack.RemoveAt($stack.Count - 1) }
                    break
                }
            }
            continue
        }

        # --- strip block comments -------------------------------------------------------------
        # Cheap single-line handling: it removes the obvious "// mentions WM_TIMER in prose" cases
        # without pretending to be a real lexer. A leftover hit is reviewed by hand.
        $code = $line
        if ($inBlockComment)
        {
            $close = $code.IndexOf('*/')
            if ($close -lt 0) { continue }
            $code = $code.Substring($close + 2)
            $inBlockComment = $false
        }
        while ($true)
        {
            $open = $code.IndexOf('/*')
            if ($open -lt 0) { break }
            $close = $code.IndexOf('*/', $open + 2)
            if ($close -lt 0) { $code = $code.Substring(0, $open); $inBlockComment = $true; break }
            $code = $code.Substring(0, $open) + $code.Substring($close + 2)
        }
        $comment = $code.IndexOf('//')
        if ($comment -ge 0) { $code = $code.Substring(0, $comment) }

        # --- guarded? -------------------------------------------------------------------------
        $guarded = $false
        foreach ($frame in $stack)
        {
            if ($frame.Win -and $frame.Live) { $guarded = $true; break }
        }
        if ($guarded) { continue }

        if ($tokenPattern.IsMatch($code))
        {
            $findings.Add(('{0}:{1}: {2}' -f $rel, ($i + 1), $trimmed))
        }
    }
}

Write-Host ("Scanned {0} file(s)." -f $files.Count)
if ($findings.Count -eq 0)
{
    Write-Host 'No unguarded Win32 identifiers found.'
}
else
{
    Write-Host ("{0} unguarded line(s):" -f $findings.Count)
    $findings | ForEach-Object { Write-Host $_ }
}
