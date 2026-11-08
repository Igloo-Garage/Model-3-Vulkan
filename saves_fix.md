# Save-state load fix — implementation and porting notes

**Status:** fixed in Model 3-Vulkan `v0.9.9.6` (source date 2026-11-07).
**Scope:** the raw `.st` / `core.zst` block file produced by `CModel3::SaveState()`, not the
`.zst` compression wrapper. This document is written for porting the same fix to another
platform — in particular the Android build — or for re-implementing the load path in a
different save manager.

The short version: the old “load the state, then wait three seconds” workaround is **no longer
required for correctness**. The root cause was missing device state in the save image, not a
timing problem in the loader. `SM3D_DEFER_LOAD_SECONDS=0` loads `scuddx`, `vf3tb` and `daytona2`
at the first frame and the game runs normally. The release still keeps the three-second guard as
a fallback (see §4.4), so players see the splash for three seconds by default; that is a choice,
not a requirement.

---

## 1. Symptom

Before the fix, a save state loaded at the start of a new session behaved differently from one
loaded three seconds later:

- **Virtua Fighter 3 Team Battle:** the match resumed but ran at roughly **1/25 speed**
  (0.3 s of game time in 8 s of wall clock, versus 7.7 s for the same state loaded 3 s in).
- **Scud Race / VF3:** the PowerPC spun forever on a DMA chain; the game looked frozen or
  ran in slow motion.
- **Attract-mode saves:** the game timed out of its network self-test and rebooted to the
  system-check screen, or hung on “Please wait” at car select.
- **Scud Race with a loaded state:** the timer crawled, the car did not move, or the game sat in
  a wait loop on a byte at `0x00100037`.
- **Sound:** some states restored with music but no sound effects, or with the BGM stopped.

The workaround was to wait **172 emulated frames (≈3 s at 57.524 Hz)** before applying the state,
while the start-up artwork covered the boot. That hid the problem for the tested games but did not
fix it: the state was still being applied to a machine whose devices had not finished their
power-on handshake.

---

## 2. What was actually wrong

A Model 3 save image is a set of named blocks (`CBlockFile`). Several pieces of hardware state
were either absent from the image or restored in the wrong order:

1. **The 53C810 SCSI controller's PCI base address and halt flag were not saved.**
   The game writes the base address through PCI configuration space during boot. Without it,
   `CModel3`'s register window for `0xF9`/`0xC1` ignores every access after a load
   (`SCSI.GetBaseAddress() != 0xC0` in `CModel3::Read8/Write8`). VF3 and Scud Race then wait on
   their DMA chains forever. This is the **slow-motion / freeze** root cause.
2. **The simulated network board (CSimNetBoard) was not saved at all.**
   Its handshake state, both 64 KB comm banks and the 128 KB buffer were missing. A loaded state
   therefore ran the network check against zeros, timed out and rebooted to the self-test. This
   is the **attract-mode save** root cause.
3. **The CPU interrupt line was not re-driven after load.**
   `CIRQ::Read/Write` state is saved, but the physical `ppc_set_irq_line()` is a line, not a
   register; `ppc_load_state()` overwrites MSR. A state saved with an interrupt pending restored
   with the controller reporting it and the CPU never seeing it. `IRQ.RefreshInterruptLine()`
   must be called **after** `ppc_load_state()`.
4. **The sound board's SCSP interrupt line was not re-driven.**
   `SCSP_LoadState()` restores `pend`/`en`, but not the 68K IRQ line. Without
   `SCSP_RefreshIRQ()` the sound board never receives effect commands again: music still plays,
   effects are silent.
5. **The DSB MPEG decoder's active window was not saved.**
   The DSB register shadows (`usingMPEGStart/End`, `usingLoopStart/End`) are not always the
   window actually playing; Scud Race leaves `0x80008000` (“no track”) sentinels after the music
   starts. Restoring from those shadows either hangs `MpegDec` or stops the car-select BGM.
6. **Worker completion flags and the renderer's snapshot pair survived the load.**
   If a load happens between `BeginSimulationFrame()` and `EndSimulationFrame()` (F7 while a
   frame is in flight), the next `EndSimulationFrame()` would skip waiting for the newly posted
   frame, and `SyncGPUs()` could copy culling RAM while the PPC thread was still writing it.

---

## 3. The fix in this tree

### 3.1 `53C810 PCI` block

`Src/Model3/53C810.cpp`, `SaveState()` / `LoadState()`:

```cpp
// after the existing "53C810" block
SaveState->NewBlock("53C810 PCI", __FILE__);
UINT8 halted = Ctx.halt ? 1 : 0;
SaveState->Write(&baseAddress, sizeof(baseAddress));   // UINT32
SaveState->Write(&halted, sizeof(halted));             // UINT8
```

Load:

```cpp
if (Result::OKAY == SaveState->FindBlock("53C810 PCI"))
{
  UINT8 halted;
  SaveState->Read(&baseAddress, sizeof(baseAddress));
  SaveState->Read(&halted, sizeof(halted));
  Ctx.halt = halted != 0;
}
else if (baseAddress == 0)
  baseAddress = 0xC0;   // every Step 1.x game uses this; Step 2.x boards do not
```

Why a **separate** block: old readers stop after `53C810` and never look for the new block; old
saves have no block and fall back to `0xC0`. Do not append the fields to the existing `53C810`
block — that changes its length and breaks every old save.

The constructor initialises `baseAddress = 0`, so a fresh machine starts with the window closed
until the game writes the PCI register.

### 3.2 `SimNetBoard` block

`Src/Network/SimNetBoard.cpp`, `SaveState()` / `LoadState()`:

- magic `kSimNetBoardMagic` (validate it; a bad magic rebuilds the state instead of trusting it)
- `m_state` (`UINT8`), `m_running` (`UINT8`), `m_numMachines`, `m_counter`, `m_segmentSize`,
  `m_IRQ2ack`, `m_status0`, `m_status1`, `m_commbank` (`UINT8`)
- `RAM[0x10000]`
- `Buffer[0x20000]` (both comm banks)

On load, re-derive the `CommRAM`/`externalCommRAM` pointers from `m_commbank`; never save the
pointers themselves:

```cpp
if (m_commbank) { CommRAM = Buffer + 0x10000; externalCommRAM = Buffer; }
else            { CommRAM = Buffer;          externalCommRAM = Buffer + 0x10000; }
```

If the block is missing (old save), call `SynthesizeReadyState()`: it builds the single-machine
`GameType::one` ready state (`m_status1 = 0x2021`, `m_IRQ2ack = 0x01`,
`RAM16[0x400/0x402/0x404] = 0 / 0x3ef0 / 848`) and fills both comm banks. Without this the game
reads zero status, times out and reboots.

**256 → 848 repair.** A save taken during the boot’s “NETWORK CHECKING” diagnostic can carry the
256-byte test segment (`RAM16[0x402] = 0x0200`) instead of Daytona 2’s 848-byte gameplay segment
(`0x3ef0`). With 256 bytes, the loopback copy lands at `0x200` instead of `0x450` and the
car-select screen hangs on “Please wait” while still playing the attract music. `LoadState()`
detects `m_state == ready && m_segmentSize == 0x100` for Daytona 2, rewrites
`RAM16[0x400/0x402/0x404]`, rebuilds both bank headers and `memmove`s the segment area. Keep this
repair; it is what makes old attract-mode saves usable.

### 3.3 Interrupt line re-drive

`Src/Model3/Model3.cpp`, `CModel3::LoadState()`:

```cpp
IRQ.LoadState(SaveState);
ppc_load_state(SaveState);
IRQ.RefreshInterruptLine();   // must be after ppc_load_state()
```

`CIRQ::RefreshInterruptLine()`:

```cpp
ppc_set_irq_line(((irqState & irqEnable) || (irqState & (~0xFFu))) ? 1 : 0);
```

