# Model 3-Vulkan User Manual (Vulkan + SDL3)

> Applies to **v0.9.8.3b**. Every version the program shows - window title bar, console banner, save
> metadata - comes from `SUPERMODEL_FORK_VERSION` in `Src/Version.h`.
> This manual is about *how to use the emulator and how to set it up*. Measured rendering data lives in
> `sdl3_reports/`, the change history in `devdocs/CHANGELOG.md`, and troubleshooting patterns in
> `devdocs/FIXES.md`.

---

## Contents

1. [The two executables](#1-the-two-executables)
2. [Quick start](#2-quick-start)
3. [What this build gives you](#3-what-this-build-gives-you)
4. [Recommended settings](#4-recommended-settings)
5. [Hotkeys](#5-hotkeys)
6. [Save states: F5 / F6 / F7 and Manage Saves](#6-save-states-f5--f6--f7-and-manage-saves)
7. [Files and folders](#7-files-and-folders)
8. [Performance reference](#8-performance-reference)
9. [Troubleshooting](#9-troubleshooting)
10. [Known limitations](#10-known-limitations)
11. [Command line (optional)](#11-command-line-optional)
12. [Linux / macOS differences](#12-linux--macos-differences)

---

## 1. The two executables

The package ships two executables. **Their renderers are fixed at build time** — this is not a setting:

| | `Model3-Vulkan.exe` | `Supermodel-LegacyOG.exe` |
| --- | --- | --- |
| Platform layer | SDL3 | SDL2 |
| Renderer | **Vulkan 3D only** (needs Vulkan 1.3) | **OpenGL only** (New 3D / Legacy 3D, OpenGL 4.1) |
| Frame cadence | Fixed 57.524 Hz logic + high-refresh presentation (follow display / fixed rate / VRR multiples) + motion interpolation | Classic one-simulation-frame-per-present at 57.524 Hz |
| Transparency | Two-subpass on-chip composite (default) / per-pixel sorted (OIT) | Original New 3D path |
| Stats panel | Yes (cadence/interpolation rows only have real values here) | Yes, but no high-refresh cadence data |
| Save states | `Data/Saves/*.zst` (with thumbnails); can also read legacy `.st` | `Data/Saves_Legacy/*.st`; falls back to reading `Data/Saves/*.st` |
| When to use it | **Default choice** when the GPU/driver supports Vulkan 1.3 | No Vulkan 1.3, OpenGL only, or you want the old renderers / supersampling |

Both share `Data/Config/`, `Data/NVRAM/` and `Data/Assets/`. **Save folders are separate** (see section 7):
a save made in the OpenGL build does not appear as a card in the Vulkan build's Manage Saves dialog,
but "Import legacy .st" converts it.

The two builds also name themselves apart in the title bar, so a screenshot says which one is running:
`Model 3-Vulkan v0.9.8.3b - <game>` for the Vulkan build and `Supermodel-OpenGL v0.9.8.3b - <game>` for
the OpenGL one.

> The SDL3 build needs **Vulkan 1.3 core** (`dynamicRendering` and `synchronization2` are core 1.3
> features). If the machine cannot provide it, a message box names the GPU and version it found;
> in that case use `Supermodel-LegacyOG.exe`.

---

## 2. Quick start

1. Unzip the **whole folder** somewhere writable — the executable and the `Data/` and `Config/` folders
   beside it are one program; do not copy the exe on its own.
2. Run `Model3-Vulkan.exe`. The settings window opens directly and **no console window appears**.
3. Point "ROM directory" at the folder holding your game `.zip` files. If you put them in a
   `roms/` folder next to the executable (also accepted: `rom`, `romset`, `romsets`, any case), the
   emulator selects it automatically on the next start. The field can point anywhere else too.
4. Pick a game in the list (double-click also works). The box art and an English synopsis appear above;
   the status bar at the bottom shows how many ROM sets were found.
5. Press **Start Game**. `Esc` in game returns to the settings window — no need to restart the program.

**Before you start:** 64-bit Windows 10 or 11. The SDL3 build needs Vulkan 1.3; the OpenGL build
needs OpenGL 4.1. Nothing else has to be installed — the C++ runtime and SDL are linked in, and the
only external binary is `vulkan-1.dll`, which the graphics driver provides. The extracted folder is
relocatable (settings and saves move with it), and the first run creates `Data/Config/Model3-Vulkan.ini`
(the OpenGL build keeps `Data/Config/Supermodel.ini`).

---

## 3. What this build gives you

### 3.1 Vulkan 3D renderer

- **Real3D texture pages and mip packing** are decoded the way the hardware lays them out, so distant
  scenery and backgrounds no longer render black. All three transparency formats the hardware uses
  (T1RGB5 with the contour bit, interleaved A4L4, RGBA4) are implemented, and transparent texels are
  discarded so they cannot pollute the depth buffer.
- **Coplanar decals and an overlay pass**: sponsor logos, numbers and light details no longer z-fight
  with the paint underneath.
- **Quad interpolation**: primitives are submitted as 4-vertex quads instead of 6-vertex triangle lists
  (−33% vertices), and the fragment shader intersects the bilinear surface with a view-space ray.
  At 4K with MSAA 4x: 119.9 → 120.0 FPS, and decals/edges are more accurate.
- **Two-subpass on-chip compositing** is the default transparency path: priorities 0–3 are drawn in one
  pass and the transparent layers are blended inside it. On the VF3 beach at 4K 4x this went from
  **226 → 240–241 Hz (+6%)**, pixel-identical to the old path at 1x (≤1 LSB), and it fixed the loss of
  the last priority's transparency under 4x MSAA (Daytona 2 car shadows / waterfall).
- **Optional per-pixel sorting (OIT)**: within each priority, transparent fragments go into a
  fixed-slot A-buffer (4 layers × 8 bytes per pixel) and are composited far-to-near. Still 120 Hz at
  4K + MSAA 4x; costs about 118 MB of VRAM at 2K and 253 MB at 4K. Fixes more-than-two-layer overlaps
  (tyre smoke, splashes, foliage) and submission order that disagrees with depth.
- **Hardware MSAA 2x / 4x / 8x**, applied to polygon coverage only, so textures and decals stay sharp.
  This also fixed the dark seam dots along polygon edges.
- **Pipeline cache** persisted to `Data/Cache/pipelines-{ui,game}.bin`: 20 ms cold → 8 ms warm.
- **Native crosshair drawing** for light-gun games: median `poll-to-render` went from **77.6 → 13.2 µs**.
- **Validation tooling**: `Tools/Validate-Vulkan.ps1` runs the synchronization / GPU-assisted matrices
  (both clean: 0 VUID, 0 hazard).

### 3.2 Presentation cadence: no more judder on a high-refresh panel

This is the most noticeable feature. Model 3 logic runs at **57.524 Hz**, while displays scan at
60/120/144/240 Hz, so a one-to-one mapping must beat. The emulator now **fixes the logic clock at
57.524 Hz and follows the display**, filling the sub-frame gap with motion interpolation. The controls
are on the **Video** tab, under **Presentation cadence**:

- **Follow the display (recommended)**: adapts automatically when you change monitor, refresh rate, or
  windowed/fullscreen mode — nothing to configure.
- **Fixed present rate**: 60 / 75 / 90 / 120 / 144 / 165 / 240 / 480 Hz. For a locked fullscreen mode or a
  fixed CPU/GPU cost. Rates above what the display reports are greyed out; a saved value is clamped at
  start-up.
- **VRR integer multiples**: 2x = 115.05 Hz, 3x = 172.57 Hz, 4x = 230.10 Hz, 8x = 460.19 Hz. Every
  simulation frame is scanned exactly N times and alpha is pinned to `k/N` by a phase stepper, so the
  cadence cannot be uneven. Requires G-SYNC/FreeSync and V-Sync on, with roughly 2% headroom
  (2x needs ≥120 Hz, 3x ≥176, 4x ≥235, 8x ≥470). Unsupported steps are greyed out, and an ini value is
  clamped to the fastest one the panel can take.
- **F8** toggles 1x ↔ 2x in game for an A/B on the same scene.
- **Present phase calibration** measures how far past a vblank the submission lands and shifts the phase
  by up to one vblank. With Fixed 120 this turned `0/121 confirmed` into `121/121`, about −4.6 ms
  end-to-end.
- **Cost of interpolation**: the picture is one simulation frame behind (≈17.4 ms) but every present is a
  new frame. Turning interpolation off (`LowLatency`) repeats each frame instead — lowest latency, but
  the stepping is visible on a fast panel.

### 3.3 Picture enhancements

- **Widescreen**: widens the view frustum horizontally so the scene genuinely extends outwards instead of
  being stretched. Vertical field of view is untouched, so the middle of the picture is unchanged. It only
  affects the full-screen viewport, and a 4:3 window has no extra width to use.
- **Widescreen Background**: stretches the game's own 2D background across the side bars.
- **Cabinet backdrop (bezel)**: built-in cabinet artwork or your own PNG, brightness adjustable, drawn
  only in the side bars. Skipped automatically while Widescreen is on, because there are no bars left.
- **CRT filter**: scanlines integrated analytically per output pixel (no moiré at 4K), three phosphor
  masks (aperture grille / slot mask — what Sega's cabinets used / dot triad), multi-scale glow from the
  hardware mip chain, brightness compensation and highlight roll-off. Four **presets that cannot beat**
  are provided, plus a live readout of scanline and mask pitch that warns about colour banding.
- **FXAA**: very cheap post-process anti-aliasing for integrated graphics; prefer MSAA on a discrete GPU.
- **Integer scaling (pixel-exact)**: the 2D layer is a fixed 496×384 texture, so only whole-number scaling
  keeps every texel the same size. Presets 1x–8x (1080p → 2x, 1440p → 3x, 2160p → 5x), with a live
  readout of the multiple actually in effect and what the next step would need.
- **Low-resolution 3D rendering**: rasterises the 3D scene at **496×384** or **992×768** and upscales it
  onto the picture rectangle; the 2D HUD keeps its own resolution. 992×768 is twice native — sharper
  than 496×384, still far cheaper to fill than the picture resolution. It takes precedence over
  integer scaling, which is greyed out while it is on.
- **Texture filtering, three modes**: Hardware (radial estimate, as the real hardware) / Sharp
  (screen-space derivatives) / Sharp + 4x anisotropic.

### 3.4 Audio

- **One SCSP DSP path**: the `0x7C0–0x7FF` hole is no longer mirrored into `MADRS` — that mirroring made
  Fighting Vipers 2's effect channel self-oscillate (the loud buzzing).
- **DSB (MPEG music)**: 32 kHz → 44.1 kHz via a 64-tap polyphase sinc (320:441), which removed the
  per-frame boundary clicks.
- **Per-game default volumes**: `scud/scudplus` effects 65%, `daytona2/dayto2pe` 70%, `swtrilgy` music
  70%, `spikeout/spikeofe` 75%. Applied only when you have not set the volumes yourself.
- **Asynchronous sound-board thread** by default; in the high-refresh path the sound board runs inside the
  fixed logic step instead, so two clocks cannot drift apart.
- **Audio buffer 400 ms** by default (`AudioLatency=40`, a percentage).

### 3.5 Input (some of this is untested)

Everything below is implemented and works in the paths that have been used. The items marked
**(untested)** were written without the hardware to try them on — no wheel, no light gun, no Sinden
border and no second machine were available — so treat them as "should work" rather than "verified".
Gamepads are the exception: they are in daily use here (an Xbox-layout pad and a Switch Pro Controller).

- **SDL3 gamepads**: hot-plug, standard button names, Xbox / PS / 8BitDo detection, separate triggers.
- **Wheel force feedback (untested)** through SDL Haptics with a 20 ms constant-force smoothing
  envelope. The master switch is on the Common page.
- **Gamepad steering smoothing**, four steps (off / light / medium / strong). A stick is a step input
  while an arcade wheel has mechanical damping.
- **Two light guns (untested)**: devices are distinguished by `SDL_MouseID`, and on Windows the
  platform layer registers raw input itself so both guns see separate motion.
- **Sinden border (untested)**: width and brightness adjustable, drawn after the cabinet backdrop and
  before the crosshair.
- **Per-game control list**: the Controls tab shows only the actions the selected game uses; double-click
  a row to rebind.
- **Networking (untested on two machines)**: native sockets (WinSock / BSD), and SDL2 and SDL3 builds
  can link to each other. Daytona 2's single-machine self-link goes through an in-process loopback and
  never touches a socket — that path is exercised; linking two machines is not.

### 3.6 Interface and tooling

- Chinese and English, switchable while the program runs; settings carry hover explanations, and
  many of them state the cost and a recommended value.
- Game list with region/version, box art and an English synopsis. A missing box art is cached as missing;
  adding the file later needs a restart.
- **Manage saves** shows `.zst` saves as thumbnail cards with Load / Rename / Delete. **Import legacy .st**
  scans both `Data/Saves` and `Data/Saves_Legacy`, takes the newer file per slot, and never overwrites a
  newer `.zst`.
- No black window at start-up: the window is created hidden, the artwork is shown, and the window is
  revealed once the first frame has been presented — for the settings window and for the game window.
- Window position and size are remembered; the minimum window is **1280×720 logical pixels**; crossing a
  DPI boundary re-derives the minimum and re-scales the interface.
- **Stats panel** (`` ` `` key): renderer, sync method, present rate vs panel rate, render resolution,
  cadence (whether it is a whole multiple), the CPU and GPU model names, **CPU average and peak core
  clock**, GPU load (and clock/VRAM on NVIDIA), RAM used, the **audio buffer fill as a two-second range
  plus the sound board's L/R peak level**, under/over-run counts, FPS, frame time, 1% low,
  interpolation state.
- Screenshots with `Alt+S` into `Data/Screenshots/`.
- **HD texture packs** (Core tab, and the Tab panel while playing): **Load HD texture pack** reads
  replacements from `Data/Assets/HDTextures/<romset>/`; **Dump this game's textures** exports every
  sub-texture the game draws to `Data/Assets/DumpTextures/<romset>/` as an RGBA8 PNG whose name carries
  its content hash. PNG, WebP and **DDS** (BC1/BC3/BC5/BC7, with or without a mip chain, optionally
  zstd-compressed to `.dds.zst`) are accepted at any scale (2x, 4x, 8x ...), with a full 8-bit alpha
  channel, hardware mip chains and 16x anisotropic filtering. DDS blocks and their mip chain are
  uploaded untouched, so they cost less video memory and load faster; when one hash has several files
  the priority is `.dds.zst` > `.dds` > `.webp` > `.png`, and a GPU without BC compression skips the
  DDS files and keeps using PNG/WebP. Textures without a replacement keep using
  the original. **F4** switches the replacements on and off while playing, which is the quickest way
  to compare a pack against the original artwork; **F3** (or **Shift+F4**) and the panel's Reload button rescan
  the folder without restarting, and a texture that finishes dumping can be upscaled and dropped
  straight into `HDTextures/<romset>/` under the same name. While the panel is open the emulator is
  paused, so the reload applies when you close it.
- **Appearance** (Common → Appearance, right under Language): three interface themes, **Light (eye-care,
  default)**, **Dark** and **Original interface**, applied immediately. The first two use neither pure white
  nor pure black - the light theme is a warm alabaster with terracotta accents and charcoal ink, the dark
  one a warm rock grey with amber accents and off-white text. Panel backgrounds are a **dual-Kawase
  frosted blur** of the frame behind them (half resolution, four passes, run only while a panel is open)
  with a 1px specular rim; the light theme lifts the blurred picture toward warm white so it reads as
  vellum rather than as a grey smear. **Original interface** is the flat warm-paper look from before the
  glass rework - opaque cream panes, a blue accent, no backdrop picture - kept for anybody who preferred
  it, so nothing taken away is lost. The same section has **Frosted glass**, which can be turned off for
  an opaque fill - the "ultra-low" hardware preset turns it off for you, and the original theme is opaque
  by design so the switch is greyed out there.

---

## 4. Recommended settings

### 4.1 Three steps

1. **Common → Hardware presets** (just above the Video options): pick a tier (see 4.2).
2. **Video → Presentation cadence**: leave it on **Follow the display (recommended)**. If the display
   supports VRR and runs at 120 Hz or more, try **2x**.
3. **Common → Fullscreen**, then **Fullscreen refresh mode**:
   - G-SYNC/FreeSync available: `Exclusive fullscreen - G-SYNC/VRR` (the panel follows the present rate).
   - No VRR but the display can do 57/58 Hz: `Exclusive fullscreen - arcade exact 57.524 Hz` (needs a
     custom timing).
   - Not sure, or want the simplest option: borderless fullscreen. "Follow the display" already removes
     the judder.

**Refresh rate and G-SYNC setup**

- On a **60/75/90 Hz** display, **Follow the display** is all you need. 75/90 Hz are followed and
  interpolated at the panel's own rate; there is nothing extra to configure. 75/90 Hz is already
  noticeably smoother than 60 Hz (more presents per logic step), while 120 Hz and up still adds
  visible sub-frame smoothness.
- On a **high-refresh** display, set the monitor to **120/144/240 Hz in Windows or the GPU control
  panel first**, then let Follow the display follow it. Follow the display uses the **current mode**; it
  cannot raise the desktop rate by itself. **Fixed present rate only changes the pacer target and does
  not switch the monitor mode.**
- For **G-SYNC/FreeSync**, open **NVIDIA Control Panel → Display → Set up G-SYNC → Enable G-SYNC**
  (enable it for fullscreen and/or windowed as needed), and keep **V-Sync on**. The VRR 2x/3x/4x/8x
  modes also require this.

### 4.2 Hardware presets (one click)

**Common → Hardware presets**, just above the Video options. The combo writes several related keys at
once; the same picture choices are also available individually below on the page.

| Tier | Resolution | MSAA | Transparency sorting | Texture filtering |
| --- | --- | --- | --- | --- |
| **Ultra low / fastest (496×384, integrated)** | 496×384 + integer scaling | Off (1×) | Original (two layers) | Hardware (radial) |
| **Integrated / low-end (1080p, 1x)** | 1920×1080 | Off (1×) | Original (two layers, bit-exact) | Sharp (screen-space derivatives) |
| **Entry discrete (1080p, 2x)** | 1920×1080 | 2x | Original (two layers) | Sharp |
| **Mid-range discrete (2K, 2x)** | 2560×1440 | 2x | Per-pixel sorted (OIT) | Sharp + 4x anisotropic |
| **High-end discrete (4K, 4x)** | 3840×2160 | 4x | Per-pixel sorted (OIT) | Sharp + 4x anisotropic |

- The **Ultra low** tier is a whole-system preset, not only the four columns: it also selects **Original
  57.524 Hz**, turns motion interpolation, FXAA, CRT and glow off, and disables **Exact quad
  interpolation** (the fast hardware-triangle path). It is aimed at Intel HD 530 / UHD 620 class
  integrated GPUs; non-planar quads can show a diagonal seam.
- On a 1080p panel the integer multiple is **2x**, so the visible area - and the 3D render size - is
  **992×768**, not 496×384 stretched. **Integer scaling is what keeps the low resolution alive:** with
  it off, fullscreen renders at the panel resolution fitted to 4:3 (about **1395×1080** on 1080p),
  roughly twice the pixels, which is why the frame rate drops there and the tier reads **Custom**.
- The four GPU tiers set only the four columns. **Exact quad interpolation** stays on by default and
  lives under **Core → Advanced**.
- Resolution is the **window / render size**, clamped to the current display, and takes effect **next
  session**.
- In fullscreen the render size is the display resolution, so this row only matters in windowed mode.
- Whether the picture is pixel-exact is decided by **Integer scaling**, which uses multiples of 496×384
  and ignores the window size.
- If the mid-range tier is too slow: **set Transparency sorting back to Original first, then drop MSAA one
  step**. That order gives the most frames back.

> Rough GPU mapping (not a requirement): integrated = Intel Iris/UHD or AMD Vega APU; entry discrete =
> GTX 1650 / RTX 3050 / RX 6400 class; mid-range = RTX 3060–4060 Ti / RX 6600–7600; high-end =
> RTX 4070 Ti and up / RX 7800 XT and up. Judge by the frame rate you actually get.

### 4.3 Choosing a presentation cadence

All of this is on the **Video** tab, under **Presentation cadence**.

| Option | Present rate | Requires | Latency | Best for |
| --- | --- | --- | --- | --- |
| Original 57.5 Hz | 57.524 Hz | A 57.5 Hz display mode or VRR | Lowest (no interpolation) | Arcade-exact comparison |
| **Follow the display (recommended)** | Panel refresh rate | Nothing | One simulation frame behind with interpolation | Almost everyone |
| Fixed present rate | 60/75/90/120/144/165/240/480 | Nothing | Same as above | Locked fullscreen, fixed cost |
| VRR 2x / 3x / 4x / 8x | 115.05 / 172.57 / 230.10 / 460.19 | VRR + V-Sync + panel headroom | One frame with interpolation; lowest without | High-refresh + VRR, perfectly even |

- With "Follow the display", one logic step maps to about 2.09 presents at 120 Hz, 2.50 at 144 Hz and
  4.17 at 240 Hz. Those non-integer ratios are exactly what interpolation solves.
- Setting a fixed rate **below** the panel only repeats frames; it is not "faster". Setting it **above**
  the panel drops the presents that miss a scan-out. Fixed rates the display does not report are greyed
  out, and a saved value above the panel's maximum is clamped at start-up.
- **Leave V-Sync on** (FIFO). The VRR multiples require it, otherwise presents get quantised back to the
  panel's own rate.
- **Fixed present rate only changes the pacer target; it does not switch the monitor mode.** If the
  desktop is at 60 Hz, selecting 75/90/120 does not make the panel scan at that rate - set the mode in
  Windows or the GPU control panel first. On an ordinary 60/75/90 Hz display, Follow the display is all
  you need.
- For G-SYNC/FreeSync, enable **NVIDIA Control Panel → Display → Set up G-SYNC → Enable G-SYNC** and
  keep V-Sync on (see 4.1).
- To compare latency, press **F8** in game to flip 1x ↔ 2x on the same scene.

### 4.4 Video options, one by one

| Setting | Default | Recommendation | Notes / cost |
| --- | --- | --- | --- |
| MSAA | Off | Integrated: off. Entry/mid: 2x. High-end: 4x | Coverage only, textures stay sharp; 8x needs driver support and is expensive at 4K |
| Texture filtering | Hardware (radial) | **Sharp** on any GPU; "Sharp + 4x anisotropic" on high-end | Not original hardware behaviour: sharper, and a few surfaces can shimmer slightly |
| Transparency sorting | Original (two layers) | **Per-pixel sorted (OIT)** on a discrete GPU | Needs a **new session**; extra VRAM (≈118 MB at 2K, ≈253 MB at 4K) |
| Arcade checkerboard | — | Only for dense dust, spray or snow | **Experimental.** Third choice in the same list: each transparency layer covers half the pixels, as the board does, so effects come out at half strength instead of turning solid where both layers overlap. It **cannot** reproduce the two-layer composite for LOD cross-fades: Daytona 2's waterfall comes out as a flashing blue block (20.1% of the fall's pixels versus 4.9% on the original path). Use Original or OIT for waterfalls, water sheets and similar scenes. Softer translucency, and slightly slower than Original |
| FXAA | Off | Integrated may use it; on a discrete GPU prefer MSAA | Very cheap, but softens the whole picture |
| Integer scaling | Off | On if you want pixel-exact output | 1080p → 2x, 1440p → 3x, 2160p → 5x; with it **off**, fullscreen renders at the panel resolution (≈1395×1080 at 1080p), which is much heavier |
| Widescreen | Off | On for a 16:9 display you want filled | Widens the frustum, no stretching; the backdrop is skipped while on |
| Widescreen Background | Off | On if you use Widescreen but not a cabinet image | Independent of Widescreen: with Widescreen off it still fills the bars with the game's own 2D background (blue in Daytona 2). Turn it off for black bars |
| Cabinet backdrop | Built-in image | Keep if you like the arcade look | Only drawn in the side bars; brightness adjustable |
| CRT filter | Off | 1080p: **Scanlines**; 4K: **Scanlines + Phosphor Mask** | Use **Recommended preset**; hand-tuning both pitches easily beats into colour bands |
| Mask type | Slot mask | Slot mask to match Sega cabinets | Aperture grille = Trinitron-like, dot triad = ordinary shadow mask |
| FPS in title | Off | On while tuning | Written into the window title once a second, with audio under/over-run counters. Nothing is drawn over the picture; the title bar is hidden in exclusive fullscreen. The stats panel shows FPS too, but not the audio counters |
| Stats panel | Off | On when diagnosing | Four styles: Amber (recommended) / Slate / Contrast / Outline |
| Motion interpolation | On | On | Off = lowest latency, but frames repeat on a fast panel |
| Interpolation scope | Auto | Auto | Auto interpolates the full-screen viewport and moving small ones (mirrors); "All viewports" forces every 3D viewport; "Main viewport only" leaves the rest discrete. 2D layers are never interpolated |

Presentation cadence, Fixed present rate, VRR multiples, Motion interpolation and Interpolation scope
are all on **Video → Presentation cadence**. Exact quad interpolation (Vulkan only) is under
**Core → Advanced**.

**The three texture filtering modes, and what they look like on screen**

| Mode | What decides the mip level | What changes |
| --- | --- | --- |
| Hardware (radial, as the real hardware) | The polygon's distance estimate - the same isotropic rule Real3D used | The arcade picture. Large flat surfaces blur a little earlier than they need to |
| Sharp (screen-space derivatives) | The uv footprint of the pixel, which quad interpolation makes exact | Roads, walls and floors running away from the camera keep the detail their pixels can carry. On Daytona 2's far wall / billboard region at 4K with MSAA 4x, regional high-frequency energy rose from 9.39 to **10.48** |
| Sharp + 4x anisotropic | The same footprint, plus up to four samples along the direction the surface is stretched | Removes the directionality the Sharp mode still has: a long oblique road stays crisp instead of blurring along its length. Same measurement: **12.10** |

The metric is "how much fine detail survives in that region", so higher is sharper; it says nothing about
any other scene. The anisotropic mode costs about 0.05 ms/frame at 4K (four taps across two mip levels)
and measured 119.3 Hz present against 120.0 for the other two - enough to lose a frame at the very top
of a 120 Hz budget.

Neither Sharp mode is original hardware behaviour: both are sharper than the arcade, and where the
hardware's blur was hiding aliasing a surface can shimmer slightly. That is why the default is
Hardware - and why the four GPU tiers switch it to Sharp, where the extra sharpness is the point. The
ultra-low tier leaves it on Hardware (radial), which is the cheapest lookup.

### 4.5 Audio

- Sound and music each 0–200%, default 100% (Common page).
- **Audio buffer** (`AudioLatency`, 5–100%): the default **40 → 400 ms**. Dropping to 20 (200 ms) lowers
  latency but under-runs more easily when the machine hitches; the title bar's `audio uN` counter shows it.
- Keep "Game sound effects" (SCSP) and "MPEG music" (DSB) enabled.
- Per-game defaults (see 3.4) apply only while you have not changed the volumes yourself.
- Daytona 2's background music is quiet by design; music at 150% or more is reasonable there.

### 4.6 Input and force feedback

- **Gamepads**: the SDL3 build offers two input systems, **SDL Gamepad** (recommended) and **SDL**.
  SDL Gamepad uses SDL's standard mapping, so the layout matches Xbox/PS pads and the two triggers are
  separate axes; SDL (raw joystick) pairs the triggers onto one axis and exists for older wheels and
  sticks. A configuration written for the OpenGL build — which names XInput or DirectInput — is switched
  to SDL Gamepad automatically, with a line in the log. Hot-plugging works with either.
  Buttons are matched by **position**: Back/Select/Share = coin, Start/Options/Menu = start/confirm.
  8BitDo pads work in every mode under SDL; their L2/R2 are buttons rather than analogue triggers.
  Changing the input system renames the trigger axes, so check the bindings on the Controls tab
  afterwards (pressing the controller preset again resets them to a known layout).
- **Gamepad steering smoothing**: medium is the right default for a pad in a racing game; keep it off with
  a real wheel or direct-drive base.
- **Force feedback**: tick the master switch on the **Common** page first, then tune the sliders on the
  **FFB** tab. Without the master switch nothing on that page does anything (the page says so).
  Some ROM sets have no drive-board program, so that game has no force feedback at all.
- **Two light guns**: two separate mouse devices (or one mouse plus one gun device). With a single device
  the crosshair follows the system cursor. The device count is cached when a session starts, so plugging
  a gun in mid-game needs a new session.
- **Sinden border**: for light-gun cabinets; drawn after the backdrop and before the crosshair.

**Not verified on hardware:** wheel force feedback, two-gun setups, the Sinden border and the link
between two machines are implemented but have never been tried on the matching hardware (see 3.5).
The settings exist and are wired to the SDL paths the platform layer exposes; the first report from a
real wheel, Sinden gun or second machine will be the first confirmation.

### 4.7 Save states

- **Save** mid-race, mid-fight, or on a stable menu. **Do not** save on load/transition/score/K.O.
  screens: such a state can be parked in a cross-frame handshake and may hang when restored.
- **Manage saves → Load** lets the game boot normally for about **3 seconds of emulated time**, covering
  the screen with the start-up artwork, then applies the state and takes the artwork down. The result is
  identical to loading with F7 during play.
- Do not treat loading as "skip the boot": `-load-state` follows the same 3-second path.

### 4.8 Networking (currently mainly Daytona 2)

- Two machines: each machine's "local port" and "peer port" are each other's, and "peer address" is the
  other machine's IP.
- **Local loopback link** (one machine, two instances): one button sets both ports equal and the address
  to `127.0.0.1`.
- SDL2 and SDL3 builds can link to each other (same protocol).
- **Untested**: only the single-machine loopback has been exercised here. Two-machine linking is
  implemented (native sockets, same wire format as the SDL2 build) but has not been tried.

---

## 5. Hotkeys

| Key | Action |
| --- | --- |
| `Esc` | End the session and return to the settings window |
| `Alt+P` | Pause / resume |
| `Alt+R` | Reset (soft reset; NVRAM is kept) |
| `Alt+Enter` | Toggle fullscreen |
| `F5` | Save a state to the current slot |
| `F6` | Change save slot (1–16; selects the next empty slot) |
| `F7` | Load the current slot; if it is empty, load the newest save |
| `F8` | Toggle presentation cadence 1x ↔ 2x |
| `F4` | Switch HD texture replacements on/off (compare against the original artwork) |
| `F3` / `Shift+F4` | Rescan the HD texture pack (`Data/Assets/HDTextures/<romset>/`) |
| `F9` / `F10` | Music volume − / + |
| `F11` / `F12` | Effects volume − / + |
| `Alt+S` | Screenshot into `Data/Screenshots/` |
| `Alt+N` | Clear NVRAM (restore factory settings for this game) |
| `Alt+I` | Cycle the light-gun crosshair style |
| `Alt+T` | Toggle frame limiting |
| `Alt+U` / `Alt+O` | Dump input state / frame timings (diagnostics) |
| `Tab` | In-game settings panel — **opening it pauses the emulator**; closing resumes |
| `` ` `` (above Tab) | Toggle the stats panel |

Changes made in the in-game panel are written to the ini only when you press **Apply**; **Cancel**
restores them. The panel's Save/Load also cancel a pending Manage Saves load and take its cover down
immediately.

---

## 6. Save states: F5 / F6 / F7 and Manage Saves

- **Format**: the Vulkan build writes `Data/Saves/<set>.zst<slot>`, a zstd container that includes a
  480×360 thumbnail. The same state is about 31 MB as a plain `.st` and about 7 MB as `.zst`. Legacy `.st`
  files are still readable, and the in-game menu prefers `.zst`.
- **Per-set isolation**: saves are named after the set that is actually running (identified from the zip
  contents by CRC), not after the zip file name. A `scud.zip` that contains `scudau` saves as `scudau.*`.
- **F6 and F7 mean different things**: F6 selects the *next empty slot* (so F5 creates a new save); F7
  falls back to the **highest-numbered existing save** when the current slot is empty. That is why F7
  works right after a restart.
- **LegacyOG** writes `.st` to `Data/Saves_Legacy/` and still falls back to reading `Data/Saves/*.st`.
- **Manage saves** filters cards by the selected set and offers Load / Rename / Delete. Deleting removes
  both the `.zst` and any `.st` with the same slot; the slot is left as a hole (no renumbering) and the
  next F5 reuses it.
- **Import legacy .st** scans `Data/Saves` and `Data/Saves_Legacy`, converts the newer file per slot, and
  never overwrites a newer `.zst`.

---

## 7. Files and folders

| Path | Contents |
| --- | --- |
| `Data/Config/Model3-Vulkan.ini` | Vulkan build settings (the OpenGL build has its own ini) |
| `roms/` | ROM set `.zip` files; also `rom`, `romset` and `romsets` are scanned, any case |
| `Config/Games.xml` | Definitions for 63 ROM sets (file names/CRCs, inputs, drive board) |
| `Data/Saves/` | `.zst` saves of the Vulkan build (legacy `.st` may also live here) |
| `Data/Saves_Legacy/` | `.st` saves of the OpenGL build |
| `Data/NVRAM/` | Per-set NVRAM / bookkeeping |
| `Data/Screenshots/` | `Alt+S` screenshots |
| `Data/Cache/` | Pipeline caches `pipelines-ui.bin` and `pipelines-game.bin` |
| `Data/Assets/Boxarts/` | Box art WebP per set (sources in `SOURCES.txt`) |
| `Data/Supermodel.log` | Runtime log (shared by both executables; the later one overwrites) |
| `Scripts/` | `ArcadeDisplay.ps1` and its two launchers - the 57/58 Hz display helper (7.1) |

- To keep logs apart: `-log-output=Data/xxx.log`.
- Any game can have its own `[setname]` section in the ini to override global settings (volumes, keys…).
- The folder is relocatable: settings and saves move with it. The first run creates
  `Data/Config/Model3-Vulkan.ini`, migrating the previous `Supermodel-VK-SDL3.ini` (or
  `Supermodel.ini`, if that is all that exists) so existing settings are kept. After that the ini
  records everything the interface changes.

### 7.1 The ArcadeDisplay helper in `Scripts/`

`Scripts/ArcadeDisplay.bat` (English) and `Scripts/ArcadeDisplay-zh.bat` (Chinese) are launchers for
`Scripts/ArcadeDisplay.ps1`. Double-clicking either with no arguments lists every video mode the
display offers and flags the ones that can carry the arcade's 57.524 Hz:

```
ArcadeDisplay.bat                                list the modes (same as -Action List)
ArcadeDisplay.bat -Action Save                   save the current display settings
ArcadeDisplay.bat -Action Restore                put them back
ArcadeDisplay.bat -Action Run -Game "E:\roms\daytona2.zip"
                                                 save, start that game, restore when it exits
```

Other arguments: `-Monitor N` (Windows' display number), `-Hz 57.5242`, `-Width` / `-Height`,
`-KeepMode` (do not restore afterwards), `-ExtraArgs "..."` (passed through to the emulator) and
`-SupermodelExe <path>` (default: `Model3-Vulkan.exe` beside `Scripts\`; pass
`Supermodel-LegacyOG.exe` for the OpenGL build). `-NoPause` closes the window without waiting for a key.

Why it exists: Windows composites a borderless window at the desktop refresh rate, so a 60/144 Hz
desktop cannot scan at 57.524 Hz and a 1:1 mapping is impossible. Running in exclusive fullscreen with
a real 57/58 Hz mode is the only clean fix, and on most drivers that mode has to be created once by
hand (NVIDIA: Display → Change resolution → Customise). `-Action List` tells you whether one exists.

You do not need the helper if the display supports VRR: use **Follow the display**, or the emulator's
own **Exclusive fullscreen - arcade exact 57.524 Hz** setting, which applies the mode itself.

---

## 8. Performance reference

Measured in `sdl3_reports/` on the same machine (not theoretical):

| Item | Result |
| --- | --- |
| Two-subpass on-chip composite (default transparency path) | VF3 beach at 4K 4x: 226 → **240–241 Hz** (≈+6%); bit-identical at 1x |
| Quad interpolation | −33% vertices; 4K MSAA 4x: 119.9 → **120.0 FPS** |
| Per-pixel sorted transparency (OIT) | Same **120 Hz** as the two-layer path at 4K + MSAA 4x (the linked-list version managed 27–37 Hz and was dropped) |
| High-refresh interpolation (Follow display, 120 Hz) | Logic 57.5 / present 120.0; present path 8.35 → **0.97–1.20 ms/frame** |
| VRR 2x | Measured `115.05 / 57.53 Hz`; interpolated frames 0.27–0.35 ms/frame |
| Present phase calibration | Fixed 120: `0/121` → **`121/121 confirmed`**, ≈−4.6 ms |
| Pipeline cache | 20 ms cold → **8 ms** warm |
| End-of-frame spin (SDL3) | Process CPU 13.17 → **11.03 s per 20 s** (≈−10.7% of one core), zero audio under-runs |
| Native crosshair | Median `poll-to-render` **77.6 → 13.2 µs** |
| "Sharp + 4x anisotropic" texture filtering | About **+0.05 ms/frame** at 4K |

---

## 9. Troubleshooting

**Regular stutter / judder**
Check **Video → Presentation cadence**: use Follow the display; on a high-refresh VRR panel try 2x. Make
sure V-Sync is on. With G-SYNC/FreeSync, enable it in **NVIDIA Control Panel → Display → Set up G-SYNC
→ Enable G-SYNC**. On a high-refresh display, set the monitor to 120/144/240 Hz first; Follow the
display follows the current mode, and Fixed present rate does not switch the mode.

**Not enough frame rate (4K + OIT + 4x MSAA)**
Lower in this order: Transparency sorting → Original; MSAA → 2x; Texture filtering → Sharp; only then
reduce resolution.

**Is the CPU or the GPU the limit? (how to read the stats panel)**
Turn the panel on (`` ` ``) and look at two lines:

- **GPU load near 100%** → the GPU is the limit. Lower resolution first, then MSAA, then texture
  filtering. CPU clock is irrelevant here, so do not bother tuning it.
- **GPU load low, one core busy** → the CPU is the limit. The emulator's PowerPC interpreter runs on a
  single thread, so what matters is that one core, not the total load percentage.

Read the CPU line as a pair: **`3666 MHz peak 4092`** means the cores average 3.67 GHz while the
busiest one reaches 4.09 GHz. The average is dragged down by idle cores on a laptop, so judge the CPU
by the **peak** figure. If the peak stays near the base clock (1.6 GHz for an i5-8365U, 1.8 GHz for an
i7-8550U) while one core is at 100%, the machine is thermally or power limited: plug in the charger,
set Windows to **Best performance**, and set the vendor tool (Lenovo Vantage, Armoury Crate, …) to its
performance cooling mode. On a 15 W laptop this is the ceiling — a sustained 4 GHz is not available,
no matter what the tooltip says.

> The GPU line needs a source. On NVIDIA the figures come from the driver (clock, load, VRAM). On
> Intel and AMD only the **load** is available, from the same Windows counters Task Manager uses, so
> the line shows a percentage with no clock or VRAM. `SM3D_STATS_GPU=pdh` or `nvml` forces one source
> and the log names the one in use.

**After loading a save the characters do not move and the timer creeps up**
That was the old behaviour of applying a state **before the game had finished booting** (logic ran at
about 1/25 speed). It is fixed: the state is now applied after the boot has completed, with the artwork
covering those three seconds. If you still see it, check the log for
`loading deferred state ... after N emulated frame(s)` with N close to 172.

**A loaded save is stuck on one picture**
The save point itself was probably on a load/transition/score screen. Save again at a stable moment.

**A quad-cabinet game is quiet, or only the music is audible**
Scud Race, Daytona USA 2 and Sega Rally 2 put part of their sound on the cabinet's rear pair. This
build asks the output device for its real channel count: on a stereo endpoint the rear pair is folded
into the front pair by the mixer itself (the stats line then reads `2 ch`) instead of being left to
the audio library, so effects that live on the rear channels are still audible.

A save written before 0.9.8.3a does not carry the MPEG playing window. The car-select background music
of such a save stays silent after loading it until the game issues its next track command; saving
again with this build produces a state that resumes the music from its saved position.

**No sound**
Press F11/F12 first; then look for a leftover `Audio: output muted.` in the log (the panel's pause mutes
the output) and check that game's `SoundVolume` / `MusicVolume`. The stats panel's `Audio` line also
says `MUTED` while the output is disabled, and its peak pair stays non-zero if the machine is still
playing, which is the quickest way to tell a silent session from a muted one.

**The Audio line in the stats panel**
`Audio  112-198 / 400 ms  4 ch  L 38%  R 41%  u0/o0` means the queue held between 112 and 198 ms over
the last two seconds, in a 400 ms buffer, on four output channels, with no under- or over-runs. The
first figure is the one to watch: a minimum falling towards 0 means the emulator is falling behind the
sound card (the next thing you hear is a stutter), while sitting at the maximum means latency has
built up. One under-run during boot is normal — the game produces no sound until its sound board
starts — a steadily climbing count is not.

The `ch` figure is what the mixer is producing, not what the settings request. When the output device
has fewer channels, the rear pair is folded into the front pair and the line reports the reduced
count. Four-channel endpoints keep the front/rear split.

The `L`/`R` pair is the peak the sound board produced in the same window, as a percentage of full
scale. It is measured before the mute and before the volume/balance stages, so it answers “is the
emulated machine playing sound”: a rolling `L 0%  R 0%` in a game that should be noisy points at the
emulation, while a healthy level with `MUTED` next to it points at the output. Figures above 100% are
possible — the SCSP and the DSB are summed before the output clamp — and mean that mix is running hot.

**The startup splash does not appear**
The artwork is an embedded WebP. Older builds decoded it only through Windows' WIC codec, which needs
the Microsoft Store “Webp Image Extensions” package; without it the splash was skipped. The current
build falls back to its bundled libwebp decoder, so if the splash is still missing check the log for
the `Splash:` lines (the failure case names the artwork and says it could not be decoded).

**A gamepad does nothing, or the triggers are swapped**
Set the input system to **SDL Gamepad** (Common page), then check the bindings on the Controls tab.
Trigger axis names differ between input systems. The SDL3 build has no separate XInput/DirectInput
backend; Xbox pads are read through SDL Gamepad, with the two triggers on separate axes.

**No force feedback**
Tick the master switch on the Common page. Some ROM sets ship without a drive-board program (the log
says `No 'driveboard_program' ROM`), and those games have no force feedback at all.

**Box art does not appear**

Three causes, in order of likelihood:

1. **The file cannot be decoded.** Covers are `.webp`. Windows reads them through WIC, which only knows
   WebP when the **Webp Image Extensions** package from the Microsoft Store is installed; when it is
   missing the emulator falls back to a libwebp decoder compiled into the executable, so the Store
   package is not required. If the log still says `Boxart: '<set>' could not be decoded`, both decoders
   rejected that file - it is damaged or not really a WebP. For diagnostics,
   `SM3D_BOXART_DECODER=wic` or `SM3D_BOXART_DECODER=webp` forces one decoder; the log then says
   `decoded with the bundled libwebp decoder` for the fallback.
2. **The folder is not where the executable expects it.** Covers are read from
   `<folder containing the exe>\Data\Assets\Boxarts\<set>.webp` - not from a fixed path, so the whole
   folder can be moved anywhere. The log prints `Boxarts: N file(s) in '<path>'` on the first lookup;
   if that path is not where you put the files, or N is 0, that is the answer.
3. **The set has no cover.** A missing file is cached as missing, so restart the program after adding
   one; the log then names the file it looked for (`Boxart: '<set>' not found`).

**What does "Vulkan validation layers" do?**
It is the Vulkan SDK's checker for the renderer itself, not a picture option. With it on, every draw
call, descriptor binding and synchronisation is verified, and mistakes are written to
`Data/Supermodel.log` as `Vulkan: ...` or `Vulkan warning: ...` lines (after 1000 messages the rest
are suppressed). Expect a large slowdown, so leave it off for play. To report a rendering problem:
enable it, start the game, reproduce the problem, close the game, and send the log. It applies when
a Vulkan instance is created, so the next game session picks it up; the settings window's own
instance keeps the value it started with until the program is restarted. If the Vulkan SDK (or at
least `VK_LAYER_KHRONOS_validation`) is not installed, the log says `validation layer unavailable,
retrying without it` and the emulator runs normally. `-vk-validation` is the command-line form.

**A message box says Vulkan 1.3 is not available**
Update the graphics driver. If the machine still cannot provide it, use `Supermodel-LegacyOG.exe`.

**Linux: the program exits at start-up while loading a font, or Chinese text is missing**
The interface uses a system CJK font, and the rasteriser bundled with ImGui reads TrueType outlines
and OpenType/CFF but **not** the CFF2 table of variable fonts. Every candidate is now checked before
it is used, so an unsupported font can no longer take the program down: the log names it
(`GUI: skipping font '<path>' - ...`) and the next candidate is tried. If no usable font is found,
the interface falls back to the built-in face, which has no Chinese glyphs, and says so.

To fix it, install a non-variable CJK font — for example `fonts-wqy-microhei` (Debian/Ubuntu),
`wqy-microhei-fonts` (openSUSE/Fedora), or the non-VF `Noto Sans CJK`. The line
`GUI: interface font '<path>'` in the log names the face actually in use, which is the quickest way to
confirm the fix.

---

## 10. Known limitations

- **Per-instance interpolation is off**: camera interpolation works at high refresh, but individual moving
  objects are not interpolated. Enabling it produces single-frame hairline cracks where a snapped instance
  meets an interpolated one, so it has no UI switch.
- **OIT needs a new session**, and costs about 253 MB of VRAM at 4K.
- **8x MSAA** needs driver support and is expensive at 4K.
- **Supersampling belongs to the OpenGL build**; the Vulkan backend forces 1x (the log says so).
- **Not every moment can be saved**: states captured on load/transition/score screens may hang when loaded.
- **Some ROM sets lack the drive-board program** (for example `vf3tb` is missing `epr-18022.ic2`): no force
  feedback, and the log says so explicitly.
- **`scuddx` (Scud Race DX) HUD viewport position** still has a known offset, to be fixed.
- **Box art and game-list changes need a restart**: missing-art caching and list metadata are built at
  start-up.
- **No Store package is required for covers.** Windows tries WIC first and falls back to the bundled
  libwebp decoder if the Webp Image Extensions package is absent.
- **Force feedback, two light guns, the Sinden border and the two-machine link are untested on real
  hardware.** They are implemented, and the code paths are the ones the platform layer exposes, but this
  build has only been exercised with ordinary gamepads and a single machine.

---

## 11. Command line (optional)

The settings window covers almost everything; these exist for scripts and diagnostics:

```
-gui                         Open the settings window (also the default with no ROM argument)
<romset>                     Start that set directly
-load-state=<file>           Start and load a state (same 3-second deferred path as Manage Saves)
-res=<x>,<y>                 Render resolution
-fullscreen / -borderless    Fullscreen / borderless
-wide-screen                 Widen the view frustum
-vsync / -no-vsync           V-Sync on / off
-no-throttle                 Disable frame limiting
-ppc-frequency=<MHz>         Emulated PowerPC frequency
-print-games                 List supported sets and quit
-print-gl-info               Print OpenGL information and quit
-log-output=<file>           Log destination
-log-level=<level>           Log threshold (info / debug / warn / error)
-sound-volume=<%> -music-volume=<%>
-audio-device=<name> -channels=<n>
-vulkan3d -vulkan-device=<name> -vk-validation
```

`-vk-validation` is the command-line form of the "Vulkan validation layers" switch; see
Troubleshooting for when to use it.

For timing experiments only: `SM3D_DEFER_LOAD_SECONDS=<seconds>` overrides the 3-second state-load wait
(0 = apply immediately).

---

## 12. Linux / macOS differences

- **Linux**: build with `make -f Makefiles/Makefile.UNIX` into `bin/supermodel`. Dependencies:
  `libsdl2-dev libsdl2-net-dev libglu1-mesa-dev zlib1g-dev libwebp-dev fonts-wqy-microhei`; for the Vulkan runtime,
  `libvulkan1` plus `mesa-vulkan-drivers`. Running from the repository root uses its `Config/` and
  `Assets/`; running from elsewhere uses `~/.config/supermodel/` and `~/.local/share/supermodel/`.
- **Vulkan build and `.zst` saves on Linux/macOS**: the CMake target is `Model3-Vulkan`. The `.zst`
  containers are read/written by the optional C# host core, which must be published on the same OS
  (Native AOT does not cross-compile between operating systems):
  `cmake -S . -B build-cmake -G Ninja -DUSE_SDL2=OFF -DUSE_SDL3=ON -DSUPERMODEL_SDL3_PURE_VULKAN=ON`,
  then `cmake --build build-cmake`, then `cmake --build build-cmake --target hostcore`, and run
  `./build-cmake/bin/Model3-Vulkan -gui`. It needs the .NET 11 SDK and clang (Linux) or the Xcode
  command line tools (macOS). Without the host core the emulator still runs, but `.zst` is unavailable.
- **macOS**: window sizes are in points and Retina is handled through `io.DisplayFramebufferScale`, so the
  interface size does not change between Retina and non-Retina displays.
- On both platforms the Makefile build is the SDL2/OpenGL one; the Vulkan backend comes from the CMake SDL3
  target.
- **Save management**: the Manage Saves window and the in-game save overlay are compiled only into the
  SDL3 Vulkan build (`SUPERMODEL_PURE_VULKAN`). The Makefile.UNIX binary is the dual-renderer SDL2 build,
  so it has F5/F7 but no card manager; use the CMake SDL3 target if you want it. Save thumbnails on Linux
  are decoded with the bundled PNG reader, so the cards show their previews there as they do on Windows.

---

*This manual is maintained with the code. If a default or a recommendation changes, update section 4.*
