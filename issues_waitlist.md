# Issues Waitlist — 暂缓问题

> 这里记录**已确认、暂不优先处理**的问题，以及**已决定延后做的验证/文档待办**。
> 每条至少包含：症状、复现、已排除项、现有证据、下一步定位方向。
> 问题修复后把结论写进 `devdocs/CHANGELOG.md`，并从本文件删除；待办完成后同样删除。
> 本文件不承诺版本号或修复时间。

## 2026-11-07 — 下一版发版前：把存档默认延迟从 3 秒改为 0（或明确保留）

**状态**：已决定延后。v0.9.9.6 仍以 `kDeferredLoadSeconds = 3.0`（172 个仿真帧）发布；
根因已修完（见 `saves_fix.md`），但全游戏矩阵还没跑，所以这一版**不动默认值**。
**提醒**：下一版打包前先处理本节；`devdocs/RUNBOOK-RELEASE-LINUX-MACOS.md` §1 的
发版前清单里也列了这条。

**为什么可以改**：`53C810 PCI`（`baseAddress`/`halt`）、`SimNetBoard` 握手与双 bank、
`IRQ.RefreshInterruptLine()`、`SCSP_RefreshIRQ()`、DSB `MPEG` 活跃窗口块、worker 完成标志
都已进存档。实测 `SM3D_DEFER_LOAD_SECONDS=0` 时 `scuddx` / `vf3tb` / `daytona2` 第一帧
即时读档正常。

**矩阵**（全部设 `SM3D_DEFER_LOAD_SECONDS=0`，并加 `RefreshRateMode=0` 便于同帧比较）：

| 类别 | set |
| --- | --- |
| 赛车 | `scuddx`、`scud`、`scudau`、`daytona2`、`dayto2pe`、`srally2`、`lemans24`、`dirtdvls` |
| 格斗 | `vf3tb`、`vf3`、`fvipers2`、`vs2`、`vs215` |
| 动作 / 射击 | `lostwsgp`、`oceanhun`、`swtrilgy`、`harley` |
| 体育 / 其他 | `getbass`、`magtruck`、`eca`、`von2` |

每个 set 至少覆盖：① 旧档（无新块）② 新档（含新块）③ 赛道/比赛中存的档。
读档入口：`Manage Saves → 读取` 与 `-load-state`（这两条走 deferred 路径，是本节要测的）；
F7 本来就是即时路径，可作对照。**不要在载入/转场/比分画面存的档上判失败**——那种档本来
就可能停在等待循环（见 `AGENTS.md`「存档不是所有时刻都能恢复」）。

**判定**：

- 第一帧之后 PPC 正常推进（不是停在 boot ROM，也不是 1/25 速度）；
- 没有 `NETWORK CHECKING` 重启、没有「Please wait」卡死、没有「有音乐没音效」；
- 同一存档读两次，同一仿真帧截图一致（`Tools/Capture-Frame.ps1 -Frame <n>`）。

**若通过**：

1. `Src/OSD/SDL/Main.cpp` 的 `kDeferredLoadSeconds` 改为 `0.0`，并重写它上面的注释
   （现在整段都在解释为什么需要 3 秒）；
2. 同步 `saves_fix.md` §4.4、`AGENTS.md`「读档延迟是兜底」段、
   `usermanual.md`（§4.7 / §9 / §11 的 3-second 表述）与 `usermanual_chs.md`（§3 / §6）；
   `RELEASE_NOTES.md` 写明默认值变化；
3. 日志里 `loading deferred state ... after N emulated frame(s)` 在默认路径下应显示
   `after 0 emulated frame(s)`；
4. 重新打包（Windows `build.bat -package`，Linux/macOS 按 runbook），冒烟后再发。

**若某个 set 不通过**：记下 set 名、存档画面与日志里的 `PPC PC` / `mpeg=` / `sndDone`，
保留 3 秒；在 `saves_fix.md` 里列成已知限制，不要为了改默认值去动存档逻辑。

**回滚**：只改 `Main.cpp` 一个常量，改回 `3.0` 重新打包即可；存档格式不受影响。

