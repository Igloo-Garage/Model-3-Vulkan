# Supermodel-VK 架构审计与进度评审

> 归档日期：2026-10-22  
> 审计方式：静态源码检索与交叉核对。未启动模拟器、未跑构建，因此下文的“已实现/未实现”均指源码事实，不代表运行验证结果。  
> 审计范围：`Src/Graphics`、`Src/OSD`、`Src/Sound`、`Src/Inputs`、`Src/Model3`、`CMakeLists.txt`、`HostCore/`。  
> 另一个根目录 `NamcoSystem22` 与本审计无关，未纳入。  
> 本文不包含可直接粘贴的修改代码，只给结论、证据与切入点。

---

## 0. 先校准三个前提

这三点会直接影响后面 Part 2/Part 3 的判断：

1. **`LegacyOG` 与 `Vulkan-SDL3` 不是两个 Git 分支，而是同一工作树里的两个构建目标、两个独立进程。**
   - `CMakeLists.txt` 明确写了：
     - `Supermodel-LegacyOG` = SDL2 + OpenGL only
     - `Model3-Vulkan` = SDL3 + Vulkan 1.3 only
   - 两者共享 `supermodel_core` 的模拟核心源码（`Src/Model3`、`Src/Sound`、`Src/CPU`、`Src/Inputs` 等），但各自编译一份静态副本。
   - 因此大量“核心修复”本来就会同时进入两个 exe，不存在传统意义上的 cherry-pick 回迁。

2. **C# 不是应用层，也不是“C# 架构”。**
   - `HostCore/` 是可选的原生 AOT 冷路径模块，只做：
     - `.ini` 解析探针
     - `.zst<slot>` 存档容器（如 `daytona2.zst1`；zstd + 缩略图 + 元数据；早期 `.vkstate` 后缀仍兼容）
   - 真正的热路径仍是 C++：`Src/`。
   - `HostCore/Model3VulkanHostCore.csproj` 使用 `net11.0`、`PublishAot`、`NativeLib=Shared`；`HostCore/Exports.cs` 只导出 C ABI。
   - `HostCore/Contracts.cs` 里的 `MousePollingHz`、`MouseFilter`、`RecenterKey`、`SteamDeckBackButtons`、`CrtShaderName` 等字段目前**没有 C++ 消费者**，只在 `InitializeSDL3Startup()` 里打印一次日志。它们是迁移脚手架，不是已生效功能。

3. **Vulkan 后端不是“必须 1.3”。**
   - `CVulkanContext::ClassifyDevice()` 接受：
     - Vulkan 1.3 core
     - Vulkan 1.2 + `VK_KHR_dynamic_rendering` + `VK_KHR_synchronization2`
   - 同时 `CreateDevice()` 还强制要求 descriptor indexing 的一组 1.2 特性，因此实际硬件下限比“Vulkan 1.2”更具体，见 Part 2。

---

# Part 1：已实现核心功能全盘点

## 1.1 构建与架构分层

| 项目 | 事实 | 源码证据 |
|---|---|---|
| 双目标物理隔离 | Vulkan 源文件只进 `supermodel_core_vk`，GL 源文件只进 `supermodel_core` | `CMakeLists.txt` 的 `SUPERMODEL_CORE_VULKAN_SOURCES` / `SUPERMODEL_CORE_GL_SOURCES` |
| 后端无关核心 | `supermodel_core` 不链接 SDL2/SDL3，不包含 GL/Vulkan 头 | `CMakeLists.txt` 注释与 `supermodel_common` |
| 每目标一个渲染族 | SDL2 目标定义 `SUPERMODEL_OPENGL_ONLY`；SDL3 目标定义 `SUPERMODEL_PLATFORM_SDL3` + `SUPERMODEL_PURE_VULKAN` | `target_compile_definitions` |
| C++ 标准 | Legacy 目标 C++17；SDL3 目标 C++23 | `set_target_properties(... CXX_STANDARD 23)` |
| C++23 启动层 | `std::expected`、`std::format`、`std::print`、`std::stacktrace` 只在 SDL3 目标编译 | `Src/OSD/SDL3/AppRuntime.cpp`、`HostCoreBridge.cpp`、`UiVulkanBackend.cpp` |
| 两个 exe 的配置分离 | SDL3 使用 `<exe stem>.ini`，Legacy 使用历史 `Supermodel.ini` | `AppRuntime::DeriveRuntimePaths()`；`build.bat` 也保留两份 ini |

**架构评价**：渲染器边界切得很干净，`Model3`/`SCSP`/`DSB` 不知道图形 API 的存在。这是后续能安全重构的前提。

---

## 1.2 图形与渲染管线

### Vulkan 后端已落地

| 功能 | 状态 | 关键源码 |
|---|---|---|
| Real3D 场景解析 | 已实现，复用 New3D 的纯 CPU 数据结构 | `Src/Graphics/Vulkan3D/R3DSceneBuilder.cpp`、`Src/Graphics/New3D/Model.h` |
| 视口优先级 0–3、每视口矩阵/雾/聚光灯 | 已实现 | `CR3DSceneBuilder::RenderViewport()`、`CVulkan3D::DrawScene()`、`DrawFog()` |
| 反向 Z + 浮点深度 | 已实现 | `Mat4::FrustumRZ()`；`real3d.frag` 的 `gl_FragDepth = projection[3][2] * gl_FragCoord.w` |
| 硬件 MSAA 2/4/8 | 已实现，带驱动能力检查 | `CVulkan3D` 构造函数、`EnsureTargets()` |
| 透明层 trans1/trans2 | 已实现，两个 pass，深度写开启 | `New3D::Mesh::Render()`、`CVulkan3D::CreatePipeline()` |
| 微纹理 Microtexture | 已实现 | `real3d.frag` 的 `kMicro`、`GetMicroTexturePos()`、`blendFactor` 逻辑 |
| 雾、聚光灯、太阳 clamp | 已实现 | `real3d.frag` 第 388–444 行；`CVulkan3D::FillPush()` |
| 宽屏视口矩阵修正 | 已实现，并修正了 IntegerScale 下的宽高比错误 | `CR3DSceneBuilder::CalcViewport()` |
| Bindless 纹理槽 | 已实现，最多 4096 槽 | `CVulkanTextureCache::Acquire()`、`VulkanCommon.h` 的 `kMaxTextureSlots` |
| GPU 计算纹理解码 | 已实现，push descriptor + storage image；不支持时回退 CPU | `VulkanTextureCache::CreateComputePipeline()`、`RecordDecode()`、`CpuDecode()` |
| 2D Tilegen 合成 | 已实现，底层/3D 场景/顶层三层合成 | `CVulkan2D::DrawCompositePass()`、`UploadLayer()` |
| 后处理 FXAA | 已实现 | `Shaders/post.frag` |
| CRT 扫描线 + 磷光掩膜 + glow + gain | 已实现 | `post.frag`、`CVulkan2D::CreatePostPipeline()` |
| 机柜背景图 Bezel | 仅 Vulkan | `CVulkan2D::DrawBezelPass()`、`BezelLayout.h` |
| 光枪准星原生绘制 | 仅 Vulkan 的 pure build | `CVulkan2D::SetCrosshair()`、`DrawCrosshairPass()` |
| 截图/缩略图异步回读 | 已实现 | `CVulkanContext::RequestScreenshot()`、`RequestThumbnail()`、`WriteScreenshot()` |
| ImGui 游戏内面板 | 已实现，Vulkan 原生 pass | `CVulkan2D::SetOverlayPass()`、`DrawOverlayPass()` |

