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
  * Audio.cpp
  *
  * SDL audio playback. Implements the OSD audio interface.
  *
  * Buffer sizes and read/write positions must be sample-aligned. A sample is
  * defined to encompass both channels so for, e.g., 16-bit audio as used here,
  * a sample is 4 bytes. Static assertions are employed to ensure that the
  * initial set up of the buffer is correct.
  *
  * Model 3 Audio is always 4 channels. SCSP1 is usually for each front
  * channels (on CN8 connector) and SCSP2 for rear channels (on CN7).
  * The downmix to 2 channels will be performed here in case supermodel audio
  * subsystem does not allow such playback.
  * The default DSB board is supposed to be plug and mixed with the front output
  * channel. The rear channel is usually plugged to the gullbow speakers that
  * are present in most model 3 racing cabinets, while front speakers are only
  * present in Daytona2, Scud Race, Sega Rally 2.
  * As each cabinet as its own wiring, with different mixing, the games.xml
  * database will provide which can of mixing should be applied for each game.
  *
  * From twin uk cabinet diagrams, at least:
  * - lemans24: only stereo on rear speakers (gullbox) on front channels SCSP1/CN8.
  * - scud race: DSB mixed with SCSP2/CN7 for rear (gullbox) speakers, front
  *   connected to SCSP1/CN8.
  * - daytona2: front on SCSP1/CN8, rear on SCSP2/CN7
  * - srally2: SCSP2/CN7 gives left/right, and SCSP1/CN8 is split in 2 channels:
  *   mono front, mono rear. These two channels are them mixed with L/R to give
  *   the quad output.
  * - oceanhun: SCSP1/CN8 and SCSP2/CN7 are mixed together for stereo output.
  * Others are unknown so far, but it is expected that most Step 2.x games should
  * have quad output.
  */

#include "Audio.h"

#include "Supermodel.h"
#include "SDLIncludes.h"

#include <cmath>
#include <algorithm>
#include <string>

  // Model3 audio output is 44.1KHz 4-channel sound and frame rate is 60fps
#define SAMPLE_RATE_M3     (44100)
#define SUPERMODEL_FPS     (60.0f)
#define MODEL3_FPS         (57.53f)

#define MAX_SND_FREQ       (75)
#define MIN_SND_FREQ       (45)
#define MAX_LATENCY        (100)

#define NUM_CHANNELS_M3 (4)

Game::AudioTypes AudioType;
int nbHostAudioChannels = NUM_CHANNELS_M3;   // Number of channels on host

#define SAMPLES_PER_FRAME_M3  (INT32)(SAMPLE_RATE_M3 / MODEL3_FPS)

#define BYTES_PER_SAMPLE_M3   (NUM_CHANNELS_M3 * sizeof(INT16))
#define BYTES_PER_FRAME_M3   (SAMPLES_PER_FRAME_M3 * BYTES_PER_SAMPLE_M3)


static int samples_per_frame_host = SAMPLES_PER_FRAME_M3;
static int bytes_per_sample_host = BYTES_PER_SAMPLE_M3;
static int bytes_per_frame_host = BYTES_PER_FRAME_M3;

// Balance percents for mixer
float BalanceLeftRight = 0; // 0 mid balance, 100: left only,  -100:right only 
float BalanceFrontRear = 0; // 0 mid balance, 100: front only, -100:right only 
// Mixer factor (depends on values above)
float balanceFactorFrontLeft  = 1.0f;
float balanceFactorFrontRight = 1.0f;
float balanceFactorRearLeft   = 1.0f;
float balanceFactorRearRight  = 1.0f;

static bool enabled = true;         // True if sound output is enabled
// Default size of the audio buffer, as a percentage of the maximum. The buffer has to ride out any
// pause in audio production, and production happens once per frame from the main loop - so anything
// that blocks that loop for longer than the buffer's worth of audio drains it and the sound
// stutters. Blocking is easy to provoke: presenting can wait on the GPU under vsync, and competing
// with a capture application for the GPU makes that wait much longer. 20% (200 ms) was not enough to
// survive that; 40% (400 ms) is, at the cost of a little extra output latency, which for a
// frame-paced arcade emulator is a good trade. Configurable via AudioLatency for anyone who wants to
// trade responsiveness the other way.
static constexpr unsigned DEFAULT_LATENCY_PERCENT = 40;
static constexpr unsigned latency = DEFAULT_LATENCY_PERCENT;   // kept for the buffer-size arithmetic below
static constexpr bool underRunLoop = true;    // True if should loop back to beginning of buffer on under-run, otherwise sound is just skipped

