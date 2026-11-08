# Model-3-Vulkan — 项目上下文（给 AI agent）

> 本文件是长期有效的项目说明。开工前先读这里；完整的文档地图见 `INDEX.md`。
> 最常用的三份：`devdocs/CHANGELOG.md`（每轮改动记录）、
> `devdocs/TECHLOG.md`（深入技术日志，很长）、`devdocs/FIXES.md`（可复用的排错模式）。

## 项目是什么

基于Supermodel 0.3a（Sega Model 3 街机模拟器）的个人分支，以 Windows x64 为主，
同时保持 Linux 可编译。SDL2 + 自绘 ImGui 设置界面；渲染后端有 OpenGL (New3D)
和 Vulkan 两套。本分支的重点：Vulkan 后端画面正确性、GUI 改进、音频、
CRT 滤镜、机柜背景图（bezel）。

- 目录**不是 git 仓库**（没有 `.git`），不要提议提交或建分支。
- 用户用中文交流；代码、日志、界面文案用英文（游戏简介固定英文，不做中文切换）。

## 命名与版本

- 产品名：**Model 3-Vulkan**；当前 fork 版本 `0.9.9.6`（`MODEL3VULKAN_VERSION`）。
- Vulkan 主程序：`Model3-Vulkan.exe`；OpenGL 版仍叫 `Supermodel-LegacyOG.exe`。
- 冷路径模块：`Model3VulkanHostCore.dll`（工程 `HostCore/Model3VulkanHostCore.csproj`）。
- Vulkan 配置：唯一配置文件为 `Data/Config/Model3-Vulkan.ini`（不再保留旧版 `Supermodel.ini`；若存在旧版文件，启动时无新文件则重命名迁移、已有新文件则自动清理删除；陈旧 `GameXMLFile` 路径启动时自动修复）。
- 诊断日志：`Data/Model3Vulkan.log`（自动记录构建时间戳、宿主物理内存、逻辑核数、音频 DSP 全链路状态及非默认外设轴校准值）。
- 安装目录是 `dist/Model-3-Vulkan`；GitHub 仓库名是 `Model-3-Vulkan`；控制台横幅是
  `Model 3-Vulkan v0.9.9.6 - Igloo-Garage`。
- 图标与启动图的主文件在根目录：`output.ico`、`Igloo-Garage Presents.webp`。
- **历史文档保留当时的旧名**（`devdocs/CHANGELOG.md`、`TECHLOG.md`、`sdl3_reports/` 等）：
  它们记录的是当时的构建，不要批量替换成新名字。

## 常用文件

| 内容 | 位置 |
| --- | --- |
| 设置界面、游戏列表、封面预览、分隔条 | `Src/OSD/SDL/Gui.cpp` |
| 游戏内 Tab 面板、存档表格 | `Src/OSD/SDL/Overlay.cpp/.h` |
| 主循环、窗口、输入 | `Src/OSD/SDL/Main.cpp` |
| 界面翻译 | `Src/OSD/SDL/Localization.cpp` |
| 启动图（嵌入资源 + SHA-256 指纹） | `Src/OSD/Windows/SplashScreen.cpp`、`SplashResource.h` |
| Vulkan 2D / bezel | `Src/Graphics/Vulkan3D/Vulkan2D.cpp`、`BezelLayout.h` |
| 游戏列表元数据（63 个 ROM set） | `Config/Games.xml` |
| 封面与来源 | `Assets/Boxarts/<set>.webp`、`SOURCES.txt` |
| SCSP / DSP 分析（FV2 爆音） | `devdocs/DSP.md` |
| 音频全链路修复与 5.1/WAV/FLAC 手册 | `runbook_sound_fix.md` |
| REXABYTE 原创双许可独立模块（零 GPL 依赖） | `Src/Sound/IglooAudioDSP.h`、`Src/Sound/IglooMultiChannelRecorder.h`、`Src/Graphics/Vulkan3D/IglooChaseCamera.h`、`Src/Inputs/IglooInputFilter.h` |
| PowerPC → x86-64 混合 JIT | `Src/CPU/PowerPC/ppc_jit_x64.inl`、`X64Emitter.h`、`JIT_runbook_for_X64.md` |
| HD 纹理模块化 / 移植流程 | `devdocs/RUNBOOK-HD-TEXTURES.md` |
| 版本号 | `Src/Version.h` |
| 打包脚本 | `build.bat`（编译并组装 `dist/Model-3-Vulkan`） |
| Linux/macOS 发布流程 | `devdocs/RUNBOOK-RELEASE-LINUX-MACOS.md`（LAN 笔记本远程编译 + Apple Silicon CI + 打包/发布） |
| 帧率/截图/验证工具 | `Tools/Measure-FrameRate.ps1`、`Tools/Capture-Frame.ps1`、`Tools/Validate-Vulkan.ps1`、`Tools/Restore-TestIni.ps1` |
| 优化前后对比、原始样本、截图 | `sdl3_reports/`（索引见 `README.md`） |
| 用户反馈台账（issue / 评论 → 结论 / 状态 / 回复口径） | `issues_from_users.md` |
| 文档地图（先读） | `INDEX.md` |
| 玩家手册（英文主版 / 中文精简版） | `usermanual.md`、`usermanual_chs.md`（`build.bat` 会带进发行包） |

## 构建与部署

完整打包（推荐；会先结束正在运行的模拟器）：

```
build.bat
```

要直接得到可发布的干净压缩包（排除存档/NVRAM/截图/缓存/ini/日志）：

```
build.bat -package
```

产物在 `build-package/Model3-Vulkan.v<版本>.for.Windows.zip`。

阶段 2 的 CMake/Ninja 验证构建（产物在 `build-cmake/bin/`；
`build.bat` 仍是发布打包入口）：

```
build-cmake.bat
```

终端是 `sh`，`cmd /c` 不可用。从 Bash 里用 `cmd.exe //c build.bat`，
或只做增量编译再手动部署：

```
powershell -NoProfile -Command "& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'MSVC\Supermodel.vcxproj' /t:Build /p:Configuration=Release /p:Platform=x64 /m /v:q /nologo /clp:ErrorsOnly"
cp MSVC/x64/Release/Supermodel-LegacyOG.exe dist/Model-3-Vulkan/Supermodel-LegacyOG.exe
cp build-cmake/bin/Model3-Vulkan.exe dist/Model-3-Vulkan/Model3-Vulkan.exe
cp HostCore/publish/Model3VulkanHostCore.dll dist/Model-3-Vulkan/Model3VulkanHostCore.dll
```

- 只改资源或文档时不需要重编译，但要把 `Assets/` 同步到
  `dist/Model-3-Vulkan/Data/Assets/`（`build.bat` 会自动做）。
- 覆盖 exe 失败通常是模拟器还在运行。

## Linux / Ubuntu

依赖（`Scripts/install-linux-deps.sh` 装的就是这些）：

```
sudo apt install build-essential pkg-config \
    libsdl2-dev libsdl2-net-dev libglu1-mesa-dev zlib1g-dev libwebp-dev \
    libvulkan1 mesa-vulkan-drivers fonts-wqy-microhei
# .zst 保存需要 host core（Native AOT），在目标系统上发布：
sudo apt install clang zlib1g-dev
# 另需 .NET 11 SDK（Microsoft 的 apt 源或 dotnet-install.sh）
```

构建与运行：

```
# 1) SDL2/OpenGL 版（历史 Makefile；没有 .zst 卡片管理器）
make -f Makefiles/Makefile.UNIX          # 输出 bin/supermodel
./bin/supermodel -gui                    # 在仓库根目录运行

# 2) Vulkan 版（CMake；产物 build-cmake/bin/Model3-Vulkan）
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DUSE_SDL2=OFF -DUSE_SDL3=ON -DSUPERMODEL_SDL3_PURE_VULKAN=ON
cmake --build build-cmake
cmake --build build-cmake --target hostcore   # 需要 .NET 11 SDK + clang
./build-cmake/bin/Model3-Vulkan -gui
```

- 从仓库根目录运行时，配置和资源直接用该目录的 `Config/`、`Assets/`；
  换到别的目录运行则走 XDG 路径：`~/.config/supermodel/Config/`
  （`Supermodel.ini`、`Games.xml`）和
  `~/.local/share/supermodel/{Assets,NVRAM,Saves,Screenshots}/`。
- **ROM 路径**：Linux/macOS 的默认值是 exe 旁的 `roms/`（首次运行创建）；Windows 仍保留
  `C:/Downloads` 作为旧默认。三平台启动时都会扫描 exe 旁的 `rom`/`roms`/`romset`/`romsets`
  （大小写不敏感，优先含 `.zip` 的目录），找到就写入相对路径，移动整个文件夹后仍然有效。
- **`.zst` 存档**：只有 `SUPERMODEL_PURE_VULKAN` 构建有 HostCore。Linux/macOS 上必须在
  目标系统执行 `cmake --build build-cmake --target hostcore`（Native AOT 不支持跨操作系统
  交叉编译），产物分别是 `libModel3VulkanHostCore.so` / `libModel3VulkanHostCore.dylib`，
  放在 exe 旁即可；Windows 由 `build.bat` 发布 `Model3VulkanHostCore.dll`。
- 封面预览三平台都可用：Windows 用 WIC，macOS 用 ImageIO/CoreGraphics，
  Linux 用 libwebp（`libwebp-dev` 编译期、`libwebp` 运行期）。

## 运行环境

- ROM 目录：`E:\EMU\model-3-romset`
- 配置：`dist\Model-3-Vulkan\Data\Config\Model3-Vulkan.ini`
  （首次运行从旧版 `Supermodel.ini` 播种）
- 可运行目录：`dist\Model-3-Vulkan\`

## 约定

- UI 文案英文，中文只通过 `Localization.cpp` 提供。
- 发版只改 `MODEL3VULKAN_VERSION`（当前 `0.9.9.6`）。**不要动**
  `SUPERMODEL_VERSION`：它写进存档，改了就使所有旧存档失效。
- 版本号同时是 release tag：每次发版必须 bump，否则会覆盖上一个 release。
- 每轮改动都要写进 `CHANGELOG.md`：前半是中文详述，后半是可直接贴到
  GitHub Release 的中英短日志，两份保持同步。
- 第 10.2 节的每项优化完成后，把完整的前后对比（命令、原始样本、截图、
  验证矩阵）写进 `sdl3_reports/`，并在其 `README.md` 索引里加一行；
  `CHANGELOG.md` 只保留面向用户的摘要。
- 只影响 Windows 的代码用 `#ifdef _WIN32` 包起来，保持 Linux 可编译。
- 两个二进制的渲染能力是编译期固定的：`Supermodel-LegacyOG.exe` 只有 OpenGL
  （`SUPERMODEL_OPENGL_ONLY`），`Model3-Vulkan.exe` 只有 Vulkan
  （`SUPERMODEL_PURE_VULKAN`）。两个宏互斥，不要给同一个目标同时定义。
  Vulkan 源文件只在 `supermodel_core_vk` / `supermodel_core_dual` 里。
- **新功能只做 Vulkan 版，OpenGL 版冻结**。`Supermodel-LegacyOG.exe` 只接受 bug 修补
  （崩溃、渲染错误、输入/存档缺陷）和维护性改动，**不加新功能、新设置项、新界面**。
  写新东西时默认放进 `#if !defined(SUPERMODEL_OPENGL_ONLY)` 或直接进 Vulkan 源文件列表；
  需要共享的声明用空壳实现（参考 `DrawHDTextureSettings` 的写法），否则 LegacyOG 会
  编译或链接失败。Vulkan 独有清单见 `devdocs/VULKAN-ONLY-FEATURES.md`。
- 两个 exe 共用 `Data/Assets/`（含 `Boxarts/`），改名不影响封面路径。
- 资源复制规则写在 `build.bat`（例如 `Assets\Boxarts\*.webp`）。
- 封面在运行期解码 WebP，纹理最大宽度 384，按 ROM set 缓存；缺图结果也会
  缓存，补图后要重启程序才可见。Windows 用 WIC，macOS 用 ImageIO，
  Linux 用 libwebp。

## 已知的坑

- 终端是 `sh`：`cmd /c` 无效；PowerShell 里 `$` 变量要写进 `.ps1` 再 `-File`。
- 抓图不要用 `curl`：部分站点要求 UA/Referer。用 Python `urllib` 下载 +
  Pillow 统一转 WebP，参考 `Scripts/fetch_boxarts.py`、
  `Scripts/install_missing_boxarts.py`。
- `segaretro.org` 的图片路径大小写敏感。
- 设置界面、游戏内面板、封面缓存的改动都要重启程序才生效。
- Vulkan 设置窗口不能一直隐藏到首帧：隐藏窗口的 swapchain 在部分驱动上无法
  acquire，首帧会一直失败到启动图的 5 秒保底。现在上下文就绪后立即
  `ShowWindow`（启动图是它的 owned window，仍在最上层）。启动分段看
  `Data/Supermodel.log` 里的 `GUI: ... (N ms)` 与 `Vulkan: instance/device`。
- 桌面上的隐式 Vulkan 层（Steam、RTSS、ReShade、OBS、GOG）会让每次
  `vkCreateInstance` 多花约 1 秒。预检和设置窗口实例在创建时临时设
  `VK_LOADER_LAYERS_DISABLE=*`，调用后恢复；游戏会话实例保持加载层。
  改这段时不要动 `VkValidation` 路径（验证层不能被过滤）。
- SDL2 与 SDL3 在同一个 CMake 缓存里构建：`SDL_*` 缓存变量是全局的，
  只在某个目标分支 `set(... CACHE ...)` 会影响另一个。SDL3 必须显式
  `set(SDL_VULKAN ON CACHE BOOL "" FORCE)`（见 `CMakeLists.txt`）。
  症状：启动时报 `Vulkan support is either not configured in SDL`。
- **Linux 发布包的 glibc 基线是 2.39，由 CI 钉死**：0.9.8.6 的 Linux 包是在
  `ubuntu-latest`（已漂到 26.04）上编的，产物要求 `GLIBC_2.43` / `CXXABI_1.3.15`，
  Steam Deck 与 Ubuntu 24.04 在动态加载阶段就退出——无窗口、无日志，最容易被误判成
  Vulkan 问题。workflow 现在固定 `ubuntu-24.04` + `gcc-14/g++-14`，并有 `readelf` 守卫
  （`GLIBC_2.39` / `CXXABI_1.3.15` 以上即失败）。在 24.04 上实测产物最高只要求 `GLIBC_2.38` +
  `CXXABI_1.3.15` + `GLIBCXX_3.4.32`，SteamOS 3.5 及以后都能加载。不要图省事改回 `-latest`。
- **macOS/Linux 构建的两个新坑（2026-11-07 实测）**：`Gui.cpp` 不能用 MSVC 专有的
  `_stricmp`（用文件内 `CaseInsensitiveCompare()`：Windows `_stricmp` / POSIX `strcasecmp`）；
  Zig 的 clang 在 Release 下把 `-Wdate-time` 当错误，`CMakeLists.txt` 已给 Clang 目标加
  `-Wno-date-time`（启动横幅故意用 `__DATE__`/`__TIME__`）。另：Windows→macOS 用 tar 同步后
  脚本是 CRLF，要先转 LF，否则 `fetch-cmake-deps.sh` 报 `: invalid option name`。
  另：MoltenVK 会对 `primitiveRestartEnable = VK_FALSE` 的 TRIANGLE_STRIP 管线刷
  `[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Metal does not support disabling primitive restart`；
  全屏 quad 的 5 处 strip 管线已设 `VK_TRUE`（无 restart index，无行为变化）。
- **macOS 的“点”与“设备像素”必须分开**（4K 上分辨率设置无效的根源）：
  `SDL_GetWindowSize()` 和 `SDL_GetDisplayMode()/GetDesktopDisplayMode()` 在 macOS 上是 **point**，
  面板像素比在 `SDL_DisplayMode.pixel_density`（4K「Looks like 1920×1080」= 2.0）。
  - **渲染几何（`xOffset/xRes/totalXRes`、2D 合成矩形、bezel、准星、3D 视口）必须是设备像素**，
    否则 4K 面板会按 1080 行渲染再由合成器放大；`CalcScreenGeometry()` 走 `GetRenderSize()`。
  - **输入（`CInputSystem::SetDisplayGeom` 与鼠标）必须是窗口坐标**，否则轻枪与相对轴会缩放两次；
    两个调用点用 `WindowSpacePictureRect()`。
  - **游戏窗口必须带 `WindowFlag_HighDPI`（仅 `__APPLE__`）**，否则 drawable 就是 1x，改几何也没用。
  - **游戏内 ImGui 面板的 `io.DisplayFramebufferScale` 不能写死 1.0**（`Overlay.cpp` 的
    `FeedInput()`）：`ImGui_ImplVulkan_RenderDrawData()` 靠它投影，写 1.0 会让面板画在左下
    四分之一、只有一半大。设置窗口走的是 `ImGuiPlatformNewFrame()`，那边本来就对。
  - 窗口隐藏创建时 Cocoa 可能仍报 1x drawable，所以 `GetRenderSize()` 与「桌面模式 ×
    pixelDensity」取大者；不要把它改回只看 `GetWindowPixelSize()`。
  - 注意**窗口尺寸仍用 point**（`SetWindowSize` / `GetConfiguredFullscreenSize`），
    `ApplyWindowSize()` 不要改成像素，否则全屏窗口会比屏幕大一倍。
