# RUNBOOK — HD 纹理系统：模块化与移植

> 面向：要把本项目的 HD 纹理导入/加载（PNG / WebP / DDS BC1–BC7 / `.dds.zst`）
> 移植到另一个模拟器的人或 AI。
> 读完这一份应能回答三件事：**哪些代码可以直接拿、哪些必须重写、按什么顺序做最省时间**。
> 实现细节以 `Src/Graphics/Vulkan3D/HDTextures.{h,cpp}`、`DdsLoader.h`、
> `TextureDecode.h` 为准；Model 3 这一版的交接与踩坑见根目录 `hdtexture_skills.md`。

---

## 0. 先给结论

**可以模块化，但不要“把 1417 行的 `HDTextures.cpp` 复制过去”。**

这份代码实际上混了四层，只有两层是通用的：

| 层 | 内容 | 可移植性 |
| --- | --- | --- |
| L1 数据层 | DDS 解析、PNG/WebP 解码、文件名解析、目录扫描、优先级 | **高**，几乎原样 |
| L2 调度层 | 加载线程、结果队列、槽位状态、预算、热重载 generation | **高**，去掉日志/路径依赖即可 |
| L3 渲染层 | Vulkan 图像创建、分块上传、mip、sampler、bindless descriptor | **中**，换 API 要重写，策略可复用 |
| L4 模拟器层 | 内容哈希、纹理页/子矩形语义、抓取快照、材质挂载、shader 分支 | **低**，必须按目标模拟器重写 |

最高效的顺序不是“先写加载”，而是：

```
决策 → 黄金样本 → 抽 core + CLI → 目标机 dump 适配 → 目标机 load + shader → 上传/UI → 验收矩阵
```

**先做 dump（导出），再做 load（加载）。** dump 定义了哈希和文件命名，是整条链路的
“协议”；它同时也是 load 的测试 oracle。只要目标模拟器对同一张原图导出的文件名与
参考包一致，load 阶段就只剩下上传和采样两件事，调试范围会小一个数量级。

### 0.1 什么样的功能值得这样抽

不是所有功能都值得模块化。满足下面 4 条中的 3 条才划算：

- 有稳定的**数据契约**（文件格式、哈希、协议、存档布局）；
- 边界清楚，不依赖全局状态、GUI、某个模拟器的单例；
- 会在第二个项目/第二个渲染后端复用；
- 有**可自动比对的 oracle**（黄金文件、逐像素参考、固定帧截图）。

HD 纹理四项全中，所以适合。像“游戏内 Tab 面板布局”这种和具体 UI 框架、主题 token
强耦合的功能，抽成模块的收益很低，抽成 runbook + skill 就够了。

---

## 1. 先做适用性判断（半天，不要跳过）

在动代码之前，把下面 4 个问题写成半页纸的决策记录。它们决定 70% 的工作量。

### 1.1 是否要兼容现有 HD 包？

| 选择 | 后果 | 工作量 |
| --- | --- | --- |
| 要共用已有包 | 必须逐位复刻 `HashTextureRegion()`，包括非标准 FNV offset、输入顺序、页内 wrap、格式/尺寸参与哈希 | 大；任何一位不同，所有文件名都对不上 |
| 只服务新模拟器/新包 | 可以用更简单的身份，例如 XXH3/BLAKE3 over 解码后的 RGBA8，或模拟器自己的纹理 ID | 小一半以上 |

**这是最重要的一个决定。** 如果目标模拟器要读取本项目的包，哈希不是“实现细节”，
而是 pack 格式的 API，必须冻结并写测试向量。

### 1.2 目标的纹理模型是什么？

```
目标模拟器有现成的纹理替换系统？
├─ 有（Dolphin / PPSSPP / DuckStation 之类）
│    → 不要移植运行时；写 pack converter + 用目标 dump 建映射，成本最低
└─ 无
     ├─ 纹理是页面/atlas（像 Model 3 的 2048x1024 texture page）
     │    → 需要“子矩形哈希 + wrap 键”，照搬本项目的契约
     └─ 纹理是独立对象（每张贴图天然有稳定 ID）
          → 用 per-texture 哈希，跳过 wrap 键，槽位表可以更简单
```

### 1.3 目标渲染 API 支持 BCn 吗？

| 后端 | BC1/2/3/5/7 | 结论 |
| --- | --- | --- |
| Vulkan | `textureCompressionBC` 时支持 | 可直接复用 DDS 路径 |
| D3D11 / D3D12 | FL11+ 原生支持 | 可直接复用 DDS 路径 |
| OpenGL | S3TC + RGTC + BPTC 扩展 | 要查扩展，上传用 `glCompressedTexImage2D` |
| Metal（Apple Silicon） | **不支持 BCn** | 只做 RGBA8，或离线转 ASTC/ETC2 |

不支持 BCn 时，**不要**在运行时软解 DDS 再上传；直接让 pack 作者提供 PNG/WebP，
或离线转成目标平台的原生压缩格式。运行时软解 BC7 又慢又占内存。