## 2026-11-04 — 待办功能（下一版本）：Vulkan 1.1 兼容性下探（扩大旧核显与老驱动兼容面）

**状态**：已评估可行，留待下一版本实现。
**背景**：`Supermodel-LegacyOG.exe`（OpenGL 版）已轻量退役，目前 `Model3-Vulkan.exe` 要求 Vulkan 1.3 或「Vulkan 1.2 + `VK_KHR_dynamic_rendering` + `VK_KHR_synchronization2` + `descriptorIndexing`」。实际审计代码发现高于 Vulkan 1.1 的依赖点仅有 4 处，均可通过轻量兼容层无损下探至原生 **Vulkan 1.1**（覆盖 Intel Skylake/Kaby Lake HD 500/600 系列核显、旧 NVIDIA Kepler/Maxwell、早期 AMD GCN 及旧版驱动），同时在 Vulkan 1.2/1.3 现代显卡上保持零开销路径。

**四处改造切入点（实现方案已就绪）**：

1. **实例与设备分级路由（`Src/OSD/SDL3/VulkanPreflight.cpp`、`Src/Graphics/Vulkan3D/VulkanContext.cpp`）**：
   - `CreateInstance` / `ProbeVulkan13` 通过 `vkEnumerateInstanceVersion` 按设备能力请求 `1.3` / `1.2` / `1.1`（最低接受 `VK_API_VERSION_1_1`）。
   - `BackendRoute` 新增 `Vulkan11Legacy` 分支（仅要求 `independentBlend`、`samplerAnisotropy` 与 `maxPushConstantsSize >= 128`）；当设备不支持 `VK_EXT_descriptor_indexing` 时将 `m_supportsDescriptorIndexing` 置为 `false`，自动关闭 HD 纹理替换并将 Set 2 绑定为 1-slot dummy sampler。
2. **`synchronization2` 到 Vulkan 1.1 的内联自动翻译（`Src/Graphics/Vulkan3D/VulkanCommon.h`）**：
   - 在 `CmdPipelineBarrier2()` 与 `QueueSubmit2()` 包装函数内，当 1.3/KHR 函数指针为空时，将 `VkDependencyInfo` / `VkSubmitInfo2` 自动折叠转换为 Vulkan 1.0/1.1 的 `vkCmdPipelineBarrier` 与 `vkQueueSubmit`，全工程 20 处调用点零改动。
3. **无 `VK_KHR_dynamic_rendering` 设备的轻量 `VkRenderPass` + `VkFramebuffer` 缓存回退**：
   - 3D 场景默认路径（`AlphaSortMode = 0` 的 2-subpass `m_scenePass`）本身已是传统 `VkRenderPass`；不支持 `dynamic_rendering` 时将 `AlphaSortMode = 1` 自动回退至 `0`，并为 `Vulkan2D`（composite/post/bezel/crosshair/border/overlay）与 `UiBlur` 提供按附件格式键控的单 subpass `VkRenderPass` + `VkFramebuffer` 缓存。
4. **SPIR-V 1.3 着色器目标（`Scripts/CompileVulkanShaders.cmd` / `.sh`）**：
   - 通用着色器改用 `--target-env vulkan1.1`（生成 SPIR-V 1.3）；对含 `GL_EXT_nonuniform_qualifier` 的 `real3d.frag` / `real3d_oit.frag` 提供无 bindless 的 Vulkan 1.1 变体。
   - 验证钩子：新增 `SM3D_VK_FORCE_11=1` 强制走 Vulkan 1.1 Legacy 路径并用 `Tools/Capture-Frame.ps1` 做同帧逐像素 A/B 比对。

## 2026-11-01 — 待办：macOS Retina 上 4K 修复的真机复测

**状态**：代码已改（渲染几何改用设备像素、游戏窗口申请高密度 drawable、游戏内面板的
`DisplayFramebufferScale` 改真实比例），结论写在 `devdocs/CHANGELOG.md` 同日「macOS Retina 屏上
4K 设置无效」。本机无 Mac，**未在真机验证**；复测完成或拿到用户反馈后删除本节。

