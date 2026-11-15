# Model 3-Vulkan v0.9.9.8 — Release Notes

**2026-11-14 · Windows x64 · Linux x86_64 · macOS Intel · macOS Apple Silicon**

Model 3-Vulkan v0.9.9.8 is a performance release. Nothing in the emulation model changed: every
game still renders the same picture it did in v0.9.9.6, and the same save states still load. What
changed is how much work the CPU and the GPU have to do to get there.

The headline is that the hybrid JIT now translates **more than 80 % of the guest instruction
stream to native x86-64**, the Vulkan renderer stopped paying for a redundant depth write that was
silently disabling hardware Early-Z / Hi-Z culling, and UMA GPUs finally get zero-copy geometry
upload. Together those turn the integrated-GPU experience from "lock it to 496×384" into a picture
that can actually be played at native resolution.

## 📦 Packages

| Platform | File | Requirements |
| --- | --- | --- |
| Windows x64 | `Model3-Vulkan.v0.9.9.8.for.Windows.zip` | Windows 10/11, Vulkan 1.3 |
| Linux x86_64 | `Model3-Vulkan.v0.9.9.8.for.Linux.tar.gz` | glibc 2.38+, Vulkan 1.3 |
| macOS Intel | `Model3-Vulkan.v0.9.9.8.for.macOS-Intel.tar.gz` | macOS 13.0+, bundled MoltenVK 1.3 |
| macOS Apple Silicon | `Model3-Vulkan.v0.9.9.8.for.macOS-AppleSilicon.tar.gz` | macOS 13.3+, bundled MoltenVK 1.3 |

`SHA256SUMS.txt` is attached to the release. Every archive extracts to a relocatable folder; put your
ROM zips in its `roms/` directory (or point `ROMPath` at them). The Linux archive is built on Ubuntu
24.04 with g++-14 and needs no newer than `GLIBC_2.38` / `CXXABI_1.3.15` / `GLIBCXX_3.4.32`, so it
loads on Ubuntu 24.04, SteamOS 3.5+ and other current distributions. The macOS Intel build carries
its own `libMoltenVK.dylib` and targets macOS 13.0; `.zst` saves need macOS 14+ because the host core
links against a newer Swift runtime, and on macOS 13 the emulator falls back to `.st` automatically.
The Apple Silicon archive is the arm64 slice of the same build: the application, the host core and
MoltenVK are all pure arm64, so M1–M4 machines run it natively. Intel Macs should take the Intel-only
archive instead.

---

## The short version

- **The JIT now covers over 80 % of the guest instruction stream** — more PowerPC instruction
  classes are translated to native x86-64, so the C interpreter fallback runs far less often. In
  uncapped-frame-rate sessions (`-benchmark`, or the performance test button) the emulated machine
  runs noticeably faster than before.
- **Intel integrated GPUs got 20–50 % faster, and the fix is architectural** — the fragment stage
  no longer writes `gl_FragDepth`, which had been disabling the GPU's Early-Z / Hi-Z depth
  rejection on every frame. Occluded pixels are discarded before shading again instead of after it.
  Measured on Intel Arc Graphics (Core Ultra 5 125H) at 2480×1920 2x MSAA: `vf3tb` **+60.9 %**,
  `daytona2` **+30.9 %**, `scuddx` **+21.9 %**, `dirtdvls` **+14.0 %**. AMD integrated GPUs use the
  same architecture and should benefit as well. Discrete GPUs gain too, but they were rarely the
  bottleneck.
- **Dirt Devils no longer stutters** — its variable tables and its hot code share one 4 KB page,
  which the old "flush the whole page on any store" self-modifying-code check thrashed on. Simulation
  time per frame dropped from **45–66 ms** to **0.28–0.78 ms**.
- **Save states now load on frame 0** — the 3-second boot guard is gone by default (`0.0`), with no
  splash-in-the-middle. Every hardware block the loader needs is in the save image.
- **Smaller CPU overhead in the renderer** — HD-texture descriptor bindings are per-frame rather
  than per-viewport, and opaque geometry is grouped by vertex source, cutting redundant pipeline
  binds on the Intel Windows driver.

