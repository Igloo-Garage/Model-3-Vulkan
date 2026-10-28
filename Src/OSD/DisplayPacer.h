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

/*
 * DisplayPacer.h
 *
 * Decouples emulated time from presentation time.
 *
 * The emulator advances in fixed 17,384,000 ns steps (57.524160 Hz). A monitor does
 * not, and neither does a timer: 60, 120, 144, 165, 240 and 480 Hz are all
 * different rates, and none of them divides the arcade rate. Driving one
 * emulated frame per present therefore forces either the game or the display
 * onto the wrong cadence, which is the judder this class exists to remove.
 *
 * The pacer keeps two clocks:
 *
 *   logic     a nanosecond accumulator advanced by the monotonic clock. A step
 *             is due when it reaches kModel3StepNs. At most one step is
 *             released per call, so a stall cannot turn into a burst.
 *   present   an absolute deadline advanced by exactly one target period.
 *             Present calls may return late (FIFO vsync, a busy GPU); the
 *             deadline is rebased rather than accumulating "debt".
 *
 * The fraction of a step left in the accumulator is the interpolation weight
 * alpha: 0 means the previous simulation state, 1 the current one. See
 * CR3DSceneBuilder for the two scene snapshots alpha selects between.
 *
 * Phase-locked modes
 * ------------------
 * A display with variable refresh can be driven at an exact multiple of the
 * arcade rate: 2x is 115.04832 Hz, 4x 230.09664 Hz, 8x 460.19328 Hz. At a whole
 * multiple the cadence is even by construction - every emulated frame is shown
 * the same number of times - which is the one thing no amount of timer
 * precision can give at 120 or 144 Hz.
 *
 * In those modes alpha is not measured, it is the slot number: one emulated
 * frame spans N presents, the slot that runs the simulation is phase 0, and
 * phase k presents the pair of snapshots at weight k/N. That makes the weights
 * exact constants (0.5, 0.25, 0.125 ...), immune to the few hundred
 * microseconds of wake-up jitter that would otherwise show up as shimmer: at
 * 115 Hz a 200 us error is 2.3% of a slot, and at 460 Hz it is 9%.
 *
 * The consequence, and it is unavoidable: N presents per step with a constant
 * weight means the picture is always between two known frames, so it runs one
 * emulated frame (17.4 ms) behind the simulation. The zero-latency alternative
 * is the same mode with interpolation switched off, where every present uses
 * alpha 1.0 and each emulated frame is simply shown N times.
 *
 * The timing discipline is modelled on namco22-decompile's FramePacer (absolute
 * deadlines, coarse sleep plus spin, no catch-up bursts, present timestamp
 * feedback). That project does not interpolate; the sub-frame part is new here.
 */

#ifndef INCLUDED_DISPLAY_PACER_H
#define INCLUDED_DISPLAY_PACER_H

#include "Model3/Model3Timing.h"

#include <cstdint>

enum class RefreshRateMode
{
    Original = 0,   // present at the arcade rate (57.524 Hz); no interpolation (VRR 1x)
    Auto = 1,       // follow the display's current refresh rate; interpolate
    Fixed = 2,      // use FixedHz; interpolate
    // Exact multiples of the arcade rate, driven by a deterministic phase stepper
    // rather than by the clock. See the class comment: the cadence is what these
    // exist for, and a whole-number multiple is the only way to get both an even
    // scan and unique pictures without trusting a sleep.
    Vrr2x = 3,      // 115.04832 Hz
    Vrr3x = 4,      // 172.57248 Hz
    Vrr4x = 5,      // 230.09664 Hz
    Vrr8x = 6,      // 460.19328 Hz
};

// Number of presents per emulated frame for a mode: 0 for the clock-driven modes.
inline int PhaseCountForMode(RefreshRateMode mode)
{
    switch (mode)
    {
    case RefreshRateMode::Vrr2x: return 2;
    case RefreshRateMode::Vrr3x: return 3;
    case RefreshRateMode::Vrr4x: return 4;
    case RefreshRateMode::Vrr8x: return 8;
    default: return 0;
    }
}

/*
 * Whether a display running at panelHz can present `phases` times per emulated
 * frame.
 *
 * Two conditions. The multiple has to fit under the panel's own ceiling with 2%
 * to spare - at 115 Hz that is a little under half a millisecond, more than the
 * pacer's wake-up error - and 2x additionally requires a 120 Hz panel, because
 * "just barely faster than the arcade rate" is not a mode worth offering.
 *
 * Shared with the settings window so that the option it greys out and the option
 * the game loop refuses are decided by the same arithmetic.
 */
