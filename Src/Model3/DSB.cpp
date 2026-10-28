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
 * DSB.cpp
 *
 * Sega Digital Sound Board (MPEG audio). Implementation of the CDSB1 and CDSB2
 * classes. Based on code donated by R. Belmont. Many Bothans died to bring us
 * this emulation.
 *
 * TODO List
 * ---------
 * - Should MPEG_SetLoop() check for loopEnd==0? This causes crashes. Usually
 *   only occurs when loopStart is also 0, and that can only be checked here.
 * - Check actual MPEG sample rate. So far, all games seem to use 32 KHz, which
 *   may be a hardware requirement, but if other sampling rates are allowable,
 *   the code here will fail (it is hard coded for 32 KHz).
 * - Should we do some bounds checking on the MPEG start/end points?
 */

#include "DSB.h"

#include "Supermodel.h"
#include "Sound/MPEG/MpegAudio.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>

// The MPEG ROM reserved by CModel3 (DSBMPEGROM_SIZE). Offsets in the DSB save state are
// relative to this buffer; anything outside it is a stale or corrupt saved range.
static constexpr unsigned kMpegRomSize = 0x1000000;

static bool DsbLoadDebugEnabled(void)
{
	static const bool enabled = std::getenv("SM3D_LOAD_DEBUG") != nullptr;
	return enabled;
}

/*
 * Optional companion block to "DSB1"/"DSB2": the MPEG window as the decoder itself holds it.
 *
 * The fields in the DSB blocks are the register shadows last latched by the board firmware, and
 * they are not always the window that is actually playing: Scud Race leaves usingMPEG* at the
 * 0x80008000 "no track" sentinel after the music has started, because the firmware stages the
 * next command in mpegStart/mpegEnd. Restoring from those shadows alone therefore either hung
 * (before the range validation) or left the car-select music stopped. This block stores what the
 * decoder is really playing, so the track can resume at the exact byte offset it was saved at.
 *
 * Readers that predate the block skip it and use the old fields, which is why SaveState() also
 * reports isPlaying = 0 when those fields do not describe a valid range.
 */
static constexpr UINT32 kMpegActiveMagic = 0x4D504732;	// "MPG2"

static void SaveMpegActiveWindow(CBlockFile *stateFile, const char *blockName, const UINT8 *mpegROM)
{
	UINT32	magic = kMpegActiveMagic;
	INT32	offset = (INT32)MpegDec::GetActiveOffset(mpegROM);
	INT32	size = (INT32)MpegDec::GetActiveSize();
	INT32	pos = (INT32)MpegDec::GetPosition();
	UINT8	loop = (UINT8)(MpegDec::GetLoop() ? 1 : 0);

	stateFile->NewBlock(blockName, __FILE__);
	stateFile->Write(&magic, sizeof(magic));
	stateFile->Write(&offset, sizeof(offset));
	stateFile->Write(&size, sizeof(size));
	stateFile->Write(&pos, sizeof(pos));
	stateFile->Write(&loop, sizeof(loop));
}

// Returns true when the companion block exists and restored a usable window. The caller then
// skips the legacy register path; a missing or invalid block falls back to it.
static bool LoadMpegActiveWindow(CBlockFile *stateFile, const char *blockName, const UINT8 *mpegROM)
{
	UINT32	magic = 0;
	INT32	offset = -1, size = 0, pos = 0;
	UINT8	loop = 0;

	if (Result::OKAY != stateFile->FindBlock(blockName))
		return false;

	stateFile->Read(&magic, sizeof(magic));
	stateFile->Read(&offset, sizeof(offset));
	stateFile->Read(&size, sizeof(size));
	stateFile->Read(&pos, sizeof(pos));
	stateFile->Read(&loop, sizeof(loop));

	const bool valid = magic == kMpegActiveMagic && mpegROM != nullptr &&
		offset >= 0 && size > 0 && (unsigned)offset + (unsigned)size <= kMpegRomSize;
	if (DsbLoadDebugEnabled())
		InfoLog("LoadDebug mpeg active: magic=%s offset=%d size=%d pos=%d loop=%u%s",
			(magic == kMpegActiveMagic) ? "ok" : "bad", (int)offset, (int)size, (int)pos, (unsigned)loop,
			valid ? " restored" : " ignored");
	if (!valid)
		return false;

	MpegDec::SetMemory(mpegROM, (int)offset, (int)size, loop != 0);
	MpegDec::SetPosition((int)pos);
	return true;
}

/******************************************************************************
 Resampler

 MPEG Layer II audio in Model 3 is 32 KHz; the emulator's output clock is 44.1 KHz. The converter is
 a Kaiser-windowed sinc evaluated through a polyphase table. 32000:44100 reduces to 320:441, so 441
 phases reproduce the ratio exactly and the position advances in integers. The old 24.8 fixed point
 step truncated 0.7256 to 0.7227, which played every DSB track 0.41% (about 7 cents) flat.

 Frame continuity works as before: each frame decodes MpegSamplesForFrame() samples into a buffer
 whose first `retainedSamples` entries are the tail of the previous frame, and the converter returns
 how many input samples to keep for the next one. The difference is that the filter needs k_half
 samples of look-ahead, which is why MpegSamplesForFrame() now adds DSB_RESAMPLER_TAPS instead of the
 old two samples. Without it, the end of every frame would be convolved with zeros.

 The output is float in the same S16-scaled domain the SCSP uses, so the two boards are summed by
 addition in SoundBoard without a conversion or a clamp in between. Volume is applied here.
******************************************************************************/

namespace
{
	// Modified Bessel function of the first kind, order 0, for the Kaiser window.
	double BesselI0(double x)
	{
		const double halfX = x * 0.5;
		double term = 1.0;
		double sum = 1.0;
		for (int k = 1; k < 32; ++k)
		{
			term *= (halfX / k) * (halfX / k);
			sum += term;
			if (term < sum * 1e-12)
				break;
		}
		return sum;
	}
}

void CDSBResampler::Reset(void)
{
	m_inPos = 0;
	m_phase = 0;
	for (int i = 0; i < k_half; ++i)
	{
		m_histL[i] = 0.0f;
		m_histR[i] = 0.0f;
	}
}

