/**
 ** Model 3-Vulkan
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan.
 **
 ** Model 3-Vulkan is free software: you can redistribute it and/or modify it
 ** under the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Model 3-Vulkan is distributed in the hope that it will be useful, but
 ** WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 ** or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 ** for more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Model 3-Vulkan.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * ppc_jit_x64.inl
 *
 * Hybrid baseline JIT for the PowerPC 603e on x86-64 (Windows and Linux).
 *
 * Included from ppc.cpp after ppc603.c/ppc_ops.c, so it sees the interpreter's
 * static state directly: PPC_REGS ppc, the optable* handler tables, the fast
 * memory pointers and the READ8/16/32/WRITE8/16/32 helpers. Nothing is
 * duplicated, which is what keeps the two execution paths bit for bit
 * compatible: a JIT block that cannot be translated natively calls the very
 * same C handler the interpreter would have called.
 *
 * Design
 * ------
 *
 * A block is a straight run of up to 64 PowerPC instructions ending at the
 * first instruction that can change control flow or system state (a branch, or
 * one of the privileged/synchronising instructions listed in IsTerminator).
 * Each block is an ordinary System V / Microsoft x64 function, `void f(void)`,
 * so the dispatcher can call it through a pointer and the compiler's own stack
 * frame stays valid.
 *
 * Fixed register assignment inside a block. All five are callee-saved under
 * both ABIs and none of them is an argument register under either:
 *
 *   rbx  &ppc                (PPC_REGS *)
 *   r12  FastRam             base of the 8 MB of main RAM for loads
 *   r13  FastCrom            fixed CROM window, 0xFF800000
 *   r14  FastCromBank        current banked CROM window, 0xFF000000
 *   r15  FastRamWrite        same buffer as r12, for stores
 *
 * Scratch, named after the AArch64 port this was translated from so the two
 * files can be read side by side:
 *
 *   ecx  effective address      (AArch64 w9)
 *   edx  temporary A            (w10)
 *   r8d  temporary B            (w11)
 *   r9d  immediate / destination(w12)
 *   eax  value / result         (w13)
 *   eax, ecx, edx, r8, r9, r10, r11 are caller-saved and die across a CALL.
 *
 * Floating point scratch is xmm0 only. Under the Microsoft x64 ABI xmm6..xmm15
 * are callee-saved, so a block that touched one without saving it would corrupt
 * the host compiler's own state; the emitter refuses any other register.
 *
 * Nothing is kept in registers across the PowerPC instruction stream that
 * would have to be written back: every guest register lives in PPC_REGS and is
 * read and written with a single load or store. That is what makes the two
 * execution paths bit for bit compatible - a block that calls a C handler does
 * not have to marshal anything.
 *
 * Instruction count, decrementer and idle loops
 * ---------------------------------------------
 *
 * ppc.icount is only updated at a block exit and immediately before a C helper
 * runs, so that handlers reading the timebase or the decrementer see the right
 * cycle. The amount is a compile time constant: the distance from a helper call
 * to the previous one is known while the block is built, so no instruction
 * counter is kept at run time.
 *
 * The interpreter fires the decrementer interrupt when ppc.icount is decremented
 * to exactly ppc.dec_trigger_cycle. A block that spans that value would skip the
 * comparison, so the dispatcher refuses to run a block whose length would cross
 * the trigger and single-steps the interpreter through those few instructions
 * instead. That keeps the interrupt on exactly the same cycle while leaving the
 * generated code free of per-instruction clock checks.
 *
 * The interpreter's idle (wait) loop skip is shared rather than reproduced: a
 * block that ends in a backward branch feeds the same ppc_idle_step() state
 * machine the interpreter loop uses, so a game polling a device flag still gets
 * its remaining slice skipped instead of spinning natively.
 *
 * Invalidation
 * ------------
 *
 * Only two regions are executable on the Model 3 - RAM below 8 MB and the fixed
 * CROM window - so the block cache has exactly two direct mapped tables. CROM
 * is read only and therefore never needs invalidating. RAM blocks are dropped
 * when: an icbi executes, a store emitted by generated code lands in a page that
 * contains translated code (blocks are only dropped when the page really is
 * marked, which keeps the common case at four instructions per store), the
 * state is reset or loaded, or the code cache fills up.
 *
 * A store performed inside a C handler (stmw, stwcx., stswx, dcbz, an unaligned
 * access...) cannot report where it wrote, so those do not mark pages. The same
 * is true of a store the interpreter runs while the dispatcher steps across the
 * decrementer trigger. On this hardware that is safe in practice - the boot
 * loader copies the program into RAM long before any of it is translated, and
 * the runtime writes that follow are data - but a game that patched its own code
 * through one of those instructions would need the page tracking extended to the
 * bus write path.
 *
 * Verification
 * ------------
 *
 * SM3D_PPC_JIT_VERIFY=1 compiles and runs exactly the same code but brackets
 * every block with a full register state comparison against the interpreter
 * running the same instructions. Stores are logged first so that guest memory
 * can be rolled back before the reference run. See VerifyBlock().
 */

#if defined(PPC_JIT_X64)

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "X64Emitter.h"
#include "PPCDisasm.h"

#if defined(_MSC_VER)
#define PPC_JIT_NOINLINE	__declspec(noinline)
#elif defined(__GNUC__)
#define PPC_JIT_NOINLINE	__attribute__((noinline))
#else
#define PPC_JIT_NOINLINE
#endif

namespace PpcJit
{

using namespace X64;

enum
{
	kMaxBlockInsns	= 64,					/* instructions per block                 */
	kCodeCacheSize	= 16u * 1024u * 1024u,	/* machine code pool                      */
	kScratchBytes	= 32768,				/* per-block emission buffer              */
	kRamTableSets	= 8192,					/* RAM block index, kWays entries per set */
	kCromTableSets	= 2048,					/* fixed CROM block index                 */
	kWays			= 4,					/* block cache associativity              */
	kMaxSavedPages	= 8						/* RAM pages saved per block while verifying */
};

/*
 * Frame layout of a translated block, in bytes (see EmitPrologue).
 *
 * [rsp +  0 .. 31] is the Microsoft x64 shadow space every CALL needs; our own
 * cross-call scratch therefore starts at 32 and must never overlap it.
 */
enum
{
	kFrameSize		= 48,
	kFrameScratchA	= 32,					/* effective address across a bus call    */
	kFrameScratchB	= 40					/* stored value across a bus call         */
};

/*
 * The System V and Microsoft ABIs disagree on which registers carry arguments,
 * and this is the only place that difference is visible.
 */
enum
{
#if defined(_WIN32)
	kArg0			= RCX,
	kArg1			= RDX,
	kArg2			= R8
#else
	kArg0			= RDI,
	kArg1			= RSI,
	kArg2			= RDX
#endif
};

constexpr UINT32 kRamSize		= 0x00800000;	/* must match kFastRamSize in ppc.cpp */
constexpr UINT32 kCromBase		= 0xFF800000;
constexpr UINT32 kCodePageShift	= 12;			/* 4 KB pages are tracked for SMC     */
constexpr UINT32 kCodePageCount	= kRamSize >> kCodePageShift;

/* Block::flags */
enum
{
	kFlagBackwardBranch	= 1,	/* last instruction is a backward primary 16/18 branch */
	kFlagUnverifiable	= 2		/* writes memory through a C helper, see JitSavePage */
};

/*
 * The four pointers the generated code pins. Kept in one struct so that the
 * block prologue can load all of them from a single base.
 * The order matters: it is what EmitReloadPins() encodes.
 */
struct CFastPins
{
	const UINT8	*ram;
	UINT8		*ramWrite;
	const UINT8	*crom;
	const UINT8	*cromBank;
};

static_assert(offsetof(CFastPins, ramWrite) == 8, "CFastPins layout is encoded in the generated code");
static_assert(offsetof(CFastPins, crom) == 16, "CFastPins layout is encoded in the generated code");
static_assert(offsetof(CFastPins, cromBank) == 24, "CFastPins layout is encoded in the generated code");

struct CBlock
{
	UINT32	pc;				/* guest address of the first instruction   */
	UINT32	gen;				/* table generation this entry belongs to   */
	UINT32	flags;
	UINT32	branchPc;			/* address of the terminating branch        */
	UINT64	execs;				/* executions since the last report         */
	UINT16	insnCount;
	UINT16	nativeCount;
	UINT16	helperCount;
	UINT16	codeBytes;
	void	(*fn)(void);
};

struct CState
{
	/*
	 * codePage must stay first: the generated store path indexes it through a
	 * pointer to this struct.
	 */
	UINT8		codePage[kCodePageCount];

	UINT8		*code;
	size_t		codeSize;
	size_t		codeUsed;
	bool		codeWritable;		/* pool is RWX; otherwise it is toggled  */
	bool		enabled;
	bool		verify;
	bool		ready;
	bool		initialised;

	UINT32		ramGen;
	UINT32		cromGen;
	UINT32		selfModified;
	UINT32		busTouched;			/* a helper reached device space during this block */

	CBlock		ramTable[kRamTableSets * kWays];
	CBlock		cromTable[kCromTableSets * kWays];
	UINT8		victim[kRamTableSets + kCromTableSets];

	const UINT8	*lastRam;

	/* Statistics, reported through LogStats(). */
	UINT64		blocksCompiled;
	UINT64		blockExecs;
	UINT64		helperCalls;
	UINT64		helperControlChanges;
	UINT64		compileFailures;
	UINT32		compileFailurePc;
	UINT64		shortBlockBails;
	UINT64		smcFlushes;
	UINT64		verifyBlocks;
	UINT64		verifyFailures;
	UINT64		exceptionEntries;
	UINT64		idleSkips;
	UINT32		lastExceptionVector;
	UINT32		lastExceptionSrr0;
	UINT32		lastExceptionSrr1;
	UINT32		lastExceptionPc;

