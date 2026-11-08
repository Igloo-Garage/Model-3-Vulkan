# Vulkan 后端问题档案

本文件记录 Vulkan 后端排查过的画面问题：症状、复现方式、**被排除的假设**、
根因和修法。目的是下一次遇到相似画面时不用从零再摸一遍。

相关文档：`devdocs/CHANGELOG.md`（每轮改动）、`devdocs/FIXES.md`（通用排错模式）、
`devdocs/TECHLOG.md`（深入技术日志）。

---

## 1. VF3 沙滩：远处海水盖住格斗角色的腿

**状态：已修复（2026-10-23，未 bump 版本）**

### 症状

《Virtua Fighter 3 Team Battle》沙滩关卡，远处的半透明海浪会画在角色**前面**：
Pai / Shun 的腿和裙摆被海水盖住。OpenGL (New3D) 正常。

### 复现

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Capture-Frame.ps1 \
    -Set vf3tb \
    -State "<abs>\build-cmake\bin\Data\Saves\vf3tb_raw.st" \
    -Bin build-cmake/bin -At 6 -Out srf.png
```

对照图：

| 版本 | 文件 |
| --- | --- |
| OpenGL 正确 | `sdl3_reports/images/2026-10-23-vf3tb-sea-gl.png` |
| Vulkan 修复前 | `sdl3_reports/images/2026-10-23-vf3tb-sea-before.png` |
| Vulkan 修复后 | `sdl3_reports/images/2026-10-23-vf3tb-sea-after.png` |
| 修复后（发布包 MSAA 4） | `sdl3_reports/images/2026-10-23-vf3tb-sea-after-msaa4.png` |

### 诊断过程（结论：**不是深度问题**）

先做了四组实验，全部由临时环境变量控制（排查完已删除）：

| 实验 | 做法 | 结果 | 说明 |
| --- | --- | --- | --- |
| `SM3D_TRANS_DEBUG=p1` | 只画 priority 1 | 海滩、棕榈树、海水 | 海水在 priority **1** |
| `SM3D_TRANS_DEBUG=p2` | 只画 priority 2 | 两个角色 + 水花 | 角色在 priority **2** |
| `SM3D_TRANS_DEBUG=block` | 透明 pass 前把深度清到近平面 | 海水**仍然**盖住角色 | 透明 pass 的深度测试对这些像素不起作用 |
| `SM3D_TRANS_DEBUG=clearfar` | 透明 pass 前清到远平面 | 与正常几乎一致 | 同上，深度不是决定因素 |
| `SM3D_DEPTH_DEBUG=1` | 片元颜色 = `gl_FragDepth * 1e6` | 深度值正常 | 排除投影/深度写入错误 |

关键点：**depth 在每个 priority 开始时被清空**，所以 priority 2 的角色根本
不会和 priority 1 的海水做深度比较；海水是在自己的 priority 里通过深度测试、
写进透明附件，然后一直留到帧末统一合成。

### 根因

New3D 的 `R3DFrameBuffers` 是**一个 FBO 带 3 个颜色附件**：

```
Attachment 0 = 不透明颜色
Attachment 1 = trans1
Attachment 2 = trans2
```

`R3DShaderCommon.h` 的 `WriteOutputs()`：

```glsl
if (layer == LayerColour) {
    out0 = colour;   // alpha 强制 1
    out1 = vec4(0);  // 关键：擦掉更低 priority 留下的透明像素
    out2 = vec4(0);
}
```

也就是说，**不透明层绘制时顺便把透明附件清零**。这是跨 priority 唯一的
“擦除”机制，硬件没有单独的透明 pass。

Vulkan 移植把三个目标做成了**独立 image**，每个 pass 只绑一个附件，于是：

- priority 1 的海水写进 `m_trans2`；
- priority 2 的不透明角色只写 `m_color`，**没有任何东西擦除** `m_trans2`；
- 帧末合成时海水仍然存在 → 盖在角色上。

### 修法

1. **每个场景 pass 绑定全部 3 个颜色附件**（`begin()`），
   由 pipeline 的 `colorWriteMask` 决定某一层写哪个附件。
2. `CVulkan3D::CreatePipeline(int layer, bool overlay)` 生成 **6 条管线**
   （attachment 0/1/2 × base/overlay）：
   - `layer == 0`（颜色层）：三个附件的 mask 都是 `0xF`；
   - 透明层：只开自己那一个附件的 mask，其余为 `0x0`。
3. 片元着色器 `real3d.frag` 按 `WriteOutputs()` 输出三个 `out`：
   颜色层写 `colour + 0 + 0`，透明层写 `0 + 自己 + 0`。
   `pc.extra.z` 就是 attachment 编号（0/1/2）。
4. bucket → attachment 的映射：`0→0 (colour)`、`1→2 (trans2)`、
   `2→1 (trans1)`。`FillPush()` 传的是 **attachment 编号**，不是 bucket 下标，
   保证着色器输出位置和 pipeline mask 一致。
5. `DrawScene()` 每个 priority 只开一次 pass：清深度 → 画颜色层 →
   画 trans2 → 画 trans1，然后关 pass。首帧 `begin(true, true, true)`
   一次清掉三个附件。

### 验证

- VF3 沙滩：海水回到角色身后，与 OpenGL 一致。
- Daytona 2 瀑布：白色瀑布正常，无蓝块/白线
  （`sdl3_reports/images/2026-10-23-regress-daytona2.png`）。
- Sega Rally 2：天空、车体、HUD 正常
  （`sdl3_reports/images/2026-10-23-regress-srally2dx.png`）。
- Lost World：几何、LOD 淡出正常（天空见第 2 节）。

### 遗留的折中（不是本次引入）

两个透明层在 Vulkan 共用一份深度，无法像 New3D 的 `StoreDepth/RestoreDepth`
那样各自从“不透明深度”重新开始。当前按“先画主要层（trans2）、再画 trans1”
缓解；透明层彼此重叠的极端情况仍可能与硬件有细微差异。

---

## 2. VF3 的月亮/云、Lost World 的天空消失（Render2D Bottom 被盖住）

**状态：已修复（2026-10-23）**

### 症状

- VF3 沙滩：天空区域纯黑，月亮、云、蓝色渐变全都没有；
- Lost World：天空变成一整块扁平的土黄色。

Sega Rally 2 的天空正常 —— 因为它的天空是 **3D 几何体**，不是 2D 背景层。

### 根因

这两作的天空来自 **Render2D 的 Bottom 图层**（496×384 tile 层），
合成顺序是：

```
Render2D Bottom -> New3D 离屏 3D -> Composite -> Render2D Top
```

New3D 的合成 (`R3DFrameBuffers::Draw()`) 用 **SRC_ALPHA / ONE_MINUS_SRC_ALPHA**
把 3D FBO 画到主缓冲上，用的是 FBO 自己的 **alpha**：

- FBO 清屏颜色 alpha = **0.0**；
- 没有几何体的地方 alpha 保持 0 → 2D 背景透出来；
- 雾 quad 写入 alpha = 雾浓度（scroll fog 或 `1 - fogAmbient`）；
- 不透明几何体 alpha = 1。

Vulkan 的 `composite.frag` 在合成透明层后写了：

```glsl
colour = vec4(colour.rgb * (1.0 - trans.a) + trans.rgb * trans.a, 1.0);
//                                                                  ^^^ 这里
```

**alpha 被硬写成 1.0**，于是 3D 场景层永远不透明地盖住 Render2D Bottom。
天空区域的场景颜色正好是雾色（VF3 = 黑，Lost World = 土黄），
所以看起来像“天空消失”。

### 修法

`Src/Graphics/Vulkan3D/Shaders/composite.frag` 里把“New3D 两次混合”折算成
一次 source-over，并**保留场景 alpha**：

```glsl
if (pc.layer == 1u)
{
    vec4 t1 = texture(..., uLayers[4], vUv);
    vec4 t2 = texture(..., uLayers[5], vUv);
    if (t1.a * t2.a > 0.0)
    {
        // 两层透明重叠：硬件结果是完全不透明（没有像素可以混合）
        colour = vec4(mix(t1.rgb, t2.rgb, (t2.a + (1.0 - t1.a)) * 0.5), 1.0);
    }
    else
    {
        vec4 trans = t1.a > 0.0 ? t1 : t2;
        float alpha = trans.a + colour.a * (1.0 - trans.a);
        vec3 rgb = alpha > 0.0
            ? (trans.rgb * trans.a + colour.rgb * colour.a * (1.0 - trans.a)) / alpha
            : vec3(0.0);
        colour = vec4(rgb, alpha);
    }
}
```

数学上等价于 New3D 的两次 quad 绘制：

1. 先画 base：`dst = base.rgb*base.a + bg*(1-base.a)`；
2. 再画 trans：`dst = trans.rgb*trans.a + dst*(1-trans.a)`。

一次输出的 source-over 形式即上面的公式；`t1.a * t2.a > 0` 时保持原来
“重叠 = 不透明”的行为，LOD 交叉淡出不受影响。

### 关于四条“常见怀疑”的核对结果

1. **主缓冲跨 pass 的 loadOp**：本移植的合成是**一个 pass 画完**所有图层
   （bottom tile / scene / top tile 都是 `composite.frag` 里采样的纹理），
   不存在“Render2D 画完再开 pass 被 CLEAR”的路径。`DrawCompositePass`
   的 CLEAR 发生在所有图层之前，是正确用法。
2. **离屏 clear alpha**：`begin()` 的三个颜色附件统一清成
   `(0, 0, 0, 0)`，符合要求。
3. **2D / fog / composite quad 的剔除与深度**：三者都是
   `cullMode = VK_CULL_MODE_NONE`；composite 和 post 根本不绑定深度附件，
   fog pipeline 是 `depthTestEnable = VK_FALSE`。无需修改。
4. **Scroll fog 混合**：New3D 的雾 quad 是**在不透明 FBO 上覆盖写**（此时目标
   已被清成 0），混合与否结果相同；我们保持 `blendEnable = FALSE`，
   并让 alpha 进入合成阶段，行为与 New3D 一致。

### 验证

| 场景 | 修复前 | 修复后 |
| --- | --- | --- |
| VF3 沙滩 | 天空纯黑 | 月亮 + 云 + 蓝天（`sdl3_reports/images/2026-10-23-vf3tb-sea-after.png`） |
| Lost World | 扁平土黄 | 黄色云层天空（`sdl3_reports/images/2026-10-23-lostwsga-after.png`） |

Lost World 对照：`2026-10-23-lostwsga-before.png`（修复前）、
`2026-10-23-lostwsga-gl.png`（OpenGL 参考）。

---

## 3. 截图工具：`Tools/Capture-Frame.ps1`

排查画面问题时截图不能靠“抓屏幕”：

- 脚本启动的模拟器拿不到键盘焦点（Windows foreground lock），`Alt+S` 无效；
- 独占全屏下 `CopyFromScreen` 抓到的是黑屏。

所以模拟器新增了钩子 **`SM3D_SCREENSHOT_AT=<秒>`**（`Main.cpp` 主循环），
到点由模拟器自己截图（与窗口焦点无关），并写日志
`Screenshot: SM3D_SCREENSHOT_AT fired at ...`。

工具封装了“载入存档 → 等截图 → 关会话 → 报告路径”：

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Capture-Frame.ps1 \
    -Set vf3tb -Slot 1 -Bin build-cmake/bin -At 5 -Out out.png
```