static constexpr unsigned playSamples = 512;  // Size (in samples) of callback play buffer

static UINT32 audioBufferSize = 0;  // Size (in bytes) of audio buffer
static INT8* audioBuffer = NULL;    // Audio buffer

static UINT32 writePos = 0;         // Current position at which writing into buffer
static UINT32 playPos = 0;          // Current position at which playing data in buffer via callback

static bool writeWrapped = false;   // True if write position has wrapped around at end of buffer but play position has not done so yet

static unsigned underRuns = 0;      // Number of buffer under-runs that have occured
static unsigned overRuns = 0;       // Number of buffer over-runs that have occured

static AudioCallbackFPtr callback = NULL; // Pointer to audio callback that is called when audio buffer is less than half empty
static void* callbackData = NULL;         // Pointer to data to be passed to audio callback when it is called

static const Util::Config::Node* s_config = 0;

// Fractional per-frame sample generation (see GetSamplesThisFrame() in Audio.h). s_samplesPerFrameExact
// is 44100/RefreshRate; the accumulator carries the fractional remainder between calls.
static double s_samplesPerFrameExact = (double)SAMPLE_RATE_M3 / 60.0;
static double s_sampleAccumulator = 0.0;

int GetSamplesThisFrame(void)
{
    s_sampleAccumulator += s_samplesPerFrameExact;
    const int samples = (int)s_sampleAccumulator;
    s_sampleAccumulator -= (double)samples;
    return samples;
}

unsigned GetMaxSamplesPerFrame(void)
{
    return SAMPLE_RATE_M3 / MIN_SND_FREQ;
}

// Non-zero when a specific output device was opened with SDL_OpenAudioDevice(). Zero means
// the legacy device opened by SDL_OpenAudio() is in use, which needs the global lock/pause
// calls instead of the per-device ones.
static SDL_AudioDeviceID s_audioDevice = 0;

/*
 * True between a successful open and CloseAudio().
 *
 * Needed because CloseAudio() has to be callable from the error path as well as the normal one, and
 * the two are reached from places that do not know whether audio ever started. Calling it
 * unconditionally would report buffer statistics for a session that never played a sample and, on the
 * SDL_OpenAudio() route where s_audioDevice stays 0, hand SDL a close for a device it was never asked
 * to open.
 *
 * The failure this guards is not hypothetical. Without it, a session that opened audio and then failed
 * later - a renderer that could not initialise, for instance - left the device open, and every
 * subsequent session in the same process died with "Audio device is already opened". One bad
 * initialisation turned into a program that could not start any game at all, in any renderer, until it
 * was restarted.
 */
static bool s_audioOpened = false;

static void LockAudioBuffer(void)
{
    if (s_audioDevice != 0)
        SDL_LockAudioDevice(s_audioDevice);
    else
        SDL_LockAudio();
}

static void UnlockAudioBuffer(void)
{
    if (s_audioDevice != 0)
        SDL_UnlockAudioDevice(s_audioDevice);
    else
        SDL_UnlockAudio();
}


void SetAudioCallback(AudioCallbackFPtr newCallback, void* newData)
{
    // Lock audio whilst changing callback pointers
    LockAudioBuffer();

    callback = newCallback;
    callbackData = newData;

    UnlockAudioBuffer();
}

void SetAudioEnabled(bool newEnabled)
{
    enabled = newEnabled;
}

/// <summary>
/// Set game audio mixing type
/// </summary>
/// <param name="type"></param>
void SetAudioType(Game::AudioTypes type)
{
    AudioType = type;
}

static INT16 MixINT16(float x, float y)
{
    INT32 sum = (INT32)((x + y)*0.5f); //!! dither
    if (sum > INT16_MAX) {
        sum = INT16_MAX;
    }
    if (sum < INT16_MIN) {
        sum = INT16_MIN;
    }
    return (INT16)sum;
}

static float MixFloat(float x, float y)
{
    return (x + y)*0.5f;
}

static INT16 ClampINT16(float x)
{
    INT32 xi = (INT32)x; //!! dither
    if (xi > INT16_MAX) {
        xi = INT16_MAX;
    }
    if (xi < INT16_MIN) {
        xi = INT16_MIN;
    }
    return (INT16)xi;
}