	UINT8		scratch[kScratchBytes];
};

static CState s_jit;
static CFastPins s_pins;

/* Set by ppc_set_jit_enabled() before the machine is built; -1 means "not chosen yet". */
static int s_enableRequest = -1;

/******************************************************************************
 Helpers shared with the generated code
******************************************************************************/

/*
 * Points ppc.op at the host word containing pc, exactly as ppc_change_pc()
 * would. The fast path is the one that matters: a block never leaves its fetch
 * region, so the loop over the region table is only taken after a state change.
 */
static inline void SyncOp(UINT32 pc)
{
	const UINT32 offset = pc - ppc.cur_fetch.start;
	if (ppc.cur_fetch.ptr != NULL && offset <= ppc.cur_fetch.end - ppc.cur_fetch.start)
		ppc.op = &ppc.cur_fetch.ptr[offset >> 2];
	else
		ppc_change_pc(pc);
}

/*
 * Runs one PowerPC instruction through the interpreter's handler tables.
 *
 * Called from generated code as JitHelper(handler, opcode | pc << 32, consumed),
 * where consumed is the number of instructions the block has executed so far.
 * The cycle count is brought up to date first because handlers that read the
 * timebase or the decrementer (mfspr/mftb/mtspr) derive it from ppc.icount.
 *
 * Returns non-zero when the block may continue. A handler reaches the bus, and
 * the bus can assert an interrupt - a write to the tile generator or the Real3D
 * is enough - which takes the exception right there and leaves ppc.npc pointing
 * at the vector. The interpreter simply picks that up on its next iteration; a
 * translated block has to stop, or its own exit would overwrite npc with the
 * fall-through address and the interrupt would be lost.
 */
static PPC_JIT_NOINLINE int JitHelper(void (*fn)(UINT32), UINT64 opPc, UINT32 consumed)
{
	const UINT32 opcode = (UINT32)opPc;
	const UINT32 pc = (UINT32)(opPc >> 32);

	ppc.icount -= (INT32)consumed;
	ppc.pc = pc;
	ppc.npc = pc + 4;
	SyncOp(pc);

	fn(opcode);

	s_jit.helperCalls++;

	if (ppc.fatalError)
		return 0;
	if (ppc.npc != pc + 4)
	{
		s_jit.helperControlChanges++;
		return 0;
	}
	return 1;
}

/*
 * icbi. The interpreter's handler is a no-op (it refetches every instruction, so
 * it never needs one), but a block compiled from the bytes being invalidated
 * must go.
 */
static PPC_JIT_NOINLINE int JitHelperIcbi(void (*fn)(UINT32), UINT64 opPc, UINT32 consumed)
{
	const int result = JitHelper(fn, opPc, consumed);
	s_jit.selfModified = 1;
	return result;
}

/*
 * The interpreter's own handler for an opcode. The JIT calls it directly, which
 * is what keeps a C fallback as cheap as one instruction of the interpreter's
 * own dispatch loop.
 */
static void *HandlerFor(UINT32 opcode)
{
	const UINT32 prim = opcode >> 26;

	if (prim == 19)
		return (void *)(uintptr_t)optable19[(opcode >> 1) & 0x3ff];
	if (prim == 31)
		return (void *)(uintptr_t)optable31[(opcode >> 1) & 0x3ff];
	if (prim == 59)
		return (void *)(uintptr_t)optable59[(opcode >> 1) & 0x3ff];
	if (prim == 63)
		return (void *)(uintptr_t)optable63[(opcode >> 1) & 0x3ff];
	return (void *)(uintptr_t)optable[prim];
}

/*
 * Slow (bus) memory paths. Each is literally the interpreter's own helper, so
 * device space, unaligned accesses and every region outside the two fast
 * windows behave identically on both paths.
 *
 * A read or write that lands in device space (anything above 8 MB that is not
 * one of the CROM windows) is not repeatable: the JTAG port, the VDP status
 * registers and the sound board all report something different the second time
 * they are read. The lockstep verifier cannot run such a block twice, so the
 * helpers say so and the dispatcher skips the comparison for that block.
 */
static inline bool IsDeviceAddress(UINT32 address)
{
	return address >= kRamSize && (address >> 24) != 0xFF;
}

static PPC_JIT_NOINLINE UINT32 JitRead8(UINT32 address)
{
	if (IsDeviceAddress(address)) s_jit.busTouched = 1;
	return READ8(address);
}

static PPC_JIT_NOINLINE UINT32 JitRead16(UINT32 address)
{
	if (IsDeviceAddress(address)) s_jit.busTouched = 1;
	return READ16(address);
}

static PPC_JIT_NOINLINE UINT32 JitRead32(UINT32 address)
{
	if (IsDeviceAddress(address)) s_jit.busTouched = 1;
	return READ32(address);
}

static PPC_JIT_NOINLINE UINT32 JitRead16Signed(UINT32 address)
{
	if (IsDeviceAddress(address)) s_jit.busTouched = 1;
	return (UINT32)(INT32)(INT16)READ16(address);
}

static PPC_JIT_NOINLINE void JitWrite8(UINT32 address, UINT32 data)
{
	if (IsDeviceAddress(address)) s_jit.busTouched = 1;
	WRITE8(address, (UINT8)data);
}

static PPC_JIT_NOINLINE void JitWrite16(UINT32 address, UINT32 data)
{
	if (IsDeviceAddress(address)) s_jit.busTouched = 1;
	WRITE16(address, (UINT16)data);
}

static PPC_JIT_NOINLINE void JitWrite32(UINT32 address, UINT32 data)
{
	if (IsDeviceAddress(address)) s_jit.busTouched = 1;
	WRITE32(address, data);
}

/*
 * Verifier support: the contents of every RAM page a block stores into are
 * saved before the first store lands, and put back before the reference run.
 *
 * A whole 8 MB snapshot per block would be far too slow, and intercepting the
 * interpreter's stores would mean touching WRITE8/16/32, which is the hot path
 * this JIT exists to speed up. Page granularity is exact, costs about a
 * microsecond per page, and needs no cooperation from the C handlers.
 *
 * Only stores the generated code performs itself are covered. A block that
 * writes memory through a C handler (stwbrx, stwcx., the floating point and
 * string forms) is marked unverifiable and run without a comparison - those
 * handlers are the interpreter's own code, so there is nothing there for the
 * translation to get wrong.
 */
static UINT8 s_savedPages[kMaxSavedPages][4096];
static UINT32 s_savedPageAddress[kMaxSavedPages];
static int s_savedPageCount;
static bool s_savedPageOverflow;

static PPC_JIT_NOINLINE void JitSavePage(UINT32 address, UINT32 size)
{
	UINT32 first = address & ~(UINT32)0xFFF;
	UINT32 last = (address + size - 1) & ~(UINT32)0xFFF;
	for (UINT32 page = first; page <= last; page += 0x1000)
	{
		if (page >= kRamSize)
			continue;
		bool saved = false;
		for (int i = 0; i < s_savedPageCount && !saved; i++)
			saved = (s_savedPageAddress[i] == page);
		if (saved)
			continue;
		if (s_savedPageCount >= kMaxSavedPages)
		{
			s_savedPageOverflow = true;
			continue;
		}
		s_savedPageAddress[s_savedPageCount] = page;
		memcpy(s_savedPages[s_savedPageCount], FastMemory.ram + page, 4096);
		s_savedPageCount++;
	}
}

static void RestoreSavedPages(void)
{
	for (int i = 0; i < s_savedPageCount; i++)
		memcpy(FastMemory.ramWrite + s_savedPageAddress[i], s_savedPages[i], 4096);
	s_savedPageCount = 0;
}

/* -------------------------------------------------------------------------- */
/* Platform glue for the code pool                                            */
/* -------------------------------------------------------------------------- */

/*
 * Declared here because the block cache below publishes blocks through them;
 * the definitions sit with the rest of the platform code further down.
 *
 * x86-64 has coherent instruction and data caches, so publishing a block needs
 * no per-line flush; the Windows call is kept because it is the documented way
 * to make freshly written code visible and costs one call per translation.
 */
static bool ProtectCode(void *code, size_t size, bool writable);
static void FlushCode(void *code, size_t size);
static void *AllocCode(size_t size, bool *writable);
static void FreeCode(void *code, size_t size);

/******************************************************************************
 Block builder
******************************************************************************/

enum
{
	MK_READ8		= 0,
	MK_READ16		= 1,
	MK_READ16S		= 2,
	MK_READ32		= 3,
	MK_WRITE8		= 4,
	MK_WRITE16		= 5,
	MK_WRITE32		= 6
};

/*
 * True for the instructions whose C handler can write guest memory. A block
 * containing one cannot be compared against the interpreter because the stores
 * it performs inside the handler cannot be undone (see JitSavePage).
 */
static bool HelperMayWriteMemory(UINT32 opcode)
{
	const UINT32 prim = opcode >> 26;

	if (prim >= 36 && prim <= 47)		/* stw stwu stb stbu sth sthu stmw */
		return true;
	if (prim >= 52 && prim <= 55)		/* stfs stfsu stfd stfdu */
		return true;

	if (prim == 31)
	{
		switch ((opcode >> 1) & 0x3ff)
		{
			case 150:	/* stwcx.   */
			case 438:	/* ecowx    */
			case 470:	/* dcbi     */
			case 661:	/* stswx    */
			case 662:	/* stwbrx   */
			case 663:	/* stfsx    */
			case 695:	/* stfsux   */
			case 725:	/* stswi    */
			case 727:	/* stfdx    */
			case 759:	/* stfdux   */
			case 918:	/* sthbrx   */
			case 983:	/* stfiwx   */
			case 1014:	/* dcbz     */
				return true;
			default:
				break;
		}
	}
	return false;
}

/*
 * True for the handlers that read the emulated cycle count. They reconstruct the
 * timebase and the decrementer from ppc.icount, so the JIT has to bring it up to
 * date before the call; everything else can be called without touching it.
 */
static bool HelperNeedsCycles(UINT32 opcode)
{
	if ((opcode >> 26) == 31)
	{
		switch ((opcode >> 1) & 0x3ff)
		{
			case 339:	return true;		/* mfspr (incl. DEC, TBL, TBU) */
			case 371:	return true;		/* mftb                        */
			case 467:	return true;		/* mtspr                       */
			default:		return false;
		}
	}
	return false;
}

struct CBuild
{
	CEmitter	e;
	UINT32		entryPc;
	int			count;
	int			index;				/* instruction being emitted            */
	int			syncedThrough;		/* instructions already subtracted from
								   ppc.icount by a helper call          */
	int			nativeCount;
	int			helperCount;
	size_t		exitCommon;
	size_t		restoreAt;
	struct CExitFix { size_t at; bool conditional; int executed; };
	CExitFix	exitFix[kMaxBlockInsns + 8];
	int			exitFixCount;
	struct CBailFix { size_t at; bool conditional; int pending; };
	CBailFix	bailFix[kMaxBlockInsns + 8];
	int			bailFixCount;
	bool		endedWithExit;
	bool		unverifiable;		/* stores through a C handler: cannot be compared */
	bool		hasStore;			/* contains an inline store (see SMC)     */
	UINT32		flags;
	UINT32		branchPc;
};

static void JumpToExit(CBuild &b)
{
	if (b.exitFixCount < (int)(sizeof(b.exitFix) / sizeof(b.exitFix[0])))
	{
		b.exitFix[b.exitFixCount].at = b.e.BEmit();
		b.exitFix[b.exitFixCount].conditional = false;
		b.exitFixCount++;
	}
	else
	{
		b.e.Fail();
	}
}

/*
 * Leaves the block after instruction "executed" without letting the fast exit
 * (or a chained jump) run: the C handler changed control flow or halted the
 * machine, so the dispatcher has to look at ppc.npc again.
 *
 * The cycle count differs from JumpToExit's: the instructions after the bail
 * point did not run, and a helper call has already subtracted everything before
 * it, so the stub subtracts exactly the remainder.
 */
static void JumpToBail(CBuild &b, int executed)
{
	if (b.bailFixCount < (int)(sizeof(b.bailFix) / sizeof(b.bailFix[0])))
	{
		b.bailFix[b.bailFixCount].at = b.e.BEmit();
		b.bailFix[b.bailFixCount].conditional = false;
		b.bailFix[b.bailFixCount].pending = executed - b.syncedThrough;
		b.bailFixCount++;
	}
	else
	{
		b.e.Fail();
	}
}

static void JumpToBailIfZero(CBuild &b, int reg, int executed)
{
	if (b.bailFixCount < (int)(sizeof(b.bailFix) / sizeof(b.bailFix[0])))
	{
		b.bailFix[b.bailFixCount].at = b.e.CbzEmit(reg);
		b.bailFix[b.bailFixCount].conditional = true;
		b.bailFix[b.bailFixCount].pending = executed - b.syncedThrough;
		b.bailFixCount++;
	}
	else
	{
		b.e.Fail();
	}
}

/*
 * Reloads the four pinned pointers from s_pins.
 *
 * Uses R10, never RAX: this runs immediately after a bus call, where RAX holds the
 * value a load just read (and, for a helper call, the return value the caller is
 * about to test). Clobbering it there silently replaced every device read with the
 * low half of &s_pins.
 */
static void EmitReloadPins(CBuild &b)
{
	CEmitter &e = b.e;
	e.MovImm64(R10, (UINT64)(uintptr_t)&s_pins);
	e.Load64(R12, R10, 0);		/* ram      */
	e.Load64(R15, R10, 8);		/* ramWrite */
	e.Load64(R13, R10, 16);		/* crom     */
	e.Load64(R14, R10, 24);		/* cromBank */
}

/*
 * Saves the callee-saved registers the pinned set uses and reserves the frame.
 *
 * On entry a CALL has already pushed the return address, so rsp % 16 == 8. The
 * five pushes bring it to 0, and the 48-byte frame keeps it there: every CALL a
 * block makes therefore sees a correctly aligned stack.
 *
 * The 48 bytes are the Microsoft x64 shadow space ([rsp + 0..31], required even
 * on Linux where it is simply unused) plus the two cross-call scratch slots.
 */
static void EmitPrologue(CBuild &b)
{
	CEmitter &e = b.e;

	e.Push(RBX);
	e.Push(R12);
	e.Push(R13);
	e.Push(R14);
	e.Push(R15);
	e.SubImm64(RSP, kFrameSize);

	e.MovImm64(RBX, (UINT64)(uintptr_t)&ppc);
	EmitReloadPins(b);
}

/*
 * The exit path of a translated block.
 *
 * Everything the block can leave through lands in exitCommon, which brings the
 * cycle count up to date and returns to the dispatcher.
 */
static void EmitEpilogue(CBuild &b)
{
	CEmitter &e = b.e;
	b.exitCommon = e.Here();

	/*
	 * ppc.icount -= everything not yet accounted for by a helper call. The
	 * remainder is a compile time constant: helpers subtract the instructions
	 * that precede them as they are called (see EmitHelperCall).
	 */
	const int pending = b.count - b.syncedThrough;
	e.SubMemImm32(RBX, (INT32)offsetof(PPC_REGS, icount), pending);

	b.restoreAt = e.Here();
	e.AddImm64(RSP, kFrameSize);
	e.Pop(R15);
	e.Pop(R14);
	e.Pop(R13);
	e.Pop(R12);
	e.Pop(RBX);
	e.Ret();

	for (int i = 0; i < b.exitFixCount; i++)
	{
		if (b.exitFix[i].conditional)
			e.ResolveCond(b.exitFix[i].at, b.exitCommon);
		else
			e.ResolveBranch(b.exitFix[i].at, b.exitCommon);
	}
	b.exitFixCount = 0;

	/*
	 * Leave-the-block stubs. They are out of line so the fast exit never pays
	 * for them, and each carries its own constant because the cycle count is
	 * only correct for the instructions that actually ran.
	 */
	for (int i = 0; i < b.bailFixCount; i++)
	{
		const size_t at = e.Here();
		if (b.bailFix[i].conditional)
			e.ResolveCond(b.bailFix[i].at, at);
		else
			e.ResolveBranch(b.bailFix[i].at, at);

		const int pending = b.bailFix[i].pending;
		if (pending > 0)
			e.SubMemImm32(RBX, (INT32)offsetof(PPC_REGS, icount), pending);
		e.BTo(b.restoreAt);
	}
	b.bailFixCount = 0;
}

/* -------------------------------------------------------------------------- */
/* Register access                                                            */
/* -------------------------------------------------------------------------- */

static inline void LdGpr(CBuild &b, int reg, int n)
{
	b.e.Load32(reg, RBX, (INT32)offsetof(PPC_REGS, r) + 4 * n);
}

static inline void StGpr(CBuild &b, int n, int reg)
{
	b.e.Store32(RBX, (INT32)offsetof(PPC_REGS, r) + 4 * n, reg);
}

/* Copies src into dst unless they are the same register, avoiding a dead MOV. */
static inline void MovReg32(CEmitter &e, int dst, int src)
{
	if (dst != src)
		e.Mov32(dst, src);
}

/*
 * ppc.cr[field] = (value < 0 ? 8 : value > 0 ? 4 : 2) | (xer >> 31)
 *
 * Value must be live in a 32-bit register; RDX and R8 are clobbered.
 */
static void EmitSetCRField(CBuild &b, int valueReg, int field)
{
	CEmitter &e = b.e;

	e.CmpImm32(valueReg, 0);
	e.MovImm32(RDX, 0x8);
	e.MovImm32(R8, 0x4);
	e.Cmovcc32(CC_G, RDX, R8);			/* GT ? 4 : 8 */
	e.MovImm32(R8, 0x2);
	e.Cmovcc32(CC_E, RDX, R8);			/* EQ ? 2 : previous */
	e.Load32(R8, RBX, (INT32)offsetof(PPC_REGS, xer));
	e.ShrImm32(R8, 31);					/* SO -> bit 0 */
	e.Or32(RDX, R8);
	e.Store8(RBX, (INT32)offsetof(PPC_REGS, cr) + field, RDX);
}

/* Same, with the condition flags already set by a compare. */
static void EmitCRFieldFromFlags(CBuild &b, int field, bool isSigned)
{
	CEmitter &e = b.e;

	e.MovImm32(RDX, 0x8);
	e.MovImm32(R8, 0x4);
	e.Cmovcc32(isSigned ? CC_G : CC_A, RDX, R8);
	e.MovImm32(R8, 0x2);
	e.Cmovcc32(CC_E, RDX, R8);
	e.Load32(R8, RBX, (INT32)offsetof(PPC_REGS, xer));
	e.ShrImm32(R8, 31);
	e.Or32(RDX, R8);
	e.Store8(RBX, (INT32)offsetof(PPC_REGS, cr) + field, RDX);
}

static inline void EmitSetCR0(CBuild &b, int valueReg)
{
	EmitSetCRField(b, valueReg, 0);
}

/*
 * Copies a hardware flag into XER[CA] (bit 29).
 *
 * The direction is the one thing that is not symmetric between the two
 * architectures. PowerPC defines CA as "carry out" for additions and as "no
 * borrow" for subtractions. x86's CF is "carry out" for ADD and "borrow" for
 * SUB, so an addition takes SETC while a subtraction takes SETAE - using SETC
 * after a SUB would invert CA and every subfc/subfic in the machine.
 */
static void EmitXerCAFromFlags(CBuild &b, int cond)
{
	CEmitter &e = b.e;

	e.Cset32(cond, RDX);				/* 0 or 1, without touching the flags */
	e.Load32(R8, RBX, (INT32)offsetof(PPC_REGS, xer));
	e.AndImm32(R8, ~0x20000000u);
	e.ShlImm32(RDX, 29);
	e.Or32(R8, RDX);
	e.Store32(RBX, (INT32)offsetof(PPC_REGS, xer), R8);
}

/*
 * Emits a call into a C helper.
 *
 *   Windows:   rcx = opcode | pc << 32   rdx = handler   r8 = consumed
 *   System V:  rdi = ...                 rsi = ...       rdx = ...
 *
 * The bookkeeper subtracts "consumed" from ppc.icount before running the
 * handler, which is what lets a handler that reads the timebase or the
 * decrementer (mfspr/mftb/mtspr) see the right cycle. It is a compile time
 * constant: the number of instructions between this call and the previous one
 * is known while the block is being built, so nothing is counted at run time.
 *
 * The return value says whether the block may keep going (see JitHelper). The
 * pins are reloaded afterwards because a helper can reach the bus, and a CROM
 * bank switch moves FastCromBank.
 */
static void EmitHelperCall(CBuild &b, UINT32 opcode, UINT32 pc, void *bookkeeper, bool terminator, bool raw = false)
{
	CEmitter &e = b.e;
	void *handler = HandlerFor(opcode);

	const int consumed = b.index - b.syncedThrough;

	if (raw && !terminator)
	{
		/*
		 * Fast path for the handlers that read neither the timebase nor the
		 * decrementer and never leave the block: the call needs nothing but the
		 * instruction address, because an exception taken inside the handler
		 * saves ppc.npc in SRR0. Skipping the cycle bookkeeping is not a
		 * shortcut in the accounting - syncedThrough simply stays where it was,
		 * so the block exit subtracts this instruction along with the rest.
		 *
		 * This is what makes the C fallbacks affordable at all: the interpreter
		 * reaches a handler with one indexed call, and anything more here would
		 * make a translated instruction slower than an interpreted one.
		 */
		e.MovImm32(RDX, pc);
		e.Store32(RBX, (INT32)offsetof(PPC_REGS, pc), RDX);
		e.MovImm32(RDX, pc + 4);
		e.Store32(RBX, (INT32)offsetof(PPC_REGS, npc), RDX);
		e.MovImm32(kArg0, opcode);
		e.MovImm64(RAX, (UINT64)(uintptr_t)handler);
		e.CallReg(RAX);
		if (HelperMayWriteMemory(opcode))
			EmitReloadPins(b);

		e.Load32(RDX, RBX, (INT32)offsetof(PPC_REGS, npc));
		e.MovImm32(R8, pc + 4);
		e.Cmp32(RDX, R8);
		const size_t continueAt = e.BCondEmit(CC_E);
		JumpToBail(b, b.index + 1);
		e.ResolveCond(continueAt, e.Here());

		b.helperCount++;
		if (HelperMayWriteMemory(opcode))
			b.unverifiable = true;
		return;
	}

	b.syncedThrough = b.index;

	/* JitHelper(handler, opcode | pc << 32, consumed) */
	e.MovImm64(kArg0, (UINT64)(uintptr_t)handler);
	e.MovImm64(kArg1, ((UINT64)pc << 32) | (UINT64)opcode);
	e.MovImm32(kArg2, (UINT32)consumed);
	e.MovImm64(RAX, (UINT64)(uintptr_t)bookkeeper);
	e.CallReg(RAX);
	if (HelperMayWriteMemory(opcode))
		EmitReloadPins(b);

	if (!terminator)
		JumpToBailIfZero(b, RAX, b.index + 1);
	else
		JumpToExit(b);

	b.helperCount++;
	if (terminator)
		b.endedWithExit = true;
	if (HelperMayWriteMemory(opcode))
		b.unverifiable = true;
}

/* -------------------------------------------------------------------------- */
/* Memory access                                                              */
/* -------------------------------------------------------------------------- */

static void *SlowWriteFn(int kind)
{
	switch (kind)
	{
		case MK_WRITE8:		return (void *)(uintptr_t)&JitWrite8;
		case MK_WRITE16:	return (void *)(uintptr_t)&JitWrite16;
		default:			return (void *)(uintptr_t)&JitWrite32;
	}
}

static void *SlowReadFn(int kind)
{
	switch (kind)
	{
		case MK_READ8:		return (void *)(uintptr_t)&JitRead8;
		case MK_READ16:		return (void *)(uintptr_t)&JitRead16;
		case MK_READ16S:	return (void *)(uintptr_t)&JitRead16Signed;
		default:			return (void *)(uintptr_t)&JitRead32;
	}
}

static UINT32 storeBytes(int kind)
{
	switch (kind)
	{
		case MK_WRITE8:		return 1;
		case MK_WRITE16:	return 2;
		default:			return 4;
	}
}

/*
 * Emits one load or store.
 *
 * On entry the effective address is in ECX and, for stores, the value is in EAX.
 * A load leaves its result in EAX and the effective address untouched in ECX,
 * so the updating ("u") forms can write the base register back afterwards.
 *
 * The fast path is the interpreter's fast path: 8 MB of RAM addressed through
 * r12/r15 and the two CROM windows at (address >> 24) == 0xFF. Everything else
 * - device space, odd addresses and any address outside those regions - falls
 * through to the same bus call the interpreter makes.
 */
static void EmitMemAccess(CBuild &b, int kind)
{
	CEmitter &e = b.e;
	const bool store = (kind >= MK_WRITE8);
	const int ramBase = store ? R15 : R12;	/* FastRamWrite : FastRam */
	if (store)
		b.hasStore = true;
	const int alignMask = (kind == MK_READ8 || kind == MK_WRITE8) ? 0
						: (kind == MK_READ16 || kind == MK_READ16S || kind == MK_WRITE16) ? 1
						: 3;
	size_t toSlow[2];
	int nSlow = 0;
	size_t toDoneCrom = (size_t)-1;

	if (alignMask != 0)
	{
		e.TestImm32(RCX, (UINT32)alignMask);
		toSlow[nSlow++] = e.BCondEmit(CC_NE);
	}

	e.CmpImm32(RCX, (INT32)kRamSize);
	size_t toHigh = e.BCondEmit(CC_AE);

	/* --- RAM ------------------------------------------------------------ */
	/*
	 * Both directions have a fast path here, exactly as READ8/16/32 and
	 * WRITE8/16/32 do. Only reads have one in the CROM windows: the
	 * interpreter's WRITE* helpers send every store at or above 8 MB to the
	 * bus, and a store to ROM is a bus transaction, not a memory write.
	 */
	if (store)
	{
		toSlow[nSlow++] = toHigh;

		/*
		 * Self-modifying code guard: if the page holds generated code, ask the
		 * dispatcher to drop the RAM blocks once this block returns.
		 */
		e.Mov32(RDX, RCX);
		e.ShrImm32(RDX, (int)kCodePageShift);
		e.MovImm64(R8, (UINT64)(uintptr_t)&s_jit);
		e.Load8UIdx(RDX, R8, RDX);
		const size_t noSmc = e.CbzEmit(RDX);
		e.StoreImm8(R8, (INT32)offsetof(CState, selfModified), 1);
		e.ResolveCond(noSmc, e.Here());

		/* Verifying: keep a copy of the page this store is about to change. */
		if (s_jit.verify)
		{
			e.Store32(RSP, kFrameScratchA, RCX);
			e.Store32(RSP, kFrameScratchB, RAX);
			MovReg32(e, kArg0, RCX);
			e.MovImm32(kArg1, storeBytes(kind));
			e.MovImm64(RAX, (UINT64)(uintptr_t)&JitSavePage);
			e.CallReg(RAX);
			e.Load32(RAX, RSP, kFrameScratchB);
			e.Load32(RCX, RSP, kFrameScratchA);
		}
	}

	/*
	 * RAM is byte-swapped inside each 32-bit word (see READ8/READ16 in ppc.cpp):
	 * guest byte n of a word lives at offset n ^ 3. Word accesses need no
	 * adjustment; narrower ones do.
	 */
	int ramIndex = RCX;
	if (kind == MK_READ8 || kind == MK_WRITE8)
	{
		e.Mov32(R8, RCX);
		e.XorImm32(R8, 3);
		ramIndex = R8;
	}
	else if (kind != MK_READ32 && kind != MK_WRITE32)
	{
		e.Mov32(R8, RCX);
		e.XorImm32(R8, 2);
		ramIndex = R8;
	}

	switch (kind)
	{
		case MK_READ8:  	e.Load8UIdx(RAX, ramBase, ramIndex); break;
		case MK_READ16: 	e.Load16UIdx(RAX, ramBase, ramIndex); break;
		case MK_READ16S:	e.Load16SIdx(RAX, ramBase, ramIndex); break;
		case MK_READ32: 	e.Load32Idx(RAX, ramBase, ramIndex); break;
		case MK_WRITE8: 	e.Store8Idx(ramBase, ramIndex, RAX); break;
		case MK_WRITE16:	e.Store16Idx(ramBase, ramIndex, RAX); break;
		default:        	e.Store32Idx(ramBase, ramIndex, RAX); break;
	}

	const size_t toDoneRam = e.BEmit();

	/* --- CROM windows (reads only) --------------------------------------- */
	if (!store)
	{
		e.ResolveCond(toHigh, e.Here());
		e.Mov32(RDX, RCX);
		e.ShrImm32(RDX, 24);
		e.CmpImm32(RDX, 0xFF);
		toSlow[nSlow++] = e.BCondEmit(CC_NE);

		/* Bit 23 selects the window: set is the fixed one, clear the bank. */
		e.Mov64(RDX, R13);
		e.Mov64(R8, R14);
		e.TestImm32(RCX, 0x00800000u);
		e.Cmovcc64(CC_Z, RDX, R8);

		e.Mov32(R8, RCX);
		e.AndImm32(R8, 0x7FFFFF);			/* address & (kFastCromSize - 1) */
		if (kind == MK_READ8)
			e.XorImm32(R8, 3);
		else if (kind != MK_READ32)
			e.XorImm32(R8, 2);

		switch (kind)
		{
			case MK_READ8:  	e.Load8UIdx(RAX, RDX, R8); break;
			case MK_READ16: 	e.Load16UIdx(RAX, RDX, R8); break;
			case MK_READ16S:	e.Load16SIdx(RAX, RDX, R8); break;
			default:        	e.Load32Idx(RAX, RDX, R8); break;
		}

		toDoneCrom = e.BEmit();
	}

	/* --- bus ------------------------------------------------------------- */
	const size_t slowAt = e.Here();
	for (int i = 0; i < nSlow; i++)
		e.ResolveCond(toSlow[i], slowAt);

	/*
	 * The bus call needs ppc.pc/ppc.npc pointing at this instruction: a device
	 * write can assert an interrupt, and the exception saves pc+4 in SRR0. The
	 * pair is also what the check below compares, which is how the block notices
	 * that control flow changed under it.
	 */
	const UINT32 insnPc = b.entryPc + (UINT32)b.index * 4;
	e.Store32(RSP, kFrameScratchA, RCX);	/* the call clobbers the address */
	e.MovImm32(RDX, insnPc);
	e.Store32(RBX, (INT32)offsetof(PPC_REGS, pc), RDX);
	e.MovImm32(RDX, insnPc + 4);
	e.Store32(RBX, (INT32)offsetof(PPC_REGS, npc), RDX);
	MovReg32(e, kArg0, RCX);
	if (store)
	{
		MovReg32(e, kArg1, RAX);
		e.MovImm64(RAX, (UINT64)(uintptr_t)SlowWriteFn(kind));
	}
	else
	{
		e.MovImm64(RAX, (UINT64)(uintptr_t)SlowReadFn(kind));
	}
	e.CallReg(RAX);
	e.Load32(RCX, RSP, kFrameScratchA);
	EmitReloadPins(b);

	/* Leave the block if the bus took an exception and moved ppc.npc. */
	e.Load32(RDX, RBX, (INT32)offsetof(PPC_REGS, npc));
	e.MovImm32(R8, insnPc + 4);
	e.Cmp32(RDX, R8);
	const size_t continueAt = e.BCondEmit(CC_E);
	JumpToBail(b, b.index + 1);
	e.ResolveCond(continueAt, e.Here());

	const size_t toDoneSlow = e.BEmit();

	const size_t doneAt = e.Here();
	e.ResolveBranch(toDoneRam, doneAt);
	if (toDoneCrom != (size_t)-1)
		e.ResolveBranch(toDoneCrom, doneAt);
	e.ResolveBranch(toDoneSlow, doneAt);
}

/******************************************************************************
 Instruction emission
******************************************************************************/

static bool IsTerminator(UINT32 opcode)
{
	const UINT32 prim = opcode >> 26;

	if (prim == 16 || prim == 17 || prim == 18 || prim == 3)
		return true;

	if (prim == 19)
	{
		const UINT32 sub = (opcode >> 1) & 0x3ff;
		return sub == 16 || sub == 528 || sub == 50 || sub == 150;
	}

	if (prim == 31)
	{
		const UINT32 sub = (opcode >> 1) & 0x3ff;
		return sub == 146 || sub == 467 || sub == 982 || sub == 4;
	}

	return false;
}

/*
 * Effective address into ECX.
 *
 * indexForm: (RA ? r[RA] : 0) + r[RB]
 * otherwise: (RA ? r[RA] : 0) + displacement, except for the updating forms
 *            where the C helpers do not test RA either.
 */
static void EmitEffectiveAddress(CBuild &b, int ra, int rb, INT32 disp, bool indexForm, bool update)
{
	CEmitter &e = b.e;

	if (indexForm)
	{
		LdGpr(b, RCX, rb);
		if (ra != 0)
		{
			LdGpr(b, RDX, ra);
			e.Add32(RCX, RDX);
		}
		return;
	}

	if (ra != 0 || update)
	{
		LdGpr(b, RCX, ra);
		e.AddImm32(RCX, disp);
	}
	else
	{
		e.MovImm32(RCX, (UINT32)disp);
	}
}

static void EmitLoadStore(CBuild &b, UINT32 opcode, int kind, bool indexForm, bool update)
{
	const int ra = (int)((opcode >> 16) & 0x1f);
	const int rb = (int)((opcode >> 11) & 0x1f);
	const int rt = (int)((opcode >> 21) & 0x1f);
	const INT32 disp = (INT32)(INT16)(opcode & 0xffff);
	const bool store = (kind >= MK_WRITE8);

	if (store)
		LdGpr(b, RAX, rt);				/* rS is the RT field of a store */

	EmitEffectiveAddress(b, ra, rb, disp, indexForm, update);
	EmitMemAccess(b, kind);

	if (!store)
		StGpr(b, rt, RAX);
	if (update)
		StGpr(b, ra, RCX);

	b.nativeCount++;
}

/*
 * lfs / lfsu / lfsx / lfsux / stfs / stfsu / stfsx / stfsux.
 *
 * Both directions are a 32-bit memory access plus one conversion: the guest
 * keeps a single in memory and works with doubles, and a single can be moved to
 * and from a double without touching FPSCR. The memory path is the same one the
 * integer loads use, so unaligned addresses, the CROM windows and device space
 * all behave exactly as they do in the interpreter.
 *
 * Uses caller-saved xmm0 only: under the Microsoft x64 ABI xmm6..xmm15 are
 * callee-saved.
 */
static void EmitLoadStoreSingle(CBuild &b, UINT32 opcode, bool store, bool indexForm, bool update)
{
	CEmitter &e = b.e;
	const int ra = (int)((opcode >> 16) & 0x1f);
	const int rb = (int)((opcode >> 11) & 0x1f);
	const int frt = (int)((opcode >> 21) & 0x1f);
	const INT32 disp = (INT32)(INT16)(opcode & 0xffff);
	const INT32 fprOffset = (INT32)offsetof(PPC_REGS, fpr) + frt * 8;

	EmitEffectiveAddress(b, ra, rb, disp, indexForm, update);

	if (store)
	{
		e.LoadSd(XMM0, RBX, fprOffset);
		e.Cvtsd2ss(XMM0, XMM0);			/* f.f = (float)FPR(t).fd */
		e.MovdFromXmm(RAX, XMM0);		/* the store path hands the value over in eax */
		EmitMemAccess(b, MK_WRITE32);
	}
	else
	{
		EmitMemAccess(b, MK_READ32);
		e.MovdToXmm(XMM0, RAX);			/* eax holds the loaded word */
		e.Cvtss2sd(XMM0, XMM0);			/* FPR(t).fd = (double)f.f */
		e.StoreSd(RBX, fprOffset, XMM0);
	}

	if (update)
		StGpr(b, ra, RCX);

	b.nativeCount++;
}

/*
 * lfd / lfdu / lfdx / lfdux / stfd / stfdu / stfdx / stfdux.
 *
 * CModel3::Read64/Write64 split every 64-bit transfer into two 32-bit accesses:
 * the high 32 bits of FPR(t).id live at ea + 0 and the low 32 bits at ea + 4.
 * On little-endian x86-64, FPR(t).id has its low word at fprOffset + 0 and its
 * high word at fprOffset + 4.
 */
static void EmitLoadStoreDouble(CBuild &b, UINT32 opcode, bool store, bool indexForm, bool update)
{
	CEmitter &e = b.e;
	const int ra = (int)((opcode >> 16) & 0x1f);
	const int rb = (int)((opcode >> 11) & 0x1f);
	const int frt = (int)((opcode >> 21) & 0x1f);
	const INT32 disp = (INT32)(INT16)(opcode & 0xffff);
	const INT32 fprOffset = (INT32)offsetof(PPC_REGS, fpr) + frt * 8;

	EmitEffectiveAddress(b, ra, rb, disp, indexForm, update);

	if (store)
	{
		e.Load32(RAX, RBX, fprOffset + 4);	/* high word: (UINT32)(FPR(t).id >> 32) */
		EmitMemAccess(b, MK_WRITE32);
		e.AddImm32(RCX, 4);
		e.Load32(RAX, RBX, fprOffset + 0);	/* low word:  (UINT32)FPR(t).id         */
		EmitMemAccess(b, MK_WRITE32);
	}
	else
	{
		EmitMemAccess(b, MK_READ32);
		e.Store32(RBX, fprOffset + 4, RAX);	/* high word from ea + 0 */
		e.AddImm32(RCX, 4);
		EmitMemAccess(b, MK_READ32);
		e.Store32(RBX, fprOffset + 0, RAX);	/* low word from ea + 4  */
	}

	if (update)
	{
		e.SubImm32(RCX, 4);
		StGpr(b, ra, RCX);
	}

	b.nativeCount++;
}

/*
 * bcx / bclrx / bcctrx.
 *
 * check_condition_code() is unrolled: BO is a compile time constant, so each of
 * its three tests either disappears or becomes a single branch. The CTR
 * decrement has to happen even when the branch is not taken.
 */
static void EmitBranch(CBuild &b, UINT32 opcode, UINT32 pc, int form, UINT32 target)
{
	CEmitter &e = b.e;
	const int bo = (int)((opcode >> 21) & 0x1f);
	const int bi = (int)((opcode >> 16) & 0x1f);
	const bool lk = (opcode & 1) != 0;

	const bool ctrAlways = (bo & 0x04) != 0;
	const bool condAlways = (bo & 0x10) != 0;

	if (!ctrAlways)
	{
		e.Load32(RDX, RBX, (INT32)offsetof(PPC_REGS, ctr));
		e.SubImm32(RDX, 1);
		e.Store32(RBX, (INT32)offsetof(PPC_REGS, ctr), RDX);
	}

	/* Default destination: fall through. Kept in R9. */
	e.MovImm32(R9, pc + 4);

	size_t toSkip[2];
	int nSkip = 0;

	if (!ctrAlways)
	{
		e.Load32(RDX, RBX, (INT32)offsetof(PPC_REGS, ctr));
		e.CmpImm32(RDX, 0);
		/* ctr_ok = (CTR != 0) ^ bo3; bo3 set means "branch when CTR == 0". */
		toSkip[nSkip++] = e.BCondEmit((bo & 0x02) ? CC_NE : CC_E);
	}

	if (!condAlways)
	{
		e.Load8U(RDX, RBX, (INT32)offsetof(PPC_REGS, cr) + (bi >> 2));
		e.TestImm32(RDX, 1u << (3 - (bi & 3)));
		/* condition_ok = CRBIT(bi) ^ (bo1 ^ 1); bo1 set inverts the test. */
		toSkip[nSkip++] = e.BCondEmit((bo & 0x08) ? CC_E : CC_NE);
	}

	/* Branch taken. */
	if (form == 0)
	{
		e.MovImm32(R9, target);
	}
	else if (form == 1)					/* bclr: destination is the old LR */
	{
		e.Load32(R9, RBX, (INT32)offsetof(PPC_REGS, lr));
		e.AndImm32(R9, 0xFFFFFFFCu);
	}
	else								/* bcctr: destination is CTR after the decrement */
	{
		e.Load32(R9, RBX, (INT32)offsetof(PPC_REGS, ctr));
		e.AndImm32(R9, 0xFFFFFFFCu);
	}

	const size_t skipAt = e.Here();
	for (int i = 0; i < nSkip; i++)
		e.ResolveCond(toSkip[i], skipAt);

	if (lk)
	{
		e.MovImm32(RDX, pc + 4);
		e.Store32(RBX, (INT32)offsetof(PPC_REGS, lr), RDX);
	}

	e.MovImm32(RDX, pc);
	e.Store32(RBX, (INT32)offsetof(PPC_REGS, pc), RDX);
	e.Store32(RBX, (INT32)offsetof(PPC_REGS, npc), R9);
	JumpToExit(b);

	/*
	 * A backward branch is the arm of the idle-loop skip in the dispatcher, so
	 * the block has to keep returning there: chaining would spin inside the
	 * translated loop and never give the skip a chance to run the clock forward.
	 */
	if (form == 0 && target <= pc)
	{
		b.flags |= kFlagBackwardBranch;
		b.branchPc = pc;
	}

	b.nativeCount++;
	b.endedWithExit = true;
}

static void EmitOne(CBuild &b, UINT32 opcode, UINT32 pc)
{
	CEmitter &e = b.e;
	const UINT32 prim = opcode >> 26;
	const int rt = (int)((opcode >> 21) & 0x1f);
	const int ra = (int)((opcode >> 16) & 0x1f);
	const int rb = (int)((opcode >> 11) & 0x1f);
	const int rs = rt;
	const INT32 simm = (INT32)(INT16)(opcode & 0xffff);
	const UINT32 uimm = opcode & 0xffff;
	const bool rc = (opcode & 1) != 0;
	const bool oe = (opcode & 0x400) != 0;

	switch (prim)
	{
		/* ---------------------------------------------------------------- */
		/* Immediate arithmetic                                             */
		/* ---------------------------------------------------------------- */
		case 8:		/* subfic: rD = SIMM16 - rA, CA = (UINT32)SIMM16 >= (UINT32)rA */
			LdGpr(b, RDX, ra);
			e.MovImm32(RAX, (UINT32)simm);
			e.Sub32(RAX, RDX);				/* CF is "borrow", CA is "no borrow" */
			StGpr(b, rt, RAX);
			EmitXerCAFromFlags(b, CC_AE);
			b.nativeCount++;
			return;

		case 12:	/* addic  */
		case 13:	/* addic. */
			LdGpr(b, RDX, ra);
			e.MovImm32(RAX, (UINT32)simm);
			e.Add32(RAX, RDX);
			StGpr(b, rt, RAX);
			EmitXerCAFromFlags(b, CC_B);		/* CF is the carry out */
			if (prim == 13)
				EmitSetCR0(b, RAX);
			b.nativeCount++;
			return;

		case 14:	/* addi */
			if (ra != 0)
			{
				LdGpr(b, RAX, ra);
				e.AddImm32(RAX, simm);
			}
			else
			{
				e.MovImm32(RAX, (UINT32)simm);
			}
			StGpr(b, rt, RAX);
			b.nativeCount++;
			return;

		case 15:	/* addis */
		{
			const UINT32 imm = uimm << 16;
			if (ra != 0)
			{
				LdGpr(b, RAX, ra);
				e.AddImm32(RAX, (INT32)imm);
			}
			else
			{
				e.MovImm32(RAX, imm);
			}
			StGpr(b, rt, RAX);
			b.nativeCount++;
			return;
		}

		case 24:	/* ori  */
		case 25:	/* oris */
		case 26:	/* xori */
		case 27:	/* xoris */
		{
			const bool isOr = (prim == 24 || prim == 25);
			const UINT32 imm = (prim == 25 || prim == 27) ? (uimm << 16) : uimm;
			LdGpr(b, RAX, rs);
			if (imm != 0)
			{
				if (isOr)
					e.OrImm32(RAX, imm);
				else
					e.XorImm32(RAX, imm);
			}
			StGpr(b, ra, RAX);
			b.nativeCount++;
			return;
		}

		case 28:	/* andi. */
		case 29:	/* andis. */
		{
			const UINT32 imm = (prim == 29) ? (uimm << 16) : uimm;
			LdGpr(b, RAX, rs);
			e.AndImm32(RAX, imm);
			StGpr(b, ra, RAX);
			EmitSetCR0(b, RAX);
			b.nativeCount++;
			return;
		}

		case 7:		/* mulli: rD = rA * (UINT32)(INT16)imm, low 32 bits */
			LdGpr(b, RAX, ra);
			e.MovImm32(RDX, (UINT32)simm);
			e.Imul32(RAX, RDX);
			StGpr(b, rt, RAX);
			b.nativeCount++;
			return;

		/* ---------------------------------------------------------------- */
		/* Compare                                                          */
		/* ---------------------------------------------------------------- */
		case 10:	/* cmpli */
		case 11:	/* cmpi  */
		{
			const int crfd = (int)((opcode >> 23) & 0x7);
			LdGpr(b, RDX, ra);
			e.MovImm32(R8, (prim == 11) ? (UINT32)simm : uimm);
			e.Cmp32(RDX, R8);
			EmitCRFieldFromFlags(b, crfd, prim == 11);
			b.nativeCount++;
			return;
		}

		/* ---------------------------------------------------------------- */
		/* Rotate / shift                                                   */
		/* ---------------------------------------------------------------- */
		case 20:	/* rlwimi */
		case 21:	/* rlwinm */
		{
			const int sh = (int)((opcode >> 11) & 0x1f);
			const int mb = (int)((opcode >> 6) & 0x1f);
			const int me = (int)((opcode >> 1) & 0x1f);
			const UINT32 mask = ppc_rotate_mask[mb][me];

			LdGpr(b, RAX, rs);
			e.RorImm32(RAX, (32 - sh) & 31);

			if (prim == 21)
			{
				if (mask != 0xFFFFFFFFu)
					e.AndImm32(RAX, mask);
			}
			else if (mask == 0)
			{
				LdGpr(b, RAX, ra);
			}
			else
			{
				e.AndImm32(RAX, mask);
				if (mask != 0xFFFFFFFFu)
				{
					LdGpr(b, RDX, ra);
					e.AndImm32(RDX, ~mask);
					e.Or32(RAX, RDX);
				}
			}

			StGpr(b, ra, RAX);
			if (rc)
				EmitSetCR0(b, RAX);
			b.nativeCount++;
			return;
		}

		case 23:	/* rlwnm */
		{
			const int mb = (int)((opcode >> 6) & 0x1f);
			const int me = (int)((opcode >> 1) & 0x1f);
			const UINT32 mask = ppc_rotate_mask[mb][me];

			LdGpr(b, RAX, rs);
			LdGpr(b, RCX, rb);
			e.AndImm32(RCX, 0x1Fu);			/* "rol reg, cl" masks the count too */
			e.RolCl32(RAX);
			if (mask != 0xFFFFFFFFu)
				e.AndImm32(RAX, mask);
			StGpr(b, ra, RAX);
			if (rc)
				EmitSetCR0(b, RAX);
			b.nativeCount++;
			return;
		}

		/* ---------------------------------------------------------------- */
		/* Branch                                                           */
		/* ---------------------------------------------------------------- */
		case 16:	/* bcx */
		{
			UINT32 target = (UINT32)(simm & ~0x3);
			if ((opcode & 0x2) == 0)
				target += pc;
			EmitBranch(b, opcode, pc, 0, target);
			return;
		}

		case 18:	/* bx */
		{
			UINT32 target = opcode & 0x03FFFFFCu;
			if (target & 0x02000000u)
				target |= 0xFC000000u;
			if ((opcode & 0x2) == 0)
				target += pc;

			if (opcode & 0x1)
			{
				e.MovImm32(RDX, pc + 4);
				e.Store32(RBX, (INT32)offsetof(PPC_REGS, lr), RDX);
			}
			e.MovImm32(RDX, pc);
			e.MovImm32(R9, target);
			e.Store32(RBX, (INT32)offsetof(PPC_REGS, pc), RDX);
			e.Store32(RBX, (INT32)offsetof(PPC_REGS, npc), R9);
			JumpToExit(b);
			b.nativeCount++;
			b.endedWithExit = true;
			return;
		}

		case 19:
		{
			const UINT32 sub = (opcode >> 1) & 0x3ff;
			if (sub == 0)			/* mcrf */
			{
				const int crfD = rt >> 2;
				const int crfS = ra >> 2;
				if (crfD != crfS)
				{
					e.Load8U(RAX, RBX, (INT32)offsetof(PPC_REGS, cr) + crfS);
					e.Store8(RBX, (INT32)offsetof(PPC_REGS, cr) + crfD, RAX);
				}
				b.nativeCount++;
				return;
			}
			if (sub == 16)			/* bclrx */
			{
				EmitBranch(b, opcode, pc, 1, 0);
				return;
			}
			if (sub == 528)			/* bcctrx */
			{
				EmitBranch(b, opcode, pc, 2, 0);
				return;
			}
			break;
		}

		/* ---------------------------------------------------------------- */
		/* Loads and stores                                                 */
		/* ---------------------------------------------------------------- */
		case 32:	EmitLoadStore(b, opcode, MK_READ32,  false, false); return;	/* lwz  */
		case 33:	EmitLoadStore(b, opcode, MK_READ32,  false, true);  return;	/* lwzu */
		case 34:	EmitLoadStore(b, opcode, MK_READ8,   false, false); return;	/* lbz  */
		case 35:	EmitLoadStore(b, opcode, MK_READ8,   false, true);  return;	/* lbzu */
		case 36:	EmitLoadStore(b, opcode, MK_WRITE32, false, false); return;	/* stw  */
		case 37:	EmitLoadStore(b, opcode, MK_WRITE32, false, true);  return;	/* stwu */
		case 38:	EmitLoadStore(b, opcode, MK_WRITE8,  false, false); return;	/* stb  */
		case 39:	EmitLoadStore(b, opcode, MK_WRITE8,  false, true);  return;	/* stbu */
		case 40:	EmitLoadStore(b, opcode, MK_READ16,  false, false); return;	/* lhz  */
		case 41:	EmitLoadStore(b, opcode, MK_READ16,  false, true);  return;	/* lhzu */
		case 42:	EmitLoadStore(b, opcode, MK_READ16S, false, false); return;	/* lha  */
		case 43:	EmitLoadStore(b, opcode, MK_READ16S, false, true);  return;	/* lhau */
		case 44:	EmitLoadStore(b, opcode, MK_WRITE16, false, false); return;	/* sth  */
		case 45:	EmitLoadStore(b, opcode, MK_WRITE16, false, true);  return;	/* sthu */

		/* ---------------------------------------------------------------- */
		/* Floating point load/store                                        */
		/* ---------------------------------------------------------------- */
		case 48:	EmitLoadStoreSingle(b, opcode, false, false, false); return;	/* lfs   */
		case 49:	EmitLoadStoreSingle(b, opcode, false, false, true);  return;	/* lfsu  */
		case 50:	EmitLoadStoreDouble(b, opcode, false, false, false); return;	/* lfd   */
		case 51:	EmitLoadStoreDouble(b, opcode, false, false, true);  return;	/* lfdu  */
		case 52:	EmitLoadStoreSingle(b, opcode, true,  false, false); return;	/* stfs  */
		case 53:	EmitLoadStoreSingle(b, opcode, true,  false, true);  return;	/* stfsu */
		case 54:	EmitLoadStoreDouble(b, opcode, true,  false, false); return;	/* stfd  */
		case 55:	EmitLoadStoreDouble(b, opcode, true,  false, true);  return;	/* stfdu */

		/* ---------------------------------------------------------------- */
		/* Floating point sign and register moves                           */
		/* ---------------------------------------------------------------- */
		case 63:
		{
			const UINT32 sub = (opcode >> 1) & 0x3ff;
			const int fb = (int)((opcode >> 11) & 0x1f);
			const INT32 fprBase = (INT32)offsetof(PPC_REGS, fpr);

			/*
			 * fmr / fneg / fabs / fnabs only rewrite the double, or flip bits in
			 * it; none of them touches FPSCR. The record forms are left to the C
			 * handler because they also copy the class bits into CR1.
			 *
			 * They are done with 64-bit integer operations rather than the FP
			 * instructions: all four are defined as pure bit operations, so the
			 * pattern - NaN payloads included - is preserved exactly, and no
			 * xmm register is needed at all.
			 */
			if (!rc && (sub == 72 || sub == 40 || sub == 264 || sub == 136))
			{
				e.Load64(RAX, RBX, fprBase + fb * 8);
				switch (sub)
				{
					case 40:	e.BtcImm64(RAX, 63); break;	/* fneg  */
					case 264:	e.BtrImm64(RAX, 63); break;	/* fabs  */
					case 136:	e.BtsImm64(RAX, 63); break;	/* fnabs */
					default:								break;	/* fmr   */
				}
				e.Store64(RBX, fprBase + rt * 8, RAX);
				b.nativeCount++;
				return;
			}
			break;
		}

		/* ---------------------------------------------------------------- */
		/* Table 31                                                         */
		/* ---------------------------------------------------------------- */
		case 31:
		{
			const UINT32 sub = (opcode >> 1) & 0x3ff;

			switch (sub)
			{
				case 23:	EmitLoadStore(b, opcode, MK_READ32,  true, false); return;	/* lwzx  */
				case 55:	EmitLoadStore(b, opcode, MK_READ32,  true, true);  return;	/* lwzux */
				case 151:	EmitLoadStore(b, opcode, MK_WRITE32, true, false); return;	/* stwx  */
				case 183:	EmitLoadStore(b, opcode, MK_WRITE32, true, true);  return;	/* stwux */
				case 87:	EmitLoadStore(b, opcode, MK_READ8,   true, false); return;	/* lbzx  */
				case 119:	EmitLoadStore(b, opcode, MK_READ8,   true, true);  return;	/* lbzux */
				case 215:	EmitLoadStore(b, opcode, MK_WRITE8,  true, false); return;	/* stbx  */
				case 247:	EmitLoadStore(b, opcode, MK_WRITE8,  true, true);  return;	/* stbux */
				case 279:	EmitLoadStore(b, opcode, MK_READ16,  true, false); return;	/* lhzx  */
				case 311:	EmitLoadStore(b, opcode, MK_READ16,  true, true);  return;	/* lhzux */
				case 343:	EmitLoadStore(b, opcode, MK_READ16S, true, false); return;	/* lhax  */
				case 375:	EmitLoadStore(b, opcode, MK_READ16S, true, true);  return;	/* lhaux */
				case 407:	EmitLoadStore(b, opcode, MK_WRITE16, true, false); return;	/* sthx   */
				case 439:	EmitLoadStore(b, opcode, MK_WRITE16, true, true);  return;	/* sthux  */
				case 535:	EmitLoadStoreSingle(b, opcode, false, true, false); return;	/* lfsx   */
				case 567:	EmitLoadStoreSingle(b, opcode, false, true, true);  return;	/* lfsux  */
				case 599:	EmitLoadStoreDouble(b, opcode, false, true, false); return;	/* lfdx   */
				case 631:	EmitLoadStoreDouble(b, opcode, false, true, true);  return;	/* lfdux  */
				case 663:	EmitLoadStoreSingle(b, opcode, true,  true, false); return;	/* stfsx  */
				case 695:	EmitLoadStoreSingle(b, opcode, true,  true, true);  return;	/* stfsux */
				case 727:	EmitLoadStoreDouble(b, opcode, true,  true, false); return;	/* stfdx  */
				case 759:	EmitLoadStoreDouble(b, opcode, true,  true, true);  return;	/* stfdux */

				case 983:	/* stfiwx: WRITE32(ea, (UINT32)FPR(t).id) */
					e.Load32(RAX, RBX, (INT32)offsetof(PPC_REGS, fpr) + rt * 8);
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_WRITE32);
					b.nativeCount++;
					return;

				case 534:	/* lwbrx */
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_READ32);
					e.Bswap32(RAX);
					StGpr(b, rt, RAX);
					b.nativeCount++;
					return;

				case 662:	/* stwbrx */
					LdGpr(b, RAX, rs);
					e.Bswap32(RAX);
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_WRITE32);
					b.nativeCount++;
					return;

				case 790:	/* lhbrx */
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_READ16);
					e.Rol16Imm(RAX, 8);
					StGpr(b, rt, RAX);
					b.nativeCount++;
					return;