void CDSBResampler::Prepare(int outRate, int inRate)
{
	if (outRate == m_outRate && inRate == m_inRate && !m_coeff.empty())
		return;

	m_outRate = outRate;
	m_inRate = inRate;

	// Reduce the ratio so the phases and the integer advance are as small as possible: 32000:44100
	// becomes 320:441, i.e. 441 phases and an advance of 320/441 input samples per output.
	const int divisor = std::gcd(inRate, outRate);
	m_phases = outRate / divisor;
	m_step = inRate / divisor;
	m_coeff.assign((size_t)m_phases * k_taps, 0.0f);

	if (outRate == inRate)
	{
		// Unity ratio: one phase, and a kernel that copies the sample under the output instant.
		m_coeff[k_half - 1] = 1.0f;
		return;
	}

	// Cut-off at the lower of the two Nyquist frequencies (so down-sampling cannot alias), backed
	// off slightly to give the window room for a transition band instead of ringing at the edge.
	constexpr double kPi = 3.14159265358979323846;
	const double fc = std::min(1.0, (double)outRate / (double)inRate) * 0.96;
	const double beta = 8.0;
	const double windowNorm = BesselI0(beta);

	for (int p = 0; p < m_phases; ++p)
	{
		const double frac = (double)p / (double)m_phases;
		float *h = &m_coeff[(size_t)p * k_taps];
		double sum = 0.0;

		for (int k = 0; k < k_taps; ++k)
		{
			// Tap k reads input m_inPos - k_half + 1 + k, so this is its distance from the output
			// instant in input samples.
			const double d = (double)(k - k_half + 1) - frac;
			const double x = d * fc;
			const double sinc = (std::abs(x) < 1e-9) ? 1.0 : std::sin(kPi * x) / (kPi * x);
			const double r = d / (double)k_half;
			const double window = (std::abs(r) >= 1.0)
				? 0.0
				: BesselI0(beta * std::sqrt(1.0 - r * r)) / windowNorm;

			h[k] = (float)(sinc * window);
			sum += h[k];
		}

		// Unity DC gain for every phase, so the level does not modulate at the phase rate.
		if (std::abs(sum) > 1e-12)
		{
			const float scale = (float)(1.0 / sum);
			for (int k = 0; k < k_taps; ++k)
				h[k] *= scale;
		}
	}
}

int CDSBResampler::UpSampleAndMix(float *outL, float *outR, INT16 *inL, INT16 *inR, UINT8 volumeL, UINT8 volumeR, int sizeOut, int sizeIn, int outRate, int inRate)
{
	Prepare(outRate, inRate);

	// Obtain program volume settings
	const float musicVol = (float)std::max(0, std::min(200, m_config["MusicVolume"].ValueAs<int>())) * (1.0f / 100.0f);
	const float gainL = musicVol * (float)volumeL * (1.0f / 255.0f);
	const float gainR = musicVol * (float)volumeR * (1.0f / 255.0f);

	if (sizeOut <= 0 || sizeIn <= 0)
		return sizeIn > 0 ? sizeIn : 0;

	for (int outIdx = 0; outIdx < sizeOut; ++outIdx)
	{
		const int base = m_inPos - k_half + 1;
		const float *h = &m_coeff[(size_t)m_phase * k_taps];
		float left = 0.0f, right = 0.0f;

		if (base >= 0 && base + k_taps <= sizeIn)
		{
			const INT16 *srcL = inL + base;
			const INT16 *srcR = inR + base;
			for (int k = 0; k < k_taps; ++k)
			{
				left += h[k] * (float)srcL[k];
				right += h[k] * (float)srcR[k];
			}
		}
		else
		{
			for (int k = 0; k < k_taps; ++k)
			{
				const int idx = base + k;
				float sampleL, sampleR;
				if (idx < 0)
				{
					// Start of a stream (or of a block shorter than the filter): the samples before
					// the buffer are the ones kept in the history.
					sampleL = (idx >= -k_half) ? m_histL[k_half + idx] : 0.0f;
					sampleR = (idx >= -k_half) ? m_histR[k_half + idx] : 0.0f;
				}
				else if (idx >= sizeIn)
				{
					// Only reachable if a caller under-feeds the block; MpegSamplesForFrame()
					// guarantees the look-ahead. Hold the last sample rather than read past the end.
					sampleL = (float)inL[sizeIn - 1];
					sampleR = (float)inR[sizeIn - 1];
				}
				else
				{
					sampleL = (float)inL[idx];
					sampleR = (float)inR[idx];
				}

				left += h[k] * sampleL;
				right += h[k] * sampleR;
			}
		}

		outL[outIdx] += left * gainL;
		outR[outIdx] += right * gainR;

		// Advance exactly inRate/outRate input samples, without a floating point position.
		m_phase += m_step;
		if (m_phase >= m_phases)
		{
			m_inPos += m_phase / m_phases;
			m_phase %= m_phases;
		}
	}

	// Everything before keepFrom has been consumed; everything from there on is needed by the next
	// output, including the right half of its filter.
	int keepFrom = m_inPos - k_half + 1;
	if (keepFrom < 0)
		keepFrom = 0;
	if (keepFrom > sizeIn)
		keepFrom = sizeIn;

	// Fold the samples preceding the retained block into the history before moving it. Inputs may
	// alias (Sega Rally 2 feeds the same mono buffer to both sides), so compute both channels first.
	float newHistL[k_half], newHistR[k_half];
	for (int k = 0; k < k_half; ++k)
	{
		const int idx = keepFrom - k_half + k;
		if (idx < 0)
		{
			newHistL[k] = (idx >= -k_half) ? m_histL[k_half + idx] : 0.0f;
			newHistR[k] = (idx >= -k_half) ? m_histR[k_half + idx] : 0.0f;
		}
		else
		{
			newHistL[k] = (float)inL[idx];
			newHistR[k] = (float)inR[idx];
		}
	}
	for (int k = 0; k < k_half; ++k)
	{
		m_histL[k] = newHistL[k];
		m_histR[k] = newHistR[k];
	}

	const int retained = sizeIn - keepFrom;
	for (int i = 0; i < retained; ++i)
	{
		inL[i] = inL[keepFrom + i];
		inR[i] = inR[keepFrom + i];
	}
	m_inPos -= keepFrom;

	return retained;
}


/******************************************************************************
 Digital Sound Board Type 1: Z80 CPU
******************************************************************************/

UINT8 CDSB1::Read8(UINT32 addr)
{
	// ROM: 0x0000-0x7FFF
	if (addr < 0x8000)
		return progROM[addr];

	// RAM: 0x8000-0xFFFF
	return ram[addr&0x7FFF];
}

void CDSB1::Write8(UINT32 addr, UINT8 data)
{
	if (addr >= 0x8000)
		ram[addr&0x7FFF] = data;
}

