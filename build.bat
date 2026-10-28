@echo off
rem ============================================================================
rem  build.bat - build Model 3-Vulkan + Supermodel-LegacyOG and assemble a folder to hand out.
rem
rem  Produces:
rem
rem      dist\Supermodel-VK\
rem          Supermodel-LegacyOG.exe <- SDL2 build: OpenGL renderer only, maximum
rem                                     peripheral compatibility (DirectInput / Raw Input /
rem                                     XInput, full force feedback)
rem          Model3-Vulkan.exe      <- SDL3 build: Vulkan 1.3, low-latency input path
rem                                     (the product name is "Model 3-Vulkan")
rem          README.txt             <- English: what this is, and what it adds
rem          roms\                  <- put ROM set .zip files here; the emulator finds it automatically
rem          <player notes>.txt    <- any other .txt from Docs\, at the top level where a
rem                                   user will actually see it (e.g. the setup notice)
rem          Data\
rem              Config\           Games.xml, Music.xml
rem              Assets\           crosshair bitmaps, boxarts, the bezel picture
rem              NVRAM\  Saves\  Saves_Legacy\  Screenshots\  Analysis\  Cache\
rem
rem  With -package, a second tree is written for handing out:
rem
rem      build-package\Model3-Vulkan.<version>.for.Windows.zip
rem                             the same folder minus save states, NVRAM,
rem                             screenshots, the pipeline cache and your own
rem                             settings files
rem
rem  The executables are self-contained: SDL2, SDL_net and SDL3 are linked in
rem  statically, so there are no DLLs to ship. Everything configurable lives under
rem  Data\, which both executables share - and that is what makes it safe to
rem  switch between them without editing settings.
rem
rem  The Vulkan binary is built with CMake/Ninja (see build-cmake.bat) because the
rem  MSVC project still describes the SDL2 target only. Both are assembled here so
rem  a release contains both.
rem
rem  Usage:
rem      build.bat                Build and assemble the distribution folder.
rem      build.bat -package       ... and also write a clean release zip next to it.
rem      build.bat v145           Force a particular platform toolset.
rem      build.bat notest         Accepted and ignored; kept so muscle memory works.
rem
rem  Notes:
rem    - Any running emulator executable is closed first. The linker cannot overwrite a
rem      running image and reports LNK1104, which looks like a source problem and
rem      is not one.
rem    - The bezel picture is taken from Model3VulkanBezel.png in the project root (falling back to
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

rem Name of the shipped executable. The Visual Studio project and the CMake target
rem already carry this name; the copy below is what lands in dist\.
set "EXENAME=Supermodel-LegacyOG.exe"
set "EXENAME_SDL3=Model3-Vulkan.exe"

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
echo Building Supermodel-LegacyOG !VERSIONTEXT!
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
set "PACKAGE="
for %%A in (%*) do (
    if /i "%%~A"=="v143" set "WANTED=v143"
    if /i "%%~A"=="v144" set "WANTED=v144"
    if /i "%%~A"=="v145" set "WANTED=v145"
    if /i "%%~A"=="-package" set "PACKAGE=1"
    if /i "%%~A"=="package"  set "PACKAGE=1"
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
rem Both names are checked: the Visual Studio build produces Supermodel-LegacyOG.exe while the
rem assembled distribution is the same name, so the developer's copy and the one they handed out
rem are different processes and either can be the one holding the file open.
for %%N in (Supermodel-LegacyOG.exe Supermodel-VK.exe Model3-Vulkan.exe Supermodel-VK-SDL3.exe) do (
    tasklist /fi "imagename eq %%N" 2>nul | findstr /i /c:"%%N" >nul
    if not errorlevel 1 (
        echo Closing a running %%N so the build can overwrite it...
        taskkill /im %%N /f >nul 2>&1
    )
)
rem Give Windows a moment to release the file mapping.
ping -n 2 127.0.0.1 >nul 2>&1

rem ---- Start-up artwork signature ---------------------------------------------
rem  The splash picture is compiled in as a resource and checked at start-up against the SHA-256
rem  recorded in Src\OSD\Windows\SplashResource.h. Editing the picture without refreshing that hash
rem  would make every build announce itself as UNOFFICIAL, so it is regenerated here instead of
rem  being left to be remembered. Idempotent: unchanged artwork prints the same hash and writes
rem  nothing.
if exist "%ROOT%\Scripts\UpdateSplashSignature.ps1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\Scripts\UpdateSplashSignature.ps1"
    echo.
)

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
if not exist "%BIN%\Supermodel-LegacyOG.exe" (
    echo [ERROR] Build reported success but "%BIN%\Supermodel-LegacyOG.exe" is missing.
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

rem ---- Vulkan target (CMake/Ninja) ----------------------------------------------
rem  The second executable is built by CMake, not by the MSVC project. build-cmake.bat
rem  brings up vcvars64 itself and builds both CMake targets; the SDL2 one is a
rem  no-op here because MSBuild just produced the same sources, but leaving both on
rem  keeps the CMake cache stable instead of flipping USE_SDL2 between runs.
rem
rem  A failure is fatal: shipping a folder whose second executable is missing (or
rem  is an older build than the first) is worse than not shipping it at all.
echo Building Model 3-Vulkan...
echo ----------------------------------------------------------------------------
call "%ROOT%\build-cmake.bat" -DUSE_SDL3=ON
if errorlevel 1 (
    echo.
    echo [ERROR] SDL3 build failed. Nothing was assembled.
    exit /b 1
)

echo ----------------------------------------------------------------------------
echo.

set "BIN_SDL3=%ROOT%\build-cmake\bin\Model3-Vulkan.exe"
if not exist "%BIN_SDL3%" (
    echo [ERROR] SDL3 build reported success but "%BIN_SDL3%" is missing.
    exit /b 1
)

rem ---- C# host module (Native AOT) ---------------------------------------------
rem  The Vulkan executable calls into Model3VulkanHostCore.dll for its cold paths: parsing the
rem  settings file and managing the .vkstate save containers. The executable still runs
rem  without it - the renderer and the game core do not depend on it - but the save
rem  manager and the ini readout would report a missing module, which is not something to
rem  discover after handing the folder out.
rem
rem  Published from the same script so the two cannot drift: the C ABI version in
rem  The C ABI version in Model3VulkanHostCore.h is checked at load time.
echo Building the host core module...
echo ----------------------------------------------------------------------------
set "HOSTCORE_PROJ=%ROOT%\HostCore\Model3VulkanHostCore.csproj"
set "HOSTCORE_DIR=%ROOT%\HostCore\publish"
set "HOSTCORE_DLL=%HOSTCORE_DIR%\Model3VulkanHostCore.dll"
if not exist "%HOSTCORE_PROJ%" (
    echo [ERROR] "%HOSTCORE_PROJ%" is missing. It is required by the Vulkan executable.
    exit /b 1
)
dotnet publish "%HOSTCORE_PROJ%" -c Release -r win-x64 -o "%HOSTCORE_DIR%"
if errorlevel 1 (
    echo [ERROR] The host core module failed to build. Nothing was assembled.
    exit /b 1
)
if not exist "%HOSTCORE_DLL%" (
    echo [ERROR] The host core build reported success but "%HOSTCORE_DLL%" is missing.
    exit /b 1
)

rem The CMake POST_BUILD copy only sees the module when it existed at configure time, and this
rem script publishes after the CMake build. Keep build-cmake\bin directly runnable as well.
if exist "%ROOT%\build-cmake\bin" (
    copy /y "%HOSTCORE_DLL%" "%ROOT%\build-cmake\bin\Model3VulkanHostCore.dll" >nul
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
rem that wrong - deleting something the user wanted - is silent and permanent. Moving the known
rem user-data locations is the conservative direction: worst case a stale file survives a build.
set "KEEPDIR=%TEMP%\supermodel-vk-keep"
set "HAVEKEEP="
if exist "%KEEPDIR%" rmdir /s /q "%KEEPDIR%"
for %%D in (Saves Saves_Legacy NVRAM Screenshots Cache) do (
    if exist "%DIST%\Data\%%D" (
        xcopy /e /i /q /y "%DIST%\Data\%%D" "%KEEPDIR%\%%D\" >nul 2>nul
        set "HAVEKEEP=1"
    )
)
rem HD texture packs and dumps live under Data\Assets and are user data just like save states: a
rem pack can be hours of upscaling work, so a rebuild must not take it with the rest of the tree.
for %%D in (HDTextures DumpTextures) do (
    if exist "%DIST%\Data\Assets\%%D" (
        mkdir "%KEEPDIR%\Assets" 2>nul
        xcopy /e /i /q /y "%DIST%\Data\Assets\%%D" "%KEEPDIR%\Assets\%%D\" >nul 2>nul
        set "HAVEKEEP=1"
    )
)
if exist "%DIST%\Data\Config\Supermodel.ini" (
    mkdir "%KEEPDIR%\Config" 2>nul
    copy /y "%DIST%\Data\Config\Supermodel.ini" "%KEEPDIR%\Config\Supermodel.ini" >nul
    set "HAVEKEEP=1"
)
rem The Vulkan build keeps its own settings file (<exe stem>.ini, now Model3-Vulkan.ini). It is a
rem user file just like Supermodel.ini, and without this a rebuild would silently reset every
rem Vulkan-only option by reseeding it from the global file.  The previous fork name is kept too,
rem so an installation that has not run the renamed executable yet can still be migrated.
for %%F in (Model3-Vulkan.ini Supermodel-VK-SDL3.ini) do (
    if exist "%DIST%\Data\Config\%%F" (
        mkdir "%KEEPDIR%\Config" 2>nul
        copy /y "%DIST%\Data\Config\%%F" "%KEEPDIR%\Config\%%F" >nul
        set "HAVEKEEP=1"
    )
)

rem ROM folders live at the top level (roms/, ROMs/, rom/, ...). They are the user's game data,
rem not output: a rebuild must not delete them. The wildcard is case-insensitive on Windows, which
rem covers the spellings the settings window also recognizes.
for /d %%R in ("%DIST%\rom*") do (
    if exist "%%~fR" (
        xcopy /e /i /q /y "%%~fR" "%KEEPDIR%\%%~nxR\" >nul 2>nul
        set "HAVEKEEP=1"
    )
)

if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%\Data\Config"          2>nul
mkdir "%DIST%\Data\Assets"          2>nul
mkdir "%DIST%\Data\NVRAM"           2>nul
mkdir "%DIST%\Data\Saves"           2>nul
mkdir "%DIST%\Data\Saves_Legacy"    2>nul
mkdir "%DIST%\Data\Screenshots"     2>nul
mkdir "%DIST%\Data\Cache"           2>nul
mkdir "%DIST%\Data\Analysis"        2>nul
mkdir "%DIST%\roms"                2>nul
> "%DIST%\roms\README.txt" echo Put Model 3 ROM set .zip files in this folder; the emulator finds it automatically.

rem Put the user's own data back, on top of the freshly assembled tree.
if defined HAVEKEEP (
    for %%D in (Saves Saves_Legacy NVRAM Screenshots Cache) do (
        	if exist "%KEEPDIR%\%%D" xcopy /e /i /q /y "%KEEPDIR%\%%D" "%DIST%\Data\%%D\" >nul
    )
    for %%D in (HDTextures DumpTextures) do (
        if exist "%KEEPDIR%\Assets\%%D" xcopy /e /i /q /y "%KEEPDIR%\Assets\%%D" "%DIST%\Data\Assets\%%D\" >nul
    )
    if exist "%KEEPDIR%\Config\Supermodel.ini" copy /y "%KEEPDIR%\Config\Supermodel.ini" "%DIST%\Data\Config\Supermodel.ini" >nul
    for %%F in (Model3-Vulkan.ini Supermodel-VK-SDL3.ini) do (
        if exist "%KEEPDIR%\Config\%%F" copy /y "%KEEPDIR%\Config\%%F" "%DIST%\Data\Config\%%F" >nul
    )
    for /d %%R in ("%KEEPDIR%\rom*") do (
        if exist "%%~fR" xcopy /e /i /q /y "%%~fR" "%DIST%\%%~nxR\" >nul
    )
    rmdir /s /q "%KEEPDIR%"
    echo Kept     : save states, NVRAM, screenshots, HD textures, pipeline cache, ROM folders and settings files from the previous build
) else (
    echo Kept     : nothing - this is a fresh layout
)

copy /y "%BIN%\Supermodel-LegacyOG.exe" "%DIST%\%EXENAME%" >nul
if errorlevel 1 (
    echo [ERROR] Could not replace "%DIST%\%EXENAME%". Close the running emulator and rebuild.
    exit /b 1
)

rem The Vulkan build, from the CMake output. Same Data\ tree, no separate config:
rem switching executables is meant to be a copy-and-run decision, not a migration.
copy /y "%BIN_SDL3%" "%DIST%\%EXENAME_SDL3%" >nul
if errorlevel 1 (
    echo [ERROR] Could not replace "%DIST%\%EXENAME_SDL3%". Close the running emulator and rebuild.
    exit /b 1
)

rem The cold-path module. Kept beside the executable rather than in Data\: it is an
rem implementation detail of the Vulkan build, not user data, and the loader looks for it
rem next to the .exe first. It is self-contained: the zstd codec is the managed
rem ZstdSharp.Port compiled into the image, so no libzstd runtime DLL is shipped.
copy /y "%HOSTCORE_DLL%" "%DIST%\Model3VulkanHostCore.dll" >nul
if errorlevel 1 (
    echo [ERROR] Could not replace "%DIST%\Model3VulkanHostCore.dll". Close the running emulator and rebuild.
    exit /b 1
)

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
if exist "%ROOT%\Model3VulkanBezel.png"        set "BEZEL=%ROOT%\Model3VulkanBezel.png"
if not defined BEZEL if exist "%ROOT%\Assets\Model3VulkanBezel.png" set "BEZEL=%ROOT%\Assets\Model3VulkanBezel.png"

rem Any other .png in Assets\ ships as well. Done first, so an old bezel left there cannot overwrite
rem the master resolved below.
if exist "%ROOT%\Assets\*.png" (
    for %%F in ("%ROOT%\Assets\*.png") do if /i not "%%~nxF"=="Model3VulkanBezel.png" copy /y "%%~fF" "%DIST%\Data\Assets\" >nul
)

if defined BEZEL (
    copy /y "%BEZEL%" "%DIST%\Data\Assets\Model3VulkanBezel.png" >nul
    for %%B in ("%BEZEL%") do echo Bezel    : %%~nxB from %%~dpB  ^(%%~zB bytes^)
) else (
    echo Bezel    : not found ^(no backdrop will be drawn^)
)

rem Documentation travels with the build, but inside Data\ so the top level stays
rem a single file.
if exist "%ROOT%\Docs\LICENSE.txt" copy /y "%ROOT%\Docs\LICENSE.txt" "%DIST%\Data\" >nul
if exist "%ROOT%\Docs\README from The Supermodel Team.txt" copy /y "%ROOT%\Docs\README from The Supermodel Team.txt" "%DIST%\Data\" >nul

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
rem The upstream manual ("README from The Supermodel Team.txt") is on the skip list because it is
rem copied into Data\ above; copying it here as well would put it at the top level too.
for %%F in ("%ROOT%\Docs\*.txt") do (
    if /i not "%%~nxF"=="README from The Supermodel Team.txt" (
        if /i not "%%~nxF"=="LICENSE.txt" (
            if /i not "%%~nxF"=="README-PACKAGE.txt" (
                copy /y "%%~fF" "%DIST%\%%~nxF" >nul
            )
        )
    )
)

rem ---- English README at the top level -----------------------------------------
rem Shipped as README.txt: the conventional name, and the one someone who has just been handed a
rem folder looks for. README-PACKAGE.txt is the player-facing release page and the only source for it.
rem
rem Renamed rather than named README.txt in Docs\, because the upstream manual is now
rem "README from The Supermodel Team.txt" and belongs in Data\ with the rest of the reference
rem material. Two different files cannot share that name, and the manual is the one that must not move.
set "PACKAGE_README=%ROOT%\Docs\README-PACKAGE.txt"
if exist "%PACKAGE_README%" (
    copy /y "%PACKAGE_README%" "%DIST%\README.txt" >nul
    for %%R in ("%PACKAGE_README%") do echo Readme   : README.txt from %%~nxR  ^(%%~zR bytes^)
) else (
    echo Readme   : not found ^(Docs\README-PACKAGE.txt^)
)

rem The package README points at NOTICE.md for third-party attributions; keeping it beside
rem README.txt is what makes that pointer resolve without the source tree.
if exist "%ROOT%\NOTICE.md" (
    copy /y "%ROOT%\NOTICE.md" "%DIST%\NOTICE.md" >nul
    for %%N in ("%ROOT%\NOTICE.md") do echo Notice   : NOTICE.md  ^(%%~zN bytes^)
)

rem ---- User manual -------------------------------------------------------------
rem The manual is the "what can this do and how should I set it up" document, and it has to sit
rem beside README.txt rather than in Data\, where nothing ever opens it. The English file is the
rem reference; the Chinese one is the short version of the same thing.
for %%F in (usermanual.md usermanual_chs.md) do (
    if exist "%ROOT%\%%F" (
        copy /y "%ROOT%\%%F" "%DIST%\%%F" >nul
        echo Manual   : %%F
    )
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

rem ---- Optional: a clean release zip ------------------------------------------
rem The assembled folder is the thing to play from, and it deliberately keeps the player's save
rem states, NVRAM, screenshots, pipeline cache and ini files (see the KEEPDIR block above). None of
rem that belongs in a release: the saves are personal, the cache rebuilds itself, and shipping an ini
rem would present one machine's settings as if they were the defaults.
rem
rem So the package is a copy with those removed, zipped as
rem Model3-Vulkan.<version>.for.Windows.zip beside the distribution folder. Optional rather than
rem always-on because the rebuild-and-play loop wants the folder itself left untouched.
if not defined PACKAGE goto NoPackage

set "STAGE=%ROOT%\build-package\Model3-Vulkan"
set "ZIPFILE=%ROOT%\build-package\Model3-Vulkan.!VERSIONTEXT!.for.Windows.zip"
echo.
echo Packaging a clean release copy...
if exist "%ROOT%\build-package" rmdir /s /q "%ROOT%\build-package"
mkdir "%STAGE%" 2>nul

rem One line rather than caret continuations: robocopy inside a parenthesised block with line
rem continuations is a reliable way to produce a command cmd interprets differently than intended.
robocopy "%DIST%" "%STAGE%" /E /XD "%DIST%\Data\Saves" "%DIST%\Data\Saves_Legacy" "%DIST%\Data\Screenshots" "%DIST%\Data\NVRAM" "%DIST%\Data\Cache" "%DIST%\Data\Analysis" "%DIST%\Data\Assets\HDTextures" "%DIST%\Data\Assets\DumpTextures" /XF "*.log" "Supermodel.ini" "Model3-Vulkan.ini" "Supermodel-VK-SDL3.ini" "*.zip" /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo [ERROR] robocopy could not stage the release copy.
    exit /b 1
)

rem The runtime folders are recreated empty so the extracted tree has the same shape as the
rem assembled one. A zip may not store empty directories; the program creates them on demand either
rem way, so this is for the person who copies the staged folder instead of the archive.
for %%D in (Saves Saves_Legacy NVRAM Screenshots Cache Analysis) do mkdir "%STAGE%\Data\%%D" 2>nul

rem Progress reporting is suppressed: Compress-Archive draws a progress bar, and in this
rem non-interactive console the bar's arithmetic fails ("bottom cannot be greater than or equal to
rem top") and the command returns an error even though the archive was written. Deleting any partial
rem file first is the same reasoning as the staging folder above.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; if (Test-Path -LiteralPath '%ZIPFILE%') { Remove-Item -LiteralPath '%ZIPFILE%' -Force }; Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIPFILE%' -CompressionLevel Optimal"
if errorlevel 1 (
    echo [ERROR] Compress-Archive failed. The staged folder is still at "%STAGE%".
    exit /b 1
)
echo Packaged : %ZIPFILE%
for %%Z in ("%ZIPFILE%") do echo Size     : %%~zZ bytes

:NoPackage

rem ---- Report -----------------------------------------------------------------
echo.
echo Done. Model 3-Vulkan !VERSIONTEXT!
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
