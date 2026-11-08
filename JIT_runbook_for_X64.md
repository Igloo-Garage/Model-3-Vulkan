# PowerPC 603e → x86-64 (Windows / Linux) 混合模式 JIT 移植手册 (`JIT_runbook_for_X64.md`)

> 本文档提炼自 Android 版 `Arm64Emitter.h` + `ppc_jit_arm64.inl` 的完整设计、踩坑与 6.68 亿+ 块逐块锁步校验经验，用于在 PC 版 **Model 3-Vulkan**（Windows x64 + Linux x64）上复刻零外部依赖的 **PowerPC 603e → x86-64 混合模式 JIT**。

---

## 1. 为什么采用“混合模式基础块 JIT”（Hybrid Baseline JIT）

Supermodel 的 PowerPC 603e 解释器（`Src/CPU/PowerPC/ppc.cpp` + `ppc603.c` + `ppc_ops.c`）把所有 CPU 状态放在文件级静态结构 `static PPC_REGS ppc` 中，并通过 `optable` / `optable19` / `optable31` / `optable59` / `optable63` 五张函数指针表派发指令。

传统重型重编译器（如旧版 `ppc_fe.c`）之所以在 Model 3 上难以启用或容易出 Bug，是因为：
1. 跨块寄存器分配会与异常（`0x500` 外部中断、`0x900` 递减器中断）、总线访问副作用（写 Real3D/TileGen 触发中断改写 `ppc.npc`）产生同步不一致；
2. 递减器（Decrementer）与时基（Timebase）严格依赖 `ppc.icount` 在跨越 `ppc.dec_trigger_cycle` 那一刻触发；
3. 罕见的特权指令、复杂浮点影响 `FPSCR` 的指令或字符串指令一旦重写有细微偏差，游戏就会卡死或物理错乱。

**混合模式 JIT 的核心思想**：
- **同编译单元内联包含**：在 `ppc.cpp` 末尾（`#include "ppc_ops.h"` 之后）直接 `#include "ppc_jit_x64.inl"`，共享 `ppc`、`FastRam`、`FastCrom`、`optable*`、`READ8/16/32`、`WRITE8/16/32`，**零状态复制、零外部库依赖**。
- **高频指令（81%–86%）直接发射原生机器码**：整数算术/逻辑/移位/比较、条件分支、字节反转、CR 搬移、单/双精度浮点访存（`lfs`/`stfs`/`lfd`/`stfd` 等）、浮点符号操作（`fmr`/`fneg`/`fabs`/`fnabs`）全部内联发射为原生指令，直接读写 `PPC_REGS` 内存槽位。
- **低频/复杂指令（14%–19%）编译期解析 Handler 直接 `CALL`**：在翻译块时调用 `HandlerFor(opcode)` 拿到解释器自己的 C 函数指针，生成代码里直接 `CALL rax`。对于不读取 `icount`（非 `mfspr`/`mftb`/`mtspr`）且不终止块的 C Handler，连 `icount` 都无需在调用前同步，留在块出口一次性扣除。
- **内置逐块锁步校验器（Lockstep Verifier，`SM3D_PPC_JIT_VERIFY=1`）**：每个块先跑 JIT（写 RAM 前按 4 KB 页备份到 `s_savedPages`），若未触碰设备寄存器（`!s_jit.busTouched`），则回滚 RAM 页与 `ppc` 状态，用解释器单步跑同样数量的指令，逐寄存器比对 `r[0..31]`、`fpr[0..31]`、`cr[0..7]`、`lr`、`ctr`、`xer`、`msr`、`fpscr`、`pc`、`npc`、`icount`。任何 1 bit 差异立即打印 PowerPC 反汇编 + 机器码 Hex 并 `abort()`。

---

## 2. 前置条件：先确认 `PPC_FAST_MEMORY` 与 Idle-Loop Skip 已就位