常用参数：

| 参数 | 说明 |
| --- | --- |
| `-Set` | ROM set 名；自动找该 set 最新的 `.zst*` / `.st*` 存档 |
| `-Slot` | 指定存档槽 |
| `-State` | 显式存档路径（会被转成绝对路径） |
| `-NoState` | 不读档，直接启动 |
| `-Exe` | `Model3-Vulkan.exe`（默认）或 `Supermodel-LegacyOG.exe` |
| `-Bin` | 安装目录，默认 `dist\Supermodel-VK`，其次 `build-cmake\bin` |
| `-At` | 启动后多少秒截图，默认 4 |
| `-Out` | 另外复制一份到指定路径 |
| `-Env` | 额外环境变量，`A=1;B=2` |
| `-RawState` | 顺带导出旧版 raw `.st`（OpenGL 版只能读 raw） |

踩过的坑：

- **`-State` 的相对路径会按模拟器的工作目录解析**（`-Bin`），不是当前 shell。
  工具已统一转绝对路径。
- `.zst<slot>` 是 ZIP 容器（`meta.json` + `core.zst` + 可选 `thumb.png`），
  **OpenGL-only 的 `Supermodel-LegacyOG.exe` 读不了**；要对照 GL 时先用
  `-RawState` 导出 raw 版。
