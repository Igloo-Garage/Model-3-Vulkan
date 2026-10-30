# Model 3-Vulkan Release Notes

## v0.9.8.6 (2026-10-29)

**Model 3-Vulkan v0.9.8.6** makes every hotkey configurable from the INI file, fixes input handling for mice, standalone shifters and button boxes, and adds a one-click automatic launch.

### 🎮 Input

* **Hotkeys in the INI** — every in-game hotkey is an `InputUI...` entry (`InputUIExit`, `InputUISaveState`, `InputUIToggleOverlay`, ...), so a front end such as RetroBat or a cabinet build can assign its own buttons. Chords use the normal binding syntax, e.g. `JOY1_BUTTON7+JOY1_BUTTON8`. The Controls page still neither lists nor rewrites these keys, so editing the interface - or pressing **Apply recommended bindings** - never clobbers what a front end wrote; a key left out keeps the built-in default and `NONE` disables one. The two-line patch was proposed by **Tartifless** over at RetroBat (RetroBat-Official/emulatorlauncher#1456) and is included as-is.
* **Binding a mouse axis works again** — a full-axis capture compared the pointer against the display centre and could keep both X and Y, producing `MOUSE_XAXIS+MOUSE_YAXIS`, which the input system rejects as an invalid combination; the capture then silently failed. Only the dominant axis (the larger displacement) is captured now, and a capture in progress no longer appends a second axis when the pointer or a resting pedal crosses the centre, so a full-axis binding always ends up as exactly one source.
* **Standalone shifters, handbrakes and >16-button devices work in the default input mode** — with `sdlgamepad` selected, SDL3 devices that do not identify as game controllers used to be skipped, so a MOZA H-shifter or a handbrake could not be bound at all. They are opened alongside the controllers now, `BUTTON17` and up are readable on any device, and standard pads keep exactly the button numbering they had.
* **The control-preset combo stays on what you picked** — selecting "Keyboard only" was replaced on the next frame by the value still in the INI.

### 🖥️ Interface

* **Auto-Launch on Startup** — right-click a row in the game list and choose it to start that game automatically the next time the program opens. The row is marked with `★`; **Clear Auto-Launch** on the same row undoes it. It fires once per run, so pressing `Esc` returns to the list rather than restarting the game, and starting with a ROM set on the command line is unaffected because that path never opens the list. Three rough edges found while testing that path are fixed too: the key no longer shows up as a stray untranslated text box on the Core page, launching straight into a starred game no longer flashes the settings window, and the menu entry is greyed out for sets that cannot start instead of silently doing nothing.

  ![Auto-Launch on Startup](https://raw.githubusercontent.com/Igloo-Garage/Model-3-Vulkan/v0.9.8.6/Docs/images/updates/auto-launch%20on%20startup.webp)
  ![Clear Auto-Launch](https://raw.githubusercontent.com/Igloo-Garage/Model-3-Vulkan/v0.9.8.6/Docs/images/updates/clear%20auto-launch.webp)

Compatibility: save states and NVRAM are unchanged; `SUPERMODEL_VERSION` is untouched.

## v0.9.8.3b Maintenance Release (2026-10-28)

**Model 3-Vulkan v0.9.8.3b** is a maintenance update for the v0.9.8.3a milestone. It fixes a regression that silenced sound effects in every title and completes the native DDS path for HD texture packs.

### 🔧 Fixes

* **Sound effects restored in every game.** A misplaced `else` in `CSoundBoard::RunFrame()` cleared the SCSP output buffers immediately after the sound board filled them, so only the MPEG music survived. Engine, tyre, collision and menu effects were silent on fresh boots and on loaded saves alike; they are back now. Music playback, the quad-to-stereo fold and existing save states are unaffected.

### 🖼️ HD Texture Packs

* **`.dds` / `.dds.zst` support** — BC1, BC2, BC3, BC5 and BC7 images go straight into VRAM with the mip chain from the file (no blit pass). Files are uploaded in resumable chunks, so a 4096×4096 BC7 chain no longer starves the staging ring.
* **Pack order is `.dds.zst` > `.dds` > `.webp` > `.png`** when several files carry the same hash.
* **F4 toggles replacements and F3 rescans the folder**, so a pack can be compared against the original artwork without opening the in-game panel.

### 🛠️ Under the hood

* `SM3D_AUDIO_LEVEL=1` now reports the source peak split into front and rear channels, which makes "the machine is playing but one channel is missing" visible in the log.
* The 3D scene targets are handed to the 2D compositor on every exit path (including the white-flash frame), and new targets are transitioned before the first composite, so `VUID-vkCmdDraw-imageLayout-00344` no longer appears after a flash frame.
* A same-layout write-after-write barrier was added for attachments cleared at the start of a scene pass, closing a cross-submit hazard visible to synchronisation validation.

Compatibility: save states written by v0.9.8.3a (and by the Supermodel-VK fork) load unchanged; `SUPERMODEL_VERSION` is untouched.

## v0.9.8.3a Milestone Release (2026-10-28)

**Model 3-Vulkan v0.9.8.3a** represents a historic architectural milestone in Sega Model 3 emulation. This release officially graduates the project from **Supermodel-VK** into **Model 3-Vulkan**, decoupling the modern rendering pipeline from legacy OpenGL into a dedicated, high-performance Vulkan 1.3 architecture.

---

### 🌟 Executive Summary

* **Project Graduation**: Transitioned to an independent, standalone Vulkan 1.3 and SDL3 architecture. Legacy OpenGL is cleanly preserved as a standalone fallback binary (`Supermodel-LegacyOG.exe`).
* **Real3D Hardware Simulation**: Completely resolved long-standing graphics artifacts, including Real3D hardware-accurate checkerboard transparency (Mode 2) and Quad pull-model SSBOs.
* **Native HD Texture Pack Engine**: Unlocked the first-ever native texture replacement pipeline for Sega Model 3 arcade games with automatic hardware Mipmaps and `textureGrad` bypass.
* **Audio Overhaul**: Decoupled the sound emulation into a dedicated real-time thread, permanently retiring the 6-year-old `LegacySoundDSP` workaround by correcting SCSP memory mapping.
* **Modern Storage & State Management**: Introduced visual 16-slot save state management with Zstandard (`.zst`) compression, slashing save state file sizes by ~83% (crucial for handhelds like the Steam Deck).
* **Display Pacing & Low-Latency Input**: Native 57.524 Hz arcade refresh rate with 120Hz/144Hz+ motion interpolation via `CDisplayPacer`, backed by an ultra-low latency SDL3 Raw Input and HIDAPI input foundation.

---

### 🎨 Visuals & Real3D Emulation Accuracy

#### 1. 100% Hardware Quad Pull-Model (SSBO)
* Completely eliminated geometry shaders. Quads are now pulled directly from shader storage buffer objects (SSBOs).
* Implemented an inverse-bilinear solver directly within the fragment shader to recover perspective-correct `(s, t)` coordinates and native quad interpolation.
* Preserves GPU Early-Z depth culling across all modern graphics pipelines, significantly boosting fill-rate efficiency on integrated and discrete GPUs alike.

#### 2. 4x EWA Anisotropic Filtering
* Engineered an elliptical weighted average (EWA) anisotropic filtering pipeline using a 2×2 Gram matrix eigenvalue extraction with dynamic tap calculation.
* Eliminates oblique texture shimmering and cross-seam edge bleeding along track boundaries and distant geometry.

#### 3. Groundbreaking 3-Tier Transparency Architecture
* **Mode 0: Bit-Exact Two-Pass (`AlphaSortMode = 0`)**:
  * Utilizes on-chip `subpassLoad` with `VK_ATTACHMENT_STORE_OP_DONT_CARE` tile memory access for optimal bandwidth efficiency on low-power and integrated GPUs.
* **Mode 1: Per-Pixel Sorted A-Buffer OIT (`AlphaSortMode = 1`)**:
  * Employs a 4-slot structure-of-arrays (SoA) A-Buffer backed by a 5-step constant-time sorting network (<0.75ms resolve at 4K MSAA).
* **Mode 2: Arcade Hardware Checkerboard (`AlphaSortMode = 2` — *World First*)**:
  * Faithfully revives Real3D’s 1996 single-pass 50% stipple dither mask and transparent Z-write depth competition.
  * **Fixes Blinding Exhaust & Tire Burnout Smoke**: Multi-layered smoke particles in *Daytona USA 2* and *Scud Race* no longer stack exponentially into solid, blinding white walls of milk; they now render with authentic arcade depth, proper particle layering, and natural falloff.
  * **Fixes Sega Rally 2 Blizzard Blowout**: Permanently resolves the 20-year-old snow/fog overexposure bug where player cars were swallowed into solid white voids.
  * **Eliminates Dirty Foliage Halos**: Cleans up black outlines and alpha blending seams on 3D trees and leaves in *The Lost World: Jurassic Park*.
  * *Note:* This is an experimental mode intended for dense snow, dust and fog; it cannot reproduce LOD cross-fades. Use Mode 0 or Mode 1 for scenes such as Daytona 2's waterfall.

#### 4. CRT Filter Refinement & Evolution Roadmap
* Beam-profile scanline integration eliminates moiré banding and brightness unevenness at 4K without requiring point-sampling approximations.
* *Roadmap Goal:* The internal CRT shader pipeline will continue to be optimized, aiming to rival leading heavyweight modern retro shaders out of the box with near-zero performance cost.

---

### 🖼️ Native HD Texture Pack Engine

* **Clean Sub-Texture Identification**: Real3D sub-textures are cleanly identified and dumped using a 64-bit coordinate and format hash `(page, x, y, w, h, format)`.
* **Zero Seam Bleeding**: Automatic hardware Mipmap generation combined with `textureGrad` bypass prevents edge bleeding and mipmap shimmer across multi-quad models.
* **Format Support**:
  * Native `.png` (lossless masters)
  * Native `.webp` (ultra-compact distribution sizes with full alpha channel support)
  * Hardware-direct **`.dds` (BC7 direct VRAM streaming)** is actively in development.
* **Modder-Centric Architecture**: Designed to be extensible for community upscalers and artists. Support for custom PBR material maps (normals, roughness, metallic) and emissive layers is planned based on community creator requests.

---

### 🔊 Audio Modernization & Sound Board Decoupling

* **Dedicated Real-Time Audio Threading**:
  * Decoupled Motorola 68000 CPU and SCSP/DSB sound emulation into an independent real-time audio thread.
  * Window movements, resizing, asset streaming, or background GPU frame pacing fluctuations will no longer cause audio stutter, crackle, or dropouts.
* **The "LegacySoundDSP" Mystery Solved**:
  * Fixed an architectural flaw dating back to 2020 where address range `0x7C0–0x7FF` (a reserved hardware hole) was incorrectly mapped as a mirror of `MADRS`.
  * Because *Fighting Vipers 2* uploads its DSP image sequentially with 32 zeros filling this hole, the emulator was inadvertently zeroing out the game's newly uploaded delay/offset table, causing an acoustic positive feedback runaway loop (cutting victory speech to "you wi…" followed by a 0dB screech).
  * Discarded writes to the reserved hole, corrected effect return volume to `EFSDL << 13`, patched an unsigned underflow in `SCSP_w32`, and **officially retired the `LegacySoundDSP` switch**.
  * Restores pristine, authentic audio to *Fighting Vipers 2*, *Virtua Fighter 3*, and *Sega Rally 2* out of the box with zero configuration hacks.

---

### 💾 Modern Save State Management (`.zst`)

* **16 Visual Slots per Game**:
  * Fully accessible via right-click on the game list or directly through the in-game overlay menu (`Tab`).
  * Each slot captures a high-resolution screenshot thumbnail for effortless visual identification.
* **Zstandard (`.zst`) Compression**:
  * Upgraded state serialization to use modern Zstd compression.
  * Slashes save file size to **roughly 1/6th of legacy uncompressed `.st` files** (reducing 30–50MB+ dumps down to 4–7MB). Eliminates storage bloat and flash-memory write wear on handheld devices like the **Steam Deck**.
* *Usage Pro-Tip:* For select titles with complex mid-stage memory transitions, restoring save states via the in-game quick menu (`Tab`) is recommended over desktop launcher cold-booting.

---

### 🖥️ UI, Multi-Monitor & Display Pacing

* **Intelligent Window Lifecycle & Dynamic DPI**:
  * Window dimensions are preserved seamlessly between sessions.
  * Moving the emulator window between primary and secondary monitors with differing screen resolutions or OS scaling factors (e.g., 4K 150% to 1080p 100%) dynamically updates without blurry text or cropped controls.
* **Warm Eye-Care Themes**:
  * Introduced three built-in visual styles (warm light, warm dark, and the original interface), with a custom **Warm Eye-Care Theme** active by default to filter blue-white glare during extended sessions.
  * Public community theming API planned for future releases.
* **Arcade-Exact 57.524 Hz Pacing (`CDisplayPacer`)**:
  * Delivers exact 57.524 Hz arcade refresh rate on VRR/G-Sync monitors.
  * Provides smooth, motion-interpolated presentation on 120Hz, 144Hz, and 240Hz high-refresh displays.
* **SDL3 Ultra-Low Latency Input Foundation**:
  * Fully migrated the Vulkan build to SDL3 input handling, retiring the legacy DirectInput and XInput backend split.
  * SDL3 gamepad handling uses HIDAPI polling, and a dedicated RawInput path provides isolated per-device mouse tracking for dual light guns. Hot-plugging is automatic.

---

### 🛠️ Additional Fixes in This Build

* **Scud Race save states (`scudplus.zst1`)**: Loading a car-select save no longer hangs. The saved MPEG window is validated (the `0x80008000` "no track" sentinel is rejected), a stalled decoder exits with silence instead of spinning, and the sound board re-drives its interrupt line on load, so effects work after restoring a state. New saves also carry a companion MPEG block, so background music resumes from the saved position; states written by older builds return the music when the game issues its next track command.
* **Quad-cabinet audio on stereo endpoints**: The mixer now reads the output device's real channel count. On a two-channel device the rear pair is folded into the front pair by the mixer itself, instead of SDL applying a different downmix while the statistics panel still reported four channels. This restores Scud Race's car-select effects and rear-channel sound effects on normal desktop speakers.
* **Input system default**: The Vulkan build now defaults to **SDL Gamepad** (`sdlgamepad`); legacy `xinput` values are migrated in memory, and controller detection only suggests a backend that actually exists in this build.
* **Low-resolution 3D rendering**: **Video → Picture scaling** now also offers **992×768 (2× native)** beside 496×384. The 3D scene renders at 992×768 and is upscaled onto the picture rectangle, while the 2D HUD keeps its own resolution. Takes effect next session.

---

### ⚙️ Dual-Binary Deployment

To ensure 100% hardware coverage, release archives provide two pre-compiled binaries:

1. **`Model3-Vulkan.exe` (Default)**:
   * Pure Vulkan 1.3 / SDL3 modern pipeline.
   * Optimized for modern dedicated GPUs (NVIDIA GeForce, AMD Radeon, Intel Arc) and Intel Iris Xe (Gen12+) integrated graphics. Older iGPUs such as Skylake and the HD 500 class should use the OpenGL fallback.
2. **`Supermodel-LegacyOG.exe` (Fallback)**:
   * Standalone SDL2 + OpenGL 4.1 legacy build.
   * Preserves compatibility for legacy hardware lacking modern Vulkan drivers (such as Intel 4th Gen Haswell).

---

### 🚀 Upcoming Milestones: Roadmap Preview

#### Part 1: Modern Peripherals Overhaul
* Plug-and-play DirectDrive and belt/gear Force Feedback telemetry for Fanatec, Moza, Simagic, Thrustmaster, and Logitech wheels.
* Native dual-lightgun support with off-screen reload detection for **Sinden, Gun4IR, Aimtrak, and Wiimote** hardware.
* Crowdsourced **Device Probe Tool** to capture community rig telemetry and generate native auto-mapping presets.

#### Part 2: Netplay 2.0
* Zero-config P2P online multiplayer via 6-digit room codes with automatic NAT punch-through.
* Asynchronous Star-Mesh UDP architecture: local simulation remains locked at 57.524 Hz / 120 Hz+ regardless of network latency.
* Drop-in / drop-out fault tolerance: disconnected racers safely coast to the roadside without crashing the active lobby.

---

### 🔗 Links & Community

* **GitHub Repository:** [https://github.com/Igloo-Garage/Model-3-Vulkan](https://github.com/Igloo-Garage/Model-3-Vulkan)
* **Subreddit:** [r/Model3Vulkan](https://www.reddit.com/r/Model3Vulkan/)
* **Support the Project:** [Ko-fi](https://ko-fi.com/igloogarage)
