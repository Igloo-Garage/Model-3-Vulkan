# macOS 兼容性与性能报告

> 日期：2026-10-30
> 测试机：Intel 黑苹果笔记本（i5-8365U / **UHD Graphics 620**，运行期伪装为 Iris Plus 655，见 1.1.1 / macOS 13.7.8）
> 测试包：CI artifact `run 41095908870`（commit `b5dd7aa`）解出的 `Model3-Vulkan.v0.9.8.6`
> 对照包：`Supermodel-VK-0.9.2.4a`（用户本机旧版，**OpenGL/SDL2 构建**）
> 相关提交：`9a10b2d` → `9d37125` → `9eb3932` → `b47173d` → `b5dd7aa`

---

## 0. 结论摘要

| 项目 | 结论 |
| --- | --- |
| 能不能开 | **能。** v0.9.8.6 修好后在 macOS 13.7.8 上正常启动、列出 56 个 ROM set、进游戏出画面、存档读档 |
| 启动失败的根因 | 不是显卡弱、不是 MoltenVK 版本低，是**我们自己请求了 `VK_KHR_portability_enumeration`**，`vkCreateInstance` 返回 `-7`（`VK_ERROR_EXTENSION_NOT_PRESENT`） |
| `.zst` 存档 | **不是所有 Mac 都不能用**：macOS 14+ 可用，macOS 13.x 不可用（Swift 5.9 运行时符号缺失）。13.x 自动回退 `.st`，实测保存/读回都正常 |
| 旧版为什么能开 | `v0.9.2.4a` 走的是 **OpenGL（New3D，`QuadRendering=0`）**，从不碰 Vulkan |
| 性能 | 这台机器（UHD 620）在 992×768 下只有 **11–21 FPS**；**早期“OpenGL 比 Vulkan 快 2 倍”的结论已作废**（采样点不对齐，见 5.3 的更正）。同一次运行内仍成立：496×384 约是 992×768 的 2 倍帧率 |
| 后续 | （2）Vulkan 片元着色硬件采样快路径 +（3）macOS OpenGL/Legacy 包，两项都做 |

---

## 1. 测试环境

### 1.1 硬件与系统

```
CPU          Intel(R) Core(TM) i5-8365U @ 1.60GHz（4C/8T，hw.cpufrequency = 1.9 GHz）
GPU          Intel **UHD Graphics 620**（24 EU，300–1100 MHz，无 eDRAM，约 0.42 TFLOPS FP32，
             共享内存 ~35–38 GB/s）
             ⚠️ 运行期报的是 "Iris(TM) Plus Graphics 655" + deviceID 0x3ea5 —— 见 1.1.1
内存         2048 MB GPU 共享（Vulkan memory budget 报告值）
系统         macOS 13.7.8（Darwin 22.x）
显示         1920×1080 @ 60 Hz（Built-in Display）
MoltenVK     1.3.0（Vulkan 1.3.313），来自旧包 Frameworks/libMoltenVK.dylib
Xcode CLT    14.0.3（Apple clang 14）—— 因此本机**不能**编 C++23
热状态       pmset -g therm: CPU_Speed_Limit = 100（无热节流）
```

#### 1.1.1 设备名为什么是 "Iris Plus Graphics 655"

i5-8365U（Whiskey Lake-U）集成的是 **UHD Graphics 620**（deviceID `0x3EA0`，24 EU，
300–1100 MHz，无 eDRAM）。但本机每一条运行时记录都显示 `Iris(TM) Plus Graphics 655` /
`deviceID 0x3ea5`（MoltenVK 的 `GPU device:` 行、`Vulkan: using ...` 行、OpenGL 旧版的
`settings window context` 行都一样）。这是黑苹果引导器（OpenCore / Clover）注入
`device-id` 的结果 —— Whiskey Lake 核显在 macOS 上要靠伪装成受支持的型号才能拿到可用的
framebuffer / Metal 驱动。

注意：`system_profiler SPDisplaysDataType` 和 `ioreg` 也会显示伪装后的 ID，**软件侧无法分辨**；
判断真实型号只能看 CPU 规格（i5-8365U → UHD Graphics 620）。

两者不是一个量级的硬件：

| | UHD Graphics 620（真实硬件） | Iris Plus Graphics 655（被伪装成的型号） |
| --- | --- | --- |
| EU 数 | 24 | 48 |
| 频率 | 300–1100 MHz | 300–1200 MHz |
| eDRAM | 无 | 128 MB |
| FP32 峰值 | ~0.42 TFLOPS | ~0.9 TFLOPS |
| 显存带宽 | 与 CPU 共享 DDR4/LPDDR3（~35–38 GB/s） | eDRAM 额外带宽 |

