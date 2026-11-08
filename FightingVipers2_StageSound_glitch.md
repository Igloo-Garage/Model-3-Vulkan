# Fighting Vipers 2 大坝关爆音：视频素材笔记

> 文件性质：视频策划 / 素材笔记，不是发布文档。
> 主题：Fighting Vipers 2 的 Cross River Damsite 胜利画面爆音，以及 Supermodel 里
> `LegacySoundDSP` 这个“祖传开关”的真相。
>
> 一句话：
> **一个保留地址被当成了镜像，一段 DSP 偏移表被清零，一个游戏在模拟器里炸了六年耳机；
> 而修好它的那一行，删掉的正是 2020 年同一笔提交里加进来的错误假设。**

---

## 0. 先看结论

| 问题 | 答案 |
| --- | --- |
| 现象 | Cross River Damsite 赢下比赛后，胜利语音变成“you wi…”，接着一个音符开始循环、音量快速攀升，最后变成满幅高频噪音。 |
| 直接原因 | `0x7C0–0x7FF` 是 SCSP DSP 的保留空洞，不是 MADRS 的镜像。新路径把游戏上传的 32 个 0 镜像进 `MADRS[0..31]`，把刚刚写好的延迟/采样偏移表整张清零。 |
| 为什么以前没人发现 | 旧路径恰好不镜像这段；它还有一个 `EFSDL << 14` 的音量 bug，听起来“能用”。开关被当成传统，没人再问为什么。 |
| 修复 | 三条写入路径（`SCSP_w8/w16/w32`）都丢弃保留段；`LegacySoundDSP` 退休；效果声道统一为 `EFSDL << 13`。 |
| 视频卖点 | 28 年前的游戏、6 年前的开关、一行错误注释、一次删分支引发的崩溃、以及“同一段地址还会擦掉 VF3 / Sega Rally 2 的混响”这个彩蛋。 |

**给标题用的数字**：

- 游戏发售：1998 年（街机）。
- 错误提交：2020-08-22。
- 修复：2026-10（本分支）。
- 所以口播可以说“**这款游戏等了 28 年，这个开关活了 6 年**”；
  不建议把开关本身说成“十几年”——有据可查的是 2020 → 2026。

> 如果你更习惯“十几年”的说法，可以保留体感、把数字说活：
> “这个毛病在玩家记忆里跟了大家很多年；有据可查的开关是 2020 年加的，
> 而游戏本身是 1998 年的。”这样既不会丢掉情绪，也不会在时间线上被挑错。

---

## 1. 为什么这个故事值得做一期视频

1. **它有一个非常戏剧化的症状。** 不是“声音有点爆”，而是胜利语音被截断、单音循环、音量一路爬到满幅。原 issue 作者明确担心会损坏耳机或音箱。
2. **它有一个反直觉的真相。** 大家以为 `-legacy-scsp` 是“另一套 DSP”，实际上 `SCSPDSP.cpp` 只有一份 DSP 核心；新旧路径的全部差异只在 `SCSP.cpp` 的三处写入。
3. **bug 和 workaround 是同一笔提交生的。** 2020-08-22 的提交一边加入 `MADRS is mirrored twice`，一边加入 `LegacySoundDSP`。这等于“造了一个坑，同时发了一把梯子”，然后大家用了六年梯子。
4. **它有一个很漂亮的调试反转。** 删掉镜像之后，模拟器 1–2 秒内就崩了；崩溃转储里的 `rdx=0x7C0`、`rax=0x7FFFFFE0` 把另一个无符号下溢 bug 揪了出来。
5. **它的影响不止一个游戏。** 上游后来重写 SCSP 时的注释指出：VF3 和 Sega Rally 2 会先写延迟抽头、再清零 `0x7C0–0x7FF`；镜像会把抽头擦掉，混响消失。也就是说，同一段地址可能同时制造了“FV2 的噪音”和“别的游戏混响变弱”两种症状。
6. **它适合顺便介绍游戏。** Fighting Vipers 2 是 AM2 的 Model 3 格斗遗珠，装甲破坏、墙壁投掷、Super K.O.、Charlie 的大坝场地和“Under The Sun”都很有画面感。

**核心叙事一句话**：

> 这不是“模拟器声音不好”，而是一个地址被当成了另一个地址。
> 更准确地说：一个**保留空洞**被当成了 **MADRS 的第二窗口**。

---

## 2. 游戏是什么：1998 年的 AM2 街机格斗

### 2.1 基本档案

| 项目 | 内容 |
| --- | --- |
| 名称 | Fighting Vipers 2 / 格斗之蛇 2 |
| 开发 | Sega AM2 |
| 平台 | Sega Model 3 Step 2.0 |
| 街机发售 | 日本 1998-03；美国/英国 1998-08 |
| Dreamcast | 日本 2001-01-18；欧洲 2001-03-02；**美版取消** |
| 导演 | Hiroshi Kataoka（片尾字幕） |
| 声音导演 | Hidenori Shoji（片尾字幕） |
| 原角色设计 | imaitoons |
| 玩法关键词 | 装甲破坏、墙壁投掷、Super K.O.、路线系统、评分系统 |
| 角色 | 初代 10 人 + EMI、CHARLIE；DEL SOL、KUHN 等隐藏/追加角色（以游戏角色选择画面为准） |
| 冷知识 | 开发始于 1997 年初，约 10 个月；团队去 Alcatraz 取景；Tokio 场景的 T-Rex 骨架约 4000 多边形；1998-06 日本街机榜第二；2023 年《如龙外传》把它作为可玩街机收录，是 Dreamcast 版之后第一次官方再版。 |

### 2.2 玩法：这游戏的核心就是“打碎”

- **装甲破坏**：角色身上有可破坏的装甲部件，打碎后外观和受击反馈都会变化。
- **墙壁投掷**：可以把对手砸穿场地墙壁；Model 3 Step 2.0 让背景更复杂，角色还能撞进场景里。
- **Super K.O.**：在特定时机打出多段连击，可以一回合直接终结比赛，不用打满三局两胜。
- **路线系统**：对手顺序不再固定，上路通常更强，相当于内置了难度分层。
- **评分系统**：鼓励复杂、好看的连段，惩罚只会复读简单招。

