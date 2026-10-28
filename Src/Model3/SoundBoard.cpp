/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011 Bart Trzynadlowski, Nik Henson 
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
 * SoundBoard.cpp
 * 
 * Model 3 sound board. Implementation of the CSoundBoard class. This class can
 * only be instantiated once because it relies on global variables (the non-OOP
 * 68K core and an IRQ line).
 *
 * Bank Switching
 * --------------
 * Banking is not fully understood yet. It is presumed that the low 2MB of the 
 * sample ROMs are not banked (MAME), but this is not guaranteed. Below are 
 * examples of known situations where banking helps (sound names are as they
 * appear in the sound test menu).
 *
 *		sound name
 *			ROM Offsets -> Address
 * 			
 *		dayto2pe
 *		--------
 *		let's hope he does better
 *			A... -> A...	(400001=3E)
 *		doing good i'd say you have a ..
 *			E... -> E...	(400001=3D)
 *
 * From the above, it appears that when (400001)&10, then:
 *
 *		ROM A00000-DFFFFF -> A00000-DFFFFF
 *		ROM E00000-FFFFFF -> E00000-FFFFFF
 *
 * And when that bit is clear (just use default mapping, upper 6MB of ROM):
 *
 *		ROM 200000-5FFFFF -> A00000-DFFFFF
 *		ROM 600000-7FFFFF -> E00000-FFFFFF
 */

#include "SoundBoard.h"

#include <cstdlib>
#include <cmath>
#include <iomanip>
#include <sstream>
#include "Supermodel.h"
#include "OSD/Audio.h"
#include "Sound/SCSP.h"
#include "Sound/MPEG/MpegAudio.h"

// DEBUG
//#define SUPERMODEL_LOG_AUDIO	// define this to log all audio to sound.bin
#ifdef SUPERMODEL_LOG_AUDIO
static FILE		*soundFP;
#endif


// Offsets of memory regions within sound board's pool
#define OFFSET_RAM1	            0           // 1 MB SCSP1 RAM
#define OFFSET_RAM2             0x100000    // 1 MB SCSP2 RAM
// Per-frame audio buffers are sized for the worst case, not for the nominal count: at 57.524160 Hz a
// frame produces 766 or 767 samples depending on the accumulated remainder (see GetSamplesThisFrame).
#define LENGTH_CHANNEL_BUFFER   (sizeof(float)*MAX_SAMPLES_PER_FRAME)               // worst case: 32 bits x 44.1 KHz x 1/45th second
#define OFFSET_AUDIO_FRONTLEFT  0x200000                                            // 2940 bytes (32 bits, 44.1 KHz, 1/60th second) left audio channel
#define OFFSET_AUDIO_FRONTRIGHT (OFFSET_AUDIO_FRONTLEFT + LENGTH_CHANNEL_BUFFER)    // 2940 bytes right audio channel
#define OFFSET_AUDIO_REARLEFT   (OFFSET_AUDIO_FRONTRIGHT + LENGTH_CHANNEL_BUFFER)   // 2940 bytes (32 bits, 44.1 KHz, 1/60th second) left audio channel
#define OFFSET_AUDIO_REARRIGHT  (OFFSET_AUDIO_REARLEFT + LENGTH_CHANNEL_BUFFER)     // 2940 bytes right audio channel

#define MEMORY_POOL_SIZE        (0x100000 + 0x100000 + 4*LENGTH_CHANNEL_BUFFER)

// Stage markers for SM3D_LOAD_DEBUG. A hang here leaves the last marker in the log,
// which separates a stuck SCSP_Update() from a stuck DSB or audio output.
static bool SoundDebugEnabled(void)
{
	static const bool enabled = std::getenv("SM3D_LOAD_DEBUG") != nullptr;
	return enabled;
}


/******************************************************************************
 68K Address Space Handlers
******************************************************************************/

void CSoundBoard::UpdateROMBanks(void)
{
	if ((ctrlReg&0x10))
		sampleBank = &sampleROM[0x800000];
	else
		sampleBank = &sampleROM[0x000000];
}