**要检查三件事**：

1. `Data/Supermodel.log` 出现 `Display N: ... 1920x1080 @60 Hz  (2x: 3840x2160 pixels)`；
2. 系统缩放为「默认字号」时，游戏会话的 swapchain extent 仍是 **3840×2160**，统计面板的
   `Render` 行是 4:3 画框（4K 下 2880×2160），而不是 1440×1080；
3. 游戏内 Tab 面板、统计浮层、准星的位置与大小正确（面板原先写死 1.0，现在会按 2 投影）。

**若仍是 1080p**：先看日志的 `Display` 行有没有 `(2x: ...)`。没有 → `pixel_density` 没读到；
有但 swapchain 仍是 1920×1080 → Cocoa 没给高密度 drawable（检查 `WindowFlag_HighDPI` 是否真的
进了 `desc.flags`）。

> 已知边界：窗口隐藏创建时 Cocoa 可能仍报 1x drawable（`GetRenderSize()` 用「桌面模式 ×
> pixelDensity」兜底）；独占固定 57/58 Hz 模式的匹配仍按 point 比较（macOS 上本来没有这种模式）。

## 2026-10-24 — Scud Race Deluxe（scuddx / scudj）HUD 元素缺失与灰板

**状态**：暂缓（本轮不修）。**Twin/DX（`scud`）一直都能正常显示，症状只在 Deluxe 上可见。**
**影响**：`scuddx`（Export, Deluxe, Revision A）。`scudj.zip` 的 CROM 与 `scuddx`
完全相同，实际也是 Deluxe，不是独立的 Japan 版本。
**未验证**：`scuddxo`（Export, Deluxe 旧 revision），ROM 目录里没有对应 zip。

> 用户补充：**这个问题从 MAME 时代起就没有解决过**——即它不是本分支的回归，而是
> Supermodel / MAME 一脉相承的模拟缺口。

### 2026-11-01 进展（双后端 + 长局 dump）

- **ROM 排除**：`Config/Games.xml` 的 CRC32 逐文件校验，`scuddx.zip` 41/41、父包
  `scud.zip` 41/41 全部匹配，0 mismatch。坏 dump 假设排除。
- **两个后端同样**：同一份 raw state、同一时刻，Vulkan 与 LegacyOG 的灰板**逐像素一致**
  （第 4 块 HUD 视口 mean (111,119,144)、32×32 块标准差中位数 14.7，两边完全相同）。
- **CONDITION 视口不是成因**：`SM3D_HUD_DEBUG=1`（`R3DSceneBuilder::DumpHudViewport`）
  在 40 秒长局里统计 `CONDITION` 视口（`159.75x160@(336,192)`）：

  | 版本 | 模型数 | 其中贴图多边形 |
  | --- | --- | --- |
  | `scuddx` | 19 | **0** |
  | `scud` | 15 | **0** |
  | `scudau` | 19 | **0** |

  三个版本都是 0，但 `scud`（Twin）一直都能正常显示，所以「这个视口没有贴图多边形」**不是**灰板的
  成因，只能说明地图不走这条路径。速度表视口则确实有贴图（fmt=7 的 32×32 数字图集 + fmt=0
  小图标）。
- **TileGen 也不是（至少存档时刻）**：`Scripts/render_tilegen_state.py` 离线重建存档里的
  Tile Generator 块，`scuddx` / `scudau` 两层都是 0 个不透明像素；`scud.zst1` 的 layer 1 有
  17,577 像素（y=192..239 的通栏色带，不是 CONDITION 面板矩形）。两份存档不是同一时刻，
  只能当线索——脚本的越界 bug 本轮已修（右边缘运行会写到 x=503，C++ 行缓冲是 512 宽）。
- **Deluxe 与 Twin 提交的 HUD 多边形几乎相同**（状态 + 面颜色逐条对比，只有两个无贴图
  多边形的颜色不同），所以差异不在「提交了哪些多边形」。
