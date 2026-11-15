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
 * Arm64Emitter.h
 *
 * Minimal AArch64 machine code encoder for the PowerPC JIT.
 *
 * This is not a general purpose assembler: it emits exactly the handful of
 * instruction forms the PowerPC -> ARM64 translation needs, and it emits them
 * into a caller supplied buffer. Everything is little-endian AArch64 (A64)
 * instructions, no pseudo-instructions that expand to more than one word
 * unless the name says so (MovImm32/MovImm64).
 *
 * Two facilities make the generated code practical:
 *
 *  - A literal pool. A block appends up to kMaxLiterals 64-bit constants which
 *    are emitted after the code and addressed with a single LDR (literal)
 *    each, so loading the address of a C helper costs one instruction instead
 *    of four MOVZ/MOVK pairs.
 *
 *  - Forward branch fixups. Emit a branch, keep the returned position and
 *    resolve it once the target has been emitted (ResolveBranch).
 *
 * Any encoding that does not fit (immediate out of range, logical immediate
 * that cannot be represented, ...) sets Ok() to false rather than emitting
 * something subtly different; the caller then abandons the block and lets the
 * interpreter run it. A silent mis-encode would be far more expensive to find
 * than a missing optimisation.
 *
 * Only compiled for aarch64 (see ppc_jit_arm64.inl).
 */

#ifndef INCLUDED_ARM64_EMITTER_H
#define INCLUDED_ARM64_EMITTER_H

/*
 * The host encoding check (Tools/jit_emit_check.cpp) compiles this header on
 * x86 so that every emitted word can be compared against llvm-mc. Nothing in
 * here is architecture specific; only the users of it are.
 */
#if defined(__aarch64__) || defined(SUPERMODEL_ARM64_EMITTER_HOST_TEST)

#include "Types.h"

namespace Arm64
{

/*
 * Condition codes, as encoded in B.cond/CSEL/...
 */
enum Condition
{
	C_EQ = 0x0, C_NE = 0x1,
	C_CS = 0x2, C_CC = 0x3,
	C_MI = 0x4, C_PL = 0x5,
	C_VS = 0x6, C_VC = 0x7,
	C_HI = 0x8, C_LS = 0x9,
	C_GE = 0xa, C_LT = 0xb,
	C_GT = 0xc, C_LE = 0xd,
	C_AL = 0xe, C_NV = 0xf
};

/*
 * Register numbers. Wn and Xn share the number; the width is implied by the
 * method used. 31 is WZR/XZR in most contexts and SP in the addressing forms
 * that allow it.
 *
 * The names deliberately avoid LR/CTR/XER and friends: ppc.cpp #defines those
 * to PPC_REGS members, and this header is compiled inside that translation
 * unit.
 */
enum Register
{
	R0 = 0, R1, R2, R3, R4, R5, R6, R7,
	R8, R9, R10, R11, R12, R13, R14, R15,
	R16, R17, R18, R19, R20, R21, R22, R23,
	R24, R25, R26, R27, R28, R29, R30, R31,
	WZR = 31, XZR = 31, SP = 31
};

class CEmitter
{
public:
	CEmitter(void)
	{
		Reset(NULL, 0);
	}

	void Reset(UINT32 *buffer, size_t capacity)
	{
		m_buffer = buffer;
		m_capacity = capacity;
		m_size = 0;
		m_poolStart = 0;
		m_literalCount = 0;
		m_fixupCount = 0;
		m_ok = (buffer != NULL && capacity > 0);
	}

	bool Ok(void) const { return m_ok; }
	size_t Size(void) const { return m_size; }
	size_t Here(void) const { return m_size; }
	size_t PoolStart(void) const { return m_poolStart; }
	UINT32 Word(size_t i) const { return (m_buffer != NULL && i < m_size) ? m_buffer[i] : 0xFFFFFFFFu; }
	void Fail(void) { m_ok = false; }