- 脚本用 `WM_CLOSE` 结束会话会连带退出设置界面；工具内部已处理
  （也支持 `-KeepRunning` 保留进程）。
- 在 `sh` 下调用时脚本路径要用正斜杠或单引号：
  `-File Tools/Capture-Frame.ps1`（反斜杠会被 shell 吃掉）。

---

## 4. 本次涉及的文件

| 文件 | 改动 |
| --- | --- |
| `Src/Graphics/Vulkan3D/Vulkan3D.cpp` | 3 附件 pass、6 条场景管线、bucket→attachment 映射、移除临时调试开关 |
| `Src/Graphics/Vulkan3D/Vulkan3D.h` | `m_scenePipelines[3][2]`、`CreatePipeline(layer, overlay)`、`FillPush(..., layer)` |
| `Src/Graphics/Vulkan3D/Shaders/real3d.frag` | 三个颜色输出，按 `WriteOutputs()` 分派；颜色层 alpha 强制 1 |
| `Src/Graphics/Vulkan3D/Shaders/composite.frag` | 保留场景 alpha 的 source-over 合成（修复 2D 天空） |
| `Src/Graphics/New3D/New3D.cpp` | 移除临时 scene-log（原版逻辑未改） |
| `Tools/Capture-Frame.ps1` | 截图工具（新增），`-State` 绝对路径修复 |
| `Src/OSD/SDL/Main.cpp` | `SM3D_SCREENSHOT_AT` 钩子（截图工具依赖） |

