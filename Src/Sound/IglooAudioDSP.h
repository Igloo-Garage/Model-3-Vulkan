/**
 ** REXABYTE Modular Engine SDK (Audio DSP / Camera / Input)
 ** Original standalone component developed from scratch by REXABYTE SOFTWARE INC.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. All Rights Reserved.
 **
 ** Dual-Licensed Component (Commercial License / GPLv3 with Section 7 Additional Terms):
 **
 **   1. Commercial / Proprietary License (Paid License Required for Commercial Use):
 **      This file is the exclusive intellectual property of REXABYTE SOFTWARE INC.
 **      Any use, copying, modification, integration, or distribution of this file (or
 **      derivative works thereof) in any closed-source product, proprietary software,
 **      paid application, commercial arcade cabinet/hardware, or any distribution that
 **      does not disclose full corresponding source code under GPLv3 Strictly Requires
 **      a prior written, paid Commercial License from REXABYTE SOFTWARE INC.
 **      (authorized for official REXABYTE SOFTWARE INC. commercial products including
 **      the Android edition).
 **
 **   2. Open-Source GPLv3 Integration Grant (Non-Proprietary / Full Copyleft Only):
 **      When distributed strictly as part of a complete open-source GPLv3 project
 **      (such as the PC build of Model 3-Vulkan), REXABYTE SOFTWARE INC. grants permission
 **      to use and distribute this file under the GNU General Public License v3.0 SUBJECT
 **      TO the mandatory Section 7 Additional Terms (see LICENSE-ADDITIONAL-TERMS.md):
 **      all REXABYTE SOFTWARE INC. copyright notices, license headers, and in-app author
 **      attributions MUST be preserved intact. Any removal of attribution or closed-source
 **      commercial exploitation immediately terminates this open-source grant.
 **
 ** This file contains zero code, types, or headers from upstream Supermodel.
 **/

#ifndef IGLOO_AUDIO_DSP_H
#define IGLOO_AUDIO_DSP_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Igloo {
namespace AudioDSP {

/*
 * C1-continuous Knee Soft-Clipper for the master audio bus.
 *
 * Below |x| <= kKnee (0.85, i.e. -1.41 dBFS), the transfer function is strictly y = x (bit-exact
 * linear pass-through, zero harmonic coloration). Above the knee, a rational saturation curve
 * smoothly compresses peaks asymptotically toward +/-1.0 without flat-top hard clipping.
 */
inline float MasterSoftClip(float x) noexcept
{
    constexpr float kKnee = 0.85f;
    constexpr float kHeadroom = 1.0f - kKnee;
    constexpr float kInvHeadroom = 1.0f / kHeadroom;

    const float absX = std::fabs(x);
    if (absX <= kKnee)
        return x;

    const float u = (absX - kKnee) * kInvHeadroom;
    const float compressed = kKnee + kHeadroom * (u / (1.0f + u));
    return (x >= 0.0f) ? compressed : -compressed;
}

/*
 * Second-order Butterworth low-pass filter (12 dB/octave, Q = 1/sqrt(2)) for 5.1 LFE extraction.
 * Designed via bilinear transform with frequency pre-warping (default fc = 120 Hz, fs = 44100 Hz).
 * Implemented in Direct Form II Transposed for numerical stability in single-precision float.
 */
class ButterworthBiquadLPF
{
public:
    void Init(double sampleRate, double cutoffHz) noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kSqrt2 = 1.41421356237309504880;
        const double k = std::tan(kPi * cutoffHz / sampleRate);
        const double k2 = k * k;
        const double norm = 1.0 / (1.0 + kSqrt2 * k + k2);

        m_b0 = static_cast<float>(k2 * norm);
        m_b1 = static_cast<float>(2.0 * k2 * norm);
        m_b2 = m_b0;
        m_a1 = static_cast<float>(2.0 * (k2 - 1.0) * norm);
        m_a2 = static_cast<float>((1.0 - kSqrt2 * k + k2) * norm);
        Reset();
    }

    void Reset() noexcept
    {
        m_z1 = 0.0f;
        m_z2 = 0.0f;
    }

    float Process(float x) noexcept
    {
        const float y = m_b0 * x + m_z1;
        m_z1 = m_b1 * x - m_a1 * y + m_z2;
        m_z2 = m_b2 * x - m_a2 * y;
        return y;
    }

private:
    float m_b0 = 0.0f, m_b1 = 0.0f, m_b2 = 0.0f;
    float m_a1 = 0.0f, m_a2 = 0.0f;
    float m_z1 = 0.0f, m_z2 = 0.0f;
};