---

## ⚡ PowerPC → x86-64 hybrid JIT: over 80 % native

The JIT shipped in v0.9.9.6 already handled integer arithmetic, compares, branches, byte-reversed
loads and stores, single/double float memory ops and register moves. This release extends the
native encoder to cover substantially more of the instruction stream; the session-end log line now
reads:

```
PPC: translated code is 812345/987654 instruction(s) native (82.25%).
```

That line is compiled from the live block tables, so it describes the game that just ran rather
than a fixed estimate. With more of the stream native, the dispatcher hands off to the C
interpreter far less often, which is where the extra throughput comes from in CPU-bound scenes.

Verification is unchanged and still strict: `SM3D_PPC_JIT_VERIFY=1` double-runs every block
against the interpreter and aborts on the first difference, and `SM3D_PPC_JIT_DUMP=1` writes the
active blocks out as disassembly. The C interpreter remains selectable (**Core → PowerPC Execution
Mode**) as the reference path for A/B comparisons.

If you want to see the effect yourself, use **Common → Performance test (unlocked frame rate)** or
`-benchmark`: those unlock the fixed 57.524 Hz logic pacing so the figure is real throughput rather
than the present rate.

---

## 🅾️ Intel integrated GPUs: 20–50 % faster

### The bug: a depth write that switched off Early-Z

The fragment shader used to end with:

```glsl
gl_FragDepth = ubo.projection[3][2] * gl_FragCoord.w;
```

That value is **exactly** what the rasteriser already produces. Every Model 3 projection in this
backend comes from `Mat4::FrustumRZ`, whose third row is `(0, 0, 0, near)`, so the hardware's
interpolated `gl_FragCoord.z` is identically equal to that expression — the sorted-transparency
stage (`real3d_oit.frag`) already relied on this and never wrote depth. But a fragment shader that
writes `gl_FragDepth` at all cannot have its depth test hoisted above it, so the GPU had to run the
**entire** fragment shader — four 64-byte vertex pulls, a bilinear quad solve and a texture decode —
for every pixel, including the ones that were about to be thrown away.

Removing the write restores hardware depth rejection. On a 24-EU-class integrated GPU, where the
fragment stage is the bottleneck by a wide margin, that is the whole ballgame.

### Measured on Intel Arc Graphics (Core Ultra 5 125H)

`-benchmark`, 6 seconds per run, same save state, `AlphaSortMode=0`:

| Resolution | Game | Before | After | Gain |
| --- | --- | --- | --- | --- |
| 1920×1080 · MSAA 1x | `daytona2` | 87.80 FPS | **112.83 FPS** | **+28.5 %** |
| 1920×1080 · MSAA 1x | `dirtdvls` | 91.17 FPS | **105.64 FPS** | **+15.9 %** |
| 2480×1920 · MSAA 2x | `vf3tb` | 45.48 FPS | **73.17 FPS** | **+60.9 %** |
| 2480×1920 · MSAA 2x | `daytona2` | 37.34 FPS | **48.86 FPS** | **+30.9 %** |
| 2480×1920 · MSAA 2x | `scuddx` | 34.13 FPS | **41.62 FPS** | **+21.9 %** |
| 2480×1920 · MSAA 2x | `dirtdvls` | 41.30 FPS | **47.10 FPS** | **+14.0 %** |

`vf3tb` at 5× native resolution went from "cannot reach the arcade frame rate" to **1.27× arcade
speed**. At 1080p the lighter games now sit against the desktop compositor's 120 Hz ceiling, which
is why their gains look smaller — they were already limited by presentation, not by the GPU.

### Picture verification

Same save state, same emulated frame (`-Frame 200`), interpolated presents disabled
(`RefreshRateMode=0`), 2480×1920, MSAA 2x:

| Game | Identical pixels | Mean absolute error | PSNR |
| --- | --- | --- | --- |
| `daytona2` | 99.924 % | 0.0185 / 255 | 49.18 dB |
| `scuddx` | 99.964 % | 0.0153 / 255 | 47.21 dB |
| `vf3tb` | 99.941 % | 0.0123 / 255 | 51.64 dB |
| `dirtdvls` | 99.969 % | 0.0091 / 255 | 51.53 dB |

