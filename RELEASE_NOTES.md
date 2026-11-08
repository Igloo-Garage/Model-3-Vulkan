# Model 3-Vulkan v0.9.9.6 — Release Notes

**2026-11-07 · Windows (this download) · Linux / macOS builds from the same tree are produced by CI**

Model 3-Vulkan v0.9.9.6 is the biggest release this fork has shipped. It is not a maintenance
build: the emulation core now has a PowerPC → x86-64 hybrid JIT, the audio path can drive a real
4.0/5.1 arcade cabinet and record it losslessly, the chase camera has a proper Need for Speed-style
stabilizer, direct-drive wheels finally get real force feedback and live pedal calibration, and the
settings window has been rebuilt around frosted glass and three complete themes.

If you are coming from **v0.9.8.6a**, the JIT, camera, force-feedback, audio, save-state, graphics
and interface sections below are new. The platform section also recaps the v0.9.8.6a / v0.9.8.6x
hotfixes so a fresh reader sees the whole picture.

---

## The short version

- **Up to 4.1× faster in CPU-bound scenes** — the PowerPC 603e now translates hot basic blocks to native x86-64, verified against the interpreter for 675 million blocks with pixel-identical output.
- **A chase camera that behaves like a modern racing game** — spring-damper yaw stabilisation around the car's pivot, with full-screen 3D HUDs (speedometer, course map, turn arrows) staying perfectly still.
- **Force feedback that finally reaches the wheel** — SDL3 haptics initialised correctly on Windows, raw direct-drive wheel bases recognised, single-axis constant force fixed, and a live wheel/pedal calibration lab.
- **A soundtrack, not a stereo mix** — native 4.0 Quad and 5.1 Surround, a smart center/LFE upmixer, 4-tap Catmull-Rom SCSP voices, 32-bit float DSB decoding, a C¹ soft-clipper, and a lock-free multi-channel WAV/FLAC recorder.
- **Save states that resume instead of stalling** — the 53C810 PCI base address and halt flag and the simulated network board are now part of the save, so VF3 and Scud Race pick up where they left off instead of spinning on the DMA chain.
- **A front end that belongs in 2026** — frosted-glass panels, warm light / warm dark / classic themes, adaptive tables that work at any DPI, per-game audio balance, and a much richer diagnostic log.
- **The platform work that makes it shippable** — AMD Windows Vulkan driver limits handled, Linux pinned to a Steam Deck-compatible glibc, macOS Retina 4K fixed, GNOME Wayland title bars restored.

---

## ⚡ PowerPC 603e → x86-64 hybrid JIT

`PowerPCMode = 1` is the new default. Hot PowerPC basic blocks are translated once into native
x86-64 and then executed directly, with:

- a 16 MB RWX code cache and `PPC_FAST_MEMORY` direct-memory windows;
- self-modifying-code page tracking;
- idle-loop skipping;
- a lock-step verifier that runs every translated block against the interpreter.

**Verification:** 675 million blocks compared across five games, zero mismatches; the same save
state and frame is pixel-identical to the interpreter. In a CPU-bound scene the frame rate went
from **55.2 Hz to 224.5 Hz — about 4.1×**.

The C interpreter remains one menu entry away (`PowerPCMode = 0`) for reference runs and A/B tests.
The JIT lives only in the Vulkan build; the retired OpenGL build keeps the interpreter.

---

## 🎥 Chase camera & steering feel

The new **Chase Camera Stabilizer** (`ChaseCameraStabilizer`, Off / Light / Standard / Strong,
default Strong) is a spring-damper filter applied to the camera's horizontal yaw around the player
car's pivot in view space. It removes the “the whole city swings when I breathe on the stick”
problem on a gamepad without touching the car's actual heading. Full-screen 3D HUD viewports are
excluded, so Daytona USA 2's speedometer, route map and turn arrows stay nailed in place.

Paired with it:

- **Gamepad Steering Smoothing** (Off / Light / Medium / Strong, default Strong) and **Sensitivity**
  (25–100%, default 66%) on the **FFB / Sensitivity** page and in the in-game `Tab` overlay.
- Selecting **Off** resets sensitivity to **100%** automatically.
- Binding steering to a physical racing wheel bypasses both filters completely, preserving 1:1
  direct response.

---

## 🏎️ Force feedback, pedal calibration & wheel lab

Force feedback in the SDL3 build was not merely tuned — it was fixed end to end:

- **Correct haptic initialisation on Windows.** `SDL_INIT_HAPTIC` is now brought up before the
  joystick/gamepad subsystems, so DirectInput wheel bases actually appear in SDL's haptic list.
- **Raw joystick wheel bases are accepted.** The old `isGamepad` gate rejected SIMAGIC, MOZA,
  Fanatec and similar direct-drive bases; they are now opened as haptic devices.
- **Constant force sign and axis encoding fixed**, with the 20 ms attack envelope retained for a
  smooth torque build-up instead of a staircase.
- **Vibration routed through `SDL_HAPTIC_SINE`**, with a rumble fallback for devices that do not
  expose a sine effect.
- **Hardware autocenter is released**, so the game's own spring effect is in control.

On the **FFB / Sensitivity** page there is now a live calibration lab:

- centred bidirectional axes, full-travel pedals, deadzones and custom min/off/max ranges;
- columns measured from the current font and DPI, stretching to fill a wide window and switching
  to a horizontal scrollbar instead of clipping `-32768` when the window is narrow;
- a one-click **“Release H-shifter to neutral (from gears 1–4)”** binding generator on the
  **Controls** tab.

> Force Feedback is **off on a fresh install** by design: most players use a pad or keyboard, and
> enabling the drive board would make supported pads rumble constantly. Wheel users: tick
> **Force Feedback** on the FFB / Sensitivity page (or launch with `-force-feedback`) once.

---

## 🔊 Audio: from stereo fold-down to a real cabinet

### Native 4.0 Quad and 5.1 Surround

`NbSoundChannels` now supports **1 (Mono)**, **2 (Stereo)**, **4 (4.0 Quad `[FL, FR, RL, RR]`)**,
and **6 (5.1 Surround `[FL, FR, FC, LFE, RL, RR]`)**. The default is **4.0 Quad**, which matches the
arcade cabinet and behaves correctly on a stereo desktop endpoint.

The optional **5.1 upmixer** (`SurroundUpmix51`) synthesises:

- **Center (`FC`)** from the front/rear common-mode signal, lifting vocals, navigator calls and
  lead melodies out of the left/right pair;
- **LFE** through a pre-warped 120 Hz second-order Butterworth low-pass, extracting engine rumble
  and collision weight.

### Lossless multi-channel recorder

`RecordWav` captures the finished 4.0/5.1 mix through a lock-free SPSC ring buffer and a dedicated
disk-writer thread — no `fwrite`, no mutex, no blocking on the real-time audio callback.

- **`wav_float32`** — WAV, 32-bit IEEE float
- **`wav_pcm16`** — WAV, 16-bit PCM
- **`flac_pcm24`** — FLAC, 24-bit PCM
- **`flac_pcm16`** — FLAC, 16-bit PCM

Files land in `Data/Recordings/Sound/<Game>_<date>_<time>_<N>ch.wav|flac`, with proper
`WAVE_FORMAT_EXTENSIBLE` / `VORBIS_COMMENT` speaker masks for Premiere, DaVinci Resolve and
Audacity. Recording keeps the full 4/6-channel mix even on a stereo endpoint, so the file is a true
discrete surround capture rather than a fold-down.

> Recording is **off by default**. Turn it on in the Sound page or with `-record-wav`.

### SCSP and DSB fidelity