				case 918:	/* sthbrx */
					LdGpr(b, RAX, rs);
					e.Rol16Imm(RAX, 8);
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_WRITE16);
					b.nativeCount++;
					return;

				case 10:	/* addcx  */
				case 8:		/* subfcx */
					if (oe)
						break;
					LdGpr(b, RDX, ra);
					LdGpr(b, R8, rb);
					if (sub == 10)
					{
						MovReg32(e, RAX, RDX);
						e.Add32(RAX, R8);
						StGpr(b, rt, RAX);
						EmitXerCAFromFlags(b, CC_B);
					}
					else
					{
						MovReg32(e, RAX, R8);
						e.Sub32(RAX, RDX);			/* CF is "borrow"; CA is "no borrow" */
						StGpr(b, rt, RAX);
						EmitXerCAFromFlags(b, CC_AE);
					}
					if (rc)
						EmitSetCR0(b, RAX);
					b.nativeCount++;
					return;

				case 202:	/* addzex  */
				case 200:	/* subfzex */
					if (oe)
						break;
					LdGpr(b, RDX, ra);
					if (sub == 200)
						e.Not32(RDX);				/* ~rA */
					e.Load32(R8, RBX, (INT32)offsetof(PPC_REGS, xer));
					e.ShrImm32(R8, 29);
					e.AndImm32(R8, 1u);
					MovReg32(e, RAX, RDX);
					e.Add32(RAX, R8);
					StGpr(b, rt, RAX);
					EmitXerCAFromFlags(b, CC_B);
					if (rc)
						EmitSetCR0(b, RAX);
					b.nativeCount++;
					return;

				case 266:	/* addx    */
				case 40:	/* subfx   */
				case 104:	/* negx    */
				case 235:	/* mullwx  */
				{
					/*
					 * The overflow forms (oe) have to set XER[OV]/XER[SO] and a sticky SO,
					 * which is left to the C handler. The test wraps the native emission
					 * rather than breaking out of it: a bare break here belongs to the
					 * inner switch and would fall through into the store below.
					 */
					if (!oe)
					{
						LdGpr(b, RDX, ra);
						switch (sub)
						{
							case 266:
								LdGpr(b, R8, rb);
								MovReg32(e, RAX, RDX);
								e.Add32(RAX, R8);
								break;
							case 40:
								LdGpr(b, R8, rb);
								MovReg32(e, RAX, R8);
								e.Sub32(RAX, RDX);
								break;
							case 104:
								MovReg32(e, RAX, RDX);
								e.Neg32(RAX);
								break;
							default:
								LdGpr(b, R8, rb);
								MovReg32(e, RAX, RDX);
								e.Imul32(RAX, R8);
								break;
						}
						StGpr(b, rt, RAX);
						if (rc)
							EmitSetCR0(b, RAX);
						b.nativeCount++;
						return;
					}
					break;		/* oe: fall through to the C handler */
				}

				case 28:	/* andx  */
				case 60:	/* andcx */
				case 124:	/* norx  */
				case 284:	/* eqvx  */
				case 316:	/* xorx  */
				case 412:	/* orcx  */
				case 444:	/* orx   */
				case 476:	/* nandx */
					LdGpr(b, RDX, rs);
					LdGpr(b, R8, rb);
					switch (sub)
					{
						case 28:							/* a & b   */
							MovReg32(e, RAX, RDX);
							e.And32(RAX, R8);
							break;
						case 60:							/* a & ~b  */
							e.Not32(R8);
							MovReg32(e, RAX, RDX);
							e.And32(RAX, R8);
							break;
						case 124:							/* ~(a | b) */
							MovReg32(e, RAX, RDX);
							e.Or32(RAX, R8);
							e.Not32(RAX);
							break;
						case 284:							/* ~(a ^ b) */
							MovReg32(e, RAX, RDX);
							e.Xor32(RAX, R8);
							e.Not32(RAX);
							break;
						case 316:							/* a ^ b   */
							MovReg32(e, RAX, RDX);
							e.Xor32(RAX, R8);
							break;
						case 412:							/* a | ~b  */
							e.Not32(R8);
							MovReg32(e, RAX, RDX);
							e.Or32(RAX, R8);
							break;
						case 476:							/* ~(a & b) */
							MovReg32(e, RAX, RDX);
							e.And32(RAX, R8);
							e.Not32(RAX);
							break;
						default:							/* a | b   */
							MovReg32(e, RAX, RDX);
							e.Or32(RAX, R8);
							break;
					}
					StGpr(b, ra, RAX);
					if (rc)
						EmitSetCR0(b, RAX);
					b.nativeCount++;
					return;

				case 19:	/* mfcr */
					e.Load8U(RAX, RBX, (INT32)offsetof(PPC_REGS, cr) + 0);
					for (int i = 1; i < 8; i++)
					{
						e.ShlImm32(RAX, 4);
						e.Load8U(RDX, RBX, (INT32)offsetof(PPC_REGS, cr) + i);
						e.Or32(RAX, RDX);
					}
					StGpr(b, rt, RAX);
					b.nativeCount++;
					return;

				case 144:	/* mtcrf */
				{
					const int fxm = (int)((opcode >> 12) & 0xff);
					if (fxm != 0)
					{
						LdGpr(b, RDX, rs);
						for (int i = 0; i < 8; i++)
						{
							if (fxm & (0x80 >> i))
							{
								const int shift = (7 - i) * 4;
								MovReg32(e, RAX, RDX);
								if (shift != 0)
									e.ShrImm32(RAX, shift);
								e.AndImm32(RAX, 0xFu);
								e.Store8(RBX, (INT32)offsetof(PPC_REGS, cr) + i, RAX);
							}
						}
					}
					b.nativeCount++;
					return;
				}

				case 954:	/* extsbx */
					LdGpr(b, RDX, rs);
					e.Movsx8To32(RAX, RDX);
					StGpr(b, ra, RAX);
					if (rc)
						EmitSetCR0(b, RAX);
					b.nativeCount++;
					return;

				case 922:	/* extshx */
					LdGpr(b, RDX, rs);
					e.Movsx16To32(RAX, RDX);
					StGpr(b, ra, RAX);
					if (rc)
						EmitSetCR0(b, RAX);
					b.nativeCount++;
					return;

				case 26:	/* cntlzw */
					/*
					 * bsr gives the index of the highest set bit (undefined for
					 * zero, so it is replaced by 63 there), and 63 ^ 31 == 32,
					 * 31 ^ bsr == clz. This needs no CPUID check, unlike lzcnt.
					 */
					LdGpr(b, RDX, rt);
					e.Bsr32(RAX, RDX);
					e.MovImm32(R8, 63);
					e.Cmovcc32(CC_E, RAX, R8);
					e.XorImm32(RAX, 31);
					StGpr(b, ra, RAX);
					if (rc)
						EmitSetCR0(b, RAX);
					b.nativeCount++;
					return;

				case 0:		/* cmp  */
				case 32:	/* cmpl */
				{
					const int crfd = (int)((opcode >> 23) & 0x7);
					LdGpr(b, RDX, ra);
					LdGpr(b, R8, rb);
					e.Cmp32(RDX, R8);
					EmitCRFieldFromFlags(b, crfd, sub == 0);
					b.nativeCount++;
					return;
				}

				case 24:	/* slwx */
				case 536:	/* srwx */
					/*
					 * x86 masks a register shift count to 5 bits, while PowerPC
					 * uses rB & 0x3F and produces zero from 32 upwards. The value
					 * is therefore zeroed first and then shifted by the (masked)
					 * count, which leaves the zero in place.
					 */
					LdGpr(b, RAX, rs);
					LdGpr(b, RCX, rb);
					e.AndImm32(RCX, 0x3Fu);
					e.CmpImm32(RCX, 32);
					e.MovImm32(RDX, 0);
					e.Cmovcc32(CC_AE, RAX, RDX);
					if (sub == 24)
						e.ShlCl32(RAX);
					else
						e.ShrCl32(RAX);
					StGpr(b, ra, RAX);
					if (rc)
						EmitSetCR0(b, RAX);
					b.nativeCount++;
					return;

				case 824:	/* srawix */
				{
					const int sh = (int)((opcode >> 11) & 0x1f);
					LdGpr(b, RDX, rs);
					if (sh != 0)
					{
						MovReg32(e, RAX, RDX);
						e.SarImm32(RAX, sh);

						/*
						 * XER.CA = (rS < 0) && (the low SH bits of rS are not all
						 * zero): fold the sign into the value, then test whether
						 * shifting the residue out leaves anything.
						 */
						e.Mov32(R8, RDX);
						e.SarImm32(R8, 31);
						e.And32(R8, RDX);
						e.ShlImm32(R8, (32 - sh) & 31);
						e.Test32(R8, R8);
						e.Cset32(CC_NE, R8);
						e.Load32(R9, RBX, (INT32)offsetof(PPC_REGS, xer));
						e.AndImm32(R9, ~0x20000000u);
						e.ShlImm32(R8, 29);
						e.Or32(R9, R8);
						e.Store32(RBX, (INT32)offsetof(PPC_REGS, xer), R9);
					}
					else
					{
						MovReg32(e, RAX, RDX);
						e.Load32(R9, RBX, (INT32)offsetof(PPC_REGS, xer));
						e.AndImm32(R9, ~0x20000000u);
						e.Store32(RBX, (INT32)offsetof(PPC_REGS, xer), R9);
					}
					StGpr(b, ra, RAX);
					if (rc)
						EmitSetCR0(b, RAX);
					b.nativeCount++;
					return;
				}

				case 339:	/* mfspr */
				{
					const UINT32 spr = ((opcode >> 16) & 0x1f) | ((opcode >> 6) & 0x3e0);
					switch (spr)
					{
						case SPR_LR:	e.Load32(RAX, RBX, (INT32)offsetof(PPC_REGS, lr)); break;
						case SPR_CTR:	e.Load32(RAX, RBX, (INT32)offsetof(PPC_REGS, ctr)); break;
						case SPR_XER:	e.Load32(RAX, RBX, (INT32)offsetof(PPC_REGS, xer)); break;
						case SPR_SPRG0:	e.Load32(RAX, RBX, (INT32)offsetof(PPC_REGS, sprg) + 0); break;
						case SPR_SPRG1:	e.Load32(RAX, RBX, (INT32)offsetof(PPC_REGS, sprg) + 4); break;
						case SPR_SPRG2:	e.Load32(RAX, RBX, (INT32)offsetof(PPC_REGS, sprg) + 8); break;
						case SPR_SPRG3:	e.Load32(RAX, RBX, (INT32)offsetof(PPC_REGS, sprg) + 12); break;
						default:
							EmitHelperCall(b, opcode, pc, (void *)(uintptr_t)&JitHelper, false,
								!HelperNeedsCycles(opcode));
							return;
					}
					StGpr(b, rt, RAX);
					b.nativeCount++;
					return;
				}

				case 467:	/* mtspr: a terminator, the SPR may be DEC or a BAT */
				{
					const UINT32 spr = ((opcode >> 16) & 0x1f) | ((opcode >> 6) & 0x3e0);
					if (spr == SPR_LR || spr == SPR_CTR || spr == SPR_XER ||
						(spr >= SPR_SPRG0 && spr <= SPR_SPRG3))
					{
						LdGpr(b, RAX, rs);
						switch (spr)
						{
							case SPR_LR:	e.Store32(RBX, (INT32)offsetof(PPC_REGS, lr), RAX); break;
							case SPR_CTR:	e.Store32(RBX, (INT32)offsetof(PPC_REGS, ctr), RAX); break;
							case SPR_XER:	e.Store32(RBX, (INT32)offsetof(PPC_REGS, xer), RAX); break;
							default:		e.Store32(RBX, (INT32)offsetof(PPC_REGS, sprg) + 4 * (INT32)(spr - SPR_SPRG0), RAX); break;
						}
						b.nativeCount++;
						return;
					}
					break;
				}

				case 982:	/* icbi */
					EmitHelperCall(b, opcode, pc, (void *)(uintptr_t)&JitHelperIcbi, true);
					return;

				default:
					break;
			}
			break;
		}

		default:
			break;
	}

	/*
	 * Everything else - floating point, the string and multiple load/store
	 * forms, the privileged instructions and anything invalid - runs through
	 * the interpreter's own handler. Terminators among them (sc, rfi, isync,
	 * mtmsr, tw, twi) must not fall through.
	 */
	EmitHelperCall(b, opcode, pc, (void *)(uintptr_t)&JitHelper, IsTerminator(opcode),
		!HelperNeedsCycles(opcode));
}

