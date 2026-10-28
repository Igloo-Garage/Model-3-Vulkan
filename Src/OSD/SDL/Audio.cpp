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
#include "Model3/Model3Timing.h"

#include "Supermodel.h"
#include "SDLIncludes.h"

#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>

// Format the emulator mixes in. The SCSP and the DSB both produce float samples in the S16 domain.
// The SDL3 build hands those to the stream as F32 and lets SDL convert to the device's real format,
// so the mix is not quantised in the middle and the single clamp happens at the very end. The SDL2
// build keeps the historical S16 output so the legacy executable is unchanged.
#if defined(SUPERMODEL_PLATFORM_SDL3)
typedef float AudioSample;
#define HOST_AUDIO_FORMAT SDL_AUDIO_F32
#else
typedef INT16 AudioSample;
#define HOST_AUDIO_FORMAT AUDIO_S16SYS
#endif

static inline AudioSample ToHostSample(float sample)
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
    // Input is in the SCSP's S16-scaled float domain; the stream wants -1.0..+1.0. This is the only
    // clamp in the path, so nothing that fits in 16 bits is clipped before it arrives here.
    return std::max(-1.0f, std::min(1.0f, sample * (1.0f / 32768.0f)));
#else
    INT32 xi = (INT32)sample;
    if (xi > INT16_MAX)
        xi = INT16_MAX;
    if (xi < INT16_MIN)
        xi = INT16_MIN;
    return (INT16)xi;
#endif
}

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

#define BYTES_PER_SAMPLE_M3   (NUM_CHANNELS_M3 * sizeof(AudioSample))
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

/*
 * Mute state is process-wide, but a game session is not.
 *
 * SetAudioEnabled(false) is how the pause paths silence the output, and each of them is undone by a
 * matching SetAudioEnabled(true) - except when the session ends while it is still paused (the in-game
 * panel pauses the emulator, so quitting from the panel is the ordinary way to do that). The settings
 * window runs every subsequent game in this same process, so a mute left set at shutdown was
 * inherited by the next game: it started with no sound at all, from any renderer, until the program
 * itself was restarted. OpenAudio() and CloseAudio() force the flag back on for exactly that reason.
 */
static bool enabled = true;         // True if sound output is enabled

/*
 * Optional output-level probe, enabled with SM3D_AUDIO_LEVEL=1.
 *
 * Reports once a second what the mixer is actually handing to the device, plus the peak the sound
 * board produced before the balance/volume stages. The two together separate "the emulated machine
 * is silent" (a save state or a game problem) from "the machine is playing but the output is muted"
 * (an OSD problem) - which is otherwise indistinguishable from the log.
 */
static bool s_logLevel = false;
static unsigned s_levelSamples = 0;
static float s_levelPeak = 0.0f;
static float s_levelSourcePeak = 0.0f;

/*
 * Peak levels for the statistics readout.
 *
 * Measured per frame on the sound board's own buffers, so they are independent of the host channel
 * layout, of the balance/volume stages and of the mute - the readout needs exactly that to separate
 * a game that is not producing audio from one whose output is muted. The value is a fraction of
 * full scale and can exceed 1.0: the SCSP and the DSB sum before the single clamp in the callback.
 *
 * Atomics because the sound board can run on its own thread and the readout runs on the UI thread.
 * GetAudioStatus() exchanges them for zero, so the pair describes a window between two samples
 * rather than a maximum since the session started.
 */
static std::atomic<float> s_meterPeakLeft{0.0f};
static std::atomic<float> s_meterPeakRight{0.0f};

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

// Fractional per-frame sample generation (see GetSamplesThisFrame() in Audio.h). The exact
// count is 44100 divided by the *logic* rate, which is always the Model 3 video rate now - the
// presentation rate no longer controls how much emulated time a frame covers. The accumulator
// carries the fractional remainder between calls.
static double s_samplesPerFrameExact = kModel3SamplesPerStep;
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