- **4-tap Catmull-Rom cubic interpolation** on non-FM SCSP PCM voices, with FM carriers and
  modulators kept bit-exact; this removes the mirror-image aliasing on pitch-shifted engine, tyre
  and voice samples.
- **Slave-SCSP FM ring-buffer cross-talk fixed**, Slots 16–17 `EFSDL`/`EFPAN` wired to
  `DSP.EXTS[0..1]`, `LFORE` phase reset implemented, and dynamic `AR`/`D1R`/`D2R`/`RR`/`KRS`
  envelope updates corrected.
- **32-bit float DSB MPEG decoding** straight into the 64-tap polyphase resampler, with per-sample
  gain ramping to remove 57.5 Hz zipper noise.
- **C¹-continuous knee soft-clipper** on the master bus: bit-exact below -1.41 dBFS, smooth
  saturation above it, no flat-topped hard clipping when `MusicVolume` is pushed past 100%.

### Restored music and a hidden-egg fix

- **Looped MPEG intros are back.** `MpegDec` now preserves valid negative pre-loop offsets, which
  restores Daytona USA 2's 9.9-second attract intro and the 12.6-second race-theme intros.
- **Daytona USA 2 no longer plays Track 8 on every course.** Single-button view cycling
  (`VRCycle`) used to hold the default `VR3` button down on every frame from boot, which triggered
  the arcade's “hold VR3 while a race loads” Easter egg. View cycling now emits a six-frame
  momentary pulse only when you actually press the button.
- **Per-game audio balance** (Scud Race, Daytona 2, Star Wars Trilogy, Spikeout) tames the
  arcade's own effects/music divider; any explicit value in the ini or on the command line wins.

---

## 💾 Save states and the network board

- **`53C810 PCI` state block.** The SCSI controller's `baseAddress` and halt flag are now part of
  the save state. Loading at the first frame used to leave the `0xF9`/`0xC1` register windows
  disabled until the game wrote its PCI configuration, which made VF3 and Scud Race spin on the DMA
  chain. New saves load immediately; old saves fall back to `0xC0`.
- **The simulated network board is saved.** `CSimNetBoard` state, shared RAM and buffer are part of
  the save, with a synthesised single-machine ready state (and the `256 → 848` segment-size repair)
  for older saves. Attract-mode saves no longer time out into the boot self-test or hang on
  “Please Wait” at car select.

Also in this release: the drive board's command queue (one command per frame to the Z80), the wheel
servo gain and constant-force/vibration cache, a `WheelBoardServo` save block, and the `FFVibrate`
effect-count fix.

---

## 🖼️ Graphics & presentation

- **Per-pixel transparency sorting (`AlphaSortMode = 1`)** — a fixed-slot SoA A-Buffer, four
  layers per pixel, resolved per priority. It fixes scenes the two-layer scheme cannot: more than
  two translucent surfaces at one pixel, and translucent polygons submitted out of depth order.
  At 4K + MSAA 4× it holds the same 120 Hz as the two-layer path; the old linked-list prototype
  ran at 27–37 Hz and was discarded.
- **Two-subpass on-chip composite** for the default path. 4K + 4× MSAA VF3 went from 226 to
  240–241 Hz (about +6%), bit-identical at 1×.
- **Hardware MSAA (2/4/8×)**, exact quad interpolation, derivative-based texture filtering with a
  long-axis tap mode, and the arcade-exact checkerboard transparency mode as an opt-in experiment.
- **HD texture packs** — PNG, WebP, DDS BC1/BC2/BC3/BC5/BC7, and `.dds.zst`, with bindless sampling,
  async decode, chunked GPU upload and hot reload. `F4` toggles replacement/original, `F3` rescans.
  Exported dumps now write alpha only where the material actually reads it, eliminating the white
  dots and dashed lines caused by the arcade's 1-bit alpha dither.