如果目标 PC 仓库尚未合入 Android 分支的 Fast Memory 与 Idle-Loop Skip，需先补齐以下两小块（若已有则跳过）：
1. **`PPC_FAST_MEMORY`（`ppc.h` / `ppc.cpp` / `Model3.cpp`）**：
   - 注册 `ram`（8 MB，`0x00000000..0x007FFFFF`，32 位字内字节序已交换）、`ramWrite`、`crom`（固定窗口 `0xFF800000..0xFFFFFFFF`）、`cromBank`（当前 Bank 窗口 `0xFF000000..0xFF7FFFFF`）。
2. **`ppc_execute_one_instruction()` 与轻量级 Idle-Loop Skip（`ppc603.c`）**：
   - 将 `ppc_execute()` 单步循环体抽成 `static UINT32 ppc_execute_one_instruction(void)`，供 `Execute()` 跨越 `dec_trigger_cycle` 时单步过渡，以及供 `VerifyBlock()` 作参考运行。
   - 空转跳过（Idle-Loop Skip）：`PPC_IDLE_STATE` 只快照/比对整数与控制状态寄存器（`r[32]`, `cr[8]`, `lr`, `ctr`, `xer`, `msr`, `fpscr`，共 156 字节，无需拷贝 320 字节的 `fpr[32]` 与 `sr[16]`），且 `kIdleSnapshotAt = 2`、`kIdleCompareAt = 3`（第 2 次向后跳转拍快照、第 3 次比对一致即快进 `icount` 至 `0` 或 `dec_trigger_cycle + 1`）。

---

## 3. x86-64 架构设计与 ABI 铁律（Windows x64 vs Linux System V）

Model 3-Vulkan 同时支持 **Windows x64** 与 **Linux x64**，因此 `X64Emitter.h` 与 `ppc_jit_x64.inl` 必须同时兼容 **Microsoft x64 ABI** 和 **System V AMD64 ABI**。

### 3.1 两种 ABI 的核心差异与统一方案

| 特性 | Windows x64 ABI (`_WIN32`) | Linux / macOS System V ABI | **JIT 统一规则** |
| --- | --- | --- | --- |
| **整数参数寄存器** | `rcx`, `rdx`, `r8`, `r9` | `rdi`, `rsi`, `rdx`, `rcx` | 定义编译期常量 `ARG0`, `ARG1`, `ARG2` |
| **Shadow Space (Home Space)** | **必须**：`CALL` 前栈顶 `[rsp..rsp+31]` 必须预留 32 字节供被调函数写参数！ | 不需要 | 块序言统一分配固定栈帧（含 32B Shadow Space + 16B 本地暂存槽），块内所有 `CALL` 无需增减 `rsp` |
| **栈对齐约束** | `CALL` 指令执行前 `rsp % 16 == 0`（进入函数体时 `rsp % 16 == 8`） | 同左 | 序言 `push` 5 个 Callee-saved 寄存器（5×8=40B，此时 `rsp % 16 == 0`），再 `sub rsp, 48`（保持 `rsp % 16 == 0`） |
| **Callee-saved 整数寄存器** | `rbx`, `rbp`, `rdi`, `rsi`, `r12`, `r13`, `r14`, `r15` | `rbx`, `rbp`, `r12`, `r13`, `r14`, `r15` | **固定驻留指针统一用 `rbx, r12, r13, r14, r15`**（两边都是 Callee-saved，且不占 Linux 的 `rdi`/`rsi` 参数寄存器！） |
| **Callee-saved 浮点/XMM** | **`xmm6..xmm15` 高低位是 Callee-saved**，只有 **`xmm0..xmm5` 是 Caller-saved** | `xmm0..xmm15` 全是 Caller-saved | **严禁在生成代码中使用 `xmm6..xmm15`！只用 `xmm0` / `xmm1`！** |
| **可执行内存分配** | `VirtualAlloc(NULL, 16MB, MEM_COMMIT \| MEM_RESERVE, PAGE_EXECUTE_READWRITE)` | `mmap(..., PROT_READ \| PROT_WRITE \| PROT_EXEC, ...)` | 封装 `AllocCodeCache()` / `FreeCodeCache()`，x86-64 硬件保证 I-Cache 与 D-Cache 一致，无需软件刷行 |

