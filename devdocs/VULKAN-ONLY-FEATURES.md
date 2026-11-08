# Vulkan 版 vs OpenGL 版：功能差异清单

> 用途：GitHub Release 说明、Reddit 发帖、玩家常见疑问。
> 代码层面的事实来源：`ScopeOfSetting()` 的 `vulkanOnly[]`、
> `StripRetiredVulkanSettings()` 的 retired 列表、`#if !defined(SUPERMODEL_OPENGL_ONLY)`
> 守卫、以及 `CMakeLists.txt` 的 `SUPERMODEL_CORE_VULKAN_SOURCES`。
>
> **长期规则**：新功能只做 Vulkan 版；`Supermodel-LegacyOG.exe`（OpenGL）冻结，
> 只接收 bug 修补。

两个可执行文件能力是编译期固定的，不是运行时开关：

| | `Model3-Vulkan.exe` | `Supermodel-LegacyOG.exe` |
| --- | --- | --- |
| 渲染后端 | Vulkan 1.3 | OpenGL（New3D） |
| 平台层 | SDL3 | SDL2 |
| 定位 | 主力版本，新功能都在这里 | 兼容/回退版本，只修 bug |

## 一、画面与渲染

- **Vulkan 渲染后端**：动态渲染、精确四边形重建、双 Subpass 半透明合成。
- **硬件 MSAA**：由 Vulkan 渲染目标提供；OpenGL 版走的是另一套 `Supersampling`，
  两者是替代关系，不是叠加。
- **后处理链**：FXAA、CRT 扫描线 / 荫罩掩模、Glow（含强度、阈值、增益）全部由
  `Vulkan2D` 的滤镜 pass 完成。
- **机柜背景（bezel）**：`BezelMode` / `BezelImage` / `BezelBrightness`，内置画作或
  自己的 PNG 填进黑边。
- **Sinden 光枪白边**：`Vulkan2D` 的独立 pass，画在 CRT/机柜之后、overlay 之前。
- **贴图过滤两档增强**（`TexFilter=1/2`）：用 `dFdxFine/dFdyFine` 的足迹椭圆选 mip，
  模式 2 沿长轴最多 4 个 tap，改善斜掠面。OpenGL 版仍是原机径向估算。
- **精确四边形插值**（`QuadInterpolation`）：逐像素解四边形曲面求交；关掉可省 ALU。
  （OpenGL 的 `QuadRendering` 是几何层的另一回事，两个键不通用。）
- **半透明三条路径**：默认 2-Subpass 片上合成；`AlphaSortMode=1` 切换成逐像素排序的
  A-Buffer（4 层/像素）；`AlphaSortMode=2` 用真机的棋盘格半分辨率合成，雪雾/尘土不会把
  后面的车糊白。4K 下前两者都约 120 Hz。
- **高清纹理包（HD texture pack）**：导出当前游戏实际绘制的子纹理为 RGBA8 PNG，
  用任意倍数的 PNG/WebP 替换，内容哈希匹配，硬件 mip 链 + 16× 各向异性；
  F4 / 面板按钮热重载。OpenGL 版没有这个功能，也读不到这些配置。
- **管线缓存落盘**：`Data/Cache/pipelines-{ui,game}.bin`，冷启动 20 ms → 热启动 8 ms。

## 二、帧率与节奏

- **高刷插值**：逻辑固定 57.52416 Hz，呈现跟随显示器刷新率，对相机和实例矩阵做
  亚帧插值；旧的 OpenGL 循环是"一仿真一呈现"。
- **VRR 整数倍频**：2× / 3× / 4× / 8×（115.05 / 172.57 / 230.10 / 460.19 Hz），
  F8 在 1×/2× 之间热切，面板不足时自动回退。
- **呈现相位校准**：校准提交截止点与 vblank 的关系，减少约 4.6 ms 端到端延迟。
- **低延迟模式**、**相机/实例切镜阈值**、**Auto / Original / VRR / Fixed 四种呈现模式**
  及其界面，全部是 Vulkan 独有。
- **读档等 3 秒**：Vulkan 版必须等游戏启动完成再装状态（否则逻辑只跑 1/25 速），
  期间用整屏启动图盖住。

## 三、界面

- **毛玻璃背景**：设置窗口和游戏内面板的背景是当前画面的双重 Kawase 模糊。
  OpenGL 版没有模糊链，玻璃面板会回退成纯色底（`UiFrost::Plate()` 的 `backdrop = 0`
  分支）—— 主题、圆角、配色两边一致，只有"透出画面"这一层是 Vulkan 独有。
- **氛围底图**：两边都有，只是实现不同 —— Vulkan 用着色器采样封面，OpenGL 把封面
  当图片交给 ImGui（`UiFrost::AmbientWash` 的注释写明了这一点）。
- **设置界面里的 Vulkan 专属分组**：硬件预设（分辨率 + MSAA + 半透明排序 + 贴图过滤
  四档一键）、呈现节奏、后处理、机柜背景、光枪白边、高清纹理。
- **管理存档**（`SUPERMODEL_PURE_VULKAN`）：封面卡片 + 画面缩略图 + 导入旧版 `.st`。

## 三之二、仿真器核心

- **PowerPC 执行模式**（`核心` → `PowerPCMode`）：x86-64 混合模式基础块 JIT。
  常用指令编译成原生机器码，其余调用解释器自己的 C 处理函数，两条路径逐位等价。
  设置界面可在 `JIT 混合模式（推荐）` 与 `C 纯解释器（对照基准）` 之间切换，下一局生效；
  命令行 `-ppc-mode=0|1`，环境变量 `SM3D_PPC_JIT=0|1` 优先级最高。
  OpenGL 版冻结、不含这套代码（`Supermodel-LegacyOG.exe` 二进制里没有 JIT），所以没有这个开关。
