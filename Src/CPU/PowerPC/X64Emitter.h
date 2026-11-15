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
 * X64Emitter.h
 *
 * Minimal x86-64 machine code encoder for the PowerPC 603e hybrid JIT.
 *
 * Emits the exact subset of x86-64 instructions required by ppc_jit_x64.inl
 * into a caller-supplied byte buffer, with forward/backward branch fixups and
 * strict register / encoding checks:
 *  - Proper REX.W / REX.R / REX.X / REX.B prefixes for RAX(0)..R15(15), plus
 *    mandatory REX (0x40) when addressing SPL/BPL/SIL/DIL in 8-bit operations.
 *  - Mandatory SIB byte (0x24 / indexed) when RSP(4) or R12(12) is the base.
 *  - Mandatory disp8=0x00 (mod=01) when RBP(5) or R13(13) is the base with zero
 *    displacement (both [base] and [base + index]), avoiding RIP-relative or
 *    disp32-without-base encodings.
 *  - XMM register indices are restricted to XMM0..XMM1 so Windows x64
 *    callee-saved XMM6..XMM15 can never be touched.
 */

#ifndef INCLUDED_X64_EMITTER_H
#define INCLUDED_X64_EMITTER_H

#if defined(_M_X64) || defined(__x86_64__) || defined(SUPERMODEL_X64_EMITTER_HOST_TEST)

#include "Types.h"

namespace X64
{

/*
 * x86-64 condition codes as encoded in Jcc (0F 80+cc), CMOVcc (0F 40+cc)
 * and SETcc (0F 90+cc).
 */
enum Condition
{
	CC_O   = 0x0, CC_NO  = 0x1,
	CC_B   = 0x2, CC_AE  = 0x3,
	CC_CS  = 0x2, CC_CC  = 0x3,
	CC_NAE = 0x2, CC_NB  = 0x3,
	CC_E   = 0x4, CC_NE  = 0x5,
	CC_Z   = 0x4, CC_NZ  = 0x5,
	CC_BE  = 0x6, CC_A   = 0x7,
	CC_LS  = 0x6, CC_HI  = 0x7,
	CC_S   = 0x8, CC_NS  = 0x9,
	CC_P   = 0xA, CC_NP  = 0xB,
	CC_L   = 0xC, CC_GE  = 0xD,
	CC_LE  = 0xE, CC_G   = 0xF
};

/*
 * General-purpose register numbers.
 * Names avoid collision with macros defined in ppc.cpp (LR, CTR, XER, CR, ...).
 */
enum Register
{
	RAX = 0, RCX = 1, RDX = 2, RBX = 3,
	RSP = 4, RBP = 5, RSI = 6, RDI = 7,
	R8  = 8, R9  = 9, R10 = 10, R11 = 11,
	R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

/*
 * Only XMM0 and XMM1 are exposed: XMM6..XMM15 are callee-saved on Windows x64.
 */
enum XmmRegister
{
	XMM0 = 0, XMM1 = 1
};

class CEmitter
{
public:
	CEmitter(void)
	{
		Reset(NULL, 0);
	}

	void Reset(UINT8 *buffer, size_t capacity)
	{
		m_buffer = buffer;
		m_capacity = capacity;
		m_size = 0;
		m_fixupCount = 0;
		m_ok = (buffer != NULL && capacity > 0);
	}

	bool Ok(void) const { return m_ok; }
	size_t Size(void) const { return m_size; }
	size_t Here(void) const { return m_size; }
	const UINT8 *Bytes(void) const { return m_buffer; }
	UINT8 Byte(size_t i) const { return (m_buffer != NULL && i < m_size) ? m_buffer[i] : 0xCCu; }
	void Fail(void) { m_ok = false; }

	/*
	 * Resolves every pending branch fixup in place.
	 * Must be called once after the last instruction of the block.
	 */
	void Finalize(void)
	{
		if (!m_ok)
			return;

		for (int i = 0; i < m_fixupCount && m_ok; i++)
		{
			const Fixup &f = m_fixups[i];
			const INT64 nextRip = (INT64)f.dispOffset + 4;
			const INT64 delta = (INT64)f.target - nextRip;
			if (delta < -(INT64)0x80000000LL || delta > (INT64)0x7FFFFFFFLL)
			{
				m_ok = false;
				return;
			}
			Patch32(f.dispOffset, (UINT32)(INT32)delta);
		}
	}