### 1.4 范围：MVP 还是完整版？

| 范围 | 包含 | 预计 |
| --- | --- | --- |
| MVP | PNG + WebP、per-texture 哈希、逐材质绑定、启动时扫描、无热重载 | 2–3 天 |
| 标准 | 加 DDS BCn、子矩形哈希、异步加载、F4 开关 | 4–6 天 |
| 完整 | 加 `.dds.zst`、bindless、分块上传、热重载、dump、预算/回退 | 7–10 天 |

先交付 MVP，再按需要加。不要一上来做完整版。

---

## 2. 现有实现盘点：切在哪

### 2.1 文件与职责

| 文件 | 行数 | 职责 | 处理方式 |
| --- | --- | --- | --- |
| `Src/Graphics/Vulkan3D/DdsLoader.h` | 414 | 零依赖 DDS 解析，BC1/2/3/5/7，DX10 头，逐层字节校验 | **抽走**；把 `VkFormat` 换成自己的 `BlockFormat` |
| `Src/Graphics/Vulkan3D/HDTextures.cpp` | 1417 | 扫描/索引、hash、解码线程、dump 线程、Vulkan 上传、descriptor | **拆成 4 份**，见下 |
| `Src/Graphics/Vulkan3D/HDTextures.h` | 304 | 类定义、槽位、预算常量 | 拆成公共头 + Vulkan 后端头 |
| `Src/Graphics/Vulkan3D/TextureDecode.h` | 106 | Model 3 的 12 种 16-bit 纹理格式 → RGBA8 | **不要抽进 core**；放 Model 3 适配层，作为“CPU 解码必须与 shader 一致”的范例 |
| `Src/Util/PNGFile.h` | 546 | zlib PNG 读写 | 可选依赖；非 Windows 目标也可换 stb_image |
| `Src/OSD/SDL3/HostCoreZstd.h` | 59 | 可选 zstd 解压（`.dds.zst`） | 可选依赖；没有就跳过 zstd |

### 2.2 `HDTextures.cpp` 的四个切口

| 切口 | 函数/区段 | 归属 |
| --- | --- | --- |
| 文件识别与索引 | `ClassifyReplacement`、`ParseHashFromFilename`、`Scan`、`IndexEntry` | L1 通用 |
| 解码 | `DecodeImageFile`、`DecodeDdsFile`、`LoaderMain`、`LoadResult` | L1/L2 通用 |
| dump | `QueueDump`、`DumperMain`、`TextureDecode` | L2 通用 + L4 抓取 |
| GPU 上传 | `CreateSampler`、`UploadMip0`、`UploadCompressed`、`FinishImage`、`RecordUploads`、`WriteDescriptor` | L3 Vulkan 专用 |

**不要切的**：`HashTextureRegion()`。它是 Model 3 的纹理页语义（2048×1024、16-bit、
页内 wrap mask），不是通用算法。放到目标模拟器的 adapter 里，按 4.2 的契约实现或改写。

### 2.3 现有实现的资源上限（移植时照抄或按目标调整）

| 项 | 值 | 位置 |
| --- | --- | --- |
| bindless 槽位 | 2048 | `HDTextures.h` `kMaxHDTextures` |
| 显存预算 | 3 GB | `kMaxHDTextureBytes` |
| 单边最大 | 4096（与设备 `maxImageDimension2D` 取小） | `Init()` |
| 单文件最大 | 256 MB | `ReadFileBytes()` |
| dump 上限 | 50000 张/会话 | `kMaxDumpsPerSession` |
| 压缩链分块 | 8 MB/块，每帧每张最多 4 块 | `kCompressedChunkBytes` / `kCompressedChunksPerFrame` |

当前实现**不做逐出**：槽位一旦占用就留到会话结束/热重载。目标模拟器如果显存更紧，
需要加 LRU 或按 mip/尺寸分级预算；这是移植时常见的第二个改造点。

---

## 3. 目标模块布局：`HDPackKit`

不要把新模块放进模拟器的 `Src/Graphics/<backend>` 里，否则下一次换后端又要拆一次。
建议独立目录，公共头不包含 Vulkan、不包含模拟器头：

```
HDPackKit/
  include/hdpk/Pack.h          // 公共 API；只依赖 <cstdint> <string> <vector>
  include/hdpk/Dds.h           // DDS 解析；自己的 BlockFormat，不出现 VkFormat
  src/PackIndex.cpp            // 递归扫描、扩展名优先级、文件名取 hash
  src/Loader.cpp               // 工作线程、任务队列、generation、预算
  src/DecodePng.cpp            // zlib 或 stb
  src/DecodeWebp.cpp           // libwebp（Windows/Linux；macOS 用 ImageIO 或 libwebp）
  src/DecodeZstd.cpp           // 可选；没有就直接跳过 .dds.zst
  src/Dump.cpp                 // RGBA8 → PNG，后台线程
  backends/vulkan/Uploader.cpp // 参考后端：分块上传、mip、sampler、bindless
  backends/d3d11/Uploader.cpp  // 需要时再加
  tools/hdpack_cli.cpp         // scan / verify / hash / decode
```

