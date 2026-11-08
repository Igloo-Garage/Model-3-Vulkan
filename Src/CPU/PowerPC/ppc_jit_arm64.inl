/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * ppc_jit_arm64.inl
 *
 * Hybrid baseline JIT for the PowerPC 603e on AArch64 (Android).
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
 * Each block is an ordinary AAPCS64 function, `void f(void)`, so the dispatcher
 * can call it through a pointer and the compiler's own stack frame stays valid.
 *
 * Fixed register assignment inside a block (all callee-saved, so the C helpers
 * a block may call preserve them):
 *
 *   x19  &ppc                 (PPC_REGS *)
 *   x20  FastRam              base of the 8 MB of main RAM for loads
 *   x21  FastCrom             fixed CROM window, 0xFF800000
 *   x22  FastCromBank         current banked CROM window, 0xFF000000
 *   x23  FastRamWrite         same buffer as x20, for stores
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
 * The interpreter's idle (wait) loop skip is reproduced in the dispatcher: a
 * block that ends in a backward branch feeds the same state machine
 * (ppc_idle_capture/ppc_idle_unchanged), so a game polling a device flag still
 * gets its remaining slice skipped instead of spinning natively.
 *
 * Invalidation
 * ------------
 *
 * Only two regions are executable on the Model 3 - RAM below 8 MB and the fixed
 * CROM window - so the block cache has exactly two direct mapped tables. CROM
 * is read only and therefore never needs invalidating. RAM blocks are dropped
 * when: an icbi executes, a store lands in a page that contains generated code
 * (blocks are only dropped when the page really is marked, which keeps the
 * common case at four instructions per store), the state is reset or loaded, or
 * the code cache fills up.
 *
 * Verification
 * ------------
 *
 * SM3D_PPC_JIT_VERIFY=1 compiles and runs exactly the same code but brackets
 * every block with a full register state comparison against the interpreter
 * running the same instructions. Stores are logged first so that guest memory
 * can be rolled back before the reference run. See VerifyBlock().
 */

#if defined(PPC_JIT_ARM64)

#include <stddef.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include "Arm64Emitter.h"
#include "PPCDisasm.h"

#if defined(__GNUC__)
#define PPC_JIT_NOINLINE	__attribute__((noinline))
#else
#define PPC_JIT_NOINLINE
#endif

namespace PpcJit
{

using namespace Arm64;

enum
{
	kMaxBlockInsns	= 64,					/* instructions per block                 */
	kCodeCacheSize	= 16u * 1024u * 1024u,	/* machine code pool                      */
	kScratchWords	= 8192,					/* per-block emission buffer (32 KB)      */
	kRamTableSets	= 8192,					/* RAM block index, kWays entries per set */
	kCromTableSets	= 2048,					/* fixed CROM block index                 */
	kWays			= 4,						/* block cache associativity              */
	kMaxSavedPages	= 8						/* RAM pages saved per block while verifying */
};

/* Frame layout of a translated block, in bytes (see EmitPrologue). */
enum
{
	kFrameSize		= 64,
	kFrameScratchA	= 48,					/* effective address across a bus call    */
	kFrameScratchB	= 56						/* stored value across a bus call         */
};

constexpr UINT32 kRamSize		= 0x00800000;	/* must match kFastRamSize in ppc.cpp */
constexpr UINT32 kCromBase		= 0xFF800000;
constexpr UINT32 kCodePageShift	= 12;			/* 4 KB pages are tracked for SMC     */
constexpr UINT32 kCodePageCount	= kRamSize >> kCodePageShift;

/* Block::flags */
enum
{
	kFlagBackwardBranch	= 1,		/* last instruction is a backward primary 16/18 branch */
	kFlagUnverifiable	= 2			/* writes memory through a C helper, see JitSavePage */
};

/*
 * The four pointers the generated code pins. Kept in one struct so that the
 * block prologue can load all of them with two LDPs from a single literal.
 * The order matters: it is what EmitPrologue() encodes.
 */
struct CFastPins
{
	const UINT8	*ram;
	UINT8		*ramWrite;
	const UINT8	*crom;
	const UINT8	*cromBank;
};

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

	/* Statistics, reported through ppc_jit_get_stats(). */
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

	UINT32		scratch[kScratchWords];
};

static CState s_jit;
static CFastPins s_pins;
static UINT64 lastReportedExecs = 0;

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
 * Called from generated code as JitHelper(opcode | pc << 32, consumed), where
 * consumed is the number of instructions the block has executed so far. The
 * cycle count is brought up to date first because handlers that read the
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
		memcpy(s_savedPages[s_savedPageCount], FastRam + page, 4096);
		s_savedPageCount++;
	}
}

static void RestoreSavedPages(void)
{
	for (int i = 0; i < s_savedPageCount; i++)
		memcpy(FastRamWrite + s_savedPageAddress[i], s_savedPages[i], 4096);
	s_savedPageCount = 0;
}

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

static void EmitPrologue(CBuild &b)
{
	CEmitter &e = b.e;

	/*
	 * Save the callee-saved registers the pinned set uses, plus the link
	 * register: a block runs C helpers with BLR, which overwrites x30, and the
	 * value that was there is the dispatcher's return address.
	 *
	 * Note on FP/SIMD registers: under AAPCS64, d8..d15 (the low 64 bits of
	 * v8..v15) are callee-saved, whereas d0..d7 and d16..d31 are caller-saved
	 * temporaries. Generated code never saves d8..d15 here and must only use
	 * d0..d7 for floating-point scratch operations.
	 *
	 * The 64-byte frame also gives two scratch slots, used to carry the effective
	 * address and the stored value across a call into the bus helpers.
	 */
	e.StpXPre(R19, R20, SP, -kFrameSize);
	e.StpX(R21, R22, SP, 16);
	e.StpX(R23, R30, SP, 32);

	e.LdrLitX(R19, e.Literal((UINT64)(uintptr_t)&ppc));

	const int fastLit = e.Literal((UINT64)(uintptr_t)&s_pins);
	e.LdrLitX(R8, fastLit);
	e.LdpX(R20, R23, R8, 0);		/* ram, ramWrite  */
	e.LdpX(R21, R22, R8, 16);		/* crom, cromBank */
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
	e.LdrW(10, R19, (int)offsetof(PPC_REGS, icount));
	if (pending > 0 && pending <= 4095)
	{
		e.SubImmW(10, 10, pending);
	}
	else if (pending != 0)
	{
		e.MovImm32(11, (UINT32)pending);
		e.SubW(10, 10, 11);
	}
	e.StrW(10, R19, (int)offsetof(PPC_REGS, icount));

	b.restoreAt = e.Here();
	e.LdpX(R23, R30, SP, 32);
	e.LdpX(R21, R22, SP, 16);
	e.LdpXPost(R19, R20, SP, kFrameSize);
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
		{
			e.LdrW(10, R19, (int)offsetof(PPC_REGS, icount));
			if (pending <= 4095)
			{
				e.SubImmW(10, 10, pending);
			}
			else
			{
				e.MovImm32(11, (UINT32)pending);
				e.SubW(10, 10, 11);
			}
			e.StrW(10, R19, (int)offsetof(PPC_REGS, icount));
		}
		e.BTo(b.restoreAt);
	}
	b.bailFixCount = 0;
}

