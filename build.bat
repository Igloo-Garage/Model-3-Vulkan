@echo off
rem ============================================================================
rem  build.bat - build Supermodel (Vulkan) and assemble a folder you can hand out.
rem
rem  Produces:
rem
rem      dist\Supermodel-VK\
rem          Supermodel-VK.exe     <- the program
rem          README.txt           <- English: what this is, and what it adds
rem          <player notes>.txt    <- any other .txt from Docs\, at the top level where a
rem                                   user will actually see it (e.g. the setup notice)
rem          Data\
rem              Config\           Games.xml, Music.xml
rem              Assets\           crosshair bitmaps, boxarts, the bezel picture
rem              NVRAM\  Saves\  Screenshots\  Analysis\
rem
rem  The executable is self-contained: SDL2 and SDL_net are linked in statically,
rem  so there are no DLLs to ship. Everything configurable lives under Data\.
rem
rem  Usage:
rem      build.bat                Build and assemble the distribution folder.
rem      build.bat v145           Force a particular platform toolset.
rem      build.bat notest         Accepted and ignored; kept so muscle memory works.
rem
rem  Notes:
rem    - Any running Supermodel.exe is closed first. The linker cannot overwrite a
rem      running image and reports LNK1104, which looks like a source problem and
rem      is not one.
rem    - The bezel picture is taken from Supermodel3Bezel.png in the project root (falling back to
rem      Assets\ if that is absent), so editing the root copy is enough - no second copy to keep in
rem      step. The build prints which file and size it used.
rem    - The distribution deliberately contains no Supermodel.ini. One is written
rem      on first run with the current defaults (ROM path C:\Downloads, the
rem      self-linking network setup, software defaults for everything else). If
rem      you bake your own preferred settings into Src\OSD\ConfigFile.h's template
rem      they become the defaults for everyone you hand the folder to.
rem ============================================================================

setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "DIST=%ROOT%\dist\Supermodel-VK"

rem Name of the shipped executable. The project and the intermediate build still produce
rem Supermodel.exe; only the assembled copy is renamed, so nothing about the Visual Studio
rem build (launch config, debugger paths, incremental state) has to change.
set "EXENAME=Supermodel-VK.exe"

rem ---- Version, read out of Version.h ------------------------------------------
rem Read rather than repeated here. The version is written down once, in Src\Version.h, and compiled
rem into the executable; a second copy in this script would be one more place to forget to change,
rem and the two would eventually disagree about which build is which.
rem
rem Delegated to a script because extracting it with cmd alone means a quoted path, a pattern
rem containing a space and quote-delimited tokens in the same command - which cannot be written
rem readably and fails in ways that look like something else entirely.
set "VERSIONTEXT="
if exist "%ROOT%\Scripts\VersionString.ps1" (
    for /f "delims=" %%V in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\Scripts\VersionString.ps1"') do (
        if not defined VERSIONTEXT set "VERSIONTEXT=%%V"
    )
)
if not defined VERSIONTEXT set "VERSIONTEXT=v?.? (Scripts\VersionString.ps1 failed)"
if defined FORKVER if defined FORKDATE (
    set "VERSIONTEXT=v!FORKVER!-!FORKDATE!"
) else (
    set "VERSIONTEXT=(version not found in Src\Version.h)"
)
echo Building Supermodel-VK !VERSIONTEXT!
echo.

rem ---- Toolset -----------------------------------------------------------------
rem The project file names v145, which only ships with the newest toolchains. Rather
rem than failing with MSB8020 - which reads like a broken project - look at what this
rem Visual Studio actually has installed and use the newest of those. An explicit
rem argument always wins.
rem
rem Only the requests the project might name are listed. Passing a toolset as a
rem property overrides it for every project in the build, so the auxiliary projects
rem (SDL, zlib, Musashi) follow along.
set "WANTED="
for %%A in (%*) do (
    if /i "%%~A"=="v143" set "WANTED=v143"
    if /i "%%~A"=="v144" set "WANTED=v144"
    if /i "%%~A"=="v145" set "WANTED=v145"
)

