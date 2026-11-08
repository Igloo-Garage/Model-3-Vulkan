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
 * ppc603.c
 *
 * PowerPC 603e functions. Included from ppc.cpp; do not compile separately.
 */

void ppc603_exception(int exception)
{
#ifdef SUPERMODEL_DEBUGGER
		if (PPCDebug != NULL)
			PPCDebug->CPUException(exception);
#endif

	switch( exception )
	{
		case EXCEPTION_IRQ:		/* External Interrupt */
			if( ppc_get_msr() & MSR_EE ) {
				UINT32 msr = ppc_get_msr();

				SRR0 = ppc.npc;
				SRR1 = msr & 0xff73;

				msr &= ~(MSR_POW | MSR_EE | MSR_PR | MSR_FP | MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1 | MSR_IR | MSR_DR | MSR_RI);
				if( msr & MSR_ILE )
					msr |= MSR_LE;
				else
					msr &= ~MSR_LE;
				ppc_set_msr(msr);

				if( msr & MSR_IP )
					ppc.npc = 0xfff00000 | 0x0500;
				else
					ppc.npc = 0x00000000 | 0x0500;

				//MAME has this: ppc.interrupt_pending &= ~0x1;
				ppc_change_pc(ppc.npc);
			}
			break;

		case EXCEPTION_DECREMENTER:		/* Decrementer overflow exception */
			if( ppc_get_msr() & MSR_EE ) {
				UINT32 msr = ppc_get_msr();

				SRR0 = ppc.npc;
				SRR1 = msr & 0xff73;

				msr &= ~(MSR_POW | MSR_EE | MSR_PR | MSR_FP | MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1 | MSR_IR | MSR_DR | MSR_RI);
				if( msr & MSR_ILE )
					msr |= MSR_LE;
				else
					msr &= ~MSR_LE;
				ppc_set_msr(msr);

				if( msr & MSR_IP )
					ppc.npc = 0xfff00000 | 0x0900;
				else
					ppc.npc = 0x00000000 | 0x0900;

				ppc.interrupt_pending &= ~0x2;
				ppc_change_pc(ppc.npc);
			}
			break;

		case EXCEPTION_TRAP:			/* Program exception / Trap */
			{
				UINT32 msr = ppc_get_msr();

				SRR0 = ppc.pc;
				SRR1 = (msr & 0xff73) | 0x20000;	/* 0x20000 = TRAP bit */

				msr &= ~(MSR_POW | MSR_EE | MSR_PR | MSR_FP | MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1 | MSR_IR | MSR_DR | MSR_RI);
				if( msr & MSR_ILE )
					msr |= MSR_LE;
				else
					msr &= ~MSR_LE;
				ppc_set_msr(msr);

				if( msr & MSR_IP )
					ppc.npc = 0xfff00000 | 0x0700;
				else
					ppc.npc = 0x00000000 | 0x0700;
				ppc_change_pc(ppc.npc);
			}
			break;

		case EXCEPTION_SYSTEM_CALL:		/* System call */
			{
				UINT32 msr = ppc_get_msr();

				SRR0 = ppc.npc;
				SRR1 = (msr & 0xff73);

				msr &= ~(MSR_POW | MSR_EE | MSR_PR | MSR_FP | MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1 | MSR_IR | MSR_DR | MSR_RI);
				if( msr & MSR_ILE )
					msr |= MSR_LE;
				else
					msr &= ~MSR_LE;
				ppc_set_msr(msr);

				if( msr & MSR_IP )
					ppc.npc = 0xfff00000 | 0x0c00;
				else
					ppc.npc = 0x00000000 | 0x0c00;
				ppc_change_pc(ppc.npc);
			}
			break;

		case EXCEPTION_SMI:
			if( ppc_get_msr() & MSR_EE ) {
				UINT32 msr = ppc_get_msr();

				SRR0 = ppc.npc;
				SRR1 = msr & 0xff73;

				msr &= ~(MSR_POW | MSR_EE | MSR_PR | MSR_FP | MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1 | MSR_IR | MSR_DR | MSR_RI);
				if( msr & MSR_ILE )
					msr |= MSR_LE;
				else
					msr &= ~MSR_LE;
				ppc_set_msr(msr);

				if( msr & MSR_IP )
					ppc.npc = 0xfff00000 | 0x1400;
				else
					ppc.npc = 0x00000000 | 0x1400;

				ppc.interrupt_pending &= ~0x4;
				ppc_change_pc(ppc.npc);
			}
			break;

		case EXCEPTION_DSI:
			{
				UINT32 msr = ppc_get_msr();

				SRR0 = ppc.npc;
				SRR1 = msr & 0xff73;

				msr &= ~(MSR_POW | MSR_EE | MSR_PR | MSR_FP | MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1 | MSR_IR | MSR_DR | MSR_RI);
				if( msr & MSR_ILE )
					msr |= MSR_LE;
				else
					msr &= ~MSR_LE;
				ppc_set_msr(msr);

				if( msr & MSR_IP )
					ppc.npc = 0xfff00000 | 0x0300;
				else
					ppc.npc = 0x00000000 | 0x0300;

				ppc.interrupt_pending &= ~0x4;
				ppc_change_pc(ppc.npc);
			}
			break;

		case EXCEPTION_ISI:
			{
				UINT32 msr = ppc_get_msr();

				SRR0 = ppc.npc;
				SRR1 = msr & 0xff73;

				msr &= ~(MSR_POW | MSR_EE | MSR_PR | MSR_FP | MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1 | MSR_IR | MSR_DR | MSR_RI);
				if( msr & MSR_ILE )
					msr |= MSR_LE;
				else
					msr &= ~MSR_LE;
				ppc_set_msr(msr);

				if( msr & MSR_IP )
					ppc.npc = 0xfff00000 | 0x0400;
				else
					ppc.npc = 0x00000000 | 0x0400;

				ppc.interrupt_pending &= ~0x4;
				ppc_change_pc(ppc.npc);
			}
			break;

		default:
			ErrorLog("PowerPC triggered an unknown exception. Emulation halted until reset.");
			DebugLog("PowerPC triggered an unknown exception (%d).\n", exception);
			ppc.fatalError = true;
			break;
	}
}