**Vulkan 管线特征**：

- Dynamic Rendering + Synchronization2（1.3 core 或 1.2 扩展）
- `VK_KHR_push_descriptor` 可选
- Descriptor Indexing 必需
- SPIR-V 1.5，shader 以 `.spv.h` 内嵌，无运行期 GLSL 编译
- `kMaxFramesInFlight = 2`
- 每帧 ring buffer 64 MB，双帧共 128 MB
- ROM 顶点缓存上限 `6,000,000` 顶点，`R3DVertex` 64 B，理论上限约 384 MB 显存

### OpenGL 后端已落地

| 功能 | 状态 | 关键源码 |
|---|---|---|
| New3D 渲染器 | 已实现，GLSL 410 core，VBO，GPU 纹理页 | `Src/Graphics/New3D/New3D.cpp`、`R3DShader*.h` |
| Legacy3D 渲染器 | 已实现，旧式 VBO + 自定义 shader | `Src/Graphics/Legacy3D/Legacy3D.cpp` |
| 超采样 SuperAA 1–8x | 已实现 | `Src/Graphics/SuperAA.cpp` |
| CRT 色温/色域矩阵 | 已实现，5 组 D93/D65/BT.601 矩阵 | `SuperAA.cpp` 内嵌 fragment shader |
| 2D 放大模式 None/Biquintic/Bilinear/Bicubic | 已实现，仅 OpenGL | `CRender2D::Init()`、`UpscaleMode` |
| QuadRendering | 已实现，仅 New3D | `CNew3D` 构造函数、`R3DShaderQuads.h` |
| ImGui 面板 | 已实现，OpenGL3 backend | `Gui.cpp`、`Overlay.cpp` |

**合成方式差异**：

- OpenGL：New3D/Legacy3D 渲染到 `aaTarget` FBO → `CRender2D` 画 Tilegen 层 → `SuperAA::Draw()` 做超采样/CRT 色矩阵 → swap。
- Vulkan：`CVulkan3D` 渲染到 MSAA 颜色 + D32 深度 → resolve 到 `m_colorResolve` → `CVulkan2D` 按 bottom layer / scene / top layer / bezel / post / overlay / crosshair 顺序合成 → present。
- Vulkan 的 2D 层采样固定为线性过滤，`UpscaleMode` 被忽略；这是与 OpenGL 的一个明确差异。

---

## 1.3 显示与帧同步

| 机制 | 事实 | 源码 |
|---|---|---|
| Model 3 原生帧率 | 固定 `57.524160 Hz` 计算 PPC 每帧周期 | `CModel3::RunMainBoardFrame()` |
| 每帧音频样本 | `44100 / RefreshRate`，带小数进位 | `GetSamplesThisFrame()` in `Src/OSD/SDL/Audio.cpp` |
| 默认刷新率 | `RefreshRate = 60.0f` | `DefaultConfig()` |
| 街机精确模式 | `ExclusiveRefreshRate = 57.5242` 或 `ExclusiveFullscreen = 1` | `ResolveFullScreenMode()` |
| VRR 判断 | 读取 Windows EDID timing range，判断是否包含 57.524 Hz | `GetDisplayTimingInfo()`、`RateSupportForTiming()`、`ConfiguredDisplayRateSupport()` |
| 独占全屏 | 57/58 Hz 显示模式优先；找不到则回退 G-SYNC/VRR 或 borderless | `FindArcadeDisplayModeImpl()`、`ResolveFullScreenMode()` |
| Swapchain 呈现模式 | VSync 开 = FIFO；关 = MAILBOX，否则 IMMEDIATE | `CVulkanContext::CreateSwapchain()` |
| 帧队列 | `kMaxFramesInFlight = 2`，swapchain image 数 `minImageCount + 1`，MAILBOX 至少 3 | `VulkanCommon.h`、`CreateSwapchain()` |
| 帧限速 | 绝对 deadline 递增，不按 `Now()` 重基准；落后时重同步 | `Main.cpp` 主循环 4314–4341 |
| 睡眠 | SDL3：`SleepUntil` 0.5 ms spin；SDL2：1 ms 步进 + 2 ms spin | `Src/OSD/Platform/Platform.h` |
| 帧节奏统计 | FPS、1% low、cadence、poll-to-render、poll-to-present | `StatsOsd.cpp`、`SM3D_FRAME_TIMING` |

**必须指出的关键事实**：

`CModel3::RunMainBoardFrame()` 的 PPC 周期预算始终按 57.524160 Hz 计算，而主循环默认按 `RefreshRate = 60.0f` 限速。  
因此**默认配置下游戏逻辑跑快约 4.3%**；只有启用 57.5242 独占模式或显示器 VRR 跟随，才是 1:1 街机速度。`Main.cpp` 的注释已经明确承认这一点。

另外：

- 没有 `VK_KHR_present_wait`、`VK_EXT_swapchain_maintenance1`、`VK_NV_low_latency2` 的使用。
- VRR 不是通过 Vulkan 显式开启，而是依赖独占全屏 + FIFO + 驱动/显示器跟随 present rate。
- `Throttle` 与 `VSync` 是两个独立开关；关 VSync 时仍可由 `Throttle` 限速。

---

## 1.4 音频子系统

### SCSP / DSP

| 项目 | 事实 | 源码 |
|---|---|---|
| DSP 单一实现 | 只剩一条 `SCSPDSP_Step()` 路径 | `Src/Sound/SCSPDSP.cpp` |
| `0x7C0–0x7FF` 保留空洞 | `SCSP_w8/w16/w32` 写路径均丢弃，不再镜像进 `MADRS` | `SCSP.cpp` 1024–1142 |
| `SCSP_w32` 下溢保护 | 显式 `else if (addr < 0x800)` 空分支 | `SCSP.cpp` 1131–1137 |
| 效果声道电平索引 | `EFSDL << 13` | `SCSP_DoMasterSamples()` |
| 读路径不对称 | `SCSP_r16()` 仍把 `0x7C0–0x7FF` 镜像到 `MADRS[0..31]` | `SCSP.cpp` 1206–1207 |
| LegacySoundDSP | 仍解析但忽略 | `Main.cpp` `RunGameSession()` |

`SCSP_r16()` 的读镜像是一个值得单独确认的审计项：写路径已经按“保留空洞”处理，读路径却仍然返回 `MADRS` 内容。两者语义不一致；如果硬件是 open-bus/0，建议后续用 DSP 程序行为或硬件测试确认后再统一。

### DSB

| 项目 | 事实 | 源码 |
|---|---|---|
| 32k→44.1k 重采样 | 64-tap Kaiser-windowed sinc，320:441 有理比，441 相位 | `CDSBResampler::Prepare()` |
| 相位推进 | 整数相位累加，无浮点漂移 | `CDSBResampler::UpSampleAndMix()` |
| 帧尾 look-ahead | `MpegSamplesForFrame()` 额外加 `DSB_RESAMPLER_TAPS + 4` | `DSB.h` |
| 每帧严格产出 | 输出恰好 `numSamples`，输入按 320/441 消费 | `CDSBResampler` |
| Sega Rally 2 双 BGM 路由 | `StereoMode` 0/1/2；`0xA0`→Channel0（mpegR），`0xB1`→Channel1（mpegL）；`0x24` 不重置路由 | `DSB.h`、`CDSB2::WriteMPEGFIFO()`、`CDSB2::RunFrame()` |
| DSB1 单声道 | `stereo` 寄存器被保存，但 `CDSB1::RunFrame()` 始终同时混 mpegL/mpegR | `DSB.cpp` |
| 浮点输出 | DSB 输出 float，S16 标度，与 SCSP 相加不转换 | `CDSBResampler` 注释与实现 |