void CDSB1::IOWrite8(UINT32 addr, UINT8 data)
{
	switch ((addr&0xFF))
	{
	case 0xE0:	// MPEG trigger
		mpegState = data;

		if (data == 0)	// stop
		{
			MpegDec::Stop();
			return;
		}

		if (data == 1)	// play without loop
		{
			//printf("====> Playing %06X (mpegEnd=%06X)\n", mpegStart, mpegEnd);

			usingLoopStart	= 0;		// save the settings of the MPEG currently playing
			usingLoopEnd	= 0;
			usingMPEGStart	= mpegStart;
			usingMPEGEnd	= mpegEnd;

			MpegDec::SetMemory(mpegROM, mpegStart, mpegEnd - mpegStart, false);
			return;
		}

		if (data == 2)	// play with loop (NOTE: I don't think this actually loops b/c MPEG_PlayMemory() clears lstart)
		{
			//printf("====> Playing w/ loop %06X (mpegEnd=%06X, loopStart=%06X, loopEnd=%06X)\n", mpegStart, mpegEnd, loopStart, loopEnd);
			usingLoopStart	= 0;		// MPEG_PlayMemory() clears these settings and Z80 will set them up later
			usingLoopEnd	= 0;
			usingMPEGStart	= mpegStart;
			usingMPEGEnd	= mpegEnd;

			MpegDec::SetMemory(mpegROM, mpegStart, mpegEnd - mpegStart, false);		// assume not looped for now
			return;
		}
		break;

	case 0xE2:	// MPEG start, high byte
		startLatch &= 0x00FFFF;
		startLatch |= ((UINT32)data) << 16;
		break;

	case 0xE3:	// MPEG start, middle byte
		startLatch &= 0xFF00FF;
		startLatch |= ((UINT32)data) << 8;
		break;

	case 0xE4:	// MPEG start, low byte
		startLatch &= 0xFFFF00;
		startLatch |= data;

		if (mpegState == 0)
		{
			mpegStart = startLatch;
			//printf("mpegStart = %08X\n", mpegStart);
		}
		else
		{
			loopStart = startLatch;
			//printf("loopStart = %08X\n", loopStart);
			// SWA: if loop end is zero, it means "keep previous end marker"
			if (loopEnd == 0)
			{
				usingLoopStart	= loopStart;
				usingLoopEnd	= mpegEnd-loopStart;
				MpegDec::UpdateMemory(mpegROM, usingLoopStart, usingLoopEnd, true);
			}
			else
			{
				usingLoopStart	= loopStart;
				usingLoopEnd	= loopEnd-loopStart;
				MpegDec::UpdateMemory(mpegROM, usingLoopStart, usingLoopEnd, true);
			}
		}

		break;

	case 0xE5:	// MPEG end, high byte
		endLatch &= 0x00FFFF;
		endLatch |= ((UINT32)data) << 16;
		break;

	case 0xE6:	// MPEG end, middle byte
		endLatch &= 0xFF00FF;
		endLatch |= ((UINT32)data) << 8;
		break;

	case 0xE7:	// MPEG end, low byte
		endLatch &= 0xFFFF00;
		endLatch |= data;

		if (mpegState == 0)
		{
			mpegEnd = endLatch;
			//printf("mpegEnd = %08X\n", mpegEnd);
		}
		else
		{
			loopEnd			= endLatch;
			usingLoopStart	= loopStart;
			usingLoopEnd	= loopEnd-loopStart;
			MpegDec::UpdateMemory(mpegROM, usingLoopStart, usingLoopEnd, true);
			//printf("loopEnd = %08X\n", loopEnd);
		}
		break;

	case 0xE8:	// MPEG volume
		volume = 0x7F-data;
		//printf("Set Volume: %02X\n", volume);
		break;

	case 0xE9:	// MPEG stereo
		stereo = data;
		break;

	case 0xF0:	// command echo back
		break;

	default:
		//printf("Z80 Port %02X=%08X\n", addr, data);
		break;
	}
}

UINT8 CDSB1::IORead8(UINT32 addr)
{
	int	progress;

	switch ((addr&0xFF))
	{
	case 0xE2:	// MPEG position, high byte
		progress = MpegDec::GetPosition();
		progress += mpegStart;	// byte address currently playing
		return (progress>>16)&0xFF;

	case 0xE3:	// MPEG position, middle byte
		progress = MpegDec::GetPosition();
		progress += mpegStart;
		return (progress>>8)&0xFF;

	case 0xE4:	// MPEG position, low byte
		progress = MpegDec::GetPosition();
		progress += mpegStart;
		return progress&0xFF;

	case 0xF0:	// Latch
		UINT8 d;
		d = fifo[fifoIdxR];	// retrieve next command byte
		if (fifoIdxR != fifoIdxW)	// if these are equal, nothing has been written yet (don't advance)
		{
			fifoIdxR++;
			fifoIdxR &= FIFO_STACK_SIZE_MASK;
		}

		if (fifoIdxR == fifoIdxW)	// FIFO empty?
			status &= ~2;			// yes, indicate no commands left
		else
			status |= 2;

		Z80.SetINT(false);	// clear IRQ
		//printf("Z80: INT cleared, read from FIFO\n");
		return d;

	case 0xF1:	// Status
		/*
		 * Bit 0: Must be 1 for most games.
		 * Bit 1: Command pending (used by SWA instead of IRQ)
		 * SWA requires (status&0x38)==0 or else it loops endlessly
		 */
		return status;
	}

	//printf("Z80 Port Read %02X\n", addr);
	return 0;
}

static int Z80IRQCallback(CZ80 *Z80)
{
	return 0x38;
}

void CDSB1::SendCommand(UINT8 data)
{
	/*
	 * Commands are buffered in a FIFO. This probably does not actually exist
	 * on the real DSB but is necessary because the Z80 is not really synced
	 * up with the other CPUs and must process all commands it has received
	 * over the course of a frame at once.
	 */
	fifo[fifoIdxW++] = data;
	fifoIdxW &= FIFO_STACK_SIZE_MASK;
	//printf("Write FIFO: %02X\n", data);

	// Have we caught up to the read pointer?
#ifdef DEBUG
	if (fifoIdxW == fifoIdxR)
		printf("DSB1 FIFO overflow!\n");
#endif
}

void CDSB1::RunFrame(float *audioL, float *audioR, int numSamples)
{
	const int mpegSamples = MpegSamplesForFrame(numSamples, mpegFrameCarry);

	if (!m_config["EmulateDSB"].ValueAs<bool>())
	{
		// DSB code applies SCSP volume, too, so we must still mix
		memset(mpegL, 0, mpegSamples*sizeof(INT16));
		memset(mpegR, 0, mpegSamples*sizeof(INT16));
		retainedSamples = Resampler.UpSampleAndMix(audioL, audioR, mpegL, mpegR, 0, 0, numSamples, mpegSamples, 44100, 32000);
		return;
	}

	int		cycles;
	// The DSB1 CPU is 4 MHz, and one emulated frame is 44100/numSamples of a second.
	// Deriving the budget from numSamples (as DSB2 already does) keeps it correct at
	// the arcade's 57.524 Hz; the old 4000000/60 assumed a 60 Hz frame and left the
	// 68K about 4.1% short of its real work.
	const int framePeriod = (int)((4000000.0 * (double)numSamples) / 44100.0);
	// While FIFO not empty, fire interrupts, run for up to one frame
	for (cycles = framePeriod; (cycles > 0) && (fifoIdxR != fifoIdxW);  )
	{
		Z80.SetINT(true);	// fire an IRQ to indicate pending command
		//printf("Z80 INT fired\n");
		cycles -= Z80.Run(500);
	}

	// Run remaining cycles
	Z80.Run(cycles);

	//printf("VOLUME=%02X STEREO=%02X\n", volume, stereo);

	// Convert volume from 0x00-0x7F -> 0x00-0xFF
	UINT8 v = (UINT8) ((float) volume * (float)(255.0/127.0));

	// Decode MPEG for this frame
	MpegDec::DecodeAudio(&mpegL[retainedSamples], &mpegR[retainedSamples], mpegSamples - retainedSamples);
	retainedSamples = Resampler.UpSampleAndMix(audioL, audioR, mpegL, mpegR, v, v, numSamples, mpegSamples, 44100, 32000);
}

