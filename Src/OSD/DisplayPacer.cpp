/**
 ** Supermodel-VK
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright 2026 The Supermodel-VK authors
 **
 ** This file is part of Supermodel-VK, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
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

#include "DisplayPacer.h"

#include "OSD/Logger.h"
#include "OSD/Platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
constexpr double kUnknownPanelHz = 60.0;
constexpr uint64_t kRateWindowNs = 1000000000ull;
}

void CDisplayPacer::Configure(const DisplayPacerConfig& config, uint64_t nowNs)
{
    m_mode = config.mode;
    m_fixedHz = config.fixedHz;
    m_panelHz = config.panelHz;
    m_vsync = config.vsync;
    m_throttle = config.throttle;
    m_interpolation = config.interpolation;
    // A new mode or panel means the old vblank phase says nothing about the new one.
    m_presentLeadNs = 0;
    RecomputePeriod();

    InfoLog("Pace: mode %s, logic %.5f Hz, target %.3f Hz, panel %.3f Hz, vsync %d, interpolation %d.",
        RefreshRateModeName(m_mode), kModel3StepHz, TargetPresentHz(), m_panelHz, m_vsync ? 1 : 0,
        InterpolationEnabled() ? 1 : 0);

    if (m_phaseCount > 0)
    {
        // The weights this mode will cycle through, in slot order. Constants, so
        // printing them once describes every frame of the session.
        char weights[128];
        std::size_t used = 0;
        weights[0] = '\0';
        for (int i = 0; i < m_phaseCount && used + 8 < sizeof(weights); i++)
            used += (std::size_t)std::snprintf(weights + used, sizeof(weights) - used,
                "%s%.3f", i ? " " : "", (double)m_phaseAlpha[i]);
        InfoLog("Pace: %d presents per emulated frame, slot weights [%s]; slot 0 runs the simulation.",
            m_phaseCount, weights);
    }

    Reset(nowNs);
}

void CDisplayPacer::Reset(uint64_t nowNs)
{
    // Start with one step already due, so the first loop iteration builds and
    // presents a scene instead of showing an empty frame for one period.
    m_accumulator = kModel3StepNs;
    m_lastTick = nowNs;
    m_nextPresent = nowNs;
    m_lastAlpha = 1.0;
    m_logicWindowStart = nowNs;
    m_logicWindowSteps = 0;
    m_presentWindowStart = nowNs;
    m_presentWindowCount = 0;
    m_lastPresent = 0;

    // Phase stepper. Slot 0 runs the simulation, and the very first present
    // shows the current frame rather than interpolating against a snapshot that
    // does not exist yet.
    m_phase = 0;
    m_firstSlot = true;
    m_cutPending = false;
    m_anchorNs = nowNs;
    m_slotIndex = 0;
}

void CDisplayPacer::RecomputePeriod()
{
    m_phaseCount = PhaseCountForMode(m_mode);

    // The phase weights are constants, not measurements: phase k shows the pair
    // of snapshots at k/N, which is the only assignment that advances the
    // picture by exactly 1/N of a frame per present. Assigning anything else
    // (1.0 on the simulation slot, say) makes the picture jump forward and then
    // back by half a frame on every step.
    for (int i = 0; i < 8; i++)
        m_phaseAlpha[i] = (m_phaseCount > 0 && i < m_phaseCount)
            ? (float)i / (float)m_phaseCount : 1.0f;

    const double hz = TargetPresentHz();
    m_periodNs = hz > 1.0 ? (uint64_t)std::llround(1.0e9 / hz) : 0;
}

float CDisplayPacer::PhaseAlphaAt(int phase) const
{
    if (m_phaseCount <= 0)
        return 1.0f;
    if (phase < 0 || phase >= m_phaseCount)
        return 1.0f;
    return m_phaseAlpha[phase];
}

void CDisplayPacer::PhaseAlphas(float* out, int maxCount) const
{
    for (int i = 0; i < maxCount; i++)
        out[i] = PhaseAlphaAt(i);
}

void CDisplayPacer::SetPanelHz(double hz, uint64_t nowNs)
{
    if (hz <= 1.0 || std::fabs(hz - m_panelHz) < 0.01)
        return;
    m_panelHz = hz;
    RecomputePeriod();
    m_nextPresent = nowNs;
    InfoLog("Pace: panel refresh changed to %.3f Hz; target present %.3f Hz.",
        m_panelHz, TargetPresentHz());
}

double CDisplayPacer::TargetPresentHz() const
{
    switch (m_mode)
    {
    case RefreshRateMode::Original:
        return kModel3StepHz;
    case RefreshRateMode::Fixed:
        return m_fixedHz > 1.0 ? m_fixedHz : kUnknownPanelHz;
    case RefreshRateMode::Vrr2x:
        return kModel3StepHz * 2.0;
    case RefreshRateMode::Vrr3x:
        return kModel3StepHz * 3.0;
    case RefreshRateMode::Vrr4x:
        return kModel3StepHz * 4.0;
    case RefreshRateMode::Vrr8x:
        return kModel3StepHz * 8.0;
    case RefreshRateMode::Auto:
    default:
        return m_panelHz > 1.0 ? m_panelHz : kUnknownPanelHz;
    }
}

bool CDisplayPacer::InterpolationEnabled() const
{
    return m_interpolation && m_mode != RefreshRateMode::Original && !m_paused;
}

double CDisplayPacer::Ratio() const
{
    const double logic = m_measuredLogicHz > 0.0 ? m_measuredLogicHz : kModel3StepHz;
    const double present = m_measuredPresentHz > 0.0 ? m_measuredPresentHz : TargetPresentHz();
    return logic > 0.0 ? present / logic : 0.0;
}

CDisplayPacer::TickResult CDisplayPacer::Tick(uint64_t nowNs)
{
    TickResult result;
    if (m_paused)
    {
        result.alpha = m_lastAlpha;
        return result;
    }

    /*
     * Phase-locked mode: the slot decides, not the clock.
     *
     * No accumulator is consulted and no alpha is measured, so a wake-up that
     * lands 200 us late shifts the present, not the weight. The simulation runs
     * on phase 0; every other phase presents the snapshot pair at a fixed
     * weight.
     */
    if (m_phaseCount > 0)
    {
        result.step = (m_phase == 0);
        result.alpha = AlphaAt(nowNs);
        m_lastAlpha = result.alpha;
        if (result.step)
        {
            m_simSteps++;
            m_logicWindowSteps++;
        }
        UpdateMeasured(nowNs);
        return result;
    }

    if (m_lastTick == 0)
        m_lastTick = nowNs;

    // A monotonic clock cannot go backwards, but a caller may still pass a
    // stale sample. Ignoring it is safer than subtracting unsigned values.
    const uint64_t elapsed = nowNs > m_lastTick ? nowNs - m_lastTick : 0;
    m_lastTick = nowNs;
    m_accumulator += (int64_t)elapsed;

    if (m_accumulator > kMaxBacklogNs)
    {
        const int64_t excessSteps = (m_accumulator / kModel3StepNs) - 1;
        if (excessSteps > 0)
            m_droppedSteps += (uint64_t)excessSteps;
        m_accumulator = kModel3StepNs;
        result.dropped = true;
    }

    // Exactly one step per tick. A loop that has fallen behind catches up one
    // frame at a time; it never replays three frames back to back, which is the
    // difference between recovering and looking like a fast-forward.
    if (m_accumulator >= kModel3StepNs)
    {
        m_accumulator -= kModel3StepNs;
        result.step = true;
        m_simSteps++;
        m_logicWindowSteps++;
    }

    result.alpha = InterpolationEnabled()
        ? std::clamp((double)m_accumulator / (double)kModel3StepNs, 0.0, 1.0)
        : 1.0;
    m_lastAlpha = result.alpha;

    UpdateMeasured(nowNs);
    return result;
}

