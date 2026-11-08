# HD 纹理系统 —— 交接与排查手册

> 这份文件是给"下一个窗口里的 AI"以及未来的自己看的。读完这一份就能接着干活，
> 不需要翻聊天记录。最后附一段可直接粘贴的提示词。
>
> 要把这套功能模块化或移植到其他模拟器，先读 `devdocs/RUNBOOK-HD-TEXTURES.md`
> （可移植边界、接口契约、七阶段流程、后端适配表、验收矩阵）。

## 0. 一句话现状

HD 纹理导出/替换功能已经可用（Daytona 2 的 282 张子纹理，4× 包正常工作）。
今天定位并修掉了"赛道出现等距横条"的根因 —— **HD 采样路径的 wrap 模式 1 用了
`fract()` + `CLAMP_TO_EDGE`**。修复已验证（条纹指标 10.33 → 1.53，HD 关闭参考 0.97）。
临时诊断代码已全部删除，工作区只剩这个修复。**尚未 bump 版本，CHANGELOG 已写。**

## 1. 目录与访问方式

| 目录 | 角色 | 能不能改 |
| --- | --- | --- |
| `E:\Dev\Sega Model 3 Arcade Emulator\Supermodel-VK` | 稳定发布目录 | **禁止改动** |
| `E:\Dev\Sega Model 3 Arcade Emulator\Supermodel-VK-HD` | 本分支工作目录，HD 功能在这里 | 随便改 |

- 两个目录**都不是 git 仓库**，不要提提交/分支。
- 若文件工具只能进 `Supermodel-VK`，用 junction：
  `Supermodel-VK\.hd-work` → `Supermodel-VK-HD`（**用完必须删**：
  `powershell -Command "[System.IO.Directory]::Delete('<绝对路径>', \$false)"`，
  bash 里 `$` 要写成 `\$`）。若 HD 目录本身是 workspace root，直接用
  `Supermodel-VK-HD/...` 即可。
- 终端是 `sh`（bash），不是 cmd：`cmd /c` 无效，要 `cmd.exe //c`，带反斜杠的整个参数
  要单引号包住，例如 `cmd.exe //c 'build-cmake.bat -DUSE_SDL3=ON'`。

## 2. 构建

```bash
# 增量（推荐，产物 build-cmake/bin/Model3-Vulkan.exe，约 1-2 分钟）
cmd.exe //c 'build-cmake.bat -DUSE_SDL3=ON'

# 部署到测试安装目录（抓图脚本默认用这里的 exe）
cp build-cmake/bin/Model3-Vulkan.exe dist/Supermodel-VK/Model3-Vulkan.exe
```

- 改了 `Src/Graphics/Vulkan3D/Shaders/*.glsl` **必须**先跑
  `cmd.exe //c 'Scripts\CompileVulkanShaders.cmd'` 再编译。
- 那个脚本会顺带重写 `ui_ambient_frag / ui_blur_down_frag / ui_blur_up_frag /
  ui_frost_frag` 四个 `.spv.h`（源码相同、字节不同）。要和主分支一致就把
  `../Supermodel-VK/Src/Graphics/Vulkan3D/Shaders/` 里同名文件拷回来，并删掉
  生成在 `Src/Graphics/Vulkan3D/Shaders/` 下的 `*.spv`。**文件名是
  `xxx_frag.spv.h`，不是 `xxx.spv.h`。**
- 完整打包用 `cmd.exe //c build.bat`（`dist/Supermodel-VK`）。
- `build-cmake/bin/Data/` 里的 Assets **没有** HDTextures，所以测试要用 `dist` 里的 exe。

## 3. 抓图 / 验证

一条命令加载存档、跑到第 14 秒抓一帧、退出：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Capture-Frame.ps1 \
  -Set daytona2 -Slot 1 \
  -Rom "E:\EMU\model-3-romset\daytona2.zip" \
  -At 14 \
  -Out "E:\Dev\Sega Model 3 Arcade Emulator\Supermodel-VK-HD\shot.png" \
  -Ini "MSAA=4;TexFilter=0;QuadInterpolation=1;AlphaSortMode=0;LoadHDTextures=true"