void CDSB1::Reset(void)
{
	MpegDec::Stop();
	Resampler.Reset();
	retainedSamples = 0;
	mpegFrameCarry = 0.0;

	memset(fifo, 0, sizeof(fifo));
	fifoIdxW = fifoIdxR = 0;

	status = 1;
	mpegState = 0;	// why doesn't RB ever init this?
	volume = 0x7F;	// full volume
	usingLoopStart = 0;

	// Even if DSB emulation is disabled, must reset to establish valid Z80 state
	Z80.Reset();
	DebugLog("DSB1 Reset\n");
}

void CDSB1::SaveState(CBlockFile *StateFile)
{
	UINT32	playOffset, endOffset;
	UINT8	isPlaying;

	StateFile->NewBlock("DSB1", __FILE__);

	// MPEG playback state. A legacy reader rebuilds the decoder from the two ranges below
	// whenever this flag is set, so report "not playing" when those fields are the board's
	// 0x80008000 sentinel even though the decoder is running: such a reader would otherwise
	// restore an invalid window and hang. The DSB1 MPEG block carries the real window.
	const bool legacyRangeValid = mpegROM != nullptr &&
		usingMPEGStart < usingMPEGEnd && usingMPEGEnd <= kMpegRomSize;
	isPlaying	= (UINT8)(MpegDec::IsLoaded() && legacyRangeValid);
	playOffset	= (UINT32)MpegDec::GetPosition();
	endOffset	= 0;

	StateFile->Write(&isPlaying, sizeof(isPlaying));
	StateFile->Write(&playOffset, sizeof(playOffset));
	StateFile->Write(&endOffset, sizeof(endOffset));
	StateFile->Write(&usingMPEGStart, sizeof(usingMPEGStart));
	StateFile->Write(&usingMPEGEnd, sizeof(usingMPEGEnd));
	StateFile->Write(&usingLoopStart, sizeof(usingLoopStart));
	StateFile->Write(&usingLoopEnd, sizeof(usingLoopEnd));

	// MPEG board state
	StateFile->Write(ram, 0x8000);
	StateFile->Write(fifo, sizeof(fifo));
	StateFile->Write(&fifoIdxR, sizeof(fifoIdxR));
	StateFile->Write(&fifoIdxW, sizeof(fifoIdxW));
	StateFile->Write(&mpegStart, sizeof(mpegStart));
	StateFile->Write(&mpegEnd, sizeof(mpegEnd));
	StateFile->Write(&mpegState, sizeof(mpegState));
	StateFile->Write(&loopStart, sizeof(loopStart));
	StateFile->Write(&loopEnd, sizeof(loopEnd));
	StateFile->Write(&status, sizeof(status));
	StateFile->Write(&cmdLatch, sizeof(cmdLatch));
	StateFile->Write(&volume, sizeof(volume));
	StateFile->Write(&stereo, sizeof(stereo));

	// Z80 CPU state
	Z80.SaveState(StateFile, "DSB1 Z80");

	// The window the decoder is actually playing (see SaveMpegActiveWindow).
	SaveMpegActiveWindow(StateFile, "DSB1 MPEG", mpegROM);
}

void CDSB1::LoadState(CBlockFile *StateFile)
{
	UINT32	playOffset, endOffset;
	UINT8	isPlaying;

	if (Result::OKAY != StateFile->FindBlock("DSB1"))
	{
		ErrorLog("Unable to load Digital Sound Board state. Save state file is corrupt.");
		return;
	}

	StateFile->Read(&isPlaying, sizeof(isPlaying));
	StateFile->Read(&playOffset, sizeof(playOffset));
	StateFile->Read(&endOffset, sizeof(endOffset));
	StateFile->Read(&usingMPEGStart, sizeof(usingMPEGStart));
	StateFile->Read(&usingMPEGEnd, sizeof(usingMPEGEnd));
	StateFile->Read(&usingLoopStart, sizeof(usingLoopStart));
	StateFile->Read(&usingLoopEnd, sizeof(usingLoopEnd));
	StateFile->Read(ram, 0x8000);
	StateFile->Read(fifo, sizeof(fifo));
	StateFile->Read(&fifoIdxR, sizeof(fifoIdxR));
	StateFile->Read(&fifoIdxW, sizeof(fifoIdxW));
	StateFile->Read(&mpegStart, sizeof(mpegStart));
	StateFile->Read(&mpegEnd, sizeof(mpegEnd));
	StateFile->Read(&mpegState, sizeof(mpegState));
	StateFile->Read(&loopStart, sizeof(loopStart));
	StateFile->Read(&loopEnd, sizeof(loopEnd));
	StateFile->Read(&status, sizeof(status));
	StateFile->Read(&cmdLatch, sizeof(cmdLatch));
	StateFile->Read(&volume, sizeof(volume));
	StateFile->Read(&stereo, sizeof(stereo));

	Z80.LoadState(StateFile, "DSB1 Z80");

	if (DsbLoadDebugEnabled())
	{
		InfoLog("LoadDebug dsb1 raw: loaded=%u start=%u end=%u loopStart=%u loopEnd=%u offset=%u",
			(unsigned)isPlaying, (unsigned)usingMPEGStart, (unsigned)usingMPEGEnd,
			(unsigned)usingLoopStart, (unsigned)usingLoopEnd, (unsigned)playOffset);
		// The board registers are logged too: a sentinel in the shadows above is only suspicious
		// when the live registers and the decoder disagree with them.
		InfoLog("LoadDebug dsb1 regs: mpegStart=%u mpegEnd=%u mpegState=%u loopStart=%u loopEnd=%u status=%u latch=%u",
			(unsigned)mpegStart, (unsigned)mpegEnd, (unsigned)mpegState,
			(unsigned)loopStart, (unsigned)loopEnd, (unsigned)status, (unsigned)cmdLatch);
	}

	// Restart MPEG audio. The companion block is authoritative when present: the register
	// shadows above can be a sentinel while a track is still playing. Fall back to the legacy
	// fields for states written before that block existed. Both are external input: usingMPEGStart/End
	// are absolute offsets into the 16 MB MPEG ROM, and usingLoopStart is an absolute offset while
	// usingLoopEnd is already a length (see the assignments in IOWrite8). Never hand an inverted or
	// out-of-range window to MpegDec; a negative size used to reach DecodeAudio() and spin there.
	if (LoadMpegActiveWindow(StateFile, "DSB1 MPEG", mpegROM))
		return;

	const bool mainRangeValid = mpegROM != nullptr &&
		usingMPEGStart < usingMPEGEnd && usingMPEGEnd <= kMpegRomSize;
	const bool loopRangeValid = usingLoopStart < kMpegRomSize && usingLoopEnd > 0 &&
		usingLoopEnd <= kMpegRomSize - usingLoopStart;

	if (!isPlaying || !mainRangeValid)
	{
		MpegDec::Stop();
	}
	else
	{
		MpegDec::SetMemory(mpegROM, (int)usingMPEGStart, (int)(usingMPEGEnd - usingMPEGStart), false);

		if (loopRangeValid)
			MpegDec::UpdateMemory(mpegROM, (int)usingLoopStart, (int)usingLoopEnd, true);

		const unsigned activeSize = loopRangeValid ? usingLoopEnd : (usingMPEGEnd - usingMPEGStart);
		if (playOffset >= activeSize)
			playOffset = 0;
		MpegDec::SetPosition((int)playOffset);
	}
}

