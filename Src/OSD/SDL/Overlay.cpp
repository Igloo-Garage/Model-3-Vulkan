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
#include "Overlay.h"
#include "StatsOsd.h"

#include "Gui.h"                    // LoadUiFont(), DrawAdvancedSettingsPanel()
#include "Localization.h"
#include "Graphics/Vulkan3D/VulkanContext.h"
#include "OSD/Logger.h"
#include "Util/Format.h"
#include "Util/NewConfig.h"

#include "../Pkgs/imgui/imgui.h"
#include "../Pkgs/imgui/imgui_impl_opengl3.h"
#include "../Pkgs/imgui/imgui_impl_vulkan.h"
#include <SDL.h>
#include <SDL_syswm.h>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstring>
#include <string>
#include <vector>

namespace
{

ImGuiContext* g_context = nullptr;
// The context that was current before this one was created. The settings window keeps a context of
// its own for the whole run, and leaving ours current would make every later ImGui call from that
// window operate on the wrong one - which shows up as a blank or misbehaving settings screen rather
// than as an error.
ImGuiContext* g_previousContext = nullptr;

Vulkan3D::CVulkanContext* g_vulkan = nullptr;
uint64_t g_swapchainGeneration = 0;
bool g_backendReady = false;
bool g_visible = false;

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

SDL_Window* g_window = nullptr;
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

const char* const kOverlayKeys[] = {
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
		SDL_GetWindowSize(g_window, &windowW, &windowH);
		io.DisplaySize = ImVec2((float)windowW, (float)windowH);
	}
	// No HiDPI framebuffer scaling: the swapchain is the window's pixel size in this renderer.
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

#ifdef _WIN32
	HWND hwnd = nullptr;
	if (g_window != nullptr)
	{
		SDL_SysWMinfo info;
		SDL_VERSION(&info.version);
		if (SDL_GetWindowWMInfo(g_window, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
			hwnd = info.info.win.window;
	}

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

bool OverlayInit(Vulkan3D::CVulkanContext& context, SDL_Window* window, float uiScale)
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
	// DescriptorPool and DescriptorPoolSize are mutually exclusive - the backend asserts on both being
	// set or neither - and letting it build its own pool is one less object to own.
	init.DescriptorPoolSize = 8;
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
	InfoLog("Overlay: ready (%ux%u, %u swapchain image(s), scale %.2f).",
		context.Extent().width, context.Extent().height, init.ImageCount, g_uiScale);
	return true;
}

bool OverlayInitGL(SDL_Window* window, float uiScale)
{
	g_vulkan = nullptr;
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
}

void OverlayShutdown()
{
	if (!g_backendReady && g_context == nullptr)
		return;

	ImGui::SetCurrentContext(g_context);
	if (g_backendReady)
	{
		// The Vulkan path waits for the device to go idle, so it must not be called mid-frame.
		if (g_backend == OverlayBackend::Vulkan)
			ImGui_ImplVulkan_Shutdown();
		else if (g_backend == OverlayBackend::OpenGL)
			ImGui_ImplOpenGL3_Shutdown();
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

bool OverlayNewFrame(Util::Config::Node& config)
{
	/*
	 * Also runs when only the statistics readout is on.
	 *
	 * The two share one ImGui context and one ImGui frame, so the readout cannot be drawn on a frame
	 * that this function skipped - there would be no NewFrame for its window to belong to. The panel
	 * is therefore conditional below while the frame itself is not.
	 */
	const bool panel = OverlayVisible();
	const bool stats = g_backendReady && StatsOsdEnabled();
	if (!panel && !stats)
		return false;

	ImGui::SetCurrentContext(g_context);
	// The only place the two backends differ in this function: each has its own NewFrame, which is
	// what installs that backend's font texture and display size into the shared context.
	if (g_backend == OverlayBackend::Vulkan)
		ImGui_ImplVulkan_NewFrame();
	else
		ImGui_ImplOpenGL3_NewFrame();
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

	if (panel)
	{
	// Left-hand side, so the game's own view of the track stays visible while the values are changed.
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 16.0f, viewport->WorkPos.y + 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(430.0f * g_uiScale, viewport->WorkSize.y - 32.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(340.0f, 240.0f), ImVec2(FLT_MAX, FLT_MAX));

	// No collapse toggle and no scrollbar-drag surprises: this is a panel to be dragged around and
	// read, and folding it away would leave no hint that it is still open.
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (ImGui::Begin(Tr("画面设置（实时）##OverlayPanel"), nullptr, flags))
	{
		/*
		 * Buttons pinned to the bottom, outside the scrolling region.
		 *
		 * The CRT section is long enough to overflow a short panel, and it grows with the display
		 * scale - at 1.5x on a 900 pixel window it already does. As inline content the footer simply
		 * scrolled off the bottom, which makes Apply and Cancel reachable only by scrolling a panel
		 * whose scrollbar is easy to miss.
		 *
		 * The reserved height covers the button row plus the hint line under it. StartChild with a
		 * negative height means "everything except that much", so the body scrolls and the footer
		 * does not.
		 */
		const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing()
			+ ImGui::GetStyle().ItemSpacing.y;

		ImGui::BeginChild("OverlayBody", ImVec2(0, -footer), ImGuiChildFlags_None);
		DrawAdvancedSettingsPanel(config, true);
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
		if (ImGui::Button(Tr("应用##OverlayApply"), ImVec2(buttonWidth, 0)))
			apply = true;
		ImGui::SameLine();
		if (ImGui::Button(Tr("撤销##OverlayCancel"), ImVec2(buttonWidth, 0)))
			cancel = true;
		ImGui::SameLine();
		if (ImGui::Button(Tr("关闭##OverlayClose"), ImVec2(buttonWidth, 0)))
			g_visible = false;

		// Wrapped explicitly: this file has no access to Gui.cpp's helpers (they are file-static), and
		// a bare TextDisabled draws one clipped line. This string is short today, but the panel body
		// above it uses the same convention and a future edit should not have to know the difference.
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextDisabled("%s", Tr("Tab 开关此面板。改动立即生效，无需重启。"));
		ImGui::PopTextWrapPos();
	}
	ImGui::End();

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

void OverlayRenderGL()
{
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
}