/* -------------------------------------------------------------------------- */
/* Register access                                                            */
/* -------------------------------------------------------------------------- */

static inline void LdGpr(CBuild &b, int w, int n)
{
	b.e.LdrW(w, R19, (int)offsetof(PPC_REGS, r) + 4 * n);
}

static inline void StGpr(CBuild &b, int n, int w)
{
	b.e.StrW(w, R19, (int)offsetof(PPC_REGS, r) + 4 * n);
}

/*
 * ppc.cr[field] = (value < 0 ? 8 : value > 0 ? 4 : 2) | (xer >> 31)
 *
 * Value must be live in a W register; w10 and w11 are clobbered.
 */
static void EmitSetCRField(CBuild &b, int wvalue, int field)
{
	CEmitter &e = b.e;

	e.CmpImmW(wvalue, 0);
	e.MovImm32(10, 0x8);
	e.MovImm32(11, 0x4);
	e.CselW(10, 11, 10, C_GT);			/* GT ? 4 : 8 */
	e.MovImm32(11, 0x2);
	e.CselW(10, 11, 10, C_EQ);			/* EQ ? 2 : previous */
	e.LdrW(11, R19, (int)offsetof(PPC_REGS, xer));
	e.LsrImmW(11, 11, 31);				/* SO -> bit 0 */
	e.OrrW(10, 10, 11);
	e.StrB(10, R19, (int)offsetof(PPC_REGS, cr) + field);
}

/* Same, with the condition flags already set by a compare. */
static void EmitCRFieldFromFlags(CBuild &b, int field, bool isSigned)
{
	CEmitter &e = b.e;

	e.MovImm32(10, 0x8);
	e.MovImm32(11, 0x4);
	e.CselW(10, 11, 10, isSigned ? C_GT : C_HI);
	e.MovImm32(11, 0x2);
	e.CselW(10, 11, 10, C_EQ);
	e.LdrW(11, R19, (int)offsetof(PPC_REGS, xer));
	e.LsrImmW(11, 11, 31);
	e.OrrW(10, 10, 11);
	e.StrB(10, R19, (int)offsetof(PPC_REGS, cr) + field);
}

static inline void EmitSetCR0(CBuild &b, int wvalue)
{
	EmitSetCRField(b, wvalue, 0);
}

/*
 * Copies the hardware C flag into XER[CA] (bit 29). ARM64's ADDS and SUBS
 * define C as "carry out" and "no borrow" respectively, which is the exact
 * PowerPC definition of CA for both additions and subtractions.
 */
static void EmitXerCAFromCarry(CBuild &b)
{
	CEmitter &e = b.e;
	e.CsetW(11, C_CS);
	e.LdrW(10, R19, (int)offsetof(PPC_REGS, xer));
	if (!e.AndImmW(10, 10, ~0x20000000u))
		e.Fail();
	e.LslImmW(11, 11, 29);
	e.OrrW(10, 10, 11);
	e.StrW(10, R19, (int)offsetof(PPC_REGS, xer));
}

/* Reloads the pinned fast-memory pointers after a call that may have moved them. */
static void EmitReloadPins(CBuild &b)
{
	CEmitter &e = b.e;
	e.LdrLitX(R8, e.Literal((UINT64)(uintptr_t)&s_pins));
	e.LdpX(R20, R23, R8, 0);
	e.LdpX(R21, R22, R8, 16);
}

/*
 * Emits a call into a C helper.
  *
  *   x0 = opcode | (pc << 32)      w1 = instructions executed before this one
  *
  * The callee subtracts w1 from ppc.icount before running the handler, which is
  * what lets a handler that reads the timebase or the decrementer
  * (mfspr/mftb/mtspr) see the right cycle. w1 is a compile time constant: the
  * number of instructions between this call and the previous one is known while
  * the block is being built, so nothing is counted at run time.
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
 		e.MovImm32(10, pc);
 		e.MovImm32(11, pc + 4);
 		e.StpW(10, 11, R19, (int)offsetof(PPC_REGS, pc));
		e.LdrLitX(R0, e.Literal((UINT64)opcode));
		e.LdrLitX(R8, e.Literal((UINT64)(uintptr_t)handler));
		e.Blr(R8);
		if (HelperMayWriteMemory(opcode))
			EmitReloadPins(b);

 		e.LdrW(10, R19, (int)offsetof(PPC_REGS, npc));
 		e.MovImm32(11, pc + 4);
 		e.CmpW(10, 11);
 		const size_t continueAt = e.BCondEmit(C_EQ);
 		JumpToBail(b, b.index + 1);
 		e.ResolveCond(continueAt, e.Here());

 		b.helperCount++;
 		if (HelperMayWriteMemory(opcode))
 			b.unverifiable = true;
 		return;
 	}

	b.syncedThrough = b.index;

	/* JitHelper(handler, opcode | pc << 32, consumed) */
	e.LdrLitX(R0, e.Literal((UINT64)(uintptr_t)handler));
	e.LdrLitX(R1, e.Literal(((UINT64)pc << 32) | (UINT64)opcode));
	e.MovImm32(2, (UINT32)consumed);
 	e.LdrLitX(R8, e.Literal((UINT64)(uintptr_t)bookkeeper));
 	e.Blr(R8);
 	if (HelperMayWriteMemory(opcode))
 		EmitReloadPins(b);

	if (!terminator)
		JumpToBailIfZero(b, 0, b.index + 1);
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
 * On entry the effective address is in w9 and, for stores, the value is in w13.
 * A load leaves its result in w13 and the effective address untouched in w9, so
 * the updating ("u") forms can write the base register back afterwards.
 *
 * The fast path is the interpreter's fast path: 8 MB of RAM addressed through
 * x20/x23 and the two CROM windows at (address >> 24) == 0xFF. Everything else
 * - device space, odd addresses and any address outside those regions - falls
 * through to the same bus call the interpreter makes.
 */
