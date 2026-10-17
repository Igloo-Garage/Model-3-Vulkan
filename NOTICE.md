# Copyright and Attribution

**Supermodel-VK** is a modernized Vulkan 1.3 architectural fork of the **Supermodel** Sega Model 3 arcade emulator.

- **Project Repository:** https://github.com/Igloo-Garage/Supermodel-VK
- **License:** GNU General Public License v3.0 or later (GPLv3+)

---

## 1. Upstream Emulation Core

Supermodel-VK builds upon more than two decades of groundbreaking hardware reverse-engineering conducted by **The Supermodel Team**:

- **Bart Trzynadlowski** — Project founder, PowerPC core, Real3D graphics, sound board architecture
- **Nik Henson** — Input subsystems, debugger architecture, build toolchains
- **Ville Linde** — Core emulation logic, MAME contributor
- **Stefano Teso** — Core systems and original development

Along with contributions from developers listed in `Docs/README.txt`.

> **Copyright (C) 2003–2026 The Supermodel Team.**  
> Licensed under the GNU General Public License v3.0 or later. Arcade hardware preservation of Sega's Model 3 platform exists today thanks to their initial reverse-engineering efforts.

---

## 2. Supermodel-VK Architecture & Additions

Copyright (C) 2026 The Supermodel-VK Authors. Licensed under GPLv3+.

This fork replaces legacy rendering pipelines with a modern C++23 / Vulkan 1.3 backend, decoupled multi-threaded subsystems, and hardware-synchronized presentation timing.

### Codebase Additions & Scope

| Component / Subsystem | New / Modified Files | Scope & Implementation Details |
|---|---|---|
| **Vulkan 1.3 Modern Renderer** | `Src/Graphics/Vulkan3D/` | Complete Vulkan core (`Vulkan3D`, `VulkanContext`, `VulkanMemory`, `VulkanRingBuffer`, `VulkanTextureCache`, `Vulkan2D`, `R3DSceneBuilder`, `VulkanCommon.h`, `BezelLayout.h`). ~6,400+ hand-crafted lines. |
| **Interactive Settings & OSD** | `Src/OSD/SDL/` | Persistent overlay UI, bilingual localization (`Localization.{cpp,h}`), statistics HUD (`StatsOsd.{cpp,h}`), and runtime state engine. ~3,900+ lines. |
| **Abstract Render Interface** | `Src/Graphics/IRender2D.h` | Decoupled 2D/3D interface contract laying foundation for cross-hardware rendering (`RenderPayload`). |
| **Vulkan Shader Pipeline** | `Src/Graphics/Vulkan3D/Shaders/` | Hand-written GLSL shaders (`*.vert`, `*.frag`, `*.comp`) compiled to SPIR-V headers via `glslang`. |
| **Upstream Core Modernization** | 68 modified files | Deep refactoring across `Main.cpp`, `Gui.cpp`, `Audio.cpp`, `SDLInputSystem.cpp`, `InputSystem.cpp`, `SCSP.cpp`, and `New3D.cpp`. |

### Headline Architecture Features

- **Vulkan 1.3 Dynamic Rendering Backend:** Low-overhead execution with per-frame fence-gated vertex arenas, eliminating VRAM leaks and visual artifacting during extended runtime sessions.
- **Physical Beam-Profile CRT Shader:** Integrated electron-beam optical simulation featuring physical scanline profiles, aperture/slot grille masks with subpixel alignment, and authentic phosphorescent bloom.
- **Hardware-Accurate Refresh Synchronization (57.5242 Hz):** Supports three dedicated presentation modes: Arcade Native (57.5242 Hz), Hardware VRR (G-Sync / FreeSync adaptive sync), and borderless windowed mode with live display driver telemetry.
- **Decoupled Multi-Threaded Audio Architecture:** Dedicated SCSP sound board thread with fractional per-frame sample synthesis and adaptive latency buffers.
- **Non-Destructive UI State Machine:** Resident configuration window that preserves running states, allowing seamless pause/resume without process termination.
- **Robust Peripheral Hot-Plugging:** Dynamic device reconciliation for modern wheels, flight yokes, and gamepads on the SDL input subsystem.
- **Bilingual Interface:** Native English and Simplified Chinese localization covering all runtime HUDs, settings panels, and input configuration matrices.

---

## 3. Engineering & Verification Methodology

To ensure architectural stability across diverse PC hardware, Supermodel-VK enforces an empirical **Hardware-in-the-Loop** testing standard. Code modifications are not accepted solely based on static diffs; each architectural change is verified against physical hardware behavior:

- **Hardware-Bound Frame Pacing Telemetry:** Raster cadence is benchmarked directly against physical monitor hardware refresh telemetry (G-Sync/VRR panel readouts) rather than synthetic internal frame clocks to guarantee true stutter-free frame delivery.
- **Stress-Tested Controller Hot-Plugging:** Dynamic device attach/detach lifecycles are validated during live gameplay to prevent poll-loop starvation, memory safety bugs, and cache sizing race conditions.
- **Explicit Lifecycle & Teardown Verification:** Strict resource teardown ordering guarantees clean Vulkan device, swapchain, and memory arena destruction, preventing exit crashes and hung handles.
- **Preserved Core Boundaries:** Modifications maintain strict separation from legacy, high-coupling legacy paths (e.g., preserving dedicated audio decoupling over upstream monolith audio merges).

---

## 4. Third-Party Components & Attribution

Supermodel-VK links against and redistributes specific third-party open-source libraries in accordance with their respective licenses:

| Component | License | Source / Upstream Location |
|---|---|---|
| **Dear ImGui** | MIT License | `imgui-master/`, `Src/Pkgs/imgui/` |
| **SDL2 / SDL2_net** | zlib License | Fetched via `Libraries/Get_SDL.cmd` |
| **zlib** | zlib License | Fetched via `Libraries/Get_ZLib.cmd` |
| **volk** | MIT License | `Src/Pkgs/volk/` |
| **Vulkan-Headers** | Apache 2.0 | Fetched via `Libraries/Get_Vulkan.cmd` |
| **glslang** | BSD 3-Clause / Apache 2.0 | Fetched via `Libraries/Get_Libs.cmd` |
| **GLEW** | MIT / Modified BSD | `Src/Pkgs/GL/` |
| **Musashi 68000 Core** | MIT License | `Src/CPU/68K/Musashi/` |
| **tinyxml2** | zlib License | `Src/Pkgs/tinyxml2.*` |
| **minimp3** | CC0 Public Domain | `Src/Pkgs/minimp3.h` |
| **minizip / ioapi** | zlib License | `Src/Pkgs/unzip.*`, `ioapi.*` |

All third-party licenses are compatible with the GNU GPL v3.0 and are preserved in their respective source trees.

---

## 5. Non-Affiliation & Trademark Disclaimer

- **No Proprietary Assets:** Supermodel-VK does not distribute ROM images, proprietary BIOS dumps, copyrighted disc images, or original game audio/graphic assets.
- **Trademarks:** *Sega*, *Model 3*, *Daytona USA*, *Virtua Fighter*, *Sega Rally*, *Scud Race*, *Fighting Vipers*, and related marks are registered trademarks of SEGA Corporation or their respective IP owners.
- **Educational & Preservation Use:** This software is an independent research project developed for digital hardware preservation and interoperability purposes under fair-use principles. Users must supply their own legally obtained ROM dumps to operate the emulator.