- **High-refresh presentation** — Original / Auto / Fixed modes, VRR 2×/3×/4×/8× phase stepping,
  camera and instance interpolation, `F8` hot-switch, and a present-phase calibration that cut
  about 4.6 ms of end-to-end latency on a Fixed 120 Hz setup.
- **macOS Retina 4K** — the game window requests a high-density drawable and the render geometry is
  computed in device pixels, so 4K panels render at native resolution instead of a 1080p upscale.

---

## 🎨 Interface

- **The tab row stays put.** Common / Core / Video / Sound / Controls / FFB / Sensitivity / Network
  are now a fixed header; scrolling a long page no longer takes the tabs with it, so switching pages
  never means scrolling back to the top first.
- **Frosted-glass panels** with a real dual Kawase blur of the game artwork behind them, 1px glass
  highlight edges, and a 120 ms panel fade.
- **Three complete themes** — warm eye-care light (default), warm dark, and the original flat
  interface reproduced value for value.
- **The game list** carries 63 WebP box arts, a cover preview, per-game descriptions, filters,
  remembered selection/scroll position, and an optional “auto-launch on startup”.
- **Adaptive tables** — the wheel/pedal calibration table measures columns at the current font and
  DPI, stretches when there is room, and shows a horizontal scrollbar instead of clipping when
  there is not.
- **Single config file** — `Data/Config/Model3-Vulkan.ini`. Old `Supermodel.ini` files are migrated
  or removed automatically; a stale `GameXMLFile` path is repaired at startup.
- **Richer log** — `Data/Model3Vulkan.log` starts with the build timestamp, host memory and CPU
  count, the audio DSP chain, and every non-default axis calibration value.
- **Portable ROM folders** — a `rom`/`roms`/`romset`/`romsets` folder next to the executable is
  detected automatically and stored as a relative path, so moving the whole folder keeps working.

---

## 🧩 Platform work

- **Windows + AMD:** per-viewport uniforms moved out of push constants, shrinking `R3DPush` from
  208 B to 96 B. AMD's Windows Vulkan driver reports a 128-byte limit, which used to make the
  emulator exit silently after the splash screen.
- **Linux:** release builds pinned to Ubuntu 24.04 + g++-14, with a CI guard that fails above
  `GLIBC_2.39`; the 24.04 build itself needs at most `GLIBC_2.38`, so it loads on SteamOS 3.5 and
  Ubuntu 24.04. The bundled `wqy-microhei` font has a Latin-glyph guard, `<exe>/Data/` is discovered
  on Linux, and `libdecor` is included for GNOME Wayland title bars.
- **macOS:** Retina/4K fix, non-Windows mouse/keyboard input in the in-game `Tab` overlay, and the
  `.zst` HostCore module (macOS 14+; `.st` remains the fallback on macOS 13).
- **SDL3 input:** per-device mice via RawInput, late input sampling just before the simulation step,
  controller hot-plug, SDL haptics, and the Sinden light-gun border.
- **Config/log cleanup:** the legacy `Supermodel-LegacyOG.exe` is retired from the release package;
  `Model3-Vulkan.exe` is the product.

---

## 🧾 Upstream 0.4 sync (2026-11-07)

The last round before this release pulls the low-risk fixes from upstream Supermodel 0.4:

- `53C810` PCI base address and halt flag in save states;
- thread-safety fixes in the DSB FIFO, SCSP MIDI FIFO and the audio mute flag;
- drive-board command queue and wheel-servo state;
- non-merged clone ROM sets (`srally2dx`, `swtrilgyp`);
- Lost World light-gun reload / off-screen indicator (`lostwsgo` → `lostwsgp`);
- VF3 startup viewports with subnormal matrices skipped;
- Real3D config-register zero-initialisation;
- the fourth force-feedback effect slot.

---

## ⚙️ Defaults on a clean install

The release package ships **without an ini**, so a fresh install starts from these defaults. This
is the list worth knowing before your first launch.