static void PlayCallback(void* data, Uint8* stream, int len)
{
    //printf("PlayCallback(%d) [writePos = %u, writeWrapped = %s, playPos = %u, audioBufferSize = %u]\n",
    //	len, writePos, (writeWrapped ? "true" : "false"), playPos, audioBufferSize);

    // Get current write position and adjust it if write has wrapped but play position has not
    UINT32 adjWritePos = writePos;
    if (writeWrapped)
        adjWritePos += audioBufferSize;

    // Check if play position overlaps write position (ie buffer under-run)
    if (playPos + len > adjWritePos)
    {
        underRuns++;

        //printf("Audio buffer under-run #%u in PlayCallback(%d) [writePos = %u, writeWrapped = %s, playPos = %u, audioBufferSize = %u]\n",
        //	underRuns, len, writePos, (writeWrapped ? "true" : "false"), playPos, audioBufferSize);

        // See what action to take on under-run
        if (underRunLoop)
        {
            // If loop, then move play position back to beginning of data in buffer
            playPos = adjWritePos + bytes_per_frame_host;

            // Check if play position has moved past end of buffer
            if (playPos >= audioBufferSize)
                // If so, wrap it around to beginning again (but keep write wrapped flag as before)
                playPos -= audioBufferSize;
            else
                // Otherwise, set write wrapped flag as will now appear as if write has wrapped but play position has not
                writeWrapped = true;
        }
        else
        {
            // Otherwise, just copy silence to audio output stream and exit
            memset(stream, 0, len);
            return;
        }
    }

    INT8* src1;
    INT8* src2;
    UINT32 len1;
    UINT32 len2;

    // Check if play region extends past end of buffer
    if (playPos + len > audioBufferSize)
    {
        // If so, split play region into two
        src1 = audioBuffer + playPos;
        src2 = audioBuffer;
        len1 = audioBufferSize - playPos;
        len2 = len - len1;
    }
    else
    {
        // Otherwise, just copy whole region
        src1 = audioBuffer + playPos;
        src2 = nullptr;
        len1 = len;
        len2 = 0;
    }

    // Check if audio is enabled
    if (enabled)
    {
        // If so, copy play region into audio output stream
        memcpy(stream, src1, len1);

        // Also, if not looping on under-runs then blank region out
        if (!underRunLoop)
            memset(src1, 0, len1);

        if (len2)
        {
            // If region was split into two, copy second half into audio output stream as well
            memcpy(stream + len1, src2, len2);

            // Also, if not looping on under-runs then blank region out
            if (!underRunLoop)
                memset(src2, 0, len2);
        }
    }
    else
        // Otherwise, just copy silence to audio output stream
        memset(stream, 0, len);

    // Move play position forward for next time
    playPos += len;

    bool bufferFull = adjWritePos + 2 * bytes_per_frame_host > playPos + audioBufferSize;

    // Check if play position has moved past end of buffer
    if (playPos >= audioBufferSize)
    {
        // If so, wrap it around to beginning again and reset write wrapped flag
        playPos -= audioBufferSize;
        writeWrapped = false;
    }

    // If buffer is not full then call audio callback
    if (callback && !bufferFull)
        callback(callbackData);
}