- 结论：地图 / 排位文字不在两条已知路径（3D 贴图多边形 / TileGen 2D）上，至少不在存档时刻。
  继续定位需要「同一仿真帧下 Twin 与 Deluxe 的 TileGen 层 + 画面」对照，而不是两份相差几十秒
  的存档。本轮按用户口径**收尾：不修，给绕过方案**。
- 给 issue #16 的回复口径：不是坏 dump（CRC 全对）→ 不是 Vulkan 特有（New3D 同帧逐像素
  一致）→ 只在 Deluxe 上出现，Twin/DX 正常 → 从 MAME 时代就没解决，请改玩 Twin/DX。
- 证据、命令与截图：`sdl3_reports/2026-11-01-scuddx-hud/`（新增 `twin-slot1.png`、
  `scud-raw.st`、`tilegen-twin1-layer{0,1}.png`）。

### 症状

- 比赛中，后视镜/速度表区域、POSITION、CONDITION、Game Over 字体等 HUD 元素显示异常。
- 具体表现：速度表只显示数字，圆盘/表盘缺失；POSITION 底板变成均匀灰块；
  CONDITION 地图缺失；部分数字颜色不对（蓝紫色而不是金色）。
- 灰块位置固定，不随场景移动。
- 用户报告后视镜、排位/时间显示、Game Over 字体三项异常；前两项已用存档复现，
  Game Over 画面尚未单独截图验证。

### 复现

```text
# 当前构建，加载 2026-10-24 09:07 的存档
Tools/Capture-Frame.ps1 -Set scuddx -Slot 1 -At 3

# 无存档全新启动，跑到 attract demo（约 70 秒）同样复现
Tools/Capture-Frame.ps1 -Set scuddx -NoState -At 70
```

- 同一份 raw state 用 OpenGL LegacyOG 也复现。
- 旧构建（2026-10-17，`202610_archived/root-temps/TempBossTest/Supermodel-VK.exe`）
  也复现。
- 因此不是存档内容、不是最近 Vulkan 改动，也不是 Vulkan 独有问题。

### 已排除

| 假设 | 结果 |
| --- | --- |
| 存档损坏 / 读档不完整 | 无存档 attract demo 也复现；存档只是记录错误画面 |
| 最近 quad interpolation / TexFilter / Daytona 2 黑块修复 | 10-17 旧构建同样错误 |
| `TexFilter` 模式 | 0 和 2 画面一致 |
| 内部分辨率 / 整数缩放 | 1488×1152 与 3840×2160 一致 |
| Tilegen 2D 层 | `scuddx` / `scudau` 存档时刻两层都是 0 不透明像素；`scud` 的 layer 1 有 17,577 像素但不同时刻，两者都不能单独定性 |
| 某个 ROM zip 单独损坏 | `scudj` 与 `scuddx` 的 CROM CRC 相同，都是 Deluxe；Twin/DX 全部正常 |

### 关键证据

Deluxe 版 HUD 使用 4 个小 viewport，日志（`Vulkan: viewport ...`）为：

```text
271.75x128 at (112,8)   ← 速度表/后视镜区域
 87.75x72  at (408,24)  ← POSITION
159.75x160 at (336,192) ← CONDITION
 79.75x80  at (400,112) ← 第四块
495.75x384 at (0,0)     ← 主画面
```

- 截图里的灰色矩形与这四个 viewport 一一对应。
- `Interp: viewports 12 (4 HUD)` 也把 4 个小 viewport 识别为 HUD。
- 解析存档里的 viewport node：四块都是 `priority=3, select=0, number=0`，
  `fogColour=0xFFFFFF`、`fogAmbient=1.0`、`fogDensity=0`、`scrollFog=0`。
  所以灰块不是 fog quad，也不像 `select`/第二输出导致。
- Deluxe 把 HUD 拆成独立小 viewport；Twin/DX 的画面没有这组灰块，但两者的
  viewport 组成尚未逐项对照。

### 当前假设（未验证）

1. HUD viewport 内的模型使用了某种 **texture format / alpha test / microtexture**
   组合，Vulkan 与 Legacy3D 都解码错误。灰块可能是未贴图的底板，文字/地图在另一层。
