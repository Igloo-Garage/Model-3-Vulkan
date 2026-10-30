/**
 ** Model 3-Vulkan
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
 **
 ** Additional Terms under GNU GPLv3 Section 7 apply to this file
 ** (see LICENSE-ADDITIONAL-TERMS.md).
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
#include "Overlay.h"
#include "StatsOsd.h"

#include "Gui.h"                    // LoadUiFont(), DrawAdvancedSettingsPanel()
#include "Localization.h"
#if !defined(SUPERMODEL_OPENGL_ONLY)
#include "Graphics/Vulkan3D/VulkanContext.h"
#include "Pkgs/unzip.h"       // reads thumb.png out of a .zst container on hover
#endif
#include "OSD/Logger.h"
#include "Util/Format.h"
#include "Util/NewConfig.h"
#include "Version.h"

#include "../Pkgs/imgui/imgui.h"
#if !defined(SUPERMODEL_PURE_VULKAN)
#include "../Pkgs/imgui/imgui_impl_opengl3.h"
#endif
#if !defined(SUPERMODEL_OPENGL_ONLY)
#include "../Pkgs/imgui/imgui_impl_vulkan.h"
#endif
#include "OSD/Platform/Platform.h"
#include "OSD/Platform/PlatformInput.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if !defined(SUPERMODEL_OPENGL_ONLY)
// Defined next to the save-state menu, but used by OverlayInit()/OverlayShutdown() above it.
bool CreateThumbnailUploadObjects();
void DestroyThumbnailUploadObjects();
void ReleaseAllThumbnails(bool waitForDevice);
#endif

namespace
{

ImGuiContext* g_context = nullptr;
// The context that was current before this one was created. The settings window keeps a context of
// its own for the whole run, and leaving ours current would make every later ImGui call from that
// window operate on the wrong one - which shows up as a blank or misbehaving settings screen rather
// than as an error.
ImGuiContext* g_previousContext = nullptr;

#if !defined(SUPERMODEL_OPENGL_ONLY)
Vulkan3D::CVulkanContext* g_vulkan = nullptr;
uint64_t g_swapchainGeneration = 0;

/*
 * Frosted backdrop plumbing.
 *
 * g_frostSource is installed by the game loop and points at the renderer's half-resolution blurred
 * copy of the frame. g_frostTexture is our own registration of it in this ImGui context: the two
 * contexts (settings window and overlay) do not share a descriptor pool, so the renderer cannot hand
 * out a handle that both can use.
 */
OverlayFrostTextureFn g_frostSource = nullptr;
std::uint32_t g_frostGeneration = 0;
VkSampler g_frostSampler = VK_NULL_HANDLE;
#endif
// Declared in both builds: the panel's plate code has no build switch, and the OpenGL renderer has
// no blur chain, so this simply stays 0 there and the panel falls back to its tint.
ImTextureID g_frostTexture = 0;
bool g_backendReady = false;
bool g_visible = false;
// Mirrors UiFrostedGlass. Read every built frame, so the option can be switched from the panel.
bool g_frostEnabled = true;

/*
 * Which ImGui renderer backend is behind the panel.
 *
 * The panel, the statistics readout, the input handling and the snapshot/apply logic are all shared;
 * only three things differ - how a frame is begun, how the finished draw data is recorded, and what
 * has to be rebuilt when the window changes. Keeping one implementation with a backend tag rather than
 * two copies of the panel is what stops the two drifting apart, which is the usual fate of a second
 * copy of a UI.
 */
enum class OverlayBackend
{
	NoBackend,
	Vulkan,
	OpenGL
};
OverlayBackend g_backend = OverlayBackend::NoBackend;

OSD::Platform::Window* g_window = nullptr;
float g_uiScale = 1.0f;

// Values the panel can edit, captured when it opens so Cancel can put them back. Only these keys are
// recorded rather than the whole configuration: the panel is the only thing that writes them while a
// game runs, and a snapshot of everything would make Cancel able to undo things it never touched.
std::vector<std::pair<std::string, Util::Config::Node>> g_snapshot;
bool g_haveSnapshot = false;

// Input state carried between frames, because ImGui needs edges and the platform layer is otherwise
// stateless.
bool g_mouseDown[3] = {};
bool g_keyDown[512] = {};            // indexed by Win32 virtual key
#ifdef _WIN32
POINT g_lastMouse = { -1, -1 };
#endif

// A frame's worth of ImGui draw data, kept so that building the UI and recording the GPU work can
// happen at different points in the frame. ImGui::Render() is called at the end of NewFrame rather
// than in the render callback so the two cannot get out of step when a frame is skipped - ImGui
// asserts if a frame is begun and never ended, which a minimised window would otherwise cause.
bool g_frameRendered = false;

// Save-state menu callbacks, registered by the game loop for the duration of a session.
OverlaySaveHandlers g_saveHandlers;

/*
 * Mouse wheel accumulated by the platform layer's event drain.
 *
 * The rest of the input here is polled as state (GetAsyncKeyState, GetCursorPos) because the game's
 * input systems can consume or fail to update the platform's state. The wheel has no state to poll -
 * it is an event - so it is read from the per-frame accumulation in Platform::Input(), which the
 * input system's Poll() resets. That drains the queue without consuming anything, so the game still
 * sees the wheel; the panel just adds the same delta to ImGui.
 *
 * This used to be an SDL_AddEventWatch callback. The watch was a leftover from when DirectInput
 * pumped Win32 messages itself and SDL's queue was bypassed; that is no longer true of either
 * backend, and the snapshot is the same value every caller already agrees on.
 */

const char* const kOverlayKeys[] = {
	"CRTcolors",
	"UpscaleMode",
	// Vulkan2D's light-gun border pass; adjustable live from the in-game panel.
	"SindenBorder",
	"SindenBorderWidth",
	"SindenBorderBrightness",
	"WideScreen",
	"PostFXAA",
	"PostCRT",
	"PostScanlines",
	"PostScanlineIntensity",
	"PostMaskType",
	"PostMaskScale",
	"PostMaskIntensity",
	"PostGlow",
	"PostGlowLevels",
	"PostGlowThreshold",
	"PostGain"
};

/*
 * ImGui key to Win32 virtual key for the keys the panel needs.
 *
 * Hand-written rather than using imgui_impl_win32 for two reasons. The first is that SDL's own input
 * state cannot be used: CDirectInputSystem pumps messages itself (it has to, to keep the RawInput
 * handles valid) and so SDL's keyboard and mouse state is never updated while a game is running.
 * Reading the physical state with GetAsyncKeyState sidesteps that entirely and cannot interfere with
 * whichever input system the game is using.
 *
 * The second is that this is a small, fixed set of keys. The panel is sliders, combos and two
 * buttons - it needs arrows, Enter, Escape, Tab, Backspace and the digits for ImGui's ctrl-click
 * value entry, and nothing else.
 *
 * Windows only, along with IsDown() and g_lastMouse above. The table is spelled out in terms of
 * VK_* constants, which exist in no other platform's headers: leaving these declarations at file
 * scope - as they were - broke the Linux and macOS builds with "VK_TAB was not declared" and similar
 * before the compiler ever reached FeedInput(), where the #ifdef was.
 */
#ifdef _WIN32
struct KeyMap
{
	ImGuiKey imgui;
	int vk;
};