UINT8 CSoundBoard::Read8(UINT32 a)
{
	switch ((a>>20)&0xF)
	{
	case 0x0:	// SCSP RAM 1 (master): 000000-0FFFFF
		return ram1[a^1];
		
	case 0x1:	// SCSP registers (master): 100000-10FFFF (unlike real hardware, we mirror up to 1FFFFF)
		return SCSP_Master_r8(a);
		
	case 0x2:	// SCSP RAM 2 (slave): 200000-2FFFFF
		return ram2[(a&0x0FFFFF)^1];
	
	case 0x3:	// SCSP registers (slave): 300000-30FFFF (unlike real hardware, we mirror up to 3FFFFF)
		return SCSP_Slave_r8(a);
		
	case 0x6:	// Program ROM: 600000-67FFFF (unlike real hardware, we mirror up to 6FFFFF here)	
		return soundROM[(a&0x07FFFF)^1];
	
	case 0x8:	// Sample ROM bank: 800000-FFFFFF
	case 0x9:	
	case 0xA:
	case 0xB:
	case 0xC:
	case 0xD:
	case 0xE:
	case 0xF:
		return sampleBank[(a&0x7FFFFF)^1];
		
	default:
		//printf("68K: Unknown read8 %06X\n", a);
		break;
	}
	
	return 0;
}

UINT16 CSoundBoard::Read16(UINT32 a)
{
	switch ((a>>20)&0xF)
	{
	case 0x0:	// SCSP RAM 1 (master): 000000-0FFFFF
		return *(UINT16 *) &ram1[a];
		
	case 0x1:	// SCSP registers (master): 100000-10FFFF
		return SCSP_Master_r16(a);
		
	case 0x2:	// SCSP RAM 2 (slave): 200000-2FFFFF
		return *(UINT16 *) &ram2[a&0x0FFFFF];
	
	case 0x3:	// SCSP registers (slave): 300000-30FFFF
		return SCSP_Slave_r16(a);
		
	case 0x6:	// Program ROM: 600000-67FFFF
		return *(UINT16 *) &soundROM[a&0x07FFFF];
	
	case 0x8:	// Sample ROM bank: 800000-FFFFFF
	case 0x9:	
	case 0xA:
	case 0xB:
	case 0xC:
	case 0xD:
	case 0xE:
	case 0xF:
		return *(UINT16 *) &sampleBank[a&0x7FFFFF];
		
	default:
		//printf("68K: Unknown read16 %06X\n", a);
		break;
	}
	
	return 0;
}

UINT32 CSoundBoard::Read32(UINT32 a)
{
	UINT32	hi, lo;

	switch ((a>>20)&0xF)
	{
	case 0x0:	// SCSP RAM 1 (master): 000000-0FFFFF
		hi = *(UINT16 *) &ram1[a];
		lo = *(UINT16 *) &ram1[a+2];	// TODO: clamp? Possible bounds hazard.
		return (hi<<16)|lo;

	case 0x1:	// SCSP registers (master): 100000-10FFFF
		return SCSP_Master_r32(a);

	case 0x2:	// SCSP RAM 2 (slave): 200000-2FFFFF
		hi = *(UINT16 *) &ram2[a&0x0FFFFF];
		lo = *(UINT16 *) &ram2[(a+2)&0x0FFFFF];
		return (hi<<16)|lo;

	case 0x3:	// SCSP registers (slave): 300000-30FFFF
		return SCSP_Slave_r32(a);

	case 0x6:	// Program ROM: 600000-67FFFF
		hi = *(UINT16 *) &soundROM[a&0x07FFFF];
		lo = *(UINT16 *) &soundROM[(a+2)&0x07FFFF];
		return (hi<<16)|lo;

	case 0x8:	// Sample ROM bank: 800000-FFFFFF
	case 0x9:	
	case 0xA:
	case 0xB:
	case 0xC:
	case 0xD:
	case 0xE:
	case 0xF:
		hi = *(UINT16 *) &sampleBank[a&0x7FFFFF];
		lo = *(UINT16 *) &sampleBank[(a+2)&0x7FFFFF];
		return (hi<<16)|lo;
		
	default:
		//printf("68K: Unknown read32 %06X\n", a);
		break;
	}
	
	return 0;
}