	/*
	 * Appends a 64-bit constant to the literal pool and returns its index.
	 * Identical constants are shared, so the several "call this helper" sites
	 * of a block that all call the same function cost one pool entry.
	 */
	int Literal(UINT64 value)
	{
		for (int i = 0; i < m_literalCount; i++)
		{
			if (m_literals[i] == value)
				return i;
		}
		if (m_literalCount >= kMaxLiterals)
		{
			m_ok = false;
			return 0;
		}
		m_literals[m_literalCount] = value;
		return m_literalCount++;
	}

	/*
	 * Emits a load of literal pool entry "literal" (LDR literal, PC relative
	 * within +-1 MB - the pool sits at the end of the same block).
	 */
	void LdrLitX(int rt, int literal)
	{
		AddFixup(0x58000000u | (UINT32)rt, kFixLiteral, literal);
	}

	void LdrLitW(int rt, int literal)
	{
		AddFixup(0x18000000u | (UINT32)rt, kFixLiteral, literal);
	}

	/*
	 * Resolves every pending literal load and branch, then appends the pool.
	 * Must be called exactly once, after the last instruction of the block.
	 */
	void Finalize(void)
	{
		if (!m_ok)
			return;

		while (m_size & 1)		// 8-byte align the pool
			Emit(0xD503201Fu);	// NOP

		const size_t poolStart = m_size;
		m_poolStart = m_size;
		for (int i = 0; i < m_fixupCount && m_ok; i++)
		{
			const Fixup &f = m_fixups[i];
			if (f.kind == kFixBranch)
			{
				PatchBranch(f.at, (INT64)f.target);
			}
			else if (f.kind == kFixCond)
			{
				PatchCond(f.at, (INT64)f.target);
			}
			else
			{
				const INT64 target = (INT64)(poolStart + 2u * (size_t)f.target);
				PatchCond(f.at, target);
			}
		}

		for (int i = 0; i < m_literalCount; i++)
		{
			Emit((UINT32)(m_literals[i] & 0xFFFFFFFFu));
			Emit((UINT32)(m_literals[i] >> 32));
		}
	}

	/*
	 * Branches. The returned position is passed to ResolveBranch/ResolveCond
	 * once the destination is known; both patch in place and never move code.
	 */
	size_t BEmit(void)
	{
		const size_t at = m_size;
		Emit(0x14000000u);
		return at;
	}

	size_t BCondEmit(int cond)
	{
		const size_t at = m_size;
		Emit(0x54000000u | (UINT32)(cond & 0xF));
		return at;
	}

	size_t CbzEmit(int rt)
	{
		const size_t at = m_size;
		Emit(0x34000000u | (UINT32)(rt & 0x1F));
		return at;
	}

	size_t CbnzEmit(int rt)
	{
		const size_t at = m_size;
		Emit(0x35000000u | (UINT32)(rt & 0x1F));
		return at;
	}

	void ResolveBranch(size_t at, size_t target)
	{
		AddFixupResolved(at, kFixBranch, target);
	}

	void ResolveCond(size_t at, size_t target)
	{
		AddFixupResolved(at, kFixCond, target);
	}

	/* Emits B <target> for an already emitted target. */
	void BTo(size_t target)
	{
		ResolveBranch(BEmit(), target);
	}

	void BCondTo(int cond, size_t target)
	{
		ResolveCond(BCondEmit(cond), target);
	}

	/* ------------------------------------------------------------------ */
	/* Moves                                                              */
	/* ------------------------------------------------------------------ */

	void MovReg(int rd, int rm)					// Wd = Wm
	{
		Emit(0x2A0003E0u | ((UINT32)rm << 16) | (UINT32)rd);
	}

	void MovXReg(int rd, int rm)				// Xd = Xm
	{
		Emit(0xAA0003E0u | ((UINT32)rm << 16) | (UINT32)rd);
	}

	void MovImm32(int rd, UINT32 imm)
	{
		if ((imm >> 16) == 0)
		{
			Emit(0x52800000u | ((UINT32)(imm & 0xFFFF) << 5) | (UINT32)rd);
		}
		else if ((imm & 0xFFFF) == 0)
		{
			Emit(0x52800000u | (1u << 21) | ((UINT32)((imm >> 16) & 0xFFFF) << 5) | (UINT32)rd);
		}
		else
		{
			Emit(0x52800000u | ((UINT32)(imm & 0xFFFF) << 5) | (UINT32)rd);
			Emit(0x72800000u | (1u << 21) | ((UINT32)((imm >> 16) & 0xFFFF) << 5) | (UINT32)rd);
		}
	}

