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
#ifndef _STATS_OSD_H_
#define _STATS_OSD_H_

namespace OSD
{
	namespace Platform
	{
		struct Window;
	}
}

namespace Util
{
	namespace Config
	{
		class Node;
	}
}

/*
 * How the window is presenting, as resolved at run time rather than as configured.
 *
 * The two are not the same, and the difference is exactly what the readout's sync line has to get
 * right: a driver can refuse to enter exclusive fullscreen and the session continues as borderless,
 * where no variable refresh rate can engage. Reporting the configured intent there would tell the user
 * they have something they do not.
 */
enum class StatsSyncMode
{
	Windowed,             // not fullscreen at all: the compositor presents
	Borderless,           // borderless fullscreen: still composited, so VRR cannot engage
	ExclusiveVrr,         // exclusive fullscreen at the desktop mode, for G-SYNC/FreeSync to track
	ExclusiveFixedRate,   // exclusive fullscreen switched to a fixed 57/58 Hz display mode
};

/*
 * Real-time statistics readout, drawn in the top left of the running game.
 *
 * Activated and dismissed with the key above Tab (KEY_BACKQUOTE, the `~ key). Unlike the settings
 * panel this is not interactive: it is a readout, so it never takes the mouse and never has anything
 * to apply.
 *
 * What it reports, and where each figure comes from:
 *
 *   renderer        the configuration - which backend is actually running
 *   sync / rate     the fullscreen mode, the emulator's pacing rate and the arcade's own
 *   panel           the display mode the window is on, from the desktop mode
 *   CPU             model, average and peak frequency, thread count and load, over the performance
 *                   cores on a hybrid CPU
 *   GPU             model, frequency, load and video memory: NVML where an NVIDIA driver is present,
 *                   otherwise the load alone from the Windows "GPU Engine" counters
 *   memory          this process's working set
 *   FPS              measured from the emulator's own frame loop, not from the display
 *   1% low          the 99th percentile frame time over the last few seconds, inverted
 *
 * On a CPU with more than one kind of core - an Intel part with performance and efficiency cores, or a
 * third tier of low-power ones - the CPU line describes the performance cores rather than the whole
 * package, and says how many of each there are. Counting and averaging every logical processor
 * describes a machine nobody owns: too many threads, a clock the slow cores drag down, and a load
 * diluted by cores the emulator never runs on.
 *
 * The GPU figures need a vendor API: NVML ships with the NVIDIA driver. Where it is missing, the
 * Windows GPU Engine counters still provide the load - the one figure that says whether the GPU is
 * the limit - but not the clock or the video memory, and Windows offers no vendor-neutral source for
 * either. Whatever cannot be read is left out rather than shown as zero, which would read as "the GPU
 * is idle" instead of "this build cannot see it".
 *
 * Both frequency figures are printed for the CPU because they answer different questions: the average
 * describes the package, and the peak describes the core the emulation thread is actually on. On a
 * 15 W laptop the two differ by a factor of two, and showing only the average invites the conclusion
 * that the CPU refuses to boost when it is in fact boosting on the one core that matters.
 *
 * The labels are deliberately English and untranslated. CPU, GPU, VRAM, FPS and 1% low are the terms
 * these figures are looked up by, and they are the same words in every language the settings window
 * speaks - so a translation table would add a failure mode (a format string with its specifiers
 * reordered or dropped) in exchange for nothing.
 *
 * Drawn through the same ImGui context and Vulkan backend as the settings panel (see Overlay.h), so
 * it shares that panel's one structural limit: the OpenGL backends expose no swapchain to draw into,
 * and the readout is therefore Vulkan-only.
 */

/*
 * The window the readout's display figures are taken from.
 *
 * Set once when a game session starts. Only the refresh rate line needs it - everything else is either
 * a process statistic or a driver query - but the display mode cannot be read without a window to ask.
 */
void StatsOsdSetWindow(OSD::Platform::Window* window);