inline bool PanelSupportsPhaseCount(int phases, double panelHz)
{
    if (phases <= 1 || panelHz <= 1.0)
        return false;
    if (phases == 2 && panelHz < 120.0)
        return false;
    return kModel3StepHz * phases <= panelHz * 0.98;
}

/*
 * The fixed present rates the settings window offers, and the rule that decides
 * which of them a display can actually take.
 *
 * Shared rather than written twice: the combo greys out exactly what the game
 * loop clamps away, so a value written on one machine and carried to a slower
 * panel cannot silently present at a rate the panel never scans. maxSupportedHz
 * is the highest whole rate any mode of the display reports; 0 means the mode
 * list could not be read, in which case nothing is refused.
 */
inline constexpr int kFixedRefreshRates[] = { 60, 75, 90, 120, 144, 165, 240, 480 };
inline constexpr int kFixedRefreshRateCount =
    (int)(sizeof(kFixedRefreshRates) / sizeof(kFixedRefreshRates[0]));

inline bool DisplaySupportsFixedRate(int rateHz, int maxSupportedHz)
{
    return maxSupportedHz <= 0 || rateHz <= maxSupportedHz;
}

// The fastest offered rate that fits, or 60 when the display reports less than
// that (a projector or a hand-edited mode list).
inline int HighestSupportedFixedRate(int maxSupportedHz)
{
    int best = kFixedRefreshRates[0];
    for (int i = 1; i < kFixedRefreshRateCount; i++)
    {
        if (kFixedRefreshRates[i] <= maxSupportedHz && kFixedRefreshRates[i] > best)
            best = kFixedRefreshRates[i];
    }
    return best;
}

inline int ClampFixedRefreshRate(int requestedHz, int maxSupportedHz)
{
    if (DisplaySupportsFixedRate(requestedHz, maxSupportedHz))
        return requestedHz;
    return HighestSupportedFixedRate(maxSupportedHz);
}

inline const char* RefreshRateModeName(RefreshRateMode mode)
{
    switch (mode)
    {
    case RefreshRateMode::Original: return "vrr-1x";
    case RefreshRateMode::Fixed:    return "fixed";
    case RefreshRateMode::Vrr2x:    return "vrr-2x";
    case RefreshRateMode::Vrr3x:    return "vrr-3x";
    case RefreshRateMode::Vrr4x:    return "vrr-4x";
    case RefreshRateMode::Vrr8x:    return "vrr-8x";
    case RefreshRateMode::Auto:
    default:                        return "auto";
    }
}

struct DisplayPacerConfig
{
    RefreshRateMode mode = RefreshRateMode::Auto;
    double fixedHz = 120.0;
    double panelHz = 0.0;       // 0 = unknown; Auto falls back to 60
    bool vsync = true;
    bool throttle = true;
    bool interpolation = true;
};

class CDisplayPacer
{
public:
    struct TickResult
    {
        bool step = false;      // run one emulated frame before presenting
        bool dropped = false;   // the backlog clamp discarded time
        double alpha = 1.0;     // interpolation weight for this present
    };

    void Configure(const DisplayPacerConfig& config, uint64_t nowNs);
    void Reset(uint64_t nowNs);

    // Updates the detected panel rate without discarding the accumulator. Used
    // when a fullscreen toggle changes the display mode mid-session.
    void SetPanelHz(double hz, uint64_t nowNs);

    /*
     * Advances the wall clock and decides whether one emulated frame is due.
     * Call once per iteration of the game loop, before rendering.
     */
    TickResult Tick(uint64_t nowNs);

    /*
     * Blocks until the next presentation deadline. Call before rendering, then
     * use AlphaAt(Now()) for the weight, so the sleep is included rather than
     * using a value sampled at the top of the loop.
     */
    void WaitForPresent(uint64_t nowNs);

    /*
     * The absolute time WaitForPresent() is aiming at, or 0 when the pacer is
     * not throttling. Includes the present lead below.
     */
    uint64_t PresentDeadline(uint64_t nowNs) const;

    /*
     * How far before the nominal deadline to aim.
     *
     * A present only reaches the display at a vblank, so a frame submitted just
     * *after* one waits a whole period, while the same frame submitted a
     * millisecond earlier is shown at that vblank. The deadline the pacer aims at
     * has no fixed relationship to that grid, so which case a session lands in is
     * luck. The platform layer measures the gap with VK_KHR_present_wait and sets
     * this once, rather than converging on it frame by frame.
     *
     * Clamped to one period: aiming more than that earlier would move the picture
     * two vblanks ahead instead of one, which gains nothing.
     */
    void SetPresentLead(int64_t leadNs);
    int64_t PresentLead() const { return m_presentLeadNs; }
    // Time between two presents (kModel3StepNs / phases in the locked modes).
    uint64_t PresentPeriodNs() const;