void CSoundBoard::Write8(unsigned int a,unsigned char d)  
{ 
	switch ((a>>20)&0xF)
	{
	case 0x0:	// SCSP RAM 1 (master): 000000-0FFFFF
		ram1[a^1] = d;
		break;
		
	case 0x1:	// SCSP registers (master): 100000-10FFFF
		SCSP_Master_w8(a,d);
		break;
		
	case 0x2:	// SCSP RAM 2 (slave): 200000-2FFFFF
		ram2[(a&0x0FFFFF)^1] = d;
		break;
	
	case 0x3:	// SCSP registers (slave): 300000-30FFFF
		SCSP_Slave_w8(a,d);
		break;
	
	default:
		if (a == 0x400001)
		{
			ctrlReg = d;
			UpdateROMBanks();
		}
		//else
		//	printf("68K: Unknown write8 %06X=%02X\n", a, d);
		break;
	}
}

void CSoundBoard::Write16(unsigned int a,unsigned short d) 
{ 
	switch ((a>>20)&0xF)
	{
	case 0x0:	// SCSP RAM 1 (master): 000000-0FFFFF
		*(UINT16 *) &ram1[a] = d;
		break;
		
	case 0x1:	// SCSP registers (master): 100000-10FFFF
		SCSP_Master_w16(a,d);
		break;
		
	case 0x2:	// SCSP RAM 2 (slave): 200000-2FFFFF
		*(UINT16 *) &ram2[a&0x0FFFFF] = d;
		break;
	
	case 0x3:	// SCSP registers (slave): 300000-30FFFF
		SCSP_Slave_w16(a,d);
		break;
	
	default:
		//printf("68K: Unknown write16 %06X=%04X\n", a, d);
		break;
	}
}

void CSoundBoard::Write32(unsigned int a,unsigned int d)
{
	switch ((a>>20)&0xF)
	{
	case 0x0:	// SCSP RAM 1 (master): 000000-0FFFFF
		*(UINT16 *) &ram1[a] = (d>>16);
		*(UINT16 *) &ram1[a+2] = (d&0xFFFF);
		break;
		
	case 0x1:	// SCSP registers (master): 100000-10FFFF
		SCSP_Master_w32(a,d);
		break;
		
	case 0x2:	// SCSP RAM 2 (slave): 200000-2FFFFF
		*(UINT16 *) &ram2[a&0x0FFFFF] = (d>>16);
		*(UINT16 *) &ram2[(a+2)&0x0FFFFF] = (d&0xFFFF);
		break;
	
	case 0x3:	// SCSP registers (slave): 300000-30FFFF
		SCSP_Slave_w32(a,d);
		break;
	
	default:
		//printf("68K: Unknown write32 %06X=%08X\n", a, d);
		break;
	}
}


/******************************************************************************
 SCSP 68K Callbacks
 
 The SCSP emulator drives the 68K via callbacks. These have to live outside of
 the CSoundBoard object for now, unfortunately.
******************************************************************************/

// Interrupt acknowledge callback; do not automatically clear interrupts!
int IRQAck(int irqLevel)
{
	return M68K_IRQ_AUTOVECTOR;
}

// SCSP callback for generating IRQs
void SCSP68KIRQCallback(int irqLevel)
{
	// SCSP is responsible for IRQ arbitration logic
	M68KSetIRQ(irqLevel);
}

// SCSP callback for running the 68K
int SCSP68KRunCallback(int numCycles)
{
	return M68KRun(numCycles) - numCycles;
}


/******************************************************************************
 Sound Board Interface
******************************************************************************/

void CSoundBoard::WriteMIDIPort(UINT8 data)
{
	SCSP_MidiIn(data);
	if (NULL != DSB)	// DSB receives all commands as well
		DSB->SendCommand(data);
}

#ifdef SUPERMODEL_LOG_AUDIO
static INT16 ClampINT16(float x)
{
    INT32 xi = (INT32)x;
    if (xi > INT16_MAX) {
        xi = INT16_MAX;
    }
    if (xi < INT16_MIN) {
        xi = INT16_MIN;
    }
    return (INT16)xi;
}
#endif