**影响**：本报告的绝对帧率是“最弱一档 Intel 核显 + 伪装驱动”上的数据，**不能**外推到真正的
Iris Plus 655（48 EU + 128 MB eDRAM）、其他 Intel 核显或 Apple Silicon。相对结论（Vulkan 路径
每像素比 OpenGL 路径贵、496×384 与 992×768 差 2 倍）不受影响。

### 1.2 目录布局

```
~/m3v-final/Model3-Vulkan.v0.9.8.6/      # 本次验证用的包（artifact 解压 + Frameworks/ + roms 软链）
~/Supermodel-VK-0.9.2.4/                 # 用户旧版（对照）
  bin/supermodel                         # OpenGL 构建
  roms/                                  # 56 个 ROM set
```

`~/m3v-final/.../roms` 软链到 `~/Supermodel-VK-0.9.2.4/roms`，这样新包能直接列出真实 ROM。
`Data/Config/Model3-Vulkan.ini` 由程序自动生成。

### 1.3 旧版 `v0.9.2.4a` 的真实身份（重要）

旧版日志里写的是：

```
[Info]  OpenGL: settings window context is on "Intel(R) Iris(TM) Plus Graphics 655".
```

（这里的设备名同样是伪装结果，实际是 UHD 620，见 1.1.1。）

`bin/Config/Supermodel.ini`：

```
New3DEngine = 1
QuadRendering = 0        # 三角形几何，不是四边形
VSync = 0
Throttle = 1
XResolution = 992
YResolution = 768
RefreshRate = 60
```

也就是说：用户"以前能打开"的那个版本是 **SDL2 + OpenGL + New3D（三角形路径）**，和这次出问题的 Vulkan 路径没有任何关系。这就是"为什么旧版能开"的全部答案。

---

## 2. 启动失败（`VkResult -7`）的根因与修复

症状：v0.9.8.6 在 macOS 13.7.8 上弹 "Vulkan 1.3 required"，日志 `VkResult -7`。

`-7` 是 `VK_ERROR_EXTENSION_NOT_PRESENT`，**不是** `VK_ERROR_INCOMPATIBLE_DRIVER`（`-9`）。这一点是排查的关键，往"驱动太旧/显卡太弱"方向查会完全跑偏。

### 2.1 Bug 1：无条件请求 `VK_KHR_portability_enumeration`

- `VK_KHR_portability_enumeration` 是 **Vulkan loader 合成**的扩展，MoltenVK 自己不导出（对
  `libMoltenVK.dylib` 做 `strings` 计数为 0）。本构建通过 volk 直接 `dlopen` MoltenVK，
  没有 loader 来补这个扩展 → `vkCreateInstance` 直接 `-7`。
- 修法：`Src/OSD/SDL3/VulkanRequirement.cpp` 新增 `HasInstanceExtension()`，按需启用扩展并在
  需要时设置 `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`。
- 第二处：SDL3 的 Cocoa 后端**无条件**把该扩展名加进扩展列表，所以
  `Src/Graphics/Vulkan3D/VulkanContext.cpp` 的 `CreateInstance()` 现在会从 SDL 返回的列表里
  剔除这个名字，再由我们自己的逻辑决定加不加。

### 2.2 Bug 2：预检比渲染器严

- 渲染器 `ClassifyDevice()` 接受两种情况：**1.3 核心** 或 **1.2 + `VK_KHR_dynamic_rendering`
  + `VK_KHR_synchronization2`**。
- 预检只认 1.3，于是"渲染器能跑、预检先拒"。
- 修法：预检改成同一套判定（新增 `HasDeviceExtension()`），1.2 路径用 KHR 特性结构查询。

### 2.3 Bug 3：macOS 运行目录布局错

- `Src/OSD/OSX/FileSystemPath.cpp` 的 `GetPath()` 用 `<exe>/Config`，但发行包是
  `<exe>/Data/Config` → `Games.xml` / `Boxarts` / 存档目录全部找不到。
- 修法：有 `Data/` 时用 `Data/`，否则回退旧位置；默认 ROM 目录改用小写 `roms/`。

### 2.4 HostCore 部署目标

`HostCore` 的 `MACOSX_DEPLOYMENT_TARGET` 环境变量**无效**，必须用 .NET 的
`-p:AppleMinOSVersion=13.3`。Mach-O 已确认 `minos 13.3` 生效（但 Swift 符号问题另说，见第 4 节）。

---

## 3. 实机功能验证（v0.9.8.6，commit `b5dd7aa`）

### 3.1 启动与窗口