### 3.1 公共 API（建议形状，不是现有代码）

```cpp
// hdpk/Pack.h —— 不包含 Vulkan / 模拟器头文件
namespace hdpk {

struct Key        { uint64_t hash; uint32_t wrapU, wrapV; };
struct Caps       { bool blockCompressed; bool zstd; uint32_t maxDimension; };

enum class BlockFormat : uint8_t { Rgba8, Bc1, Bc2, Bc3, Bc5, Bc7 };

struct ImageDesc {
    uint32_t width, height, mipLevels, blockBytes;
    BlockFormat format;
    bool compressed;
};

class Host {                        // 模拟器实现，约 100–200 行
public:
    virtual std::string GameId() const = 0;
    virtual uint64_t HashRegion(int x, int y, int w, int h, int format) const = 0;
    virtual bool SnapshotRegion(int x, int y, int w, int h,
                                std::vector<uint16_t>& out) const = 0;
    virtual void Log(int level, const std::string& message) = 0;
};

class Backend {                     // 渲染器实现，Vulkan 版可参考现有 Upload*()
public:
    virtual bool Supports(BlockFormat format) const = 0;
    virtual bool Create(uint32_t slot, const ImageDesc& desc) = 0;
    virtual bool Upload(uint32_t slot, const UploadChunk& chunk,
                        const void* data, size_t size) = 0;
    virtual void Finish(uint32_t slot) = 0;
    virtual void Destroy(uint32_t slot) = 0;
};

class Pack {
public:
    bool Open(const std::string& directory, const Caps& caps);
    int  Resolve(const Key& key);   // >=0 可用槽位；-1 未命中/未就绪
    void Tick();                    // 热重载、预算、状态翻转
    void Poll(Backend& backend);    // 收解码结果，按策略分块上传
    void Dump(const Key& key);      // Host::SnapshotRegion -> 后台 PNG
};

} // hdpk
```

三个设计约束：

1. **`Dds.h` 不出现 `VkFormat`。** 当前 `DdsLoader.h` 直接返回 Vulkan 枚举，这是
   换 D3D/GL 时的第一处编译错误。改成 `BlockFormat` + `blockBytes`，各后端自己映射。
2. **`Pack` 不碰模拟器全局。** 目录、GameId、日志、纹理 RAM 全由 `Host` 注入。
   当前 `SetGameName()` 直接拼 `FileSystemPath::GetPath(Assets)`，移植时必须改成参数。
3. **上传策略留在 `Pack`，API 调用留在 `Backend`。** “8 MB 一块、每帧 4 块、环满顺延”
   是策略，和 Vulkan 无关；D3D12 的 upload heap 同样适用。

---

## 4. 数据契约（跨模拟器唯一不能随便改的部分）

### 4.1 目录与文件名

```
<root>/HDTextures/<GameId>/**/*.{png,webp,dds,dds.zst}
```

- 递归扫描；子目录随便分（例如 `<宽>X<高>/`）。
- 文件名里必须有**恰好一个 16 位十六进制 token** 作为哈希；解析器按 `_`、`-`、空格
  切分，取**最后一个** 16 位 hex token。导出格式：

```
tex_<page>_<x>_<y>_<w>x<h>_fmt<format>_<16-hex-hash>.png
例：tex_0_0_768_256x256_fmt0_d295d515a7b6bfb5.png
```

- 不要在文件名里再加第二个 16 位 hex token；不要用 ` (1)` 这类后缀复制文件。
- 扩展名优先级（同一 hash 多份时）：

```
.dds.zst  >  .dds  >  .webp  >  .png
```

- `.dds.zst` 要剥掉 **两个**扩展名再取 hash；`std::filesystem::stem()` 只会剥 `.zst`，
  结果把 `.dds` 当成 hash 的一部分，所有文件都匹配不上。

### 4.2 哈希（仅在需要兼容现有包时）

Model 3 的契约（`HashTextureRegion()`）：

- FNV-1a 64 位，但 **offset basis 不是标准值**：
  `1469598103934665603`（`0x14650FB0739D0383`），prime `1099511628211`（`0x100000001B3`）。
- 输入顺序：`format`、`width`、`height` 各按小端 4 字节；然后逐行逐列 16-bit texel，
  **低字节在前**。
- 读取时用页内 wrap 掩码：`x & (pageWidth-1)`、`y & (pageHeight-1)`。
- 格式、宽、高必须参与哈希：同样的 16-bit 字按 RGBA4 和 T1RGB5 解释是两张图。

**黄金测试向量**（`pageWidth=16`、`pageHeight=8`；已用实际 C++ 实现核对）：