2. HUD viewport 的 **priority / depth / transLSelect** 组合导致底板可见，
   覆盖层或文字被深度测试、层选择或 `highPriority` 路径丢弃。
3. Deluxe 程序依赖某个尚未模拟的硬件行为（可能性较低：viewport 内仍有部分数字能显示）。

### 下一步定位

- 加临时环境开关（例如 `SM3D_HUD_DEBUG=1`），在
  `CR3DSceneBuilder::SetMeshValues()` / `CacheModel()` 里对小 `screenArea` 的 viewport
  打印每个多边形的：`format / page / alphaTest / textureAlpha / polyAlpha /
  microTexture / x,y,w,h`。
- 与 `scud` / `scudau` 正常 HUD 的同名元素对照（它们在主 viewport）。
- 检查四个 HUD viewport 的 `[02]` child 指针、模型数量和实际 `DrawBucket` 计数，
  确认底板之外的数字、文字、地图模型是否被提交。
- 如果确认是格式/alpha，改 `GetTexFormat()` / `ExtractColour()` / alpha test 路径；
  如果是层/深度，改 HUD viewport 的 pass 顺序或 depth 处理。
- 修复后把结论写进 `devdocs/CHANGELOG.md`，并从本文件删除该条。

### 临时绕过

- 让玩家用 Twin/DX 版：`scud`（Export, Twin/DX）、`scudplus` / `scudplusa`、
  `scudau`（Australia, Twin/DX）。HUD 布局不同，没有这些灰板。
- 依据：`scuddx` 是 Deluxe 机台（四块独立 HUD 视口），而 Twin/DX 的 HUD 在
  主视口/其他路径，实测画面可正常游玩。

### 相关文件

- 存档：`dist/Model-3-Vulkan/Data/Saves/scuddx.zst1`（2026-10-24 09:07）；Twin 对照用
  `scud.zst1`（2026-10-27 10:42）。
- raw state（可直接给 LegacyOG）：`sdl3_reports/2026-11-01-scuddx-hud/scuddx-raw.st`、
  `scud-raw.st`（各 30.8 MB）。用 `Capture-Frame.ps1 -RawState <路径>` 生成（该开关 2026-11-01
  修好）；`.zst` 容器是 zip + `core.zst`，Python 3.14 可直接 `compression.zstd` 解。
- 本轮证据与命令：`sdl3_reports/2026-11-01-scuddx-hud/README.md`，同目录有
  `deluxe-40s.png`（当前 Vulkan）、`legacy.png`（LegacyOG，同一份 raw state）、
  `twin-40s.png`、`twin-slot1.png`、`scudau-slot7.png`、各局 `log-*.txt`
  （`SM3D_HUD_DEBUG` 输出）与 `tilegen-*.png`。
- 工具：
  - `SM3D_HUD_DEBUG=1`：`R3DSceneBuilder` 打印 HUD 视口的多边形状态 / 面颜色 / 贴图矩形
    （按视口归属，默认关闭、零开销）。
  - `Scripts/render_tilegen_state.py`（离线重建存档里的 tilegen 层；`.zst` 与 raw `.st` 都
    接受，越界 bug 2026-11-01 已修：只保留屏内像素，C++ 行缓冲宽 512）。

## 2026-10-27 — 棋盘格半透明（AlphaSortMode = 2）与 LOD 交叉淡化不兼容

**状态**：暂缓；模式 2 已定位为**试验模式**（只适合雪雾/尘土）
**影响**：Daytona 2 的瀑布（`daytona2.zst2`）。模式 2 下瀑布整片变成蓝白色噪块，随镜头移动
闪动；同一帧换模式 0 或 1 即正常。

### 复现

```bat
powershell -NoProfile -File Tools\Capture-Frame.ps1 -Bin dist\Supermodel-VK ^
  -Set daytona2 -Slot 2 -Frame 190 ^
  -Ini "RefreshRateMode=2;FullScreen=1;XResolution=2480;YResolution=1920;IntegerScale=1;MSAA=4;AlphaSortMode=2;PostCRT=0;LoadHDTextures=0;InternalResolution=0" ^
  -Out daytona2_mode2.png
```

