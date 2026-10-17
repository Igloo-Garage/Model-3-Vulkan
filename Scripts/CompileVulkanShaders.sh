#!/bin/sh
# Compile the Vulkan3D GLSL shaders to checked-in SPIR-V headers.
# Requires glslangValidator or glslang on PATH, or $VULKAN_SDK/bin.
set -e
cd "$(dirname "$0")/.."
GLSLANG="${VULKAN_SDK:+$VULKAN_SDK/bin/glslangValidator}"
if [ ! -x "$GLSLANG" ]; then
  if command -v glslangValidator >/dev/null 2>&1; then
    GLSLANG=glslangValidator
  elif command -v glslang >/dev/null 2>&1; then
    GLSLANG=glslang
  else
    echo "glslangValidator not found" >&2
    exit 1
  fi
fi
DIR=Src/Graphics/Vulkan3D/Shaders
build() {
  src="$1"
  name="$2"
  # Target SPIR-V 1.5, which is what --target-env vulkan1.2 produces. Not vulkan1.3: that emits
  # SPIR-V 1.6, which needs Vulkan 1.3 or VK_KHR_maintenance4 to load, and the backend deliberately
  # also runs on 1.2 with the dynamic rendering and synchronization2 extensions enabled.
  "$GLSLANG" -V --target-env vulkan1.2 -o "$DIR/$name.spv" "$DIR/$src"
  python3 - "$DIR/$name.spv" "$DIR/$name.spv.h" "$name" <<'PY'
import pathlib, sys
spv, header, name = sys.argv[1:]
data = pathlib.Path(spv).read_bytes()
words = [int.from_bytes(data[i:i+4], "little") for i in range(0, len(data), 4)]
lines = ["#pragma once", "#include <cstddef>", "#include <cstdint>", f"static const uint32_t {name}_spv[] = {{"]
for i in range(0, len(words), 8):
    lines.append("".join(f"0x{w:08x}," for w in words[i:i+8]))
lines.append("};")
lines.append(f"static const size_t {name}_spv_size = sizeof({name}_spv);")
pathlib.Path(header).write_text("\n".join(lines) + "\n")
PY
}
build real3d.vert real3d_vert
build real3d.frag real3d_frag
build composite.vert composite_vert
build composite.frag composite_frag
build texdecode.comp texdecode_comp
echo "Shader headers written to $DIR"