| 输入 | 期望 |
| --- | --- |
| 全 0 页，矩形 `(3,2,8,4)`，format 0 | `ac90f164e66377df` |
| 页 `page[i]=i`，矩形 `(3,2,5,3)`，format 7 | `4e7a517c05d4ede1` |
| 页 `page[i]=i`，整页 `(0,0,16,8)`，format 0 | `5e608dcb5bde440b` |

移植时先让这三个数对上，再谈渲染。

### 4.3 解码契约

- PNG/WebP 一律输出 **RGBA8**，行优先，无预乘。
- DDS 只接受 BC1/2/3/5/7；不接受的格式要**跳过并打日志**，不能静默当 RGBA8。
- **DDS 不做 Y 翻转。** PNG/WebP 和 DDS 都把源图第 0 行放到 `v=0`；离线压制加
  `-yflip` 会让替换图上下颠倒。
- BC1 用 `BC1_RGBA` 变体，不要用 `BC1_RGB`：DXT1 可能带 1-bit 透明键。
- 压缩图**不要**当 blit 源/目标：`vkCmdBlitImage` 不接受 BCn。mip 链必须来自文件。
- sRGB 变体映射到 UNORM：原机贴图不是 sRGB，采样时不能再做一次 gamma 解码。

### 4.4 槽位与上传契约

- 槽位状态：`loading`（已占位，文件未解码）→ `ready` / `failed`。
- `Resolve()` 在未就绪时返回 `-1`，材质继续走原版路径；**绝不能返回白色/未初始化图**。
- 解码结果到达后必须把 `loading` 置 `false`，否则上传循环会永远跳过它。
  （本项目的真实 bug：哈希匹配、日志有 `first match`，画面全是原贴图。）
- 上传分块：
  - RGBA8：mip 0 走 staging ring，mip 1..N 用 blit。
  - BCn：整条 mip 链从文件来，按 ≤8 MB/块、每帧每张 ≤4 块上传；
    `bufferOffset` 必须是块大小的倍数，单个 mip 超过预算时按 4×4 block row 切。
  - 环满就结束本帧、下一帧从游标继续；不要为一张纹理申请整个环。
- 预算：按解码后 RGBA 或压缩 payload 估算，超过上限就跳过并打日志。

### 4.5 已有包的迁移（converter 路径）

如果目标模拟器已经有替换包，通常**不要**手动改名，也不要为了读旧包去移植整套运行时：

1. 用目标模拟器的 dump 导出原图（和第 5 节 Stage 3 同一条路径）；
2. 用 `hdpack` 对每张原图算 hash，建立 `目标纹理 ID -> hash` 映射；
3. 按源包的命名/索引找到每张替换图对应的原图，复制成 `<hash>.png`
   （保留描述性前缀也可以，只要最后一个 16 位 hex token 是 hash）；
4. 用 `hdpack verify` 确认文件名 hash 与内容一致。

难点是“源包的图对应哪张原图”。如果两边模拟同一台机器、dump 逐像素一致，可以按原图
内容哈希自动匹配；如果解码/裁剪不同，只能按名字或人工映射。**先做 10 张的样本映射，
确认规则后再批量跑**，否则一次错位会污染整个包。

---

## 5. 阶段流程（Stage 0–7，每阶段有退出条件）

### Stage 0 — 决策记录（0.5 天）

回答第 1 节的 4 个问题，写半页 `PORTING-DECISIONS.md`：

- 是否兼容现有包（哈希冻结与否）；
- 目标是页面模型还是独立纹理；
- 后端与 BCn 支持；
- MVP / 标准 / 完整。

**退出条件**：这四项都有明确答案，且知道自己要改哪个 shader。

### Stage 1 — 黄金样本（0.5–1 天）

在 Model 3 这一版上做一个**可重复**的小样本：

1. 用固定存档开 `DumpHDTextures`，**正常跑一段**（10–20 秒）让 dump 线程把队列写完；
   不要只抓一帧就退出，队列里的 PNG 可能还没落盘。导出一个场景即可
   （建议 20–50 张，不要整游戏）。
2. 生成 `manifest.tsv`：

```
hash<TAB>page<TAB>x<TAB>y<TAB>w<TAB>h<TAB>format<TAB>file<TAB>sha256
```

3. 保存 HD 关 / HD 开两张同帧截图（`-Frame` + `RefreshRateMode=0`，见项目 A/B 规矩）。
4. 把这张场景的原始纹理页也留一份（如果目标模拟器要复算 hash）。

**退出条件**：manifest、参考图、原始页、截图四样齐全；样本能在 5 分钟内重跑。

### Stage 2 — 抽 core + CLI（1–2 天）