// Offsets of memory regions within DSB1's pool
#define DSB1_OFFSET_RAM			0		// 32KB Z80 RAM
#define DSB1_OFFSET_MPEG_LEFT	0x8000	// 1604 bytes (48 KHz max., 1/60th second, 2 extra = 2*(48000/60+2)) left MPEG buffer
#define DSB1_OFFSET_MPEG_RIGHT	0x8644	// 1604 bytes right MPEG buffer
#define DSB1_MEMORY_POOL_SIZE	(0x8000 + 0x644 + 0x644)

Result CDSB1::Init(const UINT8 *progROMPtr, const UINT8 *mpegROMPtr)
{
	float	memSizeMB = (float)DSB1_MEMORY_POOL_SIZE/(float)0x100000;

	// Receive ROM
	progROM = progROMPtr;
	mpegROM = mpegROMPtr;

	// Allocate memory pool
	memoryPool = new(std::nothrow) UINT8[DSB1_MEMORY_POOL_SIZE];
	if (NULL == memoryPool)
		return ErrorLog("Insufficient memory for DSB1 board (needs %1.1f MB).", memSizeMB);
	memset(memoryPool, 0, DSB1_MEMORY_POOL_SIZE);

	// Set up memory pointers
	ram = &memoryPool[DSB1_OFFSET_RAM];
	mpegL = (INT16 *) &memoryPool[DSB1_OFFSET_MPEG_LEFT];
	mpegR = (INT16 *) &memoryPool[DSB1_OFFSET_MPEG_RIGHT];

	// Initialize Z80 CPU
	Z80.Init(this, Z80IRQCallback);

	retainedSamples = 0;
	mpegFrameCarry = 0.0;

	return Result::OKAY;
}

CZ80 *CDSB1::GetZ80(void)
{
	return &Z80;
}

CDSB1::CDSB1(const Util::Config::Node &config)
  : m_config(config),
    Resampler(config)
{
	progROM		= NULL;
	mpegROM		= NULL;
	memoryPool	= NULL;
	ram			= NULL;
	mpegL		= NULL;
	mpegR		= NULL;

	// must init these otherwise we end up trying to read illegal addresses
	mpegStart	= 0;
	mpegEnd		= 0;
	mpegState	= 0;
	loopStart	= 0;
	loopEnd		= 0;

	DebugLog("Built DSB1 Board\n");
}

CDSB1::~CDSB1(void)
{
	delete [] memoryPool;
	memoryPool = NULL;

	progROM	= NULL;
	mpegROM	= NULL;
	ram		= NULL;
	mpegL	= NULL;
	mpegR	= NULL;

	DebugLog("Destroyed DSB1 Board\n");
}


/******************************************************************************
 Digital Sound Board Type 2: 68K CPU
******************************************************************************/

// MPEG state machine
enum
{
	ST_IDLE = 0,
	ST_GOT14,	// start/loop addr
	ST_14_0,
	ST_14_1,
	ST_GOT24,	// end addr
	ST_24_0,
	ST_24_1,
	ST_GOT74,
	ST_GOTA0,
	ST_GOTA1,
	ST_GOTA3,
	ST_GOTA4,
	ST_GOTA5,
	ST_GOTA7,
	ST_GOTB0,
	ST_GOTB1,
	ST_GOTB2,
	ST_GOTB4,
	ST_GOTB5,
	ST_GOTB6
};

static constexpr const char *stateName[] =
{
	"idle",
	"st_got_14",
	"st_14_0",
	"st_14_1",
	"st_got24",
	"st_24_0",
	"st_24_1",
	"st_got74",
	"st_gota0",
	"st_gota1",
	"st_gota3",
	"st_gota4",
	"st_gota5",
	"st_gota7",
	"st_gotb0",
	"st_gotb1",
	"st_gotb2",
	"st_gotb4",
	"st_gotb5",
	"st_gotb6"
};

void CDSB2::WriteMPEGFIFO(UINT8 byte)
{
	DebugLog("fifo: %x (state %s)\n", byte, stateName[mpegState]);
	switch (mpegState)
	{
		case ST_IDLE:
			if (byte == 0x14) mpegState = ST_GOT14;
			else if (byte == 0x15) mpegState = ST_GOT14;
			else if (byte == 0x24) mpegState = ST_GOT24;
			else if (byte == 0x25) mpegState = ST_GOT24;

			else if (byte == 0x74 || byte == 0x75)	// "start play"
			{
				usingLoopStart	= 0;
				usingLoopEnd	= 0;
				usingMPEGStart	= mpegStart;
				usingMPEGEnd	= mpegEnd;
				playing			= 1;

				MpegDec::SetMemory(mpegROM, mpegStart, mpegEnd - mpegStart, false);

				mpegState = ST_IDLE;
			}

			else if (byte == 0x84 || byte == 0x85)
			{
				MpegDec::Stop();
				playing = 0;
			}

			else if (byte == 0xa0) mpegState = ST_GOTA0;
			else if (byte == 0xa1) mpegState = ST_GOTA1;
			else if (byte == 0xA3) mpegState = ST_GOTA3;	// volume (front right?)
			else if (byte == 0xa4) mpegState = ST_GOTA4;
			else if (byte == 0xa5) mpegState = ST_GOTA5;
			else if (byte == 0xA7) mpegState = ST_GOTA7;	// volume (rear right?)

			else if (byte == 0xb0) mpegState = ST_GOTB0;
			else if (byte == 0xb1) mpegState = ST_GOTB1;
			else if (byte == 0xB2) mpegState = ST_GOTB2;	// volume (front left?)
			else if (byte == 0xb4) mpegState = ST_GOTB4;
			else if (byte == 0xb5) mpegState = ST_GOTB5;
			else if (byte == 0xB6) mpegState = ST_GOTB6;	// volume (rear left?)

			break;

		case ST_GOT14:
			mpegStart &= 0x00FFFF;
			mpegStart |= (byte<<16);
			mpegState++;
			break;
		case ST_14_0:
			mpegStart &= 0xFF00FF;
			mpegStart |= (byte<<8);
			mpegState++;
			break;
		case ST_14_1:
			mpegStart &= 0xFFFF00;
			mpegStart |= (byte);
			mpegState = ST_IDLE;

			if (playing)
			{
				usingLoopStart	= mpegStart;
				usingLoopEnd	= mpegEnd - mpegStart;
				MpegDec::UpdateMemory(mpegROM, usingLoopStart, usingLoopEnd, true);
			}

			break;
		case ST_GOT24:
			mpegEnd &= 0x00FFFF;
			mpegEnd |= (byte<<16);
			mpegState++;
			break;
		case ST_24_0:
			mpegEnd &= 0xFF00FF;
			mpegEnd |= (byte<<8);
			mpegState++;
			break;
		case ST_24_1:
			mpegEnd &= 0xFFFF00;
			mpegEnd |= (byte);

			// Do NOT reset the output routing here. Sega Rally 2 sets 0xA0/0xB1 to pick one of the
			// two mono programmes and then updates the end/loop address; resetting to stereo on
			// every 0x24 write made both programmes play at once. Resetting is done in Reset() and
			// by an explicit 0x00 argument to 0xA0 or 0xB1.
			mpegState = ST_IDLE;
			break;

		case ST_GOTA0:
			// Channel 0. Its samples come out of the decoder's right buffer (see DSB.h).
			stereo = (byte != 0x00) ? StereoMode::Channel0 : StereoMode::Stereo;
			mpegState = ST_IDLE;
			break;

		case ST_GOTA4:	// dayto2pe plays advertise tune from this state by writing 0x75
			mpegState = ST_IDLE;
			if (byte == 0x75)
			{
				usingLoopStart	= 0;
				usingLoopEnd	= 0;
				usingMPEGStart	= mpegStart;
				usingMPEGEnd	= mpegEnd;
				playing			= 1;
				MpegDec::SetMemory(mpegROM, mpegStart, mpegEnd - mpegStart, false);
			}
			break;
		case ST_GOTA5:
			mpegState = ST_IDLE;
			break;
		case ST_GOTB1:
			// Channel 1. Its samples come out of the decoder's left buffer (see DSB.h).
			stereo = (byte != 0x00) ? StereoMode::Channel1 : StereoMode::Stereo;
			mpegState = ST_IDLE;
			break;
		case ST_GOTB4:
			mpegState = ST_IDLE;
			if (byte == 0x96)
			{
				MpegDec::Stop();
				playing = 0;
			}
			break;
		case ST_GOTB5:
			mpegState = ST_IDLE;
			break;

		/*
		 * Speaker Volume:
		 *
		 * Daytona 2 uses B6, A7 for rear speakers; B2, A3 for front speakers.
		 *
		 * Star Wars Trilogy uses B0, A1 but B4, A4, B1, A1, B5, and A5 may be
		 * volume/speaker related.
		 *
		 * Sega Rally 2 uses B0, A0 and B4, A4 when setting all four but B0, A1
		 * when only setting two (in-game)
		 */
		case ST_GOTB6:	// rear left(?) volume
			volume[0] = byte;
			mpegState = ST_IDLE;
			break;
		case ST_GOTB0:	// left volume
			volume[0] = byte;
			//printf("Set L Volume: %02X\n", byte);
			mpegState = ST_IDLE;
			break;
		case ST_GOTA7:	// rear right(?) volume
			volume[1] = byte;
			mpegState = ST_IDLE;
			break;
		case ST_GOTA1:	// right volume
			volume[1] = byte;
			mpegState = ST_IDLE;
			break;
		/*case ST_GOTA0:
			volume[1] = byte;
			//printf("Set R Volume: %02X\n", byte);
			mpegState = ST_IDLE;
			break;*/
		case ST_GOTB2:
			mpegState = ST_IDLE;
			break;
		case ST_GOTA3:
			mpegState = ST_IDLE;
			break;
		default:
			break;
	}
}