const KeyMap kKeyMap[] = {
	{ ImGuiKey_Tab,        VK_TAB },
	{ ImGuiKey_LeftArrow,  VK_LEFT },
	{ ImGuiKey_RightArrow, VK_RIGHT },
	{ ImGuiKey_UpArrow,    VK_UP },
	{ ImGuiKey_DownArrow,  VK_DOWN },
	{ ImGuiKey_PageUp,     VK_PRIOR },
	{ ImGuiKey_PageDown,   VK_NEXT },
	{ ImGuiKey_Home,       VK_HOME },
	{ ImGuiKey_End,        VK_END },
	{ ImGuiKey_Insert,     VK_INSERT },
	{ ImGuiKey_Delete,     VK_DELETE },
	{ ImGuiKey_Backspace,  VK_BACK },
	{ ImGuiKey_Space,      VK_SPACE },
	{ ImGuiKey_Enter,      VK_RETURN },
	{ ImGuiKey_Escape,     VK_ESCAPE },
	// Win32 defines VK_A..VK_Z only from VK_A onward in some SDKs; the letters are simply their
	// ASCII values, which is what the character keys are anyway.
	{ ImGuiKey_A, 'A' }, { ImGuiKey_C, 'C' }, { ImGuiKey_V, 'V' }, { ImGuiKey_X, 'X' },
	{ ImGuiKey_Z, 'Z' },
	{ ImGuiKey_0, '0' }, { ImGuiKey_1, '1' }, { ImGuiKey_2, '2' }, { ImGuiKey_3, '3' },
	{ ImGuiKey_4, '4' }, { ImGuiKey_5, '5' }, { ImGuiKey_6, '6' }, { ImGuiKey_7, '7' },
	{ ImGuiKey_8, '8' }, { ImGuiKey_9, '9' },
	{ ImGuiKey_Keypad0, VK_NUMPAD0 }, { ImGuiKey_Keypad1, VK_NUMPAD1 }, { ImGuiKey_Keypad2, VK_NUMPAD2 },
	{ ImGuiKey_Keypad3, VK_NUMPAD3 }, { ImGuiKey_Keypad4, VK_NUMPAD4 }, { ImGuiKey_Keypad5, VK_NUMPAD5 },
	{ ImGuiKey_Keypad6, VK_NUMPAD6 }, { ImGuiKey_Keypad7, VK_NUMPAD7 }, { ImGuiKey_Keypad8, VK_NUMPAD8 },
	{ ImGuiKey_Keypad9, VK_NUMPAD9 },
	{ ImGuiKey_KeypadDecimal,  VK_DECIMAL },
	{ ImGuiKey_KeypadSubtract, VK_SUBTRACT },
	{ ImGuiKey_Minus,          VK_OEM_MINUS },
	{ ImGuiKey_Period,         VK_OEM_PERIOD },
	{ ImGuiKey_Comma,          VK_OEM_COMMA },
};

bool IsDown(int vk)
{
	return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
#endif  // _WIN32

// Feeds one frame of input into ImGui by reading the physical device state.
void FeedInput()
{
	ImGuiIO& io = ImGui::GetIO();

	if (g_window != nullptr)
	{
		int windowW = 0, windowH = 0;
		OSD::Platform::GetWindowSize(g_window, &windowW, &windowH);
		io.DisplaySize = ImVec2((float)windowW, (float)windowH);
	}
	// No HiDPI framebuffer scaling: the swapchain is the window's pixel size in this renderer.
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

	// Wheel delta for this frame. Read after the input system has polled, so it is this frame's
	// events; discarded while the panel is closed, because scrolling a hidden panel would bank a
	// delta that fires the moment it is opened.
	const float wheel = OSD::Platform::Input().wheelY;
	if (g_visible && wheel != 0.0f)
		io.AddMouseWheelEvent(0.0f, wheel);

#ifdef _WIN32
	HWND hwnd = g_window != nullptr
		? static_cast<HWND>(OSD::Platform::GetNativeWindowHandle(g_window))
		: nullptr;

	POINT cursor = {};
	if (GetCursorPos(&cursor) && hwnd != nullptr)
	{
		ScreenToClient(hwnd, &cursor);
		// Only sent when it moved: ImGui treats a repeated position as the mouse moving, which fights
		// with items that are being dragged.
		if (cursor.x != g_lastMouse.x || cursor.y != g_lastMouse.y)
		{
			g_lastMouse = cursor;
			io.AddMousePosEvent((float)cursor.x, (float)cursor.y);
		}
	}

	const int buttons[3] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
	for (int i = 0; i < 3; ++i)
	{
		const bool down = IsDown(buttons[i]);
		if (down != g_mouseDown[i])
		{
			g_mouseDown[i] = down;
			io.AddMouseButtonEvent(i, down);
		}
	}

	// Modifiers are queued separately from the keys they combine with.
	io.AddKeyEvent(ImGuiMod_Ctrl,  IsDown(VK_CONTROL));
	io.AddKeyEvent(ImGuiMod_Shift, IsDown(VK_SHIFT));
	io.AddKeyEvent(ImGuiMod_Alt,   IsDown(VK_MENU));

	for (const KeyMap& map : kKeyMap)
	{
		const bool down = IsDown(map.vk);
		if (down != g_keyDown[map.vk & 511])
		{
			g_keyDown[map.vk & 511] = down;
			io.AddKeyEvent(map.imgui, down);
		}
	}
#endif
}

void CaptureSnapshot(Util::Config::Node& config)
{
	g_snapshot.clear();
	for (const char* key : kOverlayKeys)
	{
		// Node's copy constructor deep-copies, which is what a snapshot needs: the panel is about to
		// write into config, and a shallow copy would see its own edits. The copy also carries the
		// ValueRange, so the settings window's tab grouping survives being restored.
		Util::Config::Node saved(config[key].GetValue() ? config[key] : Util::Config::Node(key));
		g_snapshot.emplace_back(key, saved);
	}
	g_haveSnapshot = true;
}

void RestoreSnapshot(Util::Config::Node& config)
{
	if (!g_haveSnapshot)
		return;
	for (const auto& entry : g_snapshot)
	{
		if (entry.second.GetValue())
			config.Set(entry.first, entry.second.GetValue()->MakeCopy());
	}
}

} // anonymous namespace

void OverlaySetSaveHandlers(OverlaySaveHandlers handlers)
{
	g_saveHandlers = std::move(handlers);
}

void OverlayClearSaveHandlers()
{
	g_saveHandlers = OverlaySaveHandlers();
}

/*
 * Creates the ImGui context the panel lives in, if it does not exist yet, and makes it current.
 *
 * Shared by both backends: the panel, the readout and the input handling are the same either way, and
 * the context is the part that is most expensive to get wrong - a second context left current would
 * break the settings window, and one created twice would leak a font atlas per call.
 *
 * Returns true when the caller may go on to initialise a renderer backend.
 */
static bool EnsureContext(float uiScale)
{
	if (g_context != nullptr)
	{
		ImGui::SetCurrentContext(g_context);
		return true;
	}

	IMGUI_CHECKVERSION();
	g_previousContext = ImGui::GetCurrentContext();
	g_context = ImGui::CreateContext();
	ImGui::SetCurrentContext(g_context);

	ImGuiIO& io = ImGui::GetIO();
	// No imgui.ini of its own: the panel's geometry is not worth persisting, and a stray file
	// next to the executable would be one more thing to explain.
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ApplyUiTheme();
	LoadUiFont(io, uiScale);
	if (uiScale > 1.05f)
		ImGui::GetStyle().ScaleAllSizes(uiScale);

	return true;
}