	void MovImm64(int rd, UINT64 imm)
	{
		bool any = false;
		for (int hw = 0; hw < 4; hw++)
		{
			const UINT32 chunk = (UINT32)((imm >> (16 * hw)) & 0xFFFF);
			if (chunk == 0)
				continue;
			const UINT32 op = any ? 0xF2800000u : 0xD2800000u;	// MOVK : MOVZ
			Emit(op | ((UINT32)hw << 21) | (chunk << 5) | (UINT32)rd);
			any = true;
		}
		if (!any)
			Emit(0xD2800000u | (UINT32)rd);						// MOVZ Xd, #0
	}

	/* ------------------------------------------------------------------ */
	/* Arithmetic and logic                                               */
	/* ------------------------------------------------------------------ */

	void AddW(int rd, int rn, int rm) { EmitShiftedReg(0x0B000000u, rd, rn, rm); }
	void AddsW(int rd, int rn, int rm) { EmitShiftedReg(0x2B000000u, rd, rn, rm); }
	void SubW(int rd, int rn, int rm) { EmitShiftedReg(0x4B000000u, rd, rn, rm); }
	void SubsW(int rd, int rn, int rm) { EmitShiftedReg(0x6B000000u, rd, rn, rm); }
	void AndW(int rd, int rn, int rm) { EmitShiftedReg(0x0A000000u, rd, rn, rm); }
	void OrrW(int rd, int rn, int rm) { EmitShiftedReg(0x2A000000u, rd, rn, rm); }
	void EorW(int rd, int rn, int rm) { EmitShiftedReg(0x4A000000u, rd, rn, rm); }
	void BicW(int rd, int rn, int rm) { EmitShiftedReg(0x0A200000u, rd, rn, rm); }
	void OrnW(int rd, int rn, int rm) { EmitShiftedReg(0x2A200000u, rd, rn, rm); }

	void AndsW(int rd, int rn, int rm) { EmitShiftedReg(0x6A000000u, rd, rn, rm); }

	void NegW(int rd, int rm) { Emit(0x4B0003E0u | ((UINT32)rm << 16) | (UINT32)rd); }

	void MulW(int rd, int rn, int rm)
	{
		Emit(0x1B000000u | ((UINT32)rm << 16) | ((UINT32)WZR << 10) | ((UINT32)rn << 5) | (UINT32)rd);
	}

	void SxtbW(int rd, int rn) { Emit(0x13000000u | (0u << 16) | (7u << 10) | ((UINT32)rn << 5) | (UINT32)rd); }
	void SxthW(int rd, int rn) { Emit(0x13000000u | (0u << 16) | (15u << 10) | ((UINT32)rn << 5) | (UINT32)rd); }
	void ClzW(int rd, int rn) { Emit(0x5AC01000u | ((UINT32)rn << 5) | (UINT32)rd); }
	void Rev16W(int rd, int rn) { Emit(0x5AC00400u | ((UINT32)rn << 5) | (UINT32)rd); }
	void RevW(int rd, int rn) { Emit(0x5AC00800u | ((UINT32)rn << 5) | (UINT32)rd); }

	/*
	 * Immediate forms of ADD/SUB. Anything that does not fit a 12-bit unsigned
	 * field is built in the scratch register the caller supplies.
	 */
	void AddImmW(int rd, int rn, INT32 imm, int scratch)
	{
		if (imm == 0)
		{
			if (rd != rn)
				MovReg(rd, rn);
			return;
		}
		if (imm > 0 && imm <= 4095)
		{
			Emit(0x11000000u | ((UINT32)imm << 10) | ((UINT32)rn << 5) | (UINT32)rd);
			return;
		}
		if (imm < 0 && -imm <= 4095)
		{
			Emit(0x51000000u | ((UINT32)(-imm) << 10) | ((UINT32)rn << 5) | (UINT32)rd);
			return;
		}
		MovImm32(scratch, (UINT32)imm);
		AddW(rd, rn, scratch);
	}

