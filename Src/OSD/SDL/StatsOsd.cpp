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
#include "StatsOsd.h"

#include "OSD/Audio.h"
#include "OSD/Logger.h"
#include "OSD/Platform/Platform.h"
#include "Util/NewConfig.h"

#include "../Pkgs/imgui/imgui.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace
{

bool g_enabled = false;
bool g_stateChanged = false;
bool g_placedLogged = false;
OSD::Platform::Window* g_window = nullptr;
StatsSyncMode g_syncMode = StatsSyncMode::Windowed;
StatsRenderer g_renderer = StatsRenderer::New3D;
float g_targetHz = 0.0f;

// Pushed in by the game loop every frame; zero-valued until a paced session reports.
StatsPaceInfo g_pace;

// Empty until the renderers report: the readout is drawn by the OpenGL backend whatever the game
// uses, so it can be on screen before a session has started and the figures are known. A zero width
// means "not reported" and the line is left out, rather than printing 0x0.
StatsRenderInfo g_renderInfo;

/*
 * Frame timing.
 *
 * A ring of recent frame intervals rather than a running mean, because the figure that matters here is
 * the *worst* of them: an emulator that averages 60 while stuttering has a smooth mean and a visibly
 * uneven picture. The 1% low is the standard way to say that in one number, and it needs the
 * individual samples.
 *
 * 600 frames is about ten seconds at the emulator's 60 Hz - long enough for a hitch to still be
 * counted, short enough that the figure recovers quickly once a scene changes.
 */
constexpr int kHistoryFrames = 600;
double g_frameMs[kHistoryFrames] = {};
int g_historyCount = 0;      // how many entries are valid
int g_historyNext = 0;       // where the next sample goes

/*
 * The frame rate Model 3 hardware actually runs at.
 *
 * The emulator's own Model3.cpp derives its per-frame cycle budget from the same figure at the same
 * precision, so this is the reference the Rate line measures the emulated rate against. Duplicated
 * rather than shared because the two live in different modules and the value is a hardware constant
 * that has not moved since 1996 - but if it ever does, both should be changed together.
 */
constexpr float kArcadeHz = 57.524160f;

uint64_t g_lastFrameTicks = 0;
uint64_t g_fpsWindowStart = 0;
int g_fpsWindowFrames = 0;
double g_fps = 0.0;
double g_fpsLow1 = 0.0;

/*
 * Slow-changing figures, refreshed a few times a second.
 *
 * CPU load is a delta between samples and the GPU is queried through a driver API; neither is
 * meaningful per frame, and doing them every frame would cost more than the readout is worth. 500 ms
 * is fast enough to watch a load change and slow enough to be free.
 */
constexpr uint64_t kSlowSampleMs = 500;
uint64_t g_lastSlowSample = 0;

struct CpuStats
{
	bool valid = false;
	unsigned currentMhz = 0;
	unsigned peakMhz = 0;
	unsigned maxMhz = 0;
	unsigned loadPercent = 0;

	/*
	 * The marketing name, from the registry, and empty where it could not be read.
	 *
	 * Shown above the figures for the same reason the GPU line carries its model: "2050 MHz" means
	 * something different on a 15 W four-core laptop than on a desktop part, and the reader should not
	 * have to remember which machine they are on.
	 */
	char name[96] = {};

	/*
	 * The core composition.
	 *
	 * performanceThreads is what the load and clock above describe; totalThreads is what the machine
	 * has. They differ on any hybrid part, and reporting only the total was actively misleading: a
	 * single "18 threads, 25%" line counts low-power cores that the emulator never gets scheduled on
	 * and averages their clocks into a figure that looks like a slow CPU.
	 */
	unsigned totalThreads = 0;
	unsigned performanceThreads = 0;
	unsigned efficiencyThreads = 0;
	bool hybrid = false;
};

struct GpuStats
{
	bool valid = false;
	unsigned clockMhz = 0;
	unsigned loadPercent = 0;
	double vramUsedGb = 0.0;
	double vramTotalGb = 0.0;
	char name[96] = {};
};

struct MemStats
{
	bool valid = false;
	double workingSetGb = 0.0;
	double systemUsedGb = 0.0;
	double systemTotalGb = 0.0;
};

/*
 * The output buffer, as a level rather than a rate.
 *
 * The under/over-run counters alone only say that something already went wrong; how much audio is
 * queued says how much margin is left. Both are read here because they answer different questions
 * during the same session, and the muted flag is the one that turns "no sound" into a one-glance
 * answer instead of a hunt through the log.
 */
struct AudioStats
{
	bool valid = false;
	bool muted = false;
	unsigned bufferMs = 0;
	unsigned bufferSizeMs = 0;
	unsigned channels = 0;
	unsigned underRuns = 0;
	unsigned overRuns = 0;
	unsigned fillMinMs = 0;   // fill range over the sampling window, not just the latest value
	unsigned fillMaxMs = 0;
	float peakLeft = 0.0f;    // peak since the previous sample, 1.0 = full scale (~32768)
	float peakRight = 0.0f;
};

/*
 * The display mode the window is actually in.
 *
 * Kept apart from the emulator's own RefreshRate, because the whole point of reporting it is to compare
 * the two: a target of 57.52 Hz against a display mode of 60 Hz is what G-SYNC/VRR looks like from
 * here, while a target of 57.52 Hz against a 57 Hz mode means the custom arcade timing is in effect.
 * SDL reports whole hertz for a mode, so the custom 57.5242 timing shows up as 57, and the fractional
 * part can only come from the emulator's own figure.
 */
struct DisplayStats
{
	bool valid = false;
	int refreshHz = 0;
	int width = 0;
	int height = 0;
};

CpuStats g_cpu;
GpuStats g_gpu;
MemStats g_mem;
AudioStats g_audio;
DisplayStats g_display;
bool g_gpuProbed = false;

/*
 * Fill levels of the last few samples, so the readout can show a range.
 *
 * The instantaneous fill is the number that flaps: it is one point on a sawtooth that the callback
 * and the producer move in opposite directions. Four samples at the 500 ms cadence is two seconds -
 * long enough for a slow drift towards starvation to show as a falling minimum, short enough that a
 * one-off stall from loading the next track is not still on screen a minute later.
 */
constexpr int kAudioFillSamples = 4;
unsigned g_audioFillMs[kAudioFillSamples] = {};
int g_audioFillCount = 0;
int g_audioFillNext = 0;

double GbFromBytes(unsigned long long bytes)
{
	return (double)bytes / (1024.0 * 1024.0 * 1024.0);
}

// Formats a size for display, choosing the unit so the number stays short.
std::string SizeText(double gb)
{
	char buffer[48];
	if (gb >= 1.0)
		std::snprintf(buffer, sizeof(buffer), "%.1f GB", gb);
	else
		std::snprintf(buffer, sizeof(buffer), "%.0f MB", gb * 1024.0);
	return buffer;
}

#ifdef _WIN32

/*
 * CPU.
 *
 * The frequency comes from the power-management API rather than from the registry: the registry holds
 * the nominal rating, while a modern CPU spends most of its time at some boost clock that is the
 * number worth watching. CallNtPowerInformation reports what the cores are running at now.
 *
 * Load is system-wide, measured as the complement of idle time between two samples - the same figure
 * Task Manager's "CPU" graph shows, and the one people compare against. A per-process number would be
 * smaller and less familiar.
 */
/*
 * CPU frequency, through powrprof.dll.
 *
 * Declared and loaded here rather than through <powrprof.h> and a link against powrprof.lib, for the
 * same reason the project defines no _WIN32_WINNT: the header hides this declaration behind a version
 * macro that this build does not set, and raising that macro to get one struct would change how every
 * other system header declares itself. Both the struct and the call are small and have been stable
 * since Windows XP, so stating them is less disruptive than moving the project's baseline.
 *
 * The value is the frequency the cores are running at *now*, not the nominal rating: a modern CPU
 * spends most of its time boosting, and the nominal figure would look wrong whenever anyone checked.
 */
struct ProcessorPowerInformation
{
	unsigned long number;
	unsigned long maxMhz;
	unsigned long currentMhz;
	unsigned long mhzLimit;
	unsigned long maxIdleState;
	unsigned long currentIdleState;
};

// POWER_INFORMATION_LEVEL::ProcessorInformation. The enum itself lives in winnt.h behind the same
// version macro, so the one value needed is named here.
constexpr int kProcessorInformation = 11;

// NTSTATUS, which is a LONG.
typedef long (*PFN_CallNtPowerInformation)(int, void*, unsigned long, void*, unsigned long);

PFN_CallNtPowerInformation LoadPowerApi()
{
	static PFN_CallNtPowerInformation cached = nullptr;
	static bool probed = false;
	if (probed)
		return cached;
	probed = true;

	if (HMODULE lib = LoadLibraryA("powrprof.dll"))
		cached = (PFN_CallNtPowerInformation)GetProcAddress(lib, "CallNtPowerInformation");
	// Not unloaded on purpose: the handle is process-wide and asked for at most once per machine.
	return cached;
}

/*
 * Which logical processors are the fast ones.
 *
 * Modern CPUs are not homogeneous. An Intel part with performance, efficiency and low-power efficiency
 * cores still reports every logical processor as equal to the operating system's basic queries, and a
 * readout that believes them describes a machine nobody owns: the thread count is larger than the
 * number of cores the emulator can actually rely on, the clock is an average that the slow cores drag
 * down, and the load is diluted by cores that are idle because nothing was scheduled there.
 *
 * Windows does expose the distinction, as an EfficiencyClass per logical processor, where a higher
 * class means a faster core and the classes line up with the core types. Everything sharing the top
 * class is treated as a performance core - which is also the entire population on a homogeneous CPU,
 * so the same code reports "all of them" without needing to know which kind of part it is on.
 *
 * GetSystemCpuSetInformation is declared behind the same version macro as everything else here, and
 * packed on top of that, so both the entry and the call are stated rather than included. The entry
 * layout is fixed by the API and the Size field it leads with is checked before any of it is trusted.
 */
#pragma pack(push, 1)
struct CpuSetInformation
{
	unsigned long size;
	int type;                          // CPU_SET_INFORMATION_TYPE; CpuSetInformation == 0
	unsigned long id;                  // logical processor number, as used to index the counters below
	unsigned short group;
	unsigned char logicalProcessorIndex;
	unsigned char coreIndex;
	unsigned char lastLevelCacheIndex;
	unsigned char numaNodeIndex;
	unsigned char efficiencyClass;     // higher is faster
	unsigned char flags;
	unsigned long reserved;
	unsigned long long allocationTag;
};
#pragma pack(pop)

typedef int (*PFN_GetSystemCpuSetInformation)(void*, unsigned long, unsigned long*, void*, unsigned long);

// The size the API reports for an entry. Entries are packed, so this is what the caller advances by;
// it is read from the first entry rather than assumed, so a future addition cannot silently desync the
// walk.
constexpr unsigned long kMinCpuSetSize = 20;

struct CpuTopology
{
	bool probed = false;
	bool hybrid = false;
	unsigned performanceThreads = 0;
	unsigned efficiencyThreads = 0;
	std::vector<unsigned> performanceIds;   // logical processor numbers, for the per-core counters
	// Per-processor nominal clock, indexed by processor number. Filled from the power API and used
	// both as the fallback classification and to work out which cores are the fast ones.
	std::vector<unsigned> maxMhz;
};

CpuTopology g_topology;

/*
 * The processor's marketing name, from the registry.
 *
 * HARDWARE\DESCRIPTION\System\CentralProcessor\0 holds the same string Task Manager shows, and it is
 * read once because it cannot change while the process runs. Any failure is left as an empty name:
 * the CPU line then looks exactly as it did before, which is the right degradation for a caption.
 */
void ReadCpuName()
{
	HKEY key = nullptr;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
		"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
		return;

	DWORD bytes = (DWORD)sizeof(g_cpu.name);
	DWORD type = 0;
	const LONG result = RegQueryValueExA(key, "ProcessorNameString", nullptr, &type,
		(LPBYTE)g_cpu.name, &bytes);
	RegCloseKey(key);

	// A longer value leaves the buffer untouched, so nothing can be trusted unless the read succeeded.
	if (result != ERROR_SUCCESS || type != REG_SZ)
	{
		g_cpu.name[0] = '\0';
		return;
	}
	g_cpu.name[sizeof(g_cpu.name) - 1] = '\0';

	// OEM strings are sometimes space-padded, and a leading space would indent the whole line.
	char* start = g_cpu.name;
	while (*start == ' ')
		++start;
	std::size_t length = std::strlen(start);
	while (length > 0 && start[length - 1] == ' ')
		start[--length] = '\0';
	if (start != g_cpu.name)
		std::memmove(g_cpu.name, start, length + 1);
}

/*
 * Reads the topology once and caches it: core types do not change while the process runs, and the
 * enumeration is a call plus an allocation that has no business happening several times a second.
 *
 * If the CPU sets API is unavailable - an older Windows - the per-processor nominal clock from the
 * power API is used instead: on every hybrid part so far the fast cores are rated meaningfully higher
 * than the others, so the top rating is a usable stand-in for the top efficiency class. When nothing
 * distinguishes the cores the type is simply not hybrid and the whole CPU is reported, which is the
 * honest answer rather than a guess.
 */
void ProbeCpuTopology()
{
	g_topology.probed = true;

	ReadCpuName();

	// The nominal clocks are needed either way, so they are read first.
	if (PFN_CallNtPowerInformation call = LoadPowerApi())
	{
		const unsigned count = std::thread::hardware_concurrency();
		if (count > 0)
		{
			std::vector<ProcessorPowerInformation> info(count);
			if (call(kProcessorInformation, nullptr, 0, info.data(),
				(unsigned long)(info.size() * sizeof(ProcessorPowerInformation))) == 0)
			{
				g_topology.maxMhz = std::vector<unsigned>(count, 0);
				for (const auto& one : info)
				{
					// Index by the processor number the API reports, not by array position: the entries are
					// sequential on every machine seen so far, but the number is what the per-core counters
					// are indexed by, so using it keeps the two from silently disagreeing.
					if (one.number < g_topology.maxMhz.size())
						g_topology.maxMhz[one.number] = one.maxMhz;
				}
			}
		}
	}

	HMODULE kernel = GetModuleHandleA("kernel32.dll");
	PFN_GetSystemCpuSetInformation getSets = kernel == nullptr ? nullptr :
		(PFN_GetSystemCpuSetInformation)GetProcAddress(kernel, "GetSystemCpuSetInformation");
	if (getSets != nullptr)
	{
		unsigned long needed = 0;
		// A first call with no buffer reports the size, and also fails - which is expected here.
		getSets(nullptr, 0, &needed, nullptr, 0);
		if (needed > 0)
		{
			std::vector<unsigned char> buffer(needed);
			if (getSets(buffer.data(), needed, &needed, nullptr, 0))
			{
				struct Entry { unsigned id; unsigned char cls; };
				std::vector<Entry> entries;
				unsigned topClass = 0;

				unsigned long offset = 0;
				while (offset + 4 <= needed)
				{
					const CpuSetInformation* entry = (const CpuSetInformation*)(buffer.data() + offset);
					if (entry->size < kMinCpuSetSize || offset + entry->size > needed)
						break;
					// Id is the CPU set's own identifier (256..271 on a hybrid part), not the logical
					// processor number the per-core counters and power API index by. That number is
					// LogicalProcessorIndex, within Group. Only group 0 is sampled, which is every
					// machine this readout runs on (< 64 logical processors).
					if (entry->type == 0 && entry->group == 0)
					{
						entries.push_back({ (unsigned)entry->logicalProcessorIndex, entry->efficiencyClass });
						if (entry->efficiencyClass > topClass)
							topClass = entry->efficiencyClass;
					}
					offset += entry->size;
				}

				if (!entries.empty())
				{
					for (const auto& one : entries)
					{
						if (one.cls == topClass)
						{
							g_topology.performanceThreads++;
							g_topology.performanceIds.push_back(one.id);
						}
						else
						{
							g_topology.efficiencyThreads++;
						}
					}
					g_topology.hybrid = g_topology.efficiencyThreads > 0 && g_topology.performanceThreads > 0;
					return;
				}
			}
		}
	}

	// Fallback: group by nominal clock.
	if (!g_topology.maxMhz.empty())
	{
		unsigned top = 0;
		for (unsigned mhz : g_topology.maxMhz)
			if (mhz > top)
				top = mhz;
		if (top > 0)
		{
			for (unsigned i = 0; i < g_topology.maxMhz.size(); ++i)
			{
				if (g_topology.maxMhz[i] == top)
				{
					g_topology.performanceThreads++;
					g_topology.performanceIds.push_back(i);
				}
				else
				{
					g_topology.efficiencyThreads++;
				}
			}
			g_topology.hybrid = g_topology.efficiencyThreads > 0 && g_topology.performanceThreads > 0;
		}
	}
}

/*
 * Current clock, through the performance counters Task Manager uses.
 *
 * CallNtPowerInformation reports the *nominal* clock as CurrentMhz on this generation of CPUs: a Core
 * Ultra returns 1200 MHz for a P-core and 700 MHz for an E-core no matter what they are boosting to.
 * Windows exposes the boost separately as Processor Information's "% Processor Performance", a
 * percentage of nominal. Multiplying the two gives the live clock per logical processor, and taking
 * the average over the performance cores is what this readout wants: an average over every core
 * would be dragged down by idle E-cores, and a single "CPU MHz" that never leaves the nominal figure
 * is worse than no figure at all.
 *
 * PDH is loaded at run time for the same reason as powrprof and NVML: a missing or damaged pdh.dll
 * should cost the readout, not the program.
 *
 * Both the average and the highest core are returned. The average says what the package is doing, the
 * peak says what the thread the emulator runs on can actually get - and on a laptop those two are far
 * apart, because one busy core boosting to 4 GHz among seven idle ones averages out around 2 GHz. A
 * readout that showed only the average was read as "this CPU refuses to boost" on exactly the machines
 * where the opposite was happening.
 */
typedef void* PDH_HQUERY;
typedef void* PDH_HCOUNTER;

struct PdhFmtCounterValue
{
	unsigned long status;
	union
	{
		long longValue;
		double doubleValue;
		long long largeValue;
		const wchar_t* stringValue;
	} value;
};

struct PdhFmtCounterValueItemW
{
	wchar_t* name;
	PdhFmtCounterValue value;
};

typedef long (*PFN_PdhOpenQueryW)(const wchar_t*, unsigned long long, PDH_HQUERY*);
typedef long (*PFN_PdhAddEnglishCounterW)(PDH_HQUERY, const wchar_t*, unsigned long long, PDH_HCOUNTER*);
typedef long (*PFN_PdhAddCounterW)(PDH_HQUERY, const wchar_t*, unsigned long long, PDH_HCOUNTER*);
typedef long (*PFN_PdhCollectQueryData)(PDH_HQUERY);
typedef long (*PFN_PdhGetFormattedCounterArrayW)(PDH_HCOUNTER, unsigned long, unsigned long*, unsigned long*, PdhFmtCounterValueItemW*);

constexpr unsigned long kPdhFmtDouble = 0x00000200;
constexpr unsigned long kPdhMoreData = 0x800007D2;

struct PdhApi
{
	HMODULE module = nullptr;
	PFN_PdhOpenQueryW open = nullptr;
	PFN_PdhAddEnglishCounterW addEnglish = nullptr;
	PFN_PdhAddCounterW addCounter = nullptr;
	PFN_PdhCollectQueryData collect = nullptr;
	PFN_PdhGetFormattedCounterArrayW array = nullptr;

	// CPU clock.
	PDH_HQUERY query = nullptr;
	PDH_HCOUNTER counter = nullptr;
	bool probed = false;
	bool ready = false;

	/*
	 * GPU load, for machines without NVML.
	 *
	 * A second query rather than a second counter on the first one: the two counter sets are present
	 * on different Windows versions, and sharing a query would make a missing "GPU Engine" set cost
	 * the CPU clock as well.
	 */
	PDH_HQUERY gpuQuery = nullptr;
	PDH_HCOUNTER gpuCounter = nullptr;
	bool gpuProbed = false;
	bool gpuReady = false;
	bool gpuWarm = false;

	bool apiProbed = false;
	bool apiReady = false;
};

PdhApi g_pdh;

/*
 * pdh.dll and its entry points, loaded once for both queries above.
 *
 * Runtime loading rather than a link-time dependency, for the same reason as powrprof and NVML: a
 * missing or damaged pdh.dll should cost the readout, not the program.
 */
bool LoadPdhApi()
{
	if (g_pdh.apiProbed)
		return g_pdh.apiReady;
	g_pdh.apiProbed = true;

	HMODULE module = LoadLibraryA("pdh.dll");
	if (module == nullptr)
	{
		InfoLog("Stats OSD: pdh.dll could not be loaded, so the CPU clock and GPU load will not be shown.");
		return false;
	}

	g_pdh.open = (PFN_PdhOpenQueryW)GetProcAddress(module, "PdhOpenQueryW");
	g_pdh.addEnglish = (PFN_PdhAddEnglishCounterW)GetProcAddress(module, "PdhAddEnglishCounterW");
	g_pdh.addCounter = (PFN_PdhAddCounterW)GetProcAddress(module, "PdhAddCounterW");
	g_pdh.collect = (PFN_PdhCollectQueryData)GetProcAddress(module, "PdhCollectQueryData");
	g_pdh.array = (PFN_PdhGetFormattedCounterArrayW)GetProcAddress(module, "PdhGetFormattedCounterArrayW");
	if (g_pdh.open == nullptr || g_pdh.collect == nullptr || g_pdh.array == nullptr ||
		(g_pdh.addEnglish == nullptr && g_pdh.addCounter == nullptr))
	{
		InfoLog("Stats OSD: pdh.dll lacks a needed entry point, so the CPU clock and GPU load will not be shown.");
		return false;
	}

	g_pdh.module = module;
	g_pdh.apiReady = true;
	return true;
}

bool InitPdh()
{
	if (g_pdh.probed)
		return g_pdh.ready;
	g_pdh.probed = true;

	if (!LoadPdhApi())
		return false;

	long openStatus = g_pdh.open(nullptr, 0, &g_pdh.query);
	if (openStatus != 0)
	{
		InfoLog("Stats OSD: PdhOpenQuery failed (0x%08X); no CPU clock.", (unsigned long)openStatus);
		return false;
	}

	// English path in both cases: PdhAddEnglishCounterW resolves it on any UI language, and the
	// fallback (XP only) is reached with the same text, where a localized counter name may fail.
	static const wchar_t* path = L"\\Processor Information(*)\\% Processor Performance";
	const long status = g_pdh.addEnglish != nullptr
		? g_pdh.addEnglish(g_pdh.query, path, 0, &g_pdh.counter)
		: g_pdh.addCounter(g_pdh.query, path, 0, &g_pdh.counter);
	if (status != 0)
	{
		InfoLog("Stats OSD: PdhAddEnglishCounter failed (0x%08X); no CPU clock.", (unsigned long)status);
		return false;
	}

	// Prime the query. A rate counter would need two collections; this one is instantaneous, and a
	// failed first collection simply leaves the sample invalid until the next one.
	g_pdh.collect(g_pdh.query);
	g_pdh.ready = true;
	return true;
}

bool IsPerformanceProcessor(unsigned id)
{
	for (unsigned candidate : g_topology.performanceIds)
		if (candidate == id)
			return true;
	return false;
}

/*
 * Average live clock over the performance cores, in MHz.
 *
 * The instance names are "group,processor" (e.g. "0,5"). Only group 0 is handled: every machine this
 * runs on has fewer than 64 logical processors, and on a multi-group machine the processor numbers
 * restart per group, so treating the second number as an absolute index would silently mix groups.
 * Skipping is the honest failure there; the line falls back to "-" as it does when PDH is absent.
 */
bool SampleClockFromPdh(unsigned& averageMhz, unsigned& peakMhz)
{
	if (!InitPdh() || g_topology.maxMhz.empty())
		return false;

	// The first read after the query opens is unreliable - parked cores read zero and waking cores
	// read wild spikes - so it is thrown away. The 500 ms sample cadence makes one warm-up free.
	static bool warm = false;
	if (!warm)
	{
		warm = true;
		g_pdh.collect(g_pdh.query);
		return false;
	}

	if (g_pdh.collect(g_pdh.query) != 0)
		return false;

	unsigned long bytes = 0;
	unsigned long items = 0;
	long status = g_pdh.array(g_pdh.counter, kPdhFmtDouble, &bytes, &items, nullptr);
	if ((unsigned long)status != kPdhMoreData || bytes == 0)
		return false;

	std::vector<unsigned char> buffer(bytes);
	status = g_pdh.array(g_pdh.counter, kPdhFmtDouble, &bytes, &items,
		(PdhFmtCounterValueItemW*)buffer.data());
	if (status != 0)
		return false;

	const PdhFmtCounterValueItemW* values = (const PdhFmtCounterValueItemW*)buffer.data();
	unsigned long long sum = 0;
	unsigned seen = 0;
	unsigned highest = 0;
	for (unsigned long i = 0; i < items; ++i)
	{
		const wchar_t* name = values[i].name;
		if (name == nullptr)
			continue;
		wchar_t* end = nullptr;
		const unsigned long group = wcstoul(name, &end, 10);
		if (end == name || *end != L',')
			continue;
		wchar_t* endProcessor = nullptr;
		const unsigned long processor = wcstoul(end + 1, &endProcessor, 10);
		// "0,_total" lands here with endProcessor == end + 1; it is not a core.
		if (endProcessor == end + 1 || group != 0 || processor >= g_topology.maxMhz.size())
			continue;
		if (!IsPerformanceProcessor((unsigned)processor))
			continue;
		const unsigned base = g_topology.maxMhz[processor];
		if (base == 0)
			continue;
		const double percent = values[i].value.value.doubleValue;
		// Zero means a parked core, and a spike (thousands of percent) is a transient from a core
		// changing power state. The cap is far above any real boost ratio (6 GHz at a 1 GHz base)
		// while still below the garbage the API hands out for a moment.
		if (percent <= 0.0 || percent > 600.0)
			continue;
		const double mhz = (double)base * percent / 100.0;
		const unsigned rounded = (unsigned)(mhz + 0.5);
		sum += rounded;
		if (rounded > highest)
			highest = rounded;
		++seen;
	}

	if (seen == 0)
		return false;

	averageMhz = (unsigned)(sum / seen);
	peakMhz = highest;

	static bool logged = false;
	if (!logged)
	{
		logged = true;
		InfoLog("Stats OSD: CPU %s, clock %u MHz average, %u MHz peak (%u performance core(s)).",
			g_cpu.name[0] != '\0' ? g_cpu.name : "(name unavailable)", averageMhz, peakMhz, seen);
	}
	return true;
}

/*
 * Per-processor utilisation, through NtQuerySystemInformation.
 *
 * GetSystemTimes is only system-wide, and splitting its figure between core types is not something a
 * weighted average can be trusted to do - so the per-processor counters are read instead and summed
 * over the fast cores alone. This is the same source Task Manager uses, reached through the NT call the
 * per-processor view has always come from; it is resolved at run time like everything else here.
 */
struct ProcessorPerformance
{
	long long idleTime;
	long long kernelTime;
	long long userTime;
	long long dpcTime;
	long long interruptTime;
	unsigned long interruptCount;
};

// SYSTEM_INFORMATION_CLASS::SystemProcessorPerformanceInformation.
constexpr int kSystemProcessorPerformanceInformation = 8;

typedef long (*PFN_NtQuerySystemInformation)(int, void*, unsigned long, unsigned long*);

// Summed over a subset of the processors, so the caller says which ones.
struct CpuTimes
{
	unsigned long long idle = 0;
	unsigned long long total = 0;
	bool any = false;
};

bool QueryCpuTimes(const std::vector<unsigned>& processors, CpuTimes& out)
{
	static PFN_NtQuerySystemInformation query = nullptr;
	static bool probed = false;
	if (!probed)
	{
		probed = true;
		if (HMODULE ntdll = GetModuleHandleA("ntdll.dll"))
			query = (PFN_NtQuerySystemInformation)GetProcAddress(ntdll, "NtQuerySystemInformation");
	}
	if (query == nullptr)
		return false;

	const unsigned count = std::thread::hardware_concurrency();
	if (count == 0)
		return false;

	std::vector<ProcessorPerformance> info(count);
	unsigned long returned = 0;
	if (query(kSystemProcessorPerformanceInformation, info.data(),
		(unsigned long)(info.size() * sizeof(ProcessorPerformance)), &returned) != 0)
		return false;

	const unsigned available = returned / (unsigned)sizeof(ProcessorPerformance);
	for (unsigned index : processors)
	{
		if (index >= available)
			continue;
		// KernelTime already includes idle, so total is kernel + user with no adjustment.
		out.idle += (unsigned long long)info[index].idleTime;
		out.total += (unsigned long long)info[index].kernelTime + (unsigned long long)info[index].userTime;
		out.any = true;
	}
	return out.any;
}

void SampleCpu()
{
	if (!g_topology.probed)
		ProbeCpuTopology();

	const unsigned total = std::thread::hardware_concurrency();
	g_cpu.totalThreads = total;
	g_cpu.hybrid = g_topology.hybrid;
	g_cpu.performanceThreads = g_topology.hybrid ? g_topology.performanceThreads : total;
	g_cpu.efficiencyThreads = g_topology.hybrid ? g_topology.efficiencyThreads : 0;

	/*
	 * Clock over the fast cores only. An average across every core understates the boost the emulation
	 * thread actually runs at, and the nominal clock the power API reports is not the question either;
	 * SampleClockFromPdh() turns Windows' performance percentage into the live figure.
	 */
	unsigned clockMhz = 0;
	unsigned peakMhz = 0;
	if (SampleClockFromPdh(clockMhz, peakMhz))
	{
		g_cpu.currentMhz = clockMhz;
		g_cpu.peakMhz = peakMhz;
		g_cpu.valid = true;
	}

	static CpuTimes prev;
	static bool havePrev = false;

	CpuTimes now;
	if (!QueryCpuTimes(g_topology.performanceIds, now))
	{
		// No per-processor counters: fall back to the system-wide figure, which is what this readout
		// showed before and is still true, just coarser.
		now = CpuTimes();
		FILETIME idle = {}, kernel = {}, user = {};
		if (GetSystemTimes(&idle, &kernel, &user))
		{
			ULARGE_INTEGER i, k, u;
			i.LowPart = idle.dwLowDateTime;   i.HighPart = idle.dwHighDateTime;
			k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
			u.LowPart = user.dwLowDateTime;   u.HighPart = user.dwHighDateTime;
			now.idle = i.QuadPart;
			now.total = k.QuadPart + u.QuadPart;
			now.any = true;
		}
	}

	if (havePrev && now.any && prev.any)
	{
		const unsigned long long idleDelta = now.idle - prev.idle;
		const unsigned long long totalDelta = now.total - prev.total;
		if (totalDelta > 0)
		{
			const double busy = 1.0 - (double)idleDelta / (double)totalDelta;
			const double clamped = busy < 0.0 ? 0.0 : (busy > 1.0 ? 1.0 : busy);
			g_cpu.loadPercent = (unsigned)(clamped * 100.0 + 0.5);
		}
	}
	prev = now;
	havePrev = now.any;
}

void SampleMemory()
{
	PROCESS_MEMORY_COUNTERS counters = {};
	if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
	{
		g_mem.workingSetGb = GbFromBytes(counters.WorkingSetSize);
		g_mem.valid = true;
	}

	MEMORYSTATUSEX status = {};
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status))
	{
		g_mem.systemTotalGb = GbFromBytes(status.ullTotalPhys);
		g_mem.systemUsedGb = GbFromBytes(status.ullTotalPhys - status.ullAvailPhys);
	}
}