rem ---- Find MSBuild ------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%I"
)
rem Fall back to a PATH lookup, which covers Build Tools installs and shells that
rem were opened from a developer command prompt.
if not defined MSBUILD (
    for %%I in (MSBuild.exe) do if not "%%~$PATH:I"=="" set "MSBUILD=%%~$PATH:I"
)
if not defined MSBUILD (
    echo [ERROR] MSBuild not found.
    echo         Install Visual Studio with the "Desktop development with C++" workload,
    echo         or run this from a Developer Command Prompt.
    exit /b 1
)

rem ---- Pick a toolset that is actually installed -------------------------------
rem Note the !VAR! form inside the parenthesised block below. %VAR% there would be expanded when
rem the whole block is parsed, which is before MSBUILDDIR and VSROOT are assigned, so the search
rem would look in an empty path and report every toolset as missing.
set "TOOLSET="
if defined WANTED (
    set "TOOLSET=%WANTED%"
) else (
    rem MSBuild.exe lives at <VS>\MSBuild\Current\Bin\, so three levels up from its
    rem directory is the install root; the toolset folders sit under it next to the
    rem MSBuild of the same VS version. Note the directory is taken first - appending
    rem ..\ to a file path does not resolve, it just makes a nonsense name.
    for %%I in ("%MSBUILD%") do set "MSBUILDDIR=%%~dpI"
    for %%I in ("!MSBUILDDIR!..\..\..") do set "VSROOT=%%~fI"

    rem Walk the VC\<version>\Platforms\x64\PlatformToolsets folders. for /d is used
    rem rather than dir with a wildcard, because dir only expands wildcards in the
    rem last path component - a glob like VC\*\...\v143 silently matches nothing and
    rem looks exactly like "no toolset installed".
    for /d %%V in ("!VSROOT!\MSBuild\Microsoft\VC\*") do (
        for %%T in (v145 v144 v143) do (
            if not defined TOOLSET (
                if exist "%%V\Platforms\x64\PlatformToolsets\%%T" set "TOOLSET=%%T"
            )
        )
    )
)

echo MSBuild : %MSBUILD%
if defined TOOLSET (
    echo Toolset : %TOOLSET%
) else (
    echo Toolset : as declared in the project ^(none of v143/v144/v145 found^)
)
echo.

rem ---- Close a running copy so the linker can replace the exe ------------------
rem Both names are checked: the Visual Studio build produces Supermodel.exe while the assembled
rem distribution is Supermodel-VK.exe, so the developer's copy and the one they handed out are
rem different processes and either can be the one holding the file open.
for %%N in (Supermodel.exe Supermodel-VK.exe) do (
    tasklist /fi "imagename eq %%N" 2>nul | find /i "%%N" >nul
    if not errorlevel 1 (
        echo Closing a running %%N so the build can overwrite it...
        taskkill /im %%N /f >nul 2>&1
    )
)
rem Give Windows a moment to release the file mapping.
ping -n 2 127.0.0.1 >nul 2>&1

rem ---- Build ------------------------------------------------------------------
set "TOOLSETARG="
if defined TOOLSET set "TOOLSETARG=/p:PlatformToolset=%TOOLSET%"

echo Building Release x64...
echo ----------------------------------------------------------------------------
"%MSBUILD%" "%ROOT%\MSVC\Supermodel.vcxproj" ^
    /p:Configuration=Release ^
    /p:Platform=x64 ^
    %TOOLSETARG% ^
    /m ^
    /v:minimal ^
    /nologo
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. Nothing was assembled.
    exit /b 1
)
echo ----------------------------------------------------------------------------
echo.

set "BIN=%ROOT%\MSVC\x64\Release"
if not exist "%BIN%\Supermodel.exe" (
    echo [ERROR] Build reported success but "%BIN%\Supermodel.exe" is missing.
    exit /b 1
)

