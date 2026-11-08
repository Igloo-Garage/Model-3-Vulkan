# INDEX — 项目文档地图

> **新会话先读 `AGENTS.md`，再按需查这里。**
> 产品名是 **Model 3-Vulkan**（可执行文件 `Model3-Vulkan.exe`）；GitHub 仓库名是
> **Model-3-Vulkan**；控制台横幅是
> `Model 3-Vulkan v0.9.9.6 - Igloo-Garage`。
> 根目录的 `.md`：`AGENTS.md`（长期上下文）、`README.md`（GitHub 首页）、
> `RELEASE_NOTES.md`（当前版本的 GitHub Release 正文，中英对照）、
> `LICENSE-ADDITIONAL-TERMS.md`（GPLv3 §7 附加条款）、`TRADEMARK.md`（品牌与商标政策）、
> `usermanual.md`（玩家手册英文版，发行包会带）、`usermanual_chs.md`（中文精简版）、
> `INDEX.md`（本文件）、`vulkan_issues.md`（Vulkan 画面问题档案）、
> `issues_waitlist.md`（已确认但暂缓的问题）、`issues_from_users.md`（用户反馈台账：
> 一条反馈一行的结论 / 状态 / 对外口径）。
> 第三方组件与许可声明在 `Docs/NOTICE.md`（打包时进 `Data/NOTICE.md`）。
> 其余项目文档全部在 `devdocs/`，按主题命名，文件名即用途。

## 一、最常读的三份

| 文件 | 是什么 | 什么时候读 |
| --- | --- | --- |
| `devdocs/CHANGELOG.md` | 每轮改动详述 + 可直接贴到 GitHub Release 的中英短日志（约 195 KB） | 想知道「最近改了什么 / 为什么这么改」；发版前写 release notes |
| `devdocs/TECHLOG.md` | 深入技术日志，按子系统组织（约 370 KB，很长） | 要动某一层（渲染/音频/同步/GUI）之前，先查对应章节 |
| `devdocs/FIXES.md` | 可复用的排错模式：症状 → 定位 → 根因 → 修法（约 163 KB） | 遇到"以前好像见过"的问题时 |

## 二、按主题查

| 文件 | 内容 |
| --- | --- |
| `devdocs/ROADMAP.md` | 功能盘点 + 未来优化路线图（P0/P1/P2），Batch 计划的素材来源 |
| `devdocs/RUNBOOK-HD-TEXTURES.md` | HD 纹理系统的模块化与移植手册：可移植边界、接口契约、七阶段流程、后端适配表、验收矩阵 |
| `devdocs/RUNBOOK-RELEASE-LINUX-MACOS.md` | Linux / macOS 发布构建与打包 runbook：LAN 笔记本远程编译、Ubuntu 24.04/g++-14、macOS Intel + Zig、Apple Silicon CI、MoltenVK/vtool/lipo、SHA256SUMS 与 GitHub release 流程 |
| `vulkan_issues.md` | Vulkan 画面问题档案：症状、复现命令、**被排除的假设**、根因与修法（VF3 海水/2D 天空、截图工具） |
| `issues_waitlist.md` | 已确认但暂不优先处理的问题与延后的功能/验证/文档待办：症状、复现、已排除项、下一步（当前：下一版 Vulkan 1.1 兼容下探、macOS Retina 4K 真机复测、Scud Race Deluxe HUD viewport、棋盘格/LOD 交叉淡化、低分辨率渲染文档与旧笔记本 A/B 测试） |
| `issues_from_users.md` | 用户反馈台账（GitHub issue / 评论 / 论坛）：每条写报告内容、我们的结论、状态、证据链接与对外回复口径；已修复项指向 `CHANGELOG.md`，未解决项指向 `issues_waitlist.md` |
| `devdocs/SYNC.md` | 57.524 Hz / VRR / 固定刷新率的同步诊断：实测数据与做不到的部分 |
| `devdocs/DSP.md` | SCSP DSP 与 DSB 的详细分析（FV2 爆音、Sega Rally 2 双 BGM 等） |
| `runbook_sound_fix.md` | 音频系统完整修复与高保真升级手册：4.0/5.1 环绕与巴特沃斯上混、无锁 WAV/FLAC 录制器、SCSP 4-tap 立方插值、DSB Float 直通、以及 `VRCycle` 触发 Daytona 2 隐藏选歌秘技的排错实录 |
| `saves_fix.md` | 存档读档修复与移植手册：53C810 PCI/网络板/中断线/DSB MPEG 缺失状态、加载顺序、旧档回退、172 帧延迟的来龙去脉与 Android 移植清单 |
| `devdocs/AlphaSortMode2.md` | 半透明第三档「街机真机（棋盘格）」：真机机制、公式推导、关键决策、验证数据与取舍 |
| `devdocs/DEV-STATS.md` | 开发投入统计（工时、会话、文件时间戳） |
| `devdocs/README-DRAFT.md` | 下一版 README 草稿（面向玩家） |
| `devdocs/REDDIT-POST.md` | 展示帖草稿 |
| `devdocs/plans/` | 设计方案：高刷插值、SDL3 双目标重构、Daytona 2 眩晕复测清单、B 站解说词 |
| `devdocs/prompts/` | 当初给 AI 会话用的执行提示词（历史参考，不再维护） |
| `devdocs/archive/picture-fix/` | 2026-10-22 那次画面排查的归档：截图、临时工具、根因分析 |
| `sdl3_reports/` | 每项优化的完整前后对比（命令、样本、截图、验证矩阵），**有自己的 `README.md` 索引** |
| `MacOS/MacOS_performance.md` | macOS 兼容性与性能报告：启动失败的三个根因、实机验证记录、`.zst` 的 macOS 14 门槛、Vulkan vs OpenGL 的实测对比与后续计划 |
| `weekly_dev_reports/` | 按日期归档的诊断日志与截图（2026-10-27 Scud Race 读档问题保留在此，后续回归时再处理） |
| `v9.8.3a_upload/` | 发版暂存目录：GitHub 用文档 + Windows 发布包 + `SHA256SUMS.txt` |
| `Tools/Remote/` | 部署到副机、取日志、干净退出的脚本 |
| `Tools/Capture-Frame.ps1` | 载入存档或启动游戏，由模拟器自己截图并返回 PNG 路径；`-RawState` 可导出 raw `.st` |
| `Tools/Restore-TestIni.ps1` | 还原被中断的测试脚本留在 ini 里的临时覆盖（典型症状：设置界面马赛克/闪动） |
| `Tools/Validate-Vulkan.ps1` | 同步验证 / GPU 辅助验证跑一帧，汇总日志里的 VUID / hazard 数量 |
| `Tools/Diagnostics/` | 2026-10-22 画面排查用的一次性脚本（已冻结，不再维护；其中 7 个与 `devdocs/archive/picture-fix/tools/` 逐字节相同） |
| `Docs/README-VK.txt` | ⚠️ 不是开发文档：玩家手册，`build.bat` 会复制进 `dist` |
| `usermanual.md` | 玩家手册（英文，主版本）：两个 exe 的分工、功能与优势、**推荐设置与四档预设**、热键、存档、排错、命令行。`build.bat` 会复制进 `dist` |
| `usermanual_chs.md` | 上面那份的中文精简版（只保留操作与推荐值） |