- **界面字体随包发**：`Assets/Fonts/wqy-microhei.ttc` → `Data/Assets/Fonts/`（Apache-2.0 /
  GPL-3+ 字体例外，附许可说明）。Linux 上优先用它，Windows/macOS 仍优先系统字体。
  `ProbeFontData()` 会拒绝**没有基本拉丁字形**的字体——Droid Sans Fallback 就是这种合法但
  CJK-only 的字体，选中它会让英文界面“控件都在、文字全无”（原版 Ubuntu 默认没装 wqy/Noto）。
- **从 SSH 里启动 Linux 版会卡在 Wayland present**（测试环境的坑，不是用户侧问题）：
  `ssh` 会话里用 `WAYLAND_DISPLAY=wayland-0` 跑，主线程永久停在
  `EndFrameVideo() → CVulkanContext::EndFrame() → wl_display_dispatch_queue`（Mesa WSI），
  日志停在 `Bezel: ...`、窗口不出现。与 HostCore 无关（去掉 `.so` 一样复现），加
  `DBUS_SESSION_BUS_ADDRESS` 也无效。交互式启动正常。要在远程抓帧/验证就用 Xwayland：
  `DISPLAY=:0 XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.* SDL_VIDEODRIVER=x11`。
  根因是游戏窗口隐藏创建、首次 present 之后才 `ShowWindow()`，无激活令牌时 compositor
  可能一直不给 frame callback；0.9.8.7 值得改成“首次 present 前就显示窗口”。
- **Linux 的 `FileSystemPath` 也认 `<exe>/Data/`**（与 Windows/macOS 一致）：发行包里的
  `Data/Config/Games.xml`、封面、内置字体都靠这条；`ResolvePath()` 同时锚定可执行文件，
  日志不再依赖启动目录。没有 `Data/` 的源码树回退到旧的当前目录 / XDG。
  副作用：在 `build-cmake/bin/` 里运行时，配置与日志落在 `build-cmake/bin/Data/`。
- **GNOME Wayland 的窗口标题栏来自 libdecor**：构建机缺 `libdecor-0-dev` 时 SDL3 会关闭
  `SDL_WAYLAND_LIBDECOR_SHARED`，设置窗口没有装饰、拖不动也关不掉（只能 Esc）。CI 与
  `Scripts/install-linux-deps.sh` 已包含该依赖。
- **Wayland 的“工作区”等于整块屏幕**：`SDL_GetDisplayUsableBounds` 不扣面板，
  `GetWindowBordersSize()` 在 libdecor 下返回 0。设置窗口的初始尺寸与 `ApplyGuiWindowMinimumSize()`
  都留了 48 逻辑像素的装饰余量（仅当上下边框为 0 时生效）；否则 1280×800 的 Steam Deck 上
  默认窗口（客户区 800 + 标题栏）会超出屏幕底部。改这两处时不要只按 `usableBounds` 夹。
- Linux 的封面依赖 libwebp-dev（编译）与 libwebp（运行）；缺库时 CMake 在
  配置阶段就会失败，Makefile 构建则在链接时报 `-lwebp` 找不到。
  注意 `DecodeBoxart()` 在 Linux 上只能解 WebP，而存档缩略图是 PNG：
  `DecodeImageBytes()` 用 `Util/PNGFile.h`（zlib）在内存里解码 PNG 并按 `maxWidth` 缩略，
  不要删掉这段而只留 libwebp。管理存档的卡片上传也不再限定 `_WIN32`。
- **改 ImGui 布局后检查**：tooltip 不盖住滚动条、Tab 栏不出现半截、
  垂直滚动条常驻以免控件位置跳动。
- **宽表格必须按字体/DPI 实测列宽，并在放不下时给横向滚动条**：不要写死像素
  （如 `TableSetupColumn(..., 135.0f)` 或 `SetNextItemWidth(68.0f)`）——1.5x/2x 缩放下
  这些值连 `-32768` 或表头本身都装不下。「方向盘与踏板轴死区 / 行程校准」表格的做法：
  用 `ImGui::CalcTextSize()` + `style.CellPadding` / `FramePadding` 算每列最小宽度；
  面板够宽时全部 `WidthStretch`（以最小宽度为权重）铺满，放不下时切
  `ImGuiTableFlags_ScrollX | SizingFixedFit` + 显式 `outer_size`，让表格底部出现
  左右拖动条。不要再退回固定像素或只给最后一列 `WidthStretch`。
- **`UiTextDim` / `UiTextWarn` / `UiTextHighlight` / `UiExplain` 有单参数与多参数两个重载**：
  单参数走纯文本 `ImGui::TextDisabled("%s", Tr(text))`，绝不经 `snprintf`；只有带参调用
  才走格式化模板。**不要合并回单个 `(const char* format, ...)`**：英文文案里的 `100% throw`
  会被静态 UCRT 解析成 `%`+`t`+`h` 并在 `state_case_size` 里 `_invalid_parameter` 强退
  （`0xC0000409`，点开「力反馈 / 灵敏度」即崩溃）。
- **场景快照必须在 `EndSimulationFrame()` 之后、`BeginSimulationFrame()` 之前遍历**
  （`CModel3::PrepareSceneSnapshot()`）。放到 `RenderFrame()` 里会与 PPC 写下一帧
  重叠 —— 中间隔着最长 8ms 的 `WaitForPresent()`，Culling RAM 只写了一半，
  插值会把完整帧和半帧混在一起。
- **`RenderFrame(alpha, newSimulation)` 里是 `if (newSimulation && !m_scenePrepared)`**：
  已就绪的快照再置脏会触发第二次遍历，把刚建好的场景换进“上一帧”槽，
  结果 `Interp:` 行显示 `static = 实例数`（每个实例都在和自己比较），插值静默失效。
- **插值权重的来源是 `pacer.AlphaAt(Now())`，不是 `pacer.LastAlpha()`**（后者只用于
  遥测）。呈现前先 `WaitForPresent()` 再取 alpha，睡眠时间算进这一帧。
- **提交相位校准必须只认“明显晚于 vblank”**：判据是上屏耗时 `latency > vblank/2`，
  否则当 0 处理。小于一半说明截止点已经排在 vblank 前面，或者面板在跟随我们（VRR）——
  这两种情况加提前量只会让画面更旧、延迟变大。提前量是**相位平移**，不改节拍；
  `SetPresentLead()` 上限一个 vblank。校准只在 pacer 配置后的 16 帧里阻塞测量，
  之后每帧只在截止点做一次零超时确认。
- **实例插值默认关闭**（`m_instanceInterpolation`；键 `InstanceInterpolation`，**无界面开关**；
  `SM3D_INST_INTERP=1/0` 可强制开/关）。开启后 Daytona 2 会出现**单帧**路面
  白横线/白块（地面缺一块三角、露出雾色），只留相机插值则干净。
  - **根因不是配对键污染**（新增四层配对校验：键双向唯一、同几何、位移连续性
    EMA、非有限输入拒绝；日志 `Interp:` 行有 `far/rejected/ambiguous/decompose
    fail` 计数，实测全程为 0）。
  - 真正机制：**同一帧里混用两种公式**。被跳变（snap，旋转 >`InstanceCutAngleDegrees`
    或位移异常）的实例画在“当前帧矩阵”，相邻正常插值的实例画在“两帧之间”，
    共边处差 `(1-α) × 该帧位移`（α≈0.93 时约 1-2 像素）→ 细白线，随 α 闪。
  - 试过把实例插值改成逐元素线性（`LerpMatrix`）：能严格保持“两个都插值”的共边，
    但管不了“一个插值一个跳变”的边界，**实测细线未消失**。LerpMatrix 保留，
    但它不是解。
  - 绝对干净的唯一办法是同一帧统一公式；“全部插值”会让 >30° 的实例（广告牌、
    车轮）拖影/塌线。所以保持默认关。日志 `Interp:` 有
    `turn <1/<5/<15/<30/>=30(>=90)` 分桶。
- **切镜计数要逐帧累加**，不能每 N 帧采样一次：采样点会落在两次切镜之间，
  日志长期显示 `0 cut`，而这正是该行存在的意义。
- **相位模式的 alpha 必须是 `k/N`（`k = 0..N-1`，仿真槽位 = 0）**，不能写成
  “仿真拍 1.0、插值拍 0.5”：快照对在仿真拍才更新，后者会让画面每拍前进 1.5
  再后退 0.5。要零延迟就用 `LowLatency` 关插值（全部 1.0）。
- **相位在 `RecordPresent()` 推进，不能在 `WaitForPresent()`**：`AlphaAt()` 在
  渲染前调用，在等待结束时推进会让 alpha 描述下一格；切镜标志也是同理，
  `NoteFrameCut()` 必须在 `PrepareSceneSnapshot()` 之后、`AlphaAt()` 之前。
- **新增倍频时同步改 `PanelSupportsPhaseCount()`**（`DisplayPacer.h`）：游戏循环
  和设置界面都调它，分别写两份的话界面会说可以而运行时回退。整段「呈现节奏」UI
  只在 `#if !defined(SUPERMODEL_OPENGL_ONLY)` 里编译：Legacy 跑的是原始
  “一仿真一呈现”循环，`highRefresh` 恒为 false，pacer 从不配置。
- **时间基准只有一个常量，且是 `kModel3StepNs = 17,384,000`**：它等于 `1e9 / 57.524160`
  截断（差 2.6 ppb）。不要改回 17,384,048——那是把 57.52416 四舍五入到整毫赫兹后的结果，
  慢 2.8 ppm，而且会被 pacer 的 Original 目标、VRR 2x–8x（115.04832 / 172.57248 /
  230.09664 / 460.19328 Hz）和音频样本累加器一起继承。OpenGL 经典循环的帧周期由
  `GetDesiredFramePeriodNs()` 从 Hz 直接算（`1e9 / Hz`），**不要**再走「Hz × 1000
  截断成 mHz 再除」那条路，它会重新引入同样的 48 ns/帧。
- **`RefreshRate` 的「真实机台」档存 `57.52416`，不是 57.5**：57.5 会让每个画面多 7.3 µs，
  游戏相对墙钟慢 0.042%，音频每秒少产 19 个样本，400 ms 缓冲约 16 分钟见底后周期性欠载。
  旧 ini 由 `AlignLegacyArcadeRefreshRate()` 在内存里迁移（`main()` 静默改设置界面的副本，
  `RunGameSession()` 改运行时副本并打日志）；不要改回 57.5，也不要只改界面文案。
- **自绘平台层必须自己更新鼠标指针形状**（`ImguiPlatformNewFrame()` 末尾按
  `ImGui::GetMouseCursor()` 调 `SetSystemCursor`）。它取代了 `imgui_impl_sdl*`，
  官方后端里的 `UpdateMouseCursor()` 不会自动跑；漏了的话所有
  `ImGui::SetMouseCursor()` 都静默无效（分隔条没有上下箭头、输入框没有 I-beam）。
  这段必须放在 `ImGui::NewFrame()` **之前**，因为 `NewFrame()` 会把请求重置为 Arrow。
- **Vulkan 设置窗口不要为了 swapchain 提前 `ShowWindow`**：窗口客户区在第一帧
  present 前是黑的，而启动图是方形、盖不住窗口，会露出黑边。`GuiRunOnce()`
  现在等第一帧 present 后才显示，只有 `BeginFrame()` 真的失败时才兜底提前显示。
- **`DefaultConfig()` 里 `#if !defined(SUPERMODEL_OPENGL_ONLY)` 只能放真正的
  Vulkan 键**。输入类键（`VRCycle`、`VRDefaultView`）必须在公共区：设置界面
  无条件读它们，缺键会抛 `Node "..." does not exist` 并 abort（0xC0000409）。
  发布包的 ini 往往已包含旧键，所以这种问题只在全新安装上复现。
- **volk 的入口点是进程级全局的**：`volkLoadInstance()/volkLoadDevice()` 会把函数
  指针改成新绑定的实例/设备。设置窗口和游戏会话各自持有一个 `CVulkanContext`，
  游戏会话销毁后全局指针仍指向它那个已销毁实例的 loader 跳板 —— 设备级函数靠
  传入句柄取 dispatch 还能用，实例级的 `vkQueuePresentKHR` 会直接在 ntdll 里崩。
  所以回到设置界面时必须调一次 `CVulkanContext::RebindFunctionPointers()`
  （现在在 `GuiRunOnce()` 开头）。新增“第二个 Vulkan 上下文”的路径时别忘了这一点。
- **三个场景目标是同一次 pass 的三个颜色附件**（`CVulkan3D::DrawScene()` 的
  `begin()`）：颜色层必须向 trans1/trans2 写 `vec4(0)`，这是跨 priority 唯一的
  透明像素擦除机制（New3D 的 `R3DShaderCommon.h` `WriteOutputs()`）。
  把某个层拆回独立 image、独立 pass 会让低优先级的海水一直留到帧末，VF3 沙滩
  就会把它合成到角色前面。`FillPush()` 的 `layer` 参数是 attachment 编号
  （0/1/2），bucket 1 = trans2 = attachment 2、bucket 2 = trans1 = attachment 1，
  两者不要混。**这套 mask 依赖 `independentBlend`**，`CreateDevice()` 里必须
  要求并启用；没有它时 `pAttachments` 必须完全相同，MRT 擦除就是未定义行为。
- **`composite.frag` 必须保留 3D 场景 alpha**：场景附件清成 `(0,0,0,0)`，雾 quad
  把雾浓度写进 alpha。合成时是 New3D 两次 `SRC_ALPHA` 混合的等价 source-over，
  不能把输出 alpha 写成 1；VF3 的月亮和 Lost World 的云层都是 Render2D Bottom，
  写 1 会让它们被雾色整块盖住。两层透明重叠时才输出 alpha 1。
- **`AlphaSortMode=1` 是另一套完全独立的半透明路径**（`real3d_oit.frag` /
  `oit_resolve.frag`）：
  - **必须在每个 Priority 内解析**，不能推迟到帧末。后一个 Priority 会清 depth 并画
    不透明几何，它必须无条件盖住前一个 Priority 的半透明；每层结束时把排序结果合成
    进颜色目标就天然得到这个语义，不需要“不透明擦除 OIT 节点”的额外规则。
  - 数据结构是**固定槽位 SoA A-Buffer**，不是链表：每像素 4 个 8 字节节点，槽位 `s`
    的像素 `p` 在 `s * pixelCount + p`；计数用 `imageAtomicAdd` 加在**该像素自己的
    r32ui 字**上，没有全局分配计数器。改回全局计数 + `atomicAdd` 的链表会让 4K 场景
    从 120 Hz 掉到 27–37 Hz。
  - **任何每帧上百万次访问的计数器/原子都不能放 HOST_VISIBLE 内存**：曾经为了读诊断
    把节点计数器放系统内存，每次分配走 PCIe，单这一项约 20 ms/帧（4K）。诊断值
    不值这个代价。
  - Capture PSO 必须 `early_fragment_tests` + **不写 `gl_FragDepth`**
    （`FrustumRZ` 下 `projection[3][2] * gl_FragCoord.w == gl_FragCoord.z`），
    `depthWriteEnable = FALSE`，`sampleShadingEnable = FALSE`，
    `alphaToCoverageEnable = FALSE`；Resolve 是逐采样执行（设备支持时）用节点里的
    覆盖掩码过滤。Capture pass 只绑 depth（`storeOp = NONE`），Resolve pass 只绑
    颜色附件 0——它们都不需要为 4K 4x MSAA 的三张未使用附件付 load/store。
  - 计数图在每个 capture pass 前清零（`vkCmdClearColorImage`，GENERAL layout），
    清零与上一 pass 的 resolve 之间要有 `FRAGMENT_SHADER → TRANSFER` 屏障；
    capture → resolve 之间是 `SHADER_STORAGE_WRITE → SHADER_STORAGE_READ` 的全局
    内存屏障，且**必须分成两个 dynamic rendering pass**（barrier 不允许在 pass 内）。
  - 着色逻辑只有一份：`ShadeFragment()` 在 `real3d_common.glsl`，两条路径共用。
    `kOitLayers = 4` 在 C++ 与两个着色器里各写一份，改一处要同步另两处。
  - 4K 下 A-Buffer 约 253 MB，分配失败会自动回退双层路径；每像素最多 4 层，超出
    丢弃。逐采样解析只有覆盖掩码、没有“深度通过的采样掩码”，不透明轮廓边缘可能
    有细微光晕。
  - 测试钩子：`SM3D_ALPHA_SORT=1` 强制开启，`SM3D_OIT_PER_SAMPLE=0` 强制像素率解析。
- **`AlphaSortMode=2`（街机真机/棋盘格）是试验模式，不要当通用选项**：
  - 两层半透明各自只写一半像素（`real3d.frag` 开头的奇偶 discard），合成阶段按 2×2 组盒式
    还原。合成后的 alpha 天然是原版的一半，这是模型本身的结果，不是 bug。
  - **它无法还原 LOD 交叉淡化**。交叉淡化把同一个面画两遍、alpha 互补，原版靠“两层都在 →
    不透明”还原；按像素拆层后这个信息就没了。实测 Daytona 2 瀑布（`daytona2.zst2` 第 190 帧，
    MSAA 4x）：瀑布区蓝色像素 原版 4.87% / OIT 3.92% / 棋盘格 **20.13%**（1x 下 12.09%）。
    着色器读数：该区域 20.2% 像素只有一层（alpha 0.65），27.2% 两层都在（0.66/0.54）。
  - 试过“两层合计覆盖 ≥1.9”与“单层覆盖 ≥1.8”判不透明，分别得到 7.3%（瀑布主体变半透）和
    18.1%（更糟），**都没有保留**。要真修必须另存逐像素的两层覆盖（见 `issues_waitlist.md`）。
  - 因此下拉框写「街机真机（棋盘格，试验）」，提示和手册都说明“只适合雪雾/尘土”。新增
    模式 2 相关代码时不要把它当成可推荐的默认，也不要为了让某个场景好看就改那条阈值
    分支（雪地阴影回归见 `sdl3_reports/2026-10-27-checkerboard-hd/README.md`）。