	/* ------------------------------------------------------------------ */
	/* Branches and calls                                                 */
	/* ------------------------------------------------------------------ */

	/* Emits a 5-byte near JMP rel32 and returns the fixup token (disp offset). */
	size_t BEmit(void)
	{
		Emit8(0xE9);
		const size_t dispAt = m_size;
		Emit32(0);
		return dispAt;
	}

	/* Emits a 6-byte near Jcc rel32 and returns the fixup token (disp offset). */
	size_t BCondEmit(int cond)
	{
		Emit8(0x0F);
		Emit8((UINT8)(0x80 | (cond & 0xF)));
		const size_t dispAt = m_size;
		Emit32(0);
		return dispAt;
	}

	/* Emits TEST reg32, reg32 + JZ rel32, returning the JZ fixup token. */
	size_t CbzEmit(int reg)
	{
		Test32(reg, reg);
		return BCondEmit(CC_Z);
	}

	/* Emits TEST reg32, reg32 + JNZ rel32, returning the JNZ fixup token. */
	size_t CbnzEmit(int reg)
	{
		Test32(reg, reg);
		return BCondEmit(CC_NZ);
	}

	void ResolveBranch(size_t dispAt, size_t target)
	{
		AddFixup(dispAt, target);
	}

	void ResolveCond(size_t dispAt, size_t target)
	{
		AddFixup(dispAt, target);
	}

	/* Emits a jump to an already-known target (uses rel8 when in range). */
	void BTo(size_t target)
	{
		const INT64 shortDelta = (INT64)target - (INT64)(m_size + 2);
		if (shortDelta >= -128 && shortDelta <= 127)
		{
			Emit8(0xEB);
			Emit8((UINT8)(INT8)shortDelta);
		}
		else
		{
			ResolveBranch(BEmit(), target);
		}
	}

	void BCondTo(int cond, size_t target)
	{
		const INT64 shortDelta = (INT64)target - (INT64)(m_size + 2);
		if (shortDelta >= -128 && shortDelta <= 127)
		{
			Emit8((UINT8)(0x70 | (cond & 0xF)));
			Emit8((UINT8)(INT8)shortDelta);
		}
		else
		{
			ResolveCond(BCondEmit(cond), target);
		}
	}

	void Push(int reg)
	{
		if ((reg & 8) != 0)
			Emit8(0x41);
		Emit8((UINT8)(0x50 | (reg & 7)));
	}

	void Pop(int reg)
	{
		if ((reg & 8) != 0)
			Emit8(0x41);
		Emit8((UINT8)(0x58 | (reg & 7)));
	}

	void CallReg(int reg)
	{
		if ((reg & 8) != 0)
			Emit8(0x41);
		Emit8(0xFF);
		EmitModRM(3, 2, reg);
	}

	void Ret(void) { Emit8(0xC3); }
	void Nop(void) { Emit8(0x90); }

	/* ------------------------------------------------------------------ */
	/* Register and immediate moves                                       */
	/* ------------------------------------------------------------------ */

	void Mov32(int rd, int rm)
	{
		EmitRex(false, rm, 0, rd);
		Emit8(0x89);
		EmitModRM(3, rm, rd);
	}

	void Mov64(int rd, int rm)
	{
		if (rd == rm)
			return;
		EmitRex(true, rm, 0, rd);
		Emit8(0x89);
		EmitModRM(3, rm, rd);
	}

	/*
	 * Always encodes as MOV r32, imm32 (B8+rd id) rather than XOR r32, r32 so
	 * CPU condition flags (CF/ZF/SF/OF) are never clobbered.
	 */
	void MovImm32(int rd, UINT32 imm)
	{
		if ((rd & 8) != 0)
			Emit8(0x41);
		Emit8((UINT8)(0xB8 | (rd & 7)));
		Emit32(imm);
	}

	void MovImm64(int rd, UINT64 imm)
	{
		if (imm <= 0xFFFFFFFFULL)
		{
			MovImm32(rd, (UINT32)imm);
			return;
		}
		EmitRex(true, 0, 0, rd);
		Emit8((UINT8)(0xB8 | (rd & 7)));
		Emit64(imm);
	}

