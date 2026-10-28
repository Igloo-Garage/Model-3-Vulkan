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
 * Model3Timing.h
 *
 * The one place the Model 3 video rate is written down.
 *
 * The arcade hardware scans 424 lines at 57.524160 Hz. Every clock in the emulator
 * that advances by one emulated video frame - the PowerPC cycle budget, the
 * drive-board Z80, the SCSP sample count, the DSB frame period and now the
 * presentation pacer - has to agree on that figure. Before this header each of
 * them carried its own literal, and two of them disagreed with the arcade rate
 * (the audio path followed the presentation rate, and DSB1 assumed 60 Hz).
 *
 * kModel3StepNs is derived from the arcade rate directly: 1e9 / 57.524160 is
 * 17,384,000.04 ns, so the integer below is 2.6 ppb off rather than 2.76 ppm.
 * That difference used to be dismissed as unobservable, and on its own it was:
 * but every derived figure takes it along - the pacer targets, the VRR
 * multiples (2x = 115.048320 Hz, 8x = 460.193280 Hz) and the audio sample
 * accumulator - so the whole system ran that much slow against the wall clock,
 * and a long session drained the audio ring by the same amount. One number,
 * no residual, is simpler than a documented exception.
 *
 * Anything that paces by a frame period should be worked out as 1e9 / Hz rather
 * than by rounding the rate to whole millihertz: 57.52416 Hz rounded to 57,524
 * mHz is 17,384,048 ns, which reintroduces the same error in the other place.
 * See GetDesiredFramePeriodNs() in Main.cpp.
 *
 * Nothing in this file may depend on SDL, Vulkan or the platform layer: it is
 * included by the emulation core, the OSD and the renderers.
 */

#ifndef INCLUDED_MODEL3_TIMING_H
#define INCLUDED_MODEL3_TIMING_H

#include <cstdint>

// The arcade's vertical rate. This is the rate the game logic advances at, and
// the rate the 57/58 Hz exclusive display modes are derived from.
constexpr double kModel3RefreshHz = 57.524160;

// One emulated video frame in nanoseconds: 1e9 / 57.524160, truncated.
constexpr int64_t kModel3StepNs = 17384000;

// The same step expressed as a rate, i.e. the effective logic clock when the
// fixed-step accumulator is driven by kModel3StepNs.
constexpr double kModel3StepHz = 1.0e9 / (double)kModel3StepNs;

// 44.1 KHz samples produced by one emulated frame. 766.63 at the arcade rate,
// so callers carry the fraction rather than rounding (see GetSamplesThisFrame).
constexpr double kModel3SamplesPerStep = 44100.0 / kModel3StepHz;

// Backlog limits for the fixed-step accumulator. Anything beyond three steps of
// debt is discarded rather than replayed: a stall (window drag, shader compile,
// a save state load) must not turn into a burst of catch-up frames.
constexpr int kMaxBacklogSteps = 3;
constexpr int64_t kMaxBacklogNs = kModel3StepNs * kMaxBacklogSteps;

#endif // INCLUDED_MODEL3_TIMING_H