- **默认半透明路径是一个 2-Subpass 的 `VkRenderPass`**（`AlphaSortMode=0`；
  `CVulkan3D::CreateScenePass` / `DrawScenePass`）：
  - Subpass 0 一次画完 Priority 0..3（深度用 `vkCmdClearAttachments` 清，**不结束 pass**）；
    Subpass 1 用 `subpassLoad()` 读 Trans1/Trans2，硬件混合 `ONE / ONE_MINUS_SRC_ALPHA`
    叠到颜色附件。两张透明附件 `storeOp = DONT_CARE`、per-pass 不 resolve。
  - **场景颜色是预乘 Alpha**：几何层强制 alpha 1，只有雾 quad 变了（写 `rgb * fogAmount`）。
    2D 合成器的场景层因此用 `m_pipelineScene`（`ONE`），但瓦片层/机柜背景仍是直通 Alpha，
    不要合并成一条管线。`composite.frag` 的两层公式也改写成了预乘形式，由 push constant
    `sceneComposited` 控制是否执行。
  - **用 render pass 的管线必须用 render pass 创建**：动态渲染管线（`renderPass = NULL`）
    绑不进 render pass。`CreatePipeline(..., renderPass)` 因此建两套（`m_scenePipelines` 与
    `m_scenePassPipelines`），雾也有 `m_fog` / `m_fogPass`。新增着色器变体时两边都要建。
  - **深度附件布局必须写 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`**：未启用
    `separateDepthStencilLayouts` 时 `DEPTH_ATTACHMENT_OPTIMAL` 在 render pass 里非法（即使
    格式是 D32）。
  - 2D 合成器的 descriptor 仍静态引用两张透明目标，所以新路径结束时也要把它们（MSAA 下是
    1x resolve 图）转到 `SHADER_READ_ONLY_OPTIMAL`，否则每帧 `vkQueueSubmit2` 报 layout 不匹配。
  - **LOS 帧、白闪帧、`AlphaSortMode=1` 仍走动态渲染**。`SM3D_NO_SUBPASS=1` 强制旧路径。
  - 改 `scene_composite.glsl` 后要重跑 `CompileVulkanShaders.cmd`（它同时生成
    `scene_composite_frag` 与 `scene_composite_ms_frag` 两份头）。
- **`resolveMask` 的语义是“这个 pass 写了哪些附件”**：`begin()` 按位解析。透明层 pass 只
  解析 Trans1/Trans2（0x6），颜色 pass 解析三张（0x7）。之前写反了——透明 pass 解析了颜色
  附件，自己写的透明图没解析，导致 4x MSAA 下最后一个 Priority 的半透明完全丢失（Daytona 2
  车影/瀑布），而 VF3 恰好被下一个 Priority 的 color pass 顺手解析才看起来正常。
- **Vulkan 图元是四边形，不是三角形**：`CopyVertexData()` 每个图元固定发 **4 个槽位**
  （三角形把顶点 2 复制到槽位 3），`mesh.vertexCount` 恒为 4 的倍数，
  `vkCmdDrawIndexed` 的 `vertexOffset`（基准顶点）也必须是 4 的倍数——
  着色器靠 `gl_VertexIndex & ~3` 反推四边形。改回 6 顶点三角形列表会静默错位。
  - 顶点数据是 SSBO：顶点着色器读自己那一个角点，片元着色器按 `flat vQuadBase`
    读四个。ROM / 多边形 RAM 两个 binding 由**特化常量**选择（12 条场景管线），
    不要改成运行时分支——两个 binding 都可达时驱动可能把加载提到分支外。
  - 索引缓冲是 16384 个四边形展开的固定 `0,1,2,0,2,3` 块（192 KB），更大的 mesh
    在 `DrawBucket()` 里分段画；不要按整块顶点缓冲生成索引（那是 36 MB）。
  - 片元用**视图空间射线**（`vView`）× 双线性曲面求交得到 (s,t)，与屏幕空间参数化
    不同：它天然处理 eye 后面的角点，也不需要 viewport 矩阵。不要为了“更简单”
    换回屏幕空间版本。深度仍用 `gl_FragCoord.w`（与光栅化的三角形一致），
    换成四边形插值出来的 1/w 会让 decal 与地面 z-fighting。
  - 这个着色器不经过 sampler（手工解码贴图页 + `texelFetch`），所以没有硬件式的
    `textureGrad`。mip 层级默认由 `-log2(gl_FragCoord.w²·lodBase)` 给出（原机的径向
    估算）；可选模式（`TexFilter`=1/2）改用 `dFdxFine/dFdyFine(uv)` 的足迹椭圆，
    模式 2 沿长轴最多 4 个 tap。**改这段时注意**：
    - 模式 1 必须用**长轴**选级、模式 2 才用短轴 + `log2(ratio/used)` 回填，
      否则斜掠面会闪烁；
    - 预乘 Alpha 累加**只能用在 `taps > 1`**：单 tap 必须直接返回 `texBiLinear()`，
      否则 `a == 0` 的材质（无 `kTextureAlpha`/`kAlphaTest`，alpha 最后强成 1）
      会被除成黑色；多 tap 路径也必须保留一个非预乘和（`plainRgb`）作为
      `sumA == 0` 的回退——否则同样出黑块（Daytona 2 天空，见 #12）；
    - tap 数分档 1/2/4（阈值 1.35/2.5），放大区因此自然回到 1 tap；
      三线性因子 <1/256 时不取第二层，微纹理分支仍用精确的 `fract == 0`；
    - 导数跨四边形边会给出异常大的足迹，靠 `min(lod, radialLod + 8)` 单向封顶；
      不要改成双向 clamp——这个模式的收益方向就是“比径向更小”。
    - `ubo.texFilter` 位于 ViewportUBO（因此 block 是 96 字节；`renderOptions` 紧跟其后，
      不要把它拆到 push constant，那个块已经 208 字节且 vertex/fragment 共用）。
    - `renderOptions.x` 是 Vulkan 的精确四边形开关（键 `QuadInterpolation`，默认 1）。0 走
      顶点着色器插值的快速路径，跳过 4 次 SSBO 顶点读取和 `QuadParams()` 二次求解；
      非平面四边形会出现 0-2 对角线接缝，所以只有超低配预设会把它设 0。它和 New3D 的
      `QuadRendering`（OpenGL 的三角形/四边形几何路径）不是同一个键，不要合并。
    - 调试时注意：Daytona 2 场馆的 LED 记分牌是**动态内容**，不同时刻的截图
      不能直接对比（曾因此误判模式 2 “丢字”）。`-At` 是墙钟时间，不同构建的
      启动开销不同，两张截图可能差几帧；要 A/B 就比同一帧的统计量（或先对时）。
- **`Capture-Frame.ps1 -RawState` 依赖 `SM3D_KEEP_STATE_TEMP`，两条读档路径都必须遵守**：
  立即读档（`LoadState()` 尾部）和延迟读档（帧循环里应用 pending state 之后）都只在未设
  该变量时删 `Data/Saves/.vktmp.st`。漏掉后者时脚本拷到的是 `-State` 指向的 `.zst` 容器，
  LegacyOG 报 `does not appear to be a valid save state file`（容器 = zip + `core.zst`）。
- **Scud Race 的 HUD 排查入口是 `SM3D_HUD_DEBUG=1`**（`R3DSceneBuilder::DumpHudViewport()`）：
  按视口打印 HUD 多边形的状态、面颜色、贴图矩形与 `translatorMap`/`inverted`，每个状态只在
  一个会话里打一次。现状：`scuddx`/`scud`/`scudau` 的 CONDITION 视口里**没有任何贴图多边形**，
  TileGen 层也为空，详见 `issues_waitlist.md` 与 `sdl3_reports/2026-11-01-scuddx-hud/`。
- **截图用 `Tools/Capture-Frame.ps1`**（内部走 `SM3D_SCREENSHOT_AT`）。窗口焦点、
  独占全屏、`.zst` 与 raw `.st` 的差异都已在脚本里处理；`-State` 的相对路径
  按 `-Bin` 目录解析，脚本会先转成绝对路径。
- **文档/报告/测试截图一律用 `.webp`**（用户 2026-11-07 明确要求；`Tools/Capture-Frame.ps1`、
  `Tools/Capture-Ui.ps1`、`Tools/Crop-Image.ps1` 的 `-Out` 都接受 `.webp`，内部用 ffmpeg
  `-lossless 1` 转换，不要为了省事写 `.png`）。`sdl3_reports/` 里新增的截图不要再写 `.png`。
  引擎自带 `Alt+S`（`Data/Screenshots/`）仍写 PNG，那是游戏内功能，不是报告图。
- **测试脚本会临时改写 ini，硬中断后要检查**：`Tools/Capture-Frame.ps1` 与
  `Tools/Measure-FrameRate.ps1` 的 `-Ini` 会就地改写 `Data/Config/Model3-Vulkan.ini`
  （首次会从 `Supermodel.ini` 播种），`try/finally` 负责关闭会话并还原。
  Ctrl-C 走 `finally`；
  `Stop-Process`、关终端、调试器强停不会，现场会留下 `<ini>.testbak` / `<ini>.testrun`。
  下一次运行任一测试脚本会自动还原；没有下一次时手动跑
  `powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Restore-TestIni.ps1`。
  典型症状是**设置界面变成马赛克或闪动**——那是 `VulkanDevice` 还停在测试用的显卡上，
  先看 ini 里 `VulkanDevice` 是否为空，再怀疑渲染代码。还原只在“文件仍是脚本写的那一版”
  时进行；之后被编辑过的文件会保留，测试前副本存成 `<ini>.testoriginal`。
  `-KeepRunning` 的会话仍持有测试配置；之后若在游戏内面板里保存设置，会把这些值写回 ini。
- **设置窗口右下角的「关于与许可」与 Ko-fi 徽章对**：`Gui.cpp` 的 `DrawAboutBadge()` 与
  `DrawKofiBadge()`，并排画在 **footer 的按钮行**右侧（与「启动游戏 … 退出」同高）；点击
  「关于与许可」弹出居中模态框 `AboutLegalModal`，且每帧渲染时更新 `s_footerLegalFrame`
  （与「启动游戏」按钮校验耦合）。按钮行放不下时整对徽章回退到状态行，并只在回退路径裁剪状态文字。
- **非宽屏画面两侧是蓝色不是 bug**：`WideBackground=1`（「宽屏背景」）会把游戏底部 2D 层
  横向拉满整个窗口，两侧显示的是游戏自己的背景（Daytona 2 为蓝色），与 `WideScreen` 独立。
  要黑边就关掉 `WideBackground`；游戏内「高级设置」已有琥珀色提示。A/B 见
  `sdl3_reports/2026-10-27-widebg-blue-bars/`。
- **便携 ROM 目录**：`Gui.cpp` 的 `FindPortableRomFolder()` 扫描 exe 旁的
  `rom`/`roms`/`romset`/`romsets`（大小写不敏感，优先含 `.zip` 的目录）；`ResolveRomPath()`
  先试 exe 相对路径，再走平台历史规则。存储为相对路径（如 `roms/`），移动整个文件夹后
  仍然有效。Linux 的 `FileSystemPath::GetBasePath()` 用 `/proc/self/exe`；不要把它改回空
  字符串，否则 HostCore 会从当前工作目录找 `.so`。Windows 仍保留 `C:/Downloads` 作为无
  便携目录时的默认。`build.bat` 会保留 `dist\rom*` 文件夹，发布包用 `/XF "*.zip"` 排除 ROM。
- **HostCore 的平台文件名**：Windows `Model3VulkanHostCore.dll`、Linux
  `libModel3VulkanHostCore.so`、macOS `libModel3VulkanHostCore.dylib`。
  `HostCoreBridge.cpp` 用 `ModuleFileName()` 统一；CMake 的 `hostcore` 目标在目标系统
  `dotnet publish` 并复制到 `build-cmake/bin`。Native AOT 不能从 Windows 交叉编译到
  Linux/macOS，别在 Windows 上生成 `.so`/`.dylib` 然后指望能用。
- **A/B 对比的规矩**：
  - **先问用户要存档**。从启动画面用 `-At` 猜时间抓不到具体场景——游戏要先跑完启动和
    几十秒的菜单/吸引循环，不同赛道还会轮换。没有存档就别开工。
  - 用 `-Frame <n>`（按仿真帧，不是墙钟）加 `-Ini "RefreshRateMode=0"`。前者保证两次运行
    抓到同一个仿真帧，后者把插值 alpha 固定成 1.0——实测同模式重复**逐像素相同**
    （mean 0 / max 0）；只加 `-Frame` 仍有约 1.8/255 的插值抖动，`-At` 则差 4–5 帧。
  - 帧号从会话开始算，**包含延迟读档等的 172 帧**（默认 3 秒）。要拍街机原生分辨率就加
    `-Ini "XResolution=496;YResolution=384;IntegerScale=1"`，此时窗口就是 496×384，棋盘格 1:1。
  - `SM3D_SCREENSHOT_FRAME` 只在 Vulkan 构建里计数；LegacyOG 会打一行警告然后不拍。
- **同步验证/GPU 辅助验证要显式请求**：`SM3D_VK_SYNC_VALIDATION=1` /
  `SM3D_VK_GPU_VALIDATION=1`（会自动打开 `VkValidation`）。层来自 Vulkan SDK，
  没装时回退到普通校验并记日志；只解出 DLL/JSON 时用 `VK_LAYER_PATH` 指到 `Bin`。
  两者都极慢，只用于排查。`DebugCallback` 有 1000 条上限。
  推荐直接用 `Tools/Validate-Vulkan.ps1 -Set <game> -Slot <n> -Mode sync|gpu|both`，
  它会找层、跑一帧并汇总 VUID/hazard 数量。
- **swapchain 图像的首次布局转换必须带 `srcStageOverride`**：提交在
  `COLOR_ATTACHMENT_OUTPUT` 等待 acquire 信号量，而从 `PRESENT_SRC/UNDEFINED`
  转换的 `srcStageMask` 默认是 `TOP_OF_PIPE`，会构成
  `WRITE_AFTER_READ`（同步验证每帧报）。`CmdTransition(..., 0, 1,
  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)` 建立执行依赖。
- **AMD 的 Windows Vulkan 驱动只报 128 字节 push constants**（NVIDIA / Intel / Mesa RADV 都是 256）：
  3D 场景的 `R3DPush` 曾经是 208 字节（投影矩阵、光照、雾色都塞在里面，其实都是逐视口量），
  `PickDevice()` 又硬性要求 `>= 256`，于是**所有 Windows + AMD 显卡**（Steam Deck Win11、Radeon RX、
  Ryzen APU、ROG Ally / Legion Go）在启动图之后静默退出，日志只有一句
  `no device ... 256-byte push constants`。逐视口的量必须放 `ViewportUBO`（`DrawBucket()` 每个 node
  本来就写一次），push 块守 **96 字节**、设备门槛守规范保底的 **128**。搬字段时注意：① `projection`
  在 `FillPush()` 里做过 4 次取负（Y 轴修正），取负必须跟着走到 UBO 填充处；② `real3d.frag` 的
  `gl_FragDepth` 也从 `pc.projection` 取值，只改顶点着色器会编译通过、画面也正常，但**深度全错** ——
  改之前先 `grep -rn "pc.projection"`。`ProbeVulkan13()` 不查这个属性，所以失败会晚 400 ms 出现在
  设置窗口创建设备时，看起来像崩溃而不是硬件要求。案例见
  `sdl3_reports/2026-10-31-amd-win-pushconst/`。
- **显存有预算日志**：`VK_EXT_memory_budget` 可用时，启动和分配失败会打
  `Vulkan memory (...)`，含 heap budget / 使用量 / 本会话持有量。分配器是自研
  子分配 + 持久映射环，不是 VMA；没有泄漏/Staging 卡顿/对齐问题时不要为换而换，
  详见 `vulkan_issues.md` 第 6 节。
- **存档按 ROM set 名隔离**：游戏内保存写 `Data/Saves/<set>.zst<slot>`（旧版
  `.st<slot>` 并存，游戏内菜单优先 `.zst`）。设置界面的「管理存档」按当前
  选中 set 的前缀过滤；空窗口会提示其他 set 的存档数量，日志也有
  `Manage saves: N card(s) ...`。不要跨 set 自动加载：不同 set 的状态布局
  可能不同。
- **管理存档界面只在 `SUPERMODEL_PURE_VULKAN` 构建里编译**（`Gui.cpp` 的保存卡一段）。
  Makefile.UNIX 是双渲染器 SDL2 构建，没有这部分（只有 F5/F7）；要卡片管理请用 CMake 的
  SDL3 纯 Vulkan 目标。游戏内存档浮层同理。两个平台都用 `FileSystemPath` 取目录，不需要
  另写路径逻辑。
- **两个 exe 的存档目录已分开**：Vulkan 版（`.zst` + `.st` 回退）在 `Data/Saves/`；
  LegacyOG 的 `.st` 在 `Data/Saves_Legacy/`。`FileSystemPath::SavePathType()` 在
  `SUPERMODEL_OPENGL_ONLY` 下返回 `SavesLegacy`，所有 F5/F7、游戏内面板和设置界面
  都走它；Legacy 仍会回退读取 `Data/Saves/*.st`（旧存档），但新存档只写新目录。
  `build.bat` 两个目录都保留；「管理存档」的卡片列表仍只扫 `Data/Saves`，但
  「导入旧版 .st」会同时扫描 `Data/Saves` 和 `Data/Saves_Legacy`，导入的 `.zst` 写到 `Data/Saves`。
- **删除存档要同时删 `.zst<slot>` 与 `.st<slot>`**：`SaveSlotOccupied()` 查两种
  文件，只删容器的话游戏内菜单仍会认为槽位被占用，且「导入旧版 .st」会把卡片
  带回来。删除只留空洞、不重排；`NextFreeSaveSlot()` 会第一个复用它。
- **F6 与 F7 的槽位语义不同**：F6 用 `NextFreeSaveSlot()` 选「下一个空槽」（给 F5
  新建存档），F7 在当前槽为空时用 `NewestSaveSlot()` 回退到编号最大的已有存档。
  不要把 F6 改成只读现有槽（F5 会覆盖旧档），也不要让 F7 沿用
  `NextFreeSaveSlot()` 的槽：新会话的初始槽就是第一个空槽，F7 会一直读
  `st4/st5/...`，已有的 1–3 反而读不到。
- **Vulkan 初始读档要先跑一帧再 LoadState**：Manage Saves → Load 与 `-load-state`
  在 `Model3->Reset()` 之后、第一帧之前读档，此时 `gpusReady` 还是 false、只读快照
  还没发布；Vulkan 构建必须先 `RunSimulationFrame()` 再 `LoadState()`。
  `LoadState()` 还要清 `ppcBrdThreadDone` / `sndBrdThreadDone` / `drvBrdThreadDone`，
  否则 F7 在仿真帧中途按下时，下一次 `EndSimulationFrame()` 会跳过等待。
- **vf3tb 的 K.O./Round 1 存档看起来像“3D 定格”**：人物不动、2D HUD/背景微动是
  过渡画面本身。日志 `Vulkan: first prepare after state reset` 可区分：Round 1 是
  1 node/0 draw，K.O. 是 10 nodes/425 draws。不要当 Vulkan 渲染 bug；读档要在
  比赛中存，别在 K.O./Round 1/载入画面存。
- **set 是按 zip 内容（CRC）识别的，不是按 zip 文件名**：
  `GameLoader::ChooseGameInZipArchive()` 用中央目录里的 CRC 比对，而且
  `incomplete_child_games` 的优先级高于完整的父 set。所以一个叫 `scud.zip`
  的包如果装的是 `epr-19731..19734`，运行的其实是 `scudau`，存档与 NVRAM
  也都叫 `scudau.*`。判断"这个包实际是什么"用 `GameLoader::Identify()`
  （只读中央目录，不加载 ROM，quiet 模式不刷错误日志）。
  - 列表：`ScanRomFolder()` 对每个 zip 调 `Identify()`，显示用的
    `title/version/year/supported` 和封面/简介都取 `RomEntry::resolvedSet`；
    `setName` 仍是包名，是列表行、右键菜单、`RomIndexForSet()` 的标识。
    改这块时不要把两者合并：合并后「读取」按钮会找不到行，同名多包也会互相覆盖。
  - 存档：`OpenManageSaves()` 用列表行（`g_manageSavesRow`）反查 zip 路径再
    `Identify()`，得到的 set 记在 `g_manageSavesGame`，两者不要混用。
  - 列表按文件名 vs 内容的差异很常见（用户目录 58 个包里有 15 个），日志会打
    `GUI: '<file>' holds set '<set>' - <title>`。游戏窗口标题里的地区也是为此加的。
- **打开游戏内面板自动暂停**：`overlayPausedEmulator` 区分“面板引起的暂停”
  和 Alt+P 手动暂停；关闭面板只恢复前者。面板内保存/读档依赖 `paused` 为
  true，不要再给 `if (!paused)` 加例外，否则会在面板仍打开时恢复线程。
- **游戏内面板的输入原来只在 Windows 上接上的**（0.9.8.6x 已修，但同类错误容易重现）：
  `Src/OSD/SDL/Overlay.cpp` 的 `FeedInput()` 里，指针位置、鼠标键、修饰键和整张按键表全在
  `#ifdef _WIN32` 里，没有 `#else`。非 Windows 每帧只送了滚轮，于是 Tab 能打开面板、但悬停、
  点击、方向键全无反应。现在非 Windows 从 `OSD::Platform::Input()` 的快照喂 ImGui（USB HID
  usage id 按键表 + 鼠标位掩码，注意平台层是 left/middle/right、ImGui 是 left/right/middle，
  要转序），`OverlaySetVisible()` 的“上次鼠标位置”重置也分平台。**新增依赖输入状态的
  共享 UI 代码时，一定要先问“非 Windows 会怎样”**；这类缺失不会报错，只会让界面看着像坏的。
- **音频静音标志是进程级的，会话结束必须复位**：`Src/OSD/SDL/Audio.cpp` 的
  `enabled` 是文件级静态量，面板自动暂停和 Alt+P 都会设成 `false`。若在暂停
  状态下退出（在面板里保存完直接按 Esc 就是典型），配对的
  `SetAudioEnabled(true)` 永远不会执行，而设置界面是在**同一个进程**里开下一
  局——于是新游戏全程无声，日志里却没有任何音频错误（缓冲区照常在写，只是
  `MixOut()` 走了静音分支），under-run 也不会暴涨。`OpenAudio()` /
  `CloseAudio()` 现在都强制复位；`SetAudioEnabled()` 只在状态翻转时打
  `Audio: output muted/unmuted.`。排查“没声音”先看这两行，再用统计面板的 `L/R` 峰值
  （或 `SM3D_AUDIO_LEVEL=1` 的 `source peak` / `mix peak`）区分“游戏本身没发声”和
  “机器有声但输出被静音”。
- **音频峰值读一次就清零**：`GetAudioStatus()` 用 `exchange(0)` 交付峰值窗口（最近一次
  采样以来的最大值），目前唯一的调用者是 `StatsOsd` 的 500 ms 采样器。新增第二个读取者
  会让两边各拿半个窗口——需要在别处使用时应另加累积器，不要改这里的语义。峰值取在
  balance/volume 与 mute **之前**，所以它回答的是“机器是否在发声”，不是“扬声器是否有声”；
  也不要拿它做削波报警（`MixChannels()` 会把前/后声道取平均）。
- **SCSP 音效被清空的经典症状：有音乐、没音效**。`CSoundBoard::RunFrame()` 里
  `memset(audioFL/FR/RL/RR, ...)` 只能待在 `if (m_config["EmulateSound"])` 的 **`else`**
  分支。它曾经挂在 `if (soundDebug)` 的 `else` 上（`soundDebug` 由 `SM3D_LOAD_DEBUG` 决定，
  正常游戏恒为 false），于是每帧 `SCSP_Update()` 填完缓冲立刻被清零，只剩随后混入的 DSB
  音乐——所有游戏、所有存档、全新开机都一样，与存档/四声道折叠无关。
  排查顺序：`SM3D_AUDIO_LEVEL=1` 看 `Audio: source peak X (front X, rear X)`，把
  `MusicVolume` 设为 0 后前声道峰值仍应明显大于 0（否则就是 SCSP 没输出）；再看
  `SM3D_LOAD_DEBUG=1` 的 `LoadDebug sound scsp done peak=`（SCSP 直后峰值）与
  `LoadDebug sound output done peak=`（混音前峰值），两者其一为 0 就能定位是哪一段清掉/没写。
- **`NbSoundChannels=4` 会按输出设备的真实声道数自动折叠**：Model 3 是四声道机台
  （`QuadFrontRear`：DSB 音乐 + SCSP0 走前声道，SCSP1 走后声道），但桌面端点常常
  只有 2 声道。`OpenAudio()` 现在先 `SDL_GetAudioDeviceFormat()`，设备不够 4 声道时
  把 `nbHostAudioChannels` 降为 2/1，由 `MixChannels()` 自己把后声道平均进前声道，
  日志打 `Audio: device '...' has 2 channel(s) ... folding the quad mix down to 2.`。
  **不要再让 SDL 去做 4→2**：它的 `SDL_ConvertQuadToStereo` 用 0.421/0.359/0.22 的
  权重，比我们的平均小一截，而统计面板仍显示 “4 ch”。症状是 Scud Race “选车几乎
  没声音、进赛道只剩音乐”——选车音效和后声道 SFX 都在 SCSP1。真 4 声道设备不受
  影响；5.1/7.1 端点由 SDL 把 quad 铺到 BL/BR。
- **吸引模式音乐与比赛内 BGM 走不同总线**（Scud Race 实测）：吸引音乐由 **SCSP1（后声道）**
  产生，受 `SoundVolume` 缩放；比赛内 BGM 由 **DSB（前声道）** 产生，受 `MusicVolume` 缩放。
  所以 `SoundVolume=65` 的逐游戏预设会把吸引音乐压低 `20·log10(0.65) = -3.74 dB`，而
  `MusicVolume=133` 对它完全无效；想恢复上游响度要在 ini 里给 `[scuddx]` 明确写
  `SoundVolume=100`（全局段写 100 不算显式，见 `HasExplicitAudioVolume()`）。
  另外 `RecordWav=1` 会强制 6 声道流，2 声道设备上由 SDL 的 `SDL_Convert51ToStereo` 折叠，
  后声道内容比 quad→stereo 再低约 1.6 dB（系数在 `SDL_audio_channel_converters.h`）。
  不要把 `MusicVolume` 当成“全局音乐音量”去调吸引模式。A/B 用 `Tools/Capture-Audio.ps1`
  抓设备前 PCM（sdl2/sdl3 通用），详见 `sdl3_reports/2026-11-07-upstream-125/`。
- **`MpegDec` 的窗口不能是负数或零长度，但 `dec.pos` 在前奏段合法为负**：`SetMemory/UpdateMemory` 拒绝
  `data == nullptr || offset < 0 || length <= 0`（`Stop()` 或只关 loop），
  `DecodeAudio()` 在 `size <= 0`、`frame_bytes <= 0`、位置不前进、连续 8 个零样本帧时
  `EndWithSilence()`。**注意 `UpdateMemory()` 把 `dec.buffer` 移到 `loopStart` 时会把 `dec.pos`
  减成负数 `-(loopStart - start)`**（使 `dec.buffer + dec.pos` 仍指向曲头 `start` 以播放前奏，如 Daytona 2
  Attract Mode 的 9.9 秒前奏），因此下界检查必须是 `dec.pos < dec.minPos`（`minPos = -offset`），
  **绝不能写成 `dec.pos < 0` 归零**，否则所有带循环点的 BGM 都会把前奏切掉。
- **DSB1/DSB2 读档不能相信 `usingMPEG*`**：Scud Race 在音乐已经播放时会把
  `usingMPEGStart/End/usingLoopEnd` 留成 `0x80008000`（板子的“无曲目”哨兵），
  此时 `MpegDec` 仍在正常解码（存档里的 `playOffset` 是真实位置）。读档先用
  `DSB1 MPEG` / `DSB2 MPEG` 伴生块（magic `MPG2`，`SaveMpegActiveWindow()` 写在
  DSB 块之后）恢复 `activeOffset/activeSize/pos/loop`；块不存在或非法才回退旧字段，
  并且只接受 `0 <= start < end <= 0x1000000`、`loopStart + loopEnd <= 0x1000000`
  （**本分支 `usingLoopEnd` 是长度，不是终点**）。旧存档没有伴生块，选车 BGM 读档后
  不会自己恢复，等游戏下一次发曲目命令（进赛道）才有音乐；用新版本重新存一次即可。
- **DSB 保存时的 `isPlaying` 按“旧字段是否可用”过滤**：老读档器只看 `isPlaying` 就
  调 `SetMemory(usingMPEGStart, usingMPEGEnd - usingMPEGStart)`，哨兵值会让它再次
  卡死。`SaveState()` 写成 `IsLoaded() && 旧范围合法`，真实窗口交给伴生块。
- **`CSoundBoard::LoadState()` 必须调 `SCSP_RefreshIRQ()`**：`SCSP_LoadState()` 只恢复
  `pend`/`en`，不会重新驱动 68K 中断线；漏掉后声板收不到效果命令，表现是“有音乐、
  没有音效”，而 `sndDone = 1` 照样正常。
- **读档诊断钩子**：`SM3D_LOAD_DEBUG=1` 打开 `LoadDebug ...` 与每秒 watchdog（含
  `mpeg=` / `mpegpos=`）；`SM3D_SAVE_STATE_FRAME=<n>` + `SM3D_SAVE_STATE_SLOT=<n>`
  按仿真帧存盘（Vulkan 构建），用于“读档 → 跑 N 帧 → 存新档 → 再读”的回归。
  判断“MPEG 到底在不在放”看 `mpeg=` / `mpegpos=`，不要只看 `sndDone`。
- **拖动窗口会进入 Windows 模态消息循环**，主循环整段时间停摆，而声卡线程仍在
  收数据，`underRunLoop` 会让环形缓冲重播旧样本（听感就是一小段循环）。SDL3
  目标由 `Main.cpp` 的 `ModalLoopPauseHook` 在 `WM_ENTERSIZEMOVE` 时
  `PauseThreads()` + 静音，`WM_EXITSIZEMOVE` 时 `FlushAudioBuffer()` 后恢复；
  拖动前已暂停（Alt+P/面板）时不接管。注意 SDL3 的 `SDL_WindowsMessageHook`
  返回 `true` 才是“继续处理”，返回 `false` 会吞掉消息（连 SDL 自己的
  live-resize 定时器都起不来）。SDL2 目标走的是另一套
  `ModalLoopMessageHook`（`WM_TIMER` 里 `RunFrame()`，不暂停），两者不要混。
  `FlushAudioBuffer()` 必须在 `ResumeThreads()` 之前、`SetAudioEnabled(true)` 之前
  调用：线程恢复后马上就会写新样本，先 flush 才不会把新数据一起清掉。
- UI 文案一律写中文再经 `Tr()`；`Localization.cpp` 缺条目时英文界面会直接
  输出中文。新增文案要同步加英文条目，删文案时顺手删掉失效条目。
- 界面 (?) 提示只写「是什么 / 选哪个」：单值控件一两句；选项多的下拉框/单选项逐项
  各一句。原理、数字阈值和推荐组合写进 `usermanual.md` / `usermanual_chs.md`，
  不要再在 tooltip 里展开（窗口模式下会被裁切）。
- 可点击控件要请求手形光标：用 `UiButton` / `UiSmallButton` / `UiCheckbox` /
  `UiRadioButton` / `UiTabItem` / `ComboRow`，或直接调 ImGui 后在下一行调 `HandOver()`。
  它只在 `IsItemHovered()` 为真时设置，所以禁用项自动保持箭头。漏掉的话控件能用，
  但指针一直是箭头，玩家会以为不可点。
- SCSP DSP 只有一条路径（`Src/Sound/SCSPDSP.cpp`）。`0x7C0–0x7FF` 是保留空洞，
  `SCSP_w8/w16/w32` 都必须丢弃，不能镜像进 `MADRS`：镜像会把 FV2 刚写好的偏移表
  清零，导致效果声道自激。`SCSP_w32` 里必须有一个**显式**的 `else if (addr < 0x800)`
  空分支——否则 `(addr - 0x800) / 2` 会对 `0x7C0` 无符号下溢成 ~4 GB 偏移，
  直接访问违例。`LegacySoundDSP` / `-new-scsp` / `-legacy-scsp` 仅为兼容而解析，
  不再有任何分支读取它们；效果声道音量索引是 `EFSDL << 13`。**详细分析见 `devdocs/DSP.md`。**
- DSB2 的 0xA0 / 0xB1 是 Sega Rally 2 的双 BGM 声道选择：0xA0 非 0 → `mpegR`
  喂双声道，0xB1 非 0 → `mpegL`，参数 0 → 立体声。**`0x24`（结束/循环地址）
  写入绝不能重置这个路由**，否则 SR2 两首曲子会同时播放。`StereoMode` 的枚举值
  0/1/2 写进存档，不要改。
- DSB 的 32k→44.1k 是 64-tap 多相 sinc（320:441），每帧要求
  `MpegSamplesForFrame()` 多预读 `DSB_RESAMPLER_TAPS + 4` 个样本作为滤波器右侧
  支撑。改小这个 padding 会让每帧末尾和零卷积，产生 57.5 Hz 的边界杂音；
  帧长权威始终是 `GetSamplesThisFrame()`，不要改成按时间累积的独立时钟。
  每游戏默认音量表在 `Main.cpp` 的 `ApplyPerGameAudioBalance()`，逐键检查 ini
  全局段 / `[game]` 段 / 命令行，不要直接写进 `DefaultConfig()`（那样就分不清
  默认值和用户值了）。
- SDL3 版不再依赖 SDL3_net：`TCPSend` / `TCPReceive` 走 `NativeSocket`
  （WinSock2 / BSD socket），`CMakeLists.txt` 里 Windows 链接 `ws2_32`，
  `SUPERMODEL_NO_NETWORK` 已删除。协议仍是“主机序 int 长度 + 负载”，SDL2 与
  SDL3 实例可互连。单机自连（`PortIn == PortOut` + `AddressOut = 127.0.0.1`）
  仍必须走 `CSimNetBoard` 的本地回环分支，不经过 socket。Windows 上 accepted
  socket 会继承监听 socket 的非阻塞状态，`TCPReceive` 已显式改回阻塞，否则分包会被
  误判为断线。
- **模拟网络板（`CSimNetBoard`）的状态必须入档**：`CSimNetBoard::SaveState()/LoadState()` 保存/恢复
  `m_state / m_running / m_numMachines / m_counter / m_segmentSize / m_IRQ2ack / m_status0 / m_status1 /
  m_commbank` + `RAM(0x10000)` + `Buffer(0x20000)`，`CModel3::SaveState/LoadState` 分别调用
  `NetBoard->SaveState/LoadState`（`CNetBoard` 是空实现，兼容）。旧存档没有 `SimNetBoard` 块时走
  `SynthesizeReadyState()` 合成单机自连 ready 态（GameType::one：`m_status1=0x2021`、`m_IRQ2ack=0x01`、
  `RAM16[0x400/0x402/0x404]=0/0x3ef0/848`，双 bank 头同时填好）。**注意**：3 秒延迟读档时机器仍在开机
  `NETWORK CHECKING` 诊断阶段，`RAM16[0x404]` 会被诊断程序写成 `256`（`0x100`）；回退时必须把 `0x100` 也替换成
  正式游戏的 `848`（`0x350`），否则回环拷贝落在 `0x200` 而非 `0x450`，投币进选车会卡在「しばらくお待ちください」
  且一直播 Attract 音乐（不切选车 BGM `AE 10 0A`）。**不要删掉这个回退与 256→848 修正分支**。
- **SDL3 力反馈**：`ProcessForceFeedbackCmd()` 的 SDL3 分支走
  `OSD::Platform::OpenHaptics/SetHapticEffect`（SDL_Haptic）。恒力的
  平滑是效果自带的 20 ms attack 包络，不是独立线程；改这个值会影响直驱方向盘的
  阶梯感。**Windows 下 `SDL_INIT_HAPTIC` 必须在 `SDL_INIT_JOYSTICK` / `SDL_INIT_GAMEPAD` 之前单独初始化**
  （`PlatformSDL3.cpp::Initialize` 已保障）：SDL3 内部若先初始化 Joystick，枚举时 `dinput` haptic 尚未就绪，
  随后 `SDL_DINPUT_HapticInit()` 又因 `SDL_WasInit(SDL_INIT_JOYSTICK)` 为真而跳过 `DI8DEVCLASS_GAMECTRL`，
  会让所有 DirectInput 方向盘的 `SDL_hapticlist` 为空。另外 `OpenJoystickAt()` 必须看 `OpenHaptics() || info.hasHaptic || info.hasRumble`
  （不能要求 `info.isGamepad`：直驱方向盘是 `raw joystick`），恒力在 Cartesian 单轴下 `dir[0] = 0`、正负号放在 `constant.level`，
  路面震动 `FFVibrate` 优先走 `HapticEffect::Vibration`（`SDL_HAPTIC_SINE`）。
- **SDL3 多鼠标**：每设备表按 `SDL_MouseID` 索引。Windows 普通窗口下 SDL3 只发
  全局鼠标 ID，所以平台层自己注册了 RawInput（message-only 窗口 + 线程）；
  相对模式期间 SDL 会接管注册，`EnsurePerDeviceMouseInput()` 负责交还后重新注册。
  `GetNumMice()` 在开局时缓存，中途插入的鼠标要重开一局。单鼠标仍走系统光标。
- **开关输入不能绑整轴**：`Trigger` / `Offscreen` / `AnalogTriggerLeft/Right` /
  `AnalogJoyTrigger` 都是 `AddSwitchInput`，只接受 `JOY1_RZAXIS_POS` 这种半轴；
  写成 `JOY1_RZAXIS` 会被解析成 `SourceFullAxis` 并在 `ParseMultiSource` 里丢弃。
  有键盘/鼠标备用源时静默失效，只有唯一源时才会报
  `Unable to map input ... - switching to default [NONE]`。改控制预设时看这行日志。
  旧 ini 的整轴写法由 `RepairSwitchAxisMappings()` 在加载时修正（`main()` 和
  `RunGameSession()` 各自读一次文件，两处都要调）。
- **手柄调试先看开场日志**：SDL3 每次开局会打印
  `Input: N game controller(s) reported by SDL.` 和每个设备的
  `Input: joystick N '<name>' - X axes, Y buttons, Z hat(s), game controller, rumble yes.`。
  `SM3D_PAD_PROBE=1` 会额外在每个手柄打开时震一下，并打印按键/hat/轴变化
  （raw 编号 + 标准键名），配合 `Scripts/PadProbe.ps1` 做手动测试。
  注意：无线手柄闲置会休眠，SDL 看不到它；Xbox 360 有线类手柄在 SDL 的 XInput
  后端下统一叫 `XInput Controller #N`（拿不到产品名），`DetectController()` 按
  Xbox 360 归类。
- **SDL3 版没有 DirectInput / XInput / Raw Input 后端**：`GetInputSystem()` 在
  `!SUPERMODEL_PLATFORM_SDL2` 时只建 `CSDLInputSystem`，控制器页也只有 `sdl` /
  `sdlgamepad`。`DefaultConfig()` 的 `SUPERMODEL_WIN32` 分支过去无条件写
  `InputSystem = xinput`，导致 SDL3 新装每局打一行回退日志、而下拉框因为没有匹配项显示
  “SDL”。现在 SDL3 默认 `sdlgamepad`，旧值由 `AlignLegacyInputSystem()` 在内存里改写
  （设置界面下次保存会落盘）。新增输入后端时不要只改 `#ifdef SUPERMODEL_WIN32`，
  还要看 `SUPERMODEL_PLATFORM_SDL2`：桌面 SDL3 和 LegacyOG 共用同一份 ini。
  `DetectController()` 的 `inputSystem` 推荐值也必须按构建分开，否则控制器页会写
  「suggested input system: xinput」而下拉框里没有这一项（SDL3 只推荐 `sdlgamepad`/`sdl`）。
- **PowerPC 混合 JIT（`Src/CPU/PowerPC/ppc_jit_x64.inl`，键 `PowerPCMode`）**：
  - **只有 Vulkan 版有**（`ppc.h` 的 `PPC_JIT_X64` 要求 `!SUPERMODEL_OPENGL_ONLY`，
    另外排除 `SUPERMODEL_DEBUGGER`、`PPC_VERIFY_FAST_MEMORY`）。LegacyOG 里
    `ppc_set_jit_enabled()` / `ppc_log_jit_stats()` 是空壳，不要给它们加实现。
  - **块出口/总线调用后重载 `&s_pins` 不能用 `rax`**：`rax` 就是“刚读到的值 /
    helper 的返回值”。第一版用 `rax`，结果每次经过总线的读取都返回 `&s_pins` 的低 32 位；
    症状是 Daytona 2 的引导 ROM 读 PCI 配置拿到垃圾、停在 `FFF001AC` 的 `b .` 自旋、
    只有 13 个块且 `0 helper call(s)`。**锁步校验器抓不到**：带设备访问的块因为
    `busTouched` 被跳过比对。现在用 `r10`。
  - `ppc_set_fast_memory()` 是窗口的唯一来源，由 `CModel3::PublishFastMemory()` 在
    内存池就绪、`LoadGame` 设好 fetch 区域、**每次 `SetCROMBank()`** 时调用，析构时传 NULL。
    少了 CROM bank 那次，换 bank 后的读会继续用旧窗口。
  - **`oe` 溢出形式要在内层 `switch` 外面回退**：`if (oe) break;` 写在 `case 31` 的
    case 块里时，`break` 属于内层 switch，会继续执行后面的 `StGpr`。写法是
    `if (!oe) { …native…; return; } break;`。
  - 自修改代码只追踪**生成代码自己发出的存储**；`stmw`/`stwcx.`/`stswx`/`dcbz` 等 C
    处理函数内部的写、以及跨递减器触发点时解释器代跑的几条，都不能标记页。Model 3 上
    引导加载器会在任何代码被翻译之前把程序拷进 RAM，所以实测五个游戏无问题；真要做
    运行期改代码支持，得把页追踪接到总线写路径上。
  - 调块缓存前先看 `ppc_execute()` 的分派：`ppc_jit_active()` 为假（`SM3D_PPC_JIT=0`
    或分配失败）时必须回退到 `ppc_interpret_slice()`，否则整局不推进；JIT 内部遇到
    无法翻译的 PC 也要调它而不是直接 `return`，否则那一帧的周期就丢了。
  - 验证入口：`SM3D_PPC_JIT_VERIFY=1`（逐块双跑比对，不一致 `abort`）、
    `SM3D_PPC_JIT_DUMP=1`（会话结束时把活动块反汇编写进日志）、每局结束的
    `PPC: x64 hybrid JIT: ...` 统计行。完整记录见
    `sdl3_reports/2026-10-30-ppc-x64-jit/README.md`。

- **Sinden 白边**：`SindenBorder*` 三键，`CVulkan2D::DrawBorderPass()` 在 bezel
  之后、overlay 之前绘制，坐标系是 swapchain；不要挪到 post 之前，否则会被
  CRT/机柜背景影响。改 `border.vert/.frag` 后要重跑 `CompileVulkanShaders.cmd`。

- **毛玻璃（`Graphics/Vulkan3D/UiBlur.*` + `ui_blur_down/up`、`ui_frost`、`ui_ambient`）**：
  - 链上图像**必须用 `m_context.SwapFormat()`**。带线性滤波的缩放 `vkCmdBlitImage` 只允许
    在 format-compatible 之间，BGRA8 与 RGBA8 不兼容；换成 RGBA8 会在第一次 blit 就报 VUID，
    而不是“颜色有点不对”。
  - 上升采样（`ui_blur_up.frag`）的混合是**权重 0.5 的 `SRC_ALPHA`**，不是 bloom 那套
    `ONE/ONE`。相加会把平坦区变成 1.5 倍亮，而这块图是给 tint 垫底的，不是光晕。
  - 上升采样那遍必须 `loadOp = LOAD`：它就是“把粗层叠在细层上”，清掉目标等于和黑色混合。
  - `Ensure()` 重建时旧图**延迟 4 帧销毁**（`RetiredSet`）。ImGui 已经注册的描述符还指着
    旧 view，resize 那一帧的绘制命令也已经烘焙好了 —— 立刻销毁就是采样已释放内存。
    判断“图变了”要看 `CUiBlur::Generation()`，不能比 `View()`：分配器会复用地址。
  - 游戏内链路只在 `m_uiFrostGate()` 为真时录制（`OverlayWantsFrost()`），
    日志在状态翻转时打 `Vulkan2D: frosted backdrop active … / … idle`。
    设置窗口那边靠 `m_backdropKey` 缓存，只在换封面 / 改主题 / resize 时重录。
  - 着色器里**深浅两个主题要走相反的分支**：深色压高光，浅色向 `vec3(0.96,0.94,0.91)`
    提亮 55% 再回补饱和度。浅色沿用压暗会让面板发灰发脏。
  - `ui_frost.frag` / `ui_ambient.frag` 的 push block 与 `CUiBlur::FrostPush`、
    `UiVulkanBackend.cpp` 的 `AmbientPush` 必须逐字段对齐；改完要重跑两个编译脚本
    （`CompileVulkanShaders.cmd` **和** `.sh`，四个新单元两边都要有）。

- **毛玻璃画在 ImGui 里（`UiFrost::Plate()`，实现在 `Gui.cpp`）**：
  - uv 是**屏幕绝对坐标 / `io.DisplaySize`**，所以一张半分辨率纹理服务所有面板；
    面板位置变了不用重新模糊。
  - `Plate()` 内部自己 `PushClipRect(pMin, pMax, false)`。窗口内容区运行时的裁剪矩形已经
    收窄到 padding 内，不覆盖的话圆角和 1px 高光边会被切掉。
  - **纹理绘制不吃 `ImGuiStyleVar_Alpha`**：面板淡入时要把同一个权重单独传给 `Plate(alpha)`，
    否则文字淡入而玻璃瞬间全不透明。
  - 玻璃矩形画在**内容矩形**上，所以内容必须自己内缩（`panePad`）；子窗口要
    `AlwaysUseWindowPadding + NoBackground`，否则内容从圆角边开始，或子窗口自己的底色
    矩形让 padding 环看起来像“内容贴边”。存档表格还额外 `Indent`，因为斑马纹是一块
    可见面，槽位数字是拿它量间距的。
  - 主题 token 在 `MakePalette()`（`Gui.cpp`）里，两种玻璃主题都不含纯黑纯白。新增颜色**只加在这里**，
    不要在控件里写 `ImVec4`：`ImGuiCol_Text` 曾经在游戏列表里写死旧浅色 `inkDim`，深色主题下
    那行几乎看不见。警告色是 `AttentionColour()`，浅色 `#9E5C00`、深色 `#ECA34A`。
  - 统计面板的四套配色**不跟主题变**（它压在游戏画面上，自带底板和文字色）。
  - 配置键是 `UITheme`（**0 = 浅色暖色护眼，默认；1 = 深色；2 = 原始界面**）与 `UiFrostedGlass`。
    改主题立即生效（`GUI()` 里检测到变化后 `RescaleUi()`）；游戏内面板要等下一局，
    因为它有独立的 ImGui 上下文。
  - **`2 = 原始界面` 是重做前那一版的逐值复制**（`MakePalette()` 的 `Classic` 分支 + `ApplyUiTheme()`
    的 `classic` 分支）。新增玻璃相关代码时必须用 `UiThemeIsClassic()` / 已有的 `classicUi` 门控：
    这个主题不画 Plate、不画氛围底图、`panePad` 与 `footerPadding` 为 0、窗口自带不透明底色、
    `FrameBorderSize 0`。漏一处就会在“原始”外观里冒出玻璃或多余留白。
    游戏内面板在 `Overlay.cpp` 里单独门控：`OverlayWantsFrost()` 返回 false（不录模糊链）、
    不画 Plate、不把 `WindowBg` 透明、`WindowPadding` 不抬到 26/22、`OverlayBody` 保留底色、
    存档表不加 `Indent`。`GuiCreate()` / `RunGameSession()` 两处把 `UITheme` 数字映射成枚举，
    漏掉 2 就只能在设置窗口里看到原始主题、进游戏又变回玻璃。
  - 原始界面下「毛玻璃背景」开关**置灰**（本来就不透明）。置灰项的 tooltip 要
    `IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)`，否则悬停无提示、看起来像坏了。

- **启动图解码优先 WIC，失败时回退内置 libwebp**：WIC 只有在装了 Microsoft Store 的
  「Webp Image Extensions」时才认识 WebP；干净系统上旧代码会直接跳过启动图（日志
  `Splash: the artwork could not be decoded`）。现在 `DecodeArtwork()` 失败后用
  `WebPDecodeBGRA()` 再试，`SM3D_SPLASH_LIBWEBP=1` 可强制走这条路径做验证。
  封面在 `Gui.cpp` 有各自的 WIC→libwebp 回退，两处不要混淆。
- 改启动图（根目录 `Igloo-Garage Presents.webp`）后**不要手动改哈希**：
  `build.bat` / `build-cmake.bat` 编译前会自动跑
  `Scripts/UpdateSplashSignature.ps1`。只单独跑 MSBuild（增量调试）时要自己跑一次，
  否则 exe 会在启动图底部标红 `UNOFFICIAL BUILD`。

- **高清纹理包的四个硬约束**：
  - **bindless 数组不能在 set 0**：set 0 有 `UNIFORM_BUFFER_DYNAMIC` 和
    `STORAGE_BUFFER_DYNAMIC`，任何同布局的 binding 都不能带
    `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`（VUID 03001）。数组在 **set 2**，
    set 1 保留给逐像素排序；`CreateOitSetLayout()` 必须无条件执行，否则
    `AlphaSortMode=0` 时 set 号会塌缩。`u_hdTextures` 的 `layout(set = 2, binding = 0)`
    与 `HDTextures.cpp` 的 `WriteDescriptor()` 要一起改。
  - **键名是 `DumpHDTextures`，不是 `DumpTextures`**：后者是既有的“退出时导出原始
    16-bit 贴图页”（`Real3D.cpp`、`-dump-textures`），不能复用。
  - **哈希是 FNV-1a 64 位，但 offset basis 不是标准值**：`HDTextures.cpp` 用
    `0x14650fb0739d0383`（标准 FNV-1a-64 是 `0xcbf29ce484222325`），prime 是标准的
    `0x100000001b3`。输入顺序是 `format/width/height`（各 4 字节小端）+ 逐行逐列的
    16-bit texel（低字节在前，带页内 wrap 掩码）。文件名取最后一个 16 位十六进制 token；
    改算法会让所有已发布的替换包失配，**不要“修正”这个常量**。
  - **上传在 `CVulkanTextureCache::RecordSync()`**（`RenderFrame` 开头，不是 `DrawScene`
    里），用同一个 ring buffer 分块传 mip 0。`RecordUploads()` 里新解完的槽必须把
    `loading` 置 false，否则后面的上传循环会永远跳过它（表现为“哈希匹配、日志有
    first match，但画面全是原贴图”）。
  - 重载会 `vkDeviceWaitIdle` 并清空所有槽；游戏内面板打开时模拟器暂停，所以扫描要等
    关闭面板、恢复仿真后才会发生（面板上有提示）。
  - **替换图的地址模式只能是 REPEAT / MIRRORED_REPEAT，不能出现 CLAMP**。
    wrap 0/1 都映射 REPEAT、2/3 映射 MIRRORED_REPEAT，`GetTextureValue()` 里**不要**再
    用 `fract(uv)` 去“还原 repeat”。真机原始路径的 wrap 1 是“先 fract、再把双线性
tap 钳在子贴图边缘”，但对硬件过滤器来说，钳位会把滤波足迹里越过边界的 tap 全读成
    边缘纹素 —— 缝的宽度等于整个足迹，不是半个纹素。斜掠路面（Daytona 2 的
    `(0,768) 256x256` 沥青）上表现为每个平铺周期一条横带，而贴张纯色图就“好了”
    （两边同色），极易误判成超分/导出问题。详见 `devdocs/CHANGELOG.md` 的
    「HD 平铺边界」一节与 `sdl3_reports/2026-10-25-hd-wrap-seam/`。

- **导出贴图的 alpha 是“渲染器读到的值”，不是“存储里的位”**（`DumpTexture()` / `QueueDump()`）：
  T1RGB5 的 bit 15 与 4-bit 格式的 alpha nibble 是 **1-bit 掩码**，美术拿它当**抖动**表示半透明；
  而 `GetTextureValue()` 对「没有 `kTextureAlpha` 也没有 `kAlphaTest`」的材质会把 alpha 强成 1。
  所以 dump 必须带 `alphaUsed = (flags & (kMatAlphaTest | kMatTextureAlpha)) != 0`：不读就写 255，
  读就保留原机掩码。**同一个哈希被不同材质用到时不能靠遇到顺序定稿**——`m_dumpedOpaque`
  记着哪些是削平写的，后到的读 alpha 材质会触发**重导**（文件名不变）。把这里改回“原样输出
  所有位”，就会重现社区反馈的“导出图布满白点/虚线”（`sdl3_reports/2026-11-01-dump-alpha-dither/`）。
  排查工具：`Tools/inspect-texturedump.py`（alpha 直方图 + 抖动/掩码判别；`--sheet` 出画面与
  alpha 对照图，`--flatten` 给超分工具写不透明副本）。
- **HD 纹理包的 DDS（BCn）路径**（`DdsLoader.h` + `HDTextures.cpp`）：
  - **文件优先级是 `.dds.zst` > `.dds` > `.webp` > `.png`**，在 `Scan()` 建表时一次决定
    （`m_index` 存 `{path, kind}`），`Resolve()` 不做磁盘探测。`.dds.zst` 有两个扩展名，
    哈希要从“剥掉后缀的文件名”里取，用 `path::stem()` 会把 `.dds` 当成哈希的一部分。
  - **BCn 图像的 usage 只能有 `TRANSFER_DST | SAMPLED`**：`vkCmdBlitImage` 不能以压缩图为
    源或目标，`STORAGE` 也不行。mip 链必须来自文件（`mipLevels = ddsInfo.mipLevels`），
    上传完一次 barrier 直接到 `SHADER_READ_ONLY`，**不要**把它塞回 RGBA 的 blit 循环。
  - **Staging 的 `bufferOffset` 必须是块大小的倍数**：整条链一次 `memcpy` 是不行的 —— 4096×4096 BC7
    约 21.3 MB，而 ring 每帧切片只有 64 MB（`kRingSliceBytes`），而且一张纹理不能占满整个切片。
    `UploadCompressed()` 用 `(uploadMip, uploadBlockRow)` 游标分块，每块 ≤ `kCompressedChunkBytes`
    （8 MB）、每帧每张 ≤ `kCompressedChunksPerFrame`（4 块），单个 mip 超过预算时按 4×4 block row
    切（`imageOffset.y = blockRow * 4`）。环满就结束本帧、下一帧从游标继续，
    `CVulkanRingBuffer::Remaining()` 用来提前裁剪大小以免刷 `ring buffer exhausted`。
    不要去把整条链塞回一次 `ring.Alloc(payload.size(), 16, ...)`：单张超过 64 MB 时它会永远申请
    不到，纹理静默不出现。
  - 设备必须启用 `textureCompressionBC`（`CVulkanContext::CreateDevice()`，
    `TextureCompressionBCSupported()`）。不支持时扫描阶段就跳过 DDS 并告警，不能等到
    `vkCreateImage` 失败。单个格式还要过 `VkFormatProperties` 的
    `SAMPLED_IMAGE | TRANSFER_DST | SAMPLED_IMAGE_FILTER_LINEAR`（采样器是线性的）。
  - **`.dds.zst` 用 HostCore 的可选导出 `HostCore_DecompressZstd`**（两段式：NULL 取大小 →
    分配后解压）。C++ 侧走 `OSD/SDL3/HostCoreZstd.h`：Vulkan 核心是 C++17，不能包含用
    `std::expected` 的 `HostCoreBridge.h`。这个导出**不在** `Bridge::Load()` 的“完整 ABI”
    检查里，旧模块仍能加载，只是 `.dds.zst` 被跳过。
  - **不要给 DDS 加 Y 翻转**：PNG/WebP 路径把图像第 0 行直接上传到 `v=0`，DDS 路径把第 0 个
    块行放到同一位置。离线打包工具不需要 `-yflip`；实测同一张图 PNG 版与 BC1 版逐帧平均差
    0.08/255、朝向一致。

- **3D 场景目标交给 2D 合成器必须覆盖所有退出路径**：`CModel3::RenderFrame()` 里 `TileGen` 的
  2D 合成**先于** `GPU.RenderFrame()`，所以它采样的是**上一帧**的场景目标。
  `CVulkan3D::DrawScene()` 的任何提前 `return`（白闪是其中之一）都必须调 `handOverTargets()`，
  把颜色/透明目标（MSAA 下是 resolve 图）转到 `SHADER_READ_ONLY_OPTIMAL`；漏一条路径就是下一帧
  合成时的 `VUID-vkCmdDraw-imageLayout-00344`（描述符声明 SHADER_READ_ONLY，实际还是
  COLOR_ATTACHMENT），而且只错一帧，看起来非常“间歇”。同理，`EnsureTargets()` 新建目标后要立刻
  做一次 UNDEFINED → SHADER_READ_ONLY，因为首帧合成在第一次 DrawScene 之前。
  MSAA 下 trans 附件由子 pass 以 COLOR_ATTACHMENT 交回，下一帧动态 pass 再 clear 时不产生布局
  转换，需要 `begin()` 对 `loadOp == CLEAR` 的附件补一个**同布局**依赖 barrier，否则是跨提交
  `WRITE_AFTER_WRITE`（同步验证报 `Vulkan errors`，不是 VUID）。

- **Vulkan 管线缓存一分两半**：设置窗口和游戏会话各自的 `CVulkanContext` 同时
  存在，分别读写 `Data/Cache/pipelines-ui.bin` / `pipelines-game.bin`。不要合并
  成一个文件：后销毁的上下文会用自己较早的内存副本覆盖另一个的条目。新增管线
  创建调用时第二个参数要传 `context.PipelineCache()`，不要再写
  `VK_NULL_HANDLE`。`build.bat` 已把 `Data/Cache` 列入保留目录。
- **晚采样不排空事件队列**：`CInputs::PollGameInputsLate()` →
  `CInputSystem::RefreshState()` 只重新 pump + 重读快照，事件属于帧首的
  `Poll()`，抽两次会重复送滚轮刻度和热插拔通知。UI 输入也不重轮询
  （`IsUIInput()` 要跳过），否则 `prevValue` 被推平、边缘被吞。面板打开时
  （`m_gameInputSuppressed`）直接返回。`PollPointerLate()` 仍保留，它负责
  准星用的那次 OS 指针查询。
- **存档不是所有时刻都能恢复**：存档只在帧边界拍，但游戏逻辑可能跨帧等一个握手
  （载入/转场画面就是典型）。这种状态读回来可能停在等待循环里——PPC 看着在跑，
  实际在 `cmpw` + 条件回跳里转圈，主线程卡在 `EndSimulationFrame()` 的同步等待。
  用户侧规则：在赛道中或稳定菜单存档，不要在载入/转场/比分画面存。
- **读档延迟是兜底，不再是根因修复**（Vulkan）：`Manage Saves → Load` 和 `-load-state` 仍走
  `Main.cpp` 的延迟路径（`kDeferredLoadSeconds = 3.0` × 57.524 Hz = 172 个**仿真帧**），
  但 2026-11-07 已把真正的根因补进存档：`53C810 PCI` 的 `baseAddress`/`halt`、
  `SimNetBoard` 握手与双 bank、`IRQ.RefreshInterruptLine()`、`SCSP_RefreshIRQ()`、
  DSB 的 `MPEG` 活跃窗口块，以及读档时的 worker 完成标志。实测 `scuddx`、`vf3tb`、`daytona2`
  在 `SM3D_DEFER_LOAD_SECONDS=0` 下都能第一帧即时读档，不再需要等 3 秒。
  **不要把这个等待改成墙钟**：它按仿真帧计数，机器跑不满时等待的仿真时间才不变短。
  172 帧保留作兜底（未覆盖的游戏/在启动握手中存的档）；是否默认改成 0 要另跑全游戏矩阵。
  **v0.9.9.6 仍以 3 秒发布；改成 0 是下一版发版前的待办，见 `issues_waitlist.md` 2026-11-07 条。**
  F7 / 面板 Load 会立刻清掉待读档并撤掉过渡图。完整根因、块布局与 Android 移植清单见
  `saves_fix.md`。
- **整屏启动图是 `ShowStartupSplash(window, true)`**：`coverMonitor` 为真时铺满游戏窗口
  所在显示器的整个 `rcMonitor`（不是 62% 方形），用作品画盖住游戏启动过程。它是
  `WS_EX_TOPMOST` 的 layered window，所以游戏窗口可以在它下面正常显示首帧，不会因为
  “隐藏窗口拿不到 swapchain”而卡住。`HideStartupSplash()` 在三处调用：装完状态、F7/
  面板手动读档、`DestroyGLScreen()`。
- **中断输入线不属于存档**：`CIRQ::LoadState()` 恢复 `irqEnable`/`irqState` 后，
  必须由 `CModel3::LoadState()` 调 `IRQ.RefreshInterruptLine()`（在
  `ppc_load_state()` 之后，否则被 MSR/PC 覆盖）。新增任何“线上信号”类的硬件状态时，
  问一句：它有没有在 load 后重新驱动？
- **子 set 的父包是按文件名找的**：`GameLoader::Load()` 先试
  `<parent>.zip`，找不到才用 `FindArchiveForSet()` 扫目录按内容识别。所以
  `scudplus.zip` / `scudplusa.zip` 这类只带补丁 ROM 的包必须能在同一目录找到
  内容为 `scud` 的包；删掉父包前想清楚。扫描只在失败路径上跑。
- **ImGui 的 stb_truetype 不支持 CFF2，且失败路径是 abort**：`AddFontFromFileTTF()` 遇到可变字体
  （`-VF.otf` / `-VF.ttc`，表 `CFF2`）会在 `ImGui_ImplStbTrueType_FontSrcInit` 里
  `IM_ASSERT` 强退（Issue #5，openSUSE）。**任何新增字体候选都必须先过 `ProbeFontFile()`**
  （`Src/OSD/SDL/FontProbe.cpp`）：它用同一份 `imstb_truetype.h` 预解析，并且先做带边界检查的
  表目录校验——stb 自己不做边界检查，一个 4 KB 的垃圾文件就能让它段错误。Linux 候选列表不要
  再加 `-VF` 文件；Fontconfig 走 `fc-match -s` 排序列表逐个探测，不要退回单个 `fc-match`。
- **统计浮层的 CPU 频率是平均值**：`StatsOsd.cpp` 用「标称频率 × `% Processor Performance`」
  再对**所有非休眠性能核取平均**。一核满载、其余空闲时平均值只有一半，看起来像「主频上不去」，
  所以同时显示 `peak`；判断瓶颈只看 peak，不要只看平均。
- **非 NVIDIA 机器的 GPU 占用走 PDH**：`\GPU Engine(*)\Utilization Percentage`，实例名形如
  `pid_N_luid_..._eng_0_engtype_3d`，**引擎后缀是小写 `3d`**——用 `wcsstr(..., L"engtype_3D")`
  永远匹配不到，而失败形态是「GPU 行静默消失」。这条路径只有占用，没有频率/显存（Windows 没有
  厂商中立的替代）；`SM3D_STATS_GPU=pdh|nvml` 可强制来源，日志会写用的是哪个。

- **追尾镜头防抖（`ChaseCameraStabilizer`）的四个硬约束**：
  - **真机相机矩阵在视口根节点（`vpnode[0x02] & 0x00FFFFFF`），不在 `MatrixSlot[0]`**：Scud Race 等游戏的
    `MatrixSlot[0]` 恒为单位阵。`CR3DSceneBuilder` 必须用 `addr == m_vpRootAddr` 精确捕获根节点矩阵到
    `vp.cameraMatrix`（不能只看 `m_cullingDepth == 1` 或 `models.empty()`，因为 `DescendCullingNode` 在压栈前
    先递归 `sibling2Ptr`，迈凯伦车尾回火等挂载在 `sibling2Ptr` 上的分支会抢先执行）。
  - **不要把世界相机矩阵写回 `vp.baseMatrix` 做平移插值**：实例插值默认关闭时，玩家车停在当前帧 $N$，对世界相机做平移插值
    会让车身在 120 Hz 子帧上前后跳动半米（肉眼重影、单帧截图看不出）。防抖只围绕视图空间车心 $(0, 0, -L)$ 做纯 Y 轴水平偏航旋转。
  - **同一视口内所有模型必须使用完全相同的防抖矩阵（`currYawLag`）**：绝不能按包围盒区分“玩家车模型”与“路面模型”分别用不同旋转角
    （否则 Daytona 2 车底与车侧两块相邻路面瓦片在交界处会因角度差裂开 1 像素细缝、露出底层蓝色 2D 背景）；且同一仿真步的各 120 Hz
    子帧保持同一 `currYawLag`，避免静止快照在相邻子帧画在两个微差水平角度产生重影。全屏视口共用主赛道视口驱动的唯一 `m_sceneYawLag`，
    并在 `prevViewport.find` 的 `continue` 之前赋值，防止回火插入第 13 个视口导致链表地址平移时漏掉稳定或误报 `cut`。
  - **只对与主赛道共享 3D 世界相机的视口施加防抖（`sharesPrimaryCamera`）**：Daytona USA 2 的速度表、路线图和转弯指示箭头
    画在 `496×384` 的全屏高优先级 3D HUD 视口里（相机在原点附近）；必须要求 `vp.priority == primaryCamVp->priority`
    且 `vp.cameraMatrix == primaryCamVp->cameraMatrix`（且 `|camT| >= 2.0`），否则全屏 3D HUD 也会跟着方向盘左右晃动。
- **`VRCycle`（单键循环切视角）必须是瞬时脉冲（`m_vrPulseFrames = 6`），不能常驻按住**：
  街机的 `VR1`~`VR4` 是瞬时微动按钮。若在 `IsVRSelected()` 里写成 `if (m_vrCycle) return index == m_vrCycleIndex;`，
  会让默认 `VR3` 从开机第 1 帧起全程处于被死死按住的状态，100% 触发《Daytona USA 2》的“按住 VR3 进赛道强制把 BGM 换成
  Track 8 (`AE 10 08`)”隐藏选歌秘技，导致所有赛道发车后都放同一首歌。
- **音频高保真链路与多声道录制（详见 `runbook_sound_fix.md`）**：
  - `NbSoundChannels` 支持 `1 / 2 / 4 / 6`（5.1 Surround 交错顺序 `[FL, FR, FC, LFE, RL, RR]`）；`SurroundUpmix51` 用共模提取合成 `FC`、用 120 Hz 二阶巴特沃斯低通合成 `LFE`。
  - `RecordWav` + `RecordWavFormat`（`wav_float32` / `wav_pcm16` / `flac_pcm24` / `flac_pcm16`）通过 SPSC 无锁环形缓冲在后台线程写入 `Data/Recordings/Sound/<英文游戏名>_YYYY-MM-DD<星期缩写>_HH-MM-SS_<N>ch.wav/.flac`；开启录制时强制保留 4/6 声道不向立体声折叠。
  - SCSP 纯 PCM 通道（`!isFmCarrier && !isFmModulator`）走 4-tap Catmull-Rom 立方插值，FM 通道保持 2-tap 线性；DSB 启用 `MINIMP3_FLOAT_OUTPUT` 32-bit Float 直通 64-tap 重采样器并带逐样本线性音量斜坡；主总线 `ToHostSample()` 使用 $C^1$ 连续 Knee Soft-Clipper（膝点 `0.85`）。

## 当前状态（截至 2026-11-04）

- **架构解耦：4 个 REXABYTE SOFTWARE INC. 原创核心算法物理剥离为独立双许可头文件（Clean-Room Decoupling）**（2026-11-04）：
  将 `v0.9.8.6a` 之后新增的原创核心算法从原有 GPL 源文件中物理剥离为 **100% 零 Supermodel 依赖、仅使用标准 C++ 库**的独立头文件，著作权独家归属 **REXABYTE SOFTWARE INC.** 并采用商业/开源双许可（可直接复用到商业版 `E:\Dev\Model3onAndroid`）：
  1. `Src/Sound/IglooAudioDSP.h`（`Igloo::AudioDSP`）：`MasterSoftClip`、`ButterworthBiquadLPF`、`UpmixQuadTo51`、`CubicInterpolate4Tap`、`LinearGainRamper`；
  2. `Src/Sound/IglooMultiChannelRecorder.h`（`Igloo::Audio`）：`WaveFormatExtensibleHeader`、`MultiChannelRecorder<SampleT>`（SPSC 无锁环形缓冲、后台落盘线程、WAV Extensible 与 RFC 9639 24/16-bit 无损多声道 FLAC 编码器）；
  3. `Src/Graphics/Vulkan3D/IglooChaseCamera.h`（`Igloo::Camera`）：`ChaseCameraStabilizer` 弹簧阻尼偏航角积分器、视图空间车心支点 $(0, 0, -L)$ 反旋转矩阵与视锥角度裕量计算；
  4. `Src/Inputs/IglooInputFilter.h`（`Igloo::Input`）：`GamepadSteeringFilter`（$\gamma$ 曲线 + 指数平滑 + 步长速率限制）与 `IsRacingWheelDeviceName` 方向盘启发式识别器。

- **新增：原生 4.0 / 5.1 多声道输出 + 智能 5.1 上混器 + 无锁 WAV/FLAC 录制器 + SCSP 4-tap 立方插值与 DSB Float 直通 + `VRCycle` 脉冲修复 + 单一 `Model3-Vulkan.ini` 与日志诊断增强**（2026-11-04）：
  支持 5.1 环绕声（`NbSoundChannels=6`、`SurroundUpmix51`）与无锁多声道 WAV/FLAC 录制（保存至 `Data/Recordings/Sound/`）；
  SCSP 非 FM 通道升级为 4-tap Catmull-Rom 立方插值并修复从芯片 FM `RINGBUF` 串扰、Slot 16/17 `EXTS` 混音与动态包络/LFO 寄存器更新；
  DSB MPEG 解码改为 32-bit Float 直通与逐样本音量斜坡；修复 `VRCycle` 常驻按住 `VR3` 触发 Daytona 2 隐藏选曲秘技导致所有赛道播放同一首 BGM 的问题；
  `GamepadSteeringSmoothing` 选关闭时灵敏度自动归位 100%，且绑定真实方向盘时自动旁路保持 1:1 直通；
  移除 `Supermodel.ini` 残留（仅保留 `Data/Config/Model3-Vulkan.ini`），增强 `Data/Model3Vulkan.log` 自动诊断信息。
  完整架构与排错见 `runbook_sound_fix.md` 与 `devdocs/CHANGELOG.md` 2026-11-04。

- **构建与日志：`Supermodel-LegacyOG.exe` 轻量退役 + 诊断日志改名为 `Model3Vulkan.log`**（2026-11-02，未 bump 版本）：
  `CMakeLists.txt`、`build-cmake.bat`、`build.bat` 默认仅构建并打包 `Model3-Vulkan.exe`（不再编译/分发
  `Supermodel-LegacyOG.exe`）；默认诊断日志改为 `Data/Model3Vulkan.log`；「核心 → 显示与运行」隐藏旧版
  `BorderlessWindow` 复选框。详见 `devdocs/CHANGELOG.md` 2026-11-02。

- **新增：追尾镜头防抖（`ChaseCameraStabilizer`）+ 摇杆转向灵敏度（`GamepadSteeringSensitivity`）**（2026-11-02，未 bump 版本）：
  「力反馈 / 灵敏度」标签页移至「按键」紧后方（并把「网络」挪到最末），页面顶部及游戏内 Tab 面板提供「摇杆转向平滑」（默认强力 `3`）、
  「摇杆转向灵敏度 (%)」（默认 `66%`）与「追尾镜头防抖」（默认强力 `3`）。围绕视图空间玩家车心支点 $(0, 0, -L)$ 对相机水平偏航做弹簧阻尼平滑；
  已解决 120 Hz 子帧车身前后重影、建筑锯齿虚晃、Scud Race 迈凯伦松油门车尾回火视口平移闪动，以及 Daytona 2 全屏 3D HUD（码表/路线图/箭头）
  跟随晃动问题。详见 `devdocs/CHANGELOG.md` 2026-11-02。

- **性能：Android 场景构建优化移植回 PC**（2026-11-02，未 bump 版本）：YouTube 评论「单核重」
  排查确认 JIT 指令翻译层在 PC 已完整（与 Android arm64 逐 case 对齐），但 Android 性能第 3 轮
  的 `CR3DSceneBuilder` 优化未反哺；本次移植三处（`DrawModel` 负缓存 `m_dynamicRomModels` +
  `find`、`CopyVertexData` 一次 resize 直写、`hypot`→`sqrt`），与 `E:\Dev\Model3onAndroid`
  逐行对齐。编译 0 错误；笔记本实机（i5-8365U/UHD620）`daytona2.zst1` 冒烟通过；逐像素 A/B
  未做。笔记本上有新旧对照目录（`C:\EMU\Model3-Vulkan.v0.9.8.7.preopt` = 旧版）。
  详见 `devdocs/CHANGELOG.md` 2026-11-02。

- **修复：macOS Retina 屏上 4K 设置无效**（2026-11-01，未 bump 版本，**无 Mac 真机，未验证**）：
  游戏窗口从未申请 `SDL_WINDOW_HIGH_PIXEL_DENSITY`，Cocoa 给的是 1x drawable，4K 面板在系统
  默认缩放下按点尺寸（1920×1080）渲染再被拉伸；同时 `CalcScreenGeometry()` 读的是点尺寸，所以
  整数倍缩放 / CRT 预设 / 4K 硬件预设都按 1080 行算。现在游戏窗口申请高密度 drawable，
  渲染几何改用设备像素（`GetRenderSize()`），输入仍用窗口坐标（`WindowSpacePictureRect()`）。
  Windows/Linux 数值不变（`pixelDensity = 1.0`，`GetWindowPixelSize == GetWindowSize`）。
  详见 `devdocs/CHANGELOG.md` 同日「macOS Retina 屏上 4K 设置无效」与 `devdocs/FIXES.md` 案例 53。
- **用户反馈一轮（2026-11-01）**：见 `issues_from_users.md` 与
  `sdl3_reports/2026-11-01-user-issues/`。#17 转向灵敏度入口太隐蔽（英文标签页 `FFB` →
  `FFB / Sensitivity`，页顶提示收窄到力反馈分组，提示补「按没有方向盘的手柄玩家调的」）；
  #15 Daytona 2 相机「自己上下变」判断为视角键（`VRCycle` 默认开、手柄 **Y**），新增
  `Input: view cycle -> VRn.` 日志便于下次定性。#16 仍未修（见下条）。
- **排查：Scud Race Deluxe HUD 灰板（issue #16）**（不修，属长期模拟缺口）：ROM CRC 逐文件校验
  全对（`scuddx` 41/41），排除坏 dump；Vulkan 与 LegacyOG 的灰板逐像素一致，排除渲染后端。
  **`scud`（Twin/DX）一直都能正常显示，症状只在 Deluxe 上**。40 秒长局显示 `scuddx`/`scud`/
  `scudau` 的 CONDITION 视口**都是 0 个贴图多边形**、TileGen 层基本为空——这两条在 Twin 上同样
  成立，所以**都不是成因**，只能说明地图不走已知两条路径（别再拿它们当“三版一样烂”的证据）。
  用户确认「从 MAME 时代就没解决过」。绕过：改用 Twin/DX 版本（`scud`/`scudau`/`scudplus`/
  `scudplusa`）。新增 `SM3D_HUD_DEBUG` 钩子，详见 `issues_waitlist.md` 与
  `sdl3_reports/2026-11-01-scuddx-hud/`。
- **修复：`Capture-Frame.ps1 -RawState` 拿到的是容器而不是 raw state**：延迟读档路径单独删
  `.vktmp.st` 且未看 `SM3D_KEEP_STATE_TEMP`，两者现在一致。

- **修复：导出的贴图上有白点 / 虚线**（2026-11-01，未 bump 版本）：社区反馈导出图“坏了”，
  实际是 T1RGB5 bit 15 的 1-bit 透明抖动被写进了 PNG 的 alpha，而不用 alpha 的材质在运行时
  会被强制成不透明。现在 `DumpTexture()` 按材质决定：不读 alpha 写 255，读 alpha 保留原机掩码，
  同一哈希后到读 alpha 的材质会重导。重叠的 288 个哈希 RGB 逐像素一致、89 张从带 alpha 变成
  全不透明；新增 `Tools/inspect-texturedump.py` 排查工具。
  详见 `sdl3_reports/2026-11-01-dump-alpha-dither/`。

- **已发布：v0.9.8.6a（仅 Windows）——Windows 上 AMD 显卡启动即退出**：AMD 的 Windows Vulkan
  驱动 `maxPushConstantsSize` 只报 **128**（NVIDIA/Intel/Mesa 都是 256），而 `R3DPush` 是 **208**
  字节且 `PickDevice()` 硬性要求 `>= 256`，于是启动图闪完就退出，设置窗口都出不来。现在把逐视口的
  `projection` / `lightDir` / `lightParams` / `fogColour`（共 112 B）搬进 `DrawBucket()` 本来就
  每 node 写一次的 `ViewportUBO`，`R3DPush` = **96 B**，门槛改为规范保底的 **128**；并补了逐设备
  诊断行（设备名 / API / 驱动版本 / push constant 字节数 / 拒绝原因）。同一存档同一仿真帧的 A/B
  **逐像素相同**（`sdl3_reports/2026-10-31-amd-win-pushconst/`）。产物
  `v9.8.6a_upload/Model3-Vulkan.v0.9.8.6a.for.Windows.zip`（19,205,744 B，SHA-256
  `d5cff6c6d2d074f5ff3a2ad5c142d7452d5e3d48baa66c80a4f2ff442cf6445f`），tag `v0.9.8.6a`
  **未标记 Latest**（Latest 仍是 `v0.9.8.6x`），发布时临时停用 `release.yml` 以免触发 CI。
  改动**已同步回主树 0.9.8.7**（本地，未发布，版本号未 bump），主树自身的同帧 A/B 也是逐像素相同。
  手边没有 AMD 硬件，128 这个数字仍待一台 AMD Windows 机器确认。

- **Linux 启动链路四修**（2026-10-31，**未发布**，`MODEL3VULKAN_VERSION` 本地改成 `0.9.8.7`）：
  CI 基线固定为 Ubuntu 24.04 + g++-14（发布包曾要求 glibc 2.43，Steam Deck / 24.04 直接
  加载失败）；界面字体随包发并拒绝无拉丁字形字体；Linux 改用 `<exe>/Data/`；Wayland 补上
  libdecor 标题栏。验证在一台 Ubuntu 24.04.4 + Intel UHD 620 实机完成（编译、预检、设置窗口、
  截图），Steam Deck 与 Windows 未复测。详见 `sdl3_reports/2026-10-31-linux-startup/`。
  **下一版 Release Notes 的英文草稿**（说明是 GitHub `ubuntu-latest` 镜像前移导致的兼容性事故）
  记在 `devdocs/CHANGELOG.md` 同日的「下一版 Release Notes 草稿」一节；发版前不要写进
  `RELEASE_NOTES.md`，那个文件会被 `release.yml` 直接当作发布正文。
  PowerPC JIT 那一轮尚未收尾，发版时会与这些改动一起走。
  **已基于 0.9.8.6 快照单独出了 Linux-only 热修包 `v0.9.8.6l`**（同一套修复，不含 JIT）：
  产物与英文发布正文在 `v9.8.6l_upload/`，内部记录见 `devdocs/CHANGELOG.md` 同日「发布：
  v0.9.8.6l」一节。**该包已重新打包并覆盖上传**（19,278,090 B，SHA-256
  `33a774bc3e845b3ba6875c16c56230b047189cb4498d8398fba3cef803c57ffb`），现在**内含
  `libModel3VulkanHostCore.so`**，`.zst` 存档开箱可用（Native AOT，只要 `GLIBC_2.34`，
  用户不需要装 .NET）。主工作树仍是 `0.9.8.7`、未发布；JIT 的说明留待将来的大版本。
  **已被 `v0.9.8.6x` 取代**（与 macOS 合并发布，见下）。

- **发布：v0.9.8.6x（Linux + macOS，2026-10-31）**：同一份源码树的 Linux 与 macOS 两个包，
  取代 `v0.9.8.6l` / `v0.9.8.6m`，也是当前 Latest。**唯一的代码改动**是游戏内 Tab 面板在
  非 Windows 上收不到输入（见「已知的坑」）。
  - Linux `Model3-Vulkan.v0.9.8.6x.for.Linux.tar.gz` 19,277,981 B，SHA-256
    `d00c7a9b03d4429c9336152ea9034d586302b96bfca0b73d769b5c388c7e0ce6`；
  - macOS `Model3-Vulkan.v0.9.8.6x.for.macOS-AppleSilicon.tar.gz` 17,217,614 B，SHA-256
    `461fb2aaa3894ef26c84af101d9b96d2e9d3d2ec20079085e770a0afbbdd6b3b`；
  - macOS `Model3-Vulkan.v0.9.8.6x.for.macOS-Intel.tar.gz` 17,825,125 B，SHA-256
    `3706b40bc98fff151627be74f8cb61337413c1c27479fb837113ab971ed8bd66`；
  - 产物与英文正文在 `v9.8.6x_upload/`，内部记录见 `devdocs/CHANGELOG.md` 同日「发布：v0.9.8.6x」。
  - **macOS 的两个包由 CI 出**：本机 Zig 编不出 arm64，改用 GitHub Actions 的 macOS runner
    （现有的 `build-model3.yml` 本来就是 universal），再在本机用 `llvm-lipo-18 -thin` 拆成
    Apple Silicon / Intel 两个包。临时分支 `release/0.9.8.6x` 用 Git Data API 建、构建完已删，
    master 未动。`.zst` 仍需 macOS 14+。
  - **黑苹果的源码树命名**：`~/m3v-legacy-src` 才是 6m 的树（有 mac `Data/` 修复）；
    `~/m3v-src` 是缺那一处修复的副本。Apple clang 14 编不了 C++23，必须用
    `~/bin/zig-cxx`（Python `ziglang` 模块）。

- **PowerPC 603e → x86-64 混合模式 JIT**（2026-10-30，未 bump 版本）：
  新增 `Src/CPU/PowerPC/ppc_jit_x64.inl`（配合已有的 `X64Emitter.h`）；「核心」页新增
  `PowerPCMode` 下拉框（`JIT 混合模式（推荐）` / `C 纯解释器（对照基准）`，默认 JIT，
  下一局生效），命令行 `-ppc-mode=0|1`，环境变量 `SM3D_PPC_JIT=0|1` 优先。
  前置设施同时补齐：`PPC_FAST_MEMORY`（`CModel3::PublishFastMemory()`）、
  `ppc_execute_one_instruction()`、wait-loop skip（`SM3D_IDLE_SKIP=0` 可关）。
  验证：编码器自检 17/17；5 个游戏约 **6.75 亿块锁步校验 0 失败**；同存档同帧与解释器
  **逐像素一致**；CPU 瓶颈场景 224.5 Hz vs 55.2 Hz（约 4.1 倍）。
  两个必读的坑（`&s_pins` 不能用 `rax`、`oe` 分支的 `break` 作用域）写在 AGENTS.md
  「已知的坑」与 `devdocs/FIXES.md` 案例 51。详见
  `sdl3_reports/2026-10-30-ppc-x64-jit/README.md`。

- **版本升至 v0.9.8.6**（2026-10-29，**已发布**）：`MODEL3VULKAN_VERSION` 从 `0.9.8.3b`
  改为 `0.9.8.6`，窗口标题栏、控制台横幅、关于页、统计面板水印、游戏内面板同步；
  `SUPERMODEL_VERSION` 未动（存档兼容）。本轮内容：`InputUI*` 可从 INI 配置（RetroBat /
  Tartifless 补丁）、鼠标全轴绑定的主导轴过滤、SDL3 默认模式下独立排挡杆与大键设备、
  键位预设下拉框写回、游戏列表「设为启动时自动运行」（`AutoLaunchGame`）、「关于与开源许可」
  弹窗居中。发布页：<https://github.com/Igloo-Garage/Model-3-Vulkan/releases/tag/v0.9.8.6>；
  旧代码归档分支 `archive/v0.9.8.3b`。详见 `devdocs/CHANGELOG.md` 的 2026-10-29 小节。

- **GPLv3 §7 署名焊死 + 加拿大公司商标合规 + 目录彻底改名**（2026-10-29，未 bump 版本）：
  - 仓库根目录与发行目录已统一为 `Model-3-Vulkan` 与 `dist/Model-3-Vulkan`（已移除旧 `Supermodel-VK-SDL3.ini` 迁移逻辑）。
  - 根目录新增 `LICENSE-ADDITIONAL-TERMS.md`（GPLv3 §7(b)-(e) 附加条款）与 `TRADEMARK.md`（绑定加拿大实体公司 **REXABYTE SOFTWARE INC.** 与未注册商标 `Model 3-Vulkan™` / `Igloo-Garage™`）；第三方/上游署名清单 `NOTICE.md` 放在 `Docs/NOTICE.md`。打包时这三份与 `Docs/LICENSE.txt` 统一收进 `dist/Model-3-Vulkan/Data/`。
  - 原创 C++ 源码与 GLSL/`.spv.h` 着色器文件头统一为 `Model 3-Vulkan` 及 `Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)`；自有品牌宏统一为 `MODEL3VULKAN_*`（保留上游 `SUPERMODEL_VERSION` 与 `Supermodel-LegacyOG.exe`）。
  - 启动图双行字幕（`SplashScreen.cpp`）、统计面板底部水印（`StatsOsd.cpp`）、游戏内 Tab 面板底部链接（`Overlay.cpp`）、设置界面底部常驻「关于与许可」徽章及弹窗（`Gui.cpp` 的 `DrawAboutBadge()` / `AboutLegalModal`）及控制台横幅（`Main.cpp`）已焊死署名，**后续优化严禁删除或隐藏这四处署名显示逻辑**。

- **v0.9.8.3b 已发布**（2026-10-28）：高清纹理 DDS（BC1/2/3/5/7）与 `.dds.zst` 支持、
  F4 切换替换贴图 / F3 重扫、Vulkan 场景目标布局与同步修复，以及「所有游戏只有音乐、
  没有音效」的修复。发布资产：Windows zip、Linux/macOS tarball（CI）、`SHA256SUMS.txt`；
  源码快照目录 `v9.8.3b_source`、上传资产目录 `v9.8.3b_upload`（与上一版同样式）。

- **修复：所有游戏只有音乐、没有音效**（2026-10-28，已含在 v0.9.8.3b）：
  `CSoundBoard::RunFrame()` 里清空四个 SCSP 输出的 `memset` 挂错了 `else`（挂在调试日志的
  `if (soundDebug)` 上，而不是 `if (EmulateSound)` 上），正常游戏每帧都在 `SCSP_Update()` 之后
  立刻清零，只剩 DSB 音乐。已校正顺序；`SM3D_AUDIO_LEVEL=1` 的源峰值现在拆成 front/rear。
  详见 `devdocs/CHANGELOG.md` 的 2026-10-28 一节。

- **Scud Race 读档：卡死 / 无声 / 四声道折叠**（2026-10-27，未 bump 版本）：
  `scudplus.zst1` 的 DSB1 影子变量是 `0x80008000` 哨兵，旧代码把它当有效范围导致
  `DecodeAudio()` 死循环；现在 `MpegDec` 拒绝非法窗口并在无进展时退出，DSB 读档校验
  范围，`CSoundBoard::LoadState()` 重驱动 `SCSP_RefreshIRQ()`。音频侧按输出设备的真实
  声道数混合（二声道端点自动把后声道折进前声道）。新增可选存档块 `DSB1 MPEG` /
  `DSB2 MPEG`，新存档可以把 BGM 从原位置续播（旧存档没有这个块，选车 BGM 不会自己
  恢复）。详见 `devdocs/CHANGELOG.md` 的 2026-10-27 一节。

- **Ko-fi 徽章 + 便携 ROM 目录 + HostCore 跨平台**（2026-10-27）：设置窗口 footer 的按钮行
  右侧新增 Ko-fi 支持徽章（`https://ko-fi.com/igloogarage`）；启动时自动采用 exe 旁的
  `rom`/`roms`/`romset`/`romsets`，Linux 的 `GetBasePath()` 改用 `/proc/self/exe`；
  HostCore 的加载名按平台区分（`.dll`/`.so`/`.dylib`），CMake 新增 `hostcore` 发布目标。
  详见 `devdocs/CHANGELOG.md` 的 2026-10-27 一节。

- **产品改名：Model 3-Vulkan v0.9.8.3a**（未 bump 版本）：主程序改为
  `Model3-Vulkan.exe`，冷路径模块改为 `Model3VulkanHostCore.dll`，配置迁移到
  `Model3-Vulkan.ini`；控制台横幅是单行
  `Model 3-Vulkan v0.9.8.3a(formerly Supermodel-VK) - Igloo-Garage`。
  启动图与主程序图标改用根目录的新 `Igloo-Garage Presents.webp` / `output.ico`。
  详见 `devdocs/CHANGELOG.md` 的 2026-10-27 一节。

- **测试脚本的 ini 保护**（未 bump 版本）：`Capture-Frame.ps1` / `Measure-FrameRate.ps1`
  的 `-Ini` 覆盖现在带 `<ini>.testbak` + `<ini>.testrun` 侧车，启动时自动还原被硬杀的现场；
  中断时先关闭会话再还原。手动入口 `Tools/Restore-TestIni.ps1`。
  典型症状与排查见「已知的坑」和 `devdocs/FIXES.md` 案例 49。

- **高清纹理导出与替换（HD Texture Pack）已实现**（未 bump 版本）：
  - 新增 `Src/Graphics/Vulkan3D/HDTextures.{h,cpp}` + `TextureDecode.h`；按子纹理
    `(page,x,y,w,h,format)` 做 FNV-1a 64 位内容哈希（逐 tile 版本号失效缓存），
    导出 RGBA8 PNG 到 `Data/Assets/DumpTextures/<set>/`，从
    `Data/Assets/HDTextures/<set>/` 加载 PNG/WebP 任意倍数替换图。
  - Vulkan 侧：loader 线程解码，`RecordSync()` 分块上传 + `vkCmdBlitImage` 生成 mip，
    `maxAnisotropy=16`；bindless 数组独立放在 **descriptor set 2**（set 0 含动态 UBO/SSBO，
    不能同时开 `UPDATE_AFTER_BIND`，VUID 03001）。
  - 着色器 `real3d_common.glsl` 在 `R3DMaterial.hdTexIndex >= 0` 时走
    `textureGrad(u_hdTextures[...], uv, dFdxFine(uv), dFdyFine(uv))`，跳过页内解码与微纹理。
  - 界面：`核心` 页底部 + 游戏内 Tab 面板；`LoadHDTextures` / `DumpHDTextures`，
    面板按钮与 **F3**（或 Shift+F4）重扫目录（`Vulkan3D::RequestHDTextureReload()`），
    **F4** 切换替换/原版（改的就是 `LoadHDTextures`，下一仿真帧生效，不重扫）。
  - 健壮性：2048 槽 / 3 GB 上限；未就绪的槽返回 -1，材质继续采样原页；`build.bat`
    保留这两个 Assets 子目录，`-package` 排除它们。
  - 验证记录见 `devdocs/CHANGELOG.md` 的 `2026-10-25`「高清纹理导出与替换」一节。

- **界面重做：毛玻璃 + 浅色/深色/原始三套主题**（未 bump 版本）：
  - 面板背景改成当前画面的**双重 Kawase 模糊**（半分辨率四遍，`Graphics/Vulkan3D/UiBlur.*`），
    带 1px 玻璃高光边；游戏内链路只在 Tab 面板打开时录制，关着时 0 开销。
  - 三套主题，`UITheme`（**0 = 浅色暖色护眼，默认；1 = 深色；2 = 原始界面**），在「常用 →
    界面外观」里，切换立即生效。两种玻璃主题都不用纯黑纯白；浅色会把模糊画面向暖白提亮
    （不回补饱和度会发灰）。**原始界面**是毛玻璃重做前的平面暖纸外观（不锈钢蓝强调色、不透底、
    无底图），逐值复制，用户可以随时切回去。
  - 设置窗口把所选游戏封面洗淡铺成**氛围底图**，界面变成三块浮在上面的玻璃面板；
    每块玻璃内缩留白（`panePad`），子窗口 `AlwaysUseWindowPadding + NoBackground`。
    游戏内面板单独用更大的 `WindowPadding`，`OverlayBody` 无底色，存档表额外 `Indent`。
  - 圆角 14/10/7/6/10 + `CircleTessellationMaxError 0.10` + `FrameBorderSize 1`；
    按钮/复选框悬停 120ms 指数平滑，面板呼出 120ms smoothstep 淡入。
  - 超低配极速预设会写入 `UiFrostedGlass=0`。
  - 工具：`Tools/Capture-Ui.ps1`（DPI-aware 抓窗口）、`Tools/Crop-Image.ps1`、
    `Tools/Measure-PanelInsets.py`（量面板边距）。
  - 详见 `devdocs/CHANGELOG.md` 的 `2026-10-25` 一节。

- **超低配极速预设 + GUI 状态记忆**（未 bump 版本）：
  - 硬件预设新增「超低配极速（496×384，核显）」，写 `IntegerScale=1`、`QuadInterpolation=0`、
    `RefreshRateMode=0`、`MotionInterpolation=0`、`PostFXAA/PostCRT/PostGlow=0` 等；
    `QuadInterpolation` 走 `ViewportUBO.renderOptions.x`，默认仍为精确路径。
  - **低分辨率靠 `IntegerScale`，不是 496×384 这个数字**：`CalcScreenGeometry()` 在窗口/全屏下
    会把 `XResolution/YResolution` 按 4:3 放大到可用区域（1080p 全屏 → 1395×1080）。点对点
    勾选时改按 496×384 × N（1080p → 992×768）。取消勾选会让 3D 场景按面板分辨率渲染、
    核显明显掉帧；预设下方为此有琥珀色提示，不要把它理解成“把原生分辨率拉伸到 1080p”。
  - **`InternalResolution` 是索引，不是布尔**：0 = 关闭，1 = 496×384，2 = 992×768（2x 原生）。
    `Vulkan3D` 用它设置 `m_internalExtent`，`Vulkan2D` 只关心是否非零并把场景贴到画面矩形。
    新增档位时同步改 `DefaultConfig` 的允许值 `{0,1,2}`、`Gui.cpp` 的 combo 和
    `Localization.cpp`。它与 `IntegerScale` 互斥；后者仍按原生的 496×384 取整数倍。
  - 从 Manage Saves 的「读取」进入游戏，按 Esc 返回后自动重开该 set 的存档页；
    从游戏列表启动则仍回列表（启动路径会清 `g_returnToManageSaves`）。
  - 退出时保存 `GuiLastSelectedGame` / `GuiGameListScrollY` / `GuiGameListFilter`，
    下次启动恢复选中项、封面、筛选和滚动位置（滚动在表格行提交后延迟 1–2 帧应用）。
  - `FixedRefreshRate` 的候选档位按窗口所在显示器的完整模式列表置灰，启动时把过高值
    钳制到最高可用档；GUI 与游戏循环共用 `DisplayPacer.h` 的候选表与 `ClampFixedRefreshRate()`。

- **读档时机修正**（未 bump 版本）：`Manage Saves → Load` / `-load-state` 不再在开机阶段
  装状态，而是等 172 个仿真帧（≈3 秒）后用整屏启动图过渡。修掉了 VF3TB 读比赛中存档后
  “人物不动、计时缓慢爬升”（逻辑只跑 1/25 速度）的问题。详见
  `devdocs/CHANGELOG.md` 的 `2026-10-25` 一节。
- **存档缩略图文件名带序号**（`.vkthumb-<n>.png`）：固定文件名在渲染器没能覆盖旧文件时
  会把上一次的截图打进新容器，表现为缩略图和实际画面不一致。

- **默认半透明路径已改为双 Subpass 片上合成**（未 bump 版本）：4K 4x 下 VF3 海滩
  226 → 240–241 Hz（约 +6%，比“跳过全部 resolve”的探针还快）；1x 与旧版逐像素一致
  （≤1 LSB）；修掉了 4x MSAA 下最后一个 Priority 的透明丢失。
- **「高级设置 → 硬件预设」四档一键组合**（分辨率 + MSAA + 半透明排序 + 贴图过滤，
  分辨率按主显示器上限下调，下一局生效）：核显 1080p/1x/双层/锐利；入门独显 1080p/2x；
  中端独显 2K/2x/OIT；高端独显 4K/4x/OIT。档位表在 `DrawAdvancedSettingsPanel()` 的
  `QualityTier tiers[]`，改档位要同步 `Localization.cpp` 的按钮文案。
  详见 `sdl3_reports/2026-10-24-subpass-composite/README.md`。

- **设置界面下拉框按内容自适应**（`ComboRow` / `ComboWidth`，下限 120 px）：短选项不再
  占满一行，长选项（档位、贴图过滤、半透明排序）不再被截断。「统计面板」配色下拉框位于
  「显示帧率」与「统计面板」之间，统计面板关闭时置灰；英文预设名是单词（Amber / Slate /
  Contrast / Outline，完整说明在 tooltip），英文的「Statistics panel」缩写为「Stats panel」，
  所以 1280 宽下整行仍能并排。
- **最小窗口 1280x720、默认窗口 1280x800 逻辑像素**：720 是布局真正能渲染的下限
  （1080p 屏在 150% 下逻辑工作区只有 1280x720），800 只在屏幕放得下时作为首次启动的
  舒适尺寸。下限**按当前显示器重算**：`ApplyGuiWindowMinimumSize()` 在每帧检测显示器 /
  content scale / 工作区变化，并扣除窗口边框（`GetWindowBordersSize()`），所以从 150%
  屏拖到 100% 屏不会保留原先的 1920x1080 物理下限；已超出新工作区的窗口会被拉回来
  （最大化窗口不动）。
- **跨 DPI 时界面重新缩放**：同一检测发现 content scale 变化时调用 `RescaleUi()`
  （`ApplyUiTheme()` + `ScaleAllSizes()` + `FontSizeBase = 18 × scale`，ImGui 1.92 动态
  烘焙字形，不重建图集），并把窗口重置为 1280x800 逻辑尺寸。150% → 100% 得到 1.0× 界面
  和 1280x800 窗口，不会占满屏幕。截图里的 `2026-10-24-ui-scale-100.png` 就是这一档。
- **macOS**：窗口用 point，Retina 由 `io.DisplayFramebufferScale` 处理，所以
  `GetDPIScale()` 固定 1.0，`RescaleUi()` 不会触发（跨 Retina / 非 Retina 界面尺寸不变，
  这是 macOS 的正常行为）。跨显示器的最小尺寸 / 工作区封顶逻辑照常生效；SDL3 的 Cocoa
  后端没有实现 `SDL_GetWindowBordersSize`，所以 `GetWindowBordersSize()` 在标题栏尺寸为
  0 时按标准 28 pt 补一个上边框，避免窗口底边钻到 Dock 下面。

- 63 个 ROM set 全部有 WebP 封面（`Assets/Boxarts`）。
- 启动不再黑窗：窗口隐藏创建 → 主题图在屏幕正中显示 → 首帧 present 后换成界面
  （`Src/OSD/Windows/SplashScreen.cpp`，两个 exe 共用）。**游戏窗口**同样隐藏创建，
  在 `Main.cpp` 的 `EndFrameVideo()` 首次 present 后才显示（这里不放图）。
  Manage Saves → Load / `-load-state` 另有一份整屏版本（`coverMonitor`），只用于盖住
  读档前的 3 秒启动过程；日志有 `Splash: ... covering the monitor` 与
  `Save state: covering the boot sequence ...`。
- 设置界面左列：ROM 目录 → 可拖动预览面板（封面 + 英文简介）→ 游戏列表；
  ROM 数量显示在窗口底部状态栏；分隔比例存 `GuiPreviewSplit`。
- **高刷插值已实现**（v0.9.5.5a）：逻辑固定 57.524Hz（`Model3Timing.h` /
  `DisplayPacer`），呈现跟随面板刷新率，亚帧对相机与实例矩阵做
  lerp + slerp（`MotionInterpolation.*`）。新安装默认 `RefreshRateMode=Auto`
  + 插值开；旧 ini 的 `RefreshRate` 首次启动自动迁移。详见
  `sdl3_reports/2026-10-23-10-high-refresh-interpolation.md`。
- **VRR 整数倍频已实现**（v0.9.5.6a）：`RefreshRateMode` 3/4/5/6 =
  2x/3x/4x/8x（115.05 / 172.57 / 230.10 / 460.19Hz），相位步进器把 alpha
  固定为 `k/N`，F8 在 1x/2x 间热切。面板不足时自动回退；`PaceAssumePanelHz`
  是测试专用钩子。详见 `sdl3_reports/2026-10-23-11-vrr-multiples.md`。
- **Batch 2 已实现**（未 bump 版本）：SDL3 方向盘恒力/回中/阻尼（20 ms 力矩
  平滑）、按 `SDL_MouseID` 的双光枪 + Windows RawInput、Sinden 白边、原生 TCP
  联机（`NativeSocket`，可连 SDL2 版）。详见 `CHANGELOG.md` 的 `2026-10-23`
  的 Batch 2 一节。未在真实外设/双机上实测。
- **存档与版本识别**：存档按运行中的 set 命名，而 set 由 zip 内容的 CRC 识别，
  不是文件名。`GameLoader::Identify()` 现在同时用于「管理存档」（
  `g_manageSavesRow` / `g_manageSavesGame`）和游戏列表（`RomEntry::resolvedSet`），
  所以 `scuda.zip` 这种名字不在 Games.xml 里的包也能显示正确版本并启动。
- **窗口拖动暂停**：SDL3 目标在 `WM_ENTERSIZEMOVE` 暂停 + 静音，松手 flush 后恢复
  （SDL2 目标仍是模态循环里跑帧）。
- **打开游戏内面板自动暂停**（`overlayPausedEmulator`），关闭时只恢复面板引起的
  暂停。
- **Batch 3 已实现**（未 bump 版本）：Vulkan pipeline cache 持久化到
  `Data/Cache/pipelines-{ui,game}.bin`（冷启 20 ms → 热启 8 ms）；输入改为
  仿真步前一刻晚采样（`PollGameInputsLate`）；Auto 模式下显示器已在 57.5 Hz
  或提供 57.524 Hz 模式时自动用 `Original`。详见 `CHANGELOG.md` 的 Batch 3 一节。
- **Batch 4（第 1、2、3 项）**（未 bump 版本）：
  - 实例插值加了四层配对校验，但结论是**保持默认关闭**并撤出设置界面——白线是“跳变实例
    与插值实例交界”的混合公式裂缝，无廉价修法。
  - `VK_KHR_present_wait` + `VK_KHR_present_id` 已启用。
  - **提交相位校准**：pacer 配置后 16 帧阻塞测出截止点落后 vblank 的距离，设一个固定
    提前量（相位平移，不改节拍）。Fixed 120 下从 `0/121` 变成 `121/121 confirmed`，
    约减少 4.6 ms 端到端延迟。详见 `CHANGELOG.md`。
- **逐像素半透明排序（`AlphaSortMode=1`）已实现**（未 bump 版本）：每个 Priority 内
  用固定槽位 SoA A-Buffer（4 层/像素 × 8 B）收集半透明片元，排序后合成；4K + MSAA 4x
  下与双层路径同为 120 Hz。默认 0 保持逐位兼容。链表版因全局原子与指针追逐只有
  27–37 Hz，已废弃；细节与性能表见 `sdl3_reports/2026-10-24-alpha-sort/README.md`，
  约束见上面「已知的坑」。
- 详细改动见 `CHANGELOG.md` 的 `2026-10-23` 一节。