static void ppc603_check_interrupts(void)
{
	if (MSR & MSR_EE)
	{
		if (ppc.interrupt_pending != 0)
		{
			if (ppc.interrupt_pending & 0x1)
			{
				ppc603_exception(EXCEPTION_IRQ);
			}
			else if (ppc.interrupt_pending & 0x2)
			{
				ppc603_exception(EXCEPTION_DECREMENTER);
			}
			else if (ppc.interrupt_pending & 0x4)
			{
				ppc603_exception(EXCEPTION_SMI);
			}
		}
	}
}

void ppc_reset(void)
{
	ppc.fatalError = false;	// reset the fatal error flag
	
	ppc.pc = ppc.npc = 0xfff00100;

	ppc_set_msr(0x40);
	ppc_change_pc(ppc.pc);

	ppc.hid0 = 1;

	ppc.interrupt_pending = 0;

	ppc.tb = 0;
	ppc.timer_frac = 0;
	DEC = 0xffffffff;
	ppc.total_cycles = 0;
	ppc.cur_cycles = 0;
	ppc.icount = 0;

#if defined(PPC_JIT_X64)
	// Anything translated was translated for the memory the machine had before this reset.
	ppc_jit_flush_all();
#endif
}

/******************************************************************************
 Idle (wait) loop skip

 A game that has finished a frame sits in a short backward branch polling a
 flag that only a device or an interrupt can change. Nothing it can observe
 changes while its register state repeats, so once that has happened a few
 times the rest of the slice can be handed to the timebase and the decrementer
 instead of being spun out - stopping one instruction before any decrementer
 trigger so that interrupt still arrives on exactly the right cycle.

 The snapshot covers the integer and control state only. Floating point
 registers cannot be observed by a loop that has changed nothing else, and
 skipping 320 bytes of them is what makes the comparison cheap enough to run on
 every pass.

 SM3D_IDLE_SKIP=0 (or ppc_set_idle_loop_skip(false)) disarms it for a game that
 turns out to read something the snapshot cannot see.
******************************************************************************/

typedef struct
{
	UINT32	r[32];
	UINT32	cr[8];
	UINT32	lr;
	UINT32	ctr;
	UINT32	xer;
	UINT32	msr;
	UINT32	fpscr;

} PPC_IDLE_STATE;