double CDisplayPacer::AlphaAt(uint64_t nowNs) const
{
    if (m_paused)
        return m_lastAlpha;

    if (m_phaseCount > 0)
    {
        // Exact constants, except when there is nothing to interpolate against.
        if (m_firstSlot || m_cutPending || !InterpolationEnabled())
            return 1.0;
        return m_phaseAlpha[m_phase];
    }

    if (!InterpolationEnabled())
        return 1.0;

    int64_t value = m_accumulator;
    if (nowNs > m_lastTick)
        value += (int64_t)(nowNs - m_lastTick);
    value = std::clamp<int64_t>(value, 0, kModel3StepNs);
    return (double)value / (double)kModel3StepNs;
}

uint64_t CDisplayPacer::PresentTarget(uint64_t nowNs) const
{
    if (!m_throttle)
        return 0;

    if (m_phaseCount > 0)
    {
        // Absolute anchor plus k/N of a step, never an accumulated period: see the note on the
        // stall handling in WaitForPresent().
        return m_anchorNs +
            (uint64_t)std::llround((double)m_slotIndex * (double)kModel3StepNs / (double)m_phaseCount);
    }

    if (m_periodNs == 0)
        return 0;
    if (m_nextPresent == 0)
        return nowNs;
    // A stall has left the deadline in the past; WaitForPresent() rebases onto now rather than
    // presenting a burst to catch up, and this reports the rebased value so both agree.
    if (m_nextPresent + m_periodNs < nowNs)
        return nowNs;
    return m_nextPresent;
}

uint64_t CDisplayPacer::PresentDeadline(uint64_t nowNs) const
{
    const uint64_t nominal = PresentTarget(nowNs);
    if (nominal == 0 || m_presentLeadNs <= 0)
        return nominal;

    const uint64_t lead = (uint64_t)m_presentLeadNs;
    return nominal > lead ? nominal - lead : 0;
}