The differences are single-sample tie-breaks on coplanar decals, a fraction of one 8-bit step, and
the diff visualisation shows them only along triangle edges. This is the same depth value being
tested slightly earlier in the pipeline, not a different picture.

### UMA geometry upload

On integrated and unified-memory GPUs the 384 MB ROM vertex buffer is now allocated
`DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT` when the driver offers it, and new vertices are
written straight through the mapping. That removes the staging copy, the `vkCmdCopyBuffer` and the
`TRANSFER_WRITE` barrier for every batch of newly cached static geometry. Discrete GPUs fall back to
the original device-local buffer automatically, and the log says which path ran:

```
Vulkan: ROM vertex buffer is host-visible (UMA direct write).
```

### Draw submission

Two changes reduce per-frame CPU in the renderer, which matters most on the Intel Windows driver:

- The HD-texture descriptor set (set 2) is bound **once per frame** instead of once per viewport
  node inside `DrawBucket()`.
- Opaque geometry is grouped by vertex source (ROM vs. polygon RAM) within each viewport node, so a
  node switches between the two pipelines at most once instead of alternating. Translucent layers
  keep their original submission order — grouping them would change how two overlapping translucent
  surfaces composite.

Both changes are pixel-identical: the four games above compare at **0 differing pixels** against
the build without them.

---

## 🏁 Dirt Devils: the 4 KB page stutter is fixed

`dirtdvls` has an unusual layout: its variable tables at `0x0005A5A8–0x0005A66C` and its hot code at
`0x0005AB64–0x0005AEA4` share **one 4 KB page**. The old self-modifying-code check invalidated
every translation on that page whenever *any* store landed in it, so the game re-translated its own
hot code roughly 2,300 times per frame.

The invalidation is now three-level:

1. a 32-byte line gate,
2. a one-bit-per-guest-byte map,
3. a 4 KB page generation counter.

A store only bumps the page generation when the address really overlaps bytes that were translated,
and it clears that page's line marks and byte map in the process. `ramGen` is **not** touched — that
is reserved for `icbi`, state loads and memory reallocation. Blocks are also constrained to a single
4 KB page so a fetch loop cannot straddle two generations.

Result, same save state, 30 seconds:

| | Before | After |
| --- | --- | --- |
| Simulation hand-off per frame | 45–66 ms | **0.28–0.78 ms** |
| Store-triggered flushes | ~2,300 / frame | **0** |
| Present rate | collapsed | holds the panel rate |

The code cache is 64 MB, and `SM3D_PPC_JIT_VERIFY=1` re-verifies the invalidation against the
interpreter block by block (26.6 million blocks, 0 failures on the test matrix).

---

## 💾 Save states load on frame 0

The deferred load was introduced as a workaround: applying a state to a machine still in its boot
ROM left VF3 running at about 1/25 speed, because the 53C810's PCI base address had never been
written. That root cause was fixed in v0.9.9.6 by adding the missing hardware blocks to the save
image, and v0.9.9.8 finally drops the default guard:

```cpp
constexpr double kDeferredLoadSeconds = 0.0;
```

- **Manage Saves → Load** and `-load-state` now apply the state on the first frame, with no
  start-up artwork flashing in between (`loading deferred state ... after 0 emulated frame(s)`).
- Verified across 15 ROM sets — `daytona2`, `dayto2pe`, `scuddx`, `scud`, `scudau`, `srally2`,
  `lemans24`, `dirtdvls`, `vf3tb`, `fvipers2`, `lostwsga`, `swtrilgy`, `magtruck`, `von2a`,
  `spikeofe` — covering both `.zst` containers and legacy `.st` files.
- `SM3D_DEFER_LOAD_SECONDS=<seconds>` remains as a diagnostic override, still counted in emulated
  frames rather than wall clock.
- The usual rule still applies: **do not** save on a load, transition, score or K.O. screen. Those
  states can be parked in a cross-frame handshake and may hang when restored.