/******************************************************************************
 Block cache
******************************************************************************/

static void FlushAll(void);

static CBlock *SetFor(UINT32 pc)
{
	if (pc < kRamSize)
		return &s_jit.ramTable[((pc >> 2) & (kRamTableSets - 1)) * kWays];
	if (pc >= kCromBase)
		return &s_jit.cromTable[(((pc - kCromBase) >> 2) & (kCromTableSets - 1)) * kWays];
	return NULL;
}

static UINT32 SetIndexFor(UINT32 pc)
{
	if (pc < kRamSize)
		return (pc >> 2) & (kRamTableSets - 1);
	return kRamTableSets + (((pc - kCromBase) >> 2) & (kCromTableSets - 1));
}

static UINT32 GenerationFor(UINT32 pc)
{
	return (pc < kRamSize) ? s_jit.ramGen : s_jit.cromGen;
}

static void MarkCodePages(UINT32 firstPc, int insnCount)
{
	UINT32 first = firstPc >> kCodePageShift;
	UINT32 last = (firstPc + (UINT32)(insnCount * 4)) >> kCodePageShift;
	if (last >= kCodePageCount)
		last = kCodePageCount - 1;
	for (UINT32 page = first; page <= last; page++)
		s_jit.codePage[page] = 1;
}

static const UINT32 *HostOpForPc(UINT32 pc, UINT32 *regionEnd)
{
	UINT32 offset = pc - ppc.cur_fetch.start;
	UINT32 range = ppc.cur_fetch.end - ppc.cur_fetch.start;
	if (ppc.cur_fetch.ptr != NULL && offset <= range)
	{
		*regionEnd = ppc.cur_fetch.end;
		return &ppc.cur_fetch.ptr[offset >> 2];
	}

	for (UINT32 i = 0; ppc.fetch != NULL && ppc.fetch[i].ptr != NULL; i++)
	{
		offset = pc - ppc.fetch[i].start;
		range = ppc.fetch[i].end - ppc.fetch[i].start;
		if (offset <= range)
		{
			*regionEnd = ppc.fetch[i].end;
			return &ppc.fetch[i].ptr[offset >> 2];
		}
	}
	return NULL;
}