	/* Rd = Rn - imm, with imm in [0, 4095]. */
	void SubImmW(int rd, int rn, UINT32 imm)
	{
		if (imm > 4095)
		{
			m_ok = false;
			return;
		}
		Emit(0x51000000u | (imm << 10) | ((UINT32)rn << 5) | (UINT32)rd);
	}

	/*
	 * Logical immediates. These return false when the constant cannot be
	 * encoded, and the caller falls back to MOV + register form.
	 */
	bool AndImmW(int rd, int rn, UINT32 imm)
	{
		UINT32 n, immr, imms;
		if (!EncodeLogicalImm32(imm, &n, &immr, &imms))
			return false;
		Emit(0x12000000u | (n << 22) | (immr << 16) | (imms << 10) | ((UINT32)rn << 5) | (UINT32)rd);
		return true;
	}

	bool OrrImmW(int rd, int rn, UINT32 imm)
	{
		UINT32 n, immr, imms;
		if (!EncodeLogicalImm32(imm, &n, &immr, &imms))
			return false;
		Emit(0x32000000u | (n << 22) | (immr << 16) | (imms << 10) | ((UINT32)rn << 5) | (UINT32)rd);
		return true;
	}

	bool EorImmW(int rd, int rn, UINT32 imm)
	{
		UINT32 n, immr, imms;
		if (!EncodeLogicalImm32(imm, &n, &immr, &imms))
			return false;
		Emit(0x52000000u | (n << 22) | (immr << 16) | (imms << 10) | ((UINT32)rn << 5) | (UINT32)rd);
		return true;
	}

	bool AndsImmW(int rd, int rn, UINT32 imm)
	{
		UINT32 n, immr, imms;
		if (!EncodeLogicalImm32(imm, &n, &immr, &imms))
			return false;
		Emit(0x72000000u | (n << 22) | (immr << 16) | (imms << 10) | ((UINT32)rn << 5) | (UINT32)rd);
		return true;
	}

	/* ------------------------------------------------------------------ */
	/* Compare / test                                                     */
	/* ------------------------------------------------------------------ */

	void CmpW(int rn, int rm)
	{
		Emit(0x6B00001Fu | ((UINT32)rm << 16) | ((UINT32)rn << 5));
	}

	void CmpImmW(int rn, UINT32 imm)		// imm must fit 12 bits
	{
		if (imm > 4095)
		{
			m_ok = false;
			return;
		}
		Emit(0x7100001Fu | (imm << 10) | ((UINT32)rn << 5));
	}

	bool TstImmW(int rn, UINT32 imm)
	{
		UINT32 n, immr, imms;
		if (!EncodeLogicalImm32(imm, &n, &immr, &imms))
			return false;
		Emit(0x7200001Fu | (n << 22) | (immr << 16) | (imms << 10) | ((UINT32)rn << 5));
		return true;
	}

	/* ------------------------------------------------------------------ */
	/* Shifts                                                             */
	/* ------------------------------------------------------------------ */

	void LslImmW(int rd, int rn, int sh)
	{
		sh &= 31;
		Emit(0x53000000u | ((UINT32)((32 - sh) & 31) << 16) | ((UINT32)(31 - sh) << 10) | ((UINT32)rn << 5) | (UINT32)rd);
	}

	void LsrImmW(int rd, int rn, int sh)
	{
		sh &= 31;
		Emit(0x53000000u | ((UINT32)sh << 16) | (31u << 10) | ((UINT32)rn << 5) | (UINT32)rd);
	}

	void AsrImmW(int rd, int rn, int sh)
	{
		sh &= 31;
		Emit(0x13000000u | ((UINT32)sh << 16) | (31u << 10) | ((UINT32)rn << 5) | (UINT32)rd);
	}

	/*
	 * AArch64 has no flag-setting shift alias: LSLS does not exist, and the
	 * bitfield encoding with the S bit set is undefined (llvm-objdump prints it
	 * as <unknown>). Anything that needs flags after a shift has to shift and
	 * then compare, which is what the JIT does.
	 */
	void RorImmW(int rd, int rn, int sh)
	{
		sh &= 31;
		Emit(0x13800000u | ((UINT32)rn << 16) | ((UINT32)sh << 10) | ((UINT32)rn << 5) | (UINT32)rd);
	}