/*
 * GPU, through NVML.
 *
 * Loaded at run time rather than linked, for two reasons. The library ships with the NVIDIA driver, so
 * assuming it at link time would make the executable refuse to start on a machine that has no NVIDIA
 * card - which is a much worse failure than a missing readout. And there is no vendor-neutral
 * equivalent that is not version-sensitive: the D3DKMT counters Task Manager uses have changed shape
 * between Windows releases, whereas NVML is deliberately stable.
 *
 * Every entry point is optional. If any is missing the related line is simply not shown, which is why
 * each one is checked rather than trusting that a present nvml.dll exports all of them.
 */
typedef int nvmlReturn_t;
typedef void* nvmlDevice_t;

enum { NVML_SUCCESS = 0 };
enum { NVML_CLOCK_GRAPHICS = 0 };

struct NvmlUtilization { unsigned int gpu; unsigned int memory; };
// The size fields are written by the driver as 64-bit and are the one part of this ABI that has
// changed shape historically, so the struct is laid out explicitly rather than guessed.
struct NvmlMemory { unsigned long long total; unsigned long long free; unsigned long long used; };

typedef nvmlReturn_t (*PFN_nvmlInit)(void);
typedef nvmlReturn_t (*PFN_nvmlShutdown)(void);
typedef nvmlReturn_t (*PFN_nvmlDeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*);
typedef nvmlReturn_t (*PFN_nvmlDeviceGetUtilizationRates)(nvmlDevice_t, NvmlUtilization*);
typedef nvmlReturn_t (*PFN_nvmlDeviceGetMemoryInfo)(nvmlDevice_t, NvmlMemory*);
typedef nvmlReturn_t (*PFN_nvmlDeviceGetClockInfo)(nvmlDevice_t, int, unsigned int*);
typedef nvmlReturn_t (*PFN_nvmlDeviceGetName)(nvmlDevice_t, char*, unsigned int);