同一存档、同一配置只改 `AlphaSortMode=0/1` 得到对照帧。

### 已测得的数据

| 半透明排序 | MSAA | 瀑布区蓝色像素占比 |
| --- | --- | --- |
| 原版（双层）`0` | 4x | 4.87% |
| 逐像素排序 `1` | 4x | 3.92% |
| 棋盘格 `2` | 4x | 20.13% |
| 棋盘格 `2` | 1x | 12.09% |

着色器里读出的覆盖（临时调试输出，已移除）：瀑布一带 20.2% 的像素只有 trans1 一层
（平均 alpha 0.65，trans2 为空），27.2% 两层都在（0.66 / 0.54），48.4% 无半透明。

### 已排除

| 假设 | 结果 |
| --- | --- |
| 本次「低分辨率渲染」改动 | 四组截图全部 `InternalResolution=0`；改动前后的二进制在模式 0 下逐像素相同（max 0 / mean 0） |
| 构建 / 着色器版本 | 重新编译前后 `composite_frag.spv.h` 与 Supermodel-VK 树逐字节相同 |
| HD 纹理包 | `LoadHDTextures=0` 与 `1` 都复现 |
| CRT / 后处理 | `PostCRT=0`，`PostFXAA/PostGlow=0` |
| 只是阈值太严 | 两次调阈值实测：合计覆盖 ≥1.9 → 蓝 7.34%（瀑布主体变半透）；单层 ≥1.8 → 蓝 18.08%（更糟） |

### 结论

棋盘格每层只写一半像素，合成后的 alpha 天然是原版的一半。交叉淡化是「同一面画两遍、
alpha 互补」，原版靠“两层都在 → 不透明”还原；按像素拆层后这个信息不复存在，2×2 组无法
反推。**不是阈值问题，是信息丢失。**

### 下一步（如果要做）

1. 保留逐像素的两层覆盖信息再合成，例如额外一张每像素 `r8` 存“互补对”标记
   （绘制时若 `alpha1 + alpha2 ≈ 1` 就置位），合成时对标记像素走原版公式。
2. 或让模式 2 的透明层保持全分辨率、只把“半强度”应用于特定 alpha 区间——但那已经偏离
   真机模型，需要先确认目标观感。
3. 复测样本：`daytona2.zst2` 第 190 帧（模式 0/1/2 三张基线都在
   `sdl3_reports/2026-10-27-checkerboard-hd/images/`）。

## 2026-10-27 — 待办：低分辨率渲染（`InternalResolution`）文档

**状态**：功能已实现并本机验证，**故意只差文档**——等旧笔记本（UHD 620）的真实
帧率数据齐了再一起写，避免报告里只有独显数字。
**不要在未读本节的情况下重做功能。**

### 已实现（本轮无需重做）

- 键 `InternalResolution`：`0` = 关（默认）/ `1` = 496×384，写在 `DefaultConfig()`
  的 Vulkan 区，并由 `ConfigBuilders.cpp` 加入 OpenGL 剔除表。
- 3D 场景目标（color/depth/trans1/trans2/resolve/OIT A-Buffer）按 496×384 分配；
  `CVulkan2D` 把场景层按 UV 0..1 线性放大到画面矩形；2D HUD 不变。统计面板显示
  `Render 496x384 -> 1395x1080`。
- 与 `IntegerScale`、`WideScreen` 互斥（内部渲染优先，界面置灰）；超低配极速预设
  写入 `InternalResolution=1`，其它键未动。
- 关闭时与改动前逐像素一致（重建的改动前二进制对比：max 0 / mean 0）；开启时
  MSAA 0/2、`AlphaSortMode` 0/1、子 Pass/动态渲染、4K 全屏 + bezel + CRT 都验证过，
  同步验证无新增 VUID。
- 改动文件：`Src/OSD/SDL/Main.cpp`、`Src/Graphics/Vulkan3D/Vulkan3D.{h,cpp}`、
  `Vulkan2D.{h,cpp}`、`Gui.cpp`、`Localization.cpp`、`Src/Util/ConfigBuilders.cpp`。