### 主机音频输出

| 项目 | SDL2 / Legacy | SDL3 / Vulkan |
|---|---|---|
| 设备打开 | `SDL_OpenAudioDevice` / `SDL_OpenAudio` | `SDL_OpenAudioDeviceStream` |
| 样本格式 | `AUDIO_S16SYS` | `SDL_AUDIO_F32` |
| 回调 | SDL2 audio callback | `SDL_SetAudioStreamGetCallback` |
| 转换 | SDL/driver | `SDL_AudioStream` 内部转换到设备格式 |
| 缓冲 | `AudioLatency` 默认 40% | 同 |
| 欠载/过载计数 | `GetAudioBufferStats()` | 同 |

**“动态重采样”现状**：

- 内部只有 **固定比率** 32k→44.1k 多相重采样。
- 每帧样本数会随 `RefreshRate` 变化，但重采样比率本身不随音频缓冲水位调整。
- SDL3 的 `SDL_AudioStream` 会做设备格式/采样率转换，但这是 SDL 内部行为，不是模拟器的动态 ASRC。
- 没有基于 under/over-run 的闭环比率控制。

### 混音与音量

| 项目 | 事实 | 源码 |
|---|---|---|
| SCSP 内部 | 整数累加，输出前 `ICLIP18` 或 `ICLIP16` | `SCSP_DoMasterSamples()` |
| DSB | float，S16 标度，直接加到 SCSP 缓冲 | `CDSBResampler::UpSampleAndMix()` |
| 4→2 downmix | `MixFloat(x,y) = (x+y)*0.5` | `Audio.cpp` |
| 最终钳位 | SDL3 在 `ToHostSample()` 归一化到 -1..1 | `Audio.cpp` |
| Per-game 音量 | 硬编码表：scud 65/100、daytona2 70/100、swtrilgy 100/70、spikeout 75/100 等 | `Main.cpp` `s_perGameAudioBalance` |
| 优先级 | 命令行 > `[game]` ini 段 > 全局 ini（仅当偏离默认）> 内置默认 | `ApplyPerGameAudioBalance()`、`HasExplicitAudioVolume()` |

### 声音线程

- `AsyncSoundBoardThread = true` 默认开启。
- `CModel3::StartThreads()` 创建 `SoundBoardNoSync` 线程；音频回调只负责 `WakeSoundBoardThread()`。
- 声音线程在 `RunSoundBoardThread()` 中循环 `RunSoundBoardFrame()`，直到 `IsAudioBufferFull()`。
- 优点：主循环卡顿（拖窗口、GPU present 阻塞）不会中断声音。
- 代价：声音板可以领先主线程最多一个音频缓冲的时间；对 run-ahead/确定性回放不友好。

---

## 1.5 平台、输入、配置与网络

### 平台层

- `Src/OSD/Platform/Platform.h` 提供窗口、显示、时钟、睡眠、GL 上下文、Vulkan surface 的抽象。
- `PlatformSDL2.cpp` / `PlatformSDL3.cpp` 分别实现。
- 游戏窗口隐藏创建，首帧 present 后显示；设置窗口有启动图覆盖。
- SDL3 启动层使用 `std::expected` 做路径解析，失败时弹 SDL message box。
- SDL3 设置窗口使用 `UiVulkanBackend`，ImGui 直接画到 swapchain。

### 输入

| 能力 | LegacyOG | Vulkan-SDL3 |
|---|---|---|
| 输入系统 | SDL2 / SDL GameController / DirectInput / XInput / RawInput | SDL3 Gamepad / Joystick |
| Windows 默认 | `xinput` | 无 DirectInput，`Gui.cpp` 强制回退到 `sdlgamepad` |
| 力反馈 | DirectInput constant/spring/friction/vibrate；SDL2 haptic | SDL3 haptic constant/spring/friction/vibrate；恒力 20 ms attack 平滑 |
| 多鼠标 | RawInput 路径支持 `GetNumMice()`、每设备 `m_rawMseStates` | 按 `SDL_MouseID` 每设备表 + Windows RawInput；`MOUSE1_`/`MOUSE2_` 可用 |
| 光枪 | 绝对坐标轴 + `PollPointerLate()` | 同左，支持双枪；新增可配置 Sinden 白边 |
| 控制预设 | `standard` / `keyboard` / `custom`，按控制器族自动检测 | 同左，但只支持 SDL Gamepad 族 |

关键源码：

- `CSDLInputSystem::ProcessForceFeedbackCmd()`：SDL3 分支已完整处理 constant/spring/friction/vibrate/stop。
- `PlatformInputSDL3.cpp`：按 `SDL_MouseID` 的每设备表；Windows RawInput 线程补齐普通窗口下的设备区分。
- `CVulkan2D::DrawBorderPass()`：最终输出上的 Sinden 白边，可调宽度/亮度。
- `Src/Network/NativeSocket.*`：SDL3 的 `TCPSend` / `TCPReceive` 原生 socket 实现。
- `CDirectInputSystem::CreateJoystickEffect()`：Windows Legacy 的完整 DirectInput 效果。
- `CInputs::PollPointerLate()`：在 `RunFrame()` 前重新采样光枪轴。
- `CInputs::ApplyGameSpecificInputRanges()`：Lost World 的 ADC 校准范围。

### 配置

合并顺序（`Main.cpp` `RunGameSession()`）：

```text
DefaultConfig
  < 全局 ini
  < 命令行
  < [game] ini 段
  < 命令行（再次覆盖）
```

最终优先级是：**命令行 > 游戏专属段 > 全局 ini > 默认值**。

`Games.xml` 负责 ROM 集元数据、输入类型、硬件 stepping、ROM 布局；不是用户偏好文件。  
目前没有“每游戏 Profile 编辑 UI”；游戏专属段需要手工写 ini。

### 网络

| 项目 | LegacyOG | Vulkan-SDL3 |
|---|---|---|
| 真实 TCP 联机 | SDL2_net | 默认无；`USE_SDL3_NET=ON` 才链接 |
| 单机自连 | `CSimNetBoard` 本地回环 | 同左 |
| `TCPSend`/`TCPReceive` | 真实 socket | `SUPERMODEL_PLATFORM_SDL3` 下为空实现 |

`SimNetBoard::Init()` 识别 `PortIn == PortOut && AddressOut == 127.0.0.1` 后走进程内回环，不依赖 socket。  
因此 Vulkan-SDL3 默认只能单机通过 NETWORK CHECKING，不能多机联机。

### 存档

- Legacy：传统 `.st` + NVRAM `.nv`.
- Vulkan pure：`HostCore` 的 `<game>.zst<slot>` 容器（如 `daytona2.zst1`；zstd 压缩 + 缩略图 + JSON 元数据），失败时回退传统 `.st`。
- 早期版本使用 `.vkstate` 后缀，`VkState.cs` 仍会扫描并加载；临时文件是 `.vktmp.st` / `.vkstmp.st`，缩略图暂存为 `.vkthumb.png`。
- 16 个快速存档槽；`F5`/`F7`；存档缩略图异步生成。

---

# Part 2：LegacyOG 与 Vulkan-SDL3 深度差异对比