static void EmitMemAccess(CBuild &b, int kind)
{
	CEmitter &e = b.e;
	const bool store = (kind >= MK_WRITE8);
	const int ramBase = store ? R23 : R20;	/* FastRamWrite : FastRam */
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
		if (!e.TstImmW(9, (UINT32)alignMask))
			e.Fail();
		toSlow[nSlow++] = e.BCondEmit(C_NE);
	}

	e.MovImm32(10, kRamSize);
	e.CmpW(9, 10);
	size_t toHigh = e.BCondEmit(C_CS);

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
		e.LsrImmW(11, 9, (int)kCodePageShift);
		e.LdrLitX(R8, e.Literal((UINT64)(uintptr_t)&s_jit));
		e.LdrBIdx(11, R8, 11);
		const size_t noSmc = e.CbzEmit(11);
		e.MovImm32(11, 1);
		e.StrB(11, R8, (int)offsetof(CState, selfModified));
		e.ResolveCond(noSmc, e.Here());

		/* Verifying: keep a copy of the page this store is about to change. */
		if (s_jit.verify)
		{
			e.StrW(9, SP, kFrameScratchA);
			e.StrW(13, SP, kFrameScratchB);
			e.MovReg(0, 9);
			e.MovImm32(1, storeBytes(kind));
			e.LdrLitX(R8, e.Literal((UINT64)(uintptr_t)&JitSavePage));
			e.Blr(R8);
			e.LdrW(13, SP, kFrameScratchB);
			e.LdrW(9, SP, kFrameScratchA);
		}
	}

	/*
	 * RAM is byte-swapped inside each 32-bit word (see READ8/READ16 in ppc.cpp):
	 * guest byte n of a word lives at offset n ^ 3. Word accesses need no
	 * adjustment; narrower ones do.
	 */
	int ramIndex = 9;
	if (kind == MK_READ8 || kind == MK_WRITE8)
	{
		if (!e.EorImmW(11, 9, 3))
			e.Fail();
		ramIndex = 11;
	}
	else if (kind != MK_READ32 && kind != MK_WRITE32)
	{
		if (!e.EorImmW(11, 9, 2))
			e.Fail();
		ramIndex = 11;
	}

	switch (kind)
	{
		case MK_READ8:  	e.LdrBIdx(13, ramBase, ramIndex); break;
		case MK_READ16: 	e.LdrHIdx(13, ramBase, ramIndex); break;
		case MK_READ16S:	e.LdrShIdx(13, ramBase, ramIndex); break;
		case MK_READ32: 	e.LdrWIdx(13, ramBase, ramIndex); break;
		case MK_WRITE8: 	e.StrBIdx(13, ramBase, ramIndex); break;
		case MK_WRITE16:	e.StrHIdx(13, ramBase, ramIndex); break;
		default:        	e.StrWIdx(13, ramBase, ramIndex); break;
	}

	const size_t toDoneRam = e.BEmit();

	/* --- CROM windows (reads only) --------------------------------------- */
	if (!store)
	{
		e.ResolveCond(toHigh, e.Here());
		e.LsrImmW(10, 9, 24);
		e.CmpImmW(10, 0xFF);
		toSlow[nSlow++] = e.BCondEmit(C_NE);

		if (!e.TstImmW(9, 0x00800000u)) 	/* bit 23 selects the window */
			e.Fail();
		e.CselX(10, 22, 21, C_EQ);			/* bit clear: banked window */

		e.LslImmW(11, 9, 9);
		e.LsrImmW(11, 11, 9);				/* address & (kFastCromSize - 1) */
		if (kind == MK_READ8)
			e.EorImmW(11, 11, 3);
		else if (kind != MK_READ32)
			e.EorImmW(11, 11, 2);

		switch (kind)
		{
			case MK_READ8:  	e.LdrBIdx(13, 10, 11); break;
			case MK_READ16: 	e.LdrHIdx(13, 10, 11); break;
			case MK_READ16S:	e.LdrShIdx(13, 10, 11); break;
			default:        	e.LdrWIdx(13, 10, 11); break;
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
	e.StrW(9, SP, kFrameScratchA);		/* the call clobbers the address */
	e.MovImm32(10, insnPc);
	e.MovImm32(11, insnPc + 4);
	e.StpW(10, 11, R19, (int)offsetof(PPC_REGS, pc));
	e.MovReg(0, 9);
	if (store)
		e.MovReg(1, 13);
	e.LdrLitX(R8, e.Literal((UINT64)(uintptr_t)(store ? SlowWriteFn(kind) : SlowReadFn(kind))));
	e.Blr(R8);
	if (!store)
		e.MovReg(13, 0);
	e.LdrW(9, SP, kFrameScratchA);
	EmitReloadPins(b);

	/* Leave the block if the bus took an exception and moved ppc.npc. */
	e.LdrW(10, R19, (int)offsetof(PPC_REGS, npc));
	e.MovImm32(11, insnPc + 4);
	e.CmpW(10, 11);
	const size_t continueAt = e.BCondEmit(C_EQ);
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
 * Effective address into w9.
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
		LdGpr(b, 9, rb);
		if (ra != 0)
		{
			LdGpr(b, 10, ra);
			e.AddW(9, 9, 10);
		}
		return;
	}

	if (ra != 0 || update)
	{
		LdGpr(b, 9, ra);
		e.AddImmW(9, 9, disp, 12);
	}
	else
	{
		e.MovImm32(9, (UINT32)disp);
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
		LdGpr(b, 13, rt);				/* rS is the RT field of a store */

	EmitEffectiveAddress(b, ra, rb, disp, indexForm, update);
	EmitMemAccess(b, kind);

	if (!store)
		StGpr(b, rt, 13);
	if (update)
		StGpr(b, ra, 9);

	b.nativeCount++;
}

/**
 * lfs / lfsu / lfsx / lfsux / stfs / stfsu / stfsx / stfsux.
 *
 * Both directions are a 32-bit memory access plus one conversion: the guest
 * keeps a single in memory and works with doubles, and a single can be moved to
 * and from a double without touching FPSCR. The memory path is the same one the
 * integer loads use, so unaligned addresses, the CROM windows and device space
 * all behave exactly as they do in the interpreter.
 *
 * Uses caller-saved d0 (never d8..d15, which are callee-saved under AAPCS64).
 */
static void EmitLoadStoreSingle(CBuild &b, UINT32 opcode, bool store, bool indexForm, bool update)
{
	CEmitter &e = b.e;
	const int ra = (int)((opcode >> 16) & 0x1f);
	const int rb = (int)((opcode >> 11) & 0x1f);
	const int frt = (int)((opcode >> 21) & 0x1f);
	const INT32 disp = (INT32)(INT16)(opcode & 0xffff);
	const int fprOffset = (int)offsetof(PPC_REGS, fpr) + frt * 8;

	EmitEffectiveAddress(b, ra, rb, disp, indexForm, update);

	if (store)
	{
		e.LdrD(0, R19, fprOffset);
		e.FcvtDToS(0, 0);			/* f.f = (float)FPR(t).fd */
		e.FmovWFromS(13, 0);		/* the store path hands the value over in w13 */
		EmitMemAccess(b, MK_WRITE32);
	}
	else
	{
		EmitMemAccess(b, MK_READ32);
		e.FmovSFromW(0, 13);		/* w13 holds the loaded word */
		e.FcvtSToD(0, 0);			/* FPR(t).fd = (double)f.f */
		e.StrD(0, R19, fprOffset);
	}

	if (update)
		StGpr(b, ra, 9);

	b.nativeCount++;
}

/**
 * lfd / lfdu / lfdx / lfdux / stfd / stfdu / stfdx / stfdux.
 *
 * CModel3::Read64/Write64 split every 64-bit transfer into two 32-bit accesses:
 * the high 32 bits of FPR(t).id live at ea + 0 and the low 32 bits at ea + 4.
 * On little-endian AArch64, FPR(t).id has its low word at fprOffset + 0 and its
 * high word at fprOffset + 4.
 */
static void EmitLoadStoreDouble(CBuild &b, UINT32 opcode, bool store, bool indexForm, bool update)
{
	CEmitter &e = b.e;
	const int ra = (int)((opcode >> 16) & 0x1f);
	const int rb = (int)((opcode >> 11) & 0x1f);
	const int frt = (int)((opcode >> 21) & 0x1f);
	const INT32 disp = (INT32)(INT16)(opcode & 0xffff);
	const int fprOffset = (int)offsetof(PPC_REGS, fpr) + frt * 8;

	EmitEffectiveAddress(b, ra, rb, disp, indexForm, update);

	if (store)
	{
		e.LdrW(13, R19, fprOffset + 4);		/* high word: (UINT32)(FPR(t).id >> 32) */
		EmitMemAccess(b, MK_WRITE32);
		e.AddImmW(9, 9, 4, 12);
		e.LdrW(13, R19, fprOffset + 0);		/* low word:  (UINT32)FPR(t).id         */
		EmitMemAccess(b, MK_WRITE32);
	}
	else
	{
		EmitMemAccess(b, MK_READ32);
		e.StrW(13, R19, fprOffset + 4);		/* high word from ea + 0 */
		e.AddImmW(9, 9, 4, 12);
		EmitMemAccess(b, MK_READ32);
		e.StrW(13, R19, fprOffset + 0);		/* low word from ea + 4  */
	}

	if (update)
	{
		e.SubImmW(9, 9, 4);
		StGpr(b, ra, 9);
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
		e.LdrW(9, R19, (int)offsetof(PPC_REGS, ctr));
		e.AddImmW(9, 9, -1, 12);
		e.StrW(9, R19, (int)offsetof(PPC_REGS, ctr));
	}

	/* Default destination: fall through. */
	e.MovImm32(9, pc + 4);

	size_t toSkip[2];
	int nSkip = 0;

	if (!ctrAlways)
	{
		e.LdrW(10, R19, (int)offsetof(PPC_REGS, ctr));
		e.CmpImmW(10, 0);
		/* ctr_ok = (CTR != 0) ^ bo3; bo3 set means "branch when CTR == 0". */
		toSkip[nSkip++] = e.BCondEmit((bo & 0x02) ? C_NE : C_EQ);
	}

	if (!condAlways)
	{
		e.LdrB(10, R19, (int)offsetof(PPC_REGS, cr) + (bi >> 2));
		if (!e.TstImmW(10, 1u << (3 - (bi & 3))))
			e.Fail();
		/* condition_ok = CRBIT(bi) ^ (bo1 ^ 1); bo1 set inverts the test. */
		toSkip[nSkip++] = e.BCondEmit((bo & 0x08) ? C_EQ : C_NE);
	}

	/* Branch taken. */
	if (form == 0)
	{
		e.MovImm32(9, target);
	}
	else if (form == 1)					/* bclr: destination is the old LR */
	{
		e.LdrW(9, R19, (int)offsetof(PPC_REGS, lr));
		e.AndImmW(9, 9, 0xFFFFFFFCu);
	}
	else								/* bcctr: destination is CTR after the decrement */
	{
		e.LdrW(9, R19, (int)offsetof(PPC_REGS, ctr));
		e.AndImmW(9, 9, 0xFFFFFFFCu);
	}

	const size_t skipAt = e.Here();
	for (int i = 0; i < nSkip; i++)
		e.ResolveCond(toSkip[i], skipAt);

	if (lk)
	{
		e.MovImm32(10, pc + 4);
		e.StrW(10, R19, (int)offsetof(PPC_REGS, lr));
	}

	e.MovImm32(10, pc);
	e.StpW(10, 9, R19, (int)offsetof(PPC_REGS, pc));
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
			LdGpr(b, 9, ra);
			e.MovImm32(12, (UINT32)simm);
			e.SubsW(13, 12, 9);
			StGpr(b, rt, 13);
			EmitXerCAFromCarry(b);
			b.nativeCount++;
			return;

		case 12:	/* addic  */
		case 13:	/* addic. */
			LdGpr(b, 9, ra);
			e.MovImm32(12, (UINT32)simm);
			e.AddsW(13, 9, 12);
			StGpr(b, rt, 13);
			EmitXerCAFromCarry(b);
			if (prim == 13)
				EmitSetCR0(b, 13);
			b.nativeCount++;
			return;

		case 14:	/* addi */
			if (ra != 0)
			{
				LdGpr(b, 9, ra);
				e.AddImmW(9, 9, simm, 12);
			}
			else
			{
				e.MovImm32(9, (UINT32)simm);
			}
			StGpr(b, rt, 9);
			b.nativeCount++;
			return;

		case 15:	/* addis */
		{
			const UINT32 imm = uimm << 16;
			if (ra != 0)
			{
				LdGpr(b, 9, ra);
				if (imm != 0)
				{
					e.MovImm32(12, imm);
					e.AddW(9, 9, 12);
				}
			}
			else
			{
				e.MovImm32(9, imm);
			}
			StGpr(b, rt, 9);
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
			LdGpr(b, 9, rs);
			if (imm != 0)
			{
				if (!(isOr ? e.OrrImmW(9, 9, imm) : e.EorImmW(9, 9, imm)))
				{
					e.MovImm32(12, imm);
					if (isOr)
						e.OrrW(9, 9, 12);
					else
						e.EorW(9, 9, 12);
				}
			}
			StGpr(b, ra, 9);
			b.nativeCount++;
			return;
		}

		case 28:	/* andi. */
		case 29:	/* andis. */
		{
			const UINT32 imm = (prim == 29) ? (uimm << 16) : uimm;
			LdGpr(b, 9, rs);
			if (!e.AndImmW(9, 9, imm))
			{
				e.MovImm32(12, imm);
				e.AndW(9, 9, 12);
			}
			StGpr(b, ra, 9);
			EmitSetCR0(b, 9);
			b.nativeCount++;
			return;
		}

		case 7:		/* mulli: rD = rA * (UINT32)(INT16)imm, low 32 bits */
			LdGpr(b, 9, ra);
			e.MovImm32(12, (UINT32)simm);
			e.MulW(13, 9, 12);
			StGpr(b, rt, 13);
			b.nativeCount++;
			return;

		/* ---------------------------------------------------------------- */
		/* Compare                                                          */
		/* ---------------------------------------------------------------- */
		case 10:	/* cmpli */
		case 11:	/* cmpi  */
		{
			const int crfd = (int)((opcode >> 23) & 0x7);
			LdGpr(b, 9, ra);
			e.MovImm32(10, (prim == 11) ? (UINT32)simm : uimm);
			e.CmpW(9, 10);
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

			LdGpr(b, 9, rs);
			e.RorImmW(9, 9, (32 - sh) & 31);

			if (prim == 21)
			{
				if (mask != 0xFFFFFFFFu && !e.AndImmW(9, 9, mask))
				{
					e.MovImm32(12, mask);
					e.AndW(9, 9, 12);
				}
			}
			else if (mask == 0)
			{
				LdGpr(b, 9, ra);
			}
			else
			{
				if (!e.AndImmW(9, 9, mask))
				{
					e.MovImm32(12, mask);
					e.AndW(9, 9, 12);
				}
				if (mask != 0xFFFFFFFFu)
				{
					LdGpr(b, 13, ra);
					if (!e.AndImmW(13, 13, ~mask))
					{
						e.MovImm32(12, ~mask);
						e.AndW(13, 13, 12);
					}
					e.OrrW(9, 9, 13);
				}
			}

			StGpr(b, ra, 9);
			if (rc)
				EmitSetCR0(b, 9);
			b.nativeCount++;
			return;
		}

		case 23:	/* rlwnm */
		{
			const int mb = (int)((opcode >> 6) & 0x1f);
			const int me = (int)((opcode >> 1) & 0x1f);
			const UINT32 mask = ppc_rotate_mask[mb][me];

			LdGpr(b, 9, rs);
			LdGpr(b, 10, rb);
			if (!e.AndImmW(10, 10, 0x1Fu))
				e.Fail();
			e.NegW(12, 10);				/* RORV by -sh == ROL by sh */
			e.RorvW(9, 9, 12);
			if (mask != 0xFFFFFFFFu && !e.AndImmW(9, 9, mask))
			{
				e.MovImm32(12, mask);
				e.AndW(9, 9, 12);
			}
			StGpr(b, ra, 9);
			if (rc)
				EmitSetCR0(b, 9);
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
				e.MovImm32(10, pc + 4);
				e.StrW(10, R19, (int)offsetof(PPC_REGS, lr));
			}
			e.MovImm32(10, pc);
			e.MovImm32(9, target);
			e.StpW(10, 9, R19, (int)offsetof(PPC_REGS, pc));
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
					e.LdrB(9, R19, (int)offsetof(PPC_REGS, cr) + crfS);
					e.StrB(9, R19, (int)offsetof(PPC_REGS, cr) + crfD);
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
			const int fprBase = (int)offsetof(PPC_REGS, fpr);

			/*
			 * fmr / fneg / fabs / fnabs only rewrite the double, or flip bits in
			 * it; none of them touches FPSCR. The record forms are left to the C
			 * handler because they also copy the class bits into CR1.
			 *
			 * fneg and fabs are done with the FP instructions rather than an
			 * integer xor: both are defined as pure sign-bit operations, so the
			 * bit pattern - NaN payloads included - is preserved exactly.
			 * Uses caller-saved d0 (d8..d15 are callee-saved under AAPCS64).
			 */
			if (!rc && (sub == 72 || sub == 40 || sub == 264 || sub == 136))
			{
				e.LdrD(0, R19, fprBase + fb * 8);
				switch (sub)
				{
					case 40:		e.FnegD(0, 0);	break;	/* fneg  */
					case 264:		e.FabsD(0, 0);	break;	/* fabs  */
					case 136:		e.FabsD(0, 0); e.FnegD(0, 0); break; /* fnabs */
					default:								break;	/* fmr   */
				}
				e.StrD(0, R19, fprBase + rt * 8);
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
					e.LdrW(13, R19, (int)offsetof(PPC_REGS, fpr) + rt * 8);
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_WRITE32);
					b.nativeCount++;
					return;

				case 534:	/* lwbrx */
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_READ32);
					e.RevW(13, 13);
					StGpr(b, rt, 13);
					b.nativeCount++;
					return;

				case 662:	/* stwbrx */
					LdGpr(b, 13, rs);
					e.RevW(13, 13);
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_WRITE32);
					b.nativeCount++;
					return;

				case 790:	/* lhbrx */
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_READ16);
					e.Rev16W(13, 13);
					StGpr(b, rt, 13);
					b.nativeCount++;
					return;

				case 918:	/* sthbrx */
					LdGpr(b, 13, rs);
					e.Rev16W(13, 13);
					EmitEffectiveAddress(b, ra, rb, 0, true, false);
					EmitMemAccess(b, MK_WRITE16);
					b.nativeCount++;
					return;

				case 10:	/* addcx  */
				case 8:		/* subfcx */
					if (oe)
						break;
					LdGpr(b, 9, ra);
					LdGpr(b, 10, rb);
					if (sub == 10)
						e.AddsW(13, 9, 10);
					else
						e.SubsW(13, 10, 9);
					StGpr(b, rt, 13);
					EmitXerCAFromCarry(b);
					if (rc)
						EmitSetCR0(b, 13);
					b.nativeCount++;
					return;

				case 202:	/* addzex  */
				case 200:	/* subfzex */
					if (oe)
						break;
					LdGpr(b, 9, ra);
					if (sub == 200)
						e.OrnW(9, WZR, 9);
					e.LdrW(10, R19, (int)offsetof(PPC_REGS, xer));
					e.LsrImmW(10, 10, 29);
					e.AndImmW(10, 10, 1u);
					e.AddsW(13, 9, 10);
					StGpr(b, rt, 13);
					EmitXerCAFromCarry(b);
					if (rc)
						EmitSetCR0(b, 13);
					b.nativeCount++;
					return;

				case 266:	/* addx    */
				case 40:	/* subfx   */
				case 104:	/* negx    */
				case 235:	/* mullwx  */
				{
					if (oe)
						break;			/* the overflow forms go to the C handler */
					switch (sub)
					{
						case 266:
							LdGpr(b, 9, ra); LdGpr(b, 10, rb); e.AddW(13, 9, 10);
							break;
						case 40:
							LdGpr(b, 9, ra); LdGpr(b, 10, rb); e.SubW(13, 10, 9);
							break;
						case 104:
							LdGpr(b, 9, ra); e.NegW(13, 9);
							break;
						default:
							LdGpr(b, 9, ra); LdGpr(b, 10, rb); e.MulW(13, 9, 10);
							break;
					}
					StGpr(b, rt, 13);
					if (rc)
						EmitSetCR0(b, 13);
					b.nativeCount++;
					return;
				}

				case 28:	/* andx  */
				case 60:	/* andcx */
				case 124:	/* norx  */
				case 284:	/* eqvx  */
				case 316:	/* xorx  */
				case 412:	/* orcx  */
				case 444:	/* orx   */
				case 476:	/* nandx */
					LdGpr(b, 9, rs);
					LdGpr(b, 10, rb);
					switch (sub)
					{
						case 28:	e.AndW(13, 9, 10); break;
						case 60:	e.BicW(13, 9, 10); break;
						case 124:	e.OrrW(13, 9, 10); e.OrnW(13, WZR, 13); break;	/* ~(a|b) */
						case 284:	e.EorW(13, 9, 10); e.OrnW(13, WZR, 13); break;	/* ~(a^b) */
						case 316:	e.EorW(13, 9, 10); break;
						case 412:	e.OrnW(13, 9, 10); break;
						case 476:	e.AndW(13, 9, 10); e.OrnW(13, WZR, 13); break;	/* ~(a&b) */
						default:	e.OrrW(13, 9, 10); break;
					}
					StGpr(b, ra, 13);
					if (rc)
						EmitSetCR0(b, 13);
					b.nativeCount++;
					return;

				case 19:	/* mfcr */
					e.LdrB(13, R19, (int)offsetof(PPC_REGS, cr) + 0);
					for (int i = 1; i < 8; i++)
					{
						e.LslImmW(13, 13, 4);
						e.LdrB(10, R19, (int)offsetof(PPC_REGS, cr) + i);
						e.OrrW(13, 13, 10);
					}
					StGpr(b, rt, 13);
					b.nativeCount++;
					return;

				case 144:	/* mtcrf */
				{
					const int fxm = (int)((opcode >> 12) & 0xff);
					if (fxm != 0)
					{
						LdGpr(b, 9, rs);
						for (int i = 0; i < 8; i++)
						{
							if (fxm & (0x80 >> i))
							{
								const int shift = (7 - i) * 4;
								if (shift != 0)
									e.LsrImmW(10, 9, shift);
								else
									e.MovReg(10, 9);
								e.AndImmW(10, 10, 0xFu);
								e.StrB(10, R19, (int)offsetof(PPC_REGS, cr) + i);
							}
						}
					}
					b.nativeCount++;
					return;
				}

				case 954:	/* extsbx */
					LdGpr(b, 9, rs);
					e.SxtbW(13, 9);
					StGpr(b, ra, 13);
					if (rc)
						EmitSetCR0(b, 13);
					b.nativeCount++;
					return;

				case 922:	/* extshx */
					LdGpr(b, 9, rs);
					e.SxthW(13, 9);
					StGpr(b, ra, 13);
					if (rc)
						EmitSetCR0(b, 13);
					b.nativeCount++;
					return;

				case 26:	/* cntlzw */
					LdGpr(b, 9, rt);
					e.ClzW(13, 9);
					StGpr(b, ra, 13);
					if (rc)
						EmitSetCR0(b, 13);
					b.nativeCount++;
					return;

				case 0:		/* cmp  */
				case 32:	/* cmpl */
				{
					const int crfd = (int)((opcode >> 23) & 0x7);
					LdGpr(b, 9, ra);
					LdGpr(b, 10, rb);
					e.CmpW(9, 10);
					EmitCRFieldFromFlags(b, crfd, sub == 0);
					b.nativeCount++;
					return;
				}

				case 24:	/* slwx */
				case 536:	/* srwx */
					LdGpr(b, 9, rs);
					LdGpr(b, 10, rb);
					if (!e.AndImmW(10, 10, 0x3Fu))
						e.Fail();
					e.CmpImmW(10, 32);
					e.CselW(9, 9, WZR, C_CC);		/* shift >= 32 produces zero */
					if (sub == 24)
						e.LslvW(13, 9, 10);
					else
						e.LsrvW(13, 9, 10);
					StGpr(b, ra, 13);
					if (rc)
						EmitSetCR0(b, 13);
					b.nativeCount++;
					return;

				case 824:	/* srawix */
				{
					const int sh = (int)((opcode >> 11) & 0x1f);
					LdGpr(b, 9, rs);
					if (sh != 0)
					{
						e.AsrImmW(13, 9, sh);

						/*
						 * XER.CA = (rS < 0) && (the low SH bits of rS are not all
						 * zero): fold the sign into the value, then test whether
						 * shifting the residue out leaves anything.
						 */
						e.AsrImmW(10, 9, 31);
						e.AndW(10, 10, 9);
						e.LslImmW(10, 10, (32 - sh) & 31);
						e.CmpImmW(10, 0);
						e.CsetW(11, C_NE);
						e.MovImm32(12, 0x20000000u);
						e.LdrW(10, R19, (int)offsetof(PPC_REGS, xer));
						e.BicW(10, 10, 12);
						e.LslImmW(11, 11, 29);
						e.OrrW(10, 10, 11);
						e.StrW(10, R19, (int)offsetof(PPC_REGS, xer));
					}
					else
					{
						e.MovReg(13, 9);
						e.MovImm32(12, 0x20000000u);
						e.LdrW(10, R19, (int)offsetof(PPC_REGS, xer));
						e.BicW(10, 10, 12);
						e.StrW(10, R19, (int)offsetof(PPC_REGS, xer));
					}
					StGpr(b, ra, 13);
					if (rc)
						EmitSetCR0(b, 13);
					b.nativeCount++;
					return;
				}

				case 339:	/* mfspr */
				{
					const UINT32 spr = ((opcode >> 16) & 0x1f) | ((opcode >> 6) & 0x3e0);
					switch (spr)
					{
						case SPR_LR:	e.LdrW(13, R19, (int)offsetof(PPC_REGS, lr)); break;
						case SPR_CTR:	e.LdrW(13, R19, (int)offsetof(PPC_REGS, ctr)); break;
						case SPR_XER:	e.LdrW(13, R19, (int)offsetof(PPC_REGS, xer)); break;
						case SPR_SPRG0:	e.LdrW(13, R19, (int)offsetof(PPC_REGS, sprg) + 0); break;
						case SPR_SPRG1:	e.LdrW(13, R19, (int)offsetof(PPC_REGS, sprg) + 4); break;
						case SPR_SPRG2:	e.LdrW(13, R19, (int)offsetof(PPC_REGS, sprg) + 8); break;
						case SPR_SPRG3:	e.LdrW(13, R19, (int)offsetof(PPC_REGS, sprg) + 12); break;
						default:
							EmitHelperCall(b, opcode, pc, (void *)(uintptr_t)&JitHelper, false,
								!HelperNeedsCycles(opcode));
							return;
					}
					StGpr(b, rt, 13);
					b.nativeCount++;
					return;
				}

				case 467:	/* mtspr: a terminator, the SPR may be DEC or a BAT */
				{
					const UINT32 spr = ((opcode >> 16) & 0x1f) | ((opcode >> 6) & 0x3e0);
					if (spr == SPR_LR || spr == SPR_CTR || spr == SPR_XER ||
						(spr >= SPR_SPRG0 && spr <= SPR_SPRG3))
					{
						LdGpr(b, 13, rs);
						switch (spr)
						{
							case SPR_LR:	e.StrW(13, R19, (int)offsetof(PPC_REGS, lr)); break;
							case SPR_CTR:	e.StrW(13, R19, (int)offsetof(PPC_REGS, ctr)); break;
							case SPR_XER:	e.StrW(13, R19, (int)offsetof(PPC_REGS, xer)); break;
							default:		e.StrW(13, R19, (int)offsetof(PPC_REGS, sprg) + 4 * (int)(spr - SPR_SPRG0)); break;
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
	b.e.Reset(s_jit.scratch, kScratchWords);
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
		b.e.MovImm32(10, lastPc);
		b.e.MovImm32(9, lastPc + 4);
		b.e.StpW(10, 9, R19, (int)offsetof(PPC_REGS, pc));
	}

	EmitEpilogue(b);
	b.e.Finalize();

	if (!b.e.Ok())
		return false;

	/*
	 * A zero word inside the code region means the emitter produced a hole: an
	 * instruction was counted but never written. Executing one is a SIGILL in the
	 * middle of a block, so the block is refused instead - the dispatcher then
	 * leaves the rest of the slice to the interpreter, which is slow but correct.
	 */
	for (size_t i = 0; i < b.e.PoolStart(); i++)
	{
		if (s_jit.scratch[i] == 0)
		{
			static UINT32 loggedHoles = 0;
			if (++loggedHoles <= 8)
				ErrorLog("PPC JIT: block at %08X has a hole at word %u of %u; leaving it to the interpreter.",
					entryPc, (unsigned)i, (unsigned)b.e.PoolStart());
			return false;
		}
	}

	const size_t bytes = b.e.Size() * 4;
	if (s_jit.codeUsed + bytes + 16 > s_jit.codeSize)
	{
		FlushAll();
		if (s_jit.codeUsed + bytes + 16 > s_jit.codeSize)
			return false;
	}

	if (!s_jit.codeWritable &&
		mprotect(s_jit.code, s_jit.codeSize, PROT_READ | PROT_WRITE) != 0)
		return false;

	UINT8 *dest = s_jit.code + s_jit.codeUsed;
	memcpy(dest, s_jit.scratch, bytes);
	s_jit.codeUsed += (bytes + 15) & ~(size_t)15;

	if (!s_jit.codeWritable &&
		mprotect(s_jit.code, s_jit.codeSize, PROT_READ | PROT_EXEC) != 0)
		return false;

	__builtin___clear_cache((char *)dest, (char *)dest + bytes);

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
	if (!s_jit.initialised)
		return;

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
	 * The generated code, one line per four instructions. The block starts on a
	 * 16-byte boundary, so the addresses are known: this can be pasted into a
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
 * over the decrementer trigger. The generated code keeps this in a register and
 * decrements it as it chains from block to block, which is what lets a chained
 * jump stay inside the exact cycle budget the interpreter would have used.
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
		if (s_pins.ram == NULL)
			return;

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
			return;

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
		 * Idle (wait) loop detection, mirroring ppc_execute(). A loop that
		 * cannot observe a change within this slice cannot leave it, so once
		 * its register state repeats the rest of the slice can be skipped -
		 * stopping one instruction short of any decrementer trigger so that
		 * interrupt still arrives on exactly the right cycle.
		 */
		if (block->flags & kFlagBackwardBranch)
		{
			if (ppc.npc <= block->branchPc)
			{
				if (idleCooldown > 0)
				{
					--idleCooldown;
				}
				else if (ppc.npc == idleTarget)
				{
					if (++idleHits == kIdleSnapshotAt)
					{
						ppc_idle_capture(&idleState);
					}
					else if (idleHits >= kIdleCompareAt)
					{
						if (IdleSkipEnabled && ppc_idle_unchanged(&idleState))
						{
							s_jit.idleSkips++;
							const int trigger = ppc.dec_trigger_cycle;
							if (trigger < 0 || trigger >= ppc.icount)
							{
								ppc.icount = 0;
								return;
							}
							ppc.icount = trigger + 1;
							idleHits = 0;
						}
						else
						{
							idleHits = 0;
							idleCooldown = kIdleCooldown;
						}
					}
				}
				else
				{
					idleTarget = ppc.npc;
					idleHits = 1;
				}
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
 * Reads a diagnostic switch into "out".
 *
 * The environment variable is what the desktop builds use; an Android app is
 * started by the activity manager and cannot be given one, so the same switch
 * can also be set as a "debug." system property, which `adb shell setprop`
 * can write without root:
 *
 *   adb shell setprop debug.sm3d.ppc_jit_verify 1
 *
 * An empty result means "not set".
 */
static void ReadSetting(const char *envName, const char *propName, char *out, size_t outSize)
{
	out[0] = '\0';
	const char *value = getenv(envName);

#if defined(__ANDROID__)
	char prop[PROP_VALUE_MAX] = { 0 };
	if ((value == NULL || value[0] == '\0') && __system_property_get(propName, prop) > 0)
		value = prop;
#else
	(void)propName;
#endif

	if (value != NULL)
		snprintf(out, outSize, "%s", value);
}

static void Shutdown(void)
{
	if (s_jit.code != NULL)
	{
		munmap(s_jit.code, s_jit.codeSize);
		s_jit.code = NULL;
	}
	s_jit.ready = false;
}

static bool Initialise(void)
{
	if (s_jit.initialised)
		return s_jit.ready;
	s_jit.initialised = true;

	char jitValue[32];
	char verifyValue[32];
	ReadSetting("SM3D_PPC_JIT", "debug.sm3d.ppc_jit", jitValue, sizeof(jitValue));
	ReadSetting("SM3D_PPC_JIT_VERIFY", "debug.sm3d.ppc_jit_verify", verifyValue, sizeof(verifyValue));

	/*
	 * Enabled by default on AArch64; set SM3D_PPC_JIT=0 (or debug.sm3d.ppc_jit=0)
	 * to force the pure interpreter for A/B comparisons.
	 */
	s_jit.enabled = (jitValue[0] != '0');
	s_jit.verify = (verifyValue[0] == '1');

	/*
	 * The idle-loop skip is armed in ppc_init() from SM3D_IDLE_SKIP before this
	 * runs; honour the Android property form of the same switch so a game that
	 * turns out to depend on the spinning can be checked on the device.
	 */
	char idleValue[32];
	ReadSetting("SM3D_IDLE_SKIP", "debug.sm3d.idle_skip", idleValue, sizeof(idleValue));
	if (idleValue[0] == '0')
		ppc_set_idle_loop_skip(false);

	if (!s_jit.enabled)
	{
		InfoLog("PPC: ARM64 hybrid JIT disabled (SM3D_PPC_JIT=0).");
		return false;
	}

	/*
	 * One RWX mapping is what every mobile JIT would like; Android allows it
	 * for app processes, but if a policy or a hardened kernel refuses, fall
	 * back to toggling the pool between RW (while writing a block) and RX
	 * (the rest of the time).
	 */
	void *code = mmap(NULL, kCodeCacheSize, PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (code != MAP_FAILED)
	{
		s_jit.codeWritable = true;
	}
	else
	{
		code = mmap(NULL, kCodeCacheSize, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (code == MAP_FAILED)
		{
			ErrorLog("PPC: ARM64 hybrid JIT could not allocate %u MB of code cache; using the interpreter.",
				(unsigned)(kCodeCacheSize >> 20));
			return false;
		}
		s_jit.codeWritable = false;
	}

	s_jit.code = (UINT8 *)code;
	s_jit.codeSize = kCodeCacheSize;
	s_jit.codeUsed = 0;
	s_jit.ramGen = 1;
	s_jit.cromGen = 1;
	s_jit.ready = true;

	InfoLog("PPC: ARM64 hybrid JIT enabled (%u MB %s code cache%s).",
		(unsigned)(kCodeCacheSize >> 20), s_jit.codeWritable ? "RWX" : "W^X",
		s_jit.verify ? ", lockstep verification on" : "");
	return true;
}

static void GetStats(PPC_JIT_STATS *stats)
{
	stats->blocksCompiled = s_jit.blocksCompiled;
	stats->blockExecutions = s_jit.blockExecs;
	stats->helperCalls = s_jit.helperCalls;
	stats->smcFlushes = s_jit.smcFlushes;
	stats->codeBytes = (UINT64)s_jit.codeUsed;
	stats->verifyBlocks = s_jit.verifyBlocks;
	stats->verifyFailures = s_jit.verifyFailures;
	stats->compileFailures = s_jit.compileFailures;
	stats->compileFailurePc = s_jit.compileFailurePc;
	stats->shortBlockBails = s_jit.shortBlockBails;
	stats->currentPc = ppc.pc;
	stats->currentNpc = ppc.npc;
	stats->icountRemaining = (UINT32)ppc.icount;
	stats->msr = ppc.msr;
	stats->interruptPending = (UINT32)ppc.interrupt_pending;
	stats->exceptionEntries = s_jit.exceptionEntries;
	stats->idleSkips = s_jit.idleSkips;

	/*
	 * Native/helper coverage is a property of the translated code, not of the
	 * last interval: most executions now happen inside a chain of blocks and
	 * never return to the dispatcher, so counting them per execution would only
	 * measure the blocks that could not be chained. Summing the live blocks
	 * answers the question that matters - how much of the guest code has a
	 * native translation.
	 */
	{
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
		stats->nativeInstructions = native;
		stats->helperInstructions = helper;
	}

	/*
	 * The four most executed blocks since the last report. A game that has
	 * stopped making progress sits in one, and the address says which wait loop
	 * it is - the same information a PC sample would give, but counted.
	 */
	for (int i = 0; i < 4; i++)
	{
		stats->hotPc[i] = 0;
		stats->hotExecs[i] = 0;
	}

	const CBlock *tables[2] = { s_jit.ramTable, s_jit.cromTable };
	const int counts[2] = { kRamTableSets * kWays, kCromTableSets * kWays };
	for (int t = 0; t < 2; t++)
	{
		for (int i = 0; i < counts[t]; i++)
		{
			const CBlock &b = tables[t][i];
			if (b.fn == NULL || b.execs == 0)
				continue;
			for (int slot = 0; slot < 4; slot++)
			{
				if (b.execs > stats->hotExecs[slot])
				{
					for (int j = 3; j > slot; j--)
					{
						stats->hotExecs[j] = stats->hotExecs[j - 1];
						stats->hotPc[j] = stats->hotPc[j - 1];
					}
					stats->hotExecs[slot] = b.execs;
					stats->hotPc[slot] = b.pc;
					break;
				}
			}
		}
	}

	/* Reset the window so the next report describes the next interval. */
	for (int t = 0; t < 2; t++)
		for (int i = 0; i < counts[t]; i++)
			((CBlock *)tables[t])[i].execs = 0;

	/*
	 * If one block dominates, the machine is sitting in a wait loop. Dump it the
	 * first time that happens for a given address: the instructions say what the
	 * loop is waiting for, which is the only useful thing a log can offer when a
	 * game stops making progress.
	 */
	static UINT32 lastDumpedPc = 0;
	const UINT64 windowTotal = s_jit.blockExecs - lastReportedExecs;
	lastReportedExecs = s_jit.blockExecs;
	if (stats->hotPc[0] != 0 && stats->hotPc[0] != lastDumpedPc &&
		stats->hotExecs[0] * 2 > windowTotal && windowTotal > 1000)
	{
		lastDumpedPc = stats->hotPc[0];
		UINT32 regionEnd = 0;
		const UINT32 *hostOp = HostOpForPc(stats->hotPc[0], &regionEnd);
		if (hostOp != NULL)
		{
			ErrorLog("PPC JIT: %08X accounts for %llu of %llu block executions; it looks like a wait loop:",
				stats->hotPc[0], (unsigned long long)stats->hotExecs[0], (unsigned long long)windowTotal);
			for (int i = -4; i < 14; i++)
			{
				const UINT32 insnPc = stats->hotPc[0] + (UINT32)(i * 4);
				if (i < 0 && stats->hotPc[0] < 16u)
					continue;
				if (insnPc >= regionEnd)
					break;
				char mnem[64];
				char oprs[128];
				mnem[0] = oprs[0] = '\0';
				UINT32 regionEndForPc = regionEnd;
				const UINT32 *insn = HostOpForPc(insnPc, &regionEndForPc);
				if (insn == NULL)
					continue;
				DisassemblePowerPC(*insn, insnPc, mnem, oprs, sizeof(oprs), true);
				ErrorLog("  %08X: %08X  %s %s", insnPc, *insn, mnem, oprs);
				if (i >= 0 && IsTerminator(*insn))
					break;
			}

			char regs[64];
			for (int i = 0; i < 32; i += 4)
			{
				snprintf(regs, sizeof(regs), "  r%-2d %08X %08X %08X %08X",
					i, ppc.r[i], ppc.r[i + 1], ppc.r[i + 2], ppc.r[i + 3]);
				ErrorLog("%s", regs);
			}
			ErrorLog("  lr %08X ctr %08X xer %08X msr %08X srr0 %08X srr1 %08X pc %08X npc %08X",
				ppc.lr, ppc.ctr, ppc.xer, ppc.msr, ppc.srr0, ppc.srr1, ppc.pc, ppc.npc);

			/* The machine code of the block itself. */
			CBlock *hot = NULL;
			for (int t = 0; t < 2 && hot == NULL; t++)
			{
			const CBlock *table = (t == 0) ? s_jit.ramTable : s_jit.cromTable;
				const int entries = (t == 0) ? kRamTableSets * kWays : kCromTableSets * kWays;
				for (int i = 0; i < entries; i++)
				{
					if (table[i].fn != NULL && table[i].pc == stats->hotPc[0])
					{
						hot = (CBlock *)&table[i];
						break;
					}
				}
			}
			if (hot != NULL)
			{
				const UINT32 *code = (const UINT32 *)(const void *)hot->fn;
				const int words = (int)(hot->codeBytes / 4);
				ErrorLog("  block has %d instruction(s), %d native, %d helper, %d code word(s):",
					hot->insnCount, hot->nativeCount, hot->helperCount, words);
				for (int i = 0; i < words; i += 4)
				{
					snprintf(regs, sizeof(regs), "    %08X %08X %08X %08X",
						code[i], i + 1 < words ? code[i + 1] : 0,
						i + 2 < words ? code[i + 2] : 0, i + 3 < words ? code[i + 3] : 0);
					ErrorLog("%s", regs);
				}
			}
		}
	}
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

static void ppc_jit_execute(void)
{
	if (PpcJit::s_jit.ready)
		PpcJit::Execute();
}

#endif	// PPC_JIT_ARM64