static bool BuildBlock(CBlock *block, UINT32 entryPc, const UINT32 *ops, const UINT32 *pcs, int count)
{
	CBuild b;
	b.e.Reset(s_jit.scratch, kScratchBytes);
	b.entryPc = entryPc;
	b.count = count;
	b.index = 0;
	b.syncedThrough = 0;
	b.nativeCount = 0;
	b.helperCount = 0;
	b.exitCommon = 0;
	b.restoreAt = 0;
	b.exitFixCount = 0;
	b.bailFixCount = 0;
	b.endedWithExit = false;
	b.unverifiable = false;
	b.hasStore = false;
	b.flags = 0;
	b.branchPc = 0;

	EmitPrologue(b);

	for (int i = 0; i < count; i++)
	{
		b.index = i;
		EmitOne(b, ops[i], pcs[i]);
	}

	if (!b.endedWithExit)
	{
		/* Fell out of the block: execution continues at the next instruction. */
		const UINT32 lastPc = pcs[count - 1];
		b.e.MovImm32(RDX, lastPc);
		b.e.MovImm32(R9, lastPc + 4);
		b.e.Store32(RBX, (INT32)offsetof(PPC_REGS, pc), RDX);
		b.e.Store32(RBX, (INT32)offsetof(PPC_REGS, npc), R9);
	}

	EmitEpilogue(b);
	b.e.Finalize();

	if (!b.e.Ok())
		return false;

	/*
	 * Every instruction must have been emitted either natively or as a helper
	 * call. A missing case in EmitOne() would silently shorten the block while
	 * the dispatcher still charged it the full instruction count, so the block is
	 * refused instead - the dispatcher then leaves the rest of the slice to the
	 * interpreter, which is slow but correct.
	 */
	if (b.nativeCount + b.helperCount != count)
	{
		static UINT32 loggedMiscounts = 0;
		if (++loggedMiscounts <= 8)
			ErrorLog("PPC JIT: the block at %08X emitted %d of %d instruction(s); leaving it to the interpreter.",
				entryPc, b.nativeCount + b.helperCount, count);
		return false;
	}

	const size_t bytes = b.e.Size();
	if (s_jit.codeUsed + bytes + 16 > s_jit.codeSize)
	{
		FlushAll();
		if (s_jit.codeUsed + bytes + 16 > s_jit.codeSize)
			return false;
	}

	if (!s_jit.codeWritable &&
		!ProtectCode(s_jit.code, s_jit.codeSize, true))
		return false;

	UINT8 *dest = s_jit.code + s_jit.codeUsed;
	memcpy(dest, s_jit.scratch, bytes);
	s_jit.codeUsed += (bytes + 15) & ~(size_t)15;

	if (!s_jit.codeWritable &&
		!ProtectCode(s_jit.code, s_jit.codeSize, false))
		return false;

	FlushCode(dest, bytes);

	block->pc = entryPc;
	block->gen = GenerationFor(entryPc);
	block->flags = b.flags;
	if (b.unverifiable)
		block->flags |= kFlagUnverifiable;
	block->branchPc = b.branchPc;
	block->insnCount = (UINT16)count;
	block->nativeCount = (UINT16)b.nativeCount;
	block->helperCount = (UINT16)b.helperCount;
	block->codeBytes = (UINT16)(bytes > 0xFFFF ? 0xFFFF : bytes);
	block->fn = (void (*)(void))(void *)dest;

	s_jit.blocksCompiled++;
	return true;
}

