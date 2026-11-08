# AlphaSortMode = 2 — 街机真机棋盘格半透明

> 状态：**试验模式**，默认关闭（`AlphaSortMode` 默认仍为 `0`）。它与 LOD 交叉淡化
> （Daytona 2 的瀑布）不兼容，见第 7 节第 5 条。
> 相关：`devdocs/CHANGELOG.md` 的 `2026-10-26` 一节、`sdl3_reports/2026-10-26-checkerboard-alpha/`。

## 1. 背景

原版 New3D 渲染器作者 Ian（`dukey`）说明了 Real3D 真机处理半透明的方式：

> "the difference between the real h/w is supermodel renders polys with translucency at full
> resolution, where as on the real h/w it it's effectively halved" … "If two transparency layers
> overlap, which happens in daytona, the result is fully opaque because there are no pixels to
> blend against."

他当年做过一个实验构建来还原这件事（交错而非棋盘格，理由是方便调试），没有完成。

### 1.1 参考截图哪张是哪张

他给的 imgur 相册有两张，容易看反：

| | 上图 | 下图 |
| --- | --- | --- |
| 实际是 | **原版 Supermodel**（全分辨率） | **他的实验版**（半分辨率 + 交错） |
| 半透明边缘 | 硬边（每个雾四边形的几何边） | 软、糊 |
| 不透明几何（树/车/路面） | 锐利 | **也糊了** |
| 横向条纹 | 无 | 有（他自己说的 interlacing） |

判断依据：他原话是 *"you can see the interlacing"*，而交错线在**下图**；上图那圈“清晰轮廓”
是全分辨率下才有的，正好是反证。

这一点很重要：它意味着**验收方向是“变软”而不是“更清晰”**。同时也说明他的实验版软过头了——
除了半透明层，连不透明几何一起糊了；本实现只作用于 `trans1`/`trans2` 两个附件，不透明几何
逐像素不变。

Supermodel 从 0.3a 起两层半透明都是**全分辨率**：`trans1`/`trans2` 两个附件，每层按深度写入
只保留最靠前的一个面，最后由合成器套两层公式。这个模型在大多数游戏里没问题，但它和真机的
差别在《Sega Rally 2》的雪地扬雪上非常明显。

## 2. 原版为什么会在 Sega Rally 2 上糊车

这是**推断**，不是从硬件文档推出来的，但和观察到的现象一致：

1. 雪雾由几十个半透明四边形组成，它们按 `transLSelect` 位分散到 `trans1` / `trans2` 两层。
2. 全分辨率下两层覆盖同一像素是常事，于是合成器的「两层同时覆盖 → 不透明」规则
   （`t1.a * t2.a > 0` 时输出 `mix(t1, t2)` 且 alpha = 1）在整片雪雾上大面积成立。
3. 结果：本该是 30–50% 的雾变成了实心灰白，后面的车被彻底盖住。

真机上这个规则也存在，但两层各自只占一半像素，**同一像素不会同时属于两层**，所以规则在
覆盖区域里基本不触发；未被覆盖的那一半像素保持底色的清澈。这就是“车能从雾里看出来”的来源。

## 3. 三种模式的定位

| 值 | 名称 | 半透明怎么做 | 用途 |
| --- | --- | --- | --- |
| `0` | 原版（双层，逐位兼容） | 两层全分辨率 + 2-Subpass 片上合成 | 默认，逐位兼容旧版画面；最快 |
| `1` | 逐像素排序（OIT） | 每 Priority 内固定槽位 A-Buffer，按深度排序后合成 | 超过两层重叠不画错；多占显存 |
| `2` | 街机真机（棋盘格） | 两层各占一半像素，合成时按 2×2 组还原 | 密集效果不再糊白；半透明整体变淡 |

## 4. 实现

### 4.1 配置与钳位

`CVulkan3D::CVulkan3D()`：

