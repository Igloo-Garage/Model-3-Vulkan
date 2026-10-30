#!/usr/bin/env bash
#
# Assembles the relocatable Linux/macOS package from build-cmake/bin.
#
# The Windows package is build.bat's job. This mirrors its layout: the
# executable, the HostCore module, Data/, roms/, Scripts/, README.txt and the
# manuals at the top level. The macOS Frameworks directory (MoltenVK) is copied
# when the caller has put it in the repository root.
#
#   bash Scripts/package-cmake.sh Linux
#   bash Scripts/package-cmake.sh macOS

set -euo pipefail

platform="${1:-}"
case "$platform" in
    Linux|macOS) ;;
    *)
        echo "usage: $0 Linux|macOS" >&2
        exit 2
        ;;
esac

root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(grep -E '^#define[[:space:]]+MODEL3VULKAN_VERSION' "$root/Src/Version.h" | awk -F'"' '{print $2}')"
if [ -z "$version" ]; then
    echo "Could not read MODEL3VULKAN_VERSION from Src/Version.h" >&2
    exit 1
fi

stage="$root/Model3-Vulkan.v${version}"
archive="$root/Model3-Vulkan.v${version}.for.${platform}.tar.gz"

rm -rf "$stage" "$archive"
mkdir -p "$stage"/Data/{Config,Assets,NVRAM,Saves,Screenshots,Cache,Analysis} "$stage"/roms "$stage"/Scripts

cp "$root/build-cmake/bin/Model3-Vulkan" "$stage"/
if [ -f "$root/build-cmake/bin/libModel3VulkanHostCore.so" ]; then
    cp "$root/build-cmake/bin/libModel3VulkanHostCore.so" "$stage"/
fi
if [ -f "$root/build-cmake/bin/libModel3VulkanHostCore.dylib" ]; then
    cp "$root/build-cmake/bin/libModel3VulkanHostCore.dylib" "$stage"/
fi

cp -R "$root/Config/." "$stage/Data/Config/"
cp -R "$root/Assets/." "$stage/Data/Assets/"
cp "$root/Model3VulkanBezel.png" "$stage/Data/Assets/"
cp "$root/Docs/LICENSE.txt" "$stage/Data/"
cp "$root/Docs/README from The Supermodel Team.txt" "$stage/Data/"
cp "$root/Docs/NOTICE.md" "$root/LICENSE-ADDITIONAL-TERMS.md" "$root/TRADEMARK.md" "$stage/Data/"

cp "$root/Docs/README-PACKAGE.txt" "$stage/README.txt"
cp "$root/usermanual.md" "$root/usermanual_chs.md" "$root/supported-games.txt" "$stage"/

if [ -f "$root/Scripts/install-linux-deps.sh" ] && [ "$platform" = "Linux" ]; then
    cp "$root/Scripts/install-linux-deps.sh" "$stage/Scripts/"
fi

if [ "$platform" = "macOS" ] && [ -d "$root/Frameworks" ]; then
    cp -R "$root/Frameworks" "$stage"/
fi

printf 'Put Model 3 ROM set .zip files in this folder; the emulator finds it automatically.\n' \
    > "$stage/roms/README.txt"

chmod +x "$stage/Model3-Vulkan"

( cd "$root" && tar -czf "$archive" "$(basename "$stage")" )

echo "Package: $archive"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$archive"
else
    shasum -a 256 "$archive"
fi