rem ---- Translation audit ------------------------------------------------------
rem A missing English entry cannot break anything: Tr() falls back to the Chinese source text, so
rem the only symptom is one label staying Chinese while English is selected - and that is easy to
rem ship without noticing, because in Chinese mode it looks perfectly correct. Reported on every
rem build so the count stays visible. Not fatal, since a false positive should not block a release.
if exist "%ROOT%\Scripts\AuditTranslations.ps1" (
    echo Checking the English translation table...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\Scripts\AuditTranslations.ps1"
    echo.
)

rem ---- Assemble the distribution ----------------------------------------------
echo Assembling "%DIST%"...

rem Start clean so a stale file from an older layout cannot linger - but keep what the user made.
rem
rem The rmdir below used to take their save states, NVRAM, screenshots and settings file with it,
rem which made the folder unusable as somewhere to actually play from: a rebuild would quietly
rem discard a save state that may be the only record of a position someone had reached.
rem
rem Moved aside and put back rather than deleted selectively. Enumerating what to remove would need
rem revisiting every time a new kind of output or user data appears, and the failure mode of getting
rem that wrong - deleting something the user wanted - is silent and permanent. Moving the four known
rem user-data locations is the conservative direction: worst case a stale file survives a build.
set "KEEPDIR=%TEMP%\supermodel-vk-keep"
set "HAVEKEEP="
if exist "%KEEPDIR%" rmdir /s /q "%KEEPDIR%"
for %%D in (Saves NVRAM Screenshots) do (
    if exist "%DIST%\Data\%%D" (
        xcopy /e /i /q /y "%DIST%\Data\%%D" "%KEEPDIR%\%%D\" >nul 2>nul
        set "HAVEKEEP=1"
    )
)
if exist "%DIST%\Data\Config\Supermodel.ini" (
    mkdir "%KEEPDIR%\Config" 2>nul
    copy /y "%DIST%\Data\Config\Supermodel.ini" "%KEEPDIR%\Config\Supermodel.ini" >nul
    set "HAVEKEEP=1"
)

if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%\Data\Config"          2>nul
mkdir "%DIST%\Data\Assets"          2>nul
mkdir "%DIST%\Data\NVRAM"           2>nul
mkdir "%DIST%\Data\Saves"           2>nul
mkdir "%DIST%\Data\Screenshots"     2>nul
mkdir "%DIST%\Data\Analysis"        2>nul

rem Put the user's own data back, on top of the freshly assembled tree.
if defined HAVEKEEP (
    for %%D in (Saves NVRAM Screenshots) do (
        if exist "%KEEPDIR%\%%D" xcopy /e /i /q /y "%KEEPDIR%\%%D" "%DIST%\Data\%%D\" >nul
    )
    if exist "%KEEPDIR%\Config\Supermodel.ini" copy /y "%KEEPDIR%\Config\Supermodel.ini" "%DIST%\Data\Config\Supermodel.ini" >nul
    rmdir /s /q "%KEEPDIR%"
    echo Kept     : save states, NVRAM, screenshots and Supermodel.ini from the previous build
) else (
    echo Kept     : nothing - this is a fresh layout
)

copy /y "%BIN%\Supermodel.exe" "%DIST%\%EXENAME%" >nul

rem Games.xml defines the game list; without it the interface has nothing to show.
rem Music.xml carries the per-game track names. Both are required, so a missing one
rem is a hard error rather than something to discover after handing the folder out.
for %%F in (Games.xml Music.xml) do (
    if not exist "%ROOT%\Config\%%F" (
        echo [ERROR] "%ROOT%\Config\%%F" is missing. It is required at run time.
        exit /b 1
    )
    copy /y "%ROOT%\Config\%%F" "%DIST%\Data\Config\%%F" >nul
)

rem Crosshair bitmaps for the light-gun games.
if exist "%ROOT%\Assets\*.bmp" copy /y "%ROOT%\Assets\*.bmp" "%DIST%\Data\Assets\" >nul