	/* ------------------------------------------------------------------ */
	/* 32-bit and 64-bit ALU (register-to-register)                       */
	/* ------------------------------------------------------------------ */

	void Add32(int rd, int rm)  { EmitRex(false, rm, 0, rd); Emit8(0x01); EmitModRM(3, rm, rd); }
	void Sub32(int rd, int rm)  { EmitRex(false, rm, 0, rd); Emit8(0x29); EmitModRM(3, rm, rd); }
	void And32(int rd, int rm)  { EmitRex(false, rm, 0, rd); Emit8(0x21); EmitModRM(3, rm, rd); }
	void Or32(int rd, int rm)   { EmitRex(false, rm, 0, rd); Emit8(0x09); EmitModRM(3, rm, rd); }
	void Xor32(int rd, int rm)  { EmitRex(false, rm, 0, rd); Emit8(0x31); EmitModRM(3, rm, rd); }
	void Cmp32(int rn, int rm)  { EmitRex(false, rm, 0, rn); Emit8(0x39); EmitModRM(3, rm, rn); }
	void Test32(int rn, int rm) { EmitRex(false, rm, 0, rn); Emit8(0x85); EmitModRM(3, rm, rn); }

	void Add64(int rd, int rm)  { EmitRex(true, rm, 0, rd); Emit8(0x01); EmitModRM(3, rm, rd); }
	void Sub64(int rd, int rm)  { EmitRex(true, rm, 0, rd); Emit8(0x29); EmitModRM(3, rm, rd); }

	void Neg32(int rd)  { EmitRex(false, 0, 0, rd); Emit8(0xF7); EmitModRM(3, 3, rd); }
	void Not32(int rd)  { EmitRex(false, 0, 0, rd); Emit8(0xF7); EmitModRM(3, 2, rd); }

	void Imul32(int rd, int rm)
	{
		EmitRex(false, rd, 0, rm);
		Emit8(0x0F);
		Emit8(0xAF);
		EmitModRM(3, rd, rm);
	}

	void ImulImm32(int rd, int rm, INT32 imm)
	{
		EmitRex(false, rd, 0, rm);
		if (imm >= -128 && imm <= 127)
		{
			Emit8(0x6B);
			EmitModRM(3, rd, rm);
			Emit8((UINT8)(INT8)imm);
		}
		else
		{
			Emit8(0x69);
			EmitModRM(3, rd, rm);
			Emit32((UINT32)imm);
		}
	}

	/* ------------------------------------------------------------------ */
	/* Immediate ALU                                                      */
	/* ------------------------------------------------------------------ */

	void AddImm32(int rd, INT32 imm)
	{
		if (imm == 0)
			return;
		EmitAluImm32(0, rd, (UINT32)imm);
	}

	void OrImm32(int rd, UINT32 imm)
	{
		if (imm == 0)
			return;
		EmitAluImm32(1, rd, imm);
	}

	void AndImm32(int rd, UINT32 imm)
	{
		EmitAluImm32(4, rd, imm);
	}

	void SubImm32(int rd, INT32 imm)
	{
		if (imm == 0)
			return;
		EmitAluImm32(5, rd, (UINT32)imm);
	}

	void XorImm32(int rd, UINT32 imm)
	{
		if (imm == 0)
			return;
		EmitAluImm32(6, rd, imm);
	}

	void CmpImm32(int rd, INT32 imm)
	{
		EmitAluImm32(7, rd, (UINT32)imm);
	}

	void TestImm32(int rd, UINT32 imm)
	{
		EmitRex(false, 0, 0, rd);
		Emit8(0xF7);
		EmitModRM(3, 0, rd);
		Emit32(imm);
	}

	void AddImm64(int rd, INT32 imm)
	{
		if (imm == 0)
			return;
		EmitAluImm64(0, rd, imm);
	}

	void SubImm64(int rd, INT32 imm)
	{
		if (imm == 0)
			return;
		EmitAluImm64(5, rd, imm);
	}

	/* ------------------------------------------------------------------ */
	/* Sign/zero extension, byte swap, bit scan and bit manipulation      */
	/* ------------------------------------------------------------------ */

	void Movsx8To32(int rd, int rm)
	{
		EmitRex(false, rd, 0, rm, rm >= 4);
		Emit8(0x0F);
		Emit8(0xBE);
		EmitModRM(3, rd, rm);
	}