	void LslvW(int rd, int rn, int rm) { Emit(0x1AC02000u | ((UINT32)rm << 16) | ((UINT32)rn << 5) | (UINT32)rd); }
	void LsrvW(int rd, int rn, int rm) { Emit(0x1AC02400u | ((UINT32)rm << 16) | ((UINT32)rn << 5) | (UINT32)rd); }
	void AsrvW(int rd, int rn, int rm) { Emit(0x1AC02800u | ((UINT32)rm << 16) | ((UINT32)rn << 5) | (UINT32)rd); }
	void RorvW(int rd, int rn, int rm) { Emit(0x1AC02C00u | ((UINT32)rm << 16) | ((UINT32)rn << 5) | (UINT32)rd); }

	/* ------------------------------------------------------------------ */
	/* Conditional select                                                 */
	/* ------------------------------------------------------------------ */

	void CselW(int rd, int rn, int rm, int cond)
	{
		Emit(0x1A800000u | ((UINT32)rm << 16) | ((UINT32)(cond & 0xF) << 12) | ((UINT32)rn << 5) | (UINT32)rd);
	}

	void CselX(int rd, int rn, int rm, int cond)
	{
		Emit(0x9A800000u | ((UINT32)rm << 16) | ((UINT32)(cond & 0xF) << 12) | ((UINT32)rn << 5) | (UINT32)rd);
	}

	void CsetW(int rd, int cond)
	{
		// CSINC Wd, WZR, WZR, invert(cond)
		const int inv = (cond ^ 1) & 0xF;
		Emit(0x1A800400u | ((UINT32)WZR << 16) | ((UINT32)inv << 12) | ((UINT32)WZR << 5) | (UINT32)rd);
	}

	/* ------------------------------------------------------------------ */
	/* Floating point, only the conversions and sign operations the guest  */
	/* load/store forms need.                                              */
	/* ------------------------------------------------------------------ */

	void FcvtSToD(int rd, int rn) { Emit(0x1E22C000u | ((UINT32)rn << 5) | (UINT32)rd); }
	void FcvtDToS(int rd, int rn) { Emit(0x1E624000u | ((UINT32)rn << 5) | (UINT32)rd); }
	void FabsD(int rd, int rn) { Emit(0x1E60C000u | ((UINT32)rn << 5) | (UINT32)rd); }
	void FnegD(int rd, int rn) { Emit(0x1E614000u | ((UINT32)rn << 5) | (UINT32)rd); }

	/*
	 * AArch64 keeps the integer and the FP/SIMD register files apart: FCVT
	 * writes Vd, not Xd, so a value that has been converted has to be moved
	 * with an FP load/store or FMOV. Getting this wrong is silent - the
	 * integer register simply keeps the unconverted bits.
	 */
	void LdrD(int ft, int rn, int off) { EmitUnsignedOffset(0xFD400000u, ft, rn, off, 8); }
	void StrD(int ft, int rn, int off) { EmitUnsignedOffset(0xFD000000u, ft, rn, off, 8); }
	void FmovWFromS(int rd, int rn) { Emit(0x1E260000u | ((UINT32)rn << 5) | (UINT32)rd); }
	void FmovSFromW(int rd, int rn) { Emit(0x1E270000u | ((UINT32)rn << 5) | (UINT32)rd); }
	void FmovXFromD(int rd, int rn) { Emit(0x9E660000u | ((UINT32)rn << 5) | (UINT32)rd); }
	void FmovDFromX(int rd, int rn) { Emit(0x9E670000u | ((UINT32)rn << 5) | (UINT32)rd); }

	/* ------------------------------------------------------------------ */
	/* Memory                                                             */
	/* ------------------------------------------------------------------ */