### 本机已有数据（Intel Arc，供报告正文使用）

1080p 画面（1395×1080）、8× 相位、VSync 关：

| 配置 | 帧时间 | present |
| --- | --- | --- |
| `InternalResolution=0` | 5.05 ms | 198 Hz |
| `InternalResolution=1` | **2.96 ms** | **338 Hz** |
| `IntegerScale` 2x 参照 | 4.12 ms | 243 Hz |

UHD 620 的绝对值等下面 B 项跑完再补。

### 文档待写清单

1. `devdocs/CHANGELOG.md` 的 `2026-10-27` 加一节：机制（三个场景附件一起缩、
   2D 不缩）、与 `IntegerScale`/`Widescreen` 的互斥、预设、验证矩阵、性能表。
2. `usermanual.md` / `usermanual_chs.md`：`InternalResolution` 是什么、和
   `IntegerScale` 的区别、什么时候选它、推荐搭配（MSAA/半透明排序/预设）。
3. `AGENTS.md`：加一条坑（3D 目标与 2D 坐标系分离；窗口/全屏的缩放由 2D 层负责；
   新增读 scene target 尺寸的代码要跟着走）与「当前状态」条目。
4. 新建 `sdl3_reports/2026-10-27-low-res-internal/README.md`：命令、原始日志、
   截图（关/开/整数缩放三张，同一帧）、验证矩阵；并在
   `sdl3_reports/README.md` 索引加一行。

## 2026-10-27 — 待办：旧笔记本（i5-8365U / UHD 620）低分辨率 A/B 测试

**状态**：未开始。**需要用户先提供**：SSH 地址与用户名、`daytona2` 的存档槽号
（以及确认笔记本上已有该 ROM 与存档）。没有这两项不要开工。

### 目标

得到 UHD 620 上 `InternalResolution` 关/开的绝对值，补全上面 A 项的性能表，
并顺带验证低分辨率路径在核显 + 1080p60 面板上的正确性。

### 工具缺口（先改再测）

- `Tools/Remote/Deploy-ToLaptop.ps1` 目前只拷 `Supermodel-LegacyOG.exe`，
  **测不到 Vulkan 功能**。需要加 `-Exe` / `-Target`（或 `-Files`）参数，并同步：
  - 进程占用检查里的 `Supermodel-VK\.exe` 匹配不到 `Model3-Vulkan.exe`；
  - 部署 `Model3-Vulkan.exe` **和** `Model3VulkanHostCore.dll`；
  - 需要把 `Tools/`（至少 `Capture-Frame.ps1`、`Measure-FrameRate.ps1`、
    `TestIniGuard.ps1`、`Restore-TestIni.ps1`）也送过去，因为测试脚本要在
    笔记本本机跑（ini 在笔记本上）。
  - 继续**不要覆盖**笔记本的 `Data/`（ini/NVRAM/存档属于那台机器）。

### 测试矩阵（建议）

同一存档、同一帧，1080p60 面板，VSync 关，至少三组：

| 组 | `-Ini` 关键值 |
| --- | --- |
| A 关 | `InternalResolution=0;XResolution=1920;YResolution=1080;IntegerScale=0` |
| B 开 | 同上 + `InternalResolution=1` |
| C 参照 | `InternalResolution=0;XResolution=992;YResolution=768;IntegerScale=1`（2x） |

- 固定逻辑分辨率与半透明路径（建议 `MSAA=0;AlphaSortMode=0;RefreshRateMode=6;`
  `PaceAssumePanelHz=600;VSync=0`，与 Arc 那组一致；以当时日志里的实际键为准）。
- 用 `Tools/Capture-Frame.ps1 -Frame <n>` 抓同一帧，`Tools/Measure-FrameRate.ps1`
  取 present/render 中位数；每组至少两次，确认重复性。
- 拉回 `Data/Supermodel.log`（`Tools/Remote/Get-LaptopLog.ps1`）、截图与测试前后的
  配置哈希；结束前确认 ini 已还原（见 `AGENTS.md` 的「测试脚本会临时改写 ini」）。