	void Movsx16To32(int rd, int rm)
	{
		EmitRex(false, rd, 0, rm);
		Emit8(0x0F);
		Emit8(0xBF);
		EmitModRM(3, rd, rm);
	}

	void Movzx8To32(int rd, int rm)
	{
		EmitRex(false, rd, 0, rm, rm >= 4);
		Emit8(0x0F);
		Emit8(0xB6);
		EmitModRM(3, rd, rm);
	}

	void Movzx16To32(int rd, int rm)
	{
		EmitRex(false, rd, 0, rm);
		Emit8(0x0F);
		Emit8(0xB7);
		EmitModRM(3, rd, rm);
	}

	void Bswap32(int rd)
	{
		if ((rd & 8) != 0)
			Emit8(0x41);
		Emit8(0x0F);
		Emit8((UINT8)(0xC8 | (rd & 7)));
	}

	/* Rotates the low 16 bits of rd left by sh; upper 16 bits are untouched. */
	void Rol16Imm(int rd, int sh)
	{
		Emit8(0x66);
		if ((rd & 8) != 0)
			Emit8(0x41);
		Emit8(0xC1);
		EmitModRM(3, 0, rd);
		Emit8((UINT8)(sh & 15));
	}

	void Bsr32(int rd, int rm)
	{
		EmitRex(false, rd, 0, rm);
		Emit8(0x0F);
		Emit8(0xBD);
		EmitModRM(3, rd, rm);
	}

	/* 64-bit bit test and complement / reset / set (used for fneg/fabs/fnabs). */
	void BtcImm64(int rd, int bit)
	{
		EmitRex(true, 0, 0, rd);
		Emit8(0x0F);
		Emit8(0xBA);
		EmitModRM(3, 7, rd);
		Emit8((UINT8)(bit & 63));
	}

	void BtrImm64(int rd, int bit)
	{
		EmitRex(true, 0, 0, rd);
		Emit8(0x0F);
		Emit8(0xBA);
		EmitModRM(3, 6, rd);
		Emit8((UINT8)(bit & 63));
	}

	void BtsImm64(int rd, int bit)
	{
		EmitRex(true, 0, 0, rd);
		Emit8(0x0F);
		Emit8(0xBA);
		EmitModRM(3, 5, rd);
		Emit8((UINT8)(bit & 63));
	}

	/* ------------------------------------------------------------------ */
	/* Shifts and rotates                                                 */
	/* ------------------------------------------------------------------ */

	void RolImm32(int rd, int sh) { EmitShiftImm32(0, rd, sh); }
	void RorImm32(int rd, int sh) { EmitShiftImm32(1, rd, sh); }
	void ShlImm32(int rd, int sh) { EmitShiftImm32(4, rd, sh); }
	void ShrImm32(int rd, int sh) { EmitShiftImm32(5, rd, sh); }
	void SarImm32(int rd, int sh) { EmitShiftImm32(7, rd, sh); }

	/* Variable shifts/rotates by CL (low 8 bits of RCX). */
	void RolCl32(int rd) { EmitShiftCl32(0, rd); }
	void RorCl32(int rd) { EmitShiftCl32(1, rd); }
	void ShlCl32(int rd) { EmitShiftCl32(4, rd); }
	void ShrCl32(int rd) { EmitShiftCl32(5, rd); }
	void SarCl32(int rd) { EmitShiftCl32(7, rd); }

	/* ------------------------------------------------------------------ */
	/* Conditional move and conditional set                               */
	/* ------------------------------------------------------------------ */

	void Cmovcc32(int cond, int rd, int rm)
	{
		EmitRex(false, rd, 0, rm);
		Emit8(0x0F);
		Emit8((UINT8)(0x40 | (cond & 0xF)));
		EmitModRM(3, rd, rm);
	}

	void Cmovcc64(int cond, int rd, int rm)
	{
		EmitRex(true, rd, 0, rm);
		Emit8(0x0F);
		Emit8((UINT8)(0x40 | (cond & 0xF)));
		EmitModRM(3, rd, rm);
	}

	void Setcc(int cond, int rd)
	{
		EmitRex(false, 0, 0, rd, rd >= 4);
		Emit8(0x0F);
		Emit8((UINT8)(0x90 | (cond & 0xF)));
		EmitModRM(3, 0, rd);
	}