/*
 * Which renderer this session actually got.
 *
 * Passed in rather than read from the configuration, because the two disagree exactly when it
 * matters: asking for Vulkan on a driver that cannot provide it falls back to OpenGL for that session,
 * and a readout that repeated the configuration would name a backend that is not the one drawing the
 * picture. The caller is the only place that knows, so it says.
 */
enum class StatsRenderer
{
	Vulkan3D,
	New3D,
	Legacy3D,
};

void StatsOsdSetRenderer(StatsRenderer renderer);

/*
 * The name of the graphics device, for the GPU line.
 *
 * Passed in because the renderer is the only part of the program that knows it: the Vulkan context
 * has it from the physical-device properties. NVML supplies its own name when it is available, so
 * this is what puts a name on the line on machines where only the Windows GPU Engine counters are
 * readable - the ones where the GPU matters most and the readout used to be blank.
 *
 * Optional. An empty string leaves the line in its unnamed single-line form.
 */
void StatsOsdSetGpuName(const char* name);

/*
 * Anti-aliasing, and which kind.
 *
 * The two renderer families reach the same goal by different means, and the readout names the one
 * actually in use rather than the setting that asked for it:
 *
 *   Supersampling  the OpenGL backends multiply the render buffer and average it back down.
 *                  The factor comes from the Supersampling setting.
 *   Msaa           the Vulkan backend rasterises into a multisampled image and resolves it.
 *                  The level comes from the MSAA setting, and it is not always granted - a driver
 *                  can refuse the sample count, in which case 1 is what is really happening.
 *
 * The distinction matters to the reader because the two smooth different things: supersampling
 * averages the whole finished image, MSAA smooths polygon edges only. Two sessions with "4x" on
 * screen are not doing the same thing.
 */
enum class StatsAntiAliasing
{
	None,
	Supersampling,
	Msaa,
};

/*
 * What the renderer is really producing, for the Render line under Panel.
 *
 * Pushed in from where the renderers are initialised, not derived from the configuration, and that is
 * the whole point: the configuration says what was asked for. What actually happens can differ by a
 * factor of several - a supersampled OpenGL buffer is not the resolution the setting names, Vulkan
 * ignores the Supersampling setting entirely, MSAA can be refused, and in fullscreen the window is
 * sized to the display rather than to XResolution, so the render buffer is neither number.
 *
 *   width / height          the buffer the 3D scene is rasterised into
 *   outputWidth / outputHeight  the resolution the finished picture is presented at, which differs
 *                           from the above only under supersampling
 *   samples                 1 when there is no anti-aliasing
 */
struct StatsRenderInfo
{
	unsigned width = 0;
	unsigned height = 0;
	unsigned outputWidth = 0;
	unsigned outputHeight = 0;
	int samples = 1;
	StatsAntiAliasing method = StatsAntiAliasing::None;
};

void StatsOsdSetRenderInfo(const StatsRenderInfo& info);

/*
 * The rate the emulator is pacing itself to, pushed in rather than read from the configuration.
 *
 * Needed by the borderless cadence check below, which has to work even when the readout is switched
 * off - the person who most needs to know why borderless fullscreen stutters is not necessarily the
 * person who has a readout on screen. Read on every frame because it is one float and the alternative
 * is a stale value after the setting is changed.
 */
void StatsOsdSetTargetRate(float hz);

/*
 * Fixed-step pacing, for the Pace line.
 *
 * The emulator advances at 57.524 Hz regardless of what the display does; the
 * pacer decides how many presents fall between two simulation steps and hands
 * out the alpha weight that the renderer interpolates by. This line exists so
 * that the difference between the two clocks is visible: logic near 57.52 and
 * present near the panel rate mean the pipeline works; a present rate that has
 * collapsed onto the logic rate means the display is the limit (vsync, or a
 * compositor), and the ratio explains what is on screen.
 *
 * `cuts` is cumulative for the session, not per second: a camera cut is an
 * event, and a counter that decays would read as "no cuts right now" even on a
 * scene that cuts every few seconds.
 */