    // Call after a successful present, for the measured present rate. `alpha` is
    // the weight the frame was actually drawn with, kept so that the readout
    // describes the picture on screen rather than the one the clock predicted.
    void RecordPresent(uint64_t nowNs, double alpha);

    /*
     * Recomputes alpha for a later instant without advancing the accumulator.
     * Clamped to [0,1]; a missed boundary presents the current state until the
     * next Tick releases the step.
     */
    double AlphaAt(uint64_t nowNs) const;

    // Pausing holds the last presented alpha instead of letting the accumulator
    // run on. Resume rebases both clocks to now, so a long pause cannot leave a
    // burst of simulation steps behind it.
    void SetPaused(bool paused, uint64_t nowNs);

    double TargetPresentHz() const;
    RefreshRateMode Mode() const { return m_mode; }
    double PanelHz() const { return m_panelHz; }
    double FixedHz() const { return m_fixedHz; }
    double MeasuredPresentHz() const { return m_measuredPresentHz; }
    double MeasuredLogicHz() const { return m_measuredLogicHz; }
    double LastAlpha() const { return m_lastAlpha; }
    bool InterpolationEnabled() const;
    bool IsPaused() const { return m_paused; }

    /*
     * Phase-locked presentation (VRR 2x and up).
     *
     * A phase-locked pacer does not derive alpha from the clock at all: the
     * number of presents per emulated frame is an integer, so the slot number is
     * the phase, and the weight is phase / count. That is the difference between
     * "alpha hovers around 0.50" and "alpha is 0.50" - a thread wake-up jitter
     * of 200 us is 2.3% of a 8.7 ms slot, which is visible as shimmer on a slow
     * pan, and no amount of timer precision removes it.
     */
    bool IsPhaseLocked() const { return m_phaseCount > 0; }
    int PhaseCount() const { return m_phaseCount; }
    // The slot the last present used, so that the phase and the alpha the readout
    // shows describe the same frame.
    int PhaseIndex() const { return m_lastPhase; }
    float PhaseAlphaAt(int phase) const;
    void PhaseAlphas(float* out, int maxCount) const;

    /*
     * Records whether the step that just completed contained a camera cut.
     *
     * Every present of such a step uses alpha 1.0: the two snapshots describe
     * different scenes, so anything between them is a blend of the old and new
     * camera - the ghosting the cut detector exists to prevent. Call once per
     * emulated frame, after the scene snapshot was built, with false when there
     * was no cut, so the flag cannot stick.
     */
    void NoteFrameCut(bool cut) { m_cutPending = cut; }

    uint64_t SimSteps() const { return m_simSteps; }
    uint64_t Presents() const { return m_presents; }
    uint64_t DroppedSteps() const { return m_droppedSteps; }
    double Ratio() const;

private:
    void RecomputePeriod();
    void UpdateMeasured(uint64_t nowNs);
    void AdvancePhase(uint64_t nowNs);
    /*
     * The absolute time WaitForPresent() is aiming at before the present lead is
     * subtracted. Kept separate so the lead is applied in exactly one place.
     */
    uint64_t PresentTarget(uint64_t nowNs) const;

    RefreshRateMode m_mode = RefreshRateMode::Auto;
    double m_fixedHz = 120.0;
    double m_panelHz = 0.0;
    bool m_vsync = true;
    bool m_throttle = true;
    bool m_interpolation = true;
    bool m_paused = false;

    int64_t m_accumulator = 0;
    uint64_t m_lastTick = 0;
    uint64_t m_nextPresent = 0;
    uint64_t m_periodNs = 0;
    int64_t m_presentLeadNs = 0;
    double m_lastAlpha = 1.0;

    // Phase stepper. m_phaseCount is 0 in the clock-driven modes, which is what
    // IsPhaseLocked() reports.
    int     m_phaseCount = 0;
    int     m_phase = 0;              // 0 = the slot that runs the simulation
    int     m_lastPhase = 0;          // the slot the last present used
    float   m_phaseAlpha[8] = {};
    bool    m_cutPending = false;
    bool    m_firstSlot = false;
    bool    m_resync = false;
    uint64_t m_anchorNs = 0;          // wall clock of slot 0
    uint64_t m_slotIndex = 0;         // how many slots were presented since the anchor

    uint64_t m_simSteps = 0;
    uint64_t m_presents = 0;
    uint64_t m_droppedSteps = 0;

    uint64_t m_logicWindowStart = 0;
    uint32_t m_logicWindowSteps = 0;
    double m_measuredLogicHz = 0.0;

    uint64_t m_presentWindowStart = 0;
    uint32_t m_presentWindowCount = 0;
    double m_measuredPresentHz = 0.0;

    uint64_t m_lastPresent = 0;
};

#endif // INCLUDED_DISPLAY_PACER_H
