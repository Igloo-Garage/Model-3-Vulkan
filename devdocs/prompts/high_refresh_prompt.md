# Sega Model 3 模拟器 (纯 Vulkan + SDL3) 高刷插值与帧平滑 (120Hz~480Hz) 执行提示词

本文档提供可直接复制到你的 **Sega Model 3（纯 Vulkan 后端 + SDL3）** 模拟器项目新会话窗口中的工程级指令提示词。方案深度借鉴了 `spacestate1/namco22-decompile` 的宿主显示节奏控制（`[HOST] display N Hz, vsync or timer-paced`）与固定逻辑步进思想，并专门结合 **Model 3 (Real3D Pro-1000 场景图硬件 + 57.524 Hz 刷新率)** 的底层特性量身定制。

## 完整执行提示词（直接复制到 Model 3 项目新窗口发送）

> **将以下引用块内容完整复制到新窗口发送即可：**
>
> 请先全面审阅当前 Sega Model 3 模拟器（纯 Vulkan 后端 + SDL3）的主循环调度、Real3D Pro-1000 场景图遍历（Culling RAM / Viewport / Matrix Stack）、2D Tilegen 图层合成以及音频（SCSP / DSB）同步代码。
>
> **核心目标**：借鉴 `spacestate1/namco22-decompile` 的高刷帧平滑架构（游戏内部逻辑死锁街机原生帧率，渲染呈现端跟随显示器高刷插值输出），为本模拟器实现 **“逻辑固定 57.524 Hz 步进 + 呈现端（Render Tick）按物理高刷 120Hz / 144Hz / 240Hz / 480Hz 插值平滑”**，彻底根除 Model 3 57.524 Hz 在现代高刷屏上的差频打拍卡顿（Cadence Judder）。
>
> 请严格按照以下 **4 个递进阶段（Phase 1 \~ Phase 4）** 先分析现有代码结构并输出详细设计方案（列出涉及的文件、数据结构与 Shader 修改点），待我确认后再分步编码实施：
>
> **Phase 1：SDL3 纳秒级固定步进蓄水池与逻辑/渲染解耦 (`MainLoop` & `DisplayPacer`)**
> 1. **逻辑步进与呈现循环彻底解耦**：
>    - 定义 Model 3 硬件级精确仿真步长常量：`SIM_STEP_NS = 17,384,048`（对应原生 `57.52416 Hz`）；
>    - 使用 SDL3 高精度计时器 `SDL_GetTicksNS()` 构建固定步长蓄水池（Fixed Timestep Accumulator）：
>      - 当 `accumulator >= SIM_STEP_NS` 时：推进 PowerPC 603e 仿真周期、触发 VBlank / IRQ、执行 SCSP 与 DSB MPEG 音频块生成、刷新方向盘力回馈（FFB）状态，并将 `accumulator -= SIM_STEP_NS`；
>      - 施加防死锁钳制（Spiral of Death Clamp）：若因窗口拖拽或读档导致 `accumulator > SIM_STEP_NS * 3`，强制截断为 `SIM_STEP_NS`，严禁连续疯狂追帧。
> 2. **多档位显示节奏控制（借鉴 `namco22-decompile` Pacer）**：
>    - 通过 `SDL_GetCurrentDisplayMode` 自动探测当前显示器物理刷新率，支持配置项 `RefreshRateMode`：
>      - `Original`（锁定 57.524 Hz 传统模式）；
>      - `Auto`（自动跟随显示器物理刷新率 + VSync / VRR）；
>      - `Fixed`（显式指定 `60 / 120 / 144 / 165 / 240 / 480 Hz` 定时器/VSync 驱动模式）。
>    - 在每次呈现周期计算归一化亚帧插值权重：$\alpha = \text{clamp}(\text{accumulator} / \text{SIM\_STEP\_NS}, 0.0, 1.0)$。
>
> **Phase 2：Real3D Pro-1000 双缓冲场景快照与矩阵插值（从相机级到实例级逐步推进）**
> 1. **第一级（必做，零开销高收益）：多视口摄像机矩阵插值（Per-Viewport Camera Lerp/Slerp）**：
>    - Real3D 硬件支持最多 4 个独立优先级视口（Viewport 0\~3，分别用于主赛道视野、车内后视镜、3D 仪表盘等）；
>    - 在每次 `57.524 Hz` 仿真步完成（Ping-Pong 缓冲翻转点）时，按视口索引独立保存上一帧与当前帧的观察矩阵（`PrevViewMatrix[vp]`、`CurrViewMatrix[vp]`）；
>    - **严禁对旋转矩阵直接做逐元素线性相加**（会导致 $\alpha=0.5$ 时画面收缩畸变）：必须将矩阵分解为平移向量 $\mathbf{T}$（执行 `lerp`）与旋转四元数 $\mathbf{Q}$（执行最短路径 `slerp`），重新合成亚帧视图矩阵 `InterpViewMatrix[vp](alpha)` 并通过 Vulkan UBO / Push Constants 传入顶点着色器。
> 2. **第二级（进阶，全动态物体丝滑）：Real3D 矩阵栈 / 模型实例插值（Instance Matrix Interpolation）**：
>    - 利用 Real3D Pro-1000 硬件的天然优势——多边形网格静态常驻 VROM，动态物体（玩家车辆、对手赛车、《Virtua Fighter 3》角色骨骼节点）均通过 Culling Node 引用的 **变换矩阵表（Matrix Stack / Coordinate System RAM）** 驱动；
>    - 为每个渲染批次记录其唯一的 Culling 节点路径签名或矩阵槽位 ID（`MatrixSlotID`），在双缓冲快照中匹配上一帧同 ID 的模型变换矩阵，在 GPU / CPU 端对动态物体的 `ModelMatrix` 同步执行 `lerp + slerp`。
>
> **Phase 3：镜头切镜检测（Cut Detection）与图层豁免策略**
> 1. **自动镜头切换与转场保护**：
>    - 在计算每个视口的插值前，校验帧间位移增量 $\Delta d = \|\mathbf{T}_N - \mathbf{T}_{N-1}\|$ 与旋转夹角 $\Delta \theta = 2\arccos(|\mathbf{Q}_N \cdot \mathbf{Q}_{N-1}|)$；
>    - 若 $\Delta d$ 超过场景阈值（如切换第一/第三人称视角、吸引模式分镜切换）或 $\Delta \theta > 15^\circ$，判定为 **镜头切镜（Scene Cut）**，当前帧强制令 $\alpha = 1.0$，彻底杜绝转场瞬间产生“跨地图飞掠”或鬼影残像。
> 2. **正交 UI 视口与 2D Tilegen 层豁免**：
>    - 自动识别正交投影/固定 3D HUD 视口（如《Daytona USA 2》的 3D 档位显示、《SpikeOut》的 3D 血条）以及 **2D Tilegen 字符/卷轴图层**，将其锁定为离散更新（$\alpha = 1.0$），保证数字秒表、转速表指针与文字绝对清晰锐利、不发虚。
>
> **Phase 4：240Hz/480Hz 极速亚帧复用与帧节奏遥测诊断**
> 1. **亚帧（Sub-frame）零冗余上传优化**：
>    - 在两次 `57.524 Hz` 仿真步之间的多个高刷呈现帧（例如 240Hz 下每跑 1 次 PowerPC 仿真会绘制约 4.17 个 Vulkan 渲染帧），**完全跳过纹理缓存校验、跳过 2D Tilegen 纹理解码上传、跳过静态几何顶点重建**！
>    - 亚帧仅更新插值后的矩阵 UBO / Push Constants 并重放 3D 光栅化与 CRT 后处理 Command Buffer，确保即使在 480Hz 下 CPU 占用也与 60Hz 基本持平。
> 2. **帧节奏诊断覆盖层（借鉴 `[PACE]` 遥测）**：
>    - 在统计面板中增加实时帧节奏监控行：显示 `Logic: 57.52 Hz | Present: 120.0 / 240.0 Hz (Ratio: 2.09x / 4.17x) | Alpha: 0.xx | Cuts: N`，直观验证高刷插值管线的工作状态。