bool CSoundBoard::RunFrame(void)
{
	// How many samples this frame produces. Derived from RefreshRate with fractional carry-over so
	// the audio clock stays locked to the sound card at any emulated refresh rate; exactly 735 at
	// 60 Hz, and alternating 766/767 at the arcade rate. Must be fetched once per frame, in step
	// with OutputAudio() and SCSP_Update().
	const int numSamplesThisFrame = GetSamplesThisFrame();
	const bool soundDebug = SoundDebugEnabled();
	if (soundDebug)
		InfoLog("LoadDebug sound begin samples=%d", numSamplesThisFrame);
	SCSP_SetFrameLength(numSamplesThisFrame);

	// Run sound board first to generate SCSP audio
	if (m_config["EmulateSound"].ValueAs<bool>())
	{
		M68KSetContext(&M68K);
		SCSP_Update();
		M68KGetContext(&M68K);

		if (soundDebug)
		{
			float scspPeak = 0.0f;
			for (int i = 0; i < numSamplesThisFrame; ++i)
			{
				scspPeak = std::max(scspPeak, std::fabs(audioFL[i]));
				scspPeak = std::max(scspPeak, std::fabs(audioFR[i]));
				scspPeak = std::max(scspPeak, std::fabs(audioRL[i]));
				scspPeak = std::max(scspPeak, std::fabs(audioRR[i]));
			}
			InfoLog("LoadDebug sound scsp done peak=%.1f", scspPeak);
		}
	}
	else
	{
		/*
		 * Sound emulation is off: the mix is music only, so the SCSP side of the buffers must not
		 * carry whatever a previous frame or session left there.
		 *
		 * This clear used to sit in the `else` of the *debug log* above instead of the `else` of
		 * this block, which meant it ran on every ordinary session - right after SCSP_Update() had
		 * filled all four channels. The result was music with no sound effects at all, in every
		 * game, whether the session was a fresh boot or a loaded save. Only DSB is mixed in later,
		 * so only DSB survived. If the effects ever go silent again, check this ordering first.
		 */
		memset(audioFL, 0, LENGTH_CHANNEL_BUFFER);
		memset(audioFR, 0, LENGTH_CHANNEL_BUFFER);
		memset(audioRL, 0, LENGTH_CHANNEL_BUFFER);
		memset(audioRR, 0, LENGTH_CHANNEL_BUFFER);
	}

	// Compute sound volume as 
	float soundVol = (float)std::max(0,std::min(200,m_config["SoundVolume"].ValueAs<int>()));
	soundVol = soundVol * (float)(1.0 / 100.0);

	// Apply sound volume setting to SCSP channels only
	for (int i = 0; i < numSamplesThisFrame; i++) {
		audioFL[i] *= soundVol;
		audioFR[i] *= soundVol;
		audioRL[i] *= soundVol;
		audioRR[i] *= soundVol;
	}

	// Run DSB and mix with existing audio, apply music volume
	if (NULL != DSB) {
		if (soundDebug)
			InfoLog("LoadDebug sound dsb begin");
		// Will need to mix with proper front, rear channels or both (game specific)
		constexpr bool mixDSBWithFront = true; // Everything to front channels for now
		// Case "both" not handled for now
		if (mixDSBWithFront)
			DSB->RunFrame(audioFL, audioFR, numSamplesThisFrame);
		else
			DSB->RunFrame(audioRL, audioRR, numSamplesThisFrame);
		if (soundDebug)
			InfoLog("LoadDebug sound dsb done");
	}

	// Output the audio buffers
	if (soundDebug)
		InfoLog("LoadDebug sound output begin");
	float mixPeak = 0.0f;
	if (soundDebug)
	{
		for (int i = 0; i < numSamplesThisFrame; ++i)
		{
			mixPeak = std::max(mixPeak, std::fabs(audioFL[i]));
			mixPeak = std::max(mixPeak, std::fabs(audioFR[i]));
			mixPeak = std::max(mixPeak, std::fabs(audioRL[i]));
			mixPeak = std::max(mixPeak, std::fabs(audioRR[i]));
		}
	}
	bool bufferFull = OutputAudio(numSamplesThisFrame, audioFL, audioFR, audioRL, audioRR, m_config["FlipStereo"].ValueAs<bool>());
	if (soundDebug)
		InfoLog("LoadDebug sound output done full=%d peak=%.1f", bufferFull ? 1 : 0, mixPeak);

#ifdef SUPERMODEL_LOG_AUDIO
	// Output to binary file
	INT16	s;
	for (int i = 0; i < numSamplesThisFrame; i++)
	{	
		s = ClampINT16(audioFL[i]);
		fwrite(&s, sizeof(INT16), 1, soundFP);	// left channel
		s = ClampINT16(audioFR[i]);
		fwrite(&s, sizeof(INT16), 1, soundFP);	// right channel
		s = ClampINT16(audioRL[i]);
		fwrite(&s, sizeof(INT16), 1, soundFP);	// left channel
		s = ClampINT16(audioRR[i]);
		fwrite(&s, sizeof(INT16), 1, soundFP);	// right channel
	}
#endif // SUPERMODEL_LOG_AUDIO

	if (soundDebug)
		InfoLog("LoadDebug sound end");
	return bufferFull;
}