	/* Sets rd (32-bit) to 0 or 1 from condition flags without altering EFLAGS. */
	void Cset32(int cond, int rd)
	{
		Setcc(cond, rd);
		Movzx8To32(rd, rd);
	}

	/* ------------------------------------------------------------------ */
	/* Memory: [base + disp]                                              */
	/* ------------------------------------------------------------------ */

	void Load32(int rt, int base, INT32 disp)
	{
		EmitRex(false, rt, 0, base);
		Emit8(0x8B);
		EmitMemDisp(rt, base, disp);
	}

	void Store32(int base, INT32 disp, int rs)
	{
		EmitRex(false, rs, 0, base);
		Emit8(0x89);
		EmitMemDisp(rs, base, disp);
	}

	void StoreImm32(int base, INT32 disp, UINT32 imm)
	{
		EmitRex(false, 0, 0, base);
		Emit8(0xC7);
		EmitMemDisp(0, base, disp);
		Emit32(imm);
	}

	void Load64(int rt, int base, INT32 disp)
	{
		EmitRex(true, rt, 0, base);
		Emit8(0x8B);
		EmitMemDisp(rt, base, disp);
	}

	void Store64(int base, INT32 disp, int rs)
	{
		EmitRex(true, rs, 0, base);
		Emit8(0x89);
		EmitMemDisp(rs, base, disp);
	}

	void Load8U(int rt, int base, INT32 disp)
	{
		EmitRex(false, rt, 0, base);
		Emit8(0x0F);
		Emit8(0xB6);
		EmitMemDisp(rt, base, disp);
	}

	void Store8(int base, INT32 disp, int rs)
	{
		EmitRex(false, rs, 0, base, rs >= 4);
		Emit8(0x88);
		EmitMemDisp(rs, base, disp);
	}

	void StoreImm8(int base, INT32 disp, UINT8 imm)
	{
		EmitRex(false, 0, 0, base);
		Emit8(0xC6);
		EmitMemDisp(0, base, disp);
		Emit8(imm);
	}

	void SubMemImm32(int base, INT32 disp, INT32 imm)
	{
		if (imm == 0)
			return;
		EmitRex(false, 5, 0, base);
		if (imm >= -128 && imm <= 127)
		{
			Emit8(0x83);
			EmitMemDisp(5, base, disp);
			Emit8((UINT8)(INT8)imm);
		}
		else
		{
			Emit8(0x81);
			EmitMemDisp(5, base, disp);
			Emit32((UINT32)imm);
		}
	}

	void CmpMemImm32(int base, INT32 disp, INT32 imm)
	{
		EmitRex(false, 7, 0, base);
		if (imm >= -128 && imm <= 127)
		{
			Emit8(0x83);
			EmitMemDisp(7, base, disp);
			Emit8((UINT8)(INT8)imm);
		}
		else
		{
			Emit8(0x81);
			EmitMemDisp(7, base, disp);
			Emit32((UINT32)imm);
		}
	}

	/* ------------------------------------------------------------------ */
	/* Memory: [base + index] (scale = 1, 64-bit base + zero-ext index)   */
	/* ------------------------------------------------------------------ */

	void Load32Idx(int rt, int base, int index)
	{
		EmitRex(false, rt, index, base);
		Emit8(0x8B);
		EmitMemIdx(rt, base, index);
	}

	void Store32Idx(int base, int index, int rs)
	{
		EmitRex(false, rs, index, base);
		Emit8(0x89);
		EmitMemIdx(rs, base, index);
	}

	void Load16UIdx(int rt, int base, int index)
	{
		EmitRex(false, rt, index, base);
		Emit8(0x0F);
		Emit8(0xB7);
		EmitMemIdx(rt, base, index);
	}

	void Load16SIdx(int rt, int base, int index)
	{
		EmitRex(false, rt, index, base);
		Emit8(0x0F);
		Emit8(0xBF);
		EmitMemIdx(rt, base, index);
	}

	void Store16Idx(int base, int index, int rs)
	{
		Emit8(0x66);
		EmitRex(false, rs, index, base);
		Emit8(0x89);
		EmitMemIdx(rs, base, index);
	}