```cpp
m_alphaSortMode = std::clamp(config["AlphaSortMode"].ValueAsDefault<int>(0), 0, 2);
if (sortEnv != nullptr && sortEnv[0] >= '0' && sortEnv[0] <= '2' && sortEnv[1] == '\0')
    m_alphaSortMode = sortEnv[0] - '0';      // SM3D_ALPHA_SORT=0/1/2
```

`DefaultConfig()`（`Main.cpp`）允许值改成 `{ 0,1,2 }`。设置界面「高级设置 → 半透明排序」第三项
「街机真机（棋盘格）」/ `Arcade hardware (checkerboard)`。每局读一次，重开生效（和 MSAA 一样）。

启动日志会写明最终值（任何一个降级路径都可能把 `1` 退回 `0`，没有这行日志就没法解释截图）：

```
Vulkan: transparency sorting 2 (arcade checkerboard, half resolution).
```

### 4.2 光栅化掩码（`real3d.frag`）

在 `main()` 开头、`ShadeFragment()` **之前**：

```glsl
if (ubo.renderOptions.y > 0.5 && pc.extra.z > 0.5)
{
    const int parity = (int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 1;
    const int wanted = (pc.extra.z < 1.5) ? 0 : 1;   // 附件 1 = trans1 = 偶格
    if (parity != wanted)
        discard;
}
```

- `pc.extra.z` 是**附件号**（0 = 颜色、1 = trans1、2 = trans2），不是 bucket 号。`DrawBucket`
  里 `attachment = (layer == 0) ? 0 : (layer == 1 ? 2 : 1)`，注意 bucket 1 是 trans2。
- 放在着色之前，所以被丢弃的片元连纹理采样都不做。雪雾覆盖大半屏时这一步省掉约一半的
  半透明着色开销。
- 用**屏幕像素**奇偶，不是街机像素（见 5.1）。

### 4.3 开关怎么传（两条路径）

| 开关 | 载体 | 为什么 |
| --- | --- | --- |
| 是否棋盘格 | `ViewportUBO.renderOptions.y`（原 `yzw` 未用） | 每局固定、每个视图相同；用它就不用再造 12 条场景管线 |
| 是否跳过还原 | `LayerPush.checkerRaw`（2D 合成器 push constant） | 只在 2D 合成阶段有效 |

`DrawBucket()` 填 UBO 时写 `renderOptions[1] = (m_alphaSortMode == 2) ? 1.0f : 0.0f`。

### 4.4 渲染路径：为什么不用双 Subpass

模式 2 走**动态渲染**（和 OIT 一样），不走 `CreateScenePass()` 那条 2-Subpass 路径。原因很硬：

> `subpassLoad()` 只能读**当前像素**的 input attachment，没有邻域寻址。而棋盘格还原必须读
> 2×2 组里的另外三格。

代价是拿不到双 Subpass 的片上合成收益（4K 4x 下那一项约 +6%）。这是模式的固有成本，不是
实现选择。

`m_scenePrecomposited` 保持 `false`，所以 2D 合成器会执行自己的半透明合成块。

### 4.5 合成与还原（`composite.frag`）

每个 2×2 组按层拆开：`trans1` 占 (0,0) 和 (1,1)，`trans2` 占 (1,0) 和 (0,1)。

**只有一层占格**时：

```
sum   = a00 + a11 + a10 + a01          // 另一层这两格恒为 0
rgb   = (Σ rgbᵢ·aᵢ) / sum              // 预乘累加再反除，避免边缘发暗
alpha = sum / 4
out   = rgb·alpha + base·(1 - alpha)
```

`/4` 就是“有效减半”的来源：**四格里只有两格属于这一层**，所以满覆盖得到的是一半 alpha，
而不是把一格铺满整组。

**两层都占格**时：输出不透明（Daytona 2 的“双层重叠变实心”），颜色取两层各自预乘平均后的
`mix`。判据是 **`sum1 >= 1.0 && sum2 >= 1.0`**（每层两格平均 alpha 至少 0.5），不是“只要有覆盖”——
阈值取错会把一层薄雾飘过阴影的地方判成不透明，车底出现硬边黑梯形（见第 7 节）。

**两组都空**时：保持原画面（不写 alpha，保住 `Render2D Bottom` 依赖的场景 alpha）。

