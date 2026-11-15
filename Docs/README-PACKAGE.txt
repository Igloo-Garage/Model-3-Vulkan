========================================================================
Model 3-Vulkan (v0.9.9.6)
Modernized Standalone Sega Model 3 Arcade Emulator
Igloo-Garage
REXABYTE SOFTWARE INC., British Columbia, Canada
========================================================================

Project Repository:  https://github.com/Igloo-Garage/Model-3-Vulkan
Latest Release:      https://github.com/Igloo-Garage/Model-3-Vulkan/releases/latest
Documentation:       usermanual.md (EN) / usermanual_chs.md (中文)

Model 3-Vulkan(TM) and Igloo-Garage(TM) are trademarks and brand assets of
REXABYTE SOFTWARE INC.  GPLv3 Section 7(e) grants no trademark rights.  See
`Data\LICENSE-ADDITIONAL-TERMS.md` and `Data\TRADEMARK.md`; a modified
redistribution must use a different name and icon and must state that it is
modified.

Model 3-Vulkan is a standalone modernization of the Sega Model 3 arcade
platform, introducing a native Vulkan 1.3 rendering backend, dedicated
audio threading, modern display synchronization, and a full graphical
launcher interface.

No copyrighted ROM files or BIOS dumps are included in this package.

------------------------------------------------------------------------
KEY HIGHLIGHTS & IMPROVEMENTS
------------------------------------------------------------------------

  * PowerPC 603e to x86-64 Hybrid JIT:
    - Hot PowerPC basic blocks are translated once into native x86-64
      and executed directly, with a 16 MB code cache, direct-memory
      windows, self-modifying-code page tracking and idle-loop skipping.
    - Verified in lock-step against the interpreter across 675 million
      blocks with zero mismatches and pixel-identical output.
    - Up to ~4.1x higher frame rates in CPU-bound scenes (224.5 Hz vs
      55.2 Hz in our test). The C interpreter remains one menu entry away.

  * Native Vulkan 1.3 3D Engine:
    - Accurate Real3D texture paging, hardware quad rasterization, exact
      quad interpolation and three hardware-accurate transparency passes.
    - Optional per-pixel transparency sorting (AlphaSortMode = 1) for
      smoke, spray, foliage and glass that the two-layer scheme cannot
      order correctly.
    - Two-subpass on-chip composite: 4K + 4x MSAA VF3 went from 226 to
      240-241 Hz, bit-identical at 1x.

  * Need for Speed-style Chase Camera:
    - Spring-damper horizontal yaw stabilisation around the player car's
      pivot, so gamepad micro-corrections no longer swing the whole
      horizon. Full-screen 3D HUDs (speedometer, route map, arrows) stay
      perfectly still. Off / Light / Standard / Strong.

  * Real Force Feedback & Wheel Calibration:
    - SDL3 haptics initialised correctly on Windows; raw direct-drive
      wheel bases (SIMAGIC, MOZA, Fanatec and similar) are recognised.
    - Constant-force sign/axis encoding fixed, 20 ms attack envelope,
      SDL_HAPTIC_SINE vibration with rumble fallback, hardware autocenter
      released to the game.
    - Live wheel/pedal deadzone and full-travel calibration table plus a
      one-click H-shifter neutral binding generator.

  * Cabinet-Grade Audio:
    - Native 4.0 Quad (default) and 5.1 Surround output, with a smart
      common-mode Center extractor and a 120 Hz second-order Butterworth
      LFE low-pass for the optional 5.1 upmix.
    - 4-tap Catmull-Rom cubic interpolation on non-FM SCSP voices, slave-
      SCSP FM cross-talk fix, dynamic envelope/LFO register updates.
    - 32-bit float DSB MPEG decoding into a 64-tap polyphase resampler
      with per-sample gain ramping, plus a C1-continuous knee soft-clipper
      on the master bus.
    - Built-in lock-free multi-channel WAV (32-bit float / 16-bit PCM)
      and FLAC (24-bit / 16-bit) recorder. Off by default.

  * Modern Display Synchronization:
    - Arcade-exact 57.524 Hz logic pacing with smooth frame delivery on
      VRR (G-Sync / FreeSync) and 120/144/240 Hz displays, plus VRR
      integer multiples (2x/3x/4x/8x) and F8 hot-switching.
    - Present-phase calibration cuts roughly 4.6 ms of end-to-end latency
      on a Fixed 120 Hz setup.

  * Visual Save State Engine (.zst):
    - Dedicated visual save slots per game with live thumbnail previews.
    - Zstandard compression keeps save states compact for handhelds.
    - Save states include the 53C810 PCI state and the simulated network
      board, so VF3 and Scud Race resume instead of stalling on the DMA
      chain.

  * HD Texture Packs:
    - PNG, WebP, DDS BC1/BC2/BC3/BC5/BC7 and .dds.zst replacement textures
      with bindless sampling, async decode and hot reload.
    - F4 toggles replacement/original; F3 rescans the pack while playing.

  * Modern Presentation & Interface:
    - Frosted-glass panels, three complete themes (warm light, warm dark,
      original flat), 63 WebP box arts and a cover preview.
    - True widescreen frustum expansion, CRT beam/phosphor filters,
      integer scaling, custom bezel backdrops and Sinden light-gun border.
    - Per-game audio balance, portable ROM folders, multi-mouse support
      and dual light guns.