1. 建 `HDPackKit/`，把 L1/L2 代码搬过去。
2. 逐项去依赖：
   - `FileSystemPath::GetPath(Assets)` → `Pack::Open(directory)` 参数；
   - `InfoLog/ErrorLog` → `Host::Log`；
   - `Vulkan3D::` → `hdpk::`；
   - `DdsLoader` 的 `VkFormat` → `BlockFormat`；
   - `SUPERMODEL_PURE_VULKAN` 的 zstd → `Caps.zstd` + 可选解码器。
3. 写 `hdpack` CLI：

```bash
hdpack scan   <dir>                          # 统计各类型、重复 hash、无 hash 文件
hdpack verify <dir> --manifest manifest.tsv  # 文件名 hash 是否等于内容 hash
hdpack hash   --page page0.bin --rect 3,2,8,4 --format 0 --page-size 2048x1024
hdpack decode <file> --out out.rgba          # PNG/WebP/DDS -> RGBA8 或 BCn payload
```

**退出条件**：

- `scan` 的计数与模拟器日志 `HD textures: N replacement(s) indexed ...` 一致；
- 4.2 的三个黄金向量全部匹配；
- `verify` 对参考包 0 失败；
- core 能在只有 C++17 + zlib + libwebp 的环境里编译，**不链接 Vulkan**。

### Stage 3 — 目标模拟器的 dump 适配（1 天）

这一步只验证**哈希与抓取**，不要碰渲染。

1. 实现 `Host::HashRegion()`；如果目标也是页面模型，按 4.2 逐位实现。
2. 在目标的材质/绘制路径上，对每个唯一子纹理调用 `Dump()`（内部先 `SnapshotRegion`，
   立刻复制 texel，不能把页面指针交给后台线程）。
3. **导出写的是“渲染器采样到的像素”，不是“存储里的每一位”。** 老机器很多是 1-bit
   透明位（Model 3 的 T1RGB5 bit 15、4-bit 格式的 alpha nibble），美术拿它做抖动表示
   半透明；而不用 alpha 的材质在运行时会把 alpha 强成 1。按材质判断：不读 alpha 就写
   255，读就保留原掩码。反过来做（原样输出所有位）会让导出的图看上去布满白点/虚线，
   这些假象会被当成 dump 的 bug 追一整天（本项目案例见
   `sdl3_reports/2026-11-01-dump-alpha-dither/`）。同一哈希可能先后被两种材质用到，
   要能重导一遍，不能靠遇到顺序定稿。
4. 启动后只打日志：`hash + rect + format`，先不写文件也行。
5. 用 `hdpack verify` / manifest 比对。

**退出条件**：

- 目标模拟器对同一游戏的同一张原图算出的 hash 与参考 manifest 一致（至少 10 张）；
- dump 出的 PNG 与参考 dump 逐像素一致（同一解码路径应完全一致）；
- 如果不一致，**停在这里查原因**（纹理 RAM 布局、格式号、矩形、页大小、解码），
  不要继续做 load。哈希不对，后面的工作全部白费。

### Stage 4 — 目标模拟器的 load + shader（1–2 天）

1. 在**材质构建时**解析一次，不要每 draw 调用：

```cpp
material.hdTexIndex = pack.Resolve({ hash, wrapX, wrapY });   // 未命中 = -1
```

2. shader 增加绑定数组和分支（见第 6 节）。
3. 未就绪/未命中必须继续采样原版；不要用白色占位。
4. 用“把矩形坐标编码进颜色”的调试手法确认某块画面用的是哪张贴图
   （`hdtexture_skills.md` 第 7 节；比材质索引稳定，材质索引每帧会变）。

**退出条件**：

- 日志有 `first match` 和 `first replacement is sampleable`；
- 固定帧 A/B 只有预期贴图变化；
- 没有白块/黑块/整块缺失；
- alpha、wrap、反色、微纹理行为与原版一致（第 6 节）。

### Stage 5 — 上传与后端（1–2 天）

1. 先把 RGBA8 路径跑通：staging → image → mip blit → sampler → descriptor。
2. 再加 BCn：`TRANSFER_DST | SAMPLED`，不要 `TRANSFER_SRC`，不要 `STORAGE`；
   整条链从文件来，分块上传。
3. 逐格式检查设备能力：`SAMPLED_IMAGE | TRANSFER_DST | SAMPLED_IMAGE_FILTER_LINEAR`
   （采样器是线性的；缺这个位就是未定义行为）。
4. 加预算日志：heap budget / 使用量 / 本会话持有量。

**退出条件**：

- 4K BC7 包能加载，帧时间无可感知尖峰；
- 上传跨帧时日志是 `uploaded in N piece(s) over M frames`，`M>1` 正常；
- 同步验证 0 VUID / 0 hazard；
- 显存不超过预算，超限有明确日志。

### Stage 6 — UI / 热重载（0.5–1 天）

1. 配置键：`LoadHDTextures` / `DumpHDTextures`。**不要复用**已有的 `DumpTextures`
   （那是“退出时导出原始 16-bit 页”的另一功能）。