> **血泪教训（ARM64 `d8..d15` 污染事件在 Windows x64 上的对应物）**：
> 在 Windows x64 ABI 下，**`xmm6..xmm15` 是非易失（Callee-saved）寄存器**！MSVC / Clang `-O2` 编译 `Execute()` -> `BuildBlock()` 时极常把常量向量或循环变量缓存在 `xmm6..xmm15` 中。如果 JIT 块使用了 `xmm6..xmm15` 却未在序言保存，会直接破坏宿主编译器的寄存器状态！因此 **JIT 生成代码中只允许使用 `xmm0` 和 `xmm1`**。

### 3.2 块内固定寄存器分配（Pinned Registers）

全部选用在 Windows x64 与 Linux System V 下**共同属于 Callee-saved**、且**不与任何函数调用参数寄存器冲突**的 5 个寄存器：

| x86-64 寄存器 | 用途 | 说明 |
| --- | --- | --- |
| **`rbx`** | `&ppc` (`PPC_REGS *`) | 访问所有 Guest 寄存器：`[rbx + disp32]` |
| **`r12`** | `FastRam` (`const UINT8 *`) | 8 MB 主内存快速读基址 |
| **`r13`** | `FastCrom` (`const UINT8 *`) | 固定 CROM 窗口基址 (`0xFF800000`) |
| **`r14`** | `FastCromBank` (`const UINT8 *`) | 当前 Bank CROM 窗口基址 (`0xFF000000`) |
| **`r15`** | `FastRamWrite` (`UINT8 *`) | 8 MB 主内存快速写基址 |

**块内临时（Scratch）寄存器**：
- 整数临时：`eax`/`rax`、`ecx`/`rcx`、`edx`/`rdx`、`r8d`/`r8`、`r9d`/`r9`、`r10d`/`r10`、`r11d`/`r11`（全部在双平台下可自由读写，跨 `CALL` 不保留）。
- 建议固定约定（与 `ppc_jit_arm64.inl` 的 `w9..w13` 一一对应，极易对照移植）：
  - `ecx` (`RCX`)：有效地址 `ea`（对应 ARM64 的 `w9`）
  - `edx` (`RDX`)：临时操作数 A（对应 ARM64 的 `w10`）
  - `r8d` (`R8`)：临时操作数 B（对应 ARM64 的 `w11`）
  - `r9d` (`R9`)：立即数暂存（对应 ARM64 的 `w12`）
  - `eax` (`RAX`)：访存读写值 / 算术结果（对应 ARM64 的 `w13`）
- 浮点临时：仅使用 **`xmm0`**（必要时 **`xmm1`**）。

### 3.3 栈帧布局（Prologue / Epilogue）

进入 `block->fn()` 时，`CALL` 已将 8 字节返回地址压栈，此时 `rsp % 16 == 8`：
```nasm
; EmitPrologue:
push rbx
push r12
push r13
push r14
push r15                ; 5 * 8 = 40 字节，此时 rsp % 16 == 0！
sub  rsp, 48            ; 分配 48 字节：
                        ;   [rsp + 0 .. 31]  : 32B Windows x64 Shadow Space（任何 CALL 可随时覆盖）
                        ;   [rsp + 32]       : kFrameScratchA（跨慢速总线 CALL 保存有效地址 ea）
                        ;   [rsp + 40]       : kFrameScratchB（跨 JitSavePage CALL 保存写入值）
mov  rbx, imm64(&ppc)
mov  rax, imm64(&s_pins)
mov  r12, [rax + 0]     ; FastRam
mov  r15, [rax + 8]     ; FastRamWrite
mov  r13, [rax + 16]    ; FastCrom
mov  r14, [rax + 24]    ; FastCromBank

; EmitEpilogue:
; 扣除未同步的 icount:
sub  dword ptr [rbx + offsetof(PPC_REGS, icount)], pending
; 恢复栈帧并返回:
add  rsp, 48
pop  r15
pop  r14
pop  r13
pop  r12
pop  rbx
ret
```
> 注意：因为 `[rsp + 0..31]` 留给被调函数的 Shadow Space，我们自己的跨 `CALL` 暂存槽 `kFrameScratchA` / `kFrameScratchB` **必须放在 `[rsp + 32]` 和 `[rsp + 40]`**（绝不能放在 `[rsp + 0..31]`，否则 Windows x64 下被调函数会把参数写进 `[rsp + 8..32]` 覆盖你的暂存值！）。

