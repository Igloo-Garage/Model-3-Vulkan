/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2021 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
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
 * DSB.h
 *
 * Header file for the Sega Digital Sound Board (Type 1 and 2) devices. CDSB1
 * is an implementation of the Z80-based DSB Type 1, and CDSB2 is the 68K-based
 * Type 2 board. Only one may be active at a time because they rely on non-
 * reentrant MPEG playback code.
 */

#ifndef INCLUDED_DSB_H
#define INCLUDED_DSB_H

#include "Types.h"
#include "CPU/Bus.h"
#include "CPU/68K/68K.h"
#include "CPU/Z80/Z80.h"
#include "Util/NewConfig.h"

#include <vector>

#define FIFO_STACK_SIZE			0x100
#define FIFO_STACK_SIZE_MASK	(FIFO_STACK_SIZE - 1)

// Maximum number of 44.1 KHz samples a single emulated frame can produce. The actual count depends
// on RefreshRate and alternates as the fractional remainder carries (at the arcade rate of
// 57.524160 Hz it is 766 or 767), so per-frame audio buffers are sized for this worst case, which
// corresponds to the lowest permitted refresh rate. Use GetSamplesThisFrame() (OSD/Audio.h) to get
// the count for the frame actually being emulated.
#define MAX_SAMPLES_PER_FRAME   (44100/45)

/*
 * Length of the DSB resampling filter, shared by MpegSamplesForFrame() below and CDSBResampler. The
 * frame has to decode this much audio ahead of what it consumes so that the filter has its full
 * right-hand support at the end of the buffer; with less padding the last outputs of every frame
 * would be filtered against zeros and the music would pick up a 57.5 Hz edge artefact. 64 taps at
 * 32 kHz keep the passband flat to ~14.5 kHz and reach -80 dB by the input Nyquist frequency.
 */
constexpr int DSB_RESAMPLER_TAPS = 64;

/*
 * MpegSamplesForFrame(numSamples)
 *
 * Number of 32 KHz MPEG source samples needed to fill a frame that will produce numSamples 44.1 KHz
 * samples, including the look-ahead the resampler filter needs at the end of the block. The fraction
 * is carried in a caller-owned accumulator so the 32 kHz:44.1 kHz ratio stays exact across frames
 * even though neither count is an integer at non-60 Hz rates.
 *
 * The DSB's MPEG buffers are sized for 48 kHz at 60 Hz. At the rates actually used (32 kHz, any
 * permitted refresh rate) the worst case is 45 Hz: 32000/45 + taps + 4 = 780 samples against 802
 * allocated, so the extra look-ahead still fits.
 */
inline int MpegSamplesForFrame(int numSamples, double& carry)
{
	carry += (double)numSamples * (32000.0 / 44100.0);
	const int whole = (int)carry;
	carry -= (double)whole;
	return whole + DSB_RESAMPLER_TAPS + 4;
}

/******************************************************************************
 Resampling

 Used internally by the DSB's MPEG code. If this becomes sufficiently generic,
 it can be moved to Sound/. Not intended for general use for now.
******************************************************************************/

/*
 * CDSBResampler:
 *
 * Frame-by-frame sample rate converter for the DSB's MPEG output. It replaces the old linear
 * interpolator with a Kaiser-windowed sinc, evaluated through a polyphase table whose phase count
 * is derived from the greatest common divisor of the two rates: 32000:44100 reduces to 320:441, so
 * 441 phases reproduce the ratio exactly and the phase accumulator is integer arithmetic. There is
 * no rounding to accumulate, which the old 24.8 fixed point step did (it truncated 0.7256 to
 * 0.7227 and played every DSB track 0.41% flat).
 *
 * A frame produces exactly sizeOut samples and consumes sizeOut*inRate/outRate input samples. The
 * remainder of the block is copied back to the front for the next call, and the last k_half input
 * samples before that point are kept separately as history. Because the clock authority stays with
 * the caller's per-frame sample count (GetSamplesThisFrame() in OSD/Audio.h), the DSB can never
 * drift against the SCSP or starve: it is asked for, and delivers, exactly one frame of audio per
 * frame, at whatever RefreshRate the emulator is running.
 *
 * The input is INT16 (the MPEG decoder's native output) and the output is float in the same
 * S16-scaled domain the SCSP uses, so the two can be summed without an intermediate conversion.
 */
class CDSBResampler
{
public:
	int		UpSampleAndMix(float *outL, float *outR, INT16 *inL, INT16 *inR, UINT8 volumeL, UINT8 volumeR, int sizeOut, int sizeIn, int outRate, int inRate);
	void	Reset(void);
	CDSBResampler(const Util::Config::Node &config)
	  : m_config(config)
	{
		Reset();
		// Both DSBs run at 32 kHz into the 44.1 kHz output; build the table up front so the first
		// frame does not allocate or compute it while the audio device is already running.
		Prepare(44100, 32000);
	}
private:
	static constexpr int k_taps = DSB_RESAMPLER_TAPS;
	static constexpr int k_half = k_taps / 2;