	void LdrW(int rt, int rn, int off)	{ EmitUnsignedOffset(0xB9400000u, rt, rn, off, 4); }
	void StrW(int rt, int rn, int off)	{ EmitUnsignedOffset(0xB9000000u, rt, rn, off, 4); }
	void LdrX(int rt, int rn, int off)	{ EmitUnsignedOffset(0xF9400000u, rt, rn, off, 8); }
	void StrX(int rt, int rn, int off)	{ EmitUnsignedOffset(0xF9000000u, rt, rn, off, 8); }
	void LdrB(int rt, int rn, int off)	{ EmitUnsignedOffset(0x39400000u, rt, rn, off, 1); }
	void StrB(int rt, int rn, int off)	{ EmitUnsignedOffset(0x39000000u, rt, rn, off, 1); }
	void LdrH(int rt, int rn, int off)	{ EmitUnsignedOffset(0x79400000u, rt, rn, off, 2); }
	void StrH(int rt, int rn, int off)	{ EmitUnsignedOffset(0x79000000u, rt, rn, off, 2); }

	/* Indexed by Wm, zero extended: [Xn, Wm, UXTW]. */
	void LdrWIdx(int rt, int rn, int rm)  { EmitRegOffset(0xB8604800u, rt, rn, rm); }
	void StrWIdx(int rt, int rn, int rm)  { EmitRegOffset(0xB8204800u, rt, rn, rm); }
	void LdrBIdx(int rt, int rn, int rm)  { EmitRegOffset(0x38604800u, rt, rn, rm); }
	void StrBIdx(int rt, int rn, int rm)  { EmitRegOffset(0x38204800u, rt, rn, rm); }
	void LdrHIdx(int rt, int rn, int rm)  { EmitRegOffset(0x78604800u, rt, rn, rm); }
	void StrHIdx(int rt, int rn, int rm)  { EmitRegOffset(0x78204800u, rt, rn, rm); }
	void LdrShIdx(int rt, int rn, int rm) { EmitRegOffset(0x78E04800u, rt, rn, rm); }
	void LdrSbIdx(int rt, int rn, int rm) { EmitRegOffset(0x38E04800u, rt, rn, rm); }

	/* Pair moves, used by the block prologue/epilogue and pc/npc stores. */
	void StpXPre(int rt, int rt2, int rn, int imm)	{ EmitPair(0xA9800000u, rt, rt2, rn, imm, 8); }
	void StpX(int rt, int rt2, int rn, int imm)		{ EmitPair(0xA9000000u, rt, rt2, rn, imm, 8); }
	void LdpX(int rt, int rt2, int rn, int imm)		{ EmitPair(0xA9400000u, rt, rt2, rn, imm, 8); }
	void LdpXPost(int rt, int rt2, int rn, int imm)	{ EmitPair(0xA8C00000u, rt, rt2, rn, imm, 8); }
	void StpW(int rt, int rt2, int rn, int imm)		{ EmitPair(0x29000000u, rt, rt2, rn, imm, 4); }

	void Blr(int rn) { Emit(0xD63F0000u | ((UINT32)rn << 5)); }
	void Br(int rn) { Emit(0xD61F0000u | ((UINT32)rn << 5)); }
	void Ret(void) { Emit(0xD65F03C0u); }
	void Nop(void) { Emit(0xD503201Fu); }

private:
	enum { kMaxLiterals = 320, kMaxFixups = 1024 };
	enum { kFixLiteral = 0, kFixBranch = 1, kFixCond = 2 };

	struct Fixup
	{
		UINT32 at;
		UINT32 kind;
		UINT32 target;
	};

	void Emit(UINT32 insn)
	{
		if (!m_ok)
			return;
		if (m_size >= m_capacity)
		{
			m_ok = false;
			return;
		}
		m_buffer[m_size++] = insn;
	}

	void AddFixup(UINT32 insn, UINT32 kind, UINT32 target)
	{
		if (m_fixupCount >= kMaxFixups)
		{
			m_ok = false;
			return;
		}
		const UINT32 at = (UINT32)m_size;
		Emit(insn);
		m_fixups[m_fixupCount].at = at;
		m_fixups[m_fixupCount].kind = kind;
		m_fixups[m_fixupCount].target = target;
		m_fixupCount++;
	}