UINT8 CDSB2::Read8(UINT32 addr)
{
	if (addr < (128*1024))
		return progROM[addr^1];

	if (addr == 0xc00001)
	{
		return cmdLatch;
	}
	if (addr == 0xc00003)	// bit 0 = command valid
	{
		return 1;
	}

	if (addr == 0xe80001)
	{
		return 0x01;	// MPEG busy status: bit 1 = busy
	}			// polled by irq2, stored | 0x10 at f01010

	if ((addr >= 0xf00000) && (addr < 0xf10000))
	{
		addr &= 0x1ffff;
		return ram[addr^1];
	}

	//printf("R8 @ %x\n", addr);
	return 0;
}

UINT16 CDSB2::Read16(UINT32 addr)
{
	if (addr < (128*1024))
	{
		return *(UINT16 *) &progROM[addr];
	}
	if ((addr >= 0xf00000) && (addr < 0xf20000))
	{
		addr &= 0x1ffff;
		return *(UINT16 *) &ram[addr];
	}

	//printf("R16 @ %x\n", addr);
	return 0;
}

UINT32 CDSB2::Read32(UINT32 addr)
{
	UINT32 hi, lo;

	if (addr < (128*1024))
	{
		hi = *(UINT16 *) &progROM[addr];
		lo = *(UINT16 *) &progROM[addr+2];
		return (hi<<16)|lo;
	}
	if ((addr >= 0xf00000) && (addr < 0xf20000))
	{
		addr &= 0x1ffff;
		hi = *(UINT16 *) &ram[addr];
		lo = *(UINT16 *) &ram[addr+2];
		return (hi<<16)|lo;
	}
	//printf("R32 @ %x\n", addr);
	return 0;
}

void CDSB2::Write8(UINT32 addr, UINT8 data)
{
	if ((addr >= 0xf00000) && (addr < 0xf20000))
	{
		addr &= 0x1ffff;
		ram[addr^1] = data;
		return;
	}

	if (addr == 0xd00001) return;

	if (addr == 0xe00003)
	{
		WriteMPEGFIFO(data);
		return;
	}

	//printf("W8: %x @ %x (PC=%x)\n", data, addr, m68k_get_reg(NULL, M68K_REG_PC));
}

void CDSB2::Write16(UINT32 addr, UINT16 data)
{
	if ((addr >= 0xf00000) && (addr < 0xf20000))
	{
		addr &= 0x1ffff;
		*(UINT16 *) &ram[addr] = data;
		return;
	}
	//printf("W16: %x @ %x\n", data, addr);
}

void CDSB2::Write32(UINT32 addr, UINT32 data)
{
	if ((addr >= 0xf00000) && (addr < 0xf20000))
	{
		addr &= 0x1ffff;
		*(UINT16 *) &ram[addr+0] = (data>>16);
		*(UINT16 *) &ram[addr+2] = data&0xFFFF;
		return;
	}
	//printf("W32: %x @ %x\n", data, addr);
}

void CDSB2::SendCommand(UINT8 data)
{
	/*
	 * Commands are buffered in a FIFO. This probably does not actually exist
	 * on the real DSB but is necessary because the 68K is not really synced
	 * up with the other CPUs and must process all commands it has received
	 * over the course of a frame at once.
	 */
	fifo[fifoIdxW++] = data;
	fifoIdxW &= FIFO_STACK_SIZE_MASK;
	//printf("Write FIFO: %02X\n", data);

	// Have we caught up to the read pointer?
#ifdef DEBUG
	if (fifoIdxW == fifoIdxR)
		printf("DSB2 FIFO overflow!\n");
#endif
}