enum
{
	kIdleSnapshotAt	= 2,		// backward branches seen before the state is kept
	kIdleCompareAt	= 3,		// ... and before a repeat counts as idle
	kIdleCooldown	= 64		// blocks to wait after a comparison that did not match
};

static bool IdleSkipEnabled = true;

void ppc_set_idle_loop_skip(bool enabled)
{
	IdleSkipEnabled = enabled;
}

static void ppc_idle_capture(PPC_IDLE_STATE *state)
{
	memcpy(state->r, ppc.r, sizeof(state->r));
	for (int i = 0; i < 8; i++)
		state->cr[i] = ppc.cr[i];
	state->lr = ppc.lr;
	state->ctr = ppc.ctr;
	state->xer = ppc.xer;
	state->msr = ppc.msr;
	state->fpscr = ppc.fpscr;
}

static bool ppc_idle_unchanged(const PPC_IDLE_STATE *state)
{
	if (memcmp(state->r, ppc.r, sizeof(state->r)) != 0)
		return false;
	for (int i = 0; i < 8; i++)
	{
		if (state->cr[i] != ppc.cr[i])
			return false;
	}
	return state->lr == ppc.lr && state->ctr == ppc.ctr && state->xer == ppc.xer &&
		state->msr == ppc.msr && state->fpscr == ppc.fpscr;
}

/*
 * ppc_idle_step():
 *
 * Feeds one "the CPU just took a backward branch to ppc.npc" observation to the
 * detector. When the loop turns out to be idle the clock is advanced here, and
 * the caller's loop simply continues - it will either run the one instruction
 * that fires the decrementer or see icount at zero and stop.
 *
 * Shared by the interpreter loop and the JIT dispatcher so that both execution
 * modes skip exactly the same loops at exactly the same point. Returns true
 * when the clock was advanced, which is what the JIT counts.
 */
static bool ppc_idle_step(PPC_IDLE_STATE *state, int *hits, int *cooldown, UINT32 *target)
{
	if (*cooldown > 0)
	{
		--(*cooldown);
		return false;
	}

	if (ppc.npc != *target)
	{
		*target = ppc.npc;
		*hits = 1;
		return false;
	}

	++(*hits);
	if (*hits == kIdleSnapshotAt)
	{
		ppc_idle_capture(state);
		return false;
	}
	if (*hits < kIdleCompareAt)
		return false;

	if (!ppc_idle_unchanged(state))
	{
		*hits = 0;
		*cooldown = kIdleCooldown;
		return false;
	}

	*hits = 0;
	const int trigger = ppc.dec_trigger_cycle;
	if (trigger < 0 || trigger >= ppc.icount)
		ppc.icount = 0;
	else
		ppc.icount = trigger + 1;
	return true;
}

/*
 * ppc_execute_one_instruction():
 *
 * One iteration of the interpreter loop, exactly as ppc_execute() used to run it.
 *
 * Split out for the hybrid JIT: the dispatcher uses it to step across the
 * decrementer trigger a block must not span, and the lockstep verifier uses it
 * as the reference run.
 */
static UINT32 ppc_execute_one_instruction(void)
{
	UINT32 opcode;

	ppc.pc = ppc.npc;
	
	// Debug breakpoints
	/*
	if (ppc.pc == 0x9d40)
	{
		printf("%X R3=%08X R4=%08X\n", ppc.pc, REG(3), REG(4));			
		
	}
	*/
		
	opcode = *ppc.op++;	// Supermodel byte reverses each aligned word (converting them to little endian) so they can be fetched directly
	ppc.npc = ppc.pc + 4;

#ifdef SUPERMODEL_DEBUGGER
	if (PPCDebug != NULL)
	{
		while (PPCDebug->CPUExecute(ppc.pc, opcode, (PPCDebug->instrCount > 0 ? 1 : 0)))
			opcode = *ppc.op++;
	}
#endif // SUPERMODEL_DEBUGGER

	switch(opcode >> 26)
	{
		case 19:	optable19[(opcode >> 1) & 0x3ff](opcode); break;
		case 31:	optable31[(opcode >> 1) & 0x3ff](opcode); break;
		case 59:	optable59[(opcode >> 1) & 0x3ff](opcode); break;
		case 63:	optable63[(opcode >> 1) & 0x3ff](opcode); break;
		default:	optable[opcode >> 26](opcode); break;
	}

	ppc.icount--;
	
	if (ppc.icount == ppc.dec_trigger_cycle)
	{
		ppc.interrupt_pending |= 0x2;
		ppc603_check_interrupts();
	}

	//ppc603_check_interrupts();

	return opcode;
}