#if !defined(SUPERMODEL_OPENGL_ONLY)
bool OverlayInit(Vulkan3D::CVulkanContext& context, OSD::Platform::Window* window, float uiScale)
{
	g_vulkan = &context;
	g_window = window;
	g_uiScale = uiScale;

	// Rebuilt when the swapchain changes: the backend sizes its per-image buffers from the image
	// count it is told, and a stale count is the kind of thing that only misbehaves much later.
	const uint64_t generation = context.SwapchainGeneration();
	if (g_backendReady && g_backend == OverlayBackend::Vulkan && generation == g_swapchainGeneration)
		return true;

	if (g_backendReady)
	{
		ImGui::SetCurrentContext(g_context);
#if !defined(SUPERMODEL_OPENGL_ONLY)
		ReleaseAllThumbnails(true);
		DestroyThumbnailUploadObjects();
#endif
		ImGui_ImplVulkan_Shutdown();
		g_backendReady = false;
	}
	g_backend = OverlayBackend::NoBackend;

	if (!EnsureContext(g_uiScale))
		return false;

	ImGui_ImplVulkan_InitInfo init = {};
	init.Instance = context.Instance();
	init.PhysicalDevice = context.PhysicalDevice();
	init.Device = context.Device();
	init.QueueFamily = context.QueueFamily();
	init.Queue = context.Queue();
	// Shares the game session's persistent pipeline cache (see CVulkanContext).
	init.PipelineCache = context.PipelineCache();
	// DescriptorPool and DescriptorPoolSize are mutually exclusive - the backend asserts on both being
	// set or neither - and letting it build its own pool is one less object to own. The pool holds one
	// descriptor for the font atlas and one per cached save thumbnail, so it has to cover the sixteen
	// slots the menu can show plus a little room to spare.
	init.DescriptorPoolSize = 32;
	init.MinImageCount = 2;
	init.ImageCount = context.SwapImageCount() > 2 ? context.SwapImageCount() : 2;
	init.UseDynamicRendering = true;

	// Dynamic rendering has no render pass, so the attachment format takes its place and the backend
	// refuses to build a pipeline without it. The swapchain format is what the overlay is drawn into.
	VkFormat format = context.SwapFormat();
	init.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
	init.PipelineInfoMain.Subpass = 0;
	init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	init.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;

	if (!ImGui_ImplVulkan_Init(&init))
	{
		ErrorLog("Overlay: the ImGui Vulkan backend could not be initialised. The in-game panel is unavailable.");
		ImGui::SetCurrentContext(g_previousContext);
		return false;
	}

	g_backend = OverlayBackend::Vulkan;
	g_backendReady = true;
	g_swapchainGeneration = generation;

	/*
	 * Sampler for the frosted backdrop.
	 *
	 * Linear filtering is the whole point - the blur chain writes half the swapchain resolution and
	 * the panels are drawn at full resolution, so nearest filtering would show the 2x2 blocks. Clamped
	 * because the panel's uv comes from screen coordinates and can land a fraction of a texel outside
	 * at the very edge of the window; maxLod 0 because the image has no mip chain.
	 */
	if (g_frostSampler == VK_NULL_HANDLE)
	{
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxLod = 0.0f;
		if (vkCreateSampler(context.Device(), &samplerInfo, nullptr, &g_frostSampler) != VK_SUCCESS)
		{
			g_frostSampler = VK_NULL_HANDLE;
			ErrorLog("Overlay: could not create the frosted-backdrop sampler; panels use a plain tint.");
		}
	}

	// Thumbnails are optional: a failure here leaves the menu without hover previews rather than
	// taking the whole panel down.
	if (!CreateThumbnailUploadObjects())
		ErrorLog("Overlay: the save thumbnail uploader could not be created; hover previews are disabled.");

	InfoLog("Overlay: ready (%ux%u, %u swapchain image(s), scale %.2f).",
		context.Extent().width, context.Extent().height, init.ImageCount, g_uiScale);
	return true;
}
#endif

bool OverlayInitGL(OSD::Platform::Window* window, float uiScale)
{
#if defined(SUPERMODEL_PURE_VULKAN)
	(void)window;
	(void)uiScale;
	ErrorLog("Overlay: the OpenGL backend is not part of this executable.");
	return false;
#else
#if !defined(SUPERMODEL_OPENGL_ONLY)
	g_vulkan = nullptr;
#endif
	g_window = window;
	g_uiScale = uiScale;

	if (g_backendReady && g_backend == OverlayBackend::OpenGL)
		return true;

	if (g_backendReady)
	{
		ImGui::SetCurrentContext(g_context);
		ImGui_ImplOpenGL3_Shutdown();
		g_backendReady = false;
	}
	g_backend = OverlayBackend::NoBackend;

	if (!EnsureContext(g_uiScale))
		return false;

	/*
	 * The GLSL version is stated rather than left to the backend's default.
	 *
	 * The game's context is a 4.1 core profile (see CreateGLScreen), and a core profile refuses the
	 * compatibility-profile shaders the backend would otherwise emit - the failure is a shader compile
	 * with no useful message, at the moment the panel is first drawn. The settings window asks for the
	 * same version for the same reason.
	 */
	if (!ImGui_ImplOpenGL3_Init("#version 410"))
	{
		ErrorLog("Overlay: the ImGui OpenGL backend could not be initialised. The in-game panel is unavailable.");
		ImGui::SetCurrentContext(g_previousContext);
		return false;
	}

	g_backend = OverlayBackend::OpenGL;
	g_backendReady = true;
	InfoLog("Overlay: ready on the OpenGL backend (scale %.2f).", g_uiScale);
	return true;
#endif
}

void OverlayShutdown()
{
	if (!g_backendReady && g_context == nullptr)
		return;

	ImGui::SetCurrentContext(g_context);
	if (g_backendReady)
	{
		// The Vulkan path waits for the device to go idle, so it must not be called mid-frame.
#if !defined(SUPERMODEL_OPENGL_ONLY)
		if (g_backend == OverlayBackend::Vulkan)
		{
			// Descriptors belong to the ImGui backend's pool, so they have to go back before it shuts
			// down; the images themselves are owned by CVulkanMemory and are released in the same step.
			ReleaseAllThumbnails(true);
			ImGui_ImplVulkan_Shutdown();
			DestroyThumbnailUploadObjects();
		}
#endif
#if !defined(SUPERMODEL_PURE_VULKAN)
		if (g_backend == OverlayBackend::OpenGL)
			ImGui_ImplOpenGL3_Shutdown();
#endif
		g_backendReady = false;
	}
	g_backend = OverlayBackend::NoBackend;
	ImGui::DestroyContext(g_context);
	g_context = nullptr;
	// Hands the settings window its context back.
	ImGui::SetCurrentContext(g_previousContext);
	g_previousContext = nullptr;
	g_visible = false;
	g_haveSnapshot = false;

#if !defined(SUPERMODEL_OPENGL_ONLY)
	/*
	 * The registered frost descriptor lived in the backend's pool, which has just been destroyed, so
	 * the handle is dead. The sampler outlives the backend (it is created once and only used to
	 * re-register the next session's texture), but it does have to be released here.
	 */
	g_frostTexture = 0;
	g_frostGeneration = 0;
	if (g_frostSampler != VK_NULL_HANDLE && g_vulkan != nullptr)
	{
		if (VkDevice device = g_vulkan->Device())
			vkDestroySampler(device, g_frostSampler, nullptr);
		g_frostSampler = VK_NULL_HANDLE;
	}
#endif
}

bool OverlayVisible()
{
	return g_visible && g_backendReady;
}

bool OverlayActive()
{
	return g_backendReady;
}