### 4.6 2D 合成器接线

- `CVulkan2D::SetArcadeCheckerboard(bool)` / `SetCheckerRaw(bool)`，由 `CVulkan3D::EndFrame()`
  每帧推送（`m_scenePrecomposited` 旁边）。
- `LayerPush` 新增两个 `uint32_t`，`composite.frag` 的 push block 同步加字段，`sizeof(LayerPush)`
  自动带到 pipeline layout。

### 4.7 测试钩子

```
SM3D_ALPHA_SORT=2     强制模式 2（不动 ini）
SM3D_CHECKER_RAW=1    跳过 2×2 还原，且两层改用最近邻采样
```

`SM3D_CHECKER_RAW` 必须同时换最近邻采样，否则看不见图案：场景是 2480×1920，合成到
3840×2160 时双线性采样会把 1px 棋盘格平均掉——只关掉盒式还原时 raw 和默认看起来一模一样
（第一次实现就踩了这个坑）。

## 5. 关键决策

### 5.1 棋盘格用屏幕像素，不用 496×384 街机像素

- 原生分辨率下两者等价。
- 4K 下（`496×384 × 5 = 2480×1920`）按街机像素做会得到 5×5 的粗方块，看着像纱窗；
  按屏幕像素做，再经 2×2 盒式还原，得到的是一层平滑的半强度雾。
- 想还原真机那种粗图案是模式 C，未做（见第 10 节）。

### 5.2 还原用 2×2 盒式，不用“自己的那一格”

- 单格采样会得到 1px 网点：4K 下是纱窗，不是 CRT 上的半分辨率观感。
- 盒式恰好等于 CRT 光斑对四个子像素的积分，也恰好给出 “effectively halved” 的 alpha。

### 5.3 预乘累加

两层各自的两格在边缘处 alpha 不同（一边满覆盖一边空）。直接平均直通 alpha 的颜色会让边缘
偏暗，所以按 `Σ(rgb·a)/Σa` 求颜色，alpha 单独求。

### 5.4 保留“双层重叠 → 不透明”

这是 `dukey` 明确描述过的硬件行为，也是模式 0 已有的规则（`scene_composite.glsl` 与
`composite.frag` 各一份）。在棋盘格模式下它被定义在 **2×2 组**上，而不是单像素上——因为
两层各占一半像素，单像素永远不会同时属于两层。

## 6. 验证

### 6.1 帧对齐

`-At` 是墙钟秒数，启动开销每次差几毫秒，两次运行可能差 4–5 个游戏帧。现在用帧号钩子
`SM3D_SCREENSHOT_FRAME`（`Capture-Frame.ps1 -Frame <n>`）：

- 计数与延迟读档共用 `simFramesThisSession`，所以帧号**包含**等开机的 172 帧（默认 3 秒）。
- 只在“携带仿真步”的那个 present 上拍。相位锁定模式下一帧会被呈现 2–8 次，alpha 各不相同，
  用墙钟触发无法说明落在哪一次。
- 还需要 `-Ini "RefreshRateMode=0"` 把插值 alpha 固定成 1.0。实测：

  | 配置 | 同模式两次运行的差异 |
  | --- | --- |
  | `-At` | mean 4.4 / 255（差 4–5 帧） |
  | `-Frame` | mean 1.84 / 255（帧对了，alpha 还在飘） |
  | `-Frame` + `RefreshRateMode=0` | **mean 0 / max 0（逐像素相同）** |

拍原生分辨率加 `-Ini "XResolution=496;YResolution=384;IntegerScale=1"`，窗口变成 496×384，
棋盘格 1:1。

这 1.2% 的分支覆盖率是靠临时把「双层重叠 → 不透明」分支涂成品红统计的（见第 7 节），
统计完已删除。

### 6.2 数据

存档 `Data/Saves/srally2dx.zst10`（SR2 Export/Deluxe，雪地吸引模式），`-At 6`：