#if defined(SUPERMODEL_PLATFORM_SDL3)
// SDL3 has no legacy open path: there is exactly one stream, and it owns the device.
static SDL_AudioStream* s_audioStream = NULL;
#else
// Non-zero when a specific output device was opened with SDL_OpenAudioDevice(). Zero means
// the legacy device opened by SDL_OpenAudio() is in use, which needs the global lock/pause
// calls instead of the per-device ones.
static SDL_AudioDeviceID s_audioDevice = 0;
#endif

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

/*
 * Soft start.
 *
 * A game resumed from a save state starts with the SCSP and the DSB mid-waveform: the first sample
 * is not a zero crossing, so playback steps from silence to whatever level the restored machine was
 * at, and that step is the click heard when a state is loaded. The same applies after an unpause or
 * a buffer flush.
 *
 * The ramp is armed by those events but only starts on the first block that is not silent - the
 * sound that needs it can begin seconds after the restore, and a ramp consumed during silence would
 * leave the click untouched.
 */
static constexpr unsigned kFadeInSamples = SAMPLE_RATE_M3 / 50;   // 20 ms
static unsigned s_fadeInRemaining = 0;

static void ArmAudioFadeIn(void)
{
    s_fadeInRemaining = kFadeInSamples;
}

static void LockAudioBuffer(void)
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
    if (s_audioStream != NULL)
        SDL_LockAudioStream(s_audioStream);
#else
    if (s_audioDevice != 0)
        SDL_LockAudioDevice(s_audioDevice);
    else
        SDL_LockAudio();
#endif
}

static void UnlockAudioBuffer(void)
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
    if (s_audioStream != NULL)
        SDL_UnlockAudioStream(s_audioStream);
#else
    if (s_audioDevice != 0)
        SDL_UnlockAudioDevice(s_audioDevice);
    else
        SDL_UnlockAudio();
#endif
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
    if (newEnabled == enabled)
        return;

    // One line per transition, so a silent session can be told apart from a muted one in the log.
    InfoLog("Audio: output %s.", newEnabled ? "unmuted" : "muted");

    // Resuming starts from an arbitrary point in the buffer, so ramp back in rather than stepping.
    if (newEnabled)
        ArmAudioFadeIn();
    enabled = newEnabled;
}

void FlushAudioBuffer(void)
{
    if (!s_audioOpened)
        return;

    // Take the lock the callback runs under: playPos is read there, and moving it underneath a
    // callback would let the two disagree about where the buffer has wrapped to.
    LockAudioBuffer();
    writePos = 0;
    playPos = 0;
    writeWrapped = false;
#if defined(SUPERMODEL_PLATFORM_SDL3)
    if (s_audioStream != NULL)
        SDL_ClearAudioStream(s_audioStream);
#else
    if (s_audioDevice != 0)
        SDL_ClearQueuedAudio(s_audioDevice);
#endif
    UnlockAudioBuffer();

    // Whatever comes next starts from silence, so it should not start at full level.
    ArmAudioFadeIn();
}

/// <summary>
/// Set game audio mixing type
/// </summary>
/// <param name="type"></param>
void SetAudioType(Game::AudioTypes type)
{
    AudioType = type;
}

static float MixFloat(float x, float y)
{
    return (x + y)*0.5f;
}

// Raises a meter peak to the larger of the two. Contention is two threads at most, and one attempt
// per frame, so the compare-exchange never spins in practice.
static void RaisePeak(std::atomic<float>& peak, float value)
{
    float current = peak.load(std::memory_order_relaxed);
    while (value > current &&
        !peak.compare_exchange_weak(current, value, std::memory_order_relaxed))
    {
    }
}

/*
 * Destination for one callback's worth of mixed audio.
 *
 * SDL2 hands the callback a raw buffer to fill; SDL3 hands it a stream to push into. Doing the
 * push here keeps the ring-buffer bookkeeping below byte-for-byte identical for both backends.
 */