/*
 * Intelligent 4.0 Quadraphonic -> 5.1 Surround Upmixer.
 *
 * Extracts common-mode center vocal/navigator signal (FC) from the front pair plus subtle cockpit
 * presence from the rear pair (with slight mid-subtraction on FL/FR to prevent center build-up),
 * and extracts the subwoofer channel (LFE < 120 Hz) via a 2nd-order Butterworth low-pass filter.
 * Outputs standard Windows WAVE_FORMAT_EXTENSIBLE / SDL3 channel order: [FL, FR, FC, LFE, RL, RR].
 */
inline void UpmixQuadTo51(
    float& ioFL, float& ioFR, float inRL, float inRR,
    float& outFC, float& outLFE,
    ButterworthBiquadLPF& lfeFilter) noexcept
{
    const float frontMid = 0.5f * (ioFL + ioFR);
    const float rearMid  = 0.5f * (inRL + inRR);
    outFC = 0.70710678f * frontMid + 0.25f * rearMid;
    ioFL -= 0.25f * frontMid;
    ioFR -= 0.25f * frontMid;

    const float quadSum = 0.25f * (ioFL + ioFR + inRL + inRR) * 1.41421356f;
    outLFE = lfeFilter.Process(quadSum);
}

/*
 * 4-tap Catmull-Rom cubic spline interpolation for 12-bit fractional phase (fpart12 in 0..4095).
 * Clamps the reconstructed sample to the signed 16-bit PCM range [-32768, 32767].
 */
inline int32_t CubicInterpolate4Tap(int32_t y0, int32_t y1, int32_t y2, int32_t y3, int32_t fpart12) noexcept
{
    constexpr float kInvPhaseScale = 1.0f / 4096.0f;
    const float t   = static_cast<float>(fpart12) * kInvPhaseScale;
    const float fm1 = static_cast<float>(y0);
    const float f0  = static_cast<float>(y1);
    const float f1  = static_cast<float>(y2);
    const float f2  = static_cast<float>(y3);

    const float c0 = f0;
    const float c1 = 0.5f * (f1 - fm1);
    const float c2 = fm1 - 2.5f * f0 + 2.0f * f1 - 0.5f * f2;
    const float c3 = 0.5f * (f2 - fm1) + 1.5f * (f0 - f1);

    const float out = ((c3 * t + c2) * t + c1) * t + c0;
    const int32_t res = static_cast<int32_t>(std::lrintf(out));
    return std::max<int32_t>(-32768, std::min<int32_t>(32767, res));
}

/*
 * Per-sample linear gain ramper to eliminate frame-boundary zipper noise when hardware
 * attenuation registers step at frame rates (e.g. 57.524 Hz).
 */
class LinearGainRamper
{
public:
    void Reset() noexcept
    {
        m_smoothedL = -1.0f;
        m_smoothedR = -1.0f;
    }

    // Prepares a linear gain ramp for `numSamples` output samples and commits the target gains
    // as the ending state of the block. Returns false if `numSamples <= 0`.
    bool BeginBlock(
        float targetGainL, float targetGainR, int numSamples,
        float& outCurGainL, float& outCurGainR,
        float& outStepGainL, float& outStepGainR) noexcept
    {
        if (m_smoothedL < 0.0f || m_smoothedR < 0.0f)
        {
            m_smoothedL = targetGainL;
            m_smoothedR = targetGainR;
        }

        if (numSamples <= 0)
        {
            m_smoothedL = targetGainL;
            m_smoothedR = targetGainR;
            outCurGainL = targetGainL;
            outCurGainR = targetGainR;
            outStepGainL = 0.0f;
            outStepGainR = 0.0f;
            return false;
        }

        const float invSamples = 1.0f / static_cast<float>(numSamples);
        outStepGainL = (targetGainL - m_smoothedL) * invSamples;
        outStepGainR = (targetGainR - m_smoothedR) * invSamples;
        outCurGainL = m_smoothedL;
        outCurGainR = m_smoothedR;
        m_smoothedL = targetGainL;
        m_smoothedR = targetGainR;
        return true;
    }

    void SnapTo(float targetGainL, float targetGainR) noexcept
    {
        m_smoothedL = targetGainL;
        m_smoothedR = targetGainR;
    }

private:
    float m_smoothedL = -1.0f;
    float m_smoothedR = -1.0f;
};

} // namespace AudioDSP
} // namespace Igloo

#endif // IGLOO_AUDIO_DSP_H