void CDisplayPacer::SetPresentLead(int64_t leadNs)
{
    if (leadNs < 0)
        leadNs = 0;
    const int64_t maxLead = (int64_t)PresentPeriodNs();
    if (maxLead > 0 && leadNs > maxLead)
        leadNs = maxLead;
    m_presentLeadNs = leadNs;
}

uint64_t CDisplayPacer::PresentPeriodNs() const
{
    if (m_phaseCount > 0)
        return (uint64_t)(kModel3StepNs / m_phaseCount);
    return m_periodNs;
}

void CDisplayPacer::WaitForPresent(uint64_t nowNs)
{
    if (m_phaseCount > 0)
    {
        if (!m_throttle || m_phaseCount <= 0)
            return;

        /*
         * Absolute anchor, rational slot offset rather than an accumulated
         * period: 17,384,000 / 3 is not a whole number, and rounding it once
         * per present would drift the phase against the simulation over hours.
         * Taking the offset from the anchor keeps slot k exactly k/N of a frame
         * after it, for every N.
         */
        const uint64_t target = PresentDeadline(nowNs);
        const uint64_t now = OSD::Platform::Now();

        if (now > target + kModel3StepNs)
        {
            // A stall longer than an emulated frame. Resynchronise onto a
            // simulation slot instead of replaying the phases we missed - one
            // step per iteration is the rule everywhere else in this class.
            const uint64_t late = now - target;
            m_droppedSteps += late / kModel3StepNs;
            m_anchorNs = now;
            m_slotIndex = 0;
            m_resync = true;
            return;
        }

        if (target > now)
            OSD::Platform::SleepUntil((OSD::Platform::Nanoseconds)target);
        return;
    }

    if (!m_throttle || m_periodNs == 0)
        return;

    uint64_t now = OSD::Platform::Now();
    if (m_nextPresent == 0)
        m_nextPresent = nowNs != 0 ? nowNs : now;

    // A stall (window drag, shader compile, swapchain recreation) leaves the
    // deadline far in the past. Rebase instead of presenting a burst to catch
    // up - the picture may skip ahead, but it never fast-forwards.
    if (m_nextPresent + m_periodNs < now)
        m_nextPresent = now;

    // m_nextPresent stays the nominal deadline; the lead only moves the instant we
    // wake, so it cannot accumulate into the period.
    const uint64_t nominal = m_nextPresent;
    const uint64_t target = PresentDeadline(nowNs);
    while (static_cast<uint64_t>(OSD::Platform::Now()) < target)
        OSD::Platform::SleepUntil((OSD::Platform::Nanoseconds)target);

    m_nextPresent = nominal + m_periodNs;

    // If the wake-up itself was late by more than a period, the next deadline
    // is already behind us. Rebase once, here, rather than carrying the debt.
    now = OSD::Platform::Now();
    if (m_nextPresent + m_periodNs < now)
        m_nextPresent = now + m_periodNs;
}

void CDisplayPacer::RecordPresent(uint64_t nowNs, double alpha)
{
    m_presents++;
    m_lastAlpha = alpha;

    // The phase advances with the present, not with the wait: a present that the
    // window could not service still consumed its slot, and advancing in
    // WaitForPresent would let alpha describe the next slot at render time.
    if (m_phaseCount > 0)
    {
        m_lastPhase = m_phase;
        m_firstSlot = false;
        if (m_resync)
        {
            m_resync = false;
            m_phase = 0;
            m_slotIndex = 1;
        }
        else
        {
            m_phase = (m_phase + 1) % m_phaseCount;
            m_slotIndex++;
        }
    }

    if (m_presentWindowStart == 0)
        m_presentWindowStart = nowNs;
    m_presentWindowCount++;

    const uint64_t elapsed = nowNs > m_presentWindowStart ? nowNs - m_presentWindowStart : 0;
    if (elapsed >= kRateWindowNs)
    {
        m_measuredPresentHz = (double)m_presentWindowCount * 1.0e9 / (double)elapsed;
        m_presentWindowStart = nowNs;
        m_presentWindowCount = 0;
    }

    m_lastPresent = nowNs;
}

void CDisplayPacer::SetPaused(bool paused, uint64_t nowNs)
{
    if (paused == m_paused)
        return;
    m_paused = paused;
    if (!paused)
        Reset(nowNs);
}

void CDisplayPacer::UpdateMeasured(uint64_t nowNs)
{
    if (m_logicWindowStart == 0)
        m_logicWindowStart = nowNs;

    const uint64_t elapsed = nowNs > m_logicWindowStart ? nowNs - m_logicWindowStart : 0;
    if (elapsed >= kRateWindowNs)
    {
        m_measuredLogicHz = (double)m_logicWindowSteps * 1.0e9 / (double)elapsed;
        m_logicWindowStart = nowNs;
        m_logicWindowSteps = 0;
    }
}