------------------------------------------------------------------------
ONE EXECUTABLE
------------------------------------------------------------------------

  Model3-Vulkan.exe

  - Requires: a Vulkan 1.3 compatible GPU and driver.
  - Includes: the full Vulkan renderer, hybrid JIT, high-refresh pacing,
    .zst visual saves, the in-game Tab panel and the statistics HUD.
  - Statically linked: no SDL, Vulkan-loader or VC++ redistributable to
    install separately (a working Vulkan 1.3 driver is still required).

  Supermodel-LegacyOG.exe (the OpenGL fallback) is no longer part of the
  release package. The Vulkan build is the product.

------------------------------------------------------------------------
QUICK START (30 SECONDS)
------------------------------------------------------------------------

  1. Extract the entire archive to any writable folder.
  2. Place your game ROM archives (.zip) into the included `roms\` folder.
     (Or configure a custom folder later inside the settings window).
  3. Launch `Model3-Vulkan.exe`.
  4. Select a title from the game list and double-click (or click "Start Game").
  5. Press [Esc] during gameplay to return cleanly to the settings menu.

------------------------------------------------------------------------
DEFAULT HOTKEYS
------------------------------------------------------------------------

  Esc               Return to launcher / settings menu
  Alt + Enter       Toggle Fullscreen
  Alt + P           Pause / Resume
  Alt + R           Reset game
  Tab               Open in-game overlay settings panel
  ` (Tilde)         Toggle live hardware statistics HUD
  F5 / F6 / F7      Save state / Next save slot / Load state
  F8                Toggle presentation cadence (1x <-> 2x)
  F3 / Shift + F4   Reload HD texture pack (rescan the folder)
  F4                Toggle HD texture replacements / original textures
  F9 / F10          Music volume Down / Up
  F11 / F12         Sound effects volume Down / Up
  Alt + S           Capture screenshot (saved to Data\Screenshots\)
  Alt + N           Clear current NVRAM

------------------------------------------------------------------------
DIRECTORY LAYOUT
------------------------------------------------------------------------

  roms\             Default directory for game ROM archives (.zip)
  Data\Config\      Configuration matrices, game lists, and button maps
  Data\Assets\      Box art, crosshair overlays, HD textures, and bezel graphics
  Data\Saves\       Modern visual save states (.zst) and legacy imports
  Data\NVRAM\       Virtual arcade battery-backed NVRAM files
  Data\Screenshots\ High-resolution screenshots
  Data\Recordings\  Optional WAV/FLAC multi-channel audio recordings
  Data\Cache\       Pipeline and shader caches (auto-generated)
  Data\Model3Vulkan.log  Primary runtime diagnosis log
  Scripts\          Display mode helpers for arcade refresh calibration

------------------------------------------------------------------------
SYSTEM REQUIREMENTS
------------------------------------------------------------------------

  * Operating System: Windows 10 or Windows 11 (64-bit).
  * GPU: Vulkan 1.3 capable (NVIDIA GTX 900+ / AMD RX 400+ / Intel Arc or
    Iris Xe; older Intel HD 500-class iGPUs do not provide Vulkan 1.3).
  * No external runtime dependencies required (statically linked).

  Default state on a fresh install: audio recording, HD texture loading and
  dumping, raw texture/memory dumps, the statistics overlay, MSAA, CRT/glow
  filters and force feedback are all OFF. Hybrid JIT, 4.0 quad audio, Auto
  pacing with motion interpolation, chase-camera stabilisation and the
  frosted-glass interface are ON. See RELEASE_NOTES.md and usermanual.md
  for the full list.

------------------------------------------------------------------------
CREDITS & LICENSING
------------------------------------------------------------------------

Model 3-Vulkan is licensed under the GNU General Public License v3.0 (GPLv3).
See `Data\LICENSE.txt`, `Data\LICENSE-ADDITIONAL-TERMS.md`, `Data\TRADEMARK.md`
and `Data\NOTICE.md` for full licensing and component attributions.

This project stands upon the reverse-engineering foundations laid by
The Supermodel Team (Bart Trzynadlowski, Nik Henson, Ville Linde, Stefano
Teso, and contributors). Upstream repository: https://github.com/trzy/Supermodel