/*
 * ppc_interpret_slice():
 *
 * Runs instructions until the slice ends or the machine halts. Used when the
 * hybrid JIT is off, and by its dispatcher for the instructions it cannot
 * translate, so that a slice always runs exactly as many instructions as the
 * interpreter alone would have run.
 */
static void ppc_interpret_slice(void)
{
	PPC_IDLE_STATE idleState;
	UINT32 idleTarget = 0;
	int idleHits = 0;
	int idleCooldown = 0;

	while (ppc.icount > 0 && !ppc.fatalError)
	{
		ppc_execute_one_instruction();

		/* A taken backward branch is the arm of the wait-loop skip. */
		if (IdleSkipEnabled && ppc.npc <= ppc.pc)
			ppc_idle_step(&idleState, &idleHits, &idleCooldown, &idleTarget);
		else
			idleHits = 0;
	}
}

int ppc_execute(int cycles)
{
	ppc.cur_cycles = cycles;
	ppc.icount = cycles;
	ppc.tb_base_icount = cycles + ppc.timer_frac;
	ppc.dec_base_icount = cycles + ppc.timer_frac;

	// Check if decrementer exception occurs during execution (exception occurs after decrementer
	// has passed through zero)
	if ((UINT32)(ppc.dec_base_icount / ppc.timer_ratio) > DEC)
		ppc.dec_trigger_cycle = ppc.dec_base_icount - ((1 + DEC) * ppc.timer_ratio);
	else
		ppc.dec_trigger_cycle = 0x7fffffff;

	ppc_change_pc(ppc.npc);

	/*{
		char string1[200];
		char string2[200];
		opcode = BSWAP32(*ppc.op);
		DisassemblePowerPC(opcode, ppc.npc, string1, string2, sizeof(string2), true);
		printf("%08X: %s %s\n", ppc.npc, string1, string2);
	}*/

	ppc603_check_interrupts();

#ifdef SUPERMODEL_DEBUGGER
	if (PPCDebug != NULL)
		PPCDebug->CPUActive();
#endif // SUPERMODEL_DEBUGGER

#if defined(PPC_JIT_X64)
	/*
	 * The dispatcher reproduces the interpreter's loop exactly - same pc/npc/op/icount
	 * handling, same decrementer trigger, same wait-loop skip - so the only way to tell
	 * the two paths apart is the code cache log line. It falls back to the interpreter
	 * for any slice it cannot finish itself.
	 */
	if (ppc_jit_active())
		ppc_jit_execute();
	else
#endif
		ppc_interpret_slice();

#ifdef SUPERMODEL_DEBUGGER
	if (PPCDebug != NULL)
		PPCDebug->CPUInactive();
#endif // SUPERMODEL_DEBUGGER

	// Update timebase and decrementer.  Both are updated at same rate as specified by timer_ratio.
	ppc.timer_frac = ((ppc.tb_base_icount - ppc.icount) % ppc.timer_ratio);
	ppc.tb += ((ppc.tb_base_icount - ppc.icount) / ppc.timer_ratio);
	DEC -= ((ppc.dec_base_icount - ppc.icount) / ppc.timer_ratio);
	
	/*
	{
		char string1[200];
		char string2[200];
		opcode = BSWAP32(*ppc.op);
		DisassemblePowerPC(opcode, ppc.npc, string1, string2, sizeof(string2), true);
		printf("%08X: %s %s\n", ppc.npc, string1, string2);
	}
	*/

	int executed = cycles - ppc.icount;
	ppc.total_cycles += executed;
	ppc.cur_cycles = 0;
	ppc.icount = 0;
	ppc.tb_base_icount = 0;
    ppc.dec_base_icount = 0;
	return executed;
}