---

## 4. PowerPC → x86-64 指令映射中的四大极易踩坑点（必读！）

在移植 `EmitOne()` 时，下面四个硬件语义差异必须 100% 严格遵守（锁步校验器会逐位检查）：

### 坑点 1：减法进位 `XER[CA]` 在 x86 `SUB` 与 ARM64 `SUBS` 上的标志位**正好相反**！
- **PowerPC 定义**：
  - 加法（`addic` 12、`addic.` 13、`addcx` 31/10、`addzex` 31/202）：无符号溢出时 `XER[CA] = 1`（即 `(UINT32)(a + b) < (UINT32)a`）。
    - 在 x86-64 上：`add eax, edx` 后，`CF = 1` 表示无符号进位，**`SETC`（Carry，`0x0F 0x92`）直接等于 `XER[CA]`**。
  - 减法（`subfic` 8：`rD = SIMM16 - rA`；`subfcx` 31/8：`rD = rB - rA`）：PowerPC 定义为 `a + ~b + 1` 的进位，即**无借位（`(UINT32)minuend >= (UINT32)subtrahend`）时 `XER[CA] = 1`**！
    - 在 ARM64 上，`SUBS` 的硬件 `C` 标志也是“无借位 = 1”（`C_CS`）；
    - **但在 x86-64 上，`sub eax, ecx` 的硬件 `CF` 标志是“有借位（Borrow）= 1”**！
    - 因此，在 x86-64 上执行 `sub` 计算 `subfic` / `subfcx` 后，**必须用 `SETNC` / `SETAE`（No Carry / Above or Equal，`0x0F 0x93`）来取 `XER[CA]`**，绝不能用 `SETC`！

### 坑点 2：有符号比较 `cmp`/`cmpi` vs 无符号比较 `cmpl`/`cmpli`
- `cmp` (31/0) 和 `cmpi` (11) 是**有符号比较**（`INT32`）：
  - 在 x86-64 `cmp eax, edx` 后：`<` 用 `JL` / `CMOVL`（Less），`>` 用 `JG` / `CMOVG`（Greater）。
- `cmpl` (31/32) 和 `cmpli` (10) 是**无符号比较**（`UINT32`）：
  - 在 x86-64 `cmp eax, edx` 后：`<` 用 `JB` / `CMOVB`（Below），**`>` 必须用 `JA` / `CMOVA`（Above）**！
  - 若误用有符号 `CMOVG`，`daytona2` 的 IEEE 754 双精度数学库函数 `0x000A939C`（`cmpli cr5,0,r5,0x47`）在 `r5 < 0` 时会走错分支。

### 坑点 3：变长移位（`slwx` / `srwx` / `rlwnm`）与 `CL` 寄存器掩码
- x86-64 的变长移位指令 `shl reg, cl` / `shr reg, cl` / `rol reg, cl` 只认 **`cl`（`rcx` 的低 8 位）**，而且 **x86 硬件自动将移位量模 32（`& 0x1F`）**！
- 而 PowerPC 的 `slwx` (31/24) 和 `srwx` (31/536) 规定：移位量取 `rB & 0x3F`（低 6 位），**当 `(rB & 0x3F) >= 32` 时结果必须为 `0`**！
- 因此在 x86-64 上实现 `slwx` / `srwx` 时：
  1. 先把 `rB` 读入 `ecx`，`and ecx, 0x3F`；
  2. `shl eax, cl`（或 `shr eax, cl`）；
  3. `cmp ecx, 32`；`mov edx, 0`；`cmovae eax, edx`（当 `(rB & 0x3F) >= 32` 时强制清零）！