struct Nvml
{
	HMODULE module = nullptr;
	nvmlDevice_t device = nullptr;
	PFN_nvmlInit init = nullptr;
	PFN_nvmlShutdown shutdown = nullptr;
	PFN_nvmlDeviceGetHandleByIndex handle = nullptr;
	PFN_nvmlDeviceGetUtilizationRates utilization = nullptr;
	PFN_nvmlDeviceGetMemoryInfo memory = nullptr;
	PFN_nvmlDeviceGetClockInfo clock = nullptr;
	PFN_nvmlDeviceGetName name = nullptr;
	bool ready = false;
};

Nvml g_nvml;

void ProbeNvml()
{
	g_gpuProbed = true;

	g_nvml.module = LoadLibraryA("nvml.dll");
	if (g_nvml.module == nullptr)
	{
		// A driver installed through a non-standard path keeps it beside the driver files rather than
		// in the system directory, which is what the fallback path covers.
		g_nvml.module = LoadLibraryA("C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
	}
	if (g_nvml.module == nullptr)
	{
		InfoLog("Stats OSD: nvml.dll is not present, so the GPU figures will not be shown.");
		return;
	}

	// nvmlInit_v2 is the current entry point; older drivers only have nvmlInit.
	g_nvml.init = (PFN_nvmlInit)GetProcAddress(g_nvml.module, "nvmlInit_v2");
	if (g_nvml.init == nullptr)
		g_nvml.init = (PFN_nvmlInit)GetProcAddress(g_nvml.module, "nvmlInit");
	g_nvml.shutdown = (PFN_nvmlShutdown)GetProcAddress(g_nvml.module, "nvmlShutdown");
	g_nvml.handle = (PFN_nvmlDeviceGetHandleByIndex)GetProcAddress(g_nvml.module, "nvmlDeviceGetHandleByIndex_v2");
	if (g_nvml.handle == nullptr)
		g_nvml.handle = (PFN_nvmlDeviceGetHandleByIndex)GetProcAddress(g_nvml.module, "nvmlDeviceGetHandleByIndex");
	g_nvml.utilization = (PFN_nvmlDeviceGetUtilizationRates)GetProcAddress(g_nvml.module, "nvmlDeviceGetUtilizationRates");
	g_nvml.memory = (PFN_nvmlDeviceGetMemoryInfo)GetProcAddress(g_nvml.module, "nvmlDeviceGetMemoryInfo");
	g_nvml.clock = (PFN_nvmlDeviceGetClockInfo)GetProcAddress(g_nvml.module, "nvmlDeviceGetClockInfo");
	g_nvml.name = (PFN_nvmlDeviceGetName)GetProcAddress(g_nvml.module, "nvmlDeviceGetName");

	if (g_nvml.init == nullptr || g_nvml.handle == nullptr)
	{
		InfoLog("Stats OSD: nvml.dll is present but does not export what is needed; no GPU figures.");
		return;
	}
	if (g_nvml.init() != NVML_SUCCESS)
	{
		InfoLog("Stats OSD: NVML could not be initialised; no GPU figures.");
		return;
	}
	// Device 0 is the one the renderer picked in practice, and enumerating to match exactly would mean
	// importing the device-name API to compare against what Vulkan chose.
	if (g_nvml.handle(0, &g_nvml.device) != NVML_SUCCESS)
	{
		InfoLog("Stats OSD: NVML found no device 0; no GPU figures.");
		return;
	}

	g_nvml.ready = true;

	// The model name is read once; it does not change while the process runs. Shown on the GPU line so
	// the clock and VRAM figures carry a make rather than reading as a bare NVIDIA telemetry dump.
	if (g_nvml.name != nullptr)
		g_nvml.name(g_nvml.device, g_gpu.name, sizeof(g_gpu.name));

	InfoLog("Stats OSD: NVML available (%s); GPU figures will be shown.",
		g_gpu.name[0] != '\0' ? g_gpu.name : "name unavailable");
}

/*
 * Case-insensitive wide substring test.
 *
 * The C runtime has no wcsistr, and this is needed in exactly one place: the engine suffix inside a
 * PDH instance name. Matching the whole name would not do either - the pid and LUID in front of the
 * suffix change per process and per adapter.
 */
bool ContainsNoCase(const wchar_t* haystack, const wchar_t* needle)
{
	const std::size_t length = std::wcslen(needle);
	if (length == 0)
		return true;

	for (const wchar_t* start = haystack; *start != L'\0'; ++start)
	{
		std::size_t i = 0;
		while (i < length && start[i] != L'\0' && std::towlower(start[i]) == std::towlower(needle[i]))
			++i;
		if (i == length)
			return true;
	}
	return false;
}

/*
 * GPU load without a vendor API, through the counter set Task Manager's GPU graphs read.
 *
 * The set is "GPU Engine", one instance per (process, GPU, engine), named like
 * "pid_1234_luid_0x00000000_0x00016056_phys_0_eng_0_engtype_3d". Summing the 3D instances gives
 * the share of the 3D engine in use, which is the figure that answers "is the GPU the limit here".
 *
 * This exists because NVML covers NVIDIA only: on an Intel or AMD laptop the readout used to have no
 * GPU line at all, and the GPU is exactly where a weak integrated part shows up first. Clock and
 * video memory stay unavailable there - Windows exposes no vendor-neutral equivalent - so the line
 * carries the load alone rather than inventing zeroes for the rest.
 *
 * Best effort, like everything else here: a Windows too old for the counter set has no GPU line, and
 * that is logged once instead of retried every half second.
 */
bool SampleGpuLoadFromPdh()
{
	if (!g_pdh.gpuProbed)
	{
		g_pdh.gpuProbed = true;
		if (!LoadPdhApi())
			return false;

		if (g_pdh.open(nullptr, 0, &g_pdh.gpuQuery) != 0)
			return false;

		static const wchar_t* path = L"\\GPU Engine(*)\\Utilization Percentage";
		const long status = g_pdh.addEnglish != nullptr
			? g_pdh.addEnglish(g_pdh.gpuQuery, path, 0, &g_pdh.gpuCounter)
			: g_pdh.addCounter(g_pdh.gpuQuery, path, 0, &g_pdh.gpuCounter);
		if (status != 0)
		{
			InfoLog("Stats OSD: the GPU Engine counters are not available (0x%08X), so GPU load will not "
				"be shown for this adapter.", (unsigned long)status);
			return false;
		}

		// A rate counter needs two collections; the first interval has no length and is discarded.
		g_pdh.collect(g_pdh.gpuQuery);
		g_pdh.gpuReady = true;
	}
	if (!g_pdh.gpuReady)
		return false;
	if (!g_pdh.gpuWarm)
	{
		g_pdh.gpuWarm = true;
		return false;
	}

	if (g_pdh.collect(g_pdh.gpuQuery) != 0)
		return false;

	unsigned long bytes = 0;
	unsigned long items = 0;
	long status = g_pdh.array(g_pdh.gpuCounter, kPdhFmtDouble, &bytes, &items, nullptr);
	if ((unsigned long)status != kPdhMoreData || bytes == 0)
		return false;

	std::vector<unsigned char> buffer(bytes);
	status = g_pdh.array(g_pdh.gpuCounter, kPdhFmtDouble, &bytes, &items,
		(PdhFmtCounterValueItemW*)buffer.data());
	if (status != 0)
		return false;

	const PdhFmtCounterValueItemW* values = (const PdhFmtCounterValueItemW*)buffer.data();
	double total = 0.0;
	bool any = false;
	for (unsigned long i = 0; i < items; ++i)
	{
		if (values[i].name == nullptr)
			continue;
		// PDH_CSTATUS_VALID_DATA and PDH_CSTATUS_NEW_DATA are the two "use this" codes; anything
		// else is a counter that is still warming up or has lost its instance.
		if (values[i].value.status != 0 && values[i].value.status != 1)
			continue;
		// English by construction: the counter was added through PdhAddEnglishCounterW. The engine suffix
		// is lowercase there ("engtype_3d") but has been reported in mixed case by some drivers, so the
		// comparison ignores case rather than depending on which of the two this machine produces.
		if (!ContainsNoCase(values[i].name, L"engtype_3d"))
			continue;
		const double percent = values[i].value.value.doubleValue;
		if (percent <= 0.0)
			continue;
		total += percent;
		any = true;
	}
	if (!any)
		return false;

	// A laptop with two GPUs has a 3D engine on each, and the sum can pass 100. The figure is meant to
	// be "how busy is the 3D engine", so it is capped rather than printed as 140% and read as a bug.
	g_gpu.loadPercent = total > 100.0 ? 100u : (unsigned)(total + 0.5);
	g_gpu.valid = true;
	return true;
}

/*
 * Which counter the GPU line reads.
 *
 * NVML is preferred because it is also the only source that knows the clock and the video memory. The
 * Windows counters are the fallback for every other vendor, and SM3D_STATS_GPU forces one of the two
 * so that the fallback can be exercised on a machine that has NVML as well - testing it only on
 * hardware without NVML is how a broken fallback ships.
 */
enum class GpuSource
{
	Auto,
	Nvml,
	Pdh,
};

GpuSource g_gpuSource = GpuSource::Auto;
bool g_gpuSourceLogged = false;

void ReadGpuSourceOverride()
{
	static bool read = false;
	if (read)
		return;
	read = true;

	const char* value = std::getenv("SM3D_STATS_GPU");
	if (value == nullptr)
		return;
	if (std::strcmp(value, "pdh") == 0)
		g_gpuSource = GpuSource::Pdh;
	else if (std::strcmp(value, "nvml") == 0)
		g_gpuSource = GpuSource::Nvml;
	else
		InfoLog("Stats OSD: SM3D_STATS_GPU='%s' is neither 'pdh' nor 'nvml'; ignored.", value);
}

void SampleGpu()
{
	ReadGpuSourceOverride();

	/*
	 * The source is logged once, because "GPU 0%" means different things depending on which counter it
	 * came from - a driver telemetry block that is not updating, or a Windows counter with no 3D engine
	 * instance for this process. A support log that names the source settles that in one line.
	 */
	if (g_gpuSource == GpuSource::Pdh)
	{
		if (!g_gpuSourceLogged)
		{
			g_gpuSourceLogged = true;
			InfoLog("Stats OSD: GPU load comes from the Windows GPU Engine counters (SM3D_STATS_GPU=pdh).");
		}
		SampleGpuLoadFromPdh();
		return;
	}

	if (!g_gpuProbed)
		ProbeNvml();
	if (!g_nvml.ready)
	{
		if (g_gpuSource == GpuSource::Nvml)
		{
			if (!g_gpuSourceLogged)
			{
				g_gpuSourceLogged = true;
				InfoLog("Stats OSD: SM3D_STATS_GPU=nvml but NVML is not available; no GPU line.");
			}
			return;
		}

		// No NVIDIA telemetry. The Windows counter still gives the load, which is the one figure that
		// decides whether the GPU is the limit; clock and VRAM are left out rather than shown as zero.
		if (!g_gpuSourceLogged)
		{
			g_gpuSourceLogged = true;
			InfoLog("Stats OSD: no NVML on this machine; GPU load comes from the Windows GPU Engine counters.");
		}
		SampleGpuLoadFromPdh();
		return;
	}

	if (g_nvml.clock != nullptr)
	{
		unsigned mhz = 0;
		if (g_nvml.clock(g_nvml.device, NVML_CLOCK_GRAPHICS, &mhz) == NVML_SUCCESS)
			g_gpu.clockMhz = mhz;
	}
	if (g_nvml.utilization != nullptr)
	{
		NvmlUtilization util = {};
		if (g_nvml.utilization(g_nvml.device, &util) == NVML_SUCCESS)
			g_gpu.loadPercent = util.gpu;
	}
	if (g_nvml.memory != nullptr)
	{
		NvmlMemory mem = {};
		if (g_nvml.memory(g_nvml.device, &mem) == NVML_SUCCESS)
		{
			g_gpu.vramUsedGb = GbFromBytes(mem.used);
			g_gpu.vramTotalGb = GbFromBytes(mem.total);
		}
	}
	g_gpu.valid = true;
}

void ShutdownNvml()
{
	if (g_nvml.ready && g_nvml.shutdown != nullptr)
		g_nvml.shutdown();
	g_nvml.ready = false;
	if (g_nvml.module != nullptr)
	{
		FreeLibrary(g_nvml.module);
		g_nvml.module = nullptr;
	}
}

/*
 * Clears the warm-up state of the rate counters.
 *
 * The interval between the last sample of one session and the first of the next has no length, so a
 * figure taken across it would read as zero. The PDH queries themselves stay open - they are
 * process-wide handles, and closing them would only mean reopening them next session.
 */
void ResetSlowSampleWarmup()
{
	g_pdh.gpuWarm = false;
}

#else
// Other platforms: the frame, CPU-thread and memory figures still work, the vendor-specific ones do
// not. Kept as no-ops rather than #ifdef'ing the call sites, so the readout's logic reads the same
// everywhere.
void SampleCpu() {}
void SampleMemory() {}
void SampleGpu() {}
void ShutdownNvml() {}
void ResetSlowSampleWarmup() {}
#endif

/*
 * The display mode the window is actually on.
 *
 * Reported from the desktop mode rather than from SDL_GetWindowDisplayMode, which looks like the
 * obvious call and is the wrong one here: it returns the mode matching the window's size, so a
 * 1600x1200 window on a 3840x2160 panel reports "1600x1200@60" - the render resolution and the
 * emulator's own pacing rate dressed up as a panel figure. The desktop mode answers the question the
 * line exists for, which is what the display itself is scanning at.
 *
 * In the fixed-arcade path this reports the 57/58 Hz mode the emulator switched to, and in the
 * G-SYNC/VRR path it reports the panel's own mode (e.g. 120 Hz), above which G-SYNC is free to track
 * whatever rate the emulator presents at. Both are the truth about the panel; neither is the rate the
 * game is emulated at, which is what the Rate line above it is for.
 */
void SampleDisplay()
{
	if (g_window == nullptr)
		return;

	const OSD::Platform::DisplayId display = OSD::Platform::GetWindowDisplay(g_window);
	OSD::Platform::DisplayMode mode{};
	if (OSD::Platform::GetDesktopDisplayMode(display, &mode))
	{
		// The readout prints an integer rate; SDL3 reports a float. Rounded rather than truncated so
		// 59.94 Hz reports 60, which is what the display's own OSD says.
		g_display.refreshHz = (int)(mode.refreshHz + 0.5f);
		g_display.width = mode.width;
		g_display.height = mode.height;
		g_display.valid = true;
	}
}

/*
 * Called from the 500 ms sampler, never per frame: GetAudioStatus() takes the audio device lock to
 * snapshot the ring positions consistently, and that lock belongs to the producer and the callback.
 */
void SampleAudio()
{
	AudioStatus status;
	GetAudioStatus(status);
	g_audio.valid = status.opened;
	g_audio.muted = status.muted;
	g_audio.bufferMs = status.bufferMs;
	g_audio.bufferSizeMs = status.bufferSizeMs;
	g_audio.channels = status.channels;
	g_audio.underRuns = status.underRuns;
	g_audio.overRuns = status.overRuns;
	g_audio.peakLeft = status.peakLeft;
	g_audio.peakRight = status.peakRight;

	// A range rather than the latest fill: a buffer that is healthy on average but touches a low
	// minimum every few seconds is the stutter that has not happened yet.
	if (status.opened)
	{
		g_audioFillMs[g_audioFillNext] = status.bufferMs;
		g_audioFillNext = (g_audioFillNext + 1) % kAudioFillSamples;
		if (g_audioFillCount < kAudioFillSamples)
			g_audioFillCount++;
	}
	else
	{
		g_audioFillCount = 0;
		g_audioFillNext = 0;
	}

	g_audio.fillMinMs = 0;
	g_audio.fillMaxMs = 0;
	if (g_audioFillCount > 0)
	{
		g_audio.fillMinMs = g_audioFillMs[0];
		g_audio.fillMaxMs = g_audioFillMs[0];
		for (int i = 1; i < g_audioFillCount; ++i)
		{
			g_audio.fillMinMs = std::min(g_audio.fillMinMs, g_audioFillMs[i]);
			g_audio.fillMaxMs = std::max(g_audio.fillMaxMs, g_audioFillMs[i]);
		}
	}
}

void SampleSlowStats()
{
	SampleCpu();
	SampleMemory();
	SampleAudio();
	SampleGpu();
	SampleDisplay();
}

// The 99th percentile of the recorded frame intervals, inverted. Sorting a copy is fine at this size
// and happens twice a second at most.
double ComputeLow1()
{
	if (g_historyCount < 60)
		return 0.0;

	std::vector<double> sorted(g_frameMs, g_frameMs + g_historyCount);
	std::sort(sorted.begin(), sorted.end());

	// Index of the slowest 1%, clamped so a short history still yields a value inside the array.
	int index = (int)(sorted.size() * 0.99);
	if (index >= (int)sorted.size())
		index = (int)sorted.size() - 1;
	const double worst = sorted[index];
	return worst > 0.0 ? 1000.0 / worst : 0.0;
}

const char* RendererName()
{
	// Read from what the caller reported, not from the configuration: see the note on StatsRenderer.
	switch (g_renderer)
	{
	case StatsRenderer::Vulkan3D: return "Vulkan 3D";
	case StatsRenderer::Legacy3D: return "Legacy 3D";
	default:                      return "New 3D";
	}
}

/*
 * How the panel's rate and the emulated rate relate, and whether that relationship is a problem.
 *
 * Borderless fullscreen normally leaves the panel on its own rate - the desktop compositor presents at
 * that rate, and no driver-level variable refresh engages - so the emulated frames are fitted onto it.
 * When the panel rate is a whole multiple of the emulated rate the fitting is even (a 115 Hz panel
 * shows each 57.5 Hz frame exactly twice), and when it is not, some frames are shown one more time than
 * others. That is the judder people describe as "slightly uneven" in borderless mode, and it is a
 * property of two numbers that the readout already has - so it can be stated rather than left to be
 * puzzled over.
 *
 * The assumption is that the panel is on its own rate, and it can be false: a driver configured to
 * apply VRR to windowed output lets the display follow the window instead, and then each frame is shown
 * exactly once and nothing here applies. Nothing can distinguish the two from inside the program -
 * either way the frames are presented at the same rate and the frame rate reads the same - so this
 * function answers the question it can answer ("how do the two rates divide, if the panel is on its
 * own rate") and callers say which case it describes. The display's own on-screen refresh readout is
 * the only authority on whether VRR is actually engaging.
 *
 * The tolerance is 1%: 115 against 57.5 is exactly 2, and no real display is exact enough for a
 * tighter test to be useful.
 *
 * Uneven comes with an actionable number, which is what NearestMultiple() below produces. The arcade
 * rate is not a round figure, so panel rates do not divide into it either: the exact multiples are
 * 115.05, 172.57, 230.10, 287.62 Hz and so on, and none of them is a refresh rate a display ships
 * with. A reader told only "uneven" has no way to know what to aim for, whereas "4x = 230.10 Hz" can
 * be typed straight into the driver's custom resolution dialog. It also makes a counter-intuitive
 * result legible: a 230 Hz panel is *even* (3.998x) while a 240 Hz one is not (4.17x), and a higher
 * rate is no help at all - 480 Hz is 8.34x, equally uneven.
 */
enum class Cadence
{
	Unknown,        // nothing to compare, or not a mode where this applies
	Even,           // the panel rate is a whole multiple of the emulated rate
	Uneven          // it is not, so frames are repeated unevenly
};

/*
 * The whole multiple of the emulated rate nearest to the panel's, as (multiple, Hz).
 *
 * Only meaningful when a multiple of at least 2 exists, which is the same condition the even test
 * uses: below that the panel cannot show each frame more than once, so there is no cadence to align
 * to and the answer is an exclusive sync mode rather than a better panel rate. Callers check the
 * result's first field, which is 0 when there is nothing to suggest.
 *
 * "Nearest" is decided on the ratio, not on the resulting rate - for a 240 Hz panel that is 4x, which
 * is nearer to 4.17 than 5x is, even though 5x would overshoot the panel's own maximum.
 */
std::pair<int, double> NearestMultiple()
{
	const double targetHz = g_targetHz;
	if (targetHz <= 0.0 || !g_display.valid || g_display.refreshHz <= 0)
		return { 0, 0.0 };

	const double ratio = (double)g_display.refreshHz / targetHz;
	const int multiple = (int)std::floor(ratio + 0.5);
	if (multiple < 2)
		return { 0, 0.0 };

	return { multiple, multiple * targetHz };
}

Cadence ClassifyCadence(double& ratio)
{
	ratio = 0.0;

	/*
	 * Borderless and exclusive VRR, but not exclusive fixed rate.
	 *
	 * In the fixed-rate mode the panel really is being driven at the emulated rate - SetWindowDisplayMode
	 * put it on a 57/58 Hz mode - so there is no relationship left to divide. Testing it anyway would be
	 * actively misleading: SDL reports whole hertz, so the mode reads 57 against a 57.5242 target, which
	 * is 0.991 and classifies as uneven. That is the integer rounding of a 1:1 mapping being reported as
	 * a fault.
	 *
	 * Exclusive VRR belongs in the same group as borderless because the panel's *own* rate is what an
	 * application can see in both: the instantaneous scan rate is not exposed, so what is measured is
	 * the mode the panel is configured at. Whether that rate describes what is happening depends on VRR
	 * engaging, which nothing here can determine - so the figure is reported with that stated, and the
	 * callers word it for the mode it arrived in.
	 */
	if ((g_syncMode != StatsSyncMode::Borderless && g_syncMode != StatsSyncMode::ExclusiveVrr) ||
		!g_display.valid || g_display.refreshHz <= 0)
		return Cadence::Unknown;

	const double targetHz = g_targetHz;
	if (targetHz <= 0.0)
		return Cadence::Unknown;

	ratio = (double)g_display.refreshHz / targetHz;
	if (ratio < 1.0)
	{
		// The panel is slower than the emulator: frames have to be dropped rather than repeated, which
		// is uneven by construction and not something a multiple test can describe.
		return Cadence::Uneven;
	}

	/*
	 * 1x counts as even, and has to be tested first among the multiples because it is the best case
	 * there is: a panel running at exactly the emulated rate shows every frame once, which is as even
	 * as the relationship can get. It used to be excluded - the test asked for nearest >= 2 - and the
	 * result was the readout calling a perfect 1:1 session "uneven" while printing the 1.0000 scans per
	 * frame that says otherwise, with the advice to change sync mode attached. A 60 Hz panel at a 60 Hz
	 * pace is a common desktop configuration, so that was not a rare reading.
	 *
	 * Excluding 1 was presumably guarding against a ratio just below 1 rounding up to 1 and being
	 * called even. That case is already handled by the early return above: anything under 1.0 never
	 * reaches here.
	 */
	const double nearest = std::floor(ratio + 0.5);
	if (nearest >= 1.0 && std::fabs(ratio - nearest) <= nearest * 0.01)
		return Cadence::Even;

	return Cadence::Uneven;
}

/*
 * Readout palettes.
 *
 * Every preset keeps the same split the reference OSDs use: the static label is deliberately quiet
 * (cyan or cool grey) and the figure is the part the eye should land on (amber or warm sand). The
 * three semantic colours are only for the frame-rate figures and the cadence verdict, so a healthy
 * session reads mostly as two tones and a dip stands out without a wall of red.
 *
 * The background is the second half of the readability fix: a dark translucent plate makes the text
 * independent of what the game is drawing underneath. The last preset drops the plate and uses the
 * 1-pixel shadow instead, for players who want nothing but floating text.
 */
struct StatsPalette
{
	ImVec4 label;
	ImVec4 value;
	ImVec4 good;
	ImVec4 warn;
	ImVec4 bad;
	ImVec4 background;
	float rounding;
	bool shadow;
};

const StatsPalette kStatsPalettes[] =
{
	// 0: amber classic (default). #56B6C2 labels, #FF9E3B values, #8CE99A / #FCC419 / #FF6B6B.
	{
		ImVec4(0.337f, 0.714f, 0.761f, 1.0f),
		ImVec4(1.000f, 0.620f, 0.231f, 1.0f),
		ImVec4(0.549f, 0.914f, 0.604f, 1.0f),
		ImVec4(0.988f, 0.769f, 0.098f, 1.0f),
		ImVec4(1.000f, 0.420f, 0.420f, 1.0f),
		ImVec4(0.070f, 0.078f, 0.094f, 0.72f),
		6.0f, false
	},
	// 1: slate labels (#ABB2BF) with warm sand values (#E5C07B) and a teal "good" (#4EC9B0).
	{
		ImVec4(0.671f, 0.698f, 0.749f, 1.0f),
		ImVec4(0.898f, 0.753f, 0.482f, 1.0f),
		ImVec4(0.306f, 0.788f, 0.690f, 1.0f),
		ImVec4(0.988f, 0.769f, 0.098f, 1.0f),
		ImVec4(1.000f, 0.420f, 0.420f, 1.0f),
		ImVec4(0.070f, 0.078f, 0.094f, 0.72f),
		6.0f, false
	},
	// 2: high contrast. White labels, a near-black 86% plate and a larger radius.
	{
		ImVec4(0.960f, 0.965f, 0.980f, 1.0f),
		ImVec4(1.000f, 0.750f, 0.250f, 1.0f),
		ImVec4(0.600f, 1.000f, 0.650f, 1.0f),
		ImVec4(1.000f, 0.820f, 0.200f, 1.0f),
		ImVec4(1.000f, 0.400f, 0.400f, 1.0f),
		ImVec4(0.020f, 0.025f, 0.030f, 0.86f),
		8.0f, false
	},
	// 3: minimal outline. No plate; a black 1-pixel shadow separates the text from the picture.
	{
		ImVec4(0.337f, 0.714f, 0.761f, 1.0f),
		ImVec4(1.000f, 0.620f, 0.231f, 1.0f),
		ImVec4(0.549f, 0.914f, 0.604f, 1.0f),
		ImVec4(0.988f, 0.769f, 0.098f, 1.0f),
		ImVec4(1.000f, 0.420f, 0.420f, 1.0f),
		ImVec4(0.070f, 0.078f, 0.094f, 0.0f),
		0.0f, true
	},
};

const ImVec4& FpsColour(const StatsPalette& palette, double fps)
{
	if (fps <= 0.0)
		return palette.value;
	if (fps >= 57.0)
		return palette.good;
	if (fps >= 50.0)
		return palette.warn;
	return palette.bad;
}

/*
 * Draws one line, optionally with the 1-pixel shadow used by the no-plate preset. The shadow is a
 * separate draw command underneath the text, which is exactly what makes the text readable over a
 * white flash or a neon sign without a background box.
 */
void StatsText(const ImVec4& colour, bool shadow, const char* format, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	if (shadow)
	{
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f),
			ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.86f)), buffer);
	}
	ImGui::TextColored(colour, "%s", buffer);
}

