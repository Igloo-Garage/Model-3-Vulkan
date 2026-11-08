# Issues from users — 用户反馈台账（GitHub / 论坛）

> 用途：一条用户反馈一行索引，写清「他报了什么 / 我们的结论 / 状态 / 证据在哪」。
>
> - **已修复**：技术细节在 `devdocs/CHANGELOG.md` 对应日期，证据与复现命令在 `sdl3_reports/`。
> - **未解决**：跟踪条目在 `issues_waitlist.md`（症状、已排除项、下一步）。
> - **对外回复**：直接取每条下面的「回复口径」，措辞已按「先结论、再依据」写过。
> - **当日汇总报告**：`sdl3_reports/<日期>-user-issues/`（证据、截图、文件清单）。
>
> 本文件只做索引与对外口径，不重复技术细节。

## 一览（最新的在最上面）

| # | 日期 | 报告者 | 一句话 | 状态 |
| --- | --- | --- | --- | --- |
| — | 2026-11-02 | YouTube 评论 | 「single core heavy as fuck，3.8 GHz Xeon v4 跑不满，当年 Core 2 2.93 都满速」 | **已定位**：发布版还是 C 解释器；另可能有 GPU 墙。JIT 版发布后按口径回复 |
| #17 | 2026-11-01 | wahrheit360 | Xbox 手柄转向过于灵敏，找不到灵敏度选项 | **已处理**：功能本来就有，标签页太隐蔽；UI 已改（下一版） |
| #16 | 2026-11-01 | —（issue #16） | Scud Race Deluxe 仪表盘灰板、看不清 | **未修复**：已知限制，改用 Twin/DX 版本 |
| #15 | 2026-11-01 | WishIWasSanta | Daytona 2 行进中相机自己上下变、低到看不见路面 | **待确认**：判断为视角键（默认 Y），已加切换日志 |
| — | 2026-11-01 | gamerg_（评论） | macOS 4K 屏在系统「默认字号」下选 2160p 无效，只输出 1080p | **已修复**（下一版，待 Mac 真机复测） |
| — | 2026-11-01 | godmd5（评论） | 导出的贴图布满白点 / 虚线 | **已修复**（下一版） |

---

## — 「single core heavy as fuck」（YouTube 评论，2026-11-02）

**报告**：「single core heavy as fuck, can't run full speed on 3.8ghz xeon v4, I remember original
model 3 emu playing full speed on core2 2.93ghz.」

**结论**：「单核重」属实，但要拆成两条独立的墙，修法不同：

- **CPU 墙（他的主要症状）**：所有已发布版本跑的都是 C 解释器（无 JIT、无 idle skip）。本机基准：
  同一 Daytona 2 场景解释器 55.2 Hz——连逻辑帧都跑不满，Xeon v4 的单核比本机弱，只会更低；
  JIT 版同场景 224.5 Hz。他记忆里 Core 2 满速是真的，但那是 496×384 原生输出 + 无滤镜 + 老渲染器，
  工作负载和现在的默认设置不同。
- **GPU 墙（顺带排查）**：「支持 Vulkan」≠ 性能够。Xeon E5 v4 平台没有核显（E3 v4 也只有 Iris Pro），
  常配老旧独显。笔记本实机复现（UHD 620）：2x 内部分辨率 GPU 100% → 33 fps、CPU 只有 22% 且降到
  1.3 GHz（在等 GPU + 15W 功耗共享）；内部分辨率改原生 496×384 → 满 57.5 fps。
- **判据**：游戏内统计面板（Tab 上方键）——CPU% 顶满 = 等 JIT 版；GPU% 100% = 降内部分辨率 / 选低配预设。

**处理**：JIT 在 0.9.8.7（未发布，笔记本实测中）；同日还把 Android 场景构建三项优化反哺回 PC
（`devdocs/CHANGELOG.md` 2026-11-02）。

**回复口径**（英文，建议 JIT 版发布后再发）：见下。

**证据**：笔记本 `C:\EMU\Model3-Vulkan.v0.9.8.7.for.Windows\Data\Supermodel.log` 的 `PACE:` 行
（sim 7.93 ms / render 19.71 ms / GPU 100%）；`sdl3_reports/2026-10-30-ppc-x64-jit/`。

---

## — macOS 4K 屏上分辨率设置无效（gamerg_ 评论，2026-11-01）

**报告**：「选了 2160（4K），但我的 Apple 显示器偏好是默认字号，它把 4K 当成 1080p，输出不变成
宽屏；必须把桌面切成『更多空间』（桌面图标变小）才生效。别的 Supermodel 构建能正常切到 2160p。」

**结论**：症状成立，机制要纠正一下：

- macOS 的「默认字号」**不是把面板跑成 1080p**，面板仍是 4K；系统给应用的是 1920×1080 的逻辑坐标
  + 2 倍后备缓冲。申请了高密度 drawable 的应用拿到的就是 3840×2160。