	void	Prepare(int outRate, int inRate);

	const Util::Config::Node &m_config;

	// Polyphase kernel: k_phases rows of k_taps coefficients (see DSB.cpp).
	std::vector<float> m_coeff;
	int		m_phases = 0;   // outRate/gcd(inRate, outRate): number of phases
	int		m_step = 0;     // inRate/gcd(inRate, outRate): input advance per output, numerator
	int		m_outRate = 0;
	int		m_inRate = 0;

	// Position of the next output: m_inPos is an index into the current input block and
	// m_phase/m_phases its fraction. Integer arithmetic only, so the ratio cannot drift.
	int		m_inPos = 0;
	int		m_phase = 0;

	// The k_half input samples immediately preceding the current block, so the first outputs of a
	// frame can be filtered without a special case at the buffer start.
	float	m_histL[k_half] = {};
	float	m_histR[k_half] = {};
};


/******************************************************************************
 DSB Base Class
******************************************************************************/

/*
 * CDSB:
 *
 * Abstract base class defining the common interface for both DSB board types.
 */

class CDSB: public IBus
{
public:
	/*
	 * SendCommand(data):
	 *
	 * Send a MIDI command to the DSB board.
	 */
	virtual void SendCommand(UINT8 data) = 0;

	/*
	 * RunFrame(audioL, audioR, numSamples):
	 *
	 * Runs one frame and updates the MPEG audio. Audio is mixed into the
	 * supplied buffers (they are assumed to already contain audio data).
	 *
	 * Parameters:
	 *		audioL		Left audio channel, one frame.
	 *		audioR		Right audio channel.
	 *		numSamples	Number of 44.1 KHz samples this frame covers, as returned by
	 *					GetSamplesThisFrame(). Varies by a sample between frames because a frame
	 *					is 1/RefreshRate seconds and 44100/RefreshRate is not an integer.
	 */
	virtual void RunFrame(float *audioL, float *audioR, int numSamples) = 0;

	/*
	 * Reset(void):
	 *
	 * Resets the DSB. Must be called prior to RunFrame().
	 */
	virtual void Reset(void) = 0;

	/*
	 * SaveState(SaveState):
	 *
	 * Saves an image of the current device state.
	 *
	 * Parameters:
	 *		SaveState	Block file to save state information to.
	 */
	virtual void SaveState(CBlockFile *SaveState) = 0;

	/*
	 * LoadState(SaveState):
	 *
	 * Loads and a state image.
	 *
	 * Parameters:
	 *		SaveState	Block file to load state information from.
	 */
	virtual void LoadState(CBlockFile *SaveState) = 0;

	/*
	 * Init(progROMPtr, mpegROMPtr):
	 *
	 * Initializes the DSB board. This member must be called first.
	 *
	 * Parameters:
	 *		progROMPtr	Program (68K or Z80) ROM.
	 *		mpegROMPtr	MPEG data ROM.
	 *
	 * Returns:
	 *		OKAY if successful, otherwise FAIL.
	 */
	virtual Result Init(const UINT8 *progROMPtr, const UINT8 *mpegROMPtr) = 0;

	virtual ~CDSB()
	{
	}
};


/******************************************************************************
 DSB Classes

 DSB1 and DSB2 hardware. The base class, CDSB, should ideally be dynamically
 allocated using one of these. See CDSB for descriptions of member functions.
******************************************************************************/

/*
 * CDSB1:
 *
 * Sega Digital Sound Board Type 1: Z80 plus custom gate array for MPEG
 * decoding.
 */
class CDSB1: public CDSB
{
public:
	// Read and write handlers for the Z80 (required by CBus)
	UINT8 	IORead8(UINT32 addr);
	void 	IOWrite8(UINT32 addr, UINT8 data);
	UINT8	Read8(UINT32 addr);
	void 	Write8(UINT32 addr, UINT8 data);

	// DSB interface (see CDSB definition)
	void 	SendCommand(UINT8 data);
	void 	RunFrame(float *audioL, float *audioR, int numSamples);
	void 	Reset(void);
	void	SaveState(CBlockFile *StateFile);
	void	LoadState(CBlockFile *StateFile);
	Result 	Init(const UINT8 *progROMPtr, const UINT8 *mpegROMPtr);

	// Returns a reference to the Z80 CPU
	CZ80 *GetZ80(void);

	// Constructor and destructor
	CDSB1(const Util::Config::Node &config);
	~CDSB1(void);

private:
  const Util::Config::Node &m_config;

	// Resampler
	CDSBResampler	Resampler;
	int				retainedSamples;	// how many MPEG samples carried over from previous frame
	double			mpegFrameCarry;		// fractional 32 kHz sample carry (see MpegSamplesForFrame)

  // MPEG decode buffers (48KHz, 1/60th second + 2 extra padding samples)
	INT16	*mpegL, *mpegR;