## 2.1 对比矩阵

| 维度 | LegacyOG（SDL2 + OpenGL） | Vulkan-SDL3（SDL3 + Vulkan） |
|---|---|---|
| **构建目标** | `supermodel_core` + `PlatformSDL2` | `supermodel_core_vk` + `PlatformSDL3` |
| **语言标准** | C++17 | C++23（仅 SDL3 目标） |
| **图形 API** | OpenGL 4.1 core | Vulkan 1.3 core，或 1.2 + Dynamic Rendering + Synchronization2 |
| **渲染器** | New3D + Legacy3D 可选 | 只有 Vulkan 版 New3D 场景解释器；无 Legacy3D |
| **AA** | SuperAA 1–8x 超采样 | 硬件 MSAA 2/4/8；另可选 FXAA |
| **CRT 色矩阵** | `CRTcolors` 1–5，SuperAA 内嵌 shader | 无；只有扫描线/掩膜/glow/gain |
| **后处理** | 无扫描线/掩膜/glow；只有超采样 + 色矩阵 | FXAA + scanline + phosphor mask + mip-chain glow + gain |
| **2D 放大** | None/Biquintic/Bilinear/Bicubic | 固定线性过滤，忽略 `UpscaleMode` |
| **QuadRendering** | 支持（New3D 几何 shader） | 不支持，始终 triangle list |
| **纹理解码** | CPU 解码 + GL 纹理页 | GPU compute 解码；无 push descriptor/storage image 时回退 CPU |
| **纹理绑定** | 两个 `usampler2D` 页 | Bindless 槽数组，最多 4096 |
| **Bezel** | 无 | 有，且 widescreen 时自动不加载 |
| **准星** | `CCrosshair`，BMP/vector | `CVulkan2D` 原生 vector；`CrosshairStyle` 无效 |
| **MSAA 与 LOS** | 无此冲突 | MSAA >1 时 `CopyLos()` 直接跳过，LOS 读数不可用 |
| **Swapchain** | `SDL_GL_SwapWindow` + swap interval | FIFO / MAILBOX / IMMEDIATE；`kMaxFramesInFlight=2` |
| **VRR** | 依赖驱动/独占全屏 | 同左；EDID 判断代码是共享的 |
| **音频格式** | S16 callback | F32 `SDL_AudioStream` + get callback |
| **音频线程** | async sound board 可选 | 同左 |
| **FFB** | DirectInput 完整效果 + SDL2 haptic | 仅 rumble；constant/spring/friction 不支持 |
| **多鼠标/光枪** | RawInput 支持每设备鼠标 | 仅全局单指针；事件 `which` 未使用 |
| **真实联机** | SDL2_net | 默认 stub；需 `USE_SDL3_NET=ON` |
| **存档** | `.st` | `.zst<slot>`（C# HostCore；如 `daytona2.zst1`，早期 `.vkstate` 兼容）+ `.st` 回退 |
| **配置** | `Supermodel.ini` | `Model3-Vulkan.ini`（独立） |
| **硬件下限** | OpenGL 4.1 | Vulkan 1.3，或 1.2 + 两扩展 + descriptor indexing 特性 |
| **显存开销** | 超采样 FBO 随 aaValue² 增长 | ROM 顶点缓存上限约 384 MB + 64 MB×2 ring + 4096 纹理槽 |
| **性能取向** | 兼容性优先；旧 GPU、旧 Windows、DirectInput 外设 | 现代 GPU；GPU 解码/绑定更省 CPU，MSAA 更省带宽 |

## 2.2 关键差异解读

### 1）默认 60 Hz 是最大的“隐性行为差异”

- `RunMainBoardFrame()` 固定 57.524160 Hz 的 PPC 周期。
- 主循环默认按 60 Hz 限速。
- 结果：默认配置游戏逻辑快 4.3%，而音频/DSB 按 60 Hz 产生样本。
- `ExclusiveRefreshRate = 57.5242` 或 VRR 跟随才能 1:1。

这不是 Vulkan 独有，Legacy 也一样；但 Vulkan 的 `ResolveFullScreenMode()` 提供了更完整的 57.524 路径。  
**建议把“街机精确刷新率”从“可选高级项”提升为默认推荐路径**，至少在检测到 57/58 Hz 模式或 VRR 支持时自动提示。

### 2）Vulkan 的“低延迟”目前主要靠 CPU 睡眠，不是显示时序

- 有 `SleepUntil()` 的 0.5 ms spin。
- 有 `SM3D_FRAME_TIMING` 的 poll-to-render / poll-to-present 统计。
- 但没有 `VK_KHR_present_wait`，也没有 Reflex。
- `kMaxFramesInFlight = 2` + FIFO 意味着最多约 2 帧的 presentation 队列延迟。

### 3）多人联机是 Vulkan-SDL3 的功能缺口

`TCPSend.cpp` 在 SDL3 下是空实现；`CMakeLists.txt` 默认 `USE_SDL3_NET=OFF` 并定义 `SUPERMODEL_NO_NETWORK=1`。  
所以 Vulkan-SDL3 发布包只能单机自连，不能像 Legacy 那样多机联机。这是“现代分支”目前最明显的功能倒退。

### 4）力反馈是 Vulkan-SDL3 的第二个功能缺口

- Legacy 的 DirectInput 路径有 `GUID_ConstantForce`、`GUID_Spring`、`GUID_Friction`、`GUID_Sine`。
- Vulkan 的 SDL3 路径只有 `SetGamepadRumble()`，且 `VibrationEffect()` 把 low/high 设成同一个值。
- `ProcessForceFeedbackCmd()` 对 `FFConstantForce`、`FFSelfCenter`、`FFFriction` 直接返回 false。

### 5）GUI 的作用域表有过时项

`Gui.cpp` 的 `ScopeOfSetting()` 把 `Crosshairs` / `CrosshairStyle` 标成 OpenGL-only。  
但 `Main.cpp` 的 `UpdateOverlayCrosshair()` 明确在 Vulkan 下调用 `CVulkan2D::SetCrosshair()`。  
结果是：Vulkan 用户能看到自动开启的 P1 准星，却在设置界面里看到准星选项被灰掉。  
这是低风险、高收益的修复点。

### 6）C# 字段是“未来接口”，不是现状

`HostCoreConfig` 里的 `MousePollingHz`、`MouseFilter`、`RecenterKey`、`SteamDeckBackButtons`、`CrtShaderName`、`Vrr` 没有进入 C++ 行为。  
如果对外宣传“C# 架构”，容易让用户误以为这些设置已经生效。

---

# Part 3：是否有必要把 Vulkan-SDL3 的新功能反向同步给 LegacyOG？

## 3.1 先看依赖边界：大部分“回迁”其实已经发生

因为 `supermodel_core` 是共享源码，以下改动**不需要回迁**，LegacyOG 已经编译进去了：

