@echo off
rem ============================================================================
rem  ArcadeDisplay.bat - English launcher for Scripts\ArcadeDisplay.ps1
rem
rem  Double-click with no arguments to list every video mode the display offers
rem  and flag the ones that can carry the arcade 57.524 Hz. Arguments pass
rem  straight through, so the documented forms still work:
rem
rem      ArcadeDisplay.bat -Action List
rem      ArcadeDisplay.bat -Action Save
rem      ArcadeDisplay.bat -Action Run -Game "E:\roms\daytona2.zip"
rem      ArcadeDisplay.bat -Action Restore
rem
rem  A wrapper is needed because a .ps1 cannot be run by double-clicking (it
rem  opens in Notepad instead) and because PowerShell refuses an untrusted script
rem  by default. This calls PowerShell with the policy bypassed for this one
rem  invocation, and holds the window open so the output can be read.
rem
rem  The Chinese counterpart is ArcadeDisplay-zh.bat; it differs only in the
rem  -Lang argument it passes. All localised text lives in the script.
rem
rem  WHY THIS FILE IS PURE ASCII, DELIBERATELY.
rem  cmd.exe re-reads a .bat at a byte offset as it executes, and that offset is
rem  computed while parsing in the code page that was active at the time. A
rem  "chcp 65001" part-way down the file therefore invalidates the offsets for
rem  every following line, and the symptom is that lines lose their "rem" keyword
rem  and get executed as commands - verified: a Chinese comment came back as
rem  "'<text>' is not recognized as an internal or external command".
rem  Keeping the launcher ASCII-only removes the whole class of problem, and the
rem  script itself carries a UTF-8 BOM so PowerShell reads its Chinese correctly
rem  whatever the system code page is.
rem ============================================================================

rem Console to UTF-8 so PowerShell's output is not mangled on the way out. It is
rem the first executable line, before any command whose output could be affected.
chcp 65001 >nul 2>nul

setlocal EnableExtensions
set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"
set "SCRIPT=%HERE%\ArcadeDisplay.ps1"

set "ARGS=%*"
if "%ARGS%"=="" set "ARGS=-Action List"

rem Stripped by substring comparison rather than by scanning the text, which
rem would choke on an argument containing ^ or &.
set "NOPAUSE="
if not "%ARGS%"=="%ARGS:-NoPause=%" set "NOPAUSE=1"
set "ARGS=%ARGS:-NoPause=%"

if not exist "%SCRIPT%" (
    echo.
    echo   [ERROR] ArcadeDisplay.ps1 was not found next to this file.
    echo           Expected at:
    echo             %SCRIPT%
    echo.
    goto :done
)

rem -File rather than -Command so paths and arguments keep their quoting, and
rem -ExecutionPolicy Bypass because a script shipped alongside a build is
rem untrusted by default and would otherwise be refused outright. This applies
rem to this single invocation and changes nothing system-wide.
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -Lang en %ARGS%
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo   [NOTE] The helper exited with code %RC%.
    echo.
)

:done
rem Held open so a double-clicked window does not vanish before it can be read.
rem Skip with:  ArcadeDisplay.bat -Action List -NoPause
if not defined NOPAUSE (
    echo   Press any key to close...
    pause >nul
)

endlocal