2. 开关要逐仿真帧读，切换立即生效。
3. 热重载：全局 serial + `Tick()` 比较；重载时等待设备空闲、清空槽位、递增 generation，
   让在途的解码结果被丢弃。
4. 日志在状态翻转时打一行，避免每帧刷屏。

**退出条件**：

- F4 等价开关能在同一局内 A/B；
- F3 等价重载能捡起磁盘上新加的图；
- 重载不会崩溃、不会把旧 descriptor 留在在途帧里。

### Stage 7 — 验收矩阵（0.5–1 天）

至少跑完第 10 节的矩阵。**退出条件**：全部通过，或每个例外都有记录和理由。

### 如果只有 3 天（最小路径）

1. 第 1 天：Stage 0 + 1 + 2，只做 PNG/WebP 和 `hdpack scan/verify`；
2. 第 2 天：Stage 3 + 4，per-texture 哈希、逐材质绑定、shader 分支；
3. 第 3 天：跑第 10 节的格式/缺图/回归子集，固定帧 A/B。

这条路径**放弃** DDS、`.dds.zst`、bindless、热重载。目标是先证明
“哈希对 → 能找到 → 能采样”这条主链，再按需要加功能。

---

## 6. 采样语义清单（移植最容易错的地方）

### 6.1 绑定

- Vulkan 参考实现把数组放在 **set 2**，用 `UPDATE_AFTER_BIND` + 部分绑定。
  **不能放 set 0**：set 0 有 `UNIFORM_BUFFER_DYNAMIC` / `STORAGE_BUFFER_DYNAMIC`，
  同布局带 `UPDATE_AFTER_BIND` 违反 VUID 03001。
- 数组元素未绑定的槽位不会被读取：`hdTexIndex < 0` 走原版分支。

### 6.2 UV 与 mip

- 替换图就是**子纹理矩形本身**；`uv` 与原版路径使用同一套归一化，不要二次变换。
- 用显式梯度的采样：`textureGrad`（Vulkan/GLSL）、`SampleGrad`（HLSL）、
  `gradient2d`（MSL），不要用普通 `texture()`。导数来自精确的逐像素 uv，
  斜掠面才能选对 mip。
- 导数必须在分支**之前**求值（uniform control flow）；否则编译器/驱动行为不确定。
- 跳过微纹理（microtexture）分支：它的重复低分辨率网格会抵消替换图的细节。

### 6.3 Wrap

- 替换图只保存子纹理，原版的“页内重复 + 边缘钳位”没有意义。
- **所有 wrap 都用 REPEAT / MIRRORED_REPEAT，绝不出现 CLAMP。**
- 不要在 shader 里 `fract(uv)` 再配 CLAMP：缝宽会等于整个滤波足迹，斜掠路面会
  出现每个平铺周期一条横带（Daytona 2 的真实 bug）。
- 映射：wrap 0/1 → REPEAT；wrap 2/3 → MIRRORED_REPEAT。

### 6.4 Alpha / 颜色

- 保留原版的 alpha 规则：alpha test 阈值、`kTextureAlpha` 的两遍拆分、
  无 alpha 材质最后强制不透明。
- 反色标志（`kInverted`）要在替换图上同样应用。
- 场景颜色是否预乘由原管线决定；替换图本身按直通 RGBA 上传，不要擅自预乘。

### 6.5 缺图回退

- 未命中、加载中、解码失败、超预算、设备不支持 BCn：全部回退原版。
- 回退是**默认行为**，不是错误路径；日志限流，不要每帧刷。

---

## 7. 后端适配表

| 项目 | Vulkan | D3D11 | D3D12 | OpenGL | Metal |
| --- | --- | --- | --- | --- | --- |
| BCn | `textureCompressionBC` | FL11+ | FL11+ | S3TC/RGTC/BPTC 扩展 | Apple Silicon **无** |
| 压缩上传 | `vkCmdCopyBufferToImage` | `UpdateSubresource` / `Map` | `CopyTextureRegion` | `glCompressedTexImage2D` | 转 ASTC 或 RGBA8 |
| mip | 文件链或 `vkCmdBlitImage`（仅 RGBA） | RGBA 可 `GenerateMips`；BC 用文件链 | 同 D3D11 | RGBA `glGenerateMipmap`；BC 用文件链 | 文件链或离线 |
| 采样梯度 | `textureGrad` | `SampleGrad` | `SampleGrad` | `textureGrad`（GLSL 4.0+） | `gradient2d` |
| 数组/绑定 | descriptor array | `Texture2D tex[N]` | descriptor table | `sampler2DArray` 或 bindless | `texture2d_array` |
| 对齐 | `bufferOffset` 块对齐 | 行/深度 pitch | 256 B 行 pitch、64 KB placed | 无特殊 | 无特殊 |
| 颜色空间 | UNORM，不用 sRGB 变体 | UNORM | UNORM | `GL_RGBA8` | 同 |