---

## 5. 回归矩阵（2026-10-23）

| 游戏 | 场景 | 结果 | 截图 |
| --- | --- | --- | --- |
| VF3 TB | 沙滩（海水 + 2D 天空） | 正常 | `2026-10-23-vf3tb-sea-after.png` |
| VF3 TB | 同上，发布包 MSAA 4 | 正常 | `2026-10-23-vf3tb-sea-after-msaa4.png` |
| Daytona 2 | 瀑布 | 正常 | `2026-10-23-regress-daytona2.png` |
| Sega Rally 2 DX | 草原 | 正常 | `2026-10-23-regress-srally2dx.png` |
| Lost World | 圆顶基地 + 2D 天空 | 正常 | `2026-10-23-lostwsga-after.png` |

测试构建为 `build-cmake/bin`（MSAA 0），发布包为 `dist/Supermodel-VK`（MSAA 4，
VRR 120Hz），两者都已实测。

---

## 6. 工具链对照：Synchronization Validation 与 VMA（2026-10-23）

### 6.1 同步验证 / GPU 辅助验证

**用法**

校验层不随项目分发。装 Vulkan SDK 后可以直接跑；本机是从 SDK 1.4.363.0 的安装包里
只解出 `VkLayer_khronos_validation.dll/.json` 到
`%LOCALAPPDATA%\Supermodel-VK\VulkanLayers\Bin`，用 `VK_LAYER_PATH` 指过去，不安装
整个 SDK。`Tools/Validate-Vulkan.ps1` 会自己找这个目录（或 `%VULKAN_SDK%\Bin`），
跑一局短会话，然后把日志里的 VUID / hazard / Vulkan error 统计出来：

```powershell
# 同步验证（默认）
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Validate-Vulkan.ps1 -Set vf3tb -Slot 1

# GPU 辅助验证
... -Set vf3tb -State <存档绝对路径> -Mode gpu -Bin build-cmake/bin -At 8

# 两个都开
... -Mode both -LayerPath D:\VulkanSDK\Bin
```

它内部调用 `Tools/Capture-Frame.ps1`，所以截图、关会话、`-State` 绝对路径这些行为
与平时完全一致；只是额外带了环境变量并解析 `Data/Supermodel.log`。

两个开关都通过环境变量请求，且会自动打开 `VkValidation` 本身：

| 变量 | 作用 | 开销 |
| --- | --- | --- |
| `SM3D_VK_SYNC_VALIDATION=1` | `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`：报告 `WRITE_AFTER_WRITE` / `READ_AFTER_WRITE` / `WRITE_AFTER_READ`，给出命令缓冲、管线阶段和 image layout | 很大（每帧逐屏障跟踪） |
| `SM3D_VK_GPU_VALIDATION=1` | `VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT`：给着色器插桩，抓越界采样/描述符访问 | 很大（每个着色器都变慢） |

实现位置：`VulkanContext::InitDevice()` 读环境变量，`CreateInstance()` 在
`VK_LAYER_KHRONOS_validation` 提供 `VK_EXT_validation_features` 时挂
`VkValidationFeaturesEXT`。层不存在或旧版层没有该扩展时，回退到普通校验并明确记日志，
不会让游戏起不来。

**实测结果（VF3 沙滩，RTX 5060 Ti）**

- 同步验证先报了一个每帧都出现的真实 hazard：swapchain 图像的第一次布局转换
  （`PRESENT_SRC/UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL`）的 `srcStageMask` 是
  `TOP_OF_PIPE`，而提交的 acquire 信号量等待阶段是 `COLOR_ATTACHMENT_OUTPUT`，
  转换可能发生在 `vkAcquireNextImageKHR` 完成之前（`WRITE_AFTER_READ`）。
  修法：`CmdTransition()` 增加可选的 `srcStageOverride`，swapchain 的首次转换传入
  `COLOR_ATTACHMENT_OUTPUT`，与等待阶段建立执行依赖。之后 **0 hazard**。