**Off by default (opt in when you want them):**

| Option | Key | Where |
| --- | --- | --- |
| Audio recording | `RecordWav` | Sound |
| HD texture replacement | `LoadHDTextures` | Core |
| HD texture dumping | `DumpHDTextures` | Core |
| Raw memory / texture dumps | `DumpMemory`, `DumpTextures` | Network |
| Statistics overlay | `ShowStatsOsd` | Video |
| Crosshairs | `Crosshairs` | Video |
| Sinden border | `SindenBorder` | Video |
| Vulkan validation | `VkValidation` | Video |
| Exclusive fullscreen / exclusive refresh | `ExclusiveFullscreen`, `ExclusiveRefreshRate` | Video |
| Per-pixel OIT | `AlphaSortMode = 1` | Video |
| Instance interpolation | `InstanceInterpolation` | no UI (internal A/B switch) |
| MSAA / CRT / glow / FXAA | `MSAA = 0`, `PostCRT = 0`, `PostGlow = 0`, `PostFXAA = 0` | Video |
| Integer scaling / widescreen stretch | `IntegerScale`, `WideScreen`, `WideBackground` | Video |
| Force feedback | `ForceFeedback` | FFB / Sensitivity |

**On by default, deliberately:**

- **Hybrid JIT** (`PowerPCMode = 1`).
- **4.0 Quad audio** (`NbSoundChannels = 4`). 5.1 is a deliberate choice; the upmixer only runs
  when six channels are selected. On a stereo endpoint the 4-channel mix is folded by the emulator,
  not by SDL.
- **Auto refresh pacing + motion interpolation** (`RefreshRateMode = 1`, `MotionInterpolation = 1`).
- **Chase camera stabilizer Strong** and **gamepad steering smoothing Strong** (both bypass a real
  wheel).
- **Warm light frosted-glass UI** (`UITheme = 0`, `UiFrostedGlass = 1`).
- **Cabinet bezel backdrop** (`BezelMode = 1`) — turn it off in Video if you prefer black bars.
- **Network self-link** (`Network = 1`, `SimulateNet = 1`, ports 1970/1970 on loopback), required
  for the netboard games to boot.
- **Async sound board thread** and **high process priority**.

---

## 🔄 Upgrading from v0.9.8.6a

- Save states and NVRAM remain compatible: `SUPERMODEL_VERSION` is untouched.
- `Supermodel.ini` is migrated to `Data/Config/Model3-Vulkan.ini` on first launch; if both exist,
  the stale file is removed.
- Existing `.st` saves still load; new saves prefer the `.zst` container. `Data/Saves/` is kept.
- HD texture packs in `Data/Assets/HDTextures/<set>/` keep working; DDS and `.dds.zst` are new
  options, not replacements.
- The pipeline cache is rebuilt once after the upgrade; the first launch is slower than subsequent
  ones.
- If you extract into a fresh folder, copy `Data/Saves`, `Data/NVRAM`, `Data/Recordings`,
  `Data/Assets/HDTextures` and your ROM folder across.

---

## ⚠️ Known issues

- **Scud Race Deluxe (`scuddx`) HUD** — the grey CONDITION panel and some wrong-colored rank text
  are a long-standing Model 3 emulation gap. Upstream Supermodel 0.4 still shows the same thing on
  the same save state. The Twin/DX versions (`scud`, `scudau`, `scudplus`, `scudplusa`) display the
  HUD correctly; use those if it bothers you.
- **`.zst` on macOS 13** — the HostCore module needs macOS 14+. The emulator logs it once and falls
  back to `.st`; saving and loading still work.
- **Force feedback is opt-in** — see the note above. Without a wheel it stays off, which is why a
  fresh install does not rumble a gamepad.
- **5.1 on a stereo endpoint** — if you select 6 channels, SDL folds the stream to stereo with its
  own coefficient table; the default 4.0 mode uses the emulator's fold and is the recommended
  setting for stereo speakers.