This is the same predicate `Assert()`/`Deassert()` use. It must be called after the CPU state is
restored, because `ppc_load_state()` overwrites MSR (and therefore the CPU's IRQ mask).

### 3.4 Sound board re-drive

`Src/Model3/SoundBoard.cpp`, `CSoundBoard::LoadState()`:

```cpp
SCSP_LoadState(SaveState);
SCSP_RefreshIRQ();   // re-drive the 68K IRQ line from SCSP pend/en
```

Without it: music works, effects are silent. If the port has its own sound-board wrapper, it
must call the equivalent after restoring the SCSP registers.

### 3.5 DSB MPEG companion blocks

`Src/Model3/DSB.cpp`:

- `DSB1 MPEG` / `DSB2 MPEG` block, magic `0x4D504732` (`"MPG2"`), then
  `INT32 activeOffset`, `INT32 activeSize`, `INT32 pos`, `UINT8 loop`.
- `SaveMpegActiveWindow()` writes `MpegDec::GetActiveOffset/GetActiveSize/GetPosition/GetLoop`.
- `LoadMpegActiveWindow()` validates `magic`, non-null ROM, `offset >= 0`, `size > 0`,
  `offset + size <= kMpegRomSize`, and only then calls `MpegDec::SetMemory` and
  `MpegDec::SetPosition`. Missing/invalid block → fall back to the legacy register path.

`SaveState()` also reports `isPlaying = 0` when the legacy range is a sentinel, so an old reader
does not try to restore an invalid window. Old saves without the companion block resume the
track only when the game next issues a command (e.g. entering the course); new saves resume at
the exact byte offset.

### 3.6 Worker flags and renderer snapshot

`CModel3::LoadState()` (before restoring devices):

```cpp
m_sceneDirty = true;
m_simInFlight = false;
ppcBrdThreadDone = false;
sndBrdThreadDone = false;
drvBrdThreadDone = false;
GPU.ResetInterpolation();
```

Also `SetCROMBank(cromBankReg)` is called immediately after reading the bank register, before
`ppc_load_state()`; this republishes the JIT’s fast-memory windows. In a port with a different
JIT or memory map, do the equivalent: invalidate any translated code / cached memory windows
that depend on the CROM bank.

### 3.7 Load order (copy this)

`CModel3::LoadState()` currently does:

1. Read the `Model 3` block (RAM, backup RAM, security RAM, input bank, …).
2. `GPU.LoadState()`, `TileGen.LoadState()`, mark the scene dirty, clear worker flags,
   `GPU.ResetInterpolation()`.
3. `EEPROM.LoadState()`, `SCSI.LoadState()`, `PCIBridge.LoadState()`, `IRQ.LoadState()`.
4. `ppc_load_state()`, then `IRQ.RefreshInterruptLine()`.
5. `SoundBoard.LoadState()` (which calls `SCSP_RefreshIRQ()` internally).
6. `DriveBoard->LoadState()`, crypto, JTAG.
7. `NetBoard->LoadState()` (or `SynthesizeReadyState()`).

The order matters: devices that the CPU reads immediately after resume must be restored before
the CPU, and the interrupt line must be re-driven after the CPU. The network board is last
because it can use the game info (model, game type) and the RAM it has just been given.

---

## 4. Backward compatibility

### 4.1 Block format

`Src/BlockFile.cpp` / `BlockFile.h`: a block file is a sequence of blocks:

```
uint32 blockLength     (including the header)
uint32 nameLength      (including the trailing NUL)
uint32 commentLength   (including the trailing NUL)
char   name[nameLength]
char   comment[commentLength]
byte   data[blockLength - header]
```

`FindBlock()` searches by name; blocks can be appended in any order, and an old reader simply
never asks for a new block. Values are written with `fwrite` of the raw POD — **host-endian**,
no byte swapping. Android/ARM64 is little-endian, so the same bytes are compatible with the
x86-64 build. A big-endian port would need to swap every field, which is why the emulator core
itself is not big-endian-ready.

### 4.2 What old saves get

| Missing block | Fallback |
| --- | --- |
| `53C810 PCI` | `baseAddress = 0xC0` (Step 1.x); `Ctx.halt` remains whatever the `53C810` block restored |
| `SimNetBoard` | `SynthesizeReadyState()` builds the single-machine ready state |
| `DSB1/DSB2 MPEG` | legacy `usingMPEG*` fields; may stop until the next game command |
| `WheelBoardServo` | `0x7F` (the old default) |

Do **not** change the `SUPERMODEL_VERSION` string for these additions: it is written into the
save header and is the compatibility contract. Add blocks, do not renumber or resize existing
ones.

### 4.3 `.zst` is only a container

The Windows build stores the raw block file inside a `.zst<slot>` container (`zip` + `core.zst`,
optional thumbnail). The emulator unpacks it to `.vktmp.st` and loads that. The Android port can
use its own container — the portable payload is the **raw block file**. If the Android save
manager reads/writes `.zst`, keep the `core.zst` bytes untouched.

### 4.4 Deferred load and the 172-frame guard

`Src/OSD/SDL/Main.cpp`, `Supermodel()`:

```cpp
constexpr double kDeferredLoadSeconds = 3.0;          // 172 frames at 57.524 Hz
double deferredLoadSeconds = kDeferredLoadSeconds;
if (const char *delay = std::getenv("SM3D_DEFER_LOAD_SECONDS"))
  deferredLoadSeconds = std::atof(delay);             // 0 = apply immediately
const uint64_t deferredLoadFrames =
  (uint64_t)(deferredLoadSeconds * kModel3RefreshHz);
```

The wait is counted in **emulated frames**, not wall clock, so a machine that cannot keep up does
not shorten the boot the state is waiting for. The start-up artwork covers the boot on Windows
(`ShowStartupSplash(window, true)`).

With the fixes above, `SM3D_DEFER_LOAD_SECONDS=0` is sufficient for the tested games. The three
seconds remain as a safety net for states saved during a boot handshake or for games whose
devices are not covered yet. If you remove the default in a port, keep the environment override
and test the full game matrix first.

> `v0.9.9.6` ships with the three-second default. Switching it to 0 is tracked in
> `issues_waitlist.md` (2026-11-07) and waits for that matrix; the pending list there names the
> sets, the pass criteria and the files to update.

---

## 5. Verification

### 5.1 Immediate load

Command used for the screenshots (Windows):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Capture-Frame.ps1 `
  -Set scuddx -Slot 2 -Frame 300 -Ini "RefreshRateMode=0" `
  -Env "SM3D_DEFER_LOAD_SECONDS=0" `
  -Out sdl3_reports\2026-11-07-upstream-125\saves_fix_immediate.webp
```

Log:

```
Manage saves: loading deferred state '...scuddx.zst2'
  (PPC PC=FFF00100 LR=00000000 after 0 emulated frame(s)).
Loaded state from '.../.vktmp.st' (PPC PC=000046D0, LR=00004610).
```

The state is applied at frame 0, and the race is running:

- `sdl3_reports/2026-11-07-upstream-125/saves_fix_immediate.webp` (frame 300)
- `sdl3_reports/2026-11-07-upstream-125/saves_fix_immediate_f600.webp` (frame 600)

The car position and speed change between the two frames, so the simulation is advancing rather
than sitting in a wait loop. The previous A/B also measured VF3TB at 0.3 s of game time in 8 s
(immediate, before the fix) versus 7.7 s (three seconds in); after the fix the immediate load
runs normally.

### 5.2 Other games

The same `SM3D_DEFER_LOAD_SECONDS=0` run was verified with:

- `scuddx` `zst2` (in-race save)
- `vf3tb` `zst1` (normal match; timer advances)
- `daytona2` `zst1` (normal race)
- a newly written `scuddx` `zst20` containing the `53C810 PCI` block

### 5.3 What to look for in a port

- `SM3D_LOAD_DEBUG=1` equivalent: a `save`/`after-load` dump with `scsi halt`, `baseAddress`,
  net state/segment/status, PPC PC/LR.
- A state loaded at frame 0 should reach the in-race PC (not the boot ROM) and keep advancing.
- No “network checking” reboot, no “Please wait” hang, no music-without-effects.
- The same state file loaded twice must produce the same `PPC PC/LR` and the same screenshot at
  the same emulated frame.

---

## 6. Android porting checklist

The Android build uses the same emulation core; the cleanest port is to take the source files
above unchanged. If the save manager or thread model differs, check each item:

1. **Save/load both blocks.** `53C810 PCI` and `SimNetBoard` must be written and read. If the
   Android save manager re-serializes the state, it must preserve unknown blocks byte-for-byte.
2. **Do not save pointers.** Save `baseAddress` (integer), `halt` (byte), the net-board fields
   and the two buffers. Re-derive `CommRAM` / `externalCommRAM` after load.
3. **Load order.** Restore the devices the CPU reads first, then `ppc_load_state()`, then
   `IRQ.RefreshInterruptLine()`, then the sound board (with `SCSP_RefreshIRQ()`), then the network
   board. See §3.7.
4. **Thread quiescence.** Pause the emulation/GPU/audio worker threads before loading and clear
   their “frame done” flags; reset interpolation and the scene snapshot; resume afterwards. On
   Android a single-threaded scheduler may not have those flags, but the equivalent “in-flight
   frame” state must still be discarded.
5. **JIT / fast memory.** If the Android JIT caches translated blocks or direct-memory windows,
   invalidate them on load, and call the CROM-bank publish hook after reading `cromBankReg`.
6. **Audio.** Call `SCSP_RefreshIRQ()` after restoring the SCSP registers. Restore the DSB MPEG
   active window from the companion block; keep the legacy fallback for old saves. If the Android
   audio callback holds a resampler/ring buffer, reset its history on load.
7. **Network board.** If Android uses the same `CSimNetBoard`, port the block and the
   `SynthesizeReadyState()` / 256→848 repair. If it uses a stub, implement the same minimum ready
   state or the netboard games will not boot past self-test.
8. **Endianness / integer sizes.** ARM64 is little-endian; `UINT32`/`UINT8` are fixed sizes. Do not
   write `size_t`, `long`, pointers or `std::string` into a block.
9. **Save slot / container.** `.zst` is optional. Keep the raw block payload compatible; a
   `.zst` file from Windows can be unpacked and loaded on Android if the core version matches.
10. **Test matrix.** At minimum `scuddx`, `vf3tb`, `daytona2`, `srally2`; old save (no new blocks)
    and new save; immediate load (`0 s`) and deferred load (`3 s`); in-race saves only. Avoid
    saving during load/transition screens — the game may be mid-handshake and the state can
    restore into a wait loop.
11. **Do not bump `SUPERMODEL_VERSION`.** The new blocks are additive; changing the version
    invalidates every existing save. If the Android port needs a different format, version it in
    the container, not in the core string.
12. **Logging.** Port the `SM3D_LOAD_DEBUG` dump (or an equivalent) so a user can report
    `PPC PC/LR`, net state and segment size after a load. This is what distinguishes “the state
    did not apply” from “the game is waiting on a device”.

---

## 7. Common mistakes

- **Removing the three-second wait without the 53C810/network fixes.** The state is applied to a
  machine with no PCI base address and no network state → slow motion, reboot or hang.
- **Appending fields to an existing block.** Old saves become unreadable. Add a new block and
  keep a fallback path.
- **Forgetting `IRQ.RefreshInterruptLine()`.** The CPU sees the controller status but never the
  interrupt line; the game spins in a wait loop.
- **Forgetting `SCSP_RefreshIRQ()`.** Music but no sound effects.
- **Saving the DSB register shadows instead of the active MPEG window.** Scud Race’s
  `0x80008000` sentinel makes the decoder restore an invalid range or stop the BGM.
- **Not clearing worker-done flags.** The next `EndSimulationFrame()` skips its wait and the
  renderer can read culling RAM while the PPC thread writes it.
- **Assuming `.zst` is the state.** It is a container; the portability boundary is the block file.
- **Saving in a load/transition screen.** Some games wait on a handshake across frames; a state
  captured there can restore into the wait loop. Save on the track or in a stable menu.
- **Changing `SUPERMODEL_VERSION`.** It is written into the save and is the compatibility
  contract; do not touch it for additive blocks.

---

## 8. File map

| File | What to port |
| --- | --- |
| `Src/Model3/53C810.cpp` | `SaveState()` / `LoadState()`, `53C810 PCI` block, `0xC0` fallback |
| `Src/Model3/Model3.cpp` | `SaveState()` / `LoadState()` order, worker flags, `IRQ.RefreshInterruptLine()` |
| `Src/Network/SimNetBoard.cpp` | `SaveState()` / `LoadState()`, `SynthesizeReadyState()`, 256→848 repair |
| `Src/Model3/SoundBoard.cpp` | `LoadState()` → `SCSP_RefreshIRQ()` |
| `Src/Model3/DSB.cpp` | `SaveMpegActiveWindow()` / `LoadMpegActiveWindow()`, `MPG2` block |
| `Src/Model3/IRQ.cpp` | `RefreshInterruptLine()` |
| `Src/OSD/SDL/Main.cpp` | deferred load (`kDeferredLoadSeconds`, `SM3D_DEFER_LOAD_SECONDS`), splash coverage |
| `Src/BlockFile.cpp/.h` | block container format; little-endian raw POD |

---

## 9. Regression commands

```
# Immediate load, in-race save, capture at two frames
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Capture-Frame.ps1 `
  -Set scuddx -Slot 2 -Frame 300 -Ini "RefreshRateMode=0" `
  -Env "SM3D_DEFER_LOAD_SECONDS=0" -Out ...\saves_fix_immediate.webp

powershell ... -Frame 600 ... -Out ...\saves_fix_immediate_f600.webp

# Same with the default three-second guard (A/B)
powershell ... -Frame 300 -Ini "RefreshRateMode=0" -Out ...\saves_fix_deferred.webp

# Save a new state containing the new blocks, then load it immediately
# (SM3D_SAVE_STATE_FRAME / SM3D_SAVE_STATE_SLOT, Vulkan build)
```

Check `Data/Model3Vulkan.log` for:

```
Manage saves: loading deferred state '...' (PPC PC=... after 0 emulated frame(s)).
Loaded state from '...' (PPC PC=..., LR=...).
```

and `SM3D_LOAD_DEBUG=1` for the `save` / `after-load` snapshot.

---

*This document describes the Windows/Vulkan implementation. The fixes are in the emulation core
and are not renderer-specific; the Android port can reuse them with the ordering and threading
notes above.*