static CBlock *GetBlock(UINT32 pc)
{
	CBlock *set = SetFor(pc);
	if (set == NULL)
	{
		s_jit.compileFailures++;
		s_jit.compileFailurePc = pc;
		return NULL;
	}

	const UINT32 gen = GenerationFor(pc);
	for (int way = 0; way < kWays; way++)
	{
		if (set[way].fn != NULL && set[way].pc == pc && set[way].gen == gen)
			return &set[way];
	}

	UINT32 regionEnd = 0;
	const UINT32 *hostOp = HostOpForPc(pc, &regionEnd);
	if (hostOp == NULL || pc >= regionEnd)
	{
		s_jit.compileFailures++;
		s_jit.compileFailurePc = pc;
		return NULL;
	}

	UINT32 ops[kMaxBlockInsns];
	UINT32 pcs[kMaxBlockInsns];
	int count = 0;

	while (count < kMaxBlockInsns)
	{
		const UINT32 insnPc = pc + (UINT32)count * 4;
		if (insnPc >= regionEnd)
			break;
		const UINT32 op = hostOp[count];
		ops[count] = op;
		pcs[count] = insnPc;
		count++;
		if (IsTerminator(op))
			break;
	}

	if (count == 0)
		return NULL;

	/*
	 * Pick a victim: a stale entry first (its generation was invalidated by a
	 * self-modifying store), otherwise the longest-unused way of the set. A
	 * monomorphic index would miss every time two hot blocks map to the same
	 * entry, and each miss is a full translation - at five instructions per
	 * block that is most of the frame.
	 */
	CBlock *victim = &set[0];
	bool found = false;
	for (int way = 0; way < kWays && !found; way++)
	{
		if (set[way].fn == NULL || set[way].gen != gen)
		{
			victim = &set[way];
			found = true;
		}
	}
	if (!found)
	{
		const UINT32 setIndex = SetIndexFor(pc);
		victim = &set[s_jit.victim[setIndex] & (kWays - 1)];
		s_jit.victim[setIndex] = (UINT8)((s_jit.victim[setIndex] + 1) & (kWays - 1));
	}

	if (!BuildBlock(victim, pc, ops, pcs, count))
	{
		s_jit.compileFailures++;
		s_jit.compileFailurePc = pc;
		return NULL;
	}

	if (pc < kRamSize)
		MarkCodePages(pc, count);

	return victim;
}