static void MixChannels(unsigned numSamples, const float* leftFrontBuffer, const float* rightFrontBuffer, const float* leftRearBuffer, const float* rightRearBuffer, void* dest, bool flipStereo)
{
    INT16* p = (INT16*)dest;

    if (nbHostAudioChannels == 1) {
        for (unsigned i = 0; i < numSamples; i++) {
            INT16 monovalue = MixINT16(
                MixFloat(leftFrontBuffer[i] * balanceFactorFrontLeft,rightFrontBuffer[i] * balanceFactorFrontRight),
                MixFloat(leftRearBuffer[i]  * balanceFactorRearLeft, rightRearBuffer[i]  * balanceFactorRearRight));
            *p++ = monovalue;
        }
    } else {
        // Flip again left/right if configured in audio
        switch (AudioType) {
        case Game::STEREO_RL:
        case Game::QUAD_1_FRL_2_RRL:
        case Game::QUAD_1_RRL_2_FRL:
            flipStereo = !flipStereo;
            break;
        default: // all other types are left-before-right ordered and need no flip
            break;
        }

        // Now order channels according to audio type
        if (nbHostAudioChannels == 2) {
            for (unsigned i = 0; i < numSamples; i++) {
                INT16 leftvalue = MixINT16(leftFrontBuffer[i] * balanceFactorFrontLeft, leftRearBuffer[i] * balanceFactorRearLeft);
                INT16 rightvalue = MixINT16(rightFrontBuffer[i]*balanceFactorFrontRight, rightRearBuffer[i]*balanceFactorRearRight);
                if (flipStereo) // swap left and right channels
                {
                    *p++ = rightvalue;
                    *p++ = leftvalue;
                } else {
                    *p++ = leftvalue;
                    *p++ = rightvalue;
                }
            }
        } else if (nbHostAudioChannels == 4) {
            for (unsigned i = 0; i < numSamples; i++) {
                float frontLeftValue = leftFrontBuffer[i]*balanceFactorFrontLeft;
                float frontRightValue = rightFrontBuffer[i]*balanceFactorFrontRight;
                float rearLeftValue = leftRearBuffer[i]*balanceFactorRearLeft;
                float rearRightValue = rightRearBuffer[i]*balanceFactorRearRight;

                // Check game audio type
                switch (AudioType) {
                case Game::MONO: {
                    INT16 monovalue = MixINT16(MixFloat(frontLeftValue, frontRightValue), MixFloat(rearLeftValue, rearRightValue));
                    *p++ = monovalue;
                    *p++ = monovalue;
                    *p++ = monovalue;
                    *p++ = monovalue;
                } break;

                case Game::STEREO_LR:
                case Game::STEREO_RL: {
                    INT16 leftvalue =  MixINT16(frontLeftValue, frontRightValue);
                    INT16 rightvalue = MixINT16(rearLeftValue,  rearRightValue);
                    if (flipStereo) // swap left and right channels
                    {
                        *p++ = rightvalue;
                        *p++ = leftvalue;
                        *p++ = rightvalue;
                        *p++ = leftvalue;
                    } else {
                        *p++ = leftvalue;
                        *p++ = rightvalue;
                        *p++ = leftvalue;
                        *p++ = rightvalue;
                    }
                } break;

                case Game::QUAD_1_FLR_2_RLR:
                case Game::QUAD_1_FRL_2_RRL: {
                    // Normal channels Front Left/Right then Rear Left/Right
                    if (flipStereo) // swap left and right channels
                    {
                        *p++ = ClampINT16(frontRightValue);
                        *p++ = ClampINT16(frontLeftValue);
                        *p++ = ClampINT16(rearRightValue);
                        *p++ = ClampINT16(rearLeftValue);
                    } else {
                        *p++ = ClampINT16(frontLeftValue);
                        *p++ = ClampINT16(frontRightValue);
                        *p++ = ClampINT16(rearLeftValue);
                        *p++ = ClampINT16(rearRightValue);
                    }
                } break;

                case Game::QUAD_1_RLR_2_FLR:
                case Game::QUAD_1_RRL_2_FRL:
                    // Reversed channels Front/Rear Left then Front/Rear Right
                    if (flipStereo) // swap left and right channels
                    {
                        *p++ = ClampINT16(rearRightValue);
                        *p++ = ClampINT16(rearLeftValue);
                        *p++ = ClampINT16(frontRightValue);
                        *p++ = ClampINT16(frontLeftValue);
                    } else {
                        *p++ = ClampINT16(rearLeftValue);
                        *p++ = ClampINT16(rearRightValue);
                        *p++ = ClampINT16(frontLeftValue);
                        *p++ = ClampINT16(frontRightValue);
                    }
                    break;

                case Game::QUAD_1_LR_2_FR_MIX:
                    // Split mix: one goes to left/right, other front/rear (mono)
                    // =>Remix all!
                    INT16 newfrontLeftValue = MixINT16(frontLeftValue, rearLeftValue);
                    INT16 newfrontRightValue = MixINT16(frontLeftValue, rearRightValue);
                    INT16 newrearLeftValue = MixINT16(frontRightValue, rearLeftValue);
                    INT16 newrearRightValue = MixINT16(frontRightValue, rearRightValue);

                    if (flipStereo) // swap left and right channels
                    {
                        *p++ = newfrontRightValue;
                        *p++ = newfrontLeftValue;
                        *p++ = newrearRightValue;
                        *p++ = newrearLeftValue;
                    } else {
                        *p++ = newfrontLeftValue;
                        *p++ = newfrontRightValue;
                        *p++ = newrearLeftValue;
                        *p++ = newrearRightValue;
                    }
                    break;
                }
            }
        }
    }
}