## 必须死守的 6 条关键避坑红线（供审阅代码方案时逐条核对）

| 红线编号 | 避坑领域 | 为什么会踩坑 & 强制验收标准 |
|---|---|---|
| **红线 1** | **视口独立跟踪（后视镜防串扰）** | 《Daytona USA 2》《Scud Race》的主视野和顶部后视镜是两个不同的 Real3D Viewport。**必须按 `viewport_index` 分别存储 `PrevViewMatrix[4]`**；如果全局只存一个相机矩阵，主视角和后视镜会互相插值，导致画面疯狂闪烁。 |
| **红线 2** | **旋转矩阵正交性（防画面缩放形变）** | 两个旋转矩阵直接按 `(1 - alpha) * M0 + alpha * M1` 线性插值会破坏正交性（当转向 $90^\circ$ 且 $\alpha=0.5$ 时，世界会缩小为原来的 $\cos 45^\circ \approx 0.707$ 倍）。**必须拆解为四元数 `slerp` + 平移 `lerp`**。 |
| **红线 3** | **Real3D Ping-Pong 翻转时序边界** | Model 3 的 PowerPC 通过 DMA 向 Culling RAM 写数据时具有异步延迟。**场景快照必须在 Real3D 触发 Ping-Pong 缓冲翻转（帧提交完成）的确切边界提取**，绝不能在 DMA 半途读取脏矩阵。 |
| **红线 4** | **2D Tilegen 与 3D HUD 隔离** | 2D Tilemap 图层（计时器、字幕）每帧可能通过调色板动画或 Tile 索引跳变实现闪烁效果。**2D 层和 3D HUD 视口严禁参与位移插值**，否则圈速毫秒数字会变成模糊重影。 |
| **红线 5** | **音频与力回馈（FFB）时钟严格隔离** | SCSP/DSB 音频采样生成与 SDL3 方向盘力回馈指令刷写，**只能放在 `while (accumulator >= SIM_STEP_NS)` 内部（固定 57.524 Hz）**；绝不能放进外层的 120Hz/480Hz 渲染循环，否则音频会倍速、方向盘 USB 总线会被高频指令冲垮。 |
| **红线 6** | **亚帧禁止重复上传纹理与构建网格** | 在 240Hz/480Hz 下，每帧预算仅有 `2.08ms ~ 4.16ms`。如果每个亚帧都重新遍历 Real3D 场景树并上传 Staging 纹理，CPU 必将瓶颈掉帧。**亚帧必须 100% 复用最近一次仿真帧构建好的 Vulkan 顶点/索引缓冲与纹理集，仅更新矩阵缓冲**。 |