- 而 `rlwnm` (23) 是循环左移 `rB & 0x1F` 位，x86 的 `rol eax, cl` 天然对 `cl` 做 `& 0x1F`，只需一条 `rol eax, cl` + `and eax, mask` 即可！

### 坑点 4：字节反转指令 `lwbrx` / `stwbrx` / `lhbrx` / `sthbrx` 与 `cntlz`
- **32 位字节反转（`lwbrx` 31/534、`stwbrx` 31/662）**：x86-64 原生指令 **`bswap eax`**（`0x0F 0xC8 + rd`），1~2 字节搞定！
- **16 位字节反转（`lhbrx` 31/790、`sthbrx` 31/918）**：使用 `rol ax, 8`（`66 C1 C0 08`）或 `shr + shl + or` 交换低 16 位的两个字节（注意 `bswap eax` 会把低 16 位挪到高 16 位，对 16 位必须用 `rol ax, 8` 或移位拼合）。
- **前导零计数（`cntlzwx` 31/26）**：
  - 若使用 `lzcnt eax, edx`（`F3 0F BD /r`），所有支持 AVX2 的现代 x64 CPU 均支持；
  - 或者用基础 x86-64 100% 兼容的写法：`bsr eax, edx`（`0F BD /r`）+ `mov ecx, 63` + `cmovz eax, ecx` + `xor eax, 31`（当输入为 0 时 `bsr` 置 `ZF=1`4，`63 ^ 31 = 32`；当输入非 0 时 `31 ^ bsr = clz`，在任何 x86-64 CPU 上无需检查 CPUID 即可 100% 精确工作！）。

---

## 5. x86-64 优势：比 ARM64 更短的生成代码

x86-64 支持直接对 `[rbx + disp32]` 内存操作数做算术/比较/立即数写入，且立即数支持完整 32 位，无需字面量池（Literal Pool）：
1. **无需 Literal Pool**：所有 32 位立即数直接内联在指令中；64 位指针（`&ppc`、`&s_pins`、C Handler 地址）用 `mov rax, imm64`（`48 B8 + 8B`）直接加载。
2. **双精度浮点访存 `lfd` / `lfdu` / `stfd` / `stfdu`（及索引形式）**：
   - 与 ARM64 相同，直接复用两次 `EmitMemAccess(b, MK_READ32 / MK_WRITE32)`：
     - 高 32 位在 `ea + 0` <-> `[rbx + fprOffset + 4]`
     - 低 32 位在 `ea + 4` <-> `[rbx + fprOffset + 0]`
3. **单精度浮点访存 `lfs` / `lfsu` / `stfs` / `stfsu`（及索引形式）**：
   - Load（`lfs`）：`EmitMemAccess(b, MK_READ32)` 结果在 `eax` -> `movd xmm0, eax` -> `cvtss2sd xmm0, xmm0` -> `movsd [rbx + fprOffset], xmm0`。
   - Store（`stfs`）：`movsd xmm0, [rbx + fprOffset]` -> `cvtsd2ss xmm0, xmm0` -> `movd eax, xmm0` -> `EmitMemAccess(b, MK_WRITE32)`。
4. **浮点寄存器搬移与符号操作（`case 63`：`fmr` 72、`fneg` 40、`fabs` 264、`fnabs` 136，`!rc`）**：
   - `fmr` / `fneg` / `fabs` / `fnabs` 是纯位级操作（不触碰 `FPSCR`，保留 NaN payload），在 x86-64 上甚至不需要进 XMM，**直接用 64 位整数寄存器 `rax` 操作 `[rbx + fprOffset]` 的 bit 63 即可**！
     - `fmr`：`mov rax, [rbx + fb*8]` -> `mov [rbx + frt*8], rax`
     - `fneg`：`btc rax, 63`（翻转 bit 63）
     - `fabs`：`btr rax, 63`（清零 bit 63）
     - `fnabs`：`bts rax, 63`（置位 bit 63）
   - 用 `rax` + `btc`/`btr`/`bts`（或 `mov rax, 0x8000000000000000ULL` + `xor`/`andn`/`or`）不仅比 XMM 更快，而且 **0 浮点寄存器占用**！

