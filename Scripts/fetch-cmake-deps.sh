#!/usr/bin/env bash
#
# Restores the third-party sources the CMake build needs.
#
# The Windows scripts (Libraries/Get_*.cmd) cannot run on the Linux and macOS
# runners, so this is the same set of downloads expressed as shell. Versions
# must stay in step with the .cmd scripts and with Libraries/DIR.txt.
#
#   SDL3            3.2.30        static SDL3 for the Vulkan executable
#   Vulkan-Headers  1.3.296       headers the Vulkan cores include
#   zlib            1.3.1         vendored and compiled into supermodel_zlib
#   volk            1.3.296       only fetched when Src/Pkgs/volk is missing
#
# Libraries/ is ignored by git; the CI checks out a clean tree and runs this.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
libs="$root/Libraries"
mkdir -p "$libs"

fetch_zip() {
    local url="$1"
    local top="$2"
    local dest="$3"
    local tmp
    tmp="$(mktemp -d)"
    echo "Fetching $dest..."
    curl -fsSL --retry 5 --retry-delay 2 -o "$tmp/archive.zip" "$url"
    unzip -q "$tmp/archive.zip" -d "$tmp"
    rm -rf "$libs/$dest"
    mv "$tmp/$top" "$libs/$dest"
    rm -rf "$tmp"
}

fetch_zip \
    "https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.30.zip" \
    "SDL-release-3.2.30" \
    "SDL3"

fetch_zip \
    "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.296.zip" \
    "Vulkan-Headers-1.3.296" \
    "Vulkan-Headers"

fetch_zip \
    "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.zip" \
    "zlib-1.3.1" \
    "zlib-1.3.1"

if [ ! -f "$root/Src/Pkgs/volk/volk.h" ] || [ ! -f "$root/Src/Pkgs/volk/volk.c" ]; then
    fetch_zip \
        "https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.3.296.0.zip" \
        "volk-vulkan-sdk-1.3.296.0" \
        "volk-fetched"
    mkdir -p "$root/Src/Pkgs/volk"
    cp "$libs/volk-fetched/volk.h" "$root/Src/Pkgs/volk/volk.h"
    cp "$libs/volk-fetched/volk.c" "$root/Src/Pkgs/volk/volk.c"
    rm -rf "$libs/volk-fetched"
fi

test -f "$libs/SDL3/CMakeLists.txt"
test -f "$libs/Vulkan-Headers/include/vulkan/vulkan.h"
test -f "$libs/zlib-1.3.1/zlib.h"
test -f "$root/Src/Pkgs/volk/volk.h"

echo "Third-party sources are ready under $libs"