rem ---- Boxarts -----------------------------------------------------------------
rem One WebP per ROM set, named after the set. Kept in a subfolder rather than copied flat
rem into Assets\, which already holds the crosshairs and the cabinet picture. build.bat
rem ships them so a release has the artwork the game list can use without a second step.
rem WebP is roughly a quarter the size of the original PNGs and is decoded through the WIC
rem codec built into Windows 10 1809 and later.
if exist "%ROOT%\Assets\Boxarts\*.webp" (
    mkdir "%DIST%\Data\Assets\Boxarts" 2>nul
    copy /y "%ROOT%\Assets\Boxarts\*.webp" "%DIST%\Data\Assets\Boxarts\" >nul
    echo Boxarts  : copied from Assets\Boxarts
) else (
    echo Boxarts  : not found ^(game list will have no artwork^)
)

rem ---- Bezel backdrop ----------------------------------------------------------
rem The cabinet picture the picture-settings page can put behind the black bars.
rem
rem The master is taken from the project root first, and only then from Assets\. That order is
rem deliberate: the root is where the artwork is actually edited, next to the other source images,
rem and keeping the master there means an edit takes effect on the next build without anyone having
rem to remember to copy it into a second place first.
rem
rem Assets\ remains as a fallback so a checkout that predates this arrangement still builds. Only one
rem file is ever used, and the build prints which one it took along with its size, so a stale
rem duplicate cannot be shipped by accident without that being visible in the output.
rem
rem Missing is not an error: with no picture the setting simply draws nothing and the game runs.
set "BEZEL="
if exist "%ROOT%\Supermodel3Bezel.png"        set "BEZEL=%ROOT%\Supermodel3Bezel.png"
if not defined BEZEL if exist "%ROOT%\Assets\Supermodel3Bezel.png" set "BEZEL=%ROOT%\Assets\Supermodel3Bezel.png"

rem Any other .png in Assets\ ships as well. Done first, so an old bezel left there cannot overwrite
rem the master resolved below.
if exist "%ROOT%\Assets\*.png" (
    for %%F in ("%ROOT%\Assets\*.png") do if /i not "%%~nxF"=="Supermodel3Bezel.png" copy /y "%%~fF" "%DIST%\Data\Assets\" >nul
)

if defined BEZEL (
    copy /y "%BEZEL%" "%DIST%\Data\Assets\Supermodel3Bezel.png" >nul
    for %%B in ("%BEZEL%") do echo Bezel    : %%~nxB from %%~dpB  ^(%%~zB bytes^)
) else (
    echo Bezel    : not found ^(no backdrop will be drawn^)
)

rem Documentation travels with the build, but inside Data\ so the top level stays
rem a single file.
if exist "%ROOT%\Docs\LICENSE.txt" copy /y "%ROOT%\Docs\LICENSE.txt" "%DIST%\Data\" >nul
if exist "%ROOT%\Docs\README.txt"  copy /y "%ROOT%\Docs\README.txt"  "%DIST%\Data\" >nul

rem ---- Player-facing notes go at the top level ---------------------------------
rem Everything in Docs\ other than the two big reference files above is a short note meant to be
rem read before the program is ever started, so burying it in Data\ would defeat the purpose. The
rem setup notice is one of these: it says what to install (nothing), which renderer needs what from
rem the graphics driver, and where the game list comes from.
rem
rem The loop deliberately filters by *name* rather than naming the files to copy. A non-ASCII
rem filename written as a literal in a batch file is read in the console code page rather than as
rem UTF-8, so the copy fails with "file not found" and the build would only work on Windows set to
rem the same language as the author. Taking the name from the file system instead sidesteps the
rem encoding question entirely, and picks up any note dropped into Docs\ later.
rem
rem README-VK.txt is on the skip list because it is renamed on the way out - see below. Copying it
rem here as well would ship both README-VK.txt and README.txt at the top level.
for %%F in ("%ROOT%\Docs\*.txt") do (
    if /i not "%%~nxF"=="README.txt" (
        if /i not "%%~nxF"=="LICENSE.txt" (
            if /i not "%%~nxF"=="README-VK.txt" (
                copy /y "%%~fF" "%DIST%\%%~nxF" >nul
            )
        )
    )
)