---

## 6. 推荐文件结构与 GUI 选项设计

### 6.1 文件清单

| 文件路径 | 职责 |
| --- | --- |
| `Src/CPU/PowerPC/X64Emitter.h` | 轻量级 x86-64 机器码发射器（支持 `mov`/`add`/`sub`/`and`/`or`/`xor`/`cmp`/`test`/`shl`/`shr`/`sar`/`rol`/`ror`/`bswap`/`cmovcc`/`setcc`/`jmp`/`jcc`/`call`/`ret`、`movd`/`cvtss2sd`/`cvtsd2ss`/`movsd`、分支回填 Fixup），容量检查完备 |
| `Src/CPU/PowerPC/ppc_jit_x64.inl` | PowerPC 603e → x86-64 混合 JIT 主体（4-way 组相联块缓存、SMC 4KB 页写追踪、C-Helper 快速调用、原生指令发射、Lockstep Verifier、统计信息 `GetStats`） |
| `Src/CPU/PowerPC/ppc.cpp` / `ppc603.c` / `ppc.h` | 定义 `PPC_JIT_X64`（当 `(defined(_M_X64) \|\| defined(__x86_64__)) && !defined(SUPERMODEL_OPENGL_ONLY) && !defined(PPC_VERIFY_FAST_MEMORY) && !defined(SUPERMODEL_DEBUGGER) && !defined(SUPERMODEL_DISABLE_PPC_JIT)` 时启用），接入 `ppc_jit_init` / `shutdown` / `execute` / `flush_all` / `note_fast_memory` / `get_stats` / `ppc_set_jit_enabled` |
| `Src/OSD/SDL/Main.cpp` / `Gui.cpp` / `Localization.cpp` | 新增 Vulkan 专属配置键 `PowerPCMode`（在「核心 / Core」Tab 页选择 `JIT 混合模式（推荐）` 或 `C 纯解释器模式`） |
| `Tools/jit_x64_emit_check.cpp` | 宿主机编码器自检程序（对比 `llvm-objdump` / 参考字节流，验证每条 x86-64 指令字节编码 100% 正确） |

### 6.2 在 GUI「核心（Core）」Tab 增加执行模式切换选项（验证全部通过后接入）

遵守项目规则（**新功能只做 Vulkan 版，OpenGL 版 `Supermodel-LegacyOG.exe` 冻结；中文文案写进 `Gui.cpp` 并在 `Localization.cpp` 配齐英文翻译；可点击控件/下拉框使用 `ComboRow` / `HandOver()`**）：
1. **配置键（`Main.cpp` 的 `DefaultConfig()`）**：
   - 在 `#if !defined(SUPERMODEL_OPENGL_ONLY)` 块中新增：
     `config.Set("PowerPCMode", 1, "Core", 0, 0, { 0, 1 });`
     （`0` = C 纯解释器模式，`1` = JIT 混合模式，默认 `1`；并支持命令行 `-ppc-mode=0|1`）。
2. **运行时生效与环境变量优先级（`ppc.h` / `ppc.cpp` / `Main.cpp`）**：
   - 在 `ppc.h` 声明 `extern void ppc_set_jit_enabled(bool enabled);`。
   - 开局（及每帧或重配置）时由 `Main.cpp` 根据 `s_runtime_config["PowerPCMode"].ValueAsDefault<int>(1) != 0` 调用 `ppc_set_jit_enabled(...)`。
   - 若设置了环境变量 `SM3D_PPC_JIT=0` 或 `SM3D_PPC_JIT=1`，环境变量优先覆盖配置项（保持测试脚本兼容）。