- 校验层（普通参数校验）同时暴露了三个移植问题，均已修复：
  1. **`independentBlend` 未启用**：三个场景附件使用不同的 `colorWriteMask`，
     没有该特性时 `pAttachments` 必须完全相同，MRT 擦除属于未定义行为。现在在
     `CreateDevice()` 里要求并启用，不支持则明确失败（桌面 GPU 都支持）。
  2. **`VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE` 未启用**：纹理缓存为
     16 种 wrap 组合建采样器，第 4 种模式需要 `samplerMirrorClampToEdge`。
     现在按特性启用；不支持时退回 `CLAMP_TO_EDGE`（3D 着色器自己做 mirror+clamp）。
  3. **描述符布局生命周期**：纹理解码 compute 管线创建了临时
     `VkDescriptorSetLayout`，建完 pipeline layout 就销毁，随后
     `vkCreateComputePipelines` 引用到“已删除对象”。现在该布局作为成员保留到
     `m_computeLayout` 销毁之后。
- GPU 辅助验证跑同一场景 **0 个着色器访问错误**；层会自行强制
  `shaderInt64` / `vulkanMemoryModel` / `bufferDeviceAddress` 等特性并打印警告，
  这是预期行为。
- 同样的同步验证在 Daytona 2 瀑布场景也跑了一遍：**0 VUID / 0 hazard**。

**注意**：校验消息有 1000 条上限（`DebugCallback`），超出后只打印一条汇总，
避免一个系统性错误把日志刷爆。

### 6.2 显存管理：与 VMA 的对照

现有实现已经在用 VMA 文档推荐的两种关键模式，不需要替换成 VMA：

| 模式 | 现状 |
| --- | --- |
| Persistent mapped memory | `CVulkanRingBuffer` 每帧切片、HOST_VISIBLE + HOST_COHERENT、创建时映射一次，槽位 fence 等待后才重置，几何/纹理/UI 上传都走它 |
| 子分配 + 对齐 | `CVulkanMemory` 按 `VkMemoryRequirements` 的 size/alignment 子分配 image，free span 会合并；buffer 用专用分配 |
| 预算/统计 | 新增 `VK_EXT_memory_budget`：启动和分配失败时打印 heap budget / heapUsage / 本会话持有量 |
| 泄漏可见性 | 块分配与释放都记日志；退出时打印释放总量；分配失败同时打印总量、块数和剩余空闲 |

没有出现需要引入 VMA 的三个症状：

- **显存泄漏**：所有 image/buffer 走 `DestroyImage/DestroyBuffer`，block 在 `Shutdown` 释放；
  退出日志给出总量，重复进出游戏可以对比。
- **Staging 频繁分配卡顿**：上传走持久映射环，纹理页解码/上传不调用 `vkAllocateMemory`；
  唯一的按需分配在截图/缩略图保存路径，不在每帧路径上。
- **Alignment 报错**：image 用驱动返回的 `req.alignment`；UBO/storage 偏移用
  `minUniformBufferOffsetAlignment` / `minStorageBufferOffsetAlignment`；环内对齐要求幂次，
  现有调用方都满足。

**仍然值得做的后续项**（按收益排序）

1. **块大小预算化**：现在固定 64 MB 块；可以改成
   `clamp(max(request, 64 MB), 64 MB, heapBudget/8)`，减少大分辨率 + MSAA 下的块数量。
   只有在实测到块碎片或分配失败时再做。
2. **整块回收**：`FreeSuballocation` 只合并 span，不把全空的 block 还给驱动。
   swapchain 反复重建后可能有闲置块；可以加“块内全空且不是最后一个”时释放。
3. **缩略图 staging 复用**：保存存档时每次新建全屏 staging buffer，属于低频路径，
   但可以复用截图 buffer。
4. **VMA 作为备选**：如果以后需要 defragmentation、lost allocation 或显存预算驱动的
   自动降级，再引入 VMA；目前的 350 行分配器覆盖了所需功能，替换的收益不足。

### 6.3 本机校验层位置

从 Vulkan SDK 1.4.363.0 安装包解出的文件（不入库）：

```
%LOCALAPPDATA%\Supermodel-VK\VulkanLayers\Bin\
    VkLayer_khronos_validation.dll
    VkLayer_khronos_validation.json
```

其他机器装 Vulkan SDK（或 Vulkan Runtime 中的 validation layer）即可，
`VK_LAYER_PATH` 指向其 `Bin` 目录；不设置时加载器会走系统注册的层。