class AudioOut
{
public:
#if defined(SUPERMODEL_PLATFORM_SDL3)
    explicit AudioOut(SDL_AudioStream* stream) : m_stream(stream) {}

    void Write(const void* src, UINT32 len)
    {
        if (len)
            SDL_PutAudioStreamData(m_stream, src, (int)len);
    }

    void Silence(UINT32 len)
    {
        // Bounded stack buffer so a large request cannot blow the audio thread's stack.
        static const Uint8 zeros[4096] = { 0 };
        while (len)
        {
            const UINT32 chunk = (len > sizeof(zeros) ? (UINT32)sizeof(zeros) : len);
            SDL_PutAudioStreamData(m_stream, zeros, (int)chunk);
            len -= chunk;
        }
    }

private:
    SDL_AudioStream* m_stream;
#else
    explicit AudioOut(Uint8* stream) : m_pos(stream) {}

    void Write(const void* src, UINT32 len)
    {
        if (len)
        {
            memcpy(m_pos, src, len);
            m_pos += len;
        }
    }

    void Silence(UINT32 len)
    {
        memset(m_pos, 0, len);
        m_pos += len;
    }

private:
    Uint8* m_pos;
#endif
};

static void MixOut(AudioOut& out, UINT32 len)
{
    //printf("MixOut(%u) [writePos = %u, writeWrapped = %s, playPos = %u, audioBufferSize = %u]\n",
    //	len, writePos, (writeWrapped ? "true" : "false"), playPos, audioBufferSize);

    // Defensive: a request larger than the whole buffer could never be satisfied in one pass.
    if (len > audioBufferSize)
        len = audioBufferSize;

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
            // Otherwise, just copy silence to the output and exit
            out.Silence(len);
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
        // If so, copy play region to the output
        out.Write(src1, len1);

        // Also, if not looping on under-runs then blank region out
        if (!underRunLoop)
            memset(src1, 0, len1);

        if (len2)
        {
            // If region was split into two, copy second half to the output as well
            out.Write(src2, len2);

            // Also, if not looping on under-runs then blank region out
            if (!underRunLoop)
                memset(src2, 0, len2);
        }
    }
    else
        // Otherwise, just copy silence to the output
        out.Silence(len);

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

#if defined(SUPERMODEL_PLATFORM_SDL3)

// SDL3 get-callback: fired on the audio thread when the device wants more data. Anything put
// here is consumed by the request that triggered the callback.
static void SDLCALL PlayCallback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount)
{
    (void)userdata;
    (void)total_amount;

    if (additional_amount <= 0)
        return;

    AudioOut out(stream);
    MixOut(out, (UINT32)additional_amount);
}

#else

static void PlayCallback(void* data, Uint8* stream, int len)
{
    (void)data;

    AudioOut out(stream);
    MixOut(out, (UINT32)len);
}

#endif