### 2.3 大坝关：Cross River Damsite

根据 Sega Retro 收录的街机版音乐 credits：

- **Cross River Damsite（v.s. Charlie）**，BGM 是 **“Under The Sun”**。
- Bahn 的场地是 **Serpent's Cage**，BGM 是 **“3rd Signal”**。

这带来一个很有意思的档案矛盾：

- 2020-08-22 的提交信息写的是 **“loud garbage on Bahn's stage in Fighting Vipers 2”**。
- 2026-06-04 的 issue #321 写的是 **“Cross River Damsite stage”**。
- 我们自己的 `devdocs/DSP.md` 复现的也是 Cross River Dam / 大坝关。

**录制前务必确认**：是 2020 年把 Charlie 的场地记成了 Bahn 的场地，还是这个 bug 在两关都会触发？
两种结果都能讲，但不要在没有实测的情况下把“Bahn 的关卡 = 大坝关”说死。

> 口播可以这样处理：
> “原始提交说这是 Bahn 的关卡，后来的 issue 说这是 Cross River Damsite——也就是 Charlie 的大坝场地。
> 连 bug 的档案都记错了关卡，这事有多难查，可见一斑。”

### 2.4 适合片头的冷知识

- 这是《Fighting Vipers》的续作，而《Fighting Vipers》本身是《Virtua Fighter》的衍生系列；片尾还特别感谢了 **Virtua Fighter 3 团队**。
- 街机版 1998 年 3 月上市，Dreamcast 版拖到 2001 年才来；美版 Dreamcast 直接取消，所以很多美国玩家只在街机厅见过它。
- 2023 年《如龙外传》把它做成可玩街机，是这款游戏多年后第一次官方再版。
- 大坝关的 BGM 叫 **“Under The Sun”**；而 bug 发作时，玩家听到的不是“Under The Sun”，是一个单音在太阳底下自焚。

---

## 3. 现象：不是普通杂音，是自激

上游 issue #321（`shadowflash`，2026-06-04）的描述：

> When you win the match on a Cross River Damsite stage instead of hearing "you win" and victory tune playing, after which game should just load next stage you hear you wi... and then there will be looping note ramping up the volume fast, creating a VERY loud noise that sounds like badly scratched CD looping or if anyone remembers - a game with DOS4GW crashing. The issue is not only annoying but rather serious - the noise is so loud that it can damage the speakers or some sensitive headphones if you'll be playing the game at quite high volume.

翻译成口播：

> 你赢下大坝关，本该听到“you win”和胜利音乐，然后进下一关。
> 实际听到的是“you wi…”，然后一个音符开始循环，音量在很短时间内快速攀升，
> 最后变成非常响的噪音——像一张严重划伤的 CD 在循环，或者像 DOS4GW 游戏崩溃时卡住的那一声。
> 这不只是烦人；如果你音量开得大，它可能损坏音箱或敏感耳机。

**注意**：

- 这是原 issue 作者的担心，不是我们实测的损坏结论。视频里说“报告者担心会损坏设备”即可，不要说“一定会烧耳机”。
- 演示音频必须限幅，并在画面上加警告。见第 12.7 节。

---

## 4. 历史：一个开关的诞生

### 4.1 时间线

| 时间 | 事件 |
| --- | --- |
| 1998-03 | Fighting Vipers 2 街机版日本上市（Model 3 Step 2.0）。 |
| 2001-01/03 | Dreamcast 日版/欧版；美版取消。 |
| 2020-08-22 | 上游提交 `816d541b`：SCSP 大改，移植 MAME 实现；**同时**加入 `MADRS is mirrored twice` 和 `LegacySoundDSP`。 |
| 2023 | 《如龙外传》收录 FV2，官方再版。 |
| 2026-06-04 | 上游 issue #321 报告大坝关胜利画面爆音。 |
| 2026-10-21/22 | 本分支定位根因、删除镜像、退休 `LegacySoundDSP`。 |
| 2026-10-21 | 上游 master 的 SCSP 精度重写（`b1f2c162`）也把保留段标成 “nothing here”。 |

### 4.2 2020-08-22：坑和梯子一起出生

提交 `816d541b` 的信息原文：

