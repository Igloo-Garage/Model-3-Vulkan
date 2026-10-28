# Third-party notices

Model 3-Vulkan is distributed under the **GNU General Public License v3.0**; see
[`LICENSE.txt`](LICENSE.txt). It is a fork of the open-source
[Supermodel](https://github.com/trzy/Supermodel) project and includes the components listed below.
Each component remains under its own license; the original notices are kept in the corresponding
source tree.

## Emulation core

- **Supermodel** — Sega Model 3 emulator by Bart Trzynadlowski, Nik Henson, Ville Linde,
  Stefano Teso and contributors. GPLv3.
- **Musashi** — Motorola 68000 core by Karl Stenerud, relicensed for this project with the
  author's permission. GPLv3-compatible.
- **Turbo68K** — 68000 core used by the DSB2 sound board. GPLv3.
- **PowerPC and Z80 interpreters** — part of the Supermodel core. GPLv3.

## Graphics, audio and file formats

| Component | Used for | License |
| --- | --- | --- |
| SDL2, SDL2_net | Windows/Linux/macOS window, input and legacy network transport | zlib |
| SDL3 | Vulkan build window, audio, input and haptics | zlib |
| zlib | PNG decoding and compressed save-state paths | zlib |
| libwebp | Cover artwork decoding (Windows WIC, macOS ImageIO, Linux libwebp) | BSD-3-Clause |
| minimp3 | DSB MPEG audio decoding | CC0 1.0 |
| Dear ImGui | Settings window and in-game panel | MIT |
| stb_truetype (via Dear ImGui) | Font rasterisation | MIT / public domain |
| tinyxml2 | `Games.xml` and `Music.xml` parsing | zlib |
| Info-ZIP unzip | ROM archive reading | Info-ZIP |
| GLEW | OpenGL entry points (LegacyOG build) | MIT / BSD-3-Clause |
| volk | Vulkan entry points | MIT |
| Vulkan-Headers | Vulkan API headers (vendored) | Apache-2.0 |
| glslang | Shader compilation at build time | BSD-3-Clause |
| ZstdSharp.Port | Managed zstd codec in the host core module | MIT |
| .NET runtime (Native AOT) | `Model3VulkanHostCore` cold-path module | MIT |
| MoltenVK | Vulkan on macOS (macOS package only) | Apache-2.0 |

## Artwork

- The cabinet bezel, start-up artwork and application icon are original artwork distributed with
  the project.
- Game cover images under `Data/Assets/Boxarts/` are included for identification in the game list.
  Rights remain with their respective owners; they are not part of the emulator's licensed source.

## Game data

No Sega Model 3 ROM sets, sample ROMs or MPEG audio ROMs are included or distributed. The emulator
runs only with game data supplied by the user.