```
GUI: window created (96 ms).
GUI: settings file: .../Data/Config/Model3-Vulkan.ini
Vulkan: instance created (1 ms).
Vulkan: device created (5 ms).
Vulkan: using Intel(R) Iris(TM) Plus Graphics 655 (API 1.3, Vulkan 1.3 core path).
Vulkan: swapchain 1280x800, 3 images, present mode FIFO (vsync on).
Vulkan: settings window on Vulkan (1280x800, 3 swapchain image(s)).
GUI: interface ready (169 ms).
```

- `CGWindowListCopyWindowInfo`：窗口 1280×828 @ (320,82)，`onscreen=1`，`alpha=1`。
- 上面的 `Iris(TM) Plus Graphics 655` 是驱动报的伪装名，真实硬件是 UHD 620（见 1.1.1）。
- 没有任何 `-7`；`Data/` 布局生效后 `Games.xml`、`Boxarts`（63 个文件）都能找到。
- 备注：`screencapture` 在 SSH 会话下拍不到窗口，是**屏幕录制权限**问题，不是渲染问题。

### 3.2 设置界面与游戏列表

```
GUI: 56 ROM(s) listed, 0 title(s) translated for the current language,
     0 with no identifiable set.
```

`Identify()` 按 zip 内容（CRC）识别 set 的逻辑在 macOS 上同样正确。

### 3.3 进游戏（Daytona USA 2）

```
Game: 'roms/daytona2.zip' resolves to set 'daytona2' (Daytona USA 2 - Battle on the Edge, Japan, Revision A).
Vulkan: 3D target 1280x960, 1x MSAA.
Bezel: uploaded 2652x1492 (15827136 bytes) to the GPU.
Window: shown once the first frame had been presented.
Vulkan: textures - page 0 8.4% and page 1 95.8% of tiles have ever been uploaded.
Vulkan: textures - no stale tiles: every change to texture memory has been uploaded.
```

- 0 error / 0 warning（除 HostCore 一条，见第 4 节）。
- 音频正常打开并按设备声道折叠：`Audio: device 'default' has 2 channel(s) (48000 Hz);
  folding the quad mix down to 2.`

### 3.4 存档（`.st` 回退路径）

macOS 13 上 HostCore 加载失败，`.zst` 不可用，回退 `.st`：

```bash
SM3D_SAVE_STATE_FRAME=700 SM3D_SAVE_STATE_SLOT=1 ./Model3-Vulkan roms/daytona2.zip
```

```
[Info]  Save state: SM3D_SAVE_STATE_FRAME fired at emulated frame 700 (requested 700).
[Info]  Saved state to '.../Data/Saves/daytona2.st1'.
```

产物：`Data/Saves/daytona2.st1`，30,953,726 字节（30.9 MB）。

### 3.5 读档（延迟读档）

```bash
./Model3-Vulkan roms/daytona2.zip -load-state=Data/Saves/daytona2.st1
```

```
[Info]  Init state: deferring load of 'Data/Saves/daytona2.st1' until the game has started.
[Info]  Manage saves: loading deferred state 'Data/Saves/daytona2.st1'
        (PPC PC=00005424 LR=000999DC after 172 emulated frame(s)).
[Info]  Loaded state from 'Data/Saves/daytona2.st1' (PPC PC=0004C760, LR=0004C74C).
```

之后继续出帧（PACE `draws` 400+）。**macOS 13 用户存档/读档功能完整，只是容器是 `.st`。**

### 3.6 唯一的错误

```
[Error] HostCore: libModel3VulkanHostCore.dylib could not be loaded:
        dlopen(.../libModel3VulkanHostCore.dylib, 0x0006): Symbol not found:
        _swift_getTypeByMangledNameInContext2
```

---

## 4. HostCore 与 `.zst` 的系统版本门槛

### 4.1 依赖链

`otool -L libModel3VulkanHostCore.dylib`：

```
/usr/lib/libSystem.B.dylib
/usr/lib/libobjc.A.dylib
/usr/lib/swift/libswiftCore.dylib          <-- 硬链接
/usr/lib/swift/libswiftFoundation.dylib    <-- 硬链接
/usr/lib/libicucore.A.dylib
/System/Library/Frameworks/CoreFoundation.framework/...
/System/Library/Frameworks/CryptoKit.framework/...     <-- Swift 框架
/System/Library/Frameworks/Foundation.framework/...
/System/Library/Frameworks/Network.framework/...
/System/Library/Frameworks/Security.framework/...
/System/Library/Frameworks/GSS.framework/...
...（其余 swift 子库为 weak）
```