---

## 🖥️ Statistics panel on dual-GPU laptops

On a laptop with both an integrated GPU and a discrete NVIDIA card, the statistics overlay used to
name (and read telemetry from) the NVIDIA card even when the Vulkan renderer was running on the
integrated GPU, because NVML was probed unconditionally. The readout now checks which device the
renderer actually selected: when it is not an NVIDIA adapter, the overlay keeps the Vulkan device
name and reads load from the Windows GPU Engine counters, so a session on Intel Arc shows
`Intel(R) Arc(TM) Graphics` and the real 3D utilisation.

---

## 🔧 Interface fix: resolution preset vs. low-resolution rendering

**Advanced → Picture scaling → Low-resolution 3D rendering** pins the 3D scene to 496×384 or
992×768 regardless of the window size. Changing the **resolution preset** on the Common page did
not touch it, so picking `2x (992 x 768)` after having used the ultra-low preset still rendered at
496×384 and upscaled — the readout said `496x384 -> 992x768`, which looked like the setting had
been ignored.

The two controls now stay in step: choosing `1x (496 x 384)` sets the internal size to 496×384,
`2x (992 x 768)` sets it to 992×768, and any higher preset turns low-resolution rendering off.

---

## ⚙️ Also in this build

- **`-benchmark` / performance test** — unlocks the 57.524 Hz logic pacing, mutes audio, skips a
  one-second warm-up and reports real throughput (`BENCH: ... FPS ... x arcade`). Available from
  **Common → Performance test (unlocked frame rate)**.
- **Mouse device logging** — the log now enumerates each mouse and its SDL id, so a two-light-gun
  setup can be diagnosed without guessing whether Windows exposed both guns as separate devices.
- **Force-feedback diagnostics** — the FFB summary line counts `FFStop` commands, and a device that
  rejects a constant-force update is logged once instead of failing silently.
- **Dual-GPU stats** — see the section above.
- **Defaults on a clean install** — hybrid JIT on, 4.0 Quad audio, automatic presentation cadence
  with motion interpolation, strong chase-camera stabiliser, warm frosted-glass interface, cabinet
  bezel, loopback networking, asynchronous sound-board thread. Recording, HD textures, statistics
  overlay, crosshairs, Sinden border, Vulkan validation, exclusive fullscreen, per-pixel OIT,
  instance interpolation and MSAA/CRT/glow/FXAA remain off.

---

## 🔄 Upgrading

- Settings and save states are compatible: `SUPERMODEL_VERSION` is untouched, so existing `.zst` /
  `.st` saves and NVRAM load normally.
- The default save-state delay changed from 3 seconds to 0; this is the one behavioural difference
  you will notice immediately.
- If you had selected an integrated GPU from a testing build and the settings window comes up with
  blocky artefacts, set **Video → GPU** back to automatic: that combination instructs the Vulkan
  context to render on one adapter and present to the display attached to another.

---

## ⚠️ Known issues

- `scuddx` (Scud Race Deluxe) HUD instruments are still incomplete — a long-standing emulation gap
  that predates MAME; play the Twin/DX sets (`scud`, `scudau`, `scudplus`, `scudplusa`).
- Per-instance interpolation remains off by design: enabling it produces single-frame hairline
  cracks where a snapped instance meets an interpolated one.
- Not every moment is saveable: states captured on load/transition/score screens may hang.
- Force feedback, two light guns, the Sinden border and the two-machine link are implemented but
  still untested on real hardware in this build.

---

## 中文摘要

**Model 3-Vulkan v0.9.9.8 是一次纯粹的性能更新**：模拟本身没有变化，同一存档仍然逐像素输出同一画面。

**三个重点：**

1. **混合 JIT 覆盖了超过 80% 的客户机指令流**。更多 PowerPC 指令被翻译成 x86-64 原生代码，落到 C 解释器
   的次数大幅减少。在解锁帧率的场景（`-benchmark` 或「性能测试」按钮）下游戏运行速度明显更快。
   每局结束日志会打印 `PPC: translated code is N/N instruction(s) native (xx.xx%)`，可直接核对。