void CSoundBoard::Reset(void)
{
	// Even if SCSP emulation is disabled, we must reset to establish a valid 68K state
	memcpy(ram1, soundROM, 16);				// copy 68K vector table
	ctrlReg = 0;							// set default banks
	UpdateROMBanks();
	M68KSetContext(&M68K);
	M68KReset();
	//printf("SBrd PC=%06X\n", M68KGetPC());
	M68KGetContext(&M68K);
	if (NULL != DSB)
		DSB->Reset();
	DebugLog("Sound Board Reset\n");
	//printf("PC=%06X\n", M68KGetPC());
	//M68KSetContext(&M68K);
	//M68KGetContext(&M68K);
	//printf("PC=%06X\n", M68KGetPC());
}

void CSoundBoard::SaveState(CBlockFile *SaveState)
{
	SaveState->NewBlock("Sound Board", __FILE__);
	SaveState->Write(ram1, 0x100000);
	SaveState->Write(ram2, 0x100000);
	SaveState->Write(&ctrlReg, sizeof(ctrlReg));
	
	// All other devices...
	M68KSetContext(&M68K);
	M68KSaveState(SaveState, "Sound Board 68K");
	SCSP_SaveState(SaveState);
	if (NULL != DSB)
		DSB->SaveState(SaveState);
}

void CSoundBoard::LoadState(CBlockFile *SaveState)
{
	if (Result::OKAY != SaveState->FindBlock("Sound Board"))
	{
		ErrorLog("Unable to load sound board state. Save state file is corrupt.");
		return;
	}
	
	SaveState->Read(ram1, 0x100000);
	SaveState->Read(ram2, 0x100000);
	SaveState->Read(&ctrlReg, sizeof(ctrlReg));
	UpdateROMBanks();
	
	// All other devices
	M68KSetContext(&M68K);	// so we don't lose callback pointers when copying context back
	M68KLoadState(SaveState, "Sound Board 68K");
	M68KGetContext(&M68K);
	SCSP_LoadState(SaveState);

	// The 68K IRQ line is derived from the SCSP pending/enable registers and is not part of
	// either save block. Without this, a state saved with an interrupt pending restores the
	// sound board with the line low and the 68K never services the effect commands again.
	SCSP_RefreshIRQ();

	if (std::getenv("SM3D_LOAD_DEBUG") != nullptr)
	{
		M68KGetContext(&M68K);
		InfoLog("LoadDebug sound68k pc=%06X stopped=%u int_level=%u",
			(unsigned)M68KGetPC(), (unsigned)M68K.musashiCtx.stopped,
			(unsigned)M68K.musashiCtx.int_level);
	}

	if (NULL != DSB)
		DSB->LoadState(SaveState);
}