	void Load8UIdx(int rt, int base, int index)
	{
		EmitRex(false, rt, index, base);
		Emit8(0x0F);
		Emit8(0xB6);
		EmitMemIdx(rt, base, index);
	}

	void Store8Idx(int base, int index, int rs)
	{
		EmitRex(false, rs, index, base, rs >= 4);
		Emit8(0x88);
		EmitMemIdx(rs, base, index);
	}

	/* ------------------------------------------------------------------ */
	/* Scalar SSE2 floating-point conversion (XMM0..XMM1 only)            */
	/* ------------------------------------------------------------------ */

	void MovdToXmm(int xdst, int gp)
	{
		if ((xdst & ~1) != 0) { m_ok = false; return; }
		Emit8(0x66);
		EmitRex(false, xdst, 0, gp);
		Emit8(0x0F);
		Emit8(0x6E);
		EmitModRM(3, xdst, gp);
	}

	void MovdFromXmm(int gp, int xsrc)
	{
		if ((xsrc & ~1) != 0) { m_ok = false; return; }
		Emit8(0x66);
		EmitRex(false, xsrc, 0, gp);
		Emit8(0x0F);
		Emit8(0x7E);
		EmitModRM(3, xsrc, gp);
	}

	void Cvtss2sd(int xdst, int xsrc)
	{
		if (((xdst | xsrc) & ~1) != 0) { m_ok = false; return; }
		Emit8(0xF3);
		EmitRex(false, xdst, 0, xsrc);
		Emit8(0x0F);
		Emit8(0x5A);
		EmitModRM(3, xdst, xsrc);
	}

	void Cvtsd2ss(int xdst, int xsrc)
	{
		if (((xdst | xsrc) & ~1) != 0) { m_ok = false; return; }
		Emit8(0xF2);
		EmitRex(false, xdst, 0, xsrc);
		Emit8(0x0F);
		Emit8(0x5A);
		EmitModRM(3, xdst, xsrc);
	}

	void LoadSd(int xdst, int base, INT32 disp)
	{
		if ((xdst & ~1) != 0) { m_ok = false; return; }
		Emit8(0xF2);
		EmitRex(false, xdst, 0, base);
		Emit8(0x0F);
		Emit8(0x10);
		EmitMemDisp(xdst, base, disp);
	}

	void StoreSd(int base, INT32 disp, int xsrc)
	{
		if ((xsrc & ~1) != 0) { m_ok = false; return; }
		Emit8(0xF2);
		EmitRex(false, xsrc, 0, base);
		Emit8(0x0F);
		Emit8(0x11);
		EmitMemDisp(xsrc, base, disp);
	}

private:
	enum { kMaxFixups = 2048 };

	struct Fixup
	{
		size_t dispOffset;
		size_t target;
	};

	void Emit8(UINT8 b)
	{
		if (!m_ok)
			return;
		if (m_size >= m_capacity)
		{
			m_ok = false;
			return;
		}
		m_buffer[m_size++] = b;
	}

	void Emit32(UINT32 v)
	{
		Emit8((UINT8)(v & 0xFF));
		Emit8((UINT8)((v >> 8) & 0xFF));
		Emit8((UINT8)((v >> 16) & 0xFF));
		Emit8((UINT8)((v >> 24) & 0xFF));
	}

	void Emit64(UINT64 v)
	{
		Emit32((UINT32)(v & 0xFFFFFFFFULL));
		Emit32((UINT32)(v >> 32));
	}

	void Patch32(size_t offset, UINT32 v)
	{
		if (offset + 4 > m_size)
		{
			m_ok = false;
			return;
		}
		m_buffer[offset + 0] = (UINT8)(v & 0xFF);
		m_buffer[offset + 1] = (UINT8)((v >> 8) & 0xFF);
		m_buffer[offset + 2] = (UINT8)((v >> 16) & 0xFF);
		m_buffer[offset + 3] = (UINT8)((v >> 24) & 0xFF);
	}

	void AddFixup(size_t dispOffset, size_t target)
	{
		if (m_fixupCount >= kMaxFixups)
		{
			m_ok = false;
			return;
		}
		m_fixups[m_fixupCount].dispOffset = dispOffset;
		m_fixups[m_fixupCount].target = target;
		m_fixupCount++;
	}