struct StatsPaceInfo
{
	double logicHz = 0.0;
	double presentHz = 0.0;
	double targetHz = 0.0;
	double panelHz = 0.0;
	double alpha = 1.0;
	double ratio = 0.0;
	unsigned cuts = 0;
	bool interpolating = false;
	bool valid = false;

	/*
	 * Phase-locked presentation (VRR 2x and up).
	 *
	 * phaseCount is 0 in the clock-driven modes, where alpha is measured and
	 * moves continuously; a non-zero count means the presents per emulated frame
	 * are fixed and alpha takes one of a few exact values. The readout has to say
	 * which of the two it is looking at, because "alpha 0.50" means something
	 * quite different in each: a locked value is the design, a measured one is a
	 * snapshot of where the clock happened to be.
	 */
	const char* modeLabel = "";
	bool phaseLocked = false;
	int phaseIndex = 0;              // 0-based
	int phaseCount = 0;
	float phaseAlpha[8] = {};
	double subFrameMs = 0.0;         // cost of a present that carries no simulation
};

void StatsOsdSetPaceInfo(const StatsPaceInfo& info);

/*
 * Whether the readout would currently be showing a borderless judder warning.
 *
 * Asked by the caller so that the explanation reaches the settings window as well. The readout can
 * only show it to someone who has switched the readout on and is looking at the game; the person who
 * needs to know why the picture stutters in borderless fullscreen is often neither.
 *
 * False whenever there is nothing to warn about: not in borderless fullscreen, no panel mode to
 * compare against, or a rate that divides exactly.
 */
bool StatsOsdBorderlessJudderPending();

/*
 * The refresh rate the panel is configured at, or 0 when it is not known yet.
 *
 * Exposed for the one-line explanation logged alongside StatsOsdBorderlessJudderPending(), which names
 * the rate that does not divide evenly. The readout has the figure already; asking SDL for it a second
 * time in the caller would be a second source for the same number, and the two could disagree.
 */
int StatsOsdPanelRefreshHz();

/*
 * Tells the readout how the window ended up presenting.
 *
 * Called every frame rather than once, because the answer can change during a session: a fullscreen
 * toggle or a display configuration change can make the driver refuse the exclusive switch, and the
 * session then continues borderless. Only the caller knows this - see the note on StatsSyncMode.
 */
void StatsOsdSetSyncMode(StatsSyncMode mode);

/*
 * Records one emulated frame. Call once per iteration of the game loop, before the frame is run.
 *
 * Kept separate from the drawing so the numbers describe the emulator's real frame rate even while
 * the readout itself is off: toggling it on should not start the measurement, or the first seconds
 * would read as a spike.
 */
void StatsOsdFrameTick();

/*
 * Builds the readout for this frame. Call from inside an active ImGui frame, after ImGui::NewFrame().
 * Does nothing when the readout is off.
 *
 * The configuration is passed in rather than looked up, because this module has no business owning
 * one: the caller already has it, and it is what decides the renderer name the readout shows.
 */
void StatsOsdBuildWindow(const Util::Config::Node& config);

/*
 * Whether the readout should be drawn. The caller is responsible for having a working ImGui context;
 * see the note in Overlay.cpp about how the two are combined.
 */
bool StatsOsdEnabled();
void StatsOsdSetEnabled(bool enabled);
void StatsOsdToggle();

/*
 * Takes the initial state from the configuration. Called once when a game session starts, so the
 * readout survives a restart while the key remains the way to change it during play.
 */
void StatsOsdApplyConfig(bool enabled);

/*
 * True once after the state last changed, so the caller can write it back without doing so every
 * frame. Consuming it is what makes it one-shot.
 */
bool StatsOsdConsumeStateChanged();

/*
 * Releases what the stats collection holds open. Called when a game session ends.
 *
 * NVML keeps a library and a device handle alive for the life of the process, and the probe flag
 * would otherwise stop a later session from setting them up again.
 */
void StatsOsdShutdown();

#endif // !_STATS_OSD_H_