void CDSB2::RunFrame(float *audioL, float *audioR, int numSamples)
{
  const int mpegSamples = MpegSamplesForFrame(numSamples, mpegFrameCarry);

  // The 68K DBS runs for one frame of emulated time. A frame is 1/RefreshRate seconds and the CPU
  // clock is 11 MHz, so derive the cycle budget from numSamples (which is 44100/RefreshRate to
  // within the fractional carry) rather than assuming 60 Hz. At 60 Hz this yields the same
  // 11000000/60 as before.
  const int framePeriod = (int)((11000000.0 * (double)numSamples) / 44100.0);

  if (!m_config["EmulateDSB"].ValueAs<bool>())
  {
    // DSB code applies SCSP volume, too, so we must still mix
    memset(mpegL, 0, mpegSamples * sizeof(INT16));
    memset(mpegR, 0, mpegSamples * sizeof(INT16));
    retainedSamples = Resampler.UpSampleAndMix(audioL, audioR, mpegL, mpegR, 0, 0, numSamples, mpegSamples, 44100, 32000);
    return;
  }

  M68KSetContext(&M68K);
  //printf("DSB2 run frame PC=%06X\n", M68KGetPC());

  // While FIFO not empty...
  while (fifoIdxR != fifoIdxW)
  {
    cmdLatch = fifo[fifoIdxR];	// retrieve next command byte
    fifoIdxR++;
    fifoIdxR &= FIFO_STACK_SIZE_MASK;

    M68KSetIRQ(1);	// indicate pending command
    //printf("68K INT fired\n");
    m_cyclesElapsedThisFrame += M68KRun(500);
  }

  // Matthew Daniels made the interesting discovery that IRQ2 may in fact be a timer interrupt
  // rather than a per-frame interrupt.For Daytona 2 and Sega Rally 2, assuming a value
  // of 1KHz fixes music fade outs and some timing issues. It is very likely this is a
  // configurable timer and we should be on the look-out for games which appear to use
  // different values. It is equally likely that all games share a similar code base and
  // use 1KHz as the timer rate.
  while (m_cyclesElapsedThisFrame < framePeriod)
  {
    if (m_cyclesElapsedThisFrame >= m_nextTimerInterruptCycles)
    {
      // Fire timer interrupt and schedule next one
      M68KSetIRQ(2);
      m_nextTimerInterruptCycles += k_timerPeriod;
    }
    int cyclesToRun = (std::min)(m_nextTimerInterruptCycles, framePeriod) - m_cyclesElapsedThisFrame;
    m_cyclesElapsedThisFrame += M68KRun(cyclesToRun);
  }
  m_cyclesElapsedThisFrame -= framePeriod;
  m_nextTimerInterruptCycles -= framePeriod;

  M68KGetContext(&M68K);

  // Decode MPEG for this frame
  MpegDec::DecodeAudio(&mpegL[retainedSamples], &mpegR[retainedSamples], mpegSamples - retainedSamples);

  INT16 *leftChannelSource = nullptr;
  INT16 *rightChannelSource = nullptr;
  UINT8 volL = 0, volR = 0;

  switch (stereo)
  {
  default:
    case StereoMode::Stereo:
      leftChannelSource = mpegL;
      rightChannelSource = mpegR;
      volL = volume[0];
      volR = volume[1];
      break;
    case StereoMode::Channel0:
      // One programme occupies the right half of the stereo stream; put it on both outputs.
      // volume[0] is the level the game sends for this channel (command 0xB0).
      leftChannelSource = mpegR;
      rightChannelSource = mpegR;
      volL = volume[0];
      volR = volume[0];
      break;
    case StereoMode::Channel1:
      leftChannelSource = mpegL;
      rightChannelSource = mpegL;
      volL = volume[1];
      volR = volume[1];
      break;
  }

  retainedSamples = Resampler.UpSampleAndMix(audioL, audioR, leftChannelSource, rightChannelSource, volL, volR, numSamples, mpegSamples, 44100, 32000);
}

void CDSB2::Reset(void)
{
	MpegDec::Stop();
	Resampler.Reset();
	retainedSamples = 0;
	mpegFrameCarry = 0.0;

	memset(fifo, 0, sizeof(fifo));
	fifoIdxW = fifoIdxR = 0;

	mpegState = ST_IDLE;
	mpegStart = 0;
	mpegEnd = 0;
	playing = 0;
	volume[0] = 0xFF;	// set to max volume in case we miss the volume commands
	volume[1] = 0xFF;
	stereo = StereoMode::Stereo;

	// Even if DSB emulation is disabled, must reset to establish valid Z80 state
	M68KSetContext(&M68K);
	M68KReset();
	//printf("DSB2 PC=%06X\n", M68KGetPC());
	M68KGetContext(&M68K);

	m_cyclesElapsedThisFrame = 0;
	m_nextTimerInterruptCycles = k_timerPeriod;

	DebugLog("DSB2 Reset\n");
}

void CDSB2::SaveState(CBlockFile *StateFile)
{
	UINT32	playOffset, endOffset;
	UINT8	isPlaying;

	StateFile->NewBlock("DSB2", __FILE__);

	// MPEG playback state. Same reasoning as CDSB1::SaveState(): the legacy ranges can be a sentinel
	// while a track is playing, so only claim them for a legacy reader when they are usable.
	const bool legacyRangeValid = mpegROM != nullptr &&
		usingMPEGStart < usingMPEGEnd && usingMPEGEnd <= kMpegRomSize;
	isPlaying	= (UINT8)(MpegDec::IsLoaded() && legacyRangeValid);
	playOffset	= (UINT32)MpegDec::GetPosition();
	endOffset	= 0;

	StateFile->Write(&isPlaying, sizeof(isPlaying));
	StateFile->Write(&playOffset, sizeof(playOffset));
	StateFile->Write(&endOffset, sizeof(endOffset));
	StateFile->Write(&usingMPEGStart, sizeof(usingMPEGStart));
	StateFile->Write(&usingMPEGEnd, sizeof(usingMPEGEnd));
	StateFile->Write(&usingLoopStart, sizeof(usingLoopStart));
	StateFile->Write(&usingLoopEnd, sizeof(usingLoopEnd));

	// MPEG board state
	StateFile->Write(ram, 0x20000);
	StateFile->Write(fifo, sizeof(fifo));
	StateFile->Write(&fifoIdxR, sizeof(fifoIdxR));
	StateFile->Write(&fifoIdxW, sizeof(fifoIdxW));
	StateFile->Write(&cmdLatch, sizeof(cmdLatch));
	StateFile->Write(&mpegState, sizeof(mpegState));
	StateFile->Write(&mpegStart, sizeof(mpegStart));
	StateFile->Write(&mpegEnd, sizeof(mpegEnd));
	StateFile->Write(&playing, sizeof(playing));
	StateFile->Write(volume, sizeof(volume));
	StateFile->Write(&stereo, sizeof(stereo));

	// 68K CPU state
	M68KSetContext(&M68K);
	M68KSaveState(StateFile, "DSB2 68K");

	// The window the decoder is actually playing (see SaveMpegActiveWindow).
	SaveMpegActiveWindow(StateFile, "DSB2 MPEG", mpegROM);

	//DEBUG
	//printf("DSB2 PC=%06X\n", M68KGetPC());
	//printf("mpegStart=%X, mpegEnd=%X\n", mpegStart, mpegEnd);
	//printf("usingMPEGStart=%X, usingMPEGEnd=%X\n", usingMPEGStart, usingMPEGEnd);
	//printf("usingLoopStart=%X, usingLoopEnd=%X\n", usingLoopStart, usingLoopEnd);
}