**D3D11/12 的 `SampleGrad` 与 Vulkan 的 `textureGrad` 语义一致**，但 HLSL 的
`ddx/ddy` 在分支内可能被优化掉；同样要把梯度提到分支外。
**OpenGL 的 BC7 需要 `GL_ARB_texture_compression_bptc`**；没有就只走 RGBA8。
**Metal 的 Apple Silicon 不支持 BCn**，不要指望“先上传 BC7 再说”。

---

## 8. 性能与资源

| 原则 | 做法 |
| --- | --- |
| 解码不占渲染线程 | 工作线程 + 结果队列；主线程只换指针 |
| 上传不阻塞 | staging ring 分块；环满顺延到下一帧 |
| 不在 draw 里查 map | 材质构建时解析一次，存 slot 整数 |
| 不每帧重新哈希 | 按矩形 memoize，纹理页 tile 版本号变化时失效 |
| 不一次性申请整条环 | 4K BC7 链约 21 MB；单块 ≤8 MB，每帧每张 ≤4 块 |
| 不把热路径计数器放 HOST_VISIBLE | 每次访问走 PCIe，4K 下可到 20 ms/帧 |
| 不做逐出（当前实现） | 预算 3 GB + 2048 槽；显存小的目标要加 LRU |
| 重载才 idle | 只有重载/关闭时 `vkDeviceWaitIdle`，正常帧不等待 |

衡量指标：

- 首帧到 `first replacement is sampleable` 的时间；
- 加载期间帧时间 P99（不应有 >2 ms 的尖峰）；
- 稳定后每帧 GPU 时间变化（HD 开/关）；
- 显存持有量 vs 预算。

---

## 9. 反模式（不要做）

1. **不要整体复制 `HDTextures.cpp`。** 它带 `FileSystemPath`、日志、Vulkan 上下文，
   换项目会拖着整棵依赖树。
2. **不要在不需要兼容旧包时复刻 Model 3 哈希。** 直接用目标模拟器的纹理身份更简单。
3. **不要把 `VkFormat` 留在 DDS 解析器里。** 换后端第一处就编译不过。
4. **不要用 `std::filesystem::stem()` 处理 `.dds.zst`。** 会留下 `.dds`。
5. **不要给 DDS 加 Y 翻转。**
6. **不要用 CLAMP + `fract()` 复刻原版 wrap。** 这是本项目最贵的 bug 之一。
7. **不要在压缩图上做 blit、storage 或 `GenerateMips`。**
8. **不要把整条 4K BC7 链塞进一次 ring 分配。** 单张可能超过整个切片。
9. **不要在解码后忘记清 `loading`。** 上传循环会永远跳过该槽。
10. **不要在 draw 循环里做哈希或文件系统探测。**
11. **不要在主线程解码 WebP/PNG。**
12. **不要用白色/棋盘格占位未加载的替换图。** 回退原版。
13. **不要把 `.dds.zst` 当成必需能力。** 没有 zstd 就跳过，PNG/WebP 继续工作。
14. **不要忽略许可证。** 本项目是 GPLv3；目标模拟器若不带 GPL 兼容许可，
    不要复制源码，按本 runbook 的契约重写。

---

## 10. 验收矩阵

| 类别 | 用例 | 期望 |
| --- | --- | --- |
| 格式 | PNG / WebP / DDS BC1 / BC3 / BC5 / BC7 / `.dds.zst` | 各自能加载；同 hash 按优先级取胜 |
| 缩放 | 1× / 2× / 4× / 8× / 非 2 次幂 | 正确显示；mip 数正确；不越界 |
| Wrap | 0 / 1 / 2 / 3 | REPEAT / REPEAT / MIRRORED / MIRRORED；无接缝 |
| Alpha | 1-bit key / A4L4 / 无 alpha / `kTextureAlpha` | 与原版一致；无黑边/白边 |
| 颜色 | 反色材质 | 替换图同样反色 |
| 缺图 | 包缺该 hash | 原版；日志限流 |
| 坏文件 | 截断 DDS / 非法头 / 0 字节 | 跳过并报错；其他图正常 |
| 设备 | 无 BCn / 无 zstd | 跳过对应格式；PNG/WebP 继续 |
| 预算 | 超过 3 GB / 2048 槽 | 明确日志；不崩 |
| 热重载 | 运行中加图 / 删图 / 改图 | F3 后生效；在途帧安全 |
| 性能 | 4K + MSAA 4x + 大包 | 无 >2 ms 尖峰；上传跨帧正常 |
| 同步 | `Validate-Vulkan.ps1 -Mode sync` | 0 VUID / 0 hazard |
| 回归 | 固定帧 A/B，HD 开/关 | 只有预期贴图变化 |

A/B 的硬性规矩（沿用项目约定）：

- 先问用户要存档，不要从启动画面猜时间；
- 用 `-Frame <n>`（仿真帧）而不是 `-At`；
- 加 `-Ini "RefreshRateMode=0"` 把插值 alpha 固定成 1.0；
- 同模式重复应逐像素相同（mean 0 / max 0）。