	void EmitRex(bool w, int reg, int index, int rm, bool force = false)
	{
		const UINT8 bits = (w ? 0x08 : 0)
						 | (((reg   >> 3) & 1) << 2)
						 | (((index >> 3) & 1) << 1)
						 |  ((rm    >> 3) & 1);
		if (bits != 0 || force)
			Emit8((UINT8)(0x40 | bits));
	}

	void EmitModRM(int mod, int reg, int rm)
	{
		Emit8((UINT8)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7)));
	}

	void EmitSIB(int scale, int index, int base)
	{
		Emit8((UINT8)(((scale & 3) << 6) | ((index & 7) << 3) | (base & 7)));
	}

	/*
	 * Encodes [base + disp]:
	 *  - RSP(4) / R12(12) as base requires SIB 0x24.
	 *  - RBP(5) / R13(13) with disp == 0 requires mod=01 + disp8=0x00.
	 */
	void EmitMemDisp(int reg, int base, INT32 disp)
	{
		const int baseLow = base & 7;
		const bool needsSib = (baseLow == 4);

		if (disp == 0 && baseLow != 5)
		{
			EmitModRM(0, reg, base);
			if (needsSib)
				EmitSIB(0, 4, base);
		}
		else if (disp >= -128 && disp <= 127)
		{
			EmitModRM(1, reg, base);
			if (needsSib)
				EmitSIB(0, 4, base);
			Emit8((UINT8)(INT8)disp);
		}
		else
		{
			EmitModRM(2, reg, base);
			if (needsSib)
				EmitSIB(0, 4, base);
			Emit32((UINT32)disp);
		}
	}

	/*
	 * Encodes [base + index * 1]:
	 *  - RSP(4) cannot be used as an index register.
	 *  - RBP(5) / R13(13) as base requires mod=01 + disp8=0x00 (mod=00 with
	 *    SIB.base=5 encodes [index + disp32] with no base register).
	 */
	void EmitMemIdx(int reg, int base, int index)
	{
		if (index == RSP)
		{
			m_ok = false;
			return;
		}
		const int baseLow = base & 7;
		if (baseLow == 5)
		{
			EmitModRM(1, reg, 4);
			EmitSIB(0, index, base);
			Emit8(0x00);
		}
		else
		{
			EmitModRM(0, reg, 4);
			EmitSIB(0, index, base);
		}
	}

	void EmitAluImm32(int digit, int rd, UINT32 imm)
	{
		EmitRex(false, 0, 0, rd);
		const INT32 simm = (INT32)imm;
		if (simm >= -128 && simm <= 127)
		{
			Emit8(0x83);
			EmitModRM(3, digit, rd);
			Emit8((UINT8)(INT8)simm);
		}
		else
		{
			Emit8(0x81);
			EmitModRM(3, digit, rd);
			Emit32(imm);
		}
	}

	void EmitAluImm64(int digit, int rd, INT32 imm)
	{
		EmitRex(true, 0, 0, rd);
		if (imm >= -128 && imm <= 127)
		{
			Emit8(0x83);
			EmitModRM(3, digit, rd);
			Emit8((UINT8)(INT8)imm);
		}
		else
		{
			Emit8(0x81);
			EmitModRM(3, digit, rd);
			Emit32((UINT32)imm);
		}
	}

	void EmitShiftImm32(int digit, int rd, int sh)
	{
		sh &= 31;
		if (sh == 0)
			return;
		EmitRex(false, 0, 0, rd);
		if (sh == 1)
		{
			Emit8(0xD1);
			EmitModRM(3, digit, rd);
		}
		else
		{
			Emit8(0xC1);
			EmitModRM(3, digit, rd);
			Emit8((UINT8)sh);
		}
	}

	void EmitShiftCl32(int digit, int rd)
	{
		EmitRex(false, 0, 0, rd);
		Emit8(0xD3);
		EmitModRM(3, digit, rd);
	}

	UINT8 *m_buffer;
	size_t m_capacity;
	size_t m_size;
	bool m_ok;

	Fixup m_fixups[kMaxFixups];
	int m_fixupCount;
};

}	// namespace X64

#endif	// _M_X64 || __x86_64__ || SUPERMODEL_X64_EMITTER_HOST_TEST
#endif	// INCLUDED_X64_EMITTER_H