- **Recording changes the device stream** — with `RecordWav` on, the mixer keeps a 4/6-channel
  stream even on a stereo endpoint. That is intentional (the file must be discrete), but it means
  toggling recording can change the monitoring level slightly.

---

## 中文摘要

**Model 3-Vulkan v0.9.9.6 是这条分支至今最大的一次更新，不是小修补。**

- **混合 JIT**：PowerPC 603e 热代码块直接翻译成 x86-64，6.75 亿个块与解释器逐块对照零差异、
  同帧像素一致；CPU 瓶颈场景从 55.2 Hz 提升到 224.5 Hz（约 4.1 倍）。
- **极品飞车式追尾镜头**：围绕车身支点的弹簧阻尼水平偏航稳定器，手柄微操不再让整条地平线
  左右甩；Daytona 2 的码表、路线图、转弯箭头保持不动。默认「强力」，可关/轻/标准/强力。
- **真正的力反馈**：Windows 下 SDL3 触觉正确初始化，直驱方向盘（SIMAGIC / MOZA / Fanatec 等
  raw joystick 底座）可被识别，恒力方向与单轴编码修正，振动走 `SDL_HAPTIC_SINE`；新增实时
  方向盘/踏板死区与全行程校准、H 挡一键回空挡。力反馈总开关默认关闭，用方向盘时在
  「力反馈 / 灵敏度」页打开即可。
- **音频全面升级**：原生 4.0 四声道（默认）与 5.1 环绕，智能中置/120 Hz 低音炮上混；
  SCSP 非 FM 通道 4-tap 三次样条插值；DSB MPEG 32-bit Float 直通 + 64-tap 重采样；
  主总线 C¹ 软限幅；内置无锁多声道 WAV/FLAC 录音（默认关闭）。
- **读档更可靠**：53C810 PCI 基址与 halt 标志、模拟网络板状态都进了存档，VF3 与 Scud Race
  不会再卡在 DMA 链上。Daytona 2 的 9.9 秒前奏和 12.6 秒比赛曲前奏恢复；修掉了「所有赛道都
  放 Track 8」的隐藏彩蛋误触发。
- **画面与界面**：逐像素半透明排序、双 Subpass 片上合成、MSAA、精确四边形插值、DDS/`.dds.zst`
  高清纹理包、毛玻璃三主题界面、4K/Retina、高刷插值与 VRR 倍频。
- **平台**：AMD Windows 驱动 128 字节 push constant 限制、Linux 固定 Ubuntu 24.04 构建（CI 守
  `GLIBC_2.39`，实际产物最高只要求 `GLIBC_2.38`）、GNOME Wayland 标题栏、macOS Retina、
  非 Windows 游戏内面板输入全部处理。

**默认不打开**：录音、高清纹理加载/导出、原始显存/贴图导出、统计浮层、准星、Sinden 白边、
Vulkan 校验层、独占全屏/独占刷新率、逐像素 OIT、实例插值、MSAA/CRT/辉光/FXAA、点对点缩放/
宽屏拉伸、力反馈。**默认打开**：混合 JIT、4.0 四声道、自动刷新节奏 + 运动插值、强力追尾镜头
稳定与手柄转向平滑、暖色毛玻璃界面、机柜背景图、网络自连、异步声板线程。

兼容性：`SUPERMODEL_VERSION` 未改，旧存档与 NVRAM 继续可用；旧 `Supermodel.ini` 会自动迁移为
`Model3-Vulkan.ini`；旧 `.st` 存档仍可读取，新存档优先 `.zst`。

---

*Model 3-Vulkan is licensed under GPLv3. Upstream Supermodel attribution and third-party notices
are preserved in `Data/NOTICE.md`, `Data/LICENSE-ADDITIONAL-TERMS.md` and `Data/TRADEMARK.md`.*