/******************************************************************************
 Invalidation
******************************************************************************/

static void FlushAll(void)
{
	memset(s_jit.ramTable, 0, sizeof(s_jit.ramTable));
	memset(s_jit.cromTable, 0, sizeof(s_jit.cromTable));
	memset(s_jit.codePage, 0, sizeof(s_jit.codePage));
	memset(s_jit.victim, 0, sizeof(s_jit.victim));
	s_jit.ramGen++;
	s_jit.cromGen++;
	s_jit.codeUsed = 0;
	s_jit.selfModified = 0;
}

static void FlushRam(void)
{
	s_jit.ramGen++;
	memset(s_jit.codePage, 0, sizeof(s_jit.codePage));
	s_jit.selfModified = 0;
	s_jit.smcFlushes++;
}

/* Called from ppc_set_fast_memory(): a reallocated RAM buffer invalidates code. */
static void NoteFastMemory(const PPC_FAST_MEMORY *fast)
{
	if (fast != NULL)
	{
		s_pins.ram = fast->ram;
		s_pins.ramWrite = fast->ramWrite;
		s_pins.crom = fast->crom;
		s_pins.cromBank = fast->cromBank;
	}
	else
	{
		s_pins.ram = NULL;
		s_pins.ramWrite = NULL;
		s_pins.crom = NULL;
		s_pins.cromBank = NULL;
	}

	/*
	 * Before Initialise() runs there is nothing cached to invalidate, but the
	 * pins still have to be recorded: the model publishes the windows while it
	 * sets up, which is before ppc_init() allocates the pool.
	 */
	if (!s_jit.initialised)
		return;

	if (s_pins.ram != s_jit.lastRam)
	{
		s_jit.lastRam = s_pins.ram;
		FlushAll();
	}
}

/******************************************************************************
 Lockstep verifier
******************************************************************************/

static void DumpBlock(CBlock *block, const UINT32 *ops, int count)
{
	ErrorLog("PPC JIT: state mismatch in the block at %08X (%d instruction(s)):", block->pc, count);
	for (int i = 0; i < count; i++)
	{
		char mnem[64];
		char oprs[128];
		mnem[0] = oprs[0] = '\0';
		DisassemblePowerPC(ops[i], block->pc + (UINT32)i * 4, mnem, oprs, sizeof(oprs), true);
		ErrorLog("  %08X: %08X  %s %s", block->pc + (UINT32)i * 4, ops[i], mnem, oprs);
	}

	/*
	 * The generated code, one line per four words. The block starts on a 16-byte
	 * boundary, so the addresses are known: this can be pasted into a
	 * disassembler as a raw binary blob.
	 */
	const UINT32 *code = (const UINT32 *)(const void *)block->fn;
	const int words = (int)(block->codeBytes / 4);
	ErrorLog("  generated code, %d word(s) at %p:", words, (const void *)block->fn);
	for (int i = 0; i < words; i += 4)
	{
		char line[64];
		size_t used = 0;
		for (int j = 0; j < 4 && i + j < words; j++)
		{
			const unsigned word = code[i + j];
			used += (size_t)snprintf(line + used, sizeof(line) - used, "%08X ", word);
		}
		ErrorLog("    %s", line);
	}
}

/*
 * Runs the compiled block and then the interpreter over the same instructions
 * and compares the resulting architectural state.
 *
 * Guest RAM is put back between the two runs (JitSavePage recorded the pages the
 * block stores into), so both start from the same memory - a correct block
 * writes the same bytes the interpreter is about to write. Blocks that store
 * through a C handler are not compared at all; see HelperMayWriteMemory.
 *
 * The machine is left in the JIT's post state.
 */
static void VerifyBlock(CBlock *block, const UINT32 *ops, int count)
{
	const PPC_REGS saved = ppc;

	/* The block under test. */
	s_savedPageCount = 0;
	s_savedPageOverflow = false;
	s_jit.busTouched = 0;
	block->fn();
	const PPC_REGS jitResult = ppc;

	/*
	 * A block that reached a device register cannot be repeated: the second
	 * read (or write) of the JTAG port, a VDP status register or the sound
	 * board returns something else. Leave the machine in the JIT's state and
	 * move on without comparing.
	 */
	if (s_jit.busTouched)
	{
		s_savedPageCount = 0;
		SyncOp(ppc.npc);
		return;
	}

	/* Put guest memory back the way it was before the block ran. */
	RestoreSavedPages();

	/* Reference run. */
	ppc = saved;
	int executed = 0;
	while (executed < (int)block->insnCount && ppc.icount > 0 && !ppc.fatalError)
	{
		ppc_execute_one_instruction();
		executed++;
	}
	const PPC_REGS refResult = ppc;

	bool same = true;
	for (int i = 0; i < 32; i++)
	{
		if (jitResult.r[i] != refResult.r[i])
		{
			if (same)
			{
				DumpBlock(block, ops, count);
				ErrorLog("  %d interpretation(s), icount %d -> %d (JIT) / %d (reference), npc %08X / %08X.",
					executed, saved.icount, jitResult.icount, refResult.icount, jitResult.npc, refResult.npc);
				if (s_savedPageOverflow)
					ErrorLog("  more than %d RAM pages were written; the reference run may have started from different memory.",
						kMaxSavedPages);
				same = false;
			}
			ErrorLog("  r%-2d    expected %08X, JIT produced %08X (was %08X before the block)",
				i, refResult.r[i], jitResult.r[i], saved.r[i]);
		}
	}
	if (memcmp(jitResult.cr, refResult.cr, sizeof(jitResult.cr)) != 0)
	{
		if (same) { DumpBlock(block, ops, count); same = false; }
		ErrorLog("  cr      expected %02X%02X%02X%02X%02X%02X%02X%02X, JIT produced %02X%02X%02X%02X%02X%02X%02X%02X",
			refResult.cr[0], refResult.cr[1], refResult.cr[2], refResult.cr[3],
			refResult.cr[4], refResult.cr[5], refResult.cr[6], refResult.cr[7],
			jitResult.cr[0], jitResult.cr[1], jitResult.cr[2], jitResult.cr[3],
			jitResult.cr[4], jitResult.cr[5], jitResult.cr[6], jitResult.cr[7]);
	}
	if (memcmp(jitResult.fpr, refResult.fpr, sizeof(jitResult.fpr)) != 0)
	{
		if (same) { DumpBlock(block, ops, count); same = false; }
		for (int i = 0; i < 32; i++)
		{
			if (jitResult.fpr[i].id != refResult.fpr[i].id)
				ErrorLog("  f%-2d    expected %016llX, JIT produced %016llX", i,
					(unsigned long long)refResult.fpr[i].id, (unsigned long long)jitResult.fpr[i].id);
		}
	}
	if (memcmp(jitResult.sr, refResult.sr, sizeof(jitResult.sr)) != 0)
	{
		if (same) { DumpBlock(block, ops, count); same = false; }
		ErrorLog("  sr      differs");
	}

	const struct
	{
		const char	*name;
		UINT32		native;
		UINT32		reference;
	} fields[] =
	{
		{ "lr",     jitResult.lr,     refResult.lr     },
		{ "ctr",    jitResult.ctr,    refResult.ctr    },
		{ "xer",    jitResult.xer,    refResult.xer    },
		{ "msr",    jitResult.msr,    refResult.msr    },
		{ "fpscr",  jitResult.fpscr,  refResult.fpscr  },
		{ "pc",     jitResult.pc,     refResult.pc     },
		{ "npc",    jitResult.npc,    refResult.npc    },
		{ "icount", (UINT32)jitResult.icount, (UINT32)refResult.icount },
	};

	for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
	{
		if (fields[i].native != fields[i].reference)
		{
			if (same) { DumpBlock(block, ops, count); same = false; }
			ErrorLog("  %-6s  expected %08X, JIT produced %08X", fields[i].name, fields[i].reference, fields[i].native);
		}
	}

	ppc = jitResult;
	SyncOp(ppc.npc);

	s_jit.verifyBlocks++;
	if (!same)
	{
		s_jit.verifyFailures++;
		ErrorLog("PPC JIT: lockstep verification failed (%llu blocks checked, %llu failed). Halting.",
			(unsigned long long)s_jit.verifyBlocks, (unsigned long long)s_jit.verifyFailures);
		abort();
	}
}

/******************************************************************************
 Dispatcher
******************************************************************************/

/*
 * How many instructions the slice may still run without ending it or stepping
 * over the decrementer trigger.
 *
 *   budget = icount                          if icount <= dec_trigger_cycle
 *          = icount - dec_trigger_cycle - 1  otherwise
 */
static inline int SliceBudget(int icount, int trigger)
{
	return (icount > trigger) ? icount - trigger - 1 : icount;
}

/*
 * Runs compiled blocks until the slice ends, the machine halts, or something
 * turns up that only the interpreter can handle. Everything it leaves behind
 * (pc/npc/icount/op) is exactly what ppc_execute()'s own loop would have
 * produced after the same instructions.
 */