	// DSB memory
	const UINT8	*progROM;		// Z80 program ROM (passed in from parent object)
	const UINT8	*mpegROM;		// MPEG music ROM
	UINT8		*memoryPool;	// all memory allocated here
	UINT8		*ram;			// Z80 RAM

	// Command FIFO
	UINT8	fifo[FIFO_STACK_SIZE];
	int		fifoIdxR;		// read position
	int		fifoIdxW;		// write position

	// MPEG playback variables
	int		mpegStart;
	int		mpegEnd;
	int		mpegState;
	int		loopStart;
	int		loopEnd;

	// Settings of currently playing stream (may not match the playback register variables above)
	UINT32	usingLoopStart;	// what was last set by MPEG_SetLoop() or MPEG_PlayMemory()
	UINT32	usingLoopEnd;
	UINT32	usingMPEGStart;	// what was last set by MPEG_PlayMemory()
	UINT32	usingMPEGEnd;

	// Registers
	UINT32	startLatch;	// MPEG start address latch
	UINT32	endLatch;	// MPEG end address latch
	UINT8	status;
	UINT8	cmdLatch;
	UINT8	volume;		// 0x00-0x7F
	UINT8	stereo;

	// Z80 CPU
	CZ80	Z80;
};

/*
 * CDSB2:
 *
 * Sega Digital Sound Board Type 2: 68K CPU.
 */
class CDSB2: public CDSB
{
public:
	// Read and write handlers for the 68K (required by CBus)
	UINT8	Read8(UINT32 addr);
	UINT16	Read16(UINT32 addr);
	UINT32	Read32(UINT32 addr);
	void	Write8(UINT32 addr, UINT8 data);
	void	Write16(UINT32 addr, UINT16 data);
	void	Write32(UINT32 addr, UINT32 data);

	// DSB interface (see definition of CDSB)
	void 	SendCommand(UINT8 data);
	void 	RunFrame(float *audioL, float *audioR, int numSamples);
	void 	Reset(void);
	void	SaveState(CBlockFile *StateFile);
	void	LoadState(CBlockFile *StateFile);
	Result 	Init(const UINT8 *progROMPtr, const UINT8 *mpegROMPtr);

	// Returns a reference to the 68K CPU context
	M68KCtx *GetM68K(void);

	// Constructor and destructor
	CDSB2(const Util::Config::Node &config);
	~CDSB2(void);

private:
	const Util::Config::Node &m_config;

	// Private helper functions
	void	WriteMPEGFIFO(UINT8 byte);

	// Resampler
	CDSBResampler	Resampler;
	int				retainedSamples;	// how many MPEG samples carried over from previous frame
	double			mpegFrameCarry;		// fractional 32 kHz sample carry (see MpegSamplesForFrame)

	// MPEG decode buffers (48KHz, 1/60th second + 2 extra padding samples)
	INT16	*mpegL, *mpegR;

	// Output routing for the two mono programmes packed into one stereo MP2 stream (Sega Rally 2).
	// Command 0xA0 selects Channel0, whose audio the decoder delivers in its SECOND buffer (mpegR);
	// command 0xB1 selects Channel1, delivered in the FIRST buffer (mpegL). The names follow the
	// board's channel numbering rather than the decoder's L/R order: this is the mapping the
	// original Sega Rally 2 fix established, and it was inverted by a later refactor.
	// Do not change the values because they are used in save states!
	enum class StereoMode: uint8_t
	{
		Stereo = 0,     // both channels
		Channel0 = 1,   // mono, source is the decoder's right buffer
		Channel1 = 2    // mono, source is the decoder's left buffer
	};

	// DSB memory
	const UINT8	*progROM;		// 68K program ROM (passed in from parent object)
	const UINT8	*mpegROM;		// MPEG music ROM
	UINT8		*memoryPool;	// all memory allocated here
	UINT8		*ram;			// 68K RAM

	// Command FIFO
	UINT8	fifo[FIFO_STACK_SIZE];
	int		fifoIdxR;		// read position
	int		fifoIdxW;		// write position

	// Registers
	int 	cmdLatch;
	int 	mpegState;
	int 	mpegStart, mpegEnd, playing;
	UINT8	volume[2];		// left, right volume (0x00-0xFF)
	StereoMode stereo;

	// Settings of currently playing stream (may not match the playback register variables above)
	UINT32	usingLoopStart;	// what was last set by MPEG_SetLoop() or MPEG_PlayMemory()
	UINT32	usingLoopEnd;
	UINT32	usingMPEGStart;	// what was last set by MPEG_PlayMemory()
	UINT32	usingMPEGEnd;

	// M68K CPU
	M68KCtx	M68K;
	// One frame of emulated time in 68K cycles is derived per call from the frame's 44.1 kHz sample
	// count, so that the DSB's MPEG timing follows RefreshRate instead of assuming 60 Hz.
	static constexpr int k_timerPeriod = 11000000/1000; // 1KHz timer
	int m_cyclesElapsedThisFrame;
	int m_nextTimerInterruptCycles;
};


#endif	// INCLUDED_DSB_H
