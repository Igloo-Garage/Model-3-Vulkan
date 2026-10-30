#!/usr/bin/env bash
#
# syntaxcheck-linux.sh
#
# Compiles Model 3-Vulkan's sources with the host g++ for Linux, using -fsyntax-only. This is not a
# build: nothing is linked and no binary is produced. The point is to see the errors that only a
# non-MSVC compiler emits - the ones the CI finds one file per fifteen-minute run.
#
# Background: MSVC always has <windows.h> on the include path, so a Win32 identifier used outside an
# #ifdef _WIN32 guard compiles happily there and fails on gcc with "'HWND' was not declared in this
# scope". That is exactly how the Linux CI build broke while the Windows build stayed green.
#
# The SDL2, SDL2_net, GL and zlib headers are pulled out of Ubuntu packages into a scratch directory
# rather than installed, because installing needs root and this has to be runnable unattended.
#
# Usage, from Windows:
#   wsl -d Ubuntu -- bash /mnt/e/.../Scripts/syntaxcheck-linux.sh
#
# Exits non-zero if any file fails to compile.

set -u
# Without pipefail the status of "g++ ... | head -50" is head's, which is always 0, so a file that
# failed to compile would be reported as passing. Every failure below would have been silent.
set -o pipefail

# Tree under test. Pass a Windows path (E:/...) or a WSL path (/mnt/e/...).
# With no argument, use the repository this script lives in.
ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"

case "$ROOT" in
    [A-Za-z]:*)
        # E:/foo -> /mnt/e/foo
        WSL_ROOT="/mnt/$(printf '%s' "$ROOT" | cut -c1 | tr 'A-Z' 'a-z')/$(printf '%s' "$ROOT" | cut -c4-)"
        ;;
    *)
        WSL_ROOT="$ROOT"
        ;;
esac

if [ ! -d "$WSL_ROOT/Src" ]; then
    echo "Not a source tree: $WSL_ROOT" >&2
    exit 2
fi

HEADERS=/tmp/model3vulkan-headers
DEBS=/tmp/model3vulkan-debs

# The package set matters. libglu1-mesa-dev ships <GL/glu.h>, which on its own #includes <GL/gl.h>
# and stops there: gl.h lives in libgl-dev (a virtual package that resolves to libglvnd-dev), so
# omitting it fails on the very first file that pulls in glew. The first attempt did exactly that.
# libx11-dev is here because <SDL_syswm.h> pulls in <X11/Xlib.h> whenever SDL was built with the X11
# video driver, which is the case for the distro package. Xlib.h in turn includes <X11/X.h>,
# which is a protocol header shipped separately in x11proto-dev.
MISSING=0
for probe in usr/include/SDL2/SDL.h usr/include/GL/gl.h usr/include/GL/glu.h usr/include/zlib.h \
             usr/include/X11/Xlib.h usr/include/X11/X.h; do
    [ -f "$HEADERS/$probe" ] || MISSING=1
done

if [ $MISSING -ne 0 ]; then
    echo "Fetching SDL2 / GL / zlib headers into $HEADERS ..."
    mkdir -p "$DEBS" "$HEADERS"
    (
        cd "$DEBS" || exit 1
        apt-get download libsdl2-dev libsdl2-net-dev libglu1-mesa-dev libglvnd-dev \
            libgl-dev libglx-dev libx11-dev x11proto-dev zlib1g-dev >/dev/null 2>&1
    )
    for deb in "$DEBS"/*.deb; do
        dpkg-deb -x "$deb" "$HEADERS"
    done
fi

MISSING=0
for probe in usr/include/SDL2/SDL.h usr/include/GL/gl.h usr/include/GL/glu.h usr/include/X11/Xlib.h \
             usr/include/X11/X.h; do
    if [ ! -f "$HEADERS/$probe" ]; then
        echo "Missing header after extraction: $probe" >&2
        MISSING=1
    fi
done
if [ $MISSING -ne 0 ]; then
    echo "Could not obtain the GL or SDL2 headers." >&2
    exit 2
fi

cd "$WSL_ROOT" || exit 2

# Every directory under Src is on the include path, which is what Rules.inc does with
# $(foreach file,$(SRC_FILES),$(dir $(file))). The vendored libraries are added explicitly.
# libsdl2-dev's SDL_config.h is a one-line forwarder to SDL2/_real_SDL_config.h, and that file is
# installed to the multiarch directory rather than next to it. Without this include every file that
# touches SDL failed with "SDL2/_real_SDL_config.h: No such file or directory".
MULTIARCH="$HEADERS/usr/include/$(gcc -dumpmachine)"
INCLUDES="-I$HEADERS/usr/include -I$HEADERS/usr/include/SDL2 -I$MULTIARCH -I$MULTIARCH/SDL2"
for dir in $(find Src -type d); do
    INCLUDES="$INCLUDES -I$dir"
done
INCLUDES="$INCLUDES -ILibraries/Vulkan-Headers/include -ILibraries/zlib-1.3.1"

# The files a Linux build compiles, restricted to the ones that differ from upstream. An untouched
# upstream file that compiles in CI today cannot be what broke it.
FILES="${*:2}"

if [ -z "$FILES" ]; then
    FILES="Src/OSD/SDL/Main.cpp Src/OSD/SDL/Overlay.cpp Src/OSD/SDL/StatsOsd.cpp \
Src/OSD/SDL/Gui.cpp Src/OSD/SDL/Localization.cpp Src/OSD/SDL/Audio.cpp \
Src/OSD/SDL/SDLInputSystem.cpp Src/OSD/Unix/FileSystemPath.cpp \
Src/Inputs/Inputs.cpp Src/Inputs/InputSystem.cpp Src/Inputs/Input.cpp \
Src/Inputs/InputSource.cpp Src/Inputs/InputTypes.cpp Src/Inputs/MultiInputSource.cpp \
Src/Graphics/Render2D.cpp Src/Graphics/Vulkan3D/Vulkan3D.cpp \
Src/Graphics/Vulkan3D/Vulkan2D.cpp Src/Graphics/Vulkan3D/VulkanContext.cpp \
Src/Graphics/Vulkan3D/VulkanMemory.cpp Src/Graphics/Vulkan3D/VulkanRingBuffer.cpp \
Src/Graphics/Vulkan3D/VulkanTextureCache.cpp Src/Graphics/Vulkan3D/R3DSceneBuilder.cpp \
Src/Graphics/New3D/New3D.cpp Src/Pkgs/volk_impl.cpp"
fi

fail=0
failed_files=""
for file in $FILES; do
    # -Wno-... because those warnings are reported separately and the CI does not treat them as
    # fatal. -fsyntax-only keeps this to parsing and type checking: nothing is generated or linked.
    output=$(g++ -std=c++17 -fsyntax-only -DGLEW_STATIC -Wall -Wno-unused-variable \
                -Wno-unused-function -Wno-unused-but-set-variable \
                $INCLUDES "$file" 2>&1)
    status=$?
    if [ $status -ne 0 ]; then
        fail=1
        failed_files="$failed_files $file"
        printf '=== FAIL %s\n' "$file"
        printf '%s\n' "$output" | grep -E 'error:|fatal error' | head -25
    else
        # Compiled, but the warnings are still worth seeing: unused variables in the Windows-only
        # branches are how the "declared but never used" class of fault shows up.
        printf '=== ok   %s\n' "$file"
        printf '%s\n' "$output" | grep -E 'warning:' | head -10
    fi
done

echo
if [ $fail -eq 0 ]; then
    echo "All files compiled."
else
    echo "FAILED:$failed_files"
fi
exit $fail