static void Execute(void)
{
	PPC_IDLE_STATE idleState;
	UINT32 idleTarget = 0;
	int idleHits = 0;
	int idleCooldown = 0;

	while (ppc.icount > 0 && !ppc.fatalError)
	{
		/*
		 * Without published fast memory there is nothing a block could index.
		 * Hand the slice to the interpreter rather than returning with cycles
		 * unspent, which would make the emulated clock drift slower than the
		 * wall clock.
		 */
		if (s_pins.ram == NULL)
		{
			ppc_interpret_slice();
			return;
		}

		const UINT32 pc = ppc.npc;
		if (pc == 0x500 || pc == 0x900)
		{
			s_jit.exceptionEntries++;
			s_jit.lastExceptionVector = pc;
			s_jit.lastExceptionSrr0 = ppc.srr0;
			s_jit.lastExceptionSrr1 = ppc.srr1;
			s_jit.lastExceptionPc = ppc.pc;
		}
		CBlock *block = GetBlock(pc);
		if (block == NULL)
		{
			/*
			 * Nothing can be translated here. ppc_execute() reports the
			 * instructions that actually ran, so quietly returning would slow the
			 * emulated clock down against the wall clock; the rest of the slice is
			 * interpreted instead.
			 */
			ppc_interpret_slice();
			return;
		}

		const int budget = SliceBudget(ppc.icount, ppc.dec_trigger_cycle);

		/*
		 * Never run past the end of the slice: the interpreter stops the moment
		 * icount reaches zero, and overshooting here would make the cycle count
		 * (and with it the timebase and decrementer) drift by a few instructions
		 * per slice. The last blockful of a slice is simply interpreted.
		 */
		if ((int)block->insnCount > budget)
		{
			if (ppc.icount > ppc.dec_trigger_cycle)
			{
				/*
				 * The interpreter fires the decrementer interrupt when icount is
				 * decremented to exactly dec_trigger_cycle. A block that spans that
				 * value would step over it, so let the interpreter run the few
				 * instructions up to and including it.
				 */
				do
				{
					ppc_execute_one_instruction();
				} while (ppc.icount > ppc.dec_trigger_cycle && ppc.icount > 0 && !ppc.fatalError);
				if (ppc.fatalError)
					return;
				SyncOp(ppc.npc);
				idleHits = 0;
				continue;
			}
			s_jit.shortBlockBails++;
			ppc_interpret_slice();
			return;
		}

		ppc.pc = pc;

		if (s_jit.verify && (block->flags & kFlagUnverifiable) == 0)
		{
			UINT32 ops[kMaxBlockInsns];
			UINT32 regionEnd = 0;
			const UINT32 *hostOp = HostOpForPc(pc, &regionEnd);
			int count = 0;
			if (hostOp != NULL)
			{
				while (count < (int)block->insnCount && pc + (UINT32)count * 4 < regionEnd)
				{
					ops[count] = hostOp[count];
					count++;
				}
			}
			VerifyBlock(block, ops, count);
		}
		else
		{
			block->fn();
		}

		s_jit.blockExecs++;
		block->execs++;

		if (ppc.fatalError)
			return;

		/*
		 * Leave the same state the interpreter's loop has after an instruction:
		 * ppc.pc is the one that just ran, ppc.npc and ppc.op point at the next.
		 * The loop below re-reads ppc.npc at the top, exactly like the
		 * interpreter does.
		 */
		SyncOp(ppc.npc);

		if (s_jit.selfModified)
			FlushRam();

		/*
		 * Idle (wait) loop detection, sharing the interpreter's state machine. A
		 * loop that cannot observe a change within this slice cannot leave it, so
		 * once its register state repeats the rest of the slice can be skipped.
		 */
		if (block->flags & kFlagBackwardBranch)
		{
			if (ppc.npc <= block->branchPc)
			{
				if (!IdleSkipEnabled)
					idleHits = 0;
				else if (ppc_idle_step(&idleState, &idleHits, &idleCooldown, &idleTarget))
					s_jit.idleSkips++;
			}
			else
			{
				idleHits = 0;
			}
		}
	}
}

/******************************************************************************
 Initialisation
******************************************************************************/

/*
 * Reads a diagnostic switch into "out". An empty result means "not set".
 */
static void ReadSetting(const char *envName, char *out, size_t outSize)
{
	out[0] = '\0';
	const char *value = getenv(envName);
	if (value != NULL)
		snprintf(out, outSize, "%s", value);
}

static void Shutdown(void)
{
	if (s_jit.code != NULL)
	{
		FreeCode(s_jit.code, s_jit.codeSize);
		s_jit.code = NULL;
	}
	s_jit.ready = false;
}

static bool Initialise(void)
{
	if (s_jit.initialised)
		return s_jit.ready;
	s_jit.initialised = true;

	/*
	 * SM3D_PPC_JIT=0/1 wins over the configuration, so a test script can pin the
	 * mode without touching the ini. Otherwise the value Main.cpp requested is
	 * used, and with no request at all the JIT is on.
	 */
	char jitValue[32];
	char verifyValue[32];
	ReadSetting("SM3D_PPC_JIT", jitValue, sizeof(jitValue));
	ReadSetting("SM3D_PPC_JIT_VERIFY", verifyValue, sizeof(verifyValue));

	if (jitValue[0] == '0')
		s_jit.enabled = false;
	else if (jitValue[0] == '1')
		s_jit.enabled = true;
	else
		s_jit.enabled = (s_enableRequest != 0);

	s_jit.verify = (verifyValue[0] == '1');

	char idleValue[32];
	ReadSetting("SM3D_IDLE_SKIP", idleValue, sizeof(idleValue));
	if (idleValue[0] == '0')
		ppc_set_idle_loop_skip(false);

	if (!s_jit.enabled)
	{
		InfoLog("PPC: x64 hybrid JIT disabled (C interpreter selected).");
		return false;
	}

	void *code = AllocCode(kCodeCacheSize, &s_jit.codeWritable);
	if (code == NULL)
	{
		ErrorLog("PPC: x64 hybrid JIT could not allocate %u MB of code cache; using the interpreter.",
			(unsigned)(kCodeCacheSize >> 20));
		return false;
	}

	s_jit.code = (UINT8 *)code;
	s_jit.codeSize = kCodeCacheSize;
	s_jit.codeUsed = 0;
	s_jit.ramGen = 1;
	s_jit.cromGen = 1;
	s_jit.ready = true;

	/*
	 * The model publishes the memory windows while it sets up, which is before
	 * this runs, so NoteFastMemory() returned early. Pick them up now.
	 */
	NoteFastMemory(Fast);

	InfoLog("PPC: x64 hybrid JIT enabled (%u MB %s code cache%s).",
		(unsigned)(kCodeCacheSize >> 20), s_jit.codeWritable ? "RWX" : "W^X",
		s_jit.verify ? ", lockstep verification on" : "");
	return true;
}

/*
 * Selects the execution mode. Called by Main.cpp once, before the machine is
 * built, but it also handles being called later so that a reconfiguration takes
 * effect on the next slice.
 */
static void SetEnabled(bool enabled)
{
	char setting[32];
	ReadSetting("SM3D_PPC_JIT", setting, sizeof(setting));
	if (setting[0] == '0')
		enabled = false;
	else if (setting[0] == '1')
		enabled = true;

	s_enableRequest = enabled ? 1 : 0;

	if (!s_jit.initialised || s_jit.enabled == enabled)
		return;

	s_jit.enabled = enabled;
	if (enabled && !s_jit.ready)
		ErrorLog("PPC: the x64 hybrid JIT could not be enabled; the machine keeps using the interpreter.");
	else
		InfoLog("PPC: x64 hybrid JIT %s.", enabled ? "enabled" : "disabled");
}

/*
 * Diagnostic dump, armed by SM3D_PPC_JIT_DUMP=1: every live block, its instruction
 * count and a disassembly of what it translates.
 *
 * A session that stops making progress sits in one block and never returns to the
 * dispatcher for long enough to be sampled usefully, so the block list is the only
 * thing that says which wait loop it is. Covered by an environment variable rather
 * than always on because it is a page of log per session.
 */
static void DumpLiveBlocks(void)
{
	char setting[32];
	ReadSetting("SM3D_PPC_JIT_DUMP", setting, sizeof(setting));
	if (setting[0] != '1')
		return;

	ErrorLog("PPC JIT: live blocks (SM3D_PPC_JIT_DUMP=1):");
	int dumped = 0;
	for (int t = 0; t < 2 && dumped < 64; t++)
	{
		const CBlock *table = (t == 0) ? s_jit.ramTable : s_jit.cromTable;
		const int entries = (t == 0) ? kRamTableSets * kWays : kCromTableSets * kWays;
		for (int i = 0; i < entries && dumped < 64; i++)
		{
			const CBlock &block = table[i];
			if (block.fn == NULL)
				continue;
			dumped++;

			ErrorLog("  block %08X: %d instruction(s), %d native, %d helper, flags %u, %llu execution(s)",
				block.pc, block.insnCount, block.nativeCount, block.helperCount, block.flags,
				(unsigned long long)block.execs);

			UINT32 regionEnd = 0;
			const UINT32 *hostOp = HostOpForPc(block.pc, &regionEnd);
			for (int j = 0; j < (int)block.insnCount && hostOp != NULL; j++)
			{
				const UINT32 insnPc = block.pc + (UINT32)j * 4;
				if (insnPc >= regionEnd)
					break;
				char mnem[64];
				char oprs[128];
				mnem[0] = oprs[0] = '\0';
				UINT32 endForPc = regionEnd;
				const UINT32 *insn = HostOpForPc(insnPc, &endForPc);
				if (insn == NULL)
					continue;
				DisassemblePowerPC(*insn, insnPc, mnem, oprs, sizeof(oprs), true);
				ErrorLog("    %08X: %08X  %s %s", insnPc, *insn, mnem, oprs);
			}
		}
	}

	ErrorLog("  r0-3   %08X %08X %08X %08X", ppc.r[0], ppc.r[1], ppc.r[2], ppc.r[3]);
	ErrorLog("  r4-7   %08X %08X %08X %08X", ppc.r[4], ppc.r[5], ppc.r[6], ppc.r[7]);
	ErrorLog("  lr %08X ctr %08X xer %08X msr %08X srr0 %08X srr1 %08X pc %08X npc %08X icount %d trigger %d",
		ppc.lr, ppc.ctr, ppc.xer, ppc.msr, ppc.srr0, ppc.srr1, ppc.pc, ppc.npc, ppc.icount,
		ppc.dec_trigger_cycle);
}

static void LogStats(void)
{
	if (!s_jit.initialised || s_jit.blocksCompiled == 0)
		return;

	InfoLog("PPC: x64 hybrid JIT: %llu block(s) translated, %llu executed, %llu helper call(s), "
		"%llu self-modifying flush(es), %u KB of the %u MB code cache used.",
		(unsigned long long)s_jit.blocksCompiled, (unsigned long long)s_jit.blockExecs,
		(unsigned long long)s_jit.helperCalls, (unsigned long long)s_jit.smcFlushes,
		(unsigned)(s_jit.codeUsed >> 10), (unsigned)(s_jit.codeSize >> 20));

	if (s_jit.idleSkips != 0)
		InfoLog("PPC: wait-loop skip advanced the clock %llu time(s).", (unsigned long long)s_jit.idleSkips);

	/*
	 * Native/helper coverage is a property of the translated code, not of the
	 * last interval: most executions happen inside a block and never return to
	 * the dispatcher, so summing the live blocks answers the question that
	 * matters - how much of the guest code has a native translation.
	 */
	UINT64 native = 0;
	UINT64 helper = 0;
	for (int t = 0; t < 2; t++)
	{
		const CBlock *table = (t == 0) ? s_jit.ramTable : s_jit.cromTable;
		const int entries = (t == 0) ? kRamTableSets * kWays : kCromTableSets * kWays;
		for (int i = 0; i < entries; i++)
		{
			if (table[i].fn == NULL)
				continue;
			native += table[i].nativeCount;
			helper += table[i].helperCount;
		}
	}
	if (native + helper != 0)
		InfoLog("PPC: translated code is %llu/%llu instruction(s) native (%.2f%%).",
			(unsigned long long)native, (unsigned long long)(native + helper),
			100.0 * (double)native / (double)(native + helper));

	if (s_jit.verify)
		InfoLog("PPC: lockstep verification checked %llu block(s), %llu failed.",
			(unsigned long long)s_jit.verifyBlocks, (unsigned long long)s_jit.verifyFailures);
	if (s_jit.compileFailures != 0)
		ErrorLog("PPC: the x64 hybrid JIT could not translate %llu block(s); the last was at %08X.",
			(unsigned long long)s_jit.compileFailures, s_jit.compileFailurePc);

	DumpLiveBlocks();
}

/*
 * Platform glue for the code pool.
 *
 * x86-64 has coherent instruction and data caches, so publishing a block needs
 * no per-line flush; the Windows call is kept because it is the documented way
 * to make freshly written code visible and costs one call per translation.
 */
static bool ProtectCode(void *code, size_t size, bool writable)
{
#if defined(_WIN32)
	DWORD oldProtect = 0;
	return VirtualProtect(code, size, writable ? PAGE_READWRITE : PAGE_EXECUTE_READ, &oldProtect) != 0;
#else
	return mprotect(code, size, writable ? (PROT_READ | PROT_WRITE) : (PROT_READ | PROT_EXEC)) == 0;
#endif
}

static void FlushCode(void *code, size_t size)
{
#if defined(_WIN32)
	FlushInstructionCache(GetCurrentProcess(), code, size);
#else
	__builtin___clear_cache((char *)code, (char *)code + size);
#endif
}

static void *AllocCode(size_t size, bool *writable)
{
	void *code = NULL;

#if defined(_WIN32)
	code = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (code != NULL)
	{
		*writable = true;
		return code;
	}
	/* A policy or hardened system may refuse RWX; toggle the pool instead. */
	code = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	code = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (code != MAP_FAILED)
	{
		*writable = true;
		return code;
	}
	code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (code == MAP_FAILED)
		code = NULL;
#endif

	*writable = false;
	return code;
}

static void FreeCode(void *code, size_t size)
{
#if defined(_WIN32)
	(void)size;
	VirtualFree(code, 0, MEM_RELEASE);
#else
	munmap(code, size);
#endif
}

}	// namespace PpcJit

/******************************************************************************
 Entry points used from ppc.cpp and ppc603.c
******************************************************************************/

static void ppc_jit_init(void)
{
	PpcJit::Initialise();
}

static void ppc_jit_shutdown(void)
{
	PpcJit::Shutdown();
}

static void ppc_jit_note_fast_memory(const PPC_FAST_MEMORY *fast)
{
	PpcJit::NoteFastMemory(fast);
}

static void ppc_jit_flush_all(void)
{
	if (PpcJit::s_jit.ready)
		PpcJit::FlushAll();
}

static bool ppc_jit_active(void)
{
	return PpcJit::s_jit.ready && PpcJit::s_jit.enabled;
}

static void ppc_jit_execute(void)
{
	if (PpcJit::s_jit.ready)
		PpcJit::Execute();
}

#endif	// PPC_JIT_X64
