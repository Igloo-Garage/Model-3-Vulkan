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
 * FlushAudioBuffer()
 *
 * Drops everything queued for playback and restarts the ring buffer from the beginning. Called
 * when the game has been stopped without producing audio - the window move/size loop, where Windows
 * blocks the frame loop - so that the buffer's worth of audio from before the stall is not replayed
 * when the game resumes.
 */
extern void FlushAudioBuffer(void);

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

/*
 * GetAudioStatus()
 *
 * A snapshot of the output for the statistics readout: how much audio is queued ahead of the play
 * position, how much the buffer holds in total, the host format, and whether output is muted.
 *
 * bufferMs is the figure worth watching. It falls towards zero when the emulator produces audio more
 * slowly than the device consumes it - a frame loop missing its deadline, a stalled producer - and
 * sits pinned at bufferSizeMs when the reverse happens. Both ends are already counted as under- and
 * over-runs; the fill level says how much margin is left before the next one.
 *
 * Milliseconds rather than bytes or samples because that is the unit the problem is felt in, and
 * because the byte count depends on the channel count, which is a setting.
 *
 * peakLeft/peakRight are measured before the mute and the volume/balance stages, and they are a
 * window rather than a running maximum: GetAudioStatus() consumes them, so each call describes the
 * audio produced since the previous one. That is deliberate - a peak read after the mute would be
 * zero for both "the game is silent" and "the output is muted", which are the two cases this pair
 * exists to tell apart.
 */
struct AudioStatus
{
    bool opened = false;        // a device is open and the ring buffer exists
    bool muted = false;         // playback is intentionally silenced (Alt+P, the panel's pause)
    unsigned bufferMs = 0;      // audio queued ahead of the play position
    unsigned bufferSizeMs = 0;  // capacity of the ring buffer
    unsigned channels = 0;      // host channels in use (1, 2 or 4)
    unsigned underRuns = 0;     // cumulative since OpenAudio()
    unsigned overRuns = 0;
    float peakLeft = 0.0f;      // peak since the previous call, 1.0 = full scale (may exceed it)
    float peakRight = 0.0f;
};

extern void GetAudioStatus(AudioStatus& status);

#endif	// INCLUDED_AUDIO_H