3. **界面控件（`Gui.cpp` + `Localization.cpp`）**：
   - 在 `HasDedicatedUI()` 的屏蔽列表中加入 `"PowerPCMode"`（避免 `CreateControls("Core")` 用原始数字滑块渲染它）。
   - 在 `Gui.cpp` 的「核心（Core）」Tab（`CreateControls(config, "Core")` 紧下方，`#if !defined(SUPERMODEL_OPENGL_ONLY)` 内）使用 `ComboRow` 绘制下拉框：
     - 标签：`Tr("PowerPC 执行模式")` / `"PowerPC execution mode"`
     - 选项 0：`Tr("C 纯解释器模式（兼容基准）")` / `"C interpreter (reference)"`
     - 选项 1：`Tr("JIT 混合模式（推荐，x64 原生加速）")` / `"Hybrid JIT (recommended, x64 native)"`
     - 附简短 `(?)` 提示（`HelpMarker`），并在 `Localization.cpp` 的 `kEnglish[]` 中补齐对应英文词条。

---

## 7. 验证与验收标准（五步法）

1. **第一步：编码器自检**：
   编译并运行 `Tools/jit_x64_emit_check.cpp`，反汇编验证 `X64Emitter.h` 所有指令（特别是 REX 前缀 `0x48..0x4F` 在使用 `r8..r15` 时的 `REX.R` / `REX.B` 位、ModRM/SIB `[r12 + rcx]` 当基址为 `r12` 时**必须带 SIB 字节 `0x24`**、以及 `[r13 + disp]` 当基址为 `r13` 且 `disp==0` 时**必须用 `disp8=0`** 的 x86 编码规则！）。
   > **x86-64 编码器的两个经典陷阱**：
   > - `r12`（低 3 位为 `4`，与 `rsp` 相同）作为寻址基址 `[r12 + ...]` 时，ModRM 的 `rm=4` 代表“后跟 SIB 字节”！因此 `[r12 + disp]` 必须发射 SIB `0x24`，`[r12 + idx]` 必须发射 SIB `(0 << 6) | ((idx & 7) << 3) | 4`！
   > - `r13`（低 3 位为 `5`，与 `rbp` 相同）作为无偏移寻址 `[r13]` 时，`mod=00, rm=5` 代表 RIP-relative `[rip + disp32]`！因此 `[r13]` 或 `[r13 + idx]` 必须使用 `mod=01`（带 1 字节 `disp8 = 0x00`）！
2. **第二步：逐块锁步校验（`SM3D_PPC_JIT_VERIFY=1`）**：
   使用现有的 `.zst` / `.st` 存档（如 `scud`、`daytona2`、`vf3tb`），开启 `SM3D_PPC_JIT_VERIFY=1` 运行至少 **5,000 万～1 亿个块**，必须做到 **`0 failed`**。
3. **第三步：画面确定性与帧率 A/B 对比**：
   - 用 `Tools/Capture-Frame.ps1` 在同一存档、固定帧号（`-Frame 300 -Ini "RefreshRateMode=0"`）对比 `SM3D_PPC_JIT=0` 与 `SM3D_PPC_JIT=1` 的截图，确认画面 100% 正常；
   - 用 `Tools/Measure-FrameRate.ps1`（或在解除帧率限制 `-no-throttle` 下）对比 `SM3D_PPC_JIT=0` 与 `SM3D_PPC_JIT=1` 的帧耗时与极限 FPS。
4. **第四步：在 GUI「核心（Core）」页接入 `PowerPCMode` 切换选项并验证中英文界面**：
   - 验证在设置界面「核心」Tab 中切换 `C 纯解释器模式` 与 `JIT 混合模式` 能正确保存到 `Model3-Vulkan.ini` 并在启动游戏时生效（查看 `Data/Supermodel.log` 中的 `PPC: x64 hybrid JIT enabled / disabled`）。
5. **第五步：双目标构建验证**：
   运行 `build-cmake.bat`（或 `build.bat`），确认 `Model3-Vulkan.exe` 与 `Supermodel-LegacyOG.exe` 均 0 错误编译通过（谨记项目规则：新功能只进 Vulkan 版，OpenGL 版保持冻结，用 `#if !defined(SUPERMODEL_OPENGL_ONLY)` 隔离）。