HostCore 是 **.NET 11 Native AOT** 产物；.NET 的 Apple 加密后端是 Swift 实现的，因此整个
dylib 硬链接了 Swift 运行时。它用到的 `swift_getTypeByMangledNameInContext2` 在 macOS 13
的 `libswiftCore` 里不存在 → dyld 直接拒绝加载整个模块。

### 4.2 版本分界（证据）

用 `gh api` 取 swiftlang/swift 两个 release 分支的 `stdlib/public/runtime/MetadataLookup.cpp`：

| 分支 | `swift_getTypeByMangledNameInContext2` 出现次数 |
| --- | --- |
| `release/5.8` | **0** |
| `release/5.9` | **1** |

配合本机实测（`dlsym` 返回 NULL、dyld 报错），结论：该符号随 **Swift 5.9** 进入运行时，
而 **macOS 14 (Sonoma)** 才带 Swift 5.9。

### 4.3 结论与发布说明措辞

> **`.zst` 存档容器与 `.dds.zst` 高清贴图：macOS 14 (Sonoma) 及以上可用。
> macOS 13.x 自动回退 `.st`（游戏内存档、F5/F7、`-load-state` 全部正常），只是不压缩。**

不是"所有 Mac 用户都用不了"。同一份二进制在 14+ 上走 `.zst`，在 13.x 上走 `.st`。

### 4.4 可能的彻底修法（未实施）

1. 用更老的 .NET SDK / 更老的 macOS SDK 发布 HostCore（Apple 加密后端用旧 Swift 编的版本），
   代价是要在 macOS 13 环境里能装 .NET 11 SDK。
2. 出版一个不链 CoreCrypto/Swift 的 HostCore 变体（裁剪 crypto 依赖），不确定 Native AOT
   是否允许。
3. 接受现状 + 把 HostCore 失败解释成人话（**本次未做，v0.9.8.6m 仍打印 `[Error]`**），例如：
   `HostCore 需要 macOS 14+ 才能提供 .zst 容器；本次使用 .st，功能不受影响。`

---

## 5. 性能对比（本次主测）

### 5.1 测量方法（可复现）

两边都用**窗口标题栏的 1 秒滑动 FPS**，这样 Vulkan 与 OpenGL 用的是同一个口径：

```bash
# 1) 启动（位置参数必须是 zip 路径，不是 set 名）
nohup ./Model3-Vulkan roms/daytona2.zip >/dev/null 2>&1 &

# 2) 每 15 秒读一次标题（需要辅助功能权限；本机 SSH 会话已有）
P=$(pgrep -f "Model3-Vulkan roms")
osascript -e "tell application \"System Events\" to get title of every window of (first process whose unix id is $P)"
```

标题形如：

```
Model 3-Vulkan v0.9.8.6 - Daytona USA 2 - Battle on the Edge (Japan, Revision A) - 21.257 FPS [audio u2/o0]
```

采样协议：启动后先跑 **25 秒**热身（跳过载入/开机画面），然后每 15 秒取一次，共 3 次；
PACE 行取同一时间窗的最后两行。旧版用同样协议（旧版没有 PACE，只有标题栏）。

Vulkan 侧另外记录日志里的 `PACE:` 行，它把一帧拆成：

```
PACE: logic 20.43 Hz, present 20.47 Hz, target 57.52 Hz, alpha 1.000,
      sim 1687, presents 1689, dropped 1301, sim 45.55 ms/frame, render 5.55 ms/frame,
      draws 876, cuts 117, lead 0.00 ms.
```

- `logic` = 实际仿真帧率
- `sim` = 一帧仿真步的墙钟时间（含等待上一帧呈现）
- `render` = 本帧命令录制耗时
- `draws` = 本帧 3D 绘制调用数

### 5.2 "核显平衡（992×768，2x 原生）"预设写入了什么

`Src/OSD/SDL/Gui.cpp` 的 `QualityTier tiers[]` + 应用代码：

```
XResolution = 992          YResolution = 768
MSAA = 0                   AlphaSortMode = 0        TexFilter = 0
InternalResolution = 2     IntegerScale = true
RefreshRateMode = 0        MotionInterpolation = false
LowLatency = true          UiFrostedGlass = false
QuadInterpolation = false  PostFXAA = false
PostCRT = 0                PostScanlines = 0        PostGlow = 0
```

即：3D 场景固定 992×768 渲染再等比放大、无 MSAA、原版半透明、径向单 tap 贴图、
关插值、关后处理、关毛玻璃、关精确四边形。

### 5.3 主数据表