| 项 | 结果 |
| --- | --- |
| 同步验证（`Validate-Vulkan.ps1 -Mode sync -Ini AlphaSortMode=2`） | `0 VUID / 0 sync hazard / 0 error` |
| 车身区域平均逐通道差（模式 0 vs 2） | 5.62 / 5.60 / 5.47（0–255），峰值 152 |
| 整帧平均逐通道差 | 2.23 / 2.24 / 2.21 |
| 帧时间 | 两种模式都跑满 120 Hz，8.33 ms/帧 |

截图上，车尾的 `COROLLA` / `Castrol` 原本被灰雾罩成低对比，模式 2 下恢复饱和；树、护栏、
雪坡与模式 0 **逐像素相同**（不透明几何不经过这条路径）。

## 7. 已知问题与取舍

1. **半透明整体变淡**：这是模型本身的结果（另一格显示底色），不是 bug。真机在 CRT 上被
   扫描线平均，原机画面本来就是半强度；LCD 1:1 上会更明显。
2. **「双层重叠 → 不透明」的阈值必须收紧**。最初写成 `sum1 > 0.0 && sum2 > 0.0`（只要有覆盖），
   结果在 SR2 雪地上，一层薄扬雪飘过车身阴影就把整块阴影判成不透明，车底出现**硬边黑色
   梯形**（`sdl3_reports/2026-10-26-checkerboard-alpha/shadow_rule_ab.png` 左侧）。
   现在改成 `sum1 >= 1.0 && sum2 >= 1.0`，与“完全去掉这条规则”在该帧上逐像素相同，
   只在真正密集的双层重叠时触发。
   若仍有游戏被误触发，删掉这条分支即可退化为纯半分辨率混合。
3. **比模式 0 慢**：透明层要多存一遍、由 2D 合成器读回来，拿不到片上合成。
4. **观感与本机不符时**：`SM3D_CHECKER_RAW=1` 可以看原始图案，用于判断是“图案不该被抹平”
   还是“半透明不该这么淡”。
5. **与 LOD 交叉淡化不兼容（2026-10-27，Daytona 2 瀑布）**。交叉淡化把同一个面画两遍、
   alpha 互补；原版靠“两层都在 → 不透明”还原成不透明的白瀑布。棋盘格每层只写一半像素，
   两层各自只剩一半覆盖，合成出的 alpha 本来就只有原版的一半：
   - 实测（`daytona2.zst2`，第 190 帧，MSAA 4x）：瀑布区蓝色像素占比 原版 4.87%、OIT 3.92%、
     棋盘格 **20.13%**；棋盘格在 MSAA 1x 下 12.09%。
   - 从着色器读出该区域的覆盖：20.2% 的像素只有 trans1 一层（平均 alpha 0.65、trans2 为空），
     27.2% 两层都在（0.66 / 0.54）。单层那部分无论怎么调阈值都只能得到半强度。
   - 试过两条修正（“两层合计覆盖 ≥ 1.9” 与 “单层覆盖 ≥ 1.8” 时判不透明），前者把蓝降到
     7.34% 但瀑布主体变半透，后者反而升到 18.08%。两者都没有保留。

   结论：这是按像素拆层后的信息丢失，不是阈值问题。**模式 2 只能当试验模式**，用于雪雾/
   尘土这类密集粒子；有交叉淡化或大片半透明水幕的游戏用模式 0 或 OIT。
   数据、截图与复现命令见 `sdl3_reports/2026-10-27-checkerboard-hd/README.md`。
6. **后续若要做“模式 2 的交叉淡化还原”**：需要保留逐像素的两层覆盖（例如另存一张
   每像素的 8 位 alpha 对，或在着色器里按 `alpha1 + alpha2 ≈ 1` 写标记位），不能再从 2×2 组
   反推。入口见 `issues_waitlist.md`。

## 8. 代码改动索引