void OverlaySetVisible(Util::Config::Node& config, bool visible)
{
	if (visible && !g_backendReady)
		return;

	if (visible == g_visible)
		return;

	if (visible)
	{
		CaptureSnapshot(config);		// The same state the panel is about to change, so a fresh frame does not start with a stale
		// key held down from before it opened.
		std::memset(g_keyDown, 0, sizeof(g_keyDown));
		std::memset(g_mouseDown, 0, sizeof(g_mouseDown));
#ifdef _WIN32
		g_lastMouse.x = -1;
		g_lastMouse.y = -1;
#endif
	}
	g_visible = visible;
}

void OverlayToggle(Util::Config::Node& config)
{
	OverlaySetVisible(config, !g_visible);
}

/*
 * Absolute local time of a save, rather than "5 min ago".
 *
 * The list is rebuilt every frame and carries only the age, so the wall-clock time is recovered by
 * subtracting it from now. A second of drift between the two is invisible at minute resolution, and
 * this avoids adding a second timestamp field to the callback structure for the same information.
 */
static const char* FormatSaveTime(double ageSeconds)
{
	static char buffer[32];
	const auto saved = std::chrono::system_clock::now() -
		std::chrono::duration_cast<std::chrono::system_clock::duration>(
			std::chrono::duration<double>(ageSeconds));
	const std::time_t time = std::chrono::system_clock::to_time_t(saved);
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &time);
#else
	localtime_r(&time, &local);
#endif
	std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H:%M", &local);
	return buffer;
}

/*
 * Save-state rows.
 *
 * Kept out of DrawAdvancedSettingsPanel because that function is also the settings window's page;
 * saves only exist while a game runs, and the handlers are what says so. The buttons act immediately
 * - they are not part of the Apply/Cancel snapshot, which is about configuration values.
 */

#if !defined(SUPERMODEL_OPENGL_ONLY)
/*
 * Hover previews.
 *
 * The thumb.png entry is read straight out of the .zst container with minizip, which is already part
 * of the core for ROM archives. This keeps the overlay independent of HostCore and avoids adding a
 * "get one thumbnail" call to the C ABI: only the row under the pointer is touched, and only until
 * its texture has been uploaded. The texture is cached until the file's write time changes, so
 * moving the pointer across the table does not decode the same PNG again.
 */
struct ThumbnailTexture
{
	std::filesystem::path path;
	std::uint64_t revision = 0;
	bool attempted = false;
	ImTextureID texture = 0;
	Vulkan3D::AllocatedImage image{};
	int width = 0;
	int height = 0;
};

std::unordered_map<unsigned, ThumbnailTexture> g_thumbnailTextures;

/*
 * Textures that were replaced while a previous frame may still be sampling them. ImGui frees a
 * descriptor set immediately on RemoveTexture, so removing it the moment the pointer leaves a row
 * could hand the GPU a descriptor that is no longer valid. They are held for a few frames instead
 * and released once the queue has certainly moved on; shutdown waits for the device and flushes
 * the list outright.
 */
struct RetiredThumbnail
{
	ImTextureID texture = 0;
	Vulkan3D::AllocatedImage image{};
	std::uint64_t frame = 0;
};

std::vector<RetiredThumbnail> g_retiredThumbnails;
std::uint64_t g_thumbnailFrame = 0;
VkSampler g_thumbnailSampler = VK_NULL_HANDLE;
VkCommandPool g_thumbnailPool = VK_NULL_HANDLE;
VkCommandBuffer g_thumbnailCmd = VK_NULL_HANDLE;
VkFence g_thumbnailFence = VK_NULL_HANDLE;
bool g_thumbnailUploadReady = false;

void DestroyThumbnailUploadObjects()
{
	VkDevice device = g_vulkan != nullptr ? g_vulkan->Device() : VK_NULL_HANDLE;
	if (device != VK_NULL_HANDLE)
	{
		if (g_thumbnailSampler != VK_NULL_HANDLE)
			vkDestroySampler(device, g_thumbnailSampler, nullptr);
		if (g_thumbnailFence != VK_NULL_HANDLE)
			vkDestroyFence(device, g_thumbnailFence, nullptr);
		if (g_thumbnailPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, g_thumbnailPool, nullptr);
	}

	g_thumbnailSampler = VK_NULL_HANDLE;
	g_thumbnailPool = VK_NULL_HANDLE;
	g_thumbnailCmd = VK_NULL_HANDLE;
	g_thumbnailFence = VK_NULL_HANDLE;
	g_thumbnailUploadReady = false;
}

bool CreateThumbnailUploadObjects()
{
	if (g_vulkan == nullptr || g_vulkan->Device() == VK_NULL_HANDLE)
		return false;

	VkDevice device = g_vulkan->Device();
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	poolInfo.queueFamilyIndex = g_vulkan->QueueFamily();
	if (vkCreateCommandPool(device, &poolInfo, nullptr, &g_thumbnailPool) != VK_SUCCESS)
	{
		DestroyThumbnailUploadObjects();
		return false;
	}

	VkCommandBufferAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc.commandPool = g_thumbnailPool;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc.commandBufferCount = 1;
	if (vkAllocateCommandBuffers(device, &alloc, &g_thumbnailCmd) != VK_SUCCESS)
	{
		DestroyThumbnailUploadObjects();
		return false;
	}

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (vkCreateFence(device, &fenceInfo, nullptr, &g_thumbnailFence) != VK_SUCCESS)
	{
		DestroyThumbnailUploadObjects();
		return false;
	}

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.maxLod = 1.0f;
	if (vkCreateSampler(device, &samplerInfo, nullptr, &g_thumbnailSampler) != VK_SUCCESS)
	{
		DestroyThumbnailUploadObjects();
		return false;
	}

	g_thumbnailUploadReady = true;
	return true;
}

void DestroyThumbnail(ThumbnailTexture& cached)
{
	if (cached.texture != 0)
	{
		ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(
			static_cast<std::uintptr_t>(cached.texture)));
		cached.texture = 0;
	}
	if (g_vulkan != nullptr && cached.image.image != VK_NULL_HANDLE)
		g_vulkan->Memory().DestroyImage(cached.image);

	cached = ThumbnailTexture();
}

void RetireThumbnail(ThumbnailTexture& cached)
{
	if (cached.texture != 0 || cached.image.image != VK_NULL_HANDLE)
	{
		RetiredThumbnail retired;
		retired.texture = cached.texture;
		retired.image = cached.image;
		retired.frame = g_thumbnailFrame;
		g_retiredThumbnails.push_back(retired);
	}
	cached = ThumbnailTexture();
}

void ReleaseRetiredThumbnails(bool force)
{
	for (auto it = g_retiredThumbnails.begin(); it != g_retiredThumbnails.end();)
	{
		if (!force && g_thumbnailFrame - it->frame <= 4)
		{
			++it;
			continue;
		}

		if (it->texture != 0)
		{
			ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(
				static_cast<std::uintptr_t>(it->texture)));
		}
		if (g_vulkan != nullptr && it->image.image != VK_NULL_HANDLE)
			g_vulkan->Memory().DestroyImage(it->image);

		it = g_retiredThumbnails.erase(it);
	}
}

void ReleaseAllThumbnails(bool waitForDevice)
{
	if (waitForDevice && g_vulkan != nullptr && g_vulkan->Device() != VK_NULL_HANDLE)
		vkDeviceWaitIdle(g_vulkan->Device());

	for (auto& item : g_thumbnailTextures)
		DestroyThumbnail(item.second);
	g_thumbnailTextures.clear();
	ReleaseRetiredThumbnails(true);
}