> **更正（2026-10-30 晚）**：下表里旧 0.9.2.4a 的 `59.1 / 45.5 / 37.1 / 42.9` 是在另一次
> 会话的**开机 / 片头段**读到的，和 Vulkan 的采样点（热身 25 秒之后）不对齐。用完全相同的
> 协议（启动后 t=12/27/42/57 秒各读一次标题栏）复测：
>
> | 构建 | t=12 s | t=27 s | t=42 s | t=57 s |
> | --- | --- | --- | --- | --- |
> | 旧 0.9.2.4a（OpenGL / New3D，992×768） | 2.98 | 14.82 | 15.91 | 20.93 |
> | 新 LegacyOG（SDL2 + OpenGL，本机编译，992×768） | 3.79 | 11.01 | 11.44 | 10.70 |
>
> 即两者在**同一区间（11–21 FPS）**，旧版略快但远不到 2 倍。这台黑苹果的 run-to-run 波动
> 可达 4 倍，**跨会话对比不可用**；只有同一次运行内的对照（分辨率 ↔ 帧率）成立。

所有读数均为同一台机器、同一 ROM。

| 构建 | 3D 分辨率 | 其它 | 标题栏 FPS / logic Hz | sim / render (ms) |
| --- | --- | --- | --- | --- |
| ~~旧 0.9.2.4a OpenGL~~（采样点不对齐，见上方更正） | 992×768 | `VSync=0`、`QuadRendering=0`、`RefreshRate=60` | 59.1 / 45.5 / 37.1 / 42.9 | （由当时 FPS 反推 17–27 ms） |
| 旧 0.9.2.4a OpenGL | 992×768 | `top` 采样 | CPU 102.4% → 104.0% | — |
| 新 Vulkan | 992×768 | 预设（FIFO、bezel+统计开） | 46.6 / 21.3 / 11.9 | 21–68 / 4–18 |
| 新 Vulkan | 992×768 | `VSync=0`（IMMEDIATE）、bezel+统计开 | 48.5 / 14.5 / 27.6 | — |
| 新 Vulkan | 992×768 | `VSync=0`、bezel+统计**关** | 13.8 / 32.4 / 11.8 | 44.6 / 46.0，render 3.6–11.1 |
| **新 Vulkan** | **496×384** | 预设（`InternalResolution=1`） | **38.7 / 42.2 / 34.2 / 36.5**（连续 70 秒稳定） | 22.0–36.3 / 2.5–6.4 |
| 新 Vulkan | 1280×960（默认配置，Auto→60 Hz 目标） | 开机段 | 39.3 / 38.9 / 42.0（draws 0） | 24–32 / 0.8–1.7 |
| 新 Vulkan | 1280×960 | 吸引循环 | 8.7 / 13.1 / 10.9 / 13.9 | 68–105 / 6.8–9.5 |

> 说明：FPS 是 1 秒窗口的瞬时值，同一轮里随场景变化。旧版那 4 个读数也是同一轮里随时间下降，
> 所以两边都受场景内容影响；看**中位趋势**而不是单个峰值。

### 5.4 分解实验

| 变量 | 改动 | 结果 | 判断 |
| --- | --- | --- | --- |
| 3D 像素数 | 992×768 → 496×384（像素数 ÷4） | 20 Hz → 35–42 Hz，`sim` 45 → 22 ms | **主导因素**，成本按填充率走 |
| bezel 背景 | `BezelMode=1 → 0` | 13.8 / 32.4 / 11.8，无改善 | 不是原因 |
| 统计面板 | `ShowStatsOsd=1 → 0` | 同上，无改善 | 不是原因 |
| 垂直同步 | FIFO → IMMEDIATE | 48.5 / 14.5 / 27.6，仍在同一区间 | 不是原因 |
| 热节流 | `pmset -g therm` | `CPU_Speed_Limit = 100` | 不是原因 |

**结论：不是叠加功能造成的，是每像素着色成本。**

### 5.5 采样分析（`sample` 3 秒）

992×768、`draws ≈ 550` 时，按栈顶聚合（数字为样本数）：

```
__psynch_cvwait                                    9256    <- 空闲线程等待
__workq_kernreturn                                 6091
mach_msg2_trap                                     5782
semaphore_wait_trap                                3858
ppc_execute(int)                                    577    <- PPC 解释器
ppc_bcx(unsigned int)                               296
ppc_lbz(unsigned int)                               136
CModel3::Read8(unsigned int)                        119
ppc_addis(unsigned int)                             114
ppc_cmpl(unsigned int)                              102
CTileGen::DrawLine(int)                              94
CModel3::Read32(unsigned int)                        48
Vulkan3D::CVulkanTextureCache::VerifyDirtyCoverage   47
_platform_memmove$VARIANT$Haswell                    30
```

要点：

- **主线程的热点是 PPC 解释器**（`ppc_execute` 系列），不是渲染命令录制 —— 与 `render` 只有
  几毫秒吻合。