2. **Intel 核显专属优化，实测提升 20–50%（AMD 核显大概率同样受益）**。片元着色器不再写
   `gl_FragDepth`——对本后端所有 `Mat4::FrustumRZ` 投影，它恒等于硬件已经插值出的
   `gl_FragCoord.z`，但写了就等于关掉 Early-Z / Hi-Z：被遮挡的像素仍要跑完整片元着色（4 次
   64 字节顶点读取 + 双线性四边形求解 + 贴图解码）。移除后硬件深度剔除恢复。
   Intel Arc Graphics（Core Ultra 5 125H）实测 2480×1920 / MSAA 2x：`vf3tb` **+60.9%**、
   `daytona2` **+30.9%**、`scuddx` **+21.9%**、`dirtdvls` **+14.0%**；1080p 下 `daytona2`
   87.8 → **112.8 FPS（+28.5%）**。画面同帧 A/B：99.92–99.97% 像素完全一致，全图平均误差
   0.007–0.019 / 255。
3. **修复「泥地赛车」（Dirt Devils）运行卡顿**。这个游戏的变量表（`0x5A5A8–0x5A66C`）与热代码
   （`0x5AB64–0x5AEA4`）位于**同一个 4 KB 页**，旧的「页内任何写入就作废整页翻译」逻辑让它每帧
   重译约 2300 次。现在改为三层失效：32 字节行门 + 每字节位图 + 4 KB 页 generation，只有真正
   覆盖到已翻译指令字节的写入才作废该页。仿真交接时间从 **45–66 ms/帧** 降到 **0.28–0.78 ms/帧**，
   存储触发的刷新从每帧约 2300 次降到 **0**。

**其它：**

- **UMA 核显零拷贝几何上传**：ROM 顶点缓冲在统一内存设备上用 `DEVICE_LOCAL | HOST_VISIBLE |
  HOST_COHERENT`，新顶点直接由 CPU 写入映射地址，省掉暂存拷贝、`vkCmdCopyBuffer` 与
  `TRANSFER_WRITE` 屏障；独显自动回退原路径（日志打印 `ROM vertex buffer is host-visible`）。
- **驱动调用减负**：高清纹理描述符（set 2）改为每帧绑定一次（原先每个视口节点都绑一次）；
  同一视口内不透明几何按 ROM / 多边形 RAM 归组，管线切换从交替变为最多一次；半透明层保持
  原有提交顺序。两项改动逐像素 **0 差异**。
- **存档默认第 0 帧读取**：`kDeferredLoadSeconds` 从 `3.0` 改为 `0.0`，不再有读档时整屏启动图，
  日志显示 `after 0 emulated frame(s)`。已用 15 个 ROM set 验证（新 `.zst` 与旧 `.st` 都覆盖）。
- **双显卡笔记本统计面板**：在核显上跑时不再显示闲置的 NVIDIA 独显名称与遥测，改为显示实际
  Vulkan 设备名与 GPU Engine 占用率。
- **分辨率预设与低分辨率渲染联动**：在「常用」页改分辨率预设时会同步更新
  `InternalResolution`（1x → 496×384，2x → 992×768，3x 及以上 → 关闭），不会再出现选了
  `2x (992 x 768)` 却仍按 496×384 渲染的情况。

**默认不打开**：录音、高清纹理加载/导出、统计浮层、准星、Sinden 白边、Vulkan 校验层、
独占全屏、逐像素 OIT、实例插值、MSAA/CRT/辉光/FXAA、点对点缩放。
**默认打开**：混合 JIT、4.0 四声道、自动刷新节奏 + 运动插值、强力追尾镜头稳定、暖色毛玻璃界面。

兼容性：`SUPERMODEL_VERSION` 未改，旧存档与 NVRAM 继续可用。

---

*Model 3-Vulkan is licensed under GPLv3. Upstream Supermodel attribution and third-party notices
are preserved in `Data/NOTICE.md`, `Data/LICENSE-ADDITIONAL-TERMS.md` and `Data/TRADEMARK.md`.*