- 我们的 bug 有两层：① 游戏窗口从未申请 `SDL_WINDOW_HIGH_PIXEL_DENSITY`，Cocoa 给的是 1x
  drawable；② `CalcScreenGeometry()` 读的是窗口的 point 尺寸，整数倍缩放 / CRT 预设 / 硬件预设
  的钳制都按 1080 行算。所以「选了 2160 也没用」，而且 4K 硬件预设会被静默钳成 1080p。
- 「输出不变成宽屏」是另一件事：默认画面就是街机 4:3 居中 + 左右黑边，那是 `WideScreen` /
  `WideBackground` 的开关，与本次修复无关。
- 「别的构建正常」无法核实（手边没有那个包）；它们可能申请了高密度 drawable，也可能直接切了
  3840×2160 的显示模式。我们走的是无边框 + 保持桌面模式，所以受桌面缩放档位影响最大。

**处理**（本分支，未发版）：渲染几何改用设备像素，输入仍用窗口坐标；游戏窗口在 macOS 上申请
高密度 drawable；游戏内 Tab 面板的 `DisplayFramebufferScale` 改为真实比例。详见
`devdocs/CHANGELOG.md` 同日条目、`devdocs/FIXES.md` 案例 53，以及 `AGENTS.md` 的「macOS 的
『点』与『设备像素』必须分开」。**Windows 已编译 + 启动验证，macOS 无真机未验证。**

**回复口径**：你的观察是对的方向，但面板其实一直跑在 4K —— 是我们没申请高分辨率缓冲区，
所以按 1080p 渲染再放大；现在已改成原生 drawable，选 2160 就是真 2160。注意默认仍是街机 4:3
加黑边，要填满两侧请开「宽屏」。

**证据**：无截图（无 Mac 真机）；代码位置见上。

---

## #17 — How to adjust the steering sensitivity（2026-11-01）

**报告**：用 Xbox Series X 手柄玩，转向过于灵敏，问能不能调。

**结论**：不是缺功能。设置里有 **「力反馈 / 灵敏度」→「手柄转向」→「摇杆转向平滑」** 四档
（关闭 / 轻度 / 中度 / 强力），只是英文界面的标签页原来只写 `FFB`，手柄玩家不会去点；页顶
提示还写成「这一页的设置才会生效」，把同页的灵敏度一起否定了。

**处理**（本分支，未发版）：

- 英文标签页 `FFB` → `FFB / Sensitivity`（`Localization.cpp`）。
- 页顶警告收窄到力反馈分组，并说明「灵敏度」和「手柄转向」随时可用（`Gui.cpp`）。
- 「摇杆转向平滑」的 (?) 提示补一句「这几档是按没有方向盘的手柄玩家调的」。
- 四档的实际作用：一阶低通 35 / 50 / 75 ms、每帧最大变化 16 / 10 / 7 %、中心曲线
  gamma 1.25 / 1.5 / 1.7（`Src/Inputs/Inputs.cpp` 的 `s_steeringSmoothing[]`）。
- 设置窗口里改完要**下一局**生效（运行期每帧读的是 `s_runtime_config` 副本）。

**回复口径**：功能一直都在 → 路径 → 手感是按没有方向盘的手柄玩家调的 → 下一局生效。

**证据**：无截图（纯文案与标签改动）；代码位置见上，`devdocs/CHANGELOG.md` 同日「UI：
「力反馈」页的标签与提示语易错过」。

---

## #16 — Scud Race Deluxe 仪表盘灰板（2026-11-01）

**报告**：`scuddx`（Export, Deluxe, Revision A）比赛中仪表盘变灰、读不出内容。

**结论**：

- **不是坏 dump**：`Games.xml` 的 CRC32 逐文件校验，`scuddx.zip` 41/41、父包 `scud.zip`
  41/41 全匹配；加载器本来就按 CRC 识别 set，坏一字节进不了游戏。
- **不是 Vulkan 特有**：同一份 raw state、同一时刻，Vulkan 与 LegacyOG（New3D）的灰板
  **逐像素一致**。
- **只在 Deluxe**：`scud`（Twin/DX）一直都能正常显示。
- **长期缺口**：从 MAME 时代起就没解决过，属 Supermodel/MAME 一脉的模拟缺口，不是本分支回归。
- CONDITION 视口 0 个贴图多边形、TileGen 层为空这两条在 Twin 上同样成立，**都不是成因**。

**状态**：未修复。跟踪在 `issues_waitlist.md` 的「2026-10-24 — Scud Race Deluxe HUD」。