> Various SCSP improvements and code cleanup:
> - Ported MAME's implementation
> - Corrected FM sound for songs in VF3 that use it
> - Music tempo now closer to real hardware thanks to emulating two SCSP chips.
> - **Add LegacySoundDSP config option for games with SCSP DSP glitches
>   (ex. engine noises in Sega Rally 2, and loud garbage on Bahn's stage in Fighting Vipers 2)**
> - Renamed SysFPS to "SoundClock" ...

同一笔提交里，新路径写下了这句：

```cpp
else if (addr < 0x800) // MADRS is mirrored twice
    *((UINT16 *)(SCSP->DSP.MADRS + (addr - 0x7c0) / 2)) = val;
```

而旧路径（`legacySound == true`）的字节写路径是：

```cpp
else if (addr < 0x7C0)
    ((unsigned char *)SCSP->DSP.MADRS)[(addr - 0x780) ^ 1] = val;
else if (addr >= 0x800 && addr < 0xC00)
    ((unsigned char *)SCSP->DSP.MPRO)[(addr - 0x800) ^ 1] = val;
// 0x7C0–0x7FF 没有分支：字节写被丢弃
```

**这就是故事的核心讽刺**：加 bug 和加 workaround 的是同一笔提交。
从那天起，用户被建议“给这个游戏加 `-legacy-scsp`”，而没有人再问“为什么旧路径没事”。

### 4.3 2026-06-04：maintainer 的答复

issue #321 下，维护者的回复原文：

> Ancient issue. Use the -legacy-scsp option for this game only on the command line,
> or LegacySoundDSP=1 in the config file, to use ElSemi’s old SCSP DSP code.

这句话本身没有错——它确实能让用户先玩上。
但它也完美展示了 workaround 债务的形态：

- 症状有了名字（“Bahn's stage 的 loud garbage”）。
- 解法有了名字（`LegacySoundDSP`）。
- 原因没有名字。
- 六年之后，解法变成了传统，传统变成了“大家都知道，但没人知道为什么”。

**视频里不要嘲笑这个回复**。更好的说法是：

> 这是典型的工程妥协：当年没有时间追根因，先给用户一个能玩的开关。
> 问题在于，开关一旦被命名为“legacy”，它就自动获得了一种“这是旧代码的错”的暗示——
> 而真相恰好相反：旧代码是对的，新代码多写了一段。

### 4.4 用户实际会看到的 ini / 命令行

全局：

```ini
LegacySoundDSP = 1
```

按游戏分段（不同前端/版本的支持方式可能不同，以你手里的 ini 为准）：

```ini
[fvipers2]
LegacySoundDSP = 1
```

命令行：

```bat
Supermodel.exe fvipers2.zip -legacy-scsp
```

这个开关在修复后仍然能被解析，但不再改变行为；日志会写：

```
Sound: LegacySoundDSP is retired and ignored; the corrected SCSP DSP path is always used.
```

---

## 5. 技术背景：SCSP 与 DSP 内存地图

### 5.1 三块内存

Model 3 的音响板使用 SCSP（Yamaha YMF292 系列，和 Saturn 同源），
其中 DSP 是一个 128 步的微程序效果器。它的地址窗口里至少有三块关键内存：

| 地址范围 | 名称 | 作用 |
| --- | --- | --- |
| `0x700–0x77F` | `COEF[64]` | DSP 系数表，16 位有符号。 |
| `0x780–0x7BF` | `MADRS[32]` | 32 个延迟/采样地址偏移，DSP 指令用它来选环形缓冲里的位置。 |
| `0x7C0–0x7FF` | **保留空洞** | 不是 DSP RAM，也不是 MADRS 的第二窗口。硬件上写进去没有意义。 |
| `0x800–0xBFF` | `MPRO[512]` | 128 步 × 4 字的微程序。 |
| `0xBF0` | 触发点 | 写这里会调用 `SCSPDSP_Start()`。 |

用 ASCII 画出来：

```text
0x700 ┌──────────────────────────────────┐
      │ COEF[64]   系数表                 │  0x700–0x77F
0x780 ├──────────────────────────────────┤
      │ MADRS[32]  延迟/采样偏移表        │  0x780–0x7BF
0x7C0 ├──────────────────────────────────┤
      │ 保留空洞（32 words）              │  0x7C0–0x7FF   ← 问题在这里
0x800 ├──────────────────────────────────┤
      │ MPRO[512]  微程序                 │  0x800–0xBFF
0xBF0 │ 写这里触发 SCSPDSP_Start()        │
0xC00 └──────────────────────────────────┘
```

### 5.2 为什么叫“保留空洞”

`MADRS` 只有 32 个 16 位字，正好占 `0x780–0x7BF`。
再往后到 `0x800` 之间空了 64 字节（32 个字），这就是 `0x7C0–0x7FF`。
硬件没有在这段放任何东西；正确的模拟行为是**丢弃写入、读回 0**。

但 2020 年的新路径把它当成了“MADRS 镜像”：

```cpp
else if (addr < 0x800) // MADRS is mirrored twice
    MADRS[(addr - 0x7c0) / 2] = val;
```

`0x7C0` 减 `0x7C0` 等于 0，`0x7FF` 减 `0x7C0` 除以 2 等于 31。
所以这行代码把 `0x7C0–0x7FF` 的 32 个字，原封不动写进了 `MADRS[0..31]`——
也就是**刚刚写好的整张偏移表**。

### 5.3 效果声道：为什么噪音会“越叠越响”

SCSP 的每个 slot 都有：

- `EFSDL`（3 位）：效果发送电平；
- `EFPAN`（5 位）：效果声像；
- DSP 的 `EFREG[16]`：16 个效果返回。

DSP 处理完的湿声会通过 `EFSDL/EFPAN` 混回主输出。
当 `MADRS` 被清零后，DSP 的每个延迟抽头都指向地址 0 附近，
它会反复读同一小段采样，再把结果送回效果返回；
效果返回又参与下一轮混音。听感上就是“一个音符在循环，音量快速攀升”。

> 口播可以把它类比成：
> “你把混响器的 32 个延迟抽头全部拔掉，插到同一个 0 地址上。
> 它不再模拟空间，而是在原地对着自己喊，越喊越大声。”

---

## 6. 根因：一行 “MADRS is mirrored twice”

### 6.1 2020 年的错误假设

`// MADRS is mirrored twice` 这句话的潜台词是：

> “MADRS 有 64 个字，`0x780–0x7FF` 都是它，所以 `0x7C0–0x7FF` 是第二个 32 字窗口。”

但 `SCSPDSP.h` 写得很清楚：

```cpp
INT16 COEF[64];     // 16 bit signed
UINT16 MADRS[32];   // offsets (in words), 16 bit
UINT16 MPRO[128*4]; // 128 steps 64 bit
```

`MADRS` 只有 32 个字，不是 64 个。
`0x7C0–0x7FF` 是保留段，不是镜像。
这句注释把“地址范围看起来连续”误当成了“硬件语义连续”。

### 6.2 FV2 的上传顺序：证据

在 `SCSP_w8()` / `SCSP_w16()` 里加临时探针（已删除），跑 FV2 到开机画面，
日志里的 DSP 写入顺序是：

```text
#1..#20   w8  0x700..0x77F      COEF，真实系数
#21..#72  w8  0x780..0x7B9      MADRS，真实偏移值（如 0x33, 0x37, 0xE8 …）
          w16 0x7BA..0x7BF      0x0000   ← MADRS 的最后 3 个字是 0
          w16 0x7C0..0x7FF      0x0000   ← 整段保留空洞清零
          w16 0x800..0xBF8      微程序（在 0x7C0 之后约 30 行才出现）
          w16 0xBF0             触发 SCSPDSP_Start()
```

关键点：

1. FV2 是**按地址顺序上传一整块 DSP 映像**。
2. `0x780–0x7B9` 是 29 个真实偏移值；`0x7BA–0x7BF` 是 3 个 0，凑满 32 个字。
3. 紧接着 `0x7C0–0x7FF` 的 32 个字全是 0。
4. 新路径把这 32 个 0 镜像进 `MADRS[0..31]`，**整张偏移表归零**。
5. 微程序在之后才上传，所以这一步不会被后续写入“救回来”。

### 6.3 自激是怎么发生的

`MADRS` 归零后的效果：

- DSP 指令里的 `MASA` 选中 `MADRS[0..31]`，全部是 0。
- 环形缓冲地址变成 `0 + DEC + ...`，所有抽头挤在同一小段数据上。
- 读到的采样被反复处理，效果返回又混回主输出。
- 输出不收敛，形成“单音循环 + 音量攀升”的自激。

我们没有逐指令追踪 DSP 程序的反馈拓扑，所以视频里说“自激形态”即可，
不要声称已经算出具体的环路增益。

### 6.4 旧路径为什么没事

旧路径的字节写入（`SCSP_w8`）对 `0x7C0–0x7FF` 没有任何分支：

```cpp
if (addr < 0x780)       // COEF
    ...
else if (addr < 0x7C0)  // MADRS
    ...
else if (addr >= 0x800 && addr < 0xC00) // MPRO
    ...
// 0x7C0–0x7FF 落空：丢弃
```

所以 MADRS 保住了。

旧路径的字写入（`SCSP_w16`）确实“收下”了这段，但写法是：

```cpp
else if (addr < 0x800)
    MADRS[(addr - 0x780) / 2] = val;
```

`0x7C0` 算出的下标是 **32**，`0x7FF` 算出的下标是 **63**。
`MADRS` 只有 32 个字，所以这是**越界写**：
它没有写进 `MADRS[0..31]`，而是踩到了紧随其后的 `MPRO[0..31]`
——也就是 DSP 微程序的前 8 条指令（每条 4 个字）。

FV2 之所以没被这个越界写坑到，是因为日志顺序显示：
微程序是在这些清零**之后**才上传的（`0x800–0xBF8` 在 `0x7C0` 之后出现），
被踩到的 32 个字马上就被真正的程序覆盖了。
但这仍然是未定义行为；任何游戏只要往这段地址写字，就可能踩到。

**所以“旧路径没事”不是因为它更正确，而是因为它错得不一样**：

- 字节路径：丢弃 → MADRS 安全。
- 字路径：越界写 MPRO → MADRS 安全，但踩微程序。
- 新路径：镜像写 MADRS → MADRS 被清零 → 自激。

---

## 7. 调查过程：三次反转

### 7.1 反转一：根本不存在“两套 DSP”

从命名看像是两个实现：

- `SCSPDSP.cpp` 里只有一份 `SCSPDSP_Step()`；
- `SCSP.cpp` 里 `legacySound` 只有 5 处引用；
- 所谓新旧引擎的全部差异只有三处：
  1. DSP 内存写入，字节路径；
  2. DSP 内存写入，字路径；
  3. 效果声道混音。

> 口播：
> “我们一直以为 `-legacy-scsp` 会切到 ElSemi 写的另一套 DSP。
> 实际上，DSP 核心只有一份。所谓‘新旧’，只是三条内存写入策略的差别。”

### 7.2 反转二：把音量改对，问题没有变

旧路径的效果声道混音是：

```cpp
UINT16 Enc = (EFPAN << 8) | (EFSDL << 14);
```

但 `LPANTABLE` 的布局是：

- `TL`：0–7 位
- `PAN`：8–12 位
- `SDL`：**13–15 位**

`EFSDL` 是 3 位。左移 14 位会把最高位顶到第 16 位，被 `UINT16` 截掉，
实际等价于 `(EFSDL & 3) << 1`，音量档位只剩 `{0, 2, 4, 6}`；
`EFSDL >= 4` 直接静音。

2020 年的代码里还有一句很诚实的注释：

```cpp
// For legacy option, 14 is the most reasonable value I can set at the moment for the EFSDL slot. - Paul
```

也就是说，旧路径的效果音量本来就是“先凑一个能听的值”。

把它改成 `<< 13` 后，两个引擎的混音**完全一致**。
如果爆音是“效果声道电平失控”导致的，旧路径应该变得更糟；
实际结果：旧路径依然没有爆音。

**结论**：爆音与效果电平无关。既然混音已经相同，两个引擎剩下的唯一差异
就是 `0x7C0–0x7FF` 的写入策略。

> 这是整个调查里最漂亮的一步：
> 不是“找到一个更像 bug 的 bug”，而是**把两个变量消到只剩一个**。

### 7.3 反转三：探针抓到了上传顺序

临时探针（已删除）记录了第 6.2 节的顺序：

- `0x700–0x77F`：COEF；
- `0x780–0x7B9`：真实 MADRS；
- `0x7BA–0x7BF`：补 0；
- `0x7C0–0x7FF`：整段 0；
- `0x800–0xBF8`：微程序；
- `0xBF0`：触发。

真相到这里就闭合了：

> 游戏不是“写坏了 MADRS”，而是**按顺序上传一整块映像**；
> 是模拟器把映像里的填充 0 当成了 MADRS 的第二窗口，替游戏“写坏”了 MADRS。

### 7.4 尾声：统一路径之后，模拟器崩了

删掉镜像、把所有游戏统一到“丢弃保留段”的路径后，
第一版构建在进游戏 1–2 秒内 `0xC0000005`；`-no-sound` 跑 12 秒正常。

这就是第 8 节的故事。

---

## 8. 第二个 bug：`SCSP_w32` 的无符号下溢

### 8.1 症状

- `Supermodel-LegacyOG.exe` 与 `Supermodel-VK-SDL3.exe` 都在进游戏 1–2 秒内崩溃。
- 异常码 `0xC0000005`（访问违例）。
- `-no-sound` 正常，说明问题在声音路径。
- 日志没有异常信息。

### 8.2 定位

WER minidump 的 ExceptionStream：

- 写操作，目标地址 `0x7FF8E4550DAC`；
- RIP = exe `+0x1AD91A`；
- CONTEXT：`rdx = 0x7C0`（`addr`），`rax = 0x7FFFFFE0`，`r8 = SCSPs`。

临时给 MSVC 链接加 `/MAP`，先用 `cmp` 确认新 exe 与部署 exe 的代码段一致
（只有 6 字节 PE 头差异），再把偏移解析成 `SCSP_w32 + 0x14A`。
反汇编：

```asm
mov word ptr [r8 + rax*2 + 0x159C], r10w
```

也就是：

```cpp
SCSP->DSP.MPRO[(addr - 0x800) / 2] = val;
```

### 8.3 根因

`SCSP_w32()` 的分支链在删除 MADRS 镜像后变成了：

```cpp
else if (addr < 0x7c0) { ...MADRS... }
else if (addr < 0xC00) { ...MPRO... }   // 0x7C0–0x7FF 落进了这里
```

`addr = 0x7C0` 时：

- `addr - 0x800` 按 `unsigned` 计算 = `0xFFFFFFC0`；
- 除以 2 = `0x7FFFFFE0`（约 4 GB 的字节偏移）；
- 写入 `SCSPs + 0xFFFFFFC0 + 0x159C` → 直接访问违例。

### 8.4 修法

补一个**显式空分支**：

```cpp
else if (addr < 0x800)
{
    // Reserved hole: discard. Falling through to the MPRO branch would compute
    // (addr - 0x800) / 2 unsigned, i.e. a ~4 GB offset. See SCSP_w8().
}
```

注意：`SCSP_w8()` 不需要这个空分支，因为它本来就不匹配任何分支；
`SCSP_w16()` / `SCSP_w32()` 需要，否则会落进 `MPRO` 分支。

> 这是视频里最好的“第二幕反转”：
> 你以为删掉一行就结束了，结果删掉一行让另一行暴露了。
> 而且它不是“逻辑错误”，是 C 的无符号算术在地址计算里咬人。

---

## 9. 修法：删掉镜像，退休开关

### 9.1 三条写入路径

最终行为：

- `SCSP_w8()`：`0x7C0–0x7FF` 不匹配任何分支 → 丢弃。
- `SCSP_w16()`：显式 `else if (addr < 0x800) { }` → 丢弃。
- `SCSP_w32()`：同样的显式空分支；并补上跨边界写第二个字的保护
  （`0x77E` / `0x7BE` / `0xBFE` 这类地址不再写到数组外）。
- `SCSP_r16()` 的 `0x7C0–0x7FF` 保留区返回 0，和写路径、硬件行为对称。

### 9.2 退休 `LegacySoundDSP`

- 删除全局 `legacySound` 分支。
- 删除 `SCSP_DoMasterSamples()` 里重复的两套效果声道混音循环。
- 删除 `NeedsLegacySoundDSP()` 和 FV2 特判。
- 统一效果声道为 `EFSDL << 13`（硬件布局）。
- `-new-scsp` / `-legacy-scsp` 与 ini 键仍被解析（外部前端不报错），
  但内部不再有任何分支读取它；值为 true 时日志会说明已忽略。
- 设置界面不再提供这个开关；`Gui.cpp` 的 `HasDedicatedUI` 把它列进“不再提供”的键。

`DefaultConfig()` 里的注释变成了：

```ini
; The SCSP DSP now has a single implementation. LegacySoundDSP, -legacy-scsp and
; -new-scsp are still accepted for compatibility with existing front-ends but are ignored.
```

### 9.3 为什么没有加限幅器

直觉修法是“在输出端加 `tanh` 软削波/限幅”。`devdocs/DSP.md` 第七节解释了为什么不做：

- 出口是 `SDL_AUDIO_S16`，SCSP 侧也有 `ICLIP16` / `ClampINT16`；
  持续满幅噪音本来就已经顶到 DAC 上限，`tanh` 只改变波形，不降低响度，护不住耳朵。
- 常见写法 `if (|x| > 0.85) x = tanh(x)` 在拐点是断开的（0.8501 → 0.6912），
  会让所有游戏的大动态都出现咔哒声。
- 真正能保护设备的只有“持续满幅 → 降增益”的熔断，而它对正常的大音量段落会误伤。

要加也只能作为**默认关闭**的选项，而不是默认行为。

---

## 10. 彩蛋：同一段地址，另一个症状

上游在 2026-10-21 的 SCSP 精度重写（`b1f2c162`）里，给 `0x7C0–0x7FF` 写了一段很长的注释：

```cpp
else if (addr < 0x800)
    ;   // 7C0h-7FFh: nothing here (manual Fig. 4.5: MADRS is 32 words at 780h-7BFh).
        // This is not a MADRS mirror: VF3 and Sega Rally 2 write their delay taps to
        // 780h-7BFh and then clear 7C0h-7FFh, which with a mirror wipes the taps and
        // silences their reverb.
```

也就是说：

- FV2 的症状是**自激噪音**；
- VF3 / Sega Rally 2 的症状可能是**混响消失/变弱**；
- 两者共享同一个错误假设：把 `0x7C0–0x7FF` 当成 MADRS 的第二个窗口。

**重要**：这条注释是上游代码里的说明，不是我们逐游戏实测的结论。
视频里可以说“上游重写时的注释指出”，不要说“我们已经实测 VF3 混响消失”。
如果要在片子里下更强的结论，先补 A/B 试听。

> 这一段很适合放在结尾：
> “同一行错误，在一个游戏里是噪音，在另一个游戏里是寂静。
> 这就是为什么模拟器的地址映射值得认真对待。”

---

## 11. 验证与未解之谜

### 11.1 已经确认的

- `SCSPDSP.cpp` 只有一份 `SCSPDSP_Step()`；新旧路径差异只在 `SCSP.cpp` 三处。
- 2020-08-22 提交同时加入镜像和 `LegacySoundDSP`。
- 镜像把 `0x7C0–0x7FF` 写进 `MADRS[0..31]`，而 `MADRS` 只有 32 个字。
- FV2 上传顺序如第 6.2 节日志所示。
- 旧路径字节写丢弃保留段；字写越界进 `MPRO`，但不破坏 `MADRS`。
- 统一路径后 `SCSP_w32` 的无符号下溢会导致 `0xC0000005`。
- 两个构建（LegacyOG / SDL3）在修复后都能跑 FV2 15 秒以上。

### 11.2 还需要人耳验收的

截至 `sdl3_reports/2026-10-22-06-audio-scsp-dsb-network.md` 的记录，自动化只能证明进程稳定，
不能判断听感。录制前建议补做：

1. **FV2 大坝关胜利画面**：无参数、无自激、胜利语音完整；混响/效果仍在。
2. **FV2 `fvipers2o`（Japan 非 A）**：同关卡是否也能复现/修复。
3. **Sega Rally 2**：任意时刻只有一首 BGM，菜单/attract 切换不叠加。
4. **Scud Race / Daytona 2 / Le Mans 24 / VF3**：混响、效果、音量与修复前一致。
5. **10 分钟以上连续播放**：无 57.5 Hz 边界杂音、无缓冲饥饿。
6. **对比旧构建**：旧构建默认路径有爆音；旧构建 `-legacy-scsp` 没有爆音但效果音量偏；新构建无参数干净。这个 A/B/C 是视频的核心演示。

### 11.3 仍未解释的

- 为什么胜利语音恰好被截断在“you wi…”？可能与语音播放和 DSP 程序切换的时序有关，未逐帧追踪。
- 触发点到底是“胜利画面加载了新的 DSP 程序”，还是“这张图的 BGM/效果切换”——探针只覆盖了开机上传，胜利画面没有逐帧记录。
- 2020 年提交信息里的 “Bahn's stage” 和 issue 里的 “Cross River Damsite” 为什么对不上；是记错关卡，还是两关都会触发。
- DSP 自激的具体反馈拓扑；我们只确认了“MADRS 被清零”这个必要条件。
- 上游 issue #321 是否已经关闭；上游 master 的代码已经不再镜像，但 issue 页面可能仍开着。录制前再查一次。

---

## 12. 视频方案

### 12.1 标题与封面

**中文标题候选**：

- 《一个地址，让 28 年前的街机游戏爆音了 6 年》
- 《Fighting Vipers 2 大坝爆音：模拟器里最贵的一行注释》
- 《把 “MADRS is mirrored twice” 删掉之后》
- 《为什么街机厅没事，模拟器却会炸耳机？》
- 《一个保留地址引发的自激：FV2 爆音修复记》

**英文标题候选**：

- *One Wrong Address: The Fighting Vipers 2 Sound Bug*
- *The Bug That Hid Behind a “Legacy” Switch*
- *MADRS Is Not Mirrored Twice*

**封面构图**：

- 左边：Bahn / Charlie 的大坝场地 + 胜利姿势（用 `Assets/Boxarts/fvipers2.webp` 做角标）。
- 右边：内存地图，`0x7C0–0x7FF` 高亮红色，箭头指向 `MADRS[0..31]`，波形在旁边爆掉。
- 大字：**“地址写错了”** / **“ONE WRONG LINE”**。
- 小字：`Fighting Vipers 2 · Model 3 · SCSP DSP`。

**时长建议**：8–12 分钟。技术密度高，但不要超过 15 分钟；把 DSP 指令细节留在文档里，视频只讲到“地址表被清零 → 自激”。

### 12.2 分镜表

| 时间 | 画面 | 声音 | 口播/字幕 |
| --- | --- | --- | --- |
| 0:00 | 黑屏 → 游戏胜利画面 → 波形瞬间爆表 | 先 1 秒“you wi…”，再 2 秒限幅后的噪音，淡出 | “这是 Fighting Vipers 2 的胜利画面。它本来应该播放‘you win’。” |
| 0:20 | 标题卡：游戏封面 + 内存地图 | 游戏 BGM “Under The Sun” 淡入 | “1998 年，AM2 把一群穿装甲的格斗家放进了一座大坝。28 年后，他们在模拟器里赢下一局，会先把你的耳机炸掉。” |
| 0:40 | 游戏画面：角色选择、装甲破坏、墙壁投掷 | 原声 | “Fighting Vipers 2，AM2 的 Model 3 格斗遗珠。装甲可以打碎，对手可以砸穿墙，Super K.O. 可以一回合终结。” |
| 1:30 | 大坝关空镜、Charlie、胜利姿势 | “Under The Sun” | “出问题的是 Cross River Damsite，Charlie 的场地。BGM 叫‘Under The Sun’。” |
| 2:00 | issue #321 截图、维护者回复 | 键盘声/停顿 | “2026 年 6 月，有人报告：赢下大坝关，听到的不是胜利音乐，是一个音符在循环、音量一路爬升。” |
| 2:30 | `LegacySoundDSP=1` / `-legacy-scsp` 高亮 | 低音铺底 | “维护者的答复是：老问题了，给这个游戏加 `-legacy-scsp`，或者 ini 里写 `LegacySoundDSP=1`。” |
| 3:00 | 时间线：1998 → 2020 → 2026 | 节奏点 | “这个开关是 2020 年 8 月 22 日加的。加 bug 和加 workaround，是同一笔提交。” |
| 3:30 | 代码对比：`MADRS is mirrored twice` | 打字机音效 | “新路径说：MADRS 被镜像了两次。听起来很合理，直到你去看手册。” |
| 4:00 | “两套 DSP” 迷思：一个核心 + 三处差异 | 轻快 | “我们以为 legacy 是另一套 DSP。实际上，DSP 核心只有一份；新旧只差三处内存写入。” |
| 4:40 | 代码：`EFSDL << 14` → `<< 13` | 计算器/提示音 | “旧路径的音量也是错的：3 位的 EFSDL 左移 14 位，最高位被截掉，音量档位只剩 0/2/4/6。” |
| 5:10 | A/B 波形：旧路径 vs 新路径 | 两段短音频 | “把音量改对，旧路径依然没有爆音。所以爆音不是电平问题。” |
| 5:40 | 探针日志逐行出现 | 终端打字声 | “FV2 是按地址顺序上传整块 DSP 映像。0x7C0–0x7FF 是保留空洞，它填的是 0。” |
| 6:20 | 内存地图动画：32 个 0 镜像进 MADRS | 低频上升 | “新路径把这 32 个 0 镜像进 MADRS——也就是刚刚写好的偏移表。整张表被清零。” |
| 7:00 | DSP 读地址 0 的循环动画 | 单音循环（限幅） | “所有抽头都指向 0，DSP 反复读同一小段数据，效果返回又混回主输出。自激。” |
| 7:30 | 删掉镜像 → 模拟器崩溃 → minidump 寄存器 | 错误音/心跳 | “删掉镜像之后，模拟器 1–2 秒内就崩了。崩溃转储里 rdx=0x7C0，rax=0x7FFFFFE0。” |
| 8:10 | 反汇编 `mov word ptr [r8 + rax*2 + 0x159C]` | CSI 式停顿 | “无符号下溢：0x7C0 减 0x800，除以 2，变成约 4 GB 的偏移。” |
| 8:40 | 显式空分支 diff | 轻快 | “修法不是加限幅器，是补一个空分支：保留段，丢弃。” |
| 9:10 | 旧构建 / `-legacy-scsp` / 新构建 A/B/C | 三段对比 | “旧构建默认：爆音。旧构建 legacy：没有爆音，但效果音量偏。新构建无参数：干净。” |
| 9:50 | 上游注释：VF3 / Sega Rally 2 混响 | 环境音 | “同一段地址，在别的游戏里可能不是噪音，而是混响消失。” |
| 10:20 | 结尾卡：1998 / 2020 / 2026 | 音乐收束 | “一个地址，一行注释，六年。修好它，不只是为了一个游戏。” |

### 12.3 开场口播（可直接念）

> 1998 年，世嘉 AM2 把一群穿着装甲的格斗家放进了一座大坝。
> 28 年后，这款游戏在模拟器里赢下一局时，会先说出“you wi…”，
> 然后把你耳机炸掉。
>
> 更离谱的是，大家都知道怎么绕过它：
> 加一个 `-legacy-scsp`，或者在 ini 里写 `LegacySoundDSP=1`。
> 但没人知道为什么。
>
> 今天我们把这件事查清楚了。
> 结论是：里面真的有一行代码写错了。
> 而且这行代码，和那个“祖传开关”，是同一笔提交里加进去的。

### 12.4 结尾口播（可直接念）

> 这不是一个游戏的问题。
> 这是一个地址的问题。
>
> 一个保留空洞被当成了镜像，
> 一张偏移表被清零，
> 一个开关被当成了传统。
>
> 把那一行删掉之后，
> Fighting Vipers 2 不需要任何参数，
> VF3 和 Sega Rally 2 的混响也不用再被擦掉。
>
> 游戏等了 28 年，开关活了 6 年。
> 而修复它，只需要承认：
> `MADRS` 不是被镜像了两次。

### 12.5 素材清单

**游戏画面**：

- 角色选择（Charlie / Bahn / Emi）。
- 装甲破坏特写。
- 墙壁投掷 / 砸穿墙。
- Super K.O.
- 大坝关全景、水、堤坝。
- 胜利姿势 + “you win” 文字。
- 失败/继续画面（用于对比“正常声音”）。

**技术画面**：

- `Src/Sound/SCSP.cpp` 的三条写入路径。
- `Src/Sound/SCSPDSP.h` 的 `COEF[64] / MADRS[32] / MPRO[512]`。
- 2020 年提交的 `MADRS is mirrored twice`。
- 2026 年修复的空分支。
- 内存地图动画。
- 探针日志。
- minidump 寄存器 + 反汇编 + MAP 文件。
- 构建日志（LegacyOG / SDL3）。
- ini / 命令行 / 日志里的 “retired and ignored”。

**外部画面**：

- issue #321 截图。
- 维护者回复截图。
- 2020-08-22 commit 页面。
- 上游 2026-10-21 的 “not a MADRS mirror” 注释。
- Sega Retro 的 Cross River Damsite / “Under The Sun” credits。

**音频**：

- 干净版胜利语音 + 胜利音乐。
- 限幅后的爆音（2 秒，淡出）。
- 旧构建 `-legacy-scsp` 的版本。
- 新构建无参数的版本。
- 大坝关 BGM “Under The Sun”（短片段，注意版权/合理使用）。

### 12.6 录制步骤

1. **准备两个构建**：
   - 旧构建：2026-10-21 之前、仍有 `MADRS` 镜像的版本（或临时把镜像加回去，但不要发布）。
   - 新构建：当前 `Supermodel-VK-SDL3.exe` / `Supermodel-LegacyOG.exe`。
2. **准备 ROM**：`fvipers2`（Japan Rev A）与 `fvipers2o`（Japan）各一份。
3. **进入大坝关**：
   - 选 Charlie 路线 / 或在对战模式选 Cross River Damsite。
   - 如果路线随机，先在比赛最后一击前存档（不要在载入/转场画面存），读档后打完。
4. **A/B/C 录音**：
   - 旧构建默认 → 爆音（限幅，2 秒）。
   - 旧构建 `-legacy-scsp` → 无爆音，但注意效果音量偏。
   - 新构建无参数 → 干净。
5. **录屏**：1080p60 / 4K60 均可；OBS 音频 48 kHz，加 Limiter（阈值 -6 dB，释放 100 ms）。
6. **截图**：用 `Tools/Capture-Frame.ps1` 抓关键帧；封面用 `Assets/Boxarts/fvipers2.webp`。
7. **日志**：新构建如果 ini 里仍有 `LegacySoundDSP=1`，确认日志出现 “retired and ignored”；这可以作为“开关已退休”的镜头。
8. **人耳验收**：按第 11.2 节清单，至少完成 FV2 大坝关、SR2 单 BGM、VF3 混响、10 分钟连续播放。

### 12.7 安全与合规

- **爆音演示必须限幅**：建议 -6 dB 到 -12 dB，2 秒内淡出；画面上加“已限幅，原声可能损坏设备”的警告。
- 不要把爆音做成无预警的 jump scare；这是技术视频，不是恐怖游戏。
- 游戏画面/BGM 的使用注意平台版权规则；技术分析、短片段、评论用途通常更安全，但不要整段播放音乐。
- 引用 GitHub issue/commit 时保留链接和日期；引用 Sega Retro/Wikipedia 时注明来源。

---

## 13. 事实核查表

| 说法 | 状态 | 依据 / 备注 |
| --- | --- | --- |
| FV2 是 1998 年 Model 3 Step 2.0 街机游戏 | ✅ | Sega Retro、`Config/Games.xml` |
| 街机日本 1998-03，美/英 1998-08 | ✅ | Sega Retro |
| Dreamcast 日版 2001-01-18、欧版 2001-03-02、美版取消 | ✅ | Sega Retro / Wikipedia |
| 导演 Hiroshi Kataoka，声音导演 Hidenori Shoji | ✅ | Sega Retro production credits |
| Cross River Damsite 是 Charlie 的场地，BGM “Under The Sun” | ✅ | Sega Retro music credits |
| 2020-08-22 提交同时加入镜像和 `LegacySoundDSP` | ✅ | commit `816d541b` |
| 提交信息写的是 “Bahn's stage” | ✅ | commit `816d541b` |
| issue #321 写的是 Cross River Damsite | ✅ | GitHub issue #321 |
| 维护者回复 “Ancient issue…LegacySoundDSP=1” | ✅ | GitHub issue #321 |
| `MADRS` 只有 32 个字 | ✅ | `Src/Sound/SCSPDSP.h` |
| `0x7C0–0x7FF` 是保留空洞，不是 MADRS 镜像 | ✅ | YMF292/SCSP 内存映射；`devdocs/DSP.md`；上游 2026 注释 |
| 旧路径字节写丢弃保留段 | ✅ | 2020 提交的 legacy 分支 |
| 旧路径字写越界进 `MPRO[0..31]` | ✅ | `MADRS[32]` 后紧跟 `MPRO`；`devdocs/DSP.md` |
| 新路径镜像清零 `MADRS[0..31]` | ✅ | 2020 提交 + 探针日志 |
| `EFSDL << 14` 会截掉最高位 | ✅ | 3 位 EFSDL + `UINT16` 索引 |
| `SCSP_w32` 无符号下溢导致 `0xC0000005` | ✅ | minidump / 反汇编 / 报告 |
| VF3 / Sega Rally 2 的混响也可能被镜像擦掉 | ⚠️ | 上游 2026 注释；**未逐游戏实测** |
| 爆音“会损坏设备” | ⚠️ | 原 issue 作者的担心；不是我们的实测结论 |
| “这个开关存在了十几年” | ❌ | 有据可查是 2020-08-22 → 2026-10；游戏本身 1998 年发售。口播可用“游戏等了 28 年，开关活了 6 年”。 |
| “有两套 DSP 实现” | ❌ | 只有一份 `SCSPDSP_Step()`；差异在 `SCSP.cpp` 三处。 |
| “旧路径是正确实现” | ⚠️ | 旧路径在保留段上碰巧做对，但效果音量 `<<14` 和字写越界都是 bug。 |

---

## 14. 参考

**本地文档/代码**：

- `devdocs/DSP.md`：SCSP/DSP 的完整分析、探针日志、建议。
- `sdl3_reports/2026-10-22-06-audio-scsp-dsb-network.md`：修复记录、崩溃取证、验证矩阵。
- `Src/Sound/SCSP.cpp`：三条写入路径、效果声道混音。
- `Src/Sound/SCSPDSP.cpp` / `SCSPDSP.h`：唯一的 DSP 核心、`COEF/MADRS/MPRO` 布局。
- `Src/OSD/ConfigFile.h`：`LegacySoundDSP` 退休后的默认注释。
- `Config/Games.xml`：`fvipers2` / `fvipers2o` 的 ROM 信息。
- `Assets/Boxarts/fvipers2.webp`：封面素材。

**外部链接**：

- GitHub issue #321：<https://github.com/trzy/Supermodel/issues/321>
- 2020-08-22 提交 `816d541b`：<https://github.com/trzy/Supermodel/commit/816d541b98c72c8021cd4202607e063b38ab074c>
- 2026-10-21 上游 SCSP 重写 `b1f2c162`：<https://github.com/trzy/Supermodel/commit/b1f2c162e8a8878f0dd737b900b393d19b8c98bd>
- 上游当前 `SCSP.cpp`：<https://github.com/trzy/Supermodel/blob/master/Src/Sound/SCSP.cpp>
- Sega Retro《Fighting Vipers 2》：<https://segaretro.org/Fighting_Vipers_2>
- Sega Retro production credits：<https://segaretro.org/Fighting_Vipers_2/Production_credits>
- Wikipedia《Fighting Vipers 2》：<https://en.wikipedia.org/wiki/Fighting_Vipers_2>

---

*最后更新：2026-10-26。本文是视频素材笔记，不是发布文档；技术结论以 `devdocs/DSP.md` 和代码为准。*