| 改动 | 位置 | Legacy 是否已有 |
|---|---|---|
| SCSP 保留空洞写丢弃 | `Src/Sound/SCSP.cpp` | 是 |
| `SCSP_w32` 空分支防下溢 | `Src/Sound/SCSP.cpp` | 是 |
| 单一 SCSP DSP 路径 | `Src/Sound/SCSPDSP.cpp` | 是 |
| DSB 320:441 多相重采样 | `Src/Model3/DSB.cpp` | 是 |
| DSB look-ahead / 每帧样本 | `DSB.h`、`DSB.cpp` | 是 |
| Sega Rally 2 双 BGM 路由 | `DSB.cpp` | 是 |
| 每帧小数样本进位 | `Src/OSD/SDL/Audio.cpp` | 是（无平台 guard） |
| 绝对 deadline 帧限速 | `Main.cpp` | 是 |
| Per-game 音量默认 | `Main.cpp` | 是 |
| 光枪晚采样 | `Src/Inputs/Inputs.cpp` | 是 |
| 单机自连回环 | `Src/Network/SimNetBoard.cpp` | 是 |
| 配置合并/优先级 | `Src/Util/ConfigBuilders.cpp` | 是 |

**结论**：Part 3 的真正对象不是“核心模拟修复”，而是**平台层和渲染层特性**。

---

## 3.2 三类建议

### A. 强烈建议同步的“平台/渲染无关层”修复

这些应该保持两个 exe 行为一致，且应作为共享核心的回归基线：

1. **SCSP / DSP / DSB 逻辑修复**
   - `Src/Sound/SCSP.cpp`
   - `Src/Sound/SCSPDSP.cpp`
   - `Src/Model3/DSB.cpp`
   - `Src/Model3/SoundBoard.cpp`
   - 理由：这些是模拟准确性，不是渲染偏好；两套 exe 必须听感一致。

2. **音频时钟与缓冲修复**
   - `GetSamplesThisFrame()` 小数进位
   - `AudioLatency` 缓冲策略
   - `AsyncSoundBoardThread` 的丢唤醒修复
   - 理由：Legacy 用户同样会遇到欠载和爆音。

3. **帧限速与掉帧恢复**
   - `nextTime += perfCountPerFrame`，不按 `Now()` 重基准
   - 落后时重同步
   - 理由：这是所有渲染后端共用的主循环逻辑。

4. **配置合并、Per-game 音量、输入映射**
   - `MergeINISections()` 顺序
   - `ApplyPerGameAudioBalance()`
   - `ApplyControlPreset()`
   - 理由：用户换 exe 时不应重新学习一套配置规则。

5. **单机网络回环**
   - `CSimNetBoard::Init()` 的 `localLoop`
   - 理由：SDL3 没有网络栈时也能过 NETWORK CHECKING；Legacy 同样受益于“不依赖 socket”的确定性。

6. **GUI 纯逻辑修复**
   - 游戏列表、封面缓存、分隔条、tooltip、鼠标指针形状
   - 理由：这些与渲染 API 无关；但注意 `Gui.cpp` 里的 Vulkan 专用分支要跳过。

**注意**：A 类中大部分已经共享；真正需要做的是**确认没有 `#if defined(SUPERMODEL_PURE_VULKAN)` 把逻辑误包进去**，而不是再复制一份代码。

---

### B. 不建议同步的“强绑定层”特性

| 特性 | 绑定对象 | 强行移植到 Legacy 的成本 |
|---|---|---|
| Vulkan 渲染器 / `CVulkan3D` / `CVulkan2D` | Vulkan 命令缓冲、dynamic rendering、descriptor indexing、swapchain | 等于重写一套 OpenGL 后端；没有 ROI |
| MSAA + resolve | Vulkan image usage / sample count | GL 已有 SuperAA，二者不是同一算法；并行维护两套 AA 无意义 |
| `post.frag` CRT 管线 | Vulkan composite + mip chain + push constants | 要重写成 GLSL 410 + FBO 多 pass；Legacy 的 SuperAA 已覆盖色矩阵，缺的是扫描线/掩膜，可单独评估但是新功能 |
| Bezel | Vulkan2D 的 scissor 分区绘制 | GL 侧要新增一套 2D 合成；与 Legacy 的“最小兼容”定位冲突 |
| GPU 纹理解码 | Vulkan compute + storage image | GL 侧要 compute shader 或 PBO；Legacy 的 CPU 解码已够用 |
| Native crosshair | Vulkan2D pipeline | GL 已有 `CCrosshair`；重复 |
| `.zst<slot>` / C# HostCore | Vulkan pure build + C# AOT | Legacy 必须保持 `.st` 兼容；引入 C# 运行库会扩大发布体积 |
| C++23 启动层 | SDL3 目标属性 | Legacy 故意保持 C++17；回迁会破坏“老工具链可编译” |
| `SDL_AudioStream` F32 | SDL3 API | SDL2 也有 `SDL_AudioStream`，但 Legacy 的 S16 callback 是刻意保留的历史行为；改它会影响所有老用户 |
| `SDL_Gamepad` 标准化 | SDL3 输入后端 | Legacy 已有 SDL GameController + DirectInput/XInput/RawInput，功能更全；没必要降级 |
| `VK_KHR_present_wait` / Reflex | Vulkan swapchain | GL 有 `SDL_GL_SetSwapInterval` / `WGL_EXT_swap_control`，但不是同一套；Legacy 不需要追求极低延迟 |

**维护成本的核心**：Legacy 的价值是“老硬件 + 老外设 + 老系统”，不是“第二个现代渲染器”。把 Vulkan 特性搬过去，会同时增加：
- 两套 shader 的视觉对齐成本
- 两套 AA 的测试矩阵
- 两套 present/同步路径的 bug 面
- 两套配置作用域的判断

---

### C. LegacyOG 的长期定位建议

**建议：功能冻结，转为“兼容性维护分支”。**

具体章程：

1. **只接收以下改动**
   - 核心模拟准确性修复（SCSP/DSB/CPU/Real3D）
   - 崩溃、安全、构建修复
   - 控制器数据库/映射修复
   - 操作系统兼容性修复
   - 配置解析安全修复

2. **不再接收**
   - 新渲染器
   - 新后处理
   - 新 UI 页面/新视觉特性
   - 新存档格式
   - 新外设抽象（除非是修复）

3. **明确保留的差异化能力**
   - OpenGL 4.1 老 GPU
   - DirectInput / RawInput / XInput 外设
   - 完整 DirectInput 力反馈
   - 真实多机联机
   - Legacy3D
   - C++17 工具链

4. **在冻结前，先把两个“现代分支必需能力”补到 Vulkan**
   - 真实联机：`USE_SDL3_NET=ON` 或自带 socket 后端
   - 力反馈：SDL3 之外的 effect backend（DirectInput/HID）
   - 否则“Legacy 冻结”会变成“Vulkan 用户永久失去联机和方向盘”。

5. **CI 策略**
   - 两个目标都必须编译通过
   - 共享核心改动必须在两个 exe 上跑同一组短会话
   - 音频 under/over 计数、帧率、存档兼容性作为回归指标

---

# Part 4：未来深度优化路线图

下面按“投入产出比”排序。P0 是低风险高收益，P1 是中等成本，P2 是研究型/长周期。

## 4.1 画面与渲染精度

### P0：Vulkan 持久化 Pipeline Cache

**现状**：应用层没有创建 `VkPipelineCache`。`Vulkan3D::CreatePipeline()`、`CreateFogPipeline()`、`Vulkan2D::CreatePostPipeline()`、`CreateCrosshairPipeline()`、`VulkanTextureCache::CreateComputePipeline()` 都传 `VK_NULL_HANDLE`。ImGui Vulkan backend 虽然接受可选 cache，但 `UiVulkanBackend` 没有提供。

**切入点**：
- `CVulkanContext` 增加 pipeline cache 生命周期
- 首次创建时从 `Data/Cache/<gpu-name>-<driver-version>.bin` 加载
- 退出/切换游戏时 `vkGetPipelineCacheData()` 写回
- 传给所有 `vkCreateGraphicsPipelines` / `vkCreateComputePipelines`
- ImGui `ImGui_ImplVulkan_InitInfo::PipelineCache`