---

## 11. 技能化：runbook、模块、skill 的分工

三者不是一回事：

| 形态 | 放哪 | 作用 | 什么时候做 |
| --- | --- | --- | --- |
| 模块 | 代码目录 | 真正复用代码 | 第二个项目要用同一套逻辑 |
| runbook | `devdocs/RUNBOOK-*.md` | 人/AI 读的流程与判断 | 现在 |
| skill | `.agents/skills/<name>/SKILL.md` | 让 agent 在正确时机自动加载 runbook | 流程会重复 3 次以上 |

**skill 不应该复述 runbook。** 它只做三件事：触发、问决策问题、强制阶段门禁。
建议控制在 60 行以内，细节全部指向 runbook。

本仓库已经放了一份最小 skill：`.agents/skills/hd-texture-port/SKILL.md`。
想在其他模拟器项目里也用它，把整个目录复制到 `~/.agents/skills/` 即可。
骨架如下，改流程时同步改它（但不要把 runbook 的内容复制进去）：

```markdown
---
name: hd-texture-port
description: Port the HD texture pack pipeline (PNG/WebP/DDS BCn/.dds.zst import,
  content hashing, async decode, chunked GPU upload, bindless sampling, hot reload)
  to another emulator. Use when the user asks to modularize or port HD texture
  replacement, or to add DDS/WebP/PNG replacement support to another emulator.
---

# HD texture port

1. Read `Supermodel-VK-HD/devdocs/RUNBOOK-HD-TEXTURES.md` first. It is the source
   of truth.
2. Before editing code, get explicit answers to:
   - Must existing Model 3 packs stay compatible (hash frozen)?
   - Is the target's texture model page/sub-rect or per-texture?
   - Which backend, and does it support BCn?
   - MVP or full scope?
3. If the target already has a texture replacement system, propose a pack converter
   first; do not port the runtime.
4. Enforce the stage gates: golden corpus -> CLI -> target dump -> load/shader ->
   upload -> UI -> matrix. Do not skip the dump gate; a hash mismatch invalidates
   everything downstream.
5. Never: use CLAMP for replacements, Y-flip DDS, blit compressed images, decode on
   the render thread, or return white for an unready slot.
6. If the target is not GPL-compatible, reimplement from the runbook, do not copy code.
```

---

## 12. 一页命令卡（Model 3 参考实现）

```bash
# 增量构建 + 部署（项目约定；终端是 sh）
cmd.exe //c 'build-cmake.bat -DUSE_SDL3=ON'
cp build-cmake/bin/Model3-Vulkan.exe dist/Supermodel-VK/Model3-Vulkan.exe

# 改了着色器先重编译
cmd.exe //c 'Scripts\CompileVulkanShaders.cmd'

# 导出一套原图（制作 HD 包的第一步；要跑一段让 dump 线程写完）
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Capture-Frame.ps1 \
  -Set daytona2 -Slot 1 -At 20 \
  -Rom "E:\EMU\model-3-romset\daytona2.zip" \
  -Out "E:\tmp\dump-run.png" \
  -Ini "RefreshRateMode=0;DumpHDTextures=true"

# 固定帧抓图（HD 开/关各一次）
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Capture-Frame.ps1 \
  -Set daytona2 -Slot 1 -Frame 190 \
  -Rom "E:\EMU\model-3-romset\daytona2.zip" \
  -Out "E:\tmp\hd-on.png" \
  -Ini "RefreshRateMode=0;MSAA=4;LoadHDTextures=true"

# 同步验证
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Validate-Vulkan.ps1 \
  -Set daytona2 -Slot 1 -Mode sync

# 参考日志（dist/Supermodel-VK/Data/Supermodel.log）
#   HD textures: N replacement(s) indexed ... (M block-compressed, ...)
#   HD textures: first match - hash ... -> '...' (.dds, slot 0).
#   HD textures: first replacement is sampleable (256x256, 9 mip(s), BC7).
#   HD textures: uploaded in 3 piece(s) over 2 frame(s).
```

数据位置：

```
dist/Supermodel-VK/Data/Assets/HDTextures/<set>/     # 生效的替换包
dist/Supermodel-VK/Data/Assets/DumpTextures/<set>/   # 导出的原图
dist/Supermodel-VK/Data/Config/Model3-Vulkan.ini     # LoadHDTextures / DumpHDTextures
```

---

## 13. 维护这份文档

- 新增格式/后端时，先改第 4 节的契约和第 7 节的适配表，再改代码。
- 每次移植踩到新坑，追加到第 9 节“反模式”，并在 `devdocs/FIXES.md` 写一条
  症状 → 定位 → 根因（如果可复用）。
- 本文件是流程手册；Model 3 的具体实现细节仍以源码和根目录
  `hdtexture_skills.md` 为准，不要在两处复制大段代码说明。