/* A label in the quiet colour followed by its figure in the loud one. */
void StatsPair(const StatsPalette& palette, const char* label,
	const ImVec4& valueColour, const char* format, ...)
{
	StatsText(palette.label, palette.shadow, "%s", label);
	ImGui::SameLine();

	char buffer[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	StatsText(valueColour, palette.shadow, "%s", buffer);
}

/*
 * A labelled figure that can carry a model name on its own line.
 *
 * Both the CPU and the GPU have a name worth showing and figures too long to sit beside it, so the
 * name goes on the label line and the numbers are indented under the value column. The window is
 * AlwaysAutoResize, so the extra line grows it rather than clipping. With no name the pair collapses
 * back to the single-line form every other row uses.
 */
void StatsPairNamed(const StatsPalette& palette, const char* label, const char* name,
	const ImVec4& valueColour, const char* figures)
{
	StatsText(palette.label, palette.shadow, "%s", label);
	ImGui::SameLine();

	if (name != nullptr && name[0] != '\0')
	{
		const float valueX = ImGui::GetCursorPosX();
		StatsText(palette.value, palette.shadow, "%s", name);
		// TextColored already advanced to the next line; only the x has to be brought back under the
		// value column. An explicit NewLine() would add a blank line between the two.
		ImGui::SetCursorPosX(valueX);
	}

	StatsText(valueColour, palette.shadow, "%s", figures);
}

} // anonymous namespace