**收益**：降低设置窗口/游戏启动时的 pipeline 编译卡顿；对驱动内部重编译也有帮助。  
**注意**：当前管线数量不多（个位数到十几个），收益主要在启动，不是每帧。

### P0：把 `CRTcolors` 色矩阵补到 Vulkan 后处理

**现状**：`SuperAA.cpp` 有 5 组 CRT 色温/色域矩阵（ARI/D93、PVM、BT.601 525/D93、BT.601 525/D65、BT.601 625/D65），Vulkan 的 `post.frag` 完全没有。  
Vulkan 用户只有扫描线/掩膜/glow，没有“日版/美版/欧版显像管色温”这一层。

**切入点**：
- `CVulkan2D::ReadPostSettingsFromConfig()` 读取 `CRTcolors`
- 在 `post.frag` 里用 push constant 或 specialization constant 选择矩阵
- 在线性空间做矩阵，再回 sRGB/输出空间
- 与现有 `PostGain` 的顺序要明确：先色矩阵，后 gain/scanline/mask

**收益**：Vulkan 与 Legacy 的 CRT 色彩表现对齐；这是最明显的“画面差异”之一。

### P1：半透明排序 / OIT

**现状**：
- `New3D::Mesh::Render()` 把透明分成 `trans1` / `trans2` 两层。
- `CVulkan3D::DrawScene()` 每层单独绘制，pipeline `depthWriteEnable = VK_TRUE`，blend 为 `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`。
- `CR3DSceneBuilder::CacheModel()` 用 `std::unordered_map<uint64_t, SortingMesh>` 按材质哈希批处理，迭代顺序不保证与原始多边形提交顺序一致。
- 因此同一透明层内多个重叠面片的绘制顺序不是硬件顺序。

**切入点**：
- 先做“顺序保真”模式：在 `SortingMesh` 外保留原始 prim 序号，透明层按序号稳定排序，而不是按 `unordered_map` 迭代顺序。
- 如果要画质优先，可做 per-pixel linked list OIT 或 weighted blended OIT：
  - storage buffer + atomic counter
  - 单独的 resolve pass
  - 只对 `trans1/trans2` 生效，opaque 不变
- 但要注意：OIT 是“更好看”，不一定是“更还原”；Real3D 硬件行为可能本来就是提交顺序混合。建议做成可选模式，并用硬件截图 A/B。

**收益**：修复透明面片互相覆盖的排序瑕疵；对车窗、火焰、烟雾、水面有效。  
**风险**：OIT 会增加带宽和显存；必须保留一个“硬件顺序”回退。

### P1：MSAA 与 LOS 的兼容

**现状**：`CVulkan3D::CopyLos()` 在 `m_samples > 1` 时直接返回，因为 D32 多重采样图像不能 resolve，也不能直接 copy。  
后果：开 MSAA 后，依赖 line-of-sight 寄存器的游戏拿不到正确读数。

**切入点**：
- 用 compute shader 读取多重采样深度在 LOS 像素的值，写入 1×1 storage buffer；或
- 在 LOS 像素做一次 1×1 的 depth-only 小 pass；或
- 用 `D24S8` + `VK_KHR_depth_stencil_resolve`，只 resolve 所需像素。
- 读取端仍然走 `CVulkan3D::GetLosValue()`，保持接口不变。

**收益**：MSAA 不再是“牺牲 LOS”的开关；对特定游戏是正确性修复。

### P1：Vulkan 顶点内存压缩 / 分段缓存

**现状**：`kMaxRomVerts = 6,000,000`，`R3DVertex` 64 B，理论上限 384 MB；`kRingSliceBytes = 64 MB`，双帧 128 MB。  
对 2 GB 显存的老卡不友好。

**切入点**：
- 顶点格式压缩：
  - UV 用 16-bit normalized
  - 法线用 16-bit / octahedral
  - `faceNormal` 可由顶点法线或几何 shader 推导的，考虑移除
  - `materialFlags` 可以移到 per-draw push constant / material index
- 或者把 ROM 顶点改为分段 buffer + LRU，而不是单一大 buffer 到 6M 才 flush。
- `m_romGpuVerts` 的诊断已经存在，可作为压力测试指标。

**收益**：显存减半甚至更多；减少老卡 OOM 和缓存 flush 导致的“模型消失一帧”。

### P2：HDR 输出

**现状**：swapchain 选 `VK_FORMAT_B8G8R8A8_UNORM` / `R8G8B8A8_UNORM`，composite 也是 `R8G8B8A8_UNORM`；`post.frag` 用 `PostGain` 在 SDR 里补偿掩膜损失。  
没有 `VK_EXT_swapchain_colorspace`，没有 HDR10/PQ/HLG。

**切入点**：
- 内部 composite 改 `R16G16B16A16_SFLOAT`
- swapchain 尝试 `VK_COLOR_SPACE_HDR10_ST2084_EXT` 或 `EXTENDED_SRGB_LINEAR_EXT`
- post 在线性光下做 glow/gain，最后 tone map 到 PQ/HLG
- 配置项 `HDRMode`，默认关闭

**收益**：CRT 掩膜/扫描线损失的光可以通过真实高亮补偿，glow 更自然。  
**限制**：Real3D/Tilegen 源是 8-bit，HDR 不会增加场景细节，只改善后处理余量；必须做 SDR 回退。

### P2：Nanao MS9 几何/聚焦模型

**现状**：`post.frag` 有束流积分扫描线、三种磷光掩膜（aperture grille / slot / shadow mask）、beam-dependent hole size，但**没有曲率、会聚误差、暗角、玻璃反射**。

**切入点**：
- 桶形畸变 LUT（screen-space warp），在 composite 采样前做 UV 变换
- RGB 三通道独立微小偏移，模拟会聚误差
- 边缘暗角 + 轻微玻璃高光
- 这些应作为可选 `CRTGeometry` 参数，不要默认开启，否则会破坏像素对齐

**收益**：更接近 Nanao MS9 的“弧面玻璃 + 荧光点”观感，而不是平面网格。

### P2：Vulkan QuadRendering 对齐

**现状**：`QuadRendering` 只在 New3D 生效；Vulkan 始终 `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`。  
如果某些游戏的四边形路径在 New3D 下更准确，Vulkan 就缺少这个选项。

**切入点**：
- 用 geometry shader 把 `lines_adjacency` 扩展为 quad；或
- 用 mesh shader（Vulkan 1.3 可选）做同样的展开
- 或者明确文档：Vulkan 只支持 triangle 路径

**收益**：渲染路径对齐；但优先级低于透明排序和内存。

---

## 4.2 声音与混音保真度

### P1：SCSP 与 DSB 的混音域统一（谨慎）

**现状**：
- `SCSP_DoMasterSamples()` 在输出前做 `ICLIP18` / `ICLIP16`。
- DSB 输出 float，直接加到 SCSP 缓冲。
- 最终 `ToHostSample()` 再钳位。

**问题**：SCSP 的中间钳位是硬件 DAC 位宽行为，不一定是 bug；但它意味着“SCSP 内部已经饱和，DSB 还能继续加”。  
**建议**：
- 先确认硬件是“每片 SCSP 独立饱和”还是“混音后饱和”。
- 如果目标是高保真模式，可提供 `HighHeadroomMix`：SCSP 用 int32/float 累加，输出前不钳位，最终统一 clamp；默认仍保留硬件钳位。
- 不要直接用 `tanh` 软削波掩盖电平问题。

