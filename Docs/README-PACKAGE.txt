========================================================================
Model 3-Vulkan (v0.9.8.6)
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

  * Native Vulkan 1.3 3D Engine:
    - Accurate Real3D texture paging, hardware quad rasterization, and
      three hardware-accurate transparency passes.
    - Resolves 20-year-old transparency glitches: proper particle layering
      on tire smoke/exhaust in Daytona USA 2 & Scud Race, and fixes the
      blinding blizzard overexposure in Sega Rally 2.

  * Glitch-Free Decoupled Audio:
    - Real-time sound emulation running on a dedicated thread; moving/resizing
      windows or loading assets will never stutter or pop audio.
    - Permanently resolves the infamous Fighting Vipers 2 match-end DSP
      runaway loop screech (retiring the legacy-scsp workaround).

  * Modern Display Synchronization:
    - Arcade-exact 57.524 Hz logic pacing with silky-smooth frame delivery
      on modern VRR (G-Sync / FreeSync) and 120/144/240 Hz displays.

  * Visual Save State Engine (.zst):
    - 16 dedicated visual save slots per game with live thumbnail previews.
    - Next-gen Zstandard compression reduces save state sizes to ~1/6th of
      legacy formats (saving gigabytes on handhelds like Steam Deck).

  * Modern Presentation & Handheld Enhancements:
    - Full graphical UI with game list and instant settings adjustments.
    - True widescreen frustum expansion, authentic CRT beam/phosphor filters,
      integer scaling, and custom bezel backdrops.
    - Support for modern racing wheel force feedback and dual light guns.

------------------------------------------------------------------------
THE TWO EXECUTABLES
------------------------------------------------------------------------

  1. Model3-Vulkan.exe (Default / Recommended)
     - Requires: Vulkan 1.3 compatible GPU & drivers.
     - Features: Full modern renderer, high-refresh pacing, .zst visual saves,
       and in-game telemetry HUD.

  2. Supermodel-LegacyOG.exe (Fallback)
     - Requires: OpenGL 4.1.
     - Purpose: Lightweight standalone fallback build ensuring plug-and-play
       compatibility on older legacy GPUs (e.g., Intel 4th Gen Haswell).

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
  F9 / F10          Music volume Down / Up
  F11 / F12         Sound effects volume Down / Up
  Alt + S           Capture screenshot (saved to Data\Screenshots\)
  Alt + N           Clear current NVRAM

------------------------------------------------------------------------
DIRECTORY LAYOUT
------------------------------------------------------------------------

  roms\             Default directory for game ROM archives (.zip)
  Data\Config\      Configuration matrices, game lists, and button maps
  Data\Assets\      Box art, crosshair overlays, and bezel graphics
  Data\Saves\       Modern visual save states (.zst) and legacy imports
  Data\NVRAM\       Virtual arcade battery-backed NVRAM files
  Data\Screenshots\ High-resolution screenshots
  Data\Cache\       Pipeline and shader caches (auto-generated)
  Data\Supermodel.log Primary runtime diagnosis log
  Scripts\          Display mode helpers for arcade refresh calibration

------------------------------------------------------------------------
SYSTEM REQUIREMENTS
------------------------------------------------------------------------

  * Operating System: Windows 10 or Windows 11 (64-bit).
  * GPU (Vulkan): Vulkan 1.3 capable GPU (NVIDIA GTX 900+ / AMD RX 400+ /
    Intel Arc or Iris Xe; older Intel HD 500-class iGPUs do not provide
    Vulkan 1.3 and should use the OpenGL build).
  * GPU (OpenGL Fallback): OpenGL 4.1 compatible graphics adapter.
  * No external runtime dependencies required (statically linked).

------------------------------------------------------------------------
CREDITS & LICENSING
------------------------------------------------------------------------

Model 3-Vulkan is licensed under the GNU General Public License v3.0 (GPLv3).
See `Data\LICENSE.txt`, `Data\LICENSE-ADDITIONAL-TERMS.md`, `Data\TRADEMARK.md`
and `Data\NOTICE.md` for full licensing and component attributions.

This project stands upon the reverse-engineering foundations laid by
The Supermodel Team (Bart Trzynadlowski, Nik Henson, Ville Linde, Stefano
Teso, and contributors). Upstream repository: https://github.com/trzy/Supermodel