	void AddFixupResolved(size_t at, UINT32 kind, size_t target)
	{
		if (m_fixupCount >= kMaxFixups)
		{
			m_ok = false;
			return;
		}
		m_fixups[m_fixupCount].at = (UINT32)at;
		m_fixups[m_fixupCount].kind = kind;
		m_fixups[m_fixupCount].target = (UINT32)target;
		m_fixupCount++;
	}

	void PatchBranch(size_t at, INT64 target)
	{
		const INT64 delta = target - (INT64)at;
		if (delta < -(INT64)0x8000000 || delta > (INT64)0x7FFFFFF)
		{
			m_ok = false;
			return;
		}
		m_buffer[at] |= (UINT32)(delta & 0x3FFFFFF);
	}

	void PatchCond(size_t at, INT64 target)
	{
		const INT64 delta = target - (INT64)at;
		if (delta < -(INT64)0x40000 || delta > (INT64)0x3FFFF)
		{
			m_ok = false;
			return;
		}
		m_buffer[at] |= (UINT32)(delta & 0x7FFFF) << 5;
	}

	void EmitShiftedReg(UINT32 base, int rd, int rn, int rm)
	{
		Emit(base | ((UINT32)rm << 16) | ((UINT32)rn << 5) | (UINT32)rd);
	}

	void EmitUnsignedOffset(UINT32 base, int rt, int rn, int off, int scale)
	{
		if (off < 0 || (off % scale) != 0 || (off / scale) > 4095)
		{
			m_ok = false;
			return;
		}
		Emit(base | ((UINT32)(off / scale) << 10) | ((UINT32)rn << 5) | (UINT32)rt);
	}

	void EmitRegOffset(UINT32 base, int rt, int rn, int rm)
	{
		Emit(base | ((UINT32)rm << 16) | ((UINT32)rn << 5) | (UINT32)rt);
	}

	void EmitPair(UINT32 base, int rt, int rt2, int rn, int imm, int scale)
	{
		if ((imm % scale) != 0)
		{
			m_ok = false;
			return;
		}
		const int imm7 = imm / scale;
		if (imm7 < -64 || imm7 > 63)
		{
			m_ok = false;
			return;
		}
		Emit(base | ((UINT32)(imm7 & 0x7F) << 15) | ((UINT32)rt2 << 10) | ((UINT32)rn << 5) | (UINT32)rt);
	}

	/*
	 * Logical immediate encoding.
	 *
	 * Rather than implement the "highest set bit of N:NOT(imms)" derivation,
	 * the encoder is brute forced: for a 32-bit form (N = 0) there are only
	 * 32 * 31 candidates and decoding one is a rotate of a run of ones. This
	 * runs at JIT compile time and is guaranteed to agree with the hardware
	 * because it uses the architectural decode.
	 */
	static UINT32 DecodeLogicalImm32(UINT32 immr, UINT32 imms)
	{
		const UINT32 welem = (imms >= 31) ? 0xFFFFFFFFu : ((1u << (imms + 1)) - 1u);
		const UINT32 r = immr & 31u;
		if (r == 0)
			return welem;
		return (welem >> r) | (welem << (32 - r));
	}

	static bool EncodeLogicalImm32(UINT32 value, UINT32 *n, UINT32 *immr, UINT32 *imms)
	{
		if (value == 0 || value == 0xFFFFFFFFu)
			return false;		// not representable as a 32-bit logical immediate
		for (UINT32 r = 0; r < 32; r++)
		{
			for (UINT32 s = 0; s < 31; s++)
			{
				if (DecodeLogicalImm32(r, s) == value)
				{
					*n = 0;
					*immr = r;
					*imms = s;
					return true;
				}
			}
		}
		return false;
	}

	UINT32 *m_buffer;
	size_t m_capacity;
	size_t m_size;
	size_t m_poolStart;		/* first word of the literal pool  */
	bool m_ok;

	UINT64 m_literals[kMaxLiterals];
	int m_literalCount;

	Fixup m_fixups[kMaxFixups];
	int m_fixupCount;
};

}	// namespace Arm64

#endif	// __aarch64__

#endif	// INCLUDED_ARM64_EMITTER_H