std::string CSoundBoard::DebugState(void)
{
	std::ostringstream s;
	M68KGetContext(&M68K);
	s << "sb68k pc=" << std::hex << std::setw(6) << std::setfill('0') << M68KGetPC()
		<< " stopped=" << std::dec << (unsigned)M68K.musashiCtx.stopped
		<< " irq=" << (unsigned)M68K.musashiCtx.int_level
		<< " mpeg=" << (MpegDec::IsLoaded() ? 1 : 0)
		<< " mpegpos=" << MpegDec::GetPosition()
		<< SCSP_DebugState();
	return s.str();
}


/******************************************************************************
 Configuration, Initialization, and Shutdown
******************************************************************************/

void CSoundBoard::AttachDSB(CDSB *DSBPtr)
{
	DSB = DSBPtr;
	DebugLog("Sound Board connected to DSB\n");
}


Result CSoundBoard::Init(const UINT8 *soundROMPtr, const UINT8 *sampleROMPtr)
{
	float	memSizeMB = (float)MEMORY_POOL_SIZE/(float)0x100000;
	
	// Receive sound ROMs
	soundROM = soundROMPtr;
	sampleROM = sampleROMPtr;
	ctrlReg = 0;
	UpdateROMBanks();

	// Allocate all memory for RAM
	memoryPool = new(std::nothrow) UINT8[MEMORY_POOL_SIZE];
	if (NULL == memoryPool)
		return ErrorLog("Insufficient memory for sound board (needs %1.1f MB).", memSizeMB);
	memset(memoryPool, 0, MEMORY_POOL_SIZE);
	
	// Set up memory pointers
	ram1 = &memoryPool[OFFSET_RAM1];
	ram2 = &memoryPool[OFFSET_RAM2];
	audioFL = (float*)&memoryPool[OFFSET_AUDIO_FRONTLEFT];
	audioFR = (float*)&memoryPool[OFFSET_AUDIO_FRONTRIGHT];
	audioRL = (float*)&memoryPool[OFFSET_AUDIO_REARLEFT];
	audioRR = (float*)&memoryPool[OFFSET_AUDIO_REARRIGHT];

	// Initialize 68K core
	M68KSetContext(&M68K);
	M68KInit();
	M68KAttachBus(this);
	M68KSetIRQCallback(IRQAck);
	M68KGetContext(&M68K);
		
	// Initialize SCSPs
	SCSP_SetBuffers(audioFL, audioFR, audioRL, audioRR, MAX_SAMPLES_PER_FRAME);
	SCSP_SetCB(SCSP68KRunCallback, SCSP68KIRQCallback);
	if (Result::OKAY != SCSP_Init(m_config, 2))
		return Result::FAIL;
	SCSP_SetRAM(0, ram1);
	SCSP_SetRAM(1, ram2);
	
	// Binary logging
#ifdef SUPERMODEL_LOG_AUDIO
	soundFP = fopen("sound.bin","wb");	// delete existing file
	fclose(soundFP);
	soundFP = fopen("sound.bin","ab");	// append mode
#endif

	return Result::OKAY;
}

M68KCtx *CSoundBoard::GetM68K(void)
{
	return &M68K;
}

CDSB *CSoundBoard::GetDSB(void)
{
	return DSB;
}

CSoundBoard::CSoundBoard(const Util::Config::Node &config)
  : m_config(config)
{
	DSB = NULL;
	memoryPool = NULL;
	ram1 = NULL;
	ram2 = NULL;
	audioFL = NULL;
	audioFR = NULL;
	audioRL = NULL;
	audioRR = NULL;
	soundROM = NULL;
	sampleROM = NULL;

	sampleBank = nullptr;
	ctrlReg = 0;

	DebugLog("Built Sound Board\n");
}

CSoundBoard::~CSoundBoard(void)
{	
#ifdef SUPERMODEL_LOG_AUDIO
	// close binary log file
	fclose(soundFP);
#endif

	SCSP_Deinit();

	DSB = NULL;

	if (memoryPool != NULL)
	{
		delete [] memoryPool;
		memoryPool = NULL;
	}
	ram1 = NULL;
	ram2 = NULL;
	audioFL = NULL;
	audioFR = NULL;
	audioRL = NULL;
	audioRR = NULL;
	soundROM = NULL;
	sampleROM = NULL;

	DebugLog("Destroyed Sound Board\n");
}
