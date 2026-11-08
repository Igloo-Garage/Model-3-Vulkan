# Supermodel-VK 更新记录

## 2026-11-07

### 发布：v0.9.9.6（Windows 干净包）

- **产物**：`build-package/Model3-Vulkan.v0.9.9.6.for.Windows.zip`，18,882,967 B，SHA-256
  `ca7ad03518c4b5b2c6922df5e7caa06acdf6b8c0f866cb41357b137290a36f56`；校验文件
  `build-package/SHA256SUMS.txt`。
- **构建**：`cmd.exe //c build.bat -package`（CMake/Ninja + HostCore Native AOT）。exe 文件时间
  `21:15`；日志构建戳 `Nov  7 2026 19:18:52`（`Main.cpp` 未重编；`Gui.cpp`、`Vulkan2D`、
  `Vulkan3D`、`UiBlur`、`UiVulkanBackend` 的移植/警告修复已链接）。
- **冒烟测试**：zip 解压到干净目录后，用完整 ROM 路径启动
  `E:\EMU\model-3-romset\scuddx.zip` 跑 12 秒（命令行参数是 ROM 文件，不是 set 名），日志确认
  `Model 3-Vulkan v0.9.9.6`、`Game: '...' resolves to set 'scuddx'`、`PPC: x64 hybrid JIT enabled`、
  Vulkan 设备 NVIDIA RTX 5060 Ti、`Audio: device 'default' has 2 channel(s) ... folding the
  4-channel mix down to 2`，没有 `[Error]`/`failed` 行。
- **包内文案**：`RELEASE_NOTES.md` / `README.txt` 已换成清理掉读档延迟表述的版本（按用户要求，
  对外文案不提 3 秒）。
- **包内容**：`Model3-Vulkan.exe`、`Model3VulkanHostCore.dll`、`README.txt`、`RELEASE_NOTES.md`、
  `usermanual.md`/`usermanual_chs.md`、`Data/Config`、`Data/Assets`（63 张封面、字体、机柜图）、
  `Scripts/`；没有 ini、日志、存档、NVRAM、录像、缓存、HD 纹理包/导出。
- **默认值审计**（首次运行）：
  - 默认关闭：`RecordWav`、`LoadHDTextures`、`DumpHDTextures`、`DumpMemory`、`DumpTextures`、
    `ShowStatsOsd`、`Crosshairs`、`SindenBorder`、`VkValidation`、`ExclusiveFullscreen`/
    `ExclusiveRefreshRate`、`AlphaSortMode=1`、`InstanceInterpolation`、`MSAA`、`PostFXAA`/
    `PostCRT`/`PostGlow`、`IntegerScale`、`WideScreen`/`WideBackground`、`ForceFeedback`。
  - 默认开启：`PowerPCMode=1`（JIT）、`NbSoundChannels=4`（4.0）、`RefreshRateMode=1`（Auto）+
    `MotionInterpolation`、`ChaseCameraStabilizer=3`、`GamepadSteeringSmoothing=3`、
    `UITheme=0` + `UiFrostedGlass=1`、`BezelMode=1`、`Network`/`SimulateNet`、
    `AsyncSoundBoardThread`、`HighPriority`。
- **发布笔记**：`RELEASE_NOTES.md` 已重写为只含 v0.9.9.6 的对外正文（英文主体 + 中文摘要），
  旧的多版本笔记移到 `devdocs/RELEASE-NOTES-ARCHIVE.md`，避免 `release.yml` 的
  `body_path: RELEASE_NOTES.md` 把历史版本一起发出去。
- **文档一致性**：`Docs/README-PACKAGE.txt` 移除 LegacyOG 段落、更新 JIT/追尾镜头/力反馈/音频/
  HD 纹理亮点与 `Data/Model3Vulkan.log` 路径；`usermanual.md` / `usermanual_chs.md` 同步说明
  Windows 包只含 Vulkan 版，并修正日志路径。新增 `saves_fix.md`（存档读档根因、块布局、
  旧档回退、加载顺序与 Android 移植清单），`INDEX.md` / `AGENTS.md` 已链接。
- **Linux/macOS 发布文档**：新增 `devdocs/RUNBOOK-RELEASE-LINUX-MACOS.md`（LAN 笔记本远程编
  Linux 与 macOS Intel、Apple Silicon 走 GitHub Actions、MoltenVK/vtool/lipo、SHA256SUMS 与
  GitHub release 流程）；`Scripts/package-cmake.sh` 现在会把 `RELEASE_NOTES.md` 带进 tar 包。
- **首次运行日志**：`Main.cpp` 的 HostCore 检查在 ini 尚不存在时输出
  `host core ini: not created yet (first run).`，不再显示 `host core ini failed`（最终包冒烟输出已确认）。
- **发版决策：存档默认延迟保持 3 秒**。根因已补进存档，但全游戏矩阵尚未跑完，
  `kDeferredLoadSeconds` 仍为 `3.0`。下一版发版前跑完矩阵再决定是否改为 0，待办、矩阵
  与回滚方式见 `issues_waitlist.md` 2026-11-07 条。对外发布文案（`RELEASE_NOTES.md`、
  `Docs/README-PACKAGE.txt`）**按用户要求不提延迟**，只描述存档块本身的修复。

### 构建：macOS Intel 包（v0.9.9.6）

- **产物**：`v9.9.9.6_upload/Model3-Vulkan.v0.9.9.6.for.macOS-Intel.tar.gz`，
  23,686,678 B，SHA-256 `b7b7a74358506ca8ccb807a8e24ba283d14545591e8231ea92bb7b134d5d5849`。
- **环境**：LAN 笔记本 macOS 13.7.8（Intel i5-8365U）。Apple clang 14 编不了 C++23，用
  `~/bin/zig-cc` / `~/bin/zig-cxx`（`python3.14 -m ziglang`，Zig 0.17 / LLVM 22）；
  `cmake 4.4.4` 与 `ninja 1.13.2` 是 pip 用户目录里的包（`~/Library/Python/3.14/bin`）。
- **命令**：`cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3
  -DUSE_SDL2=OFF -DUSE_SDL3=ON -DSUPERMODEL_SDL3_PURE_VULKAN=ON`；configure 154 s，编译 5:55。
- **产物检查**：`lipo -info` 为 x86_64；Zig 默认 `minos` 13.7.8，已用
  `xcrun vtool -set-build-version macos 13.0 13.3 -output ...` 改到 13.0。解压后
  `-print-games` 显示 `Model 3-Vulkan v0.9.9.6`，MoltenVK 1.3.0 / Vulkan 1.3.313；
  包内 `Data/Config`、`Assets/Boxarts`、`Frameworks`、`README.txt`、`RELEASE_NOTES.md` 齐全，
  没有 ini/日志/存档。
- **HostCore**：笔记本未装 .NET SDK，复用 2026-10-31 CI 的 universal
  `libModel3VulkanHostCore.dylib`（HostCore 源码自 10-28 未改，11 个导出符号全部匹配）。
  macOS 13 上它缺新 Swift 运行库，启动会打印 `host core unavailable ...
  _swift_getTypeByMangledNameInContext2` 并回退 `.st`，与上一版一致。
- **本轮发现并修复的三个移植问题**：
  - `Src/OSD/SDL/Gui.cpp` 的新代码用了 MSVC 专有的 `_stricmp`（H 挡映射与
    `SM3D_BOXART_DECODER` 分支），macOS/Linux 编译失败；改为文件内
    `CaseInsensitiveCompare()`（Windows 走 `_stricmp`，POSIX 走 `strcasecmp`）。Windows 行为不变；
    已打的 Windows 包无需重建，下次构建会带上该实现。
  - Zig 的 clang 驱动在 Release 下把 `-Wdate-time` 当成错误，而启动横幅故意写构建时间戳；
    `CMakeLists.txt` 给 Clang 目标加 `-Wno-date-time`。MSVC 路径不受影响。
  - **MoltenVK 的 `VK_ERROR_FEATURE_NOT_PRESENT` 控制台警告**：macOS 会打印
    `[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Metal does not support disabling primitive
    restart.`。Metal 不能关闭 primitive restart，而 Vulkan 管线默认
    `primitiveRestartEnable = VK_FALSE`；警告只出现在 TRIANGLE_STRIP 管线（UI 毛玻璃、2D 合成、
    fog/post）。这些全屏 quad 的索引里没有 restart index，所以在 5 处 strip 管线显式设
    `VK_TRUE` 即可静默，渲染结果不变。重新打包后设置窗口重跑 20 秒，
    `grep -c FEATURE_NOT_PRESENT` 为 0。
- **流程差异已回写 `devdocs/RUNBOOK-RELEASE-LINUX-MACOS.md`**：Windows→macOS 同步后的 CRLF
  转换、pip 版 cmake/ninja 的 PATH、`vtool -output` 用法、HostCore 复用路径、日志在 exe 旁。

### 构建：Linux 包（v0.9.9.6）

- **产物**：`v9.9.9.6_upload/Model3-Vulkan.v0.9.9.6.for.Linux.tar.gz`，19,392,340 B，SHA-256
  `fd62236750152548d3c5e42feb05f83be4c99df780ceec1a4cbf001e18376a4f`。
- **环境**：同一台笔记本的 Ubuntu 24.04.4（`rex@192.168.1.67`），8 核；g++-14 14.2.0、
  CMake 3.28.3、Ninja；`libdecor-0-dev` / `libwebp-dev` 已装；.NET 11.0.100-rc.3 在 `~/.dotnet`。
- **命令**：`export PATH="$HOME/.dotnet:$PATH"` → configure（29 s）→
  `cmake --build ... --target Model3-Vulkan --parallel`（2:00）→ `--target hostcore`（16 s）→
  `bash Scripts/package-cmake.sh Linux`。
- **基线**：产物要求 `GLIBC_2.38` / `CXXABI_1.3.15` / `GLIBCXX_3.4.32`，在 CI 守卫
  （`GLIBC_2.39` / `CXXABI_1.3.15`）之内；两个二进制都是 x86-64 ELF。
- **冒烟**：解压后 `-print-games`，HostCore 正常加载
  （`Model3VulkanHostCore 1.0 (Native AOT)`），Vulkan preflight 在 Intel UHD 620 上通过
  （Mesa Vulkan 1.4），版本 `v0.9.9.6`；日志在 `Data/Model3Vulkan.log`，包内无 ini/存档/日志。
- **流程差异**：`dotnet` 不在非交互 SSH 的 PATH，必须在 **configure 前** 导出 `~/.dotnet`，
  否则 CMake 不会创建 `hostcore` 目标（`ninja: error: unknown target 'hostcore'`）。
  已写进 runbook §2.1–§2.3 与 §2.7；另补充了双系统 SSH host key 的处理。

### UI：设置窗口 Tab 行固定在顶部

- **问题**：设置窗口右侧的 Tab 行与页面内容在同一个滚动 child 里，Core / FFB 等长页面往下滚后
  Tab 行一起滚走，切换页面要先滚回顶部。
- **改动（`Src/OSD/SDL/Gui.cpp`）**：Tab 行留在外层 `Settings` child 的顶部，每个 Tab 的内容包进一个
  尺寸为「剩余空间」的 `BeginChild("##page...")`，只有页面滚动并常驻垂直滚动条；外层内容高度
  恰好等于可用高度，因此不会再产生第二个滚动条。「按键」页不套页面 child，表格自己的
  `ScrollY` 就按剩余高度算；经典主题仍沿用外层的 `Borders`/背景。
- **验证**：`build-cmake.bat` 编译通过；`sdl3_reports/2026-11-07-sticky-tabs/capture_sticky_tabs.ps1`
  在 3840×2072 窗口下截取前后对照，滚动 12 格后 Tab 行仍在顶部（`before_scroll.webp` /
  `after_scroll.webp`）。

### 吸收上游 0.4 的低风险修复：53C810 读档、线程安全、力反馈/加载器/Lost World/VF3

- **背景**：我们的树记录的上游基线是 `0.3a-2026.09.28`（commit `8b4de23`）。对照 `v0.4-20261107-git-ff92a99` 的 124 个 commit 后，先落地不改变渲染/时序语义的第 1、2、5 组。第 3 组（TileGen 总线时序，Scud Race 调色板）与第 4 组（SCSP DSP/LFO 重写）不在本轮。
- **53C810 存档（`Src/Model3/53C810.cpp`）**：新增独立块 `53C810 PCI`，保存 `baseAddress` 与 `Ctx.halt`；旧存档缺失时回退 `baseAddress = 0xC0`；构造函数初始化 `baseAddress = 0`。这解决了我们一直用 3 秒延迟读档绕开的问题：新会话第一帧读档时游戏尚未写 PCI 配置，`0xF9`/`0xC1` 寄存器窗口一直被忽略，VF3/Scud Race 在 DMA 链上打转。实测 `SM3D_DEFER_LOAD_SECONDS=0` 下 `scuddx`、`vf3tb`、`daytona2` 都能正常即时读档；延迟读档不再是必需。
- **线程安全**：`Src/Model3/DSB.cpp/.h` 的 `fifoIdxW` 改为 `std::atomic<int>`（release/acquire）；`Src/Sound/SCSP.cpp` 的 `CheckPendingIRQ()` 在 `MIDILock` 下读取 MIDI FIFO 指针，并把 `SCSP_UpdateRegR()` 的调用移到解锁之后；`Src/OSD/SDL/Audio.cpp` 的 `enabled` 改为 `std::atomic<bool>`。
- **力反馈/驱动器（`Src/Model3/DriveBoard/DriveBoard.*`、`WheelBoard.*`）**：主板命令进入队列，每个命令给 Z80 看满一帧；方向盘新增伺服增益 `m_servoGain`，恒力/振动先缓存、每帧按增益发送，`WheelBoardServo` 独立存档块（旧档回退 `0x7F`）。
- **加载器（`Src/GameLoader.*`、`Config/Games.xml`）**：新增 `Region::merge`，支持非 merged clone；`srally2dx` 增加可选 DSB2 ROM 区域，`swtrilgyp` 的 `banked_crom` 设为 `merge="false"`。
- **游戏修复**：Lost World set 名 `lostwsgo` → `lostwsgp`，并实现光枪屏幕边缘 reload / off-screen 指示；VF3 启动时跳过模型矩阵为 subnormal 的视口；`CReal3D` 零初始化 `m_configRegisters`；`NUM_FF_EFFECTS` 3 → 4，修正 `FFVibrate` 的 DirectInput 数组越界隐患。
- **验证**：`build-cmake.bat`（Vulkan）与 `MSVC/Supermodel.vcxproj`（LegacyOG）均编译通过；`scuddx`/`vf3tb`/`daytona2` 即时读档截图见 `sdl3_reports/2026-11-07-upstream-125/`。`scuddx` 的灰板/CONDITION 地图仍在，符合预期：它属于第 3 组 TileGen 时序。

**Release notes**

```
en: Ported the low-risk upstream 0.4 fixes: 53C810 save states now restore the PCI base address and halt flag
    (immediate save-state loads work without the 3-second boot delay), made the DSB/SCSP/audio flags thread-safe,
    added the drive-board command queue and wheel servo gain, enabled non-merged clone ROM sets, fixed Lost World
    light-gun reloads, skipped VF3's subnormal startup viewports, and initialised Real3D's config registers.
zh: 吸收上游 0.4 的低风险修复：53C810 存档恢复 PCI 基址与 halt 标志（立即读档不再需要 3 秒启动等待），
    DSB/SCSP/音频标志线程安全化，驱动器命令队列与方向盘伺服增益，支持非 merged clone ROM 集，
    修复 Lost World 光枪重新装填，跳过 VF3 启动时的 subnormal 矩阵视口，并初始化 Real3D 配置寄存器。
```

### attract mode 音乐响度对比：上游 0.4 为什么更响

- **结论**：不是 DSP 链路差异，而是两个可量化的设置/折叠因素。
  1. Scud Race 的逐游戏平衡 `SoundVolume=65` 只缩放 SCSP；吸引模式的音乐恰好由 SCSP1（后声道）产生，
     因此被压低 `20·log10(0.65) = -3.74 dB`，而 `MusicVolume=133` 只作用于比赛内的 DSB BGM。
  2. 6 声道 5.1 流在 2 声道设备上由 SDL 的 5.1→stereo 表折叠，后声道系数（0.252/0.155）低于
     quad→stereo（0.359/0.220），再损失约 1.6 dB。
- **实测**（同一段 30 秒音乐，2 声道设备实际听感）：上游 `-27.64 dBFS`；我们默认 `-32.98 dBFS`；
  我们强制 100/100 `-29.26 dBFS`。同为 100/100 时我们的 4→2 折叠与上游 2 声道只差 +0.23 dB。
- **新增工具**：`Tools/Capture-Audio.ps1`（SDL disk 驱动抓设备前 PCM，sdl2/sdl3 通用，可关闭 disk 走真实设备）。
- **证据与试听**：`sdl3_reports/2026-11-07-upstream-125/20261107_audio_analysis.md`（完整报告）与 `audio/attract_ab_compare.wav`。
- **未改代码**：没有改逐游戏平衡或折叠方式；想恢复上游响度可在 ini 给 `[scuddx]` 明确写 `SoundVolume=100`。

**Release notes**

```
en: Documented why Scud Race's attract-mode music is quieter than upstream 0.4: the per-game
    SoundVolume=65 preset only scales the SCSP bus (where attract music lives), and SDL's
    5.1-to-stereo fold costs another ~1.6 dB on a stereo endpoint. No engine change.
zh: 记录 Scud Race 吸引模式音乐比上游 0.4 轻的原因：逐游戏 SoundVolume=65 只缩放 SCSP（吸引音乐所在总线），
    以及 SDL 的 5.1→立体声折叠在 2 声道设备上再损失约 1.6 dB。未改引擎代码。
```

## 2026-11-04

### 音频：原生 4.0 / 5.1 物理多声道输出 + 智能 5.1 硬件上混器 + 内置无锁 WAV/FLAC 多声道录制器

- **目标**：彻底释放 Model 3 双 SCSP + DSB 四声道街机座舱音频潜力，支持物理 4.0 Quad / 5.1 Surround 声卡直出、5.1 中置/重低音智能合成，以及面向社区视频创作者的无损多声道内录（解决外部录屏软件强制压成 2 声道的问题）。
- **实现（`Src/OSD/SDL/Audio.cpp`、`Src/OSD/Audio.h`、`Src/OSD/FileSystemPath.*`、`Gui.cpp`、`Main.cpp`、`Localization.cpp`）**：
  1. **原生 4.0 Quad 与 5.1 Surround 帧交错输出**：
     - `NbSoundChannels` 扩展为 `{ 1, 2, 4, 6 }`（界面下拉框：`1（单声道）` / `2（立体声）` / `4（4.0 四声道环绕）` / `6（5.1 环绕声）`）。
     - 4.0 模式严格按 `[FL, FR, RL, RR]` 交错；5.1 模式严格按 Windows `WAVE_FORMAT_EXTENSIBLE` / SDL3 标准顺序 **`[FL, FR, FC, LFE, RL, RR]`** 交错。
     - `OutputAudio()` 栈端混音缓冲扩充至 `MAX_HOST_CHANNELS (6) * maxFrameSamples`。
  2. **智能 5.1 声道上混器（`SurroundUpmix51`，默认开启，CLI `-upmix-51` / `-no-upmix-51`）**：
     - **中置 (`FC`)**：实时提取前声道共模信号（$0.7071 \times \text{frontMid}$）与后声道座舱共模（$0.25 \times \text{rearMid}$）凝聚领航员播报与主旋律，并对前左/前右适度扣除 $0.25 \times \text{frontMid}$ 防止中置能量堆积。
     - **低音炮 (`LFE`)**：通过双线性变换预畸变设计的 **120 Hz 二阶巴特沃斯低通滤波器**（`ButterworthBiquadLPF`，Direct Form II Transposed）提取四声道之和中的引擎轰鸣与碰撞超低音（实测 Daytona 2 录音中 `LFE` 轨 `< 150 Hz` 低频能量占比达 **93.7%**）。
  3. **内置无锁异步多声道 WAV / FLAC 录制器（`RecordWav` + `RecordWavFormat`）**：
     - **音频回调线程零阻塞**：`AudioOut::Write/Silence` 仅将交错后的样本通过 `memory_order_acquire/release` 推入 $2^{20}$ 槽位（4 MB）SPSC 无锁环形缓冲区并唤醒后台独立落盘线程，绝不在实时音频线程中调用 `fwrite` 或加互斥锁。
     - **开启录制时自动保持 4/6 声道不折叠**：即使在仅有 2 声道耳机/声卡的电脑上，开启录制后混音器也保持完整的 4.0 / 5.1 交错流（由 SDL3 `SDL_AudioStream` 自动为声卡播放下混），录制出的文件依然是真正的独立 4 声道或 6 声道（实测前后声道互相关系数仅 `-0.0067`）。
     - **四种无损录制格式（`RecordWavFormat`）**：
       - `wav_float32`：`WAV · 32-bit IEEE Float（无损浮点）`（68 字节 `WAVE_FORMAT_EXTENSIBLE` 头，4.0 掩码 `0x33`、5.1 掩码 `0x3F`）
       - `wav_pcm16`：`WAV · 16-bit PCM（标准整型）`
       - `flac_pcm24`：`FLAC · 24-bit PCM（无损压缩，高动态）`（内置零外部依赖 RFC 9639 多声道 FLAC 编码器，含 `STREAMINFO` 回填、`VORBIS_COMMENT` 声道掩码标签、`SUBFRAME_CONSTANT` / `SUBFRAME_FIXED` Rice 编码 / `SUBFRAME_VERBATIM` 自适应择优及帧头 `0xFFF8` CRC-8 / 帧尾 CRC-16 校验）
       - `flac_pcm16`：`FLAC · 16-bit PCM（无损压缩，小体积）`
     - **保存目录与命名**：自动保存在 `Data/Recordings/Sound/`（`build.bat` 重编译保留、`-package` 排除），文件名格式为 `<英文游戏名>_YYYY-MM-DD<星期三字母缩写>_HH-MM-SS_<N>ch.wav/.flac`（如 `Daytona USA 2 - Power Edition_2026-11-04Wed_09-24-39_6ch.flac`）。

**Release notes**

```
en: Added native 5.1 Surround (6-channel [FL, FR, FC, LFE, RL, RR]) output alongside 4.0 Quad, a smart 5.1
    hardware upmixer (common-mode Center extraction + 120 Hz 2nd-order Butterworth LFE low-pass filter), and
    a lock-free asynchronous multi-channel lossless audio recorder supporting WAVE_FORMAT_EXTENSIBLE WAV
    (32-bit IEEE Float / 16-bit PCM) and RFC 9639 FLAC (24-bit / 16-bit PCM) saved to Data/Recordings/Sound/.
zh: 新增原生 5.1 环绕声（6 声道）物理输出、智能 5.1 硬件上混器（前后共模中置提取 + 120Hz 二阶巴特沃斯低通 LFE 重低音），
    以及内置无锁异步多声道无损录音器，支持录制 WAV（32-bit Float / 16-bit PCM）与 FLAC（24-bit / 16-bit PCM）至 Data/Recordings/Sound/。
```

### 音频：SCSP 非 FM 通道 4-tap Catmull-Rom 立方插值 + DSB 全链路 32-bit Float 直通与音量斜坡 + 主总线软限幅

- **背景**：对双 Yamaha YMF292-F (SCSP)、DSB1/DSB2 (MPEG Layer II) 与混音主总线进行逐行硬件还原度审计，消除芯片源头的变调混叠、MPEG 解码 `int16_t` 量化截断、音量阶跃咔哒声与主总线硬削波。详见根目录 `runbook_sound_fix.md`。
- **修复与优化（`Src/Sound/SCSP.cpp`、`Src/Sound/MPEG/MpegAudio.cpp`、`Src/Model3/DSB.cpp`、`Src/OSD/SDL/Audio.cpp`）**：
  1. **SCSP 纯 PCM 通道升级为 4-tap Catmull-Rom Cubic 样条插值**：
     - 每帧预计算各芯片活动 Slot 的 32-bit FM 调制源掩码 `modMask0/1`；对不参与 FM 调制的纯 PCM 通道（`!isFmCarrier && !isFmModulator`）启用带循环边界保护（`FetchSlotSample`）的 4-tap Catmull-Rom 三次样条插值（`CubicInterpolate4Tap`），消除低采样率引擎/轮胎/人声采样大幅变调时的镜像混叠，同时保持 FM 合成音色逐位不变。
  2. **SCSP 跨芯片 FM 与寄存器缺陷修复**：
     - 修复 `SCSP_UpdateSlot()` 写死 `SCSPs->RINGBUF`（导致从芯片 `SCSPs[1]` 运行 FM 时错误跨芯片读取主芯片 `SCSPs[0]` 环形缓冲）的 bug；
     - 补齐 `SCSP_Init()` 中从芯片 `SCSPs[1].Slots[i].slot = i` 与 `SCSPRAM_LENGTH` 初始化；
     - 修复 `SCSP_UpdateSlotReg()` 动态修改 `0x08..0x0B`（`AR/D1R/D2R/RR/DL/KRS`）或 `0x10..0x11`（`OCT/FNS`）时未按 `octave + 2*KRS` 重算包络速率的问题，并实现 `LFORE` 置位时复位 `PLFO/ALFO` 相位；
     - 补齐 YMF292-F 第 17、18 路 Effect 通道（`Slots[16..17]` 的 `EFSDL/EFPAN` 对外部输入 `DSP.EXTS[0..1]` 的混音）。
  3. **DSB MPEG Layer II 32-bit Float 无损直通 + 逐样本线性音量斜坡**：
     - 在 `MpegAudio.cpp` 启用 `#define MINIMP3_FLOAT_OUTPUT`，新增 `MpegDec::DecodeAudioFloat()` 将多相综合滤波器的浮点输出直接直通 `CDSBResampler`，消除中间 `int16_t` 截断与硬削波；
     - 在 `CDSBResampler::UpSampleAndMix()` 内引入 `m_smoothedGainL/R` 逐样本线性增益斜坡，消除 DSB 硬件音量寄存器在帧边界（57.5 Hz）跳变产生的 Zipper Noise。
  4. **主总线 $C^1$ 连续无损膝点软限幅器（`MasterSoftClip`）**：
     - 在 `ToHostSample()` 中对 $|x| \le 0.85$（$-1.41\text{ dBFS}$）保持 100% 比特级线性直通，超过膝点时按一阶导数连续的有理数饱和曲线渐近收敛至 $\pm 1.0$，消除大音量或多通道叠加时的平顶方波破音。

**Release notes**

```
en: Upgraded non-FM SCSP voices from 2-tap linear to 4-tap Catmull-Rom cubic spline interpolation, fixed
    slave-SCSP FM ring-buffer cross-talk and dynamic envelope/LFO register updates, switched the DSB MPEG
    decoder to direct 32-bit float output with per-sample volume ramping, and added a C1-continuous knee
    soft-clipper on the master bus.
zh: SCSP 纯 PCM 通道升级为 4-tap Catmull-Rom 三次样条插值，修复从芯片 FM 跨芯片读错缓冲与动态包络/LFO 寄存器更新，
    DSB MPEG 解码升级为 32-bit Float 全链路无损直通并加入逐样本音量平滑斜坡，主总线新增 C1 连续膝点软限幅器。
```

### 修复：《Daytona USA 2》所有赛道发车后总是播放同一首背景音乐（`VRCycle` 常驻按死 `VR3` 触发隐藏选歌秘技）

- **问题触发**：用户实测《Daytona USA 2: Power Edition》（`dayto2pe`）时发现，无论选择初级环形赛道（`zst12`）、中级海盗船赛道（`zst15`）还是高级摩天楼赛道，车一发动进入比赛，背景音乐全都变成同一首歌（Track 8 `0x8DF436`）。
- **排查与证据**：
  1. 提取对比 `dayto2pe.zst12` 与 `dayto2pe.zst15` 中 DSB2 68K 的命令接收缓冲 `RAM[0x0000..0x0030]`，发现两份存档在发车时主板 PowerPC 发给 DSB2 的 MIDI 命令序列完全相同：`ae 10 0a`（选车 BGM）$\to$ `ae 10 00`（停止）$\to$ **`ae 10 08`**。说明不是 MPEG 解码器偏移算错，而是游戏主程序主动要求播放 `AE 10 08`。
  2. 反汇编 DSB2 68K 固件 `epr-20886.ic2`（`0x8008` 曲目表）确认：初级、中级、高级赛道的正常曲目分别是 `AE 10 02`、`AE 10 03`、`AE 10 04`，而 `AE 10 08` 是《Daytona USA 2》街机原版的**官方隐藏选曲秘技（在选完车进入赛道加载时一直按住面板上的 `VR3` 黄键不放，即可强制将该局 BGM 替换为 Track 8）**。
  3. 检查 `Src/Inputs/Inputs.cpp` 的 `CInputs::IsVRSelected(int index)`：当开启「单键循环切换视角」（`VRCycle = 1`，默认开启，且默认 `VRDefaultView = 2` 即 `VR3`）时，旧代码写为 `if (m_vrCycle) return index == m_vrCycleIndex;`，导致 **`VR3` 键从开机第 1 帧起全程每一帧都返回 `true`（相当于玩家把 `VR3` 键死死焊住不松手）**，100% 触发了隐藏选曲秘技！
- **修复（`Src/Inputs/Inputs.h`、`Src/Inputs/Inputs.cpp`）**：
  - 街机面板上的 `VR1`~`VR4` 是**瞬时微动按钮（Momentary Push Button）**，切视角只需短按一下即松开。
  - 在 `CInputs` 中新增脉冲计数器 `m_vrPulseFrames = 0`：开局及选车期间不按住任何 VR 键（游戏原生默认即为 VR3 追尾视角）；仅当玩家在比赛中实际按下 `VRNext` 切视角键时，才推进 `m_vrCycleIndex` 并触发 **6 个仿真帧（~104 ms）的瞬时按下脉冲**随后自动松开。

**Release notes**

```
en: Fixed a bug where Daytona USA 2 played the same background track on every course when single-button
    view cycling (VRCycle) was enabled: IsVRSelected() previously held the VR3 button down permanently on
    every frame, which triggered Daytona USA 2's arcade Easter egg (holding a VR button while a race loads
    overrides the course BGM with track 8). View cycling now emits a 6-frame momentary button pulse on press.
zh: 修复开启「单键循环切换视角（VRCycle）」时《Daytona USA 2》所有赛道发车后都播放同一首背景音乐的问题：此前
    IsVRSelected() 让默认视角键 VR3 在每一帧都保持常驻按下，触发了街机原版的“按住 VR 键进赛道强制切换隐藏 BGM”彩蛋；
    现改为仅在按下切视角键时发出 6 帧瞬时脉冲。
```

### 配置与诊断日志：移除旧版 `Supermodel.ini` 残留 + 增强 `Data/Model3Vulkan.log` 自动诊断 + 真实方向盘自动旁路摇杆平滑

- **背景**：`Supermodel-LegacyOG.exe` 已退役，配置目录无需再保留 `Supermodel.ini`；同时为了更快定位用户提交的日志，需要增强 `Data/Model3Vulkan.log` 的开场环境、外设校准与音频链路记录，并防止真实方向盘被手柄摇杆平滑/灵敏度干扰。
- **修改（`Src/OSD/SDL/Main.cpp`、`Src/OSD/SDL/Gui.cpp`、`Src/OSD/SDL/Localization.cpp`、`Src/OSD/SDL/SDLInputSystem.cpp`、`Src/Inputs/Inputs.cpp`、`build.bat`）**：
  1. **`Data/Config/` 仅保留唯一的 `Model3-Vulkan.ini`**：
     - 启动时若检测到旧版 `Supermodel.ini`：当 `Model3-Vulkan.ini` 不存在时直接将其重命名为 `Model3-Vulkan.ini`，已存在时则自动删除残留的 `Supermodel.ini`。
     - `build.bat`、「保存设置」弹窗与另存为对话框默认文件名统一为 `Data\Config\Model3-Vulkan.ini`。
     - 启动加载配置时，若发现 `GameXMLFile` 仍指向已不存在的旧目录（如移动文件夹前的路径），自动修正为当前 `Data/Config/Games.xml`。
  2. **`Data/Model3Vulkan.log` 自动诊断增强**：
     - 首行记录完整产品版本与构建时间戳：`Model 3-Vulkan v0.9.9.6 - Igloo-Garage (Rex) (save-state format 0.3a-WIP, built <Date> <Time>)`；
     - 自动记录安装基准路径、配置文件路径、宿主物理内存总量/可用量及 CPU 逻辑线程数；
     - 开局记录完整的音频 DSP 管线状态（SCSP 4-tap 立方插值、DSB 32-bit Float + 64-tap 重采样、主总线软限幅、声道数、5.1 上混器、录制状态）以及所有非默认的设备轴校准参数（`min / off / max / deadzone / saturation` 与 `wheel/gamepad` 分类）。
  3. **真实方向盘自动旁路摇杆平滑与灵敏度 + `Off` 档自动归位**：
     - 当「摇杆转向平滑 (`GamepadSteeringSmoothing`)」选为 `关闭（原始 1:1）` 时，「摇杆转向灵敏度 (%)」自动归位为 `100%` 并置灰；
     - 当 `CInputs::IsSteeringBoundToWheel()` 检测到 `InputSteering` 绑定在真实方向盘（非标准手柄的 `raw joystick` 或方向盘型号）上时，`FilterSteering()` 自动完全旁路，保持 1:1 零延迟直通。

**Release notes**

```
en: Retired the legacy Supermodel.ini file in favour of a single Data/Config/Model3-Vulkan.ini (migrating or
    removing stale copies automatically), enriched Data/Model3Vulkan.log with build/memory/path/DSP/axis-calibration
    diagnostics and automatic GameXMLFile path repair, and made Stick steering smoothing + sensitivity reset to
    100% when Off and bypass automatically when steering is bound to a physical racing wheel.
zh: 移除旧版 Supermodel.ini 残留（统一仅保留 Data/Config/Model3-Vulkan.ini 并自动修复陈旧 GameXMLFile 路径），
    增强 Data/Model3Vulkan.log 的系统内存/路径/音频链路/外设轴校准自动诊断记录；「摇杆转向平滑」选关闭时灵敏度
    自动归位 100%，且在绑定真实方向盘时自动旁路保持 1:1 直通。
```

### UI：「方向盘与踏板轴死区 / 行程校准」表格在任意 DPI / 窗口宽度下自适应

- **问题触发**：用户反馈「力反馈 / 灵敏度」页底部的校准表格中，`轴 / 实时读数` 表头被截成 `Axis / live val...`，
  `起止点` 三个数字输入框里的 `-32768` 只显示成 `-3276`，下拉框里的 `Centred bidirectional`、`Full-travel pedal` 也被截断；
  且即使最大化窗口也不会变宽（前 4 列原本写死为 `WidthFixed` 的固定像素，且未乘 DPI 缩放系数）。
- **修复（`Gui.cpp` 的 `DrawJoystickAxisCalibrationPanel`）**：
  1. 列宽改为用 `ImGui::CalcTextSize()` + `style.CellPadding` / `FramePadding` **按当前字体与 DPI 实测计算**，
     不再出现写死的 `135 / 230 / 125 / 135 / 68` 像素；
  2. **窗口够宽（含最大化）时**：5 列全部 `WidthStretch`，以实测最小宽度为权重按比例拉伸满整个面板；
     Column 4 内的 `Min / Off / Max` 三个输入框也在有余量时按比例加宽（上限 1.45×）；
  3. **窗口较窄、放不下时**：整张表切为 `ImGuiTableFlags_ScrollX | SizingFixedFit` + 显式 `outer_size`，
     表格底部自动出现**左右拖动滚动条**，任何列都不会被裁切或重叠；
  4. 数字输入框最小宽度改为能完整显示 `-32768`（`CalcTextSize("-327688")` + 内边距），
     列间距统一改为 `max(6px, ItemInnerSpacing.x)` / `max(8px, ItemSpacing.x)`，不再硬编码 4px。
- **影响**：仅设置界面布局；配置键与存档格式未变，无需重存存档。

**Release notes**

```
en: The wheel/pedal axis calibration table now measures its columns at the current font and DPI scale:
    it stretches to fill a maximized window, and shows a horizontal scrollbar instead of clipping
    headers, combo labels or the -32768 range inputs when the window is narrow.
zh: 「方向盘与踏板轴死区 / 行程校准」表格改为按当前字体与 DPI 实测列宽：窗口最大化时 5 列按比例铺满，
    窗口较窄时表格底部出现左右滚动条，不再截断表头、下拉选项或 -32768 输入框。
```

### 中文译名：`Spikeout` 改为「锐击」、`Daytona USA 2` 改用「梦游美国 2」

- **问题触发**：网友反馈现有中文译名不准。
- **修改（`Src/OSD/SDL/Localization.cpp` 的 `kGameTitles[]`）**：
  - `Spikeout`：`街头格斗` → **`锐击`**；
  - `Spikeout Final Edition`：`街头格斗 最终版` → **`锐击 最终版`**（保持原有的 `Final Edition` → `最终版` 译法）；
  - `Daytona USA 2 - Battle on the Edge`：`戴通纳 USA 2 - 边缘之战` → **`梦游美国 2：决战边缘`**；
  - `Daytona USA 2 - Power Edition`：`戴通纳 USA 2 - 动力版` → **`梦游美国 2：动力版`**；
  - 顺手把「按键」页的分组名 `Spikeout 按键` 改为 `锐击 按键`（其余分组名如「魔法卡车控制」「电脑战机控制」都是中文，保持统一）。
- **影响**：只影响中文界面下游戏列表与窗口标题里显示的标题；英文界面、配置键和存档格式均不变，不需要重编译存档或重选游戏。

**Release notes**

```
en: Renamed Spikeout to its common Chinese title 锐击 (and Spikeout Final Edition to 锐击 最终版), and
    switched Daytona USA 2 to the Chinese series name 梦游美国 2 (决战边缘 / 动力版).
zh: 中文译名修正：Spikeout / Spikeout Final Edition 改为「锐击」/「锐击 最终版」，
    Daytona USA 2 改用系列惯用名「梦游美国 2：决战边缘」/「梦游美国 2：动力版」。
```

## 2026-11-03

### 版本：升到 v0.9.9.6

- `MODEL3VULKAN_VERSION` 从 `0.9.8.7`（本地未发布）改为 `0.9.9.6`；`SUPERMODEL_VERSION` 未动（存档兼容）。
- 本轮汇总：方向盘力反馈（含踏板轴校准 / H 挡自动空挡）、追尾镜头防抖与摇杆转向灵敏度、
  MPEG 循环前奏、模拟网络板存档（Attract Mode 读档修复）、PowerPC x64 混合 JIT、
  macOS Retina 4K、HD 贴图导出 alpha、Linux 启动链路、Windows AMD push constants、
  Android 场景构建优化反哺、`Data/Model3Vulkan.log` 日志改名。
- 文档同步：`README.md`（新增「What's New in v0.9.9.6」）、`Docs/README-PACKAGE.txt`、
  `usermanual.md`、`usermanual_chs.md`、`INDEX.md`、`AGENTS.md` 的当前版本字样一并更新；
  历史条目保留原版本号。
- 窗口标题栏、控制台横幅、关于页、统计面板水印、游戏内面板都由 `MODEL3VULKAN_TITLE_FULL` 派生，
  无需逐处修改。

**Release notes**

```
en: Version 0.9.9.6 rolls up this cycle's work: steering wheel force feedback, a Need for Speed-style
    chase camera with stick sensitivity, restored MPEG music intros, attract-mode save states,
    the PowerPC x86-64 hybrid JIT, macOS Retina, HD texture dump alpha, Linux startup, and the
    Windows/AMD push-constant fix.
zh: 版本 0.9.9.6 汇总本轮全部改动：方向盘力反馈、极品飞车式追尾镜头与摇杆灵敏度、MPEG 音乐前奏恢复、
    Attract Mode 存档、PowerPC x86-64 混合 JIT、macOS Retina、HD 贴图导出 alpha、Linux 启动链路
    与 Windows/AMD push constants 修复。
```

### 修复：Daytona USA 2 等游戏带循环点的 MPEG 音乐前奏被跳过（约 9.9 秒前奏丢失）

- **问题触发**：网友对比 OpenGL 原版录音（`Daytona USA 2 PE_ music_ OpenGL.mp3`，13.02 秒）反馈，Daytona USA 2 Power Edition
  在标题 Logo 结束后进入自动演示比赛（Attract Mode）时，原版开头有一段约 9 秒钟紧凑的鼓点+吉他铺垫前奏，而我们的 Vulkan 版直接跳过了这 9 秒前奏、
  一上来就从第 10 秒的主歌高潮段开始播放。
- **排查与证据**：
  1. 用 `ffmpeg` 将网友的 13.02 秒录音与 `dayto2pe.zip` 内全部 18 首 DSB2（MPEG 音乐板）音轨做包络互相关比对，以 **0.991** 相关性精确命中
     **第 8 号音轨（`Track 8`，DSB2 68K 程序 ROM `epr-20886.ic2` 偏移 `0x8118`）**：
     - 起始地址 `start = 0x00A29978`，结束地址 `end = 0x00B117B8`（总长 59.36 秒），**循环点地址 `loopStart = 0x00A50438`**；
     - 前奏长度 $\text{loopStart} - \text{start} = \text{0x26AC0} = 158,400\text{ 字节}$，按 128 kbps（$16,000\text{ B/s}$）换算恰好为 **`9.90` 秒**！
     - 与之前 Fighting Vipers 2 的 SCSP DSP（`0x7C0–0x7FF`）修改**完全无关**，该曲目纯属 DSB2 MPEG 音轨。
  2. **真正根因（`Src/Sound/MPEG/MpegAudio.cpp:440`）**：
     - 当 DSB1/DSB2 播放任何“前奏 + 循环体”结构的 BGM 时，会先调 `MpegDec::SetMemory(mpegROM, start, end - start, false)`（`dec.buffer = rom + start`，`dec.pos = 0`），
       紧接着在同一帧调 `MpegDec::UpdateMemory(mpegROM, loopStart, end - loopStart, true)`：
       `dec.buffer` 移到 `loopStart`（`0x00A50438`），同时 **`dec.pos = dec.pos - diff = -158400`（负数！）**，使 `dec.buffer + dec.pos` 仍指向曲头 `start`（`0x00A29978`）。
       正常播放时，`dec.pos` 从 `-158400` 递增到 `0`（播完 9.9 秒前奏），再从 `0` 播到 `dec.size`，到底后 `dec.pos = 0` 刚好回到 `loopStart` 循环。
     - 2026-10-27 为修复 `scudplus.zst1` 读档死循环，在 `MpegAudio.cpp:440` 加上了 `if (dec.pos < 0 || EndOfBuffer()) { if (dec.loop ...) dec.pos = 0; }`，
       把所有负的 `dec.pos` 当成越界并立即归零跳到 `loopStart`，导致 Daytona USA 2 Attract Mode 的 9.9 秒前奏、赛道 BGM（Track 2/3/6 的 12.6 秒前奏）
       以及所有带循环前奏的 DSB 曲目全部被切掉前奏！
- **修复（`Src/Sound/MPEG/MpegAudio.cpp`、`Src/Model3/DSB.cpp`、`Src/Sound/SCSP.cpp`）**：
  - 在 `Decoder` 中记录 `minPos = -offset`（即底层 ROM / 自定义音轨缓冲区的真正起点相对于 `dec.buffer` 的负偏移）；
  - `DecodeAudio()` 与 `SetPosition()` 允许 `dec.pos` 在 `[dec.minPos, dec.size)` 内为负数，仅当 `dec.pos < dec.minPos` 时钳回 `0`，仅在 `EndOfBuffer()` 时触发循环回绕；
  - 顺带修复 `SCSP.cpp` 的 `CheckPendingIRQ()` / `SCSP_DoMasterSamples()` / `SCSP_LoadState()` 中混用全局指针 `SCSP` 与主芯片 `SCSPs[0]` 的问题，确保 MIDI 中断挂起位始终打在主 SCSP 上。

**Release notes**

```
en: Restored the pre-loop intro of looped MPEG music tracks (such as the 9.9-second intro of Daytona USA 2's
    attract-mode theme and the 12.6-second intros of its race themes), which had been skipped because
    MpegDec::DecodeAudio() clamped legitimate negative pre-loop offsets to zero after UpdateMemory().
zh: 修复 Daytona USA 2 等游戏带循环点的 MPEG 音乐前奏被跳过的问题（如 Attract Mode 演示曲开头的 9.9 秒前奏与正赛 BGM
    的 12.6 秒前奏）：允许 MpegDec 在 UpdateMemory() 设置循环点后保留合法的负偏移前奏区间，不再误跳至循环起点。
```

### 修复：方向盘力反馈完全无响应 + 新增踏板轴死区/全行程校准与 H 挡自动空挡

- **问题触发**：用户反馈使用速魔（SIMAGIC Alpha EVO）直驱方向盘基座、独立踏板与 H 挡排挡杆游玩 Daytona USA 2 等赛车游戏时，
  旧版 OpenGL 版力反馈完整，但 Vulkan 版完全没有任何震动、回正或阻尼反馈；同时反映缺少轴死区/踏板全行程调节界面与 H 挡松开回空挡快捷设置。
- **根因定位（共 5 处叠加缺陷）**：
  1. **非手柄外设被第一道门直接拦截（`SDLInputSystem.cpp:561`）**：
     原代码写为 `joyDetails.hasFFeedback = info.isGamepad && info.hasRumble`。方向盘基座在 SDL 中属于 `raw joystick`（`isGamepad = false`），
     导致 `hasFFeedback` 恒为 `0`，所有力反馈指令在 `CInputSystem::SendForceFeedbackCmd()` 第一道门即被丢弃
     （日志：`FFB: command -1 for joystick 0 dropped (hasFFeedback=0, axis 0 hasFF=0)`）。
  2. **Windows 下 SDL3 `SDL_INIT_HAPTIC` 与 `SDL_INIT_JOYSTICK` 初始化顺序陷阱（`PlatformSDL3.cpp`）**：
     SDL3 内部若先初始化 `SDL_INIT_JOYSTICK`（如 `DetectController()` 或 `SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC)`），
     `WINDOWS_JoystickInit()` 枚举设备时 `SDL_dinputhaptic.c` 的 `dinput` 尚未创建，`SDL_DINPUT_HapticMaybeAddDevice()` 直接返回 `false`；
     随后 `SDL_DINPUT_HapticInit()` 看到 `SDL_WasInit(SDL_INIT_JOYSTICK)` 已为真，又跳过 `DI8DEVCLASS_GAMECTRL`，导致 `SDL_hapticlist`
     对所有 DirectInput 方向盘恒为空。现改为只要请求 `Subsystem_Gamepad | Subsystem_Haptic`，必先单独执行 `SDL_InitSubSystem(SDL_INIT_HAPTIC)`。
  3. **路面震动（`FFVibrate`）在 SDL3 下漏掉了 `SDL_HAPTIC_SINE` 正弦波效果（`PlatformInputSDL3.cpp`、`SDLInputSystem.cpp`）**：
     原 SDL3 路径把 `FFVibrate` 只发给 `SetGamepadRumble()`（且要求 `entry->gamepad != nullptr`），导致直驱方向盘无震动；
     现新增 `HapticEffect::Vibration`（`SDL_HAPTIC_SINE`，周期 50 ms），优先通过方向盘电机输出高频震动，不支持时再回退到 `SDL_RumbleGamepad` / `SDL_RumbleJoystick`。
  4. **恒力（`SDL_HAPTIC_CONSTANT`）单轴方向与正负号错误（`PlatformInputSDL3.cpp`）**：
     DirectInput 与 Linux evdev 的 Cartesian 单轴模式要求 `direction.dir[0] = 0`、由 `constant.level` 的正负号（`[-32767, +32767]`）决定左右力矩方向；
     原代码将 `level` 取了绝对值 `std::fabs(strength)`，导致单轴方向盘恒力方向错乱。现已与 SDL2 / DirectInput 路径对齐，并补齐 `SDL_SetHapticAutocenter(haptic, 0)`。
  5. **负半轴还原精度差 1（`SDLInputSystem.cpp:1067`）**：
     `NormalizeAxis(-32768)` 除以 `32768.0f` 得到 `-1.0f`，而 `GetJoyAxisValue()` 统一乘回 `32767.0f` 得到 `-32767`，
     导致 `OffVal = -32768` 的踏板在松开静止时恒偏离 `OffVal`。现对负半轴恢复乘以 `32768.0f`。
- **新增界面功能（`Gui.cpp`、`Localization.cpp`）**：
  1. **「力反馈 / 灵敏度」页新增「方向盘与踏板轴死区 / 行程校准」面板**：
     - 实时显示所选控制器（`JOY1`–`JOY4`）各轴（`X, Y, Z, RX, RY, RZ, S1, S2`）的原始读数进度条（`-32768 .. +32767`）；
     - 提供「起点模式」一键切换：**「居中双向 (方向盘/摇杆 Off=0)」**、**「全行程踏板 (正向 -32768→32767)」**、**「全行程踏板 (反向 +32767→-32768)」** 与「自定义起止点」，
       彻底解决独立踏板默认只识别后半段行程的问题；
     - 提供逐轴 **死区 (`DeadZone` 0–99%)**、**饱和度 (`Saturation` 1–200%)** 滑块，`Min / Off / Max` 精确输入框，以及「取当前起点」「重置」按钮。
  2. **「按键」页与空挡绑定弹窗新增「H 挡松开自动回空挡（按 1–4 挡生成）」按钮**：
     - 自动提取当前 `InputGearShift1..4` 绑定的所有排挡杆按键并拼合 `!JOY1_BUTTONx+...` 写入 `InputGearShiftN`（保留键盘空挡键如 `KEY_T`），
       实现真实 H 挡摘挡即回空挡，无需手动编辑 `.ini`。

**Release notes**

```
en: Fixed steering wheel force feedback in the SDL3/Vulkan build (DirectInput haptic initialization order,
    raw-joystick capability gate, signed constant-force torque, SDL_HAPTIC_SINE vibration, and autocenter
    reset). Added a live wheel/pedal axis deadzone and full-travel pedal calibration table on the
    FFB / Sensitivity page, plus a one-click H-shifter release-to-neutral generator on the Controls page.
zh: 修复 Vulkan (SDL3) 版方向盘无力反馈的问题（修正 DirectInput 力反馈初始化顺序、非手柄方向盘能力门控、
    恒力正负方向、正弦波路面震动与硬件自动回中复位）。在「力反馈 / 灵敏度」页新增带实时读数的「方向盘与踏板轴
    死区 / 全行程校准」表格，并在「按键」页新增「H 挡松开自动回空挡（按 1–4 挡生成）」快捷按钮。
```

### 修复：Attract Mode 存档读档卡住并回退到系统检测画面（网络板状态未存档）

- **问题触发**：用户反馈 `dayto2pe` 的 **Attract Mode（自动演示）存档 `zst5 / zst9 / zst10`** 读档后约 3 秒卡住，
  随后跳回初始系统检测 / `NETWORK CHECKING` 画面；而比赛中存档（`zst7/zst8`）读档正常。
- **根因**（用 Python 反汇编 `dayto2pe` 的 PowerPC RAM 逐帧确认）：
  - 比赛存档中主内存 `ram[0x105007]` 非 0，游戏在 `0x093ed0` 处 `lbz + cmpwi + bne` 直接跳过网络板检查；
    而 Attract 存档 `ram[0x105007] = 0`，每帧都会走到 `0x093ee4` 读取 `CSimNetBoard::ReadIORegister(0x8a)`（`m_status1`）。
  - `m_status1` 正常应为 `0x2021`（单机自连 ready 态）。但 `CSimNetBoard::SaveState()/LoadState()` 一直是空实现，
    `CModel3::SaveState/LoadState` 也从没调用过 `NetBoard->SaveState/LoadState`，`netRAM`/`netBuffer` 也不入档；
    读档后网络板状态复位，`ReadIORegister()` 因 `m_running == false` 返回 `0`，`(m_status1>>13)&7 == 0`，
    游戏计数到 28 次后 `bgt` 重启回系统检测。
- **修复（`Src/Network/SimNetBoard.{h,cpp}`、`Src/Model3/Model3.cpp`）**：
  1. 实现 `CSimNetBoard::SaveState()`：新增可选块 `"SimNetBoard"`（magic `SNBT`），保存 `m_state / m_running / m_numMachines /
     m_counter / m_segmentSize / m_IRQ2ack / m_status0 / m_status1 / m_commbank` 以及 `RAM(0x10000)`、`Buffer(0x20000)`；
     `LoadState()` 按 `m_commbank` 重建 `CommRAM`/`externalCommRAM` 两个 bank 指针。
  2. `CModel3::SaveState/LoadState` 增加 `NetBoard->SaveState/LoadState` 调用（`CNetBoard` 空实现保持兼容）。
  3. **旧存档回退与 3 秒延迟读档段大小陷阱**：`LoadState()` 找不到 `SimNetBoard` 块时调用 `SynthesizeReadyState()`，合成单机自连 ready 态
     （GameType::one：`m_running=true, m_state=ready, m_numMachines=1, m_status0=0, m_status1=0x2021, m_IRQ2ack=0x01`）。
     **注意**：3 秒延迟读档（172 帧）触发时，机器正处于开机 `NETWORK CHECKING` 诊断阶段，诊断程序会把 `RAM16[0x402]=0x0200`、
     `RAM16[0x404]=256`（`0x100`）写进 `netRAM`；而 `daytona2` 正式进入游戏/Attract Mode 后的通信段大小是 **`848`（`0x350`，`RAM16[0x402]=0x3ef0`）**。
     若只判断 `RAM16[0x404]==0`，合成出的 `m_segmentSize` 会变成 `256`，导致 `RunFrame()` 把主柜 848 字节包拷到 `0x200` 而不是 `0x450`（从柜槽全 0），
     结果投币按 Start 后卡在选车前的「しばらくお待ちください」（请稍候）等待联机同步画面，无法真正进入 `CAR SELECT`，因此也一直不发送 `AE 10 0A`（选车 BGM）而继续循环播放 Attract Mode 音乐（`AE 10 09`）。
     现对 `RAM16[0x404]==0 || RAM16[0x404]==0x100 || IsGame("daytona2")` 一律重置为 `848` 并同时写入 `Buffer` 双 bank 头，同时在 `LoadState()` 中自动修复已按 256 字节保存的存档（如 `zst11`）。
  4. **顺带修复英文界面点开「力反馈 / 灵敏度」Tab 触发 `0xC0000409` 闪退**：`UiTextDim` / `UiTextWarn` / `UiTextHighlight` / `UiExplain` 原先全部把 `Tr(format)` 直接作为 `vsnprintf` 格式串传入；
     校准面板新增英文说明里的 `"0-100% throw"` 被静态 UCRT 当成 `%` + 空格标志 + `t`（`ptrdiff_t` 长度修饰符）+ `h`（非法二次长度修饰符），在 `state_case_size` 中触发 `_invalid_parameter` → `_invoke_watson` 强退。
     现将这四个辅助函数拆分为「单参数纯文本直传 `"%s"`」与「多参数 `snprintf` 模板」两个重载，彻底杜绝翻译文本含 `%` 引起的崩溃或乱码。

**Release notes**

```
en: Fixed loading Attract Mode save states (e.g. dayto2pe zst5/zst9/zst10) stalling and falling back to
    the system test: the simulated net board state was never saved, so the game's network check read a
    zero status register and rebooted. CSimNetBoard now saves/restores its handshake state plus the shared
    RAM/buffer, and old saves fall back to a synthesized single-machine ready state.
zh: 修复 Attract Mode 存档读档卡住并回退到系统检测画面（如 dayto2pe zst5/zst9/zst10）的问题：模拟网络板状态此前从未
    写入存档，游戏网络检查读到一个全 0 状态寄存器后自行重启。现在 CSimNetBoard 会存档/恢复握手状态与共享 RAM/缓冲，
    旧存档则回退为合成的单机自连 ready 态，无需重存即可恢复。
```

## 2026-11-02

### 构建与日志：`Supermodel-LegacyOG.exe` 轻量退役 + 诊断日志改名为 `Model3Vulkan.log`

- **构建与打包（`CMakeLists.txt`、`build-cmake.bat`、`build.bat`）**：
  - 默认只构建并发布纯 Vulkan 可执行文件 `Model3-Vulkan.exe`（`USE_SDL2=OFF`、`USE_SDL3=ON`、
    `SUPERMODEL_SDL3_PURE_VULKAN=ON`），跳过 MSBuild 编译与拷贝 `Supermodel-LegacyOG.exe` 的步骤；
    OpenGL 源码仍保留在仓库中备查，不随发行包分发。
- **诊断日志更名（`Main.cpp`、`VulkanRequirement.cpp`、`ConfigFile.h`、`Tools/*.ps1`、`Scripts/*.ps1`）**：
  - 默认日志文件由 `Data/Supermodel.log` 改为 `Data/Model3Vulkan.log`（macOS 为 `Model3Vulkan.log`）。
- **界面清理（`Gui.cpp`）**：
  - 从「核心 → 显示与运行」隐藏仅对窗口模式去标题栏生效的 `BorderlessWindow` 复选框，避免与「常用」页的
    「无边框全屏（Borderless fullscreen）」混淆；命令行 `-borderless` 保持兼容。
  - 修复「管理存档」卡片内缩略图水平偏左、与下方居中标题不对齐的问题：给卡片子窗口补上
    `ImGuiChildFlags_AlwaysUseWindowPadding`，并统一按实际内容区宽度 `ImGui::GetContentRegionAvail().x`
    计算缩略图、文件名、时间、体积与操作按钮的水平居中轴线。

**Release notes**

```
en: The distribution now ships only Model3-Vulkan.exe (the legacy OpenGL executable is retired from
    release packages), the diagnostic log is renamed to Data/Model3Vulkan.log, and the legacy
    windowed-mode Borderless Window checkbox is hidden from the GUI to avoid confusion with
    Borderless fullscreen.
zh: 发行包现仅包含 Model3-Vulkan.exe（旧版 OpenGL 可执行文件从发布包退役），诊断日志更名为
    Data/Model3Vulkan.log，并从设置界面隐藏旧版窗口去边框复选框（避免与「无边框全屏」混淆）。
```

### 新增：追尾镜头防抖 / 极品飞车式第三人称追随（`ChaseCameraStabilizer`）

- 背景：Daytona USA 2、Scud Race 等赛车游戏的第三人称追尾视角把相机朝向（Yaw）和车体朝向刚性绑定；
  车头稍微左右修正，相机不仅跟着偏转，还因为离车尾有一段力臂而在世界空间剧烈横摆，整幅远景和赛道在屏幕上左右猛甩。
  此前第一层「手柄转向平滑」（`GamepadSteeringSmoothing`）平滑了输入端，但进入弯道或车身微摆时游戏自己算出的相机仍然死锁车头。
- 实现（方案 A，渲染端 `CVulkan3D::PrepareMotion()` + `EffectiveModelMatrix()`，键 `ChaseCameraStabilizer`）：
  1. **捕获视口真实相机矩阵（`vp.cameraMatrix`）**：
     实测 Scud Race 等游戏的 `MatrixSlot[0]` 恒为单位阵（`vp.baseMatrix` 全程不变），真正的世界→相机矩阵挂在每个视口根节点
     （`vpnode[0x02] & 0x00FFFFFF`，即 `m_vpRootAddr`）的 `matrixOffset` 上。`CR3DSceneBuilder::DescendCullingNode()`
     现在在 `addr == m_vpRootAddr` 处精确捕获 `vp.cameraMatrix`（不受 `sibling2Ptr` 先行递归影响），同时保持 `vp.baseMatrix`
     不动，以免改变动态模型纵向距离 $Z$ 的插值基准。
  2. **围绕玩家车心支点 $(0, 0, -L)$ 的纯水平偏航（View-space Y-axis Yaw）弹簧阻尼**：
     - 自动从全屏视口中心前方（$|x| < 1.2, |y| < 1.8, z \in [-16.0, -3.5]$）相对于相机近乎静止的模型（玩家车身/底盘/车轮）
       测出跟车距离 $L$（`m_sharedPivotDistance`，Scud Race 实测为 `7.25`）并做 EMA 平滑；
     - 从主全屏赛道视口（模型数最多的 `vp.fullScreen`）计算帧间水平转向增量
       $\delta\psi = \text{atan2}((\Delta R_{\text{step}})_{0,2}, (\Delta R_{\text{step}})_{2,2})$，累加进场景共享滞后角
       `m_sceneYawLag` 并按预设时间常数 $\tau$ 与微摆死区衰减、按最大允许偏角钳制；
     - 在 `EffectiveModelMatrix()` 中左乘围绕车心的逆向水平旋转
       $C(\psi) = T(0, 0, -L) \cdot R_y(-\psi) \cdot T(0, 0, +L)$，使玩家车心 $(0, 0, -L)$ 在屏幕上纹丝不动、车头随转向自然偏摆，
       而远景建筑与赛道保持平稳；开启时 `CR3DSceneBuilder` 同步把视锥裁剪半径放宽 `1.25x` 以防大偏角边缘漏剪。
  3. **三档预设（0 关 / 1 轻度 / 2 标准 / 3 强力）与出厂默认**：
     - `0`：关闭（原始街机镜头）
     - `1`：轻度（$\tau = 0.25\text{ s}$，死区 $1.5^\circ$，上限 $7.5^\circ$）
     - `2`：标准（极品飞车式追随，$\tau = 0.50\text{ s}$，死区 $3.0^\circ$，上限 $13.0^\circ$）
     - `3`：强力（镜头最稳，$\tau = 0.85\text{ s}$，死区 $4.5^\circ$，上限 $18.0^\circ$，**新装默认**）
     - 同时新增 **`GamepadSteeringSensitivity`（摇杆转向灵敏度，25%–100%，新装默认 `66%`）**，并把
       `GamepadSteeringSmoothing` 新装默认设为 **`3`（强力，$\tau = 155\text{ ms}$，每帧最大步长 `3.2%`，
       中心曲线 `gamma = 2.50`）**，让手柄玩家开箱即获平稳顺滑的转向与镜头体验。
     - 设置窗口 Tab 栏把 **「力反馈 / 灵敏度」** 移到 **「按键」（Controls）** 紧后方、把 **「网络」** 挪到最末，
       并在页面顶部优先展示「手柄转向」三项；游戏内 `Tab` 面板同样支持实时调节并立即生效。
- 实测踩过的四个坑（均已解决）：
  1. **不能把世界相机矩阵直接塞回 `vp.baseMatrix` 做平移插值**：实例插值默认关闭，玩家车的 `localMat` 停在当前帧 $N$；
     若 `InterpolateRigid(prevBase, currBase, alpha)` 在 120 Hz 子帧上把相机位置退回帧 $N-1$ 与 $N$ 之间，车身会在每两个 120 Hz
     子帧之间前后跳动约 0.5 米（肉眼呈剧烈前后重影，单帧截图却完全正常）。分离 `cameraMatrix` 并只绕视图空间车心 $(0, 0, -L)$
     做纯水平旋转后，车心纵向距离 $Z$ 恒定不变。
  2. **同一视口内所有模型与子帧必须统一使用单一 `currYawLag`（防路面蓝线裂缝与子帧重影）**：
     Daytona 2 的路面由多块相邻瓦片（Model）拼接而成。之前按相机空间包围盒（`|vx| < 1.8, vz in [-10.5, -4.0]`）区分玩家车与世界建筑并赋予不同
     子帧插值起点，会导致车底/车侧处于包围盒边缘的一块路面瓦片与相邻路面瓦片使用了相差 `(1-α)·deltaYaw` 的两个旋转矩阵，共边处裂开 1 像素细缝、
     露出底层蓝色 2D 背景（与之前实例插值的共边裂缝同理）。现改为整个视口及该仿真步的所有子帧统一左乘唯一的 `currYawLag` 矩阵：所有路面瓦片共边
     逐位严格相等（彻底消除路面蓝线），且同一仿真帧快照不会在连续两个 120 Hz 子帧上画在两个不同水平位置（消除建筑虚影）。
  3. **迈凯伦松油门车尾回火（Backfire）引发的单帧闪动与误切镜**：
     - 回火出现时游戏在 Culling RAM 视口链表中间动态插入第 13 个视口（`viewports: 12 -> 13 -> 12`），导致后续视口的
       `vp.sourceAddress` 整体后移 `0x28`，旧代码在 `pit == prevViewport.end()` 时直接 `continue` 跳过了末尾赛道视口的稳定器赋值，
       并触发 `cuts` 重置 `DisplayPacer`；
     - 同时 `DescendCullingNode` 在压栈前先递归 `sibling2Ptr`，回火分支会抢先覆盖基于深度的相机矩阵捕获。
     - 现改为：① `addr == m_vpRootAddr` 精确捕获根节点相机矩阵；② 全屏视口共用唯一的 `m_sceneYawLag` 且在 `prevViewport.find`
       之前赋值；③ 全屏视口的 `motion.cut` 只认真实相机角度/距离跳变（`sceneCameraCut`），忽略视口链表槽位平移与 HUD/特效小视口。
  4. **Daytona USA 2 的全屏 3D HUD 视口（速度表 / 路线图 / 指示箭头）不能跟着世界相机转**：
     Scud Race 的 HUD 在独立小视口（`!vp.fullScreen`），而 Daytona 2 的 3D 码表、路线图和转弯指示箭头画在 `496×384` 的全屏高优先级
     视口（相机位于屏幕原点附近）。新增 `sharesPrimaryCamera()` 判据：仅当全屏视口与主赛道视口同属一个 `priority` 且共享同一个
     3D 世界相机矩阵（`|camT| >= 2.0` 且 `vp.cameraMatrix == primaryCamVp->cameraMatrix`）时才施加防抖旋转，全屏 3D HUD 保持钉死在屏幕上。

**Release notes**

```
en: Added a Need for Speed-style chase camera stabilizer (Off / Light / Standard / Strong) and a
    Stick steering sensitivity slider (25%-100%), placed at the top of FFB / Sensitivity (now right
    after Controls) and in the in-game Tab panel. New installations default to Strong smoothing, 66%
    stick sensitivity, and Strong chase camera stabilization so gamepad racers feel steady out of
    the box.
zh: 新增「追尾镜头防抖」（关闭 / 轻度 / 标准-极品飞车式追随 / 强力）与「摇杆转向灵敏度（25%–100%）」滑块，
    置于「力反馈 / 灵敏度」页顶部（已移至「按键」标签页紧后方）及游戏内 Tab 面板。全新安装默认开启强力摇杆平滑、
    66% 转向灵敏度与强力追尾防抖，手柄玩家开箱即获平稳不晃的驾驶体验。
```

### 性能：把 Android 性能第 3 轮的场景构建优化移植回 PC（`CR3DSceneBuilder`）

- 背景：YouTube 评论称「单核重、3.8 GHz Xeon 跑不满」，排查发现 JIT 本体（指令翻译层）在
  PC 已完整（与 Android `ppc_jit_arm64.inl` 逐 case 对齐、6.75 亿块锁步 0 失败），但 Android
  性能第 2/3 轮里配套的**场景构建 CPU 优化没有反哺**（对比 `E:\Dev\Model3onAndroid` 同文件）。
- 本次移植三处，与 Android 版逐行对齐：
  1. **`DrawModel()` 动态模型负缓存**：新增 `m_dynamicRomModels` 集合。ROM 模型第一次被
     `IsDynamicModel()` 判定为动态后记入集合，之后**每次 draw 不再跑整个多边形的写区检查**；
     `m_romMap` 改用 `find`（不再用 `operator[]` 插入空值）。几何缓存冲刷时（
     `kMaxRomVerts` 溢出）与 `m_romMap` 一起清空。顺带把 16 次循环的矩阵拷贝换成两次
     `std::memcpy`（64 字节，语义相同）。
  2. **`CopyVertexData()` 一次 `resize(oldSize + 4)` + 直写**：取代四次 `emplace_back`
     （每次带边界检查），`faceColour`/`faceNormal`/`textureNP` 提升到循环外，逐位语义不变。
  3. **LOD 计算 `std::hypot(x,y,z)` → `std::sqrt(x²+y²+z²)`**（hypot 的溢出保护缩放是白付的）。
- 安全性：`dynamic` 的判定只依赖模型地址是否落在可写区（ROM 内容不变，地址不变），负缓存
  不会过期；Android 侧已在真机跑了同一实现。
- 验证：CMake 双目标编译 0 错误；笔记本（i5-8365U + UHD 620）用 `daytona2.zst1` 冒烟测试通过
  （JIT enabled、读档正常、游戏内截图 1.7 MB）。**开发机同存档同帧逐像素 A/B 未做**（改用笔记本
  实机对比，见 `preopt` 文件夹），需要时可补跑 `Tools/Capture-Frame.ps1` 两版对照。

**Release notes**

```
en: Ported the Android build's scene-builder optimisations back to the PC: ROM models remember
    whether they are dynamic instead of re-walking every polygon per draw, vertex copies append
    four slots in one resize, and LOD blending uses a plain square root. Lower per-frame CPU on
    the main thread; rendering output is unchanged.
zh: 把安卓版的场景构建优化移植回 PC：动态 ROM 模型用负缓存避免每次绘制都遍历全部多边形、
    顶点拷贝改为一次扩容直写、LOD 计算改用普通开方。主线程每帧 CPU 更低，画面输出不变。
```

## 2026-11-01

### 修复：macOS Retina 屏上 4K 设置无效（按「默认字号」缩放时只输出 1080p）

- 触发：GitHub 评论（gamerg_）——4K Apple 显示器在系统「默认」缩放下选 3840×2160，
  程序仍按 1920×1080 输出；要把桌面切成「更多空间」才有效。他的另一句「不变成宽屏」
  是误解：默认就是街机 4:3 加黑边，那是 `WideScreen` / `WideBackground` 的开关，与本次无关。
- 他的判断**症状对、机制不准确**：macOS 的「默认字号」并不会把面板跑成 1080p；面板仍是 4K，
  只是给应用 1920×1080 的逻辑坐标 + 2 倍后备缓冲。申请了高密度 drawable 的应用拿到的就是
  3840×2160。我们没申请，才会由合成器去放大。
- 根因（两层，缺一不可）：
  1. **游戏窗口没有申请高分辨率 drawable**。`SDL_WINDOW_HIGH_PIXEL_DENSITY` 过去只设在设置
     窗口上（`Gui.cpp`），游戏窗口的 `desc.flags` 只有 `Vulkan|Hidden`（GL 同理）。没有这一位，
     Cocoa 的 CAMetalLayer 是 1x，窗口按点尺寸渲染再被拉伸到面板。
  2. **几何计算读的是窗口的点尺寸**。`CalcScreenGeometry()` 用 `GetWindowSize()`，而 macOS 的
     窗口和 `SDL_GetDesktopDisplayMode()` 都是 point；面板像素比在
     `SDL_DisplayMode.pixel_density` 里（4K「Looks like 1920×1080」= 2.0）。画框、整数倍缩放、
     CRT 预设、硬件预设的钳制因此全按 1080 行算，4K 档被钳成 1080p。
- 改法（坐标空间分开）：**渲染几何用设备像素，输入仍用窗口坐标**。
  - `OSD::Platform::DisplayMode` 增加 `pixelDensity`（SDL3 从 `SDL_DisplayMode::pixel_density`
    填充，SDL2/Windows 保持 1.0）；`DisplayInfo` 同步，日志打
    `Display 1: ... (2x: 3840x2160 pixels)`。
  - `Main.cpp` 新增 `GetRenderSize()`：以 `GetWindowPixelSize()` 为准；窗口仍隐藏时 Cocoa 可能
    还报 1x，所以再与「桌面模式 × pixelDensity」取大者。`CalcScreenGeometry()`、
    `GetIntegerScaleInfo()`、`LogWindowSize()` 都改用它。
  - `WindowSpacePictureRect()` 把画框换算回窗口坐标，只给 `Inputs->Poll()` 与
    `ConfigureInputs()` 用（输入系统按点缩放鼠标）。准星仍用像素坐标，`uiScale` 乘像素密度。
  - 游戏窗口在 `__APPLE__` 下加 `WindowFlag_HighDPI`（`CreateVulkanScreen` / `CreateGLScreen`）。
  - `CVulkanContext::CreateSwapchain()` 的 `currentExtent` 回退改用 `GetWindowPixelSize()`。
  - `Overlay.cpp` 的 `DisplayFramebufferScale` 不再写死 1.0——它原先的注释（「swapchain 等于
    窗口尺寸」）只在非 Retina 上成立；不改的话游戏内 Tab 面板会画在左下四分之一、只有一半大。
  - `Gui.cpp` 的硬件预设钳制与 `CrtPresetOutputHeight()` 改按设备像素；
    `StatsOsd` 的 `Panel` 行显示真实面板像素而不是 point 尺寸。
- 未改动：4:3 / `WideScreen` / `Stretch` 的比例逻辑、渲染器内部坐标、存档、音频、着色器。
  顺带说明：默认画面仍是 496×384 的信号比例（约 1.292:1）居中加黑边；真机光栅是 4:3
  （1.333），差的是原机的非方像素，这一点从上游继承，本次不动。
- 其他平台：
  - **Windows**：`GetWindowPixelSize == GetWindowSize`、`pixelDensity = 1.0`，高密度标志只在
    `__APPLE__` 下加，几何与输入数值完全不变。已用 `build-cmake.bat` 分别编译
    `Supermodel-LegacyOG.exe`（SDL2+GL）与 `Model3-Vulkan.exe`（SDL3+Vulkan），并启动设置界面
    确认窗口仍是 1920×1200 @1.5 系统缩放。**尚未做带游戏的逐像素 A/B。**
  - **Linux**：游戏窗口不申请高密度，drawable 等于窗口尺寸，数值不变；本机无 Linux 环境，未实测。
  - **macOS**：修复目标，**无真机，未验证**。复测看三处：`Display 1: ... (2x: ...)`、
    swapchain extent 是否 3840×2160、游戏内 Tab 面板与准星大小是否正常。
- 风险与已知边界：窗口隐藏创建时 Cocoa 可能仍报 1x drawable，`GetRenderSize()` 用桌面模式的
  密度兜底；首帧本来呈现到隐藏窗口，不可见，窗口显示后 swapchain 会跟到 2x。独占固定
  57/58 Hz 模式的匹配仍按 point 比较（macOS 上本来就没有这种模式，未变）。

**Release notes**

```
en: Fixed 4K output on Retina Macs. The game window never asked macOS for a high-density
    drawable, so a 4K display running at its default scaled desktop size rendered the game at
    1920x1080 and let the compositor scale it up - choosing 3840x2160 changed nothing unless the
    desktop was switched to a non-Retina resolution. The window now gets a native drawable and all
    picture geometry is computed in device pixels, so the selected resolution reaches the panel.
    Aspect ratio and widescreen behaviour are unchanged.
zh: 修复 macOS Retina 屏上 4K 设置无效的问题。游戏窗口过去没有向系统申请高密度 drawable，
    4K 屏在系统默认缩放下实际按 1920x1080 渲染再被拉伸，选 3840x2160 也不会生效。现在游戏窗口
    使用原生分辨率，画面几何按设备像素计算，所选分辨率真正送到面板。比例与宽屏行为不变。
```

### 新增：视角切换日志（issue #15 排查用）

- 触发：GitHub issue #15「Camera angle problem」，报告说 Daytona 2 / Power Edition 行进中相机“自己”上下变更，
  有时低到看不到前方。
- 判断依据：`VRCycle` 默认开启（`Main.cpp` 的默认配置），把 4 个 VR 视角键合并成一个；
  标准预设下 `InputVRNext = KEY_D,JOY1_BUTTON4`，即手柄 **Y**（手柄按钮索引 3）。
  按一次切一档，其中一档就是低机位。所以“随机变相机”首先应怀疑是误触 Y。
- 新增：切视角时打一行 `Input: view cycle -> VRn.`。玩家下次回报时，日志能直接区分
  “视角键被触发”与“游戏自己动了相机”（后者才是真 bug）。

### UI：「力反馈」页的标签与提示语易错过（issue #17）

- 触发：GitHub issue #17「How to adjust the steering sensitivity」——用 Xbox 手柄玩，转向过于灵敏。
  功能早就有（「力反馈 / 灵敏度」页 →「手柄转向」四档：关闭 / 轻度 / 中度 / 强力），但英文界面的
  tab 标签只写 **FFB**，手柄用户根本不会去点；而且页顶的橙色提示写着「这一页的设置才会生效」，
  把同页的灵敏度与手柄转向也包了进去。
- 改法：英文 tab 标签改为 `FFB / Sensitivity`；提示语收窄到力反馈分组，并说明「灵敏度」和
  「手柄转向」随时可用（中文与 `Localization.cpp` 英文条目同步）。
- 四个档位的作用（`Inputs.cpp` 的 `s_steeringSmoothing[]`）：一阶低通 35/50/75 ms，
  每帧最大变化 16/10/7%，中心曲线 gamma 1.25/1.5/1.7。只作用于转向轴，真方向盘建议关。
  设置窗口里改完要到下一局才生效（运行期每帧读的是 `s_runtime_config`）。

### 排查：Scud Race Deluxe HUD 灰板（issue #16）——ROM 与渲染后端都排除

- 触发：GitHub issue #16「Ingame displays appear gray and are unreadable」，报告者用的是
  `scuddx`（Export, Deluxe, Revision A）。对应 `issues_waitlist.md` 的 2026-10-24 条目。
- 新增诊断钩子 `SM3D_HUD_DEBUG=1`（`R3DSceneBuilder::DumpHudViewport` / `CacheModel`）：
  按视口打印 HUD 多边形的状态、面颜色、贴图矩形、`translatorMap`/`inverted`，默认关闭。
- 已排除：
  - **坏 dump**：`Games.xml` 的 CRC32 逐文件校验，`scuddx.zip` 41/41、父包 `scud.zip`
    41/41 全匹配，0 mismatch。
  - **渲染后端**：同一份 raw state、同一时刻，Vulkan 与 LegacyOG 的灰板**逐像素一致**
    （第 4 块 HUD 视口 mean (111,119,144)、flat 14.7，两边相同）。
  - **存档内容 / attract demo**：无存档全新启动跑到 attract demo 同样复现。
- 查过但没有区分度的两条路径（在 Twin 上同样成立，但 Twin 画面正常，所以都不是成因）：
  - CONDITION 视口 `159.75x160@(336,192)` 在 40 秒长局里 `scuddx`/`scud`/`scudau`
    **都是 0 个贴图多边形**（19/15/19 个模型全为无贴图底板）。
  - TileGen 2D 层：`scuddx`/`scudau` 的存档时刻两层都是 0 不透明像素；
    `scud` 的 layer 1 有 17,577 像素（y=192..239 的通栏色带），但两份存档不同时刻，只能当线索。
- 结论：`scud`（Twin/DX）**一直都能正常显示**，症状只在 **Deluxe** 上；用户补充「这个问题从 MAME
  时代起就没解决过」，即属于 Supermodel/MAME 一脉的模拟缺口，不是本分支新引入的。未修复，
  绕过方式是使用 Twin/DX 版本（`scud`/`scudau`/`scudplus`/`scudplusa`）。
- 顺带修好 `Scripts/render_tilegen_state.py`：右边缘的 tile 运行会写到 `x=503`，而 Python 每行
  只有 496 像素（C++ 的行缓冲是 512 宽，多出的是 padding）；现在只保留屏内像素，并支持直接读
  `.zst` 容器（`compression.zstd`，Python 3.14）。
- 证据、命令与截图：`sdl3_reports/2026-11-01-scuddx-hud/`；条目更新在 `issues_waitlist.md`。

### 修复：`Capture-Frame.ps1 -RawState` 拿到的不是 raw state

> **内部工具，与用户无关**：`Tools/` 不随发行包分发，玩家走的是 F5/F7 与「管理存档」，
> 全程不碰这个开关。这一条只方便我们自己拿 raw `.st` 做 LegacyOG 对照（以及以后读档问题的复现），
> **不要写进 release notes**。

- 症状：用 `.zst` 存档跑 `-RawState` 时，拷贝出来的是**容器 zip 本身**；用 LegacyOG 打开会报
  `does not appear to be a valid save state file`（`.zst` 容器是 zip + `core.zst`）。
- 根因：延迟读档路径（`Main.cpp` 里应用 pending state 的那段）在读完状态后**无条件删除**
  `Data/Saves/.vktmp.st`，没有检查 `SM3D_KEEP_STATE_TEMP`；而脚本是在会话运行中拷贝该文件，
  于是文件已被删、只能回退拷贝 `-State` 指向的容器。立即读档路径本来就遵守这个变量。
- 修复：该路径同样只在 `SM3D_KEEP_STATE_TEMP` 未设置时才删除临时状态。
- 验证：`scuddx.zst1` + `-RawState` 现在得到 30,862,469 字节、以 `Supermodel Save State`
  块头开头的 raw `.st`；LegacyOG 用同一文件 `Loaded state from ...` 成功。

### 修复：导出的贴图上有白点 / 虚线（1-bit alpha 抖动被当成画面）

- 症状：GitHub 评论（godmd5）贴出四张 Fighting Vipers 2 的角色脸部贴图，整张脸上覆盖白色
  点阵和竖虚线，问「导出功能是不是不完善」。肉眼看起来像 take 图坐标错位、行跨度错、
  或位深解码错。
- 根因：那些点是 **alpha = 0** 的像素，RGB 完全正常。Model 3 没有平滑 alpha（T1RGB5 用
  bit 15、4-bit 格式用 alpha nibble 当 1-bit 掩码），美术拿这一位做**抖动**表示半透明，
  所以 alpha 通道本身就是一张 2×2 / 4×4 的网点图。看图软件（尤其 GitHub 会把透明区合成
  到白底）就把它画成白点/白虚线。而运行时 `real3d_common.glsl` 的 `GetTextureValue()` 对
  「没有 `kTextureAlpha` 也没有 `kAlphaTest`」的材质会把 alpha 强成 1 —— 也就是说，对这类
  材质，导出文件里那半张 alpha 网点图**根本没参与画面**，却照样被写了进去。
- 证据（同一存档，`Tools/inspect-texturedump.py` + 逐像素比对）：
  - 症状图的 alpha 直方图只有 `0` / `255` 两档，纯白 RGB 为 0 %；行首像素
    `(239,189,132,255)`、`(239,189,132,0)` —— **RGB 相同，只有 alpha 翻转**。
  - 单独画 alpha 通道可见 2 px 周期竖条，部分图里能看见摩托车/人物剪影：确实是美术做的
    掩码，不是内存垃圾。
  - 新旧构建重叠的 **288 个哈希 RGB 逐像素完全相同**：取子纹理的区域、页选择、wrap 掩码、
    位深解码都没有错，错的只有 alpha 的语义。
- 修法（不改哈希、不改文件名、不改着色器）：
  - `CVulkan3D::BuildDrawList()` 把
    `alphaUsed = (material.flags & (kMatAlphaTest | kMatTextureAlpha)) != 0`
    传给 `CVulkanTextureCache::DumpTexture()`。
  - `DumpTexture()` 保留「同一哈希只写一次」，但记下哪些是削平 alpha 写的；如果之后有
    读 alpha 的材质也画这块区域，就**重新导出**一遍（文件名不变，日志打
    `... is also used with alpha; re-dumping it with the source alpha channel`）。
    材质遍历顺序是任意的，不能靠「第一次遇到就定稿」。
  - `CHDTexturePack::QueueDump()` / `DumperMain()`：`keepAlpha == false` 时解码后把 alpha
    写 255，首次发生时打
    `'<path>' is drawn by materials that ignore the texture's alpha; the dump is written opaque ...`。
  - 语义因此收敛成一句话：**导出的 PNG 就是渲染器真正会采样到的像素**。
- 验证（`fvipers2.zst2`，`Capture-Frame.ps1` + `DumpHDTextures=1`，600 帧）：
  - 新旧两个构建重叠 288 个哈希：RGB `np.array_equal` 全为真；其中 **89 张**从「30 %~60 %
    像素带 alpha」变为全不透明（就是网友看到的那些脸）。
  - T1RGB5 的 alpha 统计：旧构建 `a==0` 0.248 / `a==255` 0.752；新构建 0.029 / 0.971。
  - 画面无变化：dump 只写文件，不改 GPU 路径。
  - 前后对比图与完整数据：`sdl3_reports/2026-11-01-dump-alpha-dither/`。
- 顺手补的排查工具：`Tools/inspect-texturedump.py`（+ `.md`）。按格式统计 alpha 直方图、
  用「孤立透明像素比例」区分抖动点阵与镂空掩码，`--sheet` 输出画面（黑底合成）与 alpha 两份
  对照图，`--flatten` 给超分工具写一份不透明副本。
- 文档：设置页「导出当前游戏纹理」的 (?) 提示、`Localization.cpp` 英文条目、
  `usermanual.md` / `usermanual_chs.md` 的 HD 纹理一节、`devdocs/FIXES.md` 案例 52。

**Release notes**

```
en: Dumped textures no longer come out covered in white dots. The Model 3 stores a one-bit alpha
    mask and its artwork dithers that bit for translucency, so a dump used to carry a dot grid in
    the alpha channel even for materials that ignore alpha entirely - which looks like a broken
    image in any viewer. Dumps now match what the renderer samples: only textures some material
    actually reads alpha from keep the mask, everything else is written opaque.
zh: 修复导出的贴图布满白点/虚线的问题。原机是 1-bit alpha，美术用它做抖动来表示半透明，
    而不用 alpha 的材质在运行时会被强制成不透明；导出现在按材质决定，只有真正读 alpha 的
    贴图才保留掩码，其余写成全不透明。
```

### 下一版 Release Notes 草稿（2026-11-01 起，未发布）

> 只记在这里，发版时再黏进 `RELEASE_NOTES.md`：`release.yml` 会把那个文件原样当作发布正文。
> 发版时与「Linux 启动」那份草稿、以及 JIT 那一轮合并。对外口径与用户台账见
> `issues_from_users.md`。

```text
Three answers and two fixes from this week's reports

Exported textures no longer come out covered in white dots. The Model 3 stores a one-bit alpha
mask, and the artwork dithers that bit for translucency; a dump used to carry that dot grid in the
alpha channel even for materials that ignore alpha entirely, which every image viewer renders as a
broken picture. Dumps now match what the renderer actually samples: only textures some material
reads alpha from keep the mask, everything else is written opaque. If you have already exported a
set, export it again - file names and hashes are unchanged.

The force-feedback page is easier to find. The tab that read just "FFB" is now "FFB / Sensitivity",
and the warning on that page no longer implies that everything on it is inactive unless force
feedback is on. If a pad's steering feels too twitchy, the setting you want is Settings ->
FFB / Sensitivity -> Stick steering smoothing: Off / Light / Medium / Strong, tuned for pad players
without a wheel, and picked up when the next game session starts (issue #17).

Known issue: Scud Race Deluxe (scuddx) instrument panel stays grey. This is not a bad ROM dump and
not a Vulkan problem - the same save state gives a pixel-identical grey panel on the OpenGL build,
and the Deluxe panel content has never been emulated correctly in this line of emulators, going
back to the MAME days. Until it is solved, the Twin/DX sets (scud, scudau, scudplus, scudplusa)
render their HUD correctly; scudj contains the same program ROMs as scuddx, so it is a Deluxe set
too (issue #16).

Daytona USA 2's camera is not random. The cabinet has four camera views on the VR1-VR4 buttons, and
this build cycles them with a single key that is bound to Y on a pad by default: one press steps to
the next view, and one of them sits low enough to hide the road ahead. Rebind or clear it under
Settings -> Controls -> view switching if it gets in the way, or turn the single-button cycling off
(issue #15).
```

**中文短版**

```text
本周的三条答复与两项修复

· 导出的贴图不再布满白点。原机用 1-bit alpha 做抖动表示半透明，而不用 alpha 的材质在运行时
  会被强制成不透明；现在导出与运行时采样一致：只有真正读 alpha 的贴图保留掩码，其余写成全
  不透明。已导出的包请重新导出一次，文件名与哈希不变。
· 「力反馈」标签页改名为「力反馈 / 灵敏度」，页顶提示不再把整页否定；手柄转向发飘请用
  设置 →「力反馈 / 灵敏度」→「手柄转向」→「摇杆转向平滑」（关闭/轻度/中度/强力），
  为没有方向盘的手柄玩家调的，下一局生效（issue #17）。
· 已知问题：Scud Race Deluxe（scuddx）仪表盘灰板。不是坏 dump，也不是 Vulkan——同一存档在
  OpenGL 版上逐像素一致，这个面板从 MAME 时代起就没被正确模拟。在修好之前请用 Twin/DX：
  scud / scudau / scudplus / scudplusa；scudj 与 scuddx 是同一套程序 ROM，也是 Deluxe
  （issue #16）。
· Daytona USA 2 的相机不是随机的：机台有四个视角键，本版本用一个键循环，手柄默认绑在 Y 上。
  嫌它碍事可在设置 → 控制 → 视角切换里改键或关掉单键循环（issue #15）。
```

## 2026-10-31

### 发布：v0.9.8.6a（仅 Windows）——AMD 显卡在 Windows 上启动即退出

- **产物**（`v9.8.6a_upload/`）：`Model3-Vulkan.v0.9.8.6a.for.Windows.zip`
  19,205,744 B，SHA-256
  `d5cff6c6d2d074f5ff3a2ad5c142d7452d5e3d48baa66c80a4f2ff442cf6445f`；
  `SHA256SUMS.txt`、英文发布正文 `RELEASE_NOTES.md`。GitHub 上的源码树暂不更新。
- **触发**：Fightinggamer541（Steam Deck + Windows 11）的日志。启动图出现约 0.4 秒后进程退出，
  只留下 `Vulkan: no device offers a usable API version ... together with graphics, present and
  256-byte push constants.` 与 `GUI: the Vulkan settings window could not create its device.`。
- **根因**：AMD 的 Windows Vulkan 驱动把 `maxPushConstantsSize` 报成 **128**（Vulkan 规范保底值），
  而 NVIDIA / Intel / Mesa RADV 都报 256，后端一直只按 256 开发。3D 场景的 `R3DPush` 是 **208
  字节**（`projection` 64 + `lightDir`/`lightParams`/`fogColour` 各 16 都在里面，共 112 字节其实
  是**逐视口**而非逐绘制的），`CVulkanContext::PickDevice()` 又硬性要求 `>= 256`，于是在任何
  Windows + AMD 显卡（Steam Deck Win11、Radeon RX、Ryzen APU、掌机）上设备被直接刷掉。
  启动预检 `ProbeVulkan13()` 不查这个属性，所以启动图能出来、失败晚 400 ms，看起来像崩溃而不像
  硬件要求。
- **修复**：把这 112 字节搬进 `DrawBucket()` 本来每个 node 就要写一次的 `ViewportUBO`，
  `R3DPush` 降到 **96 字节**（`modelView` 64 + `fogParams` 16（w = 模型 alpha）+ `extra` 16），
  设备门槛从 256 改为规范保底的 128。改动文件：`VulkanCommon.h`（结构体 + `static_assert`）、
  `Vulkan3D.cpp`（`FillPush` 瘦身、UBO 填投影/光照/雾色，`EffectiveProjection()` 提前到写块之前）、
  `Vulkan3D.h`、`real3d.vert`（新增 `ViewportUBO` 声明、读 `ubo.projection`）、
  `real3d_common.glsl`（`pc.*` → `ubo.*`、push 块缩小）、`real3d.frag`（`gl_FragDepth` 用
  `ubo.projection[3][2]`）、`VulkanContext.cpp`（门槛与逐设备诊断）。
- **两个必记的坑**：① `projection` 在 `FillPush()` 里做了 4 次取负（Y 轴修正），搬家时这四次取负
  必须跟着走到 UBO 填充处；② `real3d.frag` 的 `gl_FragDepth` 也从 `pc.projection` 取值，只改
  顶点着色器会编译通过、画面也正常，但深度是错的——是 `grep pc.projection` 把它抓出来的。
- **验证**：同一台机器（RTX 5060 Ti / Windows）、同一存档 `daytona2.zst4`、同一仿真帧
  （`-Frame 300`、`RefreshRateMode=0`）、同一设置，未打补丁与打了补丁的同一棵 0.9.8.6 树各抓一帧：
  **逐像素相同**（mean 0.0 / max 0 / 错位像素 0%）。该场景覆盖投影、光照、雾与深度写入，正是搬动
  的那几个量。细节与复现命令见 `sdl3_reports/2026-10-31-amd-win-pushconst/`。
- **未验证**：手边没有 AMD 硬件；128 这个数字来自日志与 Vulkan 规范，仍需一台 AMD Windows 机器
  （或 `vulkaninfo --summary`）确认。
- **打包用的树**：`v9.8.6a_source` 由 `v9.8.6_source`（0.9.8.6m 快照）复制而来。源码快照**不含**
  `Libraries/`，必须先从主树复制 `SDL2-2.30.11`、`SDL2_net-2.4.0`、`SDL3`、`Vulkan-Headers`、
  `zlib-1.3.1`、`glslang`，否则 MSBuild 报 `Cannot open source file: ..\..\Libraries\SDL2-2.30.11\...`。
  改完着色器先跑 `Scripts\CompileVulkanShaders.cmd`，再 `build.bat -package`。
- **对比用的基线**：`v9.8.6a_baseline`（同一快照的未修改副本，只跑了 `build-cmake.bat`）。
  抓图需要 HostCore 解 `.zst`，`build-cmake/bin` 里没有该模块，从 dist 复制了
  `Model3VulkanHostCore.dll`。
- **发布正文口径**：按要求只写 Windows/AMD，不涉及其他平台；含一段作者自述（兼容性工作量）。
- **已同步回主树（本地，未发布）**：同一套修改已落到 `0.9.8.7` 工作树（六个渲染文件与 0.9.8.6 快照
  逐字节相同，直接复制；`VulkanContext.cpp` 手工改门槛与诊断文案；重跑了
  `CompileVulkanShaders.cmd`）。主树自己的同帧 A/B（同一存档、`-Frame 300`、1488×1152）
  同样 **逐像素相同**（mean 0.0 / max 0）。证据是 `sdl3_reports/2026-10-31-amd-win-pushconst/`
  的 `port-pre-frame300.png` 与 `port-post-frame300.png`；pre 那一帧是在 `build-cmake/bin` 的
  临时副本里跑完改动前的构建后拍的，该副本已删。`Src/Version.h` 仍是 `0.9.8.7`，等大版本一起发。
- **`dist/Model-3-Vulkan` 仍是 0.9.8.6m**：要就地试这个修复，把 `build-cmake/bin/Model3-Vulkan.exe`
  复制过去，或直接跑 `build.bat`。

### 发布：v0.9.8.6x（Linux + macOS）——合并取代 0.9.8.6l 与 0.9.8.6m

- **产物**（`v9.8.6x_upload/`，已上传并标记 Latest）：
  - `Model3-Vulkan.v0.9.8.6x.for.Linux.tar.gz` 19,277,981 B，SHA-256
    `d00c7a9b03d4429c9336152ea9034d586302b96bfca0b73d769b5c388c7e0ce6`
  - `Model3-Vulkan.v0.9.8.6x.for.macOS-AppleSilicon.tar.gz` 17,217,614 B，SHA-256
    `461fb2aaa3894ef26c84af101d9b96d2e9d3d2ec20079085e770a0afbbdd6b3b`
  - `Model3-Vulkan.v0.9.8.6x.for.macOS-Intel.tar.gz` 17,825,125 B，SHA-256
    `3706b40bc98fff151627be74f8cb61337413c1c27479fb837113ab971ed8bd66`
  - `SHA256SUMS.txt`；英文发布正文见 `v9.8.6x_upload/RELEASE_NOTES.md`。
- **本版唯一的新代码改动**：游戏内 Tab 面板在 Linux/macOS 上完全收不到输入。
  `Src/OSD/SDL/Overlay.cpp` 的 `FeedInput()` 把指针位置、鼠标键、修饰键和整张按键表全部
  包在 `#ifdef _WIN32` 里，没有 `#else`，所以非 Windows 每帧只送了滚轮：面板能开，但悬停、
  点击、方向键全无反应。现在从 `OSD::Platform::Input()` 的每帧快照喂 ImGui（指针、三个鼠标键、
  Ctrl/Shift/Alt、HID usage id 按键表），`OverlaySetVisible()` 的“上次鼠标位置”重置也补了
  非 Windows 版本。Windows 分支一行未动。
- **Linux 部分**：与 `v0.9.8.6l` 完全一致（glibc 2.38 基线、内置字体、`<exe>/Data/`、Wayland
  标题栏、进游戏黑屏、`.zst` 模块随包），只是版本号改为 `0.9.8.6x`。
- **macOS 部分**：与 `v0.9.8.6m` 一致（`VkResult -7`、preflight 接受 Vulkan 1.2、`Data/` 路径），
  加上面板修复。
- **macOS 构建环境（这台黑苹果）**：`~/m3v-src` 是 6m 源码去掉 macOS `Data/` 修复的副本，
  真正的 6m 树是 `~/m3v-legacy-src`（`Src/OSD/OSX/FileSystemPath.cpp` 有 25 行差异）。Apple
  clang 14 没有 `<expected>`/`<print>`，必须用 `~/bin/zig-cxx`（Python `ziglang` 模块，0.17）；
  `build-zig` 由
  `cmake -S . -B build-zig -DUSE_SDL2=OFF -DUSE_SDL3=ON -DSUPERMODEL_SDL3_PURE_VULKAN=ON
  -DCMAKE_C_COMPILER=$HOME/bin/zig-cc -DCMAKE_CXX_COMPILER=$HOME/bin/zig-cxx` 配置。
  产物默认 `minos 13.7.8`（≈ 主机系统版本，会拦住 13.0–13.6），用
  `vtool -set-build-version macos 13.0 13.3 -replace` 改回。
- **macOS 两个包都来自 CI 的 universal 构建**：本机 Zig 编不出 arm64（`-target aarch64-macos`
  在 SDL3 的 hidapi 上报找不到 `CoreFoundation` 头，补 `-isysroot`/`-iframework` 后又卡在 imgui 的
  `FLT_MAX`）。做法：在 master（6b4e7fc，即 6m）上用 GitHub Git Data API 直接建一个只改两处
  （`Overlay.cpp` + `Version.h`）的分支 `release/0.9.8.6x`，`gh workflow run build-model3.yml`
  跑现有的 macOS job（它本来就是 `-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"` + universal
  HostCore + MoltenVK 1.3.0），拿到 universal tar.gz 后在本机用 `llvm-lipo-18 -thin` 把
  `Model3-Vulkan` / `libMoltenVK.dylib` / `libModel3VulkanHostCore.dylib` 分别拆成 arm64 与
  x86_64 两个包。构建完删掉那个分支，master 不动。
- **已知限制**：`.zst` 仍需 macOS 14+（Swift 运行库）；macOS 部署目标 13.3；Linux 需 glibc 2.38。
- **验证**：Linux（Ubuntu 24.04.4 + Intel UHD 620 + Xwayland，0.9.8.6x 二进制）启动横幅
  `Model 3-Vulkan v0.9.8.6x`、认出 `daytona2`、截图正常；macOS（13.7.8 + MoltenVK，i5-8365U）
  设置窗口、ROM 识别、Scud Race 运行、Tab 面板鼠标与键盘操作均正常（由用户手动确认）。
- **事故记录**：清包时 `roms/` 在 6m 目录里是符号链接，`rm -f roms/*.zip` 顺着链接删掉了
  `~/Supermodel-VK-0.9.2.4/roms` 的全部 set。已从 Windows `E:\EMU\model-3-romset` 补回 57 个
  （2.9 GB），用户确认自行核对备份。**以后任何 `rm -rf <dir>/*` 前先 `ls -ld <dir>` 看是不是
  链接**。
- **发布流程**：先 `gh workflow disable 387892340`（`release.yml` 监听 `v*`，会用未修复源码
  触发 CI），`gh release create v0.9.8.6x --target master`，传三个资产，再 `enable`。

### 发布：v0.9.8.6l（仅 Linux）——从 0.9.8.6 快照单独出包

- **为什么不是 0.9.8.7**：PowerPC JIT 那一轮尚未收尾，不随此版发布。本次从 `v9.8.6_source`
  （0.9.8.6m 的源码快照，**不含 JIT**）出发，只把 Linux 的几个启动问题补上，单独出一个 Linux
  包；GitHub 上的源码暂不更新。
- **产物**：`v9.8.6l_upload/Model3-Vulkan.v0.9.8.6l.for.Linux.tar.gz`
  （**重新打包后** 19,278,090 B，SHA-256
  `33a774bc3e845b3ba6875c16c56230b047189cb4498d8398fba3cef803c57ffb`；首版为 17,365,969 B，
  SHA-256 `08b09d5ca9c743c4ebfa00c2ca30bdebf9d2ee8a3a04d7a8ec1fe8e3b68078b4`，已被覆盖）；
  英文发布正文见 `v9.8.6l_upload/RELEASE_NOTES.md`，校验文件 `SHA256SUMS.txt`。
- **包内**：`Data/Config` 只有 `Games.xml` / `Music.xml`（`package-cmake.sh` 现在会删掉 `*.ini`
  与 `*.bak-*`）；`Data/Assets/Fonts/wqy-microhei.ttc` 随包；`README.txt` 首行为
  `Model 3-Vulkan (v0.9.8.6l, Linux hotfix)`；**含 `libModel3VulkanHostCore.so`**（Native AOT，
  4,071,168 B，只要求 `GLIBC_2.34`），所以 `.zst` 存档开箱可用、不需要用户装 .NET。
  二进制与包内文档经字符串检查，无任何 JIT 痕迹。
- **`.zst` 补充验证（成品包解压到干净目录，Ubuntu 24.04 + Intel UHD 620 + Xwayland）**：
  `host core: Model3VulkanHostCore 1.0 (Native AOT)`；`SM3D_SAVE_STATE_FRAME=220` 写出
  `Data/Saves/daytona2.zst2`（942,037 B，来自 30,953,258 B，缩略图 5,809 B）；同帧截图画面
  正常（Daytona 2 联机检查画面 + bezel）。
- **本版包含的 Linux 修复**（与下面 0.9.8.7 条目同源，按 0.9.8.6 快照逐处移植）：
  1. 构建基线：本机 Ubuntu 24.04 + `gcc-14/g++-14`；产物实测只要求 `GLIBC_2.38`、
     `CXXABI_1.3.15`、`GLIBCXX_3.4.32`，无任何 ≥2.39 符号（0.9.8.6 原包要求 2.43）；
  2. 内置界面字体，`ProbeFontData()` 拒绝无拉丁字形的字体，移除 Droid fallback 候选；
  3. Linux 认 `<exe>/Data/`，`ResolvePath()` 锚定可执行文件；
  4. Wayland 下设置窗口的标题栏（构建依赖 `libdecor-0-dev`）；
  5. 进游戏黑屏：窗口按配置尺寸创建、`SetWindowSize` 用 `SDL_SyncWindow` 等 Wayland 确认、
     `CalcScreenGeometry` 对 <16 px 的窗口尺寸回退；另含 1280×800（Steam Deck）的 48 px 装饰余量。
- **验证（Ubuntu 24.04.4，Intel UHD 620，GNOME Wayland）**：452 个目标编译通过；界面 1280×800、
  包内字体、`interface ready 131 ms`；`daytona2` 进游戏为 `swapchain 992x768` 创建 → 全屏
  1920×1080 → 画面 `1395x1080`（修复前是 `1x1`）；从干净解包目录运行能列出 4 个 ROM。
  **未在 Steam Deck 实机复测。**
- **必须记住**：glibc 修复是「在 24.04 上编译」得到的结果，不是源码里的常量；CI workflow 的
  钉版本 + `readelf` 守卫只存在于主工作树，**不在这个包里**。下次把源码推到 GitHub 时要一并带上。

### 修复：Linux 发布包要求 glibc 2.43（Steam Deck 与 Ubuntu 24.04 全部无法启动）

- 症状：GitHub issue #14「Vulkan not working on Steam Deck」，以及同类的「一开就消失、没有任何
  日志」。发布包里的 `Model3-Vulkan` 在 Ubuntu 24.04 上直接报
  `/lib/x86_64-linux-gnu/libm.so.6: version 'GLIBC_2.43' not found`，进程连 `main()` 都进不去，
  所以既没有窗口也没有日志。
- 根因：`.github/workflows/build-model3.yml` 用的是 `runs-on: ubuntu-latest`，而 runner 已经漂到
  Ubuntu 26.04（glibc 2.43 + GCC 15 的 libstdc++）。`readelf -V` 显示 `GLIBC_2.43`
  （`asinf`/`sqrtf`/`atan2f`/`log10f`/`acosf`）与 `CXXABI_1.3.15`。任何低于 2.43 的系统——
  SteamOS、Ubuntu 24.04 LTS——都在动态加载阶段退出，与 Vulkan 无关。
- 修复：CI 固定 `ubuntu-24.04`，显式 `gcc-14` / `g++-14`（启动层的 `<print>` 需要 GCC 14 的
  libstdc++），并新增「Check the glibc/libstdc++ baseline」步骤：产物需要高于 `GLIBC_2.39` 或
  `CXXABI_1.3.15` 时构建直接失败，不再靠事后发现。在同版本 24.04 上实测重编的产物最高只要求
  `GLIBC_2.38` / `CXXABI_1.3.15` / `GLIBCXX_3.4.32`（原先 0.9.8.6 是 `GLIBC_2.43` +
  `CXXABI_1.3.15`），即 SteamOS 3.5 及以后与 Ubuntu 24.04+ 都能加载。
- 实测（Ubuntu 24.04.4，Intel UHD 620）：修复前 `ldd` 即报 2.43；修复后自源码编译、预检、
  设置窗口全部正常。证据与截图见 `sdl3_reports/2026-10-31-linux-startup/`。

### 修复：Linux 界面文字全部不显示（字体候选选中了 CJK-only 字体）

- 症状：设置窗口能打开，按钮、勾选框、下拉箭头、滚动条都在，**一个字符都没有**。
- 根因：`LoadUiFont()` 的 Linux 候选在 wqy 与 Noto CJK 都缺失时落到
  `/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf`。该字体只有 CJK 字形
  （实测 `U+0041 'A' -> 无`、`U+0030 '0' -> 无`），而英文界面的标签全是拉丁字符，于是整块
  消失。原版 Ubuntu 默认既不装 wqy 也不装 Noto CJK；`install-linux-deps.sh` 会装 wqy，
  所以作者的机器从未暴露这个组合。
- 修复：
  - **随包内置字体** `Data/Assets/Fonts/wqy-microhei.ttc`（Apache-2.0 / GPL-3+ with Font
    exception，附 `LICENSE-wqy-microhei.txt`）。Linux 上优先用它；Windows/macOS 仍优先系统
    字体（雅黑 / PingFang），内置只作最后回退，外观不变。
  - `ProbeFontData()` 新增**覆盖检查**：没有基本拉丁字形（`A`、`0`）的字体直接判为不可用并
    写日志，不再只看“能否解析”。这条规则对所有平台生效，属于同类问题的通用防线。
  - Linux 候选列表移除 Droid Sans Fallback。
- 验证：同一份二进制，换字体前后的截图对比（`01-blank-text-no-titlebar.png` /
  `02-bundled-font.png`）；日志出现 `GUI: interface font '.../Data/Assets/Fonts/wqy-microhei.ttc'`。

### 修复：Linux 不读发行包自己的 `Data/`

- 症状：从解压目录启动时日志出现 `Game and ROM set definitions could not be loaded!`，
  游戏列表为空；封面和内置字体同样定位不到。
- 根因：`Src/OSD/Unix/FileSystemPath.cpp` 仍按「当前目录有 `Config/` 就用当前目录，否则走 XDG」，
  而发行包布局是 `<exe>/Data/Config/`。Windows 与 macOS 早已按 `Data/` 判定，只有 Linux 没跟上。
- 修复：与 macOS 对齐——`<exe>/Data` 存在时，所有路径（含日志目录）都在其下；`ResolvePath()`
  改为相对可执行文件解析，日志不再依赖启动目录（此前 `Data/` 不存在时日志会静默不写）。
  没有 `Data/` 的源码树保留旧的当前目录 / XDG 行为。
- 注意：这意味着在 `build-cmake/bin/` 里运行时，配置与日志落在
  `build-cmake/bin/Data/`，而不是仓库根目录的 `Config/`。

### 修复：GNOME Wayland 下设置窗口没有标题栏

- 症状：窗口没有标题栏，拖不动、也没有关闭按钮，只能按 Esc。
- 根因：构建机没装 `libdecor-0-dev`，SDL3 配置为 `SDL_WAYLAND_LIBDECOR_SHARED: OFF`；GNOME 不
  提供服务端装饰，客户端又不画，于是完全没有装饰。
- 修复：CI 与 `Scripts/install-linux-deps.sh` 增加 `libdecor-0-dev`，并把这套脚本补全为可直接
  构建的依赖（`cmake`/`ninja`/`g++-14`、SDL3 需要的 X11/Wayland/音频头文件）。

### 打包：发行包不再包含作者本机的 ini 备份

- `Config/*.bak-*` 会被原样带进发行包的 `Data/Config/`（Linux 与 Windows 都是）。现在 Linux 的
  `package-cmake.sh` 在复制后删除 `*.ini` 与 `*.bak-*`，Windows 的 `robocopy` 增加
  `/XF "*.bak-*"`。发行包只带 `Games.xml` 与 `Music.xml`。

### 修复：1280×800 屏幕（Steam Deck）上默认窗口高过屏幕

- 症状：Steam Deck 的 Desktop Mode 下，设置窗口底部被屏幕边缘切掉，最后一行控件看不见。
- 根因：默认客户区是 1280×800，只按 `usableBounds` 夹一次。Windows / X11 的工作区已经扣掉
  任务栏或面板，而 **Wayland 不向客户端暴露面板占位**，`SDL_GetDisplayUsableBounds` 返回整块
  1280×800；libdecor 的标题栏又是窗口建立之后才有的（`SDL_GetWindowBordersSize` 返回 0）。
  于是 800 高的客户区 + 约 24 px 标题栏，超出了 800 px 的屏幕。
- 修复：初始尺寸与 `ApplyGuiWindowMinimumSize()` 都留出 48 逻辑像素的装饰余量，且只在
  “上下边框为 0”（即 Wayland 的客户端装饰）时生效。1280×800 屏幕得到 1280×752 客户区；
  1920×1080 上仍是 1280×800（实机日志未变）。
- 未验证：没有 Steam Deck 实机，结论来自尺寸计算与 1920×1080 上的回归日志。

### 版本

- `MODEL3VULKAN_VERSION` → `0.9.8.7`。`0.9.8.6m` 是 macOS 专用发布的后缀，不能留给 Windows /
  Linux 的下一版。

**Release notes**

```
en: Fixed the Linux release, which could not start at all on Steam Deck or Ubuntu 24.04: it was built
    on a CI image whose glibc is newer than anything current, so the loader rejected it before any of
    the emulator's code ran. The build is now pinned to Ubuntu 24.04 and fails if the binary ever
    needs a newer libc again. Two more Linux fixes: the interface ships its own font, because a stock
    Ubuntu has no CJK face and the fallback it does have has no Latin glyphs (the window opened with
    every label missing); and the package's own Data/ folder is now read on Linux like on the other
    platforms. The settings window also gets a title bar on Wayland again.
zh: 修复 Linux 发布包在 Steam Deck 与 Ubuntu 24.04 上完全无法启动的问题：CI 镜像的 glibc 比任何
    现行发行版都新，加载器在模拟器代码执行之前就退出了。构建基线现在固定为 Ubuntu 24.04，并要求
    产物不得依赖更新的 libc。另修两处 Linux 问题：界面字体改为随包分发（原版 Ubuntu 没有 CJK
    字体，其回退字体又没有拉丁字形，窗口打开后所有文字缺失）；发行包自己的 Data/ 目录现在也会被
    读取。Wayland 下设置窗口恢复标题栏。
```

### 下一版 Release Notes 草稿（英文，随 JIT 那一轮一起发）

> 只记在 CHANGELOG 里，**不要**提前写进 `RELEASE_NOTES.md`：`release.yml` 把那个文件原样当作
> release 正文发布（`body_path: RELEASE_NOTES.md`），草稿写进去会被直接公布。发版时把下面这段
> 与 JIT 的条目合并，再黏到 `RELEASE_NOTES.md`。

```text
Why the 0.9.8.6 Linux package refused to start

The 0.9.8.6 Linux build could not run on Steam Deck, Ubuntu 24.04, or most other current
distributions. It exited instantly - no window, no log, nothing to report.

The cause was the build environment, not the emulator and not your hardware. Linux packages are
compiled by GitHub Actions, and the runner we asked for is `ubuntu-latest`: a label GitHub keeps
pointed at the newest Ubuntu release. That image moved forward under us, so the package came out
linked against glibc 2.43 while SteamOS and the current Ubuntu LTS are older. The dynamic loader
rejects such a binary before the first line of our code runs, which is why the failure looked like
a crash instead of a version requirement. Vulkan, the GPU and the driver had nothing to do with it.

In short: GitHub's `ubuntu-latest` image moved to a newer Ubuntu under us, and the package silently
inherited that system's minimum requirements.

A moving build image is a compatibility trap for anything that ships binaries rather than source:
it raises the minimum system requirement silently, and the damage only shows up on other people's
machines. This is now fixed where it belongs - the runner is pinned to a specific Ubuntu release,
and the build fails if the finished binary ever requires a newer libc or C++ ABI than the oldest
system we support. That requirement is measured from the produced file with `readelf`, not assumed.

The same image also explains the second Linux report from this period: the interface opened with
every label missing. A stock Ubuntu installs no CJK font, and the fallback it does have has no
Latin glyphs. The interface now ships its own font and rejects any face without Basic Latin
coverage.

SteamOS 3.5+, Ubuntu 24.04+ and anything with glibc 2.38 or newer will load the new package.
```

## 2026-10-30

### 新功能：PowerPC 603e → x86-64 混合模式 JIT（`PowerPCMode`）

- 为 PC 版新增 PowerPC 的混合模式基础块 JIT：常用整数/浮点访存指令直接发射 x86-64 机器码，
  其余仍调用解释器自己的 C 处理函数，因此两条执行路径逐位等价。AArch64（Android）分支
  的 `ppc_jit_arm64.inl` 是参考实现，移植手册为仓库根目录 `JIT_runbook_for_X64.md`。
- 新增 `Src/CPU/PowerPC/ppc_jit_x64.inl`（配合已有的 `X64Emitter.h`）：4-way 组相联块缓存、
  4 KB 页自修改代码追踪、Windows x64 / System V 双 ABI、内置逐块锁步校验器
  （`SM3D_PPC_JIT_VERIFY=1`）与活动块反汇编诊断（`SM3D_PPC_JIT_DUMP=1`）。
- 设置界面「核心」页新增「PowerPC 执行模式」下拉框（`C 纯解释器（对照基准）` /
  `JIT 混合模式（推荐）`，默认 JIT），中英文均已接入；ini 键 `PowerPCMode`（0/1），
  命令行 `-ppc-mode=0|1`，环境变量 `SM3D_PPC_JIT=0|1` 优先级最高（测试脚本用）。
  下一局生效，因为它决定进程级代码缓存的归属。
- 前置设施（手册第 2 节）：`PPC_FAST_MEMORY` 由 `CModel3::PublishFastMemory()` 在内存池
  就绪、读档后与每次 CROM 换 bank 时发布；`ppc_execute()` 抽出
  `ppc_execute_one_instruction()`，并新增轻量级 wait-loop skip（寄存器状态重复即把剩余周期
  交给时基与递减器，`SM3D_IDLE_SKIP=0` 可关）。
- 验证：编码器自检 17/17；`daytona2` / `scud` / `vf3tb` / `fvipers2` / `srally2` 共
  **约 6.75 亿块锁步校验，0 失败**；同存档同帧截图与解释器**逐像素一致**（遮掉宿主侧统计
  浮层后 mean 0 / max 0）；CPU 瓶颈场景 496×384 下 224.5 Hz / 4.45 ms 对
  55.2 Hz / 18.12 ms（约 4.1 倍）；原生指令覆盖 79%–97%。
  `Model3-Vulkan.exe` 与 `Supermodel-LegacyOG.exe` 均 0 错误编译，后者二进制中没有 JIT
  （新功能只在 Vulkan 版）。详见 `sdl3_reports/2026-10-30-ppc-x64-jit/README.md`。
- 两个踩坑已写进报告与代码注释：块出口/总线调用后重载 `&s_pins` 不能用 `rax`
  （它正装着刚读到的值，会让每次经过总线的读取返回指针低位）；`oe` 溢出形式在
  `case 31` 内层 `switch` 里写 `if (oe) break;` 属于内层 switch，会继续执行后面的存储。

### 修复：macOS 启动即报 "Vulkan 1.3 required"（VkResult -7）

- 症状：macOS 包一启动就弹「Model3-Vulkan - Vulkan 1.3 required」，内容是
  `vkCreateInstance failed for a Vulkan 1.3 instance (VkResult -7)`。0.9.2.4a 的 macOS 包
  正常，0.9.8.x 的包在 macOS 上必现（用户的黑苹果与 GitHub issue #12 的网友都是如此）。
- `-7` 是 `VK_ERROR_EXTENSION_NOT_PRESENT`（`VK_ERROR_INCOMPATIBLE_DRIVER` 是 -9），
  所以既不是显卡太弱，也不是驱动太旧。
- 根因：0.9.8.x 新增的启动预检 `ProbeVulkan13()` 在 `__APPLE__` 下无条件请求
  `VK_KHR_portability_enumeration`。该扩展属于 **Vulkan loader**：loader 读取 ICD 的
  `is_portability_driver` 标志后自己合成它，MoltenVK 本体并不导出
  （`strings Frameworks/libMoltenVK.dylib` 中该字符串出现 0 次）。而 `volkInitialize()`
  直接 `dlopen` 包内的 `Frameworks/libMoltenVK.dylib`，中间没有 loader，于是
  `vkCreateInstance` 因为请求了不存在的扩展而失败。0.9.2.4a 没有预检、渲染器也不请求
  该扩展，所以能正常进界面。
- 修复：
  - `Src/OSD/SDL3/VulkanRequirement.cpp`：新增 `HasInstanceExtension()`，
    只有实现真的列出 `VK_KHR_portability_enumeration` 时才启用扩展并设置
    `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`（两者必须成对）。
  - `Src/Graphics/Vulkan3D/VulkanContext.cpp`：渲染器实例同样按需添加，
    复用 `LayerProvidesInstanceExtension(nullptr, ...)`，`info.flags` 同步设置。
    开发者用系统 Vulkan loader 跑时仍然会启用，设备枚举不受影响。
  - 失败文案按平台区分：macOS 不再说「更新显卡驱动 / 改用 Supermodel-LegacyOG.exe」
    （macOS 包里没有那个可执行文件），改为说明 Metal 设备不可用（虚拟机无 GPU 加速 /
    GPU 不支持 Metal）。
- 验证：`build-cmake.bat -DUSE_SDL3=ON` 两个单元重编译并链接通过；CI 的
  `build-model3.yml` 在 master 上手动触发（run 41084227903），Linux 与 macOS 两个 job
  均编译、打包成功——`__APPLE__` 分支的语法与头文件在真实的 macOS SDK 下已验证。
- **追加修复：预检比渲染器严**。`ClassifyDevice()` 一直允许两条路径：Vulkan 1.3 核心，
  或 Vulkan 1.2 + `VK_KHR_dynamic_rendering` + `VK_KHR_synchronization2`；
  `CreateDevice()` 在 1.2 路径上用 `VkPhysicalDeviceDynamicRenderingFeaturesKHR` /
  `VkPhysicalDeviceSynchronization2FeaturesKHR` 查询并启用这两个特性。但 `ProbeVulkan13()`
  只接受 `apiVersion >= 1.3`，于是 Intel 黑苹果（MoltenVK 报 1.2）在 0.9.2.4a 能跑、
  0.9.8.x 却被预检挡在渲染器之外。
  - 预检改为与渲染器同一套判定：1.3 查 `VkPhysicalDeviceVulkan13Features`；1.2 先查两个
    设备扩展（`HasDeviceExtension()`，入口点从临时实例取），再用 KHR 特性结构查询。
  - 对话框标题/正文改为「Vulkan 1.3，或带 dynamic rendering 与 synchronization2 扩展的
    Vulkan 1.2」，并列出具体缺失的扩展名。
  - 验证：Windows 编译通过；CI run 41087814429 的 Linux/macOS 均成功，产物二进制中确认
    包含新标题、`VK_KHR_dynamic_rendering` 与 macOS 专用提示文案。

### 修复：macOS 包找不到自己的 Data/（Games.xml、封面、存档全部失效）

- 症状：0.9.8.x 的 macOS 包即使过了预检，设置界面也没有游戏列表、没有封面、存档目录是空的。
- 根因：`Src/OSD/OSX/FileSystemPath.cpp` 的 `GetPath()` 返回 `<exe>/Config`，而发行包是
  `<exe>/Data/Config/`（`Scripts/package-cmake.sh macOS` 的布局，与 Windows 的 `build.bat`
  一致）。`Games.xml`、`Assets/Boxarts`、`NVRAM`、`Saves` 全在 `Data/` 下面。
- 修复：有 `Data/` 时用 `Data/`，没有时回退到旧位置（有人在仓库根目录直接运行 `make` 产物
  仍然可用）。默认 ROM 目录改为小写 `roms/`，与包内目录一致。

### 实机验证：macOS 13.7.8（Intel 黑苹果，i5-8365U）

- 三个修复合并后的产物（CI run 41095908870，commit `b5dd7aa`）在本机逐项验证：
  - 设置窗口 1280×800 正常创建，`GUI: interface ready (169 ms)`，无 `-7`；
  - `Vulkan 1.3 preflight passed: ... (Vulkan 1.3)`，`Created VkDevice`，3 张 swapchain 图；
  - `GUI: 56 ROM(s) listed, 0 with no identifiable set.`（列表与按 CRC 识别 set 均正常）；
  - Daytona USA 2 进游戏：3D 目标建立、bezel 上传、贴图页 95.8% 上传、音频按设备声道折叠；
  - `.st` 存档：`SM3D_SAVE_STATE_FRAME=700` 写出 `daytona2.st1`（30.9 MB），
    `-load-state=` 在 172 个仿真帧后读回，PC 从 `00005424` 变为 `0004C760`；
  - 全程日志只有一条 HostCore 加载失败（见下），无其他 error/warning。
- 性能（同机、同 992×768）：24 EU 核显上 3D 场景是主要成本，`InternalResolution` 从
  992×768 降到 496×384 后帧率约翻倍；细节与采样见 `MacOS/MacOS_performance.md`。
  注意：该文档中“OpenGL 版明显快于 Vulkan 版”的早期结论来自不同时间点的采样，已作废。

### 发现：`.zst` 存档在 macOS 13 不可用（需要 macOS 14+）

- `libModel3VulkanHostCore.dylib` 是 .NET 11 Native AOT 产物，硬链接了
  `/usr/lib/swift/libswiftCore.dylib` 与 `libswiftFoundation.dylib`（.NET 的 Apple 加密后端
  是 Swift 实现的，还链了 `CryptoKit.framework`）。
- dyld 报缺 `_swift_getTypeByMangledNameInContext2`：`swiftlang/swift` 的 `release/5.8` 分支
  里 0 处、`release/5.9` 里 1 处，即 Swift 5.9 运行时才有，而 macOS 14 才带 Swift 5.9。
  本机 `dlsym` 实测也返回 NULL。
- 结论：macOS 14+ 用 `.zst`，13.x 自动回退 `.st`（实测保存/读回都正常）。发布说明按此措辞。

### 发布：v0.9.8.6m 只发 macOS

- `MODEL3VULKAN_VERSION` → `0.9.8.6m`（`SUPERMODEL_VERSION` 未动）。后缀 `m` 表示这是 0.9.8.6 的
  macOS 构建；Windows / Linux 仍然是 0.9.8.6，不需要重发。
- 本次 release 只有一个资产 `Model3-Vulkan.v0.9.8.6m.for.macOS.tar.gz` + `SHA256SUMS.txt`，
  不含 Windows / Linux 包：macOS 平台层之外没有任何改动。
- 发布说明开头明确标注 **for macOS only**。

### 决定：macOS 不再提供传统 OpenGL 版本

- 个人精力和设备有限，无法同时维护三大平台 × 两套渲染后端（6 个构建）并逐个实测，
  macOS 包只出 Vulkan 版；`Supermodel-LegacyOG` 仍只在 Windows 提供。
- 因此 `MacOS/` 下关于 macOS OpenGL 包的构建探索（CMake 里 SDL2 目标的 OBJC 语言修复、
  `SDLIncludes.h` 的头文件兼容）**不进本次发布**，暂留在工作树里备查。

## 2026-10-29

### GPLv3 第 7 条署名与商标保护

- 新增 `LICENSE-ADDITIONAL-TERMS.md`：依据 GPLv3 Section 7(b)(c)(d)(e)，对 Igloo-Garage
  原创的 Vulkan 渲染器、着色器、设置界面、游戏内面板、统计面板、HD 纹理管线与 HostCore
  声明附加条款——必须保留作者署名与仓库链接、修改版必须标注、不得用原作者名称/Logo 宣传、
  GPL 不授予商标权。删除或隐藏署名显示代码即违反 §7(b)，并按 §8 终止授权。
- 新增 `TRADEMARK.md`：明确 `Model 3-Vulkan`、`Igloo-Garage`、`output.ico`、
  `Igloo-Garage Presents.webp`、`Model3VulkanBezel.png` 为品牌标识，禁止第三方整合包、收费平台
  或硬件盒子直接套壳冒用；允许“based on Model 3-Vulkan”式的真实陈述。上游 `Supermodel`
  名称与代码版权仍归 The Supermodel Team，本项目不主张其商标权。
- `Src/Version.h` 新增 `MODEL3VULKAN_AUTHOR`、`MODEL3VULKAN_COMPANY`、`MODEL3VULKAN_REPO_URL`、
  `MODEL3VULKAN_COPYRIGHT`、`MODEL3VULKAN_TRADEMARK_NOTICE`、`MODEL3VULKAN_LEGAL_SHORT`，让启动画面、
  关于页、游戏内面板、统计面板与控制台横幅共用同一组字符串，避免四处署名不一致。
- 启动画面改为双行：第一行 `Model 3-Vulkan - Developed by Rex (Igloo-Garage)`，哈希不匹配时
  标红为 `UNOFFICIAL MODIFIED BUILD (Original by Igloo-Garage)`；第二行固定显示仓库地址与
  `GPLv3 (No Warranty)`。
- 统计面板底部固定两行署名（无独立开关）；游戏内 Tab 面板底部增加作者与可点击的仓库链接；
  设置窗口底部常驻栏（Ko-fi 徽章左侧）新增「关于与许可」（`About & licence`）矢量徽章按钮
  （`DrawAboutBadge()`），点击弹出居中模态窗口 `AboutLegalModal` 展示版权、无担保、GPLv3、
  上游 Supermodel 致谢与许可证文件位置，并与「启动游戏」按钮做帧级渲染校验耦合
  （`s_footerLegalFrame`）；原「核心」页底部的折叠栏已移除，保持「核心」页清爽；
  控制台横幅增加仓库地址与 GPLv3 提示。
- `Src/OSD/SDL/Gui.cpp` 补齐此前缺失的 GPLv3 文件头；`Src/Graphics/Vulkan3D/**`、
  `Src/OSD/SDL3/**`、`Overlay.*`、`StatsOsd.*`、`Localization.*`、`FontProbe.*`、
  `SplashScreen.*` 的版权行更新为
  `Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)`，并加入
  `Additional Terms under GNU GPLv3 Section 7 apply to this file`。上游 `Supermodel Team`
  的版权与致谢保留。
- 品牌实体绑定：`TRADEMARK.md`、`NOTICE.md`、`README.md` 与 `LICENSE-ADDITIONAL-TERMS.md`
  明确 `Model 3-Vulkan™` / `Igloo-Garage™` 及图标、启动图、机柜边框是
  **REXABYTE SOFTWARE INC.**（加拿大不列颠哥伦比亚省）的商标与品牌资产；™ 表示未注册/
  普通法商标，并声明 GPLv3 §7(e) 不授予商标权。关于页与控制台横幅增加同一段商标声明。
  上游 `Supermodel` 名称不在本项目的商标主张范围内。
- 项目自有标识去 `SUPERMODEL_` 化：`MODEL3VULKAN_TITLE`、`MODEL3VULKAN_VERSION`、
  `MODEL3VULKAN_UPSTREAM_RELEASE`、`MODEL3VULKAN_*` 署名宏、`MODEL3VULKAN_EXE_NAME`、
  `MODEL3VULKAN_SPLASH_SHA256`、`IDI_MODEL3VULKAN` 统一替换；`SupermodelManifest.xml` →
  `Model3VulkanManifest.xml`，两份 `SupermodelResources.rc` → `Model3VulkanResources.rc`，
  `supermodel_build_bot.py` → `model3vulkan_build_bot.py`。上游 `Src/Supermodel.h`、
  `SupermodelDebugger.*`、`SUPERMODEL_VERSION`（写进存档）与 `Supermodel-LegacyOG.exe` /
  MSVC legacy 工程保持不变。
- 源码头里的项目名统一改为 `Model 3-Vulkan`（65 个 `.cpp/.h/.mm/.rc`），保留上游
  `Supermodel` 的版权与致谢；`Supermodel-VK` 不再作为本项目名称出现在源码头。
- 删除旧版 `Supermodel-VK-SDL3.ini` 迁移：Vulkan 首次运行只从 `Supermodel.ini` 播种；
  `Tools/Capture-Frame.ps1`、`Measure-FrameRate.ps1`、`Restore-TestIni.ps1` 与 `build.bat`
  的查找/保留列表同步移除该项。`Supermodel.ini` 仍保留，因为 LegacyOG 还在用。
- 发行目录改名为 `dist/Model-3-Vulkan`，`build.bat`、`Tools/*.ps1`、`AGENTS.md`、
  `INDEX.md`、玩家手册同步更新；`build.bat` 在旧 `dist/Supermodel-VK` 仍存在且新目录不存在时
  会报错停止，避免丢掉存档/NVRAM/设置。
- 清理 `Tools/Diagnostics/Temp*.ps1` 与 `TempBuildDiag.bat` 等硬编码旧路径的临时脚本；
  `Scripts/syntaxcheck-linux.sh` 改为默认使用脚本所在仓库，并支持 Windows/WSL 路径参数；
  `Tools/Diagnostics/test_webp.ps1` 改为相对路径。
- 开发工具同步新名字：`Tools/Remote/Deploy-ToLaptop.ps1` 的默认目标改为
  `C:\EMU\Model-3-Vulkan`，运行检测同时匹配 `Model3-Vulkan.exe` 与
  `Supermodel-LegacyOG.exe`；`Get-LaptopLog.ps1` / `Test-CleanExit.ps1` 的远程路径同步；
  `Tools/Validate-Vulkan.ps1` 的层缓存路径改为
  `%LOCALAPPDATA%\Model-3-Vulkan\VulkanLayers\Bin`，临时目录改为 `model3vulkan-laptop`。
  **需要同步把笔记本上的实际目录/共享改名，否则这些脚本会找不到目标。**
- 所有 `.vert/.frag/.comp/.glsl` 源文件加版权头；`Scripts/CompileVulkanShaders.cmd` 与 `.sh`
  在生成 `.spv.h` 时自动写入同一段版权头，并已重新生成全部 shader 头。
- `build.bat` 与 `Scripts/package-cmake.sh` 把 `LICENSE-ADDITIONAL-TERMS.md`、`TRADEMARK.md`、
  `NOTICE.md`、`Docs\LICENSE.txt` 统一放进发行包的 `Data/` 子目录（顶层 `README.txt` 与关于页
  指向 `Data/`），保持发行包根目录清爽。
- `NOTICE.md` 修正 `LICENSE.txt` 的相对链接（实际在 `Docs/`），并引用新增的两份文件；
  `README.md` 的 License 章节同步说明附加条款与商标政策。
- 验证：`build-cmake.bat -DUSE_SDL3=ON` 两个目标均链接成功（`Model3-Vulkan.exe` /
  `Supermodel-LegacyOG.exe`）；用 `grep -a` 确认两个 exe 都含仓库链接、`UNOFFICIAL MODIFIED
  BUILD` 与统计面板署名；PowerShell 工具脚本通过语法解析检查；`dist/Supermodel-VK` 已完整迁移
  重命名为 `dist/Model-3-Vulkan`，新 exe 与法务文件（位于 `Data/`）已部署到位。未做人工目视
  （启动画面双行字幕的排版需在实际显示器上确认）。

**Release notes**

```
en: Added GPLv3 Section 7 attribution terms and a trademark policy. The startup splash, the About
    panel, the in-game panel and the statistics overlay now carry the author credit and repository
    link; modified builds are marked as unofficial and cannot use the project name or artwork.
zh: 新增 GPLv3 第 7 条署名附加条款与商标政策。启动画面、关于页、游戏内面板与统计面板固定显示
    作者署名和仓库链接；修改版必须标注为非官方，且不得冒用项目名称与美术资源。
```

### 外设与界面体验：UI 热键可配置、鼠标轴绑定修复、键位预设回弹、启动自动运行

- **版本**：`MODEL3VULKAN_VERSION` 从 `0.9.8.3b` 改为 `0.9.8.6`（标题栏、控制台横幅、
  关于页、统计面板水印、游戏内面板同步，都由 `MODEL3VULKAN_TITLE_FULL` 派生）；
  `SUPERMODEL_VERSION` 未动，旧存档仍可读。手册、`README.md`、`RELEASE_NOTES.md`、
  `INDEX.md`、`Docs/README-PACKAGE.txt` 的当前版本字样一并更新，历史条目保留原版本号。
- **核对：128 键外设扩展（MOZA H-Shifter，113–119 键）已在位**。
  `PlatformInput.h` 的 `kMaxGamepadButtons = 128`、`GamepadState::buttons` 为
  `std::bitset<128>`；SDL2/SDL3 平台层都是 `state->buttons.set(i, true)` 且循环上界是
  `kMaxGamepadButtons`；`NUM_JOY_BUTTONS = 128`，`LookupJoyPart()` 动态解析
  `BUTTON1`–`BUTTON128`（静态表仍只列到 32，动态分支在表查找之前），`LookupName()`
  用 `snprintf` 生成 `BUTTON%d` 静态串。本轮未改动这部分。
- **`InputUI*` 可以从 INI 配置**（对接 RetroBat / 街机框体）：`CInput::LoadFromConfig()`
  的 `if (IsConfigurable())` 改为 `if (!IsVirtual())`。UI 动作的 id 是 `UIExit`、`UISaveState`
  等，拼出的键名正是 `InputUIExit` / `InputUISaveState` / `InputUIToggleOverlay`。
  `IsConfigurable()` 本身不变，因此「按键」页表格仍不列 UI 项、`StoreToConfig()` 仍不写
  回 UI 键、`ApplyControlPreset()` 不会覆盖用户在 INI 里写的值；键缺失时沿用
  `Inputs.cpp` 的硬编码默认。两行补丁由 RetroBat 的 Tartifless 提出
  （RetroBat-Official/emulatorlauncher#1456），按原样合入，并在 `Input.cpp` 里补了说明
  为什么不用 `IsConfigurable()`。
- **修复 UI 里无法录上鼠标全轴**：`CInputSystem::CheckMouseSources()` 在
  `fullAxisOnly == true` 时先算出主导轴（Dominant Axis），只允许该轴进入 `sources`。
  鼠标 X/Y 是绝对坐标，指针偏离屏幕中心时两轴都能通过 `IsActive()`，而人手的轻微抖动
  会让两轴同时成立，被拼成 `MOUSE_XAXIS+MOUSE_YAXIS`——`GetCombinedType()` 判定为非法
  组合后整帧结果被丢弃，于是用户按住横向拖动也绑不上 `P1 Analog Gun X-Axis`。
  主导轴按「相对显示区中心的位移 ÷ 该轴自身长度」比较（避免长轴仅凭几何取胜），
  滚轮轴用累积增量。`CheckMouseSources()` 只被 `ReadMapping()`（绑定录制/校准）调用，
  不在每帧路径上，对游戏运行没有开销。
- **修复「键位预设」下拉框选完立刻回弹**：`DrawQuickSettings()` 里 `ComboRow()` 返回真时
  立即 `config.Set("ControlPreset", ...)`。此前只改局部 `presetIndex`，下一帧又从 INI
  读回旧值重算，界面看起来像控件失效。真正应用预设仍由「应用推荐键位」按钮执行。
- **新增「设为启动时自动运行」**：游戏列表右键菜单新增一项，当前行就是被标记的 set 时
  显示「取消启动时自动运行」，否则显示「设为启动时自动运行」；被标记的行在名称列前加
  `★ `。新配置键 `AutoLaunchGame`（默认空，写在 `DefaultConfig()` 的公共段而不是 Vulkan
  专属段，否则 LegacyOG 读不到会 abort）。列表扫描完成后由进程级 `s_autoLaunchDone`
  保证只尝试一次：找到且 `supported` 就选中并启动，否则留在列表并记一行日志；
  从游戏按 Esc 返回后标志已为真，不会反复重启。菜单项的写入立即落盘
  （`g_writeSettings`），`SaveGameListPreferences()` 同步保存该键，所以即使这次拒绝保存
  其它设置，星标也不会丢。
- 构建验证：`build-cmake.bat -DUSE_SDL3=ON` 两个目标均链接成功，改动的 5 个文件在两个
  目标里共 10 个对象全部重编译，0 错误 0 警告（唯一告警来自捆绑的 SDL2
  `Libraries/SDL2-2.30.11/src/joystick/SDL_joystick.c` 的 C4819，与本轮无关）。
  短时启动实测：设置窗口正常打开、57 个 ROM 列出、日志无 `Node "..." does not exist`。
  已部署到 `dist/Model-3-Vulkan/`（原 `dist/Supermodel-VK` 已于 2026-10-29 改名）。

**Release notes**

```
en: Hotkeys can now be set from the INI file (InputUIExit, InputUISaveState, ...), which is what
    front ends such as RetroBat write for cabinet builds; that two-line patch came from RetroBat's
    Tartifless. Binding a mouse axis in the interface works instead of silently failing, the
    control-preset combo no longer snaps back, and the game list gained "Auto-Launch on Startup".
zh: 热键现在可以从 INI 配置（InputUIExit、InputUISaveState 等），RetroBat 等前端可直接写入
    （补丁来自 RetroBat 的 Tartifless）；界面上绑定鼠标轴不再静默失败，键位预设下拉框不再回弹，
    游戏列表新增「设为启动时自动运行」。
```

### 收尾修复：自动启动边界、全轴录制边界、默认 sdlgamepad 下的排挡杆

- **「核心」页顶部的裸输入框**：`AutoLaunchGame` 在 `DefaultConfig()` 里属于 `Core` 组，
  上一轮只加了右键菜单，忘了把它列进 `Gui.cpp` 的 `HasDedicatedUI()`，于是通用渲染器
  在「核心」页把它画成了一个未翻译的 `InputText`。已加入该列表（并注明它是列表偏好，
  不是仿真设置）。
- **触发自动启动时设置窗口闪一下**：`GUI()` 里自动启动把 `exit` 置真后，仍然走完了
  `BeginFrame()` → `Render()`，`GuiRunOnce()` 随即撤掉启动图并 `RaiseSettingsWindow()`，
  下一行才 `break` 进游戏——看上去就是窗口一闪。现在 `GUI()` 在自动启动/SM3D_AUTOSTART
  检查之后、`BeginFrame()` 之前直接 `return`（Vulkan 构建）；`GuiRunOnce()` 的循环末尾
  在 `exit` 为真时只 `HideStartupSplash()` 清掉启动图并 `break`，**不再抬升设置窗口**，
  窗口全程保持隐藏。原来那处「首帧已呈现就撤启动图」的分支加了 `!exit` 门控。
- **右键菜单防呆**：未收录/缺文件的 ROM（`!entry.supported`）且当前不是星标行时，
  「设为启动时自动运行」用 `BeginDisabled()` 置灰；已经是星标的行仍允许
  「取消启动时自动运行」，否则一个被删掉的 zip 留下的偏好只能改 ini 才能清。点击该项时
  同步 `selectedGameIndex = row`，左侧封面与高亮跟着右键行走（与「管理存档...」一致）。
- **两个全轴不能 `+` 组合**（`MultiInputSource.cpp`）：`GetCombinedType()` 里第二个
  `SourceFullAxis` 以前会静默通过并返回 `SourceFullAxis`，现在与「全轴 + 半轴」一样返回
  `SourceInvalid`。这种组合只可能由旧录制路径写出，`ParseMultiSource()` 会拒绝它。
- **`ReadMapping()` 区分「收集初始屏蔽源」与「正式录制」**：`CheckAllSources()` /
  `CheckMouseSources()` / `CheckJoySources()` 新增 `capturing` 参数，`ReadMapping()`
  第 1957 行传 `false`、循环内第 1980 行传 `true`。
  - `capturing == false` 时不做任何主导轴过滤、不做单轴截断、不区分正反向——鼠标停在
    角落、踏板停在 `-32768` 这类「开机就激活」的轴必须全部进 `badSources`，否则进循环
    第一帧就被当成用户输入。
  - `capturing == true` 且 `fullAxisOnly` 时：`sources` 里已经有一个 `SourceFullAxis`
    就直接返回，回中过程中不再追加第二条全轴（`+MOUSE_YAXIS`、越过中心的
    `+MOUSE_XAXIS_INV` 都不会再出现）。
  - 主导轴只在 `capturing` 阶段计算，且跳过已经在 `badSources` / `sources` 里的源，
    避免屏蔽列表里的静止轴抢走主导权。
  - 同一物理轴统一归并为正向（`AXIS_FULL`）：由于底层 `ScaleAxisValue(0, 0, 3) >= 2`
    在负半轴只触发 `AXIS_INVERTED`、正半轴只触发 `AXIS_FULL`，现在将同轴的 `AXIS_FULL` 与
    `AXIS_INVERTED` 视为同一物理轴配对检测——向任意方向（左/右、上/下）移动鼠标或推摇杆都能被
    检测并统一录制为正向全轴（`MOUSE_XAXIS` / `JOY1_XAXIS`），同时初始屏蔽阶段也会把正反向
    一并加入 `badSources`（防止停在 `-32768` 的踏板回升过零点时误触发正向源）。
  - `CheckJoySources()` 补上与鼠标相同的主导轴逻辑，按
    `|joyVal - offVal| / range` 归一化比较各活动全轴（短行程排挡杆不会因为行程小而输给
    方向盘）；`ANY_JOYSTICK` 合并录制时按设备逐个取值，因为合并源没有单一原始值。
- **默认 `sdlgamepad` 模式下支持独立排挡杆 / >16 键外设**（SDL3）：
  - `OpenJoystickAt()` 不再跳过 `!info.isGamepad` 的设备（`MOZA H-Shifter`、手刹这类
    原始摇杆以前直接被丢弃，绑不上任何键）。
  - 新增 `m_isGamepad`（与 `m_gamepadIds` 同步增删）：标准手柄走 SDL 标准布局
    （`numAxes = 6`、`numPOVs = 4`、`numButtons = max(17, info.numButtons)`），其余设备
    走自身的 `numAxes/numHats/numButtons`。
  - `GetJoyAxisValue()` / `IsJoyPOVInDir()` / `IsJoyButPressed()` / `LogPadChanges()`
    的 `m_useGameController` 判定改成按设备：
    `m_useGameController && m_isGamepad[joyNum]`。
  - `IsJoyButPressed()` 对标准手柄保留 `0..16` 的 `PadButton` 映射；`butNum >= 17`
    （如 `BUTTON113`–`BUTTON119`）或非标准设备回退到
    `state.buttons.test(butNum)`（上界 `kMaxGamepadButtons`）。
  - 与标准手柄零冲突：游戏控制器仍按标准布局读取，已有 `JOY1_BUTTONn` 绑定含义不变。
- 构建与部署：`build-cmake.bat -DUSE_SDL3=ON` 两个目标均链接成功，改动的
  `Gui.cpp`、`InputSystem.cpp/.h`、`MultiInputSource.cpp`、`SDLInputSystem.cpp/.h`
  强制重编译后 0 错误 0 警告。两个 exe 已复制到 `dist/Model-3-Vulkan/`，SHA-256 与
  `build-cmake/bin/` 逐字节一致：
  `Model3-Vulkan.exe` = `b8e8b16740280af32a444c9083ba3e494dab3b360da529470ead02f54d0abf06`，
  `Supermodel-LegacyOG.exe` = `e4b517a82ec3ebc406c9a236b9d6baae1714351fe6ad86a82664a3051bcbf5e3`。
- 运行验证（`dist/Model-3-Vulkan`，临时在 ini 尾部写入 `AutoLaunchGame = vf3`，
  测完已从备份还原）：日志依次为
  `GUI: game list restored` → `GUI: auto-launching 'vf3' at startup.` →
  `Configuration successfully saved` → 直接进游戏；**没有** `Splash: replaced by the first
  presented frame` / `removed by the safety deadline` 那两行，说明启动图是被 `exit` 分支
  静默撤掉的，设置窗口未抬升。无 ImGui 断言、无 `Node "..." does not exist`。
  未做实机外设测试（需 MOZA 排挡杆与多踏板方向盘）。

**Release notes**

```
en: Cleaned up the auto-launch edge cases and the full-axis capture path. The Core page no longer
    has a stray untranslated text box, launching straight into a starred game no longer flashes the
    settings window, and the star menu is disabled for sets that cannot start. Binding a full axis
    now records exactly one axis (positive direction, never while a previous one is still held) and
    a resting pedal or a pointer in a corner can no longer trigger it. In the default SDL3 input
    mode, standalone shifters and handbrakes are opened alongside game controllers, and devices
    with more than 16 buttons can be bound.
zh: 收尾修复自动启动与全轴录制。核心页不再多出未翻译的输入框；自动启动进游戏不再闪现设置
    窗口，无法启动的 set 不能再打星；录制全轴只会录一条（优先正向，回中前不追加新轴），
    静止的踏板和停在角落的鼠标不会误触发。SDL3 默认模式下独立排挡杆/手刹会与手柄一起打开，
    超过 16 键的外设也能绑定。
```

### 修复：「关于与开源许可」弹窗偏左、偏上

> 并入尚未发布的 **v0.9.8.6**，`MODEL3VULKAN_VERSION` 不再改动。

- 症状：点击设置窗口 footer 的「关于与许可」徽章后，模态框不在窗口正中，而是明显偏左
  偏上；设置窗口缩放或换到另一种 DPI 的显示器后位置还会再变一次。
- 根因：`BeginPopupModal()` 的默认居中用 `ImGuiCond_FirstUseEver`，只在弹窗**出现那一帧**
  算一次坐标。该弹窗带 `AlwaysAutoResize` 且没有固定宽度，`UiText()` / `UiTextDim()` 又把
  换行宽度取自 `GetContentRegionAvail()`——首帧量到的是文字还没换行时的一行宽度，后续帧里
  换行后的内容把窗口撑宽，新的宽度又改变换行位置，直到窗口尺寸稳定为止；居中坐标却是按最初
  那个窄尺寸算的，之后没有任何代码再改位置，所以停在偏左偏上处。
- 修复（`Src/OSD/SDL/Gui.cpp`，`GUI()` 内）：打开弹窗前显式
  `SetNextWindowPos(DisplaySize * 0.5f, ImGuiCond_Always, (0.5f, 0.5f))`，并
  `SetNextWindowSize((min(580 * uiScale, DisplaySize.x - 48 * uiScale), 0), ImGuiCond_Always)`。
  宽度锁定后 `GetContentRegionAvail().x` 不再随内容变化（ImGui 把 `SetNextWindowSize()` 当作
  `AlwaysAutoResize` 的覆盖项，只有高度继续自适应），换行反馈环被切断；位置每帧重算，
  设置窗口缩放、界面缩放或跨屏 DPI 变化后仍然居中。
- 验证：`build.bat` 完整重建两个 exe 并组装 `dist/Model-3-Vulkan`，0 错误。打开设置窗口、
  点击徽章后抓取客户区（1920×1200 @150%）：弹窗整体外框 `x 524..1395`、`y 281..917`，
  中心 `(959.5, 599)`，客户区中心 `(960, 600)`——水平差 0.5 px、垂直差 1 px。
  量外框时要把标题栏底色（`ImGuiCol_TitleBg`）和正文底色一起算进去，只看 `PopupBg`
  会漏掉标题栏、误判成「偏下 22 px」。`dist/Model-3-Vulkan/` 的两个 exe 与
  `build-cmake/bin/`、`MSVC/x64/Release/` 的 SHA-256 逐字节一致：
  `Model3-Vulkan.exe` = `6cd6148e401a8e2ce92ac102475731b60865cfca44cdbbe7706cbec98b59b62c`，
  `Supermodel-LegacyOG.exe` = `12b875e614b2f375cde980a3081e5351abc3c690b44bc96385ef227c7cbf9454`。

### 整理：第三方署名清单移到 `Docs/NOTICE.md`

> 并入尚未发布的 **v0.9.8.6**。

- `NOTICE.md`（第三方/上游组件署名）从仓库根移到 `Docs/NOTICE.md`；本项目自己的
  `LICENSE-ADDITIONAL-TERMS.md`（GPLv3 §7 附加条款）与 `TRADEMARK.md`（品牌与商标政策）
  继续留在根目录，保持源码根目录一眼可见。
- `build.bat` 与 `Scripts/package-cmake.sh` 的取源路径改为 `Docs/NOTICE.md`，打包目标仍是
  `Data/NOTICE.md`，玩家侧路径不变（`Docs/README-PACKAGE.txt` 与关于弹窗里的 `Data\...`
  不做改动）。
- `README.md` 的第三方署名链接改指 `Docs/NOTICE.md`；`Docs/NOTICE.md` 内部相对链接同步为
  `../LICENSE-ADDITIONAL-TERMS.md`、`../TRADEMARK.md` 与同级的 `LICENSE.txt`；`AGENTS.md`、
  `INDEX.md` 的路径说明一并更新。
- 验证：`bash -n Scripts/package-cmake.sh` 通过；全仓搜索已无指向根目录 `NOTICE.md` 的引用；
  重打包后 `Data/NOTICE.md` 内容与 `Docs/NOTICE.md` 一致。

### 发布：v0.9.8.6

- tag `v0.9.8.6` 推送到 `master` 后由 GitHub Actions 自动完成：`build-model3.yml` 构建
  Linux/macOS 两个包，`release.yml` 创建 release。三个 job 全部 success：
  <https://github.com/Igloo-Garage/Model-3-Vulkan/releases/tag/v0.9.8.6>
- 同时修了一件会让发布直接失败的事：仓库里的 `release.yml` 仍在 grep
  `SUPERMODEL_FORK_VERSION`，改名后该宏已不存在，`Get version` 会产出空的 `v`。
  已随本次提交改为 `MODEL3VULKAN_VERSION`。
- 资产：Windows 包（本地 `build.bat -package` 生成后上传）、Linux、macOS 三个包与
  `SHA256SUMS.txt`（只含三个发布包，与上一版格式一致）：

  ```
  f787ddea75195e99ff25212ec55366517a68ebd66571febf61caf571635fb010 *Model3-Vulkan.v0.9.8.6.for.Windows.zip
  d52ecfba32580fdef19aefcb2dfdefae554cde5b8ff3130d4801a8db813bd17c *Model3-Vulkan.v0.9.8.6.for.Linux.tar.gz
  ae4b7d1a0bbe424cbc6c91ab217ebf2e98b4bfcbb221f5c003c92aa9f79fdcc1 *Model3-Vulkan.v0.9.8.6.for.macOS.tar.gz
  ```
- 发布正文（`RELEASE_NOTES.md`）新增 RetroBat / Tartifless 署名、SDL3 排挡杆与多键设备条目；
  Auto-Launch 条目下加入两张操作截图，链接指向 tag 内的 `Docs/images/updates/*.webp`
  （文件名带空格，链接用 `%20` 编码）。README 首页原有的 8 个 `Docs/images/` 截图链接未动。
- 旧代码归档：更新前把 `master` 的 `e0ddf03` 推到分支 `archive/v0.9.8.3b`，加上现有的同名
  tag，旧文件名（`SupermodelHostCore` / `SupermodelResources` / `supermodel_build_bot.py` 等）
  随时可取回。
- 源码按本地布局提交：`LICENSE-ADDITIONAL-TERMS.md`、`TRADEMARK.md` 在仓库根目录，
  `NOTICE.md` 在 `Docs/`；仓库根目录的旧 `LICENSE.txt` 随本地布局被删除（GPL 正文仍在
  `Docs/LICENSE.txt`，发行包内为 `Data/LICENSE.txt`）。
- 备注：GitHub 的许可证识别只扫描仓库根目录，根目录没有 `LICENSE*` 后仓库首页可能不再显示
  GPL-3.0 徽标；需要徽标时在根目录补一份 `LICENSE.txt` 即可，发行包不受影响。
- 发布后补提交（不移动 tag）：根目录补上 `LICENSE.txt`（与 `Docs/LICENSE.txt` 逐字节相同，
  `cmp` 验证），让 GitHub 识别为 GPL-3.0；`build.bat` 仍从 `Docs\` 取源打进 `Data/`，不变。
- 同一提交把 release notes 里两张截图从 Markdown 改为 HTML `<img width="48%">` 并排显示，
  并用 `gh release edit v0.9.8.6 --notes-file RELEASE_NOTES.md` 更新已发布的正文（tag 未动）。

## 2026-10-28

> 本轮已发布为 **v0.9.8.3b**（`SUPERMODEL_FORK_VERSION`）：含下面的音效修复、
> F4/F3 热键调整、DDS 纹理包支持与 Vulkan 布局/同步修复。

### 修复：所有游戏只有音乐、没有音效

- 症状：任何游戏、任意存档或全新开机，MPEG 音乐正常，SCSP 音效（引擎、碰撞、菜单提示）
  全部消失。用户最初怀疑是上一轮「四声道折叠到二声道」的改动，实际无关。
- 根因在 `CSoundBoard::RunFrame()`：清空四个 SCSP 输出缓冲的
  `memset(audioFL/FR/RL/RR, 0, LENGTH_CHANNEL_BUFFER)` 被挂在了
  `if (soundDebug)` 的 **`else`** 上，而不是 `if (m_config["EmulateSound"])` 的 `else`。
  `soundDebug` 由 `SM3D_LOAD_DEBUG` 决定，正常游戏时恒为 false，于是**每一帧**流程都是：
  `SCSP_Update()` 写好四路效果音 → 紧跟着被 memset 清零 → 只有之后才混入的 DSB 音乐留下。
  所以音乐永远正常、音效永远没有，与存档无关（不存档也一样）。
- 修复：清零只放在「关闭音效仿真」的 `else` 分支里；`LoadDebug sound scsp done peak=`
  的峰值统计移进仿真分支内部；原位置补注释说明这个顺序的意义，避免再次被“顺手整理”回去。
- 诊断增强：`SM3D_AUDIO_LEVEL=1` 的每秒统计现在把源峰值拆成前后两组：
  `Audio: source peak %.1f (front %.1f, rear %.1f), mix peak %.4f, ...`。Model 3 的四声道里
  DSB 音乐与 SCSP1 走前声道、SCSP2 走后声道，「前声道很响、后声道为零」或「把音乐
  `MusicVolume` 调到 0 后源峰值仍不为零」就是效果音这一侧有没有信号的直接判据。
- 验证（`scudplus.zst12`，`SM3D_AUDIO_LEVEL=1`）：
  - 修复前只有音乐时：源峰值 6.7k–10.7k，混音峰值 0.10–0.16（与更早的日志一致）。
  - 修复后把 `MusicVolume` 设为 0（只剩 SCSP）：源峰值仍有 5.6k–11.7k，说明效果音
    确确实实在产生；修复前这一项必然是 0，因为缓冲刚被清零。
  - 音乐与音效同时开启时峰值 13.6k–16.3k；用户实机确认音效已恢复。
  - 该游戏的后声道只有约 46.8 的常驻底噪（SCSP2 基本不用），属正常，不是丢声道。
- 无损回归：音乐、四声道折叠、`NbSoundChannels=2/4` 的混合路径都没有改动。

**Release notes**

```
en: Fixed the missing sound effects. A misplaced else cleared the SCSP buffers right after they
    were filled, so every game played its MPEG music with no effects at all; the clear now only
    runs when sound emulation is switched off.
zh: 修复所有游戏只有音乐、没有音效的问题。一个错位的 else 在 SCSP 填完缓冲后立刻把它清零，
    现在只在关闭音效仿真时才清零。
```

### 高清纹理：F4 改为切换「替换贴图 / 原版」，重扫移到 F3

- 之前 F4 只调 `RequestHDTextureReload()`（重扫目录）；想对比替换效果得开面板改勾选框。
- 现在 **F4 直接切换 `LoadHDTextures`**（面板勾选框写的同一个值），`CVulkan3D::BuildDrawList()`
  每仿真帧读取，**下一帧生效**；已加载的图留在显存里，来回切零成本，也不重扫。
  首次按 F4 打开时会自动扫描目录（`Tick()` 发现 `m_scanned == false`）。
- 原「重扫」移到 **F3**（`KEY_F3,KEY_SHIFT+KEY_F4`：F3 与 Shift+F4 任一个都行），
  面板里的「重新加载高清纹理」按钮不变，打包作者边玩边换图不受影响。
- 面板提示、设置页文案、`Localization.cpp`、玩家手册热键表、`ConfigFile.h` 注释同步更新；
  顺手把提示文案里的 DDS 说明补齐（上一轮漏在 UI 文案里）。
- 验证：编译零警告；F4 日志 `HD textures: replacements enabled/disabled with the F4 shortcut.`，
  画面在替换/原版之间切换；F3 日志 `reload requested with the F3 shortcut.`。

**Release notes**

```
en: F4 now switches HD texture replacements on and off while playing, which is the quickest way
    to compare a pack against the original artwork. Rescanning moved to F3 (Shift+F4 also works); the in-game
    panel button still does the same thing.
zh: F4 改为在游戏中切换高清替换贴图与原版贴图（即时 A/B）；重扫目录改为 F3（Shift+F4 亦可），
    面板里的重新加载按钮不变。
```

### HD 纹理：压缩链分块上传 + uLayers 布局报错修复

**1) 压缩纹理的 Staging 容量边界**

- 现状：staging 环是 `kRingSliceBytes = 64 MB` **每帧槽**（`kMaxFramesInFlight = 2`），
  几何、RGBA 替换图、2D 上传共用同一帧切片。
- 旧行为：`UploadCompressed()` 一次 `ring.Alloc(payload.size())`。4096×4096 BC7 整链 ≈ 21.3 MB
  （level 0 就 16 MB）虽然能放进 64 MB，但一张图就占掉三分之一帧切片；一旦单张超过切片容量
  （当前被 4096 上限挡住，但改上限/格式就会踩到），`Alloc()` 每帧都失败 → 槽永远 pending、
  纹理永不出现，并且每帧刷 `Vulkan: ring buffer exhausted`。
- 现在：可续传游标 `(uploadMip, uploadBlockRow)`，每次最多 `kCompressedChunkBytes = 8 MB`；
  单层超过预算时按 **4×4 block row** 切（`imageOffset.y = blockRow * 4`，
  `imageExtent.height` 取剩余 texel 行，最后一块可为 1–3 行），`bufferOffset` 仍 16 字节对齐。
  每帧每张最多 `kCompressedChunksPerFrame = 4` 片（≤32 MB），环满就退出循环，下一仿真帧
  从游标继续，**不重传已传部分**。
- 新增 `CVulkanRingBuffer::Remaining()`：按剩余空间裁剪本次请求，环满表现为“延迟”而不是
  “分配失败”，不再刷错误日志。
- 完成时若用了多片，日志：
  `HD textures: 4096x4096 BC7 chain (21845 KB) uploaded in 14 piece(s) over 4 frame(s).`
- 实测：4 张 4096 图（共 74.6 MB）塞 64 MB 环，全部加载完成、无错误、画面正确；
  小包（≤1 MB/张）仍然一片一帧。

**2) `uLayers[4]/[5]` 的 `VUID-vkCmdDraw-imageLayout-00344`**

- 根因：`uLayers[4]/[5]` 是 trans1/trans2（MSAA 下是它们的 resolve 图），描述符声明
  `SHADER_READ_ONLY_OPTIMAL`；2D 合成器在 `CModel3::RenderFrame()` 里先于 3D 渲染，采样的是
  **上一帧**的场景目标。`DrawScene()` 的**白闪路径**（`white = m_blockCulling && !m_noWhiteFlash`）
  在 `begin()` 把三个附件转成 `COLOR_ATTACHMENT_OPTIMAL`、`finishScene()` 后直接 `return`，
  漏掉了其他路径末尾的统一交接 → 下一帧合成时布局仍是 COLOR_ATTACHMENT。这解释了它的间歇性
  （只在白闪帧之后出现一次）和“关掉 HD 也有”。
- 修复：把末尾交接抽成 `handOverTargets()`，白闪路径同样调用；`EnsureTargets()` 创建目标后
  立即把合成器可见的图从 UNDEFINED 转到 `SHADER_READ_ONLY`（首帧合成发生在 3D 之前，
  原来采样的是 UNDEFINED 状态）。
- 同一路径还暴露出一个既有同步缺口：MSAA 下 trans 附件由子 pass 以
  `finalLayout = COLOR_ATTACHMENT` 交回，下一帧的动态 pass 再 clear 时**不产生 layout 转换**，
  于是跨提交 WRITE_AFTER_WRITE。`begin()` 现在对 `loadOp == CLEAR` 的附件补一个**同布局**
  依赖 barrier（src/dst = `COLOR_ATTACHMENT_OUTPUT`/`WRITE`），不改布局、只补顺序。
- 验证：`Tools/Validate-Vulkan.ps1 -Mode sync`，MSAA 1/2/4 × `AlphaSortMode` 0/1 各两次、
  HD 开/关，以及带 DDS 包再两次：**VUID 0 / sync hazard 0 / Vulkan error 0**。

**Release notes**

```
en: Large block-compressed HD textures are now uploaded in resumable pieces, so a 4K DDS can no
    longer stall or spam the log when it does not fit the staging ring in one request. The
    intermittent uLayers layout VUID that sync validation reported is fixed as well.
zh: 大尺寸压缩纹理改为可续传分块上传，4K DDS 不会再因为一次申请不下而卡住或刷日志；
    同步验证里间歇出现的 uLayers 布局 VUID 也一并修掉。
```

### HD 纹理：原生 DDS（BC1/BC3/BC5/BC7）与 .dds.zst 支持

- **新增** `Src/Graphics/Vulkan3D/DdsLoader.h`：零依赖的 DDS 解析器，支持传统 FourCC
  （`DXT1`/`DXT3`/`DXT5`/`ATI2`/`BC5U`）与 `DX10` 扩展头（DXGI 71/72、74/75、77/78、83、98/99），
  分别映射到 `VK_FORMAT_BC1_RGBA_UNORM_BLOCK` / `BC2` / `BC3` / `BC5` / `BC7`。
  - 头部用 `std::memcpy` 读入（不做未对齐指针访问），校验 `dwSize == 124`、`ddspf.dwSize == 32`、
    尺寸非零且 ≤ 16384、拒绝 volume/array/cubemap。
  - **逐层预计算并校验**：`blocksWide/blocksHigh = max(1, (mip+3)/4)`，
    `mipBytes = blocksWide * blocksHigh * blockSize`，累计后与文件剩余字节比较；
    尾部不完整时把 `mipLevels` 截断到实际完整存在的层数，Level 0 不完整直接拒绝。
- **打包与查找优先级**：`.dds.zst` > `.dds` > `.webp` > `.png`。目录扫描时一次性建表
  （`m_index: hash -> {path, kind}`），运行期 `Resolve()` 不再做 4 次 `exists`。
  `.dds.zst` 文件名有两个扩展名，扫描器按“剥掉匹配到的后缀长度”取哈希，不能用 `path::stem()`。
- **`.dds.zst` 走既有的 HostCore zstd 通道**：`Model3VulkanHostCore` 新增可选导出
  `HostCore_DecompressZstd`（两段式：传 NULL 取解压后大小 → 分配后再解压），
  C# 侧复用 `ZstdCodec.GetDecompressedSize/Decompress`。C++ 侧新增 C++17 兼容的
  `OSD/SDL3/HostCoreZstd.h` 薄封装（Vulkan 核心仍是 C++17，不能包含用 `std::expected` 的
  `HostCoreBridge.h`）。旧模块没有这个导出时 `.dds.zst` 会被跳过并回退到同哈希的 `.dds`/`.png`。
- **Vulkan 端**：`CVulkanContext::CreateDevice()` 查询并启用 `textureCompressionBC`，
  布尔标志 `m_supportsBC`（`TextureCompressionBCSupported()`），启动日志
  `Vulkan: BC texture compression available/unavailable`。设备不支持时扫描阶段直接跳过 DDS 并告警。
  - BCn 图像的 `usage` 只有 `TRANSFER_DST | SAMPLED`（不加 `TRANSFER_SRC`/`STORAGE`，
    压缩块不能 blit、也不能被 shader 写入），`mipLevels` 取自文件。
  - Staging 子区间按 **16 字节对齐**分配（BC1 块 8 字节、其余 16，Vulkan 要求 `bufferOffset`
    是块大小的倍数），整条 mip 链一次 `memcpy`、一次 `vkCmdCopyBufferToImage`。
  - 上传前一次 barrier 把整张图 `UNDEFINED → TRANSFER_DST`，上传后一次 barrier
    `TRANSFER_DST → SHADER_READ_ONLY`，**完全跳过 `vkCmdBlitImage` 的 mip 生成循环**。
  - 采样器沿用原有配置（`maxLod = VK_LOD_CLAMP_NONE`，各向异性在支持时开启）。
- **验证**：DDS 解析器跑了 13 个用例（正常 BC1/BC5/BC7、截断 mip 链、Level 0 不完整、
  `dwSize` 错误、arraySize=2、cubemap、非 2 次幂、声明层数超出自然链、空指针），全部通过。
  实机 A/B（Daytona 2，`daytona2.zst1`，`-Frame 900` + `RefreshRateMode=0`，MSAA 4x）：
  同一张图的 PNG 版本与 BC1 DDS 版本**逐帧平均差 0.08/255**、朝向一致（把源图上下翻转后
  生成的 DDS 差异明显更大）；`.dds.zst` 路径同样通过（平均差 0.08/255）。
  日志：`HD textures: first match ... (.dds.zst, slot N)` 与
  `HD textures: 282 replacement(s) indexed ... (1 block-compressed ...)`。
  同步验证（`Validate-Vulkan.ps1 -Mode sync`，496×384/1x）多次运行 **sync hazard 恒为 0**；
  其中几次会报 `uLayers[4]/[5]` 的 `VUID-vkCmdDraw-imageLayout-00344` —— **关掉 HD 也同样
  偶尔复现**（`AlphaSortMode=0` 下也是），属于既有的 2D 合成/透明目标描述符问题，不是 DDS 引入。
- **不改 Y 方向**：原 PNG/WebP 路径把图像第 0 行直接上传到 `v=0`，全程没有 CPU 端翻转；
  DDS 路径同样把第 0 个块行放到 `v=0`，两条路径朝向一致。离线压制 **不需要** `-yflip`。

**Release notes**

```
en: HD texture packs can now use DDS (BC1/BC3/BC5/BC7) and zstd-compressed .dds.zst files.
    The blocks are uploaded as-is, mip chains included, so VRAM use and load time both drop.
    PNG/WebP still work, and the pack's file priority is .dds.zst > .dds > .webp > .png.
zh: HD 纹理包现在支持 DDS（BC1/BC3/BC5/BC7）与 zstd 压缩的 .dds.zst。压缩块连同 mip 链
    原样上传，显存占用和加载时间都下降；PNG/WebP 仍照常工作，优先级为
    .dds.zst > .dds > .webp > .png。
```

### 文档：恢复 README 的截图展区

- 现象：GitHub 首页的 README 变成纯文字，原本的 8 张界面与画面对比图全部消失。
- 原因不是图丢了：v0.9.8.3b 重写 README（提交 `3a5cf94`）时删掉了 8 行
  `![...](Docs/images/*.webp)` 引用，图片文件本身一直在仓库的 `Docs/images/` 里
  （共 9 张，含未被引用的 `Video settings.webp`）。
- 恢复：按原位置把引用插回「What's New in Milestone v0.9.8.3a」——透明度模式示意、
  HD 纹理选项、两张 Daytona 2 HD 对比、存档管理、明暗主题、刷新节奏。
  顺手把 footer 的空链接 `[](https://ko-fi.com/igloogarage)` 换成 GitHub 的 Ko-fi 按钮图，
  与 master 上 `e093e51` 那次提交保持一致（本地那份比它早 5 分钟，缺这一行）。
- 本地工作树补齐：`Docs/images/` 原本缺这 9 张 webp（只存在于 `v9.8.3a_source` /
  `v9.8.3b_source` 快照），已复制回来；目录名由 `Docs/Images` 改成 `Docs/images`，
  与仓库里已提交的大小写一致。Windows 不区分大小写，但重新打包或新建仓库时
  `Images` / `images` 会变成两条路径。
- 快照同步：`v9.8.3b_source/README.md`（保持 CRLF）与 `v9.8.3b_upload/README.md`（LF）
  一并换成带图版本，避免下次整包上传把修复冲掉；`v9.8.3a_*` 是历史快照，未改。
- 验证：GitHub 上的 `README.md` 与本地**逐字节一致**（blob `b848c512`，提交 `e0ddf03`），
  `Docs/images` 引用 8 处，页面刷新后可见。
- 仅文档变更，不涉及代码，**不需要重新编译或发版**（版本号仍为 `0.9.8.3b`）。

**Release notes**

```
en: Restored the README screenshot gallery. The images were still in the repository; the
    v0.9.8.3b rewrite had dropped the markdown links to them.
zh: 恢复 README 里的截图展区。图片一直都在仓库里，v0.9.8.3b 重写时删掉的只是引用。
```

### 文档：HD 纹理模块化与移植 runbook

- 新增 `devdocs/RUNBOOK-HD-TEXTURES.md`：把 HD 纹理系统拆成 4 层（数据/调度/渲染/模拟器），
  给出可移植边界、`HDPackKit` 接口契约、Stage 0–7 的退出条件、后端适配表、验收矩阵与
  反模式。核心结论是**先移植 dump、再移植 load**：dump 定义哈希与文件名，是 load 的 oracle。
- 哈希契约附 3 个黄金测试向量（全 0 页 / 递增页 / 整页，`pageWidth=16`、`pageHeight=8`），
  已用实际 C++ 实现核对，移植时可先对这三个数。
- 新增项目内 skill `.agents/skills/hd-texture-port/SKILL.md`：只做触发、四个决策问题与
  阶段门禁，细节指向 runbook，避免两份文档漂移。
- `INDEX.md`、`AGENTS.md`、`hdtexture_skills.md` 已加指针。
- 仅文档变更，不涉及代码，**不需要重新编译或发版**（版本号仍为 `0.9.8.3b`）。

**Release notes**

```
en: Added a developer runbook for modularizing and porting the HD texture pack pipeline.
zh: 新增 HD 纹理系统的模块化与移植开发手册。
```

## 2026-10-27

### 界面：按钮 hover 色与 Tab 一致可见

- 浅色主题的 `ImGuiCol_ButtonHovered`（`p.hover`）与 `ImGuiCol_Button`（`p.button`）只差约 4/255，
  底部按钮和「浏览」看起来完全没有 hover 反应；顶部 Tab 用的是 accent 着色，所以明显。
- 新增 `UiPalette::buttonHover`：WarmPaper 取 12% accent 混合，WarmDark 取更亮的暖灰；
  Classic 保持原值（逐值复刻，不改旧外观）。`UiButton` 的 120ms 动画现在有可见的目标色。
- 验证：hover 截图 `weekly_dev_reports/2026-10-27-ui-review/button-hover.png`。

**Release notes**

```
en: Buttons now have a visible hover colour instead of a ~4/255 shift that read as no response.
zh: 按钮 hover 改为可见的高亮色，不再只是约 4/255 的变化。
```

### 画面：低分辨率渲染新增 992×768（2x 原生，等比放大）

- 「画面」→「画面缩放」的低分辨率下拉框现在有三档：关闭 / 496×384（原生）/ 992×768（2x 原生）。
  三者都只影响 3D 场景的渲染尺寸，2D HUD 仍按 496×384 合成；992×768 比 496×384 清晰，
  填充开销仍远低于画面分辨率，适合“核显跑不动画面分辨率、又觉得 496×384 太糊”的情况。
- `InternalResolution` 本身就是索引（0/1/2），`Vulkan3D` 按值设置 `m_internalExtent`：
  1 → 496×384，2 → 992×768。`Vulkan2D` 的合成路径不变，仍把场景图放大到画面矩形。
- 与 `IntegerScale`（点对点）仍然互斥：后者按原生 496×384 取整数倍，勾选后会覆盖「分辨率」；
  两种方式都开时低分辨率优先，界面会提示。下一局生效。
- 验证：`-Ini "InternalResolution=2"` 读 `scudplus.zst1`，日志
  `Vulkan: low-resolution rendering - 3D scene 992x768 upscaled onto the 2790x2160 picture.`，
  截图见 `weekly_dev_reports/2026-10-27-ui-review/internal-resolution-992x768.png`；
  `InternalResolution=1` 路径未改。

**Release notes**

```
en: Low-resolution 3D rendering now also offers 992x768 (2x native), between the 496x384 option
    and full picture resolution.
zh: 低分辨率 3D 渲染新增 992×768（2x 原生）档位，介于 496×384 和画面分辨率之间。
```

### 启动图：没有系统 WebP 编解码器时改用内置 libwebp

- **问题**：启动图是内嵌 WebP，`SplashScreen.cpp` 只用 WIC 解码。WIC 只有在安装了 Microsoft
  Store 的「Webp Image Extensions」时才认识 WebP；干净系统或部分笔记本上没有这个包，
  `DecodeArtwork()` 失败，日志打 `Splash: the artwork could not be decoded; the window will be
  shown empty instead.`，启动图不显示，但程序正常进入界面。
- **为什么封面正常**：`Gui.cpp` 的封面解码在 WIC 失败后会回退到编译进来的 libwebp；启动图没有
  这条回退，所以“界面出来之后 WebP 就能用”只是两个解码路径不一致。
- **修复**：`DecodeArtwork()` 在 WIC 失败时用 `WebPDecodeBGRA()` 解码同一份内嵌字节，再交给
  现有的 WIC 缩放/格式转换。日志：
  `Splash: artwork decoded with the bundled libwebp decoder (the system WebP codec is unavailable).`
  装了系统 WebP 编解码器时仍优先走 WIC，行为不变。
- **验证**：`SM3D_SPLASH_LIBWEBP=1` 强制走内置解码，日志出现上述行且
  `Splash: replaced by the first presented frame`；不设变量时仍只有 `Splash: artwork verified`。

**Release notes**

```
en: The startup splash now falls back to the bundled WebP decoder when Windows has no system WebP
    codec, so it appears on clean installs too.
zh: 系统没有 WebP 编解码器时，启动图会用内置解码器显示，干净系统上也能看到。
```

### 启动图：官方构建的尾注改为「Developed by Rex」

- `SplashScreen.cpp` 的画面底部原来写 `Model 3-Vulkan  -  official build`，现在写
  `Model 3-Vulkan  -  Developed by Rex`。哈希不匹配时的红色 `UNOFFICIAL BUILD` 保持不变，
  它仍是启动图的防篡改提示。
- 启动图本身和 `Scripts/UpdateSplashSignature.ps1` 不变；只改文字，不影响指纹。

**Release notes**

```
en: The splash caption now reads "Developed by Rex" instead of "official build". The red
    UNOFFICIAL BUILD warning for a modified splash is unchanged.
zh: 启动图底部文字由「official build」改为「Developed by Rex」；启动图被修改时的红色
    UNOFFICIAL BUILD 警告不变。
```

### 输入：SDL3 版默认值改为 SDL 手柄，控制器页说明 Xbox 手柄的去处

- **问题**：`DefaultConfig()` 在 Windows 上无条件写 `InputSystem = xinput`，但 SDL3 版只编译
  SDL 后端。新装或旧 ini 会每局打一行回退日志，控制器页的下拉框因为没有匹配项而显示
  “SDL”，文件里却是 `xinput`——看起来像 XInput 被删掉了。
- **修复**：SDL3 版默认写 `sdlgamepad`，允许值只保留 `sdl`/`sdlgamepad`；LegacyOG（SDL2）
  仍是 XInput 默认、五项可选。新增 `AlignLegacyInputSystem()`：SDL3 版读到
  `dinput`/`xinput`/`rawinput` 时在内存里改成 `sdlgamepad` 并记日志，设置界面下次保存会把
  文件改正；按游戏段仍可能覆盖，`GetInputSystem()` 的回退保留。
- **界面**：控制器页在 SDL3 版新增一行提示「Xbox 手柄选「SDL 手柄」；本版本不提供 XInput /
  DirectInput」。`(?)` 提示按构建分开：SDL3 版说明 Xbox 手柄经 XInput 读取、左右扳机分开；
  SDL2 版仍列出 XInput/DirectInput/Raw Input。`DetectController()` 的推荐值也按构建分开：
  SDL3 版只推荐下拉框里存在的 `sdlgamepad`（SDL 认识的手柄）或 `sdl`（原始 joystick），
  SDL2 版保持 Xbox→`xinput`、未知→`dinput`。
- **手册**：英文版排错条目从「输入系统选 XInput」改为「SDL 手柄」，并说明 SDL3 版没有单独的
  XInput/DirectInput 后端。
- **验证**：控制器页截图（下拉框只有 SDL / SDL Gamepad，提示行可见，
  `Detected: Xbox 360 Wireless Controller (suggested input system: sdlgamepad)`）；
  `-Ini "InputSystem=xinput"` 开局日志
  `Input: 'xinput' is not available in the SDL3 build; using 'sdlgamepad'.`，退出后 ini 为
  `InputSystem = sdlgamepad`。

**Release notes**

```
en: The Vulkan build now defaults to SDL Gamepad, explains in the Controllers page that Xbox pads
    are read through it, and rewrites an old xinput/dinput setting instead of silently falling back.
zh: Vulkan 版默认输入系统改为 SDL 手柄，控制器页说明 Xbox 手柄走这里，并把旧的
    xinput/dinput 设置自动改写，而不是只打一行回退日志。
```

### 存档读档：Scud Race 卡死、无声与四声道折叠（`scudplus.zst1`）

**症状链**

- 从「管理存档」冷启读 `scudplus.zst1`（选车画面）会卡死：主线程停在
  `EndSimulationFrame()` 等声板，日志最后一个声板标记是 `DSB begin`，没有 `DSB done`。
- 卡死修掉后，选车画面几乎没有声音；进赛道只剩音乐，没有引擎、碰撞、轮胎声。
- 同一份存档把 `NbSoundChannels` 设成 2 就正常，设成 4 就不行。

**根因 1：存档里的 MPEG 窗口是哨兵值**

`CDSB1::LoadState()` 用 `usingMPEGStart/End` 重建解码器，但 Scud Race 在音乐已经
播放时会把这几个影子变量留成 `0x80008000`（板子的“无曲目”哨兵），而 `MpegDec`
本身仍在正常解码：

`LoadDebug dsb1 raw: loaded=1 start=2147516416 end=2147516416 loopStart=0 loopEnd=2147516416 offset=712512`

（2147516416 = `0x80008000`；`offset` 是真实的播放位置，说明存档瞬间确实在放歌。）
旧 `SetMemory()` 不校验，`dec.size` 变成负数：`EndOfBuffer()` 恒真、`loop` 每轮把
`pos` 清 0、`frame_bytes` 也是负的，`DecodeAudio()` 于是永不返回。

**修复**

- `MpegDec::SetMemory/UpdateMemory` 拒绝 `data == nullptr || offset < 0 || length <= 0`；
  `Stop()` 连窗口一起清；`SetPosition()` 越界钳到 0；`DecodeAudio()` 在 `size <= 0`、
  `frame_bytes <= 0`、位置不前进、连续 8 个零样本帧时走 `EndWithSilence()` 退出。
- `CDSB1/CDSB2::LoadState()` 把旧字段当不可信输入：只接受
  `0 <= start < end <= 0x1000000`、`0 <= loopStart`、`loopEnd > 0`、
  `loopStart + loopEnd <= 0x1000000`（**本分支 `usingLoopEnd` 是长度，不是终点**），
  非法或 `isPlaying == 0` 就 `MpegDec::Stop()`。
- `CSoundBoard::LoadState()` 在 `SCSP_LoadState()` 之后调 `SCSP_RefreshIRQ()`，用两个
  SCSP 的 `pend & en` 重新驱动 68K 中断线；原来读档后声板收不到新的效果命令，
  正是“有音乐、没音效”的那一半。

**根因 2：四声道请求落到了二声道设备上**

`scudplus` 在 `Games.xml` 里是 `QuadFrontRear`：DSB 音乐 + SCSP0 走前声道，SCSP1
走后声道（机台的后置喇叭）。这台机器的默认端点只有 **2 声道**：

`Audio: device 'default' has 2 channel(s) (48000 Hz); folding the quad mix down to 2.`

`NbSoundChannels=4` 时 SDL 仍会接受 4 声道流，再由它自己用 `SDL_ConvertQuadToStereo`
折叠（权重 0.421 / 0.359 / 0.22），音量比我们自己的平均小一截，而统计面板照旧显示
“4 ch”。选车画面的声音和一部分赛道音效只存在于后声道，听感就是“几乎没声音 /
只剩音乐”。

**修复**：SDL3 路径在打开流之前用 `SDL_GetAudioDeviceFormat()` 查询输出设备；设备
声道数不足 4 时把 `nbHostAudioChannels` 降到 2（或 1），由 `MixChannels()` 自己把
后声道平均进前声道，日志写明设备真实声道数与折叠结果。设备真的支持 4 声道时行为
不变；5.1/7.1 端点仍交给 SDL 把 quad 铺到 BL/BR。

**同时补上的：选车 BGM 的续播**

旧存档的问题是“解码器在放，寄存器是哨兵”，这种情况旧格式表达不了。现在
`CDSB1::SaveState()` / `CDSB2::SaveState()` 在 DSB 块之后追加一个可选块
`DSB1 MPEG` / `DSB2 MPEG`（magic `MPG2`），写入解码器真实的活动窗口
`activeOffset / activeSize / pos / loop`；读档时优先用它，块不存在或非法才回退到
旧字段。老版本读新存档会跳过这个块（`CBlockFile` 按名字查找），并且 `isPlaying`
在保存时已经按“旧字段是否可用”过滤，不会再被哨兵值带进死循环。

- 验证：`magic=ok offset=3669124 size=930815 pos=179712 loop=1 restored`，读档后
  watchdog 的 `mpegpos` 从 179712 继续增长（268992 → 285120 → 301248 …）。
- **旧存档没有这个块**：`scudplus.zst1` 里没有真实窗口可恢复，所以读档瞬间选车 BGM
  仍是停的，进赛道游戏重新发曲目命令后才响。用新版本在游戏中重新存一次即可；
  不要为了“救旧档”去猜一个窗口，猜错会把下一首曲子接着放出来。

**验证**

```
# 4 声道配置（ini 未改）也应自动折叠并恢复正常电平
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Capture-Frame.ps1 `
  -Set scudplus -Slot 1 -Frame 1800 -Env "SM3D_DEFER_LOAD_SECONDS=3;SM3D_LOAD_DEBUG=1"
# 统计浮层：Audio: ... 2 ch L 46% R 49% u1/00；日志：folding the quad mix down to 2
```

- `LoadDebug mpeg active` 与 watchdog 的 `mpeg=|mpegpos=` 是这条链路的一眼判据；
  `SM3D_SAVE_STATE_FRAME` / `SM3D_SAVE_STATE_SLOT` 用于“读档 → 跑 N 帧 → 存新档 →
  再读”的回归（Vulkan 构建）。

**Release notes**

```
en: Loading a Scud Race save no longer hangs the emulator, and quad-cabinet games now fold
    properly on a stereo output instead of leaving the rear half quiet. Saves written by this
    build also resume the background music at the exact position it was saved at.
zh: 修复 Scud Race 读档卡死；四声道机台游戏在二声道设备上不再丢后声道（自动折叠）；
    新版本写入的存档可以从原位置继续播放背景音乐。
```

### 界面：「Vulkan 校验层」补上说明

- 「常用」页的「Vulkan 校验层」复选框后面新增 `(?)`：说明它是 Vulkan SDK 的检查器，会把发现的
  问题写进 `Supermodel.log`，正常游玩请关闭（会明显变慢），排查花屏/闪烁/崩溃时临时打开。
- `usermanual.md` 第 9 节与 `usermanual_chs.md` 第 7 节补充通俗说明：使用步骤（打开 → 复现 →
  退出 → 发日志）、日志行前缀、1000 条上限、没装 Vulkan SDK 时的回退，以及对之后新开的游戏
  生效这一限制；命令行章节注明 `-vk-validation` 是同一个开关。

**Release notes**

```
en: The "Vulkan validation layers" option now has a (?) tooltip, and the manual explains what it is,
    when to turn it on and where its output goes.
zh: 「Vulkan 校验层」选项新增 (?) 提示，手册补充通俗说明（作用、什么时候开、日志在哪）。
```

### 界面：Ko-fi 徽章与底部按钮同排

- 设置窗口的 Ko-fi 徽章从 footer 的状态行移到按钮行右侧，和「启动游戏 … 退出」处在同一条
  基线上；窄窗口放不下时仍回退到状态行，只有回退路径才需要裁剪状态文字。footer 的总高度不变。
- 顺带确认「非宽屏时两侧是蓝色」不是清屏或合成的问题：`WideBackground=1` 会把游戏的 2D
  背景层横向拉满（Daytona 2 的底色是蓝色），它和 `WideScreen` 是两个独立开关。要黑边就在
  画面页关掉「宽屏背景」。A/B 截图与命令见 `sdl3_reports/2026-10-27-widebg-blue-bars/`。
- 验证：`cmake --build build-cmake --target Model3-Vulkan`；`Capture-Ui.ps1` 截图见
  `sdl3_reports/2026-10-27-kofi-badge-row/`。

**Release notes**

```
en: The Ko-fi badge in the settings window now sits on the action-button row instead of the status
    line. The blue bars beside a non-widescreen picture come from the "wide background" option
    stretching the game's 2D layer; turn it off for black bars.
zh: 设置窗口的 Ko-fi 徽章改为与底部按钮同排；非宽屏画面两侧的蓝边来自「宽屏背景」把游戏 2D
    背景层拉满，关掉它即可恢复黑边。
```

### 控制台横幅：Model 3-Vulkan v0.9.8.3a(formerly Supermodel-VK)

- Vulkan 构建的 `Title()` 单行横幅改为
  `Model 3-Vulkan v0.9.8.3a(formerly Supermodel-VK) - Igloo-Garage`。版本号用
  `SUPERMODEL_FORK_VERSION` 拼接，下次发版时横幅和窗口标题不会各说各的。
- LegacyOG 的三行横幅不变；`README.md`、`AGENTS.md`、`INDEX.md` 与发行版 README 同步。

**Release notes**

```
en: The Vulkan console banner now reads "Model 3-Vulkan v0.9.8.3a(formerly Supermodel-VK) -
    Igloo-Garage".
zh: Vulkan 版控制台横幅改为「Model 3-Vulkan v0.9.8.3a(formerly Supermodel-VK) - Igloo-Garage」。
```

### 设置界面：Ko-fi 支持徽章、便携 ROM 目录、HostCore 跨平台

**Ko-fi 徽章**

- 设置窗口 footer 新增 Ko-fi 支持徽章（最初在状态行右侧，后移到按钮行，见本节上方的
  「界面：Ko-fi 徽章与底部按钮同排」），点击打开
  `https://ko-fi.com/igloogarage`。徽章是自绘的（杯子 + 蒸汽 + “Ko-fi”），不依赖图片资源，
  随界面缩放；悬停有手形光标和本地化提示。
- 新增 `OSD::Platform::OpenURL()`，SDL2/SDL3 后端都走 `SDL_OpenURL`，Windows/Linux/macOS
  不需要各自的分支。
- footer 的状态行高度从一行文字扩展到 30×scale 以容纳徽章；“找到 N 个 ROM”和临时提示
  会按徽章左边界裁剪，不会压到徽章下面。

**便携 ROM 目录**

- 启动时扫描 exe 旁的 `rom` / `roms` / `romset` / `romsets`（大小写不敏感），优先选择
  真正含 `.zip` 的目录。仅当配置的路径不可用、为空，或仍是旧默认 `C:/Downloads` 时才采用，
  用户手动选过的有效路径不会被覆盖。
- 选中后把相对路径（如 `roms/`）写回配置；`ResolveRomPath()` 先按 exe 目录解析，再走
  平台历史规则，所以移动整个安装目录后设置仍然有效。
- Linux 的 `FileSystemPath::GetBasePath()` 改用 `/proc/self/exe` 得到安装目录；默认 ROM 路径
  改为 exe 旁的 `roms/`（首次运行创建）。Windows 保留 `C:/Downloads` 作为无便携目录时的
  默认。
- `build.bat` 会在发行目录创建 `roms/README.txt`，并在重建时保留 `dist\rom*` 文件夹；
  `-package` 用 `/XF "*.zip"` 排除 ROM，避免把用户的游戏数据打进发布包。

**HostCore 跨平台**

- 加载名按平台区分：Windows `Model3VulkanHostCore.dll`、Linux
  `libModel3VulkanHostCore.so`、macOS `libModel3VulkanHostCore.dylib`；
  `HostCoreBridge.cpp` 统一用 `ModuleFileName()`，错误消息和 CMake 部署路径同步。
- CMake 新增 `hostcore` 目标：在目标系统执行 `dotnet publish -r <rid>` 并复制到
  `build-cmake/bin`。Linux/macOS 的 `.zst` 存档因此可用；Native AOT 不能从 Windows
  交叉编译到 Linux/macOS，必须在目标系统上发布。
- `Scripts/install-linux-deps.sh` 增加 `libvulkan1`、`mesa-vulkan-drivers` 和
  `fonts-wqy-microhei`，并注明 host core 需要 clang 与 .NET 11 SDK。

**验证**

- `build.bat` 全量构建通过；`Capture-Ui.ps1` 截图确认徽章在右下角，杯子与 “Ko-fi” 清晰，
  位置不压状态文字。
- 便携目录：把配置临时改为 `ROMPath = C:/Downloads` 并放入一个 `roms/test.zip`，日志出现
  `GUI: using the portable ROM folder 'roms/' instead of 'C:/Downloads'.`，退出后配置为
  `ROMPath = roms/`；随后恢复原配置并删除测试 zip。
- `cmake --build build-cmake --target hostcore` 在 Windows 发布并复制
  `Model3VulkanHostCore.dll`；`HostCore/test_abi.exe` 仍为 `ALL ABI TESTS PASSED`。
- WSL Ubuntu 用 g++ 编译并运行了 `Unix/FileSystemPath` 的测试程序：`GetBasePath()` 返回
  测试二进制所在目录，默认 ROM 路径为 `roms/`。完整的 Linux GUI/Vulkan 构建与 HostCore
  `.so` 未在本机构建（WSL 缺 SDL3/CMake/.NET）；Windows 上 `hostcore` 目标已实测。
  字体崩溃修复沿用既有实现（`ProbeFontFile()` + `fc-match -s` 排序列表）。

**Release notes**

```
en: The settings window now has a Ko-fi badge in the bottom-right corner, and a roms/ folder next
    to the executable is found automatically - no need to re-select the ROM path after an update.
    Linux/macOS can publish the .zst host core with the new CMake `hostcore` target.
zh: 设置窗口右下角新增 Ko-fi 支持徽章；exe 旁的 roms/ 等目录会被自动识别，更新后不用重选 ROM
    路径。Linux/macOS 可用新的 CMake `hostcore` 目标生成 .zst 存档所需的模块。
```

### 产品改名：Model 3-Vulkan v0.9.8.3a（exe / DLL / 配置 / 图标 / 启动图）

- Vulkan 主程序由 `Supermodel-VK-SDL3.exe` 改名为 **`Model3-Vulkan.exe`**；窗口标题与关于行
  使用 **`Model 3-Vulkan v0.9.8.3a`**（`SUPERMODEL_TITLE` / `SUPERMODEL_FORK_VERSION`）。
  `Supermodel-LegacyOG.exe` 名称不变。
- 冷路径模块 `SupermodelHostCore.dll` → **`Model3VulkanHostCore.dll`**；工程文件改为
  `HostCore/Model3VulkanHostCore.csproj`，C# 命名空间同步为 `Model3VulkanHostCore`。
  加载路径、错误消息、`HostCore_Version` 字符串与构建脚本已对齐。
- Vulkan 配置由 exe 名自动派生，因此变为 **`Data/Config/Model3-Vulkan.ini`**。首次运行会
  优先从旧 fork 的 `Supermodel-VK-SDL3.ini` 复制，其次 `Supermodel.ini`；实测迁移后两份
  文件哈希一致，用户设置（分辨率/MSAA/AlphaSortMode 等）未丢。
- 控制台横幅缩成一行，最终文本为
  `Model 3-Vulkan v0.9.8.3a(formerly Supermodel-VK) - Igloo-Garage`（详见本节上方的
  「控制台横幅」条目）；旧的 “a Sega Model 3 arcade emulator / Based on Supermodel ... /
  Copyright ...” 三行只在 LegacyOG 构建里保留。`-help` 的 Usage 行在 Vulkan 构建里显示
  `Model3-Vulkan`。
- 图标改为直接使用根目录的新 `output.ico`（CMake / MSVC / MinGW 三条资源路径），删除旧的
  `Src/OSD/Windows/Supermodel.ico`；启动图使用根目录新的 `Igloo-Garage Presents.webp`，
  `SplashResource.h` 的 SHA-256 已由 `Scripts/UpdateSplashSignature.ps1` 刷新。
- `build.bat` / `build-cmake.bat` / CMake 目标 / 测试脚本 / 手册 / README 同步新名字；
  安装目录仍保留 `dist/Supermodel-VK`，发布包改为
  `build-package/Model3-Vulkan.v<版本>.for.Windows.zip`。

**验证**

- `build.bat` 全量构建通过：`dist/Supermodel-VK/{Model3-Vulkan.exe, Model3VulkanHostCore.dll}`。
- `Model3-Vulkan.exe -print-games`：横幅为指定文本，日志前缀 `Model3-Vulkan:`，
  `host core: Model3VulkanHostCore 1.0 (Native AOT)`；首次运行从
  `Supermodel-VK-SDL3.ini` 迁移到 `Model3-Vulkan.ini`，两份文件 SHA-256 相同。
- 从 exe 资源中读回启动图：303166 字节，SHA-256 与 `Igloo-Garage Presents.webp` 一致；
  32×32 图标像素与 `output.ico` 逐字节一致。

**Release notes**

```
en: The Vulkan build is now Model 3-Vulkan v0.9.8.3a (Model3-Vulkan.exe). The host module is
    Model3VulkanHostCore.dll, settings live in Model3-Vulkan.ini and are migrated automatically,
    and the console banner is now a single Igloo-Garage line. The splash artwork and application
    icon were replaced.
zh: Vulkan 版更名为 Model 3-Vulkan v0.9.8.3a（Model3-Vulkan.exe），冷路径模块改为
    Model3VulkanHostCore.dll，配置迁移到 Model3-Vulkan.ini（自动从旧名复制），控制台横幅
    改为单行 Igloo-Garage 文本。启动图与主程序图标已更换。
```

### 排查：Daytona 2 瀑布蓝块 —— 棋盘格半透明定位为试验模式

**一、现象**

加载 `daytona2.zst2`（瀑布就在眼前）后，瀑布整片变成蓝白色噪块，随镜头移动闪动。
用户报告“以前修过怎么又出现了”，且无论是否加载 HD 纹理包都存在。

**二、定位**

同一存档、同一帧（`-Frame 190`）只改半透明排序，其余配置相同：

| 半透明排序 | MSAA | 瀑布区蓝色像素占比 |
| --- | --- | --- |
| 原版（双层）`AlphaSortMode=0` | 4x | 4.87% |
| 逐像素排序 `AlphaSortMode=1` | 4x | **3.92%** |
| 街机真机（棋盘格）`AlphaSortMode=2` | 4x | **20.13%** |
| 街机真机（棋盘格）`AlphaSortMode=2` | 1x | 12.09% |

四组都是 `InternalResolution=0`。改动前后的二进制在模式 0 下逐像素相同
（max 0 / mean 0），重新编译前后 `composite_frag.spv.h` 与 Supermodel-VK 树逐字节一致，
所以与本次「低分辨率渲染」改动、构建和 HD 纹理包都无关。蓝块是**今天同步进来的棋盘格
模式**的固有行为；当年修好的是默认双层路径，棋盘格从没跑过那条规则。

**三、根因**

从着色器里临时输出覆盖值（已移除）测得，瀑布一带 20.2% 的像素**只有 trans1 一层**
（平均 alpha 0.65，trans2 为空），27.2% 两层都在（0.66 / 0.54），48.4% 无半透明。

棋盘格每层只写一半像素，合成后 alpha 天然是原版的一半：单层那部分只能是半强度；两层都在
时颜色是两层各半混出来的（白色水幕 + 蓝色水面 → 蓝白）。原版靠「两层都在 → 不透明」把
交叉淡化还原成不透明的白瀑布，这个信息按像素拆层后已经不存在了。

试过两条修正，都没有保留：

- 两层合计覆盖 ≥ 1.9 时判不透明，颜色按覆盖率加权：蓝 20.13% → **7.34%**，但单层那 20% 仍是
  半透，瀑布主体变薄、后面岩石透出来。
- 单层覆盖 ≥ 1.8 时判不透明：蓝 20.13% → **18.08%**，更糟——该处占主导的一层本身偏蓝。

**四、处理**

- 模式 2 在界面文案、手册和 `devdocs/AlphaSortMode2.md` 里标记为**试验模式**，只推荐用于
  雪雾/尘土这类密集粒子；有 LOD 交叉淡化或大片半透明水幕的游戏用原版或 OIT。
- 数据、截图（模式 0/1/2、MSAA 1x/4x、两次尝试、覆盖调试图）和复现命令写入
  `sdl3_reports/2026-10-27-checkerboard-hd/README.md`。
- 若要真正还原，需要保留逐像素的两层覆盖信息（例如额外一张 r8 标记“互补对”），
  入口记在 `issues_waitlist.md`。
- 着色器与二进制已还原到改动前状态，未保留任何修复尝试。

**Release notes**

```
en: Transparency sorting "Arcade hardware (checkerboard)" is now marked experimental.
    It cannot reproduce the two-layer composite for LOD cross-fades, so Daytona 2's
    waterfall turns into a flashing blue block there (20.1% of the fall's pixels versus
    4.9% on the original path). Use Original or Per-pixel sorted (OIT) for those games;
    the checkerboard remains the option of choice for dense snow and dust.
zh: 半透明排序的「街机真机（棋盘格）」标记为试验模式。它无法还原 LOD 交叉淡化的双层合成，
    Daytona 2 的瀑布在该模式下会变成闪动的蓝块（瀑布区蓝色像素 20.1%，原版 4.9%）。
    这类场景请用「原版（双层）」或「逐像素排序（OIT）」；雪雾/尘土仍然用棋盘格。
```

### 同步：半透明第三档「街机真机（棋盘格）」（`AlphaSortMode = 2`）

从 Supermodel-VK 同步过来，实现与取舍见 `devdocs/AlphaSortMode2.md`，验证记录见
`sdl3_reports/2026-10-27-checkerboard-hd/`。

- 新增第三项「街机真机（棋盘格）」/ `Arcade hardware (checkerboard)`；默认仍为原版。
- 两层半透明各自只写棋盘格的一半像素（`trans1` 偶格、`trans2` 奇格），片元在 `ShadeFragment()`
  之前就丢弃；合成阶段按 2×2 组做盒式还原，因此下雪/扬雪这类密集效果只有半强度，
  而不是两层一重叠就变实心。
- 与 HD 纹理功能无交叉：掩码与还原路径都不读 set 2（`u_hdTextures`），"set 1 总是存在"的
  差异也不受影响。
- 同步了帧号截图钩子 `SM3D_SCREENSHOT_FRAME`（`Capture-Frame.ps1 -Frame <n>`）与
  `Validate-Vulkan.ps1` 的 `-Rom` / `-Frame` 透传。
- 新增 `devdocs/AlphaSortMode2.md`。

### 同步：超低配预设与「点对点」说明、翻译审计脚本

- **「整数倍缩放」才是低分辨率的来源**：硬件预设下方在「496×384 + 未勾选点对点」时显示琥珀色
  说明；`IntegerScale` 的 (?) 与预设 tooltip 改成跟 Supermodel-VK 一致——取消点对点后全屏按面板
  分辨率渲染（1080p 约 1395×1080），核显帧率明显下降。手册 §4.2/§4.4 与中文版同步，`AGENTS.md`
  也补上这条约定。
- **修复硬件预设误判**：`tierAlphaSort` 的钳位从 `0..1` 改为 `0..2`。之前选中「街机真机（棋盘格）」
  时会被当成 `1`，预设下拉框可能错误地显示成中端/高端档位。
- **`Scripts/AuditTranslations.ps1`** 换成支持「相邻字符串字面量拼成一个 key」的版本，避免把多段
  `Tr()` 文案误报为缺失翻译。
- `devdocs/VULKAN-ONLY-FEATURES.md` 的半透明说明由两条路径更新为三条（含棋盘格）。
- 在 `AGENTS.md` 记录 HD 纹理哈希的精确常量（FNV-1a 64 位，offset basis 非标准值），
  避免以后误当笔误“修正”而让所有已发布的替换包失配。

### 修复：`build-cmake.bat` 因缺 `Supermodel3Bezel.png` 而失败

上一版裁剪树时把根目录的机柜背景图弄丢了，`supermodel_runtime_data` 目标直接报
`Error copying file`，整个 Ninja 构建停在那一步。从 `dist/Supermodel-VK/Data/Assets/`
恢复了一份到根目录（内容一致）。

### 工具：测试脚本的临时 ini 覆盖带崩溃恢复

`Tools/Capture-Frame.ps1` 与 `Tools/Measure-FrameRate.ps1` 的 `-Ini` 会就地改写
`Data/Config/Supermodel-VK-SDL3.ini`。之前只有正常退出和 Ctrl-C 能还原；
`Stop-Process`、关终端或调试器强停会留下测试值。最明显的后果是 `VulkanDevice`
停在测试用的显卡上，下次打开设置界面时跨显卡渲染，表现为整窗马赛克或闪动——
看起来像渲染 bug，实际是配置残留。

- 新增 `Tools/TestIniGuard.ps1`：还原前校验“当前文件是否仍是脚本写的那一版”，
  是才覆盖；之后被编辑过的文件保留，并把测试前副本存成 `<ini>.testoriginal`。
- 覆盖期间在 ini 旁放 `<ini>.testbak`（原始字节副本）与 `<ini>.testrun`（补丁文件
  SHA-256）。两个测试脚本启动时先调用还原，所以被硬杀的现场会在下一次运行时自动修复；
  也可以手动运行 `Tools/Restore-TestIni.ps1`。
- 补丁与还原都移进 `try/finally`，中断时先关闭会话再还原 ini，避免模拟器退出时
  把测试值写回文件。`-KeepRunning` 不关闭会话，但配置文件照常还原。
- `Data/Supermodel.log` 被上一个会话占用时，脚本直接报“先关闭模拟器再试”，
  不再抛裸的 `Remove-Item` 错误。
- 自测：`Tools/__test_guard.ps1`（补丁/还原/空标记/编辑冲突/坏 token）、
  `Tools/__test_restore.ps1`（手动还原脚本）。

**Release notes**

```
en: Transparency sorting now has a third option, "Arcade hardware (checkerboard)":
    each layer writes half of the pixels, so dense snow/dust no longer turns solid
    and hides the car behind it. The ultra-low preset now explains that Integer
    scaling is what keeps its low resolution alive, and the hardware-preset combo
    no longer misreports a tier when checkerboard is selected.
zh: 半透明排序新增第三档「街机真机（棋盘格）」：两层各写一半像素，雪雾/尘土不会
    再把后面的车糊白。超低配预设补充说明「低分辨率靠整数倍缩放」，并修掉了选中
    棋盘格时硬件预设误判档位的问题。
```

## 2026-10-26

### 新增：HD 纹理自动分类工具（`Tools/classifytextures.py`）

把 `Data/Assets/DumpTextures/<set>/` 里导出的子纹理自动分成两组，方便分别用不同
超分模型跑：

- **`surface`**（赛道地面/远景山体/建筑表面，走 `4xTextureDAT2_otf`）
- **`detail`**（路标/车牌/赞助商 Logo/仪表盘数字，走 `4x_UltraSharp` / `4x-Remacri`）

分类依据（纯本地，只用 Pillow + numpy，无模型/无网络）：

- 页 1 是 Model 3 的 2D/HUD 页（数字条、Logo、标志、精灵），直接进 `detail`。
- `fmt` 非 0/7 的亮度/alpha 格式（1–6、8–11）进 `detail`。
- alpha 精灵/镂空进 `detail`；大面积低对比 alpha（云/雾）归 `surface`。
- 页 0 不透明贴图按内容打分：主色背景、平色、边缘、短笔画/文字、连通域形状、
  平铺性（半程平移自相似）综合判定；窗户/砖墙/木纹等矩形平铺件被压低。

输出：`<out>/surface/`、`<out>/detail/`（默认拷贝，`--move` 改为移动）、
`texture-classification.csv`（含全部特征和判定原因）、`_review/` 缩略图
（`boundary.jpg` 是阈值附近的样本，`surface`/`detail` 两组的末页是各自最接近
边界的样本）。改完 CSV 的 `group` 列后用 `--reuse` 重新应用。

默认阈值 4.5，可用 `--threshold` 调整；因分类器无法自动验证，边界样本需要人工
复核后再定阈值。

## 2026-10-25

### 高清纹理导出与替换（HD Texture Pack）

- **粒度是子纹理，不是整张贴图页**。Real3D 把 8 种 TFM 格式和折叠的 mip 金字塔塞进
  两张 2048×1024 的页里，游戏还会用 `UploadTextures` 局部覆写，直接导出页面没有可用的
  替换语义。现在在 `BuildDrawList()` 里按 `(page, texX, texY, w, h, format)` 取 Base Level 0，
  用页内 wrap 掩码读源 texel，连同 `format/w/h` 一起做 FNV-1a 64 位哈希。
- **哈希缓存带逐 tile 版本号**。`CVulkanTextureCache` 新增 `m_tileRevision[2]`，
  `UploadTextures()` 每标脏一个 32×32 tile 就把它的计数 +1；哈希缓存只在该矩形覆盖的
  tile 版本变化时重算。整页上传（ROM 初始数据）会把全部 tile 计数 +1。动画贴图不会
  连累同页的静态矩形，只有真正重叠写入的矩形会重新哈希。
- **导出（`DumpHDTextures=1`）**：把子纹理解码成直通 RGBA8 PNG，写到
  `Data/Assets/DumpTextures/<set>/tex_<page>_<x>_<y>_<w>x<h>_fmt<format>_<16位哈希>.png`。
  - 解码复用 `real3d_common.glsl` 的 `ExtractColour()` 逐位实现（`Src/Graphics/Vulkan3D/TextureDecode.h`），
    T1RGB5 透明位、A4L4、RGBA4、L8/L4 的 luma 透明色都与着色器一致。
  - 源 texel 在调用线程先快照（带 wrap 掩码），PNG 编码和写盘交给后台线程；否则首次
    出现大量贴图时会在仿真拍上卡顿。
  - `m_dumpedHashes` 去重，同一内容只写一次；单局上限 50000 个文件。
  - **注意键名**：ini 里 `DumpTextures` 早已表示“退出时导出原始 16-bit 贴图页”
    （`Real3D.cpp` + `-dump-textures`），新功能用 `DumpHDTextures`，两者互不影响。
- **加载（`LoadHDTextures=1`）**：扫描 `Data/Assets/HDTextures/<set>/`（递归），
  从文件名里最后一个 16 位十六进制 token 取哈希；同一哈希同时有 PNG/WebP 时优先 PNG。
  - 支持 `.png` 与 `.webp`：Windows 用 `Src/Pkgs/webp` 的 vendored 解码器，Linux 用系统
    libwebp；PNG 走 `Util::PNG::DecodeRGBA()`。分辨率任意（实测 2× 与 512×661），
    alpha 为完整 8 位，不再受 T1RGB5 1-bit 透明限制。
  - 解码在 loader 线程；主线程在 `RecordSync()` 里建 `VK_FORMAT_R8G8B8A8_UNORM` 图像、
    用 ring buffer 分块上传 mip 0，再用 `vkCmdBlitImage` 生成完整 mip 链。
  - 采样器 `maxAnisotropy=16`（设备支持时），地址模式按 `wrapMode`：0/1=REPEAT、
    2/3=MIRRORED_REPEAT，uv 原样交给采样器（早期版本把 1 做成
    `fract(uv)` + CLAMP_TO_EDGE，会在每个平铺边界留一条横幅，见下面「HD 平铺边界」一节）。
  - **bindless 数组单独放 set 2**：`VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` 不能与
    `UNIFORM_BUFFER_DYNAMIC`/`STORAGE_BUFFER_DYNAMIC` 同布局（VUID 03001），而 set 0 的
    视口/材质/顶点切片都是动态偏移。现在 set 0 = 原材质/贴图页，set 1 = 逐像素排序，
    set 2 = `sampler2D u_hdTextures[]`（`PARTIALLY_BOUND | UPDATE_AFTER_BIND`）。
    为了让 set 号固定，`CreateOitSetLayout()` 现在无条件执行，即使 `AlphaSortMode=0`。
  - 槽位在“文件已解码、图像已上传”之后才 `Resolve()` 出非负索引；在此之前材质继续采样
    原页，所以加载大包时不会出现白块或未初始化纹理。上限 2048 槽 / 3 GB，超出会记日志。
- **着色器分支（`real3d_common.glsl`）**：`R3DMaterial.pad0` 改成 `hdTexIndex`（-1 = 原路径），
  非负时直接 `textureGrad(u_hdTextures[nonuniformEXT(hdTexIndex)], uv, dFdxFine(uv), dFdyFine(uv))`，
  完全跳过 `texelFetch`、页内 mip 偏移和手工双线性；导数用四边形求解得到的精确 uv，
  4×/8× 贴图在斜掠面上也能选对 mip。微纹理（`kMicro`）在该分支里跳过，避免低清网格
  破坏高清细节。`kInverted`、`kAlphaTest`、`kTextureAlpha` 两遍规则与 `TexFilter` 之外
  的行为保持原样；替换贴图不参与 `TexFilter` 的径向/各向异性估算，由采样器完成过滤。
- **界面**：`核心` 页底部与游戏内 Tab 面板新增「高清纹理」一节：
  `LoadHDTextures`、`DumpHDTextures` 两个复选框，面板里另有「重新加载高清纹理」按钮和
  `F4` 提示。两个开关每仿真帧重新读取，游戏内直接生效；重新加载会 `vkDeviceWaitIdle`、
  清槽、重扫目录。面板打开时模拟器是暂停的，所以按钮的扫描在关闭面板恢复后完成。
- **打包**：`build.bat` 把 `Data/Assets/HDTextures` 与 `DumpTextures` 列入“保留用户数据”
  集合（不会在重建 `dist` 时被删），`-package` 的发行 zip 则排除这两个目录。
- **验证**：Daytona 2 存档（`daytona2.zst1`）导出 279 张子纹理，全部为可见的 RGBA8 图像；
  把全部替换图做成 2× 纯绿 + 90/255 白色块后重新载入，截图整体变绿、HUD 与半透明区域
  都正确，日志 `first replacement is sampleable (64x64, 7 mip(s))`；另用 512×661 WebP
  验证 `10 mip(s)` 与 WebP 解码。默认半透明路径 `Tools/Validate-Vulkan.ps1 -Mode sync`：
  0 VUID、0 hazard；`-Mode gpu` 下没有 HD 相关报告。`AlphaSortMode=1` 下会报既有的
  `uLayers` 2D 合成布局 VUID，**关掉 HD 也同样出现**，属于另一条路径的旧问题。
- 已知限制：替换贴图按 hash 识别，改图后必须按 `F4`（或面板按钮）重扫；`LoadHDTextures`
  关闭时不会为 HD 计算哈希，因此也没有额外开销；实例/相机插值、LOS、OIT 路径与 HD 分支互不影响。

**Release notes**

```
HD texture packs: dump the sub-textures a game actually draws as RGBA8 PNGs
(Data/Assets/DumpTextures/<set>/) and load replacements from
Data/Assets/HDTextures/<set>/ at any scale, with smooth 8-bit alpha.
PNG and WebP, hash-matched by content, hardware mip chains and 16x
anisotropic filtering, and F4 / the in-game panel reloads the folder without
restarting. Two new settings on the Core tab and in the Tab panel:
Load HD texture pack and Dump this game's textures (DumpHDTextures).
```

### HD 平铺边界在斜掠面上变成等距横条（wrap 模式 1 的滤波缝）

- **症状**：Daytona 2 关掉 HD 时路面是干净的灰色；开启 4× HD 包后，赛道里那条顺光
  照方向铺开的路面（材质矩形 `(0,768) 256x256`，即
  `tex_0_0_768_256x256_fmt0_d295d515a7b6bfb5.png`）出现整条等距横向细条，远处更密。
  旁边的路面、右侧白墙、看台都正常。
- **根因不在贴图，也不在超分**：材质 `wrapMode=1`，HD 路径用 `fract(uv)` 配
  `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` 来复刻真机的“先 repeat、再在子贴图边缘钳位”。
  但对*过滤器*来说，钳位意味着滤波足迹里越过边界的每一根 tap 都读边缘纹素 ——
  缝不是半个纹素宽，而是**整个足迹宽**。斜掠路面上一块纹素在屏幕上被拉成几十像素，
  于是每个平铺重复周期出现一条横带。真机原始路径用 `& mask` 循环取坐标，是无缝的。
- **实测定位**：涂纯色时条纹消失（band-std 10.33 → 0.44），说明缝两边同色就看不见，
  内容本身无罪；贴图行/列高通 std 只有 0.69 / 16.6（源 0.51 / 16.4），4× 版与源图
  统计几乎一致。改 wrap 后 band-std 回到 1.53（HD 关闭参考值 0.97），画面保留 HD 细节。
- **修复**：`AddressModeForWrap()` 把 wrap 0/1 都映射到 REPEAT、2/3 到
  MIRRORED_REPEAT（不再用 CLAMP/MIRROR_CLAMP），`GetTextureValue()` 里删掉 `fract()`，
  uv 原样传给 `textureGrad`。替换图本身就是子贴图矩形，“边缘钳位”已经没有要保护的东西。
- **验证**：`MSAA=4`、`TexFilter` 0 与 2、`AlphaSortMode` 0 与 1、`QuadInterpolation` 1
  都用同一存档帧抓图对比；`TexFilter` 与 `AlphaSortMode` 对条纹**完全无影响**，
  HD 开/关是唯一变量。证据图见 `sdl3_reports/2026-10-25-hd-wrap-seam/`。
- 附带观察：修好后路面能看到 AI 超分带来的沥青颗粒，斜掠角度下比原机粗，属超分风格，
  不是渲染缺陷；嫌颗粒重就换温和的超分模型，或对这张图单独降锐。

### 界面重做：双重模糊毛玻璃、暖色护眼浅色/深色主题、连续圆角

- **双重 Kawase 毛玻璃（新文件 `Src/Graphics/Vulkan3D/UiBlur.{h,cpp}`）**：面板背景改成
  当前画面的模糊拷贝，不再是不透明底色。链路四遍，全部 ≤1/2 分辨率：
  1. `vkCmdBlitImage`（线性）把 swapchain 抓进 `level0`（1/2）。抓取用 blit 而不是采样，
     采样需要一张随 acquired image 变化的描述符；blit 在恰好 0.5 倍时就是 2×2 盒式滤波，
     比 5-tap 更适合做首级。
  2. `ui_blur_down.frag`：5-tap 对角盒式（Kawase），`level0` → `level1`（1/4）。
  3. `ui_blur_up.frag`：8-tap 帐篷，`level1` → `level0`，**混合权重 0.5**（`SRC_ALPHA`），
     而不是 bloom 那套 `ONE/ONE`。相加会让平坦区变成 1.5 倍亮，而这块图是要被
     tint 覆盖而不是当光晕用的。
  4. `ui_frost.frag`：再一遍 8-tap + 色彩调整，写入 `m_uiBlurTexture`（ImGui 采样它）。
  - 所有链上图像都用 **swapchain 自己的格式**。不只是颜色问题：带线性滤波的缩放 blit
    只允许在 format-compatible 之间，BGRA8 与 RGBA8 不兼容。
  - 重建时旧图**延迟 4 帧销毁**（`RetiredSet`）。ImGui 已经注册的描述符仍指向旧 view，
     resize 那一帧的绘制命令已经烘焙好了；直接销毁等于采样已释放内存。
- **毛玻璃着色器**：
  - *Vibrancy*：Rec.709 亮度为轴做 1.16 倍饱和度提升。
  - *浅色主题提亮*：`mix(rgb, vec3(0.96,0.94,0.91), 0.55)` 后再回补 1.30 倍饱和度。
    浅色面板下面若沿用深色的压暗逻辑会发灰发脏；向暖白提亮才是浅色玻璃的对应操作，
    提亮后不回补饱和度会让封面变成灰调。
  - *深色主题*：保留膝点以上的软高光压缩（有理曲线，不用 clamp —— clamp 会把膝点以上
    全部压成同一个颜色，在深色面板上就是一块苍白污迹）。
  - *微噪点*：`1.5/255` 的 interleaved gradient noise（屏幕空间，不逐帧跳动）。
    它同时消除 8-bit 断层并给磨砂面一点颗粒触感。
- **ImGui 桥接**：`UiFrost::Plate()`（`Gui.cpp`）用
  `AddImageRounded(tex, pMin, pMax, pMin/displaySize, pMax/displaySize, …)` 把**屏幕绝对
  坐标直接映射成 uv**，因此一张半分辨率纹理服务所有面板；上方叠主题 tint，再画 1px 高光边
  （四周 6% 白 + 顶边 14% 白；浅色主题是四周 10% 暖灰 + 顶边 65% 白）。Plate 内部自己
  `PushClipRect`，否则窗口内容区已收窄到 padding 内的裁剪矩形会把圆角和高光切掉。
- **零开销**：`CVulkan2D::SetUiFrostGate()` 每帧询问一次“这一帧有人要采样毛玻璃吗”，
  否则整条链一条命令都不录。日志在状态翻转时打
  `Vulkan2D: frosted backdrop active (744x576, half of the 1488x1152 frame)` /
  `… idle; nothing is sampling it`，实测关面板时不出现 active 行。
- **启动器氛围底图**：设置窗口不再清成纯色，而是把所选游戏的封面按 cover 裁切铺满、
  降饱和、向主题色洗淡并加暗角（Vulkan 走新管线 `UiVulkanBackend::DrawAmbientPass()` +
  `ui_ambient.frag`；OpenGL 用背景 draw list 的 Image+RectFilled 近似）。跟随**选中项**而不是
  悬停项 —— 悬停会让壁纸每划过一个游戏就变一次，每次都要重算整条模糊链。
  - 布局因此重排成三块玻璃：左栏（ROM + 预览 + 列表）、右栏（设置页）、底部状态条。
    根窗口 `SetNextWindowBgAlpha(0)`，玻璃块之间和窗口四周留出的缝就是底图。
  - 每块玻璃**内缩留白**（`panePad = 16×uiScale`），子窗口改成
    `AlwaysUseWindowPadding + NoBackground`，所以输入框、表格、页签、按钮都不再贴着圆角边
    （这正是上一版看着“挤”的原因：玻璃矩形等于内容矩形，内容从边框上开始）。
  - 游戏内面板单独把 `WindowPadding` 提到 `(26,22)×scale`（共享的 18px 是给整窗设置页调的，
    浮在画面上小面板用同一个值只有一半的观感）；`OverlayBody` 改成 `NoBackground`，否则子窗口
    默认的底色矩形会在 padding 环里再画一块，眼睛会把它的边当面板边框。存档表格再额外
    `Indent(5×scale)` —— 它的斑马条纹是一块可见面，槽位数字是拿它量间距的。
  - 控件（`FrameBorderSize`）从 0 改成 1：浅色主题下面板底色和 `FrameBg` 只差几个百分点，
    不给一道 10% 的内聚线的话，存档表里的 Save/Load 几乎看不出是按钮。
- **主题**：`UITheme`（0 = 浅色暖色护眼，**默认**；1 = 深色；2 = 原始界面），`ApplyUiTheme()`
  走一张 `UiPalette` 表。
  - 浅色：面板 `rgba(244,240,232,0.78)`、子容器 `rgba(236,231,221,0.65)`、
    卡片 `rgba(226,220,209,0.70)` / 悬停 `rgba(216,209,196,0.88)` / 激活
    `rgba(204,196,182,0.95)`、主文字 `#262522`、次要 `#6E6A63`、强调 `#C86D3B`、
    边框 `rgba(60,55,45,0.10)`。
  - 深色：面板 `rgba(26,28,32,0.72)`、卡片 `rgba(38,41,48,0.55)` / 悬停
    `rgba(52,56,66,0.70)`、主文字 `#EAE6DF`、次要 `#9A968E`、强调 `#E09F5A`。
  - 两套都没有纯黑与纯白。切换立即生效（`GUI()` 里检测 `GetUiTheme()` 变化后
    `RescaleUi()`，它先 `ApplyUiTheme()` 再把尺寸乘回 DPI 比例）；游戏内面板在下一局生效，
    因为浮层有自己的 ImGui 上下文。macOS 顺便把 `NSApp` 外观跟着切（`ApplyDarkAppearance()`）。
  - **`2 = 原始界面`**：毛玻璃重做之前的那一套，逐值复制（纸底 `#F6F4EF`、子容器 `#FBFAF6`、
    蓝色强调 `#1A63C7`、`WindowPadding (20,18)` / `FramePadding (12,7)` / `ItemSpacing (12,9)`、
    圆角 14/12/8/12/8/9、`ChildBorderSize 1`、`FrameBorderSize 0`）。不是“新旧折中”，就是原来
    那一版：不透明白底、无氛围底图、三块玻璃与 `panePad` 全部跳过、高光边透明。喜欢旧外观的人
    随时可以切回去，不需要装旧版本。这个主题下「毛玻璃背景」开关置灰（它本来就不透明），
    用 `ImGuiHoveredFlags_AllowWhenDisabled` 保留 tooltip 说明。
- **圆角与抗锯齿**：`WindowRounding 14` / `ChildRounding 10` / `FrameRounding 7` /
  `GrabRounding 6` / `PopupRounding 10`（DPI 缩放后），`CircleTessellationMaxError 0.10`
  （默认 0.30 在 14px 半径上能看出折角），三个 `AntiAliased*` 显式置 true。
  `ChildBorderSize` 改为 0：边由 Plate 的高光边画，ImGui 再画一圈会变成两重线。
- **微动效**：
  - 面板打开时整体（含玻璃）120ms smoothstep 淡入（`PanelFadeAlpha()`，`ImGuiStyleVar_Alpha`
    管文字与控件，Plate 单独接同一权重 —— 纹理绘制不吃 style alpha）。
  - 按钮 / 复选框悬停做指数平滑：`UiHoverBegin/End` 按 ImGui ID 存 0..1 权重，
    同时把 `ImGuiCol_Button` 与 `ImGuiCol_ButtonHovered` 压成**同一个**混合色，
    ImGui 在两种状态间选谁都得到这一帧的插值结果。时间常数 40 ms（120 ms 到 95%），
    与帧率无关。
- **硬编码颜色清理**：游戏列表里“未收录”行原来写死旧的浅色 `inkDim`
  （`ImVec4(0.490,0.475,0.447,1)`），深色主题下几乎看不见，改为读
  `Colors[ImGuiCol_TextDisabled]`；警告色 `kAttentionColour` 拆成 `AttentionColour()`，
  深色用 `#ECA34A`。统计面板四套配色自带底板与文字色，不随主题变（它压在游戏画面上，
  不能跟着窗口主题走）。
- **超低配极速预设**顺带写入 `UiFrostedGlass=0`，档位匹配也检查它；
  改过这一项会回落到「自定义」。
- **原始界面主题的门控**：`OverlayWantsFrost()` 在这个主题下直接返回 false（不录模糊链），
  游戏内面板也不画 Plate、不把 `WindowBg` 透明化、不把 `WindowPadding` 抬到 26/22，
  `OverlayBody` 恢复自带底色，存档表也不再加 `Indent` —— 否则“原始”主题里会冒出一层纸色玻璃
  和一圈多余留白。
- 工具：新增 `Tools/Capture-Ui.ps1`（DPI-aware 抓窗口，用于看界面）与
  `Tools/Crop-Image.ps1`（放大局部看圆角/高光）。`SM3D_OVERLAY_OPEN` 改为开局 120 次循环
  后再打开面板 —— 在第 0 帧打开会先暂停模拟器，窗口是黑的、面板也不会被绘制。
- 验证：CMake 两个目标（`Supermodel-LegacyOG` SDL2/OpenGL 与 `Supermodel-VK-SDL3`
  纯 Vulkan）编译通过；启动器浅色/深色、游戏内面板浅色实机截图确认玻璃、圆角、高光边、
  内缩留白与文字对比度；日志确认 `UiBlur: frosted backdrop chain ready (2017x1321 ->
  1008x660 -> 504x330)`。**原始界面主题只做了编译与代码审阅**（色值/间距逐项对照重做前的
  `ApplyUiTheme()`），本轮没有再抓图；`build.bat -package` 已重新打包
  `build-package/Supermodel-VK.v0.9.6.3a.for.Windows.zip`。**未跑** `Tools/Validate-Vulkan.ps1`：
  这台机器没有装 Vulkan SDK（无 `VK_LAYER_KHRONOS_validation`），同步/GPU 辅助验证无法启用；
  代码按既有约定逐次 `CmdTransition` 显式转换布局，swapchain 先转 `TRANSFER_SRC` 再转回
  `COLOR_ATTACHMENT`，blur 图像各自跟踪 layout。

**Release notes**

- zh：设置窗口与游戏内 Tab 面板重做为毛玻璃界面：面板背景是当前画面的双重模糊
  （Kawase 降/升采样，半分辨率四遍），带 1px 玻璃高光边；三套主题——**浅色暖色护眼
  （默认）**、深色与**原始界面（重做前的外观）**，都在「常用」→「界面外观」里，切换立即生效。
  前两套都不用纯白纯黑，浅色是暖羊皮纸 + 陶土琥珀，深色是暖岩灰 + 琥珀金，浅色主题会把
  模糊画面向暖白提亮（不会发灰）；原始界面就是重做前的平面暖纸样式，喜欢旧版的人可以随时
  切回去。启动器额外把所选游戏的封面洗淡后铺成底图，界面变成三块浮在底图上的
  玻璃面板，内容都从圆角边往里留了空。按钮悬停与面板呼出有 120ms 平滑过渡，圆角更细腻。
  面板关闭时模糊链完全不执行（0 开销）；核显可以在同一处关掉毛玻璃，
  「超低配极速」预设会自动关。
- en: The settings window and the in-game Tab panel are now frosted glass: a dual-Kawase blur of
  the frame behind each panel (half resolution, four passes) with a 1px specular rim, over three
  themes - **Light (warm eye-care, default)**, **Dark** and **Original interface** (the look from
  before the rework) - switchable live from Common -> Appearance. The first two use neither pure
  white nor black: light is warm alabaster with a terracotta accent, dark is warm rock grey with
  amber, and the light theme lifts the blurred picture toward warm white so it never reads as grey.
  Original interface is the flat warm-paper look the interface had before the glass rework, kept so
  that nothing taken away is lost. The launcher washes the selected game's cover across the window
  as an ambient backdrop and floats three glass panes over it, with all content inset from the
  rounded edges. Hover and panel-open transitions are smoothed over 120 ms and the corners are
  finer. The blur chain is skipped entirely while no panel is open (zero cost); it can be switched
  off for an opaque fill, and the Ultra-low preset does that for you.

### 超低配极速预设、四边形快速插值、GUI 状态记忆与固定刷新率置灰

- **Vulkan 四边形快速插值**：`real3d_common.glsl` 原来对所有四边形执行 `SolveQuadST()`
  逆双线性求解，并为此每像素拉取 4 个 64 字节顶点记录。现在 `ViewportUBO` 增加
  `renderOptions.x`（block 80 → 96 字节）：
  - 1 = 精确路径，与旧版逐位一致（默认）。
  - 0 = 快速路径：顶点着色器额外输出 `vNormal` / `vUvShade` / `vFaceNormal` /
    `vTextureNP` / `vColourBits`，片元直接使用硬件三角形插值，跳过 4 次 SSBO 读取和
    二次方程求解；代价是非平面四边形会出现 0-2 对角线接缝。
  - 新键 `QuadInterpolation`（默认 true），环境变量 `SM3D_QUAD_INTERP=1/0` 可临时强制。
    注意它和 New3D 的 `QuadRendering` 不是同一件事，后者仍是 OpenGL 的三角形/四边形选择。
- **硬件预设新增「超低配极速（496×384，核显）」**：一次写入 496×384 + `IntegerScale=1`
  + `MSAA=0` + `AlphaSortMode=0` + `TexFilter=0` + `QuadInterpolation=0` +
  `RefreshRateMode=0` + `MotionInterpolation=0` + `LowLatency=1` + `PostFXAA=0` +
  `PostCRT=0` + `PostScanlines=0` + `PostGlow=0`。档位匹配会检查这些附加键，手动改过
  任何一项都会回落到「自定义」。中英文标签、悬浮提示和「四边形精确插值」复选框已补齐。
  实测 daytona2 存档在 496×384 窗口下正常运行，日志 `Vulkan: quad interpolation fast`。
- **Manage Saves 返回上下文**：从存档卡片的「读取」进入游戏时记录 `g_returnToManageSaves`
  与目标行；`GuiRunOnce()` 在返回时调用 `OpenManageSaves()`，重新读取卡片（刷新缩略图
  和时间戳）而不是退回顶层游戏列表。双击列表 / 点「启动游戏」会清除该标志。
  已用 `SM3D_LOAD_SAVE` + Esc 实测：日志 `GUI: returning from a session started in Manage
  Saves; reopening 'daytona2'`，返回后弹窗仍在。
- **游戏列表跨会话记忆**：退出时写入 `GuiLastSelectedGame`（ROM set 名，不是行号）、
  `GuiGameListScrollY`、`GuiGameListFilter`。启动首帧按 set 名恢复选中项和封面，滚动位置
  用延迟恢复标志（`g_gameListScrollRestorePending`）：表格行提交后再 `SetScrollY()`，最多
  重试 4 帧，期间不覆盖保存值。
- **固定刷新率按显示器能力置灰**：`DisplayPacer.h` 统一候选表 `{60,75,90,120,144,165,240,480}`
  和 `ClampFixedRefreshRate()`；GUI 枚举窗口所在显示器的完整模式列表（桌面模式不够），
  高于最高档的条目用 `BeginDisabled` 置灰并给出「当前显示器最高支持 N Hz」提示。
  启动时把旧配置里的过高值钳制到最高可用档（GUI 与游戏会话各做一次），实测 60 Hz
  显示器上 120 → 60，日志 `Pace: FixedRefreshRate 120 -> 60 Hz`。
- **界面布局**：硬件预设从「核心」页的「高级设置」移到「常用」页的 Video 选项上方；
  「呈现节奏」整段移到「画面」页的第一节。全屏刷新模式里引用「上面的呈现节奏」的说明
  同步改为「画面」页，逻辑与键名不变。
- **刷新率与 G-SYNC 建议常驻显示**：「呈现节奏」下方固定两行灰色说明——60/75/90Hz 屏直接用
  「跟随显示器」（75/90 也会按面板速率插值），高刷屏先在 Windows 或显卡控制面板固定
  120/144/240Hz；G-SYNC 在 NVIDIA 控制面板 → 显示 → 设置 G-SYNC 启用，并说明
  「固定呈现频率」只改呈现目标、不切换显示器模式。原先挂在「跟随显示器」上的长悬浮提示
  相应缩短（窗口模式下会被裁掉），`usermanual.md` §4.1 / §4.3 / §9 与
  `usermanual_chs.md` 4.1 / 4.3 / 常见问题同步。
- **所有 Tab 的 (?) 提示改为简洁版**：单值控件一句话；选项较多的下拉框/单选项逐项各一句
  （硬件预设、固定呈现频率、VRR 倍频、插值范围、统计面板配色、全屏刷新方式、输入系统、
  CRT 色彩、2D 放大、CRT 滤镜、点阵类型、MSAA、贴图过滤、半透明排序、摇杆转向平滑）。
  原理、数字与推荐组合仍写在手册里，界面只保留「是什么 / 选哪个」。最长的一条（独占全屏
  G-SYNC）从约 25 行降到 2 行。
- **可点击控件显示手形光标**：平台层本来就有 `ImGuiMouseCursor_Hand` → 系统手形光标的映射，
  只是没有控件请求。新增 `HandOver()`，在 `UiButton` / `UiSmallButton` / `UiCheckbox` /
  `UiRadioButton` / `UiTabItem` / `ComboRow` 里调用，并补到游戏列表行、折叠标题、右键菜单项
  与游戏内面板的保存/应用/撤销/关闭。禁用控件 `IsItemHovered()` 为假，保持箭头。
- **Linux/macOS 存档缩略图**：`LoadSaveCards()` 的上传原本被 `_WIN32` 包住，而 Linux 的
  `DecodeBoxart()` 只有 libwebp，存档缩略图却是 PNG。现在 Linux 用 `Util/PNGFile.h`（zlib）
  在内存里解码 PNG 并按 `maxWidth` 缩放；上传步骤不再限定 Windows，macOS（ImageIO）也能显示
  卡片预览。游戏内面板的缩略图走同一函数，一并受益。
- **OpenGL-only 构建不再写入 Vulkan 专属键**：超低配预设里的 `MSAA` / `AlphaSortMode` /
  `TexFilter` / `QuadInterpolation` / `Post*` 用 `#if !defined(SUPERMODEL_OPENGL_ONLY)` 包住，
  避免在共享 ini 的根节点写出与 `[Video]` 同名、会遮蔽 Vulkan 版的重复键。

**Release notes**

- zh：新增「超低配极速」硬件预设：原生 496×384 + 整数倍缩放、关闭 MSAA/插值/FXAA/
  CRT/辉光，并启用 Vulkan 的四边形快速插值（跳过每像素 4 次顶点读取和二次方程求解）。
  设置界面现在会记住上次选中的游戏、搜索过滤和列表滚动位置；从「管理存档」读档进入游戏后，
  按 Esc 返回会直接回到该游戏的存档页；固定刷新率里显示器不支持的档位会置灰，旧配置里的
  过高值会在启动时自动钳制。设置窗口同时把「硬件预设」移到「常用」页、「呈现节奏」移到
  「画面」页顶部，并在其下方常驻刷新率与 G-SYNC 设置提示；各页 (?) 提示已改为简洁版，
  多选项下拉框逐项一句。可点击控件现在显示手形光标；Linux/macOS 的存档缩略图也能显示。
- en: Adds an Ultra low / fastest hardware preset: native 496x384 with integer scaling,
  MSAA/interpolation/FXAA/CRT/glow off, and Vulkan's fast quad path (no four vertex reads or
  quadratic solve per pixel). The settings window now remembers the last selected game, the search
  filter and the list scroll position; leaving a game started from Manage Saves returns to that
  game's save page; fixed present rates the display cannot scan are greyed out and an old
  too-high value is clamped at start-up. Hardware presets now sit on the Common tab and
  Presentation cadence at the top of the Video tab, with the refresh-rate and G-SYNC setup
  notes always visible beneath it. All (?) tooltips are now short; dropdowns with many
  entries describe each option in one line. Clickable controls now show the hand cursor,
  and save-state thumbnails also work on Linux and macOS.

### 全系统对齐到真机 57.524160 Hz（New 3D 节拍 + VRR 倍频 + 时间基准）

- 现象：LegacyOG（New 3D）的统计面板写 `Rate 57.50 Hz  arcade 57.52  (-0.04%)`。
- 根因有两层：
  1. `RefreshRate` 的「真实机台」预设存的是 **57.5**，而 OpenGL 经典循环直接拿它算
     `perfCountPerFrame`（1e12 / 57,500 = 17,391,304 ns）。每帧仿真仍按时间基准推进，
     所以游戏相对墙钟慢 0.042%；声音板每帧产出的样本数也跟着少，环形缓冲每秒少约
     19 个样本（400 ms 缓冲约 16 分钟见底），长局会周期性欠载。
  2. 时间基准本身是 **17,384,048 ns**，来自把 57.52416 Hz 四舍五入到整毫赫兹
     （57,524 mHz）再取倒数。真值是 17,384,000.04 ns，所以整个系统慢 2.76 ppm。
     单独的 2.8 ppm 可以忽略，但它会被 pacer 的 Original 目标、VRR 2x–8x 的呈现目标
     （绝对值 ×N）和音频样本累加器一起继承 —— 倍频就是把同一个相对误差带进更高的呈现率。
- 改动：
  - `kModel3StepNs` 改为 **17,384,000**（= `1e9 / 57.524160` 截断，差 2.6 ppb）。
    pacer 的 Original、VRR 2x/3x/4x/8x 目标因此变成 57.524160 / 115.048320 /
    172.572480 / 230.096640 / 460.193280 Hz。
  - `GetDesiredRefreshRateMilliHz()` 换成 `GetDesiredFramePeriodNs()`：帧周期直接用
    `1e9 / Hz` 算，不再先截断到整毫赫兹 —— 否则 57.52416 Hz 会又变成 17,384,048 ns，
    从另一扇门把同样的误差放回来。
  - `DefaultConfig()` 的 `RefreshRate` 允许值 `{57.5, 60}` → `{57.52416, 60}`。
  - `-true-hz` 同样写 `57.52416`。
  - 选项文案 `57.5 Hz（真实机台）` → `57.524 Hz（真实机台）`，`Localization.cpp` 同步。
  - 新增 `AlignLegacyArcadeRefreshRate()`：读到的 `RefreshRate` 在 57.5±0.01 内就改成
    57.52416。`main()` 对设置界面的副本静默执行，`RunGameSession()` 对运行时副本执行并
    打一行日志。ini 里的 57.5 不主动改写，下次设置界面写文件时落盘；其他值不动。
- 验证：
  - LegacyOG + daytona2：面板 `Rate 57.52 Hz  arcade 57.52  (+0.00%)`，
    日志 `rate 57.5242 Hz`（4 位小数）与 `Pace: RefreshRate 57.50 -> 57.52416 Hz ...`。
  - Vulkan + Fixed 120：`Rate 120.00 Hz  arcade 57.52  (+108.61%)`、`Pace 57.50 / 120.0 Hz`
    不变；修改走的是 `kModel3StepHz`，两者同源。
- 剩下的误差：整数纳秒截断的 0.04 ns/帧（2.6 ppb），约 0.23 ms/天，不做补偿。

**Release notes**

- zh：音频与帧节奏的时间基准修正到真机 57.524160 Hz。New 3D（OpenGL 版）以前按
  57.50 Hz 节拍、整个系统又多出 2.8 ppm 偏差，长时间运行会比真机略慢、音频缓冲缓慢变空；
  现在逻辑步、呈现目标和 VRR 2x–8x（115.048/172.572/230.097/460.193 Hz）全部同源对齐。
  旧 ini 里的 `RefreshRate = 57.5` 会在开局时自动当作 57.524 处理，设置界面下次保存时改写。
- en: The time base is now the arcade's exact 57.524160 Hz. The OpenGL build used to pace at
  57.50 Hz, and the shared step constant carried another 2.8 ppm of rounding - which the VRR
  2x-8x targets inherited. Logic steps, present targets (115.048/172.572/230.097/460.193 Hz)
  and the audio sample accumulator now all derive from the same exact figure. An existing
  `RefreshRate = 57.5` is treated as 57.524 automatically and rewritten on the next settings save.

### 统计面板：音频缓冲区间与声板 L/R 峰值

- 现状：`Audio  143 / 400 ms  4 ch  u0/o0` 只有一个瞬时水位。水位本身是锯齿
  （回调往下推、制片往上写），单点读数在几十毫秒里乱跳，看不出“最低点是不是在往下走”；
  而“没声音”时面板只有 MUTED 一个线索，分不清“机器没发声”和“输出被静音”——后者的
  `source peak` / `mix peak` 以前只在 `SM3D_AUDIO_LEVEL=1` 的日志里。
- 水位改为最近 2 秒（4 个 500 ms 采样）的 min–max：`Audio  112-198 / 400 ms ...`。
  只有一次采样时仍显示单值；`StatsOsdShutdown()` 清空历史，下一局从当前值重新开始。
- 新增声板输出峰值 L/R：`Audio.cpp` 的 `OutputAudio()` 在 SCSP/DSB 混好的源缓冲上取
  `max(|front|, |rear|)`，按 32768 归一化；`AudioStatus` 增加 `peakLeft` / `peakRight`。
  - 取在 balance/volume 之前、mute 之前。mute 发生在播放回调里，取在它后面会让
    “游戏静音”和“输出被静音”都读成 0，正好丢掉这一对要回答的问题。
  - 声板可能跑在自己的线程上，所以用 `std::atomic<float>` + relaxed CAS 取大；
    每帧每声道一次 CAS，开销可忽略。
  - **`GetAudioStatus()` 用 `exchange(0)` 读走即清零**：面板的 500 ms 采样看到的是一个
    窗口，而不是“开机以来的最大值”。目前只有 `StatsOsd` 调用它（这一点写进了
    `Audio.h`）；新增第二个读取者会各拿一半窗口。
  - `OpenAudio()` / `CloseAudio()` 也清零，避免上一局的值漏进下一局。
  - `SM3D_AUDIO_LEVEL=1` 的 `source peak` 改为复用同一组每帧峰值，不再单独扫四遍缓冲。
- 显示 `L 38%  R 41%`，超过 100% 照实显示（SCSP 与 DSB 相加后、输出 clamp 之前）。
  整行只在 MUTED 时用 warn 色；源峰值不作为削波报警——`MixChannels()` 会把前/后声道取平均，
  源峰值超过 100% 并不等于能听到削波。
- 实测（`Tools/Capture-Frame.ps1 -Set daytona2 -NoState -At 25`，`ShowStatsOsd=1`）：
  面板 `Audio  58-71 / 400 ms  4 ch  L 59%  R 64%  u1/o0`；同一段日志
  `source peak 23606–32732`（≈72–100%），与面板一致。开机服务菜单（无声）显示
  `L 0%  R 0%`。开机一次的欠载计数照旧。

**Release notes**

- zh：统计面板的 Audio 行现在显示最近两秒的缓冲水位区间（如 `112-198 / 400 ms`），
  并新增声板输出的 L/R 峰值百分比。峰值取在静音与音量之前，所以“游戏本身没声音”和
  “输出被静音”可以直接区分：峰值仍在跳但标着 MUTED = 输出被关；一直是 0% = 机器没发声。
- en: The stats panel's Audio line now shows the buffer fill as a two-second range (e.g.
  `112-198 / 400 ms`) and adds the sound board's L/R peak level as a percentage. The peaks are
  measured before the mute and the volume stages, so a silent game and a muted output are finally
  distinguishable at a glance: levels moving with MUTED next to them mean the output is off, a
  steady 0% means the emulated machine is not producing sound.

### 手柄转向平滑：模拟 270° 方向盘的阻尼与中心精度

- 现状：`CAxisInput::Poll()` 对摇杆轴是 1:1 直通，只有输入系统的死区/饱和缩放；
  `CWheelBoard` 每仿真帧直接读 `steering->value`，所以手柄 1~2 帧打满会原样进入游戏，
  Daytona 2 / Scud Race 的第三人称追尾相机对转向一阶差分很敏感，表现为镜头猛甩。
- 新增 `GamepadSteeringSmoothing`（0 关 / 1 轻 / 2 中 / 3 强），在
  `CInputs::PollGameInputsLate()` 里、CPU 跑帧之前执行一次：
  - 中心精度曲线 `sign(x)·|x|^gamma`（gamma 1.25 / 1.50 / 1.70）；
  - 一阶低通（时间常数 35 / 50 / 75 ms，按 57.524 Hz 仿真步长计算）；
  - 单帧最大变化量限制（0.16 / 0.10 / 0.07 归一化单位/帧，满舵约 100~250 ms）。
- 只改 Steering 轴，不改油门/刹车/换挡；真实方向盘用户建议保持 0。
- 设置界面「力反馈 / 灵敏度」页新增「手柄转向」下拉，改动即时生效
  （主循环每帧把 `s_runtime_config` 的值同步给 `CInputs`）。
- 新增 `SM3D_STEER_PROBE=1`：每 30 个仿真帧打印
  `SteerProbe: raw=... curved=... filtered=... value=... preset=...`，用于实机调参；
  `SM3D_STEER_FORCE=<signed fraction>` 可注入固定轴值，便于无手柄验证阶跃响应。
- 第一层不涉及渲染端相机矩阵；第二层（`CameraStabilizer`）留待单独实现/验证。

**Release notes**

- zh：手柄摇杆控制赛车方向盘时新增可选的转向平滑（关闭 / 轻度 / 中度 / 强力）。
  中心区更细腻，满舵约 0.1~0.25 秒，避免轻推摇杆导致 Daytona 2 / Scud Race
  追尾镜头剧烈左右抽搐。默认关闭，真实方向盘建议保持关闭；设置界面即时生效。
- en: Optional stick steering smoothing for gamepad racers (Off / Light / Medium / Strong).
  The centre is finer and full lock takes about 0.1-0.25 s, which stops a short stick flick
  from whipping the chase camera in Daytona 2 / Scud Race. Off by default; best left off with
  a real wheel. Applies immediately from the settings window.

## 2026-10-24

### 读档要等游戏启动完成（Vulkan）——“慢 25 倍”的真正原因

- 现象：从设置界面 Manage Saves → Load 载入 `vf3tb.zst10`（比赛中的存档）后画面像定格：
  人物几乎不动，只有计时以 0.01 秒的步长缓慢变化，音乐正常。同一份存档在已经运行中的
  游戏里用 F7 读则完全正常。
- 定位：不是渲染、不是存档损坏，也不是“存档点在 K.O.”。同一份 state 在不同时刻读档，
  读档后固定 8 秒截图，游戏内计时推进量分别是：第 1 帧 0.37 s、第 1 秒 0.32 s、
  第 3 秒 7.7 s、第 30 秒 6.75 s。**在游戏开机初始化结束之前读档，仿真仍按 57.5 Hz
  跑满，但游戏逻辑只推进到约 1/25**，所以看起来是“人物不动、时间在爬”。
- `Manage Saves → Load` 原本在开局 1 秒（约 57 个仿真帧）装状态，那时游戏还在引导阶段；
  `-load-state` 的“先跑一帧再读档”同样落在这个窗口里。等游戏起来（≥3 秒仿真时间）
  再装，行为与 F7 完全一致。
- 修法：
  - 延迟读档改为**按仿真帧计时**（`kDeferredLoadSeconds = 3.0` → 172 帧 @ 57.524 Hz），
    不再用墙钟：机器跑不满时等待的仿真时间不会变短。`SM3D_DEFER_LOAD_SECONDS` 仍可覆盖
    （0 = 立刻），只用于诊断。
  - `-load-state` 并入同一条延迟路径，不再在 `Reset()` 之后立刻读档，也不再需要
    “先跑一帧”这个补丁。
  - 等待期间用**整屏启动图**盖住：`ShowStartupSplash(window, /*coverMonitor=*/true)`
    把启动图铺满游戏窗口所在显示器的整个 `rcMonitor`（不再是 62% 方形），游戏窗口在它
    底下正常显示首帧，3 秒后装状态、撤图。玩家看不到游戏自己的开机画面。
  - Tab 面板的 Load / F7 仍然优先，并会立刻撤掉过渡图。
- 未解：为什么早期读档会让逻辑慢 25 倍。设备侧有状态没进存档是主要嫌疑（例如
  `C53C810` 只存寄存器、不存 `Ctx.halt` 等脚本执行状态），当前修法是绕开这个窗口。
- 验证：`SM3D_LOAD_SAVE` + `SM3D_SCREENSHOT_AT=12`，`vf3tb.zst10` 读档后约 9 秒：
  计时 24"18 → 16"62、比赛时间 5"87 → 13"37（≈正常速度）；日志
  `loading deferred state ... after 172 emulated frame(s)` 与
  `start-up artwork removed after the state was applied`；窗口探针确认过渡图
  `2560x1440 @ (0,0)`（150% DPI 下的整屏）覆盖，游戏窗口在其下已可见。

### 玩家手册（`usermanual.md` / `usermanual_chs.md`）

- 新增英文主版本 `usermanual.md`：两个 exe 的分工、Vulkan/SDL3 新功能与优势（附实测数字）、
  **推荐设置**（四档硬件预设的精确值、呈现节奏选择表、画面/声音/输入/存档逐项建议）、热键、
  文件目录、性能参考、排错、已知限制、命令行。
- `usermanual_chs.md` 是同一内容的中文精简版（只保留操作与推荐值），细节指向英文版。
- `build.bat` 把两份手册复制到发行包顶层（与 `README.txt` 并列）。
- 顺手修正 Vulkan 启动失败的弹窗：原文让用户改用不存在的 `Supermodel-VK.exe`，
  现在是 `Supermodel-LegacyOG.exe`。

### Windows 封面不再依赖 Store 的「Webp Image Extensions」（内置 libwebp 1.5.0）

- 起因：另一台 Win11 笔记本上主界面一个封面都不显示。路径不是写死的（由
  `GetModuleFileNameA` 推出 `<exe 目录>/Data/Assets/Boxarts/<set>.webp`），真正原因是那台机器
  没装 `Microsoft.WebpImageExtension`，WIC 对所有 `.webp` 一律返回失败。
- Windows 现在有两条解码路径（`Gui.cpp` 的 `DecodeBoxart()` 派发）：
  先走 `DecodeBoxartWIC()`，失败再走 `DecodeBoxartWithLibwebp()`（`WebPDecodeRGBA` +
  自写双线性缩放到最大宽度 384）。回落成功会打
  `Boxart: '<file>' decoded with the bundled libwebp decoder.`；
  `SM3D_BOXART_DECODER=wic|webp` 可强制其中一条（诊断用）。
  macOS 的 ImageIO 与 Linux 的 `-lwebp` 分支未改动。
- 解码器是 vendored 的 libwebp **1.5.0** 解码子集，放在 `Src/Pkgs/webp/`
  （`COPYING` 为 BSD-3-Clause，随仓库；`README.md` 记来源与取舍）。只编解码侧
  `dec/` `dsp/` `utils/`，不编编码器。Windows 两个目标都静态链进去（`CMakeLists.txt`
  的 `SUPERMODEL_WEBP_SOURCES`、`MSVC/Supermodel.vcxproj` 的 `<ClCompile>`），Linux/macOS 不编。
- **踩坑**：`dsp/*.c` 无条件声明 `VP8DspInitSSE2/SSE41` 等入口，实现分在 SSE 文件里，
  由 MSVC x64 的 `_M_X64` 自动定义 `WEBP_USE_SSE2/SSE41` 选中。只加通用 `.c` 会链接失败
  （16 个 unresolved external）；必须同时编 12 个 SSE 文件（含 `common_sse2.h` /
  `common_sse41.h`）。不要去改 libwebp 源码消引用。
- `src/dsp/lossless.h` 无条件 include `src/enc/histogram_enc.h`，后者又引
  `backward_references_enc.h` 与 `webp/encode.h`，所以 `src/enc/` 只补两个头、
  `src/webp/` 要放全套公开头，但不需要编编码器。
- 封面诊断日志：首次查表打 `Boxarts: N file(s) in '<path>'`，逐个 set 在
  `info` 级别打 `not found` / `could not be decoded`（成功不刷屏，除回落那一条）。
- 验证：`SM3D_BOXART_DECODER=webp` 启动后日志出现
  `Boxarts: 63 file(s) in '...'` 与逐个 `decoded with the bundled libwebp decoder`，
  封面在列表里正常显示（本机 WIC 本来是好的，所以必须强制才走得到这条路径）。
- 文档同步：`usermanual.md` §9/§10、`usermanual_chs.md` §2/常见问题、
  `Docs/README-VK.txt` 系统要求、`README.md` Known limitations 都删掉了“需要 Store 包”的说法。

### 统计浮层：CPU 型号、单核峰值频率、非 NVIDIA 机器的 GPU 占用

- **CPU 行改成两行**（照 GPU 行的样子）：型号在标签行，数字在下一行缩进。型号取
  `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0\ProcessorNameString`，
  读失败就退回单行（行为与以前一致），顺手去掉 OEM 字符串的首尾空格。
- **数字加了 `peak`**：以前只显示「8 个非休眠逻辑处理器的平均频率」。这个数在笔记本上误导性很大：
  一核满载 4.1 GHz、其余七核 1.0–1.6 GHz，平均值只有 2 GHz 左右，看起来像「主频上不去」。
  现在同时打 `%u MHz  peak %u`，日志也改成
  `Stats OSD: CPU <型号>, clock N MHz average, M MHz peak (K performance core(s))`。
- **GPU 行不再依赖 NVIDIA**：没有 NVML 时回落 PDH 的
  `\GPU Engine(*)\Utilization Percentage`，按实例名里的 `engtype_3d` 把各进程的 3D 引擎占用求和
  （上限 100%）。只提供**占用**，频率和显存没有不依赖厂商的替代方案，所以那两项直接不显示，
  而不是打 0 冒充测量值。
- **踩的坑**：PDH 的实例名是小写 `engtype_3d`，用 `wcsstr(name, L"engtype_3D")` 永远匹配不到，
  而失败形态是「GPU 行凭空消失」——不会报错。现在用不区分大小写的子串匹配（`ContainsNoCase`），
  CRT 没有 `wcsistr`，自己写了 20 行。实例名形如
  `pid_1916_luid_0x00000000_0x0001adf4_phys_0_eng_0_engtype_3d`，pid 和 LUID 每次都变，
  所以只能匹配后缀。
- GPU 名字现在由 Vulkan 上下文提供（`CVulkanContext::DeviceName()`，取自
  `vkGetPhysicalDeviceProperties`），因为 `StatsOsd` 自己不知道渲染器选了哪张卡。NVML 可用时仍用
  NVML 的名字覆盖。
- `SM3D_STATS_GPU=pdh|nvml` 可强制数据源（诊断用，和 `SM3D_BOXART_DECODER` 同一套思路）；
  日志每次会话打一行说明用的是哪个源：
  `Stats OSD: no NVML on this machine; GPU load comes from the Windows GPU Engine counters.`
- 实测（开发机 RTX 5060 Ti，强制 `pdh` 与默认各截一帧）：
  默认路径 `GPU NVIDIA GeForce RTX 5060 Ti / 1897 MHz 19% VRAM 3.9 GB / 15.9 GB`；
  强制 PDH 后 `GPU NVIDIA GeForce RTX 5060 Ti / 18%`；CPU 行
  `Intel(R) Core(TM) Ultra 5 125H / P 3666 MHz peak 4092 8P+10E threads 20%`。
  同时验证了 PowerShell 的 `Get-Counter '\GPU Engine(*)\Utilization Percentage'` 与本机实例名格式。
- 文档：`usermanual.md` §3.6 与 §9 新增「如何从统计面板判断 CPU/GPU 瓶颈」；
  `usermanual_chs.md` §3 与常见问题同步。
- **音频行**：`Audio  143 / 400 ms  4 ch  u0/o0`。`GetAudioStatus()`（`Src/OSD/Audio.h`）
  在 500 ms 采样里取快照：环形缓冲区水位与容量（毫秒）、主机声道数、累计欠载/溢出、
  以及 `enabled` 翻转后的静音标志（MUTED 用 warn 色）。
  水位必须在音频锁下读：`writePos`/`playPos` 不在锁下读会撞上回调刚推进 `playPos` 但还没清
  `writeWrapped` 的瞬间，差距会整整多一个缓冲区长度。
  毫秒换算是 `bytes / (bytes_per_frame_host × kModel3StepHz)`，与 `OpenAudio()` 自己那行
  `Audio: buffer %u ms` 对得上（默认 `AudioLatency=40` → 400 ms）。
  开机阶段出现 1 次欠载是正常的（声音板启动前不发声），手册里写明了。

### 设置面板：统计面板说明去掉过时限制；「显示帧率」改名「标题栏帧率」

- `HelpMarker` 里那句「只有 Vulkan 渲染引擎下有效」删掉了：用 `Tools/Capture-Frame.ps1`
  `-Exe Supermodel-LegacyOG.exe -Ini ShowStatsOsd=1` 截了一帧，OpenGL/New 3D 下统计面板正常显示
  （`Renderer New 3D` 那行就在图里）。这句从 Overlay 支持 OpenGL 时就已经错了，只是没人回头看。
- 同一段说明更新为实际内容：「CPU 型号与平均/单核最高频率及占用」「GPU 型号与占用（有 NVIDIA
  驱动时还显示频率与显存）」——即本日新增的两项。
- 勾选项 `显示帧率` → `标题栏帧率`（英文 `FPS in title`）。它和统计面板不是一回事：每秒把
  FPS 与音频欠载/溢出计数写进窗口标题，不在画面上叠任何东西，独占全屏时标题栏不可见。
  名字改成写在哪里，并加悬停说明；配置键 `ShowFrameRate` 与 `-show-fps` 命令行不变。
- 注意：`Localization.cpp` 的英文表以中文原文为键，改标签/说明文字必须同步改表，否则英文界面
  会直接显示中文。两处已同步。

### Linux：启动时因 CFF2 可变字体崩溃（Issue #5）

- 现象：openSUSE Tumbleweed 用户启动即退出。`strace` 看到加载
  `/usr/share/fonts/truetype/NotoSansCJKsc-VF.otf`，随后在 `imgui_draw.cpp` 的
  `ImGui_ImplStbTrueType_FontSrcInit` 里报
  `stbtt_InitFont(): failed to parse FontData ... Check FontDataSize.` 并 abort。
- 根因：ImGui 自带的 stb_truetype 1.26 支持 `glyf` 与 `CFF `（普通 OpenType/CFF），
  **不支持 `CFF2`**（可变字体）。Fontconfig 把 VF 字体排在第一位，`AddFontFromFileTTF()`
  直接把它交给 ImGui，而 ImGui 的失败路径是 `IM_ASSERT_USER_ERROR` → 进程退出，
  启动窗口都还没画出来。
- 新增 `Src/OSD/SDL/FontProbe.{h,cpp}`：`ProbeFontFile()` 在交给 ImGui **之前**用**同一份**
  `imstb_truetype.h` 做一次 `stbtt_InitFont`（自带 `STBTT_STATIC` 私有副本，与
  `imgui_draw.cpp` 里那份同源，所以“探测通过”等价于“ImGui 不会再失败”）。
  此外先做带边界检查的表目录解析：
  - 识别 `CFF2` 并给出明确原因（不是笼统的“字体损坏”）；
  - 校验字体签名、表数量、每张表都落在文件内、`cmap/head/hhea/hmtx/maxp` 齐全、
    `glyf`/`loca` 成对或存在 `CFF `。
  - **这一步不是可选的**：stb_truetype 自己声明不做边界检查，测试里一个 4 KB 的 `0x5A`
    垃圾文件让 `stbtt_InitFont` 直接段错误（字节 4-5 被读成 23,130 张表）。现在这种文件
    在解析前就被拒。ImGui 还有 `font_data_size > 100` 的断言，所以再加 1 KB 下限。
- `LoadUiFont()`：所有候选（Windows/macOS 硬编码列表、Linux 列表、Fontconfig 结果）都要过
  `ProbeFontFile()`，失败则 `GUI: skipping font '<path>' - <原因>.` 并试下一个。全部失败时
  回落到内置字体并明确写日志（中文不会显示 + 建议装 `fonts-wqy-microhei`），而不是静默变方块。
  成功时新增 `GUI: interface font '<path>'.`，以后字体问题直接看日志。
- Linux 候选重排：TrueType 优先（wqy-microhei / wqy-zenhei / DroidSansFallbackFull /
  AR PL UMing、UKai），再是 Noto CJK（CFF 可读），**不再列出 `-VF` 文件**（CFF2 永远不可用）。
- Fontconfig 查询从 `fc-match`（单个最佳匹配）改为 `fc-match -s`（排序列表），最多取 8 个候选
  逐个探测：Fontconfig 仍决定顺序，兼容性决定在哪里停下。openSUSE 的问题正是
  “最佳匹配不可用”。
- 独立测试（单独用 MSVC 编译 `FontProbe.cpp`）：
  - 合成 CFF2 sfnt → `a variable font (CFF2 table)...`；
  - CFF2 在 TTC 里 → 同上；face 1 → `no font face at the requested index`；
  - 4 KB `0x5A` 垃圾 → `not a TrueType/OpenType font (unrecognised signature)`
    （修复前是段错误）；
  - 截断的真实字体（1 KB）→ `a font table lies outside the file`；
  - `msyh.ttc` / `arial.ttf` / `segoeui.ttf` → USABLE。
- 实测启动：`GUI: interface font 'C:\Windows\Fonts\msyh.ttc'.`、`GUI: interface ready (1450 ms)`
  ——探测多读一次字体文件，成本在噪声内。
- 构建：`FontProbe.cpp` 加入 CMake、`Makefiles/Rules.inc` 与 `MSVC/Supermodel.vcxproj`。

### 版本号 v0.9.6.3a；两个 exe 各自的名字；发行包收尾

- `SUPERMODEL_FORK_VERSION` → `0.9.6.3a`（`SUPERMODEL_VERSION` 未动，存档仍兼容）。
- `SUPERMODEL_TITLE` 改为按构建区分：Vulkan 版 `Supermodel-VK`，OpenGL-only 版
  `Supermodel-OpenGL`。标题栏因此是 `Supermodel-VK v0.9.6.3a - <游戏>` /
  `Supermodel-OpenGL v0.9.6.3a - <游戏>`，控制台 banner、设置窗口标题与启动图落款同步。
  以前两者都叫 Supermodel-VK，从截图分不出跑的是哪个 exe。
- 发行包说明重写：`Docs/README-VK.txt`（发布为 `README.txt`）从 918 行缩到一页，
  只保留“这是什么 / 新增了什么 / 两个 exe / 快速开始 / 热键 / 目录 / 系统要求 / 去哪看详情”。
- `Docs/安装说明.txt` 合并进两份手册后删除（它是同一批内容的第二份拷贝，而且已经过时）。
  合并进去的独有内容：免安装说明与唯一外部依赖 `vulkan-1.dll`、首次运行生成
  `Data/Config/Supermodel.ini`、目录可整体移动、输入系统怎么选、热插拔、
  列表为空（缺 `Games.xml`）、缺 `api-ms-win-*.dll` 等。
  **顺带修正手册的错误**：原文写“输入系统选 XInput”，但 SDL3 版只有 `SDL` / `SDL 手柄`；
  旧 ini 里的 `xinput` 会在开局时自动转成 `sdlgamepad`（日志有记录），手册已按实际行为重写。
- `Makefiles/Rules.inc` 的 `PKG_FILES` 去掉该文件，改为携带 `usermanual.md` / `usermanual_chs.md`。
- `build.bat -package`：组装完后另存一份干净副本并压缩成
  `build-package/Supermodel-VK.v<版本>.for.Windows.zip`，自动排除 `Data/Saves`、
  `Data/Saves_Legacy`、`Data/Screenshots`、`Data/NVRAM`、`Data/Cache`、`Data/Analysis`、
  `*.log` 和两个 ini（发行包不能带开发者本人的存档与设置）。
- 手册补充说明发行包里的其余内容：新增 §7.1 讲 `Scripts/ArcadeDisplay.*`（List/Save/Restore/Run、
  参数、为什么需要它、有 VRR 时不用它）；§4.4 新增“贴图过滤三档的实际效果”表，
  把各向异性的收益写成可核对的数：Daytona 2 远墙区域高频能量 9.39 → 10.48（锐利）→ 12.10
  （+4x 各向异性），4K 下约 +0.05 ms/帧。中文版同步（精简）。
- 手册 §3.5 标题改为 “Input (some of this is untested)”，并逐项标出**未实测**的功能：
  方向盘力反馈、双光枪、Sinden 白边、双机联机（开发机只有普通手柄、没有光枪和第二台机器）；
  §4.6 / §4.8 / §10 与两份 README、中文精简版 §3/§4.4/§8 同步声明。
- `Scripts/ArcadeDisplay.ps1` 的默认 exe 改为 `Supermodel-VK-SDL3.exe`（原来优先 LegacyOG，
  发行包里双击 `-Action Run` 会启动 OpenGL 版）。`-SupermodelExe` 仍可指定另一个。

**Release notes**

- zh：修复从「管理存档 → 读取」载入比赛中存档后画面像定格的问题（人物不动、计时缓慢爬升）。
  原因是读档发生在游戏开机初始化结束之前，此后游戏逻辑只以约 1/25 的速度推进。现在会等
  游戏启动完成（约 3 秒仿真时间）再装状态，这段时间用启动图整屏遮盖，随后直接进入存档
  画面；`-load-state` 走同一条路径，F7 / 面板读档不受影响。
- zh：Windows 机器不用再装 Microsoft Store 的「Webp Image Extensions」——程序已内置 WebP 解码器，
  缺该包时自动回落，封面照常显示；日志会写 `decoded with the bundled libwebp decoder`。
- zh：统计浮层现在显示 CPU 型号和**单核峰值频率**（以前只有一个被空闲核拉低的平均值），Intel/AMD
  核显也能看到 GPU 占用（数据来自任务管理器同源的 Windows 计数器）。
- en: Fixed the picture appearing frozen (characters still, timer creeping up) after loading a
  mid-match save from Manage Saves → Load. The state was applied before the game had finished
  booting, after which its logic ran at about one twenty-fifth speed. The state is now applied
  once the game has started (about three seconds of emulated time), with the start-up artwork
  covering the screen in the meantime; `-load-state` takes the same path, and F7 / the Tab
  panel's Load are unchanged.
- en: Windows no longer needs the "Webp Image Extensions" package from the Microsoft Store. A WebP
  decoder is now linked into the executable and used automatically when the Windows one is absent,
  so covers appear either way; the log says `decoded with the bundled libwebp decoder`.
- en: The statistics readout now names the CPU, shows the peak single-core clock next to the average
  (the average alone reads as "this CPU will not boost" on laptops), and reports GPU load on Intel
  and AMD adapters through the same Windows counters Task Manager uses.
- zh：修掉 Linux 上一个启动即退出的崩溃：系统 CJK 字体如果是可变字体（`-VF.otf`，表 `CFF2`），
  ImGui 自带的字体解析器读不了并强退。现在所有系统字体先探测再交给 ImGui，不可用的跳过并写日志，
  Fontconfig 结果改为按排序逐个尝试（优先 TrueType），全失败则回落到内置字体并提示装
  `fonts-wqy-microhei`。
- en: Fixed a start-up crash on Linux when the system CJK font is a variable font (`-VF.otf`, CFF2
  table), which the font rasteriser bundled with ImGui cannot read. Every candidate is now probed
  before it reaches ImGui, unusable ones are skipped and named in the log, Fontconfig results are
  tried in ranked order with TrueType preferred, and a missing font falls back to the built-in face
  with an explicit hint instead of exiting.

## 2026-10-24

### 初始读档与线程完成标志（Vulkan）

- Manage Saves → Load 与 `-load-state` 都在 `Model3->Reset()` 之后、第一帧之前读档；
  此时 `gpusReady` 仍为 false，`SyncGPUs()` 尚未发布只读快照。Vulkan 构建现在先
  `RunSimulationFrame()` 跑一帧核心仿真，再 `LoadState()`，让开局前读档与 F7 的
  “运行中读档”具有相同的不变量。
  （**已被 2026-10-25 的延迟读档取代**：这个方案仍然把状态装进了尚未启动完成的机器，
  表现为游戏逻辑慢约 25 倍。现在两条路径都改为启动完成后按仿真帧延迟读档。）
- `CModel3::LoadState()` 清除 `ppcBrdThreadDone` / `sndBrdThreadDone` /
  `drvBrdThreadDone`：F7 在仿真帧中途按下时，`PauseThreads()` 会把它们置 true；
  不清的话下一次 `EndSimulationFrame()` 会跳过对新一帧的等待。
- 诊断：`Loaded state` 日志带 PPC PC/LR；每 5 秒 PACE 前打印一次 PPC 采样；
  `CVulkan3D::ResetInterpolation()` 后首帧打印 node/draw/current/blockCulling；
  新增 `SM3D_LOAD_SAVE` 开发者钩子，配合 `SM3D_SAVE_MANAGER` 复现
  Manage Saves → Load 的完整路径。
- 验证：`vf3tb.zst4` 通过 `-load-state` 与 Manage Saves 钩子得到逐字节相同的截图；
  PACE `sim` 57.5Hz、`presents` 120Hz、`draws` 425。`zst2` 是 Round 1 过渡
  （1 node/0 draw），属于不适合读档的过渡状态，不是 Vulkan 渲染 bug。

### F7 快速读档：空槽回退到最新存档

- 现象：在 1–3 槽存了三份后重启，按 F7 没反应；日志里反复出现
  `Unable to load state from '.../vf3tb.st4'`、`.st5`、`.st8`……。
- 原因：新会话的当前槽是 `NextFreeSaveSlot()` 选的**第一个空槽**（1–3 已占用时是 4），
  这是为了让 F5 新建存档；但 F6 的语义也是“跳到下一个空槽”，所以从 4 开始只会走 5、6、7……，
  F7 永远在试着读不存在的槽位，已有的 1–3 反而无法通过键盘选到。
- 修法：`LoadState()` 在按槽位读档且当前槽为空时，回退到 `NewestSaveSlot()`（1–16 中
  编号最大的已占用槽），并记录实际读取的槽位；一个存档都没有时给出明确的
  `No save state in slot N`，而不是模糊的 `Unable to load state`。
  F6 仍保持“下一个空槽”，因为它是给 F5 新建存档用的；要读旧槽请用 Tab 面板的
  Save States 列表，或直接 `-load-state=<file>`。
- 新会话的初始槽仍是第一个空槽，所以 F5 不会在未读档时覆盖已有存档；一旦 F7
  回退并成功读取，当前槽会切到那个存档，之后的 F5 就是“继续玩并覆盖它”。

**Release notes**

- zh：修复重启后按 F7 没有反应的问题。新会话原本停在第一个空槽，F6 又只跳到空槽，
  导致 F7 一直在读不存在的槽位。现在当前槽为空时 F7 会自动读取编号最大的已有存档，
  并在日志里写明实际槽位；没有存档时会给出明确提示。F6 仍是“下一个空槽”（供 F5
  新建存档），要选旧槽请用 Tab 面板的 Save States 列表。
- en: Fixed F7 doing nothing after a restart. A new session starts on the first empty
  slot and F6 also skips occupied slots, so F7 kept trying slots that did not exist. F7
  now falls back to the newest save when the selected slot is empty, logs the slot it
  actually loaded, and reports clearly when there are no saves at all. F6 still selects
  the next empty slot for F5; use the Tab panel's Save States list to pick an older slot.

### LegacyOG 存档改写到 Data/Saves_Legacy

- `Supermodel-LegacyOG.exe`（`SUPERMODEL_OPENGL_ONLY`）写的是原始 `.st`，读不了 Vulkan 版的
  `.zst` 容器；两个 exe 共用一个 `Data/Saves/` 时，槽位列表会取决于最后一个写入者。
- 新增 `FileSystemPath::SavePathType()`：OpenGL-only 构建返回 `SavesLegacy`，其他构建返回
  `Saves`。`SaveSlotContainer()` / `SaveSlotLegacyFile()` / `SaveFolder()` 都用它，所以
  F5/F7、游戏内面板、设置界面自动一致；新增目录由 `GetPath()` 按需创建。
- 兼容旧存档：Legacy 仍会回退读取 `Data/Saves/*.st`（旧文件不移动、不复制），但新存档只写
  `Data/Saves_Legacy/`。Vulkan 版的「管理存档」卡片列表仍只扫描 `Data/Saves/`，但
  「导入旧版 .st」会同时扫描 `Data/Saves/` 与 `Data/Saves_Legacy/`，转换后的 `.zst` 写到
  `Data/Saves/`，不需要手动复制文件。
- `build.bat` 的保留/重建列表加入 `Saves_Legacy`，全量打包不会删掉它。
- 三平台路径实现（Windows / Unix / macOS）都加了新枚举值；Linux 的 Makefile 构建没有定义
  `SUPERMODEL_OPENGL_ONLY`，仍使用 `Data/Saves/`（CMake 的 SDL2 目标会用新目录）。

**Release notes**

- zh：`Supermodel-LegacyOG.exe` 的即时存档改写到 `Data/Saves_Legacy/`，不再和 Vulkan 版的
  `Data/Saves/` 混在一起；旧 `.st` 仍可读取，新存档只写新目录。Vulkan 版的「导入旧版 .st」
  会同时扫描这两个目录。`build.bat` 会保留两个目录。
- en: Save states written by `Supermodel-LegacyOG.exe` now go to `Data/Saves_Legacy/` instead
  of sharing `Data/Saves/` with the Vulkan build. Old `.st` files in `Data/Saves/` are still
  readable; new saves only go to the new folder. "Import legacy .st" in Manage Saves scans both
  folders. `build.bat` preserves both folders.

### 呈现节奏命名与 G-SYNC 说明修正（Vulkan）

- 「固定刷新率」改名「固定呈现频率」（英文 `Fixed present rate`），下面的下拉改为
  「呈现频率」。原名和显示器刷新率同名，选 Fixed 120Hz 之后再看「全屏刷新方式」里的
  G-SYNC/VRR，很容易读成两个互斥的刷新率设置。
- 「独占全屏 · G-SYNC/VRR」的说明改为按新模型表述：Vulkan 版的呈现速率由
  「呈现节奏」决定（`RefreshRateMode` / `FixedRefreshRate`），这一项只决定谁定扫描时刻。
  选中后会在下面直接显示面板跟随的速率（如 `120.00 Hz`），并说明两者可以同时使用。
  旧文案「EDID 包含 57.5Hz 就自动按街机 57.5Hz 呈现」只保留在 LegacyOG；
  那里 `RefreshRate` 仍是限帧来源，行为没有变。
- 修正 G-SYNC 路径的启动日志：Vulkan 版不再声称 `pacing at the arcade rate`
  （pacer 读 `RefreshRateMode`，不读 `RefreshRate`）；LegacyOG 的日志保持原样。
- Vulkan 版隐藏「核心 → 显示与运行 → 刷新率」。这个键只驱动 OpenGL 的一仿真一呈现
  循环；Vulkan 循环由 pacer 控制，改了没有效果（音频也早已不读它）。
  双后端构建保留它，因为回退到 OpenGL 时仍然有效。
- `Localization.cpp` 同步了改名后的条目、新的 G-SYNC 提示和速率说明行。

**Release notes**

- zh：设置界面的「固定刷新率」改名「固定呈现频率」，避免和显示器刷新率混淆；独占
  G-SYNC/VRR 的说明改为「只决定谁定扫描时刻」，并显示面板实际跟随的呈现速率
  （Fixed 120Hz + G-SYNC = 面板按 120Hz 扫描，两者可以同时使用）。Vulkan 版隐藏
  不再起作用的「刷新率」下拉，启动日志也不再误报「按街机速率呈现」。
- en: The "Fixed refresh rate" cadence is now "Fixed present rate", so it no longer reads like
  a display setting. The exclusive G-SYNC/VRR option now says it only decides who drives the
  scan and shows the rate the panel will follow (Fixed 120 Hz + G-SYNC = the panel scans at
  120 Hz; the two are independent and can be combined). The Vulkan build hides the legacy
  Refresh rate dropdown, which no longer affects pacing, and its log no longer claims the
  session is pacing at the arcade rate.

### 启动图：底部不再显示版本号

- 启动图底部的说明行原来由 `SUPERMODEL_TITLE_FULL` 拼出，即
  `Supermodel-VK v0.9.5.6a  -  official build`。图片本身没有文字，这一行是 GDI 在运行时画上去的，
  但每次发版都会跟着版本号变，看起来像需要重做启动图。
- 现在只保留产品名与指纹标记：`Supermodel-VK  -  official build` /
  `Supermodel-VK  -  UNOFFICIAL BUILD`。版本号仍在标题栏、控制台 banner 和 About 行显示；
  SHA-256 校验未变，资源被替换时同样标红。
- 只改 `SplashScreen.cpp` 一处，图片资源与 `SplashResource.h` 的哈希都不需要动。

### LegacyOG：隐藏无效的「呈现节奏」（拖窗暂停仍保持回退）

- 「常用 → 呈现节奏」整段（原始 57.5 / 跟随显示器 / 固定刷新率 / VRR 倍频 /
  运动插值 / 插值范围）在 `#if !defined(SUPERMODEL_OPENGL_ONLY)` 里编译。
- OpenGL 目标 `highRefresh` 恒为 false，pacer 从不 `Configure()`；`New3D` / `Legacy3D`
  也没实现 `PrepareFrame()` / `RenderFrame(alpha, ...)`。这些控件改了没有任何效果。
- 「全屏刷新方式」（`ExclusiveRefreshRate` / `ExclusiveFullscreen`）是共享代码，
  Legacy 真的会用它切换独占全屏，保留。
- 拖窗暂停 + 静音那一项仍保持回退（`Main.cpp` 回到 SDL2 `WM_TIMER` 的原始路径）。
  它曾与本次改动同批引起整机卡死，分离后用 `/t:Rebuild` 全量重编单独验证。

### 存档管理：每张卡片可删除（Vulkan 版）

- 「管理存档」的每张卡片在 Rename 后面新增 Delete。点一次进入确认状态
  （[确认删除] [取消]），再点确认才真正删除；确认按钮占的是原 Load 的位置，
  所以双击 Delete 的第二次会落在 Cancel 上，不会误删。
- 删除同时移除 `<set>.zst<slot>` 容器和同槽位的旧版 `<set>.st<slot>` 回退文件。
  只删容器的话，`SaveSlotOccupied()` 仍会认为槽位被占用（它查两种文件），
  且「导入旧版 .st」会把卡片带回来。
- 删除后重新读取存档目录，卡片按时间重新排序。游戏内菜单的 16 个槽位本来就
  每帧从文件重建（槽位变空时缩略图缓存也会被丢弃），所以打开 Tab 面板看到的
  就是删除后的列表，不需要重启。
- 不重排已有槽位：空出来的序号就是下一次保存时 `NextFreeSaveSlot()` 找到的第一个
  空位。新开一局（不是从存档进入）时 F5 会选第一个空槽；从存档进入时仍写回该存档
  的槽位；F6 / 游戏内菜单的 Save 也可用空槽。
- 新增本地化条目：「删除」「确认删除」「删除失败」。
- 卡片内的标题、日期、大小和按钮行改为在卡片内容区水平居中（与缩略图对齐）：
  标题能放一行时居中，超出时换行并占满宽度；确认行的两个按钮也居中。
- 卡片最小宽度现在按翻译后的动作按钮行测量（三个按钮 + 左右各一份 `ItemSpacing`），
  保证按钮行居中后两侧留白相等。之前 150% 缩放下按钮行几乎占满内容宽度，
  `Delete` 会贴到右边框；根因是居中偏移被夹到 0 后整行向右溢出。

### 统计面板：Vulkan 的 Render 行改为「可见 3D 区域 → 输出尺寸」

- 之前 Vulkan 直接把交换链 extent 填进 `StatsRenderInfo`，全屏 4K 下就是
  `3840x2160`，即使 Resolution 选了 3x、整数缩放把可见区定成 2480x1920。
  读起来像 Resolution 没生效，实际只是报的是渲染目标而不是场景尺寸。
- 现在 `ReportRenderInfo()` 接收 `xRes/yRes`：Render 行显示场景实际光栅化的区域
  （例如 `2480x1920  MSAA 2x  -> 3840x2160`），箭头后是交换链/输出尺寸。
  OpenGL 超采样路径不变（仍是 `total*aa -> total`）。
- 整数缩放开启时 `XResolution` 本来就会被覆盖（设置页已有提示），这是设计行为。

### Vulkan：OIT 还原 layered（LAY=1）互斥语义 + srally2 扬沙柔化（AlphaSortMode=1 专属）

- 之前 Vulkan 完全不消费 `Mesh::layered`：OIT 里同一团沙尘的相邻/前后 quad 会各自入链并在
  resolve 里二次混合，沙尘盒子中间出现刺眼的深色棱角折线。
- 现在 `kMatLayered` 随材质进着色器；OIT capture 把 `LAY=1` 写进节点 key 的 bit 9（排序时
  mask 掉，不影响深度顺序），`oit_resolve.frag` 对每个像素/MSAA sample 只让**深度最近的那 1 个**
  layered 片元参与合成，其余跳过 —— 用 1 个 bit 还原硬件 stencil 的互斥语义，不加新 pass、
  不改深度格式（仍是 `D32_SFLOAT`）、零额外显存。
- 同时（仅 OIT，经 `kSoftEffects` specialization constant 打开）对沙尘做纯 ALU 柔化：
  `colour.a *= 0.70`，再乘 `smoothstep(0.08, 0.55, |N·V|)` 的轮廓羽化，低于 1/32 直接 discard。
  `TL=31/32` 的新生沙尘从 ~97% 不透光降到 ~68%，隐约透出轮胎/车尾；`TL=9..17` 的淡出帧自然平滑衰减。
- 去重和柔化共用 `IsDustEffect()` 指纹：**`LAY=1 && 不受光 && TLM=0 && textured`**。必须带 textured：
  srally2 车底还有一块**无纹理**的 layered 阴影/滑痕网格（flags 0x300），它同样命中 `LAY+LUM+TLM`；
  若一并去重/羽化，`|N·V|` 在相邻 quad 之间不连续，会在地面上画出折线。只处理带纹理的沙尘粒子
  （992,896 / 864,928），阴影保持原样。
- 默认双层路径逐位不变（`kSoftEffects=0`）。
- 验证：同一存档 `AlphaSortMode=0/1` 对照，沙尘带区域平均差 ~20（p95 ~59-68）；OIT + MSAA2 同步校验
  **0 VUID / 0 hazard / 0 error**。

### Vulkan：默认半透明路径改为双 Subpass 片上合成（AlphaSortMode = 0）

**现象**

- 默认路径（`AlphaSortMode=0`，New3D 的 Base + Trans1 + Trans2 三层）每个 Priority 都要
  结束一次 render pass 再开下一次：三张 4K 4x MSAA 颜色附件在每次开关时都要 load/store，
  半透明层最后再 resolve 成 1x 图给 2D 合成器采样。4K 下每张 4x MSAA 附件 133 MB。
- 更严重的是 **MSAA 下透明层的结果会丢**：透明 pass 只给颜色附件挂了 resolve，自己写的
  Trans1/Trans2 没有解析。最后一个 Priority 的半透明永远到不了 1x 图。《Daytona 2》吸引
  模式整帧只有一个 Priority，4x MSAA 下车影、瀑布的半透明完全消失；VF3 只是恰好被下一个
  Priority 的 color pass 顺带解析了，看起来正常。1x 没有 resolve，所以一直是对的。

**方案**

- 把默认模式的整个 3D 场景收进**一个 2-Subpass `VkRenderPass`**（`CVulkan3D::CreateScenePass`）：
  - Subpass 0：雾 quad + Priority 0..3（颜色层、Trans2、Trans1）。Priority 之间的深度清零用
    `vkCmdClearAttachments`，**不再结束 render pass**。
  - Subpass 1：全屏三角形（`scene_composite.glsl`）用 `subpassLoad()` 读 Trans1/Trans2，
    按 New3D 的两层公式算出预乘颜色，交给固定功能混合器 `ONE / ONE_MINUS_SRC_ALPHA` 叠到
    颜色附件上。两个透明附件 `storeOp = DONT_CARE`、`finalLayout` 仍是颜色附件，**不落显存、
    不 resolve、不回采**。
  - 依赖是 `0 → 1` 的 `COLOR_ATTACHMENT_OUTPUT → FRAGMENT_SHADER | COLOR_ATTACHMENT_OUTPUT`，
    `VK_DEPENDENCY_BY_REGION_BIT`：像素级局部依赖，tile-based GPU 可以把两张透明附件留在
    tile memory 里。
  - MSAA 时 Subpass 1 的 `pResolveAttachments` 指向 1x 图，合成与降采样一次完成；Subpass 1
    按采样执行（`sampleShadingEnable`，`subpassLoad(..., gl_SampleID)`），透明边缘保留抗锯齿。
  - **1x 时零拷贝**：不分配第 4 张附件，Subpass 0/1 直接在最终 1x 颜色纹理上画，pass 结束
    就是合成器直接采样的图。
  - 合成着色器在两层 alpha 都为 0 时 `discard`：Model 3 画面大部分像素没有半透明，这一条
    让低带宽核显完全不用做颜色附件的读-改-写。
- **场景颜色改为预乘 Alpha**：几何层本来就强制 `alpha = 1`，唯一受影响的是雾 quad（现在写
  `rgb * fogAmount`）。这样 Subpass 1 的硬件混合与 2D 合成器都可以直接用
  `ONE / ONE_MINUS_SRC_ALPHA`，不必回读颜色附件（没有 feedback loop、没有 GENERAL layout）。
- `composite.frag` 的 push constant 新增 `sceneComposited`：由 3D pass 合成过的帧跳过它自己的
  两层混合；同名数学改写为预乘形式（等价，误差 ≤1 LSB）。场景层在 2D 合成器里换用
  `ONE / ONE_MINUS_SRC_ALPHA` 的管线（瓦片层与机柜背景仍是直通 Alpha）。
- 未使用 `VK_KHR_dynamic_rendering_local_read`：那是 Vulkan 1.4 时代的扩展，Intel Gen9.5
  不保证支持；多 Subpass + `subpassLoad` 是 Vulkan 1.0 能力。
- 回退路径保留且逐像素一致：`AlphaSortMode=1`（需要 capture/resolve 之间的 barrier）、
  黑屏白闪、以及**需要 LOS 回读的帧**（`SceneNeedsLos()`：compute dispatch 不能录在 render pass
  里）继续走动态渲染。`SM3D_NO_SUBPASS=1` 可强制走旧路径做 A/B 或驱动兼容兜底。
- 顺带修掉上面那个 MSAA 解析 bug：现在每个 pass 只 resolve 自己真正写过的附件（颜色 pass
  解析三张，透明 pass 解析 Trans1/Trans2）。

**性能（3840×2160，VF3 海滩静态存档，RTX 5060 Ti，`RefreshRateMode=6`、
`PaceAssumePanelHz=600`、`VSync=0`，用 present Hz 反推帧耗时）**

| 配置 | present | 帧耗时 |
| --- | ---: | ---: |
| MSAA 4x，改动前（动态渲染） | 226.1 Hz | 4.42 ms |
| MSAA 4x，只跳过全部 resolve（探针） | 237.9 Hz | 4.20 ms |
| MSAA 4x，双 Subpass | **240.2–241.3 Hz** | **4.14–4.16 ms** |
| MSAA 4x，同版本动态渲染（`SM3D_NO_SUBPASS=1`） | 230.2 Hz | 4.34 ms |
| MSAA 1x，改动前 / 双 Subpass | 245.3 / 245.9 Hz | 4.08 / 4.07 ms |
| 1920×1080 MSAA 1x，双 Subpass | 441.9 Hz | 2.26 ms |

- 4K 4x：比改动前快约 **6%**，比跳过所有 resolve 的探针还快（探针仍要 load/store 三张附件）。
- 1x 没有 resolve 可省，收益在误差内；这一档的收益在核显上体现为带宽而不是这台机器上的帧率。

**验证**

- 逐像素：同一存档、同一 `-At`、3840×2160 **1x**，新旧构建最大差 **1 LSB**（均值 0.13，
  来自雾色预乘的 8 位量化），双 Subpass 与同版本动态渲染**完全一致**（最大差 1）。
- 4K 4x：改动前 4x 与 1x 在月亮/天空这类平坦区域最大差 123（透明层解析错误的痕迹），
  改动后同一区域差 **0**；剩余差异只在几何边缘（抗锯齿）。
- `Tools/Validate-Vulkan.ps1`：4K 4x 双 Subpass、1x 双 Subpass、4x OIT、4x 动态渲染回退
  四种组合均为 **0 VUID、0 sync hazard、0 Vulkan error**；4K 4x GPU 辅助验证 **0 VUID、
  0 hazard**。
- 详见 `sdl3_reports/2026-10-24-subpass-composite/README.md`。

**硬件分级建议**

设置界面「高级设置 → 硬件预设」是一个与其他选项一致的下拉菜单，四个档位依次设好分辨率、
MSAA、半透明排序、贴图过滤（分辨率按当前显示器上限下调，下一局生效）：

| 档位 | 分辨率 | MSAA | 半透明排序 | 贴图过滤 |
| --- | --- | --- | --- | --- |
| 核显 / 低配 | 1080p | 1x | 原版双层 | 锐利 |
| 入门独显 | 1080p | 2x | 原版双层 | 锐利 |
| 中端独显 | 2K | 2x | 逐像素 OIT | 锐利 + 4x 各向异性 |
| 高端独显 | 4K | 4x | 逐像素 OIT | 锐利 + 4x 各向异性 |

- 核显档的收益来自：双 Subpass 片上合成不存/不 resolve 三张 MSAA 附件、合成阶段
  `discard` 掉大部分无半透明像素、1x 零拷贝；画面锐度来自四边形逆双线性插值 + 导数 mip，
  均不依赖 MSAA。
- 中端档开始用逐像素排序：2K 下 A-Buffer 约 118 MB。帧率不够时先降回原版双层，
  再降 MSAA。
- 需要分清两件事：**分辨率是窗口/渲染尺寸（成本）**，**点对点由「整数倍缩放」决定**
  （它取 496×384 的整数倍，与窗口大小无关）。全屏时渲染尺寸就是显示器分辨率，
  分辨率这一项只在窗口模式下起作用。

**顺手修复：整数倍缩放在小窗口下把 4:3 拉成 16:9**

- 旧逻辑只用**面板**算倍数（4K 面板 = 5x = 2480×1920），随后把宽、高**分别**裁到窗口：
  1920×1080 的窗口会得到 1920×1080 的可见区，4:3 画面被横向拉伸。
- 现在倍数取「面板」与「窗口」两者中较小的那个：同一组合得到 2x = 992×768 居中，
  宽高比与像素对齐都不变。混合 DPI 下窗口大于面板的情况不受影响（面板倍数本来就小）。
- 日志可核对：`Video: integer scaling 2x - visible area 992x768, panel 3840x2160 in a 1920x1080 window.`

**设置界面的下拉框宽度改为按内容自适应**

- 之前两套规则：手写页固定 `220 px`，通用 `CreateControls` 取可用宽度的 55%。后果是
  同一页面里“关闭”这种短选项占一大块空白，而「档位」「贴图过滤」「半透明排序」的长选项
  被截成“Sharp + 4x anis…”。
- 新增 `ComboRow()`，所有下拉框（含通用控件）走同一套：宽度 = 最长选项 + 内边距，
  下限 120、上限整行；标签放不下时先缩小控件，仍放不下就换到控件上方；标签改为手动
  绘制，`##id` 保持不变。
- 「统计面板」配色下拉框现在位于「显示帧率」与「统计面板」之间，并在统计面板未勾选
  时置灰；悬停仍能看到说明。
- 为了让这一行在 1280 宽下与三个勾选框并排，英文预设名缩成单词（Amber / Slate /
  Contrast / Outline，完整名称写进 tooltip），英文的「Statistics panel」也缩短为
  「Stats panel」。中文标签不变。
- 覆盖范围：常用页（语言、显示设备、分辨率、输出显示器、呈现刷新率）、核心页（全部
  通用下拉框、硬件预设、MSAA、贴图过滤、半透明排序）、画面页（CRT 色彩、2D 放大、
  机柜背景、CRT 滤镜/点阵类型）、控制页（输入系统、手柄型号、键位预设、起始视角）。
- 截图：`sdl3_reports/images/2026-10-24-combo-widths-min-disabled.png`（最小窗口、未勾选
  统计面板）、`2026-10-24-combo-widths-min-enabled.png`（勾选后）、
  `2026-10-24-combo-widths-core.png`（核心页）、`2026-10-24-combo-widths-video.png`（画面页），
  以及中文界面的 `2026-10-24-combo-widths-zh-min-disabled.png` / `2026-10-24-combo-widths-zh-video.png`。

**最小窗口 1280x720、默认 1280x800（跨显示器不再保留上一块屏的下限）**

- **默认窗口仍是 1280x800**，但**最小尺寸回到 1280x720**：布局真正能渲染的下限是 720，
  而 1080p 屏在 150% 缩放下逻辑工作区只有 1280x720，把 800 当成最小高度在那块屏上
  根本放不下。现在 1280x800 只在屏幕放得下时作为首次启动的舒适尺寸，用户可以缩到
  1280x720；更小的屏幕由下面的工作区封顶继续下调。
- **下限改成按“当前显示器”重算**：之前只在创建窗口时算一次，拖到 100% 的 1080p 屏后
  Win32 仍按 150% 屏的 1920x1080 物理像素限制，窗口比屏幕还高。现在每帧比较
  `GetWindowDisplay()` / content scale / 工作区，一变就重算，并把已经超出新工作区的
  窗口拉回来（最大化窗口交给窗口管理器，不动）。
- **跨 DPI 时界面也会重新缩放**：`RescaleUi()` 在 `NewFrame()` 之前重设样式
  （`ApplyUiTheme()` + `ScaleAllSizes(newScale)`）并设置 `FontSizeBase = 18 × newScale`；
  ImGui 1.92 会按需动态烘焙字形，不需要重建字体图集。同时把窗口重置为 1280x800 逻辑
  尺寸（再按新 DPI 缩放、夹到工作区），所以从 150% 屏拖到 100% 屏不会留下一个“占满
  屏幕”的窗口，也不会把 1.5× 的控件硬塞进 100% 的屏幕。
  截图：`sdl3_reports/images/2026-10-24-ui-scale-100.png`（1.0×、1280x800 窗口）。
- **macOS**：`GetDPIScale()` 固定返回 1.0（窗口坐标是 point，Retina 由 ImGui 的
  framebuffer scale 处理），所以 `RescaleUi()` 不会触发，界面在 Retina / 非 Retina 之间
  保持同样的 point 尺寸，这是 macOS 的正常行为。跨显示器的最小尺寸 / 工作区封顶逻辑
  照常生效。SDL3 的 Cocoa 后端没有实现 `SDL_GetWindowBordersSize`，所以边框为 0 时
  `GetWindowBordersSize()` 按标准 28 pt 标题栏补一个上边框，避免窗口底边钻到 Dock 下面。
- **工作区上限扣除窗口边框**：新增 `OSD::Platform::GetWindowBordersSize()`（SDL2/SDL3），
  最小尺寸是客户区，之前直接拿工作区当客户区上限，1080p 屏上再加上标题栏就会多出
  约 56 px。现在按“工作区 − 边框”封顶；日志会打出工作区与边框：
  `GUI minimum window size: 1920x1080 (scale 1.50, work area 3840x2088, borders 11/45/11/11).`

**Release notes**

- zh：默认半透明路径改用双 Subpass 片上合成：三张 4K MSAA 附件不再逐 Priority 存取与
  resolve，4K 4x 下快约 6%，核显上带宽收益更大。同时修复 4x MSAA 下最后一个 Priority 的
  半透明丢失（《Daytona 2》车影/瀑布）。1x 画面与旧版逐像素一致（≤1 LSB）。
- en: The default transparency path now composites the whole 3D scene in a two-subpass render
  pass, so the three 4K MSAA targets are never stored, resolved or re-sampled between
  priorities - about 6% faster at 4K with 4x MSAA and a much larger bandwidth saving on
  integrated GPUs. Also fixes translucency being lost for the last priority under 4x MSAA
  (Daytona 2's car shadows and waterfall). The 1x picture is pixel-identical to the previous
  build within 1 LSB.
- zh：设置界面的下拉框改为按内容自适应宽度：短选项（如“关闭”）不再占满一行，硬件预设、
  贴图过滤、半透明排序等长选项也不再被截断；标签放不下时先缩控件，仍放不下就换到上方。
  「统计面板」配色下拉框移到「显示帧率」与「统计面板」之间，统计面板关闭时置灰；
  默认窗口 1280x800、最小窗口 1280x720，跨显示器时按新 DPI 重算尺寸并重新缩放界面
  （拖到 100% 屏会回到 1.0× / 1280x800，不会占满屏幕或保留上一块屏的物理下限）。
- en: Combo boxes in the settings window now size themselves to their contents - short entries
  such as "Off" no longer stretch across the row, and long ones (hardware tier, texture
  filtering, transparency sorting) are no longer clipped. When the label does not fit beside
  the box it shrinks first and then moves above it. The stats-panel colour picker now sits
  between "Show frame rate" and "Stats panel" and is greyed out while the panel is off; the
  default window is 1280x800 with a 1280x720 minimum. Moving the window to another monitor
  re-scales the interface and resets it to that default size for the new DPI, so a 150% -> 100%
  move no longer leaves an oversized window or oversized controls on the smaller screen.

### Vulkan：可选的逐像素半透明排序（AlphaSortMode = 1）

**现象**

- 原版 `R3DFrameBuffers` 只有 trans1 / trans2 两张半透明附件，每层每个像素只保留一个
  半透明面，最后由合成器按硬件的两层公式混合。同一像素超过两层（尾烟叠加、水花、
  树叶、玻璃）时多出的层直接消失；同一层内多边形提交顺序与深度相反时，前后颈倒。
  开启 MSAA 后 `gl_FragDepth` 又让硬件 Early-Z 完全失效，半透明片元无论是否被挡都
  会跑完整个着色器。

**方案**

- 新增 `AlphaSortMode`（ini 与「高级设置 → 半透明排序」，0/1，默认 0）。
  `0` 保持原有双层 MRT 路径，逐位不变；`1` 启用逐像素排序，需要重开一局生效。
- **每个 Priority 内部解析，而不是整帧末尾**：颜色层画完后，半透明片元进入 A-Buffer，
  本 Priority 结束时立即排序并合成进颜色目标。这样下一个 Priority 的 Depth Clear 与
  不透明几何天然覆盖前一层的结果，不需要额外的“不透明擦除 OIT 节点”规则，跨
  Priority 遮挡语义与原机一致（VF3 的海面仍然被高 Priority 的角色挡住）。
- **固定槽位 A-Buffer（SoA），不是链表**：每像素最多 4 个节点，8 字节/节点
  （`packUnorm4x8` + 32 位复合键），槽位 `s` 的像素 `p` 位于
  `s * pixelCount + p`，相邻像素在 Warp 内访问相邻字，L2 合并访问。计数是一张
  `r32ui` 图，用 `imageAtomicAdd` 在**本像素自己的字**上加一，没有全局分配计数器。
- 复合键 = 深度（float bits 的高 20 位）| 层序（2 位）| 采样覆盖掩码（8 位）。
  同深度时 trans1 排在 trans2 之上，与旧版图层绘制顺序和合成权重一致；排序以深度
  为主键，不按图层分块。
- **Capture PSO**：`early_fragment_tests` + 不写 `gl_FragDepth`（该投影下
  `nearVal * gl_FragCoord.w` 与 `gl_FragCoord.z` 恰好相等），因此被同 Priority 不透明
  几何挡住的半透明片元在着色前就被剔除，绝不分配槽位；`depthWriteEnable = FALSE`
  （排序不再需要“最近者胜”），`sampleShadingEnable = FALSE`、
  `alphaToCoverageEnable = FALSE`（每像素只插入一次）。pass 只绑 depth，且
  `storeOp = NONE`；`ShadeFragment` 已有的 `alpha < 1/32` 剔除继续生效。
- **Resolve PSO**：只绑颜色附件 0（透明附件与 depth 都不绑），无深度测试，
  `ONE / ONE_MINUS_SRC_ALPHA` 预乘 source-over，一次混合完成整张排序后的列表。
  MSAA 下按采样执行（`sampleRateShading` 可用时），用节点里的覆盖掩码逐采样过滤，
  保留半透明边缘的抗锯齿；不可用时回退为逐像素。
- 头文件/着色器共享 `ShadeFragment()`（`real3d_common.glsl`），两条路径的着色、雾、
  剔除规则不会漂移。
- 每帧每个含半透明的 Priority 只多做两次最小 pass + 一次 33 MB 的计数图清零；
  4K 下 A-Buffer 为 4 × 3840 × 2160 × 8 ≈ 253 MB，显存不够时自动回退双层路径。

**性能（3840×2160 + MSAA 4x，RTX 5060 Ti）**

- VF3 海滩（静态、大面水面/影子）：双层 120 Hz / render 0.74–0.82 ms；OIT 120 Hz /
  render 0.76–0.90 ms。
- Daytona 2 吸引模式比赛（瀑布占大半屏，2300 draws）：双层 120 Hz / render 0.78 ms；
  OIT 120 Hz / render 0.67–0.77 ms。
- 中间实现记录：第一版 Per-Pixel Linked List 在同样场景只有 27–37 Hz。逐段关掉
  插入代码后恢复 120 Hz，最终定位到两件事：全局 `atomicAdd` 计数器被放在
  **HOST_VISIBLE** 内存里（每次分配走系统内存/PCIe，约 20 ms/帧），以及链表指针
  追逐 + 动态下标局部数组。改成设备本地内存 + 固定槽位 SoA 后完全消失。
  详见 `sdl3_reports/2026-10-24-alpha-sort/README.md`。

**验证**

- `Tools/Validate-Vulkan.ps1` 等价命令（VF3，4K + MSAA 4x，`SM3D_ALPHA_SORT=1`）：
  同步验证 **0 VUID、0 sync hazard**；GPU 辅助验证 **0 VUID、0 hazard**。
- 临时红色调试输出确认捕获范围精确覆盖 VF3 的海面与角色影子，高 Priority 的角色
  本体不被覆盖（说明跨 Priority 遮挡正确）；截图见报告目录。
- 双层路径（`AlphaSortMode=0`）回归：同一存档画面与帧率不变。

**已知限制**

- 每像素最多 4 层；超过的片元被丢弃（安全失败）。这已覆盖原机 2 层的限制，但不是
  无限层。
- 逐采样解析只能拿到光栅化覆盖掩码，拿不到“深度测试通过的采样掩码”，因此在不透明
  物体轮廓处、半透明面跨深度边界时，被挡住的采样仍可能被合成（细微光晕）。
- 计数图每个 Priority 清零一次；场景有多个含半透明的 Priority 时，这是本模式相对
  双层路径主要的固定开销。

**Release notes**

- zh：新增可选的「半透明排序」模式：逐像素收集并按深度排序半透明片元，解决超过两层
  重叠时的吞噬与提交顺序颠倒；4K + MSAA 4x 下与双层路径同帧率。默认关闭，保持原版
  逐位兼容。
- en: Added an optional sorted-transparency mode that collects translucent fragments per
  pixel and composites them back to front, fixing the two-layer ceiling and wrong
  submission order. It runs at the same frame rate as the two-layer path at 4K with 4x
  MSAA. Off by default for bit-exact compatibility.

### Vulkan：Daytona 2 天空里的黑块（预乘 Alpha 除零）

**现象**

- 《Daytona USA 2 Power Edition》混合关卡后半程，天空里出现纯黑四边形（灯柱旁、
  POSITION 面板右侧）。选择「贴图过滤 = 锐利 + 4x 各向异性」时出现，切回「硬件」
  后消失。

**根因**

- 各向异性路径把多个 tap 按预乘 Alpha 累加后反预乘（`rgb = sumRgb / sumA`）。
  Model 3 的 T1RGB5 纹素把透明度放在 bit15，而**材质可以不声明任何 alpha 用法**
  （既无 `kTextureAlpha` 也无 `kAlphaTest`），这种材质最后会被强成 `alpha = 1`。
- 纹理所有 tap 的 `a == 0` 时 `sumA == 0`，除法得 0 → 颜色全黑，alpha 又被强成 1
  → 不透明纯黑四边形。单 tap 路径（模式 0/1）从不做这个除法，所以只有模式 2 中招。

**修复**

- 多累计一个非预乘和（`plainRgb`），仅在 `sumA` 太小时使用，回到单 tap 会得到的
  结果；正常足迹仍按 alpha 加权。每 tap 多 3 次加法。

**顺手修复**

- `RegionCoverage()` 在 `BuildDrawList()` 里调用，而上传发生在它之后：游戏本帧刚写
  的纹素仍停在 `m_dirty` 里，于是被报成「写了但没到 GPU」。现在「正排队的写入」不算
  缺失，同一会话的误报从 12 行降到 0 行。

**验证**

- 同一存档、同一 `-At`、3840×2160 + MSAA 4x，天空区域暗像素：
  模式 0 = 2766、模式 1 = 2994、模式 2 修复前 = 9309、修复后 = **2766**（与改动前
  Vulkan 2783、New3D 2784 一致）。
- 各向异性的锐度收益保留：同帧路面细节 3.24（模式 0）→ 3.85（模式 2）。
- 同步验证：0 VUID、0 hazard。
- 详见 `sdl3_reports/2026-10-24-13-dayto2pe-black-quads.md`。

**Release notes**

- zh：修复「锐利 + 4x 各向异性」贴图过滤下，Daytona USA 2 天空偶尔出现黑色多边形
  的问题（全透明纹素的颜色被除零算成了黑色）。
- en: Fixed black polygons appearing in Daytona USA 2's sky with the "Sharp + 4x
  anisotropic" texture filter (fully transparent texels were divided by a zero
  alpha sum and came out black).

### 输入：Lost World 鼠标光枪准星与游戏光标对齐（ADC 校准）

**现象**

- 用鼠标玩《The Lost World》（`lostwsga` / `lostwsgp` / `lostwsgo`）时，模拟器的
  准星总落在游戏自绘光标（红点）的**右下方**，鼠标越靠右下偏得越多。

**根因**

- 光枪硬件上报的是 8 位 ADC，但游戏拿到值后会再套一层**内置的固定校准**才换算屏幕
  坐标，校准行程比 0–255 窄（实测 X 约 15..222、Y 约 5..217，Y 反向）。
- 旧实现把整条 0–255 当成可用行程：鼠标只能扫到画面靠左上的一小块，准星却按 0–255
  归一化到整屏，于是准星跑到游戏光标右下方。

**修法**

- `CAxisInput::SetRange(min, off, max)` + `CInputs::ApplyGameSpecificInputRanges()`：
  对这三个 set 把 `analogGunX` 设成 `15,119,222`、`analogGunY` 设成 `250,144,38`
  （Y 反向，对应游戏 ADC 5..217）。`ConfigureInputs()` 在 `LoadFromConfig` 之后调用。
- 准星路径（Vulkan `UpdateOverlayCrosshair()`、OpenGL `CCrosshair::Update()`）统一改用
  `ValueAsFraction()` 归一化，与输入走同一套范围，鼠标即可从画面左缘扫到右缘，
  准星与游戏光标重合。

**SDL3 说明**

- SDL3 后端把鼠标换成浮点子像素坐标 + 按设备 ID 的 RawInput（`GetMouseAxisValueFloat`），
  这只是「鼠标坐标 → ADC」的输入链路变了；校准常量（15/119/222、250/144/38）与
  `Model3.cpp` 里 Lost World 的 Y 轴反转（`255 - value`）都原样保留，校准不受影响。
- 验证：150% DPI 下 3x 原生分辨率窗口模式、5x 原生分辨率全屏均对齐；`SM3D_GUN_PROBE=1`
  打印每个采样点的 ADC 值与最终准星像素，用于排查。

**Release notes**

- zh：修复《The Lost World》鼠标光枪准星与游戏光标错位。光枪 8 位 ADC 会被游戏再套
  一层固定校准，行程窄于 0–255；现在按校准后的行程（X 15..222、Y 5..217 反向）缩放
  鼠标，准星与游戏自绘光标重合，150% DPI、3x 窗口 / 5x 全屏均已验证。
- en: Fixed the mouse light-gun crosshair drifting away from the game's own cursor
  in The Lost World. The gun's 8-bit ADC is run through a second fixed calibration
  inside the game, so its travel is narrower than 0-255; the mouse is now scaled to
  the calibrated travel (X 15..222, Y 5..217 inverted), lining the emulator's
  crosshair up with the game cursor. Verified at 150% DPI in both 3x windowed and
  5x fullscreen.

## 2026-10-23

### Vulkan：可选的屏幕空间贴图 LOD（含 4x 各向异性）

**现象**

- 四边形插值之后，着色器已经有逐像素精确的 `uv`，但 mip 层级仍然是 New3D 的径向公式
  `-log2(gl_FragCoord.w² · lodBase)`。它在构造上是各向同性的，只看多边形离镜头多远，
  不看它斜到什么程度，所以远处斜掠的路面、墙壁比它本来能支持的细节更糊。

**方案**

- 新增 `TexFilter`（ini 与「高级设置 → 贴图过滤」，0/1/2）：
  - `0`（默认）硬件径向，与之前完全一致；
  - `1` 用 `dFdxFine(uv)`/`dFdyFine(uv)` × 基级贴图尺寸得到像素足迹，取**长轴**
    `log2(major)` 作为层级（不产生闪烁）；
  - `2` 取**短轴** `log2(minor)`，并沿长轴最多 4 个 tap；`ratio > 4` 的部分折算回
    层级（`+ log2(ratio/used)`），避免 20:1 斜掠面只采 4 点出现条纹。
- 足迹椭圆用 2×2 矩阵 `[dx dy]` 的奇异值算：`a = dx·dx`、`b = dy·dy`、`c = dx·dy`，
  特征方程 `λ² − trace·λ + det = 0` 直接给两根，长轴方向取较大特征值的特征向量。
- 采样仍走原来的手工 `texelFetch` 打包贴图页解码，只是把层级从 `comp 参数` 换成
  足迹，并把 `texBiLinear` 包一层 `taps` 平均；微纹理路径保持单 tap。
- 模式通过 `ViewportUBO` 新增的 `texFilter` 传递（64 → 80 字节），Init 时读一次，
  与 MSAA 一样需要重新开一局。

**验证**

- 同步验证（模式 2，4K + MSAA 4x）：0 VUID、0 sync hazard。
- 画面（Daytona 2 远墙/楼牌区域）：高频能量 9.38（模式 0）→ 10.46（1）→ 12.08（2），
  目视楼牌与栏杆明显更清楚；对比图
  `sdl3_reports/images/2026-10-23-texfilter-compare.png`。
- 开销：4K + MSAA 4x 下 `render` 0.80 → 0.81 → 0.85 ms/frame。
- 默认仍为模式 0，未开启时与改动前逐像素一致。

**采样路径的打磨**

- 多 tap 改为**预乘 Alpha 累加**（`sumRgb += rgb·a`，只除一次），避免透明纹素的
  黑色背景污染斜看的镂空边缘；单 tap 路径保持原样逐位不变。
- tap 数按长宽比分档：<1.35 取 1、<2.5 取 2、其余最多 4；放大区（lod≤0）因此回到
  “1 tap × 单层”。三线性因子 <1/256 时不再采第二层。
- 导数跨四边形边时用一个单侧 clamp（`lod = min(lod, radialLod + 8)`）封住糊边；
  这个模式的收益方向相反，所以单向封顶不损失真实收益。
- 解析导数方案已评估但**不采用**：需要额外 4 次 mat4·vec4 + 约 120–150 FLOP，
  而现状只是 2 条 `dFdxFine` 级别的指令，收益限于一像素边界；评估写在
  `sdl3_reports/2026-10-23-12-quad-interpolation.md`。

**Release notes**

- zh：新增可选的贴图过滤设置（高级设置 → 贴图过滤）。默认仍是原机行为；选「锐利」
  或「锐利 + 4x 各向异性」后，远处斜掠的路面和墙壁会明显更清晰。4K 下约多花
  0.05 ms/帧。
- en: New optional texture filtering (Advanced → Texture filtering). The default still
  matches the real hardware; "Sharp" and "Sharp + 4x anisotropic" keep distant roads and
  walls seen at a grazing angle noticeably clearer, at about 0.05 ms/frame at 4K.

### Vulkan：四边形插值（Pull-Model SSBO + 逆双线性，无几何着色器）

**现象**

- Model 3 的原生光栅基元是四边形。Vulkan 后端把每个四边形拆成 `0,1,2 / 0,2,3`
  两个三角形，交给硬件重心坐标插值：四个角不共面（或屏幕投影不是平行四边形）时，
  UV、法线、着色沿 0-2 对角线出现折痕。OpenGL 的 `-quad-rendering` 用几何着色器
  解决，但几何着色器在移动/Intel 驱动上不再是可靠选项，也与“零几何开销”相悖。

**方案**

- **数据**：`CopyVertexData()` 改成每图元固定 4 个槽位（三角形把顶点 2 复制到槽位 3），
  四边形顶点数 6 → 4（省 1/3 显存与上传量）。`GpuQuadVertex` 重排为 4 组 16 字节，
  std430 与 C 布局完全一致：`pos`、`normal + textureNP`、`uv + fixedShade`、
  `faceNormal + colour`（64 字节，不变）。
- **管线**：`VkPipelineVertexInputStateCreateInfo` 全空，顶点数据由着色器从 SSBO
  拉取；拓扑固定 `TRIANGLE_LIST`，没有几何/细分阶段。索引缓冲是一块 192 KB 的
  固定 `0,1,2,0,2,3` 模式（16384 个四边形展开），一次 `vkCmdDrawIndexed` 画完一个 mesh。
- **顶点着色器**：只读自己那一个角点算 `gl_Position`，输出 `flat out uint vQuadBase`
  与 `centroid out vec3 vView`。ROM / 多边形 RAM 两个 SSBO 由**特化常量**选择，
  管线编译期定死，不用运行时分支。
- **片元着色器**：从同一 SSBO 拉四个角点（同段 256 字节，L1 广播），以 `vView` 为
  视线在**视图空间**做射线 × 双线性曲面求交，得到四边形自己的 (s,t)，再对 uv、法线、
  视点坐标做双线性混合。透视已在 (s,t) 里，不需要再除以插值的 1/w。
- **数值**：平行四边形 / 近平行四边形 / 三角形都走线性分支（`k2` 恰为 0，三角形是
  `g == -e` 的三重积）；二次分支用 Citardauq 形式避免有效位抵消；两个根都是真实交点，
  用“是否落在单位方形内 + 深度是否一致”挑选；最后夹紧到 [0,1]，MSAA 轮廓片元的
  UV 不会越界。深度仍用光栅器的 `gl_FragCoord.w`，保证 decal 不与地面 z-fighting。
- **mip**：仍由 `-log2(gl_FragCoord.w² · lodBase)` 显式给出。这个着色器不经过 sampler
  （手工解码 Real3D 打包贴图页），没有隐式导数要修正，`textureGrad` 不适用。

**验证**

- `Tools/Validate-Vulkan.ps1 -Set daytona2 -Slot 1 -Mode sync`：0 VUID、0 sync hazard。
  （校验层的会话极慢，**不能用来测帧率**。）
- 不带校验层、同存档同配置的 A/B：
  - 3840×2160 + MSAA 4x：119.9 → 120.0 FPS，帧时 8.34 → 8.33 ms；
  - 1488×1152 + MSAA 0：present 119.99 → 120.01 Hz，render 0.79 → 0.69–0.76 ms/frame。
- 四边形顶点上传量降到原来的 2/3（每图元 6 → 4 个 64 字节顶点）。
- 算法用 Python 移植逐点回代：平行四边形 / 梯形 / 斜四边形 / 扭曲四边形 / 退化三角形
  的最大相对误差 ≤ 9e-17。
- 画面：Daytona 2 赛道、岩壁、瀑布、顶棚桁架、HUD 与改前结构一致，逐像素差异集中在
  有贴图的四边形内部（预期）。
- 详见 `sdl3_reports/2026-10-23-12-quad-interpolation.md`。

**Release notes**

- zh：Vulkan 后端改用与 Real3D 硬件一致的四边形插值：不再把四边形拆成两个三角形，
  而是每个图元存 4 个角点、由片元着色器自行求解四边形参数并做双线性插值。贴图、法线
  与着色沿对角线不再出现折痕，同时每个四边形少用 1/3 顶点，性能与改前相同或略好。
- en: The Vulkan backend now interpolates quads the way the Real3D hardware does. Quads
  are no longer split into two triangles: each primitive stores four corners and the
  fragment shader solves the quad's own parameters, so texture coordinates, normals
  and shading no longer crease along the diagonal. Each quad also costs a third fewer
  vertices, and performance is unchanged or slightly better.

### Vulkan：VF3 海水盖住角色、2D 天空消失（三个颜色附件 + 合成 alpha）

**现象**

- 《VF3 Team Battle》沙滩关卡，远处半透明海水画在格斗角色**前面**（腿和裙摆被盖住），
  OpenGL 正常。
- 同一场景的月亮/云/蓝天整个消失，天空纯黑；《The Lost World》天空也变成一块
  扁平土黄色。Sega Rally 2 的天空正常（那是 3D 几何体，不是 2D 背景层）。

**根因一：透明层的“擦除”机制丢了**

- New3D 的 `R3DFrameBuffers` 是**一个 FBO 带三个颜色附件**（opaque / trans1 /
  trans2）。不透明层通过 `R3DShaderCommon.h` 的 `WriteOutputs()` 向 trans1/trans2
  写 `vec4(0)`，把更低 priority 留下的透明像素**擦掉**——硬件没有单独的透明
  pass，这是跨 priority 唯一的擦除机制。
- Vulkan 移植把三个目标做成独立 image、每个 pass 只绑一个附件，于是 priority 1
  的海水写进 trans2 后，priority 2 的角色无法擦除它，帧末合成时海水仍然盖在
  角色上。
- 排查先排除了深度方向：`SM3D_TRANS_DEBUG=block/clearfar` 都改变不了结果，
  `p1` / `p2` 证明海水在 priority 1、角色在 priority 2（每个 priority 都清深度，
  两者根本不共享深度值）。
- 修法：每个场景 pass 绑定全部三个附件，用 pipeline 的 `colorWriteMask` 决定
  写的目标；生成 6 条管线（3 附件 × base/overlay）。颜色层三个 mask 全开，
  片元着色器写 `colour + 0 + 0`；透明层只开自己那个附件。

**根因二：合成器把 3D 场景 alpha 写死成 1**

- VF3 / Lost World 的天空来自 **Render2D Bottom**（496×384 tile 层）。New3D 用
  `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` 把 3D FBO 画到主缓冲，用的是 FBO 自己的
  alpha：清屏 alpha 0、雾 quad 写雾浓度、几何体 alpha 1。
- `composite.frag` 在合成完透明层后写 `..., 1.0`，3D 场景永远不透明地盖住
  2D 底图。天空区域的场景颜色正好是雾色（VF3 黑、Lost World 土黄）。
- 修法：按 New3D 的两次混合折算成一次 source-over，保留场景 alpha；两层透明
  重叠时仍输出 alpha 1（硬件行为：没有像素可混合）。

**工具**

- 新增 `Tools/Capture-Frame.ps1`：载入存档 → 模拟器自己截图（`SM3D_SCREENSHOT_AT`，
  不依赖窗口焦点、独占全屏也能用）→ 关会话 → 报告路径。支持 `-Set/-Slot/-State/`
  `-Exe/-Bin/-At/-Out/-Env/-RawState`。相对路径的 `-State` 会转成绝对路径
  （子进程工作目录是 `-Bin`）。

**验证**

- VF3 沙滩：海水回到角色身后，月亮/云/蓝天恢复，与 OpenGL 参考一致；
  `build-cmake`（MSAA 0）与 `dist`（MSAA 4）都验证。
- 回归：Daytona 2 瀑布、Sega Rally 2、Lost World 几何与 LOD 均正常。
- 完整过程与对照图见根目录 `vulkan_issues.md` 与 `sdl3_reports/images/`。

**Release notes**

- zh：修复 Vulkan 后端两处画面问题：VF3 沙滩的远处海水会盖在格斗角色前面，
  以及 VF3 / The Lost World 的 2D 天空（月亮、云层）整个消失。现在三个场景目标
  在同一个 pass 内作为三个颜色附件渲染，不透明层会正确擦除低优先级的透明像素；
  合成器也保留了 3D 场景 alpha，Render2D 底层背景可以正常透出。
- en: Fixed two Vulkan rendering problems: VF3's distant sea was drawn over the
  fighters, and the 2D sky (moon, clouds) was missing in VF3 and The Lost World.
  The three scene targets are now rendered as three colour attachments of one
  pass, so the opaque layer erases transparency left by lower priority layers,
  and the compositor keeps the scene alpha so the Render2D bottom layer shows
  through.

### Vulkan：同步验证 / GPU 辅助验证 / 显存预算

**新增开发开关**

- `SM3D_VK_SYNC_VALIDATION=1`：LunarG 的 Synchronization Validation，检查
  `WRITE_AFTER_WRITE` / `READ_AFTER_WRITE` / `WRITE_AFTER_READ`，会打印命令缓冲、
  管线阶段和 image layout。
- `SM3D_VK_GPU_VALIDATION=1`：GPU-Assisted Validation，给着色器插桩，抓越界采样
  和描述符访问。
- 两者自动打开 `VkValidation`；校验层不存在时回退并记日志。`DebugCallback` 加了
  1000 条上限，避免系统性错误刷爆日志。

**验证层暴露并修复的四个问题**

1. **swapchain acquire 与首次布局转换的竞态**：提交在
   `COLOR_ATTACHMENT_OUTPUT` 等待 acquire 信号量，而 `PRESENT_SRC/UNDEFINED`
   的转换默认从 `TOP_OF_PIPE` 开始，同步验证每帧报 `WRITE_AFTER_READ`。
   `CmdTransition()` 增加 `srcStageOverride`，swapchain 首次转换显式带上等待阶段。
2. **`independentBlend` 未启用**：三个场景附件的 `colorWriteMask` 不同，没有该
   特性时属于未定义行为（NVIDIA 容忍，其他驱动不一定）。现在
   `CreateDevice()` 要求并启用。
3. **`MIRROR_CLAMP_TO_EDGE` 未启用**：纹理缓存的第 4 种 wrap 组合需要
   `samplerMirrorClampToEdge`；现在按特性启用，不支持时退回 `CLAMP_TO_EDGE`。
4. **纹理解码 compute 管线的描述符布局被提前销毁**：创建 pipeline layout 后立即
   销毁临时 `VkDescriptorSetLayout`，随后 `vkCreateComputePipelines` 引用到已删除
   对象。现在保留到 pipeline layout 销毁之后。

**显存可观测性**

- 启用 `VK_EXT_memory_budget`（可用时）：启动和分配失败打印 heap budget /
  heapUsage / 本会话持有量；退出时打印释放总量。
- 分配器本身不换 VMA：持久映射环 + 子分配已经覆盖现有需求，只有当以后出现
  defragmentation / lost allocation / 预算驱动的降级需求时再引入。

**验证结果**

- VF3 沙滩场景：同步验证 0 hazard，GPU 辅助验证 0 着色器访问错误，画面正常。
- Daytona 2 瀑布场景：同步验证 0 VUID / 0 hazard。
- 校验层 1.4.363.0，层文件只解出到 `%LOCALAPPDATA%`，用 `VK_LAYER_PATH` 指向，
  不修改系统安装。用法见 `vulkan_issues.md` 第 6 节。

**Release notes**

- zh：Vulkan 后端新增开发用的同步验证 / GPU 辅助验证开关，并修复了校验层发现的
  三个兼容性问题（swapchain 布局转换竞态、未启用 `independentBlend`、未启用
  `MIRROR_CLAMP_TO_EDGE`）；NVIDIA 之外的驱动会更可靠。新增显存预算日志，分配
  失败时能看出是显存不足还是碎片。
- en: Added opt-in synchronization and GPU-assisted validation switches, and fixed
  three portability problems they found (a swapchain layout-transition race,
  `independentBlend` and `MIRROR_CLAMP_TO_EDGE` not being enabled). Added
  `VK_EXT_memory_budget` logging so an allocation failure says whether the heap is
  full or fragmented.

### 外设：Razer Onza 与 Xbox 360 无线手柄

**识别结果**

- SDL3 直接枚举到两块手柄（`Input: 2 game controller(s) reported by SDL.`）：
  - `XInput Controller #1` —— Razer Onza Tournament Edition（有线）。SDL 的 XInput
    后端拿不到产品名，所有有线 360 类手柄都报这个通用名；6 轴 / 11 键 / 1 hat，
    支持 rumble。
  - `Xbox 360 Wireless Controller` —— 6 轴 / 11 键 / 1 hat，支持 rumble。
- 无线手柄闲置会休眠：本次测试中途设备数从 2 掉到 1，按一下手柄就回来。
- 日志新增每个设备的开场行（名字、轴/键/hat 数、是 game controller 还是原始
  joystick、有没有 rumble）。以前“手柄没反应”完全看不出 SDL 到底有没有看到它。

**修掉的问题：标准布局把手柄扳机绑到了整轴**

- 现象：每次启动两条
  `Unable to map input Trigger2 to [JOY2_RZAXIS] - switching to default [NONE]`；
  光枪游戏里手柄扳机不工作，P2 更是完全没绑定。
- 原因：`Trigger` / `Offscreen` / `AnalogTriggerLeft` / `AnalogTriggerRight` /
  `AnalogJoyTrigger` 都是**开关**输入，只能接轴的一半
  （`JOY1_RZAXIS_POS`）。标准布局写的是整轴 `JOY1_RZAXIS`，解析成
  `SourceFullAxis` 后被丢弃。P1 还带着键盘和鼠标的备用源，所以只表现为“手柄
  扳机没用”；P2 只有这一个源，于是直接变成 `NONE` 并报错。
- 修法：`s_standardMappings` 里这 8 个键改成 `_POS`（LT = `ZAXIS_POS`，
  RT = `RZAXIS_POS`）。新增 `RepairSwitchAxisMappings()`：加载 ini 时把旧版本写
  下的整轴映射就地改成 `_POS` 并记日志——否则设置界面会把旧值原样写回去。

**新增 `SM3D_PAD_PROBE=1`**

- 每个手柄打开时震一下（400 ms），之后按变化打印按键/hat/轴：
  `Input: pad 1 raw button 3 down.`、`Input: pad 1 Y/North down.`、
  `Input: pad 1 RT = 1.00.`。raw 编号和标准键名同时给出，正好用来判断
  “设备报了但映射错了”还是“设备根本没报”。
- 轴只在变化 ≥0.25 时记录，摇杆慢慢推到死区不会刷屏。
- `Scripts/PadProbe.ps1` 按当前 ini 的 `ROMPath` 启动一局，方便手动测。
- `DetectController()` 把 `XInput Controller #N` 归类为 Xbox 360，设置界面的
  “手柄型号”不再把它显示成“其他 PC 手柄”。

**Release notes**

- zh：手柄日志现在会写下每个设备的名称、轴/键数量和 rumble 支持；修复标准
  布局把光枪/模拟枪的扳机绑到整轴、导致手柄扳机无效（P2 直接变成 NONE）的
  问题，旧 ini 会自动修正。新增 `SM3D_PAD_PROBE=1`，按键时同时打印原始编号和
  标准键名，便于排查手柄映射。
- en: The log now lists every opened controller with its name, axes/buttons and
  rumble support. Fixed the standard layout binding the gun triggers to a whole
  axis, which left pad triggers dead (and P2 unbound); existing .ini files are
  repaired on load. `SM3D_PAD_PROBE=1` prints each press with both the raw button
  number and the standard name, which makes a mapping problem visible.

### 退出暂停中的游戏后，下一局全程没有声音

**现象**

- 在游戏里存了档（`dayto2pe.zst1`），退出到设置界面，再从「管理存档」按「读取」
  进去：画面正常、力反馈正常，但**一点声音都没有**。重启程序后再载入同一份存档
  就有声。
- 日志里没有任何音频错误：`Audio: buffer 400 ms`、`767 samples per frame`、
  under-run 只有 2–3 次。因为缓冲区确实照常在写、在播，只是**送到声卡前被静音
  了**。

**根因：`enabled` 是进程级的，会话不是**

- `Src/OSD/SDL/Audio.cpp` 的输出使能是文件级 `static bool enabled`。
- 打开游戏内面板（Tab）会自动 `PauseThreads()` + `SetAudioEnabled(false)`，
  面板里的保存也发生在这个暂停状态；此时若直接按 Esc / 关窗口退出，配对的
  `SetAudioEnabled(true)` 永远不会执行。
- `CloseAudio()` 只关设备、`OpenAudio()` 只开设备，两者都不复位这个标志。
  设置界面在**同一个进程**里跑下一局，于是新会话从 `enabled = false` 开始：
  混音器正常工作、缓冲区健康，唯独 `MixOut()` 走 `out.Silence()` 分支。
- 这也解释了为什么“退出整个程序再进”就好了——进程重启把标志带回 `true`。

**修法**

- `OpenAudio()` 开头把 `enabled` 复位为 `true`：会话总是从有声开始。如果发现
  它是上一局留下的 `false`，日志写明
  `Audio: output was still muted from the previous session; re-enabling.`。
- `CloseAudio()` 结束时同样复位，任何退出路径（含出错收尾）都不会把静音带出
  会话。
- `SetAudioEnabled()` 只在状态真正翻转时打一行 `Audio: output muted.` /
  `Audio: output unmuted.`。以后再遇到“没声音”，日志能直接区分“游戏没发声”
  和“输出被静音”。
- 新增诊断钩子 `SM3D_AUDIO_LEVEL=1`：每秒一行
  `Audio: source peak <源> , mix peak <混合后>, output on|MUTED, under-runs N.`。
  `source peak` 取 SCSP+DSB 混音后的原始电平，`mix peak` 取经过音量/平衡和
  软启动之后的最终输出，两者一起就能把“存档里机器是哑的”和“机器有声但输出
  被关掉”分开。

**验证**

- 全新进程载入**用户那份** `dayto2pe.zst1`（`build-cmake` 的 SDL3 目标，
  `Scripts/run-log.ps1`）：`Audio: source peak 19471.3, mix peak 0.5942,
  output on`，连续 6 秒稳定在 0.49–0.66 满量程。存档本身没有问题：SCSP、
  DSP、DSB2 的 MPEG 播放位置都恢复了。
- 因此根因只能是输出使能，而不是存档缺字段、DSB 声道路由或 `SoundVolume`
  被读成 0（日志里 `SoundVolume=70`，与 `ApplyPerGameAudioBalance()` 一致）。
- 端到端复现（新增 `Scripts/gui-drive.ps1`，用真实鼠标路径驱动设置界面）：
  1. `SM3D_SAVE_MANAGER=dayto2pe` 打开管理存档 → 点「读取」→ 游戏内 Tab 开面板
     （自动暂停 + 静音）→ Esc 退出到设置界面；
  2. 在游戏列表里双击 `dayto2pe` 再开一局。
  - **去掉复位代码时**：第一局 `Audio: output muted.`；第二局一进来就是
    `Audio: output was still muted from the previous session`，全程
    `source peak 8458.1, mix peak 0.2581, output MUTED` —— 机器有声、输出被关，
    正是用户描述的现象。第二局是**全新开局**（不是读档），说明标志与会话怎么
    开始无关，只看上一局怎么结束。
  - **加上复位代码后**：第二局没有任何 mute 记录，开局后
    `source peak 19788.7, mix peak 0.6039, output on`，与第一局一致。
- `Scripts/run-log.ps1` 顺带修了带空格的路径没加引号：`-State` 会被 PowerShell
  拆成 `E:/Dev/Sega` + `Model` + `3`…，表现为 `Could not open 'Model'`。

**Release notes**

- zh：修复“在游戏内面板里存档后退出，再从设置界面读档进入却完全没有声音”。
  音频开关是进程级的，面板自动暂停设下的静音在关闭游戏时没有复位，于是同一个
  进程里的下一局一直静音；现在每次开局都会强制恢复有声。日志新增静音/取消
  静音的记录，以及 `SM3D_AUDIO_LEVEL=1` 的电平探针，便于区分“游戏没发声”和
  “输出被静音”。
- en: Fixed a session that started with no sound at all after quitting a game while
  the in-game panel had it paused. The mute flag is process-wide and was not cleared
  when the session ended, so the next game launched from the settings window played
  silently; every new session now starts audible. The log records mute/unmute
  transitions, and `SM3D_AUDIO_LEVEL=1` prints the mixer level so a silent game can
  be told apart from a muted output.

### 存档管理：区域版本提示与打开面板自动暂停

**一、"游戏里存了，Manage saves 里看不到"**

- 现象：在 Scud Race 里保存后，in-game 菜单的 slot 6/7 能看到存档，但回到设置
  界面的「管理存档」却是空的。
- 表层原因：存档按 ROM set 名落盘（`<set>.zst<slot>`），管理窗口按**当前选中
  的 set** 前缀过滤。
- 真正的根因：**set 是按 zip 里的 CRC 识别出来的，不是按 zip 文件名**
  （`GameLoader::ChooseGameInZipArchive()`）。用户的 `scud.zip` 里装的是
  `epr-19731..19734`，正是 `scudau`（Australia）的 CROM；而 `scuda.zip` 里才是
  Export 版（`epr-19688..19691`）的完整 ROM。所以运行 `scud.zip` 时实际跑的是
  `scudau`，存档自然叫 `scudau.zst6/7`。列表里那一行却按文件名显示成
  "Scud Race (Export)"，两边对不上。
- 修法（不改存档格式、不跨 set 自动加载——不同 set 的状态布局可能不同）：
  - `GameLoader` 新增 `Identify()`：只读 zip 中央目录做 CRC 比对，不加载任何
    ROM 数据，回答"这个 zip 实际是哪个 set"。
  - `OpenManageSaves()` 先用它解析当前行对应的 zip，再按解析结果列存档。
    于是从 `scud` 那一行打开，也能看到 `scudau` 的卡片。列表行与 set 名分开
    保存（`g_manageSavesRow` / `g_manageSavesGame`），「读取」按钮仍按行号选
    游戏，不会因为解析后名字变了而选不中。
  - `GameTitleForSet()` 回落到 `Games.xml` 的定义，`scudau` 这种没有独立 zip
    的 set 也能显示 "Scud Race (Australia, Twin/DX)" 而不是裸名字。
  - 日志新增两条：`'scud' holds the ROMs of set 'scudau' (...)` 和
    `Game: '...\scud.zip' resolves to set 'scudau' (...)`，以后看日志就能区分
    "没存档"和"开错了 set"。
  - 游戏窗口标题补上区域版本：`Scud Race (Australia, Twin/DX)`。这是运行中
    唯一能看到实际版本的地方，否则按文件名启动的假象永远存在。
  - 管理窗口为空时仍给出可诊断提示：当前 set 有几个旧版 `.st`；存档目录里
    其他 set 各有多少个 `.zst`；并提示在游戏列表里右键实际玩的那个版本。
  - 旧版 `.st` 仍通过「导入旧版 .st」转换；转换后沿用原槽位号（`.st6` → `.zst6`）。

**二、打开 in-game 面板时自动暂停**

- Tab 打开面板时自动 `PauseThreads()` 并静音，画面停在打开前的最后一帧；
  关闭面板时恢复。这样拖动滑块或选存档槽时车不会继续往前开。
- 只撤销"面板引起的暂停"：用 Alt+P 手动暂停的游戏，关闭面板后仍保持暂停；
  在面板内按 Alt+P 则交还暂停控制权（可以手动让游戏在面板后继续跑）。
- 面板内的保存/读档沿用同一个 `paused` 状态，不会出现"存完档面板还开着、
  游戏已经恢复"的情况。

**三、游戏列表按 zip 内容识别 set**

- 既然 set 是按 CRC 识别的，列表只按文件名显示就必然说谎：实测用户目录里 58 个包有
  15 个"文件名说的版本 ≠ 里面的 ROM 版本"。典型例子：
  - `scud.zip` → 实际是 `scudau`（Australia）；`scuda.zip` → 实际是 `scud`（Export）。
  - `scudj.zip` → 实际是 `scuddx`（Deluxe），不是 Japan。
  - `dirtdvlsa.zip` → `dirtdvlsau`（CROM 文件名缺扩展名，但 CRC 对得上）。
  - `srally2x.zip` → `srally2dx`，`vs299b.zip` → `vs299j`，`lostwsgo.zip` → `lostwsgp`，
    `bass.zip` → `getbassur`，`eca.zip` → `ecau`，`oceanhun.zip` → `oceanhuna`，
    `vf3.zip` → `vf3c`，`harley.zip` → `harleya`，`bassdx.zip` → `getbassdx`，
    `vs29915.zip` → `vs29915a`。
- `ScanRomFolder()` 现在对每个 zip 调 `GameLoader::Identify()`（只读中央目录，不加载
  ROM），标题/区域/年份/是否可启动都取自识别结果；`RomEntry` 新增 `resolvedSet`，
  与 `setName`（包名，列表行与右键菜单的标识）分开。封面和游戏简介按 `resolvedSet`
  查，所以 `scuda.zip` 也能显示 Export 版的封面和介绍。
- 好处：列表里不再有"未收录、无法启动"的假警报（之前有 7 个包被标成不可用，其实
  都可以跑）；点开 `scud` 行不会再默默启动另一个版本。
- `Identify()` 用 `quiet` 模式调 `ChooseGameInZipArchive()`，扫描阶段不刷
  `not found in ...` 错误日志；启动失败时仍照常报。
- 扫描开销：58 个包实测 GUI 启动无可见变化（只读 ZIP 中央目录）。

**四、窗口拖动/缩放时暂停**

- 现象：窗口模式下用鼠标拖标题栏，画面停住但声音变成一小段循环。
- 原因：拖拽会进入 Windows 自己的模态消息循环（从 `DispatchMessage` 里进入），主循环
  整段时间不在跑，所以音频不再生产；而声卡线程仍按实时节奏拉数据，环形缓冲
  反复 under-run。`Audio.cpp` 的 `underRunLoop = true` 会从写指针后面重播旧数据，
  这就是那段循环声。
- 修法（SDL3 目标）：在 `SDL_SetWindowsMessageHook` 里监听
  `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE`（`Main.cpp` 的 `ModalLoopPauseHook`）：
  - 进入：`paused = true` + `PauseThreads()` + `SetAudioEnabled(false)`，和 Alt+P
    走同一套暂停路径，`pacer.SetPaused()` 也会把这一段从节拍里剔除。
  - 退出：`FlushAudioBuffer()`（新增，清空环形缓冲并重置读写指针）→
    `ResumeThreads()` → 恢复音量，避免把拖动前的尾巴重播一遍。
  - 如果拖动前已经是暂停状态（Alt+P 或 in-game 面板），不接管；拖动结束也不会
    替用户恢复。
- 注意 SDL3 的回调语义：返回 `true` 才是“没处理，继续走”，返回 `false` 会把消息
  吞掉（连 SDL 自己的 live-resize 定时器都起不来）。
- SDL2 目标不受影响：它原本就是在模态循环里用 `WM_TIMER` 跑帧（
  `ModalLoopMessageHook`），本次只是把 `SetModalLoopHook()` 多接一个暂停标志。

**五、文档**

- `Docs/README-VK.txt`：F6 槽位说明从 1–8 改为 1–16；补充 `.zst<slot>` 命名、
  Manage saves 与区域 set 的关系、旧 `.st` 的导入方式；说明列表按包里实际 ROM
  识别版本。

**验证**

- `SM3D_SAVE_MANAGER=scud`（该 zip 实际是 Australia ROM）：
  `'scud' holds the ROMs of set 'scudau' (Scud Race (Australia, Twin/DX));
  listing that set's saves.` + `Manage saves: 3 card(s) for 'scudau' ...`。
- 列表扫描：`58 ROM(s) listed ... 0 with no identifiable set`，另有 15 行
  `GUI: '<file>' holds set '<set>' - <title>` 诊断日志。
- `SM3D_SAVE_MANAGER=scudau`：`2 card(s) ...`（`scudau.zst6/7`）。
- 启动 `scud.zip`：标题为 `Scud Race (Australia, Twin/DX)`；启动 `scuda.zip`：
  标题为 `Scud Race (Export, Twin/DX)`，日志 `Game: ... resolves to set 'scud'`。
- `SM3D_OVERLAY_OPEN=1` 启动 Daytona 2：窗口标题变为 `... (Paused)`，确认
  打开面板即暂停。关闭面板的恢复路径与现有 `s_overlayWasVisible` 边沿同一处。
- 窗口拖动：向游戏窗口注入 `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE`（真实拖动时
  Windows 发的就是这两条），日志依次出现
  `Window: drag/resize started; pausing the emulator until it ends.` 与
  `Window: drag/resize finished; resuming.`，进程无卡死。
- CMake/Ninja 两个目标零错误；MSVC LegacyOG 未受影响。

**Release notes**

- zh：管理存档现在按 zip 里的实际 ROM 识别 set（和启动游戏时一致），在 Scud
  Race (Export) 行打开也能看到 Australia 版写的存档；窗口标题补上区域版本，
  日志会写明 `scud.zip` 实际解析成哪个 set。游戏列表同样按内容识别版本，
  文件名和内容不一致的包会显示真实版本（不再误标为“未收录”）。打开游戏内
  面板自动暂停并静音，关闭后恢复，手动 Alt+P 的暂停不受影响。窗口模式下拖动
  标题栏或拉边框已改成暂停（原来是画面停住、声音循环），松手后从当前帧继续。
- en: Manage Saves now identifies a set from the ROMs inside the archive, exactly as
  the game does on launch - so opening it for the Scud Race (Export) row lists the
  saves the session actually wrote, even when that zip holds the Australian
  revision. The game list does the same, so a row always shows the revision its
  archive really holds instead of the one its file name suggests. The in-game panel
  now pauses and mutes the game while it is open, and resumes on close unless the
  player paused manually. Dragging or resizing the game window now pauses instead of
  looping the audio, and playback continues from the current frame on release.

### 读档后没有震动 / 读档第一声有杂音

- "勾了力反馈、读了存档却没震动"：存档是在 `ForceFeedback = 0` 时拍的，驱动板在存档里
  是 disabled；读档时会话是 enabled，`CDriveBoard::LoadState()` 判定状态不匹配，
  直接 `Disable()` —— 整局都不会再有力反馈命令。日志现在写明原因：
  `Drive board halted: the save state was recorded with it disabled, this session started
  with it enabled ... start a new game ...`。用新开的游戏（不用存档）就有震动，那之后
  存的档也会带着 enabled 的板。
- 新增两条一次性诊断日志：`FFB: command N for joystick J dropped (...)`（绑定不是手柄
  模拟轴时）和 `FFB: vibration accepted/rejected by the SDL rumble path.`（命令是否
  真的发到 SDL）。后来又补了每 5 秒的汇总：`FFB: commands in the last ~5 s -
  constant N, self-centering N, friction N, vibration N`，用来区分"游戏没发"和
  "发了但设备不理"。
- **震动原来是边沿式的**：驱动板只在数值变化时发一次（真机马达是档位，给了就一直转），
  而 `SDL_RumbleGamepad` 是定时脉冲，所以撞墙最多嗡 0.1 秒。现在手柄震动按
  **电平**处理：`m_padVibration[]` 存住当前档位，`ServicePadVibration()` 每帧续
  500 ms 脉冲，归零才停——和真机一致。
- 实测：Daytona USA 2 有震动；Scud Race 的日志里 `vibration` 恒为 0（它只发
  `constant` / `self-centering`，不用那个马达通道），所以 Scud Race 没有震动是
  游戏行为，不是手柄或驱动问题。
- 力反馈页的「校准摇杆」按钮删了：它直接调 `CInputs::CalibrateJoysticks()`，那是
  命令行交互（`puts` 提示 + 读键），GUI 构建里没有控制台，点了什么也不会发生。
  改成一行说明：校准用 `-config-inputs` 启动后按 B，手柄轴由 SDL 归一化。
- 读档/恢复后的第一声杂音：`Audio.cpp` 加了 20 ms 软启动斜坡（`ArmAudioFadeIn()`），
  在 `OpenAudio()`、`FlushAudioBuffer()`、`SetAudioEnabled(false→true)` 时武装，
  在第一个非静音块上展开。故意不在静音期消耗斜坡——读档后可能要几秒才出声音。

### 力反馈页只显示本构建真正生效的设置

- SDL3 版里 `DirectInput*` / `XInput*` 这组键没有任何代码读（`GetInputSystem()` 把
  所有选择映射到 SDL），但设置页照旧把这两组列出来，看起来像“震动只能在 XInput 里
  设”。现在 `CreateControls()` 在 SDL3 构建里跳过这两组键；SDL2 构建不变（它真的
  支持两套后端）。
- 顺带澄清（写进下面给用户的回答）：HIDAPI 设备（Switch Pro、8BitDo 的 Switch
  模式）在 Windows 上只有 `SDL_RumbleGamepad` 这一条震动路径，没有 `SDL_Haptic`
  驱动，所以恒力/回中/摩擦三个滑块对它们无效；“SDL 振动”是有效的。

### 读档后挂起：中断输入线没有随存档恢复

- 现象：Scud Race 在 "DECIDED LIKE THIS / LET'S START" 转场画面存的档，读回来能进游戏
  几帧，然后整个进程卡死（单核 100%，只能强制结束）。同一次游戏里比赛中的存档正常。
- 定位：临时探针确认主线程卡在 `EndSimulationFrame()` 的同步等待（不是渲染）；
  模拟线程的 PPC 停在一个 4 条指令的等待循环里（`lbz r3` / `lbz r4` 读同一个 RAM
  字节、`cmpw`、条件回跳）。
- 原因：`CIRQ::LoadState()` 恢复了 `irqEnable` / `irqState`，但**没有重新驱动 PPC
  的中断输入线**。这条线不属于任何一方的存档字段，只在 `Assert()`/`Deassert()` 时
  求值；所以"存档时有一个已使能的中断正挂着"的情况下，读档后控制器认为有中断、
  CPU 却永远收不到。
- 修法：新增 `CIRQ::RefreshInterruptLine()`，`CModel3::LoadState()` 在
  `ppc_load_state()` **之后**调用（必须在之后：`ppc_load_state` 会覆盖 MSR/PC，
  之前注入的异常会丢）。
- 诚实说明：那个具体存档仍然卡——它的等待循环不是等这个中断（读档那一刻
  `irqEnable=0x6E`、`irqState=0x01`，挂起的 bit 0 正好是被屏蔽的那一个）。所以
  **转场/载入画面不适合存档**这条经验仍然成立；这次修复堵的是同类的另一个洞。
- 经验：在赛道中或稳定的菜单里存档，不要在载入/转场/比分画面按 F5，也不要反复
  覆盖同一个槽位。

### Batch 3：管线缓存、晚采样输入与 57.5 Hz 自适应

**一、Vulkan Pipeline Cache 磁盘持久化**

- `CVulkanContext` 现在拥有一个 `VkPipelineCache`：创建设备后从
  `Data/Cache/pipelines-<tag>.bin` 读取，`Shutdown()` 时用
  `vkGetPipelineCacheData()` 写回。写入先写 `.part` 再改名字，中途崩掉不会留下
  半截文件被驱动反复拒绝；驱动不认的旧缓存会得到一个空缓存，不算错误。
- 所有管线创建都接上它：`CVulkan2D`（主/准星/白边/post）、`CVulkan3D`
  （不透明/半透明/fog/LOS compute）、`VulkanTextureCache`（compute），以及
  设置窗口与游戏内面板的 ImGui 后端（`ImGui_ImplVulkan_InitInfo.PipelineCache`）。
- 两个上下文（设置窗口、游戏会话）**同时存在**，所以各用各的文件
  （`pipelines-ui.bin` / `pipelines-game.bin`）。共用一个文件时，后销毁的那个会
  用自己较旧的副本覆盖另一个的条目，每次运行都会丢一半。
- `FileSystemPath` 新增 `Cache` 类型（Windows `Data/Cache/`，Unix/macOS 同
  Assets 的规则）。`build.bat` 现在把它和 Saves/NVRAM/Screenshots 一起保留，
  否则每次重打包都要重新编译全部着色器。
- 实测（RTX 5060 Ti，Daytona 2）：`Vulkan: pipelines ready` 冷启 20-21 ms，
  热启 8-9 ms；缓存文件 187.7 KB（游戏）/ 15.1 KB（界面）。

**二、全游戏输入晚采样**

- 新增 `CInputs::PollGameInputsLate()`：在 `BeginSimulationFrame()`（高刷路径）
  或 `RunFrame()`（原路径）之前重采一次设备状态并重轮询该游戏用到的输入。
  `Poll()` 仍在帧首，负责 UI 快捷键与面板开关；晚采样只碰游戏输入。
- `CInputSystem` 新增 `RefreshState()`：SDL 后端实现为
  `BeginInputFrame()` + 重读快照（键盘/鼠标/每个手柄），**不排空事件队列**——
  事件属于帧首，抽两次会把滚轮刻度和热插拔通知送两遍。旧后端保持空实现。
  为此把 `CSDLInputSystem::Poll()` 里复制快照的代码抽成 `ReadSnapshot()`，两条
  路径共用。
- 面板打开时（`m_gameInputSuppressed`）晚采样直接返回，行为和 `Poll()` 一致；
  UI 输入不重轮询，否则 `prevValue` 会被推平、边缘被吞掉。
- `PollPointerLate()` 保留：它额外做一次 OS 指针查询，让准星和本帧的准星位置
  一致；晚采样在它之后。

**三、57.524 Hz 智能自适应**

- 逻辑时钟从 v0.9.5.5a 起就固定 57.524160 Hz（`Model3Timing.h` +
  `DisplayPacer`），呈现率只影响画面节奏，不再影响游戏速度。本轮补上“自动
  选中街机节奏”的两条规则（`ResolveConfiguredRefreshRateMode()`）：
  - `Auto` 且显示器**当前**就跑在 57.5 Hz 附近（原生 57/58 Hz 模式，或 VRR 已经
    跟随模拟器）→ 改用 `Original`，不再插值。否则每帧都白等半个模拟帧。
  - 配置里没有任何地方指定 `RefreshRateMode`（全新安装），且显示器**真的提供**
    57.524 Hz 模式（`HasArcadeDisplayMode()`）→ 用 `Original`，每帧只显示一次。
- 故意不用 EDID timing range 作为触发条件：绝大多数 60 Hz 显示器的 EDID 范围
  都包含 57.5（50-75 Hz），但那只是“能扫描”，不选模式它仍然跑 60 Hz；用 57.5 Hz
  往 60 Hz 扫描里送比插值更颠。真正的 57.5 Hz 建议仍是
  `ExclusiveRefreshRate = 57.5242` 或独占全屏，设置界面也一直是这样提示的。
- 模式解析每帧都重算（换显示器要立即生效），所以模式可用性查询做了按显示器
  缓存，避免每帧枚举 fullscreen modes。

**四、子 set 的父包也按内容查找**

- `GameLoader::Load()` 以前只会按 `<parent>.zip` 这个文件名找父包。父包一旦被
  改名（或者合并包里解析出的是子 set），子 set 就报
  `Expected to find parent ROM set of 'scudplus'`——哪怕需要的 ROM 就在旁边。
- 现在文件名找不到时，`FindArchiveForSet()` 会扫 ROM 目录，对每个 zip 跑一次
  `Identify()`（只读中央目录），挑出内容确实是那个父 set 的包，并打一行
  `Game: '...' has no 'scud.zip'; using '...' as the parent ROM set.`。只在失败
  路径上多花一次扫描（57 个包约 70 ms）。

**验证**

- 父包回退：把 `scudplus.zip` 和 `scuda.zip` 单独放到空目录（没有 `scud.zip`），
  启动 `scudplus` → `has no 'scud.zip'; using '.../scuda.zip' as the parent ROM set.`，
  游戏正常进入，`resolves to set 'scudplus'`。
- 管线缓存：删除 `Data/Cache` 后启动游戏 → `pipeline cache ready` 无 loaded 行，
  退出时写出 `pipelines-game.bin`（192161 字节）；第二次启动 →
  `pipeline cache loaded from ... (187.7 KB)`，`pipelines ready` 20→8 ms。
  重新 `build.bat` 后 `Data/Cache` 仍在。
- 晚采样：Daytona 2 跑 15 s 无 `[Error]`，`Pace:` 行正常；
  `SM3D_OVERLAY_OPEN=1` 仍能打开面板并暂停。
- 两个目标 0 error。

**Release notes**

- zh：Vulkan 管线编译结果缓存到 `Data/Cache`，第二次启动省掉大部分着色器编译；
  方向盘/手柄的输入改为在仿真步前一刻采样，UI 快捷键仍在帧首；显示器如果本来
  就跑在 57.5 Hz（或提供该模式），自动切到街机原生节奏。
- en: Compiled Vulkan pipelines are now cached under `Data/Cache` and reused on the
  next start. Game controls are sampled immediately before the emulation step while
  the UI hotkeys stay at the top of the frame. A display that already runs at the
  arcade rate, or offers a 57.5 Hz mode, is now presented at it automatically.

### Batch 2：SDL3 力反馈、多鼠标光枪与多机联机

Batch 1 完成后，Vulkan/SDL3 版相对 LegacyOG 还缺三块外设能力。这一轮把它们补齐，
不改变已有画面与音频路径。

**一、方向盘力反馈（Constant / Spring / Friction）**

- 平台层新增 SDL_Haptic 抽象（`PlatformInput.h`）：`OpenHaptics()` /
  `CloseHaptics()` / `SetHapticEffect()` / `StopHapticEffect()` /
  `StopAllHapticEffects()`。句柄在第一条力反馈命令到达时从摇杆打开，手柄震动仍走
  原来的 `SetGamepadRumble()`。
- `CSDLInputSystem::ProcessForceFeedbackCmd()` 的 SDL3 分支不再对
  `FFConstantForce` / `FFSelfCenter` / `FFFriction` 返回 false，而是和 SDL2 分支
  使用同一组 `SDL*Max` 缩放键构造效果；`FFStop` 停止全部效果。
- 恒力用效果自带的 attack 包络做 20 ms 平滑：游戏仍以 57 Hz 下发目标力矩，
  直驱方向盘收到的是连续变化而不是 57 Hz 阶梯。效果支持只在首次创建时查询一次，
  不在每帧重复调用 `SDL_HapticEffectSupported()`。
- `Platform::Initialize()` 新增 `Subsystem_Haptic`，SDL3 后端映射到
  `SDL_INIT_HAPTIC`；`CloseGamepad()` 先销毁效果再关闭 haptic 与摇杆。
- 效果不可用时返回 false，游戏自身的无反馈回退逻辑不受影响。

**二、SDL3 多鼠标光枪与 Sinden 白边**

- `PlatformInputSDL3` 按 `SDL_MouseID` 维护每设备状态表（`MouseInfo` /
  `MouseState`、`GetMouseCount()` / `GetMouseInfo()` /
  `GetMouseStateByIndex()` / `GetMouseStateById()`）。
- Windows 下 SDL3 只在相对鼠标模式才打开 RawInput 并发送带设备 ID 的事件；
  普通窗口里所有事件都是全局 ID，无法区分两把枪。因此平台层自带一个
  message-only 窗口 + RawInput 线程，直接解析 `WM_INPUT` 的 `hDevice` 与
  `RAWMOUSE`。相对模式期间让 SDL 接管注册，退出相对模式后自动重新注册；
  `RefreshPerDeviceMouse()` 在光枪晚采样点再排空一次队列。
- 位置处理：绝对设备采用上报坐标（屏幕坐标转客户区），相对设备按自己的
  delta 积分；每个设备的第一个样本用系统光标位置作为起点。只有一个鼠标时
  仍完全沿用原来的绝对光标行为，单枪游戏不受影响。
- `CSDLInputSystem::GetNumMice()` 返回真实数量，`MOUSE1_` / `MOUSE2_` 绑定
  因此可用；轴、按钮和 `GetMouseDetails()` 都按设备读取。输入系统在开局时
  缓存设备数，中途插入的鼠标需要重开一局才出现在绑定里。
- Sinden 白边：新增 `border.vert` / `border.frag` 与
  `CVulkan2D::CreateBorderPipeline()` / `DrawBorderPass()`。画在最终输出上、
  composite/post 与机柜背景之后、游戏内面板之前，因此 CRT 滤镜和 bezel
  不会移动或柔化它。三档配置：`SindenBorder`、`SindenBorderWidth`
  （0–256 输出像素）、`SindenBorderBrightness`（0–100%）。
- UI：画面页新增「光枪白边」小节，游戏内面板同步提供；三键加入
  `kOverlayKeys` 与 `SaveOverlaySettings()`，撤销/应用行为与其它画面项一致。
  OpenGL 构建不显示该小节。

**三、多机 TCP 联机**

- 新增 `Src/Network/NativeSocket.h/.cpp`：Windows 用 WinSock2，其它平台用
  BSD socket，负责启动/清理、`TCP_NODELAY`、非阻塞设置和带 EINTR 处理的
  send/recv。
- `TCPSend` / `TCPReceive` 的 `SUPERMODEL_PLATFORM_SDL3` 分支不再是空实现：
  发送仍按「主机序 int 长度 + 负载」的原有协议，因此 SDL2 与 SDL3 实例可以
  互相联机；接收端解析长度并限制单包 16 MB。
- 监听 socket 设 `SO_REUSEADDR`、非阻塞，16 ms accept 轮询；接受的连接显式
  改回阻塞模式（Windows 会让 accepted socket 继承监听 socket 的非阻塞状态，半包会被
  误判为断线），并设置 `TCP_NODELAY`。
- CMake 删除 `SUPERMODEL_NO_NETWORK`，Windows 的 SDL3 目标链接 `ws2_32`。
  `CSimNetBoard` 的单机回环分支保持不变，仍不经过 socket。

**验证**

- CMake/Ninja：`Supermodel-VK-SDL3` 与 `Supermodel-LegacyOG` 均链接成功；
  MSVC `Supermodel.vcxproj`（LegacyOG）零错误。
- 全部 Vulkan 着色器重新生成，`CheckShaderHeaders.ps1` 通过。
- 原生 socket 端到端测试（临时独立程序，未加入仓库）：TCPReceive 监听 →
  TCPSend 连接 → 发送 20 字节 → 接收内容逐字节一致。
- Daytona 2 冒烟（MSAA 4x，`SindenBorder=1`）：连续运行无 Vulkan 报错，
  边框管线创建成功，MSAA 与帧节奏路径正常初始化。
- 未在实机验证：真实方向盘、双 Gun4IR/Sinden、两台机器联机（当前环境只有
  一套输入设备）。这几项需要用户侧实测。

**Release notes**

- zh：SDL3 版补齐方向盘恒力/回中/阻尼力反馈（20 ms 力矩平滑）；支持按
  `SDL_MouseID` 区分的双光枪，并新增可配置的 Sinden 白边；多机 TCP 联机
  不再依赖 SDL3_net，可与 SDL2 版互连。
- en: The SDL3 build gains wheel constant-force/spring/friction feedback with
  20 ms torque smoothing, per-device light guns keyed by SDL_MouseID plus a
  configurable Sinden border, and native TCP networking that links with the
  SDL2 build without SDL3_net.

### VRR 整数倍频：115.05 / 172.57 / 230.10 / 460.19 Hz

上一轮的高刷插值让呈现频率跟随面板，`alpha` 由蓄水池连续算出。它消除了
“帧重复间隔不均”，但 alpha 是测出来的——线程唤醒抖动 200µs 相当于 2.3% 的
步长，慢速平移时仍会看到轻微抖动。本轮新增整数倍频模式：**N 次呈现 = 1 个
仿真帧**，alpha 是常量而不是测量值。

**一、确定性相位状态机**

- `RefreshRateMode` 扩到 0–6：0=VRR 1x（原 Original）、1=Auto、2=Fixed、
  3=VRR 2x（115.04832Hz）、4=VRR 3x（172.57248）、5=VRR 4x（230.09664）、
  6=VRR 8x（460.19328）。
- `m_phase == 0` 的槽位跑一次完整仿真（PPC/SCSP/DSB/FFB），其余槽位只更新
  插值矩阵并提交；相位在 `RecordPresent()` 推进，而不是在 `WaitForPresent()`，
  否则 `AlphaAt()` 会描述下一格。
- 时间锚点用有理式 `anchor + round(slot × step / N)`，3x/8x 除不尽的周期不会
  逐帧累积取整误差；2x 恰好是 8,692,024ns（硬件半周期）。
- 迟到超过一整个仿真帧时重锚并把相位复位到仿真槽位，`dropped` 按整帧累加，
  保持“每次迭代至多一步仿真”的纪律。

**二、alpha 常数必须是 k/N**

最初的设想是“仿真拍 alpha=1.0、插值拍 alpha=0.5”。按当前快照对语义（alpha=0
显示 M−1，1 显示 M）展开会得到 `−0.5 → +1.0 → +0.5 → +2.0`：每拍前进 1.5
再后退 0.5，快速平移时是 115 次/秒的 ±8.7ms 抖动。唯一单调的分配是
`alpha = k/N`（仿真拍为 0），代价是恒定落后一个仿真帧。零延迟路线由“关闭
插值”提供：所有槽位 alpha=1.0，每个仿真帧原样显示 N 次，节奏同样绝对均匀。

**三、面板门限与回退**

- 目标频率须 ≤ 面板率 × 0.98，且 2x 额外要求 ≥120Hz（3x ≥176、4x ≥235、
  8x ≥470）。同一个 `PanelSupportsPhaseCount()` 被游戏循环和设置界面共用，
  不会出现“界面可选、运行时不认”。
- 不满足时按 8x→4x→3x→2x→1x 回退并打一行日志。
- 相位模式下 `VSync=0` 会告警：没有 FIFO，面板不会跟随，会被量化回自身刷新率。
- PresentMode 未改动；呈现仍用绝对 deadline + 0.5ms 粗睡 + 自旋收尾，从不提前提交。

**四、配置、命令行与热键**

- `RefreshRateMode` 支持 0–6；`-vrr-1x/2x/3x/4x/8x` 或 `-refresh-rate-mode=N`。
- **F8** 在 VRR 1x ↔ VRR 2x 之间即时切换（面板不足 120Hz 时拒绝并提示），
  方便同一场景下直接 A/B。
- 新增 `PaceAssumePanelHz`（默认 0 = 真实面板）：测试专用，用来在本机验证
  “面板不足时回退”与 3x/4x/8x 的门限。

**五、遥测**

- 统计读数在相位模式下改为四行：`Pacer`（模式/呈现率/面板率/locked）、
  `Sim`（逻辑率/当前 alpha）、`Phase`（槽位号与全部权重）、`Sub`（不含仿真的
  呈现耗时）；`Cadence` 直接报 `even (Nx locked)`，不再拿 115 与 120 相除
  误报 `uneven (1.04x)`。
- `PACE:` 日志增加模式名、相位、`sub ms/frame`；配置时额外一行记录槽位权重。

**六、验证（120 Hz 面板）**

- `VRR 2x` + 插值开：`present 115.04–115.06`、`logic 57.52–57.54`，
  `sim:presents ≈ 1:2`，`sub 0.27–0.35 ms/frame`，`dropped` 仅启动阶段 14 次。
- `VRR 2x` + `LowLatency=1`：`alpha 1.000` 恒定（每帧显示 2 次）。
- `PaceAssumePanelHz=60`：自动回退 `vrr-1x` 并记录日志。
- `PaceAssumePanelHz=240` + 4x：模式与权重正确（`0.000 0.250 0.500 0.750`），
  真实 120Hz 面板下 present 被压在 120、logic 降到 40、dropped 增长 ——
  正好说明门限判断是必要的。
- `Fixed 120` 回归：`present 119.98`、连续 alpha，与上一轮一致；LegacyOG 不受影响。
- 详细数据见 `sdl3_reports/2026-10-23-11-vrr-multiples.md`。

**Release notes**

- zh：新增 VRR 整数倍频呈现模式。此前的高刷插值让画面跟随面板刷新率（120/144Hz），
  新模式则让面板按街机 57.524Hz 的整数倍扫描：2x = 115.05Hz、3x = 172.57Hz、
  4x = 230.10Hz、8x = 460.19Hz，每个仿真帧正好扫描 N 次，节奏不可能不匀，
  亚帧权重是固定常数而非测量值。需要 VRR（G-SYNC/FreeSync）与垂直同步；
  面板刷新率不足时自动回退到能用的档位并记录日志。按 F8 可在 VRR 1x 与
  VRR 2x 之间即时切换，方便同一场景下对比。开启插值时每帧都是新画面
  （恒定落后 17.4ms）；关闭插值则每个仿真帧显示 N 次，延迟为零。统计读数
  新增 Pacer / Sim / Phase / Sub 四行，可直接确认 alpha 锁定在预期值、
  仿真帧率维持 57.52Hz。
- en: VRR multiples are in. Where the previous high-refresh work followed the
  panel's rate, this drives the panel at an exact multiple of the arcade's
  57.524 Hz: 2x = 115.05 Hz, 3x = 172.57 Hz, 4x = 230.10 Hz, 8x = 460.19 Hz.
  Every emulated frame is scanned exactly N times, so the cadence cannot be
  uneven, and the sub-frame weight is a fixed constant rather than a
  measurement. VRR (G-SYNC/FreeSync) and vsync are required; a display that
  cannot scan fast enough falls back to the fastest multiple it can take, with
  a log line. F8 switches between VRR 1x and VRR 2x on the fly for an A/B of
  the same scene. With interpolation on every present is a new picture (one
  emulated frame of latency); with it off each frame is shown N times and
  latency is minimal. The statistics readout gains Pacer, Sim, Phase and Sub
  lines that confirm the locked weights and the 57.52 Hz simulation rate.

### 高刷插值：57.524Hz 固定逻辑 + 呈现端插值（根治 cadence judder）

Model 3 的逻辑时钟是 57.524160Hz，而 60/120/144/165/240/480Hz 面板没有一档
是它的整数倍（120Hz 为 2.086×、144Hz 为 2.503×、240Hz 为 4.172×）。旧主循环
“一次仿真 = 一次呈现”，在高刷屏上只能在两帧/三帧之间来回跳，表现为规律顿挫。
本轮把逻辑与呈现彻底拆开：**逻辑固定 57.524Hz 步进，画面按面板物理刷新率
输出，中间的亚帧由相机/实例矩阵插值补齐**。

**一、固定步蓄水池与显示节奏**

- 新增 `Src/Model3/Model3Timing.h`：`kModel3StepNs = 17384048`（对应
  57.524160Hz）、每步 767 样本（精确 766.6365）、`kMaxBacklogNs = 3 × step`。
- 新增 `Src/OSD/DisplayPacer.{h,cpp}`：纳秒蓄水池 + 绝对呈现 deadline；
  每次至多释放一步，积压超过 3 步直接截断（拖窗口/读档后不追帧）；
  呈现迟到时重置 deadline 而不是补帧；`alpha = accumulator / step` 钳制 0..1。
- 三种呈现模式（`RefreshRateMode`）：`Original`（57.5Hz，不插值）、
  `Auto`（跟随显示器，每秒重读一次模式）、`Fixed`（60/120/144/165/240/480）。
  旧 ini 的 `RefreshRate=60` 在首次启动时自动迁移为 `Fixed 60`，逻辑仍 57.524Hz。
- 主循环改为流水线：`Tick → EndSimulationFrame → PrepareSceneSnapshot →
  BeginSimulationFrame → WaitForPresent → RenderFrame(alpha)`。仿真在后台线程
  跑（6–8ms），呈现路径只剩场景匹配与命令提交；内联仿真会让 120Hz 直接
  miss vblank（实测掉到 97Hz）。

**二、双快照与矩阵插值**

> **当前默认只做相机插值。** 实例矩阵插值（车辆、车轮这类自身在动的网格）默认关闭：
> 用 Daytona 2 存档逐帧抓图发现，开启后约 3/4 次运行会出现**单帧**的地面白块（缺一块
> 三角、露出背景雾色），只保留相机插值 0/3 次。疑似实例匹配键被游戏复用的矩阵槽位污染。
> 复现方法见 `sdl3_reports/2026-10-23-10-high-refresh-interpolation.md` 第八节第 0 条，
> 调试时可用 `SM3D_INST_INTERP=1` 重新打开。

- `CR3DSceneBuilder` 增加 `m_nodesPrev` 双缓冲（每仿真帧交换一次）、模型
  `localMat`、视口 `baseMatrix`，以及基于 Culling 节点路径 + 模型地址 +
  矩阵槽位的 `instanceKey`。
- 新增 `Src/Graphics/Vulkan3D/MotionInterpolation.{h,cpp}`：矩阵拆成平移/
  四元数/缩放，插值时**平移 lerp + 旋转最短路径 slerp**。直接对矩阵元素线性
  插值会破坏正交性（转 90° 时 α=0.5 画面会缩到 0.707 倍），这是硬性要求。
- 场景遍历移到 `EndSimulationFrame()` 之后、`BeginSimulationFrame()` 之前
  ——那是唯一一个 Culling RAM 里是完整一帧、且没有线程在写的时刻。
  放在 present 之前会与 PPC 写下一帧重叠（中间隔着最长 8.3ms 的呈现等待）。
- 后视镜等小视口按 Culling 源地址**独立**维护上一帧矩阵与运动均值，
  不会与主视野互相插值。

**三、切镜检测与豁免**

- 每视口独立判定：帧间位移超过 `CutDistanceScale(8) × EMA`、或旋转超过
  `CameraCutAngleDegrees(15°)`、或布局/缩放跳变 → 判定切镜，该帧 `alpha=1`，
  不会跨地图飞掠或留下鬼影。EMA 跟随系数 0.2，跟车时阈值随速度自动抬高。
- 2D Tilegen 图层完全不参与插值（每仿真帧生成一次，亚帧只重放纹理），
  计时器、圈速、字幕保持锐利；3D HUD 视口（3D 仪表/血条）按
  `InterpolationScope` 离散更新（0 自动 / 1 全部 / 2 仅全屏视口）。

**四、亚帧零冗余与遥测**

- 场景遍历、绘制列表、材质写入、纹理同步、动态顶点上传全部由 `rebuild`
  门控，只在仿真帧发生；亚帧只更新矩阵 push constant 并重放绘制。
- 绘制列表改为 `[priority][overlay][layer]` 分桶，材质一次写入所有 frame
  slot，纹理覆盖诊断每仿真帧一次 —— 呈现路径从 8.35 降到 **0.97–1.20 ms/frame**。
- 动态顶点缓冲从每 present 归零的 ring 换成双槽持久缓冲，避免亚帧读到
  被覆盖的数据。
- 游戏内 ImGui 面板与统计读数限到 60Hz 构建（跳过构建时重放上一份 draw
  data，面板不会闪空），480Hz 下 UI 开销与 60Hz 相同。
- 每 5 秒一行 `PACE:`（逻辑/呈现速率、alpha、仿真与呈现耗时、绘制数、
  累计切镜数），每 300 仿真帧一行 `Interp:`（视口/实例匹配、窗口内切镜数）；
  统计读数新增 `Pace` / `Interp` 两行。切镜计数最初每 300 帧采样一次，
  长期显示 0——采样点总落在两次切镜之间，现改为逐帧累加。

**五、配置与界面**

- 新增键：`RefreshRateMode`、`FixedRefreshRate`、`MotionInterpolation`、
  `LowLatency`、`InterpolationScope`、`CameraCutAngleDegrees`、
  `InstanceCutAngleDegrees`、`CutDistanceScale`。新安装默认 `Auto` + 插值开。
- 常用页新增「呈现节奏」：三选一 + 固定刷新率下拉 + 运动插值开关 +
  插值范围，并显示当前面板速率对应的每逻辑步呈现帧数。
- `LowLatency` 与插值开关写成互补的两个键，界面上只有一个复选框，
  不会出现“勾了却不生效”的组合。
- 高刷模式下强制同步声卡线程（`AsyncSoundBoardThread=false`）：音频必须由
  固定步生成，否则会与仿真时钟自由跑。`Original` 模式保留异步选项。

**六、验证**

- `cmd.exe //c build-cmake.bat -DUSE_SDL3=ON` 三个目标全部编译链接通过。
- harley（120Hz 面板，Fixed 120）：`logic 57.46–57.50` / `present 119.87–119.96`，
  `sim 0.06–0.28 ms/frame`、`render 0.97–1.20 ms/frame`，实例匹配 224/226、
  `static 0`、`snap 0`；启动阶段 91 次切镜、稳定后 0 次。
- vf3：11 个视口 / 399 个实例，匹配 365、`static 190`、无切镜。
- 把 `CameraCutAngleDegrees` 临时降到 1.0° 可复现切镜计数（260 帧内 3 次），
  证明判定链端到端工作；恢复 15° 后为 0。
- `Supermodel-LegacyOG.exe`（OpenGL）路径不受影响，`highRefresh=false`。
- 详细数据与六条红线逐条核对见
  `sdl3_reports/2026-10-23-10-high-refresh-interpolation.md`。

**Release notes**

- zh：新增高刷新率插值呈现。逻辑固定 57.524Hz 步进，画面按显示器实际刷新率
  输出，两者之间的亚帧由相机与动态物体矩阵插值补齐，120/144/240Hz 屏幕上
  长期存在的规律顿挫（cadence judder）不再出现。常用页新增「呈现节奏」：
  原始 57.5Hz / 跟随显示器（推荐）/ 固定刷新率三选一，另有运动插值开关与
  插值范围。相机按四元数插值（转向不变形）；切镜头、2D 计时与字幕自动
  离散更新，不发虚。统计读数新增 `Pace`/`Interp` 两行，可直接看到逻辑与呈现
  速率、亚帧权重和累计切镜数。
- en: High-refresh interpolation is in. Logic still steps at a fixed 57.524 Hz
  while the picture is presented at the display's own rate, with the sub-frame
  in between filled in by interpolating camera and instance matrices - which
  removes the cadence judder that 120/144/240 Hz panels have always shown with
  this hardware. A new Presentation cadence section on the Common tab offers
  Original 57.5 Hz, Follow the display (recommended) and Fixed refresh rate,
  plus a motion-interpolation switch and scope. Camera rotation is interpolated
  as a quaternion so turns do not shrink; camera cuts, 2D timers and captions
  update discretely and stay sharp. The statistics readout gains Pace and Interp
  lines showing the logic and present rates, the sub-frame weight and the
  cumulative cut count.

### Batch 4（第 1、2 项）：实例插值配对校验 + 呈现端确认（present_wait）

**一、实例插值：把“配对”做可靠，但结论是继续保持默认关闭**

- 目标本来是把实例插值修好并默认开启。为此补了四层配对校验，都在
  `Vulkan3D::PrepareMotion()`：
  - 键在上一帧和当前帧**两边都必须唯一**（重复判模糊、不配）；
  - 两帧必须是**同一几何**（模型地址、缩放、贴图窗口、页、mesh 数全一致）；
  - 位移必须是**连续运动**（按每实例自身历史的 EMA，超过 `CutDistanceScale`
    倍就跳变），历史按 key 存，10 秒不出现则淘汰；
  - `DecomposeRigid()` 入口拒绝**非有限输入**（NaN/∞ 之前会静默通过并产出
    NaN 四元数）。
- 所有被拒的配都退化为“只画当前帧矩阵”，即原来关掉实例插值的干净画面，
  所以**最坏情况 = 关闭时**。日志 `Interp:` 行新增 `far / rejected / ambiguous /
  decompose fail / max move / max turn` 计数。
- 新增 `InstanceInterpolation`（默认 false，**只在 ini 里**，没有界面开关），
  `SM3D_INST_INTERP=1/0` 可强制开/关（原来只能开）。这样不会让用户在界面上
  随手勾到一个已知有瑕疵的实验项。
- **结论：白线不是配错、也不是 NaN。** 实测中 `far/rejected/ambiguous/decompose
  fail` 全程为 0，但白线仍在。真正的机制是**同一帧里混用了两种公式**：
  - 被跳变（snap）的实例画在“当前帧矩阵”；
  - 正常插值的实例画在两帧之间。
  两者共边处差 `(1-α) × 该帧位移`；α≈0.93 时是位移的 7%，亚像素到 1-2 像素，
  正好是一条细白线，α 变化时就会闪。
- 中间试过把实例插值从 slerp 换成**逐元素线性插值（`LerpMatrix`）**：这对“两个
  都插值的相邻面”是数学上严格的共边保持（线性组合保共边），但对“一个插值、
  一个跳变”的边界无能为力——**实测细线并未消失**。`LerpMatrix` 保留（它比
  slerp 更适合实例），但它不是这个问题的解。
- 要让画面绝对干净，同一帧所有实例必须用同一公式。“全部插值”不行——>30° 的
  实例（广告牌朝向翻转、车轮）会拖影或塌成一条线。所以**唯一保证干净的选项
  就是关闭**，`InstanceInterpolation` 默认 false，只作为可选项保留。
- 日志 `Interp:` 行新增 `turn <1/<5/<15/<30/>=30(>=90)` 分桶，便于以后判断
  跳变到底是真旋转还是分解出的假象。

**二、VK_KHR_present_wait + present_id：把“帧有没有上屏”变成可观测**

- `CVulkanContext` 在设备支持时启用 `VK_KHR_present_id` + `VK_KHR_present_wait`
  （两个扩展、两个 feature 都检测后按需请求），`EndFrame()` 给每个 present 挂
  `VkPresentIdKHR`，`WaitForPresentDisplay(timeout)` 可等到指定 present 真正上屏。
- 实测：**阻塞等待对节拍没有帮助**——Fixed 120 在 120Hz 面板上 `0/121` 次确认
  都超时（提交总是落在 vblank 之后，图要下一周期才上屏），vrr-2x 下 `66%` 确认；
  pacer 自身的睡眠本来就已经把帧摆对了位置。因此只做**非阻塞确认**（timeout 0），
  不改节拍：`SM3D_FRAME_TIMING` 的 Timing 行新增 `N/M confirmed by the display`。
- 附带发现：Fixed 120 下 `0/121` 说明提交相位系统性落后 vblank 一整帧，这是一段
  可以靠“相位校准”追回来的约 8ms 延迟。

**三、提交相位校准（已实现）**

- 机制：pacer 的呈现截止点与面板 vblank 网格没有固定关系。落在 vblank 之后提交，
  这一帧就得多等一个周期才上屏（120Hz 下 8.3ms）。
- 校准：pacer 配置后（启动 / 换模式 / 换面板）的前 16 帧，用
  `WaitForPresentDisplay(2×呈现周期)` 阻塞测出“上屏时刻 − 截止点”的距离，换算出
  截止点落后 vblank 多少，据此设一个**固定提前量**（`SetPresentLead`，上限一个
  vblank）。之后不再阻塞，只在截止点做一次零超时确认（日志 `N/M confirmed`）。
- 判据只认“上屏耗时超过半个 vblank”：小于一半说明已经排在 vblank 前面，或者面板
  在跟随我们（VRR）——这两种情况加提前量只会让画面更旧，所以不加。
- 提前量是**相位平移**，不改变节拍。相位锁定模式的 alpha 是常量，完全不受影响；
  时钟驱动模式的 alpha 会早采样几毫秒，这是换回一个周期延迟的代价。
- 实测（3840x2160 @120Hz）：
  - Fixed 120：`0/121` → `121/121 confirmed`，lead ≈3.2-3.7 ms，30 秒不漂移；
  - vrr-2x：仍为 115.05 Hz，lead 0（面板快于呈现率，无固定相位可锁）；
  - 原始 57.5Hz：`58/58 confirmed`，lead 2.5 ms。
- 日志：`Pace: present phase calibrated - display was X ms after the deadline, lead Y ms.`
  PACE 行末尾新增 `lead Y ms`。

## 2026-10-22

### 存档模块去原生依赖：ZstdSharp.Port 替换 libzstd P/Invoke

`HostCore` 的 `.zst` 容器原来 P/Invoke 官方 `libzstd.dll`，因此每个平台都要在
`SupermodelHostCore.dll` 旁边再放一份原生库（Windows `.dll` / Linux `.so` /
macOS `.dylib`），还要保证它的 ABI 与托管模块同步。本轮把编解码器换成
NuGet 包 `ZstdSharp.Port` 0.8.8：它是参考实现的纯 C# 移植，Native AOT 会把
它直接编进 `SupermodelHostCore.dll`，产物重新变成单个自包含模块。

**一、工程与构建**

- `SupermodelHostCore.csproj` 增加 `ZstdSharp.Port` 的
  `PackageReference`，删除 `native\libzstd.dll` 的 `None`/复制项。包没有反射
  面，`PublishAot` + `EnableTrimAnalyzer` 下发布无裁剪/AOT 警告；`net11.0`
  使用它的 `net9.0` 资产，不额外引入依赖。
- 删除 `HostCore/native/libzstd.dll`。`build.bat` 不再检查/复制
  `libzstd.dll`，只把 `SupermodelHostCore.dll` 放进 `dist`；`CMakeLists.txt`
  的 SDL3 POST_BUILD 也删掉对应拷贝；`HostCore/run-test.bat` 不再为测试 exe
  准备该 DLL。
- 发布产物 `HostCore/publish` 现在只有 `SupermodelHostCore.dll`（约 3.94 MB，
  比之前 3.43 MB 多出的部分就是编进去的编解码器）和 PDB。

**二、C# 编解码与内存路径**

`ZstdCodec.cs` 保留原有 `Level = 3`：

- **存盘**：`CompressionStream` 直接包住 `ZipArchiveEntry` 的流，
  `stream.Write(ReadOnlySpan<byte>)` 从 C++ 传入的 `byte*` 视图读取，不再先
  生成一个压缩后的大数组。压缩前 `SetPledgedSrcSize()` 把原始大小写进帧头，
  行为与旧的一发式 `ZSTD_compress` 一致（外部工具可据此预分配）。编码失败
  仍不会留下坏存档：`VkState.Save` 写的是 `<path>.tmp`，任何异常都会删除它。
- **读档**：`DecompressionStream` 直接把 `core.zst` 解到 C++ 预分配的
  `Span<byte>`，不再经过 `MemoryStream` + `ToArray()`。解完后额外读 1 字节，
  检测短帧、超长帧或拼接的第二个帧；长度与 `meta.json` 的 `rawSize` 不符
  时按损坏存档处理。
- `ZstdException` 统一翻译成 `InvalidDataException`，继续由 C ABI 的
  `try/catch` 转换成 `HOSTCORE_ERR_FORMAT`，托管异常不会越过边界。

`VkState.LoadCore()` 改为 `TryLoadCore(path, meta, destination, out required)`：

- 第一次探测（`destination` 为空）在 `meta.json` 有 `rawSize` 时只读 ZIP 目录
  和 JSON，不再为了拿大小先完整解压一遍；第二次调用直接解进目标缓冲区。
- 原始 `.st` 仍由 `IsZip()` 嗅探，然后 `FileStream.ReadExactly()` 读进同一块
  目标缓冲区；`*.zst*` / `*.vkstate` 的枚举、`meta.json` 源生成器、缩略图
  读取、`Rename()`、`List()` 均未改动。
- 只有缺少 `rawSize` 的早期容器会退回到 `ArrayPool` 缓冲区：用
  `ZSTD_decompressBound` 得到上界，再用 `Decompressor.Unwrap(frame, dest)`
  写入目标；这条路径保证老 `.vkstate` 仍能打开。

`Exports.cs` 的 `HostCore_LoadVkState` 只改了内部实现：它把 C++ 指针和容量
包装成 `Span<byte>` 传给 `TryLoadCore`，导出函数名、参数、返回值和
`BufferTooSmall` 两段式协议完全保持原样，C++ 端零修改。

**三、验证**

- `dotnet publish HostCore/SupermodelHostCore.csproj -c Release -r win-x64`：
  成功，无裁剪/AOT 警告；`HostCore/publish` 中没有 `libzstd.dll`。
- `HostCore/run-test.bat`：`ALL ABI TESTS PASSED`（2 MB 状态压缩/解压往返、
  列表、改名、旧 `.st` 转换）。
- 旧存档：`dist/.../Saves` 下 `fvipers2.zst2/.zst3`、`lamachin.zst0`–`zst7`
  全部读出，`RawSize` 与 `meta.json` 一致；`fvipers2.zst2` 的解压结果与
  7-Zip 的参考 zstd 解码器逐字节相同（SHA-256
  `7e9ad1cd…f940fe90`）。
- 真实状态：`daytona2.st1`（30,953,636 B）→ 6,473,022 B，压缩 97 ms；
  读回 SHA-256 与原始 `.st` 相同；把新容器里的 `core.zst` 交给 7-Zip 解码，
  结果同样逐字节一致，确认帧符合 RFC 8878。
- 旧 `.st`：`daytona2.st1` 直接读入，大小与哈希正确。

**Release notes**

- zh：存档模块不再依赖原生 `libzstd` DLL，改用纯托管 `ZstdSharp.Port` 并编进
  Native AOT 镜像，部署只需 `SupermodelHostCore.dll`；旧 `.zst` / `.vkstate`
  与 `.st` 存档继续可读，31 MB 状态仍压缩到约 6.4 MB（~0.1 s）。
- en: The save module no longer ships a native libzstd binary. The pure managed
  ZstdSharp.Port is compiled into the Native AOT image, so
  `SupermodelHostCore.dll` is self-contained. Existing `.zst` / `.vkstate` and
  `.st` saves still load, and a 31 MB state still compresses to about 6.4 MB in
  roughly 0.1 s.

### Manage saves 高度、16 个快速存档槽与游戏内 hover 缩略图

**一、Manage saves 弹窗**

设置界面右键游戏 → **Manage saves...** 的弹窗原来用 `ImGuiCond_Appearing` 设置
初始尺寸，之后仍可拖动右下角改变大小。拖到最小时卡片被压成几个像素高，窗口里
什么都看不到，只能关掉重开。

`DrawManageSavesModal()` 现在：

- 用 `SetNextWindowSize(..., ImGuiCond_Always)` 每帧重新应用
  `DisplaySize * (0.75, 0.85)`：宽度仍为 75%，高度比上一版增加 10 个百分点，
  给 16 个槽位的卡片网格留出纵向空间；设置窗口被放大/缩小时弹窗同步跟随；
- `BeginPopupModal()` 增加 `ImGuiWindowFlags_NoResize`，去掉缩放手柄并禁止
  拖拽改变大小；位置仍可移动，内容超出时由内部存档表格滚动；
- ImGui 的 `BeginPopupModal()` 本身已强制 `NoCollapse`，标题栏双击不会再把
  窗口折叠成一条。

**二、快速存档槽从 8 个扩到 16 个**

`Main.cpp` 新增 `SAVE_SLOT_COUNT = 16`，F6 循环、游戏内菜单列表和 F5/F7 的
槽位编号统一使用它。文件命名仍是 `game.zst1`–`game.zst16`（旧式
`game.st1`–`game.st16` 仍可读）；超过 16 的容器 Manage saves 仍会列出，
但快速菜单不再为它们留行。

F6 现在选择“下一个空槽”而不是“下一个数字”：`SaveSlotOccupied()` 同时检查
`.zst` 和旧 `.st`，从当前槽位之后开始扫描并在 16 个槽位内绕回；只有 16 个都
占用时才退回下一个数字，避免 F6 失去作用。F5 单独按仍然写回刚 load 的槽位，
这是“读档后继续玩、再覆盖同一存档”的正常路径；F6 则是明确的“换一个新槽”
操作。

注意槽位编号是 **1–16**，`zst0` 是导入旧 `.st0` 时留下的 0 号文件，不在快速
菜单的 16 个槽位里。因此当目录里已有 `zst0`–`zst7` 时，占用的是快速槽
1–7；从 `.zst4` load 后按 F6 会选 **8**，再按一次选 **9**，而不是 9/10。

游戏内菜单本身的光标内容区 (`OverlayBody`) 可以滚动，16 行不会把底部的
Apply / Cancel / Close 顶掉；它也不是因为菜单高度被截断才“看不到”，原先只有
8 个槽位时只是刚好排到菜单底部而已。

**三、游戏内菜单的 hover 缩略图**

`OverlaySaveSlot` 增加 `path`（实际容器/旧 `.st` 路径）和 `revision`（文件写入
时间）。鼠标停在某一行时：

- 用项目已有的 minizip (`unzip.c`) 直接读取该 `.zst` 容器里的 `thumb.png`；
  `IsZipContainer()` 先检查 ZIP 魔数，因此**只有 `.zst` 存档有缩略图**；
  旧式 `.st` 是裸 block file，没有 `thumb.png`，不会显示预览；
- `GuiDecodeImageBytes()` 复用 GUI 现有的 WIC / ImageIO / libwebp 解码；
- Vulkan 用一个独立的 command pool / fence / sampler 上传纹理，缓存键是
  `槽位 + revision`；被替换的旧纹理延迟 4 帧释放，避免上一帧的 draw list 还在
  采样时就把 descriptor 释放掉；
- 预览用 `BeginTooltip()` 定位在表格右边缘、当前行顶部，属于独立的顶层窗口，
  不受菜单 body 高度、底部按钮区域或子窗口裁剪限制；
- hover 判定改成 `IsMouseHoveringRect(..., clip=false)` 加
  `IsWindowHovered(ChildWindows)`。原因是 `TableNextRow()` 之后 ImGui 仍保留
  上一行最后一列（Actions）的 cell clip rect，默认裁剪时只有鼠标落在 x≈600
  的按钮列才会命中；这一点在旧的 8 槽布局里不容易发现。
- ImGui 后端的 `DescriptorPoolSize` 从 8 提高到 32（字体 + 16 个槽位缓存 +
  少量延迟释放的旧纹理）。

**四、验证**

- `cmake --build build-cmake --target Supermodel-VK-SDL3` 与
  `Supermodel-LegacyOG` 均编译链接通过；
- 游戏内 overlay 实测：16 行可以滚动到 16，`lamachin.zst7` hover 时右侧
  正常显示 480×360 缩略图；旧 `.st` 槽位没有缩略图；
- 在已有 `lamachin.zst0`–`zst7` 的目录里实测 F6/F5：依次生成
  `lamachin.zst8` 和 `lamachin.zst9`，没有覆盖 5/6；测试文件随后已删除；
- Manage saves 弹窗高度为 85%、宽度 75%，无缩放手柄。

**Release notes**

- zh：Manage saves 弹窗高度提高到全局窗口的 85%（宽度仍为 75%）并保持
  不可缩放；快速存档槽从 8 个扩到 16 个，F6 现在跳到下一个空槽
  （已有 `zst0`–`zst7` 时从 `.zst4` load 后按 F6/F5 会写 `zst8`、再写
  `zst9`，不再覆盖 5/6）；游戏内菜单 hover `.zst` 存档时会在右侧显示
  缩略图（旧式 `.st` 没有缩略图）。
- en: The Manage saves dialog is now 85% tall (still 75% wide) and remains
  fixed-size; quick-save slots go from 8 to 16, and F6 now selects the next
  empty slot (with `zst0`–`zst7` present, loading `.zst4` then F6/F5 writes
  `zst8`, then `zst9`, instead of overwriting 5/6); hovering a `.zst` save in
  the in-game menu shows its thumbnail on the right (legacy `.st` has no
  thumbnail).

### 游戏内统计面板：双色 OSD 与 4 套配色预设

原来的统计面板整块都是同一种琥珀色，标签和数值抢同一层注意力；在 Daytona 2 的
白色反光或霓虹招牌上，单色文字很容易和背景混在一起。本轮把绘制方式换成现代
OSD 的“标签/数值分离 + 帧率语义色”，并提供可选配色。

**一、绘制方式**

- `StatsOsd.cpp` 新增 `StatsPalette`、`StatsPair()` 和 `StatsText()`：静态标签用低调的
  天青蓝/冷灰，动态数值用琥珀/暖黄；标签和数值分开绘制，不再是一条同色文本。
- 帧率语义色按 Model 3 的 57.524 Hz 基准分三档：`>= 57.0` 薄荷绿 `#8CE99A`，
  `50.0–56.9` 暖琥珀 `#FCC419`，`< 50.0` 珊瑚红 `#FF6B6B`。`FPS`、新增的
  `Frame`（毫秒）和 `1% low` 三行共用这套阈值。
- `Cadence` 也带语义色：even 用绿色，uneven 用琥珀色，不再和普通数值一个颜色。
- 新增 `Frame` 行（`1000 / FPS` ms），和参考 OSD 的 `TIME` 对应。

**二、四套预设**

| 索引 | 名称 | 标签 | 数值 | 底板 |
| --- | --- | --- | --- | --- |
| 0 | 琥珀经典（默认） | `#56B6C2` | `#FF9E3B` | 深炭黑 72% + 6px 圆角 |
| 1 | 冷灰暖黄 | `#ABB2BF` | `#E5C07B` | 同上 |
| 2 | 高对比 | 白色 | 亮琥珀 | 近黑 86% + 8px 圆角 |
| 3 | 极简描边 | `#56B6C2` | `#FF9E3B` | 无底板，1px 黑色文字描边 |

极简描边模式用 `ImGui::GetWindowDrawList()->AddText()` 先在 `(x+1, y+1)` 画一遍
黑色半透明文字，再画主文字；这样在纯白闪光/花哨贴图上仍然是可读的。

**三、设置界面**

- Common 页的 `Statistics panel` 复选框后面增加一个下拉框，只显示当前预设名
  （如 `Amber classic`），没有额外的 “Style” 字样或 `(?)` 标记；悬停在下拉框上
  会显示一段预设说明。
- 新配置键 `StatsOsdStyle`（0–3），`DefaultConfig()` 用 allow-list 限制范围，
  `HasDedicatedUI()` 收录该键，避免它被通用控件列表重复显示。
- 选择只在下一局生效（设置窗口在游戏运行时不可见），与 `ShowStatsOsd` 的持久化
  方式一致。

**四、验证**

- `Supermodel-VK-SDL3` 与 `Supermodel-LegacyOG` 均编译链接通过；
- 游戏内实测 4 套预设：琥珀经典、冷灰暖黄、高对比、极简描边均正常绘制；
- FPS 57.6 显示绿色，`1% low` 54.7 显示琥珀色，验证动态阈值；
- 设置界面 Common 页下拉框位于 `Statistics panel (?)` 之后，文字不再被面板右边缘裁切。

**Release notes**

- zh：统计面板换成双色 OSD：天青蓝/冷灰标签 + 琥珀/暖黄数值，FPS、Frame 和
  1% low 按 57 / 50 fps 阈值自动变绿/黄/红；设置页新增 4 套配色预设下拉框
  （琥珀经典、冷灰暖黄、高对比、极简描边）。
- en: The statistics readout now uses a two-colour OSD style: cyan/slate labels with
  amber/sand values, and FPS / Frame / 1% low turn green / yellow / red at the 57 and
  50 fps thresholds. A preset dropdown on the Common settings page offers Amber
  classic, Slate & sand, High contrast and Minimal outline.

### SCSP DSP 只保留一条路径；Sega Rally 2 双 BGM 修复

**一、`LegacySoundDSP` 退休**

`DSP.md` 的结论已经明确：`SCSPDSP.cpp` 只有一份 `SCSPDSP_Step()`，新旧引擎的差异
只有 `SCSP.cpp` 里的三处。FV2 的爆音来自新引擎把 `0x7C0–0x7FF`（YMF292 规范里的
保留空洞，不是 DSP RAM）镜像写入 `MADRS`：游戏按地址顺序上传整块 DSP 映像时，
这段填充 0 会把 `0x780–0x7BF` 刚写好的偏移表清零。

本次把这条路径统一：

- `SCSP_w8()` / `SCSP_w16()` 删除 `0x7C0–0x7FF` → `MADRS[...]` 的镜像；该地址段
  直接丢弃，与硬件一致。`SCSP_w32()` 里同样的镜像也删掉了（原文只提到 w8/w16，
  但 32 位写入同样会改到这张表），并补上跨边界写第二个字的越界保护
  （`0x77E` / `0x7BE` / `0xBFE` 这类地址不再写到数组外）。32 位路径的丢弃分支
  必须是显式的 `else if (addr < 0x800) { }`：否则 `addr = 0x7C0` 时
  `(addr - 0x800) / 2` 会无符号下溢成 ~4 GB 偏移，在写入 `MPRO` 时直接崩溃。
- 删除全局 `legacySound`、`SCSP_Init()` 里的配置读取，以及 `SCSP_DoMasterSamples()`
  里重复的两套效果声道混音循环。
- `Main.cpp` 删除 `NeedsLegacySoundDSP()` 和 FV2 特判注入。`-new-scsp` /
  `-legacy-scsp` 仍被解析（外部前端不报错），ini 里遗留的 `LegacySoundDSP` 也
  仍然能读入，但内部不再有任何分支读取它；为 true 时日志会写明已忽略。
- 设置界面不再显示这个开关（`Gui.cpp` 的 `HasDedicatedUI` 加键），默认 ini 模板
  里也去掉该键，只留一句“兼容保留、已忽略”的说明。

行为上所有游戏现在都走“丢弃保留段”的路径，FV2 不再需要任何命令行参数。
没有加入 `tanh` 软削波或满幅降增益熔断（理由见 `DSP.md` 第七节）。

**二、Sega Rally 2 两首 BGM 叠加**

2019 年的修复（上游 90faeaf）已经确认 SR2 把两首单声道 BGM 分别编码进一条
立体声 MP2 的左右声道，并用 0xA0 / 0xB1 选择播放哪一首：

- 0xA0 非 0 → 用解码器的**右**缓冲（`mpegR`）喂左右两个输出；
- 0xB1 非 0 → 用解码器的**左**缓冲（`mpegL`）；
- 参数 0 → 恢复立体声。

四天后的一次重构（bb89bc8）把它改成了 `StereoMode`，并顺手做了两件错事：把
A0/B1 对应的缓冲对调；又在 `0x24`（设置结束/循环地址）里把路由重置为 `Stereo`。
SR2 先选声道再更新地址，于是每次都被重置回立体声，两首曲子同时播放。

现在恢复 2019 年验证过的语义：

- `ST_GOTA0` → `Channel0`（`mpegR`，音量 `volume[0]`）；
- `ST_GOTB1` → `Channel1`（`mpegL`，音量 `volume[1]`）；
- `ST_24_1` 不再改动路由；只有 `Reset()` 和显式参数 0 才会回到立体声。
- 枚举值 0/1/2 不变，旧存档仍能读。

**三、DSB 重采样与浮点混音**

DSB 的 32 kHz → 44.1 kHz 原来是 24.8 定点线性插值。步长
`(32000<<8)/44100` 向下取整成 `185/256 = 0.7227`，精确值是 `320/441 = 0.7256`，
所以每首 MP2 都偏低约 0.41%（约 7 音分）；线性插值本身还带来高频镜像和发闷
的音色。

- `CDSBResampler` 换成 Kaiser 窗（β = 8）加窗 sinc 的多相滤波器，64 taps。
  32 kHz : 44.1 kHz 化简为 320 : 441，441 个相位精确覆盖这个比率，位置由整数
  相位累加器推进，不存在长期漂移。截止频率取较低一方的 Nyquist（0.96 倍，给
  窗留过渡带），并逐相位归一化到直流增益 1。
- 滤波器需要右侧 32 个样本的支撑，所以 `MpegSamplesForFrame()` 的预读从 2 个
  样本增加到 `DSB_RESAMPLER_TAPS + 4 = 68` 个。每帧产出多少 44.1 kHz 样本仍由
  `GetSamplesThisFrame()` 决定，DSB 只是被要求在同一帧里产出同样多：57.524 Hz
  的动态重采样同步、`AudioLatency` 与 ring buffer 全部原样保留，因此不可能和
  SCSP 时钟走散或缓冲饥饿。45 Hz 时最坏占用 780 / 802 个样本，仍在原缓冲区
  之内。
- SDL3 输出改为 `SDL_AUDIO_F32`：SCSP 和 DSB 本来就在同一个 S16 标度的 float
  域相加，现在 ring buffer 也存 float，整数化只发生在 SDL 流交给设备的最后一
  步。全链路只有一次钳位（`ToHostSample`），不再在混音中间做 `INT16` 截断。
  SDL2 的 LegacyOG 仍输出 `AUDIO_S16SYS`，行为不变。
- 没有加入 `tanh`、软削波或任何压缩/限幅器。

**四、每游戏默认音量平衡**

真实框体上 DSB 子板到主混音之间有固定的模拟分压，模拟器一直按 100/100 平铺，
于是 Scud Race / Daytona 2 的引擎声压过 BGM，Star Wars Trilogy 又相反。
`Main.cpp` 新增内置表（先按 ROM set 名匹配，再按 `game.parent`）：

| ROM set | SoundVolume | MusicVolume |
| --- | --- | --- |
| `scud`, `scudplus` | 65 | 100 |
| `daytona2`, `dayto2pe` | 70 | 100 |
| `swtrilgy` | 100 | 70 |
| `spikeout`, `spikeofe` | 75 | 100 |

只在用户没有显式指定时生效，并且**逐键**判断：`[game]` 段与 `-sound-volume` /
`-music-volume` 任意值都算显式；全局 ini 段只有偏离默认值 100 才算显式。后者是
因为设置界面保存时会把合并后的整个全局段写回文件，一个从未动过的 100 也会出现
在 ini 里；如果按“键存在”判断，用户在 GUI 里保存过一次后所有预设都会失效。
确实希望某个预设游戏保持 100 的，可以在它自己的 `[set]` 段里写明（该段总是按
原值生效）。内置表在命令行的最终合并之前写入，所以命令行仍然拥有最高优先级。

**五、SDL3 版单机自连（Scud Race 卡在 NETWORK CHECKING）**

`Supermodel-VK-SDL3.exe` 默认不链接 SDL3_net：`Libraries` 里只有 `SDL2_net`，
CMake 的 `USE_SDL3_NET` 默认 OFF，目标因此带着 `SUPERMODEL_NO_NETWORK=1`，
`TCPSend` / `TCPReceive` 是空实现。`CSimNetBoard` 的 `ConnectProc` 会一直等
`Connected()` 返回 true，所以 Scud Race / Daytona 2 这类带网络板的游戏会永远停在
NETWORK CHECKING。ini 里的 `PortIn = PortOut = 1970`、`AddressOut = 127.0.0.1`
本身是正确的自连写法，不是配置问题。

- `CSimNetBoard::Init()` 现在识别自连（`PortIn == PortOut` 且
  `AddressOut == 127.0.0.1`），不再创建 `TCPSend` / `TCPReceive`，也不启动连接
  线程。
- `testing` 状态直接以 `numMachines = 0`、`machineIndex = 0` 进入 `ready`，
  type 1（Scud Race）与 type 2（Daytona 2、Le Mans、Virtual-On）都处理。
- `ready` 状态把本该由回环送回的 segment 直接 `memmove` 到 peer 槽位，等价于
  自连时的一次 TCP 往返。
- 日志会写 `Net board: local loopback (single machine, port 1970).` 与
  `Net board: link ready (1 machine(s)).`，排查网络问题不用再猜。
- 真正的多机连接（`PortIn != PortOut` 或非 127.0.0.1）不受影响；SDL2 版仍然
  走 SDL2_net。

> 完整的工程记录（滤波器频响、逐帧记账、崩溃定位过程、验证矩阵）见
> `sdl3_reports/2026-10-22-06-audio-scsp-dsb-network.md`；可复用的排错模式见
> `FIXES.md` 案例 46。

### Batch 1：MSAA 视线检测、CRT 色温、2D 放大与半透明顺序

这是 Vulkan 平替 LegacyOG 三步路线的第一批，只修正确性，不动界面样式、
存档、帧同步统计等既有优化。

**一、MSAA 下恢复 LOS**

- 根因：多重采样深度图不能 `vkCmdCopyImageToBuffer`，`D32_SFLOAT` 也不支持
  resolve，`CVulkan3D::CopyLos()` 在 `m_samples > 1` 时直接返回。
- 新增 `Shaders/los.comp`：单个 1×1 工作组的 compute shader，用
  `texelFetch(sampler2DMS, …)` 逐 sample 读取 LOS 坐标处的深度，取最大值
  （深度清 0 + GREATER，最大值即最近几何；平均会把被边缘覆盖的 sample 和
  清屏值混成一个硬件从未产生过的深度），写入原 `m_los` host-visible SSBO
  的同一槽位，`GetLosValue()` 接口与读回逻辑不变。
- `CVulkan3D` 新增 LOS compute 管线、descriptor set 与 sampler；MSAA 时
  深度图增加 `VK_IMAGE_USAGE_SAMPLED_BIT`。构造函数改为用真实 usage 探测
  格式/采样数，并检查所选队列是否有 COMPUTE，任一不满足就回退 1x 并写日志，
  而不是开着 MSAA 静默丢掉 LOS。
- `CopyLos()`：MSAA 走
  `DEPTH_ATTACHMENT → SHADER_READ_ONLY → dispatch → SHADER_WRITE→HOST_READ
  屏障 → DEPTH_ATTACHMENT`；1x 的 "copy image to buffer" 路径原样保留。

**二、CRT 色温与 2D 放大滤波**

- `PostPush` 增加 `crtColors`；`ReadPostSettingsFromConfig()` 读取 `CRTcolors`
  （0–5，越界按 0），非 0 时也切到 post 离屏路径。
- `post.frag` 内联 SuperAA 的 5 组矩阵与 gamma 规则：先按管子的 gamma
  （2.5 / 2.25）或 sRGB 解码到线性，`linear *= matrix`（与 OpenGL 的
  `finalColor *= colmatrix` 完全一致，避免转置差异），再编码回 sRGB；位置在
  FXAA/glow 之后、scanline/mask/gain 之前。
- `CVulkan2D::Init()` 现在真正接收 `UpscaleMode`，原生 496×384 时强制
  Nearest（与 `CRender2D` 相同）。composite 管线新增 nearest sampler
  （binding 2）与 biquintic/bicubic 分支，Tilegen 两层按模式采样；3D 场景与
  机柜背景固定双线性。
- `ScopeOfSetting()` 解除 `CRTcolors`、`UpscaleMode`、`Crosshairs`、
  `CrosshairStyle` 的 OpenGL-only 标记：前两项本轮已实现，后两项 Vulkan 早已
  原生绘制。此前 Pure-Vulkan 构建会把这些设置整行隐藏。

**三、半透明提交顺序**

- `CR3DSceneBuilder::CacheModel()` 不再用 `unordered_map` 的迭代顺序生成 mesh
  列表：meshes 放入按首次提交顺序增长的 vector，每批记录 `order`（首个多边形
  的提交序号），落地前按 `order` 稳定排序。
- 可能进入 trans1/trans2 的材质（`textureAlpha` / `polyAlpha` / `transLSelect`，
  三者都在 `PolyHeader::Hash()` 里）按连续 run 拆批，避免同一状态的非连续 run
  被合批后越过中间提交的几何；不透明材质仍按状态合批，draw call 增量只落在
  透明几何上。车窗、烟雾、火焰不再随哈希表扩容换序闪烁。

**四、细节**

- `SCSP_r16()` 的 `0x7C0–0x7FF` 保留区改为返回 0，与写路径和
  `SCSP_r8()` 严格对称。

**验证**

- 全部 Vulkan 着色器用 `Scripts/CompileVulkanShaders.cmd` 重编，
  `los.comp` 已加入 `.cmd` / `.sh` / `CheckShaderHeaders.ps1`；
  `CheckShaderHeaders.ps1` 通过。
- 全新 CMake/Ninja 构建树：`Supermodel-VK-SDL3` 369/369、
  `Supermodel-LegacyOG` 399/399 链接成功。本项目源码 0 error / 0 warning；
  唯一一条 C4819 来自 bundled SDL2 的 `SDL_joystick.c`，是既有第三方告警。
- 冒烟测试：Daytona 2，`MSAA=4` + `CRTcolors=1` + `UpscaleMode=1`
  （双五次），连续跑 45 s 无崩溃、无 Vulkan 报错；日志确认 4x MSAA、
  深度可采样、COMPUTE 队列可用，离屏合成与后处理路径正常。

**Release notes**

- zh：开启 MSAA 后视线检测不再失效；Vulkan 新增 5 组 CRT 色温矩阵和
  双五次/双三次 2D 放大；半透明车窗与烟雾按硬件提交顺序混合；准星、CRT 色彩
  与 2D 放大设置不再被误标为仅 OpenGL。
- en: Line-of-sight detection now keeps working with MSAA. The Vulkan backend
  gains the five CRT colour-temperature matrices and biquintic/bicubic 2D
  upscaling, and translucent surfaces blend in hardware submission order.
  Crosshair, CRT colour and 2D upscale settings are no longer marked OpenGL-only.

### 透明层独立缓冲合成：Daytona 2 瀑布后方蓝块

**一、问题**

Daytona 2 瀑布左侧山体在特定机位出现整块蓝色。之前误判为 CRT 荫罩噪点；关掉
`PostCRT` 后蓝块仍在，且 OpenGL（New3D）同存档为绿色，说明是 Vulkan 独有的
渲染问题。蓝块随相机位置出现/消失，像"后面一层没被盖住"。

**二、根因**

Real3D 的 LOD 交叉淡化同时绘制两个 LOD（alpha 约 0.058 与 0.942），在 New3D
中分别写入 trans1 / trans2 两张独立缓冲，合成时只要两层都覆盖就强制不透明。
Vulkan 原来把两层依次用 `SRC_ALPHA/ONE_MINUS_SRC_ALPHA` 混合进同一目标，
互补 alpha 会留下 `0.058 × 0.942 ≈ 5.5%` 的背景；又因 trans2 用 `GREATER`
且 trans1 先写深度，trans2 在相同深度被整层挡掉，只剩 5.8% 的山体，蓝块更大。

**三、修法**

- `CVulkan3D` 新增 `m_trans1` / `m_trans2` 及 MSAA resolve 目标；3D 管线关闭
  混合，透明层分别写入自己的缓冲（后画覆盖先画）。
- 透明层深度比较改为 `GEQUAL`，并**先画 trans2（主层）、再画 trans1（淡出层）**。
  MSAA 深度图不能复制，无法像 New3D 那样在两层之间恢复深度，因此让主层先写，
  避免被淡出层遮挡。
- `composite.frag` 采样两张透明层（slot 4/5），按 New3D 的公式合成后与场景色
  结合：两层都覆盖时结果不透明，否则按覆盖层 alpha 混合。

**验证**

- Daytona 2 存档帧（蓝块现场，`(174,573) 220x239`）：蓝占优 42.0% → 0.0%，
  与 New3D 平均差 61.5 → 2.1；灰白占比 19.3% 与 New3D 18.9% 一致（游戏本身的
  岩石/瀑布内容）。
- Daytona 2 帧 1150：瀑布区蓝占比 2.29% vs New3D 2.32%，与修复前平均差 1.17。
- LostWorld `lostwsga.st5` 第 300 帧：暗部 48.0% vs OpenGL 48.6%（旧修复版
  54.5%），与 OpenGL 平均差 16.53（旧版 18.45），透明层深度写入无回归。
- 完整报告见 `sdl3_reports/2026-10-22-07-transparency-layers.md`。

**Release notes**

- zh：Vulkan 的 trans1/trans2 改为独立缓冲并按 New3D 的公式合成，Daytona 2
  瀑布后方的整块蓝色消失；LOD 交叉淡化、车窗和烟雾的透明顺序与 OpenGL 一致。
- en: Vulkan now renders trans1/trans2 into separate targets and composites them
  with New3D's formula, removing the blue block behind Daytona 2's waterfall and
  matching the OpenGL blend order for LOD cross-fades, windows and smoke.

### MSAA 接缝黑点：护栏墙棱线上的黑色虚线

**一、问题**

Daytona 2 瀑布附近的混凝土护栏，在 MSAA 4x 下沿墙顶与墙面的接缝出现一条
细的黑色虚线：单个像素、灰度约 131，放大 4 倍非常明显；同一存档把 MSAA 设为
关闭后，这些像素与 New3D 逐像素一致（254,255,255）。

**二、根因**

MSAA 下，一个像素可能被两个面共同覆盖（墙顶面与墙面是相邻、几乎共面的两个
draw）。片元只在像素中心着色，中心落在墙面多边形之外时，插值出的 UV 已经越过
该面在贴图页里的矩形，采到相邻矩形的暗色 texel；这个颜色又被 4 个采样点共同
使用，于是出现整像素的暗点。New3D 单采样时，中心在墙顶上的像素根本不会产生
墙面片元，因此看不到这些点。

**三、修法**

- `real3d.vert` / `real3d.frag` 的全部 7 个 varying（`vView`、`vNormal`、`vUv`、
  `vColour`、`vFixedShade`、`vFaceDot`、`vLodBase`）改为 `centroid` 插值。
  部分覆盖的像素改为在三角形内部取插值点，UV 不再越界；顶点输出与片元输入
  的限定符必须一致。
- 重新生成着色器头（`Scripts/CompileVulkanShaders.cmd`，
  `CheckShaderHeaders.ps1` 通过）。

**验证**

- 黑点像素 `(1502,1059)`、`(1516,1064)`、`(1525,1067)` 等：修复前 (131,132,132)，
  修复后 231–254，与周围墙顶一致。
- 墙体边缘区域（`1400,980 750x270`）中比 New3D 暗 60 以上的像素：1024 → 823，
  其余是轮廓抗锯齿的正常过渡，不再是孤立黑点。
- 整帧与修复前相比只有 0.44% 像素变化，全部沿多边形边缘；瀑布蓝块区域
  （`174,573 220x239`）平均 BGR 与蓝占比完全不变。
- 回归：LostWorld 第 300 帧 0.14% 像素变化（平均差 0.09），暗部占比 50.2%
  不变；Daytona 帧 1150 蓝占比 4.78% 不变，与 New3D 平均差 6.76 → 6.81。
- 完整报告见 `sdl3_reports/2026-10-22-08-msaa-centroid-seams.md`。

**说明**

墙顶与灌木等背景交界的**轮廓**仍会有 MSAA 抗锯齿产生的深浅交替像素，放大后
看是一条断续的边；这是抗锯齿本身，和上面的贴图越界黑点不是一回事。要与
OpenGL 无 AA 的画面完全一致，把 MSAA 设为「关闭」即可。

**Release notes**

- zh：修复 MSAA 下护栏、墙体棱线与接缝出现的黑色虚线（边缘像素的 UV 越界
  采到相邻贴图的暗色 texel）；轮廓抗锯齿不受影响。
- en: Fixed the dotted black lines along guardrail and wall seams with MSAA: edge
  fragments were sampling a neighbouring texture rectangle at the polygon border.
  Silhouette antialiasing is unchanged.

### 清理排查脚手架：恢复微纹理混合与 CRT 荫罩孔尺寸

**一、问题**

排查蓝块和接缝时在源码里留下了一批 TEMPORARY 诊断钩子，其中两处**直接改变
默认画面**：

- `real3d.frag` 把微纹理混合强度强制为 `blendFactor = 0.0`；
- `post.frag` 把 CRT 荫罩孔半径固定为 `0.40`，不再随亮度变化。

第一项影响所有带 microtexture 的表面（该存档帧有 92 个 draw），使 Vulkan 与
New3D 的平均差明显变大；第二项只在开启 CRT 滤镜时可见。

**二、修法**

- 恢复微纹理混合，与 New3D `R3DShaderCommon.h` 的公式一致。
- 恢复束流相关孔半径 `0.24 + 0.28 * clamp(luma, 0.0, 1.0)`（设计说明见
  `TECHLOG.md` 3.3）。
- 删除本次调查用的全部环境变量钩子与日志：`SM3D_FREEZE_FRAME` /
  `SM3D_CAP_FREEZE`、`SM3D_CAP_*`、`SM3D_VK_*` 系列、draw-ID 与材质 dump、
  `R3DPush.debug`、着色器调试分支；`R3DPush` 从 224 字节回到 208 字节。
- 保留原有开发者钩子：`SM3D_FRAME_TIMING`、`SM3D_OVERLAY_OPEN`、
  `SM3D_SAVE_MANAGER`、`SM3D_AUTOSTART`。
- 保留所有修复：透明层双缓冲与合成公式、`GEQUAL`、colour 层 alpha 强制为 1、
  `centroid` 插值、post 的单元中心亮度采样。

**验证**

- Daytona 2 蓝块存档帧：整帧与 New3D 平均差 **5.34 → 3.02**（均开启
  centroid）；蓝块区域保持不变（2.203）。
- Daytona 2 帧 1150：与 New3D 平均差 **6.81 → 4.90**，瀑布蓝占比 4.78%
  不变（New3D 4.92%）。
- LostWorld 第 300 帧：仅 0.08% 像素变化，暗部占比 50.2% 不变。
- 接缝黑点仍为 231–254，`centroid` 修复保留。
- `grep -rn "TEMPORARY\|DIAGNOSTIC" Src/` 为 0；诊断构建与 `build.bat` 均
  通过；无钩子运行 15 秒无报错。
- 完整报告见 `sdl3_reports/2026-10-22-09-cleanup-microtexture.md`。

**Release notes**

- zh: 清理排查期间留下的诊断代码，并恢复被诊断开关关掉的微纹理混合与
  亮度相关的 CRT 荫罩孔尺寸；Daytona 2 与 OpenGL 的平均差从 5.34 降到 3.02。
- en: Removed the investigation-only diagnostics and restored microtexture blending
  and the brightness-dependent CRT mask hole size they had disabled. The Daytona 2
  frame difference from OpenGL drops from 5.34 to 3.02.

### 设置界面：CRT 色彩与 2D 放大移到「画面」页

**一、问题**

`CRTcolors` 和 `UpscaleMode` 都属于画面选项，但它们在 ini 里归在 `[Video]` 组，
又被通用控件渲染，于是落到了「核心」页的「显示与运行」列表中，夹在无边框
窗口、帧率和刷新率之间。两项都没有说明文字，CRT 色彩尤其容易让人以为它是
模拟核心选项。

**二、改法**

- 把 `CRTcolors` / `UpscaleMode` 加入 `HasDedicatedUI`，通用控件不再绘制它们。
- 新增手写控件放在「画面」页顶部，CRT 滤镜上方，分组标题「色彩与缩放」。
- 两项各加 (?) 说明：CRT 色彩说明各预设的色温/色域，以及 Vulkan 即时生效、
  OpenGL 需要重新开一局；2D 放大说明四种滤镜的取舍，并注明需要重开一局。
- 游戏内面板（Tab）顶部也加入这两项，可在跑图时直接调。Vulkan 下两项都
  即时生效：`CRTcolors` 本来每帧读取，`UpscaleMode` 改为在
  `CVulkan2D::RefreshPostSettings()` 里每帧读取（原生 496x384 仍强制最近邻）。
  OpenGL 仍需重开一局，面板里会多一行提示，且两项已加入 `kOverlayKeys`，
  点「撤销」能恢复。
- 修正一处过期注释：分辨率与全屏在「常用」页，不在「画面」页。
- CRT 色彩与 CRT 滤镜预设各补一句说明：前者只改颜色（色温/色域），
  后者只改扫描线/荫罩几何与亮度补偿，两者可自由组合。

**验证**

- 英文界面截图：Video 页顶部为 Colour & Scaling → CRT Colour / 2D Upscale，
  下方是 CRT Filter；Core 页不再出现这两项。游戏内面板顶部同样显示这两项，
  无 (?) 标记。
- SDL3 诊断构建通过；`Supermodel-LegacyOG` 与 `Supermodel-VK-SDL3` 均重新打包。

**Release notes**

- zh：把「CRT 色彩」和「2D 放大」从「核心」页移到「画面」页的 CRT 滤镜上方，
  并加入游戏内面板顶部；Vulkan 下两项都可实时调节。
- en: Moved CRT Colour and 2D Upscale from the Core tab to the top of the Video tab,
  above the CRT filter, and added them to the top of the in-game panel. Both are
  live under Vulkan.

### 修复：游戏内 Apply 不保存两项新设置，返回设置界面也不刷新

**一、问题**

在游戏内面板调整 CRT 色彩 / 2D 放大后点「应用」，退出游戏回到设置界面，
Video 页仍然显示旧值，配置文件里也没有写入。

**二、根因（两处）**

1. `SaveOverlaySettings()` 只把一份写死的键列表从运行配置写回 ini，新加的
   `CRTcolors` / `UpscaleMode` 不在列表里，所以 Apply 根本没有保存它们。
2. `GuiRunOnce()` 使用窗口自己的 `state->config`，从游戏返回时不会重新读取
   配置文件。即使 Apply 已经写盘，设置界面仍显示开局前的旧值；更糟的是，
   下一次保存会用旧值覆盖刚写入的文件。

**三、修法**

- 把 `CRTcolors` / `UpscaleMode` 加入 `SaveOverlaySettings()` 的持久化列表，
  并注明需与 `Overlay.cpp` 的 `kOverlayKeys`（Cancel 快照）保持一致。
- 新增 `LoadGuiConfig()`，把「全量配置 + 全局段合并 + 纯 Vulkan 渲染器覆盖 +
  macOS ROM 路径修复」集中到一处；`GuiCreate()` 首次加载和 `GuiRunOnce()`
  从游戏返回后的重载都用它。
- `GuiState` 新增 `firstRun`：只有从游戏返回时才重读文件，首次启动不会因重读
  丢失运行期修复（如强制 Vulkan、ROM 路径回退）。

**验证**

- 构建：SDL3 诊断构建与 `build.bat`（两个 exe）均通过。
- 现在 Apply 会写入 `CRTcolors` / `UpscaleMode`；返回设置界面后 Video 页
  读取的是文件里的新值，后续保存也不会再用旧值覆盖。
- Close（不点 Apply）仍然不写盘，返回后按文件值显示，行为不变。

**Release notes**

- zh：修复游戏内「应用」不保存 CRT 色彩/2D 放大，以及退出游戏后设置界面
  Video 页不刷新的问题。
- en: Fixed the in-game Apply button not persisting CRT Colour and 2D Upscale, and the
  settings window showing stale Video values after a game.

### 统计面板：CPU 频率改为实时采样（P 核）

**一、问题**

统计面板的 CPU 行只显示线程数与占用，频率一直是「-」。GPU 频率能显示是因为
NVML 直接给出当前时钟；CPU 用的 `CallNtPowerInformation(ProcessorInformation)`
在当前这一代混合架构 CPU（Core Ultra）上返回的是标称时钟（P 核 1200 MHz、
E 核 700 MHz），不是实时 boost。此外拓扑探测把 CPU set 的 `Id`（256..271）当成
逻辑处理器号存进了 `performanceIds`，而电源 API / PDH 的下标是
`LogicalProcessorIndex`（0..17），两者对不上，采样永远为空。

**二、修法**

- 修正拓扑探测：用 `LogicalProcessorIndex`（且只取 group 0）作为逻辑处理器号；
  `Id` 只是 CPU set 自身的句柄。
- 新增 PDH 实时采样：`Processor Information(*)\% Processor Performance` × 标称
  时钟 = 实时频率。这是 Task Manager 同源、免驱动的办法；HWMonitor /
  MSI Afterburner 靠内核签名驱动读 MSR `IA32_APERF` / `IA32_MPERF`，本项目
  不引入驱动。
- 只对 P 核取平均：跳过停靠核返回的 0、唤醒核的瞬时尖峰（钳位到 600%），并
  丢弃第一次预热采样。混合架构在频率前标一个 `P`。
- GPU 行改为两行：第一行显示型号（NVML `nvmlDeviceGetName`），下一行缩进显示
  频率/占用/显存；OSD 窗口用 `AlwaysAutoResize`，加行会自动增高，不裁剪。

**验证**

- Ultra 5 125H（8P+10E）游戏内采样：P 核平均约 4170 MHz（此前为「-」）。
- SDL3 诊断构建与 `build.bat`（两个 exe）均通过。

**Release notes**

- zh：统计面板的 CPU 频率改为 PDH 实时采样，混合架构显示 P 核平均频率
  （游戏内约 4.2 GHz），不再显示「-」；GPU 行先显示型号，再换行对齐显示
  频率与显存占用。
- en: The stats OSD samples the CPU clock through PDH and shows the average over the
  performance cores (about 4.2 GHz in-game) instead of a dash. The GPU line now shows
  the model name with the clock and VRAM figures on the next line.

## 2026-10-21

### 指针形状、启动图黑框、渲染器一行、LegacyOG 启动崩溃

**一、自绘平台层补上鼠标指针形状**

`ImguiPlatform.cpp` 取代了 `imgui_impl_sdl2/sdl3`，但没有实现官方后端里的
`UpdateMouseCursor()`，于是所有 `ImGui::SetMouseCursor()` 都看不见效果：封面与
游戏列表之间的分隔条不显示上下箭头，输入框不显示 I-beam。拖动逻辑本身一直是
好的（模拟拖动能把 `GuiPreviewSplit` 改掉），只是看不出来那条细缝可拖。

- `Platform.h` 新增 `enum class SystemCursor` 和 `SetSystemCursor(Window*, SystemCursor)`；
  SDL2 / SDL3 各实现一份，`SDL_CreateSystemCursor` 的结果按形状缓存，且只在形状
  真的变化时才调用 `SDL_SetCursor`（SDL 自己没有 early-out，见 SDL issue #6113）。
- `ImGuiPlatformNewFrame()` 末尾按上一帧的 `ImGui::GetMouseCursor()` 选形状。它和
  官方后端一样在 `ImGui::NewFrame()` **之前**执行：`NewFrame()` 会把请求重置成
  Arrow，放在之后就只能读到默认值。
- 指针从隐藏恢复显示时（例如从全屏游戏回到游戏内面板）先复位成箭头，否则设置
  界面最后用过的缩放/文本光标会被带进游戏。

**二、启动图后面弹出黑框**

Vulkan 版为了让隐藏窗口的 swapchain 能 acquire，在设备创建后立刻 `ShowWindow`；
但第一帧还要约 400 ms（字体图集、封面解码上传、首帧管线），这段时间窗口客户区
是黑的。启动图是 1294×1294 的正方形，窗口是 1920×1080，盖不住，于是图片两侧
露出黑边。

- 设置窗口改为一直隐藏到第一帧真正 present 之后（`GuiRunOnce()` 里
  `HideStartupSplash()` + `RaiseSettingsWindow()` 仍在同一帧完成），用户只看到
  启动图直接换成界面。
- 驱动确实无法对隐藏窗口 acquire 的情况保留兜底：`GUI()` 在 `BeginFrame()` 失败时
  置 `g_guiFrameSkipped`，循环看到它就提前显示窗口并记一行日志，不必等到 5 秒保底。
- 实测：首帧 acquire 0 ms、绘制+present 374–456 ms，全部发生在窗口隐藏期间；
  以 60 Hz 采样 89 帧，黑边区域无暗帧。

**三、去掉灰色不可选的渲染器一行**

两个可执行文件已经在编译期各自只含一套渲染器，界面上再用一个置灰的单选
按钮加一句说明去“告知”读者，是多余的一行：

- 纯 Vulkan 版不再画灰色的 `Vulkan 3D` 与 “This build contains only the Vulkan 1.3
  renderer.”；其下直接是校验层、显示设备等真实设置。
- LegacyOG 不再画灰色的 `OpenGL (Legacy Compatibility)`；画面页直接从 `New 3D` /
  `Legacy 3D` 开始（两者都是 OpenGL 引擎，需要保留）。
- 同时删掉那句硬编码中文“此版本只包含 OpenGL 渲染管线。”——它没有对应的英文
  条目，在全英文界面里是唯一一处漏出来的中文；`Localization.cpp` 里随之失效的
  Vulkan 版本条目也一并删掉。

**四、LegacyOG 用全新配置启动即崩溃**

CMake 版 `Supermodel-LegacyOG.exe` 在 ini 里没有 `VRCycle` 时直接 `abort()`
（0xC0000409），日志停在 `GUI: interface ready`；异常信息是
`Node "VRCycle" does not exist`。

`VRCycle` / `VRDefaultView` 是**输入**设置，却被和 MSAA、后处理键一起放进了
`#if !defined(SUPERMODEL_OPENGL_ONLY)` 里，而设置界面「控制」页无条件读取它们。
发布包里的 ini 早就写入了这两个键，所以只有全新安装、干净配置会崩。已把两个键
移回公共默认配置。

**五、封面预览最多占一半**

左列的分隔条可以把封面预览拉得很高，只剩一条两三行高的游戏列表。现在预览上限
是左列高度的一半（`minListHeight = splitHeight * 0.5f`），原来的固定像素下限
（`140 * uiScale`）在大窗口下根本拦不住。加载时也按同一上限夹紧，旧的 ini 里
存的 0.59 之类会在下次启动时被改成 0.5。

**六、按 set 自动选用旧版 SCSP DSP**

上游对 Fighting Vipers 2 的建议是“给这个游戏加 `-legacy-scsp`”。现在改成内置：
`Main.cpp` 新增 `NeedsLegacySoundDSP(game)`，按 set 名**或 parent 名**匹配（`fvipers2o`
的 `parent="fvipers2"`，克隆集自动继承），在加载完游戏、合并 ini 的游戏 section
之前注入 `LegacySoundDSP = true`。优先级仍然清楚：命令行的 `-new-scsp` /
`-legacy-scsp` 和 ini 里 `[fvipers2]` 的显式值都会覆盖它。

顺带把两个引擎的差异查清楚了——它们共用同一个 `SCSPDSP.cpp`，`SCSP.cpp` 里只
有两处不同：

- DSP 内存写入：新版把 `0x7C0–0x7FF` 当作 MADRS 镜像再写一遍（符合硬件），旧版
  不处理；
- 效果声道音量：`Enc = (EFPAN << 8) | (EFSDL << 13)` 是新版，旧版写的是 `<< 14`，
  而 EFSDL 是 3 位——移 14 位会溢出到第 16 位被截掉，实际变成
  `(EFSDL & 3) << 1`，即音量档位 {0,2,4,6}。旧版反而是“算错了”才发出 FV2 想要的
  声音。

把这一行改成 `<< 13` 后（两个引擎混音完全一致），用户实测 FV2 大坝关胜利画面
**依然没有爆音**，说明爆音与电平无关，而是第三个差异：新引擎把 `0x7C0–0x7FF`
的写入镜像进 `MADRS`。临时探针显示 FV2 是按地址顺序上传整块 DSP 映像：
`0x700–0x7B9` 是真实值，`0x7BA–0x7FF` 全是 0，之后才写 `0x800–0xBF8` 的微程序。
镜像写入等于把刚填好的 MADRS 偏移表整张清零，DSP 于是反复读同一段数据、
越叠越响。

同时修掉了旧引擎字写路径的越界：`MADRS[32]` 后面紧跟着 `MPRO[0..31]`，
`0x7C0–0x7FF` 算出的下标是 32–63，会把 DSP 微程序前 8 条指令改掉。现在与字节
路径一致地丢弃。对 FV2 无影响（微程序是在这些清零之后才上传的，日志顺序可证）。

详细的现象、证据、推断与后续建议写在 `DSP.md`。

**七、按 ESC 回设置界面后无响应并闪退（volk 的全局函数指针）**

症状：从游戏按 ESC 回到设置界面，几秒后窗口 not responding，然后进程消失，
退出码 0xC0000005，故障模块永远是 `ntdll.dll`。

依次在 `GuiRunOnce()` 入口、ImGui 帧开始/结束、`ImGui::Render()`、
`UiVulkanBackend::Render()` 里布探针后，位置定在一行：`vkQueuePresentKHR`。
游戏会话中 `vkAcquireNextImageKHR`、`vkEndCommandBuffer`、`vkQueueSubmit2`
都正常，只有 present 崩。

原因是 **volk 把入口点存在进程级的全局变量里**。设置窗口的 `CVulkanContext`
初始化时 `volkLoadInstance/LoadDevice` 绑好自己的实例与设备；游戏会话又新建一个
`CVulkanContext`，把这批全局指针改绑到自己的 `VkInstance`；会话结束销毁它时，
全局指针仍然指向**已销毁实例**的 loader 跳板。设备级函数（acquire/submit）
靠传入的句柄取 dispatch，所以还能用；`vkQueuePresentKHR` 是实例级函数，
跳板已失效 → 在 ntdll 里挂。

修复：`CVulkanContext` 新增 `RebindFunctionPointers()`（`volkLoadInstance` +
`volkLoadDevice`），`GuiRunOnce()` 开头调用一次。这样无论哪种回到设置界面的
路径（游戏结束、启动图结束、会话中途失败）都会重新绑定。

顺带加了一个开发者钩子 `SM3D_AUTOSTART=<set>`（与 `SM3D_SAVE_MANAGER` 同类）：
首帧直接把指定 set 当作双击启动，方便脚本化测试，不依赖窗口尺寸推出行坐标。

**验证**

- `build-cmake.bat -DUSE_SDL3=ON` 与 `build.bat` 均构建通过，`dist` 已重新打包。
- 自动启动 `scud` → 运行 6 s → ESC：回到设置界面后 15 s 内窗口无一次 not
  responding（`IsHungAppWindow` 采样全为 false），进程正常退出。
- LegacyOG 用不含 `VRCycle` 的 ini 启动，并逐个点过右侧标签页不再崩溃。
- SDL3 版停在分隔条上：系统光标为 `IDC_SIZENS`，拖动后 `GuiPreviewSplit` 落盘。

### SDL2 反向剥离：Supermodel-LegacyOG（纯 OpenGL）

为了和纯 Vulkan 的 `Supermodel-VK-SDL3.exe` 彻底分工，原 SDL2 目标改名为
**`Supermodel-LegacyOG.exe`**，并在编译期移除全部 Vulkan 代码。两个二进制现在
各自只含一套渲染器，不再有“运行时二选一”的双轨维护。

**构建目标与源集**

- `CMakeLists.txt` 的源列表拆成三段：`SUPERMODEL_CORE_SOURCES`（无图形 API）、
  `SUPERMODEL_CORE_VULKAN_SOURCES`（Vulkan3D 六个单元 + `volk_impl.cpp` +
  `imgui_impl_vulkan.cpp`）、`SUPERMODEL_CORE_GL_SOURCES`（保持不变）。
- `supermodel_add_core(target mode)` 改为 `OPENGL` / `VULKAN` 两态：
  Vulkan 头文件目录只挂在 Vulkan 核心上，OpenGL 目标连 `<vulkan/vulkan.h>`
  都包含不到。
- LegacyOG 目标不再编译 `VulkanContext.cpp`、`PlatformVulkanSDL2.cpp`，
  并从链接线上去掉 volk；SDL2 子构建里 `SDL_VULKAN=OFF`。
- 新增编译期裁剪宏 `SUPERMODEL_OPENGL_ONLY`，与 `SUPERMODEL_PURE_VULKAN`
  互斥，两个文件各加了一道 `#error` 保险。
- MSVC 工程同步：删除 10 个 Vulkan 源文件、去掉 Vulkan 头目录、删除 4 个
  `CheckShaderHeaders.ps1` 预编译事件，输出改名 `Supermodel-LegacyOG.exe`；
  `build.bat` / `build-cmake.bat` 的产物名同步。

**界面与配置**

- 「常用」页渲染器一行锁定为 `OpenGL (Legacy Compatibility)`（置灰），
  New 3D / Legacy 3D 仍可选——两者都是 OpenGL 引擎。
- 移除 Vulkan 校验层、Vulkan 显示设备、硬件 MSAA、FXAA/CRT/荫罩/辉光整条
  后处理链、机柜背景图；超采样、CRT 色彩、2D 放大、整数倍缩放等 OpenGL
  参数保持原样。
- `DefaultConfig()` 不再生成上述键。新增 `Util::Config::StripRetiredVulkanSettings()`，
  在启动、每局会话、GUI「从文件加载设置」和游戏内面板 Apply 四条路径上擦除
  旧 ini 里的残留键（含游戏专属 section），避免脏配置被写回。
- 命令行 `-vulkan3d` / `-vulkan` / `-vk-validation` / `-vulkan-device=` 被识别后
  静默忽略，并打印一行提示，直接以 OpenGL 启动；帮助文本不再列出它们。
- 封面等资源路径未变：两个 exe 仍共用 `Data/Assets/`（含 `Boxarts/`）。

**验证**

- 同一 build 目录同时构建 `Supermodel-LegacyOG.exe` 与
  `Supermodel-VK-SDL3.exe` 通过。
- `dumpbin /dependents` 无 `vulkan-1.dll`；exe 中无 `CVulkan` / `vkCreateInstance`
  符号；`compile_commands.json` 中 LegacyOG 与 OpenGL 核心的编译单元均无
  Vulkan 头路径。
- `-print-gl-info` 正常输出 OpenGL 4.1 信息；`-vulkan3d -h` 打印废弃提示并
  正常显示帮助。

**修复（本轮自测发现）**

- 构建 SDL2 时设置的 `SDL_VULKAN=OFF` 会写进 CMake 全局缓存；当
  `build.bat` 在同一目录再配 SDL3 时，SDL3 也继承了该值，导致
  `SDL_VIDEO_VULKAN` 未定义，运行 `Supermodel-VK-SDL3.exe` 报
  “Vulkan support is either not configured in SDL”。已改为：SDL2 分支不再改
  这个变量，SDL3 分支在 `add_subdirectory` 前显式 `SDL_VULKAN=ON`（FORCE）。
- `build.bat` 现在同时保留 `Supermodel.ini` 与 `Supermodel-VK-SDL3.ini`；
  之前每次打包都会删掉 SDL3 自己的配置并重新播种。
- MSVC 工程补上 `<TargetName>Supermodel-LegacyOG</TargetName>`，消除
  `MSB8012`（TargetPath 与 Linker OutputFile 不一致）。

### SDL3 启动耗时优化（7.1 s → ~3.2 s）

启动分段计时显示，慢的原因不在渲染，而在启动路径上重复的 Vulkan 实例和
桌面覆盖层：

| 阶段 | 优化前 | 优化后 |
| --- | --- | --- |
| 1.3 预检（实例 + 枚举 5 个设备） | ~2.2 s | ~1.2 s |
| 设置窗口 Vulkan 上下文 | ~2.6 s | ~1.4 s |
| 首帧（含第三次实例枚举） | ~1.8 s | ~0.4 s |
| **合计** | **~7.1 s** | **~3.2 s** |

- 设置界面的设备下拉框不再自己创建一次性 Vulkan 实例：预检时已经把设备名
  列表缓存下来（`KnownVulkanDeviceNames()`），首帧不再多付一次约 1 s 的
  `vkCreateInstance` + 枚举。
- 预检和设置窗口的实例在 `vkCreateInstance` 期间临时设置
  `VK_LOADER_LAYERS_DISABLE=*`，避开 Steam / RTSS / ReShade / OBS / GOG
  这些隐式层（实测每次实例创建约 1 s）。调用结束立即恢复，游戏会话的实例
  仍会加载层，覆盖层不受影响；开启 `VkValidation` 时不跳过。
- Vulkan 设置窗口在上下文就绪后立即 `ShowWindow`（启动图是它的 owned
  window，仍在最上层）。之前窗口一直隐藏到首帧，而隐藏窗口的 swapchain 在
  部分驱动上无法 acquire，首帧一直失败到启动图的 5 秒保底才显示——这就是
  偶发 7–8 秒启动的来源。
- 新增启动分段日志（`GUI: ... (N ms)`、`Vulkan: instance/device/swapchain`），
  以后遇到启动慢可以直接看日志定位。

### 移植 v0.9.2.3a 的界面与封面修复

旧版 `update_20261021.md` 记录的 DPI/布局/封面修复在本分支上缺失，现已
全部移植：

- **DPI**：macOS 上 `GetDPIScale()` 固定返回 1.0（Retina 由 ImGui 的
  framebuffer scale 处理）；首次运行的窗口尺寸也不再乘显示 DPI。
- **最小窗口**：1280x720 逻辑像素，按 DPI 缩放并封顶到工作区；新增
  `OSD::Platform::SetWindowMinimumSize()`（SDL2/SDL3 各自实现）。从
  1024x768 机器上保存的尺寸会被拉回最小值，而不是打开一个布局放不下的窗口。
- **紧凑模式**：内容宽度低于 1100 逻辑像素时，左列改为
  `max(360, 40%)`、底部按钮改两行（3+2）、Tab 栏改用
  `FittingPolicyScroll`；封面/简介预览始终保留，只改比例不改内容。
- **游戏列表列宽**：ROM 列按当前字体下最长 set 名实测宽度，年份列固定
  56 逻辑像素，游戏名列占剩余宽度；英文最后一个 Tab 改为 `FFB`。
- **封面解码**：Windows 用 WIC，macOS 用 ImageIO + CoreGraphics，
  Linux 用 libwebp（解码时就缩放）；`LoadBoxart()` 不再用 `#ifdef _WIN32`
  包住解码与上传，macOS/Linux 的封面和存档缩略图现在都能显示。
- **构建依赖**：CMake 在 Linux 链 `webp`、macOS 链
  `ImageIO/CoreGraphics/CoreFoundation`；`Makefile.UNIX` 增 `-lwebp`，
  `Makefile.OSX` 增三个 framework；`install-linux-deps.sh`、README 与
  CI 增加 `libwebp-dev`。

验证：在本机 150% 缩放下最小窗口被正确限制为 1920x1080 物理
（1280x720 逻辑），列宽与 FFB 标签正常；Windows 封面路径未变。
macOS/Linux 解码器与旧版一样，需要在对应平台上各做一次实机验证。

### SDL3 阶段 5A：Pure-Vulkan 目标隔离骨架与 C++23 启动层

本轮开始把 `Supermodel-VK-SDL3.exe` 从“SDL3 版”推进到“纯血 Vulkan 性能版”。
底层骨架、启动硬检测和独立配置已经落地；OpenGL 源的物理剔除开关已接好，
但要等设置界面/准星/主循环的 Vulkan 端口完成才能打开（见下）。

**CMake 双目标隔离**

- `CMakeLists.txt` 新增 `SUPERMODEL_SDL3_PURE_VULKAN` 选项，并把核心源文件拆成
  `SUPERMODEL_CORE_SOURCES`（与图形 API 无关）和 `SUPERMODEL_CORE_GL_SOURCES`
  （New3D、Legacy3D、FBO/Shader/Render2D/SuperAA、GLEW、imgui_impl_opengl3）。
- 核心库改由 `supermodel_add_core(target with_opengl)` 创建：
  `with_opengl=false` 时不编译上述源、不链 `OpenGL::GL`/`OpenGL::GLU`；
  `find_package(OpenGL)` 也只在需要 GL 的目标存在时才执行。
- SDL3 目标单独设置 `CXX_STANDARD 23` / `cxx_std_23`，SDL2 目标与
  `supermodel_core` 保持 C++17；上游 PowerPC/Real3D/SCSP 代码未做任何 C++23 改写。
- SDL3 目标显式只挂 `Libraries/SDL3/include`，SDL2 目标只挂 SDL2/SDL2_net 头；
  两个后端的头文件与符号不再有机会互相污染。
- Pure-Vulkan 开关目前带 FATAL_ERROR 说明：`Gui.cpp`、`Crosshair.cpp`、
  `Main.cpp` 的 GL 初始化段、`Overlay.cpp` 的 GL 半部仍是 OpenGL-only，
  直接打开会产生不完整的二进制。开关的源集/链接分支已经写好，阶段 5B 端口完成后即可启用。

**C++23 启动层（仅 SDL3 目标）**

- 新增 `Src/OSD/SDL3/AppRuntime.h/.cpp`：
  - 用平台 API + `std::filesystem` 取得可执行文件路径与 stem；
  - `std::expected<RuntimePaths, RuntimePathError>` 返回配置路径，
    调用方必须处理失败；
  - monadic `std::optional` 完成 `exe → stem → <stem>.ini`；
  - 运行 `Supermodel-VK-SDL3.exe` 自动绑定 `Data/Config/Supermodel-VK-SDL3.ini`，
    目录规则仍由 `FileSystemPath` 决定（Linux 的 XDG 布局不受影响）。
- 新增 `Src/OSD/SDL3/VulkanRequirement.h/.cpp`：
  - 在主窗口和 ROM 加载之前，用 volk 创建一次性 Vulkan 1.3 实例；
  - 逐个检查非软件/非 D3D12 shim 设备：`apiVersion >= 1.3`、
    `VkPhysicalDeviceVulkan13Features::dynamicRendering`、
    `synchronization2`、以及图形队列族；
  - 失败原因用 `std::expected` 的 error 返回，`std::println` 输出诊断，
    `std::stacktrace::current(0, 16)` 写入 `Data/Supermodel.log`（此时正式 logger
    尚未创建，直接追加）；
  - 最后调用 `SDL_ShowSimpleMessageBox` 弹出英文错误框（提示更新驱动或改用
    `Supermodel-VK.exe`），安全退出。macOS 的 MoltenVK portability 标志也已处理。
- `Main.cpp` 在 `main()` 最开端调用上述检查；SDL2 目标完全不进入这些代码。

**SDL3 目标锁定 Vulkan**

- `RunGameSession()` 在配置合并后强制 `Vulkan3DEngine=true`；命令行/ini/游戏段
  里的 `-legacy3d`、`New3DEngine=0` 等会被忽略并记录一行日志。
- SDL3 目标不再执行“Vulkan 失败自动回退 OpenGL”的分支：启动检查已保证设备可用，
  会话级失败会弹框说明并退出。SDL2 目标保留原回退逻辑。
- 为满足 C++23 对 `goto` 不得跨过已初始化变量的规则，把帧循环及其局部变量包进
  独立作用域；析构顺序和 `QuitError` 清理对象完全不变。

**验证**

- CMake/Ninja 同时构建 `Supermodel-VK.exe`（SDL2/C++17）与
  `Supermodel-VK-SDL3.exe`（SDL3/C++23）通过。
- SDL3 版 `-print-games`：输出 `Vulkan 1.3 preflight passed: NVIDIA GeForce RTX 5060 Ti (Vulkan 1.4)`，
  并生成 `Data/Config/Supermodel-VK-SDL3.ini`；SDL2 版仍读取原 `Supermodel.ini`。
- 当前限制：SDL3 目标仍链接 GL/GLU（设置界面与准星尚未 Vulkan 化），
  Pure-Vulkan 物理剔除开关保持关闭。

### SDL3 阶段 5B：Pure-Vulkan 真正启用（设置界面 / 准星 / 主循环）

设置界面、准星和主循环里的 OpenGL 分支已经全部有条件编译化，
`SUPERMODEL_SDL3_PURE_VULKAN` 默认改为 **ON**：SDL3 目标现在不编译任何 GL 源、
不包含任何 GL 头，也不链接 `opengl32` / `glu32`。

**设置界面迁到 Vulkan**

- 首次运行 `Supermodel-VK-SDL3.exe` 且自己的 ini 不存在时，从同目录的
  `Supermodel.ini` 复制一份作为种子，保留 ROM 路径、按键映射与窗口尺寸；
  纯 Vulkan 版会在会话开始时忽略/覆盖其中的 OpenGL 渲染器选项。
- 新增 `Src/OSD/SDL3/UiVulkanBackend.h/.cpp`：用 `CVulkanContext` 托管设置窗口的
  instance/device/swapchain，以 dynamic rendering 录制 ImGui 的 draw data，
  在 `BeginFrame`/`Render` 中完成图像布局转换、清屏、绘制和 present。
- 封面纹理改为 Vulkan 路径：WIC 解码后的 RGBA 像素经 staging buffer 上传为
  `VK_FORMAT_R8G8B8A8_UNORM` 图像，`ImGui_ImplVulkan_AddTexture()` 返回的
  descriptor set 直接作为 `ImTextureID`。释放时反向拆除，不残留 descriptor。
- `Gui.cpp` 的 `GuiState` 在纯 Vulkan 下持有 `CVulkanContext*` 而非 GL context；
  窗口用 `WindowFlag_Vulkan` 创建；每帧不再调用 `SDL_GL_SwapWindow`，
  改为 `UiVulkanBackend::Render()` 内部 present。窗口缩放事件会重建 swapchain。
- 设置界面读取的“实际渲染卡”改为 `VkPhysicalDeviceProperties::deviceName`。
- boxart 缓存结构从 `GLuint` 改为 `ImTextureID`，同一份 UI 代码在 GL/Vulkan 下共用。

**游戏内与主循环**

- `Overlay.cpp`：纯 Vulkan 下只编译 ImGui Vulkan 后端，`OverlayInitGL` /
  `OverlayRenderGL` / `ImGui_ImplOpenGL3_*` 全部不参与编译。
- `Crosshair.cpp` 在 SDL3 目标中整体不编译；准星继续由 `CVulkan2D::SetCrosshair`
  原生绘制。
- `Main.cpp`：`CreateGLScreen`、`SetGLGeometry`、`ResizeGLScreen`、`PrintGLInfo`、
  `SaveFrameBuffer`、`TestPolygonHeaderBits` 以及 New3D/Legacy3D/SuperAA 的构造
  全部在纯 Vulkan 下被条件编译移除；游戏窗口只走 `CreateVulkanScreen`，
  渲染器只构造 `CVulkan2D` + `CVulkan3D`。
- `-print-gl-info` 在纯 Vulkan 版中给出明确提示而不是尝试创建 GL 窗口。
- `Model3.h` 不再包含 `Graphics/SuperAA.h`，改为前向声明；`Model3.cpp` 只在
  非纯 Vulkan 构建中包含并调用 `SuperAA::Draw()`，核心库得以完全无 GL 头。
- New3D 的 `Mat4/Model/PolyHeader/R3DFloat/Vec` 是纯 CPU 代码，移入公共核心
  源集供 Vulkan 场景构建器复用；GL 源集只保留真正需要 GL 的文件。

**验证**

- 两个目标均编译通过：`Supermodel-VK.exe`（SDL2/C++17/OpenGL）与
  `Supermodel-VK-SDL3.exe`（SDL3/C++23/Pure Vulkan）。
- `dumpbin /imports` 与 `/symbols` 检查：纯 Vulkan exe 无 `OPENGL32`/`GLU32`
  导入，也没有 `glewInit`、`glGenTextures`、`ImGui_ImplOpenGL3` 符号。
- SDL3 版 `-gui`：日志确认 `Vulkan: settings window context is on "NVIDIA GeForce RTX 5060 Ti"`、
  `GUI: settings window on Vulkan (1920x1200, 3 swapchain image(s))`；
  游戏列表 56 项、封面缩略图正常显示。
- SDL3 版从设置界面启动 `daytona2`，游戏画面、bezel、Overlay 均正常；
  Esc 返回设置界面后进程继续运行，无 GL 回退、无崩溃。
- SDL2 版 `-print-games` 正常，原 `Supermodel.ini` 与 OpenGL 渲染路径未受影响。

**说明**

- 新配置文件中仍保留历史 GL 选项键，是为了兼容两个 exe 共用 `Data/Config` 的迁移；
  纯 Vulkan 版会忽略并强制 `Vulkan3DEngine=true`。
- `SUPERMODEL_SDL3_PURE_VULKAN=OFF` 仍可作为回退开关，构建带 OpenGL 的 SDL3 变体。

### SDL3 阶段 5C：修复 Explorer 双击启动崩溃

用户从资源管理器双击 `Supermodel-VK-SDL3.exe` 时，进程在出现窗口前直接退出，
Windows 事件日志记录 `0xc0000409`（`int 29h`, `FAST_FAIL_FATAL_APP_EXIT`），
偏移落在 CRT 的 `abort()` 路径。

**根因**

- 启动层用 `std::println(stderr, ...)` 输出 ini 路径与 Vulkan 检查结果。
  MSVC 的 `<print>` 在目标流不可写时抛出 `std::system_error`；从资源管理器
  启动的 GUI 进程没有控制台，stderr 无有效句柄，异常未被捕获，最终
  `std::terminate` → `abort()`。从终端启动时 stderr 有效，所以之前测试没有复现。

**修复**

- 新增 `Src/OSD/SDL3/StartupLog.h`：`StartupPrint()` 用 `try/catch` 包住
  `std::println`，写入失败时静默继续；启动诊断是 best-effort，不能反过来阻止程序启动。
- `VulkanRequirement.cpp` 与 `Main.cpp` 的所有启动期 `std::println` 改为
  `StartupPrint`。
- `main()` 的 `InitializeSDL3Startup()` 外加一层 `catch (const std::exception&)`，
  任何意外异常改为弹框说明后退出，不再静默 abort。

**验证**

- 用资源管理器方式（无控制台、无有效 stderr）启动 `dist` 的 SDL3 exe：
  进程正常存活，日志显示 `GUI: settings window on Vulkan`，事件日志无新崩溃记录。
- 重新执行 `build.bat`，`dist` 两个 exe 均为最新；SDL2 版回归正常。
- 重新检查 `dist` 的 SDL3 exe：无 `OPENGL32`/`GLU32` 导入，无 GLEW/ImGui GL 符号。

### SDL3 阶段 6：C# Native AOT 冷路径与 .vkstate 存档管理器

第二阶段的第一块：把「配置解析」和「存档容器」这类高复杂度冷路径搬到
C#（.NET 11 Native AOT），C++ 侧只保留热路径，通过 C ABI 调用。

**C# 模块（`HostCore/`，产出 `SupermodelHostCore.dll`）**

- `PublishAot` + `NativeLib=Shared`：编译成纯机器码 DLL，无运行时宿主；
  `JsonSerializerIsReflectionEnabledByDefault=false`，JSON 全部走源生成器。
- 导出函数全部 `[UnmanagedCallersOnly]`，内部 `try/catch` 兜住 IO/JSON/解压
  异常，返回状态码 + UTF-8 错误缓冲区，异常不越过 C ABI 边界。
- 功能：`.ini` 解析/写回、`.vkstate` 打包与解包、ZIP 魔数嗅探（老 `.st` 直通）、
  存档列表（不打开 `core.zst`）、`RenameVkState` 原地改 `meta.json`。
- 容器：标准 ZIP，内含 `core.zst`（合法的 zstd 帧；当时是用手写的 RAW/RLE 编码器，
  本轮已换成 libzstd，见下）、`thumb.png`、`meta.json`；30.9 MB 的 Daytona 2 存档当时
  能降到 5.8 MB（那是几乎全零的早期状态）。

**C++ 侧接入**

- `Src/OSD/SDL3/SupermodelHostCore.h` 定义 C ABI；`HostCoreBridge` 用
  `std::expected` 封装加载、调用与错误传播，DLL 缺失时只降级为日志提示。
- 设置界面游戏列表右键 → **Manage saves...**：卡片网格显示缩略图、本地时间、
  文件大小；双击卡片或点「改名」进入行内输入（自动聚焦并全选），回车写回
  `meta.json`；「Load」直接带状态开局；「Import legacy .st」把老存档转成
  `.vkstate`，时间取文件修改时间。
- 读档仍交给上游 `LoadState`：C# 把 `core.zst` 解到 `Saves/.vktmp.st`，
  C++ 走原路径加载后删除临时文件，`BlockFile` 二进制布局完全没动。

**本轮顺带修掉的三个真实缺陷**

1. **存档管理器窗口不显示**：`OpenPopup` 在 `Supermodel` 窗口里发出，
   `BeginPopupModal` 却在 `ImGui::End()` 之后调用，两者的窗口 ID 不同，查找
   每帧失败。改为和其他模态框一起在窗口内绘制，并用 `###ManageSaves`
   把标题（可翻译）与身份分离。
2. **SDL3 下任何输入框都打不进字**：SDL3 的文本输入默认关闭，平台层从未调用
   `SDL_StartTextInput`。现在 `ImGuiPlatformNewFrame` 按 `io.WantTextInput`
   统一开关（SDL2 后端保持原行为），搜索框、按键绑定、改名框全部恢复。
3. **「Load」把程序关掉**：只置了 `exit=true`，没有设置 `selectedGameIndex`，
   设置窗口退出后没有游戏可开于是直接退出。现在按存档所属 ROM set 反查行号。

**构建与发布**

- `build.bat` 增加 `dotnet publish` 步骤并复制 DLL 到 `dist\Supermodel-VK\`；
  CMake 的 SDL3 目标 POST_BUILD 也把 DLL 拷到 `build-cmake\bin`。

**验证**

- 右键 daytona2 → 菜单 → 模态框弹出，两张卡片（缩略图/日期/大小）正常。
- 改名为 `vega`，`python zipfile` 读回 `meta.json` 已是新名字。
- 游戏中 F5 生成 `daytona2.st1`（30.9 MB）→「Import legacy .st」→
  `daytona2.st1.vkstate`（5.8 MB），卡片日期显示存档文件的本地时间。
- 点该卡片「Load」：日志出现 `Loaded state from '...Saves/.vktmp.st'`，
  Daytona USA 2 直接进入读取后的画面，临时文件已删除。
- `build.bat` 全量通过；`dist` 的 SDL3 exe 启动日志为
  `host core: SupermodelHostCore 1.0 (Native AOT)`，SDL2 版照旧。

### 版本号 0.9.5.3a 与存档命名 `.zst1`

**版本号**

- `SUPERMODEL_FORK_VERSION` 改为 `0.9.5.3a`（标题栏、控制台 banner、发布包目录名、
  release tag 同步）。`SUPERMODEL_VERSION` 仍为 `0.3a-WIP`，存档/NVRAM 的兼容性
  校验值没有变，老 `.st` 照样能读。

**存档命名 `.st<N>` → `.zst<N>`**

- 新存档文件名形如 `daytona2.zst1`，对应老版 `daytona2.st1`：同一套槽位命名，
  文件名只多一个字母，`.st1` / `.zst1` 一眼能对上。
- 容器内部完全没变：仍是标准 ZIP，内含 `core.zst` / `thumb.png` / `meta.json`；
  读档依旧靠文件头嗅探（`PK\x03\x04` = 容器，否则按裸 `.st` 处理），文件名不参与判断。
- 列表扫描 `*.zst*` 与 `*.vkstate`：槽位名（`.zst1`）、裸 `.zst`、以及上一轮构建写出
  的 `.vkstate` 都能列出，不会从界面上凭空消失。
- 「Import legacy .st」保留槽位号：`daytona2.st1` → `daytona2.zst1`，卡片标题也用新文件名。
- 界面空状态文案与 `Localization.cpp` 的中英条目一起改了。

**卡片排版**

- 日期与大小恢复为两行：合并成一行后，在大界面缩放下（本项目 200% 那一档）
  会被卡片宽度裁掉后半截。大于 1 MB 时改用 MB 显示，避免 `5653.0 KB` 这种长度。

**验证**

- `HostCore/run-test.bat` 重跑：ABI 测试全部通过，列表扫描拿到 `test.zst` / `test2.zst`。
- 设置界面导入真实存档：`daytona2.st1`（30.9 MB）→ `daytona2.zst1`，卡片标题与日期
  正常；点击「Load」后日志 `HostCore: loading save '…\daytona2.zst1'`，紧接
  `Loaded state from '…/.vktmp.st'`，游戏直接进入读档后的画面，临时文件已删。
- `build.bat` 全量通过，`dist` 两个 exe 的版本均为 v0.9.5.3a。

### 真 zstd 压缩与游戏内 F5/F7

上一轮的 `core.zst` 是手写的 zstd 帧，只发 RAW/RLE 块：31 MB 的存档出来 17.6 MB，
`.zst` 这个名字名不副实。本轮换成官方 libzstd，并把游戏内的 F5/F7 接到同一条链上。

**编解码**

- 新增 `HostCore/ZstdCodec.cs`：P/Invoke `ZSTD_compress` / `ZSTD_decompress` /
  `ZSTD_getFrameContentSize`，级别 3；`HostCore/ZstdFrame.cs`（手写 RAW/RLE）删除。
- 只依赖官方 `libzstd.dll`（zstd 1.5.7 win64，1.2 MB），随包放在
  `SupermodelHostCore.dll` 旁边；`.NET 11` 的 BCL 仍无 zstd 流。
- 旧的 RAW/RLE 帧是合法 zstd，libzstd 照常解 —— 上一轮写的 `.vkstate` / `.zst`
  不需要迁移。

**实测**

同一份 gameplay 状态（`daytona2.st1`，30,953,636 B），换编码器前后：

| | 大小 |
| --- | --- |
| 旧 RLE-only 容器 | 17.6 MB |
| zstd 级别 1 | 7.04 MB |
| zstd 级别 3（当前） | 6.45 MB |

速度（同一台机器，含进程启动开销）：压缩 127 ms，解压 103 ms。存档是暂停状态下的
手动动作，这点时间无感；解压只在读档时发生一次。

刚开机/演出画面那种大面积空白的状态压缩得更恨：游戏内 F5 实测 30,953,636 B →
924,789 B（0.9 MB）。ABI 测试里 2 MB 的可压缩样本从 2,103,229 B 降到 6,886 B。

**游戏内存档**

- F5 写入 `<游戏名>.zst<槽位>`（如 `daytona2.zst1`），不再是 `.st1`。
- F7 优先读同槽位的 `.zst`（C# 解包到 `Saves/.vktmp.st` 后交给上游 `LoadState`，
  读完删除），没有 `.zst` 时回退读老的 `.st`——旧存档照旧可用。
- 任何一步失败（模块缺失、写盘失败、容器损坏）都会自动回退到传统 `.st` 写入，
  保证不丢存档，日志里有明确原因。
- 游戏内「即时存档」面板优先显示 `.zst` 槽位，旧的 `.st` 槽位仍会列出。
- 设置界面的「Manage saves...」也改成扫 `Saves` 根目录、按 `<游戏名>.zst` 前缀过滤，
  和 F5/F7 用的是同一批文件（之前它扫的是每游戏一个子目录，游戏内存的档根本不会出现）；
  「Import legacy .st」同样直接产出 `<游戏名>.zst<N>`。
- 上游 `CBlockFile` 一个字没改：先把状态写成暂存文件 `.vkstmp.st`，读完删掉再交给 C#。

**发布**

- `libzstd.dll` 进 `dist` 顶层、`build-cmake\bin`（CMake POST_BUILD）和
  `HostCore\publish`；`build.bat` 里缺失即视为打包失败。
- `HostCore/run-test.bat` 会把 `libzstd.dll` 拷到测试 exe 旁（DLL 搜索路径是 exe 所在目录）。

**验证**

- `HostCore/run-test.bat`：ALL ABI TESTS PASSED，压缩/解压往返一致。
- 游戏内：双击 daytona2 开局 → F5 → `Saves/daytona2.zst1`（924,789 B）→
  F7 → 日志 `Loaded state from '…/.vktmp.st'`，临时文件已删。
- 回到设置界面右键 → Manage saves...：卡片列表里出现同一个 `daytona2.zst1`（903 KB），
  可以直接 Load/Rename。
- 回归：把 `.zst1` 移走、只留旧的 `daytona2.st1`，F7 正常加载该 `.st`。

**F5 缩略图**

- 走和截图同一条异步回读链（同一个 staging buffer、同一个 worker），但截取点在
  **合成/后处理之后、机柜背景与设置面板之前**，所以存下来的是游戏画面，不带 UI。
- 固定 480×360（4:3，居中裁切）。不跟显示器分辨率联动：卡片在屏幕上始终是几百像素，
  存 4K 原帧（每张 5–8 MB）只会把容器撑大。
- 写盘用 `.vkthumb.png.part` 再改名；存档包在缩略图就绪后的下一帧完成，
  0.5 s 内没等到就写一个无缩略图的存档，不会卡住。
- 卡片图片区随界面缩放（`150 × uiScale`，上限 320），文字与图片比例一致；
  `UiScale()` 提到纯 Vulkan 块外，两个后端共用。
- 存档管理器弹窗放大到屏幕的 75%（原来是固定 940×660），大屏上一屏能看到更多卡片。
- 弹窗标题用游戏列表里的**游戏名**（含地区/版本），不再是 ROM set 名：现在显示
  `Manage saves - L.A. Machineguns: Rage of the Machines (Japan)`，而不是 `lamachin`。

**F5 写回哪个槽位**

- 从管理器 Load 一个存档进游戏时，代码会从文件名里解析槽位号（`lamachin.zst0` → 0、
  `lamachin.st2` → 2）并设为当前槽位，所以之后的 F5 是**覆盖刚载入的那个存档**，
  不会再另存到槽位 1。
- F7 与游戏内「即时存档」面板本来就用当前槽位，保持一致；旧 `.st` 槽位载入后 F5
  会在同一个槽位上写出 `.zst`，旧的 `.st` 保留不动。

**验证（缩略图）**

- 游戏内 F5 → `daytona2.zst1`，F6 → F5 → `daytona2.zst2`；两个容器均含
  `thumb.png`（480×360，5,080 B），日志 `thumbnail 5080 bytes`。
- 抽出 PNG 检查：画面是 Daytona 2 的 service menu 全屏图，**没有设置面板、没有机柜黑边**。
- 设置界面 Manage saves... 截图：两张卡片各带缩略图、日期、908 KB 大小，弹窗为屏幕 75%。
- 载入 `lamachin.zst0`（从旧 `.st0` 导入的）进游戏后按 F5：日志写回
  `Saves/lamachin.zst0`（不是 `.zst1`），容器 5.4 MB、缩略图 152,838 B。

**未做**

- 无。上一轮留的「F5 缩略图」已在本次完成。

**设置页整理（纯 Vulkan）**

- 纯 Vulkan 目标下，`CreateControls()` 直接**跳过**作用域不匹配的项：`QuadRendering`、
  `Supersampling`、`CRTcolors`、`UpscaleMode`、`Crosshairs`、`CrosshairStyle`
  不再以「灰显 + This is not the one running」出现。
- 「画面」页只放**图片相关**的东西：CRT 滤镜（含玻璃辉光）、宽屏背景、机柜背景图。
  `DrawCrtFilterSettings()` 从 Core 页抽出来，设置窗口在「画面」页调用；
  游戏内实时面板（`overlayMode`）仍然绘制它 —— 那才是对照活动画面调滤镜的地方。
- 其余的 Video 组键（无边框窗口、帧率锁、刷新率、DisplayNumber、ShowConsole、关闭白闪）
  是运行方式而不是画面效果，全部移到 Core 页的「显示与运行」分组；
  `UpdateTempValues(config, "Video", ...)` 随一起搬过去，避免临时值回写丢编辑。
- `WideBackground` 加入 `HasDedicatedUI`，在「画面」页手写，位置在机柜背景图**上方**：
  它决定两侧到底有没有黑边，和背景图是同一个区域的两个方案。说明文字已入翻译表，
  英文界面下不再出现中文 tooltip。
- 新增 `KeyHelpText()`：给通用渲染器画的控件配一行的说明，`CreateControls()` 在控件后面
  自动补一个 `(?)`。第一批是 **Disable White Flash**（「街机硬件做块剔除时整屏刷白」），
  中英条目已入表；“显示与运行”分组的标题也一并翻译。
- SDL2 目标保持原有「灰显 + 说明」策略；CRT 页也随之落到 Video。
- 验证：Video 页只剩 CRT 滤镜 → 宽屏背景 → 机柜背景图；Core 页到「显示与运行」
  （Borderless Window / Frame Limit / Refresh Rate …）；游戏内面板仍能看到 Glass Glow 与存档表格。

**Release notes**

- zh：存档容器换成真正的 zstd（官方 libzstd）：同一份 31 MB 状态从 17.6 MB 降到
  6.4 MB（压缩 ~0.1 s）；游戏内 F5 写入 `游戏名.zst1`（带 480×360 缩略图，F6 换槽位后
  F5 写入 `.zst2`，从某个存档进游戏后 F5 就覆盖那个存档），F7 优先读 `.zst`、没有时回退老 `.st`。
  同时：版本号 v0.9.5.3a、存档管理器（右键游戏 → Manage saves...，缩略图卡片/
  双击改名/一键带状态开局，弹窗为屏幕 75%、标题用游戏名）、SDL3 界面无法输入文字与
  「Load」直接退出的修复。设置页面也整理过：Video 页去掉 OpenGL 专用项，CRT 滤镜从
  Core 页移到 Video 页。
- en: The save container now uses real zstd (official libzstd): the same 31 MB state drops
  from 17.6 MB to 6.4 MB, in about 0.1 s. In-game F5 writes `game.zst1` with a 480×360
  thumbnail (F6 then F5 writes `.zst2`; a session started from a save overwrites that save),
  and F7 reads `.zst` first, falling back to the classic `.st` when a slot has none. Also in
  v0.9.5.3a: the save manager (right-click a game, Manage saves...; 75%-of-screen dialog titled
  with the game name), fixes for typing in the SDL3 settings window and for "Load" quitting the
  program instead of starting the game. The Video page also drops the OpenGL-only options, and the
  CRT filter has moved there from Core.

### 启动图：用主题画替代黑窗，并兼作构建指纹

**问题**

- 设置窗口由 SDL 创建后立刻可见，但 Vulkan 设备/交换链（或 GL 上下文）要 1～2 秒
  才建好；这段时间窗口里没有任何内容，表现为每次启动都先黑一屏。
- 本机实测：进程启动 → 窗口创建约 2.8 s，窗口创建 → 首帧约 1.5～2 s，
  后者正是原来那段黑窗。

**做法**

- `GuiCreate()` 改为以 `WindowFlag_Hidden` 创建窗口（仅 Windows），
  启动图先顶上；首帧真正 present 后再显示窗口。
- 新增 `Src/OSD/Windows/SplashScreen.h/.cpp`：
  - 用 WIC 从内存解码 WebP（与封面预览同一个解码器，不引入第三方库）；
  - 画到 BGRA 画布（底色取图片四角平均色），再用 GDI 在底部写版本行；
  - 用 `UpdateLayeredWindow` 显示在 `WS_EX_LAYERED | TOOLWINDOW | TOPMOST`
    的弹出窗口上：没有任务栏按钮、不抢焦点、始终压在别人上面；
  - 尺寸取显示器工作区短边的 62%（上限 2048），正方形，**永远居中**——
    与主窗口尺寸无关，因为主窗口本身可能被拉得比桌面还大。
  - 注意 `UpdateLayeredWindow` 的 `pptDst` 是窗口的屏幕坐标而不是偏移量，
    传 `(0,0)` 会把图挪到左上角（本轮踩过一次）。
- `GuiRunOnce()` 在第一帧 present 之后销毁启动图并 `RaiseSettingsWindow()`：
  Vulkan 的 present 在 `GUI()` 内部（`g_guiFramePresented` 在这里置位），
  OpenGL 在 `SwapWindow()` 之后置位；另有 5 秒兜底，
  万一没有任何帧 present（窗口最小化等）也不会把界面挡在启动图后面。
- 启动图不可用时（资源缺失、解码失败）`ShowStartupSplash()` 返回 false，
  窗口照旧立刻显示，行为与改动前完全一致。

**嵌入与防伪**

- 图片以 RCDATA（id 102）编进 exe，不是随包文件，往目录里丢一个同名图片盖不掉。
  `MSVC/SupermodelResources.rc` 和 CMake 生成的 rc 都从
  `Src/OSD/Windows/SplashResource.h` 取 id，两边不会写岔。
- 同一个头文件记录图片的 SHA-256；程序启动时自己算一遍，不一致就在启动图底部
  标红 **UNOFFICIAL BUILD** 并写 ErrorLog（期望值/实际值都给）。
  `Scripts/UpdateSplashSignature.ps1` 由 `build.bat`、`build-cmake.bat` 在编译前调用，
  换图后不用手动改哈希。
- 定位说明：这是「改动检测 + 品牌标识」，不是拷贝保护。GPLv3 允许商业分发，
  从源码重新构建也总能去掉检查；它能做的是让「换张图就当自己的发布」立刻露馅。

**验证**

- 两个 exe 冷启动均为：启动图居中显示 → 首帧后销毁 → 界面出现；
  `Data/Supermodel.log` 记录 `Splash: 1294x1294 at (1273,397) in work area (0,0 3840x2088)`
  与 `Splash: artwork verified, sha256 …`。
- 故意把哈希改错再编译：启动图底部变红 `UNOFFICIAL BUILD`，日志同时给出两个哈希；
  恢复后重新构建即回到 `official build`。
- 版本号提到 v0.9.5.4a（启动图底部显示的就是它）。

**游戏窗口（`Main.cpp`）：同样不再先亮一个空窗**

- `CreateGLScreen()` / `CreateVulkanScreen()` 改为带 `WindowFlag_Hidden` 创建，
  `EndFrameVideo()` 在首次 present 之后 `ShowWindow()`（`s_windowRevealPending` 保证只发生一次）；
  原来在创建后立刻 `RaiseWindow()` 的那一处随之后移，因为抬一个隐藏窗口等于提前曝光。
- 这里不放图：两个渲染后端都以 `EndFrameVideo()` 为唯一出口
  （Vulkan 在 `CVulkanContext::EndFrame`，OpenGL 在 `SwapWindow`），
  在那里插入一步显示，比往渲染循环里塞图层安全得多。
- `DestroyGLScreen()` 会清掉待显示标记，会话在首帧前就结束（加载失败等）时不会漏给下一个窗口。
- 实测（daytona2，独占全屏与窗口化各一次）：日志出现
  `Window: shown once the first frame had been presented.`，
  窗口出现在首帧而不是创建时刻；从进程启动到窗口可见约 7 s（含 ROM 加载与管线编译）。
  首帧本身仍是黑的——那是 Model 3 自己还没开机（约 1 s 后出 BIOS 文字），不是窗口初始化问题。

**Release notes**

- zh：启动不再有那 2 秒黑窗。窗口先隐藏，改用「Igloo-Garage Presents Supermodel VK」
  主题图在屏幕正中覆盖，第一帧画好之后才显示界面。这张图以 RCDATA 编进 exe，
  并带 SHA-256 构建指纹——图片被换过的二进制会在图底标红 `UNOFFICIAL BUILD`。
  游戏窗口（命令行直接开局）同样改为首帧后才显示，不再先亮一个空窗。
- en: No more black window at startup. The window is created hidden and the
  "Igloo-Garage Presents Supermodel VK" artwork is shown centred instead, with the interface
  appearing as soon as the first frame is drawn. The picture is compiled into the executable as
  an RCDATA resource carrying a SHA-256 build mark, so a binary whose artwork was swapped
  reports UNOFFICIAL BUILD. The game window is created hidden too and appears on its first
  presented frame.

## 2026-10-20

### SDL3 双目标重构：阶段 1 设计（未改运行行为）

新增平台抽象设计，为后续 `Supermodel-VK.exe`（SDL2）与
`Supermodel-VK-SDL3.exe`（SDL3）双目标独立编译做准备。本轮只新增头文件与
设计文档，没有修改现有代码路径，未重新编译。

- `Src/OSD/Platform/Platform.h`：窗口、显示、GL 上下文、纳秒时钟、原生句柄的
  后端无关接口；不包含任何 SDL 头。
- `Src/OSD/Platform/PlatformInput.h`：键盘/鼠标快照、1000Hz 鼠标浮点位移累加、
  中性事件队列、手柄热插拔与标准布局。
- `Src/OSD/Platform/PlatformVulkan.h`：`SDL_Vulkan_CreateSurface` 的 SDL2/SDL3
  差异封装，渲染器不再直接认识 `SDL_Window`。
- `devdocs/plans/SDL3-Platform-Refactor.md`：耦合地图、零虚函数切分原则、分阶段实施
  顺序与验收闸门。
- `devdocs/plans/CMakeLists.skeleton.txt`：双目标 CMake 骨架，SDL2/SDL3 目标隔离、
  输出命名、编译期宏与 LTO 配置。

设计结论：采用“编译期后端 + 自由函数”而非 `IPlatform` 虚接口；SDL2/SDL3
各只链接自己的三个平台源文件。现有 `CInputSystem` 虚接口保留，不作为热路径
重写对象。

### SDL3 重构阶段 1A：SDL2 平台层落地

已把第一阶段从设计推进到可编译、可运行的 SDL2 实现，行为保持不变。

- 新增 `Src/OSD/Platform/SDL2/`：
  - `PlatformSDL2.cpp`：窗口/显示模式/GL 上下文/纳秒时钟/原生 HWND；
  - `PlatformInputSDL2.cpp`：统一事件泵、键盘/鼠标快照、手柄注册表；
  - `PlatformVulkanSDL2.cpp`：封装 `SDL_Vulkan_GetInstanceExtensions`、
    `SDL_Vulkan_CreateSurface` 与 surface 销毁。
- `CVulkanContext` 不再包含或前置声明 `SDL_Window`，改用
  `OSD::Platform::Window*` 和 `Platform::GetWindowSize()`。
- `CVulkanContext::Shutdown()` 通过平台层销毁 surface，SDL3 后端可改调
  `SDL_Vulkan_DestroySurface` 而不影响渲染器。
- `CSDLInputSystem::Poll()` 不再直接调用 `SDL_PollEvent` /
  `SDL_GetKeyboardState` / `SDL_GetMouseState`，改为消费平台快照；退格键/滚轮/
  手柄热插拔语义保持不变。
- 主循环计时刻改用 `Platform::Now()` 与 `Platform::SleepUntil()`，为 SDL3 的
  `SDL_GetTicksNS`/`SDL_DelayNS` 预留同一接口。
- MSVC 工程与 `Makefiles/*` 增加 `SUPERMODEL_PLATFORM_SDL2` 宏及三个平台源文件。

验证：MSVC Release x64 编译通过；用 Daytona USA 2 启动 Vulkan 窗口模式，日志
确认 `Vulkan: using NVIDIA GeForce RTX 5060 Ti`、`Vulkan 3D engine enabled`，
并分别用 `-input-system=sdl` 与实际默认 XInput 路径启动成功。

### SDL3 重构阶段 1B：GUI 与 ImGui 去 SDL 化

- 新增 `Src/OSD/Platform/ImguiPlatform.cpp/.h`，直接消费
  `Platform::Event` 与 `Platform::InputSnapshot`，不再依赖
  `imgui_impl_sdl2/sdl3`；键盘映射使用 USB HID usage ID，SDL2/SDL3 共用。
- `Gui.cpp` 的 `GuiState` 改为 `Platform::Window*` / `Platform::GLContext*`；
  窗口创建、GL 上下文、交换缓冲、DPI、窗口尺寸、事件循环均改为平台调用。
- `GuiCreate/GuiRunOnce/GuiDestroy` 不再出现 `SDL_CreateWindow`、
  `SDL_GL_*`、`SDL_PollEvent`、`ImGui_ImplSDL2_*`。
- `SetWindowIcon`、DirectInput、`Overlay` 的窗口参数统一改为
  `Platform::Window*`；HWND 由 `Platform::GetNativeWindowHandle()` 获取。
- `imgui_impl_sdl2.cpp` 从 MSVC 与 Makefile 构建中移除（源文件保留作为
  回退参考）。
- `Overlay` 的滚轮仍使用 SDL event watch，因为游戏默认走 DirectInput 时
  平台输入泵不运行；这一部分留到主循环统一事件泵时处理。

验证：MSVC Release x64 编译通过；`-gui` 启动成功，日志显示
`GUI window: 1959x1437`、`OpenGL: settings window context is on ...`；
默认 XInput 游戏路径仍能进入 Vulkan 并运行。

### SDL3 重构阶段 2：CMake 双目标骨架

- 新增根 `CMakeLists.txt` 和 `build-cmake.bat`（Ninja + MSVC，自动调用
  `vswhere`/`vcvars64.bat`）。
- `supermodel_core` 是不带平台宏的静态库；`VulkanContext.cpp` 因依赖
  `PlatformVulkan` 改为每个后端各编译一次。
- SDL2 目标输出 `build-cmake/bin/Supermodel-VK.exe`，命名与发行版一致。
- SDL2/SDL2_net 直接使用 vendored 源码构建；zlib 改用显式静态目标，
  避免 zlib 自带 CMakeLists 在 out-of-source 配置时重命名仓库里的
  `zconf.h`（那会破坏 MSVC 工程）。
- CMake 专用资源脚本只嵌入图标；`SupermodelManifest.xml` 通过
  `/MANIFESTINPUT` 合并，避开 CMake 自动 manifest 与 `.rc` 内 manifest 的
  `CVT1100 duplicate resource`。
- Post-build 把 `Config/`、`Assets/` 和 `Supermodel3Bezel.png` 复制到
  `build-cmake/bin/Data/`，产物可直接运行。
- `USE_SDL3=ON` 在 SDL3 SDK 或后端源文件缺失时给出明确 FATAL_ERROR；
  双目标接口已就位，等阶段 3 实现后端。

验证：`build-cmake.bat` 配置/构建成功；CMake 产物可运行
`-print-games`、`-gui` 和 Daytona USA 2 的 Vulkan 窗口模式；
同一时间 MSVC 工程仍能编译通过。

### SDL3 重构阶段 3：SDL3 原生后端与双二进制落地

阶段 3 完成，`build-cmake.bat -DUSE_SDL3=ON` 现在同时产出
`Supermodel-VK.exe`（SDL2）和 `Supermodel-VK-SDL3.exe`（SDL3），两者各自静态链接
自己的 SDL，互不共享符号。

**平台层（SDL3）**

- `Libraries/SDL3` 引入 SDL 3.2.30（release-3.2.30，去掉 `.git`）。
- `Src/OSD/Platform/SDL3/` 三个源文件与 SDL2 版一一对应；
  `SDL_Vulkan_CreateSurface` 改为带 allocator 的 SDL3 签名，
  `SDL_Vulkan_GetInstanceExtensions` 不再接收窗口。
- 显示枚举用 `SDL_DisplayID`（id 稳定），全屏模式列表用
  `SDL_GetFullscreenDisplayModes`，独占全屏用 `SDL_SetWindowFullscreenMode`。
- `Platform::Now()` 走 `SDL_GetTicksNS()`。
- 新增 `Platform::CenterWindowOnDisplay()`、`SetDPIAwareness()`、
  `GetAudioOutputDeviceCount()/GetAudioOutputDeviceName()`，把原先散落在
  `Main.cpp`/`Gui.cpp` 里的 SDL 细节收回平台层。
- SDL3 目标的 Windows 入口 `WinMain` 放在 `PlatformSDL3.cpp`：把 UTF-16
  命令行转成 UTF-8 后调用 `main()`，避免为了 `SDL3main` 在所有 TU 里引入
  `SDL_main.h`。

**共享代码去 SDL 化**

- `Main.cpp` 不再包含任何 SDL 头（SDL2 分支仅保留 Win32 消息钩子所需的
  头文件）。窗口创建、GL 上下文、全屏/显示模式、标题、鼠标捕获、光标、
  游戏手柄探测全部走平台层。
- 全屏流程改为平台层的「一次性进入独占模式」：
  `ResolveFullScreenMode()` 只决定模式并记录，`ApplyFullScreenMode()` 调用
  `SetExclusiveFullscreen()`，被驱动拒绝时回退无边框。
- `Audio.cpp` 用 `SDL_AudioStream` + get callback 复刻原来的环形缓冲模型：
  设备、格式、线程唤醒语义与 SDL2 版一致，调好的音频参数不用改。
- `Thread.cpp` 用内联适配器抹平 SDL3 改名（`SDL_Mutex`/`SDL_Semaphore`/
  `SDL_Condition`、`SDL_WaitSemaphore` 等），类体两种后端共用。
- `SDLInputSystem` 保留同一套键位/轴映射与配置解析，仅设备访问分支：
  SDL2 用 `SDL_Joystick`/`SDL_GameController`/`SDL_Haptic`，SDL3 用
  `PlatformInput` 的 id + `GamepadState`。按钮顺序刻意与 SDL2 版相同，
  现有 `JOY1_BUTTONn` 绑定换二进制仍然有效。
- `Overlay` 的滚轮改为读 `Platform::Input().wheelY`，删掉了
  `SDL_AddEventWatch` 的历史包袱。
- `Crosshair`/`StatsOsd` 改用平台 DPI、显示模式与时钟接口。

**网络层解耦**

- `TCPReceive.h/.cpp`、`TCPSend.h/.cpp`、`TCPSendAsync.h/.cpp`、
  `NetOutputs.h/.cpp` 的套接字成员改为 `void*`，SDL2 分支内部再转回
  `TCPsocket`/`SDLNet_SocketSet`。
- SDL3 目标没有 SDL3_net：TCP 收发为惰性空实现，`CNetOutputs::Initialize()`
  打印一次提示后继续运行（两个 exe 共用 `Data/Config`，不能因为
  `Outputs = net` 就拒绝启动）。

**构建**

- `CMakeLists.txt`：SDL 头文件路径改为按目标挂载，SDL3 目标不会看到 SDL2
  的头；网络源文件两个目标都编译；`USE_SDL3=ON` 的临时 FATAL_ERROR 保护
  已移除。
- 移除 `Model3.cpp` / `NetBoard.cpp` 里 37 处 `SDL_ShowSimpleMessageBox`
  （热路径上的调试弹窗，紧邻的日志行已经包含同样信息），核心库彻底不再
  依赖 SDL。

**验证**

- `build-cmake.bat -DUSE_SDL3=ON` 构建出两个 exe；MSVC Release x64 工程
  同时编译通过。
- 两个 exe 的 `dumpbin /imports` 只有系统 DLL：SDL2 版多一个 `WS2_32.dll`
  （SDL_net），SDL3 版没有；没有任何 SDL/SDL2_net 动态库依赖，说明静态
  链接且没有混链。
- `-print-games`、`-gui`（`GUI window: 1920x1200`、`OpenGL: settings window
  context is on ...`）在 SDL3 版正常。
- Daytona USA 2 + `-vulkan3d` 在 SDL3 版跑通：Vulkan 设备初始化、音频设备
  `Speakers (Realtek(R) Audio)` 打开、`Audio: 735 samples per frame`、
  Overlay 就绪、bezel 上传正常；配置里写的是 `xinput`，日志自动提示
  `'xinput' is not available in the SDL3 build; using 'sdlgamepad'`。

**已知限制（SDL3 版）**

- 暂无 netplay / MAMEHooker 网络输出（未链接 SDL3_net）。
- 力反馈只有 rumble；恒定力、弹簧、摩擦三种效果需要 SDL2 的 haptic 接口。
- `dinput` / `xinput` / `rawinput` 不可用，配置里写了会自动回退
  `sdlgamepad` 并记录一行日志。

### SDL3 重构阶段 4：光枪低延迟路径

### 1. 亚像素准星（消除微调跳动）

光枪把窗口像素映射到街机光枪粗得多的坐标范围（Lost World 是 150..651 /
80..465）。原来的链路在 `CSDLInputSystem::Poll()` 就 `(int)input.mouseX`
取整了，于是慢速微调时会一跳一跳。

- `CInputSystem` 新增 `virtual float GetMouseAxisValueFloat()`，默认返回整数版
  （DirectInput 行为不变）。
- `CSDLInputSystem` 保存未取整的 `m_mouseXF/m_mouseYF`，SDL3 后端直接给浮点
  坐标。
- `CMseAxisInputSource::ScaleAxisValue()` 全部改用 1/256 像素为单位计算；
  `Scale()` 只看比例，因此结果只在小数部分上更细，不会改变量程。

### 2. 临界即时采样（Late Polling）

输入原本在帧首 `Inputs->Poll()` 采一次，之后还要建 ImGui 面板、记录渲染
命令，光枪能感知到这段延迟。

- `Platform::SamplePointerNow()`：绕开事件队列直接问系统。Windows 用
  `GetCursorPos` + `ScreenToClient`（取当前有焦点的窗口，客户端坐标与事件
  快照同一坐标系）；其他平台退化为 `SDL_PumpEvents` + `SDL_GetMouseState`；
  SDL2 后端直接返回 `false`。
- `CInputSystem::RefreshPointer()`（默认空）由 `CSDLInputSystem` 实现；
  相对鼠标模式下跳过，因为那时 OS 光标位置没有意义。
- `CInputs::PollPointerLate(game)`：只重采 `gunX/gunY/analogGunX/analogGunY`
  四个模拟轴，不碰任何开关源，因此不会造成二次“按下/松开”边沿；面板打开时
  同样抑制，无枪游戏直接返回。
- 主循环改为 `Poll → OverlayNewFrame → PollPointerLate →
  UpdateOverlayCrosshair → RunFrame`。准星从 ImGui 帧之前移到之后：
  `g_crosshair` 是在 `OverlayRender`（渲染阶段）读的，放在这里既不影响绘制，
  又让准星与真正用于模拟的瞄准值一致。

### 3. 高回报率鼠标的事件积压

SDL3 后端本来就把每个 `SDL_EVENT_MOUSE_MOTION` 的浮点 `xrel/yrel` 累加到
`Input().mouseDX/mouseDY`，并且每帧把队列排空到 `PollEvent()` 返回 false，
1000Hz 鼠标一帧的十几个事件全部计入，不存在“只取最后一个”的丢失。绝对
坐标取最后一次事件的位置，也正是光枪需要的那一个。

验证：SDL3 版跑 `lostwsga`（The Lost World）进入 Vulkan 窗口模式，日志确认
`Input: mouse fallback mappings applied to player 1 light gun`、
`mouse confined to the game window for light gun aiming`、`Crosshairs: enabled
for player 1 light gun`，无错误。

### 4. 发行包同时包含两个 exe

`build.bat` 在 MSBuild 之后调用 `build-cmake.bat -DUSE_SDL3=ON`，SDK 构建失败
直接终止（不会出现只带一个 exe 的半成品），并把
`build-cmake\bin\Supermodel-VK-SDL3.exe` 复制到 `dist\Supermodel-VK\`。两个
exe 共用同一份 `Data\`，切换只是双击哪个文件。

### SDL3 后续优化 #1：Vulkan 准星改原生绘制

光枪游戏开着准星时，主循环过去每帧都会在 `Inputs->Poll()` 和 `RunFrame()` 之间
调用 `OverlayNewFrame()`：哪怕面板和统计读数都关着，也会建一个完整的 ImGui 帧，
只为把 24 个顶点写进前景 draw list。这段 CPU 时间正好落在输入采样和提交渲染
之间，是光枪能感觉到的延迟。本轮把准星移出 ImGui，改由 Vulkan 渲染器直接画。

**渲染器**

- `CVulkan2D` 新增 `CrosshairState` / `SetCrosshair()`，以及一条独立的小管线：
  `crosshair.vert` / `crosshair.frag`，每名玩家 12 个顶点全部由 `gl_VertexIndex`
  生成，没有顶点缓冲、descriptor set 或 sampler。
- 形状、比例、颜色和尺寸与原来的 ImGui 版本完全一致（2px 间距、9.6 高、4.8 底，
  按 `窗口高 / 480 × UI 缩放`）；P1 红色、P2 绿色不变。
- `DrawCrosshairPass()` 在 `DrawOverlayPass()` 之后执行，所以准星仍位于游戏内面板
  和统计读数之上，也仍会被截图捕获。`EndFrame` 的顺序现在是
  合成 → 后处理 → 机柜背景 → ImGui 面板 → 准星 → 截图。
- 准星不再依赖 ImGui 后端：即使 overlay 初始化失败，Vulkan 准星仍然可用。
  OpenGL 路径不变，继续由 `CCrosshair` 绘制。
- `Scripts/CompileVulkanShaders.cmd/.sh` 和 `Scripts/CheckShaderHeaders.ps1`
  加入了新着色器，避免“改了 GLSL 但没重编”的旧坑。

**Overlay 与主循环**

- `Overlay` 不再持有准星状态；`OverlayNewFrame()` 在面板和统计读数都关闭时直接
  返回，不再为了准星构建 ImGui 帧。`OverlayRender()` / `OverlayRenderGL()` 的条件
  同步收紧，不会读上一帧遗留的 draw data。
- `Main.cpp` 用 `s_vk2dRenderer` 把每帧的准星位置交给渲染器；`overlayScale` 提升到
  循环作用域，准星尺寸与面板使用同一个缩放值。
- 新增环境变量 `SM3D_FRAME_TIMING=1` 诊断：每秒在 `Data/Supermodel.log` 输出一行
  `Timing:`，包含 ImGui 帧耗时（`ui`）、从输入采样到提交渲染的间隔
  （`poll-to-render`），以及到 `vkQueuePresentKHR` / `SDL_GL_SwapWindow` 返回的间隔
  （`poll-to-present`）。未设置时每帧只多一次分支。

**实测对比**（Windows，RTX 5060 Ti，`lostwsga.zip -vulkan3d`，2480×1920 窗口，
约 60 fps，每次 12 秒；改前 4 次共 23 个 1 秒样本，改后 3 次共 24 个）

| 指标 | 改前 | 改后 |
|---|---|---|
| `ui`（ImGui 帧耗时）中位数 | 69.4 µs/帧（63.1–100.5） | 0.2 µs/帧 |
| `poll-to-render` 中位数 | 77.6 µs/帧（69.1–112.3） | 13.2 µs/帧（12.0–16.2） |

即每帧在输入采样与提交渲染之间少约 **64 µs** CPU 时间（约 83%），并且不再为
24 个三角形构建 ImGui 帧。

**画面核对**

- 同样条件下抓取 P1 单准星：新旧截图的红色准星包围盒分别为
  `(1856,965)-(1995,1104)` 与 `(1856,965)-(1994,1104)`，纯色像素数 3360 对 3378，
  中心相差约 1–2 px。差异集中在边缘，来自 ImGui `AddTriangleFilled` 的抗锯齿
  羽化；形状和尺寸一致。
- P1+P2 双准星、以及 `SM3D_OVERLAY_OPEN=1` 打开设置面板时，准星都正常显示在
  面板之上。

**验证**

`build.bat`（MSVC + CMake 双目标）通过。两个 exe 的 `-print-games`、`-gui`、
`lostwsga -vulkan3d` 均正常；SDL3 的 OpenGL 后备（`Vulkan3DEngine=0` + `-new3d`）
也跑通，`CCrosshair` 行为未变。测试用的 `Supermodel.ini` 已还原。

**复现**

把 `Crosshairs` 设为 3，运行
`Supermodel-VK-SDL3.exe <rom>\lostwsga.zip -vulkan3d`，设置
`SM3D_FRAME_TIMING=1`，读取 `Data/Supermodel.log` 中的 `Timing:` 行；改前数据
用改动前的二进制执行同一命令即可。

完整的前后对比、原始样本、像素测量和截图保存在
`sdl3_reports/2026-10-20-01-vulkan-crosshair-native.md`；`sdl3_reports/README.md`
是本目录的索引和测量口径说明。独占全屏下 Vulkan 与 OpenGL 的公平对比（含
`poll-to-present`）见 `sdl3_reports/2026-10-20-02-fullscreen-vulkan-vs-opengl.md`：
UI 侧两者相同，API 提交侧 OpenGL 早约 0.16–0.50 ms，但这不等于画面更早出现。

### SDL3 后续优化 #2：帧尾自旋窗口 2 ms → 0.5 ms

主循环在帧尾用 `Platform::SleepUntil()` 等下一个截止时间。原实现对两个后端都是
1 ms 步进睡眠 + 固定 2 ms 自旋；在 57.5/60 Hz 下这 2 ms 约占单核 11%，是纯忙等。

**改动**

- `Src/OSD/Platform/Platform.h`：SDL3 改为请求 `remaining - 0.5 ms` 后自旋 0.5 ms；
  SDL2 保持 1 ms 步进 + 2 ms 自旋（`SDL_Delay` 只有毫秒粒度，亚毫秒自旋无法
  吸收过冲）。
- `Src/OSD/SDL/Main.cpp`：`SM3D_FRAME_TIMING=1` 增加 `Sleep timing` 行，输出
  唤醒延迟 p50/p99/max（600 样本滑动窗口）和音频 under/over 计数；同一探针还输出
  `run`（`RunFrame` 墙钟，见 #3）。

**为什么不是计划里的 0.3 ms**

实测 0.3 ms 没有额外 CPU 收益（中位数 10.95 s vs 11.03 s / 20 s，在噪声内），
但 p99 从 56 µs 升到 240 µs（最差 430 µs），因为 `SDL_DelayNS` 的过冲超过了
0.3 ms 的自旋预算。0.5 ms 是最小的、能把 p99 拉回亚毫秒的窗口。

**实测**（窗口 2480×1920，`VSync=0`，`lostwsga + -vulkan3d`，每次 20 秒，3 次）

| SDL3 自旋 | 进程 CPU s/20 s | 中位数 | 每次运行的 p99 中位数（三次） |
|---|---:|---:|---:|
| 2 ms | 13.141 / 13.313 / 13.172 | 13.172 | 2.0 / 1.9 / 27.1 µs |
| 0.3 ms | 10.844 / 11.813 / 10.953 | 10.953 | 239.9 / 430.1 / 238.8 µs |
| **0.5 ms** | 11.031 / 11.250 / 10.922 | **11.031** | **56.2 / 196.8 / 26.2 µs** |

即每 20 秒省约 2.14 s 进程 CPU（约 10.7% 单核），音频 under/over-run 三次都是
0/0。SDL2 的代码路径未改，回归运行 CPU/抖动与原值在同一噪声范围。

完整数据见 `sdl3_reports/2026-10-20-03-spin-window.md`。

### SDL3 后续优化 #3：LTO A/B（未保留）

给 CMake 目标打开 `SUPERMODEL_LTO=ON` 做了交替 A/B。LTO 确实生效（130 个
`/GL`、2 个 `/LTCG`），全量构建 56 s，exe 从 6,057,472 增到 6,091,264 bytes
（+0.56%）。

- **限帧 60 Hz**：`RunFrame` 墙钟在 5 组里都短了 2–7%，但进程 CPU 没有同方向
  变化（LTO 平均还高 0.037 s / 15 s，在噪声内）。
- **关闭帧限制**：两种构建都是 **117–118 fps**，`run` 约 8.48 ms/frame，完全一致。
  说明限帧时的 `run` 差异来自线程同步/节流，不是实际工作量减少。

按“没有可测量收益就回退”的原则：`SUPERMODEL_LTO` 保持默认 **OFF**，不修改
`build.bat` / `build-cmake.bat`；只保留 `SM3D_FRAME_TIMING` 新增的 `run` 字段。
完整数据见 `sdl3_reports/2026-10-20-04-lto-ab.md`。

### G-SYNC/VRR 独占自动按街机 57.5Hz 呈现

此前选中「独占全屏 · G-SYNC/VRR」时，模拟器仍原样使用 `RefreshRate`；如果它是
60，游戏逻辑按 57.524Hz 的帧预算运行，实际速度比真机快约 4.3%。

现在：选中 G-SYNC/VRR 且 `ExclusiveRefreshRate=0` 时，开始一局前读取当前输出
显示器的 EDID 时序范围；包含 57.524Hz 就**只改运行时配置**，把呈现节奏切到
街机速率，面板跟着模拟器走。

- 不写回 ini：切回无边框后原来的 60 仍然生效。
- `画面` 页的 `Refresh Rate` 在这时置灰并显示 57.5，注明
  「G-SYNC 自动使用街机 57.5Hz」；`60 Hz（推荐）` 改为
  `60 Hz（非 VRR / 60Hz 面板）`。
- 不把「读不到 EDID」当成「不支持」：范围未知时保持用户配置，并在日志里写明
  是 `unknown`。多显示器时按名称匹配当前输出。
- `ExclusiveRefreshRate` / `ExclusiveFullscreen` 不再出现在 `画面` 页的通用
  列表；它们由 `常用` 页的三选一独占写入，避免编辑出互相矛盾的组合。
- 修复独占 G-SYNC 下误报 `Frame cadence: borderless fullscreen ...` 的问题。

**实测**（Odyssey G70D，EDID 48–144Hz，声明 VRR 48–144Hz，
`lostwsga.zip -vulkan3d`）：ini 写 `RefreshRate=60` 时，G-SYNC 下日志输出
“the display timing range includes 57.524 Hz, pacing at the arcade rate”，统计
读数 `rate 57.5242 Hz (arcade 57.5242)`，每秒 58 帧；切到无边框后保持
`rate 60.0000 Hz`、每秒 60–61 帧，且不会读取 EDID。两个 exe 结果一致。

完整记录见 `sdl3_reports/2026-10-20-05-vrr-auto-arcade-rate.md`。

---

### 退出确认弹窗的按钮文字被裁切

本轮只修正设置界面退出确认弹窗的按钮文字裁切。已用 MSVC Release x64 编译通过，并更新到 `dist\Supermodel-VK\Supermodel-VK.exe`。

**现象**
英文界面（高 DPI 下更明显）点击 Quit 后，`Save settings before quitting?` 弹窗里的
`Save and Quit` 和 `Don't Save` 文字在按钮内显示不完整。

**原因**
`ConfirmExit` 的三个按钮使用固定像素宽度（140/120），是按中文标签定的；
英文翻译更长，实际文字宽度超过按钮内宽，而弹窗本身虽然是 `AlwaysAutoResize`，
也只能放大到按钮的外框，无法把被按钮裁掉的文字露出来。

**修复**
`Src/OSD/SDL/Gui.cpp`
- `ConfirmExit` 的三个按钮改为按当前语言的实际文字宽度计算统一宽度：
  取三个标签中最宽的一个，`CalcTextSize(Tr(label)).x + FramePadding*2 + 16`，下限 120px；
- 按钮行统一宽度；弹窗因此自动放大，英文/中文和高 DPI 下都不再裁切。

---

## 2026-10-19

本轮更新集中在设置界面、游戏内 Tab 面板、机柜背景图渲染和首次运行体验。所有改动已用 MSVC Release x64 编译通过，并更新到 `dist\Supermodel-VK\Supermodel-VK.exe`。

---

## 一、设置界面

### 1. 「分辨率」里的 Custom 选项无响应

**现象**
Common 页的 Resolution 下拉框选择 `Custom` 后没有任何反应，自定义宽高输入框不出现。

**原因**
下拉框的选中项每帧都从 `XResolution/YResolution` 反推。选择 Custom 本身不改变分辨率，下一帧索引又被算回原预设，输入框刚出现就消失。

**修复**
`Src/OSD/SDL/Gui.cpp`
- 用持久状态记住「用户显式选择了 Custom」；
- 只有选择具体预设时才清除该状态；
- 选择 Custom 后，宽度/高度输入框稳定显示，并以当前分辨率为初始值。

### 2. Controls Tab 移到 Sound 后面

新的 Tab 顺序：

```
Common → Core → Video → Sound → Controls → Network → Force feedback / Sensitivity
```

`Src/OSD/SDL/Gui.cpp`

### 3. Controllers 页文字精简

`Src/OSD/SDL/Gui.cpp`、`Src/OSD/SDL/Localization.cpp`

- Input system 旁的 `(?)` 汇总：
  - 按键在 Controls 页修改；
  - Xbox 手柄建议选 XInput，左右扳机才会分开；
  - 手柄按键按位置对应（Back/Select/Share = 投币，Start/Options/Menu = 启动/确认）；
  - 不同输入系统的扳机轴名不同（`JOY1_RZAXIS` / `JOY1_ZAXIS_POS`）。
- 刹车扳机不匹配时保留一行警告和 `Fix trigger binding` 按钮；
  原来的「当前是 …，需要 …」「或点应用推荐键位…」移入按钮旁的 `(?)`。
- 8BitDo 的 L2/R2 按钮式扳机说明移至 Controller model 下拉框旁的 `(?)`。
  - 同时修复该标记出现在很长的 `Detected: ...` 行尾部时被挤成竖排 `( ? )` 的问题。
- 「应用推荐键位会覆盖 Controls 页绑定」改为按钮后的 `(?)`。
- 对应英文翻译同步更新。

### 4. Tab 栏不再出现半截 Tab

`Src/OSD/SDL/Gui.cpp`

- `ImGuiTabBarFlags_FittingPolicyScroll` → `ImGuiTabBarFlags_FittingPolicyMixed`。
- Tab 总宽度不足时先按比例缩窄；只有窄到最小宽度仍放不下时才退回滚动。
- 切换到后面的 Tab 后，不再在左侧留下半个空白 Tab。

### 5. Tab 栏右侧左右箭头位置固定

`Src/OSD/SDL/Gui.cpp`

**现象**
切换 Tab 时，右侧的 ◀▶ 箭头会左右跳动：有些 Tab 内容高、出现垂直滚动条，有些内容矮、滚动条消失，导致 Tab 栏可用宽度变化。

**修复**
给右侧 `Settings` 子窗口加 `ImGuiWindowFlags_AlwaysVerticalScrollbar`，始终预留滚动条宽度。Tab 栏、箭头和其他控件的位置不再随 Tab 内容高度变化。

### 6. Tooltip 不覆盖右侧滚动条

`Src/OSD/SDL/Gui.cpp` 的 `DrawWrappedTooltip()`

- 创建 tooltip 前先用 `CalcTextSize()` 计算换行后的实际宽度；
- 以当前窗口的内容区右边界（`GetWindowContentRegionMax()`，已排除滚动条和内边距）为限制；
- 超出时整体左移，右侧保留 8px 边距；
- 左侧同样做边界限制。

### 7. 首次运行 Allow 后自动回到前台

`Src/OSD/SDL/Gui.cpp`

**现象**
首次运行经过 Windows SmartScreen，点击 Allow 后程序只出现在任务栏，需要用户再点一下。

**修复**
新增 `RaiseSettingsWindow()`：
- `ShowWindow(SW_RESTORE)`；
- `SetWindowPos(HWND_TOP, SWP_SHOWWINDOW)`；
- `AttachThreadInput` 附加到当前前台线程后调用 `SetForegroundWindow`，随后立即解除；
- 在 `GuiCreate()` 后调用一次；
- 在 `GuiRunOnce()` 每次进入设置界面时再调用一次（包括从游戏返回）。

非 Windows 平台仍使用 `SDL_ShowWindow` + `SDL_RaiseWindow`。

### 8. 「输出显示器」选项重新显示在 Video 区

`Src/OSD/SDL/Gui.cpp`

**现象**
Common 页只剩 `Display device` 和 `Resolution`，看不到以前的监视器/输出显示器选项；只有勾选 Fullscreen 后才在下方出现。

**原因**
输出显示器下拉框被放在 `if (FullScreen)` 的独占全屏设置块里，窗口化模式下整个块不绘制。

**修复**
- 把输出显示器组合框抽成 `DrawOutputDisplayCombo()`；
- 移到 Video 区 `Resolution` 之后，不再依赖 Fullscreen 开关；
- 仍然只在检测到 2 个以上输出时显示；
- 该选项同时影响窗口模式游戏窗口所在显示器和独占全屏所在显示器，因此始终可见是合理的。

### 9. 游戏列表分为上下两部分（封面 + 英文简介）

`Src/OSD/SDL/Gui.cpp`、`Src/OSD/SDL/Localization.cpp`、`build.bat`

**布局**
- 左列现在是：ROM 目录行 → 预览面板 → 游戏列表；
- 预览面板默认约占左列可用高度的 42%，可拖动分隔条调整；列表占用剩余高度；
- “找到 N 个 ROM”从列表内部移到窗口底部的状态栏，与保存/载入提示同一行；表格现在使用列表的全部剩余高度；
- 封面保持宽高比，与面板同高，宽度最多占面板的一半；
- 封面右侧显示游戏标题、版本、年份和一段英文简介。

**交互**
- 鼠标悬停某一行时，预览切换到该游戏；列表先于预览绘制，预览使用同一帧的命中结果，因此没有一帧延迟；
- 鼠标离开列表后，预览回到当前选中的游戏；
- 单击选中后，即使鼠标移开，预览仍保持该游戏；双击仍然直接启动；
- 预览与列表之间的分隔条可以上下拖动，悬停时显示横向抓手和上下调整光标；
- 分隔位置按“预览占左列的比例”保存，缩放窗口时两边按比例一起变化，封面也随之放大/缩小；
- 拖动后的比例写入配置 `GuiPreviewSplit`，下次启动保持；载入设置文件或恢复默认时回到默认比例；
- 拖动范围有下限，避免把预览或列表拖到不可用的大小。

**封面加载**
- 从 `Data/Assets/Boxarts/<ROM set>.webp` 读取；文件缺失时显示带边框的「无封面」占位，文字区域位置不变；
- Windows 上使用系统自带的 WIC WebP 解码器（Windows 10 1809+），不需要第三方库；
- 解码时先用 WIC 将图片缩到最大宽度 384 px，再上传为 OpenGL 纹理；
- 纹理按 ROM set 缓存，命中和失败都缓存，悬停时不会重复读盘；`GuiDestroy()` 统一释放。

**简介**
- 简介固定为英文，按 ROM set 前缀归并（例如所有 `vs2*` 共用一个条目，`daytona2` / `dayto2pe` 共用一条）；
- 目前覆盖 Games.xml 中的所有游戏系列，63 个 set 全部有封面和英文简介；
- 没有匹配条目时显示「这个游戏还没有简介。」。

**构建**
- `build.bat` 的 Boxarts 复制规则由 `*.png` 改为 `*.webp`。

---

## 二、游戏内 Tab 面板（Picture settings (live)）

### 1. CRT Filter 可折叠

`Src/OSD/SDL/Gui.cpp`、`Src/OSD/SDL/Localization.cpp`

- `CRT Filter` 改为 `CollapsingHeader`，默认展开，可点击收起；
- 展开/收起状态在本次运行内保持；
- 设置窗口和游戏内面板共用同一段代码。

### 2. 存档表格重构

`Src/OSD/SDL/Overlay.cpp`、`Src/OSD/SDL/Overlay.h`、`Src/OSD/SDL/Main.cpp`

- 列布局改为 `Slot | Status | Actions`；
- Slot 列按表头实际文字宽度计算，明显变窄；
- Status 列改为自动伸展；
- Status 显示绝对本地时间，格式 `YYYYMMDD-HH:MM`，例如 `20261019-11:49`；
- 删除 `Use for F5/F7` 按钮；
  - 点击某个槽位的 Save / Load 会自动把该槽位设为当前；
  - F6 仍可循环槽位，`*` 仍标记当前槽位；
- 移除 `OverlaySaveHandlers::setCurrent` 回调及相关翻译条目；
- 操作列宽度按 Save / Load 实际文字测量，不再写死。

### 3. 删除多余说明文字

`Src/OSD/SDL/Overlay.cpp`、`Src/OSD/SDL/Localization.cpp`

删除：
- Save states 下方的 F5/F7/F6 与 Apply/Cancel 说明；
- 表格底部的读档覆盖提示；
- 面板底部 `Tab opens and closes this panel...` 提示。

同时调整底部按钮区高度，不再为已删除的提示行预留空间。

### 4. 游戏内面板默认高度

`Src/OSD/SDL/Overlay.cpp`

- 从接近整屏高度改为工作区的 **75%**；
- 面板仍可手动拖动边框调整；
- 内容超出时主体区域滚动，Apply/Cancel/Close 固定在底部。

### 5. 鼠标滚轮可滚动面板

`Src/OSD/SDL/Overlay.cpp`

**原因**
游戏内面板的键盘/鼠标输入是通过轮询状态实现的，但滚轮是事件，之前没有转发给 ImGui，`io.MouseWheel` 始终为 0。

**修复**
- 用 `SDL_AddEventWatch()` 监听 `SDL_MOUSEWHEEL`；
- 只观察、不移除事件，游戏本身仍能收到滚轮；
- 每帧通过 `io.AddMouseWheelEvent()` 交给 ImGui；
- 面板关闭时丢弃累计值；
- `OverlayShutdown()` 中注销事件监听。

### 6. Controls 页切换卡顿

`Src/OSD/SDL/Gui.cpp`

**原因**
每次进入 Controls 都会重新创建 `CInputs` / `CInputSystem`，构造过程会枚举 DirectInput/SDL 设备、摇杆轴和力反馈能力。离开该 Tab 时 `inputs` 被置空，因此每次回来都重新枚举。

**修复**
- 设置窗口存活期间缓存输入系统，切换 Tab 不再销毁重建；
- 仅在以下情况主动失效：
  - 切换 Input System；
  - 应用推荐键位；
  - 载入设置文件；
  - 恢复默认设置；
- 启动游戏前主动释放缓存，并清空绑定编辑状态，避免与游戏会话同时占用输入设备。

### 7. 宽屏背景提示

`Src/OSD/SDL/Gui.cpp`、`Src/OSD/SDL/Localization.cpp`

当 `WideScreen` 关闭而 `WideBackground` 开启时，在宽屏开关下方显示提示：2D 背景仍会拉伸铺满窗口，因此四周不是黑边；需要黑边请关闭「宽屏背景」。

---

## 三、机柜背景图 / 黑边渲染

### 1. 取消宽屏后黑边显示蓝色块

`Src/Graphics/Vulkan3D/Vulkan2D.cpp`、`Vulkan2D.h`

**原因**
`BezelMode=1` 且启动时宽屏开启的情况下，背景图不会在启动阶段加载。之后在游戏内取消宽屏时，`LoadBezelImage()` 创建了背景图纹理，但**没有更新合成 descriptor 的槽位 3**。该槽位仍指向启动时的 2D 底层纹理（游戏天空/背景），于是黑边被拉伸成纯蓝色块。

宽屏模式下上下各 `correction` 像素的修正带也会采样同一个错误槽位，形成画面顶部的蓝色粗线。

**修复**
- 新增 `CVulkan2D::UpdateBezelDescriptor()`，只更新槽位 3；
- `LoadBezelImage(bool updateDescriptor)` 在延迟加载成功后可重新写入 descriptor；
- 延迟加载路径中先 `vkDeviceWaitIdle`，避免与仍在执行的上一帧冲突（一次性的设备级等待，仅在切换宽屏时发生）。

### 2. 宽屏模式不绘制机柜背景

`Src/Graphics/Vulkan3D/Vulkan2D.cpp`

`DrawBezelPass()` 在 `m_wide` 为 true 时直接返回。宽屏没有侧边黑边，也不应该把背景图画进上下修正带。

### 3. 宽屏裁剪矩形的垂直边界

`Src/Graphics/Vulkan3D/BezelLayout.h`

**原因**
`ComputeBezelGameRect()` 的宽屏分支使用整个输出高度，忽略整数倍缩放造成的上下 letterbox。2D 层比 3D 视口高出 `correction` 像素的溢出因此没有被裁掉，表现为画面顶部的蓝色横线。

**修复**
宽屏/宽背景分支的垂直范围改为使用真实画面范围：

```cpp
y = extentHeight - (yOffset + yRes) + correction;
h = yRes - 2 * correction;
```

水平仍为整宽。2D 层溢出被正确裁剪。

### 4. 新安装默认显示机柜背景图

`Src/OSD/SDL/Main.cpp`、`Src/OSD/ConfigFile.h`

- `DefaultConfig()` 中 `BezelMode` 默认值从 `0` 改为 `1`；
- 首次运行生成的 `Supermodel.ini` 模板加入：

```ini
BezelMode = 1
BezelImage = ""
BezelBrightness = 100
```

`WideScreen` 默认仍为 `false`。只有没有旧配置的新用户会应用该默认值；已有 `Supermodel.ini` 不受影响。

说明：背景图只画在黑边上。默认窗口 1280×960 是 4:3，本身没有黑边；全屏或在宽于 4:3 的窗口里运行时才会显示。

### 5. 背景图设置提示更新

`Src/OSD/SDL/Gui.cpp`、`Src/OSD/SDL/Localization.cpp`

- 明确「更改背景图或亮度需要重新开一局」；
- 「在游戏内取消宽屏会立即显示背景图」；
- 与实际的延迟加载行为一致。

---

## 四、游戏封面（Boxarts）

从 libretro-thumbnails 的 `MAME/Named_Boxarts` 下载了本项目 Games.xml 对应的盒绘，其余不在该集合中的 set 从其他来源补齐：

- 主源：`https://github.com/libretro/libretro-thumbnails/tree/master/MAME/Named_Boxarts`
- 保存位置：`Assets/Boxarts/<ROM set>.webp`
- 共 **63 张**，Games.xml 中的所有 ROM set 都有封面
- 已全部从 PNG/JPEG 转为 WebP，总体积约 4.7 MB，约为原图的四分之一；
- 文件名按本项目的 ROM set 命名，便于游戏列表按 set 名直接查找；
- `Assets/Boxarts/SOURCES.txt` 记录了每张图对应的原始文件名与来源；
- `Scripts/fetch_boxarts.py` 可重复执行下载/更新；安装了 Pillow 时会自动把下载的 PNG 转成 WebP（未安装则保留 PNG，但不会被打包）；
- `Scripts/install_missing_boxarts.py` 负责补齐 libretro 没有的 8 个 set（下载后统一转 WebP）；
- `build.bat` 打包时会自动复制到 `Data/Assets/Boxarts`；
- 当前 `dist` 也已同步。

**Windows 发布包读取封面**

GitHub 发布包把 `Assets/` 放在可执行文件旁，而 Windows 版读的是 `Data/Assets/`。
`MigrateLegacyDataLayout()` 现在会在首次启动时把 `Assets/`（含 `Boxarts/` 子目录）
递归复制到 `Data/Assets/`：

- 以前只迁移 `Config/`、`NVRAM/`、`Saves/`，平铺复制会跳过 `Boxarts/`；
- 现在这段在设置文件存在性检查之前执行，已经运行过一次旧版本的安装也会在下次启动时补齐；
- 只有当 `Data/Assets/` 还没有任何内容（文件或子目录）时才迁移，用户主动删除的封面不会在每次启动时被重新复制。

libretro 集合中没有、改用其他来源的封面：

| ROM set | 图形内容 | 来源 |
| --- | --- | --- |
| `von2`、`von2a`、`von254g`、`von2o` | US Dreamcast 盒装正面（Cyber Troopers Virtual-On Oratorio Tangram） | video-games-museum.com |
| `fvipers2`、`fvipers2o` | JP Dreamcast 盒装正面（Fighting Vipers 2） | segaretro.org |
| `lamachin` | 街机宣传海报（L.A. Machineguns - Rage of the Machines） | flyers.arcade-museum.com |
| `skichamp` | 街机框体照片（全素材中唯一可用的图） | segaretro.org |

## 五、涉及文件

```
Src/OSD/SDL/Gui.cpp
Src/OSD/SDL/Overlay.cpp
Src/OSD/SDL/Overlay.h
Src/OSD/SDL/Main.cpp
Src/OSD/SDL/Localization.cpp
Src/OSD/ConfigFile.h
Src/Graphics/Vulkan3D/Vulkan2D.cpp
Src/Graphics/Vulkan3D/Vulkan2D.h
Src/Graphics/Vulkan3D/BezelLayout.h
Scripts/fetch_boxarts.py
Scripts/install_missing_boxarts.py
build.bat
Makefiles/Rules.inc
Assets/Boxarts/*.webp
UploadToGithub_20261017/README.md
```

## 六、验证

- MSVC Release x64 编译通过；`Supermodel.exe -print-games` 的 banner 为
  `Supermodel-VK v0.9.2.3a`；`Scripts/VersionString.ps1` 输出 `v0.9.2.3a`；
- `dist\Supermodel-VK\Supermodel-VK.exe` 本次未能覆盖（模拟器正在运行，文件被占用）；
  发版前运行 `build.bat` 会先结束进程再更新；
- 63 张 WebP boxart 已同步到 `dist\Supermodel-VK\Data\Assets\Boxarts`；
- 在模拟的发布包目录中验证：首次启动会输出 `Moved 3 file(s) ... Assets/ ... Data/Assets/`，
  `Boxarts/` 子目录也被复制；已有 `Data/Config/Supermodel.ini` 但 `Data/Assets/` 为空时会补齐；
  删除 `Data/Assets/Boxarts/` 下的单张封面后再次启动，不会被重新复制；
- 运行中的旧实例需要完全退出后重新启动才能看到改动。

> 发布前建议：把 `Src/Version.h` 里的 `SUPERMODEL_FORK_VERSION` 改为 `0.9.2.3a`（不再使用构建日期），再运行 `build.bat`。版本号同时是 release tag，每次发版必须 bump。

## 七、GitHub README

- `README.md`（仓库根目录，**当前对应已发布的 `v0.9.0.3a-20261017`**，不含今天的封面预览等新功能）
- `README-DRAFT.md`（下一版内容，含 63 张封面预览等，明天发版时用来覆盖 `README.md`）

- 顶部新增 **Download and run**：发布页链接、各平台压缩包与启动方式、首次运行指定 ROM 目录的步骤；
- Windows 部分写成三步：解压 → 双击 `supermodel.exe` → 选 ROM 目录并启动；
- 明确 Windows 双击 `supermodel.exe`（CI 发布包里的文件名；本机 `build.bat` 产出的是 `Supermodel-VK.exe`）；
- 补充 SmartScreen 提示、`Esc` 返回设置窗口、`Tab` 打开游戏内面板、配置与存档的实际位置，以及中文安装说明的位置；
- 修正 Requirements 里的 Vulkan 版本：实际要求 1.3，或 1.2 + dynamic rendering + synchronization2；
- 补全 Linux 编译依赖（`build-essential`、`pkg-config`、`zlib1g-dev`），并注明 Vulkan 运行库可选；
- 功能列表补充新的游戏列表封面预览（63 张 WebP 封面）；
- 补充 Linux 运行库说明：Linux 发布版动态链接 `libSDL2`、`libSDL2_net`、`libGL`、`libGLU`，
  缺库时报 `error while loading shared libraries: libSDL2_net-2.0.so.0`；给出
  `sudo apt install libsdl2-2.0-0 libsdl2-net-2.0-0 libglu1-mesa libgl1 zlib1g`；
- 新增 **Before first run**：Ubuntu/Debian 依赖（含可选的 `zenity`、CJK 字体、Vulkan）、
  解压与运行命令、`chmod +x` / `ldd` / `SDL_VIDEODRIVER=x11` / 无声音等排查；
  macOS 的 Gatekeeper 解锁与 MoltenVK 性能说明；
- Linux 日志提示改为 `-log-output=Supermodel.log`（默认的 `Data/Supermodel.log` 在
  发布包布局下没有 `Data/` 目录，不会生成文件）；
- 修正 Requirements 里「无需安装其他东西」的说法：静态链接只适用于 Windows，
  Vulkan 在 Linux 上是 dlopen 加载、可缺省（自动回退 OpenGL）；
- 把 `Docs/images/01GUI.jpg`、`02game.jpg`、`03game.jpg` 同步到工作区的 `Docs/Images/`，保证 README 配图在本地和 GitHub 都能找到。

**发布节奏**

- 今天先传仓库根目录的 `README.md`：它对应当前已发布的 `v0.9.0.3a-20261017`，
  不含今天的封面预览等新功能，但已经有完整的下载与运行说明；
- 明天发新版时，用 `README-DRAFT.md` 覆盖 `README.md`。
- `Makefiles/Rules.inc` 的 `PKG_FILES` 同时加入 `Docs/安装说明.txt`，
  否则 README 里“安装说明在压缩包内”的说法对发布包不成立。

---

## 八、版本号与发布流程

- `SUPERMODEL_FORK_VERSION` 改为 `0.9.2.3a`，**取消构建日期**：删除 `SUPERMODEL_FORK_DATE`，
  `SUPERMODEL_TITLE_FULL` 只拼接版本号；`Scripts/VersionString.ps1` 输出 `v0.9.2.3a`，
  `Makefiles/Rules.inc` 的包名与 `workflows/build.yml` 的 asset 名同步去掉日期。
- 标题栏、控制台 banner、发布包目录与 asset 名都变为 `v0.9.2.3a`。
- `workflows/release.yml` 改为 tag push 触发，`release` job 的 `if` 同步修正；
  push master 不再自动发布。
- 每次发版必须 bump 版本号：tag 就是版本号，重复使用会覆盖已有 release。

---

# GitHub Release 简短更新日志

## 中文版

```markdown
## 2026-10-24

### 启动
- 启动图底部不再显示版本号，只保留 `official build` / `UNOFFICIAL BUILD` 指纹标记；
  版本号仍可在标题栏、控制台和 About 行查看。

### 复古版
- 隐藏不起作用的「呈现节奏」整段（该版不跑高刷 pacer，也不支持运动插值）。

### 存档
- 「管理存档」的每张卡片新增删除（两步确认）；删除后卡片和游戏内存档列表同步，
  空出的槽位留给下一次保存使用。
- `Supermodel-LegacyOG.exe` 的 `.st` 即时存档改写到 `Data\Saves_Legacy\`，不再与 Vulkan 版的
  `Data\Saves\` 混用；旧 `.st` 仍可读取，新存档只写新目录。
- Vulkan 版「管理存档 → 导入旧版 .st」会同时扫描 `Data\Saves\` 和 `Data\Saves_Legacy\`。

### 统计
- 统计面板的 Render 行改为显示实际 3D 区域和输出尺寸
  （如 `2480x1920 MSAA 2x -> 3840x2160`），全屏 4K 下不再只显示 3840x2160。

### 输入
- 修复《The Lost World》鼠标光枪准星与游戏光标错位：光枪 8 位 ADC 会被游戏再套一层
  固定校准，行程窄于 0–255；现在按校准后的行程（X 15..222、Y 5..217 反向）缩放鼠标，
  准星与游戏自绘光标重合，150% DPI、3x 窗口 / 5x 全屏均已验证。

### 界面
- 「固定刷新率」改名「固定呈现频率」；独占 G-SYNC/VRR 的说明改为「只决定谁定扫描时刻」，
  并显示面板跟随的实际速率。Vulkan 版隐藏不再起作用的「刷新率」下拉。

## 2026-10-23

### 外设与联机
- SDL3 版补齐方向盘力反馈：恒力、回中弹簧、阻尼全部可用，恒力带 20 ms
  力矩平滑，直驱方向盘不再有 57 Hz 阶梯感。
- 支持双人光枪：按设备 ID 独立追踪鼠标，`MOUSE1_` / `MOUSE2_` 可分别绑定；
  Windows 下即使不开相对模式也能拿到每把枪的坐标。
- 新增 Sinden 光枪白边，可调宽度与亮度，画在最终输出上，不受 CRT 滤镜和
  机柜背景影响。
- SDL3 版多机 TCP 联机改为原生 socket，不再依赖 SDL3_net，可与 SDL2 版
  互相联机；单机自连仍走本地回环。

### 存档
- 「管理存档」为空时会显示旧版 `.st` 数量和**其他区域版本**的 `.zst` 数量，
  并提示在游戏列表里右键实际玩的 set（例如 Scud Race 的 `scudau`）。
- 打开游戏内面板（Tab）自动暂停并静音，关闭后恢复；手动 Alt+P 的暂停不会被
  关闭动作覆盖。

## 2026-10-22

### 音频
- SCSP DSP 统一为一条路径：`0x7C0–0x7FF` 保留地址段的写入不再镜像进 `MADRS`，
  Fighting Vipers 2 无需 `-legacy-scsp` 也不会再出现胜利画面自激爆音。
  `-new-scsp` / `-legacy-scsp` 与 ini 键仍可解析，但已不再改变行为。
- 修复 Sega Rally 2 两首 BGM 同时播放：恢复 2019 年验证过的 0xA0/0xB1 声道
  选择语义，并停止在更新循环地址时把路由重置为立体声。
- DSB 的 32 kHz → 44.1 kHz 线性插值换成 64-tap 窗函数 sinc 多相滤波器（精确
  320:441）：修掉 0.41% 的音高偏低和高频镜像/发闷。
- SDL3 输出改为 `SDL_AUDIO_F32`，混音全程浮点、只在最后钳位一次；LegacyOG
  仍为 S16。
- 新增每游戏默认音量平衡（Scud Race / Daytona 2 / Star Wars Trilogy /
  Spikeout），只在用户未显式设置时生效。
- 修复 SDL3 版单机自连时 Scud Race / Daytona 2 卡在 NETWORK CHECKING：自连
  （`PortIn = PortOut` + `127.0.0.1`）改用本地回环，不再依赖未编译进 SDL3 版的
  SDL3_net。

### 渲染
- Vulkan 的 trans1/trans2 改为独立缓冲，并按 New3D 的公式合成：Daytona 2 瀑布
  后方山体上随相机位置出现的整块蓝色消失，与 OpenGL 的差异从 61.5 降到 2.1
  （同一存档帧）。
- 修复 LOD 交叉淡化时主层被淡出层深度遮挡的问题；车窗、烟雾的透明顺序与
  OpenGL 一致。
- LostWorld 的透明层深度写入没有回归，暗部比例 48.0% 与 OpenGL 48.6% 对齐。
- 修复 MSAA 下护栏墙棱线与接缝处的黑色虚线：部分覆盖的像素改用质心插值，
  不再采到多边形边界外的相邻贴图。
- 清理为期两轮排查留下的诊断代码，恢复被诊断开关关闭的微纹理混合；Daytona 2
  与 OpenGL 的整帧平均差从 5.34 降到 3.02。
- 「CRT 色彩」和「2D 放大」从「核心」页移到「画面」页的 CRT 滤镜上方，
  并加入游戏内面板顶部；Vulkan 下两项都可实时调节。
- 修复游戏内「应用」不保存这两项、以及返回设置界面后 Video 页不刷新的问题。

### 统计
- 统计面板 CPU 频率改为 PDH 实时采样，混合架构显示 P 核平均频率，不再显示「-」。

## 2026-10-21

### 构建/C++23
- CMake 核心源拆分为“与图形 API 无关”和“OpenGL-only”两组；SDL3 目标可走
  `supermodel_add_core(with_opengl=false)`，不编译 New3D/Legacy3D/GLEW，
  不链 GL/GLU。旧 SDL2 目标仍是 C++17，行为不变。
- `Supermodel-VK-SDL3` 目标单独启用 C++23，并只挂 SDL3 头文件路径。

### 启动
- SDL3 版启动时自动绑定与 exe 同名的配置文件：
  `Supermodel-VK-SDL3.exe` → `Data/Config/Supermodel-VK-SDL3.ini`，不再和 SDL2 版共用。
- 新增 Vulkan 1.3 硬检测：在加载 ROM、创建主窗口前校验 `apiVersion >= 1.3`、
  `dynamicRendering`、`synchronization2` 与图形队列。不达标时弹出说明框并退出，
  提示更新驱动或改用 `Supermodel-VK.exe`。
- SDL3 版强制使用 Vulkan，忽略 `-legacy3d` / `New3DEngine=0` 等 OpenGL 选择，
  也不再静默回退 OpenGL。

### 渲染
- 设置界面改为 Vulkan 呈现（ImGui Vulkan 后端 + 封面 descriptor set），
  不再创建 GL 上下文；缩放窗口会重建 swapchain。
- 游戏内准星继续由 Vulkan2D 绘制；SDL3 版完全不包含 OpenGL 源与 GL/GLU 链接。

### 说明
- 设置界面与准星已完成 Vulkan 化：SDL3 版不再编译或链接任何 OpenGL 代码，
  `SUPERMODEL_SDL3_PURE_VULKAN` 开关默认打开。
- 设置界面的封面缩略图走 Vulkan staging 上传 + descriptor set，窗口缩放会重建 swapchain；
  游戏内仍由 Vulkan2D 原生绘制准星。
- `SUPERMODEL_SDL3_PURE_VULKAN=OFF` 仍可作为回退开关，构建带 OpenGL 的 SDL3 变体。
- 已修复：从资源管理器双击 SDL3 版时，因无控制台导致启动诊断输出抛异常、
  窗口出现前就退出的问题。现在启动诊断写入失败会被忽略，正常弹出窗口。

### SDL2 版反向剥离
- 原 SDL2 目标改名为 `Supermodel-LegacyOG.exe`，只保留 OpenGL：源列表、
  头文件路径和链接线中不再有 Vulkan3D、volk、Vulkan-Headers，也不再链接
  `vulkan-1.dll`。
- 设置界面不再提供 Vulkan 选项：渲染器锁定为 `OpenGL (Legacy Compatibility)`，
  Vulkan 校验层/显示设备、硬件 MSAA、FXAA/CRT/荫罩/辉光、机柜背景图均已移除；
  New 3D / Legacy 3D、超采样、CRT 色彩、2D 放大、整数倍缩放保持不变。
- 旧 ini 里的 Vulkan 键（含游戏专属 section）在读取和保存时自动清除，不再
  生成脏配置；`-vulkan3d` 等参数会被忽略并给出提示后以 OpenGL 启动。
- 两个 exe 仍共用 `Data/Assets/`，封面路径不变。
- 修复：同一构建目录同时启用 SDL2/SDL3 时，SDL2 的 `SDL_VULKAN=OFF` 会污染
  SDL3 的配置，导致 `Supermodel-VK-SDL3.exe` 无法创建窗口。现在 SDL3 显式
  开启 Vulkan；`build.bat` 也会保留 `Supermodel-VK-SDL3.ini`。
- SDL3 启动提速（约 7.1 s → 3.2 s）：设备下拉框复用预检结果，不再额外创建
  Vulkan 实例；预检与设置窗口实例临时跳过 Steam/RTSS/ReShade 等隐式层；
  Vulkan 窗口在上下文就绪后立即显示在启动图后方，不再等到 5 秒保底。
- 移植旧版的界面/封面修复：最小窗口 1280x720 逻辑像素（按 DPI 缩放并
  封顶工作区）、窄窗口紧凑布局、ROM/年份列宽实测、macOS Retina 不重复缩放；
  封面解码补齐 macOS（ImageIO）与 Linux（libwebp），FFB 标签缩短。

### 界面与启动修复
- 补上自绘 ImGui 平台层缺失的鼠标指针形状更新：悬停在封面/列表分隔条上显示
  上下箭头，输入框显示 I-beam。分隔条一直可拖，缺的只是提示。
- 修复 SDL3 版启动图背后闪黑框：设置窗口改为第一帧真正呈现后才显示，不再
  提前露出空白的黑色客户区（启动图小于窗口时尤其明显）。
- 两个 exe 的「画面」页去掉置灰的渲染器单选与说明文字：二进制已经分开，
  不再需要用不可选项再声明一次。顺带清掉了一句漏出的中文。
- 修复 `Supermodel-LegacyOG.exe` 在配置里没有 `VRCycle` 时启动即崩溃
  （0xC0000409）：该输入设置被误归到 Vulkan 专属默认值里，全新安装必现。
- 封面预览最多占左列一半：向下拖动分隔条时不再把游戏列表压成一条缝，
  旧的超限比例在下次启动时自动夹紧到 0.5。
- Fighting Vipers 2 现在自动使用旧版 SCSP DSP，不用再给快捷方式加 `-legacy-scsp`；
  命令行与 ini 游戏段仍可覆盖。
- 修复按 ESC 从游戏回到设置界面后窗口无响应、几秒后闪退的问题：volk 的入口点
  是进程级的，游戏会话销毁自己的 Vulkan 实例后，设置窗口的 present 会调到
  已失效的 loader 跳板；现在回到设置界面时重新绑定。
- SCSP：旧引擎的效果声道电平改成与硬件布局一致（`EFSDL << 13`），实测 FV2 依旧
  正常；修掉旧引擎写 `0x7C0–0x7FF` 时越过 `MADRS` 踩到 DSP 微程序的越界写。
  完整分析见 `DSP.md`。

## 2026-10-20

### 新增
- 新增 `Supermodel-VK-SDL3.exe`：基于 SDL3 的低延迟版本，与原版 SDL2 的
  `Supermodel-VK.exe` 并列发布。两者各自静态链接自己的 SDL，可以分开使用。
- 平台/输入层完成 SDL3 后端：窗口、Vulkan surface、全屏与显示模式、纳秒时钟、
  手柄热插拔。共享代码不再包含 SDL 头文件。
- 音频改用 `SDL_AudioStream`；线程同步改用 SDL3 的名称；准星、统计读数、
  游戏内面板的滚轮都改为走平台层。
- 发行包现在同时包含两个 exe（`dist\Supermodel-VK\` 里并排），共用同一份 `Data\`。

### 光枪
- 瞄准改用亚像素精度，慢速微调不再一跳一跳（DirectInput 行为不变）。
- 新增帧尾即时采样：在真正开始模拟前一瞬再问一次系统光标位置，只重算光枪的
  四个模拟轴，不影响按键边沿。1000Hz 鼠标一帧内的十几个位移事件全部计入。
- Vulkan 准星改为原生绘制，不再为它每帧构建一个完整 ImGui 帧。从输入采样到提交
  渲染的 CPU 间隔中位数由约 78 µs 降到约 13 µs，准星的大小、位置和颜色保持不变。
- 新增 `SM3D_FRAME_TIMING=1` 诊断，日志每秒输出 ImGui 帧耗时、输入到渲染的间隔，
  以及到 present/swap 返回的间隔。

### 性能
- SDL3 版的帧尾自旋从 2 ms 降到 0.5 ms。`lostwsga` 20 秒运行的进程 CPU 从约
  13.2 s 降到约 11.0 s（约 10.7% 单核），音频 under-run 仍为 0；SDL2 版保持原样。
- `SM3D_FRAME_TIMING=1` 现在输出 `ui` / `run` / `poll-to-render` / `poll-to-present`、
  `Sleep timing`（唤醒抖动）和音频 under/over 计数。
- 评估 LTO：关闭帧限制后 117–118 fps 与不开 LTO 完全相同，保持默认关闭。

### 显示
- 「独占全屏 · G-SYNC/VRR」现在会在显示器时序范围包含 57.5Hz 时自动按街机
  57.5Hz 呈现，不再默认停在 60Hz（游戏快 4.3%）。切回无边框会恢复原来的刷新率设置。
- 画面页的「刷新率」在该模式下显示 57.5Hz 并置灰；「60 Hz（推荐）」改为
  「60 Hz（非 VRR / 60Hz 面板）」。
- 「画面」页不再显示 `ExclusiveRefreshRate` / `ExclusiveFullscreen` 两个裸值项，
  统一由「常用」页的全屏刷新方式三选一管理。
- 修复独占 G-SYNC 下误报「无边框帧节奏不均」提示的问题。

### 说明
- SDL3 版暂不支持 netplay / MAMEHooker 网络输出，力反馈只有 rumble。
- 配置里写 `xinput` / `dinput` / `rawinput` 时，SDL3 版会自动回退到 `sdlgamepad`
  并在日志里提示。两个 exe 共用 `Data/Config`，不需要改配置。

### 修复
- 修复设置界面 Quit 确认弹窗在英文界面 / 高 DPI 下按钮文字（`Save and Quit`、`Don't Save`）
  被裁切的问题。按钮宽度改为按当前语言的实际文字宽度计算，弹窗会自动放大。

## 2026-10-19

### 修复
- 修复 Resolution 中选择 Custom 后无响应的问题。
- 修复取消宽屏后黑边显示蓝色块、宽屏模式下画面顶部出现蓝色横线的问题。
  根因是延迟加载的机柜背景图未更新合成 descriptor，以及宽屏裁剪矩形忽略了整数倍缩放的上下黑边。
- 修复 Controls 页每次切换都重新枚举输入设备导致的卡顿，输入系统现在跨 Tab 缓存。
- 修复首次运行经 SmartScreen 允许后窗口留在任务栏、需要再点一下的问题。
- 修复从发布包首次运行时 `Assets/` 没有迁移到 `Data/Assets/`、导致封面和准星图片找不到的问题；
  已运行过的安装也会在下次启动时补齐。

### 界面
- 新安装默认启用内置机柜背景图（非宽屏黑边时显示）。
- 游戏内面板：
  - CRT Filter 支持折叠；
  - 存档表格重新布局，Status 显示 `YYYYMMDD-HH:MM` 绝对时间；
  - 移除 Use for F5/F7 按钮，Save/Load 同时设为当前槽位；
  - 支持鼠标滚轮滚动；
  - 默认高度改为工作区的 75%；
  - 删除多余说明文字，底部按钮不再预留空白提示行；
  - Tooltip 不再覆盖右侧滚动条。
- 设置界面：
  - Controls Tab 移到 Sound 之后；
  - Controllers 页的说明文字收进对应的 (?)；
  - Tab 栏改为收缩适配，不再出现半截 Tab；
  - 右侧滚动条常驻预留，Tab 切换箭头位置固定；
  - 「输出显示器」选项移回 Video 区，不再被 Fullscreen 开关隐藏；
  - 游戏列表上方新增预览面板：悬停或单击游戏时显示 WebP 封面、标题、版本和英文简介，列表在下方滚动；
  - 预览/列表之间的分隔条可拖动，两边高度按比例随窗口缩放；
  - ROM 数量从列表内部移到窗口底部的状态栏，列表表格多出一行的高度。

### 构建
- MSVC Release x64 编译通过。
- 盒绘补齐到 63 张，覆盖 `Games.xml` 的全部 ROM set：
  - 55 张来自 libretro MAME 集合（原有）；
  - `von2` / `von2a` / `von254g` / `von2o`：US Dreamcast 盒装正面（Cyber Troopers Virtual-On Oratorio Tangram）；
  - `fvipers2` / `fvipers2o`：JP Dreamcast 盒装正面（Sega Retro）；
  - `lamachin`：街机宣传海报（Arcade Flyer Archive）；
  - `skichamp`：街机框体照片（Sega Retro，该游戏没有独立盒绘）。
- 新增 `Scripts/install_missing_boxarts.py`，用于下载这 8 个 set 的图并统一转为 WebP；`SOURCES.txt` 同步记录来源。
- 全部按 ROM set 命名，打包时由 `build.bat` 自动带入 `Data/Assets/Boxarts`。
- `Makefiles/Rules.inc` 的发布包也同步带上图片：`assets` 目录（含 `Boxarts/`）随包发布，
  `Supermodel3Bezel.png` 加入 `PKG_FILES` 并在打包时放进 `Assets/`。
```

## English short version

```markdown
## 2026-10-24

### Startup
- The start-up artwork no longer prints the version in its caption; it now shows
  only the `official build` / `UNOFFICIAL BUILD` hash marker. The version is
  still in the title bar, the console banner and the About line.

### Legacy build
- The presentation-cadence controls are hidden there: the high-refresh pacer and
  motion interpolation do not run in the OpenGL build.

### Saves
- Each card in Manage Saves now has a two-step Delete. The in-game save list reads
  the same files every frame, and the freed slot is reused by the next save.
- `Supermodel-LegacyOG.exe` now writes its `.st` save states to `Data\Saves_Legacy\`,
  separate from the Vulkan build's `Data\Saves\`. Existing `.st` files in
  `Data\Saves\` remain readable; new saves only go to the new folder.
- "Import legacy .st" in the Vulkan build's Manage Saves scans both `Data\Saves\` and
  `Data\Saves_Legacy\`.

### Statistics
- The Render line now shows the visible 3D area and the output size
  (`2480x1920 MSAA 2x -> 3840x2160`), instead of only the 4K swapchain extent.

### Rendering
- The sorted-transparency (OIT) path now honours Real3D's layered-polygon
  (LAY=1) rule: only the nearest layered fragment per pixel/sample is
  composited, carried in one spare key bit, so overlapping dust quads no
  longer double-blend into dark creases - no new pass, no depth-format change,
  no extra VRAM.
- In OIT mode, Sega Rally 2's dust/snow (LAY=1, unlit, no texture alpha,
  textured) gets a light softening pass: a 0.70 alpha cap and a |N.V| rim fade,
  so newborn dust is no longer a 97%-opaque card. The untextured layered shadow
  under the car is deliberately left alone, and the default two-layer picture
  is unchanged.

### Input
- Fixed the mouse light-gun crosshair drifting away from the game's own cursor
  in The Lost World. The gun's 8-bit ADC is run through a second fixed calibration
  inside the game, so its travel is narrower than 0-255; the mouse is now scaled
  to the calibrated travel (X 15..222, Y 5..217 inverted), lining the emulator's
  crosshair up with the game cursor. Verified at 150% DPI in 3x windowed and 5x
  fullscreen.

  ### Interface
  - "Fixed refresh rate" is now "Fixed present rate"; the exclusive G-SYNC/VRR note explains
    that it only decides who drives the scan and shows the rate the panel follows. The Vulkan
    build hides the now-inert Refresh rate dropdown.

  ## 2026-10-23

### Peripherals and networking
- Wheel force feedback is complete in the SDL3 build: constant force, spring
  and friction all work, with 20 ms torque smoothing so a direct-drive wheel
  no longer steps at 57 Hz.
- Dual light guns: mice are tracked per device and `MOUSE1_` / `MOUSE2_` can
  be bound separately. On Windows the per-device positions are available even
  outside relative mouse mode.
- New configurable Sinden border, drawn on the final output so the CRT filter
  and cabinet backdrop cannot affect it.
- Multi-machine TCP linking now uses native sockets instead of SDL3_net and
  can link with SDL2 builds; single-machine self-link still uses the local
  loopback path.

### Saves
- An empty Manage Saves window now shows the legacy `.st` count and the save
  counts of other regional sets, and points at the set that was actually
  played (for example `scudau` rather than `scud`).
- Opening the in-game panel with Tab now pauses and mutes the game, and closing
  it resumes; a manual Alt+P pause is left alone.

## 2026-10-22

### Audio
- Unified the SCSP DSP on one code path: writes to the reserved `0x7C0-0x7FF`
  range are no longer mirrored into `MADRS`, so Fighting Vipers 2 no longer
  needs `-legacy-scsp` to avoid the self-oscillating victory-screen blast.
  `-new-scsp` / `-legacy-scsp` and the ini key are still accepted but no
  longer change anything.
- Fixed both Sega Rally 2 BGM tracks playing at once: restored the verified
  0xA0/0xB1 channel routing and stopped resetting it to stereo when the
  loop/end address is updated.
- Replaced the DSB's 32 kHz -> 44.1 kHz linear interpolation with a 64-tap
  windowed-sinc polyphase filter on the exact 320:441 ratio, removing the
  0.41% flat pitch and the high-frequency images.
- The SDL3 build now outputs `SDL_AUDIO_F32` and stays in float until the
  final device conversion; LegacyOG keeps its S16 output.
- Added per-game default audio balance for Scud Race, Daytona 2, Star Wars
  Trilogy and Spikeout, applied only when the user has not set the levels.
- Fixed Scud Race and Daytona 2 hanging at NETWORK CHECKING in the SDL3 build:
  the self-link (PortIn = PortOut + 127.0.0.1) now uses an in-process loopback
  instead of requiring SDL3_net, which that executable does not link.

### Rendering
- Vulkan now renders trans1/trans2 into separate targets and composites them with
  New3D's formula. The block of blue that appeared on the hillside behind
  Daytona 2's waterfall at certain camera positions is gone, and the difference
  from OpenGL on the same save frame drops from 61.5 to 2.1.
- Fixed the dominant LOD being occluded by the fading layer's depth writes; the
  transparency order for LOD cross-fades, windows and smoke now matches OpenGL.
- LostWorld's transparency depth writes are unchanged: its dark-pixel ratio is
  48.0% against OpenGL's 48.6%.
- Fixed the dotted black lines along guardrail and wall seams under MSAA: partially
  covered pixels now use centroid interpolation instead of sampling a neighbouring
  texture rectangle at the polygon border.
- Removed the investigation-only diagnostics and restored the microtexture blend
  they had disabled; the Daytona 2 frame difference from OpenGL drops from 5.34
  to 3.02.
- Moved CRT Colour and 2D Upscale from the Core tab to the top of the Video tab,
  above the CRT filter, and added them to the top of the in-game panel. Both are
  live under Vulkan.
- Fixed the in-game Apply button not persisting these two settings, and the settings
  window showing stale Video values after a game.

### Statistics
- The stats OSD samples the CPU clock through PDH and shows the average over the
  performance cores instead of a dash.

## 2026-10-21

### Build / C++23
- The CMake core sources are now split into backend-neutral and OpenGL-only groups.
  The SDL3 target can use `supermodel_add_core(with_opengl=false)`, which omits
  New3D/Legacy3D/GLEW and does not link GL/GLU. The SDL2 target stays on C++17.
- `Supermodel-VK-SDL3` alone is compiled as C++23 and sees only the SDL3 include path.

### Startup
- The SDL3 build now binds its settings to its own executable name:
  `Supermodel-VK-SDL3.exe` reads and creates `Data/Config/Supermodel-VK-SDL3.ini`.
- New Vulkan 1.3 hard check before ROM loading and window creation: requires
  `apiVersion >= 1.3`, `dynamicRendering`, `synchronization2` and a graphics queue.
  If it fails, a message box explains the problem and points at the compatible
  `Supermodel-VK.exe` build.
- The SDL3 build always uses Vulkan: `-legacy3d` / `New3DEngine=0` are ignored and
  there is no silent OpenGL fallback.

### Rendering
- The settings window is now presented with Vulkan (ImGui Vulkan backend and
  descriptor-set boxart textures); no GL context is created, and a window resize
  recreates the swapchain.
- The in-game crosshair is still drawn by Vulkan2D; the SDL3 build compiles no
  OpenGL sources and links no GL/GLU.

### Notes
- The settings window and crosshair are now Vulkan-native: the SDL3 build compiles
  and links no OpenGL code, and `SUPERMODEL_SDL3_PURE_VULKAN` defaults to ON.
- Boxart thumbnails are uploaded through Vulkan staging buffers and used as
  descriptor sets; a window resize recreates the swapchain. The in-game crosshair
  is still drawn natively by Vulkan2D.
- `SUPERMODEL_SDL3_PURE_VULKAN=OFF` remains available as a fallback that builds
  the earlier SDL3 variant with OpenGL.
- Fixed: launching the SDL3 build from Explorer could exit before its window
  appeared. Startup diagnostics no longer abort when no console handles exist.

### SDL2 build stripped to OpenGL
- The former SDL2 target is now `Supermodel-LegacyOG.exe` and builds OpenGL
  only: no Vulkan3D sources, no volk, no Vulkan headers and no `vulkan-1.dll`
  dependency.
- The settings window no longer offers Vulkan: the renderer row is locked to
  `OpenGL (Legacy Compatibility)`, and the Vulkan validation/device options,
  hardware MSAA, FXAA/CRT/shadow-mask/glow chain and cabinet backdrop are gone.
  New 3D / Legacy 3D, supersampling, CRT colour, 2D upscaling and integer
  scaling are unchanged.
- Vulkan keys left in an existing ini (including game sections) are stripped on
  load and on save; `-vulkan3d` and friends are ignored with a notice and the
  session starts on OpenGL.
- Both executables still share `Data/Assets/`, so boxarts are unaffected.
- Fixed: when SDL2 and SDL3 are configured in the same build tree, the SDL2
  `SDL_VULKAN=OFF` setting leaked into SDL3 and left it without Vulkan video
  support, so `Supermodel-VK-SDL3.exe` could not create a window. SDL3 now
  forces Vulkan on, and `build.bat` preserves `Supermodel-VK-SDL3.ini`.
- SDL3 launch is much faster (about 7.1 s to 3.2 s): the device picker reuses
  the preflight's device list, the preflight and settings-window instances skip
  Steam/RTSS/ReShade implicit layers, and the window is shown behind the
  artwork as soon as the context is ready instead of waiting for the 5 s
  safety deadline.
- Ported the UI/cover-art fixes from 0.9.2.3a: 1280x720 logical minimum window
  (DPI-scaled and capped to the work area), compact layout on narrow windows,
  measured ROM/year columns, no double scaling on macOS Retina, and cover
  decoding on macOS (ImageIO) and Linux (libwebp). The last tab is now `FFB`.

### UI and startup fixes
- Restored mouse-cursor shape updates in the custom ImGui platform layer:
  hovering the cover/game-list splitter shows the up-down arrow and text fields
  show an I-beam. The splitter was always draggable; it just never said so.
- Fixed the black frame that appeared behind the SDL3 start-up artwork: the
  settings window now stays hidden until its first frame has actually been
  presented instead of exposing an empty black client area first.
- Removed the greyed-out renderer radio and its explanatory sentence from the
  Video tab in both builds - the two executables already are the choice. Also
  removed the one hard-coded Chinese line that leaked into the English UI.
- Fixed `Supermodel-LegacyOG.exe` aborting at startup (0xC0000409) when its
  configuration has no `VRCycle` key: the input setting had been placed inside
  the Vulkan-only defaults, so a clean install could never start.
- The cover preview is now capped at half of the left column, so dragging the
  divider down can no longer squeeze the game list into a two-row sliver; an
  over-limit ratio saved by an older build is clamped to 0.5 on the next start.
- Fighting Vipers 2 now uses the legacy SCSP DSP automatically, so its victory
  tune no longer turns into a rising, looping note; `-legacy-scsp` is no longer
  something its owner has to add to a shortcut, and both the command line and a
  game section in the ini still override the choice.
- Fixed the settings window going unresponsive and then disappearing after Esc
  from a game: volk keeps its entry points in process-wide globals, so once the
  session destroyed its own Vulkan instance the settings window's present call
  jumped through a dead loader trampoline. The window now rebinds volk's
  pointers when it comes back.
- SCSP: the legacy engine now uses the hardware level layout (`EFSDL << 13`),
  and its write to `0x7C0-0x7FF` no longer runs past `MADRS` into the DSP
  microprogram. Fighting Vipers 2 stays clean either way; the full analysis is
  in `DSP.md`.

## 2026-10-20

### Added
- New `Supermodel-VK-SDL3.exe`: a native SDL3 build shipped alongside the SDL2
  `Supermodel-VK.exe`. Each binary statically links its own SDL and they share nothing at runtime.
- Complete SDL3 platform and input backend: window creation, Vulkan surface, fullscreen and
  display modes, nanosecond clock, gamepad hot-plug. Shared code no longer includes SDL headers.
- Audio now uses `SDL_AudioStream`; threading uses the SDL3 names; crosshair, statistics readout
  and the in-game panel wheel all go through the platform layer.
- The release folder now ships both executables side by side; they share one `Data\` tree.

### Light gun
- Aiming is sub-pixel accurate, so a slow, precise aim no longer steps pixel by pixel
  (DirectInput behaviour is unchanged).
- Late pointer sampling: the OS cursor is re-read immediately before the frame is emulated, and
  only the four gun axes are re-polled, so no button edge can be seen twice. Every motion event
  a 1000 Hz mouse produces within a frame is included.
- The Vulkan crosshair is drawn natively instead of through a full ImGui frame. The median CPU
  gap between input sampling and frame submission drops from about 78 µs to about 13 µs; the
  crosshair's size, position and colours are unchanged.
- New `SM3D_FRAME_TIMING=1` diagnostic logs the ImGui frame cost (`ui`), the `RunFrame`
  cost (`run`) and the poll-to-render / poll-to-present gaps once a second.

### Performance
- The SDL3 frame-tail spin drops from 2 ms to 0.5 ms. Over a 20 s `lostwsga` run the
  process CPU falls from about 13.2 s to 11.0 s (~10.7% of one core) with no audio
  under-runs. The SDL2 build is unchanged.
- The same diagnostic also logs `Sleep timing` (p50/p99/max wake lateness) and the audio
  under/over-run counters.
- LTO was evaluated: with the frame limiter off both builds run at 117–118 fps, so the
  CMake option stays off.

### Display
- "Exclusive fullscreen - G-SYNC/VRR" now paces at the arcade 57.5 Hz automatically when
  the display's timing range includes it, instead of running 4.3% fast at 60 Hz. Switching
  back to borderless restores the stored refresh rate.
- The Video page shows Refresh rate as 57.5 Hz and greys it out in that mode;
  "60 Hz (recommended)" is now "60 Hz (non-VRR / 60 Hz panel)".
- The Video page no longer lists raw `ExclusiveRefreshRate` / `ExclusiveFullscreen`
  values; the three-way refresh mode on the Common page owns them.
- Fixed a false "borderless frame cadence" notice under exclusive G-SYNC.

### Notes
- The SDL3 build has no netplay / MAMEHooker network outputs yet, and force feedback is rumble
  only.
- If the config names `xinput` / `dinput` / `rawinput`, the SDL3 build falls back to
  `sdlgamepad` and says so in the log. Both executables share `Data/Config`, so no edits are
  needed.

### Fixed
- The Quit confirmation dialog clipped its button labels (`Save and Quit`, `Don't Save`) in the
  English UI at high DPI. Button widths are now derived from the translated labels, so the
  auto-sized dialog grows to fit them.

## 2026-10-19

### Fixed
- "Custom" in the Resolution dropdown did nothing.
- Blue bars after disabling Widescreen, and a blue line along the top in widescreen mode.
  The late-loaded cabinet backdrop never updated composite descriptor slot 3, and the widescreen
  picture rectangle ignored the integer-scale letterbox.
- Controls tab stutter: the input system was rebuilt on every visit. It is now cached across tabs
  and released before a game starts.
- First-run window stayed on the taskbar after allowing the app through SmartScreen.
- Release archives kept `Assets/` beside the executable while the Windows build reads `Data\Assets\`,
  so downloaded cover art and crosshair bitmaps were never found. The first run now copies the folder
  (including `Boxarts/`) into place, and existing installations are repaired on the next start.

### UI
- The built-in cabinet backdrop is enabled by default for new installs.
- In-game panel:
  - collapsible CRT Filter section;
  - reworked save-state table with absolute `YYYYMMDD-HH:MM` timestamps;
  - removed the Use for F5/F7 button (Save/Load also make that slot current);
  - mouse-wheel scrolling;
  - default height is now 75% of the work area;
  - removed redundant help paragraphs and their reserved footer space;
  - tooltips stay inside the panel and no longer cover the scrollbar.
- Settings window:
  - Controls tab moved after Sound;
  - controller explanations moved into the corresponding (?) tooltips;
  - tab bar shrinks to fit instead of leaving a partially clipped tab;
  - the vertical scrollbar is always reserved, so the tab scrolling arrows no longer move;
  - the output-display selector is back in the Video section and no longer hidden when Fullscreen is off;
  - a preview panel above the game list shows the hovered or selected game's WebP cover, title,
    version and a short English description, with the scrolling list below it;
  - the divider between the preview and the list can be dragged, and both parts scale with the
    window; the position is remembered in `GuiPreviewSplit` across launches;
  - the ROM count moved from inside the list to the footer status line, giving the table that
    extra row of height.

### Build
- MSVC Release x64 build passes.
- Boxarts now cover all 63 ROM sets in `Games.xml`:
  - 55 from the libretro MAME set (unchanged);
  - `von2` / `von2a` / `von254g` / `von2o`: US Dreamcast cover (Cyber Troopers Virtual-On Oratorio Tangram);
  - `fvipers2` / `fvipers2o`: JP Dreamcast cover (Sega Retro);
  - `lamachin`: arcade flyer (Arcade Flyer Archive);
  - `skichamp`: arcade cabinet photo (Sega Retro, no standalone boxart exists).
- Added `Scripts/install_missing_boxarts.py` to fetch these eight sets and convert them to WebP;
  `SOURCES.txt` records the sources.
- Named by ROM set and copied into `Data/Assets/Boxarts` by `build.bat`.
- The release package now ships the artwork and the built-in backdrop: `Assets/` (including
  `Boxarts/`) is packaged, and `Supermodel3Bezel.png` is added to `PKG_FILES` and moved into
  `Assets/` while staging.
```