static void MixChannels(unsigned numSamples, const float* leftFrontBuffer, const float* rightFrontBuffer, const float* leftRearBuffer, const float* rightRearBuffer, AudioSample* p, bool flipStereo)
{
    if (nbHostAudioChannels == 1) {
        for (unsigned i = 0; i < numSamples; i++) {
            const float monovalue = MixFloat(
                MixFloat(leftFrontBuffer[i] * balanceFactorFrontLeft,rightFrontBuffer[i] * balanceFactorFrontRight),
                MixFloat(leftRearBuffer[i]  * balanceFactorRearLeft, rightRearBuffer[i]  * balanceFactorRearRight));
            *p++ = ToHostSample(monovalue);
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
                const float leftvalue = MixFloat(leftFrontBuffer[i] * balanceFactorFrontLeft, leftRearBuffer[i] * balanceFactorRearLeft);
                const float rightvalue = MixFloat(rightFrontBuffer[i]*balanceFactorFrontRight, rightRearBuffer[i]*balanceFactorRearRight);
                if (flipStereo) // swap left and right channels
                {
                    *p++ = ToHostSample(rightvalue);
                    *p++ = ToHostSample(leftvalue);
                } else {
                    *p++ = ToHostSample(leftvalue);
                    *p++ = ToHostSample(rightvalue);
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
                    const float monovalue = MixFloat(MixFloat(frontLeftValue, frontRightValue), MixFloat(rearLeftValue, rearRightValue));
                    *p++ = ToHostSample(monovalue);
                    *p++ = ToHostSample(monovalue);
                    *p++ = ToHostSample(monovalue);
                    *p++ = ToHostSample(monovalue);
                } break;

                case Game::STEREO_LR:
                case Game::STEREO_RL: {
                    const float leftvalue =  MixFloat(frontLeftValue, frontRightValue);
                    const float rightvalue = MixFloat(rearLeftValue,  rearRightValue);
                    if (flipStereo) // swap left and right channels
                    {
                        *p++ = ToHostSample(rightvalue);
                        *p++ = ToHostSample(leftvalue);
                        *p++ = ToHostSample(rightvalue);
                        *p++ = ToHostSample(leftvalue);
                    } else {
                        *p++ = ToHostSample(leftvalue);
                        *p++ = ToHostSample(rightvalue);
                        *p++ = ToHostSample(leftvalue);
                        *p++ = ToHostSample(rightvalue);
                    }
                } break;

                case Game::QUAD_1_FLR_2_RLR:
                case Game::QUAD_1_FRL_2_RRL: {
                    // Normal channels Front Left/Right then Rear Left/Right
                    if (flipStereo) // swap left and right channels
                    {
                        *p++ = ToHostSample(frontRightValue);
                        *p++ = ToHostSample(frontLeftValue);
                        *p++ = ToHostSample(rearRightValue);
                        *p++ = ToHostSample(rearLeftValue);
                    } else {
                        *p++ = ToHostSample(frontLeftValue);
                        *p++ = ToHostSample(frontRightValue);
                        *p++ = ToHostSample(rearLeftValue);
                        *p++ = ToHostSample(rearRightValue);
                    }
                } break;

                case Game::QUAD_1_RLR_2_FLR:
                case Game::QUAD_1_RRL_2_FRL:
                    // Reversed channels Front/Rear Left then Front/Rear Right
                    if (flipStereo) // swap left and right channels
                    {
                        *p++ = ToHostSample(rearRightValue);
                        *p++ = ToHostSample(rearLeftValue);
                        *p++ = ToHostSample(frontRightValue);
                        *p++ = ToHostSample(frontLeftValue);
                    } else {
                        *p++ = ToHostSample(rearLeftValue);
                        *p++ = ToHostSample(rearRightValue);
                        *p++ = ToHostSample(frontLeftValue);
                        *p++ = ToHostSample(frontRightValue);
                    }
                    break;

                case Game::QUAD_1_LR_2_FR_MIX:
                    // Split mix: one goes to left/right, other front/rear (mono)
                    // =>Remix all!
                    {
                        const float newfrontLeftValue = MixFloat(frontLeftValue, rearLeftValue);
                        const float newfrontRightValue = MixFloat(frontLeftValue, rearRightValue);
                        const float newrearLeftValue = MixFloat(frontRightValue, rearLeftValue);
                        const float newrearRightValue = MixFloat(frontRightValue, rearRightValue);

                        if (flipStereo) // swap left and right channels
                        {
                            *p++ = ToHostSample(newfrontRightValue);
                            *p++ = ToHostSample(newfrontLeftValue);
                            *p++ = ToHostSample(newrearRightValue);
                            *p++ = ToHostSample(newrearLeftValue);
                        } else {
                            *p++ = ToHostSample(newfrontLeftValue);
                            *p++ = ToHostSample(newfrontRightValue);
                            *p++ = ToHostSample(newrearLeftValue);
                            *p++ = ToHostSample(newrearRightValue);
                        }
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

    s_logLevel = (std::getenv("SM3D_AUDIO_LEVEL") != nullptr);

    // A session always starts audible. The flag is file-scope and survives from the previous session
    // in this process, which is how quitting a paused game used to leave every later game silent.
    if (!enabled)
        InfoLog("Audio: output was still muted from the previous session; re-enabling.");
    enabled = true;
    s_meterPeakLeft.store(0.0f, std::memory_order_relaxed);
    s_meterPeakRight.store(0.0f, std::memory_order_relaxed);

    // Initialize SDL audio sub-system
#if defined(SUPERMODEL_PLATFORM_SDL3)
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return ErrorLog("Unable to initialize SDL audio sub-system: %s\n", SDL_GetError());
#else
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return ErrorLog("Unable to initialize SDL audio sub-system: %s\n", SDL_GetError());
#endif

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
#if defined(SUPERMODEL_PLATFORM_SDL3)
    desired.format = HOST_AUDIO_FORMAT;
#else
    desired.format = AUDIO_S16SYS;
    desired.samples = playSamples;
    desired.callback = PlayCallback;
#endif

#if defined(SUPERMODEL_PLATFORM_SDL3)
    // SDL3 opens a device and creates the stream that feeds it in one call. The stream starts
    // paused, so nothing is played until SDL_ResumeAudioStreamDevice() below. Conversion to the
    // device's real format is done by the stream, so the spec above is what we mix at.
    const std::string requestedDevice = s_config->Get("AudioDevice").ValueAsDefault<std::string>("");
    SDL_AudioDeviceID deviceId = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (!requestedDevice.empty())
    {
        int numDevices = 0;
        SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&numDevices);
        for (int i = 0; i < numDevices; i++)
        {
            const char* name = SDL_GetAudioDeviceName(devices[i]);
            if (name && SDL_strcasecmp(name, requestedDevice.c_str()) == 0)
            {
                deviceId = devices[i];
                break;
            }
        }
        if (devices)
            SDL_free(devices);

        if (deviceId == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
            ErrorLog("Audio device '%s' not found. Falling back to system default.", requestedDevice.c_str());
    }

    // A quad mix only makes sense when the endpoint can actually reproduce four speakers. SDL will
    // accept a 4-channel stream on a stereo device and fold it down with its own gain law, but then
    // the OSD would still claim "4 ch" and the level would not match what "2 channels" sounds like.
    // Ask the device what it has and mix at that count: a stereo endpoint then gets the rear pair
    // folded into the front pair by MixChannels(), which is exactly what selecting 2 channels did.
    auto mixForDevice = [&](SDL_AudioDeviceID id) {
        SDL_AudioSpec deviceSpec{};
        const char* deviceName = SDL_GetAudioDeviceName(id);
        if (!SDL_GetAudioDeviceFormat(id, &deviceSpec, nullptr) || deviceSpec.channels <= 0) {
            InfoLog("Audio: unable to query the output device format; mixing %d channel(s).", nbHostAudioChannels);
            return;
        }
        if (deviceSpec.channels < nbHostAudioChannels) {
            const int folded = (deviceSpec.channels >= 2) ? 2 : 1;
            InfoLog("Audio: device '%s' has %d channel(s) (%d Hz); folding the quad mix down to %d.",
                deviceName ? deviceName : "default", deviceSpec.channels, deviceSpec.freq, folded);
            nbHostAudioChannels = folded;
            desired.channels = folded;
        } else {
            InfoLog("Audio: device '%s' has %d channel(s) (%d Hz); mixing %d channel(s).",
                deviceName ? deviceName : "default", deviceSpec.channels, deviceSpec.freq, nbHostAudioChannels);
        }
    };
    mixForDevice(deviceId);

    s_audioStream = SDL_OpenAudioDeviceStream(deviceId, &desired, NULL, NULL);
    if (s_audioStream == NULL && !requestedDevice.empty() && deviceId != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
    {
        // The named device exists but refused the stream; keep going on the default one.
        ErrorLog("Unable to open audio device '%s' (%s). Falling back to system default.",
            requestedDevice.c_str(), SDL_GetError());
        deviceId = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
        mixForDevice(deviceId);
        s_audioStream = SDL_OpenAudioDeviceStream(deviceId, &desired, NULL, NULL);
    }
    if (s_audioStream == NULL && desired.channels > 2)
    {
        // Some endpoints advertise four channels but refuse a four-channel stream. Retrying at
        // stereo keeps the session alive and the mixer folds the rear pair in, rather than
        // leaving the player with only the front half of the cabinet's sound.
        InfoLog("Audio: %d-channel stream refused (%s); retrying at 2 channels.",
            desired.channels, SDL_GetError());
        nbHostAudioChannels = 2;
        desired.channels = 2;
        s_audioStream = SDL_OpenAudioDeviceStream(deviceId, &desired, NULL, NULL);
    }
    if (s_audioStream == NULL)
    {
        if (desired.channels == 2) {
            return ErrorLog("Unable to open 44.1KHz 2-channel audio with SDL: %s\n", SDL_GetError());
        } else if (desired.channels == 4) {
            return ErrorLog("Unable to open 44.1KHz 4-channel audio with SDL: %s\n", SDL_GetError());
        } else {
            return ErrorLog("Unable to open 44.1KHz channel audio with SDL: %s\n", SDL_GetError());
        }
    }
    else if (!requestedDevice.empty() && deviceId != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
    {
        InfoLog("Audio: using output device '%s'.", requestedDevice.c_str());
    }

    // Refill from our own latency buffer on the audio thread. This is the SDL3 equivalent of the
    // SDL2 playback callback and keeps the buffer model - and therefore the tuning - unchanged.
    SDL_SetAudioStreamGetCallback(s_audioStream, PlayCallback, NULL);
#else
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
#endif

    // From here on the device is open, so every exit - including the error paths in the caller - owes
    // it a CloseAudio(). Set before the buffer is allocated, because a failure to allocate the buffer
    // leaves the device open just the same.
    s_audioOpened = true;

    // A session starts from a restored machine (a loaded save, or a fresh boot mid-stage): ramp in
    // rather than stepping from silence to the current level. See ArmAudioFadeIn().
    ArmAudioFadeIn();

    // The number of samples the SCSP emits per frame is governed by how much emulated time each
    // frame advances, and that is now always one Model 3 video frame (57.524 Hz), independent of
    // the presentation rate. RefreshRate and SoundFreq are legacy settings and no longer feed this
    // calculation: a 60 Hz presentation of a 57.524 Hz simulation must still produce 766/767
    // samples per emulated frame, or the audio clock would run 4.3% slow.
    const double logicHz = kModel3StepHz;
    samples_per_frame_host = (INT32)(kModel3SamplesPerStep + 0.5);
    bytes_per_sample_host = (nbHostAudioChannels * sizeof(AudioSample));
    bytes_per_frame_host =  (samples_per_frame_host * bytes_per_sample_host);

    // Carrying the remainder keeps the long-run rate exact: 766.63 samples per frame cannot be an
    // integer, and truncating it would drain the buffer by the deficit every frame.
    s_samplesPerFrameExact = kModel3SamplesPerStep;
    s_sampleAccumulator = 0.0;
    InfoLog("Audio: %d samples per frame nominal (%.4f exact) at logic %.5f Hz.",
        samples_per_frame_host, s_samplesPerFrameExact, logicHz);
    if (s_config->TryGet("RefreshRate") != nullptr || s_config->TryGet("SoundFreq") != nullptr)
        InfoLog("Audio: RefreshRate/SoundFreq no longer affect sample generation; the fixed 57.524 Hz logic clock does.");


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
    if (!((endOfBuffer % (nbHostAudioChannels*sizeof(AudioSample))) == 0)) {
        return ErrorLog("must be an integer multiple of the sample size\n");
    }
    if (!((midpointAfterFirstFrame % nbHostAudioChannels*sizeof(AudioSample)) == 0)) {
        return ErrorLog("must be an integer multiple of the sample size\n");
    }

    writePos = std::min<int>(endOfBuffer, midpointAfterFirstFrame);
    writeWrapped = false;

    // Reset counters
    underRuns = 0;
    overRuns = 0;

    // Start audio playing
#if defined(SUPERMODEL_PLATFORM_SDL3)
    SDL_ResumeAudioStreamDevice(s_audioStream);
#else
    if (s_audioDevice != 0)
        SDL_PauseAudioDevice(s_audioDevice, 0);
    else
        SDL_PauseAudio(0);
#endif
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
    AudioSample* src;

    // Clamp to what the mix buffer can actually hold. Do NOT clamp to the nominal per-frame count:
    // the SCSP emits 766 samples on some frames and 767 on others when running at 57.5242 Hz, so a
    // cap of exactly samples_per_frame_host would silently drop the extra sample every few frames.
    const unsigned maxFrameSamples = SAMPLE_RATE_M3 / MIN_SND_FREQ;
    if (numSamples > maxFrameSamples)
        numSamples = maxFrameSamples;

    // Mix together left and right channels into single chunk of data
    AudioSample mixBuffer[NUM_CHANNELS_M3 * maxFrameSamples];
    MixChannels(numSamples, leftFrontBuffer, rightFrontBuffer, leftRearBuffer, rightRearBuffer, mixBuffer, flipStereo);

    /*
     * Soft start, see ArmAudioFadeIn(). Skipped while the block is silent: the sound that needs the
     * ramp can begin long after the state was restored, and consuming the ramp during silence would
     * leave the click it exists for untouched.
     */
    if (s_fadeInRemaining > 0)
    {
        const unsigned values = numSamples * (unsigned)nbHostAudioChannels;
        bool silent = true;
        for (unsigned i = 0; i < values && silent; ++i)
            silent = (mixBuffer[i] == (AudioSample)0);

        if (!silent)
        {
            for (unsigned i = 0; i < values && s_fadeInRemaining > 0; ++i)
            {
                const float gain = (float)(kFadeInSamples - s_fadeInRemaining) / (float)kFadeInSamples;
                mixBuffer[i] = (AudioSample)(mixBuffer[i] * gain);
                --s_fadeInRemaining;
            }
        }
    }

    /*
     * Peak of what the sound board produced this frame, for the statistics readout. Grouped by
     * speaker side and taken before the balance/volume stages and the mute, so the figure answers
     * "is the emulated machine playing sound" rather than "is anything reaching the speakers".
     * The S16 domain is scaled to a fraction of full scale here; values above 1.0 mean the two
     * boards summed past it before the callback's clamp.
     */
    {
        float peakLeft = 0.0f;
        float peakRight = 0.0f;
        for (unsigned i = 0; i < numSamples; ++i)
        {
            peakLeft = std::max(peakLeft, std::max(std::fabs(leftFrontBuffer[i]), std::fabs(leftRearBuffer[i])));
            peakRight = std::max(peakRight, std::max(std::fabs(rightFrontBuffer[i]), std::fabs(rightRearBuffer[i])));
        }
        RaisePeak(s_meterPeakLeft, peakLeft * (1.0f / 32768.0f));
        RaisePeak(s_meterPeakRight, peakRight * (1.0f / 32768.0f));

        if (s_logLevel)
        {
            const unsigned values = numSamples * (unsigned)nbHostAudioChannels;
            for (unsigned i = 0; i < values; ++i)
                s_levelPeak = std::max(s_levelPeak, std::fabs((float)mixBuffer[i]));

            s_levelSourcePeak = std::max(s_levelSourcePeak, std::max(peakLeft, peakRight));

            s_levelSamples += numSamples;
            if (s_levelSamples >= (unsigned)SAMPLE_RATE_M3)
            {
                InfoLog("Audio: source peak %.1f, mix peak %.4f, %s, under-runs %u.",
                    s_levelSourcePeak, s_levelPeak, enabled ? "output on" : "output MUTED", underRuns);
                s_levelPeak = 0.0f;
                s_levelSourcePeak = 0.0f;
                s_levelSamples = 0;
            }
        }
    }

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
    src = (AudioSample*)((UINT8*)src + bytesToCopy);

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
#if defined(SUPERMODEL_PLATFORM_SDL3)
    if (s_audioStream != NULL)
    {
        // Destroying a stream created by SDL_OpenAudioDeviceStream also closes the device.
        SDL_DestroyAudioStream(s_audioStream);
        s_audioStream = NULL;
    }
#else
    if (s_audioDevice != 0)
    {
        SDL_CloseAudioDevice(s_audioDevice);
        s_audioDevice = 0;
    }
    else
        SDL_CloseAudio();
#endif

    // Delete audio buffer
    delete[] audioBuffer;
    audioBuffer = nullptr;

    // Do not leave a mute behind for the next session in this process (see enabled): the session that
    // set it is over, and only the next session's own pause paths may silence it.
    enabled = true;
    s_levelSamples = 0;
    s_levelPeak = 0.0f;
    s_levelSourcePeak = 0.0f;
    s_meterPeakLeft.store(0.0f, std::memory_order_relaxed);
    s_meterPeakRight.store(0.0f, std::memory_order_relaxed);
}

void GetAudioBufferStats(unsigned* underrunCount, unsigned* overrunCount)
{
    if (underrunCount)
        *underrunCount = underRuns;
    if (overrunCount)
        *overrunCount = overRuns;
}

void GetAudioStatus(AudioStatus& status)
{
    status = AudioStatus();
    status.muted = !enabled;
    status.channels = (unsigned)nbHostAudioChannels;
    status.underRuns = underRuns;
    status.overRuns = overRuns;

    /*
     * The peaks are a window, not a running maximum, so reading them clears them. One reader only:
     * the statistics sampler calls this at its own cadence, and two callers would each see half a
     * window. Cleared even without a device below, so a stale figure cannot outlive a session.
     */
    status.peakLeft = s_meterPeakLeft.exchange(0.0f, std::memory_order_relaxed);
    status.peakRight = s_meterPeakRight.exchange(0.0f, std::memory_order_relaxed);

    // Nothing to measure before a device is open: a session started with -no-sound has no buffer.
    if (!s_audioOpened || audioBuffer == NULL || bytes_per_frame_host == 0)
        return;

    /*
     * Snapshotted under the same lock the producer and the callback use. Reading the two positions
     * without it can catch the callback between advancing playPos and clearing writeWrapped, which
     * yields a fill figure off by a whole buffer length - exactly the kind of number that sends
     * someone looking for a stutter that is not there.
     */
    LockAudioBuffer();
    const UINT32 writeEnd = writePos + (writeWrapped ? audioBufferSize : 0);
    const UINT32 fillBytes = writeEnd > playPos ? writeEnd - playPos : 0;
    const UINT32 capacityBytes = audioBufferSize;
    UnlockAudioBuffer();

    // bytes_per_frame_host is one arcade frame's worth at the emulated rate, so bytes per millisecond
    // is that times the frame rate - no need to involve the sample rate and channel count separately.
    const double bytesPerMs = (double)bytes_per_frame_host * kModel3StepHz / 1000.0;
    status.opened = true;
    status.bufferMs = (unsigned)((double)fillBytes / bytesPerMs + 0.5);
    status.bufferSizeMs = (unsigned)((double)capacityBytes / bytesPerMs + 0.5);
}