- `sim` 随分辨率变化说明它包含**等待上一帧 GPU 完成/呈现**的时间；所以"CPU 采样热点"和
  "sim 随像素数增长"并不矛盾：一个在 CPU，一个在等 GPU。
- `VerifyDirtyCoverage` 出现在画像里但占比很小（47/约 25k），暂不作为优化目标，记在这里备查。

### 5.6 与旧 OpenGL 版的结构差异（不能据此断言谁更快）

| | 旧 0.9.2.4a（OpenGL / New3D） | 新 Vulkan |
| --- | --- | --- |
| 图元 | `QuadRendering=0` → **三角形** | **四边形**（每图元固定 4 槽位） |
| 贴图采样 | 硬件 sampler（硬件双线性、硬件 mip、各向异性） | **手工解码贴图页 + `texelFetch`**，自己算双线性，`TexFilter=0` 时按径向估 mip |
| 曲面 | 光栅化三角形自带插值 | 片元里做**二次曲面求交**（`QuadInterpolation=0` 时才跳过） |
| 透明 | 固定管线混合 | 两个透明附件 + 双 Subpass 片上合成 |
| 每帧 CPU | 高（单核打满，New3D 也重） | 高（PPC 解释器为主） |

在 24 EU 的 UHD 620 上（被伪装成 Iris Plus 655），“每片元手工解码贴图 + 4 次 `texelFetch` +
手工双线性”的单像素成本确实高于硬件采样；桌面独显（开发机 NVIDIA）上这部分被淹没在带宽里。

但这**不**意味着本机上 Vulkan 就比 OpenGL 慢：用同一协议复测时两者都在 11–21 FPS，差异被
PPC 解释器和整机波动淹没（见 5.3 的更正）。真正能确认的只有分辨率相关性。

**仍然成立的是分辨率相关性**：像素数 ×4，`sim` 从 22 ms 涨到 45 ms。

### 5.7 对玩家的实际含义（这台机器）

| 场景 | 建议 |
| --- | --- |
| macOS + Intel 核显 | 用「超低配极速（496×384）」或 `InternalResolution=1`；同一次运行内约 35–42 Hz |
| macOS + Intel 核显 + 992×768 | 约 11–21 FPS，随场景大幅抖动，不建议 |
| 旧 OpenGL 包（0.9.2.4a） | 同一协议下 15–21 FPS，与本机编译的 LegacyOG（11 FPS）同档；功能落后几个月 |

> 这些绝对数字来自 **24 EU / 无 eDRAM / 共享内存** 的 UHD 620，是 Intel 核显里最弱的一档；
> 真正的 Iris Plus 655（48 EU + 128 MB eDRAM）或其他独显上会明显更快（**未实测**）。
> 另外本机核显被黑苹果伪装成 Iris Plus 655，macOS 加载的是该型号的 framebuffer 配置，
> 对性能的影响未单独剥离。

> 注：`motion interpolation` 在这台机器上无意义 —— 逻辑帧本来就只有 20–40 Hz。

### 5.8 未验证：Apple Silicon（M 系列）

> **本节没有任何实测数据。** 只记录推理、风险与测量清单，**不能**作为发布说明的依据。

**为什么不能直接外推**

- 本次瓶颈是“每片元手工解码贴图 + 4 次 `texelFetch` + 手工双线性 +（可选）二次曲面求交”。
  这类成本在 UHD 620（24 EU Gen9.5，纸面约 0.42 TFLOPS、共享内存带宽 ~35–38 GB/s）
  上被放大；M1 的 8 核 GPU 纸面约 2.6 TFLOPS、统一内存 ~68 GB/s，理论上会明显更快。
- 但**不能按 TFLOPS 线性外推**，因为 Apple GPU 是 **TBDR**（tile-based deferred rendering）：
  - 我们的场景带 `discard`（alpha test）与高 overdraw，`discard` 会关闭 TBDR 的
    hidden-surface removal —— 这是 TBDR 的典型弱项，可能抵消一部分算力优势。
  - 三个颜色附件 + 双 Subpass 片上合成的带宽特性与 IMR 不同，不能沿用 Intel 上的结论。
- CPU 侧：PPC 解释器是单核性能敏感的；M1/M2 的单核 IPC 与频率都远高于 i5-8365U，逻辑帧率
  上限大概率更高 —— 但这与 GPU 路径无关，不能说明"Vulkan 快"。
- 另外要澄清：M 芯片上的"Vulkan"是 **MoltenVK → Metal 的翻译层**，不是原生后端；Apple 平台的
  原生图形 API 是 Metal。OpenGL 在 Apple Silicon 上是已废弃的兼容路径（最高 4.1）。因此第 6.2 节
  的 OpenGL 包定位应为**老 Intel Mac**，不应把它当作 M 芯片的方案。

