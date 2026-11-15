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

#ifndef IGLOO_INPUT_FILTER_H
#define IGLOO_INPUT_FILTER_H

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace Igloo {
namespace Input {

struct SteeringSmoothingPreset
{
    double tau;      // first-order time constant, seconds
    double maxStep;  // maximum normalized change per 57.524 Hz frame
    double gamma;    // response curve exponent
};

// Index 0 is unused: off bypasses the filter entirely.
inline constexpr SteeringSmoothingPreset kSteeringSmoothingPresets[4] = {
    { 0.000, 1.000, 1.00 },
    { 0.055, 0.090, 1.45 },  // 1: Light  (~200 ms full lock)
    { 0.095, 0.055, 1.95 },  // 2: Medium (~320 ms full lock, fine centre)
    { 0.155, 0.032, 2.50 },  // 3: Strong (~540 ms full lock, ultra-fine centre)
};

inline const SteeringSmoothingPreset& GetSteeringSmoothingPreset(int preset) noexcept
{
    const int idx = std::clamp(preset, 0, 3);
    return kSteeringSmoothingPresets[idx];
}

/*
 * Heuristic peripheral classifier that identifies dedicated physical racing wheels
 * (direct-drive wheelbases, belt/gear FFB wheels, raw non-gamepad wheel axes) so
 * gamepad thumbstick damping and sensitivity scaling automatically bypass them for
 * 1:1 direct steering response.
 */
inline bool IsRacingWheelDeviceName(std::string_view name, bool isGamepad, int numAxes) noexcept
{
    if (!isGamepad && numAxes > 0)
        return true;

    std::string lowerName(name);
    for (char& ch : lowerName)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    return lowerName.find("wheel") != std::string::npos ||
           lowerName.find("racing") != std::string::npos ||
           lowerName.find("driving force") != std::string::npos ||
           lowerName.find("simagic") != std::string::npos ||
           lowerName.find("alpha") != std::string::npos ||
           lowerName.find("moza") != std::string::npos ||
           lowerName.find("fanatec") != std::string::npos ||
           lowerName.find("csl") != std::string::npos ||
           lowerName.find("clubsport") != std::string::npos ||
           lowerName.find("podium") != std::string::npos ||
           lowerName.find("simucube") != std::string::npos ||
           lowerName.find("cammus") != std::string::npos ||
           lowerName.find("asetek") != std::string::npos ||
           lowerName.find("g25") != std::string::npos ||
           lowerName.find("g27") != std::string::npos ||
           lowerName.find("g29") != std::string::npos ||
           lowerName.find("g920") != std::string::npos ||
           lowerName.find("g923") != std::string::npos ||
           lowerName.find("t150") != std::string::npos ||
           lowerName.find("t248") != std::string::npos ||
           lowerName.find("t300") != std::string::npos ||
           lowerName.find("t500") != std::string::npos ||
           lowerName.find("t818") != std::string::npos ||
           lowerName.find("t-gt") != std::string::npos ||
           lowerName.find("ts-pc") != std::string::npos ||
           lowerName.find("ts-xw") != std::string::npos ||
           lowerName.find("thrustmaster") != std::string::npos;
}

/*
 * GamepadSteeringFilter
 *
 * Non-linear center-precision power curve + sensitivity scaler + first-order exponential
 * low-pass filter with per-frame slew-rate limiting for gamepad thumbstick steering.
 */
class GamepadSteeringFilter
{
public:
    void Reset() noexcept
    {
        m_valid = false;
        m_filtered = 0.0;
    }

    // Returns true if the preset changed (and state was reset).
    bool SetPreset(int preset) noexcept
    {
        preset = std::clamp(preset, 0, 3);
        if (preset == m_preset)
            return false;
        m_preset = preset;
        Reset();
        return true;
    }

    // Returns true if the sensitivity changed.
    bool SetSensitivityPercent(int percent) noexcept
    {
        percent = std::clamp(percent, 25, 100);
        if (percent == m_sensitivityPercent)
            return false;
        m_sensitivityPercent = percent;
        return true;
    }

    int Preset() const noexcept { return m_preset; }
    int SensitivityPercent() const noexcept { return m_sensitivityPercent; }
    double FilteredValue() const noexcept { return m_filtered; }

    double Process(double targetSignedFraction, double stepHz = 57.524160, double* outCurved = nullptr) noexcept
    {
        const SteeringSmoothingPreset& preset = GetSteeringSmoothingPreset(m_preset);
        const double sensScale = static_cast<double>(m_sensitivityPercent) / 100.0;
        const double curved = std::copysign(std::pow(std::fabs(targetSignedFraction), preset.gamma), targetSignedFraction) * sensScale;
        if (outCurved != nullptr)
            *outCurved = curved;

        if (!m_valid)
        {
            m_filtered = curved;
            m_valid = true;
        }
        else
        {
            const double dt = 1.0 / stepHz;
            const double follow = dt / (preset.tau + dt);
            double step = (curved - m_filtered) * follow;
            step = std::clamp(step, -preset.maxStep, preset.maxStep);
            // Never overshoot the target when the remaining error is smaller than the rate cap.
            if (std::fabs(step) > std::fabs(curved - m_filtered))
                step = curved - m_filtered;
            m_filtered += step;
        }
        return m_filtered;
    }

private:
    int    m_preset = 0;
    int    m_sensitivityPercent = 100;
    bool   m_valid = false;
    double m_filtered = 0.0;
};

} // namespace Input
} // namespace Igloo

#endif // IGLOO_INPUT_FILTER_H
