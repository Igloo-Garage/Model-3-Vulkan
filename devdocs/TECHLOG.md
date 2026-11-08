# Supermodel 开发日志

本项目基于 Supermodel 0.3a（Sega Model 3 街机模拟器）进行个人分支改造，主要目标是：

1. 让 **Vulkan 渲染后端**达到与原版 OpenGL（New3D）后端相同的画面正确性
2. 解决**音频卡顿**与主循环耦合的问题
3. 修正**手柄按键**布局与冲突
4. 把模拟器改造成**图形界面外壳**（常驻 GUI，退出游戏回到设置界面）
5. 逐步引入 **CRT 后处理滤镜**与 **MSAA** 抗锯齿

本文档记录**已完成**与**未完成**的所有事项，包含验证方法与实测数据。
凡标注"已实测"的结论均有日志或对比图作为依据；未经验证的部分会明确标注。

---

## 目录

- [一、环境与构建](#一环境与构建)
- [二、Vulkan 渲染后端](#二vulkan-渲染后端)
- [三、后处理滤镜（CRT / MSAA）](#三后处理滤镜crt--msaa)
- [四、截图功能](#四截图功能)
- [五、音频](#五音频)
- [六、手柄与按键](#六手柄与按键)
- [七、界面外壳与用户体验](#七界面外壳与用户体验)
- [八、配置项参考](#八配置项参考)
- [九、未完成与已知问题](#九未完成与已知问题)
- [十、验证工具与方法](#十验证工具与方法)
- [11. 网络板（Net Board）单人游玩](#11-网络板net-board单人游玩为什么必须开启以及端口必须配对)
- [十二、发行版布局与一键构建](#十二发行版布局与一键构建buildbat)
- [十三、窗口标题与图标](#十三窗口标题与图标)
- [十四、设置界面布局精简](#十四设置界面布局精简)
- [十五、漏译审计](#十五漏译审计发现-5-处)
- [十六、载入设置后高级设置页变空](#十六载入设置后高级设置页变空makecopy-丢掉了-valuerange)
- [十七、所有 ROM 显示「未收录」](#十七所有-rom-显示未收录gamexmlfile-里的旧相对路径)
- [十八、机柜背景图（Bezel）](#十八机柜背景图bezel)
- [十九、启动时控制台黑框一闪而过](#十九启动时控制台黑框一闪而过)
- [二十、游戏内设置叠加层（Overlay）](#二十游戏内设置叠加层overlay)
- [二十一、两处渲染修复：贴图脏块丢失与 2D 层边缘杂线](#二十一-两处渲染修复贴图脏块丢失与-2d-层边缘杂线)
- [二十二、游戏进不去的原因排查](#二十二游戏进不去的原因排查网络板--驱动板--rom-依赖)
- [附：可迁移的调试模式（`FIXES.md`）](#附可迁移的调试模式fixmd)

---

## 一、环境与构建

| 项目 | 值 |
|---|---|
| 平台 | Windows 10.0.26200 (x64) |
| 编译器 | MSVC v143 (Visual Studio 2022 Community) |
| 解决方案 | `MSVC/Supermodel.sln` |
| 目标 | `Supermodel`（Release / x64） |
| GPU | NVIDIA GeForce RTX 5060 Ti |
| 显示器 | **4K (3840×2160) 32 寸**，系统缩放 150%，支持 G-SYNC（VRR 48–160Hz） |

**构建命令**

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "MSVC\Supermodel.sln" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /t:Supermodel
```

**着色器编译**（修改 `Src/Graphics/Vulkan3D/Shaders/*.frag|vert|comp` 后必须执行）

```powershell
cmd /c "Scripts\CompileVulkanShaders.cmd"
```

该脚本使用 `Libraries/glslang/bin/glslang.exe` 将 GLSL 编译为 SPIR-V，并生成同名的 `.spv.h` 十六进制头文件。**新增着色器必须在脚本的 `:build` 调用列表里登记**，否则头文件不会更新。

### 1.1 着色器是预编译的，改了不编译等于没改（重要教训）

**Vulkan 着色器不是运行时加载的**，而是**编译期嵌入**：

```cpp
#include "Shaders/post_frag.spv.h"      // 内含 SPIR-V 的 uint32 数组
```

流程是 `post.frag` → `glslangValidator` → `post_frag.spv` → `post_frag.spv.h` → 被 C++ `#include`。

**后果**：只改 `.frag` 而不运行 `CompileVulkanShaders.cmd`，**改动完全不生效**——程序运行的仍是旧着色器。而且**不会报错**，表现是"改了但没反应"，与"改动生效但效果不明显"**外观完全相同**。

**本项目实际踩过这个坑**：`post.frag` 的四处改动全部静默失效，时间差达 8 小时：

| 改动 | 章节 | 是否曾生效 |
|---|---|---|
| 扫描线光束解析积分 | 3.8 | ❌ 曾失效 |
| **辉光归一化修正** | 3.4.1 | ❌ **曾失效** |
| 辉光可调阈值 | 3.4.2 | ❌ 曾失效 |
| 高光软压缩 | 3.11 | ❌ 曾失效 |

证据：`post_frag.spv.h` 时间戳 `15:40`，而 `post.frag` 已改到 `23:53`；重新编译后 `post_frag.spv.h` 从 **44,468 → 48,259 字节**，证明内容确实变了。

**已加入构建防线**：`Scripts/CheckShaderHeaders.ps1` 挂在 `Supermodel.vcxproj` 的 **`PreBuildEvent`**（4 个配置全加），**源文件比 `.spv.h` 新就让构建失败**并给出指引：

```
Shader sources are newer than the SPIR-V that gets embedded:
  post.frag (edited 10-11 00:01, compiled 10-11 00:00)

The shaders are precompiled into .spv.h headers, so a source edit without recompiling
has no effect at all - the old shader is what runs. Run this, then build again:

    Scripts\CompileVulkanShaders.cmd
```

**已实测**：着色器最新 → 通过；人为把 `.frag` 时间戳更新 → **构建失败（exit code 1）**。

> **判断"着色器改动是否生效"的可靠方法**：对照 `post_frag.spv.h` 与 `.frag` 的时间戳。**不要凭观感判断。**

> ⚠️ 构建前请确认没有残留的 `Supermodel.exe` 进程，否则链接会报 `LNK1104: cannot open file`。

---

## 二、Vulkan 渲染后端

### 2.1 Real3D 贴图页与 mip 金字塔（已实测）

**问题**：远景物体和背景在 Vulkan 下渲染成全黑。

**原因**：Real3D 的贴图页把整个 mip 金字塔**打包在同一张 2048×1024 的 16 位图**里，各级 mip 位于固定偏移 `(kMipX[level], kMipY[level])`，而不是独立的 mip 链。原实现用 `texelFetch(..., level)` 去取不存在的 mip 层级。

**修复**：`Src/Graphics/Vulkan3D/Shaders/real3d.frag` 改为在 level 0 上用 `kMipX`/`kMipY` 偏移做硬件取值，移植了原版的 `texBiLinear`、`ExtractColour`、`LinearTexLocations`、`WrapTexCoords`、`GetTexturePosition`、`GetTextureSize`、`GetTextureValue`。

### 2.2 透明剔除与混合（已实测）

- `T1RGB5`（1 位 alpha / contour）、`A4L4` 交错、`RGBA4`（Format 7）按原版规则解析
- 片元着色器中按 `tex.a < 32/255`（或 `< 1/32`）丢弃透明纹素，避免 opaque pass 污染深度缓冲
- 修正 `alphaTest` 与 `discardAlpha`，使贴花（decal）与 contour 网格保留

### 2.3 共面贴花（Coplanar Decal）与 Overlay（已实测）

车身上的 PHANTOM 贴花、骷髅、车灯曾整体缺失。

**修复**：新增 overlay 管线 `m_overlayOpaque` / `m_overlayTranslucent`，`raster.depthBiasEnable = VK_TRUE`，并把多边形 pass 顺序对齐 New3D。深度比较用 `GREATER_OR_EQUAL` + 深度偏移，避免与车身基础涂层 z-fighting。

### 2.4 雾效（New3D 对齐）

`CreateFogPipeline()` / `DrawFog()` 实现了原版的 ambient fog 与 scroll fog 两个 pass，在几何体之前绘制，使地平线与空白区域的颜色与 New3D 一致（而不是保持黑色）。

### 2.5 白闪（Block Culling）范围修复（已实测）

**问题**：进游戏前的启动文字画面，4:3 区域两侧的黑边**变成白色**。

**原因**：Real3D 在 block culling 期间会让屏幕整片变白（真实硬件行为）。Vulkan 后端的 `m_color` 渲染目标是**整个 swapchain 大小**（3840×2160），游戏画面只占其中 4:3 子区域。原实现清的是整个目标，于是把左右黑边一起刷白。

实测几何关系（日志）：

```
Block-culling flash geometry: viewport 2790x2160 at (525,0) inside a 3840x2160 target
```

`2160 × 496/384 = 2790`，左右各 525px 黑边。

**修复**：白闪清屏限制在游戏自己的 4:3 视口内（做了 OpenGL→Vulkan 的 Y 轴翻转，视口越界时回退到全目标）。

### 2.6 4:3 缩放与全屏铺满（已实测）

`CalcScreenGeometry()` 原本只缩小视口，导致全屏时画面缩在中间一小块。改为把 4:3 画面**等比放大**到铺满屏幕高度、左右留黑边居中。

`CVulkan2D::DrawLayer` 的 scissor/viewport 也做了钳制，避免负偏移。

### 2.7 独占全屏与 G-SYNC（已实测）

新增两种全屏方式，默认**无边框**（行为不变）：

| 配置 | 作用 |
|---|---|
| `ExclusiveFullscreen = true` | 独占全屏 + G-SYNC/VRR，面板变频跟随模拟器帧率，**无需自定义分辨率** |
| `ExclusiveRefreshRate = 57.5242` | 独占全屏 + 固定 57/58Hz 时序（无 VRR 的显示器用）；找不到该模式时退化为上一种 |

命令行：`-exclusive-fullscreen`。

**途中修掉的 bug**：独占全屏时 SDL 会自行挑一个模式，导致 swapchain 变成 2400×1800（非原生）。改为**显式应用桌面模式**，swapchain 为 3840×2160，4:3 画面在原生分辨率下左右留黑边。

实测日志：

```
Exclusive fullscreen: desktop mode 3840x2160@120 Hz set; G-SYNC/VRR should track the emulator rate.
Vulkan: swapchain 3840x2160, 3 images, present mode FIFO (vsync on).
```

### 2.7.1 三种全屏方式改为互斥单选（已修复，待实测确认）

**问题**：原先「常用」页是**两个独立复选框**（G-SYNC/VRR 与 固定街机刷新率），**可以同时勾选**。而 `ResolveFullScreenMode()` 里：

```cpp
if (refreshRateHz > 0.0f)
    return FullScreenMode::ExclusiveArcade;    // 固定频率胜出
// 否则才走 G-SYNC 那条
return FullScreenMode::ExclusiveDesktop;
```

即**固定刷新率优先**，同时勾选时 G-SYNC 那项**被静默忽略**——用户勾了两个，其中一个不生效，且生效的不是最后勾的那个。**"二者只能取其一"是复选框表达不了的语义。**

**修复**：改为**三选一单选**（`无边框全屏` / `独占全屏 · G-SYNC/VRR` / `独占全屏 · 固定街机刷新率`），两项底层配置**由同一个 setter 一起写入**，保证永不矛盾：

```cpp
const auto applyMode = [&](int picked)
{
    config.Set("ExclusiveFullscreen", picked != 0);
    config.Set("ExclusiveRefreshRate", picked == 2 ? 57.5242f : 0.0f);
};
```

### 2.7.2「刷新率」在固定频率模式下置灰（已修复，待实测确认）

**问题**：「画面」页的「刷新率」（60 / 57.5）在固定街机刷新率模式下**不起作用**——`ResolveFullScreenMode()` 会把它强制覆盖：

```cpp
if (refreshRateHz > 0.0f)
    s_runtime_config.Get("RefreshRate").SetValue(refreshRateHz);
```

但它仍显示 60，是个**误导性的显示**。

**三种模式的真实关系**（此前未文档化，容易误解）：

| 模式 | 「画面」页的 `刷新率` | 是否生效 |
|---|---|---|
| 无边框全屏 | 决定模拟节奏 | **生效** |
| 独占全屏 · G-SYNC/VRR | 是节奏基准，面板跟随它 | **生效** |
| 独占全屏 · 固定街机刷新率 | 被覆盖为 57.5242 | **不起作用** |

**所以它不是"假选项"**，只在固定频率模式下被覆盖。

**修复**：该模式下把「刷新率」下拉**置灰并显示实际生效的 57.5 Hz**，右侧注明来源。

**关键取舍**：**不改写存储的值**。它仍是关掉固定频率后应恢复的选择，静默改写会丢失用户设置：

```cpp
const bool overridden = (key == "RefreshRate" &&
    config["ExclusiveRefreshRate"].ValueAsDefault<float>(0.0f) > 0.0f);
if (overridden) {
    selectedIndex = 0;              // 显示 57.5 Hz
    ImGui::BeginDisabled();
}
ImGui::Combo(uiLabel, &selectedIndex, sVectorChar.data(), (int)sVectorChar.size());
if (overridden) {
    ImGui::EndDisabled();
    ImGui::SameLine();
    UiTextDim("（由「独占全屏 · 固定街机刷新率」决定）");
}
else {
    vRange->tempValue = list[selectedIndex];   // 仅未覆盖时才写回
}
```

### 2.7.3 界面提示三种模式的真实可用性（已实现，待实测确认）

**问题**：固定街机刷新率需要显示器里**存在 57/58Hz 模式**，而要创建它得去显卡控制面板手动添加。**大多数机器上并不存在**，此时 `ResolveFullScreenMode()` 会**静默回落到 G-SYNC**——选了这一项却发现没有变化，而且界面上完全看不出来。

**实现**：把 `FindArcadeDisplayMode()` 拆出**静默版本**（`FindArcadeDisplayModeImpl(..., bool quiet)`），并对外暴露：

```cpp
bool HasArcadeDisplayMode(float refreshRateHz);   // Main.h
```

界面在选中该模式时**显示实际检测结果**，而非写死的说明文字：

| 检测结果 | 界面提示 |
|---|---|
| 存在 57/58Hz 模式 | 「已检测到显示器的 57/58 Hz 模式，可以 1:1 输出。」|
| 不存在 | **橙色警告**「未检测到 57/58 Hz 模式：启动时会自动回落到 G-SYNC/VRR。」+ 指向 `Scripts\ArcadeDisplay.ps1` |

**关键点**：

1. **复用同一套逻辑**，所以界面报告的**就是**启动时实际会做的判断，不会互相矛盾
2. **静默**：GUI 每帧重绘，原函数会打日志，每帧一条会淹没日志
3. **缓存**：枚举显示模式不便宜，且窗口打开期间答案不变，因此只查一次（`static`）

**关于「无边框模式不写回 `RefreshRate`」的设计确认**（用户确认保留）：
切到无边框全屏后 `RefreshRate` 回到 60，因为该值从未被改写——这正是"关掉固定频率就自动恢复"所需要的。

**验证状态**：构建通过。**GUI 自身是窗口模式，独占全屏只在启动游戏时才应用**，所以界面提示的准确性需在实际游戏中对照日志确认：

```
Exclusive fullscreen: fixed mode 3840x2160@57 Hz selected (arcade rate 57.5242 Hz).   ← 有该模式
Exclusive fullscreen: no usable 57/58 Hz display mode; falling back to G-SYNC/VRR.    ← 没有
```

### 2.8 Present 模式

启动时枚举并记录可用 present 模式：

```
Vulkan: available present modes: FIFO (vsync on), FIFO_RELAXED, MAILBOX (vsync off, latest frame), IMMEDIATE (no vsync), other.
```

`VSync` 开启时选 FIFO（G-SYNC 下必需，否则面板不会跟随帧率）。

### 2.9 多显卡选择

- `Src/OSD/SDL/Main.cpp` 导出 `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance`，强制混合显卡系统使用独显
- `VulkanDevice` 配置项可选择物理设备（按名称），设置界面提供下拉选择

---

## 三、后处理滤镜（CRT / MSAA）

### 3.1 硬件 MSAA（已实测）

`Src/Graphics/Vulkan3D/VulkanMemory.cpp` 的 `CreateImage()` 增加 `samples` 参数。

3D 场景渲染到**多重采样目标**，通过动态渲染的 `resolveImageView` + `AVERAGE` 自动解析为单采样图，再交给 2D 层合成。因为 MSAA 只作用于**几何覆盖**，**贴图与贴花完全不变锐**。

配置 `MSAA`：0 / 2 / 4 / 8（8x 需驱动同时支持颜色与深度附件的采样数，不支持时自动降级并写日志）。

实测日志：

```
Vulkan: 3D target 3840x2160, 4x MSAA (resolved before compositing).
```

> **注意**：`kMaxFramesInFlight = 2`（见 `VulkanCommon.h`），做任何与帧槽位相关的改动时要注意这一点。

#### 3.1.1 开 MSAA 后 3D 消失的根因（已实测修复）

**症状**：开启 MSAA 4x 后，2D 文字/UI 正常，但 3D 场景完全不见——菜单阶段是一片黑，进入赛道后是**满屏纯色**（如整片蓝）。

**根因不在 MSAA 配置本身，而在一个每帧覆盖的赋值。**

`EnsureTargets()` 会把合成器的场景视图设为解析后的图像 `m_colorResolve.view`：

```c
m_output.SetSceneColor(m_samples > VK_SAMPLE_COUNT_1_BIT ? m_colorResolve.view : m_color.view);
```

但 `EnsureTargets()` 在目标已存在时会**提前返回**（`m_color.image && ... ` 命中即 `return true`），所以这一行**只在创建目标的那一帧执行一次**。而 `EndFrame()` 里还有一行**每帧执行**的：

```c
m_output.SetSceneColor(m_color.view);   // ← 无条件，覆盖成多重采样图像
```

于是**从第 2 帧起**，合成器描述符里绑定的都是**多重采样图像视图** `m_color.view`。而多重采样图像**不能用普通 `sampler2D` 采样**（必须用 `texture2DMS`），如此采样是**未定义行为**——驱动返回常量数据，表现就是满屏纯色。

这解释了全部现象：

- 2D 层走自己的路径，不经过这个描述符 → **2D 始终正常**
- 第 1 帧正常，之后每帧被覆盖 → 偶尔"看着是好的"，一进赛道就变纯色

**修复**：`EndFrame` 在 MSAA 下交出 `m_colorResolve.view`（并在必要时把布局转为 `SHADER_READ_ONLY_OPTIMAL`），非 MSAA 分支同样处理布局：

```c
if (m_samples > VK_SAMPLE_COUNT_1_BIT)
{
    if (m_resolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) { /* 转换 */ }
    m_output.SetSceneColor(m_colorResolve.view);
}
else
{
    if (m_colorLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) { /* 转换 */ }
    m_output.SetSceneColor(m_color.view);
}
```

**同轮另外三处修正**（均为真实缺陷，非本次症状主因）：

| 位置 | 问题 | 处理 |
|---|---|---|
| `m_depth` 创建 | MSAA 下仍请求 `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`，而**多重采样图像不能作传输源**（规范只允许 `TRANSFER_DST`），属非法标志组合 | MSAA 下不再请求该标志（`CopyLos` 本就跳过） |
| 构造函数 | 从不校验驱动是否支持所请求的采样数 | 用 `vkGetPhysicalDeviceImageFormatProperties` 校验 color/depth 两种格式，不支持则降级 1x 并记录 |
| `DrawScene` 收尾 | 循环结束时 `inRenderPass` 恒为 `false`，`finishScene()` 永不执行，`m_colorResolve` 布局不转 | 改为无条件转换 |

**实测证据**（4K 独占全屏，`MSAA=4`）：

```
[Info]    MSAA=4
[Info]  Vulkan: MSAA 4x requested - colour format supported: yes, depth format supported: yes.
[Info]  Vulkan: 3D target 3840x2160, 4x MSAA (resolved before compositing).
```

修复后赛车、赛道、场地全部正常显示（用户确认）。

> **教训**：本次排查之所以耗时很久，是因为系统内**没有安装 Vulkan 验证层**（无 SDK、无隐式层、无驱动层 DLL），`VkValidation=1` 只报 `validation layer unavailable, retrying without it`，所以**始终没有拿到过 VUID 报错**，只能靠读代码推断。**建议后续调试 Vulkan 问题时先装 Vulkan SDK 或 RenderDoc。**

### 3.2 FXAA（已实测）

单 Pass 后处理，`Src/Graphics/Vulkan3D/Shaders/post.frag` 内实现 FXAA 3.11（quality 变体）。配置 `PostFXAA`。

用途：集显/轻薄本的抗锯齿。有独显时优先用 MSAA，因为 FXAA 会软化贴图细节。

### 3.3 荧光粉点阵（CRT Mask）

**实现位置**：`post.frag` 的 `ApplyMask()`，`Src/Graphics/Vulkan3D/Vulkan2D.cpp` 的 `DrawPostPass()`。

采用**距离场**而非图案乘法：每个输出像素先求其在点阵单元内的位置，再对每个色彩通道计算到该通道荧光粉子孔的**距离**，孔的半径**随亮度变化**（亮部束径大、暗部束径小）——这是让点阵看起来像"发光颗粒"而非"格子"的关键。

三种点阵（`PostMaskType`）：

| 值 | 类型 | 说明 |
|---|---|---|
| 0 | 栅条式 | 连续竖向红绿蓝条纹，无纵向结构（索尼特丽珑类） |
| 1 | 槽孔式 | 条纹被打断成短槽、隔行错开半个三色组（**世嘉街机常用**）|
| 2 | 点阵式 | 2×2 点三色组，第四格为空（较廉价彩显）|

`PostMaskScale`：一个荧光粉点占几个物理像素。

> ⚠️ **取值必须考虑 RGB 子孔采样**：3 个子孔在单元内以 `maskScale/3` 像素等距排布，**该值必须 ≥ 1 像素**，否则彩色混叠。因此 **`maskScale` 应是 3 的倍数**（3、6）：
>
> | maskScale | 子孔间距 | 结果 |
> |---|---|---|
> | 2 | 0.667 px | ❌ 彩噪 |
> | **3** | **1.000 px** | ✅ 干净（推荐）|
> | 4 | 1.333 px | ❌ |
> | **6** | **2.000 px** | ✅ 干净 |
>
> 另外它还要与**扫描线周期整除**以避免拍频，详见 **3.10.3**。

```c
// 束径随亮度变化 + 保留下限（下限过低会大幅吃光，见 3.5）
float radius = 0.24 + 0.28 * clamp(luma, 0.0, 1.0);
return colour * mix(vec3(1.0), 0.35 + 0.65 * holes, pc.maskIntensity);
```

### 3.4 Mip 链辉光（Glass Glow，已实测）

**这是"玻璃通透感"的核心，也是最难的部分。**

**方法**：用 `vkCmdBlitImage` 生成**完整 mip 链**（硬件盒式滤波，每一级半径翻倍），然后**一个着色器**采样多级并按递减权重叠加。

见 `CVulkan2D::GenerateCompositeMips()` 与 `post.frag` 的 `ComputeGlow()`。

为什么这样做：

- 常规做法是手写多级模糊链（**这正是 CRT-Royale 有 15+ 个 Pass 的原因**）
- 硬件**已经会**降采样：把 level n-1 blit 到 level n 就是盒式滤波，几级之后就等价于一次很宽的模糊，而代价只是几次 blit 调用
- 每一级仍是**局部**信息（在 level 6 采样某个 UV 得到的是该处一小块屏幕的平均），这正是"亮点周围有光晕"而非"整屏被抬亮"的原因

**关键实现细节**：

1. 采样器必须设 `maxLod = VK_LOD_CLAMP_NONE`，否则 Vulkan 默认 `maxLod = 0` 会把所有采样钳到 level 0，辉光完全失效
2. **基础画面必须显式 `textureLod(..., 0.0)`**（见 `Fetch()`）。用隐式 LOD 时，mip 链一生效就会飘到模糊层级——**实测整张画面被压暗到只剩 8.6% 亮度**，修正后恢复到 32.3%

配置：`PostGlow`（强度 0–100）、`PostGlowLevels`（累积几级 mip，1–8，默认 4）。

### 3.4.1 辉光把整片画面抬亮（已修复）

**症状**：足球等有**大面积明亮区域**的游戏，草皮"发亮、发蒙"，像整体曝光过度。

**根因是归一化方式错误，不是参数调得不好。**

原实现：

```glsl
float weight = max(Luma(sampleColour) - 0.40, 0.0);
sum += sampleColour * weight * falloff;
weightSum += weight * falloff;
return weightSum > 0.0 ? sum / weightSum : vec3(0.0);   // ← 加权平均
```

`sum / weightSum` 是**加权平均**，会导致**只要超过阈值，辉光值就 ≈ 画面颜色本身（1.0），超出多少被丢弃**：

| 区域 | luma | 超出阈值 | 旧辉光值 |
|---|---|---|---|
| 草坪（亮绿）| 0.52 | 0.12（刚过线）| **1.00** |
| 白色标线 | 0.95 | 0.55（远超）| **1.00** ← 完全相同 |

于是**草坪和白色标线的辉光强度一模一样**，再乘强度 0.20 又被增益 1.25 放大 → **整片草坪被整体抬亮约 25%**。这就是"草皮发亮"。

**修复**：改为**按 falloff 总和归一化**，让超出阈值的量级保留下来：

```glsl
sum += sampleColour * weight * falloff;
falloffSum += falloff;                                  // 不用 weight 加权
return falloffSum > 0.0 ? sum / falloffSum : vec3(0.0);
```

**为什么用 `falloffSum` 而不是 `weightSum`**：两者都能保证与 `glowLevels` 无关，但 `weightSum` 会**丢掉亮度信息**（分子分母同被 weight 缩放），`falloffSum` 则保留。这是本次修复的关键。

**实测对比**（阈值 40%、强度 20%、增益 125%）：

| 场景 | luma | 旧贡献 | 新贡献 | 变化 |
|---|---|---|---|---|
| 暗部（阴影）| 0.20 | 0.000 | 0.000 | 不变 |
| 中灰（跑道）| 0.38 | 0.000 | 0.000 | 不变 |
| **草坪（亮绿）** | 0.52 | 0.250 | **0.030** | **降到 1/8** |
| 明亮草地 | 0.62 | 0.250 | 0.055 | 降到 1/5 |
| 白线/车灯 | 0.95 | 0.250 | 0.137 | 保留一半 |
| 高光（纯白）| 1.00 | 0.250 | 0.150 | 保留 60% |

**级数无关性验证**（同一亮度，改 `glowLevels`）：

| 级数 | 旧分母 `weightSum` | 新分母 `falloffSum` | 新 glow 值 |
|---|---|---|---|
| 2 | 0.180 | 1.500 | 0.120 |
| 3 | 0.220 | 1.833 | 0.120 |
| 5 | 0.274 | 2.283 | 0.120 |
| 8 | 0.326 | 2.718 | 0.120 |

旧实现**恒为 1.0**（与级数无关，但丢掉了亮度信息）；新实现**恒为 0.120**（与级数无关，且保留了超出阈值的量级）。

### 3.4.2 新增「辉光阈值」参数（已实测）

原阈值 0.40 **写死在着色器里**，无法调整。现暴露为配置 `PostGlowThreshold`（0–100，默认 40），复用 push constant 中原先未使用的 `glowThreshold` 槽位：

```glsl
float weight = max(Luma(sampleColour) - float(pc.glowThreshold) / 100.0, 0.0);
```

界面在「高级设置 → 玻璃辉光」下新增滑杆。用途：

| 值 | 效果 |
|---|---|
| 低（20~30）| 大面积明亮区域也发光，适合夜景、暗场景（灯光晕开更明显）|
| 默认 40 | 平衡 |
| **高（60~70）** | **只有真正的高光发光**，适合草坪球场、雪地赛道、明亮天空 |

> **注意**：`PostGlow` 与 `PostGain` 是相互放大的关系（辉光加算 → 增益再乘）。觉得画面发蒙时，**优先动「辉光阈值」和「辉光强度」，而不是降「亮度补偿」**——后者会把整个画面压暗。

### 3.5 亮度补偿（Gain）

荫罩只透过约**三分之一**的光，这是物理事实。真实显像管靠极高的峰值亮度补偿（这正是街机屏看起来"刺眼又通透"的原因）。SDR 输出下只能用增益代替。

配置 `PostGain`（默认 190%）。觉得点阵开起来太暗就往上调。

### 3.6 亮度实测数据

| 配置 | 4:3 区域平均亮度 | 相对无滤镜 |
|---|---|---|
| 无滤镜（仅 4x MSAA）| 73.9 | 100% |
| CRT + 辉光（LOD bug 未修时）| 6.6 | **8.6%** ← 错误状态 |
| CRT + 辉光（修正后）| 23.8 | 32.3% |

> 说明：即使修正 LOD 后，开荫罩必然明显变暗，需靠 `PostGain` 补回。这是 SDR 下的固有取舍；**HDR 输出**才是真正的解法（见第九章）。

### 3.7 与 CRT-Royale / crt-guest-advanced-hd 的差距

**这两者不是单 Pass 滤镜**：CRT-Royale 约 **15 个 Pass**，crt-guest-advanced-hd 约 **20+ 个**。

已实现的部分（心智模型一致）：多级模糊辉光、荧光粉点阵、扫描线、**光束解析积分**（见 3.8）。

**尚未实现**：

| 缺失项 | 说明 | 单 Pass 可行性 |
|---|---|---|
| 光束查表扫描线 | Royale 用 256 宽的束斑纹理查表 | 可以（需预生成 LUT 纹理）|
| 束宽（spot size）可调 | Royale 的核心手感旋钮，束宽独立于行数 | 可以（在积分里引入束宽参数）|
| 屏幕曲率 / 桶形畸变 | PhosphorLine/PhosphorDot 都有 | 可以 |
| Gamma 分级处理 | Royale 在每个阶段进出 gamma 空间 | 可以 |
| 色差 / 色晕（chromatic aberration）| 边缘 RGB 分离 | 可以 |
| 历史帧（余辉/拖影）| 需要上一帧纹理 | 需要框架改动 |

**授权提醒**：`PhosphorDot` 等社区着色器带有明确声明——

```
//STATEMENT:The following core algorithms are original, and anyone is prohibited from
//using them to obtain commercial benefits without the formal permission of the author.
```

Supermodel 是 GPL 项目，**直接抄入会造成授权冲突**。本项目的点阵实现是**独立编写**的（仅参考技术思路：距离场、束径随亮度变化、子孔偏移）。

---

### 3.8 扫描线改为光束解析积分（已实测编译，观感待确认）

**问题**：4K 下扫描线出现宽窄不一的明暗条纹（摩尔纹）。

原因是**逐像素点采样**：

```glsl
float beam = cos(vUv.y * pc.scanlineCount * PI) * 0.5 + 0.5;   // 旧：只取像素中心
```

Model 3 是 384 行信号，4K 面板高 2160，即 **5.625 像素/行**。周期非整数时每行相位都不同，点采样就会 beat 成条纹。**这个比值只要不是整数就一定出问题**，而 2160/384 永远不是整数。

**修复**：真实电子束有宽度，一个输出像素看到的是光斑在**该像素覆盖范围内**的平均值，而 cos² 的区间平均值有闭式解：

\[
\frac{1}{\theta_1-\theta_0}\int_{\theta_0}^{\theta_1}\cos^2\theta\,d\theta
= \frac{1}{2} + \frac{\sin 2\theta_1 - \sin 2\theta_0}{4(\theta_1-\theta_0)}
\]

其中 \(\theta = vUv.y \cdot N \cdot \pi\)，一个输出像素对应 \(\Delta\theta = N\pi / resolution.y\)。

```glsl
float theta  = vUv.y * pc.scanlineCount * PI;
float dTheta = pc.scanlineCount * PI / pc.resolution.y;
float theta0 = theta - 0.5 * dTheta;
float theta1 = theta + 0.5 * dTheta;
float beam = dTheta > 0.0
    ? 0.5 + (sin(2.0 * theta1) - sin(2.0 * theta0)) / (4.0 * dTheta)
    : 1.0;
```

**收益**：

1. 彻底消除**扫描线自身的**摩尔纹，**不再要求扫描线数能整除输出高度**
2. 扫描线密度可以自由调高，行数过密时（两行落进一个像素）会**自行淡出**成平坦灰，而不是变成噪点
3. 可以放心把行数调到接近真机的物理行距（见 3.10.1）

> ⚠️ **但扫描线对齐只是「不产生扫描线自身摩尔纹」。** 开启荫罩后还有**第二个**拍频源——荫罩的隔行错开周期必须与扫描线周期整除，否则仍会出现彩波纹。这条约束**比扫描线对齐更强**，且推导见 **3.10.3**。本节讨论的光束积分**不能**解决它。

### 3.9 整数倍缩放（点对点，已实测日志）

「高级设置」新增配置 `IntegerScale`（默认 **false**）。

**背景——三层各自的真实分辨率**（本次查清，之前理解有误）：

| 图层 | 内容/渲染尺寸 | 到面板 | 是否重采样 |
|---|---|---|---|
| 3D 场景 | 直接按**视口尺寸**光栅化 | 视口 | **否**（3D 本就是最后一步光栅化） |
| 2D 瓦片层 | **固定 496×384**（硬编码） | 拉伸到视口 | **是** |
| CRT 后处理 | 窗口（面板）尺寸 | 1:1 | 否 |

关键代码 `Src/Graphics/Vulkan3D/Vulkan2D.cpp`：

```c
// 2D 层纹理永远是 496x384，与 XResolution 无关
m_context.Memory().CreateImage(496, 384, 1, VK_FORMAT_R8G8B8A8_UNORM, ...)
```

即**3D 不是"低分辨率渲染后放大"的**，全屏 4K 下 3D 已在最终视口尺寸上光栅化。真正被非整数放大的是 **2D 瓦片层**：内容固定 496×384，全屏 4K 下被拉伸 **5.625×**（双线性）→ 界面文字发糊、且因比值非整数，**有的原始像素占 5 像素、有的占 6**。

**实现**（`Src/OSD/SDL/Main.cpp` 的 `CalcScreenGeometry`）：整数倍因子取**原生信号**而非请求分辨率，因为 2D 层纹理的 texel 就是 496×384：

```c
float nativeFit = min(actualWidth / 496.0f, actualHeight / 384.0f);
int multiple = (int)nativeFit;      // 向下取整
if (multiple < 1) multiple = 1;
xResF = 496.0f * multiple;
yResF = 384.0f * multiple;
```

**实测日志**（4K 独占全屏）：

```
Video: integer scaling 5x - visible area 2480x1920 in a 3840x2160 window.
```

**取舍**（4K 16:9 面板 + 4:3 游戏）：

| 模式 | 可见区 | 黑边 |
|---|---|---|
| 关闭（默认） | 2790×2160 | 左右各 525 |
| 开启 5× | 2480×1920 | 左右各 680，上下各 120 |

即**开启后画面略小**（损失约 11% 面积）但 2D 层像素完全均匀。因此默认关闭。

> 1080p 下取 2×（992×768）；窗口模式下窗口尺寸会吸附到整数倍，所以窗口正好等于 3× 原生时两种模式结果一致。

### 3.10 4K 推荐配置（已写入实测）
以下是本机 4K 独占全屏下的推荐值，已写入 `Config/Supermodel.ini` 并实测加载正常。

> ⚠️ **本节 1~3 小节是探索过程中的历史方案，最终采用值见 3.10.4（`720 / 点阵3`）。** 演进顺序：`432/2`（有彩带）→ `360/3`（已无彩带，但点阵偏粗）→ **`720/3`（本次采用）**。

**方案 A：清爽现代（高分辨率 CRT / BVM 观感）**

| 配置 | 实际采用值 | 理由 |
|---|---|---|
| `PostCRT` | `2` | 扫描线 + 荫罩 |
| `PostScanlines` | **`360`** | 2160÷360 = **6 像素/条**，与荫罩周期整除（见 3.10.3）|
| `PostScanlineIntensity` | **`70`** | 对比度 0.321，接近真机暗缝深度 |
| `PostMaskType` | `1` | 槽孔式（世嘉街机原机）|
| `PostMaskScale` | **`3`** | **必须是 3 的倍数**，否则 RGB 子孔欠采样产生彩噪 |
| `PostMaskIntensity` | `50` | 兼顾质感与亮度 |
| `PostGlow` | `20` | 玻璃通透感；**过高会让亮部发白** |
| `PostGlowLevels` | `3` | 4K 像素多，光晕需更宽才可见 |
| `PostGlowThreshold` | `40` | 只让真正的高光发光 |
| `PostGain` | **`167`** | 补偿点阵与扫描线的光损；**过高会过曝** |
| `MSAA` | `4` | 几何边缘平滑 |
| `IntegerScale` | `1` | 点对点（2D 瓦片层固定 496×384，只有整数倍才均匀）|

**重要**：`PostGain` 与 `PostGlow` **不要同时调高**。辉光是**加算**的，而增益随后会把它一起放大，两者叠加很容易把亮部推爆。见 3.11。

**`PostMaskScale` 取值依据**：

真实街机 29 寸屏荫罩**三色组距约 0.85mm**，而 384 线信号的行距约 **1mm**——两者尺度相当。

但在本实现里，**同时满足「子孔整数像素」和「与扫描线整除」**才是硬约束（推导见 **3.10.3**）：

- **`3`**（配合 `PostScanlines = 360`）= **本机采用**：扫描行距 1.107mm、子孔 1px、完全对齐
- **`6`**（配合 `PostScanlines = 360` 或 `540`）：点阵更粗、颗粒感更强
- `4`、`5`、`2` **应避免**：子孔间距非整数像素，会产生彩噪

**方案 B：物理还原（当年框体）**

`PostScanlines = 360`、`PostScanlineIntensity = 78`、`PostMaskScale = 6`、`PostGlow = 45`、`PostGlowLevels = 6`、`PostGain = 220`。

> 原方案用 `PostScanlines = 384` + `PostMaskScale = 5`。**该组合必然产生彩波纹**（384 为奇数周期、5 非 3 的倍数），已弃用。现改用对齐的 `360 / 6`。

代价：更粗、更暗。真实老机器凑近看就是这样。

#### 3.10.1 扫描线物理尺寸：32 寸面板实测推算

> **前提更正**：显示器是 **32 寸**，不是 27 寸。像素距差 18%，直接影响扫描线的物理尺寸推算。

| 尺寸 | 屏宽 | 屏高 | 像素距 |
|---|---|---|---|
| 27" | 597.7 mm | 336.2 mm | 0.1557 mm |
| **32"（本机）** | 708.4 mm | 398.5 mm | **0.1845 mm** |

**关键机制**：`PostScanlines` 是按**整个窗口高度**（2160）铺开的，而**整数倍缩放**下画面只占 1920，所以落在画面里的线数要打折：

```
画面内有效线数 = PostScanlines × 可见区高度 / 窗口高度 = PostScanlines × 1920/2160
```

| 方案 | 画面内线数 | 行距 | 暗带 | vs 真实街机（0.99 / 0.40mm）|
|---|---|---|---|---|
| **整数倍开 + 432 行（本机采用）** | **384** | **0.922 mm** | **0.387 mm** | **仅细 7% ★** |
| 整数倍开 + 540 行 | 480 | 0.738 mm | 0.310 mm | 偏细 |
| 整数倍关 + 384 行 | 384 | 1.038 mm | 0.436 mm | 行距略粗 |
| 整数倍关 + 432 行 | 432 | 0.922 mm | 0.387 mm | 线数偏多 |

**结论**：**32 寸面板上，整数倍缩放 + 432 行恰好等于真机的 384 行与接近真机的行距**——物理尺度不用改。

#### 3.10.2 扫描线"不明显"的定量诊断（已修复）

**症状**：强度 38% 时扫描线几乎看不出。

**诊断**：扫描线对比度可解析计算。着色器做的是光束区间平均（见 3.8），振幅被 sinc 因子衰减：

\[
\text{sinc}(d)=\frac{\sin d}{d},\quad d=\frac{N\pi}{H}
\]

| 行数 | 周期 | sinc | 对比度(暗/亮) |
|---|---|---|---|
| 216 | 10.0 px | 0.984 | 0.625 |
| 384 | 5.6 px | 0.949 | 0.636 |
| **432** | **5.0 px** | **0.936** | **0.640** |
| 864 | 2.5 px | 0.757 | 0.698 |

**行数对对比度影响很小**（0.625→0.698），它**唯一**的作用是决定线条**粗细**。真正的主因是**强度**：

| 强度 | 对比度 | 平均亮度系数 |
|---|---|---|
| 38%（原值）| 0.651 | 0.810 |
| **70%（现采用）** | **0.330** | **0.650** |
| 80% | 0.250 | 0.600 |

原值 **0.651 只有 35% 的明暗差**，且铺在仅 4 像素宽的平滑余弦上，人眼会读成"整体略暗"而非"一条条线"。**真实 CRT 暗缝通常暗 60~70%（对比度 0.3~0.4）。**

**本机最终采用值**：

| 配置 | 值 | 说明 |
|---|---|---|
| `PostScanlines` | **`432`** | 画面内正好 384 条 = 街机行数 |
| `PostScanlineIntensity` | **`70`** | 对比度 0.330，接近真机 |
| `PostGain` | **`167`** | `155 × 0.70/0.65 = 166.9`；补偿强度提升带来的变暗 |

**界面已补提示**：原先 `PostScanlines` 的滑杆只显示设定值，**看不出画面里实际有多少条**（想设 384 却填 384 会得到约 341）。现在开启整数倍缩放 + 全屏时，会在滑杆下方直接显示：

```
整数倍缩放下可见区高 1920 像素，画面里实际约 384 条扫描线。
想要正好 384 条（街机行数）请填 432。
```

#### 3.10.3 荫罩与扫描线的拍频：彩波纹的根因（已修复）

**症状**：画面出现**彩色的缓慢波纹**（不是扫描线本身，而是颜色随位置起伏）。

**根因是两个图案的空间周期不整除**。两者都在**同一像素坐标空间**（窗口分辨率）里求值：

| 图案 | 周期 |
|---|---|
| 扫描线 | `H / PostScanlines` 像素 |
| 荫罩槽孔式的隔行错开 | `2 × PostMaskScale` 像素 |

原设置 `432 / 2`：`5 px` vs `4 px` → **拍频周期 `LCM(5,4) = 20 px = 3.69 mm`** —— 这就是肉眼看到的彩波纹。

更关键的是：**432 行在任何 `PostMaskScale` 下都无解**。因为 `5` 是**奇数**，而荫罩周期 `2×maskScale` **永远是偶数**，两者**不可能整除**。**"街机原行数"与"槽孔荫罩"在数学上互斥。**

**第二个独立问题：RGB 子孔欠采样。**

荫罩的 3 个 RGB 子孔在单元内以 `maskScale/3` 像素等距排布：

| maskScale | 子孔间距 | 结果 |
|---|---|---|
| **2（原值）** | **0.667 px** | ❌ **低于 1 像素 → 必然彩色混叠** |
| **3** | **1.000 px** | ✅ 每个子孔正好落在整数像素上 |
| 4 | 1.333 px | ❌ |
| 6 | 2.000 px | ✅ |

所以 `maskScale = 2` 时，荫罩本身就产生了**逐像素的彩噪**（因为三个子孔挤在不到一个像素里）。这是"彩"的来源，与拍频叠加。

**筛选：同时满足「整除对齐」+「子孔整数像素」的组合**

| 行数 | maskScale | 扫描线周期 | 荫罩周期 | 画面线数 | 扫描行距 | 点阵周期 |
|---|---|---|---|---|---|---|
| 360 | **3** | 6 px | 6 px | 320 | **1.107 mm** | 0.553 mm |
| 360 | 6 | 6 px | 12 px | 320 | 1.107 mm | 1.107 mm |
| 540 | 6 | 4 px | 12 px | 480 | 0.738 mm | 1.107 mm |
| ~~432~~ | ~~2~~ | ~~5 px~~ | ~~4 px~~ | ~~384~~ | ~~0.922 mm~~ | ~~0.369 mm~~ |
| ~~432~~ | 任意 | 5 px | 偶数 | 384 | — | **永不整除** |

**本机采用 `360 / maskScale = 3`**，理由：

1. **扫描行距 1.107mm**，最接近真实街机的 0.99mm（+12%），扫描线视觉存在感也更强
2. **子孔间距恰好 1 像素** → 每个输出像素对应一个子孔，**无彩色混叠**
3. **`6 % 6 = 0`** → 荫罩错开与扫描线完全同步，**无拍频**

**验证核对**：

```
扫描线周期 = 6 px,  荫罩错开周期 = 6 px  =>  6 % 6 = 0  (整除，无拍频)
RGB 子孔间距 = 3/3 = 1.0 px  (整数像素，无彩噪)
画面内线数 = 360 × 1920/2160 = 320 条
扫描行距 = 6.00 px × 0.1845 = 1.107 mm  (真实街机 0.99mm)
点阵周期 = 3 px × 0.1845 = 0.553 mm  (真实街机 0.85mm)
扫描线对比度 = 0.321
平均亮度 = 0.650，与之前一致 => 增益 167 无需改动
```

> **代价**：画面内线数从 384 降到 320（−17%）。**这是"无彩波纹"必然的代价**——如需保留 384 条，只能改着色器让荫罩网格锁定到扫描线周期（见下）。

#### 3.10.4 细点阵取向：`720 / 3`（本次采用，已修正彩带）

用户偏好**更细的点阵**（6px 单元观感"太别扭"），把 `PostMaskScale` 降到 **3**。但**只降点阵不改扫描线**反而制造了更严重的彩带：

原状态 `PostScanlines = 540` + `PostMaskScale = 3`：

| 图案 | 周期 | 比值 | 结论 |
|---|---|---|---|
| 扫描线 | 2160/540 = **4 px** | — | — |
| 槽孔荫罩错开 | 2 × 3 = **6 px** | 6/4 = **1.5** | ✘ 非整数倍 |

`6` 不是 `4` 的整数倍 → 每条扫描线相对荫罩的相位**漂移 2px**，累积到 **`LCM(4,6) = 12 px`** 才复位：

```
拍频周期 = 1 / |1/4 − 1/6| = 12 px
铺满 2160 行 = 屏幕上 180 条彩色横带
32 寸面板上约 2.2 mm 一条  ← 肉眼可见的"彩波纹"
```

**判据（本次总结）**：两个周期必须**成整数倍关系**（`大/小` 为整数），而不是"都能整除某个数"。`4` 与 `12` 有整数倍关系（12 = 3×4）→ 无拍频；`4` 与 `3` 没有 → 拍频 12px。

**同时满足「整数倍对齐」+「子孔整数像素（`maskScale` 为 3 的倍数）」的细点阵组合**：

| 组合 | 扫描线周期 | 荫罩周期 | 倍数 | 画面内线数 | 行距 | 子孔间距 | 结论 |
|---|---|---|---|---|---|---|---|
| **720 / 3（本次采用）** | 3 px | 6 px | **2×** | 640 | 0.55 mm | 1.0 px | ✅ |
| 360 / 3 | 6 px | 6 px | **1×** | 320 | 1.11 mm | 1.0 px | ✅ |
| ~~540 / 3~~ | 4 px | 6 px | ~~1.5~~ | 480 | 0.74 mm | 1.0 px | ❌ 12px 彩带 |
| ~~540 / 4~~ | 4 px | 8 px | 2× ✓ | 480 | 0.74 mm | **1.33 px** ❌ | ❌ 子孔欠采样 |
| ~~540 / 2~~ | 4 px | 4 px | 1× ✓ | 480 | 0.74 mm | **0.67 px** ❌ | ❌ 子孔欠采样 |

> **两条约束会互相打架**：`maskScale` 只能是 3 的倍数（3/6/9），所以"正好配合 4px 扫描线"的 `maskScale = 4` **不能用**——它虽然满足整数倍，但子孔间距 1.33px 非整数、必然彩噪。**能同时过关的只有 `maskScale = 3` 配 3px 或 6px 的扫描线周期**，即扫描线数取 **720** 或 **360**。

**第二个必须同步修正的后果：`720` 行的扫描线对比度会被 sinc 压得很低。**

光束区间平均的振幅为 `0.5 · sinc(Nπ/H)`（见 3.8、3.10.2），行数越多、sinc 越小：

| 扫描线数 | 强度 | 周期 | 行距 | 对比度 | 平均亮度 | 建议增益 | 评价 |
|---|---|---|---|---|---|---|---|
| 720 | **50%** | 3.0 px | 0.55 mm | **0.57** | 0.75 | 145 | ❌ 比用户抱怨过的 0.651 **还弱** |
| **720** | **70%** | 3.0 px | 0.55 mm | **0.38** | 0.65 | **167（不变）** | ✅ 落在真机 0.30~0.40 |
| 720 | 76% | 3.0 px | 0.55 mm | 0.33 | 0.62 | 175 | 更接近 360/70% 的观感 |
| 360 | 70% | 6.0 px | 1.11 mm | 0.32 | 0.65 | 167 | ✅ 行距最接近真机 |

**本次最终采用**：

| 配置 | 值 | 说明 |
|---|---|---|
| `PostScanlines` | **`720`** | 3px 周期，`6 % 3 = 0` 与荫罩整数倍对齐 |
| `PostScanlineIntensity` | **`70`** | 50% 时对比度仅 0.57，比"扫描线不明显"的 0.651 还弱 |
| `PostMaskScale` | **`3`** | 子孔间距恰好 1.0px |
| `PostMaskIntensity` | **`35`**（原 50） | 直接线性削减三通道分离深度，弱化固有 RGB 竖条 |
| `PostGain` | `167` | 平均亮度仍 0.65，无需改动 |

**核对**：

```
扫描线周期 = 2160 / 720 = 3.0 px
荫罩错开周期 = 2 × 3 = 6.0 px
6 % 3 = 0                      => 整数倍对齐，无拍频
RGB 子孔间距 = 3 / 3 = 1.0 px  => 整数像素，无彩噪
荫罩纵向行数 = 2160 / 3 = 720  = 扫描线数（又是一次 1:1）
扫描线对比度 = 0.38            => 真机区间 0.30~0.40
```

> **必须区分的两件事**：白字／亮边上**逐像素的 RGB 竖条纹**是荫罩本身的作用（真机亦然），只能靠降 `PostMaskIntensity` 减轻；**大面积的彩色横带**是拍频摩尔纹，靠对齐**可以完全消除**。本次解决的是后者。

> **若觉得 0.55mm 行距太细**（真机 0.99mm，此处仅一半），把 `PostScanlines` 改成 **`360`** 即可——`360 / 3` 同样完全对齐，行距 1.11mm，且增益仍是 167。

**根治方案（未实施）**：把荫罩的**垂直网格**由扫描线周期推导，而不是独立设定：

```glsl
// 让荫罩每行恰好重复整数次，任何行数都不拍频
float cellHeight = pc.resolution.y / float(pc.scanlineCount);
```

这样行数可以自由选取（例如 `400` 行 → 扫描行距 0.996mm，**物理上完美匹配真机**），荫罩自动对齐。代价是需要处理**非方形单元**（单元宽 `maskScale`、高 `cellHeight`），并让 `cellAspect` 相应调整。

#### 3.10.5「推荐配置」下拉（已实现，把数学约束固化成 UI）

上面这些约束对用户来说太隐晦，于是把它们做成 GUI 里的下拉菜单，位置在「高级设置」→「CRT 滤镜」分栏，**紧跟在「CRT 滤镜」模式选择器之后的第一行**。

**四条预设**（详见 `Gui.cpp` 的 `kCrtPresets[]`）：

| 预设 | 扫描线周期 | `PostMaskScale` | 荫罩周期 | 4K 下 `PostScanlines` | 强度 | 点阵强度 | 增益 |
|---|---|---|---|---|---|---|---|
| 细 · 现代 CRT（BVM 观感）| 3 px | 3 | 6 px | **720** | 70 | 35 | 167 |
| 细 · 点阵略粗 | 3 px | 6 | 12 px | 720 | 70 | 45 | 182 |
| 街机原机 · 扫描线最接近真机 | 6 px | 3 | 6 px | **360** | 70 | 35 | 167 |
| 街机原机 · 粗点阵 | 6 px | 6 | 12 px | 360 | 70 | 50 | 191 |

> 「荫罩周期」一列恒为 `2 × maskScale`：槽孔式的子孔隔行错开，所以真实重复周期是单元距的**两倍**。四行的 `荫罩周期 ÷ 扫描线周期` 分别是 **2 / 4 / 1 / 2**，全部为整数。`CrtMaskPeriod()` 就是算这个的。

**关键设计：扫描线数量不入表，而是算出来的。**

```cpp
// 周期固定在输出像素上 => 换分辨率观感一致
cycles = maskPeriod / scanPeriod;      // 由表构造保证整除
base   = outputHeight / maskPeriod;    // SDL_GetDesktopDisplayMode
scanlines = cycles * base;
```

4K（2160）下 `cycles × base = 2 × 360 = 720`；1080p（1080）下得到 `2 × 180 = 360`。**若把 720 写死进表，换到 1080p 周期就变成 1.5px，等于把彩带的问题换个分辨率再犯一次。**

**增益的取值依据**：以已实测的 720/70/点阵35 基准（`PostGain = 167`，平均亮度 0.65）为锚点，其余预设按荫罩透光比反推：

```
荫罩平均透光 ≈ 1 − maskIntensity × (1 − 平均孔洞率)   [孔洞率 ≈ 0.35]
点阵45%: 0.7075 / 点阵35%: 0.7725 = 0.916  => 167 / 0.916 ≈ 182
点阵50%: 0.6750 / 点阵35%: 0.7725 = 0.874  => 167 / 0.874 ≈ 191
```

**匹配与回退**：

`MatchCrtPreset()` 逐项比对 `PostScanlines / PostScanlineIntensity / PostMaskType / PostMaskScale / PostMaskIntensity / PostGain`。全部相符才高亮对应预设，否则显示 **「自定义（手动）」**——**不静默改写用户的手动值**。

> `PostGain` 也纳入比对是刻意的：它是"有没有被手改过"的最灵敏指示器。若只比对扫描线和点阵，用户调完增益后下拉框仍显示预设名，会造成"我明明选了这个预设却没生效"的误判。

**随附诊断行**（用户手改时会实时显示）：

```
扫描线周期 3.0 px，点阵周期 6 px，比值 2.00 倍
整数倍对齐：不会出现彩色横带。
```

不成整数倍时改为橙色告警并直接给出拍频周期：

```
扫描线周期 4.0 px，点阵周期 6 px，比值 1.50 倍
不成整数倍：会出现周期约 12 px 的彩色横带。
```

这条把原本要靠肉眼发现的瑕疵变成**面板直接说明**，也是"只改一个滑杆"这个坑唯一的主动拦截点。判据用 `fabs(ratio - round(ratio)) < 0.02`，留 2% 容差吸收 `2160/720` 这类浮点误差。

**代码要点**：`CrtPreset` 表、`CrtPresetOutputHeight()`、`CrtPresetScanlines()`、`MatchCrtPreset()`、`CrtMaskPeriod()` 都必须是**文件作用域**的静态函数。第一版把它们插进了 `DrawAdvancedSettings()` 内部，MSVC 报 `C2267: static functions with block scope are illegal` —— 必须放在调用点之前、函数之外。

**已实测**：源码编译通过（用 `PlatformToolset=v143` 命令行覆盖；项目文件里写的是 `v145`，本机未安装）。用户现有配置 `720 / 强度70 / 槽孔 / 粗细3 / 点阵35 / 增益167` 与预设「细 · 现代 CRT（BVM 观感）」**逐项完全一致**，下拉框应自动选中该项。

### 3.11 高光软压缩（修复"曝光过度"，已实测验证曲线）

**症状**：按 3.10 推荐值设置后，画面**像曝光过度**——但实测平均亮度并不高（见 3.6，只有无滤镜的 32% 左右）。矛盾点在于：**平均亮，亮部却糊死**。

**根因是增益的模型不对。** 原先末段是：

```glsl
colour += glow * pc.glowStrength;
colour *= pc.gain;          // ← 平乘；超过 1.0 就被硬裁成纯白
```

辉光是**加算**的，增益随后又把辉光一起放大。一个高光像素很容易算到 1.2~2.0 以上，而**硬裁会让 1.0、1.5、4.0 全部变成同一个纯白**——这就是"曝光过度"的观感来源：不是整体亮，而是**亮部丢失层次、糊成白块**。

真实显像管的束电流是**渐进饱和**的，不会硬裁。

**修复**：在增益之后加一段软高光压缩（`post.frag`）：

```glsl
if (pc.gain > 1.0)
{
    const float knee = 0.72;
    const float span = 1.0 - knee;
    vec3 over  = max(colour - knee, vec3(0.0));
    vec3 under = min(colour, vec3(knee));
    colour = under + span * (over / (over + span));
}
```

曲线性质：

- **knee 以下完全不变**（恒等映射），中间调不受影响
- knee 以上渐进趋近 1.0 但**永不超过**
- 两段在 knee 处**斜率恰好都是 1**（\(f'(o)=\text{span}^2/(o+\text{span})^2\)，在 \(o=0\) 处等于 1），所以**过渡处无可见折痕**
- 单调递增，**亮部层次全部保留**

实测数值（`knee = 0.72`）：

| 输入 | 旧（硬裁） | 新（软压缩） |
|---|---|---|
| 0.60 | 0.600 | 0.600 |
| 0.72 | 0.720 | 0.720 |
| 0.85 | 0.850 | 0.809 |
| 1.00 | 1.000 | **0.860** |
| 1.50 | 1.000 | **0.926** |
| 2.00 | 1.000 | **0.950** |
| 4.00 | 1.000 | **0.978** |

旧做法下 1.00 / 1.50 / 4.00 **完全等价于纯白**；新做法下分别是 0.860 / 0.926 / 0.978。**这是本轮"曝光过度"的真正修复。**

> **调参提示**：`PostGain` 与 `PostGlow` 是相互放大的关系（辉光加算 → 增益再乘）。觉得过曝时**优先降这两个**，而不是降 `PostMaskIntensity`。

**验证状态**：曲线数值已逐点核对，着色器编译与运行无报错。**4K 观感需用户确认。**

**调参顺序建议**（一次只动一个）：

1. **过曝 / 亮部发白** → 先降 `PostGain`，再降 `PostGlow`（两者互相放大，见 3.11）
2. **画面发闷、整体太暗** → 升 `PostGain`
3. **条纹太抢眼** → 降 `PostScanlineIntensity`
4. **想更细** → 升 `PostScanlines`（有光束积分，不会再出摩尔纹）
5. **颗粒感太强** → 降 `PostMaskIntensity` 或升 `PostMaskScale`
6. **不够"发光"** → 升 `PostGlow`（配合降 `PostGain`，避免过曝）

> 调参时 `PostCRT = 0` 会让下面所有项**全部失效**——这是最容易踩的坑。

---

## 四、截图功能

### 4.1 Vulkan 截图（已实测）

Vulkan 没有 `glReadPixels`，swapchain 内容也无法用 GDI 抓取。实现方式：

1. swapchain 创建时请求 `TRANSFER_SRC`（先查 `supportedUsageFlags`，不支持则降级并记录日志）
2. 合成完成后、转 `PRESENT_SRC` **之前**插入：
   `COLOR_ATTACHMENT → TRANSFER_SRC → vkCmdCopyImageToBuffer → 转回 COLOR_ATTACHMENT`
3. **延迟一帧回读**：等到该帧槽位再次轮到时读取（此时 `BeginFrame` 的 `vkWaitForFences` 已保证拷贝完成），**零额外等待**
4. 像素转换、压缩、写文件全部在**工作线程**

默认快捷键 `Alt+S`（`UIScreenShot`）。

### 4.2 PNG 输出

`Src/Util/PNGFile.h`（新增）：使用仓库内已有的 **zlib**（`Libraries/zlib-1.3.1`，已作为项目引用链接）做 deflate。

| 格式 | 4K 单张体积 |
|---|---|
| BMP（旧）| 33,177,722 字节（33 MB）|
| PNG（现）| 536 ~ 740 KB |

约小 **50 倍**。

> ⚠️ `Util::WriteSurfaceToBMP` 与 `Util::PNG::WriteRGB` 的**返回值是反的**（**成功返回 `false`**）。这是历史约定，两个函数都保持一致并加了注释。

### 4.3 音频不被截图打断（已实测）

截图最初会导致**严重**音频卡顿，原因是两层：

1. `vkQueueWaitIdle` 在 EndFrame 里等 GPU → 改为延迟回读
2. **PNG 压缩在主线程**（24MB 原始数据 zlib 压缩耗时数百毫秒）→ 移到工作线程

A/B 实测（同场景同时长）：

| 阶段 | 欠载次数 |
|---|---|
| 修复前（GPU 等待 + 主线程压缩）| **56** |
| 压缩移到工作线程 | 6 |
| 6 张快速连拍 | 4 |
| **无截图基线** | **3** |

### 4.4 文件名唯一性

文件名只精确到秒，快速连拍会互相覆盖。**查重必须在工作线程写入前做**——在请求时查是无效的，因为那时文件还没创建（稍后才由工作线程写）。

实测：请求 6 张 → 产出 6 张，文件名全部唯一（`_2`/`_3`… 后缀）。

---

## 五、音频

### 5.1 采样数必须由 RefreshRate 推导（已实测）

**问题**：音频周期性卡顿。

**根因**：`Src/Model3/DSB.h` 把每帧采样数写成编译期常量：

```c
#define NUM_SAMPLES_PER_FRAME   (44100/60)     // = 735
```

它在 57.5242Hz 下意味着 SCSP 每秒只产 `735 × 57.5242 = 42280` 个采样，而声卡消耗 44100 → **恒定 4.1% 缺口**，缓冲周期性排空。

同时 `Audio.cpp` 用 `SoundFreq` 算每帧字节数，与 `RefreshRate` 是**两套时钟**。

**修复**：

- 新增 `GetSamplesThisFrame()`，以 `RefreshRate` 为唯一基准 + **分数采样累加器**（在 766/767 之间交替，长期精确等于 766.6339）
- DSB 的 MPEG 音乐一并跟上，包括它自己的 `11000000/60` 帧周期（改为按帧推导）
- `OutputAudio` 的采样上限放宽到缓冲真实容量（原来正好卡在 735，会周期性丢掉第 736 个采样）

实测：

```
60fps:      Audio: 735 samples per frame nominal (735.0000 exact) at 60.0000 Hz.
57.5242Hz:  Audio: 766 samples per frame nominal (766.6339 exact) at 57.5242 Hz.
```

### 5.2 缓冲区大小与进程优先级（已实测）

**缓冲区**：原来硬编码 200ms，对主循环的任何停顿都太短。

```
改前: 200 ms (20% of maximum)
改后: buffer 400 ms (40% of maximum)
```

配置 `AudioLatency`（5–100）。

**进程优先级**：Windows 会给前台程序调度加成、压制后台程序。点其他窗口时模拟器被降级，正好卡住逐帧的音频生产。

⚠️ **踩坑**：只在启动时设置**会被重置**（实测读回是 `NORMAL`）。必须**在游戏会话开始前再设一次**。

实测确认：

```
游戏中实际优先级类 = 0x00008000  (ABOVE_NORMAL)
Process priority class set, now 0x00008000 (above normal).   ×2
```

配置 `HighPriority`（默认开）。**不推荐使用 `REALTIME_PRIORITY_CLASS`**，那会让模拟器饿死整个系统。

### 5.3 异步声板线程（已实测，**本轮核心修复**）

**问题**：拖动窗口标题栏时，声音循环卡顿。

**机制**：音频是**逐帧在主线程产生**的。拖动标题栏会让 Windows 进入**模态移动循环**，主循环被阻塞 → 音频生产停摆 → 缓冲排空 → `underRunLoop` 回绕 → 听到循环卡顿。**点其他程序**也是同一机制。

**关键发现**：Supermodel **本来就内置了异步声板线程**，只是被硬编码关闭：

```c
syncSndBrdThread = true;  // drive the sound board thread from the main frame loop (the
                          // audio-callback-driven variant is prone to losing its wake-up
                          // and starving the audio device of data)
```

它由**音频回调**驱动（`SetAudioCallback(AudioCallback, this)`），与帧循环完全解耦。原作者关闭它是因为一个**丢失唤醒竞态**：

`WakeSoundBoardThread()` 先读 `sndBrdThreadRunning`，若线程"正在运行"就**跳过发信号**。但线程可以在这次读取之后、进入等待之前完成产出并置 `running = false` —— 信号就丢了，只能等**下一次**音频回调才被唤醒。

**修复三处**：

1. **`WakeSoundBoardThread()` 无条件发信号**，去掉会丢信号的 `!sndBrdThreadRunning` 判断
2. 新增 `IsAudioBufferFull()`（`Src/OSD/SDL/Audio.cpp`），**工作线程在产出之前先检查**缓冲是否已满——这样多余唤醒完全无害（不会产生 over-run）
3. `syncSndBrdThread` 接入配置 `AsyncSoundBoardThread`（**默认开启**）

**实测数据**：

| 场景 | 同步模式 | 异步模式 |
|---|---|---|
| 正常游玩 80 秒 | 0 under / 0 over | **0 / 0** |
| **拖动窗口标题栏** | **8 under** | **0** |
| 失去焦点 30 秒 | 0 / 0 | 0 / 0 |

异步模式下拖动时窗口标题显示 `u0`，**音频缓冲从未欠载**。

> **取舍说明**：异步模式下拖动期间**画面仍会冻结**（主循环被模态循环阻塞），但**音频持续推进**。因此拖动结束后，音乐可能比画面"领先"了拖动时长。这是"声音不卡"与"音画严格同步"之间的取舍，选择前者是用户明确要求。

---

## 六、手柄与按键

### 6.1 投币 / 启动绑定（已修复）

**问题**：内置默认值把投币和启动绑到了**摇杆按下**（L3/R3）。

```
InputStart1 = KEY_1,JOY1_BUTTON9     → 索引 8 = L3（左摇杆按下）
InputCoin1  = KEY_3,JOY1_BUTTON10    → 索引 9 = R3（右摇杆按下）
```

**根因**：配置默认值有**三处来源**，互不一致：

| 来源 | 何时生效 |
|---|---|
| 文件模板 `Src/OSD/ConfigFile.h` | 全新安装 |
| 运行期默认 `DefaultConfig()`（`Src/OSD/SDL/Main.cpp`）| **ini 缺键时** ← 坑在这 |
| 标准预设 `s_standardMappings` | 点"应用推荐键位" |

**修复**：三处全部对齐为 `BUTTON8`（Start）/ `BUTTON7`（Back/Select）。

**按键按"位置"对应，不按印刷字**：

| 索引 | 位置 | 各家印字 |
|---|---|---|
| 7 | 中间左侧小键 | Xbox `Back` · Sony `Select` · 新柄 `Share` |
| 8 | 中间右侧小键 | Xbox `Start` · Sony `Option` · 新柄 `Menu` |

### 6.2 跨游戏按键冲突（已修复）

用 `Config/Games.xml` 里每款游戏**实际声明的输入类型**（`<input type="...">`）做了逐游戏冲突扫描，发现多个真冲突：

| 项目 | 改前 | 后果 |
|---|---|---|
| `InputGearShift3` | `BUTTON3`（X）| **挂三档 = 同时踩油门** |
| `InputGearShiftN` | `BUTTON7`（Back/Select）| **挂空档 = 投币** |
| `InputTwinJoyTurbo1/2` | `BUTTON7`/`BUTTON8` | **Virtual On 推涡轮 = 投币/启动** |
| `InputRearBrake` | `BUTTON6`（RB）| **Harley 后刹 = 升档**（该游戏同时声明 `shiftupdown` 和 `harley`）|
| `InputVR1` / `ViewChange` | 共用 `BUTTON2` | 合法（游戏互不共存）但不够清晰 |
| `InputVR2` / `HandBrake` | 共用 `BUTTON4` | 同上 |

**修复后的赛车布局**（用户指定）：

| 功能 | 绑定 | 位置 |
|---|---|---|
| 油门 `InputAccelerator` | `KEY_UP,JOY1_BUTTON3` | **X** |
| 刹车 `InputBrake` | `KEY_DOWN,JOY1_RZAXIS` | **RT**（xinput）|
| 升档 `InputGearShiftUp` | `KEY_Y,JOY1_BUTTON6` | **RB** |
| 降档 `InputGearShiftDown` | `KEY_H,JOY1_BUTTON5` | **LB** |
| 空档 `InputGearShiftN` | `KEY_T,JOY1_BUTTON1` | A |
| 视角 `InputVR1..VR4` | `BUTTON2` / `BUTTON4` / `BUTTON9` / `BUTTON10` | B / Y / L3 / R3 |
| 切视角 `InputViewChange` | `KEY_A,JOY1_POV1_UP` | 十字键↑ |
| 手刹 `InputHandBrake` | `KEY_S,JOY1_POV1_DOWN` | 十字键↓ |

**验证结果**：

```
预设 vs 运行期默认:  不一致 0 处
预设 vs 文件模板:    不一致 0 处
按 63 款游戏的实际输入类型扫描冲突:  0
```

### 6.3 扳机绑定与输入系统（已实测）

**RT 的内部表示与输入系统有关**：

| 输入系统 | 两扳机 | 右扳机（刹车）|
|---|---|---|
| `xinput` / `sdlgamepad` | 各占一轴 | `JOY1_RZAXIS` |
| `dinput` / `rawinput` / `sdl` | 共用 Z 轴 | `JOY1_ZAXIS_POS` |

**切换输入系统不会自动重配扳机**，会导致刹车失灵。为此在「常用」页增加了**自动检测**：不匹配时显示橙色警告 + 「修复扳机绑定」按钮。

实测（像素扫描验证渲染）：

```
正确匹配时橙色警告像素数: 0     ← 不打扰
不匹配时橙色警告像素数: 816   ← 正确报警
```

### 6.4 设置界面的手柄导航（已修）

**根因**：GUI 只初始化了 SDL 视频子系统，**没有初始化手柄子系统**。ImGui 靠 `SDL_NumJoysticks()` 找手柄，而该函数在未初始化对应子系统时**恒返回 0**。

SDL 直接验证：

```
after SDL_Init(VIDEO) only:      SDL_NumJoysticks() = 0
after SDL_InitSubSystem(GC):     SDL_NumJoysticks() = 1
                                 [0] name='Xbox 360 Wireless Controller'
```

**修复**：GUI 创建时初始化 `SDL_INIT_GAMECONTROLLER`；增加插拔自动重新检测 +「重新检测」按钮。

> **现象说明**：Xbox 360 **无线**手柄闲置数分钟会自动休眠，此时 SDL 看不到设备（日志为 `0 joystick(s)`）。按一下唤醒、再按第二下才会被识别——这是无线套件的正常行为。

### 6.7 即时存档 / 读取存档（原版已有，快捷键保持 F5 / F7）

**结论：这个功能原版就有，无需新增。** 快捷键维持：

| 动作 | 快捷键 | 配置键 |
|---|---|---|
| 存档 | **F5** | `UISaveState` |
| 切换存档位（1–8）| F6 | `UIChangeSlot` |
| 读档 | **F7** | `UILoadState` |

`Src/OSD/SDL/Main.cpp` 中已有完整实现（`SaveState()` / `LoadState()`）：

- 存档路径：`Saves/<游戏名>.st<槽位>`，槽位 `1`–`8` 由 `F6` 循环切换，默认从槽位 1 开始
- 含**文件格式版本号**（`STATE_FILE_VERSION = 6`）与 ROM 集名校验，版本不符会明确拒绝加载
- 存取前后自动 `PauseThreads()` / `SetAudioEnabled(false)`，避免存档瞬间的音频抖动

游戏内按 **Tab** 打开设置叠加层后，最下方的「即时存档」页会列出 8 个槽位，每行都有「保存 / 读取」按钮，可直接选择槽位而不必用 F6 循环。

**关于 F9 / F10**：这两个键在 Supermodel 里**原本已被占用**为音乐音量增减（`UIMusicVolUp` / `UIMusicVolDown`），因此**未改为存档键**，以免与既有功能冲突。

> 如需改成 F9 / F10，可在**「按键」页**里双击对应行直接改绑（`按键` 页只显示所选游戏用到的按键，UI 类动作选默认/全局游戏即可看到）。也可直接编辑 ini：
> ```ini
> UISaveState = KEY_F9
> UILoadState = KEY_F10
> ```
> 但那样会与音乐音量键冲突，需同时把 `UIMusicVolUp` / `UIMusicVolDown` 改到别的键。

### 6.6 视角单键循环（已实测）

**问题**：Daytona 2 等游戏有 **4 个独立视角按钮**（VR1–VR4，代码注释显示街机框体上是红/蓝/黄/绿四色按键），占用 4 个手柄键位。

**实现**：新增「单键循环切换」模式。核心改动是让 4 个视角位由输入层决定，而不是直接读按键：

```c
// Src/Model3/Model3.cpp
data &= ~(Inputs->IsVRSelected(0)<<0); // VR1 Red
data &= ~(Inputs->IsVRSelected(1)<<1); // VR2 Blue
data &= ~(Inputs->IsVRSelected(2)<<2); // VR3 Yellow
data &= ~(Inputs->IsVRSelected(3)<<3); // VR4 Green
```

`CInputs::IsVRSelected()` 按模式返回不同结果：

- **循环开启**：始终恰好一个视角有效，按 `InputVRNext` 前进（`Pressed()` 是边沿检测，按住不会连跳）
- **循环关闭**：退化为原来的「哪个按键被按住就是哪个视角」，行为与改动前完全一致

**配置**：

| 配置 | 默认 | 说明 |
|---|---|---|
| `VRCycle` | `true` | 单键循环开关 |
| `VRDefaultView` | `2` | 起始视角（0=VR1 … 3=VR4）|
| `InputVRNext` | `KEY_D,JOY1_BUTTON4` | 循环键：手柄 **Y** / 键盘 **D** |

**按键选择依据**：手柄 Y（索引 3）在这些赛车游戏里**只被 VR2 占用**，而 VR2 在循环模式下被忽略 → 两者互斥、共享同一个键不冲突。键盘用 `D`（否则 `KEY_Y` 会与升档冲突），`D` 本身只属于 VR3，同样互斥。

**实测**（日志诊断为按 6 次键的索引序列）：

```
VRDIAG: VRCycle=1 VRDefaultView=2
VRDIAG: cycle -> index 3   ← 第1次
VRDIAG: cycle -> index 0   ← 第2次
VRDIAG: cycle -> index 1
VRDIAG: cycle -> index 2
VRDIAG: cycle -> index 3
VRDIAG: cycle -> index 0   ← 第6次，正确绕回
```

> **验证方法说明**：最初试图用截图对比判断视角是否切换，但**测试时游戏还停在选单/演示画面**，视角差异只在实际比赛中可见，因此该测试无效。改用**直接打印循环索引**才得到确定性结论——这也是本项目中"先确认测试前提成立"的又一例（另见 10.5）。

### 6.5 遗留：默认值来源仍不一致

对比「文件模板」与「运行期默认」两个来源，**非赛车部分仍有约十余处不一致**（格斗/足球/钓鱼/滑雪/Virtual On 的次要按键）。这些**只在 ini 缺键时生效**，且已在 6.2 中修掉了所有**实际冲突**，剩余为纯数值差异，未处理。

---

## 七、界面外壳与用户体验

### 7.1 GUI 常驻（已实测）

**改造**：把 `RunGameSession()` 从 `main()` 中拆出，GUI 作为**应用外壳**只创建一次。

Esc 退出游戏后回到设置界面，像 PCSX2 那样。同一进程连续跑两个游戏会话实测均正常（退出码 0，第二个会话音频重建 0 欠载）。

**关于线程**：**不需要**把 GUI 放到独立线程。两个循环（GUI 循环、游戏循环）**永远不会同时活跃**，只是顺序执行——GUI 跑到你启动游戏 → 隐藏自己 → 游戏跑到 Esc → 返回来再显示。既无同步开销也无线程安全问题。

**顺带修的隐患**：原来销毁 OpenGL 用 `SDL_GL_GetCurrentContext()`（"当前谁在用删谁"）。GUI 的 GL 上下文现在跨会话存活，迟早会误删，改为显式保存并删除游戏窗口自己的上下文。

### 7.2 控制台窗口隐藏（已实测）

左侧黑色窗口是 **Windows 控制台**（`SubSystem = Console`）。从资源管理器双击启动时会额外出现，纯噪声。

默认隐藏，日志照常写 `Supermodel.log`。`ShowConsole = 1` 或 `-show-console` 可恢复，设置界面「高级设置」页有勾选框，**勾选后立即生效**。

### 7.3 窗口尺寸记忆（已实测）

新增 `GuiWindowWidth` / `GuiWindowHeight`，保存**客户区**尺寸（与 `SDL_CreateWindow` 入参一致，可精确往返）。

**DPI 处理**：缩放**只在首次运行**（无保存值）时参与计算。保存值原样使用，**绝不二次乘 DPI**——否则每启动一次窗口就会被乘一次，三次后撑满屏幕。此不变式已实测验证。

| 显示器 | 缩放 | 首次运行结果 |
|---|---|---|
| 1080p/1440p/4K @100% | 1.0 | **1280×800** |
| 4K @150% | 1.5 | 1920×1200 物理 = 视觉上 1280×800 |
| 4K @200% | 2.0 | 2560×1600 物理 = 视觉上 1280×800 |

两道安全网：最小 640×480；不超过桌面可用区域（自动避开任务栏）。

日志便于远程诊断：

```
GUI window: 1920x1200 (saved 0x0, display DPI scale 1.50, usable area 3840x2088).
```

### 7.4 恢复默认（已强化）

原本已有「恢复默认」按钮。改动：点击后**立即写入** `Supermodel.ini`，而不是等「保存设置」。

理由：这个按钮是**从损坏配置中恢复的兜底手段**，不能因为用户之后选择「不保存退出」而丢失。

### 7.5 高级设置页（已实测）

在「按键」页后新增，包含 MSAA、FXAA、CRT 滤镜、辉光等全部后处理选项，每项都有中文悬停说明。检测到当前是 OpenGL 后端时会明确提示该页不生效。

#### 7.5.1 与「画面」页的重复项（已修复）

**问题**：`MSAA`、`PostCRT`、`PostScanlines` 等同一批设置在「画面」和「高级设置」**两个页里都出现**。

**根因**：`CreateControls()` 会把 `"Video"` 分组下**所有**配置项按通用控件渲染一遍，而「高级设置」里这些项又是手工绘制的，于是两边各出现一次。

**修复**：把原来的 `IsQuickSetting()` 重命名为 `HasDedicatedUI()` 并扩充，语义更准确——**凡是已在别处手工绘制过的键，通用渲染器一律跳过**。新增跳过项：

| 键 | 说明 |
|---|---|
| `MSAA`、`PostFXAA` | 抗锯齿 |
| `PostCRT`、`PostScanlines`、`PostScanlineIntensity` | CRT 扫描线 |
| `PostMaskType`、`PostMaskScale`、`PostMaskIntensity` | 荫罩点阵 |
| `PostGlow`、`PostGlowLevels`、`PostGain` | 辉光与增益 |

**同时补上「高级设置」里缺失的 `PostGlowLevels`**：它原先只靠「画面」页自动渲染出现，加入跳过名单后若无手工控件就会**彻底消失**。现已在「辉光强度」下方补上滑杆，并说明级数与光晕半径的关系。

**结果**：「画面」页只保留常规显示选项（分辨率、全屏、宽屏、垂直同步、渲染引擎、缩放等）；抗锯齿与滤镜全部集中在「高级设置」。

### 7.6 关闭按钮修复（已实测）

**问题**：窗口模式下点右上角 X 关不掉，必须按 Esc。

**这是 GUI 常驻改造引入的回归**。SDL 源码（`Libraries/SDL2-2.30.11/src/events/SDL_windowevents.c`）：

```c
if (windowevent == SDL_WINDOWEVENT_CLOSE) {
    if (!window->prev && !window->next) {        // ← 必须是"唯一"窗口
        if (SDL_GetHintBoolean(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, SDL_TRUE)) {
            SDL_SendQuit();
```

关闭按钮只发 `SDL_WINDOWEVENT_CLOSE`，而 `SDL_QUIT` **只在关闭的窗口是唯一窗口时**才发。GUI 窗口常驻后就有两个窗口，`SDL_QUIT` 永远不来。之前 GUI 用完即毁，所以没这问题。

**修复**：两个输入系统（`CSDLInputSystem::Poll` 与 `CDirectInputSystem::Poll`）都显式处理 `SDL_WINDOWEVENT_CLOSE`。

实测（发送 `WM_CLOSE`）：

```
✓ 关闭按钮生效，进程已退出 (code 0)
[Info]  Program terminated normally.
```

### 7.7 退出游戏后设置界面鼠标指针消失（已修复，待用户确认）

**症状**：从游戏退回设置界面后，**鼠标指针不见了**，但仍能大致选中选项。

**根因是两个不对称的事实叠加：**

**① Win32 `ShowCursor()` 是计数器，不是开关。**

`InputSystem = xinput` 对应 `CDirectInputSystem`，其实现用的是 Win32 API：

```cpp
ShowCursor(visible ? TRUE : FALSE);   // TRUE 加一，FALSE 减一
```

**只有计数 ≥ 0 时 Windows 才绘制指针。** 原代码只是盲目加减。

**② 隐藏了却没人恢复。**

`Src/OSD/SDL/Main.cpp:1443`（游戏启动时）：

```cpp
// Hide mouse if fullscreen, enable crosshairs for gun games
Inputs->GetInputSystem()->SetMouseVisibility(!s_runtime_config["FullScreen"].ValueAs<bool>());
```

`FullScreen = 1` 时执行 hide → 计数变 **-1**。而**游戏退出路径上没有任何地方把它加回来**（`UngrabMouse` 只在按键设置界面和调试器里调用）。

于是计数永远停在负数，**指针永久不可见**。而且**每次全屏启动游戏都会再减一**，越来越严重。

**为什么设置界面里那句 `SDL_ShowCursor(SDL_ENABLE)` 没用**：`SDL_ShowCursor` 只改 SDL 自己的可见标志，**对 Win32 的显示计数没有任何影响**，所以无法把指针找回来。这也解释了"能选中但看不见"——鼠标悬停与点击来自**窗口消息**，与指针是否被绘制无关。

**修复（两处）**：

**其一，让 `SetMouseVisibility` 幂等**（`DirectInputSystem.cpp`）——把计数驱动到与期望状态一致，而不是盲目加减：

```cpp
int count = ShowCursor(wantVisible ? TRUE : FALSE);
while (wantVisible ? (count < 0) : (count >= 0))
    count = ShowCursor(wantVisible ? TRUE : FALSE);
```

逐状态推演验证（全部收敛、结果正确、无死循环）：

| 期望可见 | 起始计数 | 调用序列 | 结束计数 | 结果 |
|---|---|---|---|---|
| 是 | -1 | 0 | 0 | 正确 |
| 是 | -3 | -2,-1,0 | 0 | 正确 |
| 是 | 5 | 6 | 6 | 正确 |
| 否 | 2 | 1,0,-1 | -1 | 正确 |
| 否 | 0 | -1 | -1 | 正确 |
| 否 | -2 | -3 | -3 | 正确 |

这样重复请求同一状态也不会让计数漂移。

**其二，在游戏会话结束时释放鼠标**（`Main.cpp` 的 `RunGameSession`，`Exit:` 标签处）：

```cpp
if (Inputs != nullptr)
  Inputs->GetInputSystem()->UngrabMouse();
```

**这里用 `UngrabMouse()` 而不是 `SetMouseVisibility(true)`**，因为 raw-input 后端把两者绑在一起：只要鼠标处于抓取状态，它就一直隐藏指针，无论请求什么可见性。所以对那个后端而言，**释放抓取才是真正让指针回来的动作**；对其他后端两者等价。

放在 `Exit:` 而非 `Supermodel()` 的各个 `return` 前，是因为**它覆盖该函数的所有返回路径**（含错误路径），而 `Inputs` 在此处仍然存活（下一行才 `delete`）。

**为什么不在 GUI 里修**：GUI 侧的 `state->inputs` 只在用户打开「按键/灵敏度」页时惰性创建，且每帧绘制结束后又被置回 `nullptr`，因此在那里的修复**几乎永远不会执行**。而 `GuiRunOnce` 里原有的 `SDL_ShowCursor(SDL_ENABLE)` 也修不了这个问题（原因见上）。

**验证状态**：逻辑推演与构建已通过；**"退出游戏后指针回来"需要用户实测确认**（该验证必须走一次完整游戏会话）。

### 7.8 界面语言（英文 / 中文，已实测）

**需求**：「常用」页顶部加语言选择，默认英文，可切中文。

**规模实测**：

| 部分 | 数量 |
|---|---|
| 界面标签/控件 | 214 处（`WidgetLabel` 映射 66 项）|
| 悬停帮助说明 | 13 处 / 约 68 行 |
| 标签页 | 10 个 |
| 游戏名 | 63 条目（27 个不重复名）|
| **合计** | 约 290 条 / 2450 汉字 |

**不使用 gettext / ICU**：只有两种语言、无复数形式（中文本身没有）、无 RTL、无日期与数字本地化需求。这些工具是为了解决本界面**不存在**的问题，却会引入构建步骤和外部依赖。**一个查找表即可。**

#### 实现方式：源码中文作查表键

**现有中文文本原地保留**，外面套一层 `Tr()`。理由：翻译工作量与"重写为英文键"**完全一样**（都要一张完整对照表），但不重写现有字面量，**漏译只会退回中文，不会变成空白标签**——这是最关键的取舍。

```cpp
// Localization.cpp
{ "启动游戏", "Start Game" },   // 中文即键
{ "保存设置", "Save Settings" },
...
const std::string(Tr(found->second)) + "##" + key;   // WidgetLabel 包一层
```

#### 关键实现细节

**① `/utf-8` 编译选项必须同时施加于新文件。**
`Gui.cpp` 原本就带 `/utf-8`。查表靠**字节精确匹配**，若 `Localization.cpp` 用默认代码页编译，中文字面量字节不同，**整张表会全部失配**。已在 `Supermodel.vcxproj` 中为 `Localization.cpp` 同样加上。

**② 字体无需改动。**
界面加载的是**微软雅黑**（`C:\Windows\Fonts\msyh.ttc`），**自带拉丁字形**，英文界面不需要换字体。这是个常见风险点，已排除。

**③ 用包装函数而非逐处改调用。**
19 个薄包装（`UiText` / `UiButton` / `UiSep` / `UiTabItem` …）让调用点保持原样，前缀统一替换即可：

```cpp
static void UiSep(const char* label) { ImGui::SeparatorText(Tr(label)); }
static bool UiButton(const char* label, const ImVec2& size = ImVec2(0, 0)) { return ImGui::Button(Tr(label), size); }
```

> **踩过的坑**：批量替换时把**包装函数自身的定义体**也改掉了（`UiText` 调 `UiText` → 无限递归）。必须改回 `ImGui::` 前缀。这类自递归编译器不会报警，只会在运行时爆栈。

**④ `Tr()` 返回的必须是静态存储期指针。**
ImGui **不复制**标签文本，弹窗打开期间会一直持有这些指针。因此：选项列表的译文写入**调用方提供的数组**（`UiItems()`），而非共享缓冲区——否则第二个下拉框会破坏第一个仍在显示的列表。

```cpp
static const char** UiItems(const char* const* items, int count, const char** storage);
static const char* msaaTr[3];   // 调用方持有，跨帧有效
ImGui::Combo(Tr("硬件 MSAA##MSAA"), &msaaIndex, UiItems(msaaItems, 3, msaaTr), 3);
```

**⑤ `TextDisabled` / `SetTooltip` 的 printf 格式串要先翻译再格式化**，并把结果作为纯 `"%s"` 传给 ImGui——否则译文里的 `%` 会被当成转换说明符。为此使用一小圈缓冲（`UiTextDim` / `UiTooltip`）。

**⑥ `##` 后缀不翻译。**
`WidgetLabel` 已输出 `标签##配置键`，ImGui 的控件身份取自 `##` **之后**的部分。翻译时只译 `##` 之前，**控件身份才能跨语言保持不变**。

#### 当前状态

| 项目 | 状态 |
|---|---|
| 基础设施（`Localization.h/.cpp`、`Tr()`、语言下拉、`UiLanguage` 配置）| 完成 |
| 界面标签 / 按钮 / 标签页 / 选项列表 | **完成**（`kEnglish`，272 条）|
| 游戏列表名称 + 版本号 | **完成**（见 7.8.1）|
| 帮助说明（`HelpMarker` / `SetTooltip`）| **已接入但未翻译**（约 68 行）|

`HelpMarker` 内容已改为经 `Tr()` 输出，所以补帮助说明**只需补表，不用改代码**。

#### 7.8.1 游戏名与版本号的中文化（已实测）

**方向相反**：界面文本是"中文作键"，而游戏名来自 `Games.xml` 的**英文**，需要英文→中文。因此单独建了两张表，并提供两个函数：

```cpp
const char* TrGameTitle(const char* englishTitle);      // 英文名 -> 中文名
std::string TrGameVersion(const std::string& version);  // "Japan, Revision A" -> "日本, A 版"
```

**游戏名**（27 条不重复名）：**并非世嘉官方译名**——这批游戏大多未在中文区发行，采用中文玩家与港台街机圈的通行叫法。其中 `Virtua` 与 `Virtual-On` 系列沿用世嘉自家的中文品牌（**VR 战士**、**VR 射手**、**电脑战机**）；其余为社区通行译名。

#### 7.8.2 「未收录」ROM 的识别与分组（已实测）

**这是一个排查了很久的假象**：中文界面下，用户报告"游戏列表还是英文"。

**真正的原因不是翻译失效**，而是 **ROM 目录里有 7 个 Supermodel 不支持的 set**：

```
bass  dirtdvlsa  lostwsgo  scuda  scudj  srally2x  vs299b
```

它们**不在 `Games.xml` 的定义里**，因此匹配不到任何游戏 → 只能显示**原始文件名** → 译名表自然查不到。而它们按字母排序时**紧挨着已翻译的同系列条目**（`scuda`/`scudj` 就在「飞弹赛车」旁边），所以看起来像"名字没变"。

实测数据（`GUI:` 日志行）：

```
GUI: interface language = 中文 (UiLanguage = 1).
GUI: 56 ROM(s) listed, 49 title(s) translated for the current language, 7 without a definition in Games.xml.
```

49 个已支持全部翻译成功（`0` 条标题缺失译名），7 个未收录。

**关键事实**：这 7 个**根本无法启动**。`GameLoader.cpp`：

```cpp
auto it = m_game_info_by_game.find(game_name);
if (it == m_game_info_by_game.end())
{
    ErrorLog("Cannot load unknown game '%s'. Is it defined in '%s'?", ...);
```

所以把它们翻译成中文**反而会误导**——看起来是正常条目，点进去却报错。

**处理方式**：

1. **标记而非翻译**：显示为 `原始名   （未收录）`，**灰色文字**，悬停提示说明原因
2. **统一排到列表底部**（用户要求）：排序改为**先「已支持」再按名称**，未收录的一组集中在末尾

```cpp
std::sort(entries.begin(), entries.end(), [](const RomEntry& a, const RomEntry& b) {
    if (a.supported != b.supported)
        return a.supported;                     // 可运行的排前面
    return ToLowerAscii(a.title) < ToLowerAscii(b.title);
});
```

**排序实测验证**（用真实 56 个 ROM 模拟 C++ 比较逻辑）：

```
... vs29915      Virtua Striker 2 '99.1       已支持
    vs2v991      Virtua Striker 2 '99.1       已支持
    bass         bass                         ◀ 未收录
    dirtdvlsa    dirtdvlsa                    ◀ 未收录
    lostwsgo     lostwsgo                     ◀ 未收录
    scuda        scuda                        ◀ 未收录
    scudj        scudj                        ◀ 未收录
    srally2x     srally2x                     ◀ 未收录
    vs299b       vs299b                       ◀ 未收录
最后一个已支持条目位置: 48   第一个未收录位置: 49
==> 正确：未收录条目完整排在全部已支持条目之后
```

**为什么之前"看起来没生效"**：未收录条目与已支持条目**交错排列**，视觉上盖住了翻译生效的事实。分组到底部后，上半部分 49 条中文名连续可见，一眼就能看出翻译正常。

> **教训**：这次排查走了弯路。有效的做法是**先加诊断日志**（`GUI: N ROM(s), M translated, K unsupported`），一条日志就把问题从"翻译失效"改判为"7 个未收录 set"。**遇到"看起来没生效"的现象时，先量化再猜。**


**同一游戏不同地区标题的情况统一命名**：`Get Bass`（ゲットバス）是日本版标题，出口版为 `Sega Bass Fishing`——**同一款游戏的两个地区名**。因此两者共用中文名 `世嘉钓鱼`，日本版在括号内保留原始标题以便区分：

| ROM | 英文标题 | 中文显示 |
|---|---|---|
| `bassdx` | Sega Bass Fishing | 世嘉钓鱼 (美版, 豪华版) |
| `getbassdx` | Get Bass: Sega Bass Fishing | 世嘉钓鱼 (Get Bass) (日本, 豪华版) |
| `getbassur` | Get Bass: Sega Bass Fishing | 世嘉钓鱼 (Get Bass) (日本, 立式) |
| `getbass` | Get Bass: Sega Bass Fishing | 世嘉钓鱼 (Get Bass) (日本, 标准版) |

> 起初译作 `Get Bass 世嘉钓鱼`（中英混搭），已按上表统一。这是全部 27 个名字里**唯一**一处系列命名不一致的地方，其余系列本就统一（`VR 战士 3` / `VR 战士 3 团队战`、`VR 射手 2` 全家、`飞弹赛车` / `飞弹赛车 Plus`、`街头格斗` / `街头格斗 最终版`）。

**示例输出**：

```
戴通纳 USA 2 - 边缘之战 (日本, A 版)        daytona2      1998
戴通纳 USA 2 - 动力版 (日本)                dayto2pe      1998
泥地恶魔 (澳洲, A 版)                      dirtdvlsau    1998
泥地恶魔 (出口版, G 版?)                    dirtdvlsg     1998
紧急救护车 (出口版 / 日本 / 美版 / 美版场测?)
格斗之蛇 2 (日本 / 日本, A 版)
世嘉钓鱼 (Get Bass) (日本, 立式)            getbassur     1997
星球大战三部曲 街机版 (出口版, A 版)        swtrilgy      1998
VR 战士 3 (日本, D 版)                     vf3           1996
电脑战机 Virtual-On 神谕探戈 (日本, B 版)    von2          1998
VR 射手 2 (Step 2.0, 出口版, 美版)          vs2           1997
```

**版本号按词元翻译**：版本串是逗号分隔的词元（如 `Export, Twin/DX, Revision A`），**逐词元查表再拼回**，共 24 个词元。这样：

- 未翻译的组合仍**可读**，不会被丢弃
- 只需加词元，不必为 39 种完整组合各写一条
- `Step 1.5`、`16.09.98` 这类**版本标识与日期保持原样**（它们是编号而非词语）

**搜索同时匹配中英文**：查 `title`（中文）、`version`（中文）、`entry.title`（英文原名）、`entry.version`（英文原版）、`setName`。所以中文界面下输入 `dirt` 依然能筛出来——**切换语言不会破坏搜索习惯**。

**译名不确定性**（如需调整请改 `Localization.cpp` 的 `kGameTitles`）：

| 原名 | 本实现采用 | 其他流行叫法 |
|---|---|---|
| Scud Race | 飞弹赛车 | 世嘉超级房车赛 / 保持原名 |
| Dirt Devils | 泥地恶魔 | 保持原名 |
| Spikeout | 街头格斗 | 打群架 / 保持原名 |
| Fighting Vipers 2 | 格斗之蛇 2 | 格斗毒蛇 2 |

### 7.9 游戏列表显示版本号（已实测）

**问题**：63 个 ROM 条目只显示 27 个不同的游戏名——`Dirt Devils` 出现 5 次、`Virtua Striker 2 '99.1` 出现 4 次，**完全无法区分**。

**关键发现**：`Games.xml` 的 `<identity>` 节点**早已包含 `<version>` 字段**（如 `Japan, Revision A`），`GameLoader.cpp` 也已解析进 `Game::version`。**数据一直都在，只是没显示。**

**修复**：`RomEntry` 增加 `version` 字段，列表按用户提供的参照格式显示 `名称 (版本)`：
```
Daytona USA 2 - Battle on the Edge (Japan, Revision A)     daytona2     1998
Daytona USA 2 - Power Edition (Japan)                      dayto2pe     1998
Dirt Devils (Australia, Revision A)                        dirtdvlsau   1998
Dirt Devils (Export, Revision A)                           dirtdvls     1998
Dirt Devils (Export, Ver. G?)                              dirtdvlsg    1998
Dirt Devils (Japan, Revision A)                            dirtdvlsj    1998
Dirt Devils (USA, Revision A)                              dirtdvlsu    1998
```

配套改动：

- **搜索同时匹配版本**：输入 `japan` 或 `revision a` 可筛出对应版本（这是用户找 ROM 的常见方式）
- **名称列权重提高**（`SizingStretchProp` 权重 3.0，ROM 列 1.6）——名称变长后需要更多宽度
- **行悬停显示完整文本**，避免窄窗口下被裁切
- `##` 之后仍为 `setName`，**行身份与选中状态不受影响**

**与你提供的清单的差异**（供参考）：你的清单 58 条，`Games.xml` 63 条。你的清单含 `vs299b`（XML 无）；XML 含 `mgtrkbad`、`oceanhuna`、`scuddxo`、`vs299j`、`vs29915a`、`vs29915j`（你清单无）。**以 `Games.xml` 为准**，它是模拟器实际支持的范围，且版本字段比清单更细（如 `vs2` XML 为 `Step 2.0, Export, USA`，清单仅 `Step 2.0`）。

### 7.10 长帮助文本（`(?)` 提示）漏译（已修复）

**症状**：切到英文后，界面标签全是英文，但把鼠标放到「高级设置」各选项的 `(?)` 上，提示文字仍是中文；只有最后一项「Glow threshold」看着像英文。

**根因：翻译表里根本没有这些长帮助文本。**

`HelpMarker()` 和 `UiTooltip()` 都是走 `Tr()` 的，机制没问题：

```cpp
static void HelpMarker(const char* text)
{
    ImGui::SameLine();
    UiTextDim("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr(text));   // 走查表，机制正确
}
```

但 `Tr()` 是"**查不到就原样返回**"，所以问题不在机制而在**表里没有条目**。实测统计：

| 项目 | 数量 |
|---|---|
| `HelpMarker()` 调用 | 14 处（全部多行） |
| `UiTooltip()` 调用（多行） | 6 处 |
| 其中**已在翻译表中**的 | **0** |

`Localization.cpp` 里含 `\n` 的字符串字面量一共只有 12 处，全是这次加 CRT 预设时补的。所以 14 + 6 = **20 条帮助文本全部漏译**。

> 关于「Glow threshold 像是英文」：那是它的**滑杆标签**（`辉光阈值##PostGlowThreshold` → `Glow threshold##PostGlowThreshold`），标签条目本来就存在；它的**提示文本**同样漏译。两者容易看混。

**修复：补齐 20 条，并且键从源码里自动提取，不手抄。**

手抄这些键风险极高——它们是多行字符串字面量拼接，**一个空格差异就会静默回退成中文**，而且不报错。例如：

```cpp
"由显卡在光栅化阶段对多边形边缘做多重采样。……不变锐——\n"
"车身上的贴花、小字招牌不会像后处理抗锯齿那样被糊掉。\n"    // ← 源码缩进不算内容，运行时无前导空格
```

源码缩进看着是 4 个空格，但那是**代码缩进**，不在字符串里。凭肉眼抄必然出错。所以新增脚本 `Scripts/GenHelpTranslations.ps1`：

1. 对 `Gui.cpp` 里每个 `HelpMarker(` / `UiTooltip(` **做括号平衡提取**参数
2. 把参数内的各个字符串字面量**按编译器的方式拼接**成运行期字符串，直接作为键
3. 与脚本里手写的 20 条英文一一对应后，插入 `kEnglish` 表末尾

**验证**（脚本化，非目测）：

```
表中键总数: 314
待查键: 20   未命中: 0
```

做法是把 `Gui.cpp` 的键和 `Localization.cpp` 的键**都做转义还原**再比对（`\\` → `\`、`\n` → 换行、`\"` → `"`），确保比的是运行期字符串而不是源码文本。

**这个坑的通用教训**：凡是"查不到就静默返回原文"的 i18n 机制，**漏译不会报错**，只会让界面中英混杂。要么加构建期校验（检查每个 `Tr()` 的实参是否在表里），要么像这里一样把键的生成也脚本化，让键不可能抄错。**`Tr()` 的静默回退是这套设计刻意的取舍**（避免出现空白标签），代价就是漏译必须先被发现。

---

## 八、配置项参考

以下为本次改造**新增**的配置项（均在 `Config/Supermodel.ini`）：

### 视频

| 配置 | 默认 | 说明 |
|---|---|---|
| `ExclusiveFullscreen` | `false` | 独占全屏 + G-SYNC/VRR |
| `ExclusiveRefreshRate` | `0` | 独占全屏 + 固定 57.5242Hz（0 = 关闭）|
| `ShowConsole` | `false` | 显示控制台窗口 |
| `MSAA` | `0` | 硬件 MSAA：0 / 2 / 4 |
| `IntegerScale` | `false` | 只按整数倍放大（点对点），余下留黑边 |
| `UiLanguage` | `0` | 界面语言：0 英文（默认）/ 1 中文 |
| `PostFXAA` | `false` | FXAA 后处理抗锯齿 |
| `PostCRT` | `0` | CRT：0 关 / 1 扫描线 / 2 扫描线+点阵 |
| `PostMaskType` | `1` | 点阵：0 栅条 / 1 槽孔 / 2 点阵 |
| `PostMaskScale` | `2` | 一个荧光粉点占几个物理像素。**建议用 3 的倍数**（3 / 6），否则 RGB 子孔间距不足 1 像素会产生彩噪 |
| `PostMaskIntensity` | `50` | 点阵强度 % |
| `PostScanlines` | `0` | 扫描线数量（0 = 自动用 384 行）。**按整个窗口高度计**；开启整数倍缩放时画面内实际线数会按可见区比例减少，启用界面会显示实际值 |
| `PostScanlineIntensity` | `60` | 扫描线强度 % |
| `PostGlow` | `0` | 辉光强度 % |
| `PostGlowLevels` | `4` | 辉光累积的 mip 级数（1–8）|
| `PostGlowThreshold` | `40` | 辉光阈值 %：低于此亮度的区域不发光（草坪/雪地建议 60–70）|
| `PostGain` | `190` | CRT 亮度补偿 % |
| `GuiWindowWidth` / `GuiWindowHeight` | 自动 | 设置窗口客户区尺寸 |

### ROM / 网络（本次修正默认值）

| 配置 | 默认 | 说明 |
|---|---|---|
| `ROMPath` | `"C:/Downloads"` | ROM 目录。**首次运行写入此默认值**，界面「浏览」可随时改 |
| `Network` | **`true`** | 网络板。**原默认 `false`，导致 Daytona 2 等游戏卡在自检**（见 11 节）|
| `SimulateNet` | `true` | |
| `PortIn` | **`1970`** | **必须与 `PortOut` 相同**（原默认 1970 / 1971 不匹配）|
| `PortOut` | **`1970`** | 同上 |

### 声音 / 性能

| 配置 | 默认 | 说明 |
|---|---|---|
| `AudioLatency` | `40` | 音频缓冲大小 %（5–100）。**还卡就往上调** |
| `HighPriority` | `true` | 进程优先级设为 ABOVE_NORMAL |
| `AsyncSoundBoardThread` | `true` | 音频由回调驱动，与帧循环解耦 |

### 命令行

```
-exclusive-fullscreen    独占全屏 + G-SYNC
-show-console            显示控制台窗口
-async-sound             音频与帧循环解耦（默认）
-sync-sound              改回同步模式
```

---

## 九、未完成与已知问题

### 9.1 拖动窗口时**画面**仍冻结

音频已解决（5.3），但主循环仍被 Windows 模态移动循环阻塞，**画面会冻结**。

**尝试过并失败**：用 `SDL_SetWindowsMessageHook` 拦截 `WM_ENTERSIZEMOVE`，在模态循环里用定时器驱动帧更新。

**失败原因**：该 hook 由 **SDL 自己的消息泵**调用，而 `CDirectInputSystem`（`dinput`/`xinput`/`rawinput`，Windows 默认）**自己直接调 `PeekMessage`/`GetMessage`/`DispatchMessage`**（为了不让 RawInput 句柄失效），完全绕过 SDL 的泵。**该 hook 对 SDL 输入系统有效，对 DirectInput 系列无效**。

代码保留在 `Src/OSD/SDL/Main.cpp` 的 `ModalLoopMessageHook()`，注释已写明该限制。

**彻底解法**：让帧循环不依赖消息泵，即跑在独立线程上。这是架构级改动。

### 9.2 CRT 滤镜与 CRT-Royale 的差距

见 3.7。当前实现**不是** CRT-Royale 的移植，而是独立实现的多级辉光 + 点阵 + 扫描线。缺失：光束查表、屏幕曲率、gamma 分级、色差、历史帧余辉。

**移植 CRT-Royale 需要**：多 Pass 框架（pass 图、每 pass 独立输出分辨率、乒乓图像池、历史帧纹理）+ 逐个手写着色器。

### 9.3 无 HDR 输出

当前全链路是 SDR（中间目标 `R8G8B8A8_UNORM`，SDR swapchain）。荫罩会吸掉约 2/3 的光，SDR 下只能靠 `PostGain` 补偿，**代价是暗部被一起抬亮**。

真正的解法是 HDR 输出：中间目标改 `R16G16B16A16_SFLOAT`，swapchain 使用 `VK_EXT_swapchain_colorspace` 的 `HDR10_ST2084` 色彩空间 + PQ 编码。这是独立且不小的改动。

### 9.4 默认值来源仍不一致

见 6.5。非赛车部分的十余处数值差异未处理（无实际冲突）。

### 9.5 关闭按钮修复未覆盖 GUI 自身

`SDL_WINDOWEVENT_CLOSE` 的显式处理加在**游戏循环的两个输入系统**里。设置界面（`Gui.cpp`）另有一套 `SDL_PollEvent` 循环，其关闭逻辑未同步检查（实测 GUI 关闭正常，但两处逻辑不一致）。

### 9.6 未验证的场景

- **长时间运行**（>10 分钟）的稳定性，尤其是异步音频模式
- **多个游戏**（当前主要在 Daytona USA 2 上验证）
- **OBS 实际录制**负载下的表现（优先级 + 缓冲的设计目标覆盖此场景，但未实测）
- **HDR 显示器**下的表现
- **扫描线光束积分的 4K 观感**（见 3.8）——着色器已编译通过、无报错，但未逐像素核对摩尔纹是否真的消失
- **整数倍缩放在非 4K 面板上的效果**（见 3.9）——只在 4K 独占全屏下验证过日志数值

### 9.7 调试环境缺少 Vulkan 验证层

本机**未安装 Vulkan SDK**，也没有任何隐式验证层或驱动自带层，因此：

```
VkValidation = 1  →  validation layer unavailable, retrying without it
```

后果是**拿不到 VUID 报错**，Vulkan 问题只能靠读代码推断（3.1.1 的排查因此耗时很久）。

**建议**：排查 Vulkan 问题前先装 [Vulkan SDK](https://vulkan.lunarg.com/) 或 RenderDoc。装了之后 `VkValidation = 1` 会直接打印出违反的 `VUID`，能省掉大量猜测。

---

## 十、验证工具与方法

### 10.1 Vulkan 截图

见第四章。这是本项目**最重要的验证能力**——它把"盲改"变成"可自检"：

```powershell
# 游戏中按 Alt+S，输出到当前工作目录
Screenshot_2026-10-10_(15-29-48).png
```

### 10.2 日志

`Supermodel.log` 中有诊断价值的行：

```
Vulkan: available present modes: ...
Vulkan: swapchain 3840x2160, 3 images, present mode FIFO (vsync on).
Vulkan: 3D target 3840x2160, 4x MSAA (resolved before compositing).
Exclusive fullscreen: desktop mode 3840x2160@120 Hz set; G-SYNC/VRR should track the emulator rate.
Audio: 735 samples per frame nominal (735.0000 exact) at 60.0000 Hz.
Audio: buffer 400 ms (40% of maximum).
Process priority class set, now 0x00008000 (above normal).
GUI window: 1920x1200 (saved 0x0, display DPI scale 1.50, usable area 3840x2088).
GUI: controller navigation enabled (1 joystick(s) visible to SDL).
```

画面统计（用于亮度客观对比）：只统计 4:3 游戏区域，避开黑边。

### 10.3 逐游戏按键冲突扫描

思路：从 `Config/Games.xml` 读取每款游戏声明的 `<input type="...">`，映射到 `Game::INPUT_*` 标志，取出该游戏实际启用的所有输入项，再按 `JOYn_BUTTONm` / `JOYn_POVm_dir` / 轴 归类，检出同一个物理按键被两项功能占用的情况。

**这一步发现了多个真冲突**（见 6.2），比人工审查可靠。

### 10.4 音频欠载计数

`Supermodel.log` 结尾的统计是判断音频健康度的主要指标：

```
Audio: N buffer under-run(s), M over-run(s).
```

通过窗口标题也能实时观察：`60.001 FPS [audio u0/o0]`。

配合 `ShowFrameRate = 1` 还能判断"帧循环是否还在跑"——标题每秒更新一次，标题不变即循环停止。

### 10.5 模态循环是否真的触发（重要校验）

验证"拖动期间是否卡顿"时，**必须先确认窗口真的移动了**：

```csharp
// 拖动前后比对 GetWindowRect
if (p0 != p1)  // 窗口位置变化 -> 模态移动循环确实被触发，测试有效
```

**这条校验救过一次误判**：曾用 `PostMessage(WM_NCLBUTTONDOWN)` 合成拖动，报告"修复生效"，但窗口位置**没有变化**——说明模态循环根本没触发，测试什么也没证明。改用**真实鼠标按下**（`mouse_event`）后才暴露真实结果是否定的。

**任何涉及窗口交互的自动化测试都应包含这类"前置条件校验"。**

### 10.6 三处默认值来源比对

定期比对「文件模板」与「运行期默认」的差异，防止再次出现投币绑到摇杆按下这类问题。

---

## 11. 网络板（Net Board）单人游玩：为什么必须开启、以及端口必须配对

**本节结论：只改配置，不改代码。** 之前"关掉网络板"或"开了网络板仍卡死"都不是模拟器 bug，而是**端口配对**问题。

### 11.1 两种失败模式，根因都指向 `SimNetBoard.cpp`

相关代码只有两处关键分支（`Src/Network/SimNetBoard.cpp`）：

```cpp
// Init(): 网络板是否挂载 = 游戏本身有网络板 && 配置里勾了 Network
m_attached = m_gameInfo.netboard_present && m_config["Network"].ValueAs<bool>();
if (!m_attached)
    return Result::OKAY;   // 未挂载 -> IsRunning() 恒为 false
```

```cpp
// RunFrame(), State::testing 结尾
if ((numMachines == 0) && ((port_in != port_out) || (addr_out != "127.0.0.1")))
{
    ErrorLog("no slave machines detected. Make sure only one machine is set to master!");
    m_state = State::error;
    break;
}
```

**失败模式 A —— 不勾"启用网络板" → 永远卡在黑屏文字循环**

`m_attached = false`，于是 `IsRunning()` 恒为 `false`，而 `ReadIORegister()` 的第一句就是：

```cpp
uint16_t CSimNetBoard::ReadIORegister(unsigned reg)
{
    if (!IsRunning())
        return 0;
    ...
}
```

游戏主板读 `0x8a`（即 `m_status1`）时**永远拿到 0**。而网络初始化要求：

```cpp
m_status1 = IsGame("dirtdvls") ? 0x4004 : 0xe000;   // 期望值
```

游戏永远等不到 `0xe000`，于是停在网络自检画面无限循环。**这就是"不打勾就一直卡在黑屏文字这里循环"。**

> 注意：`-net` 命令行参数只会覆盖 `Network`，**不会**帮你配对端口。是否联网由这段代码逻辑决定，与"要不要联机玩"无关——单机也必须让网络板处于 `ready` 状态。

**失败模式 B —— 勾了"启用网络板"但端口没配对 → 卡死**

原来配置是 `PortIn = 1970`、`PortOut = 1971`，方向反了/不相等：

| 组件 | 行为 |
|---|---|
| `TCPSend` | 连到 `127.0.0.1:1971` → 没人监听，`nets->Connect()` 一直失败 |
| `TCPReceive` | 监听 `1970` → 没人在连 |

`m_connected` 永远为 `false`，`State::testing` 里每帧都在 `if (!m_connected) break;`，同样死循环。

### 11.2 修复：改成"自己连自己"的合法配置（等同 GUI 上的「单机对连」）

```ini
Network = 1
SimulateNet = 1
PortIn = 1970
PortOut = 1970
AddressOut = 127.0.0.1
```

`PortIn == PortOut == 1970` 时：

1. `TCPSend` 连 `127.0.0.1:1970`
2. `TCPReceive` 监听 `1970`
3. 两者在同一个端口相遇 → `m_connected = true`
4. `numMachines == 0`，但 `port_in == port_out && addr_out == "127.0.0.1"` **成立**，跳过报错分支
5. 进入 `State::ready`，`m_status1` 正常返回 → 游戏正常启动

源代码注释里写的就是这个设计意图：
> *"if there are no other linked machines, only continue if Supermodel is linked to itself"*
> *"only continue if Supermodel is linked to itself"*

**验证结果**（`daytona2`，Vulkan + 4x MSAA）：

```
Connecting to 127.0.0.1:1970 ..
Successfully connected.
[Info]  Saved NVRAM to 'NVRAM/daytona2.nv'.
[Info]  Audio: 0 buffer under-run(s), 0 over-run(s).
[Info]  Program terminated normally.
日志中无任何 Error / Warning
```

### 11.3 视角切换（`VRCycle`）与网络**完全无关**（排除误判）

`VRCycle` 的作用范围仅限输入层，逐字节确认：

| 位置 | 作用 |
|---|---|
| `Src/Inputs/Inputs.cpp:419` | 读 `VRCycle` 配置，`IsVRSelected()` 决定 4 个视角键哪一个是"当前" |
| `Src/Model3/Model3.cpp:330-335` | 仅用来设置 4 个 VR 按键位（VR1 红 / VR2 蓝 / VR3 黄 / VR4 绿）|

它**不碰** NVRAM、不碰 EEPROM Link ID、不碰网络板状态机。之前"卡在黑屏"与它无任何因果关系。

### 11.4 现在已是默认设置（本轮修改）

上面 11.2 的配置**已写入默认值**，不再需要手动设置。改动三处，都在"新装用户"路径上：

| 位置 | 旧 | 新 |
|---|---|---|
| `ConfigFile.h` 模板（首次运行写出的 ini）| `Network = false`、`PortOut = 1971` | `Network = true`、`PortOut = 1970` |
| `Main.cpp` 运行期默认值 | 同上 | 同上 |
| `ConfigFile.h` 注释 | 无 | 写明"两个端口必须相同"及原因 |

**原来两个默认值都有问题**，且叠加：

1. `Network = false` → 网络板不挂载 → `m_status1` 恒为 0 → **Daytona 2 / Scud Race / Sega Rally 2 等卡死在自检黑屏**
2. `PortOut = 1971 ≠ PortIn = 1970` → 即使手动打开网络板，两个 socket 也永远碰不到 → **仍然卡死**

所以新用户拿到默认配置时，**是能直接跑的**。已实测：新生成的 `Supermodel.ini` 为 `Network=true / PortIn=1970 / PortOut=1970 / AddressOut="127.0.0.1"`。

模板注释里也写明了「**不要把两个端口改成不同的值**」，因为那正是"看起来像修好了其实更坏"的操作。

### 11.5 排查此类问题的通用方法

1. 先看 **stdout**（不是 `Supermodel.log`）：`ConnectProc()` 用 `printf` 打印 `Connecting to ...` / `Successfully connected.`
   - 只看到 `Connecting to ...` 而没有 `Successfully connected.` → **端口/地址没配对**
2. 再搜日志里的 `Error`：出现 `no slave machines detected` → 端口不等或地址不是 `127.0.0.1`
3. 想单机玩，就用 `PortIn == PortOut` + `AddressOut = 127.0.0.1`，**不要去关网络板**

---

## 十二、发行版布局与一键构建（`build.bat`）

### 12.1 目标形态

```
Supermodel-VK\
    Supermodel-VK.exe       <- 程序本体
    README.txt              <- 英文说明：来源、许可、本 build 的改造（见 12.5.1）
    安装说明.txt            <- 中文安装说明（见 12.5）
    Data\                   <- 其余全部
        Config\             Supermodel.ini（首次运行生成）、Games.xml、Music.xml
        Assets\             准星图、机柜背景图
        NVRAM\
        Saves\
        Screenshots\
        Analysis\
        Supermodel.log
        README.txt          <- 上游 Supermodel 完整手册（85KB）
```

顶层保留这三份文本是**有意例外**（见 12.5）："顶层只有一个 exe"的前提是**数据都在 `Data\`**，而说明文件的价值就在于打开文件夹第一眼能看到。

### 12.2 把数据目录收到 `Data\` 下

**改动点**：`Src/OSD/Windows/FileSystemPath.cpp`（此前是个 15 行的桩，只有相对路径字符串）。

**两处关键决策**：

1. **以可执行文件位置为基准，而不是当前工作目录**。原先所有路径都是相对路径，于是通过「起始位置」为空的快捷方式启动、或在别处的命令提示符里启动，会**静默产生第二套配置和第二套 NVRAM** —— 用户会以为配置丢了。现在按 exe 自身目录解析，启动方式不再影响文件位置。

   > 保留配置里存相对路径（而非写死绝对路径）是有意的：这样整个文件夹可以随意搬到别的盘，设置依然指向正确位置。

2. **`GetPath()` 按需创建目录**。原先 Windows 实现**连 `PathExists()`/`MakeDir()` 都没有**（Unix/OSX 有），所以目录必须靠压缩包附带；用户一旦删掉 `NVRAM\`，存档就静默失效。

**实测**（全新目录只放一个 `Supermodel.exe`）：

```
运行前:   Supermodel.exe
运行后:   Supermodel.exe
          Data\Analysis\
          Data\Config\Supermodel.ini
          Data\Supermodel.log
```

`NVRAM\` / `Saves\` / `Screenshots\` 在首次真正用到时才创建（惰性），属预期行为。

**日志路径的处理**：配置里存 `Data/Supermodel.log`（相对），由 `Logger.cpp` 的 `CFileLogger::ReopenFiles()` 调 `FileSystemPath::ResolvePath()` 锚定到安装目录。这样日志既落在正确位置，又不会因为换盘符而失效。

#### 12.2.1 旧布局自动迁移（已实测）

**这是个真实存在的升级陷阱**：配置位置从「exe 旁」搬到 `Data\Config\` 后，老用户那份调好的设置**不会被读到**——新位置没有 ini，于是按默认值生成一份，而用户精心调过的 CRT 参数、ROM 路径、界面语言全部**静默消失**，文件却还躺在原地。

**实测确认了这个后果**（迁移逻辑加入前）：

```
新位置 Data\Config\Supermodel.ini :  ROMPath = C:/Downloads          ← 默认值
旧位置 Config\Supermodel.ini      :  ROMPath = E:/Dev/...romset-2020
                                     PostScanlines = 720
                                     UiLanguage = 1                   ← 用户实际设置
```

**修复**：启动时（`WriteDefaultConfigurationFileIfNotPresent()` 内）把旧位置的 `Config\` / `NVRAM\` / `Saves\` **复制**（不是移动）到 `Data\` 下，已存在的目标文件一律不覆盖。

- **复制而非移动**：源文件留着，降级回旧版仍可用，且复制失败不会毁掉唯一的设置副本。
- **两个位置都试**：旧代码按工作目录解析，而多数人从 exe 目录启动，所以基准目录用 exe 目录。
- **用 `printf` 而不是 `InfoLog` 报告**：迁移发生在**日志系统建立之前**（`main` 里 `WriteDefaultConfigurationFileIfNotPresent()` 在 `CreateLogger()` 之前），`InfoLog` 会走当时的 console-error logger 而被丢弃 —— 这也解释了为什么旧的 "Wrote default configuration file" 消息从来不出现在 `Supermodel.log` 里。

**实测**（构造旧布局 `Config\Supermodel.ini` + `NVRAM\testgame.nv`）：

```
Moved 1 file(s) from '...\Config/' into '...\Data/Config/'.
Moved 1 file(s) from '...\NVRAM/' into '...\Data/NVRAM/'.

Data\Config\Supermodel.ini :  ROMPath = "D:/legacy-test"
                              UiLanguage = 1          ← 旧设置被正确继承
Data\NVRAM\testgame.nv     :  存在
```

> **已知的顺序限制**：迁移在**配置读取之前**，但被迁移的 ini 要到**配置读取之后**才被读到。所以升级后的**第一次**运行仍使用默认值，从第二次起才是用户自己的设置。要彻底消掉这一次偏差，需要把迁移提到 `FromINIFile()` 之前（当前它依赖各路径常量已初始化）。**这条未修**，因为影响仅限一次性启动，且用户通常不会察觉。

> **踩坑**：`MigrateLegacyDataLayout()` 里用到 `FileSystemPath::GetBasePath()`，而该函数是我在整理 `ResolvePath()` 时删掉的（当时判断无人使用），编译才暴露。跨文件的辅助函数删除前应确认没有其他调用点——**编译是唯一的检查手段**。

> **踩坑**：`Config/Supermodel.ini` 里的 `ROMPath` 是开发者自己的路径（`E:/Dev/...`），但该文件**不随发行版分发**——`build.bat` 只复制 `Games.xml` 和 `Music.xml`。发行版的默认值来自 `Src/OSD/ConfigFile.h` 的模板。

### 12.3 SDL2 / SDL_net 改为静态链接（顶层只剩一个 exe）

**为什么必须动这个**：隐式导入（implicit linking）的 DLL **只能**从 exe 自身目录、系统目录或 `PATH` 找到，**不能**从子目录找到。所以只要 SDL 还是独立 DLL，`SDL2.dll` 和 `SDL2_net.dll` 就必须躺在 exe 旁边，`Data\` 方案就实现不了。

**改动**：`MSVC/SDL/SDL.vcxproj`、`MSVC/SDLnet/SDL_net.vcxproj`（由 `Scripts/MakeSdlStatic.ps1` 一次性转换）。四处必须同时改，漏掉任何一处得到的都是**误导性的报错**：

| 项目 | 从 | 改为 | 不改的后果 |
|---|---|---|---|
| `ConfigurationType` | `DynamicLibrary` | `StaticLibrary` | 链接器拿到的是导入库而非对象库 |
| `RuntimeLibrary` | 默认(/MD) | `MultiThreaded`(/MT) | 与 Supermodel 的 /MT 混用 → **LNK2038** |
| `OmitDefaultLibName` | `true`(/Zl) | `false` | 静态库里不含 CRT 引用 → 大量无法解析的外部符号 |
| `DLL_EXPORT` | 定义 | 去掉 | 头文件判定 `dllexport`，把整个 SDL API 从 Supermodel.exe 导出 |

`.rc` 资源文件同时移除（静态库不能携带资源段）。

**宿主工程还需补上 SDL 原先自己链接的系统库**（`Supermodel.vcxproj` 的 `AdditionalDependencies`）：

```
+ ws2_32.lib iphlpapi.lib        <- SDL_net 的 Winsock
+ setupapi.lib cfgmgr32.lib      <- SDL 的 HID / 设备枚举
+ winmm.lib imm32.lib version.lib
```

**实测**：

```
dumpbin /dependents Supermodel.exe
  OPENGL32.dll  GLU32.dll  IMM32.dll  KERNEL32.dll  USER32.dll  ADVAPI32.dll
  SHELL32.dll   ole32.dll  OLEAUT32.dll  WS2_32.dll  SETUPAPI.dll  WINMM.dll
  VERSION.dll   GDI32.dll
  （无 SDL2.dll / SDL2_net.dll）
```

全部是 Windows 系统组件。

> **教训**：`RuntimeLibrary` 那一条最初写错时脚本**不报错**——`<RuntimeLibrary>` 是 SDK 默认注入的，项目文件里没有这一行，我的规则匹配不到就静默跳过。凡是"给缺失的配置项补值"的脚本，都要校验**补了几处**，而不是只看是否跑完。修正后 Debug 4 处 / Release 4 处都到位（Debug 用 `MultiThreadedDebug`）。

### 12.4 `build.bat`

一键完成「编译 → 清理旧发行目录 → 组装 → 回写构建输出目录」。

**几个刻意的行为**：

- **先杀掉运行中的 `Supermodel.exe`**。链接器无法覆盖正在运行的可执行映像，会报 `LNK1104: cannot open file`——这个报错看起来像源码问题，实际不是。
- **工具集自动探测**。项目文件写的是 `v145`，本机只有 `v143`，直接构建会报 `MSB8020`（读起来像工程坏了）。脚本查 `<VS>\MSBuild\Microsoft\VC\*\Platforms\x64\PlatformToolsets\` 里装了哪个，按 v145 → v144 → v143 取最新的；也可以用 `build.bat v143` 强制指定。
- **`Games.xml` / `Music.xml` 缺失即硬失败**，而不是让你在把文件夹发给别人之后才发现游戏列表是空的。
- **同步写一份目录树到构建输出目录**，这样在 VS 里按 F5 直接就是能跑的，不会遇到「`Data\Config\Games.xml` 不存在」。
- **不打包 `Supermodel.ini`**。首次运行按当前默认值生成（ROM 目录 `C:\Downloads`、自连网络配置、其余软件默认）。想让自己的设置成为所有人的默认值，改 `Src/OSD/ConfigFile.h` 里的模板即可。

**用法**：

```
build.bat              编译并组装
build.bat v143         强制工具集
```

产物：`dist\Supermodel-VK\`（可直接压缩分发；文件夹可整体搬移）。

**关于可执行文件改名**：VS 构建产物仍是 `Supermodel.exe`，**只有组装到发行目录时才复制成 `Supermodel-VK.exe`**。这样 VS 的启动配置、调试器符号路径、增量编译状态都不受影响。相应地，构建前「杀进程」那一步要**两个名字都查**——开发者手上的 `Supermodel.exe` 和发给用户的 `Supermodel-VK.exe` 是两个不同进程，任何一个都可能占着文件。

> **踩坑（值得记）**：工具集探测第一版用了 `dir /b /ad "...\VC\*\...\v143"`，**永远匹配不到**——`dir` 只在**最后一段**路径支持通配符，中间段的 `*` 会被当成字面量。更糟的是失败表现与"没装工具集"完全一样。改用 `for /d` 逐层遍历。
>
> 第二版在 `if (...) else (...)` 块里用了 `%VSROOT%`，**同样永远为空**——`%VAR%` 在整块被解析时就展开，那时变量还没赋值。必须用 `!VAR!`（脚本已开 `EnableDelayedExpansion`）。

### 12.5 面向玩家的说明放在顶层（`Docs\安装说明.txt`）

**为什么需要**：程序本体只有 6 MB、目录里**一个 `.dll` 都没有**，这本来是"绿色免安装"的成果，但从用户视角看更像"是不是少东西 / 要不要装运行库"。实测最终 exe 的导入表只有 Windows 系统组件：

```
dumpbin /dependents Supermodel-VK.exe
  OPENGL32.dll  GLU32.dll  IMM32.dll  KERNEL32.dll  USER32.dll  ADVAPI32.dll
  SHELL32.dll   ole32.dll  OLEAUT32.dll  WS2_32.dll  SETUPAPI.dll  WINMM.dll
  VERSION.dll   GDI32.dll        （无 vcruntime140 / msvcp140 / SDL2 / vulkan-1）
```

`vulkan-1.dll` 不在导入表里，因为它是 **volk 运行时动态加载**的，由显卡驱动装进系统目录（本机 `C:\Windows\System32\vulkan-1.dll`，1.4.350）。取不到时 `InitDevice()` 明确报 `Vulkan: volkInitialize failed. Is vulkan-1.dll installed?` 并正常退出，不会黑屏；界面还能改选 OpenGL 渲染器（走系统自带的 `OPENGL32.dll`）。

所以给用户一句话就够：**Windows 10/11 x64 + 最新显卡驱动 + 自备 ROM**，运行库一个都不用装。

**放置位置**：放在**顶层**而不是 `Data\`。这是对"顶层只有一个 exe"的一个**有意例外**——说明文件的价值就在于打开文件夹第一眼能看到，塞进 `Data\` 就没人会读了。`build.bat` 的规则随之变成：`Docs\` 下的 `README.txt` / `LICENSE.txt` 进 `Data\`，**其余 `.txt` 进顶层**。

### 12.5.1 顶层英文 README（`Docs\README-VK.txt` → `README.txt`）

**为什么还要一份英文的**：`安装说明.txt` 是中文的（面向中文玩家），但**发行包可能被交给非中文用户**，而且"这是什么 / 基于什么 / 做了什么改造"这类**来源与出处说明**应当有一份任何语言都能读的版本。它也是对**开源许可的署名义务**的交代。

**内容**（全英文，254 行）分九节：

| 节 | 内容 |
|---|---|
| WHAT THIS IS | 基于开源 Supermodel 0.3a（含上游仓库地址）、GPL v3、**明确声明与 Supermodel 团队无隶属关系**、emulation 核心的功劳全部归上游 |
| WHAT IS NEW | Vulkan 后端 / 显示 / CRT 滤镜 / 界面（含 Tab 叠加层）/ 音频 / 输入，**分组列出** |
| SYSTEM REQUIREMENTS | Windows 10/11 x64 + 驱动；**不需要任何运行库**（说明静态链接）|
| QUICK START | 五步 + 完整按键表 |
| ROM FILES | 不附带游戏数据；未收录集合的判读提示 ← **指向日志** |
| FOLDER LAYOUT | 目录树，并指向 `Data\README.txt` 完整手册 |
| KNOWN LIMITATIONS | 如实列出（无曲率/色差/余辉、无 HDR、叠加层需 Vulkan、MSAA 与整数倍缩放需重启、边缘杂线、测试范围有限）|
| LICENCE | GPL v3、无担保、上游地址 |

**命名与位置的两个决定**：

- **源文件叫 `README-VK.txt`，发出时重命名为 `README.txt`**。因为 `Docs\README.txt` 已经是**上游 Supermodel 手册**（85KB），它必须留在 `Data\`；两个不同文件不能同名，而手册是更不该动的那一个。发出时用 `README.txt` 是因为这是**用户第一眼会找的名字**。
- **它对通用循环加了跳过项**：通用规则会把 `Docs\*.txt` 原样复制到顶层，那样顶层会同时出现 `README-VK.txt` 和 `README.txt` 两份。所以通用循环跳过它，改由**显式复制**处理：

```bat
if exist "%ROOT%\Docs\README-VK.txt" (
    copy /y "%ROOT%\Docs\README-VK.txt" "%DIST%\README.txt" >nul
    for %%R in ("%ROOT%\Docs\README-VK.txt") do echo Readme   : README.txt from Docs\README-VK.txt  ^(%%~zR bytes^)
) else (
    echo Readme   : not found ^(Docs\README-VK.txt^)
)
```

**编码**：**纯 ASCII**（0 个非 ASCII 字节）。英文内容不需要非 ASCII，所以**根本不存在编码问题** —— 不需要 BOM、不依赖代码页、任何编辑器打开都正常。正文里刻意**不写中文文件名**（需要提到中文说明时用文字描述而不用文件名），这就是它能保持纯 ASCII 的原因。

> 与 12.5 的中文说明形成对照：**中文文档必须处理编码（BOM + CRLF + 代码页），纯英文文档则完全回避了这个问题**。有条件时优先纯 ASCII。

**验证**：构建后核对

```
顶层有 README.txt                    [OK]
顶层无 README-VK.txt（未重复）        [OK]
Data\README.txt 仍是上游手册（85KB）  [OK]
顶层 README.txt 与源文件 SHA256 一致  [OK]
顶层 README.txt 纯 ASCII             [OK]  0 个非 ASCII 字节
```

### 12.5.2 中文说明的编码与踩坑

**编码**：UTF-8 **带 BOM** + CRLF。不带 BOM 的 UTF-8 中文在部分 Windows 记事本/编辑器里会显示成乱码，而这个文件唯一的使用方式就是双击用记事本打开。

**踩坑**：`build.bat` 里**不能写中文文件名字面量**。cmd.exe 按**控制台代码页**（中文 Windows 是 936）解析批次文件内容，而文件本身是 UTF-8 保存的，于是 `copy "Docs\安装说明.txt"` 会报 **找不到文件**——而且只有中文用户能复现、英文用户看着莫名其妙。改为**按名字排除**、文件名从文件系统取：

```bat
for %%F in ("%ROOT%\Docs\*.txt") do (
    if /i not "%%~nxF"=="README.txt" (
        if /i not "%%~nxF"=="LICENSE.txt" (
            copy /y "%%~fF" "%DIST%\%%~nxF" >nul
        )
    )
)
```

`%%~nxF` 由 FOR 在**每次迭代**展开（不是整块解析时），所以放括号里也安全。`build.bat` 全文保持 **0 个非 ASCII 字节**。

> 副作用：`build.bat` 末尾列出顶层文件时，中文名在**非 936 代码页**的控制台里会显示成乱码。这只是显示问题——磁盘上的文件名是对的（可用码点核对：`U+5B89 U+88C5 U+8BF4 U+660E` = `安装说明`）。

### 12.5.3 去掉中英重复：一份文档只讲一种语言

英文 README 加进来之后，出现了一个**内容重叠**问题：`安装说明.txt` 结尾本来有一段 "English summary"（系统要求 / 不需运行库 / 快速开始的英文摘要）。

逐条核对下来，中文安装说明的**全部 11 个要点都已在 `README.txt` 里**（系统要求、不需运行库、快速开始、按键、目录、ROM、未收录 FAQ、Vulkan 报错 FAQ、切换渲染器 FAQ、日志指引、许可）。

于是做了**去重**：

| 文件 | 语言 | 定位 |
|---|---|---|
| `README.txt`（顶层）| **英文** | 完整的英文文档：来源、改造、要求、限制、许可 |
| `安装说明.txt`（顶层）| **中文** | 中文安装说明（保持它原本的编号分节体例）|

**具体改动**：把 `安装说明.txt` 末尾那段英文摘要**删掉**，换成一句指向 `README.txt` 的短提示：

```
----------------------------------------------------------------------
英文文档
----------------------------------------------------------------------
英文说明见同目录下的 README.txt（介绍这个 build 是什么、做了哪些改造、
系统要求与已知限制）。
```

**为什么不是"加一份英文安装说明"**：那会变成**三份会互相漂移的文档**（中文说明、英文说明、英文 README）。现在任一种语言只有一处真相：**中文只此一份，英文只此一份**。

> **教训**：往包里加文档时，先**逐条核对它与已有文档的重叠**。"再加一份 X 的英文版"在只有一份 X 时是对的，在 X 的内容已经被另一份文档覆盖时是错的 —— 后者会制造漂移源。
>
> 顺带一提：这个去重**同时也修好了 `安装说明.txt` 的 UTF-8 BOM 风险** —— 编辑这类文件后必须复查 BOM 与 CRLF（实测：BOM 保留、CRLF 76、孤立 LF 0）。

---

## 十三、窗口标题与图标

### 13.1 改名

新增 `SUPERMODEL_TITLE` / `SUPERMODEL_BASE_RELEASE`（`Src/Version.h`），显示为：

```
Supermodel-VK
```

控制台横幅（`Title()`）：

```
Supermodel-VK - a Sega Model 3 arcade machine emulator.
Based on Supermodel 0.3a-2026.09.28 (core version 0.3a-WIP).
Copyright 2003-2026 by The Supermodel Team
```

**`SUPERMODEL_VERSION` 保持 `0.3a-WIP` 不动**——它写进存档和 NVRAM 并在读取时校验，改了会让用户已有存档全部失效。`SUPERMODEL_BASE_RELEASE` 单独保留，因为**名称里已不再包含上游版本号**，出处改由横幅和关于行体现。文档里已注明三者不可混用。

替换位置：设置窗口 `ImGui::Begin` 名、`SDL_CreateWindow` 标题、游戏窗口 `baseTitleStr`（缓冲从 128 扩到 192）、配置输入时的标题、OpenGL 探测窗口、控制台 `Title()`。

> 中途一度用过 `Model 3 Vulkan - based on Supermodel 0.3a-2026.09.28`。问题不在内容而在**长度**：它既占标题栏，又在界面里和「双击游戏启动」提示挤同一行，而这行后来被整个删掉了（见 14.1）。最终定为短名 `Supermodel-VK`。

### 13.2 图标

- **exe 图标**：`output.ico`（6 种尺寸 16→256，32bpp）复制为 `Src/OSD/Windows/Supermodel.ico`，新增 `MSVC/SupermodelResources.rc`（`IDI_SUPERMODEL 101 ICON ...`）并挂进工程 `ResourceCompile`。
  > 工程里那个 `Src/OSD/Windows/SupermodelResources.rc` 是**死文件**（未被任何工程引用），且它引用的清单路径也不对——清单实际由链接器的 `/MANIFEST` 嵌入。新 `rc` **只放图标**，避免与链接器嵌入的清单冲突（重复嵌入同一清单会被链接器拒绝）。
- **窗口图标**：新增 `SetWindowIcon()`（`Main.cpp`，声明于 `Main.h`），从**自身资源**里 `LoadImage` 出大/小两种尺寸，再经 `SDL_GetWindowWMInfo` 拿到 `HWND` 后 `WM_SETICON`。
  > 不走 `SDL_SetWindowIcon()` 是因为它只收 `SDL_Surface`，而 SDL2 读不了 `.ico`——那就得把同一张图再以 bmp/png 形式打包一份。从资源里取，图标只有一份来源，发行目录也不用多任何文件。
  >
  > **必须在 `SDL_CreateWindow` 之后调用**：SDL 创建窗口时会装上自己的默认图标。
- 调用点三处：设置窗口（`Gui.cpp`）、`CreateGLScreen`、`CreateVulkanScreen`。

**实测**：

```
LoadImage(exe, ID 101, IMAGE_ICON)  ->  非空（系统尺寸 + 16x16 均可取出）
dumpbin /headers Supermodel.exe     ->  存在 .rsrc 段
```

> **踩坑**：局部变量最初叫 `large` / `small`，编译报 `error C2628: 'HICON' followed by 'char' is illegal`。原因是 `windows.h` 链上的 `rpcndr.h` 把 `small` 定义成 `char`、`large` 定义成 `long long`，于是声明被宏替换成了乱码。改名 `iconBig` / `iconSmall`（已在注释里标注原因）。

---

## 十四、设置界面布局精简

### 14.1 去掉顶部标题行，两栏都上移

**改动前**：顶部是整行「Supermodel-VK - based on ... + 双击游戏启动...」，然后才是整行 ROM 路径，最后才是左右两栏。标题行占一整行高度，两栏被压下去。

**改动后**：

```
┌─────────────────────────┬──────────────────────────┐
│ [ROM 路径输入框] [Browse]│  常用 核心 画面 声音 ...   │  ← 两栏同一起始行
├─────────────────────────┤                          │
│  游戏列表                │  当前 Tab 内容            │
│                         │                          │
└─────────────────────────┴──────────────────────────┘
[启动游戏][保存设置][载入设置][恢复默认][退出]
```

- **删掉标题行**。窗口标题栏（系统那一条）已经写了 `Supermodel-VK`，界面内再重复一遍是冗余。
- **ROM 路径并入左栏**，成为游戏列表的头一行。它本来就是这个列表的来源，放在一起在语义上也更合理。
- **`Browse` 紧贴左栏右缘**，而不是孤零零留在整窗右侧。

**实现要点**：两栏不再是「整行表头 + 两个并列面板」，而是**两段独立的竖直堆叠**；左栏画完后，用 `SetCursorPos` 把光标**移回顶部**再画右栏。因为 ImGui 的光标只往一个方向推进，右栏要上移就必须显式回位。

所有 Y 坐标都从 `topY`（内容区起点）推算，不依赖窗口绝对尺寸。`columnWidth` 也从内容区宽度算出，两栏始终接到右边缘。

### 14.2 底部按钮：文字完整 + 均匀分布

原先是固定 `ImVec2(120, 0)`。问题在于**中文标签普遍比英文宽**（「恢复默认」4 字 vs `Restore Defaults`），固定宽度在高 DPI 下会把较长的一个裁掉，同时让短的空荡荡。

改为**先量后定**：

```cpp
widest = max over labels of CalcTextSize(Tr(label)).x    // 文字宽度设下限
padding = FramePadding.x * 2 + 24                        // 留出宽松的内边距
evenShare = (可用宽度 - 间距) / 5                        // 有空间就均匀铺开
buttonWidth = clamp(evenShare, minWidth, 200)            // 上限防止超宽屏上拉成长条
```

**文字完整优先，其次才谈铺满**：宽度下限由最宽的标签决定，所以任何窗口尺寸下都不会裁字；只有空间富余时才均分。上限 200px 避免在超宽窗口里变成五根长条。

### 14.3 新增「载入设置」按钮

在「保存设置」之后插入，用于载入用户自定义的 `.ini`：

1. 弹出确认框（说明会覆盖当前设置）
2. 点「选择文件」→ Windows 文件选择对话框（`.ini` 过滤器 + 全部文件）
3. `FromINIFile()` 读入 → 与 `DefaultConfig()` 合并 → 应用

**两个刻意的设计**：

- **与内置默认值合并，而不是直接替换**。用户存的设置文件往往只记了他关心的几项；直接替换会让其余项目落到「读不到就用零值」的状态，而不是合理的默认值。
- **保留 `ROMPath`**。ROM 目录是**机器相关**而非偏好：从别人机器拷来的设置文件如果把它一起带过来，游戏列表会指向一个不存在的目录，然后空白，且看不出原因。

导入后立刻写盘（不等「保存设置」），理由与「恢复默认」相同——用户要的是**立即生效且不会因为忘保存而丢失**。

> **踩坑**：`COMDLG_FILTERSPEC` 只借用指针，最初写成指向 `Utf8ToWide(...)` 的**临时对象**，出了语句就悬空。改为先存进具名局部变量再取 `.c_str()`。

### 14.4 `InputSystem` 默认改为 XInput

原先默认 `dinput`。现在 PC 上的手柄绝大多数自称 Xbox 标准控制器，而 **XInput 下左右扳机各占一轴**，刹车不需要 DirectInput 那套「两扳机共用 Z 轴」的配对处理（也就是「扳机配对警告」想解决的那个坑）。

- `Main.cpp` 默认值：`dinput` → `xinput`
- 下拉列表顺序：XInput 提到第一项
- `InputSystem` 的两处回退默认值同步改为 `xinput`

DirectInput 仍然保留，老式方向盘、飞行摇杆 XInput 根本看不到，只能用它。

### 14.5 Tab 合并：把空页面收掉

三个 tab 只有寥寥几行，各自占一页反而是负担。合并的依据是**内容同属一个话题**，不是"放得下就合"：

| 合并前 | 合并后 | 内容 |
|---|---|---|
| `力反馈` + `灵敏度` | **`力反馈 / 灵敏度`** | 力反馈 5 项 + 灵敏度 7 项 + 「校准摇杆」按钮 |
| `网络` + `其他` | **`网络`** | 网络板 5 项 + 输出(TCP/UDP) 4 项 + 调试转储 2 项 |

实现方式：**页内加区块小标题**，而不是把两组控件混在一起排。

```cpp
UiSep("力反馈");
CreateControls(config, "ForceFeedback");
UiSep("灵敏度");
CreateControls(config, "Sensitivity");
```

> **`UpdateTempValues` 的时序有个细节**：两个 group 必须在**动控件之前一起 init**，再在**动完控件之后一起提交**。如果按 group 逐个 init→控件→提交，第二个 group 的 init 会把第一个 group 已经改好的值覆盖掉。

**`网络` 的组名保持不变**：只是把原先归在 `Misc` 组的那几项在 `DefaultConfig()` 里改标为 `Network`，这样界面分组变了而**配置文件的键名一个没动**，已有 `Supermodel.ini` 不受影响。

> **删掉「其他」条目的连带问题**：`力反馈` / `灵敏度` 这两个字符串原先在翻译表里是作为 **tab 名**存在的。合并后它们改当**区块标题**用（`UiSep`），我一度顺手把表里那两条删了，结果审计立刻报出 2 处缺失——**同一个字符串换了用途，表里的条目依然要留着**。已补回并注明它们是页内标题。

### 14.6 底部状态栏被裁切

**症状**：`Saved` 这行文字显示不全。

**根因是两处量的错误叠加**：

1. 页脚高度写死 `44.0f`，而它实际要容纳**两行**（按钮行 + 状态行）。`GetFrameHeight() + GetTextLineHeight() + 间距` 才是真实需求。
2. 状态行定位用 `topY + available - footer * 0.18f`，这个 **0.18 是凭空来的数字**，算出来的位置在内容区之外，于是整行被裁掉一半。

**修复**：页脚高度按样式推导，状态行定位到预留的第二行：

```cpp
const float buttonHeight = ImGui::GetFrameHeight();
const float statusHeight = ImGui::GetTextLineHeight();
const float footer = buttonHeight + statusHeight + gap * 2.0f;
...
const float statusY = topY + available - statusHeight - gap * 0.5f;
```

**无论状态行是否显示都预留高度**，所以面板不会因为提示出现/消失而改变尺寸、按钮也不会跳动。

### 14.7 保存设置拆成「默认位置 / 另存为」

「保存设置」不再直接写盘，而是弹二选一：

- **保存到默认位置** → `Data\Config\Supermodel.ini`
- **另存为...** → `IFileSaveDialog`（自带覆盖确认），选定后写到该路径

**两个实现要点**：

- **另存为只影响那一次写入**。写完立即清空目标路径，否则后续每次普通保存都会继续覆盖那份导出副本，而用户以为自己存的是默认位置。
- **写入点有两处**（即时保存、退出时保存），**两处都要走同一个重定向**，只改一处会导致「退出时保存」无视另存为目标。

### 14.8 「载入设置」无效：`FromINIFile` 返回值写反了

**症状**：点「载入设置」→ 选完文件 → 对话框关闭，**什么都没发生**。

**根因**：`Util::Config::FromINIFile()` 的返回值语义是「**是否出错**」，不是「是否成功」：

```cpp
if (file.fail()) { ErrorLog(...); return true; }   // 失败 → true
...
return false;                                      // 成功 → false
```

我写成了 `if (!FromINIFile(...)) return;`——**看起来像健全性检查，实际把每一次成功加载都丢掉了**。

**这个约定有旁证**：`Main.cpp` 的三个调用点都忽略返回值，而两个 `FromXMLFile()` 调用点写成 `if (FromXMLFile(...)) { 报错 }` —— 都是「true = 出错」。**整份代码里只有我这一处写反。**

**修复**：改用 `const bool couldNotOpen = FromINIFile(...); if (couldNotOpen) return false;`，变量名直接表明语义，避免下次再读错。同时让函数返回 `bool`，失败时弹「这个文件里没有可用的设置」而不是静默关闭对话框——**静默关闭会被理解成"成功了"**。

**顺手补了一个边界**：空文件会「解析成功」成一个空 section，应用它会**把所有设置静默重置为默认值**（看起来像数据丢失，不像加载）。所以统计到 0 个有效项时也按失败处理。

---

## 十五、漏译审计（发现 5 处）

### 15.1 用户的疑问：屏幕上一行中文没被翻译

用户指出「刹车绑定的扳机形式与当前输入系统不符」这行在英文界面下仍是中文。**确实漏了。**

### 15.2 为什么截图看不出来，只能靠审计

`Tr()` 的策略是「查不到就原样返回」。这带来一个**结构性后果**：

> **在中文模式下，漏译完全不可见**——所有字符串都显示中文，漏没漏看起来一模一样。

所以漏译只会在切到英文时暴露，而且是一行一行地暴露。**必须靠审计，不能靠观察。**

### 15.3 第一次审计是错的（重要教训）

第一版脚本检查「每个 `Ui*()` 包装函数的第一个参数」，报告 **0 处缺失**——但这是**假阴性**，因为：

| 漏掉的形式 | 例子 |
|---|---|
| 包装函数没进白名单 | `UiTextColored(...)` ← **正是漏的那一行** |
| 字符串**数组**元素 | `maskItems[]`、输入系统列表 → 经 `UiItems()` 逐个 `Tr()`，但数组元素本身不是任何函数的参数 |
| 宽字符串 | `dialog->SetTitle(L"...")`，COM 对话框标题 |

修正为**扫描整个文件的字符串字面量**（跳过注释，把相邻字面量按编译器规则拼接），再用「是否含中日韩字符」过滤。一次就抓到 5 处。

> **通用教训**：审计脚本的**覆盖率**比它的**输出**更需要被怀疑。"0 个问题"和"我的扫描方式恰好没覆盖到问题"在输出上无法区分。第一版脚本没有报错、没有告警，只是安静地漏掉了整类字符串。

### 15.4 修正的 5 处

| 位置 | 字符串 | 说明 |
|---|---|---|
| `UiTextColored` | 刹车绑定的扳机形式与当前输入系统不符，刹车会没反应。 | **用户指出的那一行** |
| COM 对话框 | 选择 Model 3 ROM 目录 | ROM 目录选择框标题 |
| COM 对话框 | 选择要载入的设置文件 | 新增的载入对话框标题 |
| COM 过滤器 | Supermodel 设置 (*.ini) | |
| COM 过滤器 | 所有文件 (*.*) | |

对话框用的是 **UTF-16**（shell API），而 `Tr()` 返回 UTF-8，所以改成 `Utf8ToWide(Tr("..."))`。

### 15.5 `Scripts/AuditTranslations.ps1`

已加入 `build.bat`，**每次构建都跑一遍**并打印计数：

```
Checking the English translation table...
Table keys                          : 323
Distinct Chinese strings in Gui.cpp : 269
Missing from the table              : 0
All Chinese strings in Gui.cpp have an English entry.
```

**刻意不设为致命错误**：审计可能因「字符串存在但从不在界面出现」而误报，不该因此挡住一次发布。但计数每次都打在构建输出里，**应该永远是 0**；不是 0 就按列表补表。

---

## 十六、载入设置后高级设置页变空：`MakeCopy()` 丢掉了 `ValueRange`

### 16.1 现象

点「载入设置」选一个 `.ini`，语言切换、状态栏报告、ROM 目录判定全都正常，`Supermodel.log` 里也能看到载入后的值 —— 但 **「核心 / 画面 / 声音 / 网络 / 力反馈」五个 Tab 里一个控件都没有**，「游戏」和「控制器」两页不受影响。

### 16.2 根因

`Gui.cpp` 里这几个 Tab 的控件是**遍历配置树、按分组名筛选**生成的：

```cpp
auto vRange = val->GetValueRange();
if (vRange) {                       // 范围为空 → 整项直接跳过，连报错都没有
    if (vRange->GetGroup() == group) { ... 画控件 ... }
}
```

「这一项属于哪个 Tab」这个信息不在键名里，而在挂在每个值上的 `ValueRange` 对象里（`Node::Set(key, value, group, min, max, list)` 创建）。所以范围一丢，这些 Tab 就全空，而且**不报任何错**。

丢的位置是 `LoadSettingsFromFile()` 的最后一行：

```cpp
config = merged;    // Gui.cpp:2180
```

`Node::operator=` → `DeepCopy()` → `m_value = that.m_value->MakeCopy();` → `ValueInstance` 的拷贝构造：

```cpp
ValueInstance(const ValueInstance<T> &that)
  : GenericValue(std::type_index(typeid(T))),   // ← 只传了 type，基类不是拷贝构造
    m_data(that.m_data)
{
}                                              // ← m_valueRange 从来没被复制
```

`GenericValue` 的 `m_valueRange` 是 `private`，而拷贝构造走的是 `GenericValue(std::type_index)` 这个「按类型初始化」的构造函数，不是隐式拷贝构造 —— 于是**每一次值的拷贝都会静默丢掉范围**：`Node` 拷贝构造、`operator=`（即 `LoadSettingsFromFile`）、`ConfigBuilders.cpp` 里的 `CopyINISection()` 全都中招。

### 16.3 为什么「启动时正常、只有载入后不正常」

启动时的 `MergeINISections(&state->config, DefaultConfig(), state->fullConfig)` 是**直接构造**目标节点、没有经过任何拷贝，范围完好；而且 `MergeINISections()` 末尾自己会把 `x`（默认值）上的范围重新贴回到合并结果上，所以 `.ini` 解析出来的一律是 `std::string`、本来没有范围的值也能拿到范围。

`LoadSettingsFromFile()` 比启动路径多做的唯一一件不同的事，就是那句 `config = merged` —— 正好是唯一会走 `MakeCopy()` 的地方。

顺带一提，`UpdateTempValues()` 结尾有一句 `it->GetValue()->SetValueRange(vRange);` 会重新贴回范围（因为 `config.Set()` 换掉值类型时会重建 `ValueInstance`），但**它整段被 `if (vRange)` 包着**：范围已经在拷贝时没了，这里就没有任何东西可以恢复。

### 16.4 修法

在拷贝构造里把范围带上（`Src/Util/GenericValue.h`），从源头修，一次覆盖上述所有拷贝路径：

```cpp
// 需要 const 版本：拷贝构造的源是 const
std::shared_ptr<ValueRange> GetValueRange() const { return m_valueRange; }

ValueInstance(const ValueInstance<T> &that)
  : GenericValue(std::type_index(typeid(T))), m_data(that.m_data)
{
    SetValueRange(that.GetValueRange());
}
```

### 16.5 验证

写了个独立小程序，完全照搬 GUI 的调用顺序：`DefaultConfig()` 风格的带分组默认值 + 模拟 `.ini`（全部是 `std::string`、无范围）→ `MergeINISections()` → `config = merged` → 再按 `GetGroup()` 统计每个 Tab 有几个控件。结果：

```
  ok    PostScanlines      group Video         value '720'
  ok    PowerPCFrequency   group Core          value '100'
  ok    SoundVolume        group Sound         value '35.5'
  ok    PortIn             group Network       value '1970'
  ok    ForceFeedback      group ForceFeedback value '0'
  tab Core           3 control(s)
  tab Video          3 control(s)
  tab Sound          1 control(s)
  tab Network        2 control(s)
  tab ForceFeedback  1 control(s)

10/10 settings kept their group, 0 failure(s)
```

这个测试**有判定力**：`DeepCopy()` 里的 `MakeCopy()` 返回的是 `make_shared<...>` 出来的**全新对象**（不是共享指针），所以范围若不被复制，`config` 上的每一项都会是 `no range`、每个 Tab 都会是 0 —— 测试结果是能反向失败的。

---

## 十七、所有 ROM 显示「未收录」：`GameXMLFile` 里的旧相对路径

### 17.1 现象

保存设置后重新打开程序，游戏列表里**每一条都变成灰色「未收录」**，看起来像 `Games.xml` 丢了或损坏了。

### 17.2 先看日志

```
[Error] Failed to parse Config/Games.xml (XML_ERROR_FILE_NOT_FOUND).
[Error] Game and ROM set definitions could not be loaded! ROMs will not be detected.
[Info]  GUI: 56 ROM(s) listed, 0 title(s) translated for the current language, 56 without a definition in Games.xml.
```

`Games.xml` 一点没丢，就在 `Data\Config\Games.xml`。**找不到的是 `Config/Games.xml` —— 一个相对路径。**

### 17.3 根因

旧版 Supermodel 的 `GameXMLFile` 记的是**相对工作目录**的路径：

```
GameXMLFile = Config/Games.xml
```

以前 `Config\Games.xml` 就在 exe 旁边，所以能用。数据搬进 `Data\` 之后这个值指不到任何东西，**而它是显式设置，会覆盖程序内置的绝对路径**，于是 `GameLoader` 解析失败 → `games` 映射为空 → `ScanRomFolder()` 里每个 `.zip` 都匹配不到定义 → 全部标为未收录。

这个坏值是怎么进到配置里的：用**「载入设置」**导入了一份旧配置（那份 `ROMPath = E:\EMU\model-3-romset-2020` 就是证据，默认值是 `C:/Downloads`），旧配置里的 `GameXMLFile` 是旧的相对路径，随后**保存**就把它持久化了。

**为什么开发时没暴露**：`MSVC\x64\Release\` 下还留着一份旧布局的 `Config\Games.xml`，而 VS 启动时工作目录正好是 `MSVC\x64\Release`，相对路径就撞上了。干净的 `dist\` 里没有这个目录，立刻现原形。

### 17.4 修法

`Games.xml` / `Music.xml` 是**安装数据，不是用户偏好**——它们描述这个 build 支持哪些 ROM 集，跟着 exe 走。所以：

```cpp
// Main.h / Main.cpp
std::string ResolveDataFileSetting(const std::string& configuredPath, const std::string& fileName);
```

- 配置里指定的路径**只有真的能读到**才采纳（相对路径按 `FileSystemPath::ResolvePath()` 锚定到 **exe 目录**，与 `LogFile` 的既有约定一致）；
- 否则退回 exe 旁边自带的那份，并**打一条日志**说明替换了什么。

调用点两处：`Main.cpp` 的 `-print-games` / CLI 路径，`Gui.cpp:GuiCreate()` 的 GUI 路径。

另外在 `LoadSettingsFromFile()` 里把 `GameXMLFile` 也按「安装路径」对待（和 `ROMPath` 一样特殊处理）：载入设置文件不会用一份指向别处的路径覆盖它，这样保存出去的 ini 里不会再留着坏值。

> 注意这里**不能只用 `ResolvePath()`**：`Config/Games.xml` 解析到 exe 目录后是 `<exe>\Config\Games.xml`，新布局下依然不存在。必须配合「不存在就回退到安装位置」。

### 17.5 验证（三种边界，实测）

用 `-print-games`，它在初始化 SDL 之前就返回，无需窗口：

| 配置里的值 | 结果 |
|---|---|
| `Config/Games.xml`（旧版相对路径，指向已不存在的位置） | 拒绝 → 回退到 `Data/Config/Games.xml`，**游戏列表正常** |
| `Data/Config/Games.xml`（相对，但相对 **exe 目录**能解析到） | **采纳**，无回退日志 |
| `Z:/no-such-folder/Games.xml`（完全不存在） | 拒绝 → 回退，并打日志 |

```
Error: The settings point Games.xml at 'Config/Games.xml', which cannot be read.
       Using 'E:\...\dist\Supermodel-VK\Data/Config/Games.xml' instead.
Games defined in E:\...\dist\Supermodel-VK\Data/Config/Games.xml:
    bassdx           Sega Bass Fishing (USA, Deluxe)
    dayto2pe         Daytona USA 2 - Power Edition (Japan)
    ...
```

第二项特意**从别的目录启动**（`cd` 到仓库根后再运行），确认解析基准是 exe 目录而不是工作目录——否则「相对 CWD 也能撞上」会让这个测试失去判定力。

---

## 十八、机柜背景图（Bezel）

### 18.1 目标

1:1 点对点或普通 4:3 时，屏幕两侧（以及 1:1 时的上下）会留黑边。把一张机柜艺术图铺在这些黑边上，画面就不再孤零零地悬在纯黑里。

**要求：绝对不能盖住游戏画面，图层必须在游戏后面。**

### 18.2 关键发现：黑边本来就是「透明」的

三条既有代码合起来说明这件事：

```c
// Vulkan3D.cpp —— 场景清屏只设了 alpha，RGB 默认 0
color.clearValue.color.float32[3] = 0;
```
```c
// Vulkan2D.cpp —— 合成管线开着 alpha 混合
blend.blendEnable = VK_TRUE;
blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
```
```c
// Vulkan2D.cpp —— 所以黑边显示的是这个清屏色，不是场景画的黑
color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
color.clearValue.color = { { 0, 0, 0, 1 } };
```

也就是说：**只要在清屏之后、场景之前多画一层背景，黑边就会自动露出来** —— 那片区域 alpha 是 0。

**但没有采用这个做法。** 它依赖「场景清屏的 alpha 恰好是 0」，哪天有人改了清屏值，背景图就会突然整片消失，而且原因极难定位。改用**裁剪框**方案，与 alpha 完全无关。

### 18.3 实现：四次绘制，各自一个裁剪框

背景图在**后处理之后**画进交换链，只覆盖游戏区之外的区域：

```
合成 → 后处理（荫罩/扫描线/辉光/增益）→ 【背景图】 → 截图 → 呈现
```

放在后处理之后是刻意的：**荫罩在显像管玻璃上，不在机柜上**。让机柜照片被打上点阵条纹、再被 `PostGain` 提亮，观感会很怪。

**为什么是四次绘制而不是一次设四个裁剪框**：裁剪框数量在**管线创建时**就固定了（`VkPipelineViewportStateCreateInfo::scissorCount`），而合成管线声明的是 `1`。要一次设四个就得再建一条管线、再建一套 layout，只为省三次四顶点的 draw —— 不划算。四条边各画一次，各带一个裁剪框，成本可以忽略。

全屏四边形本身带 uv 0..1，所以裁剪框直接就决定了「背景图的哪一块落在哪条黑边上」，不需要任何按边几何。

### 18.4 几何：抽成纯函数 + 性质测试

黑边的几何有两种错法，**在屏幕上都不显眼**：

- 裁剪框**压到游戏区**几个像素 → 机柜图盖在画面上；
- 裁剪框**没铺满**黑边 → 画面旁边留一条黑缝。

所以几何没有留在渲染器里，而是抽成 `Src/Graphics/Vulkan3D/BezelLayout.h` 里的**纯函数**（不含任何 Vulkan 类型），配一个脱离 GPU 的**性质测试**：对输出尺寸、画面尺寸、偏移、`correction` 做扫描，断言**四块黑边加游戏区恰好铺满整个输出** —— 不重叠、不留缝、每块非退化且不越界。

> 游戏区本身是**两个矩形的并集**：3D 视口，和 2D 瓦片层（它比视口偏移 `m_correction` 像素）。只取其中一个就会切到另一个的边。

> **2026-10-12 修正**：该函数原先返回的是**外扩** `correction` 的并集。后来查明 `m_correction` 的用途不是"外扩"，而是**内缩**——2D 瓦片层确实比 3D 层偏移那么多（`Render2D.cpp:430` 注释：*"for some reason the 2d layer is 2 pixels off the 3D"*），而 OpenGL 后端用 **scissor 四边各裁掉 `correction`** 把这个偏移的溢出盖掉（`Main.cpp:574`~`585`、`Render2D.cpp:328`）。Vulkan 后端此前**复制了偏移但没有裁剪**，于是那些溢出像素直接可见（用户报的"边缘白线/蓝线"）。现在 `ComputeBezelGameRect()` 改为返回**内缩**矩形，`DrawLayer()` 也按它裁剪，两个后端显示的画面这才一致。

> **⚠️ 这次修正中间做错过一次，值得记下来**：第一版把裁剪实现在 **矩形本身**上——把传给 `DrawLayer()` 的 `x/y/w/h` 缩小。**那是错的**：`DrawLayer()` 把这个矩形当作 **viewport**，而图层四边形带的 uv 是 0..1，所以矩形同时决定**纹理映射范围**。把它改小不是"裁掉边缘"，而是**把整张纹理压缩进更小的区域** → 用户看到"游戏画面缩到中间去了"，四周多出一圈蓝色。
>
> **裁剪必须用 scissor，不能用 viewport**。这正是 OpenGL 的做法：`glViewport` 定位置与缩放、`glScissor` 定裁剪范围，两者互不影响。现在 `DrawLayer()` 里 viewport 用调用方给的原矩形、scissor 用裁剪后的矩形。
>
> **教训**：看到"矩形"参数时，必须先确认它在这段代码里是**裁剪**还是**几何（viewport/矩阵）**——两者的语义完全不同，改错一个是"画面被切"、另一个是"画面被缩放"。

**实测**：

```
== bezel geometry invariants ==
  real resolutions: 16 cases
  sweep: 12740 cases
  ok    wide background produces no bars
  ok    a picture that fills the output produces no bars
12756 case(s) checked, 0 failure(s)
ALL PASS
```

覆盖的真实分辨率包括 4K 4:3 铺满（左右各 480 px）、4K 整数倍 5x（1:1，**四边都有黑边**）、1440p、1080p、16:9 窗口、4:3 窗口（无边）、21:9 超宽。

**判定力已反向验证**：把左黑边故意改成铺满全高（最可能犯的错），测试立刻报出 `pixel (0,0) is owned by 2 rectangle(s), expected exactly 1` —— 它抓的是**重叠**，并且指到具体像素。

### 18.5 PNG 解码器（`Src/Util/PNGFile.h`）

为了能载入用户自己的图，需要一个**图片读取**能力。项目原先只有 `SDL_LoadBMP`（准星在用），而 `PNGFile.h` 是**写入器**，不是解码器。

**没有引入第三方库**，用已链入的 zlib 手写。范围是**除 Adam7 隔行之外的全部规范**：

| 维度 | 支持 |
|---|---|
| 色彩类型 | 0 灰度、2 真彩、3 调色板、4 灰度+alpha、6 真彩+alpha |
| 位深 | 1 / 2 / 4 / 8 / 16 |
| 滤波 | 全部五种（None / Sub / Up / Average / Paeth）|
| 透明度 | `tRNS`（调色板、灰度、真彩三种形式）|
| 隔行 | ❌ **明确报错**：`interlaced PNG is not supported; re-save the image without interlacing` |

**为什么明确拒绝隔行而不是「尽力而为」**：隔行图在正常工具里很少见，而解错的表现是「图看着有点不对」，比直接报错难查得多。一句话告诉用户怎么办更实用。

**inflate 用 `uncompress()` 而不是手写流式**：非隔行时解压后的大小是**精确可算**的（`height × (1 + 每行字节数)`），所以不需要流式解压。

**按规范，chunk CRC 不做校验**：文件坏了在 inflate 那一步就会失败，校验 CRC 只会把一个能说清原因的报错换成「校验和不匹配」。

**测试**（三种手段，共 86 项）：

```
== round trip: every colour type and bit depth, each filter type ==
== cross check against GDI+-written files ==
  ok    gdi_rgb24.png     vs GDI+ (37x23)          colourType=2 bitDepth=8
  ok    gdi_argb32.png    vs GDI+ (37x23, alpha)   colourType=6 bitDepth=8
  ok    gdi_indexed8.png  vs GDI+ (37x23)          colourType=3 bitDepth=8
  ok    gdi_indexed1.png  vs GDI+ (37x23)          colourType=3 bitDepth=1
  ok    gdi_rgb48.png     vs GDI+ (37x23)          colourType=2 bitDepth=16
== failure paths ==
  ok    random bytes rejected: not a PNG file
  ok    empty input rejected: not a PNG file
  ok    interlaced PNG rejected: interlaced PNG is not supported; ...
  ok    unknown scanline filter rejected: unknown scanline filter type
  ok    truncated file rejected: truncated chunk
  ok    corrupted image data rejected: truncated chunk

round trip: 75 case(s) checked (each = 13x7 image, one colour type, one bit depth, one filter)
ALL PASS (0 failures)
```

1. **轮转 75 组**：15 种合法（色彩类型, 位深）组合 × 5 种滤波。图片用 **13×7**（奇数宽度 → 每行不是整字节，能测到填充）。
2. **GDI+ 交叉验证 5 组**：由 **GDI+ 写出的** PNG，与同一张位图的**原始像素转储**逐像素比对。这一项独立于本项目实现，用来抓「双方都理解错的规范细节」。**覆盖度不是假设的** —— 每一步都读回实际 IHDR 字节确认 GDI+ 真的写了那些色彩类型和位深。
3. **错误路径 6 条**：确认失败是干净失败，且原因说得清楚，而不是崩溃或静默产出垃圾。

> **已知覆盖缺口**：灰度（类型 0）和灰度+alpha（类型 4）、以及位深 2/4，只有轮转测试覆盖 —— GDI+ 写不出这些格式（它会存成索引色），所以这几项是「自己写的编码器 + 自己写的解码器互相一致」，规范层面的共同误解抓不到。真实场景里最要紧的格式（类型 2/3/6 的 8 位与 16 位）都有独立实现交叉验证。

### 18.6 亮度的传递：push constant 必须整块推送

背景图亮度经 push constant 传给着色器，于是 `composite.frag` 的块从 `uint layer` 变成 `{ uint layer; float brightness; }`。

**这里有个陷阱**：push constant 的范围大小是 `range.size`，若一个调用方只推 4 字节而块声明的是 8 字节，**剩下的 4 字节是未定义值**。所以 `DrawLayer()`（画场景和瓦片层）也必须推完整的结构体、亮度传 `1.0f`，不能只推 `layer`。已在该函数处加了注释说明。

亮度只乘 **rgb，不乘 alpha**：否则带柔和 alpha 边缘的背景图会变得「更透明」，而不是更暗。

### 18.7 上传：一次性，走环缓冲

一张 4K 级别的背景图是十几 MB 纹素，而环缓冲是**每帧**重置的，不适合反复上传。但它**永不改变**，所以只上传一次：

```
m_context.Ring().Alloc(bytes, 4, offset, pointer)   // 每帧切片 64 MB，放得下
vkCmdCopyBufferToImage(...)                          // bufferRowLength = 0 → 紧凑排布，无需按 256 字节补齐
transition → SHADER_READ_ONLY_OPTIMAL
释放 CPU 侧副本
```

分配失败只是**下一帧重试**，而不是让整场游戏都没有背景图。

### 18.8 配置与界面

| 配置项 | 默认 | 说明 |
|---|---|---|
| `BezelMode` | 0 | 0 关闭 / 1 内置机柜图 / 2 自选图片 |
| `BezelImage` | 空 | 模式 2 使用的 PNG 路径；留空则退回内置图 |
| `BezelBrightness` | 100 | 背景图 rgb 百分比，10–200 |

界面放在**「画面」页**（显示选项属于那里），三档做成一个下拉而不是「复选框 + 路径框」—— 那是同一个决定，拆成两个控件反而要解释路径框为什么灰掉。亮度滑杆只在真的会画东西时才出现。

**位置有个坑**：这段必须画在 `UpdateTempValues(config, "Video", false)` **之后**。那一句会把快照里的所有 Video 键写回 config，所以任何在那之前设置的键都会被**静默还原**成编辑前的值。已在代码处注明。

**平台与生效条件**（都写进了界面提示，因为两条都猜不到）：只有 **Vulkan** 后端实现；目标与管线在启动时构建一次，所以**改完要重启**。

### 18.9 内置图的尺寸权衡

原图 `SegaModel3Bezel.jpg` 是 2752×1536、2.67 MB 的 JPEG。项目只能解码 PNG，所以转成了 PNG：

| 方案 | 体积 | 结论 |
|---|---|---|
| **全分辨率 PNG** | **6.08 MB** | 采用 |
| 半分辨率 PNG | 1.72 MB | 备选 |
| 轻微模糊后全分辨率 | — | 放弃：在 PowerShell 里手写模糊是 O(n·r) 循环，太慢，不值得为省几 MB 这么做 |

采用全分辨率的理由：这是**要给用户看的画面**（还要用来录演示视频），而 6 MB 对如今的下载来说不算什么。整个发行目录从 6.4 MB 变成 **12.2 MB**。

### 18.10 验证状态

| 项目 | 状态 |
|---|---|
| 编译 | ✅ 通过，翻译审计 0 缺失 |
| 几何正确性 | ✅ **12,756 组**性质测试，含反向验证 |
| PNG 解码 | ✅ **86 项**：75 组轮转 + 5 组 GDI+ 交叉 + 6 条错误路径 |
| 图片载入与上传 | ✅ 真实硬件实测：`Bezel: uploaded 2752x1536 (16908288 bytes) to the GPU.` |
| 黑边实际绘制 | ✅ **像素级实测**：黑边从 `0,0,0`（stddev 0）变成有内容（stddev 34，范围 0..193）|
| 不覆盖游戏区 | ✅ 性质测试证明四块黑边与游戏区恰好铺满输出；实测游戏区读数为游戏内容 |

**像素级验证的方法**（这一项一开始以为做不到，见 18.10.1）：**`PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)`**。

对比 `BezelMode` 开/关两次运行的同一区域：

| 区域 | bezel **关闭** | bezel **开启** |
|---|---|---|
| 左黑边 | `0,0,0` 纯黑，stddev 0 | **`40,36,43`，stddev 34，范围 0..193** |
| 右黑边 | `0,0,0` 纯黑，stddev 0 | **`44,41,48`，stddev 36，范围 0..212** |
| 下黑边 | `0,0,0` 纯黑，stddev 0 | **`146,146,146`，stddev 125** |

"纯黑且 stddev 为 0" 与 "有内容且 stddev 34" 是**不可能混淆**的两种读数。

#### 18.10.1 关于抓屏：`BitBlt` 不行，`PrintWindow` 行

先前判断"无法抓到 Vulkan 画面"是**不完整**的，这里更正：

| 方法 | 结果 |
|---|---|
| 桌面 `BitBlt`（全屏 flip 模式） | ❌ **纯黑** |
| 窗口 `Graphics.CopyFromScreen` | ❌ 纯黑 |
| 模拟器自带截图（换 `Alt+S` 注入） | ❌ 送不到游戏窗口（见下） |
| **`PrintWindow` + `PW_RENDERFULLCONTENT`** | ✅ **可用** |

`PW_RENDERFULLCONTENT`（`0x2`）让窗口把自己渲染进 DC，能拿到 DWM 合成的内容；普通 `PrintWindow` 和 `BitBlt` 拿不到。

> **注意事项**：`PrintWindow` 渲染的是**整个窗口**（含标题栏和边框），而客户区坐标是相对客户区的。所以抓到的图**顶部约 30 px 是标题栏**，内容整体有偏移 —— 分析区域时要留意，否则会把标题栏的亮色误读成画面内容（本次就一度如此）。

**输入注入为什么不可靠**：`CDirectInputSystem` 自己 `PeekMessage`，按键必须**持续**到至少一帧被轮询到才生效；而 `SetForegroundWindow` 受 Windows 置前限制，实测 `GetForegroundWindow()` 返回 `0`，按键根本没送到游戏窗口。所以"截图靠按键触发"这条路线不可靠，**改用抓屏**。

### 18.11 首次实测未生效：描述符布局的绑定数量漏改

**症状**：勾选「内置机柜图」并重启，黑边仍是纯黑，**日志没有任何报错**，背景图也报告加载成功。

**根因**：`composite.frag` 的数组从 `uLayers[3]` 扩到 `uLayers[4]`、描述符**写入**的数量也从 3 改成 4，但**描述符布局（layout）的绑定数量漏改**，仍是 3：

```cpp
bindings[1].descriptorCount = 3;      // ← 漏改，应为 4
...
write.descriptorCount = 4;            // ← 写入 4 个
```

后果链条：

1. 第 4 个描述符写入**超出该绑定声明的数量**，被丢弃；
2. 着色器里的 `uLayers[3]` 依然能编译、依然会执行；
3. 它读到的是**数组之外**的描述符，在本驱动上表现为一张**完全透明的纹理**；
4. 合成管线开着 alpha 混合（`SRC_ALPHA / ONE_MINUS_SRC_ALPHA`），于是黑边与"什么都没有"混合 → **画面毫无变化**。

**所以三个现象同时成立**：draw call 确实执行了、没有任何错误、却完全看不见。

**修法**：把三处（布局绑定数量、描述符写入数量、着色器数组长度）用**一个命名常量**绑在一起，避免再次漏改：

```cpp
constexpr uint32_t kCompositeLayerCount = 4;   // 0 底层瓦片 / 1 3D 场景 / 2 顶层瓦片 / 3 背景图
```

**验证层本来会抓到它**（`VUID-VkWriteDescriptorSet-descriptorCount-00311`），但本机**没有安装验证层**（见 9.7），`VkValidation = 0`，所以这条唯一的自动告警不存在。**这是 9.7 那条"建议装 Vulkan SDK 或 RenderDoc"的又一个具体代价。**

**另外加了一条诊断日志**（首次绘制及几何变化时输出），把"背景图不出现"的几种成因一次性区分开：

```
[Info]  Bezel: enabled=1 uploaded=1 extent=1600x900 game=(300,62 996x772) bars=4 brightness=1.00
[Info]  Bezel:   bar 0 = x 0 y 0 w 1600 h 62
[Info]  Bezel:   bar 1 = x 0 y 834 w 1600 h 66
[Info]  Bezel:   bar 2 = x 0 y 62 w 300 h 772
[Info]  Bezel:   bar 3 = x 1296 y 62 w 304 h 772
```

这组数字与 `game=(300,62 996x772)`（整数倍 2x：992×768 居中 + correction 4）逐项吻合。

> **教训**：凡是"一个数组的长度在 N 个地方各写一遍"的地方，就会出现这种漏改。**把数量提成常量**成本几乎为零，而漏改的代价是"功能静默失效且不报错"。另外，**没有验证层的 Vulkan 调试基本等于盲改** —— 这个 bug 如果有验证层，第一帧就会报出来。

### 18.12 已知限制

- **背景图拉伸铺满窗口，不保持宽高比**。当前主文件 2652×1492 是 **16:9 精确**（1.7775 vs 1.7778），在 3840×2160 下畸变小于 0.1%，看不出；但用户自选的 4:3 图放到 16:9 窗口里会被明显拉伸。要修得加「适应 / 裁剪」模式，改动比看起来大（要在合成着色器里传 UV 矩形，而 push constant 的布局对齐容易出错），所以留作后续。
- **只有 Vulkan 后端**。OpenGL（New3D / Legacy3D）是另一套完全独立的渲染代码，要单独实现。
- **需要重启生效**（管线在启动时创建一次）。
- **截图会包含背景图** —— 应该是想要的，但先说明。
- 背景图**不参与后处理**（这是刻意的，见 18.3），所以机柜图不会与游戏共用同一套扫描线/荫罩。

### 18.13 背景图的主文件位置与构建规则

**背景图后来被换成了作者自己的版本**（2652×1492、**16:9 精确**、不透明 RGBA），放在**项目根目录** `Supermodel3Bezel.png`。

> **宽高比这一点比看上去重要**：因为背景图是**拉伸铺满窗口**的（18.12 的已知限制），2652/1492 = 1.7775 与 16:9（1.7778）几乎一致，在 3840×2160 上基本**没有畸变**；而先前那份 2752×1536（1.7917）会有轻微横向拉伸。**换图时优先选 16:9**。

**`build.bat` 的取图规则**（按顺序）：

1. `%ROOT%\Supermodel3Bezel.png` —— **主文件**，优先
2. `%ROOT%\Assets\Supermodel3Bezel.png` —— 回退（旧布局用）

**为什么把主文件放在项目根目录**：根目录是**实际编辑素材的地方**（旁边就是 `wallpapaer.png`、`sega model 3 bezel.gif`、`Supermodel-VK.jpg`、`output.ico` 这些作者素材）。放在这里意味着**改完直接构建就生效**，不需要再往第二个位置复制一遍。

**为什么删掉了 `Assets\` 里那份**：两份文件就是两份会漂移的真相。删掉重复的那份（那是从 JPEG 转出来的旧版），`Assets\` 只留准星图，与 `Assets\DIR.txt` 的描述一致。

**构建时会打印用了哪一份、多大**：

```
Bezel    : Supermodel3Bezel.png from E:\...\  (5954004 bytes)
```

这样"到底打包了哪张图"是**看得见的**，而不是靠相信规则。

**主文件缺失时不报错**（背景图是可选功能）：

```
Bezel    : not found (no backdrop will be drawn)
```

**顺手修掉的一处自伤**：我在这个新注释里写了中文 `"画面"`，让 `build.bat` 出现 **6 个非 ASCII 字节** —— 正是本文件明令禁止的（cmd 按控制台代码页解析批次文件，见 12.5）。已改回纯英文，现在仍是 **0 个非 ASCII 字节**。

> **教训**：连"给脚本写注释"这种顺手的事，也要过一遍自己定的规矩。`build.bat` 的 ASCII 约束值得在改完后**复查一次**，而不是相信"我没写中文"。


---

## 十九、启动时控制台黑框一闪而过

### 19.1 现象与根因

从资源管理器或快捷方式启动，会有一个黑色控制台窗口**一闪而过**。

这不是"隐藏得太慢"，而是**不可能隐藏掉**的：项目链接时用的是 `/SUBSYSTEM:CONSOLE`（四个配置全是），而 Windows 会在进程启动阶段、**在我们的任何代码运行之前**就为该子系统分配并显示控制台。`ShowConsoleWindow(false)` 只是事后 `ShowWindow(SW_HIDE)` —— 它掩盖不了窗口**已经被显示过**的事实，只能赌"隐藏发生得足够快"。在快机器上这个赌注会输，而且表现为一闪。

### 19.2 修法：改子系统的类型

```
/SUBSYSTEM:CONSOLE  →  /SUBSYSTEM:WINDOWS
```

没有控制台子系统，就**没有东西可以出现** —— 不是"隐藏得快"，而是"从未创建"。

**入口点不需要改**：`SDL2main.lib` 里（`SDL_windows_main.c`）**同时**提供了

- `console_ansi_main`（被 `#define` 成 `main`，控制台子系统的入口）
- `WinMain`（Windows 子系统的入口）

两者都走同一条 `main_getcmdline()` → `SDL_main()` 路径。链接器把入口点从 `mainCRTStartup` 换成 `WinMainCRTStartup`，`WinMain` 从同一个库里解析出来，**构建直接通过**（如果这个符号不在，会立刻报 `LNK2019: unresolved external symbol WinMain`，是个清晰可查的错误）。

### 19.3 代价：`printf` 没有去处了

改成 Windows 子系统后，`printf` / `std::cout` 默认没有任何地方可写。而项目里**大量**使用它们（`-print-games` 的列表、`Music volume: %d%%`、`Screenshot created: ...` 等）。所以两种情况要**显式**处理：

| 场景 | 处理 |
|---|---|
| 从 shell 启动（cmd / PowerShell） | `AttachConsole(ATTACH_PARENT_PROCESS)` **附着到父进程的控制台** —— 命令行输出照旧出现在你已经看着的那个窗口里，**不新建窗口** |
| 从资源管理器启动 | 没有父控制台可附着，`AttachConsole` 失败，**什么也不做** → 无窗口 ✅ |
| `-show-console` / `ShowConsole = 1` | 先试附着父控制台，失败则 `AllocConsole()` 自建一个 |

日志文件（`Data\Supermodel.log`）与上面这套机制完全无关，始终照常写入 —— 这才是真正要看的输出路径。

### 19.4 两个必须小心的点

**① 不能隐藏别人的控制台。** 附着到父 shell 的控制台后，`GetConsoleWindow()` **不再返回 NULL** —— 如果这时执行"隐藏控制台"，被隐藏的是**用户自己的 cmd 窗口**。所以用一个 `s_consoleIsOurs` 标志区分所有权：

```cpp
static bool s_consoleIsOurs = false;   // 只有本进程 AllocConsole() 出来的控制台才归我们
```

`ShowConsole = 0`（默认）时：`s_consoleIsOurs` 为假 → **直接返回，什么都不隐藏**。这既不误伤用户的 shell，也不需要隐藏（本来就没有我们自己的控制台）。

**② 不能吃掉输出重定向。** 附着力父控制台后如果无条件 `freopen("CONOUT$")` 重绑 `stdout`，那么

```
Supermodel-VK.exe -print-games > list.txt
```

**会写出一个空文件** —— 调用方要求写到文件，我们却把流指向了控制台。所以要在**任何时候都可能创建控制台之前**先记录一次 stdout 是否已经可用：

```cpp
HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
s_stdoutProvided = (output != nullptr && output != INVALID_HANDLE_VALUE);
```

`BindStandardStreams()` 在 `s_stdoutProvided` 为真时**不做任何事**。这条规则对两种调用场景都正确：`AllocConsole` 只会在"本来就没有控制台"时发生，而那种情况下 stdout 必然不可用。

### 19.5 验证（含阴性对照）

**改动的实质是"Windows 不再为我们分配控制台"**，所以最有说服力的验证不是看屏幕（一闪根本看不清），而是**枚举进程的顶层窗口，看有没有控制台类窗口**：

```
=== 改为 Windows 子系统后（dumpbin /headers）===
2 subsystem (Windows GUI)        ← 原来是 3 (Windows CUI)

=== 阴性对照：检测逻辑能否看见"一定存在"的控制台？ ===
--- cmd.exe (control) ---
  class='PseudoConsoleWindow' visible=True
  console-class windows: 1 (visible: 1)
  control OK - the check can see consoles

=== 目标：从资源管理器启动（父进程链无控制台），ShowConsole = 0 ===
  class='SDL_app' visible=True
  class='temp_d3d_window_...' visible=False
  ... （IME / OpenGL 辅助窗口，均不可见）
  visible console windows: 0
==> PASS: no console window

=== 目标：从资源管理器启动，ShowConsole = 1 ===
  class='ConsoleWindowClass' visible=True
  visible console windows: 1
==> PASS: ShowConsole=1 does create a console
```

**命令行输出没有回归**（这条是 19.4② 的验证）：

```
A) 重定向到文件:  69 行, 4179 字节   ← 输出没被吃掉
B) 通过管道捕获:  正常
C) 退出码:        0
```

### 19.6 这次验证里踩的两个坑（都产生"假通过"）

**① `Add-Type` 默认按 ANSI 编组 `StringBuilder`。** 调用 `GetClassNameW` 时 UTF-16 被写进按 ANSI 读取的缓冲区，**每个类名都只回来 1 个字符**，于是 `-eq "ConsoleWindowClass"` **永远不匹配** → 报告 "0 个控制台窗口"。这个失败**看起来和通过一模一样**。必须显式写 `CharSet = CharSet.Unicode`。

**② PowerShell 函数既写管道又"返回"值。** `$x = Report ...` 会把函数输出到管道的所有字符串都收进 `$x`，于是 `if (-not $x)` 判断的是**一堆字符串**而不是布尔值。显示必须走 `Write-Host`，返回值才干净。

**③ 类名随 Windows 版本变化。** 经典 conhost 报 `ConsoleWindowClass`，新的伪控制台宿主报 `PseudoConsoleWindow`（本次对照里就是后者）。只检查前者会漏判。

**④ 从 shell 启动测不出这个 bug。** shell 自己有控制台，程序附着上去就**不会新建窗口** —— 无论代码怎么写。所以必须**用资源管理器启动**才能复现用户的实际场景。这一点最关键：前几轮测试都从 shell 启动，严格来说根本没有验证到用户的场景。

> **教训**：断言"某物不存在"的检查，**必须有阴性对照**证明它**能**发现该物存在。否则一个因编组错误而永远不匹配的比较、一个因引用错误而永远为假的判断，都会安静地报告成功。本次前两轮的 "NO CONSOLE WINDOW" 就是这种假通过。

### 19.7 已知取舍

- **从资源管理器启动时，早期致命错误只写日志，屏幕上什么也不显示**。例如配置无法解析。用户需要知道去看 `Data\Supermodel.log`。`-show-console` 可以看到。
- **从 shell 启动会附着到该 shell 的控制台**，输出与 shell 提示符混在一起（提示符可能先于程序输出出现）。这是 Windows 上这类程序的标准行为。
- **`-help` 从资源管理器启动时看不到输出**（同上）。

---

## 二十、游戏内设置叠加层（Overlay）

### 20.1 目标

CRT 滤镜这类参数**只能靠眼睛对着运动画面调**。原有流程是"改值 → 退出 → 重启 → 看"，这个循环没人受得了。做一个**游戏运行中可开关的左侧面板**：

- 只放 **CRT 滤镜**相关的项（不显示 MSAA、不显示整数倍缩放）
- **不显示小字说明和问号提示**，但**保留实时诊断读数**
- 底部 **应用 / 撤销 / 关闭** 三个按钮
- 拖动滑杆时**画面立即变化**，不需要先按"应用"

### 20.2 障碍：游戏窗口是 Vulkan，而 ImGui 只接了 OpenGL

设置窗口用的是 **ImGui + OpenGL**，而游戏跑在 **Vulkan** 上。而且**独占全屏下任何独立窗口都不可见**，所以面板**必须由游戏自己的渲染器画进交换链**，不能另开窗口。

这意味着：

| 需要的东西 | 为什么不能复用 |
|---|---|
| 独立的 ImGui 上下文 | 设置窗口的上下文绑在 OpenGL 上，且 ImGui 的渲染后端与窗口一一对应 |
| 自己的 ImGui 渲染后端 | 仓库里只有 `imgui_impl_opengl3`，没有 Vulkan 版 |
| 自己喂输入 | 见 20.3 |

### 20.3 输入：不能用 SDL 读状态

`CDirectInputSystem` **自己泵 Windows 消息**（为了保住 RawInput 句柄），**绕过 SDL**：

```2081:2095:Src/OSD/Windows/DirectInputSystem.cpp
	while (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) 
	{
		int ret = GetMessage(&msg, NULL, 0, 0);
		...
			DispatchMessage(&msg);
```

后果是**在游戏里 `SDL_GetMouseState` / `SDL_GetKeyboardState` 读到的是陈旧状态**。

**解法**：用 `GetAsyncKeyState` + `GetCursorPos` **直接读物理状态**，完全不需要消息泵，也就**不干扰任何输入系统**。边沿由逐帧比较得出。

**手写的按键映射表只覆盖面板需要的键**（方向键、Tab、Enter、Esc、退格、数字、修饰键）—— 面板全是滑杆/下拉/按钮，没有文本输入框，所以不需要字符事件；滚轮也拿不到，但不需要。

### 20.4 用官方后端，而不是自己写

**这里我判断错过一次。** 最初的理由是"ImGui 是 `1.92.6 WIP`，官方后端**必须严格同版本**，版本不符会报一堆莫名其妙的编译错"，于是打算自己写一个精简的 ImDrawData 渲染器（约 450 行）。

**这个理由是未经验证的假设。** 实际验证下来：

1. 取 master 的后端 → **确实编译失败**，报 `'DrawCallback_ResetRenderState': is not a member of 'ImGuiPlatformIO'`；
2. 但**本地核心比 master 后端旧**，所以问题是**要按日期取同期修订**，不是"不能用官方后端"；
3. 用托管平台 API 按日期定位（见 `FIXES.md` 案例 7），取 `4e7c05504a95`（核心最新变更日志 12/23 之前，该文件最后一次改动是 12/17）→ **一次编译通过**。

于是**删掉手写渲染器**（`ImguiVulkan.h/.cpp` + 两个自研着色器），改用官方后端。

**顺带解决的两件事**：

- 官方默认 `#include <volk.h>`，而本项目的 volk 在 `Src/Pkgs/volk/volk.h`。官方为这种情况留了配置项，写进 `imconfig.h`（官方明确要求放这里，因为要能被后端的 `.cpp` 看到）：
  ```cpp
  #define IMGUI_IMPL_VULKAN_USE_VOLK
  #define IMGUI_IMPL_VULKAN_VOLK_FILENAME <volk/volk.h>
  ```
- 后端用的是**动态渲染**路径，要求 `UseDynamicRendering = true` + `PipelineInfoMain.PipelineRenderingCreateInfo` 的 `sType` 必须填对，且 `RenderPass` 必须是 `VK_NULL_HANDLE`（后端有断言）。项目**本来就已经启用了 `VK_KHR_DYNAMIC_RENDERING`**，所以可以直接用。

### 20.5 参数每帧回读（实时预览的前提）

原实现把后处理参数在**构造函数**里读进成员，之后永不更新 —— 这是"改了要重启"的根因。改动：

- 抽出 `ReadPostSettingsFromConfig()`，**构造函数与刷新共用**（避免两处 clamp 不一致）
- 新增 `RefreshPostSettings()`，在 `EndFrame()` 里**每帧**调用
- 滤镜**从未开启**时后处理管线**没有创建**，所以刷新逻辑要能**按需补建**（否则切到"开启"会绑空句柄）

**这里踩到了一个顺序陷阱**，值得单独说：刷新最初放在 `DrawPostPass()` 里，而**那个函数只在滤镜已开启时才被调用** —— 于是"全关 → 开启"这个**唯一需要干活的转变**永远触发不了。已移到分支**之前**。（详见 `FIXES.md` 案例 6。）

### 20.6 面板内容：复用设置窗口的控件

面板不是重写一遍控件，而是**复用** `DrawAdvancedSettingsPanel(config, overlayMode)`，`overlayMode` 决定呈现差异：

| overlayMode = true（叠加层） | 处理 |
|---|---|
| 问号提示（`HelpMarker`） | **不画** |
| 长篇说明文字 | **不画** |
| **MSAA** | **整块跳过** —— 采样数在**创建渲染目标**时固定，改了要重启，留着就是"看着能点、其实没用"的控件 |
| **整数倍缩放** | **同样跳过** —— 它只在**重算视口几何**时被读取（即窗口尺寸变化时），实时改了不生效 |
| 拍频诊断读数 | **保留** —— 它不是说明，而是**随滑杆变化的数据**，正是防"调出彩带"的那道拦截 |

**一个意外的好发现**：那些长篇说明文字**全部装在 `HelpMarker()` 里**，所以让它空操作就一次性把说明全消掉了，不需要逐处改造。

**只在启动时生效的参数还有哪些**（都从叠加层里排除了）：MSAA、整数倍缩放。其余 CRT 参数走 push constant，**全部可实时**。

### 20.7 布局：按钮必须固定在底部

第一版把按钮**内联**在正文末尾，结果被滚出视野 —— CRT 那一节在 1.5 倍缩放下已经超出面板高度，**滚动才找得到的按钮等于没有**。

改成 `BeginChild(..., -footer)`：给按钮行预留高度，正文滚动、页脚不滚。

### 20.8 两个真 bug（都在验证过程中抓到）

**① 崩溃：空上下文解引用**（退出码 `0xC0000005`）

`ImGui::GetDrawData()` 在**切换上下文之前**被调用。而游戏运行时设置窗口**已经销毁**，`DestroyContext()` 会把"当前上下文"置空 —— 代码保存的那个"之前的上下文"因此是空，`GetDrawData()` 解引用空指针直接崩。

修法：**先 `SetCurrentContext`，再取数据**。

**② 输入抑制用错时机**

抑制标志最初只在**按下开关的那一刻**设置。而面板还能从自己的「关闭」按钮关掉 —— 那条路径不经过开关分支，于是**游戏输入永远被抑制，车开不动**。

修法：改成**每帧与面板可见状态同步**（比较上一帧），两种关闭方式行为一致。

> 这两条都是"只能靠实际跑一次才发现"的：第一个日志里什么都不说，只有访问违例；第二个要点一下关闭按钮才暴露。

### 20.9 交互语义（最终）

| 按钮 | 行为 |
|---|---|
| **应用** | 把面板涉及的那 11 个键**读-改-写**回 ini（不重建整个文件，否则会抹掉按游戏分段的配置）|
| **撤销** | 还原**打开面板时**的快照并关闭 |
| **关闭** | 保留改动，本次不写盘 |
| Tab | 开关面板 |

**快照只记录面板能改的那些键** —— 记录全部配置会让"撤销"能撤销它从未碰过的东西。

### 20.10 验证

| 项目 | 方法 | 结果 |
|---|---|---|
| 后端初始化 | 日志 | ✅ `Overlay: ready (1600x900, 3 swapchain image(s), scale 1.50)` |
| 面板确实绘制 | 强制打开 + `PrintWindow` 抓帧 + 行亮度剖面 | ✅ 2028 顶点；按钮行 y 850–882 亮度 62–92（背景 ~18.6）|
| 字体（中文）| 放大裁剪查看 | ✅ 正常，非方框 |
| 按钮可见 | 行亮度剖面 | ✅ 固定在底部（修复前被滚出视野）|
| 开关面板 | ⚠️ **未能自动验证** | 见下 |
| 实时生效 | ⚠️ 由用户确认 | |

**未能自动验证开关**：合成的按键（`keybd_event`）**送不进游戏输入系统** —— `SetForegroundWindow` 被系统拒绝，窗口拿不到焦点，DirectInput 的逐帧轮询就看不到按键（连暂停键都没反应，窗口标题始终没变成 `(Paused)`）。

因此改用**强制初始状态**来验证渲染：临时把 `g_visible` 改成 `true`，一次构建即可验证绘制、字体、布局，**完全不涉及输入**；验证后还原。（详见 `FIXES.md` 案例 10。）

### 20.11 已知限制

- **只有 Vulkan 后端**。OpenGL 是独立的另一套渲染代码。
- **`push constant` 结构体变长时，所有推送点都要推完整块** —— 否则剩余字节是未定义值。已在 `DrawLayer()` 处注明。（详见 `FIXES.md` 案例 13。）
- **MSAA 与整数倍缩放不在面板里**（无法实时，见 20.6）。
- **滚轮事件没有喂给 ImGui**（拿不到），但面板不需要。

---

## 二十一、两处渲染修复：贴图脏块丢失与 2D 层边缘杂线

### 21.1 贴图偶发变黑块（既有问题，非本轮引入）

**症状**：**偶发**贴图变黑块 / 图案缺失。最明显的触发时机是**从桌面切回全屏之后**。

**第一步是确认归属**：查文件时间戳 —— `VulkanTextureCache.cpp` 最后修改是 **10-10 11:21**，**早于本次全部工作**，所以是**既有问题**。这一步立刻排除了"是我改出来的"这个方向。

**根因**：两个问题叠加。

**A. 消费型状态提前清除** —— 脏标记在**拷贝出来的同时就清零**：

```571:577:Src/Graphics/Vulkan3D/VulkanTextureCache.cpp
	{
		std::lock_guard<std::mutex> guard(m_dirtyMutex);
		dirty[0] = m_dirty[0];
		dirty[1] = m_dirty[1];
		m_dirty[0].reset();
		m_dirty[1].reset();
	}
```

**B. 失败早退** —— 上传时分配失败，**直接退出整个函数**：

```cpp
if (!ring.Alloc(bytes, 4, offset, ptr))
{
    m_rawLayout[page] = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    return;                 // ← 退出整个函数
}
```

叠加后果：**那批脏块永久丢失** —— 原始纹理页保持旧内容，而片段着色器**直接采样这两页**（`CreateDescriptors` 的注释写明）；游戏不重写同一区域就永不修正。而且这个 `return` **放弃的不止一项**：另一页、以及本页剩余部分，一起被放弃。

**为什么是"偶发"**：只在**短时间内大量写入**时才触发。而"从桌面切回全屏"正是这种时刻 —— 交换链重建、游戏暂停再恢复，积压的写入集中在少数几帧处理。

**修法**（五点，缺一不可）：

1. 用 `pending` **副本**逐个勾掉**已成功交出**的瓦片；
2. 失败时**不退出函数**，只跳出当前页的循环，**继续处理另一页**；
3. **无条件下发布局转换** —— 否则图像卡在 `TRANSFER_DST`，而着色器当作 shader-read 采样，是未定义行为；
4. **未处理的放回** `m_dirty`，下一帧继续；
5. 每次发生都**记日志**（配额给得宽裕，真发生就该看见频率）。

**关于环缓冲容量的核实**：单页 2048×1024、瓦片 32×32，**全脏也只有 8MB**，而每帧切片是 **64MB** —— 所以"环缓冲不够"本身看起来不现实。但 `UploadGeometry()` 同样有"环满则跳过"的处理，说明**耗尽是被预期会发生的**；且 `RecordSync` 在几何上传**之前**执行，纹理先拿到空间。**结论：这个 bug 平时可能不触发，只在集中写入的时刻触发。**

> 通用模式见 `FIXES.md` 案例 5。

### 21.2 边缘白线/蓝线（用户报告：蓝在左/上、白在右/下）

**先看关键代码**：OpenGL 后端用 **scissor 把画面四边各裁掉 `correction` 像素**：

```574:585:Src/OSD/SDL/Main.cpp
  UINT32 correction = (UINT32)(((*yResPtr / 384.) * 2.) + 0.5); // due to the 2D layer compensation (2 pixels off)
  ...
    glScissor((*xOffsetPtr + correction) * aaValue, (*yOffsetPtr + correction) * aaValue, (*xResPtr - (correction * 2)) * aaValue, (*yResPtr - (correction * 2)) * aaValue);
```

而**两个后端的 2D 层偏移完全相同**：

| | 2D 层位置 |
|---|---|
| OpenGL | `glViewport(m_xOffset - m_correction, ...)` |
| Vulkan | `bottomX = m_xOff - m_correction` |

**唯一差别**：OpenGL **有 scissor 裁剪**，Vulkan **没有** → 偏移露出的杂线直接可见。

`Render2D.cpp:430` 的注释说明了原因：

```430:430:Src/Graphics/Render2D.cpp
	m_correction	= (UINT32)(((yRes / 384.) * 2.) + 0.5);		// for some reason the 2d layer is 2 pixels off the 3D
```

即 **2D 层与 3D 层之间已知的错位**，OpenGL 一直靠裁剪掩盖它。**所以这不是"街机框体多出来的画面"，是 Vulkan 后端漏了一步裁剪。**

**用户观察到的"蓝在左/上、白在右/下"完全对得上**：2D 层"左移但不补偿宽度" → 左/上多出来（游戏画在有效区外的颜色，多为背景/天空色 → 蓝），右/下少一截（露出被拉伸的边缘纹素 → 偏白）。

**修法**：让 `DrawLayer()` 按同样内缩的矩形裁剪，两个后端显示的画面这才一致。

#### 21.2.1 ⚠️ 这次修复中间做错过一次（值得记）

第一版把裁剪实现在**矩形本身**上 —— 把传给 `DrawLayer()` 的 `x/y/w/h` 缩小。**那是错的**：该矩形在这段代码里是 **viewport**，而图层四边形带的 uv 是 **0..1**，所以矩形**同时决定纹理映射范围**。把它改小不是"裁掉边缘"，而是**把整张纹理压缩进更小的区域** → 用户看到：

| 现象 | 原因 |
|---|---|
| **画面缩到中间** | 整张纹理被压进缩小后的矩形 |
| **四周多出一圈蓝边** | 被压小让出的区域露出了底色 |

**正确做法**：**裁剪必须用 scissor，不能动 viewport**（这也是 OpenGL 的做法：`glViewport` 管位置缩放、`glScissor` 管裁剪，互不影响）。现在 viewport 用调用方给的原矩形、scissor 用裁剪后的矩形。

**我犯的验证疏漏**：当时**只核对了几何数据**（日志里 `game=(308,70 984x760)` 完全正确）就宣布修好 —— 而**几何数值正确不等于画面对**。这是本轮最该避免的一类错误。

**修复后的实测**（关闭背景图，逐行量非黑内容范围）：

```
row    first non-black   last non-black   width
y=200              319             1302      984
y=400              319             1302      984
y=700              319             1302      984
```

每个采样行宽度都是 **984**，与日志中的 `984x760` 一致 → 画面**填满未被缩放**。

**一处未能完全解释的测量**：实测内容范围是 `[319, 1302)`，而裁剪矩形是 `[308, 1292)` —— **整体右移 11 像素、宽度一致**。最可能是 **Daytona 2 自己画了一圈约 11 像素的黑边框**（这恰好印证"游戏确实在有效区外画东西"），但**无法排除**其他原因。所有 7 个采样行的边界完全一致，说明是**结构性边界**而非内容明暗差异。

> 通用模式见 `FIXES.md` 案例 4 与案例 11。

### 21.3 验证状态

| 项目 | 状态 |
|---|---|
| 贴图脏块修复 | ✅ 编译通过；**由用户实测多款游戏确认问题消失** |
| 2D 层裁剪 | ✅ 编译通过；几何实测与设计一致；画面填满不被缩放（实测确认）|
| 边缘线是否真的消失 | ✅ **由用户实测多款游戏确认** |
| 副作用 | ✅ 无（画面尺寸、位置均正确） |
| 翻译审计 | ✅ 359 条 / 0 缺失 |

---

## 二十二、游戏进不去的原因排查（网络板 / 驱动板 / ROM 依赖）

### 22.1 起因

用户反馈："游戏列表里不少游戏进不了游戏，是还不支持有些游戏的主机板吗？" 其中 Ski Champ 卡在自检画面。

**先排除的方向**：不是"某些主板不支持"。上游文档明确写 *"Supermodel recognizes all known Model 3 ROM sets"* —— **63 个集合全部支持**。

### 22.2 官方兼容性矩阵：有问题的只有 4 条

从 `Docs\README.txt` 第 3 节抽出：

| 集合 | 官方标注 |
|---|---|
| `srally2p` / `srally2pa` | **真的不能玩**（原型）|
| `srally2dx` | 能玩但**可能慢** |
| `swtrilgy` / `swtrilgya` | 需在 TEST MENU 把 game 设成 `U/R`，否则卡在 "WAIT SETUP THE FEEDBACK STICK" |
| `swtrilgyp` | 同上，或等几秒 |

**其余 57 个官方标注无问题。** 所以"进不去"必有其他原因。

### 22.3 核心机制：`<netboard>true</netboard>` 决定是否需要网络板

**22 个集合需要网络板**，按驱动板类型分：

| 驱动板 | 集合数 | 状态 |
|---|---|---|
| **Wheel**（方向盘）| **13** | ✅ 驱动板 ROM 齐全 —— Daytona 2 / Dirt Devils×5 / Le Mans / Scud Race×4 / Sega Rally 2 |
| **Billboard** | 4（+14 同族）| ❌ 缺 ROM（见 22.4）|
| **Ski** | 1 | ❌ 上游未完成（见 22.5）|
| 无声明 | 4 | `harley`×2、`spikeout`×2 —— 本就走哑驱动板（按设计）|

> 单人游玩依赖 `Network=1` + `SimulateNet=1` + 端口自连 —— 见第十一节。

### 22.4 一个缺失的 ROM 影响 18 个集合（静默退化）

**实测发现**：CRC **`0x0CA70F80`**（`epr-18022.ic2`，Billboard 驱动板程序）**不在用户的 ROM 集合里**，而它被 **6 个游戏家族共 18 个集合**引用：

| 游戏 | 受影响集合 |
|---|---|
| Fighting Vipers 2 | `fvipers2`, `fvipers2o` |
| Virtua Fighter 3 | `vf3`, `vf3a`, `vf3c`, `vf3tb` |
| Virtual-On O.T. | `von2`, `von2a`, `von2o`, `von254g` |
| Virtua Striker 2 | `vs2`, `vs215`, `vs215o` |
| Virtua Striker 2 '98 | `vs298`, `vs29815` |
| Virtua Striker 2 '99.1 | `vs2v991`, `vs299`, `vs299a`, `vs299j`, `vs29915`, `vs29915a`, `vs29915j` |

**为什么缺失是静默的**：该区域在 `Games.xml` 里标着 `required="false"`，而载入器对可选区域的处理是"**失败就删掉、继续**"：

```928:934:Src/Model3/Model3.cpp
    if (error_loading_region && !region->required)
    {
      // Failed to load the region but it wasn't required anyway, so remove it
      // and proceed
      rom_set.rom_by_region.erase(region->region_name);
```

于是 `rom_set.get_rom("driveboard_program").size == 0` → 驱动板创建条件里的 `&&` 为假 → **退化为"未挂载"的哑驱动板**：

```cpp
if (game.driveboard_type == Game::DRIVE_BOARD_BILLBOARD && rom_set.get_rom("driveboard_program").size)
  DriveBoard = new CBillBoard(m_config);      // 真实现
else
  DriveBoard = new CDriveBoard(m_config);     // 兜底：报告"未挂载"
```

**游戏以为有驱动板、实际没有 → 卡在自检画面。** 而日志里**只有一条 info 级的"可选区域加载失败"，没有任何地方把它和"驱动板未挂载"连起来**。

> 通用模式见 `FIXES.md` 案例 14。

### 22.5 Ski Champ：上游未完成的滑雪板模拟

截图显示：

```
NETWORK CHECK OK !!     ← 网络板通过（不是网络问题）
ROM CHECK OK !!         ← ROM 通过
IC31 - FAILED           ← 失败
X , Y , Z CHECK NG      ← 两个轴失败（Z 未实现）
PUSH TEST SW      EXIT  ← 等测试开关
```

**源码证据链**：

| 项 | 状态 |
|---|---|
| `IC31` | **源码里零引用** —— 没有任何地方实现 |
| `X` / `Y` | 接了线（`adc[0]`/`adc[1]` 来自 `skiY`/`skiX`），但默认信号源是 `JOY1_XAXIS/YAXIS` —— 没手柄就无输入 |
| **`Z`** | **不存在 `skiZ` 输入**；`SkiBoard.cpp` 里**没有任何轴处理**；ADC 只用了 `adc[0]`/`adc[1]`，**没有 `adc[2]` 给 Z**（对比：钓鱼游戏就用 `adc[2]` 做张力传感器）|

而 `SkiBoard.cpp` 的文件头是上游自述：

```26:29:Src/Model3/DriveBoard/SkiBoard.cpp
 * Implementation of the CSkiBoard class: rumble ski pad emulation
 *
 * NOTE: Simulation does not yet work. Drive board ROMs are required.
```

**结论**：Ski Champ **不可能在单人下通过自检** —— `Z` 与 `IC31` 无实现。这是上游遗留，不是配置或 ROM 问题（`ROM CHECK OK`）。

**顺带**：`Test A` / `Test B` 其实**已绑好**（`InputTestA=KEY_6`、`InputTestB=KEY_8`，见 `Main.cpp:2642`），截图底部就是在等这个。本轮的"测试开关未绑"的判断是**错的** —— 见 22.7。

### 22.6 新增的诊断（`Extra Hardware` 与网络板进日志）

**两处诊断原本只在控制台可见，从不进日志**，这是本轮**两次误判**的直接原因。已修：

| 位置 | 改动 |
|---|---|
| `Model3.cpp` 游戏信息块（`Title:` / `ROM Set:` / `Year:` / **`Extra Hardware:`** …）| `std::cout` → **`InfoLog`** |
| `SimNetBoard.cpp` 连接提示 | `printf` → **`InfoLog`** |
| `NetBoard.cpp` 连接提示 | `printf` → **`InfoLog`**；**并从 `while (!connect())` 循环里移出**（原本每轮都打印，直接转日志会刷爆）|
| **新增**：驱动板退化为哑板时**明确报因** | 在 `else` 分支加 `ErrorLog`，把"游戏要什么"和"实际缺什么"写在一句里 |

**实测效果**（跑 `von2`，缺 Billboard ROM）—— 日志现在一次给出完整因果链：

```
[Error] 'epr-18022.ic2' with CRC32 0x0ca70f80 not found in '...\von2.zip'.
[Error] Optional ROM region 'driveboard_program' in 'von2' could not be loaded.
[Error] No 'driveboard_program' ROM for 'von2', which declares a drive board. Falling back to an
        unconnected one - the game will most likely stop at its hardware self-test. ...
[Info]      Extra Hardware: Net Board, Security Board          ← 没有 "Drive Board"
```

**`Extra Hardware` 里没有 `Drive Board`** —— 这就是决定性证据，而它**以前根本不在日志里**。

**没有改的东西**（有意）：`R8/R16/R32 unknown` 这类**逐次访问**的 `printf` 保持原样 —— 它们可能逐帧触发，转成 `InfoLog` 有刷爆日志的风险；`TCPReceive`/`TCPSend` 的 `DPRINTF` 已映射到 `DebugLog`，逐字节级输出**本该**被 info 级别过滤。

> 通用模式见 `FIXES.md` 案例 15。

### 22.7 本轮我犯的两次误判（记录下来）

| 我的判断 | 实际 | 原因 |
|---|---|---|
| "卡在网络板" | ❌ 截图明写 `NETWORK CHECK OK` | 我基于 `NetBoard.cpp` 的注释（"skichamp… need total rethink"）推测，**没看用户的实际画面** |
| "测试开关默认是 `NONE`，你没法按" | ❌ 已绑好 `KEY_6`/`KEY_8` | 被 `Inputs.cpp` 里的**兜底值**骗了（那只是配置缺失时的回退），而且我的 grep 模式没匹配到 `Input` 前缀 |

**教训**：
- **先看现象，再看代码**。用户截图上写着的字，比源码注释的推测可靠。
- **区分"默认值"与"兜底值"**：`Inputs.cpp` 里的 `"NONE"` 是**注册时的回退**，真正生效的默认值在 `Main.cpp` 的 `DefaultConfig()` 里。查默认值要查**真正设值的那一处**。
- 这两次误判的**共同根因都是"诊断信息不在日志里"** —— 所以 22.6 的改动不只是方便，而是**消除了这类误判的可能性**。

### 22.8 完整游戏列表

`Config/Games.xml` 是权威数据源。已生成 **`supported-games.txt`**（项目根目录，英文）：
- **63 个 ROM 集 / 27 款游戏**
- 两种排法：**按游戏分组**（列出各版本 + 是否需要父集）与**按集合名平铺**（便于和 ROM 目录对照）
- 明确标出**41 个需要父集**的集合，以及父集名（父集 zip **必须按名字存在**）

生成方式：直接从 `Games.xml` 解析，**不是手工抄写** —— 避免出错。

---

## 二十三、渲染器自动回退 + 收敛为纯 Vulkan 1.3

### 23.1 问题：选错渲染器就只有一个关掉的窗口

`Vulkan3DEngine` 默认 `true`，而渲染器创建失败时：

```cpp
Result screen = ... ? CreateVulkanScreen(...) : CreateGLScreen(...);
if (Result::OKAY != screen) { exitCode = 1; goto Exit; }   // 直接退出
```

用户从设置窗口选了一个游戏，得到的是**一个关掉的窗口**，而原因只写在**日志**里 —— 一份用户没有理由去打开的文本文件。

### 23.2 做法：先提示、再回退、再告知

在 `Main.cpp` 的窗口创建处加了回退分支：

1. **记录原因**（`ErrorLog`，给排查的人）
2. **发界面通知**（`GuiPostNotice`，给用户）
3. **把配置改成与实际一致**（`Vulkan3DEngine = false`）
4. **清理半成品**，然后用 OpenGL 重试
5. 恢复被"Vulkan 不支持超采样"而强制成 1x 的 `Supersampling`

**关键点一：只影响本局。** `s_runtime_config` 每局开始时从 ini 重建（`MergeINISections`），且 `SaveOverlaySettings` 只回写白名单键 —— 所以运行时改动**不会被持久化**。用户更新驱动后，Vulkan 会**自动恢复**，不会被永久卡在 OpenGL。

**关键点二（真正的坑）：失败路径没有清理，导致回退会直接失败。**

```cpp
// CreateVulkanScreen：设备被拒绝时直接返回
s_window = SDL_CreateWindow(..., SDL_WINDOW_VULKAN | ...);
s_vkContext = new CVulkanContext();
if (Result::OKAY != s_vkContext->InitDevice(...))
    return Result::FAIL;          // ← s_window / s_vkContext 都没释放

// CreateGLScreen：开头就拒绝
if (s_window != nullptr) return ErrorLog("CreateGLScreen() called more than once");
```

**设备不支持时（最常见的失败）回退会撞上这句话。** 修法是在重试前调用既有的 `DestroyGLScreen()` —— 它已经能安全处理"半初始化"的 Vulkan 上下文（`Shutdown()` 里每一步都有空指针保护，`m_frames` 是定长数组）。

**关键点三：提示文案不能写死后端名。** 回退到 New 3D 还是 Legacy 3D 取决于 `New3DEngine`（回退时实测两种都出现过），所以文案写"OpenGL 渲染器"，具体哪个交给旁边那行日志。

### 23.3 顺带确认：GUI 用 OpenGL，所以不存在"先有鸡还是先有蛋"

```cpp
// Gui.cpp
SDL_WindowFlags window_flags = ... SDL_WINDOW_OPENGL ...;
ImGui_ImplOpenGL3_Init("#version 410");
```

设置界面本身就是 OpenGL 渲染的，所以**不支持 Vulkan 的机器照样能进界面、把渲染器改成 New 3D**。而且回退后能回到界面继续操作（`RunGameSession` 返回后循环不退出）。

另外确认了 GUI 改动**即时生效**：点单选框 → `SetRenderer()` 写配置 → `saveSettings` 默认 `true` → 启动游戏前写盘 → 每局开局 `FromINIFile` 重读。**不需要按保存按钮，也不需要手动改 ini。**

### 23.4 收敛为纯 Vulkan 1.3

代码原本有 1.2 回退路径（`m_use12Fallback` + 四个核心/`KHR` 双分派帮手 + 6 个必需扩展）。**去掉的理由不是性能**：

| 理由 | 成立 |
|---|---|
| 提升性能 | ❌ 约 15 次分支/帧，对比百万级顶点，测不出来 |
| 简化代码 | ✅ 消除一条路径 |
| **那条路其实跑不通** | ✅ **决定性理由** |

**为什么跑不通**：着色器用 `--target-env vulkan1.3` 编译，产物是 **SPIR-V 1.6**（核对过 8 个 `.spv` 全是 `0x00010600`）。SPIR-V 1.6 需要 Vulkan 1.3 **或 `VK_KHR_maintenance4`** —— 而 `maintenance4` **不在那 6 个必需扩展里**。所以任何走到 1.2 路径的设备都会在创建着色器模块时失败。

现在的门槛是明确的：**Vulkan 1.3 + 256 字节推送常量**（规范只保证 128，所以这一条要显式检查）。

改动清单：
- 删 `CmdBeginRendering` / `CmdEndRendering` / `CmdPipelineBarrier2` / `QueueSubmit2` 四个帮手，13 处调用改为直接调核心函数
- 删实例创建的 1.2 重试（改为明确报错，并提示可切 OpenGL）
- 删 `m_use12Fallback`（成员 + 3 处赋值）、删 6 个 `KHR`/`EXT` 扩展请求
- 设备选择里把"1.3 加分"改为"1.3 硬性要求"

> 顺带一个反直觉的点：**在 1.3 设备上不该再按名字请求这 6 个扩展** —— 1.3 设备可以不宣称这些扩展串，按名字要求反而会拒绝本可用的硬件。

### 23.5 验证方法与两个抓屏陷阱

回退用**指向不存在的 ICD** 强制触发（`VK_ICD_FILENAMES` / `VK_DRIVER_FILES`），等价于"驱动不提供该后端"，且不改任何代码。

| 测试 | 结果 |
|---|---|
| Vulkan 主路径 | `API 1.4` → `3D target 2976x2304, 4x MSAA` ✓ |
| 强制失败 → 回退 | `retrying with the OpenGL renderer (Legacy 3D)` ✓ |
| `New3DEngine` 两种取值 | 分别回退到 New 3D / Legacy 3D ✓ |
| 通知渲染 | 橙色文字 + 「知道了」按钮，实测可见 ✓ |

**抓屏陷阱（都踩过）**：

1. **`build.bat` 重建 `dist\` 会清空配置** —— ROM 路径回到默认，游戏列表变空，于是"双击启动游戏"毫无反应。测 GUI 前必须先恢复配置。
2. **`PrintWindow` 抓 OpenGL 窗口不可靠** —— 同一个未变化的窗口连抓两次，98% 的像素都不同（双缓冲 GL 窗口不会回以一致的帧）。**改用 `BitBlt` 抓屏**，但需要窗口在前台。
3. **键盘注入需要焦点，鼠标不需要** —— `mouse_event` 按坐标命中（双击能启动游戏），而 `SendInput` 的键盘事件只送给**前台窗口**；本例中 `GetForegroundWindow()` 拿不到目标窗口，所以 `Esc` 从未送进去。

**最后用了"预置初值"隔离验证**：临时把通知文本写进 `g_rendererNotice` 的初值 → 构建 → 截 GUI → 确认橙色文字与按钮正常 → **还原并重新构建**。这样把"渲染正确"和"投递正确"分开验证，避开了无法可靠驱动 GUI 的环境限制。

### 23.6 一个构建系统的坑：`/utf-8` 是按文件加的
往 `Main.cpp` 加第一个中文字符串字面量后，编译报：

```
Main.cpp(...): error C2001: newline in constant
```

原因不是语法，是**编码**：项目**没有全局 `/utf-8`**，只给携带中文的文件单独加（`Gui.cpp` / `Localization.cpp` / `Overlay.cpp`）。`Main.cpp` 之前只有中文**注释**（那些侥幸编过了），加字符串就不行。

项目自己在 `Overlay.cpp` 的条目里就写明了这个坑：

> *"...the file carries Chinese string literals, and the project does not set /utf-8 globally, so without this the compiler reads them as the system code page and mispairs the bytes - which surfaces as a baffling 'newline in constant' rather than as anything mentioning encoding."*

**修法**：按既有做法给 `Main.cpp` 也加上 `/utf-8`。

**教训**：看到 `newline in constant` 先查**文件编码**和项目的字符集设置，别去数引号配对。

### 23.7 ROM 顶点缓存的容量是算出来的，不是猜出来的
修好尺寸不匹配（见 `FIXES.md` 案例 17）之后，问题变成"**600 万这个数字够不够**"。当时只能靠累积速率外推（约 6000 顶点/秒 → 约 16 分钟），但那假设了**线性增长**，而缓存是按模型地址去重的 —— 线性假设如果没有依据，就不该拿它当结论。

**于是做了一次 13 分钟的实测**（`ROM geometry` 诊断行每 600 帧输出一次）：

```
2.5% → 5.4% → 8.1% → 12.1% → 16.7% → 20.4% → 24.0% → 26.6% → 26.8%
   ↓ 之后 57 个采样点（约 9.5 分钟）全部保持：
26.8% → 26.8% → ... （完全平直）
```

| 指标 | 值 |
|---|---|
| 饱和值 | **1,609,005 顶点 = 容量的 26.8%**（约 250 秒达到）|
| 达峰后 | **9.5 分钟纹丝不动** |
| 清空 / 报满 | **0 次** |

**两个结论**：

1. **曲线饱和，所以它是缓存而非泄漏。** 泄漏会持续上升；这条曲线在第 25 个采样点后完全平直，与"按地址去重"的实现吻合。这也回答了"玩几小时会不会爆"——**不会**，工作集是有界的。
2. **600 万有 3.7 倍余量**，正常游玩**永远不会触发清空**，所以"清空导致静态模型消失 1 帧"这个副作用在实践中不会出现。

**为什么值得做这个测量**：如果不测，"上限够不够"就只能靠推理，而推理在这里恰好会得出**偏悲观**的结论（线性外推 → 16 分钟 → 让人以为长时间游玩有风险）。**一条有平台期的曲线比任何论证都可靠。**

### 23.8 宽屏失效：3D 渲染器做了，合成器又裁回去了

用户报告"**没有真正的宽屏模式，除非选拉伸，但那样画面被拉扁**"。

**表面矛盾**：诊断日志明确显示宽屏分支 **ACTIVE**（视锥已按 1.376 展宽、视口已设为全宽），但用户看不到任何宽屏效果。

**根因**：Vulkan 的 2D 合成器有自己的裁剪矩形，而且**漏了 WideScreen 这一路**。

```cpp
// Vulkan2D.cpp —— DrawLayer 用 PictureRect() 做 scissor
PictureRect(px, py, pw, ph);
if (cx < px) { cw -= px - cx; cx = px; }
if (cx + cw > px + pw) cw = px + pw - cx;
```

```cpp
// PictureRect() → ComputeBezelGameRect()
ComputeBezelGameRect(extentW, extentH,
    m_xOff, m_yOff, m_xRes, m_yRes,     // ← 4:3 可视区
    m_correction, m_wideBg, ...);        // ← 只有 WideBackground，没有 WideScreen
```

所以流程是：

1. `WideScreen=1` → `CalcViewport` 展宽视锥、视口设为全宽 ✓
2. 3D 场景**确实按宽屏渲染** ✓
3. `DrawLayer(1, 0, 0, extent.w, extent.h)` 的 **viewport 是全宽** ✓
4. **但 scissor 被裁到 4:3 矩形** ✗
5. 结果：只有宽屏画面的**中央 4:3 部分可见**

**而 Hor+ 的数学性质是"中央区域与窄视角逐像素相同"** —— 所以裁掉两侧后，看起来**和没开宽屏完全一样**。这个性质既是"实现正确"的证明，也是"bug 不可见"的原因：渲染器做对了，合成器裁掉了，两边都"合理"，只有合起来是错的。

**「拉伸」为什么能生效**：它把 `xRes/yRes` 直接设成窗口尺寸 → `PictureRect` = 全屏 → 不裁剪；但视锥仍是 4:3 → 画面被拉扁。这正好解释了用户观察到的两个现象。

**OpenGL 侧一直是对的**：

```cpp
// Main.cpp:579
if (WideScreen) glScissor(0, correction, totalXRes, totalYRes - correction*2);
else            glScissor(xOffset+correction, yOffset+correction, xRes-2c, yRes-2c);
```

**修法**：`ComputeBezelGameRect` 增加 `wideScreen` 参数，宽屏时与 `wideBackground` 走同一分支（全宽、上下各内缩 `correction`）。`CVulkan2D` 增 `m_wide` 成员，从配置读取。

**验证**（用既有的背景图诊断打印画面矩形）：

| WideScreen | game 矩形 | 黑边条数 |
|---|---|---|
| 0 | `(468,160 984x760)` 4:3 居中 | **4**（含左右）|
| 1 | `(0,4 1920x1072)` **全宽** | **2**（只剩上下）|

左右黑边消失、画面占满全宽，与 OpenGL 的 scissor 行为一致。顺带 `bars` 从 4 变 2 也说明**背景图不再往两侧画**（那里已经没有黑边可填）。

**教训**：

- **"渲染对了"不等于"显示对了"。** 有独立合成/裁剪层的管线里，渲染器的输出还要经过一道裁剪才能上屏；只验证前者会得出"功能正常"的错误结论。
- **一个选项的失效可能藏在另一层。** 排查时先确认"哪一层决定了可见范围" —— 本例是 `PictureRect()`（同时供 scissor 与背景图几何使用），不是 3D 渲染器。
- **移植时，"分支"也要一起移植。** OpenGL 的 scissor 有 WideScreen 分支，Vulkan 重写时那个分支没被带过来。重构里最容易丢的不是算法，是**条件分支**。

**修好之后暴露出的第二个问题：背景图与宽屏互斥**

宽屏一旦真的生效，就出现了新的组合问题：**背景图没有黑边可填了**。

背景图的存在意义是填充 4:3 画面两侧的黑边；宽屏让画面占满全宽，两边没有黑边，于是背景图**只会盖到游戏画面上**。用户直接提出了这一点（"打勾宽屏，之前的 bezel 背景图就不需要显示了，免得干扰"）。

处理方式是在**加载阶段**就跳过，而不是画的时候再判断：

```cpp
// CVulkan2D::LoadBezelImage()
if (m_wide)
{
    InfoLog("Bezel: not loaded - the widescreen option is on, so there are no side bars to fill.");
    return false;
}
```

放在加载处有两点好处：**省掉一次 PNG 解码和几十 MB 显存**（那张内置图是 2652×1492），而且**日志直接说明了原因**，而不是让人从"算出来的矩形是空的"去推断。

界面上同步处理：宽屏开启时**把背景图控件整组禁用**（`BeginDisabled`）并给出说明，而不是把它们藏起来 —— 让这个互斥关系**可见且可解释**，避免出现"一个设置悄悄吃掉另一个设置"。

**验证**（配置里 `BezelMode=1` 且 `WideScreen=1`）：

```
[Info] Bezel: not loaded - the widescreen option is on, so there are no side bars to fill.
[Info] Vulkan: viewport 495.75x384.00 at (0.00,0.00) - widescreen ACTIVE
[Info] Bezel: enabled=0 uploaded=0 extent=1920x1080 game=(0,4 1920x1072) bars=2
```

`enabled=0 uploaded=0` 确认背景图**既没上传也没绘制**，而宽屏正常生效（画面矩形全宽、只剩上下各 4px 的黑边）。

---

## 二十四、版本号、构建脚本的数据保护、以及一个"检查了但没检查对"的审计

### 24.1 版本号：单一来源，编译与构建脚本共用

界面显示的名字后面加版本号（`Supermodel-VK v1.0.3a-20261013`），用于识别具体构建。

关键是**不要让版本号出现两处**。`Src/Version.h` 里新增：

```c
#define SUPERMODEL_FORK_VERSION	"1.0.3a"
#define SUPERMODEL_FORK_DATE	"20261013"
#define SUPERMODEL_TITLE_FULL	SUPERMODEL_TITLE " v" SUPERMODEL_FORK_VERSION "-" SUPERMODEL_FORK_DATE
```

与上游的 `SUPERMODEL_VERSION`（`"0.3a-WIP"`）**刻意分开** —— 那个被写进存档并在读取时校验，改动会让用户已有的存档全部失效。

`build.bat` 通过 `Scripts/VersionString.ps1` **读取** `Version.h` 得到版本串，而不是自己再写一份。理由就是案例 17 的教训：同一个数字写两遍，迟早不一致。

> 顺带修了一个**静默取错值**的 bug：版本脚本最初用 `SUPERMODEL_FORK_VERSION\s+"([^"]+)"` 匹配，而 `Version.h` 里还有一行拼接宏
> `#define SUPERMODEL_TITLE_FULL SUPERMODEL_TITLE " v" SUPERMODEL_FORK_VERSION "-" SUPERMODEL_FORK_DATE`
> ——它也含这个标识符，于是正则匹配到后面的 `"-"`，而**最后一次匹配覆盖了正确值**，版本号变成 `v--20261013`。加行首锚定（`^\s*#define\s+NAME\s+`）后正确。
> **教训：多个匹配取最后一个，必须确认"最后一个"确实是想要的那个。**

### 24.2 build.bat 不再删除用户数据

原来的清空是 `rmdir /s /q "%DIST%"`，理由是"不让旧布局的残留文件留下" —— 但它把**用户的存档、NVRAM、截图和设置文件**一起删了。这让发布目录无法当作"实际游玩的地方"使用：一次重新构建就会静默丢弃一个可能是唯一记录的存档。

**改法是把用户数据搬开再搬回**，而不是改成"选择性删除"：

```bat
for %%D in (Saves NVRAM Screenshots) do (
    if exist "%DIST%\Data\%%D" (
        xcopy /e /i /q /y "%DIST%\Data\%%D" "%KEEPDIR%\%%D\" >nul 2>nul
        set "HAVEKEEP=1"
    )
)
```

**为什么不做选择性删除**：那需要枚举"哪些是程序文件"，而每次新增一类输出或用户数据都要重新审视这份清单；**写错的后果是静默且不可逆地删掉用户的东西**。搬移四个已知的用户数据目录，最坏结果只是"某个陈旧文件活过一次构建" —— 方向上是安全的。

**验证**：构建前放入存档/NVRAM/截图/ini，构建后**存档 SHA256 完全不变**，四类数据全部保留，且日志明确说明 `Kept : save states, NVRAM, screenshots and Supermodel.ini from the previous build`。

### 24.3 审计工具的盲区：它检查了"翻译存在"，没检查"翻译被调用"

用户报告：**切换到英文后，游戏列表的"搜索游戏"仍是中文**。

而审计一直报 `Missing from the table : 0`。**两个结论都是对的**，因为审计问的是错的问题：

| 审计问的 | 实际需要问的 |
|---|---|
| 这个中文串**在表里有英文条目吗**？ | 这个中文串**被交给 `Tr()` 了吗**？ |

「搜索游戏」**条目一直存在**（表里第 318 行），但代码是

```cpp
ImGui::InputTextWithHint("##filter", "搜索游戏", filter, sizeof(filter));   // 裸字面量，没调 Tr()
```

—— 表里有条目，**代码从不查表**，所以永远显示中文。审计的答案"条目齐全"是对的，但对用户毫无意义。

**顺带发现同一类的第二处**：`ImGui::InputText("对方地址##AddressOut", ...)` —— 同样表里有条目、代码没调 `Tr()`。

**修法分两层**：

1. **修代码**：两处补上 `Tr()`。
2. **补审计**：新增第二项检查 —— "每个中文字面量是否真的交给了会翻译它的东西"。

第 2 点是关键。实现上走了三步弯路，都值得记：

- **第一版手工维护"会翻译的包装函数"列表**：结果既漏报（列了 121 条无效项，因为 `UiColumn`/`DeviceCombo`/`UiInputInt`/`UiTabItem` 等没列全），**又可能掩盖问题** —— 我把 `BoolSetting` 猜进了列表，而它的翻译发生在下一层的 `UiCheckbox` 里。**一份方向偏"宽松"的清单，会藏住它本该抓的 bug。**
- **第二版改为自动推导**：解析每个函数体，含 `Tr()` 的算"会翻译"，再迭代传播"调用了会翻译的函数"。自维护、不会过期。
- **但第一版解析有 bug**：要求 `{` 独占一行，于是**所有单行定义的辅助函数**（`UiText`、`UiButton`、`UiSep`…）全被漏掉，误报涨到 68 条。改为允许 `{` 在同行或下一行后，降到 **0**。

**判定力验证**（这一步不能省）：把 `Tr()` 去掉重新注入原 bug，审计精确定位：

```
Not passed through Tr()             : 1
line 2584  via InputTextWithHint    搜索游戏
```

**教训**：

- **"检查通过"要先确认检查的是那件事。** 这个审计名字叫"翻译审计"，一直通过，而它验证的命题与"英文模式下会不会漏中文"不是同一个。**一个答案正确但问题问错的检查，比没有检查更危险** —— 它提供的是虚假的安心。
- **手工清单是负债。** 它需要随代码演进维护，而写漏的后果是"检查悄悄失效"。能推导的就别列举。
- **解析型工具同样要有判定力测试。** 自动推导的版本一开始误报 68 条、之前误报 121 条，只有实测才能发现"它其实没在工作"。

---

## 二十五、跟进上游 0.3a-20261013（git 7da4f9a）：只有一处值得移植

上游从 `0.3a-2026.09.28` 到 `0.3a-20261013-git-7da4f9a` 的改动**很小**。定位方法：把两棵树的 `Src` 逐文件比对，但只在**我们从未改动过的文件**上采信差异（这些文件的 mtime 仍是基准发布时间 `2026-09-28 14:05`，因此内容 == 我们的基准）。这样能自动排除我们自己那几百个改动，剩下的就是上游的。

结果：**220 个未改动文件里只有 5 个与上游不同。**

### 25.1 值得移植的：OpenGL 要求从 4.5 降到 4.1

`Graphics/New3D/R3DShaderQuads.h` 是唯一有实质意义的改动：

| | 上游旧版（= 我们的基准） | 上游新版 |
|---|---|---|
| 着色器版本 | `#version 450 core` ×3 | **`#version 410 core`** ×3 |
| `cross` 数组 | `precise float cross[4][4]` | **`precise float cross[16]`** |
| 索引 | `cross[i][j]` | `cross[i*4+j]` |
| helper invocation | `if(!gl_HelperInvocation) { discard; }` | `discard;` + 注释说明为何安全 |

**为什么这是重要的**：数组的数组（`float[4][4]`）需要 **GLSL 4.30**，而 `#version 450` 需要 OpenGL 4.5 上下文。上游把两者一起降下来，使 **New3D 只需 OpenGL 4.1**。

配套地，上游还把 GL 上下文请求统一为 4.1，并**删掉了 `CreateGLScreen()` 的 `quadRendering` 参数** —— 那个参数存在的唯一目的就是在 4.5 和 4.1 之间选择，既然着色器不再需要 4.30 特性，就没有可选的余地了。这两处一起移植。

**这对我们尤其关键，因为今天刚加了"Vulkan 失败自动回退 OpenGL"**：回退要服务的正是那些**提供不了 Vulkan 1.3 的老机器**，而它们往往同时是「支持 GL 4.1 但不支持 4.5」的（Intel 核显、较老的 AMD、macOS 上限就是 4.1）。带 `#version 450` 的着色器在 4.1 上下文里无法编译，于是**回退会在最需要它的机器上失败**。

**为什么我们之前没发现**：NVIDIA 驱动对 GLSL 版本**很宽容**，在 4.1 上下文里也接受 `#version 450`；AMD 与 Intel 则严格按版本拒绝。我们的测试机是 NVIDIA，所以这个缺陷**在本地是隐性的**，换台机器就会暴露。

**验证**（强制走 OpenGL）：

```
OpenGL Version: 4.1.0 NVIDIA 591.86
Shading Language Version : 4.10 NVIDIA via Cg compiler
```

移植前同样的配置会拿到 4.6（因为那时 `New3DEngine=0` 走的是 Legacy3D，不请求核心 profile）；移植后请求 4.1 就**精确得到 4.1**，而 `#version 410` 着色器在其中正常编译、游戏正常运行。**这同时让 README 里"OpenGL 4.1"的说法从"口号"变成事实。**

### 25.2 顺带移植的（语义等价）

| 文件 | 改动 |
|---|---|
| `Model3/RTC72421.cpp` | `time_t oldTime{0}` → `oldTime{}` |
| `OSD/SDL/Crosshair.h` | `GLuint m_crosshairTexId[2] = { 0 }` → `= {}` |
| `Util/ConfigBuilders.cpp` | `q.push({ e, config })` → `q.emplace(e, config)`（×2）|
| `Docs/README.txt` | 上游手册更新：力反馈拆成 Left/Right、PowerPC 频率默认值说明更正 |

`Docs/README.txt` 的移植**修掉了我们随包发布手册里的一处错误**：它写着 `PowerPC frequency ... The default is 50`，而**我们代码里的默认值其实是 0**（跟随游戏硬件 66/100/166 MHz）。上游新版改对了这句话。

### 25.3 刻意不移植的：`Games.xml` 里那一个 CRC

上游**悄悄改了一个 ROM 的 CRC32，没有任何说明**：

```
集合 vs298（Virtua Striker 2 '98）
  <file offset="6" name="epr-20917.17" crc32="0xC3BBB270" />   ← 我们的（= 上游旧版）
  <file offset="6" name="epr-20917.17" crc32="0x969E4BDA" />   ← 上游新版
```

**实测用户手上那套 ROM 的值**：

```
vs298.zip 里 epr-20917.17 = 0xC3BBB270   ← 匹配旧值，不匹配新值
（同区域其余三颗 epr-20918/20919/20920 两边一致）
```

**如果把这条改动搬过来，`vs298` 会立刻变成"无法加载"** —— 模拟器会报 `epr-20917.17 not found`，正是 `magtruck` 那次的情形。旧 CRC 在真实 dump 里确实存在（否则用户不可能有一个匹配它的文件），所以上游大概是**把该集合换成了另一个 dump**（重 dump），而不是修正笔误。

**决定：不移植，交由用户判断。** 若将来拿到了重 dump 版本，再更新；或者像上游处理 `mgtrkbad` 那样**新增一个集合条目**来同时支持两种 dump，而不是替换。

**这条也说明"上游更新"不等于"应当照搬"**：同一个文件里既有该移植的（着色器兼容性）、也有不该移植的（ROM 定义），而**区分它们只能靠实测**。

### 25.4 上游其余差异（未动）

- `Graphics/New3D/New3D.cpp`：只多了一段注释（说明 quad 着色器第二个三角形的已知限制），无行为变化。
- `QuadRendering` 默认值：上游 `true`，我们 `false`。这是**行为性差异**（4 视口渲染模式），可能是有意为之，未改动。
- 配置键名：上游叫 `ROMDirectory`（默认 `'ROMs'`），我们叫 `ROMPath`（默认 `C:/Downloads`）。移植手册时**已把该条改回我们的键名与默认值**，并注明上游的叫法，避免手册文档化一个我们不接受的键。

---

## 二十六、宽屏为何因游戏而异，以及网络设置归位

### 26.1 宽屏的适用范围由游戏的"视口"决定

用户观察：**有些游戏能宽屏、有些不行**（如格斗游戏）；**有些游戏在过场动画时缩回 4:3、回到操作又变宽屏**。

答案在宽屏分支的条件里：

```cpp
if (m_wideScreen && vp->vpX == 0 && vp->vpWidth >= 495 && vp->vpY == 0 && vp->vpHeight >= 383)
```

**它要求游戏为当前画面设定的视口就是整个 496×384 屏幕。** 这个视口由游戏写在 Real3D 的 culling RAM 里，每帧（甚至每场景）都可能不同 —— 所以**是否宽屏完全由游戏决定**。

**用第 23.8 节留下的视口诊断实测**：

| 游戏 | 游戏设定的视口 | 宽屏分支 |
|---|---|---|
| fvipers2（格斗）| `495.75×384 @ (0,0)` | **ACTIVE** |
| vf3（格斗）| `496×384` → `495.75×384` | **ACTIVE** |
| srally2 | `495.75×384` | **ACTIVE** |

**格斗游戏是符合条件的** —— 视口就是全屏。所以"格斗游戏不行"如果确实观察到，原因不在这个分支，更可能是：

- **游戏自己的场景本来就没铺到那么宽**（竞技场按 4:3 设计，扩出来的两侧没有内容）；
- 或者那两侧虽然被渲染出来了，但**2D 图层盖不住那么宽**，看起来像黑块（见 26.3）。

> **勘误（原第 26.1 节曾写"开了整数缩放时 `correction = 1.0`，没有可补的宽度"）**：
> 这句话是错的，而且是反的。`correction = (totalXRes/totalYRes) / (xRes/yRes)`，与整数缩放无关地
> 恒等于 `totalXRes / xRes`（因为 `yRes == totalYRes`），所以它**永远大于 1**，整数缩放下更是
> **最大**的那个。实测 4K 面板 + 5 倍整数缩放 + 宽屏：`totalXRes = 3840`、`xRes = 2480`，
> `correction = 1.5484`。整数缩放恰恰是这个因子被算错、3D 层被多拉宽 12.5% 的那个组合 ——
> 详见 26.3。

**过场缩回 4:3 则完全吻合这个条件**：过场动画通常用**带信箱黑边的较小视口**（高度不足 384），于是 `vpHeight >= 383` 不成立 → 走 else 分支保持 4:3；回到操作时视口恢复全屏 → 又变宽屏。**这是游戏机制，不是缺陷。**

要确认的话：在过场时抓一份日志，诊断行会直接打印当时的视口尺寸。

**是否该把宽屏扩展到非全屏视口？** 建议**不扩展**：

- 过场的视口是为某个特定画幅**专门取景**的，横向拉开只会露出设计者刻意框掉的内容；
- New3D 上游用的是**同一个条件** —— 这不是我们引入的行为；
- 每个视口都要算自己的画幅比例，很容易把过场拉变形。

**顺带一个脆弱点值得记录**：判据是 `vpWidth >= 495`，而实测值是 **495.75 / 496.00** —— **余量只有 0.75 像素**。若某个游戏报 494.x 就会被静默跳过，表现为"这个游戏不支持宽屏"。如果要放宽，可考虑把阈值降到 490 左右；但这属于猜测性改动，暂未动。

### 26.2 宽屏 + 整数缩放：视锥按窗口算，画面按可见区画

**用户观察**：`WideScreen = 1` 与 `IntegerScale = 1` 同时开时，画面被横向拉宽约 12.5%。

**根因**：`correction` 用的是**窗口**的画幅比，而 3D 层实际被画进的矩形是**可见区**。

两处代码在同一分支里同时改写了视口尺寸与视锥宽度，但两者依据的矩形不同：

```cpp
float correction = ((float)m_totalXRes / (float)m_totalYRes) / ((float)m_xRes / (float)m_yRes);
//                                                            ^^^^^^^^^^^^^^^ 窗口的高
vp->width  = (int)m_totalXRes;
vp->height = (int)(vp->vpHeight * m_yRatio);   // ← 实际画出来的高度
```

窗口是 `totalXRes × totalYRes`，画出来的矩形是 `totalXRes × vpHeight × m_yRatio`。**两者只有在画面铺满窗口时才相等** —— 而整数缩放的全部意义就是**不铺满**（留黑边取整）。

于是一个满屏视口（`vpHeight = 384`）在 4K 面板 5 倍下：

| 量 | 值 |
|---|---|
| 窗口高 `m_totalYRes` | 2160 |
| 画出来的高 `384 × 5` | 1920 |
| 修复前 `correction` | 3840/2160 ÷ (2480/1920) = **1.3763** |
| 修复后 `correction` | 3840/1920 ÷ (2480/1920) = **1.5484** |
| 比值 | 1.5484 / 1.3763 = **1.125** |

也就是视锥只被拉宽了应有的 88.9%，场景于是被横向放大 12.5%。2D 图层（HUD、文字）不经过这个矩阵，仍按正确比例合成，**两层就不一致了** —— 这是"整幅画面拉宽"而不是"背景被裁"的原因。

**修复**：把分母从窗口高改成输出矩形自己的高。

```cpp
const float outHeight = (vp->vpHeight > 0.0f && m_yRatio > 0.0f)
    ? vp->vpHeight * m_yRatio
    : (float)m_totalYRes;
const float correction = ((float)m_totalXRes / outHeight) / ((float)m_xRes / (float)m_yRes);
```

**为什么这是可证明的空操作（除整数缩放外）**：`m_yRatio = yRes/384`，满屏视口 `vpHeight = 384`，所以 `vpHeight × m_yRatio == m_yRes`，表达式退化为 `m_totalXRes / m_xRes` —— 与旧式在 `m_yRes == m_totalYRes`（即所有非整数缩放场景）时**逐位相同**。改动只影响整数缩放这一个本该错的组合。

**两处都改了**：

- [`Graphics/Vulkan3D/R3DSceneBuilder.cpp`](../Src/Graphics/Vulkan3D/R3DSceneBuilder.cpp:750) —— Vulkan 3D；
- [`Graphics/New3D/New3D.cpp`](../Src/Graphics/New3D/New3D.cpp:1627) —— OpenGL 的 New 3D，逻辑与上游逐字相同，**同样有此问题**（不是我们引入的）。

### 26.3 整数缩放倍率在界面上不可见

用户问："5× 才是真正的 4K？跟我设的 3840×2160 @150% DPI 有关吗？是不是要 100% DPI 才用 6×？"

**三个问题都是同一个误解的三个面**：倍率从来不在设置里，它是算出来的 ——

```
multiple = trunc(min(panelWidth/496, panelHeight/384))
```

**一、与 DPI 无关，而且必须无关**

[`ReadPanelMode`](../Src/OSD/SDL/Main.cpp:863) 走 `EnumDisplaySettingsW(ENUM_CURRENT_SETTINGS)` 读 `dmPelsWidth/dmPelsHeight`，这条 API **不受 DPI 虚拟化影响**。这正是 24 行注释（[`Main.cpp:484`](../Src/OSD/SDL/Main.cpp:484)）刻意设计的结果：在混合缩放的多屏机器上，不是系统 DPI 的那块屏会被 Windows 换算到系统 DPI 的空间（1080p 被报成 2880×1620），**从虚拟化高度算倍数就等于把"点对点"这个承诺直接作废**。日志可以佐证：

```
Display 5: Odyssey G70D  3840x2160 @120 Hz  [primary]
Display 6: S24F350  1920x1080 @60 Hz  (Windows space 2880x1620)
```

主屏没有括号后缀 = 它就是系统 DPI 的定义者，因此如实上报。**150% 根本没有进入这个计算。**

**二、2160 行的面板上 6× 是算术不可能的**

`384 × 6 = 2304 > 2160`。各面板的上限：

| 面板高度 | 最大倍数 | 可见区 |
|---|---|---|
| 1080 | 2× | 992 × 768 |
| 1440 | 3× | 1488 × 1152 |
| 2160（4K）| **5×** | 2480 × 1920 |
| 2400（3840×2400）| 6× | 2976 × 2304 |
| 2880（5K）| 7× | 3472 × 2688 |

**三、"6× = 4K"这个印象是界面给的**

[`Gui.cpp:1815`](../Src/OSD/SDL/Gui.cpp:1815) 原来把这个预设标成 `6x 分辨率 (2976 x 2304 - 4K级)`，而 2976×2304 **两个方向都不是** 3840×2160 —— 它只是像素总数接近 4K。

修的时候走了两步，第二步才是对的：

1. 先改成 `需 2304 行面板` —— 事实正确，但**放错了载体**。下拉框被扫视时只回答"该选哪个"，门槛属于解释，应该在 `(?)` 提示里。
2. 最终：去掉所有不可达档位的后缀，把标记只留给两种真实面板能落到的档位 —— `3x 分辨率 (1488 x 1152 - 2K 屏推荐)` 与 `5x 分辨率 (2480 x 1920 - 4K 屏推荐)`。行数门槛写进 `(?)`，实时倍率由下面的读数给出。

顺带纠正了同一张表里更严重的一处：`4x - 2K级`。2K 屏只有 1440 行，而 4x 需要 1536 行，**2K 屏根本进不了 4x** —— 这不是措辞含糊，是事实错误。

更麻烦的是：`XResolution/YResolution` 与 `IntegerScale` 是**两个互相覆盖的旋钮**。整数缩放开启时 [`Main.cpp:558`](../Src/OSD/SDL/Main.cpp:558) 直接把它覆盖成 `496n × 384n`，用户选了 6× 预设却拿到 5×，而界面上没有任何地方说这件事。

**修复**：新增 [`GetIntegerScaleInfo()`](../Src/OSD/SDL/Main.cpp:1134)，用与 `CalcScreenGeometry` **完全相同**的取面板逻辑（含 fallback 顺序）把实际倍率、可见区、面板尺寸、下一档所需行数算出来，在「常用」页与「画面」页各显示一行读数，并在与请求分辨率不一致时明说"会被覆盖为"。

**为什么不加 DPI 判断**：加进去只会**制造**这个 bug —— 见前面 1080p 被报成 2880×1620 的例子。DPI 在本程序里唯一的合法用途是 ImGui 的字体与控件缩放（[`Gui.cpp:3685`](../Src/OSD/SDL/Gui.cpp:3685)）。

### 26.4 顺带记录的次级缺陷（未修）

当 `XResolution × YResolution` 超过面板高度时（例：2976×2304 在 2160 行的屏上），[`Main.cpp:606`](../Src/OSD/SDL/Main.cpp:606) 的收尾钳制会把宽度也一起砍到窗口宽度，**破坏画幅而不是等比缩小**。仅在非整数缩放 + 全屏 + 分辨率高于面板时出现。属于配置错误而非代码错误，暂按文档记录。

### 26.5 网络设置从「常用」移到「网络」

用户指出：网络相关设置在「常用」页最底部，应该放进「网络」页。

**根因是两个机制叠加**：

1. 这些键（`Network`/`SimulateNet`/`PortIn`/`PortOut`/`AddressOut`）列在 `HasDedicatedUI` 里 —— 意思是"由手写代码绘制，通用渲染器请跳过"；
2. 而手写绘制的地方是 `DrawQuickSettings`（「常用」页），不是「网络」页。

于是「网络」页里的 `CreateControls(config, "Network")` **把这些键全部跳过**，只剩下三个不在白名单里的调试键（`OutputsWithLF`/`DumpMemory`/`DumpTextures`）—— **「网络」页因此几乎是空的**。

**改法**：把那段手写代码抽成 `DrawNetworkSettings(config)`，从「网络」页调用。

**一个必须注意的顺序问题**：`UpdateTempValues(group, true/false)` 是**快照/回写**机制，回写会恢复快照时的值。所以对同组键的编辑**不能夹在快照与回写之间**，否则会被静默回滚（「画面」页有两处专门的注释警告这个坑）。因此调用顺序是：

```cpp
DrawNetworkSettings(config);                  // 先，且在快照之前
UiSep("调试输出");
UpdateTempValues(config, "Network", true);    // 快照 —— 此时已包含上面的改动
CreateControls(config, "Network");            // 只剩三个调试键
UpdateTempValues(config, "Network", false);   // 回写
```

**验证**：`DrawQuickSettings` 的函数体内已不存在 `Network`/`PortIn`/`SimulateNet`（grep 确认），`DrawNetworkSettings` 定义于 1792 行、由「网络」页在 2733 行调用；构建通过，两项翻译审计均为 0。

---

## 二十七、让界面说实话：选项与渲染器的归属关系

这一节记录三件相关的事，共同的起因是**界面没有告诉用户"这个选项在当前渲染器下有没有用"**。

### 27.1 两套抗锯齿并排放着，看起来像一个功能的两半

「画面」页上有 **Supersampling**（OpenGL 的抗锯齿），「高级设置」页有 **MSAA**（Vulkan 的）。两者都是"抗锯齿"，用户完全可以都开、期待叠加效果 —— 而实际上**各自后端都会静默忽略对方的那个**（Vulkan 那条只写在日志里：`supersampling is not available in this backend; using 1x`）。

排查方法是**逐键 grep 判断"这个键被哪个渲染器读取"**，而不是凭印象：

| 归属 | 选项 | 依据 |
|---|---|---|
| **仅 Vulkan** | `MSAA`、全部 `Post*`（FXAA/CRT/扫描线/点阵/辉光/增益）、`BezelMode`/`BezelImage`/`BezelBrightness` | 只出现在 `Graphics/Vulkan3D` |
| **仅 OpenGL** | `Supersampling`、`CRTcolors`、`Crosshairs`/`CrosshairStyle`、`UpscaleMode` | `Supersampling` 被 `Main.cpp` 在 Vulkan 下强制为 1x；`CRTcolors` 由 `SuperAA` 消费；准星只在没有 Vulkan 上下文时创建；`UpscaleMode` 传给两个渲染器但 **Vulkan 的 `Init()` 参数未具名 = 忽略** |
| **仅 New3D** | `QuadRendering` | 只被 `New3D`/`R3DShader` 读取 |
| **仅 Legacy3D** | `MultiTexture`、`VertexShader`、`FragmentShader` | 只被 `Legacy3D` 读取 |
| **共有（保持可用）** | `IntegerScale`、`NoWhiteFlash`、`WideScreen`、`WideBackground`、以及显示/音频/输入/网络页全部 | 在两端共用的代码里生效 |

**实现**：新增 `ScopeOfSetting()` / `ScopeAppliesToCurrentRenderer()`，在通用渲染器 `CreateControls()` 里统一加 `BeginDisabled` 并在旁边写明原因；「高级设置」页**整体灰化**，只有 `IntegerScale` 单独排除（它是该页唯一共有的项，且**在低配机器上恰恰最想要** —— 而那正是用 OpenGL 的场景）。

**为什么灰化而不是隐藏**：隐藏会让界面**随渲染器改变形状**，令人困惑，也让人不知道这个选项存在。灰化 + 一行原因一次说清两件事。

**验证的两个要点**：
- `BeginDisabled` / `EndDisabled` **8 / 8 配平**，且每对都在同一函数内 —— 不配平会导致后续控件一直被灰；
- **分类表逐键对照源码引用位置，全部一致**。这一项最关键：分类错了会把该可用的选项灰掉，比不灰更糟。

### 27.2 游戏内叠加层只有 Vulkan 能用，而且是结构性的

用户问 `Tab` 调出的面板是否也只在 Vulkan 下可用。**是的，而且不是"顺便不支持"**：

```cpp
if (OverlayInit(*s_vkContext, s_window, overlayScale))            // 需要 Vulkan 上下文
    static_cast<Vulkan3D::CVulkan2D*>(Render2D)->SetOverlayPass(&OverlayRender);
...
if (Inputs->uiToggleOverlay->Pressed())
    if (s_vkContext)                                              // 没有就什么都不做
        OverlayToggle(s_runtime_config);
```

叠加层**画进游戏自己的 swapchain**（有独立的 ImGui 上下文 + `imgui_impl_vulkan` 后端），而 **OpenGL 后端不向外暴露 swapchain** —— 所以没有地方可以画。

**问题在于"没有反应"是静默的**：OpenGL 下按 `Tab` 什么都不发生，用户会认为快捷键坏了，而不是"这个功能属于另一个渲染器"。`README.txt` 里写了这条，**但按 Tab 的时候没人会去翻 README**。

**修法**：在**渲染器单选框下方**（做出该决定的那个位置、那一刻）加一行说明：

> 游戏内按 Tab 的设置叠加层需要 Vulkan 3D，OpenGL 下按 Tab 没有反应。

### 27.3 顺带更正："需要重启"是错的

界面上原来写着「这些选项需要**重启模拟器**才会生效」。**这句话把操作说重了**，会让用户关掉程序再打开。

实际流程由 `main()` 的循环决定：

```cpp
for (;;) {
    cmd_line.rom_files = GuiRunOnce(gui);   // 从设置窗口取要启动的游戏
    Util::Config::FromINIFile(...);         // 重新读配置
    exitCode = RunGameSession(...);         // ← 渲染器在这里创建
}
```

`RunGameSession()` 结尾调用 `DestroyGLScreen()`，**把 Vulkan 上下文、窗口、渲染器全部释放** —— 所以**每启动一局就等于整个渲染器重建一次**，连 `MSAA` 也是（它在 `CVulkan3D` 构造函数里读取，而该对象每局都 `new`）。

**正确操作是三步，不用退出程序**：`Esc` 回到设置窗口 → 改设置 → 再启动一个游戏。而且设置**自动保存**（`saveSettings` 默认 `true`，启动游戏前写盘），不需要按保存按钮。

**文案已改为**：「这些选项需要重新开一局才会生效（不用退出程序：按 Esc 回到本窗口再启动即可）。」背景图那条和 `README.txt` 的「已知限制」一并更正。

### 27.4 设置生效的三种时机

厘清后是三类，**没有一类需要退出程序**：

| 时机 | 哪些设置 |
|---|---|
| **立即生效**（每帧读取）| CRT 滤镜（扫描线/点阵/强度/亮度补偿）、辉光、FXAA —— **这正是游戏内叠加层存在的意义** |
| **重新开一局** | 渲染器选择、MSAA、超采样、分辨率、全屏、宽屏、宽屏背景、背景图、整数倍缩放、2D 放大、CRT 色彩 |
| ~~退出程序~~ | **无** |

### 27.5 一个自证的插曲

改完这些中文文案后构建，审计立刻报 `Missing from the table: 2` —— **正是 24.3 节补的那项检查在起作用**（改了文案却忘了同步翻译条目）。这算是那项检查的第一次实战捕获。

---

## 二十八、把提示词从"论文"改成"一句话"，详解移进 README

### 28.1 问题：弹窗里的段落没人读

用户指着「独占全屏 · 固定街机刷新率」那条提示说：**太长，怕用户理解不了**，希望用讨论区常见的说法。实测这一条是 **392 字符**，而最长的（CRT 推荐配置）达到 **721 字符** —— 在鼠标悬停弹出的小窗里，那是一整屏文字。

统计下来（21 条提示）：**12 条超过 200 字符，平均约 190**。

**根本原因**：这些提示是在"写文档"的心态下写的 —— 把原理、边界情况、参数推导、调参建议全塞进去。但**提示词的读者是"鼠标停在控件上的人"**，他想知道的是"这是什么、我该不该开"，而不是为什么。

### 28.2 改法：提示词一句话，详解进 README

- **提示词**：保留 1–2 句，说明"是什么 + 什么时候用"，用常见说法；
- **完整解释**：移进 `Docs/README-VK.txt` 新增的 **SETTINGS IN DETAIL** 章节（21 个小节），需要深入的人去查。

改写后：**最长 125 字符（原 721）、平均 68（原 190）**。

举例 —— 用户点名的那条：

| | 内容 |
|---|---|
| **原**（392 字符）| 被显示器锁定到 57.524Hz 扫描，与 Model 3 原始帧率 1:1 对应。需要在 NVIDIA 控制面板创建一个 57.524Hz（或 57/58Hz）的自定义分辨率。找不到这个模式时会自动降级到 G-SYNC/VRR 的独占全屏。\n\n启用后「画面」页的「刷新率」会显示 57.5 Hz 且置灰。用 Scripts\ArcadeDisplay.ps1 -Action List 查看当前是否被支持，-Action Run 自动切换并在退出后恢复原模式。 |
| **新**（63 字符）| 把显示器锁到真机的 57.5Hz，画面最顺滑。\n少数显示器或驱动不支持，遇到黑屏就换回上面的选项。 |

**关键取舍**：原提示里那句 `Scripts\ArcadeDisplay.ps1 -Action List` 是**给开发者看的**，普通用户既不会用也不需要 —— 它属于 README 而非弹窗。这类内容正是"精简"要剔除的对象。

### 28.3 实施过程中的三个技术点

改 21 条多行 C++ 字符串字面量，靠手工替换容易出错，所以写成脚本，并且**每一步都先验证再做**：

**① 用"顺序 + 指纹"定位，而不是硬编码长度。**
第一版脚本按**总长度**校验身份，结果第一条就报错：我之前的长度测量包含了尾部空白与调用的额外实参，与脚本的测量方法不同，于是每个期望值都是错的。**改成按顺序定位 + 打印前几十个字确认**，人眼核对无误再执行 —— 顺序和内容才说明"这是哪一条"，长度只说明"它多大"。

**② PowerShell 5.1 读 `.ps1` 需要 BOM。**
脚本里的中文没有 BOM 时，PowerShell 按系统 ANSI 码页解析，某些中文字节序列**恰好产生引号或截断**，于是报 `Unexpected token` / `The string is missing the terminator` —— 看起来像语法错误，实际是编码问题。**给脚本加 UTF-8 BOM 后正常**（与 23.6 节 `/utf-8` 是同一类坑，只是发生在 PowerShell 而非编译器上）。

**③ 本地化表里的 key 必须写成单行。**
翻译审计**把每行的第一个引号块当作 key**。我第一版把中文 key 按行拆成多个引号块（`"第一行\n"` `"第二行"`），审计只读到第一块 → **19 条全部报"缺失"**，尽管运行时 `Tr()` 看到的字符串完全正确。改成**一行内用 `\n` 转义**（这也是文件原有的约定）后归零。

**这个细节很隐蔽**：C++ 层面两种写法等价，但审计工具的解析方式决定了只有一种能通过。

### 28.4 顺带修掉的格式问题

脚本生成的续行缩进不对（在 8 空格处），虽然合法但难看。写了个对齐脚本，**只处理提示文本内部的续行** —— 全局"把 8 空格缩进改掉"会同时动到本来就这样缩进的数组元素。

### 28.5 一个正在起作用的检查

这次改动**又一次**被 24.3 节补的审计抓到（改了中文文案 → 立刻报 `Missing from the table: 19`）。**一项检查的价值，就在它反复拦下真实错误的过程中体现。**

### 28.6 提示词溢出面板：ImGui 的 Text 系列不换行

提示词缩短之后（28.2），用户发现它们**仍然溢出**：文字从设置面板右侧冲出去，盖到旁边的游戏列表上。

**根因**：`ImGui::TextUnformatted` / `TextDisabled` / `TextColored` **都只画一行，超出部分直接裁掉，不会自动换行**。这在两三个字的标签上没问题，但一句完整的话必然跑出边界。

而这几处文字全部经由四个自建 helper（`UiText` / `UiTextDim` / `UiTextWarn` / `UiTextColored`），所以**只改这四处就能覆盖整个界面**：

```cpp
static void PushHelpWrap()
{
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
}
static void PopHelpWrap() { ImGui::PopTextWrapPos(); }
```

**为什么用"内容区右边缘"而不是 `0.0f`**：`0.0f` 表示**窗口边缘**，而面板有内边距和滚动条，文字会贴着滚动条甚至被它压住。`GetCursorPosX() + GetContentRegionAvail().x` 才是"当前行可用的右边界"。

**顺带核查了"有没有绕过 helper 的裸调用"**（那类仍不会换行）：`Gui.cpp` 全部走 helper；`Overlay.cpp` 有一处裸 `TextDisabled`，已补上包裹并注明原因（该文件拿不到 `Gui.cpp` 里的 file-static helper）。

**验证**：构建通过、审计 0/0；截图确认「常用」页那条原本冲出去的说明**折成两行、完整收在面板内**。

> 补充一句教训：**这属于"UI 基元选错"而不是"某条文案太长"**。我上一轮缩短了文案，问题看起来缓解了，但根因没动 —— 只要以后有人写一句稍长的话，同样的溢出会再来一次。**先问"这个控件能不能容纳一句话"，再问"这句话是不是太长"。**

### 28.7 问号提示同样不换行，而且这次的注释写反了

28.6 改完**面板内文字**之后，用户再反馈：**问号（`(?)`）弹出的提示**依然很长、不换行，仍然盖到游戏列表上。

**根因**：`HelpMarker()` 用的是 **`ImGui::SetTooltip`**，而它与 `Text*` 系列一样**不换行** —— 它把文字排成一行、窗口按内容撑开，于是一句话就变成一个"和句子一样宽"的弹窗，自然越过设置窗口、盖住旁边的游戏列表。

**而我上一轮在 `UiTooltip` 上写的注释是错的**：

```cpp
// ImGui::SetTooltip is a separate window and wraps on its own, so it needs no wrap position here.
```

**事实相反**：`BeginTooltip` 把 wrap position 留在 `0`，而 `0` 的含义是"不换行"。**一句自以为是的注释，让这个缺陷多活了一轮。**

**修法**：抽出 `DrawWrappedTooltip()`，两条提示路径（`HelpMarker` 与 `UiTooltip`）都走它：

```cpp
ImGui::BeginTooltip();
ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);   // 约 30 字符宽
ImGui::TextUnformatted(text);
ImGui::PopTextWrapPos();
ImGui::EndTooltip();
```

宽度用**字号为单位**，这样跟随界面 DPI 缩放；并**与窗口宽度取小**，保证窄窗口下提示不会比窗口本身还宽。

**同时核对了全部 `SetTooltip` 调用点**：只有游戏列表那处**刻意保留单行** —— 它的用途是"窄窗口时悬停看被截断的整行文本"，本来就要一行显示；同处的另两条说明已改为换行。

**另一个叠加因素**：用户当时是**英文界面**，而那条提示的英文是 **158 字符的单行**（中文只有 2 短行）—— 英文每个字更宽，单行更夸张。已顺带压到约 123 字符并加了显式断行。

**教训**：

- **"我以为它会换行"不能替代查证。** 上一轮我在注释里断言 `SetTooltip` 会自动换行并据此跳过处理，是这次多绕一轮的直接原因。**写成注释的错误假设，比写在代码里的错误更难发现** —— 它看起来像是"已经确认过的事"。
- **同类缺陷要一次查全。** 28.6 我修了 `Text*` 系列就没再往下想；其实界面上"绘制文字"有**两类**路径（面板内联 + 悬浮提示），当时应该一起核对。

---

## 二十九、手柄检测的可发现性：按钮换行 + 状态高亮

用户提出两点，都是"信息该被看见"的问题：

1. 「重新检测」按钮**原本跟在检测信息同一行**（`SameLine()`），放在读者视线最末的位置，而且读起来像那句话的一部分，不像"属于它的操作"；
2. 「检测到：…」是**灰色说明文字**，和周围的建议段落**长得一样** —— 而"手柄到底认出来没有"恰恰是这一页最先要看的东西。

### 29.1 改动

- **按钮移到检测信息下方一行**（去掉那个 `SameLine()`），`(?)` 说明标记留在按钮右侧；
- **检测行改用高亮色**（琥珀 `1.0, 0.72, 0.30`，与 `UiTextWarn` 同一视觉色）；
- 「未检测到」那句也一并高亮 —— 它同样是"一眼要看出"的状态，而且措辞改为"点**下面的**「重新检测」"以匹配新位置。

### 29.2 新增 `UiTextHighlight` 而不是复用 `UiTextWarn`

两者**颜色相同**，但语义不同：一个是"出问题了"，一个是"请注意到这一行"。直接复用 `UiTextWarn` 会留下一个隐患 —— 将来若把警告色改成红色，**手柄检测行会跟着变红**，而那是正常的成功状态。

做法是把颜色**提成一个常量** `kAttentionColour`，两个函数共用它：

```cpp
static const ImVec4 kAttentionColour(1.0f, 0.72f, 0.30f, 1.0f);

static void UiTextWarn(...)      // 用 kAttentionColour —— 出问题了
static void UiTextHighlight(...) // 用 kAttentionColour —— 只是要看见
```

**颜色只有一处定义，名字各按语义** —— 既不会漂移，也不会让一个调用点被另一个的语义绑架。

### 29.3 验证

构建通过、翻译审计 **0 / 0**；截图确认：

```
检测到：AMD XInput Controller（Xbox One 手柄，建议输入系统：xinput）   ← 琥珀色
[重新检测] (?)                                                        ← 独立一行，在下方
```

> 这次也顺带确认了这段代码的位置：它在 **`DrawQuickSettings`** 里，即「常用」页的**「手柄」分节**，并非独立页签 —— 改之前先确认了归属，避免又出现 28 节那种"指错页签"的问题。

---

## 三十、实时统计面板：复用叠加层，但把"误导性数字"当成缺陷来修

用户想要一个 MSI Afterburner 式的画面内读数：渲染模式、CPU 频率/线程数/占用、GPU 频率/占用/显存、内存、实时 FPS 与 1% Low，用界面里那个琥珀色，按 `` ` ``（Tab 上方那个键）开关。

### 30.1 挂载点：复用 `Overlay` 的 ImGui 上下文，而不是新建一套

`Overlay.cpp` 已经为游戏内设置面板建立了一个**独立的 ImGui 上下文 + Vulkan 后端**，并且已经解决了三个不显然的问题：

1. 设置窗口的上下文必须**让位**（`g_previousContext` 的来回切换）；
2. SDL 的键盘/鼠标状态在游戏运行时**是过期的**（`CDirectInputSystem` 自己泵消息），所以只能用 `GetAsyncKeyState` 手工喂输入；
3. ImGui 的 `NewFrame`/`Render` 必须成对，否则最小化窗口会让 ImGui 断言。

新建一套会把这三点重做一遍。所以统计面板做成 `StatsOsd.cpp`，**共用同一个上下文**，由 `OverlayNewFrame()` 统一驱动。

这个决定带来一处必须改的分支：原来 `OverlayNewFrame()` 在面板关闭时直接 `return false`。而两个窗口共用**同一个 ImGui 帧**——统计面板打开、设置面板关闭时，如果函数提前返回，就没有 `NewFrame` 可供统计窗口挂靠：

```cpp
    const bool panel = OverlayVisible();
    const bool stats = g_backendReady && StatsOsdEnabled();
    if (!panel && !stats)
        return false;          // 只有两者都关才跳过整帧
```

面板本身则用 `if (panel) { ... }` 包起来。**帧是无条件的，面板是条件的。**

### 30.2 顺带解决：两个面板都锚在左上角时谁盖谁

统计面板要左上角（用户先要右上角，后改左上角），而设置面板的默认位置**也是左上角**。ImGui 的 `Begin` 按调用顺序决定叠放次序，所以把统计面板放在**设置面板之前**绘制：

```cpp
    StatsOsdBuildWindow(config);   // 先画 → 在下层
    if (panel) { ...设置面板... }   // 后画 → 在上层
```

理由：设置面板是**临时的、刻意打开的**工具，应该盖住一个**常驻**的读数。

### 30.3 核心问题：三个数字都是"真话但误导"

#### (0) `Sync:` 读配置而非实际进入的模式 —— 会**谎报** G-SYNC

初版从配置文件推 `Sync:` 标签。用户追问："如果有些用户的显示器或显卡不支持 G-SYNC/VRR，这些会显示成 60 吧？"

查下来这是**真缺陷**：`ResolveFullScreenMode()` **从不检查 VRR 能力**，`SDL_SetWindowDisplayMode` 成功就返回 `ExclusiveDesktop`。而 `ApplyFullScreenMode()` 里驱动拒绝时会**回退到 borderless 并更新 `s_fullScreenMode`**。所以：

| 实际发生 | 旧标签 | 问题 |
|---|---|---|
| 驱动拒绝独占 → 退回 borderless | `G-SYNC/VRR` | **谎报**：VRR 根本无法生效 |
| 配置要固定 57.524，但无该模式 → 回退 G-SYNC | `fixed arcade` | **谎报**：实际没用固定模式 |

实测第三种情形直接复现：

```
配置：ExclusiveRefreshRate = 57.5242
[Info] Exclusive fullscreen: no 57/58 Hz display mode at 1600x1200 (rates there: 144, 120, 119, 100, 75, 60, 59).
[Info] Exclusive fullscreen: no usable 57/58 Hz display mode; falling back to G-SYNC/VRR at the desktop mode.
旧标签 → Sync: fixed arcade          ← 与事实相反
新标签 → Sync: exclusive VRR (preconditions met)
```

**做法**：新增 `enum class StatsSyncMode`，由 `Main.cpp` 每帧传入**实际解析结果**（`StatsSyncModeForCurrentSession()` 读 `FullScreen` 配置 + `s_fullScreenMode`）。每帧而非启动一次，因为会话中途切换全屏就可能触发回退。

**并且只声明能验证的部分**：把标签写成 `exclusive VRR (preconditions met)` 而不是 `G-SYNC/VRR`（像保证）或 `unverified`（含糊 —— 没说清哪一半没验证）。

四个前提里**三个可观测**：

| 前提 | 能否观测 | 在哪看 |
|---|---|---|
| 进入独占全屏 | ✓ | `Sync:` 行本身 |
| VSync 开启（`FIFO`） | ✓ | 启动日志 `present mode FIFO` |
| 面板上限 ≥ 播放速率 | ✓ | `Panel:` + `Rate:` 两行 |
| **驱动真的在变频面板** | ✗ | **看不到** |

**第四条为什么天生不可观测**（不只是"SDL 没暴露"）：

模拟器**刻意低于面板上限运行**（57.5 fps vs 面板 120 Hz）。生产慢于消费 → swapchain 永远有空闲图像 → `vkAcquireNextImageKHR` 从不阻塞、`vkQueuePresentKHR` 立即返回。应用唯一能测到的时间信号是**它自己的 sleep 时长**，而该值在变频与否两种情况下**完全相同** —— 固定刷新率下只是同一帧被多扫几次。差异发生在"提交 → 扫描输出"之间，应用看不见。

> 反过来：若模拟器试图跑得**比面板快**，`FIFO` 会掐住它、实测 FPS 掉到面板速率 —— 那正是帧率显示暴露 vsync 上限的原理。**刻意压在面板之下，这个信号就不存在了。**

所以想确证第四条只能从外部：**显示器内置 OSD 的实时刷新率**，或 **NVIDIA 自己的 G-SYNC 指示器**。代码里确认**未使用任何 VRR 厂商 API** —— 公开 API 没有可靠的"G-SYNC 是否生效"查询，硬凑只会给出不可信的值。

> 顺带发现这台机器在 1600x1200 下可用刷新率是 `144, 120, 119, 100, 75, 60, 59` —— **没有 57/58**，所以「固定街机」档在此分辨率下必然回退。G-SYNC 是这台机器的正确选择。

#### (0b) `-0.0%` 看起来像格式化 bug

`RefreshRate = 57.5` 与真实 `57.524160` 差 `-0.042%`，按 `%+.1f` 显示为 `-0.0%` —— 读者会以为程序算错了，而不是"下拉框只能给 57.5 的残余误差"。改为 `%+.2f`：`+4.30%` / `-0.04%`。

#### (1) 刷新率显示了，但"对不对"取决于一个我没读懂的设置

第一版显示 `Sync: G-SYNC/VRR  target 60.00 Hz`。用户问："这个 60Hz 是对的吗？"

查下来 **是对的**，但暴露了一个事实：`RefreshRate` 在「画面」页是**二选一档位**（`57.5 Hz（真实机台）` / `60 Hz（推荐）`）。用户选的是推荐档，所以 60 Hz 属实——而街机原生是 `57.524160 Hz`（`Model3.cpp:2052`），即游戏**快 4.30%**。

代码里还有一处**注释与实现不符**（`ResolveFullScreenMode()`）：

```cpp
    // applies to both exclusive paths: ...      ← 注释说两条路径都适用
    if (refreshRateHz > 0.0f)                     ← 但守卫只覆盖"固定街机"档
        s_runtime_config.Get("RefreshRate").SetValue(refreshRateHz);
```

实测确认：G-SYNC 档确实尊重用户的 60 Hz 选择（帧率 60.000），固定街机档才强制 57.524。**行为是对的**（与界面把 60 Hz 标为"推荐"一致），**注释过时了**。

**做法**：改注释说明为什么**故意不**覆盖（覆盖会静默丢弃用户在界面上的选择），并把读数改造成能直接回答这个问题——把两个数并列、把差值算出来：

```
Rate: 60.00 Hz  arcade 57.52  (+4.3%)
```

#### (2) `Display: 60 Hz 1600x1200` 是**假的**

用了 `SDL_GetWindowDisplayMode()`，它返回**按窗口尺寸匹配的模式**——把渲染分辨率和仿真速率包装成了面板数字。实测启动日志里面板其实是 `3840x2160@120 Hz`。

改用 `SDL_GetDesktopDisplayMode()`（先经 `SDL_GetWindowDisplayIndex()`）。修后：

```
Panel: 3840x2160  120 Hz mode
```

与启动日志一致。**G-SYNC 下这个数字是面板自身模式（120 Hz），不是仿真速率**——这正是要看的东西：说明面板有能力跟随，而速率由上面的 `Rate` 行给出。

> 诚实说明局限：SDL **无法报告 G-SYNC 是否真的在变频**。能佐证它生效的是 FPS 数值配合 VSync 开启（日志确认 present mode 为 `FIFO`，会阻塞到面板就绪）。

#### (3) `18 threads` —— 大小核计数，用户直接指出"有点问题"

用户原话："实际没有那么多核调用吧？能否只显示 CPU 里大核心的占用核数？现在 CPU 大小核+超小核设计很容易误导人。"

**这不是显示问题，是三个数字同时错**：

| 指标 | 错误原因 |
|---|---|
| 线程数 18 | 含 10 个 E/LPE 核，模拟器不会被调度到 |
| 频率 4631 MHz | 被小核平均**拉低** |
| 占用率 25% | 被**闲置的小核稀释**，看起来游刃有余 |

**识别 P 核**：`GetSystemCpuSetInformation` 给出每个逻辑处理器的 `EfficiencyClass`（越高越快），取最高类即为性能核。同构 CPU 上所有核同类 → `efficiencyThreads == 0` → `hybrid = false`；

```cpp
g_topology.hybrid = g_topology.efficiencyThreads > 0 && g_topology.performanceThreads > 0;
```

**关键设计：核心数不依赖上述探测。** 核数走独立保底路径：

```cpp
const unsigned total = std::thread::hardware_concurrency();   // 任何 Windows、任何 CPU 都成功
g_cpu.totalThreads = total;
g_cpu.performanceThreads = g_topology.hybrid ? g_topology.performanceThreads : total;
```

三级降级，**每级都还能给出核数**：

1. `GetSystemCpuSetInformation` 可用 → 按 `EfficiencyClass` 精确分类；
2. 该 API 不可用（老 Windows）→ 按 `CallNtPowerInformation` 的**标称频率**分组（同构 CPU 全部同频 → 全归 performance）；
3. 连 `powerprof.dll` 也失败 → `performanceIds` 为空 → `hybrid=false` → 显示 `total`，占用率回退到 `GetSystemTimes` 全局值（`CPU: -  8 threads  25%`）。

**占用率必须走每核计数器**：`GetSystemTimes` 只有全系统值，无法按核心类型拆开。改用 `NtQuerySystemInformation(SystemProcessorPerformanceInformation, ...)`（任务管理器每核视图的同一数据源），**只在 P 核上求和**。同样走"自己声明结构体 + 运行期动态解析"，理由与 powrprof 完全相同：该声明在头文件里被 `_WIN32_WINNT` 挡住，而本项目没有设定该宏。

**实测（用户机器 Intel Core Ultra 5 125H）**：

```
Stats OSD: CPU is hybrid - 8 performance + 10 efficiency logical processors of 18 total;
           load and clock measured over the performance ones.
```

对照 `Win32_Processor`：`14 核 / 18 线程`；实际规格 `4P(带 HT)+8E+2LPE`。**三方吻合** —— 8 个 P 逻辑核正是模拟器能用的全部。

读数：

```
CPU: 5400 MHz  8P+10E threads  25%
```

**同构 CPU 上显示完整核数、不带 `P+E` 标记**（i7-4790K → `8 threads`）—— 用户专门问过这一点。

### 30.4 未翻译是**故意的**，而且要在审计脚本里设防

用户："这些显示参数不需要翻译成中文，因为很容易看懂。"

`Renderer` / `CPU` / `GPU` / `VRAM` / `FPS` / `1% low` 是查这些数字时使用的术语，在界面支持的两种语言里**是同一批词**。放进翻译表反而多一个失效模式：**格式串被重排或丢掉占位符**。

所以：
- 字符串**不经过 `Tr()`**（因此审计的 `Not passed through Tr()` 计数不受影响）；
- 翻译表里**删掉**了为它们加的条目；
- **但 `StatsOsd.cpp` 仍被审计脚本扫描** —— 作为**防线**：将来若有人在此文件里加中文字面量而忘了 `Tr()`，审计会报出来。注释写明这是护栏而非需要翻译：

```powershell
# StatsOsd.cpp is scanned as a guard rather than because it has anything to translate ...
```

设置窗口里那个勾选项和它的帮助文本**仍然翻译**（那是界面的一部分）。

### 30.5 验证方法的弯路（值得记下）

为验证"文字在不在左上角"，前后试了四种抓屏方式：

| 方法 | 结果 |
|---|---|
| `PrintWindow` / BitBlt | 对现代 GPU 渲染无效（`FIXES.md` 已记） |
| `CopyFromScreen` 抓客户区 | 拿到的**没有叠加层**——两次抓到的是同一块橙色**游戏 logo**，一度误判为"面板没画出来" |
| 模拟器自带截图（Alt+S） | 复制 swapchain 于叠加层**之后**，理论上最准，但**输入到不了应用**（`SetForegroundWindow` 返回 True，按键仍不生效） |
| **日志 + 配置比对** | **唯一可靠**：把"面板放在哪、算出的值是多少"直接写进日志 |

最后一条同时解决了两个问题——位置和数值都能逐项对照，例如：

```
Stats OSD: viewport 1600x1200 at (0,0) -> panel 24x24 at (12,12);
           sync G-SYNC/VRR, rate 57.5000 Hz (arcade 57.5242), panel mode 3840x2160@120 Hz.
```

`(0,0) → (12,12)` 直接证明锚点在左上角；把 `RefreshRate` 在 60 / 57.5 之间切换，日志与标题栏帧率分别给出 `60.0000 / 60.000 FPS` 与 `57.5000 / 57.500 FPS`——**配置 → 读数 → 实际帧率三点一致**。

三种同步模式的标签逐一实测：

```
G-SYNC 独占      → sync exclusive VRR (preconditions met), rate 57.5000 Hz, panel mode 3840x2160@120 Hz
固定街机（无该模式）→ sync exclusive VRR (preconditions met), rate 57.5242 Hz, panel mode 3840x2160@120 Hz
窗口化            → sync windowed, rate 57.5000 Hz, panel mode 3840x2160@120 Hz
```

第二行是**关键验证**：配置要"固定街机"、实际回退到 G-SYNC，标签如实反映实际而非配置。

> 一个反复出现的教训：**`CopyFromScreen` 得到的窗口内容不含 ImGui 叠加层**，而它看起来像"功能没生效"。三次不同的抓屏失败都被误读为渲染 bug。日志才是这个项目的可信通道。

---

## 三十一、Magical Truck Adventure 黑块：两份"脏状态"被两个不同条件保护

**症状**（用户报告）：独占全屏 + 4K + G-SYNC 下玩 `magtruck`，切到别的程序（独占全屏被系统强制最小化），切回来再进全屏，**人物和部分场景出现黑块**。

**决定性线索**（用户自己发现）：**改成 borderless 就不出现**。

### 31.1 我先走了两条错路（都该记下来）

**错路一：怀疑是我新加的统计面板污染了纹理传输。**
逐文件核查 + 实测：`StatsOsd` 不引用任何渲染资源，ImGui 后端不用我们的 ring buffer；开着面板的干净运行达到 `0 never uploaded`。**证伪。**

**错路二：怀疑 `CVulkan2D::SetSceneColor` 的句柄比较（句柄复用导致悬挂描述符）。**
实测日志显示销毁/重建的句柄**完全不同**：

```
destroyed: colour ...94E0160, resolve ...94DF980
rebuilt:   colour ...97C4F00, resolve ...97C4C60
```

而且无论复用与否结果都正确（复用→描述符恰好指向新视图；不复用→一定会重写）。**证伪。**

**错路三：怀疑上传队列的 `vector` 数据竞争。**
`queuedUploadTexturesRO = queuedUploadTextures` 与 `push_back` 无同步 → 加锁后**仍然复现**（11 vs 13 个陈旧 tile，属噪声）。**证伪。**

> 教训：**三次"看起来很像"的假设都被数据推翻。** 真正定位靠的是那个此前被我当成误报的 `stale tile` 守卫 —— 它给出了精确坐标，而坐标能区分"寻址错"与"请求丢"。

### 31.2 真正的根因：两个消费者，两种不同的保护条件

`CReal3D` 有**两套独立的"纹理变了"机制**：

| 机制 | 写入方 | 消费方 | 作用 |
|---|---|---|---|
| `textureRAMDirty` 位图 | `UploadTexture()` | `SyncSnapshots()` → `UpdateSnapshot` | 把实时 RAM 拷进 `textureRAMRO`（快照） |
| `queuedUploadTextures` 队列 | `UploadTexture()` | `BeginFrame()` → `Render3D->UploadTextures` | 告诉渲染器"这些 tile 变了" |

Vulkan 读的是 **`textureRAMRO`**（多线程模式的快照副本）。关键在**两个消费者的调用条件不同**：

```cpp
void CModel3::RenderFrame(void)
{
  if (BeginFrameVideo() && gpusReady)     // ← 最小化时为【假】
  {
    GPU.BeginFrame();                     // ← 排空上传队列（在条件【内】）
    GPU.RenderFrame();
  }
}
```

而 `SyncGPUs()`（含 `GPU.SyncSnapshots()`）在**条件之外**每帧都跑，它做的是：

```cpp
queuedUploadTexturesRO = queuedUploadTextures;   // ← 【覆盖】RO
queuedUploadTextures.clear();
```

**丢数据时序**：

1. 最小化 → `BeginFrameVideo()` 为假 → **`GPU.BeginFrame()` 不执行 → RO 未排空，条目滞留**
2. 下一帧 `SyncSnapshots()` 执行 `RO = queuedUploadTextures` → **滞留条目被覆盖丢弃**
3. 但那些写入的**脏位已被 `UpdateSnapshot` 消耗**（`textureRAMRO` 数据已更新）
4. → **数据变了、通知没了** → GPU 保留旧数据且永不复知 → 陈旧 tile → **黑块**

这就是"**必须最小化才触发**"的原因：只有最小化才会**跳过 `GPU.BeginFrame()` 却仍执行 `SyncSnapshots()`**。borderless 不会被强制最小化，所以不触发 —— 与用户观察完全一致。

### 31.3 证据：`stale tile` 守卫

定位的关键是这条既有守卫（当初为另一类问题写的），它比较"GPU 被认为持有的内容"与"实时内容"：

```
[Error] texture tile (52,23) page 0 is STALE - its contents changed but nothing marked it for upload.
        Pixels (1664,736)..(1695,767) of the raw page are wrong on the GPU.
[Error] tile (56,16) (56,17) (41,14) (41,15) (58,27) (60,24) (61,29) (62,28) (62,30) (63,30) ...
```

**tile 是散布的、不在同一区域** → 排除"寻址偏移"（那会是一整块连续区域），指向**通知丢失**。这一步把范围从"纹理寻址"缩到"上传请求"，是整个排查的转折点。

### 31.4 修法

`SyncSnapshots()` 里把**覆盖改为追加**：

```cpp
// 追加以免丢弃上一帧已就绪但未排空的条目
queuedUploadTexturesRO.insert(queuedUploadTexturesRO.end(),
  queuedUploadTextures.begin(), queuedUploadTextures.end());
queuedUploadTextures.clear();
```

（同时给队列加互斥 —— 那是同一处代码的另一个真实竞争，虽不是本症状主因，但属同一类缺陷，一并修掉。）

### 31.5 验证

| | 修复前（2 轮） | 修复后（3 轮） |
|---|---|---|
| 陈旧 tile | **11 / 13 个 `[Error]`** | **`no stale tiles`** ✓ |
| never uploaded | **148261 / 178496（83%）** | **0 / 179157** ✓ |
| 恢复后进程 | **崩溃退出** | **正常存活** |

修复后的完整报告：

```
报告1 (加载):   of 23104:   0 fully, 18496 partly, 4608 never     ← 正常加载瞬态（基线同样有）
报告2 (恢复后): of 146562: 142850 fully, 3712 partly, 0 never  ✓
报告3:          of 485426: 485426 fully, 0 partly, 0 never     ✓
```

### 31.6 顺带修掉的两个真实缺陷

**① `VulkanTextureCache` 丢脏标记**（静默）：脏标记被清空、而 `RecordRawUploads` 因 `!m_textureRam` 静默返回 → 写入永久丢失。改为保留标记 + 报错。

**② 我自己的诊断有 bug**：`UploadTextures` 的整页批量分支（`width>=2048 && height>=2048`）**提前 return**，未置 `m_everDirty` → 会把 ROM 自带纹理**错报成"游戏从未写过"**。已修。（magtruck 实测 `0 whole-page upload(s)`，所以该游戏不受影响，但错误诊断比没有诊断更危险。）

### 31.7 配置对照：必须复刻用户环境

排查过程中用户指出一个关键点：**"你都是窗口化运行，我都是全屏+4K+G-SYNC"**。补做对照后确认：

| 配置 | page0 | never uploaded |
|---|---|---|
| 窗口化 1600x1200 | 90.2% | 0% |
| 全屏 + G-SYNC + 2976x2304 + 宽屏 | 90.2% | 0% |

配置本身不是触发点，**但"最小化"这个动作才是** —— 而只有独占全屏会被强制最小化。**教训：复刻用户的运行环境是前提，否则连复现都无从谈起。**

---

## 三十二、统计面板与设置叠加层支持 OpenGL：共用一份实现

**起因**（用户实测）：笔记本（Intel UHD 620）上 `Vulkan3DEngine=1` 但日志显示

```
[Error] Vulkan: requested device 'Intel(R) UHD Graphics 620' reports API 1.2, but this backend requires Vulkan 1.3.
[Info]  Vulkan: retrying with the OpenGL renderer (New 3D).
```

于是回退到 OpenGL，而**统计面板与 Tab 面板都只在 Vulkan 下初始化**（`if (s_vkContext)`）—— 用户勾了「统计面板」却什么都不显示，**且没有任何提示**。一个勾了却静默无效的设置，比没有这个设置更糟。

### 32.1 关键判断：共用实现，而不是复制一份

面板、读数、输入处理、快照/撤销逻辑**全部共用**，只有三处分叉：

| 分叉点 | Vulkan | OpenGL |
|---|---|---|
| 每帧开始 | `ImGui_ImplVulkan_NewFrame()` | `ImGui_ImplOpenGL3_NewFrame()` |
| 绘制 | `RenderDrawData(drawData, cmd)` —— **需要调用方先开渲染作用域** | `RenderDrawData(drawData)` —— 直接画进当前 framebuffer |
| 重建时机 | swapchain 变化时重建后端 | 不需要 |

新增 `enum class OverlayBackend { None, Vulkan, OpenGL }` 作为标签，`OverlayInit()` / `OverlayInitGL()` 各设一种，`OverlayActive()` 统一对外回答"有没有后端"。

> **为什么不分叉**：复制一份面板意味着以后每次改面板都要改两处 —— 这类"第二份拷贝"必然漂移。而这轮的**根因恰好就是"两个消费者由不同条件保护"**（见第 31 节），所以这里刻意选了共用。

**可行性事先核对过三点**：

1. `imgui_impl_opengl3` 的状态存在 `io.BackendRendererUserData` —— 官方注释写明 *"to allow support for multiple Dear ImGui contexts"*，所以能与设置窗口的上下文共存
2. `Gui.cpp` 已经在用 `ImGui_ImplOpenGL3_Init("#version 410")`
3. 绘制钩子位置现成：`EndFrameVideo()` 里 `SDL_GL_SwapWindow` **之前**

### 32.2 一个必须用 `#version 410` 的理由

游戏的 GL 上下文是 **4.1 core profile**（`CreateGLScreen`），而 core profile 拒绝兼容性 profile 的着色器 —— 不指定 GLSL 版本时 imgui 默认会产出那种，失败表现为**编译错误且信息无用**。所以显式钉住：

```cpp
ImGui_ImplOpenGL3_Init("#version 410")
```

### 32.3 顺带修掉：读数报告"实际渲染器"而非配置值

原来 `Renderer:` 那行从**配置**推导：

```cpp
if (config["Vulkan3DEngine"]...) return "Vulkan 3D";   // 旧：读用户想要什么
```

而回退恰恰发生在"想要 Vulkan 却拿到 OpenGL"时 —— 读数会说谎（**这正是第 30.3(0) 节记过的同一类坑**）。改为由 `Main.cpp` 每帧传入**实际结果**：

```cpp
StatsOsdSetRenderer(s_vkContext ? StatsRenderer::Vulkan3D
  : (s_runtime_config["New3DEngine"].ValueAs<bool>() ? StatsRenderer::New3D : StatsRenderer::Legacy3D));
```

现在读数里的 `Renderer:` 行能把三种渲染器分开 —— `Vulkan 3D` / `New 3D` / `Legacy 3D`，这正是用户要的"未来分析好做区分"。

### 32.4 用户实测确认

截图显示 OpenGL(New 3D) 下面板正常，且 `Renderer: New 3D`、`FPS: 57.5`、`1% low: 57.4` 全部正常。

> 我自己的抓屏验证**失败**了 —— `CopyFromScreen` 抓的是屏幕坐标，被前台终端遮挡。用户的截图才是权威证据。

### 32.5 连带更新

- `Gui.cpp` 删掉"Tab 需要 Vulkan 3D，OpenGL 下按 Tab 没有反应"这条**已失效**的提示（留着会让用户不去试能用的东西）
- `Localization.cpp` 移除对应翻译条目
- `README-VK.txt` 把两处 "Vulkan only" 改掉

---

## 三十三、Vulkan 1.2 兼容：一套代码，运行时选路

**起因**：笔记本的 Intel UHD 620 只报 **Vulkan 1.2**，而入门门槛写死在 1.3。之前的注释记着"1.2 路径已移除，因为**它根本跑不通**"：

> The shaders are compiled to SPIR-V 1.6, which requires Vulkan 1.3 or VK_KHR_maintenance4, and that extension was never in the required list - so the path would have failed at shader-module creation.

**所以"恢复 1.2 支持"必须同时修掉 SPIR-V 版本**，否则会重演同一个失败。

### 33.1 1.3 相对 1.2 只多了三样，且都有对应扩展

| 1.3 核心特性 | 1.2 下的等价物 |
|---|---|
| dynamic rendering | `VK_KHR_dynamic_rendering` |
| synchronization2 | `VK_KHR_synchronization2` |
| SPIR-V 1.6 | `VK_KHR_maintenance4`，**或把着色器降到 SPIR-V 1.5** |

其余全是 1.2 核心（`descriptorIndexing`、`runtimeDescriptorArray`、`descriptorBindingVariableDescriptorCount` 等）—— 也就是说 **1.2 早就是实际下限，1.3 只是额外门槛**。

### 33.2 代码清点：只有三个函数族、14 个调用点

```
vkCmdBeginRendering / vkCmdEndRendering   ← Vulkan3D.cpp ×4, Vulkan2D.cpp ×8
vkCmdPipelineBarrier2                      ← Vulkan3D.cpp ×1, VulkanCommon.h ×1
vkQueueSubmit2                             ← VulkanContext.cpp ×1
```

结构体（`VkRenderingInfo` / `VkDependencyInfo` / `VkSubmitInfo2` / `*MemoryBarrier2`）随扩展提供，**不需要改**。

**关键**：volk **不做**核心名到 KHR 名的宏别名（`vkCmdBeginRenderingKHR` 是独立指针），所以恢复四个包装函数：

```cpp
inline void CmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo* info)
{
	if (vkCmdBeginRendering)      vkCmdBeginRendering(cmd, info);       // 1.3 核心
	else if (vkCmdBeginRenderingKHR) vkCmdBeginRenderingKHR(cmd, info); // 1.2 扩展
}
```

1.3 机器走核心名 —— **行为零变化**。

### 33.3 特性结构体必须按路线分岔

这是最容易写错的地方：

```cpp
if (route == BackendRoute::Vulkan13)
    features12.pNext = &features13;                    // VkPhysicalDeviceVulkan13Features
else {
    features12.pNext = &featuresDyn;                   // DynamicRenderingFeaturesKHR
    featuresDyn.pNext = &featuresSync;                 // Synchronization2FeaturesKHR
}
```

**为什么不能一律链 `Vulkan13Features`**：在 1.2 设备上链它是**未定义行为**，读到的是 0，于是后端会**错误拒绝**一台本来完全能跑的设备。查询与启用都必须用同一套结构体。

### 33.4 新增入口点校验

包装函数在**两个指针都为空时静默什么都不做** —— 这对绘制命令是正确行为，但**不该无人察觉**。所以设备创建后查一次：

```cpp
if (!Rendering2EntryPointsAvailable())   // 三个族各自至少有一个名字可用
    return Result::FAIL;                  // 明确报"驱动不支持"，而不是留一块空屏
```

### 33.5 着色器降到 SPIR-V 1.5

`Scripts/CompileVulkanShaders.cmd/.sh`：`--target-env vulkan1.3` → **`vulkan1.2`**，重编后 8 个 `.spv` 从 **1.6 → 1.5**，`.spv.h` 同步重生成。SPIR-V 1.5 在 1.2 和 1.3 上**都**能加载。

### 33.6 验证：临时钩子强制走 1.2 路径

在 1.3 设备上正常永远走不到 1.2 分支，所以**加临时钩子强制它**，验证后撤除：

```
[Info] Vulkan: using NVIDIA GeForce RTX 5060 Ti (API 1.4, Vulkan 1.2 extension path).
[Info] Vulkan3D: renderer ready.
[Info] Vulkan: textures - of 147987 textured draw(s): 147404 fully uploaded, 0 partly uploaded, 583 never uploaded
[Info] Vulkan: textures - no stale tiles
```

**不只是"没崩"**：99.6% 纹理全量上传、无陈旧 tile、无错误（那 583 个"从未上传"是已知的游戏自身填充区，与 1.3 基线一致）。

**诚实说明测试边界**：这次验证覆盖了**扩展请求、KHR 特性结构体、设备创建、SPIR-V 1.5 加载、渲染**。但**未覆盖 KHR 函数指针那条分支** —— 因为在 1.4 设备上 volk 也会加载核心名，包装函数仍走核心分支。那条分支只是空指针回退，风险低。

### 33.7 日志现在标明走的哪条路

```
Vulkan: using NVIDIA GeForce RTX 5060 Ti (API 1.4, Vulkan 1.3 core path).
Vulkan: using Intel(R) UHD Graphics 620 (API 1.2, Vulkan 1.2 extension path).
```

这是排查陌生硬件时**第一个该看的行** —— 两条路执行同样逻辑但走不同入口点，知道走了哪条能立刻缩小范围。

---

## 三十四、让"帧节奏"这件事变得可读、可解释

### 34.1 补上文档里最容易被误判的一个坑

用户问："57.5 Hz 是不是要显示器和显卡都支持才能实现？" 查下来答案是"是"，但**两条路线含义不同**，而且**其中一条代码看不到关键那一项**：

| 路线 | 显卡/驱动 | 显示器 | 代码可确认性 |
|---|---|---|---|
| **固定 57.524** | 需自定义分辨率（NVIDIA 无公开 API，只能手工建） | 需接受该时序 | ✓ 能查模式列表（`FindArcadeDisplayMode` 找 57/58） |
| **G-SYNC/VRR** | 需支持+驱动里启用 | 需支持，**且 VRR 范围下限 ≤ 57.5** | ✗ 只看得到"独占+VSync" |
| **borderless** | 无关 | 无关 | ✓ 明确"拿不到" |

**关键区分**：「显示器支持 G-SYNC」与「该显示器能在 57.5 Hz 下 G-SYNC」**是两件事**。后者取决于 **VRR 范围下限**（常见 48 Hz，但可能是 60）。低于下限时驱动要么走 **LFC**（面板扫 ~115 Hz、每帧显示两次）、要么 G-SYNC 不生效 —— 而程序**两样都看不到**。

**而整个项目此前完全没有提到 VRR 范围**（搜源码 + 两份 README + 帮助文本，零命中）。于是"我开了 G-SYNC 却没感觉"最常见的真实原因被留给了用户自己猜。

**已补进 `README-VK.txt`** 的 G-SYNC 段落：说清 range 与 feature 的区别、低于下限的两种后果、以及**怎么自己查**（显示器 OSD 的实时刷新率、NVIDIA 控制面板 "Set up G-SYNC" 页列出的范围），并把读数里的 `preconditions met` 措辞**解释了**（能确认三项，第四项要显示器自己说）。

### 34.2 borderless 的帧节奏：把两个已知数算成一句结论

borderless 拿不到 VRR，面板保持自身速率，仿真帧被"套"到面板节奏上。**当面板速率是仿真速率的整数倍时是均匀的**（115 Hz 面板把 57.5 的每帧显示恰好两次），否则有的帧多显示一次 → 这就是"轻微不均"。

**两个输入（`Panel:` 与 `Rate:`）本来就在屏幕上，但中间那步算术没人替用户做。** 现在做了：

```
Panel: 3840x2160  120 Hz mode
Cadence: uneven (2.09 panel scans per frame) - frames repeat
         unevenly; an exclusive sync mode removes this
```

整除时显示 `Cadence: even (2 frames shown per panel scan)`，**并且不提"独占模式"** —— 没问题就不该建议改设置。

**只在 borderless 下计算**：两个独占模式要么让面板跟随、要么就是面板速率本身，不存在"节奏是否整除"的问题。

**同时告知设置窗口**：读数默认关闭，而"为什么 windowed 下画面有点抖"的人**往往没开读数**。所以加了 `StatsOsdBorderlessJudderPending()`，由 `Main.cpp` 在会话内报一次 —— 既有日志也有 `GuiPostNotice` 弹窗。

### 34.3 验证（含判定力测试）

| 场景 | 预期 | 实测 |
|---|---|---|
| borderless + 57.5（面板 120 Hz，2.09） | 报警 | ✓ `Frame cadence: ... does not divide evenly` |
| borderless + 60（面板 120 Hz，恰好 2.0） | **不报警** | ✓ 静默 |
| 独占 G-SYNC + 57.5 | **不报警** | ✓ 静默 |
| 独占固定 57.524 | **不报警** | ✓ 静默 |

> 第二行是**判定力测试**：只会通过（报警）的检查不算检查。它同时验证了整数倍判定和 1% 容差。

### 34.4 顺带修掉一处我自己留下的悬空引用

`Main.cpp` 的注释里写着 `See the note on SpeedMode below.` —— 而 `SpeedMode` **全项目只出现在这条注释里**（我上次改注释时凭空写的）。已改为引用真实函数 `CModel3::RunMainBoardFrame`，并补上了"仿真时间步长与 `RefreshRate` **互相独立**"这个实质说明 —— 这才解释了为什么选 60 Hz 会让游戏**跑快 4.3%**，而不是"帧节奏不同"。

> 教训：**注释里引用符号名是有代价的。** 引用了就必须核实存在 —— 否则是一条会误导后来者的线索。这次我因此返工两次（先写错 `SpeedMode`，修正时又写错 `ComputeTimings`）。

---

## 三十五、设置项的两处诚实性修正

用户的两次追问各自暴露了一个问题：

> 「rebuild 发布的版本里一直没有 `ArcadeDisplay.ps1` 啊？」
> 「如果显示器不支持，独占 G-SYNC 这个选项应该变成灰色不可选吧？」

### 35.1 界面/文档引用了**根本没分发**的文件

`Scripts\ArcadeDisplay.ps1` 被 **5 处**引用（`Gui.cpp` 2 处、`Localization.cpp` 3 处、README 1 处），而 `build.bat` 只复制 exe、`Data\`、文档 —— **`Scripts\` 整个目录不打包**。所以发布版在教用户运行一个不存在的文件。

**这不是笔误，而是一类结构性疏漏**：帮助文本、README、build 脚本分属三个地方，三者之间没有任何机制保证一致。改动帮助文本的人不会去看 `build.bat`。

**修法（选择"分发"而不是"删掉引用"）**：

1. `build.bat` 增加一段，把脚本复制到 `%DIST%\Scripts\` —— 保留 `Scripts\` 这一层，**让文档里已经写着的路径直接变成正确的**
2. 脚本的默认 exe 解析**同时支持两种布局**（原来只认 `MSVC\x64\Release\`，发布版里不存在）：

```powershell
$parent = Resolve-Path (Join-Path $PSScriptRoot '..')
foreach ($name in @('Supermodel-VK.exe', 'Supermodel.exe')) {   # 发行布局
    $candidate = Join-Path $parent $name
    if (Test-Path $candidate) { return ... }
}
$candidate = Join-Path $parent 'MSVC\x64\Release\Supermodel.exe'  # 源码检出
```

**实测**（从发布布局运行 README 教的那条命令）：
```
Current mode: 3840x2160 @ 120 Hz
Available resolutions and refresh rates: ...
No 57/58 Hz display mode found.
```

3. README 补上 `.ps1` 的**执行策略**说明 —— 随下载分发的 `.ps1` 会被默认策略拦下：

```
powershell -ExecutionPolicy Bypass -File Scripts\ArcadeDisplay.ps1 -Action List
```
并说明 `-ExecutionPolicy Bypass` 是"让这个文件能跑"，**不是建议改系统设置**。

### 35.2 两个选项在某种配置下**完全等价** → 应禁用其一

**"固定街机刷新率"原来在任何情况下都可选**，只是选中后警告"会回落"。而回落到的是**上面那个选项**：

```cpp
if (refreshRateHz > 0.0f) {
    if (FindArcadeDisplayMode(refreshRateHz, mode) && SDL_SetWindowDisplayMode(...) == 0)
        return FullScreenMode::ExclusiveArcade;
    // 找不到 → 落到下面，返回 ExclusiveDesktop = G-SYNC 模式
}
```

**没有 57/58 Hz 模式时，两个选项产生的结果完全相同。** 两个选项做同一件事、其中一个还描述了别的东西 —— 比一个"不可用且说明原因"的选项更糟。

**已改为真正置灰**（`ImGui::BeginDisabled`），并保留琥珀色说明：

```
未检测到 57/58 Hz 模式，因此此项不可选：选了也只会回落到上面的 G-SYNC。
需要在显卡控制面板里创建一个 57.524Hz 自定义分辨率（见 Scripts\ArcadeDisplay.ps1，创建后自动生效）。
```

顺带修掉一个**陈旧缓存**：原来是 `static const bool arcadeModeAvailable`（**进程内只算一次**）。而设置窗口的存活时间长于"用户去驱动面板建自定义分辨率"这个动作 —— 自然的顺序正是**看到提示 → 去建 → 回来**，缓存会一直说"不可用"，读起来像 bug。改为**按帧节流**（约每 2 秒复查一次，`SDL_GetDisplayMode` 读的是 SDL 自己的缓存，开销很小）。所以提示里可以写"**创建后自动生效**"，不需要重启。

### 35.3 而 G-SYNC 那一项**没有**置灰，这是有意的

用户建议"不支持就置灰"，但**VRR 是否可用无法检测** —— 见第 34.1 节的结论：没有可移植接口能查询 VRR 范围，也没有接口能问"此刻驱动是否在变频"。**基于猜测去禁用，比说明"什么是未知的"更糟**，而且没有可用于猜测的依据。

所以改成**把要求写出来**：

```
需要显示器支持 VRR，且其刷新率范围包含 57.5Hz。
这两项无法自动检测，可在显示器自带菜单或显卡控制面板里查看范围。
```

**两个独占选项的处理方式不同，判据是同一条**：能检测的（固定街机模式）就检测并禁用；不能检测的（VRR 范围）就说清楚并指出去哪查。**不假装有一个我们没有的检查。**

### 35.4 验证

| 项目 | 结果 |
|---|---|
| 脚本随发布版分发 | ✓ `dist\Supermodel-VK\Scripts\ArcadeDisplay.ps1` |
| 脚本在发布布局下可用 | ✓ `-Action List` 正确列出模式并报告无 57/58 |
| 置灰条件在本机成立 | ✓ 日志 `no 57/58 Hz display mode at 1600x1200` |
| "已配置但不可用"路径 | ✓ 应用正常运行，回落 G-SYNC |
| 构建 / 翻译审计 | ✓ 退出码 0 / `Missing 0, Not passed 0` |

### 35.5 用户的追问揭出的第三个缺口：三个选项，没人回答"我该选哪个"

用户接着问：

> 「如果用户电脑显示器连 VRR 都不支持，是否 G-SYNC/VRR 那个选项也得变成不可选的灰色？」

**技术上不能检测** —— 我把 API 层查了一遍：

| 检查 | 结果 |
|---|---|
| SDL 2.30 全部头文件搜 `VRR` / `variable.refresh` / `AdaptiveSync` / `FreeSync` | **零命中** |
| `SDL_DisplayMode` | 只有**离散模式**，没有范围信息 |
| Vulkan present mode（`FIFO`/`MAILBOX`/`IMMEDIATE`） | 只描述垂直同步策略，与自适应同步无关 |

所以**不能基于能力置灰**。

**但这个问题指向了真正的缺口** —— 三种用户的实际处境：

| 显示器 | 57/58 模式 | 固定街机 | G-SYNC/VRR | 结果 |
|---|---|---|---|---|
| 有 VRR | ✗ | 置灰 | 可选 | VRR 生效 ✓ |
| 无 VRR，已建模式 | ✓ | 可选 | 可选（无效） | 固定 1:1 ✓ |
| **无 VRR，未建模式** | ✗ | **置灰** | 可选（无效） | **两个都不理想，且无引导** ← 缺口 |

第三行：一个既没有 VRR、也没建自定义分辨率的用户，看到的是"**一个灰着、另一个选了没用**"，**而没有任何人告诉他该怎么办**。

**修法不是置灰（检测不到），而是回答那个问题** —— 在三个选项**之前**加一行"怎么选"，且措辞**跟随检测结果**：

```cpp
const bool arcadeModeAvailable = ArcadeDisplayModeAvailable();
if (arcadeModeAvailable)
    UiTextDim("怎么选：有 VRR 显示器选第 2 项；没有 VRR 就选第 3 项（已检测到 57/58Hz 模式）。");
else
    UiTextDim("怎么选：有 VRR 显示器选第 2 项；没有 VRR 需要先建 57.524Hz 自定义分辨率，"
              "否则请留在第 1 项。");
```

> 措辞跟随检测结果，是因为"用第 3 项"写在**灰掉的第 3 项旁边**是没用的建议。

`arcadeModeAvailable` 因此**上提到本段开头只读一次**，避免"怎么选"那行与置灰控件对同一条件给出不同答案。

### 35.6 顺带把 G-SYNC 项的"失败后果"也说清

原来的说明只说"需要 VRR、范围要含 57.5Hz、无法检测"。补上了**选错会怎样**：

```
若显示器不支持，本项不会报错也不会更差，只是退回按面板自身的刷新率输出，
画面会有和「无边框全屏」类似的轻微帧节奏不均。
```

**并把这个"为什么不禁用"的理由写进了代码注释**，因为它是一个刻意的非对称设计：

> 固定街机项可检测、且失败**可证**（它会精确地做 G-SYNC 做的事）→ 禁用。
> G-SYNC 项不可检测、且失败**只是错过**（会话照常运行，节奏不比 borderless 差）→ 不禁用。
> **基于看不见的能力去禁用，等于从最想要它的用户手里拿走这个选项。**

### 35.7 验证

| 项目 | 结果 |
|---|---|
| 构建 | ✓ 退出码 0，无 error/warning |
| 翻译审计 | ✓ `Missing 0 / Not passed 0`（新增 2 条 + 改 1 条） |
| `BeginDisabled`/`EndDisabled` 配对 | ✓ 设置窗口运行 12 秒**无 ImGui 断言** |
| 「怎么选」分支取值 | ✓ 本机无 57/58 模式 → 走"需要先建自定义分辨率"那一版 |

> **未验证**：新文案的视觉呈现（换行、行宽）。我试了 `SetForegroundWindow` + `PrintWindow(PW_RENDERFULLCONTENT)` 两种方式都拿不到这个 OpenGL 窗口的内容。**这需要人眼确认。**

### 35.8 追查"Windows 11 能显示 VRR，我们能不能读"

用户进一步指出他在 **Windows 11 → 设置 → 系统 → 显示 → 高级显示器设置** 里看到了 VRR 信息，并问能否从这里获取、或有没有第三方脚本可调用。

**逐层追查的结果**：

| 检查 | 手段 | 结果 |
|---|---|---|
| **EDID** | 从注册表读出发行机 G70D 的完整 384 字节 EDID（base + CEA-861 + DisplayID）并逐块解析 | ✗ **不在里面**：DisplayID 块只有 `Display Parameters`，**无 Adaptive Sync 块（tag `0x2A`）**；CEA 块无范围值；找 `0x30`(48) 与 `0x90`(144) 零命中 |
| **Windows SDK** | 搜两个 SDK 版本全部头文件的 `VRR` / `VariableRefreshRate` / `AdaptiveSync` / `FreeSync` / `G-SYNC` | ✗ **零命中**（命中的都是无关的 `SYNC`）。**局限**：本机 SDK 未安装 `displayconfig.h`，所以无法核对 `DISPLAYCONFIG_DEVICE_INFO_TYPE` 的全部取值 |
| **SDL 2.30** | 搜全部头文件 | ✗ 无 VRR 接口 |
| **Vulkan present mode** | 读 `VkPresentModeKHR` 语义 | ✗ 只描述垂直同步策略 |
| **NVAPI** | 探测 `nvapi64.dll` 导出 | `nvapi_QueryInterface` **已导出**（说明可像 NVML 那样动态加载），但函数按 **ID** 取而非按名导出 → **无法在本机核实 VRR 函数 ID 与结构体**（需 NVAPI SDK） |

**技术原因**：DisplayPort 的自适应同步能力由显示器通过 **DPCD 寄存器经 AUX 通道**上报，**不走 EDID**；读 DPCD 必须经过显卡驱动的 AUX/I2C 接口，属**内核态显示驱动**职责。Windows 的「高级显示器设置」页即向驱动索取此数据。

**决定：不实现检测。** 判据与第 33/35 节一致：

1. **数据源都不在用户态可达范围**，唯一可能的路（NVAPI）**无法在本机验证**，出错概率高
2. 即使拿到，多数路径只能给出"是否支持"而非**"范围是否含 57.5"** —— 而后者才是真正要回答的问题
3. **给错答案比没有答案更糟** —— 与"不凭猜测置灰"同一条理由
4. **用户已有权威来源可看**，而我们要做的只是**告诉他去哪看**

**实际改动（零风险）**：用户找到的那个位置比我原来写的（"显示器自带菜单或显卡控制面板"）更好找，所以把它写进了帮助文本和 README，并**按"每个来源回答哪个问题"组织**，而不是列一个清单：

| 来源 | 回答什么 |
|---|---|
| Windows 11「高级显示器设置」 | **是否支持**（最快） |
| NVIDIA 控制面板「设置 G-SYNC」 | **刷新率范围**（真正要看的那个数） |
| 显示器自带 OSD | **此刻是否在变频**（唯一能看实时值的） |

README 里同时写明了"为什么这只能是文案而不是检测"（DPCD/AUX + 无公开 API），这样后来者不会再问一遍。

### 35.9 采纳资料里唯一站得住的那一块：EDID 时序范围

用户提供了一份第三方资料（三个方案）并问哪种适合集成。**我逐条实测了每个断言**，结果如下 —— 否定结果本身是交付物，它免得后来者再查一遍：

| 资料断言 | 实测 |
|---|---|
| 方案一 WMI 类可用 | ✓ 四个类都在，能读出 128 字节块 |
| 方案一 `0xFD` 能解出范围 | ✓ **偏移写法正确**（我先前算错偏移是我自己的错）。G70D = 48-144 Hz，S24F350 = 50-75 Hz |
| 方案一「支持 DP Adaptive-Sync 的显示器都会返回范围」 | ✗ `0xFD` 是**时序范围**，几乎每台显示器都有，**不是 VRR 声明** |
| 方案一/二 FreeSync `00-00-1A` | ✓ **正确 —— 且我一度漏查它**（见下）。G70D 确有该块，并带范围 48-144 |
| 方案二 注册表 EDID | ✗ 返回全部**历史**记录（本机 7 条，仅 2 条活动）；代码注释说"跳过历史残留"，但 `except` 只捕获"键不存在" |
| 方案二 启发式 `v_min<=50 && v_max>=100` | ✗ 不是 VRR 判据 —— 无 VRR 的 144Hz 屏同样是 48-144 → 误报 |
| 方案三 DXGI `PRESENT_ALLOW_TEARING` | ✗ 报告的是**驱动/合成器**能力（Win10 1803+ 基本为真），与显示器无关。**我尝试实测但 COM vtable 调用崩溃，故未实测，仅据文档含义判断** |
| 方案三 Linux `xrandr ... vrr_capable` | ✓ **真能力标志**（内核驱动从 DPCD 填的 DRM 属性），但仅限 Linux |

**我当时的错误**：曾断言"G70D 的 EDID 里没有任何 VRR 声明"，并列了下面这张表 —— **那张表是错的**，因为我只走了顶层 `tag == 7`（扩展标签）这条支路，没查顶层 `tag == 3`（VSDB）：

| 查找（**错误地只查 tag 7 支路**） | 当时结果 | 实际 |
|---|---|---|
| `0xFD` 时序范围 | ✓ 48-144 Hz | 对 |
| FreeSync VSDB `00-00-1A` | ✗ 报"不存在" | **存在**，在顶层 `tag == 3` 的 VSDB 里，我没查 |
| HDMI Forum VRR `C4-5D-D8` | ✗ 不存在 | 本机两台都没有，**无正例可证** |
| DisplayID Adaptive Sync (tag `0x2A`) | ✗ 不存在 | DisplayID 只有 `Display Parameters`，对 |

**更正见 35.11** —— VRR 范围**确实**能从 EDID 读到，且已实现。

`root\wmi` 全部类里确实没有任何 VRR 字段（只有 DPMS/GTF/sRGB 标志），这一点不受影响。

### 采纳的部分：`0xFD` 时序范围用来回答**另一个**问题

`0xFD` 虽非 VRR 范围，却正好补上「固定街机刷新率」那条路**答不了的一个问题**：**面板会不会接受 57.5Hz 的时序？**

现在的提示只说"去建自定义分辨率"，但**低于面板时序范围的自定义分辨率会被显示器拒绝**，而此前无从判断。现在有了：

```
显示器 EDID 报告的垂直时序范围：S24F350 50-75 Hz; Odyssey G70D 48-144 Hz
57.5Hz 需要落在范围内，显示器才会接受这个自定义分辨率（与 VRR 无关）。
```

**明确标注为"时序范围、与 VRR 无关"** —— 不再冒充 VRR 检测。这是**必要条件而非充分条件**：它说面板接受该时序，不说 VRR 会跟随。

**实现要点**：

1. **用 WMI 而非直接读注册表** —— 注册表保留所有历史显示器（本机 7 条），而 `WmiMonitorBasicDisplayParams` 只列**活动**的。实测过滤正确：只报 2 台
2. **复用工程已有的 WMI 模式** —— `DirectInputSystem.cpp` 已用同一 API，`wbemuuid.lib` 已链接
3. **测试两种 WMI/BSTR 用法**：`Get(L"Active")` 判活动、`Get(L"InstanceName")` 取注册表路径；EDID 从注册表读（比调 WMI 方法简单）
4. **成本控制**：模式检查每 ~2 秒复查（廉价，让用户建完自定义分辨率后自动生效），而 **EDID 读取走 WMI、耗时数十毫秒，故只抓取一次并永久缓存** —— 且在**只在需要展示时**才抓
5. **失败即静默**：读不到 EDID 就换一句说明，不让辅助信息影响功能

### 35.10 验证

```
[Info] Display EDID timing ranges: S24F350 50-75 Hz; Odyssey G70D 48-144 Hz.
```

**与独立的 PowerShell 解析逐字一致**，四条数据路径（WMI 查询、注册表读 EDID、`0xFC` 取名、`0xFD` 解析）全部成立。这条日志同时是排查用的信息 —— "模拟器认为我的显示器范围是多少"从截图答不出来。

| 项目 | 结果 |
|---|---|
| 构建 | ✓ 退出码 0 |
| 翻译审计 | ✓ `Missing 0 / Not passed 0`（新增 3 条） |
| EDID 读取 | ✓ 与独立解析一致，且只报活动显示器 |
| 设置窗口 | ✓ 运行 12 秒无异常 |

---

## 三十五·补：更正 —— VRR 范围**确实**能从 EDID 读到

**用户提供第二份资料后发现的错误。** 我在 35.8/35.9 里断言"VRR 检测不可实现"、并写进了 README 与 `FIXES.md` —— **这是错的**，已在代码、README、`TECHLOG.md`、`FIXES.md` 全部更正。

### 错在哪

我只解析了 CTA-861 扩展块里的**扩展标签**（顶层 `tag == 7`），去找 ext tag `0x01`(VSVDB) 和 `0x2A`，**漏掉了顶层 `tag == 3` 的 VSDB**。补上后的原始字节：

```
offset 24: 标签字节 0x74 → tag 3 (VSDB) len 20
      载荷: 1A 00 00 03 0F 30 90 00 00 60 08 5B 36 90 00 00 00 00 00 00
      → OUI = 00-00-1A   （AMD / VESA Adaptive-Sync）
         byte[5] = 0x30 = 48
         byte[6] = 0x90 = 144
```

**VRR 范围就在里面：48-144 Hz。** 用户那份资料里 `00-00-1A` 的偏移写法（`[offset+6]`/`[offset+7]`，offset 为标签字节位置）**完全正确**。

### 已实现，并带一个关键的不对称

`ParseEdidVrrRange()`（`Main.cpp`）遍历全部 128 字节块，在 CEA 扩展块（`[0] == 0x02`）里找顶层 `tag == 3`、OUI `1A 00 00`、`len >= 8` 的块，取 `byte[5]`/`byte[6]` 为范围。

**不对称是设计的核心，也是我上次犯错能带来的一点价值**：

| 情形 | 结论 | 依据 |
|---|---|---|
| **声明了范围** | **显示器支持 VRR，范围如此** —— 报为事实 | 显示器主动告知 |
| **没有声明** | **什么都不报**，绝不写"不支持 VRR" | 支持 VRR 可以不在 EDID 里声明，**旧版 AMD HDMI FreeSync 正是这种情况**（能力在驱动里） |

所以输出形如：

```
S24F350 50-75 Hz; Odyssey G70D 48-144 Hz (declares VRR 48-144 Hz)
```

S24F350 只有 `00-0C-03`(HDMI 1.4 Licensing, len 5)，**没有声明** → 只报时序范围，不给 VRR 结论。

> 这与 **0xFD 时序范围**是两回事，两者必须分开陈述：时序范围人人都有、与 VRR 无关；声明的 VRR 范围只在有时才报、且是正面证据。**上次的错误根源就是把"没找到声明"当成了"没有 VRR"。**

### 顺带确认了那个悬了很久的问题

G70D 声明 **VRR 48-144 Hz**，而 **57.5 落在其中** —— 所以用户的 G-SYNC 路线确实能在街机速率下变频。这个结论之前只能靠"显示器 OSD 看一眼"，现在有了 EDID 证据。

### 验证

```
[Info] Display EDID timing ranges: S24F350 50-75 Hz; Odyssey G70D 48-144 Hz (declares VRR 48-144 Hz).
```

判别正确：G70D 报出、S24F350 沉默。构建退出码 0，翻译审计 `Missing 0 / Not passed 0`。

### 第二份资料的其他断言评估

| 断言 | 实测 |
|---|---|
| `WmiMonitorConnectionParams.VideoOutputTechnology` 能报接口类型 | ✓ **实测**：S24F350 = 5 (**HDMI**)，G70D = 10 (**DisplayPort**) |
| EDID 第 20 字节报数字接口 | ✓ **实测**：G70D = 0xB5 → DisplayPort；S24F350 = 0x80 → EDID 1.3 该字段未定义（资料的脚本也正确注明"老固件未定义"） |
| S24F350 是 HDMI-only、无 DP | ✓ **与实测一致**（`VideoOutputTechnology = 5`） |
| HDMI 1.4 VSDB `00-0C-03` 的 Max_TMDS 在 `[p+7]`、HDMI Forum 在 `[p+5]` | ✓ 偏移**核对无误**（对照 HDMI VSDB 布局：payload[6] 与 payload[4]） |
| 用 Max_TMDS 判 HDMI 1.4 vs 2.0 | △ **对 S24F350 无效**：它的 VSDB `len = 5`，**根本没有 Max_TMDS 字段**（按 HDMI 规范，<7 字节即 ≤165 MHz）。脚本会读到 0 并误报"HDMI 1.3 或更低" |
| `PRESENT_ALLOW_TEARING` 等同 VRR 支持 | ✗ 上一份资料已判定为无关（测的是驱动/合成器能力） |
| **NVIDIA 全系不支持动态模式** | ⚠ **窄义正确、极易被误读**。它说的是 AMD 私有的 **HDMI 1.4 FreeSync** 协议；而用户的 G70D 在 RTX 5060 Ti 上**确实**跑着 VRR（经 DisplayPort） |
| HD 7750（GCN 1.0 / DCE 6.0）不支持 FreeSync，GCN 1.1+ 才行 | ⚠ **无法验证**（本机无该硬件）。代际划分听上去合理，但资料里那条"你的 HD 7750 / HP t740"是**关于用户其他设备的推断**，我没有任何依据确认 |
| S24F350 的 FreeSync 范围为 48-72 Hz | ⚠ **EDID 里查不到**（它没有 Adaptive-Sync 声明）。这与"旧 AMD HDMI FreeSync 不写 EDID"一致，但意味着**该数值无法用程序核实** |
| 固定 57.524Hz 自定义分辨率在帧率恒定时等效于 FreeSync | ✓ **原理正确**。且 S24F350 的 `0xFD` 时序范围 50-75 **含 57.5** → 该自定义分辨率应当会被接受 |

---

## 三十五·补二：4:3 宽频专业屏（1600×1200 @ 48-85Hz）走的是哪条路

用户提出一台复古 4:3 屏（HP LP2065，1600×1200，**48-85Hz 宽频**），问：能不能用 CRU 建 1600×1200@57.5，我们的方案是否覆盖这类显示器，怎么让用户一键成功，是不是就是「固定 57.524Hz」模式。

### 答案：是，而且正好是本功能的目标机型

**它走的正是「独占全屏 · 固定街机刷新率 57.524Hz」那条路**，代码路径逐段核实：

| 环节 | 行为 |
|---|---|
| **模式查找** | `FindArcadeDisplayModeImpl` 找 `refresh_rate == 57 \|\| 58`。三趟优先序 = **渲染分辨率 → 桌面分辨率 → 任意**。CRU 建出的 1600×1200@57 会出现在模式列表里；只要桌面是 1600×1200，第 1 或第 2 趟即命中 |
| **几何** | `CalcScreenGeometry` 按 **496/384 = 1.2917** 锁定比例 —— **比 4:3（1.3333）略窄**。1600×1200 下画面约 **1550×1200**，左右各约 25px 黑边。**比例正确、不变形** |
| **整数倍缩放** | `IntegerScale` 开：3x → 1488×1152 居中，左右 56px、上下 24px 黑边（最锐利） |
| **宽屏选项** | 4:3 屏上**无额外宽度可用 → 无效果**（README 已说明） |
| **机柜背景图** | 无侧边黑条可填 → **自动跳过**（`LoadBezelImage` 已有此分支） |

### 而这次改动正好为它加了一条直接的答案

原来提示只说"57.5Hz 需要落在时序范围内"，**让用户自己比对两个数**。现在直接算出来：

```
S24F350 50-75 Hz - 57.5Hz 在时序范围内，自定义分辨率可创建
Odyssey G70D 48-144 Hz (declares VRR 48-144 Hz) - 57.5Hz 在时序范围内，自定义分辨率可创建
```

**LP2065 的 48-85 含 57.524 → 会报「可创建」**，用户不必自己判断。

实现上把返回值从"拼好的字符串"改为**结构体列表**（`DisplayTimingInfo`），因为"是否落在范围内"这个判断涉及两条 UI 文案（在范围内／不在范围内），**必须走翻译表**，不能把中文字面量混进数据字符串。判定与措辞都放在 `Gui.cpp`。

### 「一键或自动」能到什么程度，诚实说明

| 环节 | 能否自动化 |
|---|---|
| **创建自定义分辨率** | **不能**，且这是**驱动层限制**：NVIDIA 无公开 API（`ArcadeDisplay.ps1` 已记载）、CRU 只有 GUI、Intel/AMD 也无可移植接口 |
| **检测已存在的 57/58 模式** | ✓ 自动（`FindArcadeDisplayModeImpl`） |
| **判断该不该去创建** | ✓ 自动（EDID 时序范围 + 57.5 判定，本次新增） |
| **切换到该模式并退出后恢复** | ✓ `ArcadeDisplay.ps1 -Action Run` |

所以现实流程是 **手动建一次 → 之后全自动**，且设置窗口会主动告知"该不该建"。**这个边界是硬限制而非偷懒** —— 写在文档里比让用户怀疑"是不是漏做了什么"要好。

### 一个我无法从代码确认的点，如实标注

LP2065 是 2006 年左右的 S-IPS 屏。**"48-85Hz 宽频"说明它的电子部分能同步到 48-85Hz，但不保证面板真的按输入速率扫描** —— 部分老 LCD 有固定内部刷新 + 帧缓冲，此时喂 57.5Hz 会被 scaler 丢帧/重复，judder 依旧。

**无法从 EDID 判断这一点**（EDID 只声明它接受该时序）。**判据只能靠实际观感**：用它跑 57.5Hz，看运动是否平滑；或看显示器 OSD 报告的输入时序。**这是这类显示器的固有不确定性，不应由程序猜测。**

### 35.12 双语启动器：一个 cmd 编码缺陷逼出的架构

用户要求"中英文各一个 `.bat`，方便不同语言的用户"。

**但直接做会坏。** 实测发现 —— 我把中文写在 `.bat` 的 `rem` 注释里，运行时报：

```
'??关闭，以便看清??' is not recognized as an internal or external command
'?异常，请先检查这一行是否还在最前面??' is not recognized as an internal or external command
```

**最小复现**（4 行文件）确认了：

```
rem 中文注释测试   →   '中文' is not recognized as an internal or external command
```

**根因**：`cmd.exe` 执行 `.bat` 时**按字节偏移重新定位文件读取位置**，而该偏移是在**当时生效的代码页**下算出来的。文件中间一句 `chcp 65001` 会让后续所有行的偏移错位 —— 症状就是**行首的 `rem` 关键字丢失、注释被当作命令执行**。

**这不是我能绕过的写法问题，是 cmd 的固有行为。** 三条路各测一次：

| 方案 | 实测 |
|---|---|
| `.bat` 里写中文 `rem` | ✗ 被当命令执行 |
| `.bat` 输出中文 `echo` | ✓ **字节正确**（`E6 89 8B` 完整）—— 但一旦同文件里再有中文 `rem` 就坏 |
| `.ps1` 里写中文（无 BOM） | ✗ **连读取都失败**：`$text = "街机刷新率显示助�?` 未闭合，脚本语法报错 |
| `.ps1` 里写中文（**有 BOM**） | ✓ **逐字节正确** |

最后一行是关键：**无 BOM 时 Windows PowerShell 按系统代码页读取脚本**，所以 `.ps1` 里的中文必然损坏；加 UTF-8 BOM 后 `实际字节 == 期望字节`。

### 因此定下的架构

| 文件 | 编码 | 内容 |
|---|---|---|
| `ArcadeDisplay.bat` | **纯 ASCII**（0 非 ASCII 字节） | 启动器，传 `-Lang en` |
| `ArcadeDisplay-zh.bat` | **纯 ASCII** | 启动器，传 `-Lang zh` |
| `ArcadeDisplay.ps1` | **UTF-8 带 BOM** | 全部本地化文本 |

**所有非 ASCII 文本集中到 `.ps1`，`.bat` 保持纯 ASCII** —— 这样彻底消除 cmd 那类问题，而两种语言的前言**并排放在同一个 `$Text` 哈希表里**，也不会各自漂移。顺带一个好处：**给维护者看的注释是英文、给用户看的输出才分语言**，本来就更合理。

`-Lang` 参数的注释里专门写了为什么它存在（"因为一个 cmd 限制，而不是为了整齐"），并写明 `.bat` 为什么必须纯 ASCII —— 否则后来者很可能"顺手"把中文加回 `.bat`。

### 验证

| 项目 | 结果 |
|---|---|
| `.ps1` 的 BOM | ✓ 有（438 个非 ASCII 字节） |
| 两个 `.bat` 的非 ASCII 字节 | ✓ **0** |
| 中文启动器输出 | ✓ `20 20 ... E6 89 8B` 逐字节正确 |
| `rem` 误执行 | ✓ 不再出现 |
| 参数透传（`-NoPause`） | ✓ 已被 `.bat` 消化，未传给 PS1 |
| 从发布版运行 | ✓ 两个都能列出模式 |

---

## 三十五·补三：水平频率与"更低垂直频率"是否要考虑

用户问两件事：60Hz 笔记本屏会不会被测成"不支持非标准 Hz"；以及是否要考虑水平刷新率、或 R-Type 那种 55Hz 之类的更低垂直频率。

### 更正：60Hz 屏**大概率被判定为"在范围内"**，而不是不支持

**这一点与直觉相反，值得记下。** 实数：本机 S24F350 是 75Hz 屏，EDID 报 **`50-75 Hz`** —— `57.524` 落在里面。60Hz 笔记本屏的 `0xFD` 通常报 `48-60` 或 `50-60`，**同样包含 57.5**。

原因是 **57.5 只比 60 低 4%**，所以它几乎总落在任何现代面板的垂直范围内。**这个检查很少给出否定结论** —— 它的价值在于极少数"范围下限较高"的面板，以及**那些数字本身**。

**而这次追问暴露了措辞在过度承诺。** 原写「自定义分辨率可创建」，但该检查只能证明**必要条件**：

| 情形 | 可靠性 |
|---|---|
| **不在范围内** | ✓ **可证** —— 面板不会锁定到它未列出的时序 |
| **在范围内** | ✗ **只是必要条件** —— 还取决于**显卡驱动是否开放自定义分辨率**，而笔记本 eDP 面板常常根本不开放 |

**已改为**：

```
57.5Hz 在面板时序范围内——这只是必要条件；能否真正创建还取决于显卡驱动是否开放自定义分辨率
57.5Hz 不在面板时序范围内，该显示器无法接受此时序，创建自定义分辨率无用
```

> 又一个**不对称**：否定可证、肯定只是必要。这与 VRR 那条（声明是正证据、缺失不是负证据）方向相反，但同属"**不要给超出数据支撑的结论**"。而且在"用户最没辙的机器上"（笔记本）过度承诺最伤人 —— 那里既不能建自定义分辨率，也没有别的选项。

### 水平频率：不需要用户配置，而且**隐式满足**

**Model 3 自身的水平频率**（由代码时序算出，`Model3.cpp:2052`）：

```
57.524160 Hz × 424 行（384 活动 + 40 消隐）= 24,390 Hz ≈ 24.39 kHz
```

**这是"中等分辨率"，不是标准分辨率：**

| 档次 | 水平频率 | 说明 |
|---|---|---|
| 标准分辨率（Atari / CPS / Neo Geo / Irem） | **15.72 kHz** | 用户提到的那一类 |
| **Model 3** | **24.39 kHz** | 用自家规格监视器 |
| VGA / 现代 LCD 最低档 | 31.5 kHz | 现代显示器起点 |

**推论**：15.72 kHz 的机台监视器**无法**同步 24.39 kHz —— 当年 Model 3 必须用专用中等分辨率监视器，原因就在此。而现代 LCD 最低 30 kHz 起步，**更不可能接 15.72 kHz**。

**但对我们的用途，水平频率不是用户参数**：我们**不让显示器跑 496×384@57.5**（那才是 24.4 kHz），而是让它在**自己的原生分辨率**下跑 57.5Hz：

```
1600×1200 @ 57.524 Hz → 总行数约 1248 → 水平约 71.8 kHz
```

落在现代 LCD 的 `30-81 kHz` 范围内（S24F350 实测值）。**而且这个关系是自动成立的** —— 显示器本来就在其原生分辨率下以 60Hz 左右运行，水平频率也就在那个量级；把垂直降到 57.5 只是让它低约 4%。所以**只要垂直范围通过了，水平几乎必然通过**。

**这也是不把水平范围放进界面的理由**：它是派生量、不构成独立判据，而且**部分面板报的是退化的值**（G70D 报 `69-69 kHz`，显然不可能支撑它的 4K@120Hz）。展示一个可能是错的数据，比不展示更糟。

### 更低的垂直频率（R-Type 55Hz 之类）：**不适用**

**Supermodel 只模拟 Sega Model 3。** 用户举的例子分属别的平台：

| 例子 | 平台 | 与本文关系 |
|---|---|---|
| 15.72 kHz / ~59.94-60 Hz | Atari / Capcom CPS / Neo Geo | 其他机板 |
| R-Type 384×256 @ 55 Hz | **Irem** | 其他厂商 |

而 Model 3 的速率在**仿真核心里是硬编码**的：

```cpp
// Model3.cpp:2062
unsigned frameCycles = (unsigned)((float)ppcCycles / 57.524160f);
```

**`Games.xml` 里没有任何刷新率字段**，`RefreshRate` 也只提供 57.5 / 60 两档。也就是说**这个项目不存在"每款游戏一个刷新率"的概念** —— PPC 周期预算、音频每帧样本数、驱动板计时、VBlank 全部由 57.524160 派生。加"逐游戏刷新率"会是一次深层改动，而不是加个配置项。

**所以答案是：不需要考虑。** 但把这条理由写进文档，免得以后有人看到"街机有各种刷新率"再来问一遍。

---

## 附：可迁移的调试模式（`FIXES.md`）

本文记录"这个项目做了什么"；**`FIXES.md` 记录"遇到这类问题该怎么查"** —— 把上面这些问题的**根因与定位方法**抽成了按症状索引的案例集，便于在其他项目里参考：

`FIXES.md` 收录 28 个可迁移案例，包括：

- 新功能**完全不可见且无报错**（长度散落多处导致漏改）
- **拷贝后状态丢失**（自定义拷贝构造漏了基类私有成员）
- **启动黑框一闪**（子系统问题，事后隐藏是竞态）
- **裁剪误用 viewport**（画面被缩放而非被裁）
- **脏标记提前清零**（消费型状态要先成功后清除）
- **刷新逻辑放错位置**（状态转换永不触发）
- 第三方后端与核心的**版本按日期配对**
- **多上下文库的空上下文解引用**
- **抓屏**：BitBlt 对现代 GPU 渲染无效，要用 `PW_RENDERFULLCONTENT`
- **输入注入不可靠**，优先"绕过输入"验证
- 几何用**性质测试**代替肉眼（含判定力反向验证）
- **「假通过」的验证**：断言"不存在"必须有阴性对照
- **结构体变长**后所有推送点都要推完整块
- **检查工具本身要先被检查**（ROM 审计工具连续三次给可信的错答案，每次都源于"问错了问题"）
- **两个上限各写各的**（CPU 允许 150 万顶点、GPU 缓冲只装 104 万，缺口随时间累积 → 几何被静默丢弃，表现为贴图黑块）

以及三份自查清单（渲染类问题排查顺序、偶发问题排查顺序、两个上限/约束不一致类）。

---

## 附录：本次改动的文件清单

> 开发与验证的时间/工作量统计见根目录 `DEV-STATS.md`。

| 文件 | 主要改动 |
|---|---|
| `Src/OSD/SDL/Main.cpp` | 独占全屏/G-SYNC、配置项、进程优先级、GUI 外壳、模态循环 hook、按键默认值与预设、**整数倍缩放（`CalcScreenGeometry`）**、**游戏会话结束时恢复鼠标指针**、**`HasArcadeDisplayMode()`**、**`AttachParentConsole()` / `SetConsoleVisible()`（见 十九）** |
| `Src/OSD/SDL/Main.h` | 新增 `SetConsoleVisible()` 声明（见 十九）|
| `Src/OSD/SDL/Main.h` | **`HasArcadeDisplayMode()` 声明** |
| `Src/Inputs/Inputs.cpp`, `Inputs.h` | UI 输入定义（存档 F5 / 读档 F7 沿用原版）、视角单键循环 |
| `Src/OSD/SDL/Gui.cpp` | GUI 常驻（`GuiCreate`/`GuiRunOnce`/`GuiDestroy`）、窗口尺寸记忆、高级设置页、手柄子系统初始化、扳机配对警告、**整数倍缩放开关**、**`HasDedicatedUI` 去重（滤镜选项不再在「画面」页重复）**、**补上 `PostGlowLevels` 控件**、**全屏刷新方式改单选 + 扫描线实际线数提示**、**CRT「推荐配置」下拉 + 拍频实时诊断（见 3.10.5）**、**`UiTextWarn()` 告警色文本助手** |
| `Src/OSD/SDL/Gui.h` | 上述接口 |
| `Src/OSD/SDL/Localization.h` / `.cpp` | **新增**：界面语言（`Tr()` 查表 272 条、游戏名英→中 27 条、版本词元 24 条、语言下拉）、**CRT 预设与诊断行 9 条**、**长帮助文本 20 条（见 7.10）** |
| `Src/OSD/SDL/Gui.cpp` | 游戏列表显示版本号、语言下拉、`Tr()` 包装函数（19 个）|
| `Src/OSD/ConfigFile.h` | 文件模板默认值对齐 |
| `Src/OSD/Audio.h` / `Src/OSD/SDL/Audio.cpp` | 每帧采样数推导、`IsAudioBufferFull()`、缓冲大小可配置 |
| `Src/OSD/SDL/SDLInputSystem.cpp` | 窗口关闭事件处理 |
| `Src/OSD/Windows/DirectInputSystem.cpp` | 窗口关闭事件处理、**`SetMouseVisibility` 改为幂等（Win32 显示计数器）** |
| `Src/Model3/DSB.h` / `Src/Model3/DSB.cpp` | 每帧采样数与 MPEG 帧周期按 RefreshRate 推导 |
| `Src/Model3/SoundBoard.cpp` | 使用 `GetSamplesThisFrame()` |
| `Src/Model3/Model3.cpp` | 异步声板线程、唤醒竞态修复、缓冲满检查 |
| `Src/Graphics/Vulkan3D/Vulkan3D.cpp` / `.h` | MSAA、白闪范围、雾效、overlay 管线、影子解析、**MSAA 解析图像交付修复（`EndFrame`）**、采样数支持性校验、深度 usage 修正 |
| `Src/Graphics/Vulkan3D/Vulkan2D.cpp` / `.h` | 后处理管线、mip 链辉光、合成跳转、截图记录；**机柜背景图（`LoadBezelImage` / `UploadBezel` / `DrawBezelPass`，见 十八）**；**描述符数组改用 `kCompositeLayerCount` 常量（修复布局绑定数量漏改，见 18.11）**；**首次绘制时输出几何诊断**；**`RefreshPostSettings()` 每帧回读参数（实时预览，见 20.5）**；**`DrawLayer()` 改用 scissor 裁剪 2D 层边缘杂线（见 21.2 / 21.2.1）**；**`DrawOverlayPass()` 叠加层渲染钩子** |
| `Src/Graphics/Vulkan3D/VulkanTextureCache.cpp` | **修复**：脏标记不再提前清除、上传失败不再早退、未处理瓦片放回重试（见 21.1）|
| `Src/Graphics/Vulkan3D/BezelLayout.h` | 背景图黑边几何的纯函数（可脱离 GPU 测试）；**修正：`ComputeBezelGameRect()` 由「外扩」改为「内缩」`correction`，与 OpenGL 的 scissor 语义一致（见 21.2）**；**新增 `wideScreen` 参数：宽屏时返回全宽矩形，供合成器 scissor 与背景图几何共用（见 23.8）** |
| `Src/Graphics/Vulkan3D/Vulkan2D.cpp` / `.h` | **新增 `m_wide`（读取 `WideScreen`）并传给 `PictureRect()` / `BezelGameRect()`** —— 此前合成器的裁剪矩形忽略了宽屏设置，导致 3D 渲染出的全宽画面被裁回 4:3（见 23.8）；**宽屏时在 `LoadBezelImage()` 里跳过背景图加载**（无黑边可填，且省一次解码与数十 MB 显存）|
| `Src/Graphics/Vulkan3D/R3DSceneBuilder.cpp` | **新增视口诊断**：每个不同视口输出一次尺寸与"宽屏分支是否生效"，用于区分"功能未生效"与"生效后被裁掉"（见 23.8）|
| `Src/Graphics/New3D/R3DShaderQuads.h` | **移植上游**：`#version 450`→`410`、`cross[4][4]`→扁平 `cross[16]`、discard 简化 —— 使 New3D 只需 **OpenGL 4.1**（原需 4.5），是 Vulkan 回退能在老卡上可用所必需（见 25.1）|
| `Src/Model3/RTC72421.cpp`, `Src/OSD/SDL/Crosshair.h`, `Src/Util/ConfigBuilders.cpp` | **移植上游**：语义等价的现代化清理（`{}` 初始化、`emplace`）（见 25.2）|
| `Docs/README.txt` | **移植上游手册更新**并修正一处：力反馈 Left/Right、PowerPC 频率默认值（原文档写"默认 50"实为 0）；ROMDirectory 条目改回我们的 `ROMPath`（见 25.2 / 25.4）|
| `Src/OSD/SDL/Gui.cpp` | 宽屏开启时**整组禁用背景图控件**并给出说明，让二者的互斥关系可见而非静默失效（见 23.8）|
| `Src/Graphics/Vulkan3D/Shaders/composite.frag` | **`uLayers[3]` → `uLayers[4]`；push constant 增加 `brightness`** |
| `Src/OSD/SDL/Overlay.cpp` / `.h` | **新增**：游戏内设置叠加层（独立 ImGui 上下文 + 官方 Vulkan 后端 + 手写输入喂给 ImGuiIO + 应用/撤销/关闭，见 二十）|
| `Src/OSD/SDL/StatsOsd.cpp` / `.h` | **新增**：游戏内实时统计读数（`` ` `` 开关；刷新率与街机速率对照、混合 CPU 只报性能核、NVML 取 GPU、1% Low，见 三十）；**支持 OpenGL 后端 + 报告实际渲染器**（见 三十二）|
| `Src/Inputs/Inputs.cpp` / `Inputs.h` | **新增 `uiToggleStatsOsd`**（绑 `KEY_BACKQUOTE`）|
| `Src/Pkgs/imgui/imgui_impl_vulkan.cpp` / `.h` | **新增**：官方 Vulkan 后端，**钉在 `4e7c05504a95`**（与本地核心同期，见 20.4）|
| `Src/Pkgs/imgui/imconfig.h` | **新增**：`IMGUI_IMPL_VULKAN_USE_VOLK` + `IMGUI_IMPL_VULKAN_VOLK_FILENAME`（见 20.4）|
| `Src/Inputs/Inputs.cpp` / `Inputs.h` | **新增 `uiToggleOverlay`**（绑 `KEY_TAB`）；**`SetGameInputSuppressed()`**（面板打开时不给游戏喂输入）|
| `Src/Inputs/InputSystem.cpp` | **新增 `s_reservedKeyNames[]`**：Tab 可解析但不可被游戏按键捕获（见 20.9）；**加入 `BACKQUOTE`**（见 三十）|
| `FIXES.md` | **新增**：可迁移的调试案例集（27 个案例 + 症状索引 + 十份自查清单，见 三十一至三十五）|
| `supported-games.txt` | **新增**：63 个 ROM 集 / 27 款游戏的完整英文列表，从 `Games.xml` 程序化生成（含父集依赖标注，见 22.8）|
| `Src/Model3/Model3.cpp` | **诊断**：游戏信息块（含 `Extra Hardware`）由 `std::cout` 改为 `InfoLog`；**驱动板退化为哑板时新增 `ErrorLog` 说明原因（见 22.6）** |
| `Src/Network/SimNetBoard.cpp`, `NetBoard.cpp` | **诊断**：连接提示由 `printf` 改为 `InfoLog`；`NetBoard.cpp` 那处**移出重试循环**（原本每轮都打印）|
| `Supermodel3Bezel.png`（项目根目录） | **机柜背景图的主文件**（2652×1492，16:9，不透明 RGBA）。`build.bat` 优先取这一份，**改这里就够了**（见 18.13）|
| `Src/Graphics/Vulkan3D/VulkanContext.cpp` / `.h` | 截图（swapchain 拷贝 + 延迟回读 + 工作线程）、`TRANSFER_SRC` |
| `Src/Graphics/Vulkan3D/VulkanMemory.cpp` / `.h` | 多重采样目标支持 |
| `Src/Graphics/Vulkan3D/R3DSceneBuilder.cpp` | 视口 |
| `Src/Graphics/Vulkan3D/Shaders/real3d.frag` | mip 金字塔取值、透明剔除、贴花 |
| `Src/Graphics/Vulkan3D/Shaders/post.frag` | **新增**：FXAA、点阵、辉光、增益、**扫描线光束解析积分**、**辉光归一化修正 + 可调阈值** |
| `Src/Util/PNGFile.h` | **新增**：zlib PNG 编码器；**新增 `DecodeRGBA()` / `LoadRGBA()` 解码器（见 十八）**；**补上 `OSD/Logger.h` 包含（原先靠包含顺序侥幸通过）** |
| `Src/Util/GenericValue.h` | **修复**：`ValueInstance` 拷贝构造补上 `SetValueRange()`，`GetValueRange()` 增加 const 重载 —— 修「载入设置后高级设置页变空」（见 十六）|
| `Src/Graphics/Vulkan3D/VulkanCommon.h` | **恢复四个核心/`KHR` 双分派帮手**（`CmdBeginRendering` / `CmdEndRendering` / `CmdPipelineBarrier2` / `QueueSubmit2`）+ `Rendering2EntryPointsAvailable()`（见 三十三）|
| `Src/Graphics/Vulkan3D/VulkanContext.cpp` / `.h` | `ClassifyDevice()`：受理 **Vulkan 1.2 + `VK_KHR_dynamic_rendering` + `VK_KHR_synchronization2`**；按路线分岔特性结构体；入口点校验；日志标明走的哪条路（见 三十三）|
| `Scripts/CompileVulkanShaders.cmd` / `.sh` | `--target-env` 由 `vulkan1.3` 改 **`vulkan1.2`**（SPIR-V 1.6 → 1.5），使 1.2 与 1.3 都能加载（见 三十三）|
| `Scripts/CheckShaderHeaders.ps1` | **新增**：构建前检查着色器源文件是否比 `.spv.h` 新，过期则让构建失败（见 1.1）|
| `Scripts/GenHelpTranslations.ps1` | **新增**：从 `Gui.cpp` 自动提取 `HelpMarker`/`UiTooltip` 的键并注入英文翻译，避免手抄长字符串出错（见 7.10）|
| `Scripts/ArcadeDisplay.ps1` | 57/58Hz 自定义显示模式的查询/保存/恢复 |
| `Scripts/MakeSdlStatic.ps1` | **新增**：把 SDL2 / SDL_net 由 DLL 转为静态库（见 12.3）|
| `Src/Version.h` | **新增 `SUPERMODEL_FORK_VERSION` / `SUPERMODEL_FORK_DATE` / `SUPERMODEL_TITLE_FULL`** —— fork 版本号与上游 `SUPERMODEL_VERSION`（存档兼容用）分开；带版本的显示名（见 24.1）|
| `Scripts/VersionString.ps1` | **新增**：从 `Src/Version.h` 读出 `vX.Y.Z-YYYYMMDD` 供 build.bat 使用，避免版本号写两份（见 24.1）|
| `Scripts/ArcadeDisplay.ps1` | **改为随发布版分发**（复制到 `%DIST%\Scripts\`）；**默认 exe 解析支持发行布局与源码检出两种**（原来只认 `MSVC\x64\Release\`，打包后失效；见 35.1）|
| `build.bat` | **不再清空用户数据**：构建前把 `Data\Saves`/`NVRAM`/`Screenshots`/`Config\Supermodel.ini` 搬到临时目录，构建后放回（见 24.2）；**输出中显示版本号**（见 24.1）；**新增：把 `Scripts\ArcadeDisplay.ps1` 打包进 `%DIST%\Scripts\`**（此前界面与 README 共 5 处引用它，而发布包不含该目录；见 35.1）|
| `Scripts/AuditTranslations.ps1` | **新增第二项检查**：中文字面量是否真的经过 `Tr()`；"会翻译的包装函数"改为**自动推导**而非手工清单（见 24.3）|
| `Src/OSD/SDL/Main.cpp` | 标题统一改用 `SUPERMODEL_TITLE_FULL`；标题缓冲 192 → 256 字节|
| `Src/OSD/SDL/Gui.cpp` | **网络设置从「常用」页移到「网络」页**（抽为 `DrawNetworkSettings`，见 26.2）；**新增"选项适用哪个渲染器"的分类与灰化**（`ScopeOfSetting` / `ScopeAppliesToCurrentRenderer`，见 27.1）；**渲染器选项下方新增叠加层说明**（Tab 仅 Vulkan，见 27.2）；**移除该说明**（OpenGL 后端已支持 Tab，见 32.5）；**固定街机刷新率在无 57/58Hz 模式时真正置灰 + 按帧节流复查可用性**，**G-SYNC 项补上"VRR 范围无法检测"的说明**（见 35.2 / 35.3）；**更正"需要重启"措辞为"重新开一局"**（见 27.3）；设置窗口标题带版本号；**修复 `ImGui::InputText("对方地址##AddressOut")` 未走 `Tr()`**（见 24.3）；**修复游戏列表「搜索游戏」提示未走 `Tr()`**（见 24.3）|
| `Src/OSD/Windows/FileSystemPath.cpp` | **重写**：全部路径锚定 exe 目录、收进 `Data\`、按需建目录、新增 `ResolvePath()` / `GetBasePath()`、**旧布局迁移** |
| `Src/OSD/FileSystemPath.h` | 新增 `ResolvePath()` / `GetBasePath()` 声明 |
| `Src/OSD/Unix/FileSystemPath.cpp`, `Src/OSD/OSX/FileSystemPath.cpp` | 补 `ResolvePath()` / `GetBasePath()` 桩实现（保持原行为）|
| `Src/OSD/Windows/Supermodel.ico` | **应用图标**（6 种尺寸，16→256）|
| `MSVC/SupermodelResources.rc` | **新增**：图标资源（ID 101）|
| `MSVC/Supermodel.vcxproj` | 挂入 `ResourceCompile`；补 SDL_net / SDL 所需系统库；**`SubSystem` 由 `Console` 改为 `Windows`（4 个配置，见 十九）** |
| `MSVC/SDL/SDL.vcxproj`, `MSVC/SDLnet/SDL_net.vcxproj` | **转为静态库**（见 12.3）|
| `build.bat` | **新增**：一键编译 + 组装发行目录；**每次构建跑漏译审计**；**面向玩家的 `Docs\*.txt` 复制到顶层（见 12.5）**；**背景图取项目根目录的主文件，并打印来源与大小（见 18.13）**；**英文 README 重命名为 `README.txt` 放顶层并打印来源（见 12.5.1）** |
| `Scripts/AuditTranslations.ps1` | **新增**：全文扫描中文字面量，核对英文表（见 15.5）|
| `Docs/安装说明.txt` | **新增**：面向玩家的系统要求/使用说明，构建时复制到发行目录**顶层**（UTF-8 BOM，见 12.5）|
| `Docs/README-VK.txt` | **新增 SETTINGS IN DETAIL 章节**：21 条设置详解（由提示词精简而来，见 28.2）；系统需求、回退说明、宽屏说明、生效时机；明确 README 由 `Docs\*.txt` 复制到顶层 |
| `Src/OSD/SDL/Localization.cpp` | **补 20+ 条**并**同步 19 条改写后的提示词翻译**（见 28.2 / 28.3）；回退提示、Tab 叠加层说明、渲染器归属说明、调试输出等条目 |
| `Src/OSD/SDL/Gui.cpp` | 顶部标题行删除、ROM 路径并入左栏、两栏同顶、**按钮宽度自适应 + 页脚两行高度推导**、新增「载入设置」按钮 + 文件对话框、**保存设置拆「默认位置 / 另存为」**、**`FromINIFile` 返回值修正**、**tab 合并（力反馈+灵敏度 / 网络+其他）**、`InputSystem` 列表 XInput 置顶、**4 处漏译修正**、**`GameXMLFile` 载入时按安装路径处理（见 十七）** |
| `Src/OSD/SDL/Localization.cpp` | **补 20 条**（刹车警告、4 个对话框字符串、保存/载入弹窗、页内区块标题）|
| `Src/OSD/SDL/Main.cpp` | `InputSystem` 默认 `dinput` → **`xinput`**；**旧布局迁移调用**（`MigrateLegacyDataLayout()`）；**原 `Misc` 组各项改标为 `Network`**；**`ResolveDataFileSetting()`** |
| `Src/OSD/SDL/Main.h` | 新增 `ResolveDataFileSetting()` 声明（见 十七）|
| `Src/OSD/SDL/Main.cpp` | **渲染器自动回退**：Vulkan 启动失败时记日志 + 发通知 + **清理半成品** + 重试 OpenGL，并恢复被强制的 1x 采样（见 23.2）|
| `Src/OSD/SDL/Gui.h` / `Gui.cpp` | **新增 `GuiPostNotice()`**：在渲染器选项正下方显示回退提示（橙色 + 「知道了」，点单选框即消）（见 23.2）|
| `Src/OSD/SDL/Localization.cpp` | **补 2 条**：回退提示正文 + 「知道了」按钮|
| `Src/Graphics/Vulkan3D/VulkanContext.cpp` / `.h` | **移除 Vulkan 1.2 回退**：删 1.2 实例重试、删 `m_use12Fallback`、删 6 个 `KHR`/`EXT` 扩展请求、设备选择改为 1.3 硬性要求（见 23.4）|
| `Src/Graphics/Vulkan3D/VulkanCommon.h` | **删除四个核心/`KHR` 双分派帮手**（`CmdBeginRendering` / `CmdEndRendering` / `CmdPipelineBarrier2` / `QueueSubmit2`），`CmdTransition` 内改直接调用|
| `Src/Graphics/Vulkan3D/Vulkan3D.cpp`, `Vulkan2D.cpp` | 13 处调用改为直接调用 Vulkan 1.3 核心函数|
| `MSVC/Supermodel.vcxproj` | **给 `Main.cpp` 加 `/utf-8`** —— 该文件首次出现中文字符串字面量（见 23.6）|
| `Src/OSD/SDL/Main.cpp` / `Main.h` | **新增 `GetDisplayTimingRanges()`**：经 WMI 枚举**活动**显示器 → 读注册表 EDID → 解析 `0xFC` 型号与 `0xFD` 垂直时序范围，输出如 `S24F350 50-75 Hz; Odyssey G70D 48-144 Hz`（见 35.9）|
| `Src/OSD/SDL/Gui.cpp` | 同步设置段：新增**「怎么选」一行**（措辞跟随检测结果）；**固定街机项真正置灰**（`BeginDisabled`）+ 可用性改为**按帧节流复查**；**G-SYNC 项补「VRR 范围无法检测」与失败后果**；不可用时**显示 EDID 时序范围**并标注「与 VRR 无关」（见 35.2–35.9）|
| `Src/OSD/SDL/Localization.cpp` | 补 5 条同步相关说明；移除 1 条已失效的「Tab 仅 Vulkan」（见 32.5、35.9）|
| `Src/OSD/SDL/Main.cpp` | 引入 `<wbemidl.h>` / `<oleauto.h>`（复用 `DirectInputSystem.cpp` 已有的 WMI 模式，`wbemuuid.lib` 已链接）（见 35.9）|
| `SYNC.md` | **新增**：57.524Hz 同步兼容性的独立参考 —— 三条同步路线的硬件要求、能/不能检测什么及原因、实测数据、**未验证断言清单**、用户验证清单（见 35.9、35·补三、35.12、35.13）|
| `Scripts/ArcadeDisplay.bat` / `ArcadeDisplay-zh.bat` | **新增**：中英文启动器（**纯 ASCII**），供双击使用；PS1 无法双击运行且受执行策略限制（见 35.12）|
| `Scripts/ArcadeDisplay.ps1` | 加 **UTF-8 BOM**（无 BOM 时 PowerShell 按系统代码页读取，中文必坏）；新增 `-Lang` 参数与双语前言；默认 exe 解析支持发布布局（见 35.1、35.12）|
| `Src/OSD/SDL/Main.h` / `Main.cpp` | **新增多显示器支持**：`DisplayInfo` / `GetDisplays()`（含 SDL 索引 ↔ `\\.\DISPLAYn` 配对）、`GetConfiguredDisplaySdlIndex()`、`ReadPanelMode()`（取面板真实模式，绕开 DPI 虚拟化）—— 见 35.13 |
| `Src/OSD/SDL/Main.cpp` | **模式查找按显示器参数化**：`FindArcadeDisplayModeImpl(rate, displayIndex, ...)` / `FindArcadeDisplayMode` / `HasArcadeDisplayMode` —— 自定义 57.524Hz 时序属于某一台的模式表，问错显示器会把存在的模式报成不存在 |
| `Src/OSD/SDL/Main.cpp` | **新增 `DisplayNumber` 配置 + `-display` 命令行**；`ResolveFullScreenMode()` 用它；GL/Vulkan 建窗用 `SDL_WINDOWPOS_CENTERED_DISPLAY`，窗口位置默认也跟随所选显示器 |
| `Src/OSD/SDL/Main.cpp` | **全屏窗口尺寸修正**：新增 `ApplyWindowSize()` / `GetConfiguredFullscreenSize()` / `LogWindowSize()`；全屏取显示器尺寸而非渲染分辨率 —— 见 35.13 |
| `Src/OSD/SDL/Gui.cpp` | **新增「输出显示器」下拉**（多于一屏时才显示，列出编号/型号/分辨率/该台是否可用 57/58Hz）；`GetArcadeDisplayInfo()` 在所选显示器变化时立即重算可用性 |
| `Src/OSD/SDL/Localization.cpp` | 新增 4 条：输出显示器、主显示器（默认）、57/58Hz 可用、及选择说明；并更新「上列是全部已连接的显示器」一句 |
| `Scripts/ArcadeDisplay.ps1` | **新增 `-Monitor`**：按 Windows 编号列出/切换指定显示器；`Save`/`Restore` 记录设备名；`Run` 把 `-display=<n>` 一并发给模拟器 —— 见 35.13 |
| `Src/OSD/SDL/Gui.cpp` | **OpenGL 下不再猜显卡**：改为在设置窗口的 GL 上下文建立后读 `GL_RENDERER` 显示实测值 + 日志；删除那个只有一项、名字从 Vulkan 列表猜来的置灰 Combo；换卡指引指向 Windows 图形设置（见 35.14）|
| `Src/OSD/SDL/StatsOsd.cpp` | **帧节奏增加目标数值**：新增 `NearestMultiple()`，不均时报出 `even at Nx = ... Hz`；并把该结论写入日志（面板速率截图读不到）—— 见 35.14 |
| `Src/OSD/SDL/StatsOsd.cpp` | **"无边框不能 VRR"降级为前提**：`Sync:` 改为 `borderless (panel keeps its own rate)`，`Cadence:` 标注前提并说明驱动窗口 VRR 会覆盖它；新增 `StatsOsdPanelRefreshHz()` 访问器 —— 见 35.15 |
| `Src/OSD/SDL/Main.cpp` | **同类断言一并限定**（枚举注释、`ResolveFullScreenMode` 注释块、`Frame cadence:` 日志与通知文案）：改为"默认如此，除非驱动对窗口输出也启用 VRR"—— 见 35.15 |
| `Src/OSD/SDL/Gui.cpp` | 选中「无边框全屏」时新增说明：窗口 G-SYNC 可能已生效、程序无法检测、以显示器实时刷新率为准 —— 见 35.15 |
| `Scripts/ArcadeDisplay.ps1` | 头部注释同步限定（原文把"无边框不能 VRR"写成绝对）—— 见 35.15 |
| `Scripts/ArcadeDisplay.ps1` | **修 `-Action List` 在单显示器机器上崩溃**：`Get-Monitors` 返回单元素时被 PowerShell 展开成标量，`Set-StrictMode` 下 `.Count` 抛错 —— 包 `@()` 并在注释里写明返回契约（见 35.18）|
| `Src/OSD/SDL/StatsOsd.cpp` | **独占 VRR 也报帧节奏**：`ClassifyCadence()` 纳入 `ExclusiveVrr`（显式排除独占固定率，否则 57 vs 57.5242 会把 1:1 误报成不均）；措辞与日志按模式区分；`Even` 档在独占 VRR 下不加限定语 —— 见 35.19 |
| `Src/OSD/SDL/Gui.cpp` / `Localization.cpp` | G-SYNC 提示里补一条：多数笔记本内屏不支持 VRR，这类机器上独占 VRR 与无边框等价 —— 见 35.19 |
| `Src/Graphics/Vulkan3D/Vulkan2D.cpp` | **修描述符池短缺**：`sizes[1].descriptorCount` 由 3 改为 `kCompositeLayerCount`（4）；注释里"必须一致的三处"补全为四处；**给 `CreatePipeline()` 的 5 个静默失败点各加 VkResult 日志** —— 见 35.20 |
| `Src/OSD/SDL/Audio.cpp` | **`CloseAudio()` 加 `s_audioOpened` 守卫**（未开启时安全返回、可重复调用）；新增该静态标志并在设备打开成功后置位 —— 见 35.21 |
| `Src/OSD/SDL/Main.cpp` | **`QuitError` 路径补上 `CloseAudio()`**：此前错误退出不释放音频设备，导致同一进程内后续所有会话都因 "Audio device is already opened" 失败 —— 见 35.21 |
| `FIXES.md` | 新增案例 36、37（被宽容驱动掩盖的规格违规；错误路径未归还资源导致故障放大）|
| `Src/Graphics/Vulkan3D/R3DSceneBuilder.cpp` | **修日志洪水**：去重原为 `static uint32_t s_lastKey`（只记最近一个），在视口振荡时等于每帧打印 —— 11830/12014 行。改为 `std::set<std::tuple<int,int,int,int>>`（精确无碰撞），实测 11830 → 2 行，总行数 12014 → 382 —— 见 35.22 |
| `Tools/Remote/` | **新增开发工具（不进发布）**：`Get-LaptopLog.ps1`（取笔记本日志并归纳会话：渲染器/同步/读数/设置/**首个错误**，并检测日志是否写完）、`Deploy-ToLaptop.ps1`（部署到笔记本、备份为 `.prev`、不碰 `Data\`）、`Test-CleanExit.ps1`（远程触发优雅退出以复现清理期崩溃）。`.gitignore` 已加 `Tools/` |
| `Src/OSD/SDL/StatsOsd.cpp` | **修 1x 被判不均**：`nearest >= 2.0` → `>= 1.0`（1x 是完美 1:1，却被排除），并为 1:1 单独措辞 —— 见 35.23 |
| `Src/OSD/SDL/StatsOsd.cpp` | **`Cadence:` 压成一行**：删掉"为什么/怎么办/前提"的说明文（前提与 `Sync:` 行重复），只留判定+倍数+可执行数值，5 行 → 1 行；**日志保留完整措辞** —— 见 35.25 |
| `Src/Graphics/Vulkan3D/Vulkan3D.cpp` | `~CVulkan3D` 分阶段加日志（textures/targets → buffers → pipelines/shaders），使清理期崩溃可定位 |
| `Src/OSD/SDL/Main.cpp` | 会话退出路径加阶段标记（2D 渲染器 → 3D 渲染器 → 返回设置界面），用于区分"崩在清理"与"崩在别处" |
| `Src/OSD/SDL/Localization.cpp` | 新增 4 条（OpenGL 无显卡选择说明、实际渲染卡、换卡指引），移除 4 条随旧实现失效的条目 |
| `Src/OSD/SDL/Main.h` | 新增 `DisplayInfo::primary`（来自 `MONITORINFOF_PRIMARY`，非 Windows 下取 SDL 索引 0）—— 见 35.16 |
| `Src/OSD/SDL/Main.cpp` | 日志标注 `[primary]`；**修掉自身缺陷**：显示器两行日志原写在每 2 秒过期的缓存重建路径里 → 改为按内容签名比对，仅在列表变化时打印（见 35.16）|
| `Src/OSD/SDL/Gui.cpp` | **输出显示器按角色标注**（主显示器/副显示器 + `#n`），并修正配置为 0 时下拉未选中主屏的问题；**6 处长段说明文字收进 `(?)` 提示**，界面只留硬件读数与一行结论 —— 见 35.16 |
| `Src/OSD/SDL/Localization.cpp` | 新增 11 条、改写 5 条、移除 14 条随文案收编而失效的条目（表 391→388 键）|
| `SYNC.md` | 第 9.2 节改为角色标注说明；第 2.1 节（无边框与窗口 G-SYNC）、第 10 / 11 节（高刷对齐、显卡选择）—— 见 35.14～35.16 |
| `Src/OSD/SDL/StatsOsd.h` / `.cpp` | **新增 Render 行**：`StatsRenderInfo` + `StatsOsdSetRenderInfo()`，显示实际渲染分辨率与生效的 SSAA/MSAA 倍数（与输出不同时增印 `scaled to`）；并写入日志 —— 见 35.17 |
| `Src/OSD/SDL/Main.cpp` | 新增 `ReportRenderInfo()`，在两个 `AttachRenderers` 点（首次启动、分辨率变更重建）把实际值推给读数 —— 见 35.17 |
| `Src/Graphics/Vulkan3D/Vulkan3D.h` | 新增公开访问器 `SampleCount()`，供读数报告**实际获准**的 MSAA（私有 `m_sampleBits` 可能因驱动不支持而降为 1）—— 见 35.17 |
| `SYNC.md` | 第八节读数表扩充为全部行，并新增 `Render:` 一节的解释（含 SSAA 与 MSAA 的区别）—— 见 35.17 |
| `FIXES.md` | 新增案例 29～33（全屏裁切、未验证前提被推翻、推测替代测量、界面信息分流、以二进制核对字符串）|

### 35.13 多显示器：编号配对、选择，以及全屏尺寸修正

**起因**：`Main.cpp` 里的 `const int display_in_use = 0;  // only using the first display` 是上游硬编码 —— 模拟器全屏永远去**主显示器**；`ArcadeDisplay.ps1` 也全程传 `NullDevice`（Win32 里意思就是"当前/主显示器"）。而 57.524Hz 自定义分辨率往往建在**副屏**上（主屏常是 60Hz 办公屏）。两边一致地"看不见副屏"。

**编号桥**：SDL 用 **0 起索引**，Windows 用 **`\\.\DISPLAYn`**（从 1 起且**会跳号** —— 本机是 1 和 5）。两者无法互推：实测 `\\.\DISPLAY5`（4K 主屏）在 SDL 里是 **index 0**。桥接只能用几何匹配：`SDL_GetDisplayBounds` 的矩形 ↔ `GetMonitorInfo` 的 `rcMonitor`，这是两边唯一共有的字段。

配对失败时全部编号回退为 SDL 顺序 `1..n` 并在日志注明 —— 混着"真实编号与 0"的列表无法存进一个设置，因为 0 同时表示"未选择"。

**顺带发现的真 bug：全屏窗口比屏幕大**（用户报告副屏画面上下被裁）

两条**互相独立**的根因：

| | 根因 | 主屏为何看不出来 |
|---|---|---|
| **A** | 渲染分辨率（6x 超采样 = 2976x2304）被**同时当作全屏窗口尺寸** | 全屏切换会顺手把窗口缩到屏幕大小 |
| **B** | 全屏 `SDL_SetWindowDisplayMode` 用的是 `SDL_GetDesktopDisplayMode` 的尺寸，副屏报 **2880x1620** —— **面板上不存在这个模式** | 若尺寸本来就小于屏幕，看不出 |

`2880x1620 = 1920x1080 × 1.5`，而 1.5 是**主屏**的缩放。进程只声明 system DPI aware（`SDL_HINT_WINDOWS_DPI_AWARENESS="system"`），Windows 于是把**与系统 DPI 不一致**的显示器几何换算进系统 DPI 空间；主屏 4K@150% 定义了系统 DPI，副屏 1080p@100% 被换算。SDL 的 `dpi_scaling_enabled` 只在 `SDL_HINT_WINDOWS_DPI_SCALING` 为真时开启（默认关，本项目没设），所以**原样透传**。

结果链：模式设置失败 → 全屏切换时窗口尺寸不更新 → 保留 2976x2304 → 居中 → **上下各裁 ≈612 px**。

**修复**：

- `ApplyWindowSize()` —— 全屏取**显示器尺寸**；窗口模式仍取请求分辨率（那时它确实就是一个窗口尺寸）。
- `ReadPanelMode()` —— 用 `EnumDisplaySettings(ENUM_CURRENT_SETTINGS)` 取**面板真实模式**，替换 SDL 的 w/h，**保留其刷新率**。
- `LogWindowSize()` —— 把 `window size` 与后端 `swapchain` 并列打印（两者不一致就是这个 bug 的签名）。

**验证**（同一份 ini，只换显示器）：

| | 设置的模式 | 全屏窗口 | |
|---|---|---|---|
| 副屏 修复前 | 2880x1620（不存在） | 2976x2304 | ✗ 裁切 |
| 副屏 修复后 | **1920x1080** | **2880x1620** | ✓ |
| 主屏 修复后 | 3840x2160 | 3840x2160 | ✓ 未变 |

**界面**：「输出显示器」下拉**只在多于一屏时出现**（单屏列出一项等于暗示一个不存在的选择）。EDID 读数那段补了一句"上列是全部已连接的显示器；本程序实际使用的是所选那台" —— 否则用户会以为列出的每一台都能用。

**脚本**：`-Monitor` 按 Windows 编号列出/切换；`Save`/`Restore` 记录**设备名**故能还原回同一台；`Run` 把 `-display=<n>` 一并发给模拟器。**只填一边不会报错，只会静默退回桌面刷新率** —— 所以两边必须填同一个编号。

**踩到的 PowerShell 坑**：给参数加 `[string]` 类型后，`[NullString]::Value` 默认值被**转成空字符串**，而 `EnumDisplaySettings("")` 会被拒 → 默认路径直接抛 "Unable to read the current display settings."。改为统一用**空字符串**表示"当前显示器"，只在 P/Invoke 调用前那一刻换成 `NullString`。（原注释早已警告过这个转换，是加类型约束才触发的。）

> 详见 `FIXES.md` 案例 29，以及 `SYNC.md` 第九节。

### 35.14 高刷新率对不齐，以及 OpenGL 下不该猜显卡

**问题一：240Hz / 480Hz 能对齐街机频率吗**

**只在无边框模式下才成立的问题** —— 两种独占模式让面板跟随模拟器，不存在整除关系。判定为 `面板Hz ÷ 模拟Hz` 是否整数（容差 1%，倍数 ≥2）。

结论是**高刷新率完全不帮忙**：57.524160 不是整数，所以整数倍 115.04832 / 172.57248 / **230.09664** / 287.62080 / 345.14496 / 460.19328 Hz 全都不是标准刷新率。实测：**240Hz 是 4.17x、480Hz 是 8.34x，都不均**；而 **230Hz 是 3.998x，反而均匀**。

**改进**：读数原本只报"不均"，用户无从下手。新增 `NearestMultiple()`，把目标数值一并给出，可直接填进自定义分辨率对话框：

```
Cadence: uneven (2.09 panel scans per frame) - frames repeat
         unevenly; an exclusive sync mode removes this
         even at 2x = 115.00 Hz
```

同时写日志（面板速率从截图读不出来，远程排查需要）：

```
Stats OSD: cadence uneven - 2.0870 panel scans per frame; even at 2x = 115.0000 Hz.
```

> 注意基数用的是 `RefreshRate`（57.5）而非 `kArcadeHz`（57.524160），与 `ClassifyCadence()` 一致 —— 帧节奏取决于**实际节拍速率**，2x 因此是整洁的 115.00Hz。

**问题二：为什么 OpenGL 下不能选显卡**

**OpenGL 没有设备枚举接口。** Vulkan 由应用枚举并选择 `VkPhysicalDevice`；GL 上下文属于窗口，由驱动决定哪块适配器服务它。本项目只能通过两个**进程级导出符号**"请求"独显（`NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance`，`Main.cpp` 顶部），驱动在**加载时读取一次**，进程内不可更改 —— 所以那格的 Combo 只有一个选项，即便启用也切不动。

**但原实现显示的是推测值**：从 **Vulkan** 设备列表按 `NVIDIA`/`AMD` 关键字匹配出的名字，标注"已锁定独显"。而实际渲染的卡本来就测量过（`glGetString(GL_RENDERER)`）。**把已知事实换成推测**，一旦标志被驱动忽略（可以）或被动 OS 按应用设置覆盖，界面会显示错误的卡而用户无从察觉。

**改进**：设置窗口建立自己的 GL 上下文后读取 `GL_RENDERER`，显示实测值 + 日志记录；并把"如何换卡"指向 Windows 图形设置，同时**明说该设置是否对 OpenGL 生效由驱动决定、程序无法检测**。

实测（本机双显卡 RTX 5060 Ti + Intel Arc）：

```
OpenGL: settings window context is on "NVIDIA GeForce RTX 5060 Ti/PCIe/SSE2".
```

New 3D 与 Legacy 3D 都落在独显上，说明两个标志有效 —— 但这是**测出来的**，不是猜的。

**顺带发现的不对称**：有 `-vulkan3d` 却没有 `-no-vulkan3d`，命令行无法切回 OpenGL（只能改 ini 或在界面切换）。

> 详见 `SYNC.md` 第十、十一节。

### 35.15 "无边框不能 VRR"被驱动选项推翻：6 处断言降级为前提

**起因**：用户指着 NVIDIA 控制面板「设置 G-SYNC」问无边框到底走 120Hz 还是能跟随应用。

**发现**：NVIDIA 可以把 G-SYNC 应用于**窗口输出**（「为窗口和全屏模式启用」；驱动默认「仅全屏模式」）。开了它，无边框（在 DWM 看来仍是窗口）也能让显示器跟随应用 —— **我们 6 处"borderless cannot use VRR"的断言在非默认配置下不成立**：

| 位置 | 原文 |
|---|---|
| `Main.cpp:241` | `(VRR cannot engage)` |
| `Main.cpp:615-616` | `Borderless fullscreen cannot do either, because Windows DWM...prevents VRR from engaging` |
| `Main.cpp:2652` | `Borderless fullscreen cannot use VRR, so the panel keeps its own rate` |
| `StatsOsd.cpp` 注释 | `Borderless fullscreen cannot use VRR - the desktop compositor keeps presenting...` |
| `StatsOsd.cpp` 字符串 | `"borderless, VRR off"` |
| `ArcadeDisplay.ps1` 头部 | `...does not engage in windowed or borderless fullscreen because DWM...` |

**不只是注释问题**：`Cadence:` 行**只在无边框下出现**，算 `120/57.5 = 2.09 → 不均`。若窗口 VRR 已生效，面板跟着应用走、画面可能完美，**我们却报了不存在的问题**。

**修不了逻辑，只能改措辞** —— 两种情况对应用无可观测差异（都以相同速率呈现、FPS 相同），这是继案例 22、29 之后第三次撞上"没有信号"的边界：

```
Sync: borderless (panel keeps its own rate)      ← 原 "borderless, VRR off"
Cadence: uneven if panel @ own rate (2.09 scans per frame)
         (assumes the panel is at its own rate; driver windowed
          VRR would override this - check the display's readout)
```

**加提示**（用户选的 `qualify_plus_hint`）：
- 界面：选中「无边框全屏」时，下方新增说明 —— 指出窗口 G-SYNC 可能已生效、程序无法检测、**唯一验证途径是显示器 OSD**（~57-58Hz = 已生效）
- 日志：`Frame cadence:` 行补上前提与"检测不到"的说明，并打印面板速率（为此新增 `StatsOsdPanelRefreshHz()` 访问器，避免在 `Main.cpp` 里重复查 SDL 造成两个来源）
- 文档：新增 2.1 节（含 NVIDIA 两个选项对照表），7.2 节登记为**未验证**（选项确实存在、按 DWM 语义应当适用，但我读不到显示器 OSD，故未实测确认）

**实测确认新措辞生效**（无边框 + 120Hz）：

```
Frame cadence: borderless fullscreen, and the panel rate (120 Hz) does not divide evenly into the
emulated rate, so frames are shown unevenly if the panel is running at its own rate. An exclusive
sync mode on the Common tab removes this. Note that a driver applying VRR to windowed output would
remove it as well; this cannot be detected from here, so check the display's own refresh readout.
Stats OSD: viewport ...; sync borderless (panel keeps its own rate), rate 57.5000 Hz (arcade 57.5242), panel mode 3840x2160@120 Hz.
```

**教训**：**"不可能"通常是"在某前提之下不可能"，而前提常常不在自己的代码里。** 并且同一个信念被复制到 6 处，说明它是**一个前提**而非一处笔误 —— 修的时候要按同一句话全文搜。

> 详见 `FIXES.md` 案例 31、`SYNC.md` 第 2.1 / 7.2 节。

### 35.16 界面上只留数据，解释收进 ? 提示；显示器按角色标注而非编号

**起因**（用户两条反馈）：设置页里长段说明文字占满界面、显得乱；「输出显示器」把主屏显示成"5 号"。

**问题一：把解释当界面元素**

界面上原本堆了 6 处多行段落（输出显示器说明、怎么选、无边框例外、G-SYNC 要求、固定率不可用说明、OpenGL 换卡指引）。它们都**正确**，但**正确不等于该常驻显示** —— 每加一条诊断，界面就多一段散文，读设置变成了读文档。

**原则**：**界面只放"数据"和"必须知道的一行"，解释放 `(?)`。**

| 留在界面 | 收进提示 |
|---|---|
| 用户的硬件读数（`S24F350 50-75 Hz`、`实际渲染卡：NVIDIA...`） | 这些数字**意味着什么** |
| 一行结论（`→ 57.5Hz 在时序范围内...`、`未检测到 57/58Hz 模式，此项不可选`） | 为什么、怎么办 |
| 控件本身 | 背景、例外、注意事项 |

> **注意有一处不能收**：之前用户**明确要求**"把检测到的显示器型号和 Hz 高亮显示"。所以硬件读数**留在界面上**，只把解释移走。这是"数据 vs 解释"这条线的用处 —— 它让"该不该收"有判据，而不是凭感觉。

**问题二：把内部标识当成用户语言**

`输出显示器` 里显示的是 `5: Odyssey G70D ...` —— 编号来自 Windows 的 `\\.\DISPLAYn`，**会跳号**（本机主屏就是 DISPLAY5，因为编号是历史累积的）。**结果是最重要的那台显示器看起来像"例外"。**

编号**必须**存在（存入配置、传给脚本，且它是唯一在"切换主屏"后仍稳定的名字），但**不该是标签的主语**。改为：

```
自动 · 跟随系统主显示器
主显示器 · Odyssey G70D  3840x2160 @120Hz  · #5
副显示器 · S24F350  1920x1080 @60Hz  · #1
```

角色在前（`MONITORINFOF_PRIMARY` 是权威来源，不再靠 SDL 顺序推断），编号在后以 `#n` 形式保留可用性。**"副显示器"只在恰好一非主屏时使用** —— 3 台以上时没有唯一"副屏"，编造序号比不标更糟。

顺带修掉一处默认值显示错误：配置为 0（"跟随主屏"）时 combo 原先指向**第一项**，而第一项只是碰巧是主屏；现在显式选中标了 primary 的那项。

**验证**：截屏确认（`PrintWindow` + 放大 6 倍读字）。

```
主显示器 · Odyssey G70D  3840x2160 @120Hz  · #5
```

日志同步标注 `[primary]`：

```
Display 1: S24F350  1920x1080 @60 Hz  (Windows space 2880x1620)
Display 5: Odyssey G70D  3840x2160 @120 Hz  [primary]
```

**顺带修掉的自身缺陷**：显示器那两行日志原本写在缓存重建路径里，而缓存每 2 秒过期一次 → **每 2 秒重复打印一遍**。改为按内容签名比对，**仅在列表真正变化时才打印**（插拔显示器仍会记录）。实测 14 秒内只打印一次。

**教训**：**"正确"和"该显示"是两件事。** 诊断信息越写越准的副作用，是界面越写越长；需要一个判据来分流，而"这是数据还是解释"是个够用的判据。另外**内部标识（编号、ID、端口名）不要当标签主语** —— 它对程序必需，对用户无用，还会让最常见的那一项看起来最反常。

> 详见 `SYNC.md` 第 9.2 节、`FIXES.md` 案例 32。

### 35.17 统计读数增加 Render 行：实际渲染分辨率与实际生效的抗锯齿

**起因**（用户反馈）：左上角只显示 `Panel:`（面板模式），看不到实际渲染多少像素、用了什么抗锯齿。

**要回答两个问题**，而它们都**不能用配置值回答**：

| 问题 | 为什么配置值会错 |
|---|---|
| 渲染分辨率是多少 | OpenGL 下是 `XResolution × Supersampling` 的**乘积**；Vulkan 下 `Supersampling` 被**完全忽略**（日志：`supersampling is not available in this backend; using 1x`）；全屏下又跟随显示器而非 `XResolution` |
| 抗锯齿开了几倍 | MSAA 可能被驱动**拒绝**（色深/深度格式不支持该采样数时降为 1x） |

所以沿用本项目一贯的做法：**从初始化点把实际值推给读数**，而不是让读数去读配置。新增 `StatsRenderInfo` + `StatsOsdSetRenderInfo()`，在 `Main.cpp` 的两个 `AttachRenderers` 点（首次启动、分辨率变更重建）调用 `ReportRenderInfo()`。

**数据来源（都是权威处）**：

- **OpenGL**：`SuperAA::Init(totalXRes, totalYRes)` 内部建 `width*m_aa × height*m_aa` 的 FBO —— 直接读它确认了渲染缓冲确实是 `totalXRes*aa`，输出是 `totalXRes`。`m_aa` 即 `aaValue`。
- **Vulkan**：`s_vkContext->Extent()`（交换链尺寸）+ `CVulkan3D::SampleCount()`（**实际获准**的采样数）。后者新增的公开访问器 —— `m_sampleBits` 原为私有；没有把它加进 `IRender3D`，因为三个实现里只有一个有话说。

**读数样式**（`SSAA` / `MSAA` 明确区分，因为两者做的事不同）：

```
Render: 2976x2304  MSAA 4x                                  ← Vulkan
Render: 11904x9216  SSAA 4x
        scaled to 2976x2304                                 ← OpenGL（仅当与输出不同才打印第二行）
Render: 2976x2304  no AA
```

**这一行立刻暴露了一个此前看不见的事实**：用户 ini 里 `XResolution=2976` 与 `Supersampling=4` 在 OpenGL 下叠加 → **实际渲染 11904x9216**（1.1 亿像素/帧）。这正是 OpenGL 路径比 Vulkan 慢得多的直接原因，而设置界面里显示的是 2976（那是输出分辨率）。

**验证**（屏幕 + 日志双向）：

```
Vulkan : Render: 2976x2304  MSAA 4x
New 3D : Render: 11904x9216  SSAA 4x / scaled to 2976x2304
日志   : Stats OSD: render 11904x9216, anti-aliasing SSAA 4x, scaled to 2976x2304.
         Stats OSD: render 2976x2304, anti-aliasing MSAA 4x at 2976x2304.
```

屏幕读数通过 `PrintWindow` 截屏 + 琥珀色分离后放大读取确认，与日志一致。

**同时把该行写入日志** —— 与 `Panel:` 同理，屏幕读数默认关闭且截图难以辨认，而"为什么这么慢 / MSAA 为什么没用"正是需要日志回答的问题。

> 详见 `SYNC.md` 第八节。

### 35.18 脚本在多显示器正常、单显示器崩溃：PowerShell 的集合展开

**症状**：用户笔记本上双击 `ArcadeDisplay-zh.bat` → `The property 'Count' cannot be found on this object`（`PropertyNotFoundStrict`）。同一脚本在两屏台式机上正常。

**根因**：PowerShell 函数返回时**展开集合** —— `Get-Monitors` 返回 `List[object]`，一台显示器时被展开成标量，`Set-StrictMode -Version Latest` 下 `.Count` 抛错；两台时是 `Object[]` 所以正常；零台时 `$null` 同样抛错。

**修复**：`$monitors = @(Get-Monitors | Sort-Object Number)`。已复现原失败并验证 `@()` 后 0/1/2 台皆正常，真实脚本退出码 0。同时把返回契约写进 `Get-Monitors` 注释。

**教训**：**"在我机器上是好的"要追问差在哪个维度** —— 本例差的是**显示器数量**，而这恰是新功能所依赖的维度，从未在单显示器下被走到过。

> 详见 `FIXES.md` 案例 34。

---

### 35.19 独占 VRR 模式下补回帧节奏：`preconditions met` 不能代替结论

**症状**：笔记本读数同时出现 `Sync: exclusive VRR (preconditions met)`、`Rate: 57.50 Hz`、`Panel: 1920x1080 60 Hz mode` —— 用户读作矛盾。

**澄清**：`Rate:` 是**模拟器配速目标**（`RefreshRate` 设置），不是面板速率；面板速率在 `Panel:`。所以这里有可读性问题，但不是缺陷。

**真正的缺陷**：`Cadence:` **只对无边框绘制**，设计理由是"独占模式要么面板跟随模拟器、要么就是面板速率本身"。**但无 VRR 面板上 `ExclusiveDesktop` 两者都不是** —— 面板钉在 60Hz，57.5 帧被套接。这正是无边框的帧节奏问题，读数却不报警。

用户日志两种模式都有，对照极硬：

```
独占 VRR ：（无 Cadence 行）
无边框   ：cadence uneven - 1.0435 panel scans per frame.   （60 ÷ 57.5 ✓）
```

**修法**：`ClassifyCadence()` 纳入 `ExclusiveVrr`，措辞按模式区分（`uneven if VRR is not engaging` vs `uneven if panel @ own rate`）；日志同样标注模式名。**`Even` 档在独占 VRR 下不写"如果"** —— 面板自身速率若为整数倍，VRR 变频与否都均匀，这是更强的陈述。

**显式排除独占固定率**：该模式下面板已被切到 57/58Hz，`SDL_GetDisplayMode` 读到 57，除以 57.5242 得 0.991 → 会把 1:1 的**整数取整误报成不均**。此 guard 为代码级保证，**但两台可用机器都无 57/58Hz 模式，故未实测**。

**另加提示**（用户要求）：多数笔记本内屏不支持 VRR，这类机器上独占 VRR 与无边框等价。

**教训**：**"只在 A 模式适用"要追问 A 的边界之外退化成什么** —— 这里退化成恰好等于 B 模式，而 B 有诊断、A 没有，于是**诊断最强的模式反而最安静**。

> 详见 `FIXES.md` 案例 35、`SYNC.md` 第二、八节。

### 35.20 升级驱动后崩了：被宽容驱动掩盖的描述符池短缺

**症状**：用户升级 Intel UHD 620 驱动（2026-09 版）后，笔记本上**三个渲染器全部闪退**，看不到游戏画面。升级前 OpenGL 可用。

**触发**：新驱动让 Vulkan 首次启动成功（此前是 `no device offers a usable API version`），于是执行了一条该机器上从未走过的路径 → `Vulkan2D: failed to create the composite pipeline.`

**根因**：描述符池比它服务的 layout 小。

```cpp
constexpr uint32_t kCompositeLayerCount = 4;
bindings[1].descriptorCount = kCompositeLayerCount;   // 布局 4
sizes[1].descriptorCount    = 3;                      // 池 3  ← 规格不允许
```

池不足以覆盖 layout → `vkAllocateDescriptorSets` 失败 → `CreatePipeline()` 的**无日志** `return false` → 上层只有一句笼统错误。

**讽刺之处**：该常量的注释**自己列出了"必须一致的三个地方"**（布局、write、shader 数组），**却漏了第四个 —— 描述符池**。`kCompositeLayerCount` 3→4（加 bezel）时池没跟着改。

**为什么从未暴露**：NVIDIA 驱动**容忍**了这个短缺，所以违规长期存在而台式机一切正常。

**修复**：

| 改动 | 位置 |
|---|---|
| 池计数 → `kCompositeLayerCount`；注释"三处"→"四处" | `Vulkan2D.cpp` |
| **5 个静默失败点各加 VkResult 日志**（下次一行定位） | `Vulkan2D.cpp` |
| 全树扫描同类配对（后处理 1/1、纹理缓存 16/1/1/2）—— **仅此一处不符** | — |

**教训**：**"在我机器上是好的"可能字面为真，代码仍然违规。** 驱动对规格的执行力度不同，违规代码在 A 卡正常、B 卡必崩 —— 这不是环境问题，是被掩盖的缺陷。另外：**"多处必须一致"的注释要列全**，本例漏掉的正是唯一出错的那处。

> 详见 `FIXES.md` 案例 36。

---

### 35.21 一个故障放大成"全部不能用"：错误路径没关音频设备

**症状**（同一事件）：第一次闪退后，切渲染器再试，**三个模式都闪退**。后续尝试**根本没到渲染阶段**：

```
[Error] Unable to open 44.1KHz 4-channel audio with SDL: Audio device is already opened
```

而第一次会话音频**成功打开**了。

**根因**：`OpenAudio()` 之后所有错误出口都走 `goto QuitError`，而 `QuitError` **没有 `CloseAudio()`** —— 渲染器被 `delete` 了，音频设备没有。设备一直开着，而**设置窗口仍在运行**，于是每次切换渲染器/换游戏的新会话都在 `OpenAudio` 处失败。

**修复**：`CloseAudio()` 加 `s_audioOpened` 守卫（可安全重复调用、未开启时直接返回），并在 `QuitError` 中调用。

**教训**：**错误路径必须归还和正常路径相同的资源** —— 这类不对称在正常使用下完全看不出来。而且**"所有选项都失败"时应优先怀疑共用的东西**（音频设备），而非逐个排查选项：症状指向的方向（渲染器）与原因（音频未释放）恰好相反。最后：**残留资源造成的次生错误会盖住根因** —— 第一次的 composite 错误才是真因，却被后面五次 `already opened` 淹没。

> 详见 `FIXES.md` 案例 37。

### 35.22 日志洪水：去重只记住"上一个"，振荡时退化成每帧一条

**症状**：15 秒会话产生 **12014 行**日志，**11830 行（98%）是同一条 viewport 消息在两种状态间反复**。

**根因**：注释写着"once per distinct viewport rather than per frame"，但实现用 `static uint32_t s_lastKey` **只记住最近一个**。场景构建器一帧内走多个视口，于是"与上一个不同"在**振荡时每帧成立** → 每帧打印。

**附带隐患**：键用移位+异或压进 `uint32_t`，**会碰撞**（`495x0` 与 `0x495`）→ 会**静默漏报**（不是本次洪水原因，但一并修正）。

**修复**：改用 `std::set<std::tuple<int,int,int,int>>` + `insert().second`（`R3DSceneBuilder.cpp`）。

**验证**：viewport 行 **11830 → 2**，总行数 **12014 → 382**，两条分别是全屏视口（ACTIVE）与零视口（NOT active）—— **信息零损失**。

**踩的坑**：首版用 `const auto key` + `std::set<decltype(key)>`，`decltype` 得 `const std::tuple<...>`，而 `std::allocator<const T>` 非法 → **编译失败**；当时没看退出码就测试，结果来自**上一版 exe**（数字恰好也变小，形似成功）。**先确认构建成功，再看测试数据。**

**教训**：**"去重"要问"记住几次"** —— 记住上一个 ≠ 记住全部；差别只在状态**来回振荡**时显现，而那正是最难自查的情形。另外**诊断信息要有行数预算**：加日志时通常只问"信息够不够"，忘了问"会写多少行"。

> 详见 `FIXES.md` 案例 38。

### 35.23 排除条件把"最好的情形"排除了：1x 被判成不均

**症状**：笔记本 60Hz 面板 + 60Hz 配速（完美 1:1）时，读数报

```
Cadence: uneven (borderless, panel at own rate) - 1.0000 panel scans per frame
         ↑ 说不均                       ↑ 但 1.00 是"每帧显示一次"
```

**自相矛盾，且附带建议** —— 界面跟着让你去改同步模式，即**去修一个本来没问题的地方**。

**根因**：

```cpp
ratio = 面板Hz / 配速Hz;                        // 60/60 = 1.0
if (ratio < 1.0) return Uneven;                 // 未命中
const double nearest = floor(ratio + 0.5);      // = 1
if (nearest >= 2.0 && ...) return Even;         // ← 1 被排除
return Uneven;                                  // 1.0 落到这里
```

`nearest >= 2.0` 把 **1x 排除在外**，而 1x 正是最理想的情形。排除它的本意是防"略低于 1 的比率四舍五入成 1 被误判为 even"—— **而那个情况已由上面 `ratio < 1.0` 提前拦下。**

**修复**：`nearest >= 1.0`，并为 1x 单独措辞。

**教训**：**写排除条件时，要问"被排除的是不是正好是理想情形"。** 而且**保护性条件要核对它防的那个情况是否已被别处拦住** —— 重复防护会顺手删掉合法输入。

> 详见 `FIXES.md` 案例 39。

---

### 35.24 工具层修复：从正在写入的文件里读结论

**经过**：排查中我自己踩了一次 —— 读到日志尾部停在 `Session: 2D renderer destroyed.`，判定"崩在 `~CVulkan3D`"。**一分钟后重读同一文件**，尾部却是完整的 5 个会话干净退出：

```
Session: 2D renderer destroyed.
Vulkan3D: teardown - textures and targets released.
Vulkan3D: teardown - buffers released.
Vulkan3D: teardown - pipelines and shaders released.
Session: 3D renderer destroyed.
Session: released. Returning to the settings window.
Session: returned with code 0; back to the settings window.
```

差别只是**第一次拷贝时进程仍在写**。

**危害的形状**：截断的尾部与"崩在清理中途"**完全一样**，所以它不只是数据不全，而是**会主动引导出错误结论**。而且它比空文件危险 —— 空文件你知道没数据，截断文件看起来有结论。

**工具层修复**：`Get-LaptopLog.ps1` 现在检查最后一个会话是否有结束标记（`back to the settings window` / `Program terminated normally` / `... due to an error`），三者皆无则明确警告"可能仍在写入，不要据此下结论"。已用完整日志与人工截断版双向验证。

**教训**：**从"正在增长的文件"里读结论前，先确认它写完了。** 并把这层检查**写进工具**，而不是依赖"再读一次"的灵光一现。

> 详见 `FIXES.md` 案例 40。

---

### 35.25 面板该说数据，不该说散文

**起因**（用户反馈）：笔记本全屏时左上角统计面板"出现一堆文字，不符合整个面板简洁的实时数据风格"。

**根因不止一处，而是两处叠加**：

1. **判定错误**（见案例 39）：60Hz 面板 + 60Hz 配速这种**完美 1:1** 被 `nearest >= 2.0` 排除，落进 `Uneven` 分支 → 打印不均已 + "frames repeat unevenly; an exclusive sync mode removes this" + `even at ...` + 两行前提说明 = **5 行文字**。
2. **语气错位**：即使判定正确，`Cadence:` 的措辞也是**说明文**（为什么、怎么办、前提是什么），而不是**读数**。

**修复**：把 `Cadence:` 压成一行，只留"判定 + 倍数 + 可执行数值"：

| 情形 | 改前 | 改后 |
|---|---|---|
| 60Hz + 60Hz | `uneven (1.00x)` + 4 行 | **`even (1:1)`** |
| 120Hz 无边框 | `uneven if panel @ own rate (2.09 scans per frame)` + 4 行 | **`uneven (2.09x)  even at 115.00 Hz`** |
| 120Hz 独占 VRR | `uneven if VRR is not engaging (2.09 scans/frame)` + 2 行 | **`uneven (2.09x)  unless VRR is engaging`** |

**5 行 → 1 行**，三个关键信息（判定 / 倍数 / 目标值或前提）一个没少。

**取舍**：

- **删掉"怎么办"的建议** —— 那是设置界面的事，那里已有完整 `(?)` 提示
- **删掉前提说明** —— 因为 `Sync:` 行已写 `borderless (panel keeps its own rate)`，**重复即噪音**
- **`1.0000 panel scans per frame` → `1:1`** —— 前者读起来像舍入误差，后者一眼是"理想"
- **日志保留完整措辞** —— 日志可以啰嗦，逐帧面板不行

**教训**：**同一个结论，在"实时读数"和"设置界面"里应当有不同详略。** 实时面板会被反复扫视，多一行就是多一次干扰；而它承担的任务只是"把当前状态说清"，不是"教会用户怎么做"。**判断标准是"这行在每秒被看 60 次时，还值得存在吗"。**

> 详见 `FIXES.md` 案例 41。

### 35.26 整数缩放（点对点）在混合 DPI 的机器上算错了倍率

**起因**（用户观察）：笔记本全屏上下黑边"比台式机多不少"，问是不是因为台式机用了更高的原生分辨率。

**先排除变形**：笔记本可见区 992x768，长宽比 **1.2917**，与 496:384 完全一致 —— `CalcScreenGeometry` 里 `xResF = 496*n, yResF = 384*n` 比例恒等。**没有压扁，只是画面小、黑边大。**

**真正原因**：整数倍率是 `floor(min(宽/496, 高/384))`，两台机器都被"高"卡住，但**分母不同**：

| | 笔记本（1080p） | 台式机副屏（同为 1080p） |
|---|---|---|
| 应用看到的尺寸 | 1920x1080 | **2880x1620**（DPI 虚拟化 ×1.5） |
| 倍率 | `floor(min(3.87, 2.81))` = **2** | `floor(min(5.81, 4.22))` = **4** |
| 可见区 / 占屏高 | 992x768 / 71% | 1984x1536 / **95%** |
| 上下黑边 | 156px | **42px** |

**所以黑边少的原因不是"副屏分辨率更高"** —— 两块屏都是 1080p，**差别来自这台台式机主屏 4K@150% 导致的混合 DPI 虚拟化**（与 35.13 那个"画面被裁"同源）。

**而且它让选项名不副实**：4x 是按 1620 算的，面板实际只有 1080 行 → 呈现时 `1536 逻辑行 → 1080 物理行`，**2D 纹素根本没落在整数像素块上**。"点对点"在做与它承诺相反的事。

**修复**：倍率改用**面板真实模式**（`ReadPanelMode()` 已有），窗口尺寸只在读不到时兜底。日志在两者不同时把两个尺寸都打出来：

```
Video: integer scaling 2x - visible area 992x768, panel 1920x1080 in a 2880x1620 window.
```

**验证**：

| 显示器 | 改前 | 改后 |
|---|---|---|
| 4K 主屏（无虚拟化） | `5x - 2480x1920 in a 3840x2160 window` | **不变** ✓ 无回归 |
| 1080p 副屏（虚拟化） | `4x - 1984x1536 in a 2880x1620 window` | **`2x - 992x768, panel 1920x1080`** ✓ |

**取舍要说明**：副屏的黑边会从 42px 变回 156px。**看起来"变差了"，但那才是真实的点对点** —— 之前的"填满 95%"是靠破坏像素对应关系换来的。

**教训**：**选项名承诺的行为，必须用正确的基准量去实现。** 这里三个尺寸容易混（请求分辨率 / 窗口尺寸 / 面板真实尺寸），而"点对点"只对**面板**成立。另外：**"看起来更好"不等于"实现了承诺"** —— 虚拟化坐标让画面更满，代价是承诺落空，而这个代价不会显示在任何数字上。

> 与 `SYNC.md` 第 9.3 节记录的 DPI 虚拟化同源（那次影响的是全屏窗口尺寸，这次影响的是整数倍率）。

---

### 35.27 部署工具谎报成功：改用哈希校验而非退出码

**症状**：`Deploy-ToLaptop.ps1` 在 scp 报 `failed to upload file` 之后仍打印 `done.`。

**根因**：目标机器上 exe 正在运行 → 文件被锁。而脚本的 `if ($LASTEXITCODE -ne 0 -or -not $?) { throw }` **没有触发**。

**修法**：**不追查退出码语义，改看结果** —— 拷完把远端文件读回来比对 SHA256，不一致即报错。这对所有失败模式都有效（文件被锁、路径错、网络断、scp 返回值不可靠）。同时加前置检查：部署前查远端是否有同名进程，有则明确报出并解释"退出游戏只回到设置窗口，关掉窗口进程才结束"。

**实测**：4 个文件逐一哈希校验通过；部署到笔记本后独立复核一致。

> **未验证项已标注**：那道进程守卫**未能成功构造锁定场景来触发**（ssh 里 `start /B` 没拉起进程）。但哈希校验是主防线且已证明有效，守卫是否触发不影响正确性 —— 这一条在 `FIXES.md` 里也明确写了。

**教训**：**错误处理应检查"结果"而非"别人怎么报告失败"。** 退出码是间接证据，文件内容才是直接证据；当间接证据不可靠时，换成直接证据比追查它便宜得多。而且**工具谎报成功比报错更危险** —— 它会让人去测一个不存在的东西，得出"修复无效"的错误结论。

> 详见 `FIXES.md` 案例 42。

---

## 三十六、Lost World 恐龙头部黑块：透明层漏了深度写入

**症状**：`lostwsga` 的 Carnotaurus boss 场景，Vulkan 下吻部/颈部出现多边形黑块，同一存档切 OpenGL 正常。用户描述为"眼睛和面部肌肉都分离了，贴图散掉了"。

### 36.1 定位过程

- **固定帧对比**：墙钟截图会因 Vulkan 启动耗时不同而落到不同模拟帧，无法判定差异来自渲染还是时间。临时在主循环加 `SM3D_SHOT_FRAME` / `SM3D_SHOT_PATH` 钩子，在固定模拟帧调用引擎截图，两个后端才逐像素可比。
- **逐层关闭**：关闭 trans1/trans2 后整只恐龙消失 → 模型完全属于透明层，直接排除不透明层与贴图上传。
- **单项状态对照**：只给透明管线打开深度写入，黑块立即消失。

### 36.2 根因

`CVulkan3D::CreatePipeline()` 里：

```cpp
depth.depthWriteEnable = translucent ? VK_FALSE : VK_TRUE;
```

而 New3D 的 `SetRenderStates()` 是 `glDepthMask(GL_TRUE)`，透明层只是把比较函数换成 `GL_GREATER`，**深度照写**。少了这一步，同一透明层里后绘制的远处多边形会覆盖近处：恐龙嘴内侧的暗色面盖住了吻部，形成纯黑块。

### 36.3 修法

透明管线同样启用深度写入：

```cpp
depth.depthWriteEnable = VK_TRUE;
```

### 36.4 验证

`lostwsga.st5` 第 120 / 300 / 480 帧 Vulkan 与 OpenGL 同帧对比：黑块消失，姿态与贴图一致；`lostwsga.st6` 另一场景无回归。对比图保存在 `Docs/Images/LostWorldBossDepthWrite/`。

**教训**：像素级对比的前提是**同一模拟帧**，墙钟截图会把"渲染差异"和"时间差异"混在一起。另一个可复用的判据是逐层关闭：整只模型消失能立刻排除贴图与不透明层，把范围缩到一个管线状态。

---

*最后更新：2026-10-18*
