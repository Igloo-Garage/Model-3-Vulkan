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

#include "OSD/Logger.h"
#include "Util/NewConfig.h"

#include "../Pkgs/imgui/imgui.h"
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
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
SDL_Window* g_window = nullptr;
StatsSyncMode g_syncMode = StatsSyncMode::Windowed;
StatsRenderer g_renderer = StatsRenderer::New3D;
float g_targetHz = 0.0f;

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
	unsigned maxMhz = 0;
	unsigned loadPercent = 0;

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
DisplayStats g_display;
bool g_gpuProbed = false;

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
				g_topology.maxMhz.reserve(count);
				for (const auto& one : info)
					g_topology.maxMhz.push_back(one.maxMhz);
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
					// Only processor entries are wanted; the API also reports cache and NUMA sets.
					if (entry->type == 0)
					{
						entries.push_back({ (unsigned)entry->id, entry->efficiencyClass });
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
	 * Clock and load over the fast cores only.
	 *
	 * Both are the reason this exists. An average across every core understates the boost the emulation
	 * thread actually runs at, and a load across every core understates how busy the cores it uses are -
	 * sixteen idle efficiency cores turn a saturated pair of performance cores into a comfortable
	 * looking percentage.
	 */
	if (!g_topology.performanceIds.empty())
	{
		if (PFN_CallNtPowerInformation call = LoadPowerApi())
		{
			// Processor entries are indexed by logical processor number, so the whole array is read and
			// then sampled at the ids that matter.
			const unsigned count = total > 0 ? total : (unsigned)g_topology.performanceIds.size();
			std::vector<ProcessorPowerInformation> info(count);
			if (call(kProcessorInformation, nullptr, 0, info.data(),
				(unsigned long)(info.size() * sizeof(ProcessorPowerInformation))) == 0)
			{
				unsigned long long sum = 0;
				unsigned seen = 0;
				unsigned maxSeen = 0;
				for (unsigned index : g_topology.performanceIds)
				{
					if (index >= info.size())
						continue;
					sum += info[index].currentMhz;
					if (info[index].maxMhz > maxSeen)
						maxSeen = info[index].maxMhz;
					++seen;
				}
				if (seen > 0)
				{
					g_cpu.currentMhz = (unsigned)(sum / seen);
					g_cpu.maxMhz = maxSeen;
					g_cpu.valid = true;
				}
			}
		}
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
	InfoLog("Stats OSD: NVML available, GPU figures will be shown.");
}

void SampleGpu()
{
	if (!g_gpuProbed)
		ProbeNvml();
	if (!g_nvml.ready)
		return;

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

#else
// Other platforms: the frame, CPU-thread and memory figures still work, the vendor-specific ones do
// not. Kept as no-ops rather than #ifdef'ing the call sites, so the readout's logic reads the same
// everywhere.
void SampleCpu() {}
void SampleMemory() {}
void SampleGpu() {}
void ShutdownNvml() {}
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

	const int display = SDL_GetWindowDisplayIndex(g_window);
	SDL_DisplayMode mode = {};
	if (SDL_GetDesktopDisplayMode(display < 0 ? 0 : display, &mode) == 0)
	{
		g_display.refreshHz = mode.refresh_rate;
		g_display.width = mode.w;
		g_display.height = mode.h;
		g_display.valid = true;
	}
}

void SampleSlowStats()
{
	SampleCpu();
	SampleMemory();
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

} // anonymous namespace

void StatsOsdFrameTick()
{
	const uint64_t now = SDL_GetPerformanceCounter();
	const uint64_t freq = SDL_GetPerformanceFrequency();
	if (freq == 0)
		return;

	if (g_lastFrameTicks != 0)
	{
		const double ms = (double)(now - g_lastFrameTicks) * 1000.0 / (double)freq;
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

	const double windowMs = (double)(now - g_fpsWindowStart) * 1000.0 / (double)freq;
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

	const double sampleMs = (double)(now - g_lastSlowSample) * 1000.0 / (double)freq;
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
	ImGui::SetNextWindowBgAlpha(0.35f);

	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	// Same amber the interface uses for a line that should be noticed; see kAttentionColour in Gui.cpp.
	const ImVec4 colour(1.0f, 0.72f, 0.30f, 1.0f);

	if (ImGui::Begin("##StatsOsd", nullptr, flags))
	{
		ImGui::TextColored(colour, "Renderer: %s", RendererName());

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
		 *   The emulator paces itself below the panel's maximum rate. Production is therefore always
		 *   slower than consumption, so the swapchain always has a free image, vkAcquireNextImageKHR
		 *   never blocks and vkQueuePresentKHR returns immediately. The only timing the program can
		 *   measure is its own sleep between frames, and that is identical whether the panel is varying
		 *   or scanning on a fixed schedule - in the fixed case the picture is simply presented
		 *   repeatedly. The difference happens between submit and scan-out, which no application can see.
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
		(void)config;
		const float targetHz = g_targetHz;
		const char* syncMode = "windowed";
		switch (g_syncMode)
		{
		case StatsSyncMode::ExclusiveVrr:       syncMode = "exclusive VRR (preconditions met)"; break;
		case StatsSyncMode::ExclusiveFixedRate: syncMode = "exclusive fixed rate"; break;
		case StatsSyncMode::Borderless:         syncMode = "borderless (panel keeps its own rate)"; break;
		default:                                break;
		}
		ImGui::TextColored(colour, "Sync: %s", syncMode);

		// Guarded because a zero here would divide the delta into meaninglessness; the configuration
		// range keeps it between 57.5 and 60, but a hand-edited file need not.
		//
		// Two decimals rather than one, because the arcade rate is not a round number and the whole
		// point of the line is the small difference: 57.5 against 57.5242 is -0.04%, and at one decimal
		// that reads as "-0.0%" - which looks like a formatting bug rather than the residual error of a
		// dropdown that can only offer 57.5.
		if (targetHz > 0.0f)
		{
			const double deltaPercent = 100.0 * ((double)targetHz - (double)kArcadeHz) / (double)kArcadeHz;
			ImGui::TextColored(colour, "Rate: %.2f Hz  arcade %.2f  (%+.2f%%)",
				targetHz, kArcadeHz, deltaPercent);
		}


		if (g_display.valid)
		{
			ImGui::TextColored(colour, "Panel: %dx%d  %d Hz mode",
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
				switch (g_renderInfo.method)
				{
				case StatsAntiAliasing::Supersampling:
					ImGui::TextColored(colour, "Render: %ux%u  SSAA %dx",
						g_renderInfo.width, g_renderInfo.height, g_renderInfo.samples);
					break;
				case StatsAntiAliasing::Msaa:
					ImGui::TextColored(colour, "Render: %ux%u  MSAA %dx",
						g_renderInfo.width, g_renderInfo.height, g_renderInfo.samples);
					break;
				default:
					ImGui::TextColored(colour, "Render: %ux%u  no AA",
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
					ImGui::TextColored(colour, "        scaled to %ux%u",
						g_renderInfo.outputWidth, g_renderInfo.outputHeight);
				}
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

			double ratio = 0.0;
			const Cadence cadence = ClassifyCadence(ratio);
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
					ImGui::TextColored(colour, "Cadence: even (1:1)");
				else
					ImGui::TextColored(colour, "Cadence: even (%.0fx)", ratio);
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
					ImGui::TextColored(colour, "Cadence: uneven (%.2fx)  unless VRR is engaging", ratio);
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
						ImGui::TextColored(colour, "Cadence: uneven (%.2fx)  even at %.2f Hz",
							ratio, multiple.second);
					}
					else
					{
						ImGui::TextColored(colour, "Cadence: uneven (%.2fx)", ratio);
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
		 */
		if (g_cpu.valid && g_cpu.hybrid)
			ImGui::TextColored(colour, "CPU: %u MHz  %uP+%uE threads  %u%%",
				g_cpu.currentMhz, g_cpu.performanceThreads, g_cpu.efficiencyThreads, g_cpu.loadPercent);
		else if (g_cpu.valid)
			ImGui::TextColored(colour, "CPU: %u MHz  %u threads  %u%%",
				g_cpu.currentMhz, g_cpu.performanceThreads, g_cpu.loadPercent);
		else if (g_cpu.totalThreads > 0 && g_cpu.hybrid)
			ImGui::TextColored(colour, "CPU: -  %uP+%uE threads  %u%%",
				g_cpu.performanceThreads, g_cpu.efficiencyThreads, g_cpu.loadPercent);
		else if (g_cpu.totalThreads > 0)
			ImGui::TextColored(colour, "CPU: -  %u threads  %u%%",
				g_cpu.totalThreads, g_cpu.loadPercent);

		if (g_gpu.valid)
		{
			const std::string vram = SizeText(g_gpu.vramUsedGb) + " / " + SizeText(g_gpu.vramTotalGb);
			ImGui::TextColored(colour, "GPU: %u MHz  %u%%  VRAM %s",
				g_gpu.clockMhz, g_gpu.loadPercent, vram.c_str());
		}

		if (g_mem.valid)
			ImGui::TextColored(colour, "RAM: %s", SizeText(g_mem.workingSetGb).c_str());

		ImGui::TextColored(colour, "FPS: %.1f", g_fps);
		ImGui::TextColored(colour, "1%% low: %.1f", g_fpsLow1);

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
}

void StatsOsdSetWindow(SDL_Window* window)
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

void StatsOsdSetTargetRate(float hz)
{
	g_targetHz = hz;
}

bool StatsOsdBorderlessJudderPending()
{
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
	g_cpu = CpuStats();
	g_gpu = GpuStats();
	g_mem = MemStats();
	g_display = DisplayStats();
}
