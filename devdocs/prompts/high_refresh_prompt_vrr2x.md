# Sega Model 3 模拟器 (纯 Vulkan + SDL3) 高刷插值与 VRR 2x (115.048Hz) 提示词库

本文档包含两部分：
1. **【增量补丁提示词】**：适用于你**已经完成了基础高刷插值与原有的 57.524Hz VRR 模式**，现在只需单独新增 **`VRR 2x (115.048 Hz)` 整倍频黄金模式**；
2. **【从零全量提示词】**：适用于从零开始构建完整的高刷插值与显示同步系统。

## 一、 增量补丁提示词：单独新增 `VRR 2x (115.048 Hz)` 整倍频模式（直接复制）

> **将以下引用块内容完整复制到新窗口发送即可：**
>
> 请先审阅当前项目中已实现的 **`57.524 Hz` G-SYNC/VRR 帧同步代码（FramePacer）** 以及 **高刷矩阵插值渲染管线（`Prev/Curr` 矩阵快照、`alpha` 插值权重与亚帧缓冲复用）**。
>
> **本次增量任务目标**：在保留现有 `57.524 Hz (VRR 1x)` 和固定高刷插值模式的基础上，专为支持 G-SYNC / FreeSync (VRR) 的高刷显示器（$\ge 120\text{ Hz}$）新增 **`VRR 2x (115.0483 Hz)` 精确二倍频模式**（并预留 `3x = 172.572 Hz` / `4x = 230.096 Hz` 扩展能力）。
>
> 请严格按照以下 **4 个实施要点** 先输出代码修改计划，待我确认后执行修改：
>
> **1. 采用“确定性奇偶分频器（Deterministic Phase Stepper）”替代连续时间蓄水池**
> - 在 `VRR 2x` 模式下，**严禁使用 `accumulator / SIM_STEP_NS` 计算浮点数 `alpha`**（避免操作系统线程唤醒抖动导致 `alpha` 在 `0.48 ~ 0.52` 之间微颤）；
> - 改为以 Model 3 硬件基准周期 `SIM_STEP_NS = 17,384,048 ns`（`57.52416 Hz`）的精确半周期 `HALF_STEP_NS = 8,692,024 ns`（`115.04832 Hz`）推进固定相位状态机 `subFrameIndex`（`0` 或 `1`）：
>   - **当 `subFrameIndex == 0`（偶数插值拍，耗时锚点 `+8,692,024 ns`）**：
>     - **完全跳过** PowerPC 603e 仿真、跳过 SCSP/DSB 音频推流、跳过方向盘 FFB 刷写、跳过纹理与静态网格上传；
>     - 直接传入**硬编码绝对精确常量 `alpha = 0.5f`**（若上一仿真帧触发了 `SceneCut` 切镜标志，则降级传入 `alpha = 1.0f`），仅更新各视口（Viewport 0\~3）的插值矩阵 UBO / Push Constants 并提交 Vulkan 渲染与 Present。
>   - **当 `subFrameIndex == 1`（奇数仿真拍，耗时锚点 `+17,384,048 ns`）**：
>     - 执行 1 次完整的 `57.524 Hz` PowerPC 仿真步、音频生成与 FFB 更新，完成 Real3D Ping-Pong 翻转与 `Prev/Curr` 矩阵快照滚动；
>     - 传入**硬编码绝对精确常量 `alpha = 1.0f`** 提交 Vulkan 渲染与 Present。
>
> **2. VRR 面板上限安全校验与防撞墙机制（120Hz 面板保护）**
> - 通过 SDL3 `SDL_GetCurrentDisplayMode` 检查当前显示器物理刷新率上限 `maxRefreshHz`：
>   - 仅当 `maxRefreshHz >= 120.0f` 时允许激活 `VRR 2x (115.048 Hz)`（若检测到 60Hz 显示器则自动回退至 `VRR 1x` 或 `Fixed 60Hz` 并打印日志提示）；
>   - **防撞顶保护**：在 120Hz VRR 显示器上，`115.048 Hz`（`8.692 ms`）距离 120Hz 物理上限（`8.333 ms`）仅有 `0.359 ms` 裕量。请确保 `FramePacer` 采用纳秒级绝对时间锚点（`nextTargetNs += HALF_STEP_NS`，配合粗睡眠 + `SpinWait` 微秒级收尾），且 Swapchain 保持与现有 `57.524 Hz` VRR 模式一致的 PresentMode，严防瞬时过快提交撞上 120Hz 顶棚而退化为传统 VSync 阻塞。
>
> **3. 配置项与热键切换扩展**
> - 在配置系统与 CLI 中扩展刷新率/帧同步模式枚举（如 `vrr_1x`、`vrr_2x`、`vrr_3x`），并支持游戏内快捷键一键在 `VRR 1x (57.52 Hz)` 与 `VRR 2x (115.05 Hz)` 之间实时热切换，方便直观做 A/B 顺滑度对比。
>
> **4. 统计覆盖层（Overlay）遥测增强**
> - 在性能监控面板中明确区分显示：`Pacer: VRR 2x (115.05 Hz) | Sim: 57.52 Hz | Phase: [0.50, 1.00] Locked | Sub-frame CPU: 0.xx ms`，验证偶数拍 `alpha` 锁定在 `0.50` 且仿真帧率严格维持在 `57.524 Hz`。