**测量清单（任何一台 M 芯片机器都能跑，约 20 分钟）**

1. 用同一份包，`Data/Config/Model3-Vulkan.ini` 套第 5.2 节的 992×768 预设。
2. `./Model3-Vulkan roms/daytona2.zip`，热身 25 秒。
3. 每 15 秒读一次标题栏 FPS（脚本见 7.3），共 3 次。
4. 记录同一时间窗的 `PACE:` 行（`logic` / `sim` / `render` / `draws`）。
5. 把 `InternalResolution` 改成 1（496×384），重复 2–4。
6. 记录机型、芯片、统一内存大小、macOS 版本、MoltenVK 版本。

**判据（决定发布说明里能不能写 "Apple Silicon 表现更好"）**

- 992×768 下 `sim` ≤ 20 ms（对 57.5 Hz 仍有裕量），或
- logic Hz 稳定 ≥ 50。

达不到就必须维持现在的措辞：只写已验证的事实（macOS 13 可运行、`.zst` 需 macOS 14+、
Intel 核显建议 496×384），**不写任何 M 芯片的性能承诺**。

---

## 6. 后续工作（两项都做）

### 6.1 任务 2：Vulkan 片元着色器"硬件采样"快路径

**目标**：让常见纹理格式走真 sampler（硬件双线性 + 硬件 mip + 各向异性），只在必要时回退
到手工解码。目标是在这台 UHD 620 上把 992×768 的 `sim` 从 45 ms 压到 25 ms 量级，
即 496×384 与 992×768 的差距收敛到 1.2 倍以内。

**范围草案**

1. 先量化：给 `DrawBucket()` 增加可选计时（仅诊断构建/环境变量），确认 3D 场景 pass 的
   GPU 时间随像素数线性增长。
2. 识别可硬件采样的子集：整页 8-bit 调色板贴图（无微纹理、无页内 wrap 偏移、无 4-bit 打包）
   → 生成/复用一个 `sampler2D`（或 `sampler2DArray`）路径，`textureGrad` + 硬件 mip。
3. 保留现有手工路径作为 fallback，用**特化常量**选择（不要运行时分支——本项目已有教训：
   两个 binding 都可达时驱动可能把加载提到分支外）。
4. 验收：
   - Vulkan 同步验证 / GPU 辅助验证各跑一次（`Tools/Validate-Vulkan.ps1`）；
   - 逐像素 A/B（同 `-Frame`、`RefreshRateMode=0`）不得出现新差异；
   - 重点回归 Daytona 2 天空（`sumA == 0` 黑块）、VF3 海滩（透明层）、HD 平铺边界接缝。

**风险**：贴图页 wrap/微纹理/调色板语义一旦做错，表现是"某些材质变黑或错色"，而且只在特定
游戏出现。参考 `AGENTS.md` 里关于 `taps > 1`、`plainRgb` 回退、`fract == 0` 的既有约束，
新路径必须保持同样的边界行为。

### 6.2 任务 3：macOS OpenGL / Legacy 包 —— **已取消（2026-10-30 决定）**

> 决定：**macOS 包不再提供传统 OpenGL 版本**。个人精力和设备有限，无法同时维护三大平台 ×
> 两套渲染后端（6 个构建）并逐个实测；`Supermodel-LegacyOG` 仍只在 Windows 提供。
> 发布说明里已写明这一点。

保留记录（构建方式已打通，将来若要恢复不必重新摸索）：

- CMake 的 `Supermodel-LegacyOG` 目标（SDL2 + OpenGL + `SUPERMODEL_OPENGL_ONLY`，C++17）
  在本机 Apple clang 14 上能编过并运行：`-DUSE_SDL2=ON -DUSE_SDL3=OFF`，SDL2 / SDL2_net
  从 `Libraries/` 源码编。
- 需要两处修复（**未提交**，只在本机工作树）：
  1. `CMakeLists.txt`：`if(USE_SDL2 AND APPLE) enable_language(OBJC)` —— SDL2 不启用 OBJC，
     CMake 4 用 Objective-C++ 规则编 `.m` 并注入 C++ 标准，clang 报
     `invalid argument '-std=c++17' not allowed with 'Objective-C'`。
  2. `Src/OSD/SDL/SDLIncludes.h`：用 `__has_include` 在框架布局（`<SDL2/SDL.h>`）与源码布局
     （`<SDL.h>`）之间选择。
