@echo off
rem ============================================================================
rem  ArcadeDisplay-zh.bat - Chinese launcher for Scripts\ArcadeDisplay.ps1
rem
rem  Identical to ArcadeDisplay.bat except for the language it asks the script
rem  for. See that file for the full reasoning; the short version:
rem
rem    * a .ps1 cannot be double-clicked and is refused by the execution policy,
rem      so a wrapper is required;
rem    * all localised text lives in the script, which carries a UTF-8 BOM;
rem    * this launcher stays pure ASCII on purpose.
rem
rem  Double-click with no arguments to list every video mode and flag the ones
rem  that can carry the arcade 57.524 Hz. Arguments pass through:
rem
rem      ArcadeDisplay-zh.bat -Action List
rem      ArcadeDisplay-zh.bat -Action Save
rem      ArcadeDisplay-zh.bat -Action Run -Game "E:\roms\daytona2.zip"
rem      ArcadeDisplay-zh.bat -Action Restore
rem
rem  -NoPause is consumed here and never reaches the script; it exists so the
rem  window can be closed without a keypress, which is what a test harness needs.
rem
rem  ASCII only, deliberately - see the encoding note in ArcadeDisplay.bat. cmd
rem  re-reads a .bat at byte offsets computed in the code page active when the
rem  line was parsed, so a mid-file "chcp 65001" plus non-ASCII text makes later
rem  lines lose their "rem" keyword and run as commands.
rem ============================================================================

chcp 65001 >nul 2>nul

setlocal EnableExtensions
set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"
set "SCRIPT=%HERE%\ArcadeDisplay.ps1"

set "ARGS=%*"
if "%ARGS%"=="" set "ARGS=-Action List"

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

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -Lang zh %ARGS%
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo   [NOTE] The helper exited with code %RC%.
    echo.
)

:done
if not defined NOPAUSE (
    echo   Press any key to close...
    pause >nul
)

endlocal
