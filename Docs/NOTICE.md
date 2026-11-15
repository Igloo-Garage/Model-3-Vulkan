# Third-party notices

Model 3-Vulkan is distributed under the **GNU General Public License v3.0**; see
[`LICENSE.txt`](LICENSE.txt). It is a fork of the open-source
[Supermodel](https://github.com/trzy/Supermodel) project and includes the components listed below.
Each component remains under its own license; the original notices are kept in the corresponding
source tree.

Additional terms under GPL v3 Section 7 (author attribution and origin marking) apply to the
original Igloo-Garage modules; see [`LICENSE-ADDITIONAL-TERMS.md`](../LICENSE-ADDITIONAL-TERMS.md).
The project names ("Model 3-Vulkan™", "Igloo-Garage™"), icon, startup artwork and
cabinet bezel are trademarks and brand assets of **REXABYTE SOFTWARE INC.**
(British Columbia, Canada), covered by [`TRADEMARK.md`](../TRADEMARK.md) and not
licensed by the GPL.  The ™ symbol denotes an unregistered / common-law mark;
see that file for the full policy.

## Original standalone dual-licensed SDK modules (REXABYTE SOFTWARE INC.)

The following clean-room header-only modules were developed from scratch by **REXABYTE SOFTWARE INC.**
with zero code, types, or headers from upstream Supermodel. They are the exclusive intellectual property
of **REXABYTE SOFTWARE INC.** and are distributed under a **Dual License**:

1. **Commercial / Proprietary License (Paid Authorization Required)**: Any use, integration, or
   distribution of these modules in closed-source software, proprietary commercial products, paid
   applications, or commercial arcade hardware **strictly requires a prior written, paid commercial
   license from REXABYTE SOFTWARE INC.** (authorized for official REXABYTE SOFTWARE INC. commercial
   products, including the Android edition).
2. **Open-Source GPLv3 Grant**: Permitted for use in complete open-source GPLv3 distributions
   subject to mandatory GPLv3 Section 7 author attribution and origin marking terms
   (see [`LICENSE-ADDITIONAL-TERMS.md`](../LICENSE-ADDITIONAL-TERMS.md)). Any removal of attribution
   or closed-source commercial exploitation immediately terminates the open-source grant.

| Module | Path | Purpose |
| --- | --- | --- |
| **IglooAudioDSP** (`Igloo::AudioDSP`) | `Src/Sound/IglooAudioDSP.h` | $C^1$-continuous master soft-clipper (`MasterSoftClip`), bilinear-prewarped 2nd-order Butterworth LFE low-pass filter (`ButterworthBiquadLPF`), 4.0-to-5.1 surround upmixer (`UpmixQuadTo51`), 12-bit 4-tap Catmull-Rom cubic pitch interpolator (`CubicInterpolate4Tap`), and per-sample linear gain ramper (`LinearGainRamper`). |
| **IglooMultiChannelRecorder** (`Igloo::Audio`) | `Src/Sound/IglooMultiChannelRecorder.h` | Lock-free SPSC asynchronous multi-channel audio recorder (`MultiChannelRecorder<SampleT>`), 68-byte `WAVE_FORMAT_EXTENSIBLE` Float32/PCM16 writer, and built-in RFC 9639 lossless 24-bit/16-bit multi-channel FLAC encoder with `VORBIS_COMMENT` speaker masks. |
| **IglooChaseCamera** (`Igloo::Camera`) | `Src/Graphics/Vulkan3D/IglooChaseCamera.h` | Need-for-Speed style spring-damper horizontal yaw-lag chase camera stabilizer (`ChaseCameraStabilizer`), non-linear deadband integrator, view-space player-car pivot tracker, and frustum culling angular margin helpers. |
| **IglooInputFilter** (`Igloo::Input`) | `Src/Inputs/IglooInputFilter.h` | Non-linear center-precision gamma curve + exponential low-pass + slew-rate limiter for gamepad steering (`GamepadSteeringFilter`), and heuristic racing wheel classifier (`IsRacingWheelDeviceName`). |

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
