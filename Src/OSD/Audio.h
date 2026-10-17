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
 * Audio.h
 * 
 * Header file for OS-dependent audio playback interface.
 */

#ifndef INCLUDED_AUDIO_H
#define INCLUDED_AUDIO_H

#include "Types.h"
#include "Util/NewConfig.h"
#include "Game.h"

typedef void (*AudioCallbackFPtr)(void *data);

extern void SetAudioCallback(AudioCallbackFPtr callback, void *data);

extern void SetAudioEnabled(bool enabled);
extern void SetAudioType(Game::AudioTypes type);

/*
 * OpenAudio()
 *
 * Initializes the audio system.
 */
extern Result OpenAudio(const Util::Config::Node& config);

/*
 * OutputAudio(unsigned numSamples, *INT16 leftBuffer, *INT16 rightBuffer)
 *
 * Sends a chunk of two-channel audio with the given number of samples to the audio system.
 */
extern bool OutputAudio(unsigned numSamples, const float* leftFrontBuffer, const float* rightFrontBuffer, const float* leftRearBuffer, const float* rightRearBuffer, bool flipStereo);/*
 * IsAudioBufferFull()
 *
 * Whether the output buffer has as much queued as it will accept. Uses the same condition that
 * makes OutputAudio() report the buffer as full, so a producer can ask before generating a frame
 * instead of generating one and having it discarded as an over-run.
 *
 * Added for the asynchronous sound board thread: it is woken by the audio callback, and letting it
 * check this first makes a wake-up that arrives when nothing is needed harmless.
 */
extern bool IsAudioBufferFull(void);

/*
 * GetSamplesThisFrame()
 *
 * Returns how many 44.1 KHz samples the frame about to be emulated should produce. Model 3 audio is
 * generated in emulated time and one frame advances 1/RefreshRate seconds, so the exact count is
 * 44100/RefreshRate - rarely an integer (766.63 at the arcade rate of 57.524160 Hz). Generating a
 * fixed count would make the audio clock drift against the sound card and eventually under-run, so
 * the remainder is carried to the next frame and the count alternates (766, 767, 766, ...) to keep
 * the long-run average exact. At 60 Hz this always returns 735, leaving behaviour unchanged.
 *
 * Call once per emulated frame; the accumulator advances on each call.
 */
extern int GetSamplesThisFrame(void);

/*
 * GetMaxSamplesPerFrame()
 *
 * Upper bound on GetSamplesThisFrame() for the permitted refresh rate range. Use this, not the
 * per-frame count, to size any per-frame audio buffer.
 */
extern unsigned GetMaxSamplesPerFrame(void);

/*
 * CloseAudio()
 *
 * Shuts down the audio system.
 */
extern void CloseAudio();

/*
 * GetAudioBufferStats()
 *
 * Returns the running count of audio buffer under-runs and over-runs since the
 * system was opened. A healthy configuration keeps both at zero; a steadily
 * climbing under-run count means the emulator produces slightly less audio than
 * the device consumes (the emulated frame rate is running slow relative to the
 * audio clock).
 */
extern void GetAudioBufferStats(unsigned *underruns, unsigned *overruns);

#endif	// INCLUDED_AUDIO_H