rem ---- English README at the top level -----------------------------------------
rem Shipped as README.txt: the conventional name, and the one someone who has just been handed a
rem folder looks for. It explains what the program is, that it is a rework of the open-source
rem Supermodel, and what this build adds.
rem
rem Renamed rather than named README.txt in Docs\, because Docs\README.txt is already the upstream
rem Supermodel manual and belongs in Data\ with the rest of the reference material. Two different
rem files cannot share that name, and the manual is the one that must not move.
rem
rem ASCII only, like the rest of this file's own text, so it opens correctly in any editor.
if exist "%ROOT%\Docs\README-VK.txt" (
    copy /y "%ROOT%\Docs\README-VK.txt" "%DIST%\README.txt" >nul
    for %%R in ("%ROOT%\Docs\README-VK.txt") do echo Readme   : README.txt from Docs\README-VK.txt  ^(%%~zR bytes^)
) else (
    echo Readme   : not found ^(Docs\README-VK.txt^)
)

rem ---- The arcade display helper ships with the build -------------------------
rem Scripts\ArcadeDisplay.ps1 handles the 57/58 Hz display mode: listing whether one exists, and
rem switching to it for a session and restoring the desktop afterwards. Both the settings window and
rem the README tell the reader to run it, so leaving it behind in the source tree made that an
rem instruction nobody could follow - the published folder had no Scripts\ at all.
rem
rem ArcadeDisplay.bat and ArcadeDisplay-zh.bat come with it: a .ps1 cannot be run by double-clicking,
rem and PowerShell refuses an untrusted script by default, so without a wrapper the tool is still
rem effectively unreachable for anyone who does not already use PowerShell. One launcher per
rem language, so each reads cleanly rather than mixing both on every line.
rem
rem Both are placed in a Scripts\ folder rather than loose at the top level, so the path the
rem documentation already quotes stays correct.
rem
rem The script resolves the executable relative to its own parent folder, so it works from here
rem unchanged.
if not exist "%DIST%\Scripts" mkdir "%DIST%\Scripts" 2>nul
for %%F in ("%ROOT%\Scripts\ArcadeDisplay.ps1" "%ROOT%\Scripts\ArcadeDisplay.bat" "%ROOT%\Scripts\ArcadeDisplay-zh.bat") do (
    if exist "%%~fF" (
        copy /y "%%~fF" "%DIST%\Scripts\%%~nxF" >nul
        echo Helper   : Scripts\%%~nxF  ^(%%~zF bytes^)
    ) else (
        echo Helper   : not found ^(Scripts\%%~nxF^)
    )
)

rem ---- Nothing is written next to the compiler's output ----------------------
rem A copy of the data tree (Data\, Config\, Assets\) and the documentation used to be mirrored into
rem %BIN% as well, so that launching the freshly built exe - F5 in Visual Studio - would not land on
rem an exe whose Data\Config\Games.xml does not exist yet.
rem
rem That is gone. It left a second, stale copy of the data among the compiler's own output, which is
rem not what that folder is for: MSVC\x64\Release\ now holds what msbuild produces and nothing else.
rem The convenience was also only ever needed while debugging, and the assembled folder is the thing
rem meant to be run.
rem
rem To run the build output directly, launch it with dist\Supermodel-VK as the working directory, or
rem point the debugger's working directory there. %BIN% is still used below, because it is where the
rem exe that gets assembled into dist\ comes from.

rem ---- Report -----------------------------------------------------------------
echo.
echo Done. Supermodel-VK !VERSIONTEXT!
echo.
echo Hand out this folder:
echo.
echo     %DIST%
echo.
echo Contents at the top level:
for %%F in ("%DIST%\*") do echo     %%~nxF
echo.
echo Robocopy it somewhere else, or zip the folder - it is relocatable, and the
echo ROM path (C:\Downloads) can be changed from the interface.
echo.
echo Version !VERSIONTEXT! (from Src\Version.h, also shown in the title bar).
echo.

endlocal
exit /b 0