## 二、 从零全量提示词：完整高刷插值与 VRR 整倍频系统（供全新会话参考）

> **若在全新分支从零实现整套系统，可复制以下全量提示词：**
>
> 请先全面审阅当前 Sega Model 3 模拟器（纯 Vulkan 后端 + SDL3）的主循环调度、现有的 `57.524 Hz` G-SYNC/VRR 限帧实现、Real3D Pro-1000 场景图遍历（Culling RAM / Viewport / Matrix Stack）、2D Tilegen 图层合成以及音频（SCSP / DSB）同步代码。
>
> **核心目标**：在保留现有 `57.524 Hz (1x VRR)` 原生模式的基础上，借鉴 `spacestate1/namco22-decompile` 的高刷帧平滑架构，实现 **“逻辑固定 57.524 Hz 步进 + 呈现端支持 VRR 精确整倍频（`2x=115.048Hz` / `3x=172.572Hz` / `4x=230.096Hz`）以及非 VRR 固定高刷（`120Hz / 144Hz / 240Hz / 480Hz`）插值平滑”**。
>
> 请严格按照以下 **4 个递进阶段（Phase 1 \~ Phase 4）** 先分析现有代码结构并输出详细设计方案，待我确认后再分步编码实施：
>
> **Phase 1：双轨制高刷调度器——VRR 确定性整倍频分频器 + 非 VRR 蓄水池 (`MainLoop` & `DisplayPacer`)**
> 1. **逻辑基准常量**：定义 Model 3 硬件级精确仿真步长：`SIM_STEP_NS = 17,384,048`（对应原生 `57.52416 Hz`）。
> 2. **轨道 A：G-SYNC / FreeSync / VRR 精确整倍频模式（`VRR_1x` / `VRR_2x` / `VRR_3x` / `VRR_4x`）**：
>    - `VRR 1x (57.524 Hz)`：每 `17,384,048 ns` 跑 1 次仿真 + 1 次原生渲染（$\alpha = 1.0$）；
>    - `VRR 2x (115.048 Hz)`：以子步长 `SUB_STEP_NS = 8,692,024 ns` 驱动确定性奇偶分频器（子帧 0 不跑仿真、硬编码 $\alpha = 0.5$；子帧 1 推进 PowerPC 仿真、音频与 FFB，硬编码 $\alpha = 1.0$）。
> 3. **轨道 B：非 VRR 显示器的固定高刷蓄水池模式（`Auto` / `Fixed 60 / 120 / 144 / 240 / 480 Hz`）**：
>    - 使用 `SDL_GetTicksNS()` 构建固定步长蓄水池，按 $\alpha = \text{clamp}(\text{accumulator} / \text{SIM\_STEP\_NS}, 0.0, 1.0)$ 连续插值。
>
> **Phase 2：Real3D Pro-1000 双缓冲场景快照与矩阵插值**
> 1. **第一级（多视口相机矩阵插值）**：按 Real3D 4 个视口（Viewport 0\~3）独立存储 `PrevViewMatrix[vp]` 与 `CurrViewMatrix[vp]`，分解为平移向量 $\mathbf{T}$（`lerp`）与旋转四元数 $\mathbf{Q}$（最短路径 `slerp`）。
> 2. **第二级（模型实例矩阵栈插值）**：匹配 Culling Node 矩阵槽位 ID（`MatrixSlotID`），对动态物体的 `ModelMatrix` 同步执行 `lerp + slerp`。
>
> **Phase 3：镜头切镜检测（Cut Detection）与图层豁免策略**
> 1. **切镜保护**：当视口帧间位移 $\Delta d$ 超阈值或旋转夹角 $\Delta \theta > 15^\circ$ 时，判定为 `Scene Cut`，当帧强制 $\alpha = 1.0$。
> 2. **UI 与 2D 层豁免**：正交 3D HUD 视口与 2D Tilegen 图层固定 $\alpha = 1.0$。
>
> **Phase 4：亚帧零冗余复用与遥测诊断**
> 1. **亚帧零上传**：$\alpha < 1.0$ 的插值子帧跳过纹理校验、跳过 2D 解码与静态网格重建，仅更新矩阵缓冲。
> 2. **遥测面板**：实时展示 `Logic Hz`、`Present Hz`、`Alpha` 与 `Cuts` 计数。