```

要点：

- `-Ini` 里的键只在本次运行生效，脚本结束会还原。
- 截图为 4K（3840×2160），实际渲染分辨率按 ini（当前 2480×1920 + MSAA 4x）。
- 抓不到图先看 `dist/Supermodel-VK/Data/Supermodel.log` 的
  `Screenshot: SM3D_SCREENSHOT_AT fired ...`。
- 存档：`dist/Supermodel-VK/Data/Saves/daytona2.zst1`（车静止、计时 1'00"35 左右）。
  读档有 3 秒（172 仿真帧）延迟，`-At 14` 是等它稳定。
- bash 里不要写 `"...\$1"` 这类转义传给 PowerShell，`$` 会被吃掉；路径直接写死。

条纹强度指标（判断有没有横条的标准手法）：

```python
from PIL import Image
import numpy as np
def band(p, box=(200,1400,650,1750)):          # 横条整片落在这个框里
    a = np.asarray(Image.open(p).convert('L'), np.float32)[box[1]:box[3], box[0]:box[2]]
    rows = a.mean(axis=1); k = 61
    r = rows - np.convolve(rows, np.ones(k)/k, 'same'); r = r[k//2:-k//2]
    return r.std()
# HD 关 ≈ 0.97；修复前 10.33；修复后 1.53
```

## 4. 功能结构（已实现部分）

新增文件：

| 文件 | 作用 |
| --- | --- |
| `Src/Graphics/Vulkan3D/HDTextures.{h,cpp}` | 哈希、目录递归扫描、PNG/WebP 解码线程、DDS/Zstd 加载、dump 线程、bindless 槽位、mip 上传、热重载 |
| `Src/Graphics/Vulkan3D/DdsLoader.h` | 零依赖 DDS 解析（BC1/BC3/BC5/BC7，含 DX10 头与逐层字节校验） |
| `Src/Graphics/Vulkan3D/TextureDecode.h` | 与 `real3d_common.glsl` 的 `ExtractColour()` 逐位一致的 RGBA8 解码 |
| `Src/Util/PNGFile.h` | 新增 `WriteRGBA()` |
| `Tools/resolutionsort.bat` / `.ps1` | 按 `<宽>X<高>` 分文件夹（不改文件名，默认 Skip，可选 Overwrite/Copy/Recurse/DryRun） |

改动点：`VulkanTextureCache.{h,cpp}`（逐 tile 版本号哈希缓存、bindless 在 **set 2**）、
`Vulkan3D.{h,cpp}`（`CreateOitSetLayout()` 无条件执行；`DrawBucket` 绑 set 2）、
`VulkanCommon.h`（`hdTexIndex`）、`real3d_common.glsl`（`textureGrad` 分支）、
`Inputs.{h,cpp}`（F4）、`Main.cpp`、`Gui.{h,cpp}`、`Overlay.cpp`、`Localization.cpp`、
`ConfigFile.h`、`ConfigBuilders.cpp`、`CMakeLists.txt`、`Makefiles/Rules.inc`、`build.bat`。

键名：`LoadHDTextures` / `DumpHDTextures`（**不能**复用旧的 `DumpTextures`）。
上限：单边 4096、总预算 3 GB、2048 槽、单文件 256 MB。

数据位置：

- 导出原图：`dist/Supermodel-VK/Data/Assets/DumpTextures/daytona2/`（282 张，
  已被 resolutionsort 分到 `32X32/`、`256X256/` 等子目录）
- 用户 4× 超分中间产物：`dist/Supermodel-VK/Data/Assets/DumpTextures/daytona2_4X/`
- 生效的 HD 包：`dist/Supermodel-VK/Data/Assets/HDTextures/daytona2/`（282 张，4×）
- 用户暂时不转 WebP，先解决贴图错误。

## 5. 今天修掉的 bug（wrap 边界缝）

**症状**：Daytona 2 关 HD 时路面干净；开 4× HD 后，画面左下那条沿行驶方向铺开的路面
出现整片等距横向细条（远处更密），而旁边路面/右墙/看台正常。用户描述是
"跑赛道用的中间这条路都是横条线，两边的路正常"。

**根因**：该材质 `wrapMode=1`，HD 分支用 `fract(uv)` + `CLAMP_TO_EDGE` 复刻真机的
"先 repeat、再在子贴图边缘钳位"。但对*过滤器*来说，钳位会把滤波足迹里越过边界的
tap 全读成边缘纹素 —— 缝宽 = 整个足迹宽，不是半个纹素。斜掠路面上足迹在屏幕上被拉成
几十像素，于是每个平铺周期一条横带。真机路径用 `WrapTexCoords()` 的 `& (2047>>level)`
掩码，是无缝的。

**修复**（两处，已在工作区）：

- `Src/Graphics/Vulkan3D/HDTextures.cpp` →
  `AddressModeForWrap()`：wrap 0/1 = `VK_SAMPLER_ADDRESS_MODE_REPEAT`，
  2/3 = `MIRRORED_REPEAT`（不再出现 CLAMP / MIRROR_CLAMP）。
- `Src/Graphics/Vulkan3D/Shaders/real3d_common.glsl` → HD 分支删掉 `fract()`，
  `uv` 原样传给 `textureGrad`；整段注释改成解释"为什么不能钳位"。

**证据**：`sdl3_reports/2026-10-25-hd-wrap-seam/`（before / after / HD-off 三张 4K 图 +
README 含变量隔离表）。

## 6. 已经排除的假设（**不要重做**）

| 假设 | 结论 / 证据 |
| --- | --- |
| 某张贴图的内容本身是横条 | 否。涂纯色后条纹完全消失（指标 0.44），说明缝两边同色就看不见 |
| 4× 超分引入了行结构 | 否。源图与 4× 版行/列高通 std 几乎一致（0.51/16.41 vs 0.69/16.59） |
| `TexFilter`（贴图过滤模式 0/1/2）干扰 | 否。HD 开时 0 与 2 的指标**完全相同**（10.33 / 10.33）；HD 关也是 0.97 / 0.98 |
| `AlphaSortMode=1`（OIT）干扰 | 否。0 与 1 对条纹无影响 |
| `QuadInterpolation`（精确四边形 vs 顶点插值） | 否。HD 分支根本不经过那条路径 |
| 天空贴图 `tex_0_0_128_256x128` | 无关。它在 node 15（雨棚/天空），改动它不影响路面 |
| mip 链生成（`vkCmdBlitImage` 逐级）或 `maxLod` | 代码正确，sampler `maxLod = VK_LOD_CLAMP_NONE`，blit 链无误 |
| 显存/上传/`loading` 标志 | 已正常工作（日志 `first replacement is sampleable (... 8 mip(s))`） |

## 7. 可复用的调试手段：把"这块像素用的是哪张贴图"读出来

找不到某块画面用哪张贴图时，用这招（临时改，用完删）：

1. `VulkanCommon.h` 加 `kMatDebugIndex = 1u << 12`；
2. `FillMaterial()` 末尾按环境变量置位；
3. `BuildDrawList()` 里，在 `ResolveHDTexture()` 之后把贴图矩形编码进 `hdTexIndex`：
   `material.hdTexIndex = (int32_t)(((texX >> 3) & 0xFF) << 8 | ((texY >> 2) & 0xFF));`
4. `real3d_common.glsl` 的 `ShadeFragment()` 里，取到 `mat` 之后立刻：
   ```glsl
   if ((mat.flags & kDebugIndex) != 0u) {
       uint p = uint(mat.hdTexIndex) & 0xFFFFu;
       return vec4(float((p >> 8) & 0xFFu)/255.0, float(p & 0xFFu)/255.0, 0.5, 1.0);
   }
   ```
5. 抓图后用 Python 读像素：`r*8` ≈ texX，`g*4` ≈ texY，再对照
   `DumpTextures/<set>/` 里的文件名（文件名带 `tex_<page>_<x>_<y>_<w>x<h>_fmt<n>_<hash>`）。

注意：**不要改用"材质索引"着色**，材质表索引每帧会变（draw list 随场景变化），
上一次截图里的 906 到下一帧就是别的东西了。矩形坐标是稳定的。

本次查到的对应关系（Daytona 2，同一存档帧）：

| 画面位置 | 贴图矩形 | 文件 |
| --- | --- | --- |
| 左下有横条的那条路 | `(0,768) 256x256 fmt0` | `tex_0_0_768_256x256_fmt0_d295d515a7b6bfb5.png` |
| 车所在的路面 | `(128,512)` / `(768,320)` / `(1408,448)` | 多块拼接 |
| 右侧白墙 | `(1792,256) 256x128 fmt7` | `tex_0_1792_256_256x128_fmt7_957bbcaef857b860.png` |
| 雨棚/天空 | `(0,128) 256x128 fmt0` | `tex_0_0_128_256x128_fmt0_e24a0d7978644b5b.png`（云） |

## 8. 待办 / 未验证

- [ ] 换别的赛道/别的 ROM set 看还有没有别的 wrap/贴图问题（目前只验证了 Daytona 2 这帧）。
- [ ] 用户手上可能还有其它"贴图错误"（他最初说的"有些贴图错误没解决"）。用第 7 节的办法定位。
- [x] 版本号已 bump（`Src/Version.h` 的 `SUPERMODEL_FORK_VERSION` 为 `0.9.8.3b`）；
      按约定发版才 bump，且**不要动** `SUPERMODEL_VERSION`。
- [ ] 本次修复尚未跑 `Tools/Validate-Vulkan.ps1 -Mode sync`（只是改 sampler 地址模式，
      理论无验证层影响；要送发布跑一遍更稳）。
- [ ] `dist/Supermodel-VK/Data/Config/Model3-Vulkan.ini` 里现在有 `LoadHDTextures = 1`。
- [ ] 如果嫌 4× 沥青颗粒太锐：那是超分风格，不是 bug；换温和模型或单独降锐该图。

## 9. 坑（工具与构建）

- PowerShell 脚本的 `-Ini` 值用 `K=V;K=V`；布尔写 `true`/`false`。
- `Capture-Frame.ps1 -Out` 的路径要是 Windows 绝对路径；bash 里别做变量转义。
- `Tools/resolutionsort` 只处理 PNG（用户需求）。
- 同名文件**不能加 ` (1)` 后缀**：HD 哈希在文件名里。
- 文件优先级：`.dds.zst` > `.dds` > `.webp` > `.png`（与旧版“PNG 优先于 WebP”不同，
  现在同哈希的 WebP 会盖过 PNG）。想测某一版就先把同哈希的其他扩展名删掉/改名。
- `.dds` 只接受 BC1/BC2/BC3/BC5/BC7（`DXT1`/`DXT3`/`DXT5`/`ATI2`/`BC5U` 或 DX10 头）。
  设备没有 `textureCompressionBC` 时扫描会跳过 DDS 并打 `HD textures: N DDS file(s) were
  skipped ...`，此时继续用 PNG/WebP。
- `.dds.zst` 需要与 exe 同目录的新版 `Model3VulkanHostCore.dll`（导出
  `HostCore_DecompressZstd`）。旧 DLL 会打 `N .dds.zst file(s) were skipped ...`。
- 大图上传是**可续传分块**的：每块 ≤ 8 MB，单层超过预算时按 4×4 block row 切；
  每帧每张最多 4 块，环满就顺延到下一帧。日志 `uploaded in N piece(s) over M frame(s)`
  就是这个机制，`M > 1` 是正常现象，不是卡住。
- **DDS 不要做 Y 翻转**：PNG/WebP 路径和 DDS 路径都把源图第 0 行放到 `v=0`，
  离线压制加 `-yflip` 会让替换图上下颠倒。
- `AlphaSortMode=1` 的 `uLayers` VUID **已于 2026-10-28 修掉**（白闪路径漏掉目标交接；
  `AlphaSortMode=0` 下也偶尔复现过）。现在 `Validate-Vulkan.ps1 -Mode sync` 在
  MSAA 1/2/4 × AlphaSortMode 0/1 下均为 0 VUID / 0 hazard。如果再看到
  `uLayers[4]/[5]` 的 `VUID-vkCmdDraw-imageLayout-00344`，先看 `CVulkan3D::DrawScene()`
  是否有新的提前 `return` 没调 `handOverTargets()`。
- 临时往 HD 包里写测试图**一定要先备份**到 `E:/tmp_*`（本次已经清理）。
- 每次 `Capture-Frame.ps1` 之后 `Data/Screenshots/` 会留 4K PNG，记得清。

## 10. 新窗口的提示词（直接粘贴）

```
我在 E:\Dev\Sega Model 3 Arcade Emulator\Supermodel-VK-HD 这个目录里做 Supermodel-VK
的个人分支（Sega Model 3 街机模拟器），重点是 Vulkan 后端 + 新实现的 HD 纹理导出/替换。
先读同目录下的 hdtexture_skills.md，那是上一轮工作的完整交接（目录、构建命令、抓图方法、
已修掉的 wrap 边界缝 bug、已排除的假设、以及"如何查出某块画面用哪张贴图"的调试手法）。
注意：Supermodel-VK 目录是稳定发布目录，禁止改动；Supermodel-VK-HD 才是工作目录。
终端是 bash，构建用 `cmd.exe //c 'build-cmake.bat -DUSE_SDL3=ON'`，改着色器要先跑
`cmd.exe //c 'Scripts\CompileVulkanShaders.cmd'`；抓图用 Tools/Capture-Frame.ps1，
存档参数见交接文档。

接下来的任务：<在这里写你要做的事，例如"继续验证其他赛道/其他 ROM set 有没有贴图问题"、
"检查 X 游戏的 Y 现象"、"把 4× 包换成分级锐化"…>
```