/*
static void LogAudioInfo(SDL_AudioSpec *fmt)
{
    InfoLog("Audio device information:");
    InfoLog("    Frequency: %d", fmt->freq);
    InfoLog("     Channels: %d", fmt->channels);
    InfoLog("Sample Format: %d", fmt->format);
    InfoLog("");
}
*/

/// <summary>
/// Prepare audio subsystem on host.
/// The requested channels is deduced, and SDL will make sure it is compatible with this.
/// </summary>
/// <param name="config"></param>
/// <returns></returns>
Result OpenAudio(const Util::Config::Node& config)
{
    s_config = &config;
    // Initialize SDL audio sub-system
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return ErrorLog("Unable to initialize SDL audio sub-system: %s\n", SDL_GetError());

    // Number of channels requested in config (default is 4)
    nbHostAudioChannels = (int)s_config->Get("NbSoundChannels").ValueAs<int>();

    // If game is only stereo or mono, enforce host to reduce number of channels
    switch (AudioType) {
    case Game::MONO:
        nbHostAudioChannels = std::min(nbHostAudioChannels, 1);
        break;
    case Game::STEREO_LR:
    case Game::STEREO_RL:
        nbHostAudioChannels = std::min(nbHostAudioChannels, 2);
        break;
    default: // quad types need up to 4 channels; config value already defaults to 4
        break;
    }
    // Mixer Balance
    float balancelr = std::max(-100.f, std::min(100.f, s_config->Get("BalanceLeftRight").ValueAs<float>()));
    balancelr *= 0.01f;
    BalanceLeftRight = balancelr;

    float balancefr = std::max(-100.f, std::min(100.f, s_config->Get("BalanceFrontRear").ValueAs<float>()));
    balancefr *= 0.01f;
    BalanceFrontRear = balancefr;

    balanceFactorFrontLeft  = (BalanceLeftRight < 0.f ? 1.f + BalanceLeftRight : 1.f) * (BalanceFrontRear < 0 ? 1.f + BalanceFrontRear : 1.f);
    balanceFactorFrontRight = (BalanceLeftRight > 0.f ? 1.f - BalanceLeftRight : 1.f) * (BalanceFrontRear < 0 ? 1.f + BalanceFrontRear : 1.f);
    balanceFactorRearLeft   = (BalanceLeftRight < 0.f ? 1.f + BalanceLeftRight : 1.f) * (BalanceFrontRear > 0 ? 1.f - BalanceFrontRear : 1.f);
    balanceFactorRearRight  = (BalanceLeftRight > 0.f ? 1.f - BalanceLeftRight : 1.f) * (BalanceFrontRear > 0 ? 1.f - BalanceFrontRear : 1.f);

    // Set up audio specification
    SDL_AudioSpec desired{};
    desired.freq = SAMPLE_RATE_M3;
    // Number of host channels to use (choice limited to 1,2,4)
    desired.channels = nbHostAudioChannels;
    desired.format = AUDIO_S16SYS;
    desired.samples = playSamples;
    desired.callback = PlayCallback;

    // Now force SDL to use the format we requested (nullptr); it will convert if necessary.
    // If an output device was chosen, open it. Note that if the requested device fails or is
    // left empty, SDL_OpenAudio() opens the system's current default playback device.
    const std::string requestedDevice = s_config->Get("AudioDevice").ValueAsDefault<std::string>("");
    s_audioDevice = 0;
    if (!requestedDevice.empty())
    {
        // Try opening with 0 allowed changes first (exact match). If that fails, allow format changes.
        s_audioDevice = SDL_OpenAudioDevice(requestedDevice.c_str(), 0, &desired, nullptr, 0);
        if (s_audioDevice == 0)
        {
            SDL_AudioSpec obtained{};
            s_audioDevice = SDL_OpenAudioDevice(requestedDevice.c_str(), 0, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
        }

        if (s_audioDevice == 0)
            ErrorLog("Unable to open audio device '%s' (%s). Falling back to system default.",
                requestedDevice.c_str(), SDL_GetError());
        else
            InfoLog("Audio: using output device '%s'.", requestedDevice.c_str());
    }
    if (s_audioDevice == 0 && SDL_OpenAudio(&desired, nullptr) < 0) {
        if (desired.channels==2) {
            return ErrorLog("Unable to open 44.1KHz 2-channel audio with SDL: %s\n", SDL_GetError());
        } else if (desired.channels==4) {
            return ErrorLog("Unable to open 44.1KHz 4-channel audio with SDL: %s\n", SDL_GetError());
        } else {
            return ErrorLog("Unable to open 44.1KHz channel audio with SDL: %s\n", SDL_GetError());
        }
    }

    // From here on the device is open, so every exit - including the error paths in the caller - owes
    // it a CloseAudio(). Set before the buffer is allocated, because a failure to allocate the buffer
    // leaves the device open just the same.
    s_audioOpened = true;

    // The number of samples the SCSP emits per frame is governed by how much emulated time each
    // frame advances, which is 1/RefreshRate seconds. So the audio frame accounting must use
    // RefreshRate, not the separate (legacy) SoundFreq setting. If the two disagree the buffer
    // steadily drains - RefreshRate above SoundFreq - or grows without bound (below), and once the
    // buffer empties every additional frame is an audible under-run stutter.
    float soundFreq_Hz = (float)s_config->Get("RefreshRate").ValueAsDefault<float>(0.0f);
    if (soundFreq_Hz <= 0.0f)
        soundFreq_Hz = (float)s_config->Get("SoundFreq").ValueAsDefault<float>(60.0f);
    if (soundFreq_Hz>MAX_SND_FREQ)
        soundFreq_Hz = MAX_SND_FREQ;
    if (soundFreq_Hz<MIN_SND_FREQ)
        soundFreq_Hz = MIN_SND_FREQ;
    samples_per_frame_host = (INT32)(SAMPLE_RATE_M3 / soundFreq_Hz);
    {
        // Warn if the legacy setting disagrees, since that is almost always a misconfiguration.
        const float legacy = (float)s_config->Get("SoundFreq").ValueAsDefault<float>(60.0f);
        if (std::abs(legacy - soundFreq_Hz) > 0.01f)
            InfoLog("Audio: SoundFreq %.4f differs from RefreshRate %.4f; using RefreshRate for frame accounting.",
                legacy, soundFreq_Hz);
    }
    bytes_per_sample_host = (nbHostAudioChannels * sizeof(INT16));
    bytes_per_frame_host =  (samples_per_frame_host * bytes_per_sample_host);

    // Set up fractional per-frame sample generation. samples_per_frame_host truncates (766 for the
    // exact 766.63 at the arcade rate), which loses ~0.63 samples per frame and drains the buffer
    // by 4% of the deficit... enough to under-run within seconds. Carrying the remainder keeps the
    // long-run rate exact.
    s_samplesPerFrameExact = (double)SAMPLE_RATE_M3 / (double)soundFreq_Hz;
    s_sampleAccumulator = 0.0;
    InfoLog("Audio: %d samples per frame nominal (%.4f exact) at %.4f Hz.", samples_per_frame_host, s_samplesPerFrameExact, soundFreq_Hz);


    // Create audio buffer. The latency is configurable so that a machine which struggles to keep
    // the main loop running (a busy GPU, a capture application competing for it) can trade a little
    // output latency for a buffer that actually rides out the pauses.
    unsigned latencyPercent = s_config->Get("AudioLatency").ValueAsDefault<unsigned>(DEFAULT_LATENCY_PERCENT);
    if (latencyPercent < 5) latencyPercent = 5;
    if (latencyPercent > MAX_LATENCY) latencyPercent = MAX_LATENCY;
    uint32_t bufferSize = ((SAMPLE_RATE_M3 * latencyPercent) / MAX_LATENCY) * bytes_per_sample_host;
    InfoLog("Audio: buffer %u ms (%u%% of maximum).", (unsigned)((SAMPLE_RATE_M3 * latencyPercent / MAX_LATENCY) / 44.1), latencyPercent);
    if (!(bufferSize % bytes_per_sample_host == 0)) {
        return ErrorLog("must be an integer multiple of the sample size\n");
    }
    audioBufferSize = bufferSize;

    int minBufferSize = 3 * bytes_per_frame_host;
    audioBufferSize = std::max<int>(minBufferSize, audioBufferSize);
    audioBuffer = new(std::nothrow) INT8[audioBufferSize];
    if (audioBuffer == NULL) {
        float audioBufMB = (float)audioBufferSize / (float)0x100000;
        return ErrorLog("Insufficient memory for audio latency buffer (need %1.1f MB).", audioBufMB);
    }
    memset(audioBuffer, 0, sizeof(INT8) * audioBufferSize);

    // Set initial play position to be beginning of buffer and initial write position to be half-way into buffer
    playPos = 0;
    uint32_t endOfBuffer = bufferSize - bytes_per_frame_host;
    uint32_t midpointAfterFirstFrameUnaligned = bytes_per_frame_host + (bufferSize - bytes_per_frame_host) / 2;
    uint32_t extraPaddingNeeded = (bytes_per_frame_host - midpointAfterFirstFrameUnaligned % bytes_per_frame_host) % bytes_per_frame_host;
    uint32_t midpointAfterFirstFrame = midpointAfterFirstFrameUnaligned + extraPaddingNeeded;
    if (!((endOfBuffer % (nbHostAudioChannels*sizeof(INT16))) == 0)) {
        return ErrorLog("must be an integer multiple of the sample size\n");
    }
    if (!((midpointAfterFirstFrame % nbHostAudioChannels*sizeof(INT16)) == 0)) {
        return ErrorLog("must be an integer multiple of the sample size\n");
    }

    writePos = std::min<int>(endOfBuffer, midpointAfterFirstFrame);
    writeWrapped = false;

    // Reset counters
    underRuns = 0;
    overRuns = 0;

    // Start audio playing
    if (s_audioDevice != 0)
        SDL_PauseAudioDevice(s_audioDevice, 0);
    else
        SDL_PauseAudio(0);
    return Result::OKAY;
}

bool IsAudioBufferFull(void)
{
    // The AudioDevice may not be open yet (or at all), in which case there is no buffer to fill.
    if (audioBuffer == NULL)
        return false;

    LockAudioBuffer();
    // Same predicate OutputAudio() uses for its bufferFull result and for detecting an over-run.
    const bool full = writePos + 2 * bytes_per_frame_host > playPos + audioBufferSize;
    UnlockAudioBuffer();
    return full;
}

bool OutputAudio(unsigned numSamples, const float* leftFrontBuffer, const float* rightFrontBuffer, const float* leftRearBuffer, const float* rightRearBuffer, bool flipStereo)
{
    //printf("OutputAudio(%u) [writePos = %u, writeWrapped = %s, playPos = %u, audioBufferSize = %u]\n",
    //	numSamples, writePos, (writeWrapped ? "true" : "false"), playPos, audioBufferSize);

    UINT32 bytesRemaining;
    UINT32 bytesToCopy;
    INT16* src;

    // Clamp to what the mix buffer can actually hold. Do NOT clamp to the nominal per-frame count:
    // the SCSP emits 766 samples on some frames and 767 on others when running at 57.5242 Hz, so a
    // cap of exactly samples_per_frame_host would silently drop the extra sample every few frames.
    const unsigned maxFrameSamples = SAMPLE_RATE_M3 / MIN_SND_FREQ;
    if (numSamples > maxFrameSamples)
        numSamples = maxFrameSamples;

    // Mix together left and right channels into single chunk of data
    INT16 mixBuffer[NUM_CHANNELS_M3 * maxFrameSamples];
    MixChannels(numSamples, leftFrontBuffer, rightFrontBuffer, leftRearBuffer, rightRearBuffer, mixBuffer, flipStereo);

    // Lock SDL audio callback so that it doesn't interfere with following code
    LockAudioBuffer();

    // Calculate number of bytes for current sound chunk
    UINT32 numBytes = numSamples * bytes_per_sample_host;

    // Get end of current play region (writing must occur past this point)
    UINT32 playEndPos = playPos + bytes_per_frame_host;

    // Undo any wrap-around of the write position that may have occurred to create following ordering: playPos < playEndPos < writePos
    if (playEndPos > writePos && writeWrapped)
        writePos += audioBufferSize;

    // Check if play region has caught up with write position and now overlaps it (ie buffer under-run)
    if (playEndPos > writePos)
    {
        underRuns++;

        //printf("Audio buffer under-run #%u in OutputAudio(%u) [writePos = %u, writeWrapped = %s, playPos = %u, audioBufferSize = %u, numBytes = %u]\n",
        //	underRuns, numSamples, writePos, (writeWrapped ? "true" : "false"), playPos, audioBufferSize, numBytes);

        // See what action to take on under-run
		if (underRunLoop)
		{
            // If loop, then move play position back to beginning of data in buffer
            playPos = writePos + numBytes + bytes_per_frame_host;

            // Check if play position has moved past end of buffer
            if (playPos >= audioBufferSize)
                // If so, wrap it around to beginning again (but keep write wrapped flag as before)
                playPos -= audioBufferSize;
			else
			{
                // Otherwise, set write wrapped flag as will now appear as if write has wrapped but play position has not
                writeWrapped = true;
                writePos += audioBufferSize;
            }
		}
		else
		{
            // Otherwise, bump write position forward in chunks until it is past end of play region
			do
			{
                writePos += numBytes;
			}
			while (playEndPos > writePos);
        }
    }

    // Check if write position has caught up with play region and now overlaps it (ie buffer over-run)
    bool overRun = writePos + numBytes > playPos + audioBufferSize;

    bool bufferFull = writePos + 2 * bytes_per_frame_host > playPos + audioBufferSize;

    // Move write position back to within buffer
    if (writePos >= audioBufferSize)
        writePos -= audioBufferSize;

    // Handle buffer over-run
    if (overRun)
    {
        overRuns++;

        //printf("Audio buffer over-run #%u in OutputAudio(%u) [writePos = %u, writeWrapped = %s, playPos = %u, audioBufferSize = %u, numBytes = %u]\n",
        //	overRuns, numSamples, writePos, (writeWrapped ? "true" : "false"), playPos, audioBufferSize, numBytes);

        bufferFull = true;

        // Discard current chunk of data
    }
    else
    {
    src = mixBuffer;
    INT8* dst1;
    INT8* dst2;
    UINT32 len1;
    UINT32 len2;

    // Check if write region extends past end of buffer
	if (writePos + numBytes > audioBufferSize)
	{
        // If so, split write region into two
        dst1 = audioBuffer + writePos;
        dst2 = audioBuffer;
        len1 = audioBufferSize - writePos;
        len2 = numBytes - len1;
	}
	else
	{
        // Otherwise, just copy whole region
        dst1 = audioBuffer + writePos;
        dst2 = NULL;
        len1 = numBytes;
        len2 = 0;
    }

    // Copy chunk to write position in buffer
    bytesRemaining = numBytes;
    bytesToCopy = (bytesRemaining > len1 ? len1 : bytesRemaining);
    memcpy(dst1, src, bytesToCopy);

    // Adjust for number of bytes copied
    bytesRemaining -= bytesToCopy;
    src = (INT16*)((UINT8*)src + bytesToCopy);

	if (bytesRemaining)
	{
        // If write region was split into two, copy second half of chunk into buffer as well
        bytesToCopy = (bytesRemaining > len2 ? len2 : bytesRemaining);
        memcpy(dst2, src, bytesToCopy);
    }

    // Move write position forward for next time
    writePos += numBytes;

    // Check if write position has moved past end of buffer
	if (writePos >= audioBufferSize)
	{
        // If so, wrap it around to beginning again and set write wrapped flag
        writePos -= audioBufferSize;
        writeWrapped = true;
    }
    }

    // Unlock SDL audio callback
    UnlockAudioBuffer();

    // Return whether buffer is half full
    return bufferFull;
}

void CloseAudio()
{
    // Safe to call whether or not audio was ever opened: the caller that unwinds an error has no way
    // to know, and asking it to find out is how the device came to be leaked in the first place.
    if (!s_audioOpened)
        return;

    s_audioOpened = false;

    // Report buffer health before tearing the device down. A non-zero and climbing
    // under-run count means the emulated frame rate is running slow relative to the
    // audio clock, which is what makes the sound stutter.
    InfoLog("Audio: %u buffer under-run(s), %u over-run(s).", underRuns, overRuns);

    // Close SDL audio output
    if (s_audioDevice != 0)
    {
        SDL_CloseAudioDevice(s_audioDevice);
        s_audioDevice = 0;
    }
    else
        SDL_CloseAudio();

    // Delete audio buffer
    delete[] audioBuffer;
    audioBuffer = nullptr;
}

void GetAudioBufferStats(unsigned* underrunCount, unsigned* overrunCount)
{
    if (underrunCount)
        *underrunCount = underRuns;
    if (overrunCount)
        *overrunCount = overRuns;
}