## 必须死守的 6 条关键避坑红线（供审阅代码方案时逐条核对）

| 红线编号 | 避坑领域 | 为什么会踩坑 & 强制验收标准 |
|---|---|---|
| **红线 1** | **`VRR 2x` 必须用硬编码 `0.5f` 而非时间比值** | 在 `VRR 2x` 下，子帧 0 就是严格意义上的时间中点。如果用实测时间戳除以 `17.384ms` 算 `alpha`，OS 调度微抖动会让 `alpha` 变成 `0.491` 或 `0.508`，反而引入人为微抖动。**必须直接传常量 `0.5f` 和 `1.0f`**。 |
| **红线 2** | **120Hz 面板防撞顶保护** | `115.048 Hz`（`8.692 ms`）距离 120Hz 面板物理上限（`8.333 ms`）极近。`FramePacer` 必须用绝对纳秒累加 `targetNs += 8692024` 配合自旋等待，严防某帧提前 `0.4ms` 提交撞上 120Hz 上限触发驱动级 VSync 卡顿。 |
| **红线 3** | **视口独立跟踪（后视镜防串扰）** | 《Daytona USA 2》《Scud Race》的主视野和顶部后视镜是两个不同的 Real3D Viewport。**必须按 `viewport_index` 分别存储 `PrevViewMatrix[4]`**。 |
| **红线 4** | **旋转矩阵正交性（防画面缩放形变）** | 两个旋转矩阵直接线性相加会破坏正交性导致画面收缩。**必须拆解为四元数 `slerp` + 平移 `lerp`**。 |
| **红线 5** | **音频与力回馈（FFB）仅在奇数拍执行** | SCSP/DSB 音频生成与方向盘 FFB 刷写**只能在 `subFrameIndex == 1`（57.524 Hz 仿真拍）执行**，偶数拍（$\alpha=0.5$）严禁调用。 |
| **红线 6** | **偶数拍 100% 复用 GPU 缓冲** | `subFrameIndex == 0` 的插值拍必须直接复用上一拍已绑定在 GPU 显存中的顶点缓冲与纹理集，仅更新矩阵 UBO / Push Constants。 |