| 文件 | 改动 |
| --- | --- |
| `Shaders/real3d.frag` | 透明层片元的奇偶丢弃（`main()` 开头） |
| `Shaders/composite.frag` | push block 加 `checkerboard` / `checkerRaw`；2×2 组还原；raw 分支的最近邻采样 |
| `VulkanCommon.h` | `ViewportUBO.renderOptions.y` 的语义（文档） |
| `Vulkan3D.cpp` | 模式钳到 0–2；`SM3D_ALPHA_SORT` / `SM3D_CHECKER_RAW`；UBO 写 `renderOptions[1]`；`EndFrame` 推两个标志；启动日志 |
| `Vulkan3D.h` | `m_checkerRaw`；`m_alphaSortMode` 的注释补模式 2 |
| `Vulkan2D.cpp/.h` | `LayerPush` 两个字段；`SetArcadeCheckerboard` / `SetCheckerRaw`；三处 push 初始化 |
| `OSD/SDL/Main.cpp` | `AlphaSortMode` 允许值 `{0,1,2}` |
| `OSD/SDL/Gui.cpp` | 下拉框第三项；`alphaSort` 钳位 0–2；预设匹配钳位同步改成 0–2（否则模式 2 会被当成模式 1 命中预设） |
| `OSD/SDL/Localization.cpp` | 新选项与 tooltip 的英文条目 |
| `Tools/Validate-Vulkan.ps1` | 新增 `-Rom` 透传（存档的 set 名与 zip 文件名不一致时必需） |
| `usermanual.md` / `usermanual_chs.md` | 玩家可见的说明与推荐场景 |

着色器改完必须重跑 `Scripts/CompileVulkanShaders.cmd`（生成 `.spv` 与 `.spv.h`）。

## 9. 复现命令

```bat
:: 截图（模式 2）
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Capture-Frame.ps1 ^
  -State <abs>\Data\Saves\srally2dx.zst10 -Rom E:\EMU\model-3-romset\srally2x.zip ^
  -Frame 345 -Ini "RefreshRateMode=0" -Env "SM3D_ALPHA_SORT=2" -Out mode2.png

:: 街机原生分辨率（窗口 496x384，棋盘格 1:1）
  ... -Ini "RefreshRateMode=0;XResolution=496;YResolution=384;IntegerScale=1"

:: 原始棋盘格（测试钩子）
  ... -Env "SM3D_ALPHA_SORT=2;SM3D_CHECKER_RAW=1" -Out mode2_raw.png

:: 同步验证
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Validate-Vulkan.ps1 ^
  -State <abs>\Data\Saves\srally2dx.zst10 -Rom E:\EMU\model-3-romset\srally2x.zip ^
  -Bin dist\Supermodel-VK -Ini "AlphaSortMode=2" -Frame 345 -Mode sync
```

> 做对比前**先问用户要存档**。从启动画面用 `-At` 猜时间抓不到具体场景：游戏要先跑完启动和
> 几十秒的菜单/吸引循环，不同赛道还会轮换。

## 10. 后续可选方向

- **模式 C：按街机像素格做棋盘格**（496×384 的 5×5 块，还原组同粒度）。最接近 `dukey`
  截图里的粗图案，但 4K 下是明显方格；需要在着色器里知道视口与原生分辨率的比例。
- **把 `SM3D_CHECKER_RAW` 提升成正式选项**（下拉框第四项），让玩家自选“锐利网点 / 平滑”。
  目前判断是价值不大：4K 下凑近是网点、远看等于默认。
- **模式 C 的粗粒度还原**：现在 2×2 组固定是屏幕像素；若按街机像素格做，组粒度也要跟着变，
  需要着色器知道 `496×384` 与当前视口的比例。

## 11. 参考资料

- `dukey` 关于真机半透明的说明（2026-10-26 对话，含 imgur 实验截图）。
- `Src/Graphics/New3D/PolyHeader.h`：`TranslucencyPatternSelect()`（`header[6] & 0x20000`，
  代码里当“选哪一层”用，注释猜是 "stipple alpha"）、`TranslucencyMode`（低 3 bit，解析但
  从未读取）、`EdgeOnTranslucency()`（`header[1] & 0x80`，有实现但没有调用者）。这三个字段
  的身份仍未确认，值得在拿到更多硬件资料后复核。