void StatsOsdFrameTick()
{
	const uint64_t now = (uint64_t)OSD::Platform::Now();

	if (g_lastFrameTicks != 0)
	{
		const double ms = (double)(now - g_lastFrameTicks) / 1.0e6;
		g_frameMs[g_historyNext] = ms;
		g_historyNext = (g_historyNext + 1) % kHistoryFrames;
		if (g_historyCount < kHistoryFrames)
			g_historyCount++;
	}
	g_lastFrameTicks = now;

	// Frame rate over a one-second window, which is long enough to be steady and short enough to react.
	if (g_fpsWindowStart == 0)
		g_fpsWindowStart = now;
	g_fpsWindowFrames++;

	const double windowMs = (double)(now - g_fpsWindowStart) / 1.0e6;
	if (windowMs >= 1000.0)
	{
		g_fps = (double)g_fpsWindowFrames * 1000.0 / windowMs;
		g_fpsLow1 = ComputeLow1();
		g_fpsWindowStart = now;
		g_fpsWindowFrames = 0;
	}

	// The readout being off does not stop the measurement - see the note on this function in the
	// header - but nothing needs sampling while it is off either.
	if (!g_enabled)
	{
		g_lastSlowSample = 0;
		return;
	}

	const double sampleMs = (double)(now - g_lastSlowSample) / 1.0e6;
	if (g_lastSlowSample == 0 || sampleMs >= (double)kSlowSampleMs)
	{
		g_lastSlowSample = now;
		SampleSlowStats();
	}
}