void CDSB2::LoadState(CBlockFile *StateFile)
{
	UINT32	playOffset, endOffset;
	UINT8	isPlaying;

	if (Result::OKAY != StateFile->FindBlock("DSB2"))
	{
		ErrorLog("Unable to load Digital Sound Board state. Save state file is corrupt.");
		return;
	}

	StateFile->Read(&isPlaying, sizeof(isPlaying));
	StateFile->Read(&playOffset, sizeof(playOffset));
	StateFile->Read(&endOffset, sizeof(endOffset));
	StateFile->Read(&usingMPEGStart, sizeof(usingMPEGStart));
	StateFile->Read(&usingMPEGEnd, sizeof(usingMPEGEnd));
	StateFile->Read(&usingLoopStart, sizeof(usingLoopStart));
	StateFile->Read(&usingLoopEnd, sizeof(usingLoopEnd));

	StateFile->Read(ram, 0x20000);
	StateFile->Read(fifo, sizeof(fifo));
	StateFile->Read(&fifoIdxR, sizeof(fifoIdxR));
	StateFile->Read(&fifoIdxW, sizeof(fifoIdxW));
	StateFile->Read(&cmdLatch, sizeof(cmdLatch));
	StateFile->Read(&mpegState, sizeof(mpegState));
	StateFile->Read(&mpegStart, sizeof(mpegStart));
	StateFile->Read(&mpegEnd, sizeof(mpegEnd));
	StateFile->Read(&playing, sizeof(playing));
	StateFile->Read(volume, sizeof(volume));
	StateFile->Read(&stereo, sizeof(stereo));

	M68KSetContext(&M68K);
	M68KLoadState(StateFile, "DSB2 68K");
	M68KGetContext(&M68K);

	// Technically these should be saved/restored rather than being reset but that would mean
	// the save state format has to be modified and the difference would be imperceptible anyway
	m_cyclesElapsedThisFrame = 0;
	m_nextTimerInterruptCycles = k_timerPeriod;

	if (DsbLoadDebugEnabled())
		InfoLog("LoadDebug dsb2 raw: loaded=%u start=%u end=%u loopStart=%u loopEnd=%u offset=%u",
			(unsigned)isPlaying, (unsigned)usingMPEGStart, (unsigned)usingMPEGEnd,
			(unsigned)usingLoopStart, (unsigned)usingLoopEnd, (unsigned)playOffset);

	// Restart MPEG audio; the companion block wins over the register shadows when present. Same
	// validation as CDSB1::LoadState().
	if (LoadMpegActiveWindow(StateFile, "DSB2 MPEG", mpegROM))
		return;

	const bool mainRangeValid = mpegROM != nullptr &&
		usingMPEGStart < usingMPEGEnd && usingMPEGEnd <= kMpegRomSize;
	const bool loopRangeValid = usingLoopStart < kMpegRomSize && usingLoopEnd > 0 &&
		usingLoopEnd <= kMpegRomSize - usingLoopStart;

	if (!isPlaying || !mainRangeValid)
	{
		MpegDec::Stop();
	}
	else
	{
		MpegDec::SetMemory(mpegROM, (int)usingMPEGStart, (int)(usingMPEGEnd - usingMPEGStart), false);

		if (loopRangeValid)
			MpegDec::UpdateMemory(mpegROM, (int)usingLoopStart, (int)usingLoopEnd, true);

		const unsigned activeSize = loopRangeValid ? usingLoopEnd : (usingMPEGEnd - usingMPEGStart);
		if (playOffset >= activeSize)
			playOffset = 0;
		MpegDec::SetPosition((int)playOffset);
	}

	//DEBUG
	//printf("DSB2 PC=%06X\n", M68KGetPC());
	//printf("mpegStart=%X, mpegEnd=%X\n", mpegStart, mpegEnd);
	//printf("usingMPEGStart=%X, usingMPEGEnd=%X\n", usingMPEGStart, usingMPEGEnd);
	//printf("usingLoopStart=%X, usingLoopEnd=%X\n", usingLoopStart, usingLoopEnd);

}

// Offsets of memory regions within DSB2's pool
#define DSB2_OFFSET_RAM			0		// 128KB 68K RAM
#define DSB2_OFFSET_MPEG_LEFT	0x20000	// 1604 bytes (48 KHz max., 1/60th second, 2 extra = 2*(48000/60+2)) left MPEG buffer
#define DSB2_OFFSET_MPEG_RIGHT	0x20644	// 1604 bytes right MPEG buffer
#define DSB2_MEMORY_POOL_SIZE	(0x20000 + 0x644 + 0x644)

Result CDSB2::Init(const UINT8 *progROMPtr, const UINT8 *mpegROMPtr)
{
	float memSizeMB = (float)DSB2_MEMORY_POOL_SIZE/(float)0x100000;

	// Receive ROM
	progROM = progROMPtr;
	mpegROM = mpegROMPtr;

	// Allocate memory pool
	memoryPool = new(std::nothrow) UINT8[DSB2_MEMORY_POOL_SIZE];
	if (NULL == memoryPool)
		return ErrorLog("Insufficient memory for DSB2 board (needs %1.1f MB).", memSizeMB);
	memset(memoryPool, 0, DSB2_MEMORY_POOL_SIZE);

	// Set up memory pointers
	ram = &memoryPool[DSB2_OFFSET_RAM];
	mpegL = (INT16 *) &memoryPool[DSB2_OFFSET_MPEG_LEFT];
	mpegR = (INT16 *) &memoryPool[DSB2_OFFSET_MPEG_RIGHT];

	// Initialize 68K CPU
	M68KSetContext(&M68K);
	M68KInit();
	M68KAttachBus(this);
	M68KSetIRQCallback(NULL);	// use default behavior (autovector, clear interrupt)
	M68KGetContext(&M68K);

	retainedSamples = 0;
	mpegFrameCarry = 0.0;

	return Result::OKAY;
}

M68KCtx *CDSB2::GetM68K(void)
{
	return &M68K;
}

CDSB2::CDSB2(const Util::Config::Node &config)
  : m_config(config),
    Resampler(config)
{
	progROM		= NULL;
	mpegROM		= NULL;
	memoryPool	= NULL;
	ram			= NULL;
	mpegL		= NULL;
	mpegR		= NULL;

	cmdLatch	= 0;
	mpegState	= ST_IDLE;
	mpegStart	= 0;
	mpegEnd		= 0;
	playing		= 0;

	retainedSamples = 0;
	mpegFrameCarry = 0.0;

	memset(fifo, 0, sizeof(fifo));
	fifoIdxW = fifoIdxR = 0;
	volume[0] = 0xFF;	// set to max volume in case we miss the volume commands
	volume[1] = 0xFF;
	stereo = StereoMode::Stereo;

	m_cyclesElapsedThisFrame = 0;
	m_nextTimerInterruptCycles = k_timerPeriod;

	usingLoopStart = 0;
	usingLoopEnd = 0;
	usingMPEGStart = 0;
	usingMPEGEnd = 0;

	DebugLog("Built DSB2 Board\n");
}

CDSB2::~CDSB2(void)
{
	if (memoryPool != NULL)
	{
		delete [] memoryPool;
		memoryPool = NULL;
	}

	progROM	= NULL;
	mpegROM	= NULL;
	ram		= NULL;
	mpegL	= NULL;
	mpegR	= NULL;

	DebugLog("Destroyed DSB2 Board\n");
}