- **wait-loop 跳过**（`SM3D_IDLE_SKIP=0` 可关）：向后分支的寄存器状态重复时把剩余周期交给
  时基与递减器。两条执行路径共用同一套状态机。

## 四、平台层（SDL3）

- **联机不需要 SDL3_net**：`TCPSend`/`TCPReceive` 走原生 socket（WinSock2 / BSD），
  协议不变，可以和 SDL2 版互连。
- **力反馈**：SDL_Haptic 的恒力 / 回中 / 阻尼，20 ms 攻击包络平滑。
- **多鼠标 / 双光枪**：按 `SDL_MouseID` 分设备；Windows 普通窗口下平台层自己注册
  RawInput。
- **拖动窗口自动暂停 + 音频 flush**：避免模态消息循环里环形缓冲重播旧样本。

## 五、存档格式（差异而非优劣）

- Vulkan 版写 `Data/Saves/<set>.zst<slot>`（zstd 容器，带缩略图），
  旧版 `.st` 仍可读；OpenGL 版写 `Data/Saves_Legacy/<set>.st<slot>`。
- **两边的存档不通用**（目录和容器都不同）。`.zst` 要转成 raw `.st` 才能给
  LegacyOG 用（`Tools/Capture-Frame.ps1 -RawState` 就是这么做的）。

## 六、两边完全一样的东西

New3D 的 OpenGL 渲染本身、Sinden 以外的输入映射、游戏列表/封面、中英文界面与主题配色、
启动图、统计浮层（CPU/GPU/音频）、每游戏默认音量、本地双机自连、
`Data/Assets/` 共享（封面、HD 纹理目录结构）。

---

## GitHub Release 说明（可直接粘贴）

```markdown
### Two builds, and what each one has

`Model3-Vulkan.exe` is the main build: SDL3 + a Vulkan renderer. All new work lands
there. `Supermodel-LegacyOG.exe` is the OpenGL-only build, kept for machines and driver
stacks where Vulkan is a problem; from now on it receives bug fixes only.

**Vulkan build only**

- Vulkan 1.3 renderer: exact per-pixel quad reconstruction, a two-subpass translucent
  composite, an optional per-pixel alpha sort (4 layers/pixel), and an arcade-checkerboard
  mode that reproduces how the real board resolves dense transparency.
- Hardware MSAA, plus a post chain of FXAA, CRT scanlines / shadow mask and glow.
- Cabinet bezel backdrop (built-in art or your own PNG) and the Sinden light-gun border.
- Two enhanced texture-filtering modes that pick the mip level from the real per-pixel
  footprint instead of the arcade's radial estimate.
- HD texture packs: dump the sub-textures a game actually draws, replace them at any
  scale (PNG/WebP, content-hash matched, hardware mip chains, 16x anisotropy), reload
  with F4 without restarting.
- High-refresh presentation: the game keeps running at its native 57.52416 Hz while the
  display is fed at its own rate, with sub-frame camera interpolation, VRR integer
  multiples (2x/3x/4x/8x), F8 to swap 1x/2x live, and present-phase calibration.
- Frosted-glass panels: the settings window and the in-game panel blur the frame behind
  them. The OpenGL build falls back to a solid tint.
- Save-state management with cover cards and frame thumbnails (zstd `.zst` container).
- SDL3 platform layer: native-socket netplay (no SDL3_net), SDL_Haptic force feedback,
  per-device mice for dual light guns, and automatic pause while the window is dragged.

**OpenGL build**

- The classic New3D OpenGL renderer with Supersampling, the original texture path and the
  original one-simulation-per-present loop. Same game list, covers, input mapping,
  translations, themes and statistics overlay as the Vulkan build.
- Save states use the legacy `.st` container in `Data/Saves_Legacy/`; the two builds do
  not share save files.
```

## Reddit 发帖草稿

```markdown
**Title:** Supermodel-VK: two builds now — Vulkan (new features) and OpenGL (bug fixes only)

I maintain a personal fork of Supermodel (the Sega Model 3 emulator). It ships as two
executables, and the split is compile-time, not a checkbox:

- **Model3-Vulkan.exe** — SDL3 + Vulkan. Everything new goes here.
- **Supermodel-LegacyOG.exe** — OpenGL only. From now on it gets bug fixes, not features.

What only the Vulkan build has:

- HD texture packs — dump the sub-textures a game draws, upscale them in whatever tool you
  like, drop them back in as PNG/WebP, and it hash-matches them at any scale. F4 reloads
  the folder without restarting. This is how the Daytona 2 pack in the screenshots was made.
- High-refresh presentation: the game still runs at its native 57.52416 Hz, but the display
  is fed at its own rate, with sub-frame camera interpolation and VRR 2x/3x/4x/8x.
  F8 swaps 1x/2x live.
- A Vulkan 1.3 renderer with exact per-pixel quad reconstruction, a two-subpass
  translucent composite, optional per-pixel alpha sorting, hardware MSAA, and an
  FXAA / CRT / glow post chain.
- Cabinet bezel backdrops, the Sinden light-gun border, frosted-glass UI panels, and
  save management with cover cards and thumbnails.
- SDL3 platform bits: native-socket netplay, SDL_Haptic force feedback, per-device mice
  for dual light guns.

The OpenGL build still gets the same games, covers, input mapping, translations, themes
and statistics overlay — just the classic New3D rendering path and the original pacing.
Save states are per-build (`.zst` vs legacy `.st`), so they are not interchangeable.

If you only want one thing from this post: **use the Vulkan build unless it misbehaves on
your machine, and tell me if it does** — that is the one that will keep getting features.
```