### P1：Per-game downmix / 路由增益

**现状**：
- `MixChannels()` 用 `(x+y)*0.5` 做 4→2 downmix。
- `SoundBoard::RunFrame()` 把 DSB 固定混到 front。
- `Game::AudioTypes` 有 MONO/STEREO/STEREO_RL/QUAD 等模式，但部分路由仍是“front only”。

**建议**：
- 把每游戏的 4 声道路由、DSB 混入位置、前后增益写进 `Games.xml` 或独立音频 profile。
- 避免“对所有游戏统一平均”导致某些游戏整体低 6 dB。
- 用硬件录音/频谱对比校准，而不是凭听感。

### P1：DSB1 单声道路由

**现状**：`CDSB1` 有 `stereo` 寄存器，`SaveState`/`LoadState` 都保存它，但 `CDSB1::RunFrame()` 永远把 `mpegL/mpegR` 都传入。  
**建议**：
- 确认 DSB1 游戏是否使用 `0xE9` 单声道节目。
- 如果使用，按 DSB2 的 `StereoMode` 思路实现；如果不使用，至少删掉无用寄存器或注释说明。

### P2：DSB 重采样继续提升

**现状**：64-tap、Kaiser β≈8、320:441、每相位归一化、look-ahead 68。已经优于线性插值一个数量级。  
**可选**：
- 96/128-tap 以进一步压高频镜像，但收益递减，CPU 成本上升。
- 如果未来支持 48 kHz 输出，需要新增 160:147 / 147:160 比率；`CDSBResampler::Prepare()` 已经用 `gcd` 泛化，扩展成本低。
- 验证方法：20k 帧模拟，检查最小 look-ahead margin、最大缓冲占用、THD+N。

### P2：动态 ASRC（仅在测量到漂移后）

**现状**：没有基于音频缓冲水位的比率控制。  
**建议**：
- 先用 `GetAudioBufferStats()` 记录长时间 under/over-run。
- 如果设备时钟与 44.1 kHz 有稳定漂移，再在 `OutputAudio()` 前做极小比率调整（±100 ppm），或使用 SDL3 stream 的比率接口。
- 但必须遵守“一个时钟权威”：帧长仍由 `GetSamplesThisFrame()` 决定，ASRC 只做微调，不能变成第二个时钟。

### P2：外挂无损音轨（HD Audio Replacement）

**可行性**：高，但建议只做 DSB，不做 SCSP。

**切入点**：
- `MpegDec::DecodeAudio()` 输出前拦截，或 `CDSBResampler::UpSampleAndMix()` 输入前替换。
- 用 `(game.name, usingMPEGStart)` 作为 track key；`usingMPEGStart/usingMPEGEnd` 已经在 `CDSB1/2` 中维护。
- 外挂 FLAC/WAV 按 44.1 kHz 或原始 32 kHz 存放；用同一多相重采样器转换，保持帧长权威。
- 需要处理 loop start/end、seek、暂停、存档恢复。
- SCSP 采样替换涉及每个 slot 的 pitch/envelope/loop，工作量远大于 DSB，且容易破坏游戏音效，不建议第一版做。

**风险**：版权、曲目匹配、循环点、版本差异（日版/海外版/Revision）。

### P2：Per-game 音量表外置

**现状**：`s_perGameAudioBalance[]` 硬编码在 `Main.cpp`，只有 7 个 set。  
**建议**：移到 `Games.xml` 的 `<audio>` 节点或独立 `AudioProfiles.ini`，支持 sound/music/路由/增益，并保留“用户显式设置优先”的 provenance 逻辑。

---

## 4.3 极致降低输入与显示延迟

### P0：把“晚采样”从光枪扩展到所有游戏输入

**现状**：
- `Inputs->Poll()` 在帧循环顶部。
- `PollPointerLate()` 只在光枪游戏中、在 `RunFrame()` 前重新采样 gun 轴。
- 普通手柄/键盘/方向盘没有晚采样。

**建议**：
- 拆分 UI 输入与游戏输入：
  - UI 输入（面板、快捷键）保持早采样
  - 游戏输入（转向、油门、按钮）在 `RunFrame()` 前采样
- `CInputs` 增加 `PollGameLate()`，只重采样游戏输入，不重复产生边沿。
- 对开关量要保留“上一帧状态”，避免同一物理按下被算两次。

**收益**：减少 UI 构建/渲染准备造成的 1–3 ms 输入延迟；对光枪已经验证有效。

### P1：`VK_KHR_present_wait` + `VK_EXT_swapchain_maintenance1`

**现状**：`EndFrame()` 调用 `vkQueuePresentKHR` 后立即返回；主循环用 CPU 睡眠猜下一帧。  
**建议**：
- 启用 `VK_KHR_present_wait`，用 `vkWaitForPresentKHR` 知道“这一帧真正上屏”的时间。
- 用 `VK_EXT_swapchain_maintenance1` 的 present fence 管理队列。
- 在 present wait 返回后再安排输入采样/下一帧提交，避免“早提交、排队等 vblank”。
- 可选：NVIDIA `VK_NV_low_latency2`（Reflex）做驱动级低延迟。

**收益**：把帧节奏从“猜”变成“知道”；对 57.524 Hz 这种非整数刷新率尤其有价值。

### P1：1 kHz 输入轮询线程

**现状**：`PlatformInputSDL3::BeginInputFrame()` 每帧 `SDL_PumpEvents()` 一次；`CSDLInputSystem::RefreshGamepadStates()` 每帧读一次设备。  
**建议**：
- 独立线程以 1 kHz 读 `SDL_GetGamepadState` / `SDL_GetJoystickState`，写入原子快照。
- 帧边界只读取快照，不直接访问设备。
- 对鼠标/光枪，使用 SDL3 事件中的设备 ID 做每设备状态。
- 注意 SDL 事件的线程安全边界；如果 SDL3 不允许跨线程读 gamepad，使用 `SDL_HINT_JOYSTICK_THREAD` 或平台原生 HID。

**收益**：把“物理按下 → 下一帧采样”的最坏情况从 17 ms 降到 1 ms 级别；对格斗、赛车、光枪都有感。

### P2：帧节奏探针增强

**现状**：`SM3D_FRAME_TIMING` 已输出 ui/run/poll-to-render/poll-to-present，以及 sleep p50/p99/max。  
**建议**：
- 增加 present-to-present 直方图
- 增加 `vkWaitForPresentKHR` 的实际 scanout 时间
- 增加每游戏帧时间 p99，而不是全局平均

### P3：Run-Ahead / Late Latching 的可行性

**结论：暂不建议做通用 Run-Ahead。**

原因：
- `CModel3` 状态包含 PPC、68K、Z80、SCSP、DSB、TileGen、Real3D、NetBoard，保存/恢复成本高。
- `AsyncSoundBoardThread` 默认开启，声音板与主线程异步，run-ahead 会破坏音频确定性。
- Vulkan 渲染与 GPU 状态不是简单可回滚的。
- 如果要做，必须先有“单线程确定性模式 + 完整状态快照”，成本远高于收益。

---

## 4.4 现代外设生态

### P1：Direct Drive 力反馈