- 同协议下 LegacyOG（11 FPS）与旧 0.9.2.4a（15–21 FPS）**同档**，不再有“明显更快”的理由。
- 另一个待查项：`Model3.cpp` 里 `RefreshRateMode != 0` 会强制 `syncSndBrdThread = true`，
  这条判断只该管 Vulkan 高刷路径；OpenGL 版没有那个路径，实测音频 overrun 持续累积
  （新构建 `o12 → o90`，旧版稳定 `o1`）。若将来恢复 LegacyOG，应加
  `#if !defined(SUPERMODEL_OPENGL_ONLY)` 守卫，并让 `supermodel_core` 也拿到该宏。

### 6.3 实际排期

1. **v0.9.8.6m（仅 macOS，已发）**：三个 macOS 修复 + `Data/` 布局；`.zst` 需 macOS 14+、
   13.x 用 `.st`；核显建议 496×384；发布说明明确 “for macOS only” 与 “macOS 不再提供 OpenGL 版”。
   Windows / Linux 保持 0.9.8.6 不动。
2. 任务 3（macOS OpenGL 包）**取消**，理由见 6.2。
3. 任务 2（硬件采样快路径）仍值得做 —— 影响面最大（Windows 低端核显同样受益），需要完整回归。

---

## 7. 复现步骤与坑

### 7.1 在 Mac 上跑一次

```bash
cd ~/m3v-final/Model3-Vulkan.v0.9.8.6
./Model3-Vulkan                      # 设置界面
./Model3-Vulkan roms/daytona2.zip    # 直接进游戏
./Model3-Vulkan roms/daytona2.zip -load-state=Data/Saves/daytona2.st1
```

### 7.2 坑（今天踩过的）

- **位置参数是文件路径**，不是 set 名：`daytona2` 会报 `Could not open 'daytona2'`，
  要写 `roms/daytona2.zip`。
- **`-load-state` 只接受 `=` 形式**：`-load-state X` 会报 `'-load-state' requires an argument.`
- **SSH 下 `screencapture` 拍不到窗口**（屏幕录制权限），判断窗口是否真的打开用
  `CGWindowListCopyWindowInfo`；读标题栏 FPS 用 `osascript ... System Events`（辅助功能权限）。
- **`otool` / `nm` 不在 SSH 的 PATH 里**（CLT 工具），用 `xcrun -f otool` 取绝对路径。
- 改启动图/资源后不需要重编译，但要把 `Assets/` 同步到包里。

### 7.3 测量脚本片段

```bash
# 采样
P=$(pgrep -f "Model3-Vulkan roms")
sample $P 3 -file /tmp/m3v.sample
sed -n "/Sort by top of stack/,/Binary Images/p" /tmp/m3v.sample | head -30

# 标题栏 FPS（每 15 秒一次）
for i in 1 2 3; do
  osascript -e "tell application \"System Events\" to get title of every window of (first process whose unix id is $P)"
  sleep 15
done

# PACE
grep "PACE:" Supermodel.log | tail -3
```

---

## 8. 附录

### 8.1 本次相关的源码改动（已在 master）

| 文件 | 改动 |
| --- | --- |
| `Src/OSD/SDL3/VulkanRequirement.cpp` | 新增 `HasInstanceExtension()` / `HasDeviceExtension()`；预检与渲染器同一套判定（1.3 或 1.2+KHR 扩展） |
| `Src/Graphics/Vulkan3D/VulkanContext.cpp` | `CreateInstance()` 从 SDL 扩展列表中剔除 `VK_KHR_portability_enumeration`，按需自行启用 |
| `Src/OSD/OSX/FileSystemPath.cpp` | 有 `Data/` 时用 `Data/`，否则回退旧位置；默认 ROM 目录 `roms/` |
| `HostCore/Model3VulkanHostCore.csproj`（发布参数） | 使用 `-p:AppleMinOSVersion=13.3`，不要用 `MACOSX_DEPLOYMENT_TARGET` |

### 8.2 遗留 / 待清理

- `v9.8.6_source/Src/Graphics/Vulkan3D/VulkanRequirement.cpp` 是一份**未被引用**的早期副本
  （真正生效的是 `Src/OSD/SDL3/VulkanRequirement.cpp`），未纳入 CMake 显式源文件清单，暂不影响构建。
- `Supermodel.log` 里的 HostCore `[Error]` 在 macOS 13 上会一直出现；v0.9.8.6m **未改**，仍是原文。

### 8.3 一句话给发布页

> macOS 13+ 可以运行 Model 3-Vulkan（Vulkan 1.3 via MoltenVK）。`.zst` 存档容器需要 macOS 14+；
> 13.x 会自动使用 `.st`。Intel 核显建议使用「超低配极速 496×384」预设。
