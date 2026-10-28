@echo off
cls

setlocal

cd /d "%~dp0"

echo === SDL 3.2.30 Source Downloader ===

set URL=https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.30.zip
set ZIPFILE=release-3.2.30.zip
set OUTDIR=SDL3

echo Downloading SDL3 source ZIP...
curl -L "%URL%" -o "%ZIPFILE%"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: curl failed to download the file.
    pause
    exit /b 1
)

echo Extracting ZIP using Windows built-in unzip...
powershell -NoLogo -Command "Expand-Archive -LiteralPath '%ZIPFILE%' -DestinationPath '.' -Force"

if not exist "SDL-release-3.2.30" (
    echo ERROR: Extraction failed.
    pause
    exit /b 1
)

echo Renaming folder...
if exist "%OUTDIR%" rmdir /s /q "%OUTDIR%"
ren SDL-release-3.2.30 %OUTDIR%

echo Cleaning up ZIP...
del "%ZIPFILE%"

echo Done! SDL3 source is now in: %OUTDIR%

endlocal