## 三、旧名 → 新名（以前的口头/习惯名）

| 旧 | 新 |
| --- | --- |
| `update.md` | `devdocs/CHANGELOG.md` |
| `develop.md` | `devdocs/TECHLOG.md` |
| `fix.md` | `devdocs/FIXES.md` |
| `progress_review.md` | `devdocs/ROADMAP.md` |
| `ArcadeSync57.52Hz.md` | `devdocs/SYNC.md` |
| `DSP.md`（根目录） | `devdocs/DSP.md` |
| `开发统计.md` | `devdocs/DEV-STATS.md` |
| `README.next.md` | `devdocs/README-DRAFT.md` |
| `reddit-post.md` | `devdocs/REDDIT-POST.md` |
| `plans/` | `devdocs/plans/` |
| `prompts/` | `devdocs/prompts/` |
| `picture_fix/` | `devdocs/archive/picture-fix/` |
| `prompts/high_refresh_prompt_VRR 2x (115.048Hz) .md` | `devdocs/prompts/high_refresh_prompt_vrr2x.md` |

## 四、不要当成当前文档的目录

- **`202610_archived/`**（约 3 GB）：不参与构建，可直接删掉或移走。
  - `UploadToGithub/`、`UploadToGithub_20261017/`、`za/`：发版/备份快照，里面的
    `TECHLOG.md`、`FIXES.md` 等是**旧副本**，以 `devdocs/` 为准。
  - `root-temps/`：旧日志、`.obj`、`build-diag/`、`TempBossTest/`、`png-boxarts/`
    （一次性脚本已改放到 `Tools/Diagnostics/`）。
  - `root-images/`：根目录上已不再引用的作者素材（`Supermodel-VK.jpg`、
    `Supermodel-VK-1024.jpg`、`wallpapaer.png`、`sega model 3 bezel.gif`）。
- `imgui-master/`：上游 imgui 完整下载（304 个文件），与项目实际使用的
  `Src/Pkgs/imgui/`（17 个文件，已裁剪修改）**不同**；留给升级 imgui 时对照用。
- `Libraries/`：SDL2 / SDL3 / Vulkan-Headers / zlib 源码，**构建需要，不能删**。
- `build-cmake/`、`dist/`：构建产物。

## 五、写文档的约定

- 中文详述写 `devdocs/CHANGELOG.md`；发给用户的短日志在同一节末尾（中英各一条）。
- 某一项优化需要完整验证矩阵时，写进 `sdl3_reports/`，并在其 `README.md` 加一行。
- 可复用的排错结论写 `devdocs/FIXES.md`；不要只写在 `CHANGELOG.md` 里。
- 长期有效的项目约定（构建、坑、当前状态）写 `AGENTS.md`。
- 已确认但暂缓的问题写 `issues_waitlist.md`；修复后把结论移进 `CHANGELOG.md` 并删条目。