**状态（2026-10-23）**：基础版已实现。SDL3 走 `OSD::Platform` 的 SDL_Haptic
抽象（`OpenHaptics` / `SetHapticEffect`），恒力、回中弹簧、阻尼与 rumble 均可用；
恒力用效果自带的 20 ms attack 包络平滑 57 Hz 阶梯。下面是尚未做的进阶项。

**现状**：
- `CJoyBoard::SendConstantForce()` / `SendSelfCenter()` / `SendFriction()` / `SendVibrate()` 在每帧驱动板更新时调用，约 57 Hz。
- `CInputSystem::SendForceFeedbackCmd()` 把命令下发到输入源。
- Vulkan 的 SDL3 路径只支持 rumble，且 low/high 相同。

**建议**：
1. 抽象 `IForceFeedbackBackend`：
   - `SetConstantForce(torque, slewRate)`
   - `SetDamper(friction)`
   - `SetSpring(centerStrength)`
   - `SetEngineVibration(frequency, amplitude)`
2. Windows 上优先用 DirectInput effect（可把 `DirectInputSystem.cpp` 的 effect 部分编译进 Vulkan 目标，或做独立 DLL/COM 后端）。
3. 对 Moza/Simagic/Fanatec 等 DD 基座，DirectInput 效果不一定完整；需要厂商 SDK 或 HID 输出报告。
4. **1 kHz 更新线程**：游戏逻辑仍 57 Hz 产生目标力矩，线程在帧间做插值/斜率限制，避免 57 Hz 阶梯感。
5. 每设备力矩线性化曲线：把游戏 0–255 的强度映射到实际 Nm，而不是简单 0–100%。
6. 配置：`ForceFeedback`、`FFBStrength`、`FFBSmoothing`、`FFBDeviceProfile`。

**收益**：这是 Vulkan 分支相对 Legacy 最大的外设短板；补上后 Legacy 的“兼容分支”定位才成立。

### P1：现代光枪（Sinden / Gun4IR）

**状态（2026-10-23）**：基础版已实现。`PlatformInputSDL3` 按 `SDL_MouseID` 维护
每设备表；Windows 用自建 RawInput 线程补齐普通窗口下的绝对坐标；
`CVulkan2D::DrawBorderPass()` 提供可配置 Sinden 白边。下面是尚未做的进阶项。

**现状**：
- Legacy 的 RawInput 路径已经支持多鼠标：`GetNumMice()`、`m_rawMice`、`m_rawMseStates`、`ProcessRawInput()` 按 `hDevice` 区分。
- SDL3 路径只使用 `SDL_GetMouseState()` 全局坐标，忽略 `SDL_EVENT_MOUSE_MOTION.which`。
- 没有 Sinden 白边渲染。

**建议**：
1. **SDL3 多鼠标**：
   - `PlatformInputSDL3` 增加每设备鼠标状态表，按 `SDL_MouseID` 索引。
   - 鼠标移动/按键事件用 `which` 区分设备。
   - `CInputs` 的 gun 轴支持 `MOUSE1_XAXIS` / `MOUSE2_XAXIS` 映射（Legacy 已有解析基础）。
   - 对 Gun4IR 的绝对坐标，优先使用 RawInput/平台原生绝对模式；SDL3 的绝对鼠标模式可能不够。
2. **Sinden 白边**：
   - 在 `CVulkan2D` 的 post 之后、overlay 之前，画一个白色边框。
   - 边框必须在 swapchain 坐标，不受 CRT/bezel 影响。
   - 配置：`SindenBorder`、`SindenBorderThickness`、`SindenBorderBrightness`。
   - 只在 fullscreen / 光枪游戏启用。
3. **每游戏校准**：
   - 已有 Lost World 的 `ApplyGameSpecificInputRanges()`，可扩展为通用 per-game gun calibration。
4. **准星**：
   - Vulkan 原生 crosshair 已实现；修复 `Gui.cpp` 的 scope，使 `Crosshairs` 在 Vulkan 下可配置。

### P2：DualSense 自适应扳机 / HD 震动

**现状**：
- `GamepadInfo::hasTriggerRumble` 在 SDL2/SDL3 都被检测。
- 但没有任何 `SetGamepadTriggerRumble` 调用。
- `SetGamepadRumble(low, high, duration)` 存在，`VibrationEffect()` 把 low/high 设为同一个值。

**建议**：
1. 把游戏事件映射到左右扳机震动：
   - 换挡、ABS、路肩、碰撞
   - 与方向盘 FFB 分开，避免重复
2. 自适应扳机需要 DualSense HID 输出报告，SDL3 标准 Gamepad API 不暴露；需要 HIDAPI 后端或原生 HID。
3. HD 震动：low/high 分别映射发动机低频和路面高频；更新率至少 1 kHz，而不是每帧一次。
4. 注意：街机游戏没有“扳机阻力”语义，自适应扳机更适合作为“力反馈的补充”，不要当作刹车踏板模拟。

---

## 4.5 建议的验证矩阵

任何上述改动，至少跑：

1. **构建**
   - `Supermodel-LegacyOG` 0 errors / 0 warnings
   - `Model3-Vulkan` 0 errors / 0 warnings
2. **核心回归**
   - SCSP/DSP：Fighting Vipers 2 长跑，确认无自激
   - DSB：Sega Rally 2 双 BGM 路由、Scud Race/Daytona 2 音量平衡
   - 音频：`GetAudioBufferStats()` under/over 长期为 0
3. **帧节奏**
   - `SM3D_FRAME_TIMING` 的 poll-to-present p99
   - 57.5242 独占模式与 60 Hz 模式的 A/B
4. **图形**
   - MSAA 0/2/4/8 与 LOS 游戏
   - WideScreen + IntegerScale + Bezel 组合
   - CRT 色矩阵与 Vulkan post 的 A/B 截图
5. **外设**
   - DirectInput 方向盘、XInput 手柄、SDL3 DualSense
   - 双鼠标光枪（Legacy RawInput / Vulkan 新后端）
6. **存档**
   - `.st` ↔ `.zst<slot>` 兼容（含早期 `.vkstate`）
   - 缩略图异步写入不阻塞音频

---

# 总结

**这个项目最值得肯定的地方**：

- 渲染器与模拟核心的边界干净，两个 exe 共享同一套 CPU/Real3D/SCSP/DSB 代码。
- DSB 重采样、SCSP 保留空洞、SR2 双 BGM 路由、帧限速绝对 deadline、光枪晚采样，这些都是有真实问题背景的硬核修复。
- Vulkan 后端不是“套壳”，而是完整重写了 Real3D 场景解释、纹理解码、2D 合成和后处理。

**最需要纠正的认知**：

- 不是“两个分支”，而是“一个核心 + 两套平台/渲染外壳”。
- 大多数核心修复不需要回迁，Legacy 已经编译进去了。
- 真正需要补的是 **Vulkan 分支的联机、力反馈、多鼠标光枪**，而不是把 Vulkan 渲染器搬回 Legacy。
- C# 只是冷路径存档/配置模块，不是应用架构。

**如果只做三件事**：

1. 修 `Gui.cpp` 的 `Crosshairs` scope，并把 `CRTcolors` 矩阵补到 Vulkan `post.frag`。
2. 给 Vulkan 加持久化 pipeline cache + 通用晚采样 + 1 kHz 输入轮询。
3. 把 `USE_SDL3_NET` 和 effect-based FFB 补到 Vulkan，然后再冻结 LegacyOG。

这三步的 ROI 远高于任何“把 Vulkan 后处理移植到 OpenGL”的尝试。