bool IsZipContainer(const std::filesystem::path& path)
{
	std::FILE* file = std::fopen(path.string().c_str(), "rb");
	if (file == nullptr)
		return false;

	unsigned char magic[4] = {};
	const std::size_t read = std::fread(magic, 1, sizeof(magic), file);
	std::fclose(file);
	return read == sizeof(magic) && magic[0] == 'P' && magic[1] == 'K' &&
		magic[2] == 3 && magic[3] == 4;
}

bool ReadContainerThumbnail(const std::filesystem::path& container, std::vector<std::uint8_t>& png)
{
	png.clear();

	// Only a .zst container is a ZIP with a thumb.png entry. A legacy .st is a raw block file and
	// has no picture at all; checking the magic rather than the suffix also covers a container that
	// was renamed. The caller does not draw a preview when this returns false.
	if (!IsZipContainer(container))
		return false;

	unzFile zip = unzOpen(container.string().c_str());
	if (zip == nullptr)
		return false;

	bool ok = false;
	if (unzLocateFile(zip, "thumb.png", 1) == UNZ_OK)
	{
		unz_file_info info = {};
		if (unzGetCurrentFileInfo(zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK &&
			info.uncompressed_size > 0 && info.uncompressed_size <= 64u * 1024u * 1024u)
		{
			png.resize(static_cast<std::size_t>(info.uncompressed_size));
			if (unzOpenCurrentFile(zip) == UNZ_OK)
			{
				const int read = unzReadCurrentFile(zip, png.data(), static_cast<unsigned>(png.size()));
				ok = read == static_cast<int>(png.size());
				unzCloseCurrentFile(zip);
			}
		}
	}
	unzClose(zip);

	if (!ok)
		png.clear();
	return ok;
}

bool UploadThumbnail(const std::vector<std::uint8_t>& png, Vulkan3D::AllocatedImage& image,
	ImTextureID& texture, int& width, int& height)
{
	image = Vulkan3D::AllocatedImage();
	texture = 0;
	width = 0;
	height = 0;

	if (!g_thumbnailUploadReady || g_vulkan == nullptr)
		return false;

	std::vector<std::uint8_t> rgba;
	int decodedWidth = 0;
	int decodedHeight = 0;
	if (!GuiDecodeImageBytes(png.data(), png.size(), 480, rgba, decodedWidth, decodedHeight) ||
		decodedWidth <= 0 || decodedHeight <= 0)
		return false;

	const VkDeviceSize bytes = VkDeviceSize(decodedWidth) * VkDeviceSize(decodedHeight) * 4;
	Vulkan3D::AllocatedBuffer staging;
	if (!g_vulkan->Memory().CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging))
		return false;
	std::memcpy(staging.mapped, rgba.data(), static_cast<std::size_t>(bytes));

	if (!g_vulkan->Memory().CreateImage(static_cast<uint32_t>(decodedWidth),
			static_cast<uint32_t>(decodedHeight), 1, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, image))
	{
		g_vulkan->Memory().DestroyBuffer(staging);
		return false;
	}

	image.view = g_vulkan->CreateView(image.image, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
	if (image.view == VK_NULL_HANDLE)
	{
		g_vulkan->Memory().DestroyImage(image);
		g_vulkan->Memory().DestroyBuffer(staging);
		return false;
	}

	VkDevice device = g_vulkan->Device();
	vkResetCommandPool(device, g_thumbnailPool, 0);
	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vkBeginCommandBuffer(g_thumbnailCmd, &begin) != VK_SUCCESS)
	{
		g_vulkan->Memory().DestroyImage(image);
		g_vulkan->Memory().DestroyBuffer(staging);
		return false;
	}

	Vulkan3D::CmdTransition(g_thumbnailCmd, image.image, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { static_cast<uint32_t>(decodedWidth), static_cast<uint32_t>(decodedHeight), 1 };
	vkCmdCopyBufferToImage(g_thumbnailCmd, staging.buffer, image.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	Vulkan3D::CmdTransition(g_thumbnailCmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	if (vkEndCommandBuffer(g_thumbnailCmd) != VK_SUCCESS)
	{
		g_vulkan->Memory().DestroyImage(image);
		g_vulkan->Memory().DestroyBuffer(staging);
		return false;
	}

	VkCommandBufferSubmitInfo cmdInfo = {};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.commandBuffer = g_thumbnailCmd;
	VkSubmitInfo2 submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.commandBufferInfoCount = 1;
	submit.pCommandBufferInfos = &cmdInfo;
	Vulkan3D::QueueSubmit2(g_vulkan->Queue(), 1, &submit, g_thumbnailFence);
	vkWaitForFences(device, 1, &g_thumbnailFence, VK_TRUE, UINT64_MAX);
	vkResetFences(device, 1, &g_thumbnailFence);
	g_vulkan->Memory().DestroyBuffer(staging);

	VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(g_thumbnailSampler, image.view,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	if (descriptor == VK_NULL_HANDLE)
	{
		g_vulkan->Memory().DestroyImage(image);
		return false;
	}

	texture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
	width = decodedWidth;
	height = decodedHeight;
	return true;
}

/*
 * Returns the texture for a slot, loading it on first use. A missing thumb.png is remembered too: a
 * legacy .st or a save without a capture must not make every frame retry the unzip and decode.
 */
const ThumbnailTexture* GetThumbnailTexture(const OverlaySaveSlot& slot)
{
	if (slot.path.empty() || !slot.exists)
	{
		auto stale = g_thumbnailTextures.find(slot.slot);
		if (stale != g_thumbnailTextures.end())
		{
			RetireThumbnail(stale->second);
			g_thumbnailTextures.erase(stale);
		}
		return nullptr;
	}

	ThumbnailTexture& cached = g_thumbnailTextures[slot.slot];
	if (cached.attempted && cached.path == slot.path && cached.revision == slot.revision)
		return cached.texture != 0 ? &cached : nullptr;

	RetireThumbnail(cached);
	cached.path = slot.path;
	cached.revision = slot.revision;
	cached.attempted = true;

	if (!g_thumbnailUploadReady)
		return nullptr;

	std::vector<std::uint8_t> png;
	if (!ReadContainerThumbnail(slot.path, png))
		return nullptr;

	Vulkan3D::AllocatedImage image;
	ImTextureID texture = 0;
	int width = 0;
	int height = 0;
	if (!UploadThumbnail(png, image, texture, width, height))
		return nullptr;

	cached.image = image;
	cached.texture = texture;
	cached.width = width;
	cached.height = height;
	return &cached;
}
#endif // !SUPERMODEL_OPENGL_ONLY

/*
 * Clickable controls ask for the hand pointer themselves; ImGui keeps the arrow otherwise. The
 * settings window has the same helper next to its UiButton wrapper - the overlay draws its few
 * buttons with ImGui directly, so it needs its own copy rather than a shared header for one line.
 */
static void HandOver()
{
	if (ImGui::IsItemHovered())
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

static void DrawSaveStateSection()
{
	if (!g_saveHandlers.list || !g_saveHandlers.apply)
		return;

	if (!ImGui::CollapsingHeader(Tr("即时存档##OverlaySaveStates"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		HandOver();
		return;
	}
	HandOver();

	const std::vector<OverlaySaveSlot> slots = g_saveHandlers.list();
#if !defined(SUPERMODEL_OPENGL_ONLY)
	// The preview is anchored to the right edge of the table, not to the pointer, so it stays beside
	// the row the pointer is over while the mouse moves along that row. The left edge comes from the
	// window rather than the cursor: after the collapsing header the cursor can still sit at the far
	// right, which made the row rectangle only as wide as the last column.
	const float tableLeft = ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x;
	const float tableRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
	int hoveredSlot = -1;
	ImVec2 hoveredRowMin(0.0f, 0.0f);
#endif
	/*
	 * The table is indented a little inside the body.
	 *
	 * Its alternating row background is a visible surface, so the slot numbers are read against the
	 * band's edge rather than against the panel's padding - and the band starts at the content origin,
	 * which makes the numbers look like they are pressed against the left side even though the panel
	 * has a generous margin. A few pixels of indent, plus the outer cell padding below, separates the
	 * two; the table gives up that width and nothing else moves.
	 *
	 * The original theme is exempt: it has no glass plate, so the body already sits on the panel's own
	 * background and the offset would just look like a table that missed its column.
	 */
	const float tableIndent = UiThemeIsClassic()
		? 0.0f : 5.0f * (g_uiScale > 0.01f ? g_uiScale : 1.0f);
	ImGui::Indent(tableIndent);
	if (ImGui::BeginTable("OverlaySaveSlots", 3,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
	{
		/*
		 * Column widths are measured from the translated labels rather than written as constants, so
		 * they follow both the interface language and the UI scale.
		 *
		 * Slot holds a number and at most a " *" marker; it needs far less than the 70 pixels it used to
		 * take, and the width it gives up goes to Status. Status now shows a full date and time rather
		 * than "5 min ago", which is the column that needs the room. Actions holds Save and Load only:
		 * pressing either also makes that slot current, so the separate "Use for F5/F7" button was
		 * redundant and has been removed.
		 */
		const ImGuiStyle& style = ImGui::GetStyle();
		const float slotWidth = ImGui::CalcTextSize(Tr("槽位")).x + style.CellPadding.x * 2.0f + 4.0f;
		ImGui::TableSetupColumn(Tr("槽位"), ImGuiTableColumnFlags_WidthFixed, slotWidth);
		ImGui::TableSetupColumn(Tr("状态"), ImGuiTableColumnFlags_WidthStretch);
		const float actionWidth =
			ImGui::CalcTextSize(Tr("保存")).x + ImGui::CalcTextSize(Tr("读取")).x +
			style.FramePadding.x * 4.0f + style.ItemSpacing.x + style.CellPadding.x * 2.0f + 4.0f;
		ImGui::TableSetupColumn(Tr("操作"), ImGuiTableColumnFlags_WidthFixed, actionWidth);
		ImGui::TableHeadersRow();

		for (std::size_t index = 0; index < slots.size(); ++index)
		{
			const auto& slot = slots[index];
			ImGui::TableNextRow();
#if !defined(SUPERMODEL_OPENGL_ONLY)
			const ImVec2 rowMin = ImGui::GetCursorScreenPos();
			const bool rowHovered = slot.exists &&
				ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
				ImGui::IsMouseHoveringRect(ImVec2(tableLeft, rowMin.y),
					ImVec2(tableRight, rowMin.y + ImGui::GetFrameHeightWithSpacing()), false);
			// clip=false: at this point ImGui still has the previous row's last-cell clip rect, so
			// testing against it would only recognise the Actions column. The row rectangle is
			// already in screen space and the window check above keeps off-panel rows out.
			if (rowHovered)
			{
				hoveredSlot = static_cast<int>(index);
				hoveredRowMin = rowMin;
			}
#endif
			ImGui::PushID((int)slot.slot);

			ImGui::TableSetColumnIndex(0);
			if (slot.active)
				ImGui::Text("%u *", slot.slot);
			else
				ImGui::Text("%u", slot.slot);

			ImGui::TableSetColumnIndex(1);
			if (slot.exists)
				ImGui::TextUnformatted(FormatSaveTime(slot.ageSeconds));
			else
				ImGui::TextDisabled("%s", Tr("空"));

			ImGui::TableSetColumnIndex(2);
			if (ImGui::SmallButton(Tr("保存")))
				g_saveHandlers.apply(slot.slot, true);
			HandOver();
			ImGui::SameLine();
			ImGui::BeginDisabled(!slot.exists);
			if (ImGui::SmallButton(Tr("读取")))
				g_saveHandlers.apply(slot.slot, false);
			HandOver();
			ImGui::EndDisabled();

#if !defined(SUPERMODEL_OPENGL_ONLY)
			if (rowHovered)
			{
				hoveredSlot = static_cast<int>(index);
				hoveredRowMin = rowMin;
			}
#endif

			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::Unindent(tableIndent);

#if !defined(SUPERMODEL_OPENGL_ONLY)
	if (hoveredSlot >= 0)
	{
		const OverlaySaveSlot& slot = slots[(std::size_t)hoveredSlot];
		const ThumbnailTexture* thumbnail = GetThumbnailTexture(slot);
		if (thumbnail != nullptr)
		{
			const float scale = g_uiScale > 0.01f ? g_uiScale : 1.0f;
			const float maxWidth = ImGui::GetIO().DisplaySize.x * 0.45f;
			const float width = (std::min)(320.0f * scale, maxWidth);
			const float height = width * (float)thumbnail->height / (float)thumbnail->width;

			ImGui::SetNextWindowPos(
				ImVec2(tableRight + ImGui::GetStyle().ItemSpacing.x, hoveredRowMin.y), ImGuiCond_Always);
			if (ImGui::BeginTooltip())
			{
				ImGui::Image(ImTextureRef(thumbnail->texture), ImVec2(width, height));
				ImGui::EndTooltip();
			}
		}
	}
#endif
}

/*
 * 0..1 fade weight for the panel, so opening it is a transition rather than a snap.
 *
 * Called once per built frame whether or not the panel is open, because the open edge is detected
 * here: g_visible is set by the toggle key, by the Close button and by Apply/Cancel, so watching it
 * is the only place that catches all three. A smoothstep rather than a linear ramp - a linear fade
 * over 120 ms reads as a step at both ends.
 */
static float PanelFadeAlpha()
{
	static double openedAt = -1.0;
	static bool wasVisible = false;

	if (!g_visible)
	{
		wasVisible = false;
		openedAt = -1.0;
		return 1.0f;
	}

	const double now = (double)OSD::Platform::Now() * 1.0e-9;
	if (!wasVisible)
	{
		wasVisible = true;
		openedAt = now;
	}

	constexpr double kFadeSeconds = 0.12;
	if (openedAt < 0.0 || now - openedAt >= kFadeSeconds)
		return 1.0f;

	const double t = (now - openedAt) / kFadeSeconds;
	return (float)(t * t * (3.0 - 2.0 * t));
}

/*
 * A panel button whose hover colour fades in over ~120 ms.
 *
 * The settings window's UiButton does the same thing, but that helper is file-local to Gui.cpp. The
 * trick is described on UiHoverBegin(): ImGui's own hover colours cannot be interpolated, so both
 * states are pushed as the same lerped colour and the transition is driven here.
 */
static bool OverlayButton(const char* label, const ImVec2& size)
{
	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec4 mixed = UiHoverBlend(style.Colors[ImGuiCol_Button],
		style.Colors[ImGuiCol_ButtonHovered], UiHoverBegin(label));
	ImGui::PushStyleColor(ImGuiCol_Button, mixed);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mixed);
	const bool pressed = ImGui::Button(Tr(label), size);
	ImGui::PopStyleColor(2);
	UiHoverEnd(label, ImGui::IsItemHovered());
	HandOver();
	return pressed;
}

bool OverlayWantsFrost()
{
	/*
	 * The original theme has no glass: its panel is an opaque sheet, so the whole blur chain is a
	 * frame of GPU time spent on a picture nobody samples. Answering false here is what keeps the
	 * claim "no panel, no cost" true for that theme as well.
	 */
	return g_visible && g_frostEnabled && g_backend == OverlayBackend::Vulkan &&
		!UiThemeIsClassic();
}

#if !defined(SUPERMODEL_OPENGL_ONLY)
void OverlaySetFrostTextureSource(OverlayFrostTextureFn fn)
{
	g_frostSource = fn;
}

/*
 * Brings the registered frost texture in line with what the renderer is publishing.
 *
 * Called once per built frame, before NewFrame(), because the handle has to be part of the draw
 * commands that frame produces. Registration is kept across frames: the image only changes when the
 * swapchain is resized, and re-registering every frame would churn the ImGui descriptor pool for no
 * reason.
 */
static void RefreshFrostTexture()
{
	OverlayFrostTexture source;
	if (g_frostSource != nullptr)
		source = g_frostSource();

	const bool usable = g_backend == OverlayBackend::Vulkan && g_backendReady &&
		g_frostSampler != VK_NULL_HANDLE && source.view != VK_NULL_HANDLE && source.generation != 0;

	if (!usable)
	{
		if (g_frostTexture != 0)
		{
			ImGui_ImplVulkan_RemoveTexture(
				reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(g_frostTexture)));
			g_frostTexture = 0;
			g_frostGeneration = 0;
		}
		return;
	}

	if (g_frostTexture != 0 && g_frostGeneration == source.generation)
		return;

	if (g_frostTexture != 0)
	{
		ImGui_ImplVulkan_RemoveTexture(
			reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(g_frostTexture)));
		g_frostTexture = 0;
		g_frostGeneration = 0;
	}

	const VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
		g_frostSampler, source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	if (descriptor != VK_NULL_HANDLE)
	{
		g_frostTexture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
		g_frostGeneration = source.generation;
	}
}
#endif

bool OverlayNewFrame(Util::Config::Node& config)
{
	/*
	 * Also runs when only the statistics readout is on.
	 *
	 * They share one ImGui context and one ImGui frame, so neither can be drawn on a frame that this
	 * function skipped - there would be no NewFrame for them to belong to. The panel is therefore
	 * conditional below while the frame itself is not.
	 *
	 * The light-gun crosshair is deliberately not part of this condition any more: it has its own
	 * pipeline in the Vulkan renderer (see CVulkan2D::SetCrosshair), and a crosshair-only frame must
	 * not pay for an ImGui frame.
	 */
	const bool panel = OverlayVisible();
	const bool stats = g_backendReady && StatsOsdEnabled();
	if (!panel && !stats)
		return false;

	/*
	 * Rebuilt at most 60 times a second.
	 *
	 * The pacer can present at 240 or 480 Hz, and the UI has no use for that: a panel is read by a
	 * human, and laying out every frame would put ImGui's vertex building and font-atlas bookkeeping
	 * on the critical path of a 2 ms present. Skipping the rebuild is safe because ImGui::Render()
	 * has already produced a draw list, and the Vulkan backend re-uploads and re-renders whatever
	 * draw data it is handed - a skipped frame therefore presents the same panel, not a blank one.
	 *
	 * Input is not lost either: the platform layer queues events into io as they arrive, and ImGui
	 * consumes the queue at the next NewFrame. The only visible effect is that a drag or a text
	 * caret moves at 60 Hz, which is what a 60 Hz UI looks like anyway.
	 *
	 * A change in what is on screen bypasses the throttle, so opening or closing the panel takes
	 * effect on the very next present rather than up to one UI period later.
	 */
	static uint64_t s_lastBuild = 0;
	static bool s_lastPanel = false;
	static bool s_lastStats = false;
	constexpr uint64_t kBuildPeriodNs = 1000000000ull / 60;
	const uint64_t buildNow = OSD::Platform::Now();
	if (s_lastBuild != 0 && panel == s_lastPanel && stats == s_lastStats &&
		buildNow - s_lastBuild < kBuildPeriodNs)
		return false;
	s_lastBuild = buildNow;
	s_lastPanel = panel;
	s_lastStats = stats;

	ImGui::SetCurrentContext(g_context);
#if !defined(SUPERMODEL_OPENGL_ONLY)
	// Textures replaced on a previous hover are held for a few frames so a draw list that is still
	// in flight cannot sample a descriptor that has already been freed.
	++g_thumbnailFrame;
	ReleaseRetiredThumbnails(false);
	// Registers the renderer's blurred backdrop (or drops it when the image was replaced). Before
	// NewFrame(), because the handle has to be part of this frame's draw commands.
	RefreshFrostTexture();
#endif
	// Live, so the option can be switched from the panel or from the settings window mid-session.
	g_frostEnabled = config["UiFrostedGlass"].ValueAsDefault<bool>(true);
	// The only place the two backends differ in this function: each has its own NewFrame, which is
	// what installs that backend's font texture and display size into the shared context.
#if defined(SUPERMODEL_OPENGL_ONLY)
	ImGui_ImplOpenGL3_NewFrame();
#else
	if (g_backend == OverlayBackend::Vulkan)
		ImGui_ImplVulkan_NewFrame();
#if !defined(SUPERMODEL_PURE_VULKAN)
	else
		ImGui_ImplOpenGL3_NewFrame();
#endif
#endif
	FeedInput();
	ImGui::NewFrame();


	bool apply = false;
	bool cancel = false;

	/*
	 * Drawn before the settings panel.
	 *
	 * Both are anchored to the top left, and the panel - a deliberate, temporary tool - should sit over
	 * a readout that is always there. Recording the readout first is what puts it underneath.
	 */
	StatsOsdBuildWindow(config);

	// Called every built frame, open or not: the open edge is detected inside it, and a call only
	// while the panel is visible would never see it close and so never see it open again.
	const float panelAlpha = PanelFadeAlpha();

	if (panel)
	{
	// Left-hand side, so the game's own view of the track stays visible while the values are changed.
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 16.0f, viewport->WorkPos.y + 16.0f), ImGuiCond_FirstUseEver);
	/*
	 * A capped default height rather than the full work area.
	 *
	 * Three quarters of the screen: the paragraphs of help under the controls are gone, so full
	 * height leaves mostly empty space, but the CRT section still wants room to be read without
	 * scrolling. The body scrolls when it does not fit and the window remains resizable.
	 */
	const float fullHeight = viewport->WorkSize.y - 32.0f;
	const float defaultHeight = fullHeight * 0.75f;
	ImGui::SetNextWindowSize(ImVec2(430.0f * g_uiScale, defaultHeight), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(340.0f, 240.0f), ImVec2(FLT_MAX, FLT_MAX));

	/*
	 * No ImGui background: the panel paints its own, below.
	 *
	 * A flat fill is what made the panel look like a form dropped on top of the game. What it wants
	 * instead is the frame behind it, blurred, tinted and edged - which is what the renderer produces
	 * and what UiFrost::Plate() draws. `frost` is zero when the effect is switched off, when the
	 * renderer has no chain (an OpenGL session), or on the frame a resize replaced the image; the
	 * plate then falls back to its tint, which is legible if less pretty.
	 *
	 * The original theme goes the other way on purpose: no plate, no blur, and ImGui's own opaque
	 * window background, which is exactly how this panel looked before the rework.
	 */
	const bool classic = UiThemeIsClassic();
	if (!classic)
		ImGui::SetNextWindowBgAlpha(0.0f);

	/*
	 * A little more generous than the shared theme.
	 *
	 * The shared 18px is tuned for the settings window, which is a full-window page with panes inside
	 * it. This is one small floating panel over a game picture, so the same padding reads as half the
	 * margin: at 150% scaling the panel is only ~660 physical pixels wide, and every section heading
	 * and footer button sits close enough to the rounded border to look crammed.
	 */
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, classic
		? ImGui::GetStyle().WindowPadding
		: ImVec2(26.0f * g_uiScale, 22.0f * g_uiScale));

	// The whole panel - background included - fades in over ~120 ms when it is opened, so Tab does
	// not snap a full-page interface into existence over the game. ImGui's style alpha covers every
	// colour the panel draws; the plate is passed the same value because a textured draw list
	// command is not affected by it.
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panelAlpha);
	const ImTextureID frost = (g_frostEnabled && g_frostTexture != 0) ? g_frostTexture : 0;

	// No collapse toggle and no scrollbar-drag surprises: this is a panel to be dragged around and
	// read, and folding it away would leave no hint that it is still open.
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (ImGui::Begin(Tr("画面设置（实时）##OverlayPanel"), nullptr, flags))
	{
		if (!classic)
		{
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetWindowPos();
			const ImVec2 size = ImGui::GetWindowSize();
			UiFrost::Plate(draw, frost, origin, ImVec2(origin.x + size.x, origin.y + size.y),
				ImGui::GetStyle().WindowRounding, UiFrost::Surface::Glass, panelAlpha);
		}

		/*
		 * Buttons pinned to the bottom, outside the scrolling region.
		 *
		 * The CRT section is long enough to overflow a short panel, and it grows with the display
		 * scale - at 1.5x on a 900 pixel window it already does. As inline content the footer simply
		 * scrolled off the bottom, which makes Apply and Cancel reachable only by scrolling a panel
		 * whose scrollbar is easy to miss.
		 *
		 * The reserved height covers the button row only: the hint line that used to sit under it has
		 * been removed, and leaving its height reserved would reintroduce an empty strip above the
		 * bottom of the window. StartChild with a negative height means "everything except that much",
		 * so the body scrolls and the footer does not.
		 */
		// Two rows: the Apply/Cancel/Close buttons, and the attribution line required by
		// LICENSE-ADDITIONAL-TERMS.md (GPL v3 Section 7(b)).  The body loses one row, not the
		// attribution.
		const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f;

		/*
		 * No background on the scrolling body.
		 *
		 * A child with ImGui's default background draws its own translucent rectangle across the panel,
		 * and that rectangle's edge is what the eye reads as "the panel border" - the section headings
		 * and the first control then look like they are sitting on it. Leaving it transparent makes the
		 * whole padding ring read as one margin.
		 */
		ImGui::BeginChild("OverlayBody", ImVec2(0, -footer), ImGuiChildFlags_None,
			static_cast<ImGuiWindowFlags>(classic ? 0 : ImGuiWindowFlags_NoBackground));
		// At the top, where a picture decision is made: both controls are live under Vulkan and the
		// panel exists to be adjusted while watching the picture.
		DrawPictureModeSettings(config, true);
		DrawAdvancedSettingsPanel(config, true);
#if !defined(SUPERMODEL_OPENGL_ONLY)
		DrawHDTextureSettings(config, true);
#endif
		ImGui::Spacing();
		DrawSaveStateSection();
		ImGui::EndChild();

		ImGui::Separator();

		// Apply and Cancel are about persistence, not about the effect: every control above has
		// already changed the picture. Apply keeps the values by writing them to the settings file,
		// Cancel puts the panel back the way it was found, and Close leaves them in place for this
		// session without saving.
		//
		// ImGui::Button directly rather than the settings window's UiButton wrapper: that helper is
		// file-local to Gui.cpp, and the two things it adds - Tr() on the label and a size - are
		// trivial here.
		const float buttonWidth = 110.0f * g_uiScale;
		if (OverlayButton("应用##OverlayApply", ImVec2(buttonWidth, 0)))
			apply = true;
		ImGui::SameLine();
		if (OverlayButton("撤销##OverlayCancel", ImVec2(buttonWidth, 0)))
			cancel = true;
		ImGui::SameLine();
		if (OverlayButton("关闭##OverlayClose", ImVec2(buttonWidth, 0)))
			g_visible = false;

		/*
		 * Attribution required by LICENSE-ADDITIONAL-TERMS.md (GPL v3 Section 7(b)).
		 * Always drawn while the panel is open; there is no setting that hides it.
		 */
		ImGui::Spacing();
		ImGui::TextDisabled("%s by %s  -", MODEL3VULKAN_TITLE, MODEL3VULKAN_AUTHOR);
		ImGui::SameLine();
		if (ImGui::TextLink(MODEL3VULKAN_REPO_URL))
			OSD::Platform::OpenURL(MODEL3VULKAN_REPO_URL);
		HandOver();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);

	if (cancel)
	{
		RestoreSnapshot(config);
		g_visible = false;
		cancel = false;
	}

	if (apply)
	{
		// Keep the snapshot: closing without cancelling leaves the values in place, and a later
		// Cancel should still undo back to the state the panel opened in.
		g_visible = false;
	}
	}   // end of the settings panel

	// Ended here rather than in the render callback so that a frame which never reaches the GPU - a
	// minimised window, a dropped frame - cannot leave ImGui mid-frame.

	ImGui::Render();

	g_frameRendered = true;
	ImGui::SetCurrentContext(g_previousContext);

	if (apply)
		InfoLog("Overlay: settings applied from the in-game panel.");

	return apply;
}

#if !defined(SUPERMODEL_OPENGL_ONLY)
void OverlayRender(VkCommandBuffer cmd)
{
	if (g_backend != OverlayBackend::Vulkan)
		return;
	if ((!OverlayVisible() && !StatsOsdEnabled()) || !g_frameRendered || g_context == nullptr)
		return;

	/*
	 * The context has to be made current *before* anything is read from it.
	 *
	 * The settings window is destroyed before a game session starts, and ImGui::DestroyContext()
	 * leaves no current context at all - so the context saved on entry is NULL. Calling GetDrawData()
	 * while that is current dereferences a null context and takes the process down with an access
	 * violation, which is what a single wrong ordering here costs.
	 */
	ImGui::SetCurrentContext(g_context);

	ImDrawData* drawData = ImGui::GetDrawData();
	if (drawData == nullptr || drawData->TotalVtxCount == 0)
	{
		ImGui::SetCurrentContext(g_previousContext);
		return;
	}

	ImGui_ImplVulkan_RenderDrawData(drawData, cmd);

	ImGui::SetCurrentContext(g_previousContext);
}
#endif

void OverlayRenderGL()
{
#if defined(SUPERMODEL_PURE_VULKAN)
	// Declared for the shared call sites in Main.cpp; there is no GL backend here.
	return;
#else
	if (g_backend != OverlayBackend::OpenGL)
		return;
	if ((!OverlayVisible() && !StatsOsdEnabled()) || !g_frameRendered || g_context == nullptr)
		return;

	// Same ordering requirement as the Vulkan path above, for the same reason.
	ImGui::SetCurrentContext(g_context);

	ImDrawData* drawData = ImGui::GetDrawData();
	if (drawData == nullptr || drawData->TotalVtxCount == 0)
	{
		ImGui::SetCurrentContext(g_previousContext);
		return;
	}

	/*
	 * No scope of any kind to open first.
	 *
	 * The Vulkan backend needs the caller to have the swapchain image bound as a colour attachment,
	 * because it only issues draw commands. The OpenGL backend draws into whatever framebuffer is
	 * bound - the default one, at this point in the frame - so it sets up its own state and restores
	 * it afterwards, and the caller only has to have the game's context current.
	 */
	ImGui_ImplOpenGL3_RenderDrawData(drawData);

	ImGui::SetCurrentContext(g_previousContext);
#endif
}