**回复口径**：不是坏 dump → 不是 Vulkan（New3D 同帧逐像素一致）→ 只在 Deluxe，Twin/DX 正常
→ 从 MAME 时代就没解决 → 请改玩 Twin/DX。`scudj.zip` 的 CROM 与 `scuddx` 相同，也是 Deluxe。

**证据**：`sdl3_reports/2026-11-01-scuddx-hud/`（`deluxe-40s.png`、`legacy.png`、
`twin-40s.png`、`twin-slot1.png`、`scuddx-raw.st`、`scud-raw.st`、`tilegen-*.png`、
`log-*.txt`）。

---

## #15 — Camera angle problem in Daytona USA 2（2026-11-01）

**报告**：行进中相机有时上下移动，有时低到看不见前方；报告者强调画面里的灰车是他自己的车，
不是「看着前车保险杠」的视角。

**结论**：Daytona 2 没有随机相机。机台有 4 个视角键（VR1 红 / VR2 蓝 / **VR3 黄 = 默认跟车** /
VR4 绿），本分支 `VRCycle` 默认开启并把它们合并成一个「下一视角」键；标准手柄预设是
`InputVRNext = "KEY_D,JOY1_BUTTON4"`，即手柄 **Y**。按一次换一档，其中一档就是低机位，
会一直停在那里直到再按一次。若把循环关掉，Y 变成 VR2，而**按下左/右摇杆**才是 VR3/VR4，
跑起来更容易误触；非标准手柄（SDL 不认的）按钮编号还会整体错位。

**状态**：待用户确认。**新增日志**：切视角时打 `Input: view cycle -> VRn.`
（`Src/Inputs/Inputs.cpp`），下次拿到 `Data/Supermodel.log` 就能区分「视角键被触发」与
「游戏自己动了相机」——后者才是真 bug。

**回复口径**：不是随机相机，是视角键 → 建议改掉默认的视角键（取消单键循环，或解绑 Y）→
改完还跳就发日志。

**证据**：无截图（诊断），只加了日志；代码位置见上。

---

## 评论 — 导出的贴图布满白点 / 虚线（2026-11-01，godmd5）

**报告**：四张 Fighting Vipers 2 角色脸部贴图，整张脸覆盖白点阵与竖虚线，怀疑导出功能不完善。

**结论**：那些点是 **alpha = 0** 的像素，RGB 完全正常。Model 3 没有平滑 alpha（T1RGB5 用
bit 15、4-bit 格式用 alpha nibble 当 1-bit 掩码），美术拿这一位做**抖动**表示半透明；而
运行时对「不读 alpha」的材质会强制成 1。导出以前把这些位原样写进 PNG，看图软件就画成白点。

**处理**：按材质决定 alpha——不读 alpha 写 255，读 alpha 保留原机掩码；同一哈希后到读 alpha
的材质会触发重导。重叠的 288 个哈希 RGB 逐像素一致，89 张从带 alpha 变为全不透明。新增
`Tools/inspect-texturedump.py`。

**回复口径**：不是取图/位深错，是 1-bit 透明抖动的语义；判据是「白底点白、黑底点黑 →
只能是透明度」；新版导出与运行时采样一致。

**证据**：`sdl3_reports/2026-11-01-dump-alpha-dither/`（`before-after.png`、
`alpha-demo.png`、`alpha-test*.png`）。

**后续（同日）**：他自己做 HD 包时又报了两个现象，都还没定性：

1. 布料黑边 → 用 `sep alpha for mip maps` 后消失，已确认是 mip 渗色（透明像素的黑色 RGB
   混进低层级）。
2. **Spikeout Final Edition 女生房间的弧形隔墙**：一块成矩形灰块、一条竖向木纹（背景物体
   透出来）。两种可能：① 该墙面读 alpha（同一张图里既有壁纸又有镂空格栅），他超分后的
   alpha 不再是二值 → alpha test / 两趟路径把墙挖穿；② 与他的包无关，是深度/几何本身的问题。
   判据：按 **F4** 关掉 HD 是否消失；镜头移动时是闪烁（深度）还是原地不动（alpha）。
   注意 HD 替换只改颜色采样，不可能移动几何，所以「穿模」不会由包直接引起，只能通过 alpha。

回复要点（已写进手册的 HD 一节）：模型只超分 RGB，alpha 用最近邻或事后二值化回 0/255；
`separate alpha` 只保证模型不碰 alpha，缩放滤波仍会抹开。

---

## 下一版的对外口径

四条可以按顺序进 release notes：① 导出贴图白点修复（用户可见）；②「力反馈 / 灵敏度」
标签页改名与提示修正（#17）；③ 已知限制：Scud Race Deluxe HUD（#16，写明绕过方案）；
④ Daytona 2 相机「自己动」其实是视角键（#15）。
草稿正文见 `devdocs/CHANGELOG.md` 的「下一版 Release Notes 草稿（2026-11-01 起）」。