void StatsOsdBuildWindow(const Util::Config::Node& config)
{
	if (!g_enabled)
		return;

	/*
	 * Top left, sized to its contents, and with no inputs at all.
	 *
	 * NoInputs matters more than it looks: without it the window sits under the pointer, ImGui reports
	 * the mouse as captured, and anything that consults WantCaptureMouse - now or later - would stop
	 * responding to the game while a purely informational readout is on screen.
	 */
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float pad = 12.0f;
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + pad, viewport->WorkPos.y + pad),
		ImGuiCond_Always, ImVec2(0.0f, 0.0f));

	const int paletteCount = (int)(sizeof(kStatsPalettes) / sizeof(kStatsPalettes[0]));
	int paletteIndex = config["StatsOsdStyle"].ValueAsDefault<int>(0);
	if (paletteIndex < 0 || paletteIndex >= paletteCount)
		paletteIndex = 0;
	const StatsPalette& palette = kStatsPalettes[paletteIndex];

	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, palette.background);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, palette.rounding);
	const bool shadowStyle = palette.shadow;
	if (shadowStyle)
	{
		// The outline preset has no plate to separate it from the picture; the shadow does that job,
		// so a window border would only draw a rectangle around nothing.
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	}

	if (ImGui::Begin("##StatsOsd", nullptr, flags))
	{
		StatsPair(palette, "Renderer", palette.value, "%s", RendererName());

		/*
		 * Two lines, because the question the readout is most often opened for has two halves.
		 *
		 * Sync describes the presenting mode that was actually entered, which is not always the one
		 * that was asked for - see the note on StatsSyncMode.
		 *
		 * The VRR case names its preconditions rather than claiming the rate is being varied, because
		 * that is the honest split. Three of the four things a variable refresh rate needs can be
		 * established from in here: the window is exclusive, vsync is on (a FIFO present mode), and the
		 * panel's own mode is above the rate being presented at. The fourth - whether the driver then
		 * actually scans the panel in step - cannot be observed at all, and the reason is worth stating
		 * because it is not merely an API gap:
		 *
		 *   The emulator paces itself below the panel's maximum rate, so the swapchain always has a free
		 *   image and vkQueuePresentKHR returns immediately. VK_KHR_present_wait does report when a
		 *   submitted image reaches the display, and that is printed below as "Scanned" - but it is the
		 *   rate at which our own presents were scanned out, which equals the rate we present at whether
		 *   the panel is varying or scanning on a fixed schedule. Whether the panel itself is following
		 *   the window is a property of the display, and still not observable from in here.
		 *
		 * (Were the emulator to present faster than the panel, FIFO would throttle it and the measured
		 * frame rate would fall to the panel's rate - which is how a frame-rate display normally reveals
		 * a vsync limit. Deliberately pacing below the panel removes that signal.)
		 *
		 * Naming the preconditions rather than the outcome also tells the reader what to check when the
		 * picture is not smooth: if all three hold and there is still judder, the thing to look at is
		 * the driver's own G-SYNC setting or the panel's on-screen readout of its live refresh rate,
		 * neither of which this program can inspect.
		 *
		 * Rate puts the emulator's own pacing rate next to the arcade's, with the difference between
		 * them worked out - which is the thing worth knowing and the one figure that cannot be read off
		 * any single setting. Model 3 hardware runs at 57.524160 Hz, so a target of 60 means the game is
		 * emulated about 4.3% fast, and seeing that number is rather the point: it is not a fault, but it
		 * is a choice, and one worth making deliberately.
		 */
		// Taken from the pushed value rather than read here, so that the rate the readout prints and the
		// rate the cadence check below compares against can never be two different numbers.
		const float targetHz = g_targetHz;
		const char* syncMode = "windowed";
		switch (g_syncMode)
		{
		case StatsSyncMode::ExclusiveVrr:       syncMode = "exclusive VRR (preconditions met)"; break;
		case StatsSyncMode::ExclusiveFixedRate: syncMode = "exclusive fixed rate"; break;
		case StatsSyncMode::Borderless:         syncMode = "borderless (panel keeps its own rate)"; break;
		default:                                break;
		}
		StatsPair(palette, "Sync", palette.value, "%s", syncMode);

		// Guarded because a zero here would divide the delta into meaninglessness; the configuration
		// range keeps it between 57.524 and 60, but a hand-edited file need not.
		//
		// Two decimals rather than one, because the arcade rate is not a round number and the whole
		// point of the line is the small difference: 60 against 57.5242 is +4.3%, and a preset that is
		// a hair off would read as "-0.0%" at one decimal, which looks like a formatting bug. The
		// arcade preset now stores the exact rate, so it prints +0.00% rather than -0.04%.
		if (targetHz > 0.0f)
		{
			const double deltaPercent = 100.0 * ((double)targetHz - (double)kArcadeHz) / (double)kArcadeHz;
			StatsPair(palette, "Rate", palette.value, "%.2f Hz  arcade %.2f  (%+.2f%%)",
				targetHz, kArcadeHz, deltaPercent);
		}


		if (g_display.valid)
		{
			StatsPair(palette, "Panel", palette.value, "%dx%d  %d Hz mode",
				g_display.width, g_display.height, g_display.refreshHz);

			/*
			 * What is actually being rendered, next to what the panel is being fed.
			 *
			 * The Panel line above says how big the picture is; this says how much work is going into
			 * making it, which is the other half of "why is it slow" and the only place the answer is
			 * visible. Both halves matter because they are not the same number in any of the three
			 * cases that arise:
			 *
			 *   supersampled OpenGL      the render buffer is a multiple of the output
			 *   MSAA Vulkan              same resolution, several samples per pixel
			 *   fullscreen (either)      the buffer follows the display, not XResolution
			 *
			 * Named the way the settings window names them (Supersampling / MSAA), because a reader
			 * comparing the two has to be able to line them up: "the setting says 4x, the readout says
			 * 1x" is the observation worth being able to make, and it happens when a driver refuses the
			 * sample count or when Vulkan is running and the Supersampling setting is being ignored.
			 */
			if (g_renderInfo.width > 0)
			{
				char renderText[160] = {};
				switch (g_renderInfo.method)
				{
				case StatsAntiAliasing::Supersampling:
					std::snprintf(renderText, sizeof(renderText), "%ux%u  SSAA %dx",
						g_renderInfo.width, g_renderInfo.height, g_renderInfo.samples);
					break;
				case StatsAntiAliasing::Msaa:
					std::snprintf(renderText, sizeof(renderText), "%ux%u  MSAA %dx",
						g_renderInfo.width, g_renderInfo.height, g_renderInfo.samples);
					break;
				default:
					std::snprintf(renderText, sizeof(renderText), "%ux%u  no AA",
						g_renderInfo.width, g_renderInfo.height);
					break;
				}

				/*
				 * What all that rendering is finally reduced to, shown only when it is not the same
				 * number - which is exactly the supersampled case. Printing it always would put two
				 * identical lines on screen for every other configuration, which reads as a mistake.
				 */
				if (g_renderInfo.outputWidth > 0 &&
					(g_renderInfo.outputWidth != g_renderInfo.width || g_renderInfo.outputHeight != g_renderInfo.height))
				{
					char scaled[64] = {};
					std::snprintf(scaled, sizeof(scaled), "  -> %ux%u",
						g_renderInfo.outputWidth, g_renderInfo.outputHeight);
					std::strncat(renderText, scaled, sizeof(renderText) - std::strlen(renderText) - 1);
				}

				StatsPair(palette, "Render", palette.value, "%s", renderText);
			}

			/*
			 * How the panel's rate and the emulated rate relate, in numbers rather than felt as judder.
			 *
			 * In borderless the panel normally keeps its own rate, so each emulated frame is shown a
			 * whole number of times or an uneven one, depending on whether the panel rate divides by the
			 * emulated rate. Both figures are already on screen, but the arithmetic between them is what
			 * turns "this looks slightly jerky" into a fact with a cause and a fix.
			 *
			 * Exclusive VRR reaches the same arithmetic from the other side, which is why it is reported
			 * here too. SDL can only report the mode the panel is *configured* at - the instantaneous
			 * scan rate is not visible to an application - so the same division describes what would
			 * happen if VRR were not engaging, and says nothing at all if it is. A session like that is
			 * perfect or uneven depending on something unmeasurable, so it is stated as such rather than
			 * left silent, which is what it used to be: a 60 Hz panel claiming "exclusive VRR
			 * (preconditions met)" and no mention of the 1.04x repetition underneath it.
			 *
			 * The even case needs no hedge at all, which is worth noticing: if the panel's own rate is a
			 * whole multiple of the emulated rate then the session is even whichever way VRR goes.
			 */
			const bool exclusiveVrr = (g_syncMode == StatsSyncMode::ExclusiveVrr);

			/*
			 * A phase-locked mode is even by construction and must not be classified.
			 *
			 * The panel is at 120 Hz and the target is 115.05, so the division below would
			 * report 1.04x and an uneven cadence - the opposite of the truth: every
			 * emulated frame is shown exactly twice, and the panel scans at its own rate
			 * with one scan spare. The arithmetic that describes borderless fullscreen
			 * does not apply to a mode that names its own cadence.
			 */
			if (g_pace.valid && g_pace.phaseLocked && g_pace.phaseCount > 0)
			{
				StatsPair(palette, "Cadence", palette.good, "even (%dx locked)", g_pace.phaseCount);
			}
			else
			{
			double ratio = 0.0;
			const Cadence cadence = ClassifyCadence(ratio);
			const ImVec4& cadenceColour = cadence == Cadence::Even ? palette.good
				: (cadence == Cadence::Uneven ? palette.warn : palette.value);
			if (cadence == Cadence::Even)
			{
				/*
				 * One line, in the same terse style as the rest of the readout.
				 *
				 * 1x is written as "1:1" rather than "1x": it is the ideal case - every frame shown
				 * exactly once - and a bare multiple reads as arithmetic rather than as the good
				 * outcome it is. It is also the common configuration, a 60 Hz panel at a 60 Hz pace.
				 */
				const bool oneToOne = std::fabs(ratio - 1.0) <= 0.01;

				if (oneToOne)
					StatsPair(palette, "Cadence", cadenceColour, "even (1:1)");
				else
					StatsPair(palette, "Cadence", cadenceColour, "even (%.0fx)", ratio);
			}
			else if (cadence == Cadence::Uneven)
			{
				/*
				 * One line per case, with the qualifier kept because it changes what the number means:
				 * under borderless the panel's rate is what is being measured, under exclusive VRR it is
				 * what *would* apply if the display were not varying. Everything else this used to spell
				 * out - why frames repeat, that a custom timing would fix it, that the panel's own rate
				 * is only an assumption - is in the settings window's tooltips and the documentation.
				 *
				 * A stats overlay is read at a glance while playing. Prose here does not get read; it
				 * gets in the way of the numbers that do. The Sync line already carries the assumption
				 * ("borderless (panel keeps its own rate)"), so repeating it here was redundancy as well
				 * as noise.
				 */
				if (exclusiveVrr)
				{
					StatsPair(palette, "Cadence", cadenceColour,
						"uneven (%.2fx)  unless VRR is engaging", ratio);
				}
				else
				{
					/*
					 * The rate that would be even, when there is one, on the same line as the verdict.
					 * Named in Hz because that is what the driver's custom resolution dialog asks for,
					 * and marked as the target rather than as a fix: it only helps if the panel accepts
					 * the timing, which the readout cannot know.
					 *
					 * Borderless only. In exclusive VRR the reader already has a route that needs no
					 * custom timing, and offering a resolution to create next to "let the panel follow
					 * the emulator" argues for the more complicated of the two.
					 */
					const std::pair<int, double> multiple = NearestMultiple();
					if (multiple.first > 0)
					{
						StatsPair(palette, "Cadence", cadenceColour,
							"uneven (%.2fx)  even at %.2f Hz", ratio, multiple.second);
					}
					else
					{
						StatsPair(palette, "Cadence", cadenceColour,
							"uneven (%.2fx)", ratio);
					}
				}
			}
			}
		}

		/*
		 * The CPU line reports the fast cores, and says so.
		 *
		 * On a hybrid part the clock, the count and the load all describe the performance cores rather
		 * than the whole package, because that is the population the emulator is scheduled on and the
		 * one that decides whether it keeps up. The composition is printed alongside so the subsetting
		 * is visible rather than something the reader has to infer from the numbers looking smaller than
		 * they remember.
		 *
		 * Both clocks are printed. The average is what the package is doing; the peak is what the busiest
		 * core reached, which on a laptop with one thread of emulation is the figure that matters and is
		 * usually twice the average - a single number here was read as "this CPU will not boost".
		 */
		if (g_cpu.totalThreads > 0)
		{
			char figures[160];
			if (g_cpu.valid && g_cpu.hybrid)
				std::snprintf(figures, sizeof(figures), "P %u MHz  peak %u  %uP+%uE threads  %u%%",
					g_cpu.currentMhz, g_cpu.peakMhz, g_cpu.performanceThreads,
					g_cpu.efficiencyThreads, g_cpu.loadPercent);
			else if (g_cpu.valid)
				std::snprintf(figures, sizeof(figures), "%u MHz  peak %u  %u threads  %u%%",
					g_cpu.currentMhz, g_cpu.peakMhz, g_cpu.performanceThreads, g_cpu.loadPercent);
			else if (g_cpu.hybrid)
				std::snprintf(figures, sizeof(figures), "-  %uP+%uE threads  %u%%",
					g_cpu.performanceThreads, g_cpu.efficiencyThreads, g_cpu.loadPercent);
			else
				std::snprintf(figures, sizeof(figures), "-  %u threads  %u%%",
					g_cpu.totalThreads, g_cpu.loadPercent);
			StatsPairNamed(palette, "CPU", g_cpu.name, palette.value, figures);
		}

		if (g_gpu.valid)
		{
			/*
			 * Built field by field because which ones exist depends on how the load was obtained: NVML
			 * supplies clock, load and video memory, while the Windows GPU Engine counter supplies the
			 * load alone. Printing a missing figure as zero would read as a measurement.
			 */
			char figures[192];
			std::size_t used = 0;
			figures[0] = '\0';
			if (g_gpu.clockMhz > 0)
				used = (std::size_t)std::snprintf(figures, sizeof(figures), "%u MHz  ", g_gpu.clockMhz);
			used += (std::size_t)std::snprintf(figures + used, sizeof(figures) - used, "%u%%", g_gpu.loadPercent);
			if (g_gpu.vramTotalGb > 0.0)
			{
				if (used >= sizeof(figures))
					used = sizeof(figures) - 1;
				const std::string vram = SizeText(g_gpu.vramUsedGb) + " / " + SizeText(g_gpu.vramTotalGb);
				std::snprintf(figures + used, sizeof(figures) - used, "  VRAM %s", vram.c_str());
			}
			StatsPairNamed(palette, "GPU", g_gpu.name, palette.value, figures);
		}

		if (g_mem.valid)
			StatsPair(palette, "RAM", palette.value, "%s",
				SizeText(g_mem.workingSetGb).c_str());

		/*
		 * The output buffer and the level it is being fed at.
		 *
		 * A fill level drifting towards zero is the stutter that is about to happen; one pinned at the
		 * capacity is the latency that has already built up. It is shown as a range over the last two
		 * seconds because a single sample of a sawtooth says very little: the minimum is what the next
		 * under-run depends on, and a minimum that keeps falling is visible here before the counter
		 * moves. The counters beside it only record that something already went wrong.
		 *
		 * The peak pair is measured before the mute, so a silent game and a muted output look
		 * different: the first shows a rolling 0%, the second keeps its level and says MUTED. The
		 * figure is the sound board's own output, before balance and volume, so it also states whether
		 * there was anything to hear in the first place when the user reports "no sound".
		 */
		if (g_audio.valid)
		{
			char fill[32];
			if (g_audio.fillMinMs == g_audio.fillMaxMs)
				std::snprintf(fill, sizeof(fill), "%u", g_audio.fillMaxMs);
			else
				std::snprintf(fill, sizeof(fill), "%u-%u", g_audio.fillMinMs, g_audio.fillMaxMs);

			// Full scale is 32768 in the sound board's domain; the cap only keeps a runaway figure
			// from widening the panel.
			const unsigned peakLeft = (unsigned)(std::min(999.0f, g_audio.peakLeft * 100.0f) + 0.5f);
			const unsigned peakRight = (unsigned)(std::min(999.0f, g_audio.peakRight * 100.0f) + 0.5f);

			char figures[192];
			std::snprintf(figures, sizeof(figures), "%s / %u ms  %u ch  L %u%%  R %u%%%s  u%u/o%u",
				fill, g_audio.bufferSizeMs, g_audio.channels, peakLeft, peakRight,
				g_audio.muted ? "  MUTED" : "", g_audio.underRuns, g_audio.overRuns);
			StatsPair(palette, "Audio", g_audio.muted ? palette.warn : palette.value, "%s", figures);
		}

		StatsPair(palette, "FPS", FpsColour(palette, g_fps), "%.1f", g_fps);
		if (g_fps > 0.0)
			StatsPair(palette, "Frame", FpsColour(palette, g_fps), "%.2f ms", 1000.0 / g_fps);
		StatsPair(palette, "1% low", FpsColour(palette, g_fpsLow1), "%.1f", g_fpsLow1);

		/*
		 * Emulated rate against presented rate, which is the whole point of the fixed-step pacer and
		 * is otherwise invisible: the picture looks right whether the emulator is presenting at the
		 * panel's rate or collapsed onto its own, so the only way to tell is to print both.
		 *
		 * Shown only when the pacer is actually pacing; the legacy per-frame loop has nothing to say
		 * here and a zeroed line would read as a measurement rather than as an absence.
		 */
		if (g_pace.valid && g_pace.presentHz > 0.0)
		{
			if (g_pace.phaseLocked)
			{
				/*
				 * Phase-locked: four short lines instead of two, because there are
				 * four independent things to check and a combined line would make
				 * any one of them hard to read at a glance.
				 *
				 *   Pacer   which multiple, what the panel is doing
				 *   Sim     the emulated rate and the weight this present used
				 *   Phase   the slot, and the fixed weights the mode cycles
				 *   Sub     what a present that carries no simulation costs
				 *
				 * The weights are printed in full because they are constants: a reader
				 * comparing two runs of the same mode should see the same numbers.
				 */
				const float panel = g_pace.panelHz > 0.0 ? (float)g_pace.panelHz : (float)g_pace.targetHz;
				StatsPair(palette, "Pacer", palette.good, "%s  %.1f / %.0f Hz  locked",
					g_pace.modeLabel, g_pace.presentHz, panel);
				StatsPair(palette, "Sim", palette.value, "%.2f Hz  a %.3f",
					g_pace.logicHz, g_pace.alpha);

				char phases[128];
				std::size_t used = 0;
				phases[0] = '\0';
				for (int i = 0; i < g_pace.phaseCount && i < 8 && used + 8 < sizeof(phases); i++)
					used += (std::size_t)std::snprintf(phases + used, sizeof(phases) - used,
						"%s%.2f", i ? " " : "", g_pace.phaseAlpha[i]);
				StatsPair(palette, "Phase", palette.value, "%d/%d  %s",
					g_pace.phaseIndex + 1, g_pace.phaseCount, phases);

				if (g_pace.subFrameMs > 0.0)
					StatsPair(palette, "Sub", palette.value, "%.2f ms", g_pace.subFrameMs);
			}
			else
			{
				StatsPair(palette, "Pace", palette.value, "%.2f / %.1f Hz  %.2fx  a %.2f",
					g_pace.logicHz, g_pace.presentHz, g_pace.ratio, g_pace.alpha);
				StatsPair(palette, "Interp", g_pace.interpolating ? palette.good : palette.value,
					g_pace.interpolating ? "on  cuts %u" : "off  cuts %u", g_pace.cuts);
			}
		}

		/*
		 * Reported once per enable, and deliberately at the end so it can carry the figures as well as
		 * the placement. Both halves earn their place in a log: the placement depends on a viewport this
		 * module does not own, and the figures on configuration it does not own either - so when either
		 * looks wrong, this is the line that says which of the two moved.
		 */
		if (!g_placedLogged)
		{
			g_placedLogged = true;
			InfoLog("Stats OSD: viewport %.0fx%.0f at (%.0f,%.0f) -> panel %.0fx%.0f at (%.0f,%.0f); "
				"sync %s, rate %.4f Hz (arcade %.4f), panel mode %dx%d@%d Hz.",
				viewport->WorkSize.x, viewport->WorkSize.y, viewport->WorkPos.x, viewport->WorkPos.y,
				ImGui::GetWindowSize().x, ImGui::GetWindowSize().y,
				viewport->WorkPos.x + pad, viewport->WorkPos.y + pad,
				syncMode, targetHz, kArcadeHz,
				g_display.width, g_display.height, g_display.refreshHz);

			/*
			 * The Render line, logged with the rest.
			 *
			 * Worth having in the log rather than only on screen for the same reason the panel rate is:
			 * it is what someone asking "why is this slow, or why is MSAA not helping" needs, and the
			 * on-screen readout is off by default and unreadable in a screenshot. It also records what
			 * was *granted* rather than requested - a driver can refuse the MSAA level, and Vulkan
			 * ignores the Supersampling setting outright.
			 */
			if (g_renderInfo.width > 0)
			{
				const char* aaName = g_renderInfo.method == StatsAntiAliasing::Supersampling ? "SSAA"
					: (g_renderInfo.method == StatsAntiAliasing::Msaa ? "MSAA" : "none");
				InfoLog("Stats OSD: render %ux%u, anti-aliasing %s %dx%s%ux%u.",
					g_renderInfo.width, g_renderInfo.height, aaName, g_renderInfo.samples,
					(g_renderInfo.outputWidth > 0 &&
					 (g_renderInfo.outputWidth != g_renderInfo.width || g_renderInfo.outputHeight != g_renderInfo.height))
						? ", scaled to " : " at ",
					g_renderInfo.outputWidth, g_renderInfo.outputHeight);
			}

			// The core split, because a hybrid CPU is the one figure that cannot be judged from a
			// screenshot alone: whether a machine has performance and efficiency cores, and how many of
			// each, decides whether the numbers above it are describing the whole package or a subset.
			// Saying which it is turns a suspicious reading into an explainable one.
			InfoLog("Stats OSD: CPU %s - %u performance + %u efficiency logical processors of %u total; "
				"load and clock%s measured over the performance ones.",
				g_cpu.hybrid ? "is hybrid" : "is homogeneous",
				g_cpu.hybrid ? g_cpu.performanceThreads : g_cpu.totalThreads,
				g_cpu.hybrid ? g_cpu.efficiencyThreads : 0,
				g_cpu.totalThreads,
				g_cpu.hybrid ? "" : " not");

			/*
			 * The cadence verdict, in the log as well as on screen.
			 *
			 * It is the readout's only line that names a fixable problem, and the panel rate behind it
			 * cannot be read from a screenshot - so without this the answer to "why does borderless
			 * judder on this machine" would only exist on a monitor someone has to photograph. Logged
			 * once per session with the rest of the placement, for the same reason.
			 *
			 * The mode is named in the message because the same number means different things in the two
			 * it can be reached in: under borderless it describes what is happening, and under exclusive
			 * VRR it describes what would be happening if VRR were not engaging. A support log read
			 * without that distinction would not say which.
			 */
			const bool loggedExclusiveVrr = (g_syncMode == StatsSyncMode::ExclusiveVrr);

			double ratio = 0.0;
			const Cadence cadence = ClassifyCadence(ratio);
			if (cadence == Cadence::Even)
			{
				InfoLog("Stats OSD: cadence even (%s) - %.4f panel scans per frame.",
					loggedExclusiveVrr ? "exclusive VRR, holds either way" : "borderless, panel at own rate",
					ratio);
			}
			else if (cadence == Cadence::Uneven)
			{
				const std::pair<int, double> multiple = NearestMultiple();
				const bool suggest = !loggedExclusiveVrr && multiple.first > 0;

				if (suggest)
					InfoLog("Stats OSD: cadence uneven (borderless, panel at own rate) - %.4f panel scans per "
						"frame; even at %dx = %.4f Hz.",
						ratio, multiple.first, multiple.second);
				else
					InfoLog("Stats OSD: cadence uneven (%s) - %.4f panel scans per frame.",
						loggedExclusiveVrr ? "exclusive VRR, only if VRR is not engaging" : "borderless, panel at own rate",
						ratio);
			}
		}
	}
	ImGui::End();

	if (shadowStyle)
	{
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void StatsOsdSetWindow(OSD::Platform::Window* window)
{
	g_window = window;
	g_display = DisplayStats();
}

void StatsOsdSetSyncMode(StatsSyncMode mode)
{
	g_syncMode = mode;
}

void StatsOsdSetRenderer(StatsRenderer renderer)
{
	g_renderer = renderer;
}

void StatsOsdSetGpuName(const char* name)
{
	if (name == nullptr || name[0] == '\0')
		return;

	// Kept short: this shares the readout's widest line with the figures. A driver's full device name is
	// well under this ("Intel(R) UHD Graphics 620"); the truncation only guards against a vendor string
	// that is not.
	std::snprintf(g_gpu.name, sizeof(g_gpu.name), "%s", name);
}

void StatsOsdSetTargetRate(float hz)
{
	g_targetHz = hz;
}

void StatsOsdSetPaceInfo(const StatsPaceInfo& info)
{
	g_pace = info;
}

bool StatsOsdBorderlessJudderPending()
{
	/*
	 * Only the composited paths have the problem this names. An exclusive session holds one panel
	 * scan per frame - either VRR tracking the emulator or a fixed 57/58 Hz mode - so an uneven
	 * panel-scan ratio there describes a state that cannot occur. The ratio is still logged with the
	 * placement line for support purposes; what is suppressed is the "switch to an exclusive mode"
	 * notice, which in an exclusive session would be advising the mode already in use.
	 */
	if (g_syncMode == StatsSyncMode::ExclusiveVrr || g_syncMode == StatsSyncMode::ExclusiveFixedRate)
		return false;

	double ratio = 0.0;
	return ClassifyCadence(ratio) == Cadence::Uneven;
}

int StatsOsdPanelRefreshHz()
{
	return g_display.valid ? g_display.refreshHz : 0;
}

void StatsOsdSetRenderInfo(const StatsRenderInfo& info)
{
	g_renderInfo = info;
}

bool StatsOsdEnabled() { return g_enabled; }

void StatsOsdSetEnabled(bool enabled)
{
	if (enabled == g_enabled)
		return;
	g_enabled = enabled;
	g_stateChanged = true;

	// Sampling starts from scratch, so turning it on cannot show a figure computed from the gap while
	// it was off - the frame-time history would otherwise hold one enormous interval.
	g_lastFrameTicks = 0;
	g_historyCount = 0;
	g_historyNext = 0;
	g_fpsWindowStart = 0;
	g_fpsWindowFrames = 0;
	g_fps = 0.0;
	g_fpsLow1 = 0.0;
	g_lastSlowSample = 0;
	g_placedLogged = false;
}

void StatsOsdToggle() { StatsOsdSetEnabled(!g_enabled); }

void StatsOsdApplyConfig(bool enabled)
{
	g_enabled = enabled;
	g_stateChanged = false;
	g_lastFrameTicks = 0;
	g_historyCount = 0;
	g_historyNext = 0;
	g_fpsWindowStart = 0;
	g_fpsWindowFrames = 0;
	g_placedLogged = false;
}

bool StatsOsdConsumeStateChanged()
{
	const bool changed = g_stateChanged;
	g_stateChanged = false;
	return changed;
}

/*
 * Called when the session ends.
 *
 * NVML keeps a handle and a loaded library alive for as long as the process runs, which is harmless
 * but means a later session would find g_gpuProbed already set and skip the probe. Releasing here
 * makes each session start cleanly.
 */
void StatsOsdShutdown()
{
	ShutdownNvml();
	g_gpuProbed = false;
	ResetSlowSampleWarmup();
	g_cpu = CpuStats();
	g_gpu = GpuStats();
	g_mem = MemStats();
	g_audio = AudioStats();
	g_display = DisplayStats();
	g_audioFillCount = 0;
	g_audioFillNext = 0;
}
