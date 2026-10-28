#if defined(SUPERMODEL_PURE_VULKAN) && defined(SUPERMODEL_OPENGL_ONLY)
#error "A target cannot be both Pure-Vulkan and OpenGL-only."
#endif

#if defined(SUPERMODEL_OPENGL_ONLY)
// Named in the tooltip that tells the user how to pick a GPU for OpenGL.
#define SUPERMODEL_EXE_NAME "Supermodel-LegacyOG.exe"
#elif defined(SUPERMODEL_PURE_VULKAN)
#define SUPERMODEL_EXE_NAME "Model3-Vulkan.exe"
#else
#define SUPERMODEL_EXE_NAME "Supermodel-VK.exe"
#endif

#if defined(SUPERMODEL_PURE_VULKAN)
#include "OSD/SDL3/UiVulkanBackend.h"
#else
#include <GL/glew.h>
#endif
#if defined(SUPERMODEL_PLATFORM_SDL3)
#include "OSD/SDL3/VulkanRequirement.h"
#endif
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#if defined(_WIN32)
// Vendored libwebp decoder: the fallback when WIC has no WebP codec (see DecodeBoxartWIC).
#include <webp/decode.h>
#endif
#include "GameLoader.h"
#include "../Pkgs/imgui/imgui.h"
#if defined(SUPERMODEL_PURE_VULKAN)
#include "../Pkgs/imgui/imgui_impl_vulkan.h"
#else
#include "../Pkgs/imgui/imgui_impl_opengl3.h"
#endif
#include "OSD/Platform/Platform.h"
#include "OSD/DisplayPacer.h"
#include "OSD/Platform/ImguiPlatform.h"
#include "../Src/Util/NewConfig.h"
#include "Util/ConfigBuilders.h"
#include "Util/PNGFile.h"
#include "../Src/OSD/SDL/SDLInputSystem.h"
#include "../Src/Inputs/Inputs.h"
#include "OSD/ConfigFile.h"
#include "OSD/FileSystemPath.h"
#include "OSD/Logger.h"
#include "OSD/SDL/FontProbe.h"
#if !defined(SUPERMODEL_OPENGL_ONLY)
#include "Graphics/Vulkan3D/VulkanContext.h"
#include "Graphics/Vulkan3D/HDTextures.h"
#endif
#if defined(SUPERMODEL_PURE_VULKAN)
#include "OSD/SDL3/HostCoreBridge.h"
#endif
#include "Main.h"
#include "Gui.h"
#include "Localization.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#include "OSD/Windows/SplashScreen.h"
#if defined(_WIN32) && defined(SUPERMODEL_PLATFORM_SDL2)
// DirectInput / Raw Input / XInput device handling is SDL2-only: it pumps Win32 messages itself
// and exists for the legacy peripheral ecosystem. The SDL3 target reaches the same devices through
// PlatformInput instead.
#include "../Src/OSD/Windows/DirectInputSystem.h"
#endif
#endif

#ifdef __APPLE__
#include "OSX/FileDialogs.h"
#endif

/*
 * Settings window.
 *
 * Shown when Supermodel is started with no arguments, or with -gui.
 * Common options (renderer, resolution, audio, controller, link play) are on
 * the first tab. The other tabs are generated from the config tree.
 */

static bool g_writeSettings = false;

// Path the settings are written to. Empty means the standard location in Data\Config\; a value set
// by "Save as" redirects that one write and is cleared afterwards, so a later plain save goes back
// to the standard file rather than quietly keeping the one-off destination.
static std::string g_saveAsPath;

// The settings file currently in use. Used to seed the "Save as" dialog so it opens in the right
// folder with the existing name; set by GuiCreate().
static std::string g_settingsPath;

// Transient confirmation shown under the footer buttons ("saved", "loaded"), with the time it was
// set. File scope rather than locals of the footer block because the popups that raise it are drawn
// after that block has closed.
static std::string g_statusNote;
static double g_statusAt = -10.0;

/*
 * Popup to open at the start of the next frame, or nullptr.
 *
 * Opening a popup from inside another popup and then closing that one does not work: CloseCurrentPopup()
 * truncates OpenPopupStack to the level of the popup currently being built, which discards anything
 * opened during the same frame. The result is that the new dialog never appears at all - the picker
 * closes and nothing else happens, which reads as a dead button.
 *
 * Deferring by one frame sidesteps the ordering entirely: the picker closes this frame, and the
 * follow-up dialog is opened at the top of the next one.
 */
static const char* g_pendingPopup = nullptr;

/*
 * Localisation wrappers.
 *
 * Almost every interface string is passed straight to an ImGui function, so rather than wrapping
 * each call site by hand these thin helpers do the lookup on the way through. The literals
 * themselves are left alone and stay Chinese; Localization maps them to the active language. See
 * Localization.h for why the source text doubles as the key.
 *
 * The label is always the first argument of the ImGui call being wrapped, so the signature stays
 * the same and existing calls keep working once the prefix is renamed.
 */
static void UiSep(const char* label) { ImGui::SeparatorText(Tr(label)); }
/*
 * Explanatory lines wrap at the right edge of the content region.
 *
 * ImGui's TextUnformatted / TextDisabled / TextColored all draw a single line and simply clip
 * whatever does not fit - they do not wrap. That is fine for a two-word label and wrong for an
 * explanation: several of the strings here are full sentences, and without a wrap position they run
 * past the panel's edge and over whatever is beside it. A settings panel is exactly where text has to
 * stay inside the frame, so every one of these helpers sets a wrap position.
 *
 * The position is the content region's right edge rather than 0.0f ("the window edge"), because the
 * panel has padding and a scrollbar to stay clear of.
 */
static void PushHelpWrap()
{
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
}
static void PopHelpWrap() { ImGui::PopTextWrapPos(); }

/*
 * Draws a help tooltip that wraps at a fixed width.
 *
 * Needed because ImGui::SetTooltip does NOT wrap: it lays the text out on one line and sizes the popup
 * to fit, so a sentence-length explanation produces a tooltip as wide as that sentence - wide enough
 * to extend past the settings window and over the game list beside it. A comment here previously
 * claimed SetTooltip wraps on its own, which is the opposite of the truth (BeginTooltip leaves the
 * wrap position at 0, meaning "no wrapping") and is why the long tips kept spilling out.
 *
 * The width is in font-size units so it follows the interface's DPI scaling. Roughly 30 characters
 * lands at about the width of the settings panel; the visible width is capped so it can never be
 * wider than the window it is explaining.
 */
static void DrawWrappedTooltip(const char* text)
{
	ImGuiStyle& style = ImGui::GetStyle();

	/*
	 * Measure the text before creating the tooltip.
	 *
	 * The measurement is what allows the popup to be positioned inside the settings panel rather
	 * than letting ImGui grow it to the right until it covers the scrollbar. The wrap width is
	 * capped by the current window's content area, which excludes that scrollbar and the window
	 * padding - so "inside the panel" is the visible column, not the outer window rectangle.
	 */
	const float margin = 8.0f;
	const float fontWrap = ImGui::GetFontSize() * 30.0f;
	const float contentWidth = (std::max)(
		ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x, 1.0f);
	const float maxTextWidth = (std::max)(
		contentWidth - margin * 2.0f - style.WindowPadding.x * 2.0f - style.WindowBorderSize * 2.0f, 64.0f);
	const float wrapWidth = (std::min)(fontWrap, maxTextWidth);

	const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, wrapWidth);
	const float tooltipWidth = textSize.x + style.WindowPadding.x * 2.0f + style.WindowBorderSize * 2.0f;

	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const float leftLimit = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x + margin;
	const float rightLimit = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - margin;

	ImVec2 pos(mouse.x + 16.0f * style.MouseCursorScale, mouse.y + 10.0f * style.MouseCursorScale);
	if (pos.x + tooltipWidth > rightLimit)
		pos.x = rightLimit - tooltipWidth;
	if (pos.x < leftLimit)
		pos.x = leftLimit;

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

static void UiText(const char* label)
{
	PushHelpWrap();
	ImGui::TextUnformatted(Tr(label));
	PopHelpWrap();
}
/*
 * Clickable controls ask for the hand pointer themselves.
 *
 * ImGui only sets a cursor when a widget asks for one - buttons, checkboxes, radios and combos all
 * keep the arrow - so the platform layer's ToSystemCursor() mapping never sees a Hand request.
 * Call this immediately after submitting the item. IsItemHovered() is false for disabled items,
 * which is what we want: a greyed-out control should not invite a click.
 */
static void HandOver()
{
	if (ImGui::IsItemHovered())
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

static bool UiButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
	// Hover colour interpolated over ~120 ms instead of switched instantly. Pushing the same colour
	// for both the idle and the hovered state is what makes that possible: ImGui picks one of the
	// two depending on whether the pointer is over the button, and both are the *same* lerped colour,
	// so the transition is ours to drive rather than something we have to fight ImGui for.
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
static bool UiSmallButton(const char* label)
{
	const bool pressed = ImGui::SmallButton(Tr(label));
	HandOver();
	return pressed;
}
static bool UiCheckbox(const char* label, bool* value)
{
	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec4 mixed = UiHoverBlend(style.Colors[ImGuiCol_FrameBg],
		style.Colors[ImGuiCol_FrameBgHovered], UiHoverBegin(label));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, mixed);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, mixed);
	const bool changed = ImGui::Checkbox(Tr(label), value);
	ImGui::PopStyleColor(2);
	UiHoverEnd(label, ImGui::IsItemHovered());
	HandOver();
	return changed;
}
static bool UiRadioButton(const char* label, bool active)
{
	const bool changed = ImGui::RadioButton(Tr(label), active);
	HandOver();
	return changed;
}

/*
 * The Ko-fi badge in the footer.
 *
 * Drawn rather than shipped as an image: it is a handful of primitives, it follows the interface
 * scale automatically, and a badge that is a texture would need the box-art decoder and another
 * cache for one picture.  The cup is deliberately simple - body, handle, saucer, two steam marks -
 * because it has to read at 16 pixels next to "Ko-fi".
 *
 * The click is handed to the platform layer (SDL_OpenURL), so Windows, Linux and macOS all open
 * their default browser rather than this code growing three platform branches.
 */
static constexpr const char* kKofiUrl = "https://ko-fi.com/igloogarage";

static float KofiBadgeWidth()
{
	const float scale = ImGui::GetFontSize() / 18.0f;
	const float icon = 18.0f * scale;
	return 7.0f * scale + icon + 6.0f * scale + ImGui::CalcTextSize("Ko-fi").x + 9.0f * scale;
}

static void DrawKofiBadge(float width, float height)
{
	ImGui::InvisibleButton("##kofi", ImVec2(width, height));
	const bool hovered = ImGui::IsItemHovered();
	if (hovered)
	{
		HandOver();
		DrawWrappedTooltip(Tr("在 Ko-fi 上支持开发"));
	}
	if (ImGui::IsItemClicked())
	{
		if (!OSD::Platform::OpenURL(kKofiUrl))
			InfoLog("GUI: could not open the Ko-fi page '%s'.", kKofiUrl);
	}

	const ImVec2 pMin = ImGui::GetItemRectMin();
	const ImVec2 pMax = ImGui::GetItemRectMax();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float scale = ImGui::GetFontSize() / 18.0f;
	const ImU32 background = hovered ? IM_COL32(255, 122, 119, 255) : IM_COL32(255, 94, 91, 240);
	const ImU32 white = IM_COL32(255, 255, 255, 255);
	const ImU32 steam = IM_COL32(255, 255, 255, 170);

	draw->AddRectFilled(pMin, pMax, background, 6.0f * scale);
	draw->AddRect(pMin, pMax, IM_COL32(255, 255, 255, 70), 6.0f * scale, 0, 1.0f * scale);

	const float pad = 7.0f * scale;
	const float icon = height - pad * 2.0f;
	const float left = pMin.x + pad;
	const float top = pMin.y + pad;

	// Handle first, body over it: only the right half of the ring stays visible, which reads as a
	// cup handle without needing a clipped path.
	draw->AddCircle(ImVec2(left + icon * 0.60f, top + icon * 0.54f), icon * 0.20f,
		white, 10, 1.6f * scale);
	draw->AddRectFilled(ImVec2(left, top + icon * 0.28f), ImVec2(left + icon * 0.64f, top + icon * 0.86f),
		white, 2.0f * scale, ImDrawFlags_RoundCornersBottom);
	draw->AddLine(ImVec2(left - icon * 0.05f, top + icon * 0.94f),
		ImVec2(left + icon * 0.74f, top + icon * 0.94f), white, 1.4f * scale);
	draw->AddLine(ImVec2(left + icon * 0.18f, top + icon * 0.20f),
		ImVec2(left + icon * 0.23f, top + icon * 0.06f), steam, 1.3f * scale);
	draw->AddLine(ImVec2(left + icon * 0.40f, top + icon * 0.20f),
		ImVec2(left + icon * 0.35f, top + icon * 0.06f), steam, 1.3f * scale);

	const char* label = "Ko-fi";
	const ImVec2 textSize = ImGui::CalcTextSize(label);
	const ImVec2 textPos(pMin.x + pad + icon + 6.0f * scale,
		pMin.y + (height - textSize.y) * 0.5f);
	draw->AddText(textPos, white, label);
}

static bool UiTabItem(const char* label)
{
	const bool open = ImGui::BeginTabItem(Tr(label));
	HandOver();
	return open;
}
static void UiColumn(const char* label, ImGuiTableColumnFlags flags = 0, float width = 0.0f)
{
	ImGui::TableSetupColumn(Tr(label), flags, width);
}
static bool UiInputInt(const char* label, int* value) { return ImGui::InputInt(Tr(label), value); }
static bool UiSliderInt(const char* label, int* value, int min, int max, const char* format = "%d")
{
	return ImGui::SliderInt(Tr(label), value, min, max, Tr(format));
}
static bool UiCombo(const char* label, int* current, const char* const items[], int count);

/*
 * Natural width of a combo showing `items`: the longest entry plus frame padding and the
 * drop-down arrow's square. Callers that place a combo after SameLine() use this to decide
 * whether it fits, before ComboRow() has to clamp it to the remaining space.
 */
static const float kComboFloorWidth = 120.0f;

static float ComboWidth(const char* const items[], int count)
{
	const ImGuiStyle& style = ImGui::GetStyle();
	const float chrome = style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();

	float width = kComboFloorWidth;
	for (int i = 0; i < count; i++)
	{
		const float w = ImGui::CalcTextSize(items[i]).x + chrome;
		if (w > width)
			width = w;
	}
	return width;
}

/*
 * A combo in a settings row.
 *
 * The width comes from the longest entry rather than a fixed 220 px: "Off" does not need a 220 px
 * box, and "Sharp + 4x anisotropic" was being clipped to "Sharp + 4x anis..." at that width. The
 * floor keeps a one-word list from becoming a target too small to hit; the ceiling is the row.
 *
 * Label placement follows ImGui's default (to the right of the widget), but it is drawn by hand so
 * the visible part can move above the widget when the row is too narrow for both. "Label##id" is
 * split: ImGui takes the widget's identity from the part after "##", which has to survive the
 * label moving.
 *
 * `label` is already translated and `items` are the strings actually shown, so this can be used from
 * both the hand-written pages and the generic CreateControls() renderer. The item pointers must
 * outlive the frame - ImGui keeps them while the popup is open - which is why callers translate into
 * a static array via UiItems().
 */
static bool ComboRow(const char* label, int* current, const char* const items[], int count)
{
	if (count <= 0)
		return false;

	const ImGuiStyle& style = ImGui::GetStyle();
	const char* idPos = std::strstr(label, "##");
	const std::string display = idPos != nullptr ? std::string(label, idPos - label) : std::string(label);
	const std::string id = idPos != nullptr ? std::string(idPos) : ("##" + display);

	const float avail = ImGui::GetContentRegionAvail().x;
	const float labelWidth = ImGui::CalcTextSize(display.c_str()).x;
	const float gap = style.ItemInnerSpacing.x;

	float width = ComboWidth(items, count);

	// Two attempts at keeping the label on the same line: shrink the widget to fit it, and if even
	// the floor width leaves no room, give the label its own line above a full-width widget.
	bool stacked = false;
	if (display.empty())
	{
		// No visible label ("##id" only): the widget may use the whole row.
	}
	else if (width + gap + labelWidth > avail)
	{
		width = avail - gap - labelWidth;
		if (width < kComboFloorWidth)
		{
			stacked = true;
			width = avail;
		}
	}
	if (width > avail)
		width = avail;
	if (width < 1.0f)
		width = 1.0f;

	if (stacked)
		ImGui::TextWrapped("%s", display.c_str());

	ImGui::SetNextItemWidth(width);
	const bool changed = ImGui::Combo(id.c_str(), current, items, count);
	HandOver();

	if (!stacked && !display.empty())
	{
		ImGui::SameLine();
		ImGui::TextUnformatted(display.c_str());
	}
	return changed;
}

static bool UiCombo(const char* label, int* current, const char* const items[], int count)
{
	return ComboRow(Tr(label), current, items, count);
}
static void UiTextColored(const ImVec4& colour, const char* label)
{
	PushHelpWrap();
	ImGui::TextColored(colour, "%s", Tr(label));
	PopHelpWrap();
}

/*
 * TextDisabled takes a printf format, so it cannot simply forward the label: the translated string
 * has to be formatted first. The result is handed to ImGui as a plain "%s" so that any percent
 * signs inside the translated text are not read as conversion specifiers.
 *
 * A small ring of buffers keeps concurrent or nested uses from overwriting each other within a
 * frame; ImGui copies the text it draws, so nothing has to outlive the call.
 */
static void UiTextDim(const char* format, ...)
{
	static thread_local char buffers[4][1024];
	static thread_local int next = 0;
	char* buffer = buffers[next];
	next = (next + 1) % 4;

	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffers[0]), Tr(format), args);
	va_end(args);

	PushHelpWrap();
	ImGui::TextDisabled("%s", buffer);
	PopHelpWrap();
}

/*
 * The one colour used to say "look at this line".
 *
 * Defined once because it now has two callers with different meanings - a problem (UiTextWarn) and a
 * status readout that simply has to be findable (UiTextHighlight, used for the controller-detection
 * line) - and a single "attention" colour is easier to learn than two. Keeping it in one place also
 * means a future change to how warnings look cannot silently repaint a status readout, which is what
 * would happen if each caller had its own copy of the value.
 */
static const ImVec4 kAttentionLight(0.620f, 0.360f, 0.000f, 1.00f); // #9E5C00 on warm paper
static const ImVec4 kAttentionDark(0.925f, 0.639f, 0.290f, 1.00f);  // #EC A3 4A on dark glass

// Warnings and attention lines. The colour is a guide, not decoration, so it must not rely on the
// palette's own text colour: #9E5C00 is legible on paper and nearly invisible on rock grey, so the
// two themes need two values.
static ImVec4 AttentionColour()
{
	return UiThemeIsDark() ? kAttentionDark : kAttentionLight;
}

/*
 * Warning-coloured line. Kept apart from UiTextDim so that a real problem does not read as one more
 * piece of the grey explanatory text around it.
 */
static void UiTextWarn(const char* format, ...)
{
	static thread_local char buffers[2][1024];
	static thread_local int next = 0;
	char* buffer = buffers[next];
	next = (next + 1) % 2;

	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffers[0]), Tr(format), args);
	va_end(args);

	PushHelpWrap();
	ImGui::TextColored(AttentionColour(), "%s", buffer);
	PopHelpWrap();
}

/*
 * The same amber, for a status readout rather than a fault.
 *
 * Exists because of the controller-detection line: whether a pad was recognised is the first thing
 * anyone looks for on that page, and as grey explanatory text it read the same as the paragraphs of
 * advice around it. Colouring it makes the answer visible without reading, which is the point.
 *
 * Named for what it does rather than reusing UiTextWarn, so that neither caller misrepresents the
 * other - see the note on the colour constant above.
 */
static void UiTextHighlight(const char* format, ...)
{
	static thread_local char buffers[2][1024];
	static thread_local int next = 0;
	char* buffer = buffers[next];
	next = (next + 1) % 2;

	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffers[0]), Tr(format), args);
	va_end(args);

	PushHelpWrap();
	ImGui::TextColored(AttentionColour(), "%s", buffer);
	PopHelpWrap();
}

// Tooltips take a printf format because several of them quote a measured value. They are drawn through
// DrawWrappedTooltip, which wraps them: see the note there on why ImGui's own SetTooltip does not.
static void UiTooltip(const char* format, ...)
{
	static thread_local char buffers[2][4096];
	static thread_local int next = 0;
	char* buffer = buffers[next];
	next = (next + 1) % 2;

	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffers[0]), Tr(format), args);
	va_end(args);

	DrawWrappedTooltip(buffer);
}

/*
 * Expands a Chinese option list into the active language for a combo.
 *
 * The translated pointers go into caller-owned storage rather than a buffer shared between calls,
 * because ImGui keeps the item pointers for as long as the popup is open: handing back one shared
 * buffer would let a second combo corrupt the list the first one is still showing.
 */
static const char** UiItems(const char* const* items, int count, const char** storage)
{
	for (int i = 0; i < count; ++i)
		storage[i] = Tr(items[i]);
	return storage;
}

// Keys that are drawn by hand somewhere else, so the generic renderer must leave them out.
//
// Most of these have a dedicated widget in the "常用" page. The graphics settings below are the
// other case: they have a bespoke page of their own ("高级设置") with help text explaining each
// option, and without this they would also appear in the generic "画面" list, which is how they
// ended up shown twice.
static bool HasDedicatedUI(const std::string& key)
{
    static const char* keys[] = {
#if !defined(SUPERMODEL_OPENGL_ONLY)
        "Vulkan3DEngine", "VkValidation", "VulkanDevice",
#endif
        "New3DEngine",
        "XResolution", "YResolution", "FullScreen", "Stretch", "WideScreen", "VSync", "ShowFrameRate", "ShowStatsOsd", "StatsOsdStyle",
        "IntegerScale", "UiLanguage",
        // Interface appearance. Drawn by hand at the top of the 常用 page, together with the
        // language: they are both about the window rather than about the emulator.
        "UITheme", "UiFrostedGlass",
        // Fullscreen refresh mode. The radio group on the 常用 page writes both keys together,
        // because they are one three-way choice and not two independent switches - the generic
        // list showed them as a bare 0.000000 float and a checkbox beside it, which invites setting
        // them to a combination the resolver then has to silently reinterpret.
        "ExclusiveFullscreen", "ExclusiveRefreshRate",
        // Presentation cadence, drawn by hand on the 常用 page. RefreshRateMode/FixedRefreshRate are
        // one three-way choice plus a rate, and the interpolation keys are a single decision with a
        // scope - the generic renderer would show them as raw numbers and unlabelled checkboxes.
        "RefreshRateMode", "FixedRefreshRate",
        "MotionInterpolation", "LowLatency", "InterpolationScope",
        "CameraCutAngleDegrees", "InstanceCutAngleDegrees", "CutDistanceScale",
        // Gamepad steering smoothing, drawn by hand on the 力反馈 / 灵敏度 page.
        "GamepadSteeringSmoothing",
        // Instance interpolation has no control: per-instance blending leaves a one-frame seam
        // between a snapped model and an interpolated neighbour, so it is not something to offer
        // as a checkbox. The key and SM3D_INST_INTERP stay for experiments; listed here so the
        // generic renderer does not put an unlabelled tick box on the page instead.
        "InstanceInterpolation",
        // Test hook for the phase multiples; not something to offer as a setting.
        "PaceAssumePanelHz",
        // Bezel backdrop. Hand-drawn on the "画面" page, where a display option belongs.
        "BezelMode", "BezelImage", "BezelBrightness",
#if !defined(SUPERMODEL_OPENGL_ONLY)
        // HD texture pack switches. Drawn by hand on the 核心 page and in the in-game panel,
        // where the reload button and the F4 hint belong together with them.
        "LoadHDTextures", "DumpHDTextures",
#endif
        // Paired with the backdrop by hand: it decides whether there are side bars for the backdrop
        // to fill, so the two are drawn together rather than leaving this among the runtime options.
        "WideBackground",
        // Picture options that are honoured by both renderers, drawn by hand at the top of the
        // 画面 page. They are in the ini's Video group, but the generic list put them on 核心 among
        // the runtime switches, where they read as emulation options rather than picture ones.
        "CRTcolors", "UpscaleMode",
        // Sinden light-gun border. Drawn by the Vulkan compositor, and its three controls are
        // hand-written on the 画面 page.
        "SindenBorder", "SindenBorderWidth", "SindenBorderBrightness",
        // Anti-aliasing and post-processing filters. These live in "高级设置" only.
        "MSAA", "TexFilter", "AlphaSortMode", "PostFXAA",
        "QuadInterpolation", "InternalResolution",
        "PostCRT", "PostScanlines", "PostScanlineIntensity",
        "PostMaskType", "PostMaskScale", "PostMaskIntensity",
        "PostGlow", "PostGlowLevels", "PostGlowThreshold", "PostGain",
        "SoundVolume", "MusicVolume", "EmulateSound", "EmulateDSB", "AudioDevice",
        // Retired: the SCSP DSP has a single implementation now. An old .ini or a front-end that
        // still passes -legacy-scsp keeps working, but the key no longer does anything, so it must
        // not be offered as a switch.
        "LegacySoundDSP",
        "InputSystem", "ForceFeedback",
        "Network", "SimulateNet", "PortIn", "PortOut", "AddressOut"
    };
    for (const char* name : keys)
        if (key == name)
            return true;
    return false;
}

static std::string WidgetLabel(const std::string& key)
{
    static const std::map<std::string, const char*> labels = {
        { "PowerPCFrequency", "PowerPC 频率 (MHz，0=自动)" },
        { "MultiThreaded", "多线程" },
        { "GPUMultiThreaded", "图形多线程" },
        { "QuadRendering", "四边形渲染" },
        { "BorderlessWindow", "无边框窗口" },
        { "Supersampling", "超采样" },
        { "CRTcolors", "CRT 色彩" },
        { "UpscaleMode", "2D 放大" },
        { "WideBackground", "宽屏背景" },
        { "Throttle", "锁定帧率" },
        { "RefreshRate", "刷新率" },
        { "Crosshairs", "准星" },
        { "CrosshairStyle", "准星样式" },
        { "NoWhiteFlash", "关闭白闪" },
        { "Balance", "前后平衡" },
        { "BalanceLeftRight", "左右平衡" },
        { "BalanceFrontRear", "前后声道" },
        { "NbSoundChannels", "声道数" },
        { "SoundFreq", "音频刷新率" },
        { "FlipStereo", "交换左右声道" },
        { "DirectInputConstForceLeftMax", "恒力（左）" },
        { "DirectInputConstForceRightMax", "恒力（右）" },
        { "DirectInputSelfCenterMax", "回中力" },
        { "DirectInputFrictionMax", "摩擦力" },
        { "DirectInputVibrateMax", "振动" },
        { "XInputConstForceThreshold", "XInput 恒力阈值" },
        { "XInputConstForceMax", "XInput 恒力" },
        { "XInputVibrateMax", "XInput 振动" },
        { "XInputStereoVibration", "XInput 立体振动" },
        { "SDLConstForceMax", "SDL 恒力" },
        { "SDLSelfCenterMax", "SDL 回中力" },
        { "SDLFrictionMax", "SDL 摩擦力" },
        { "SDLVibrateMax", "SDL 振动" },
        { "SDLConstForceThreshold", "SDL 恒力阈值" },
        { "Outputs", "输出" },
        { "OutputsWithLF", "输出加换行" },
        { "OutputsTCPPort", "TCP 端口" },
        { "OutputsUDPBroadcastPort", "UDP 端口" },
        { "DumpMemory", "退出时导出内存" },
        { "DumpTextures", "退出时导出贴图" },
        { "InputDigitalSensitivity", "数字灵敏度" },
        { "InputDigitalDecaySpeed", "数字回中速度" },
        { "InputKeySensitivity", "键盘灵敏度" },
        { "InputKeyDecaySpeed", "键盘回中速度" },
        { "InputMouseXDeadZone", "鼠标 X 死区" },
        { "InputMouseYDeadZone", "鼠标 Y 死区" },
        { "InputMouseZDeadZone", "鼠标 Z 死区" },
    };

    auto found = labels.find(key);
    if (found == labels.end())
        return key;
    // The label is translated but the "##key" suffix is not: ImGui takes the widget's identity from
    // the part after "##", so keeping it means the control keeps its identity across a language
    // change instead of being seen as a different widget.
    return std::string(Tr(found->second)) + "##" + key;
}

/*
 * Help text for a generated control.
 *
 * Options with a dedicated page carry their own (?) markers next to the widget. These are the ones
 * that only ever get a label in a list, where a name like "Disable White Flash" says the what but
 * not the why. The text is written in Chinese and goes through Tr(), like every other explanation,
 * so it follows the interface language.
 */
static const char* KeyHelpText(const std::string& key)
{
    static const std::map<std::string, const char*> help = {
        { "NoWhiteFlash",
            "读档和切场景时，街机硬件会把整屏刷白；开启后跳过这层白闪。\n"
            "只影响观感，不影响模拟结果。" },
    };
    const auto found = help.find(key);
    return found == help.end() ? nullptr : found->second;
}

// Defined further down with the other widget helpers; declared here because the generated controls
// use it for the per-key help above.
static void HelpMarker(const char* text);

static void UpdateTempValues(Util::Config::Node& config, const std::string group, bool init)
{
    for (auto it = config.begin(); it != config.end(); ++it)
    {
        if (it->IsLeaf() && it->Exists()) {

            auto key = it->Key();
            auto val = it->GetValue();

            if (val) {

                auto vRange = val->GetValueRange();

                if (vRange) {

                    if (vRange->GetGroup() == group) {

                        auto index = vRange->GetIndex();

                        switch (index) {
                        case 0:
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<bool>(); }
                            else { config.Set(key, std::get<bool>(vRange->tempValue)); }
                            break;
                        }
                        case 1:
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<unsigned>(); }
                            else { config.Set(key, std::get<unsigned>(vRange->tempValue)); }
                            break;
                        }
                        case 2:
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<int>(); }
                            else { config.Set(key, std::get<int>(vRange->tempValue)); }
                            break;
                        }
                        case 3:
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<float>(); }
                            else { config.Set(key, std::get<float>(vRange->tempValue)); }
                            break;
                        }
                        case 4:
                        {
                            if (init) { vRange->tempValue = config[key].ValueAs<std::string>(); }
                            else { config.Set(key, std::get<std::string>(vRange->tempValue)); }
                            break;
                        }
                        }

                        it->GetValue()->SetValueRange(vRange);
                    }
                }
            }
        }
    }
}

/*
 * Which renderer actually honours a setting.
 *
 * Added because the interface offered every option under every renderer, so two different
 * anti-aliasing systems (OpenGL's Supersampling, Vulkan's MSAA) sat side by side looking like two
 * halves of one feature, and a user could reasonably set both and expect the effect to double.
 * Whichever backend is running silently ignores the other's options - the Vulkan path even logs that
 * supersampling is unavailable and runs at 1x - so the only honest thing the interface can do is say
 * so, per option, where the option is.
 *
 * The classification is by *where each key is read*, not by intuition:
 *
 *   Vulkan   read only in Graphics/Vulkan3D
 *   OpenGL   read only outside it (OpenGL only, either backend)
 *   New3D    read only in Graphics/New3D
 *   Legacy3D read only in Graphics/Legacy3D
 *
 * Anything not listed is shared and stays enabled: IntegerScale and NoWhiteFlash are applied in code
 * common to both, and the display, audio, input and network pages have nothing to do with the
 * renderer at all.
 *
 * Checked by grepping every key against each renderer directory rather than by reading the manual -
 * the manual describes the upstream program, which has no Vulkan backend to distinguish.
 */
enum class SettingScope
{
    Any,
    Vulkan,
    OpenGL,
    New3D,
    Legacy3D,
};

template <size_t N>
static bool KeyInList(const std::string& key, const char* const (&list)[N])
{
    for (size_t i = 0; i < N; i++)
        if (key == list[i])
            return true;
    return false;
}

static SettingScope ScopeOfSetting(const std::string& key)
{
    static const char* const vulkanOnly[] = {
        // Hardware MSAA is created with the Vulkan render targets. The OpenGL backends antialias a
        // different way, through Supersampling below - the two are alternatives, not layers.
        "MSAA",
        // The scene shader's exact quad reconstruction. The OpenGL quad path is a separate key
        // (QuadRendering), so this one must not be offered while an OpenGL renderer is running.
        "QuadInterpolation",
        // The whole post-processing chain is Vulkan2D's filter pass.
        "PostFXAA", "PostCRT", "PostScanlines", "PostScanlineIntensity",
        "PostMaskType", "PostMaskScale", "PostMaskIntensity",
        "PostGlow", "PostGlowLevels", "PostGlowThreshold", "PostGain",
        // The cabinet backdrop is composed into the black bars by Vulkan2D.
        "BezelMode", "BezelImage", "BezelBrightness",
        // The light-gun border is a pass of its own in Vulkan2D.
        "SindenBorder", "SindenBorderWidth", "SindenBorderBrightness",
    };
    static const char* const openGlOnly[] = {
        // Supersampling is the OpenGL anti-aliasing path. Main.cpp forces it to 1x under Vulkan;
        // Vulkan has hardware MSAA instead.
        "Supersampling",
    };
    static const char* const new3dOnly[] = {
        // A New3D pipeline choice (R3DShader picks the quad or triangle program).
        "QuadRendering",
    };
    static const char* const legacyOnly[] = {
        // Legacy3D's externally supplied shaders and multi-sheet texturing.
        "MultiTexture", "VertexShader", "FragmentShader",
    };

    if (KeyInList(key, vulkanOnly)) return SettingScope::Vulkan;
    if (KeyInList(key, openGlOnly)) return SettingScope::OpenGL;
    if (KeyInList(key, new3dOnly))  return SettingScope::New3D;
    if (KeyInList(key, legacyOnly)) return SettingScope::Legacy3D;
    return SettingScope::Any;
}

static bool ScopeAppliesToCurrentRenderer(SettingScope scope, bool vulkan, bool new3d)
{
    switch (scope)
    {
    case SettingScope::Any:      return true;
    case SettingScope::Vulkan:   return vulkan;
    case SettingScope::OpenGL:   return !vulkan;
    case SettingScope::New3D:    return !vulkan && new3d;
    case SettingScope::Legacy3D: return !vulkan && !new3d;
    }
    return true;
}

// The key is the Chinese text, as everywhere else; Tr() looks it up.
static const char* ScopeReasonKey(SettingScope scope)
{
    switch (scope)
    {
    case SettingScope::Vulkan:   return "仅 Vulkan 渲染引擎有效，当前不是。";
    case SettingScope::OpenGL:   return "仅 OpenGL 渲染引擎有效，当前不是。";
    case SettingScope::New3D:    return "仅 New 3D 引擎有效，当前不是。";
    case SettingScope::Legacy3D: return "仅 Legacy 3D 引擎有效，当前不是。";
    default:                     return "";
    }
}

// Defined below with the display cache; declared here so the Refresh rate control can ask it
// whether the G-SYNC path is going to lock the emulator to the arcade rate.
static bool VrrArcadeRateSupported();

static void CreateControls(Util::Config::Node& config, const std::string group)
{
    // Read once rather than per key: the renderer cannot change while a page is being drawn, and
    // looking it up in the loop would be a lookup per option for no reason.
    const bool engineIsVulkan = config["Vulkan3DEngine"].ValueAsDefault<bool>(false);
    const bool engineIsNew3D  = config["New3DEngine"].ValueAsDefault<bool>(true);

    for (auto it = config.begin(); it != config.end(); ++it)
    {
        if (it->IsLeaf() && it->Exists()) {

            auto key = it->Key();
            auto val = it->GetValue();

            if (val) {

                auto vRange = val->GetValueRange();

                if (vRange) {

                    if (vRange->GetGroup() == group) {

                        if (HasDedicatedUI(key))
                            continue;

#if defined(SUPERMODEL_PURE_VULKAN)
                        /*
                         * RefreshRate is the frame limiter of the original one-simulation-one-present
                         * loop. The Vulkan loop paces itself through CDisplayPacer from
                         * RefreshRateMode/FixedRefreshRate, so this key no longer affects anything and
                         * must not be offered as if it did. The OpenGL build still uses it, and the
                         * dual build keeps it because its fallback loop does too.
                         */
                        if (key == "RefreshRate")
                            continue;
#endif

#if defined(SUPERMODEL_PLATFORM_SDL3)
                        /*
                         * This build has no DirectInput or XInput backend - GetInputSystem() maps
                         * every choice to the SDL one - so those force-feedback keys do nothing
                         * here. Leaving them on the page is actively misleading: a pad that SDL
                         * reports as a HIDAPI device (a Switch Pro controller, say) reads as
                         * "the only vibration setting is XInput", while the SDL keys just below
                         * are the ones that apply.
                         */
                        if (key.rfind("DirectInput", 0) == 0 || key.rfind("XInput", 0) == 0)
                            continue;
#endif

                        const std::string labelWithId = WidgetLabel(key);
                        const std::string::size_type idPos = labelWithId.find("##");
                        const std::string displayLabel = idPos == std::string::npos
                            ? labelWithId : labelWithId.substr(0, idPos);
                        const std::string idLabel = idPos == std::string::npos
                            ? std::string("##") + key : labelWithId.substr(idPos);

                        auto index = vRange->GetIndex();
                        auto& list = vRange->GetList();

                        /*
                         * Where the label goes.
                         *
                         * ImGui draws a widget's label to the right of the widget itself, so a
                         * long label on a narrow panel is simply clipped - which is what happens
                         * to "SDL Constant Force Threshold" on macOS, where the settings panel is
                         * narrower than on a 150% Windows desktop. Give the label a fixed share of
                         * the row, and when even that is not enough move it above the widget and
                         * let the widget use the full width. Checkboxes keep their inline label:
                         * the box is small, so the label already has nearly the whole row.
                         */
                        const ImGuiStyle& style = ImGui::GetStyle();
                        const float availableWidth = ImGui::GetContentRegionAvail().x;
                        const float desiredItemWidth = availableWidth * 0.55f;
                        
                        // Combos size and place themselves (see ComboRow): their width follows the
                        // longest entry, which is what keeps "Off" from taking a whole row while a
                        // long option is clipped. The block below is for the other widget shapes.
                        const bool listControl = index != 0 && !vRange->HasMinMax() && !list.empty();

                        const bool stackLabel = index != 0 &&
                            (availableWidth - desiredItemWidth - style.ItemInnerSpacing.x) <
                                ImGui::CalcTextSize(displayLabel.c_str()).x;

                        const char* uiLabel = stackLabel ? idLabel.c_str() : labelWithId.c_str();
                        if (!listControl)
                        {
                            if (stackLabel)
                            {
                                ImGui::TextWrapped("%s", displayLabel.c_str());
                                ImGui::SetNextItemWidth(-FLT_MIN);
                            }
                            else if (index != 0)
                            {
                                ImGui::SetNextItemWidth(desiredItemWidth);
                            }
                        }

                        auto ProcessCombo = [&](auto* valuePtr)
                        {
                            using T = std::decay_t<decltype(*valuePtr)>;

                            int selectedIndex = 0;
                            int loopCount = 0;
                            std::vector<std::string> sVector;
                            std::vector<const char*> sVectorChar;

                            for (auto& l : list) {
                                auto value = std::get<T>(l);
                                sVector.emplace_back(std::to_string(value));

                                if (value == *valuePtr) {
                                    selectedIndex = loopCount;
                                }
                                loopCount++;
                            }

                            if (key == "CRTcolors" && sVector.size() == 6) {
                                static const char* names[] = { "关闭", "ARI/D93（日版）", "PVM", "BT.601 525/D93", "BT.601 525/D65（美版）", "BT.601 625/D65（欧版）" };
                                for (int i = 0; i < 6; i++)
                                    sVector[i] = Tr(names[i]);
                            }
                            if (key == "UpscaleMode" && sVector.size() == 4) {
                                static const char* names[] = { "锐利", "双五次", "双线性", "双三次" };
                                for (int i = 0; i < 4; i++)
                                    sVector[i] = Tr(names[i]);
                            }
                            if (key == "RefreshRate" && sVector.size() == 2) {
                                static const char* names[] = { "57.524 Hz（真实机台）", "60 Hz（非 VRR / 60Hz 面板）" };
                                for (int i = 0; i < 2; i++)
                                    sVector[i] = Tr(names[i]);
                            }
                            if (key == "SoundFreq" && sVector.size() == 2) {
                                static const char* names[] = { "57.524 Hz", "60 Hz" };
                                for (int i = 0; i < 2; i++)
                                    sVector[i] = Tr(names[i]);
                            }
                            if (key == "Crosshairs" && sVector.size() == 4) {
                                static const char* names[] = { "关闭", "仅玩家 1", "仅玩家 2", "玩家 1 和 2" };
                                for (int i = 0; i < 4; i++)
                                    sVector[i] = Tr(names[i]);
                            }

                            for (auto& s : sVector) {
                                sVectorChar.emplace_back(s.c_str());
                            }

                            // A fixed arcade refresh rate, or the G-SYNC path on a display whose
                            // timing range contains 57.5 Hz, overrides this setting. Show the value
                            // that will actually be used and make it read-only, rather than leaving a
                            // number on screen that has no effect.
                            //
                            // The stored preference is deliberately left untouched: it is still what
                            // takes over if the override is turned off again, so silently rewriting
                            // it here would lose the user's choice.
                            const bool isRefreshRate = (key == "RefreshRate");
                            const bool fixedRateOverride = isRefreshRate &&
                                config["ExclusiveRefreshRate"].ValueAsDefault<float>(0.0f) > 0.0f;
                            const bool vrrAutoRate = isRefreshRate &&
                                config["ExclusiveRefreshRate"].ValueAsDefault<float>(0.0f) <= 0.0f &&
                                config["ExclusiveFullscreen"].ValueAsDefault<bool>(false) &&
                                VrrArcadeRateSupported();
                            const bool rateOverridden = fixedRateOverride || vrrAutoRate;

                            if (rateOverridden) {
                                selectedIndex = 0;      // 57.524 Hz, the arcade rate
                                ImGui::BeginDisabled();
                            }
                            ComboRow(labelWithId.c_str(), &selectedIndex, sVectorChar.data(), (int)sVectorChar.size());
                            if (rateOverridden) {
                                ImGui::EndDisabled();
                                if (fixedRateOverride)
                                    UiTextDim("（由「独占全屏 · 固定街机刷新率」决定）");
                                else
                                    UiTextDim("（G-SYNC 自动使用街机 57.5Hz）");
                            }
                            else {
                                vRange->tempValue = list[selectedIndex];
                                // The G-SYNC path applies this setting only when the display's range
                                // does not confirm 57.5 Hz; otherwise the override above is showing the
                                // arcade rate instead. Worth saying plainly, since the two cases look
                                // the same at a glance.
                                if (isRefreshRate && config["ExclusiveFullscreen"].ValueAsDefault<bool>(false)) {
                                    UiTextDim("（G-SYNC 下为呈现节奏，显示器跟随）");
                                }
                            }
                        };

                        auto ProcessScalar = [&](auto valuePtr, ImGuiDataType type)
                        {
                            using T = std::decay_t<decltype(*valuePtr)>;
                            auto min_ = std::get<T>(vRange->GetMin());
                            auto max_ = std::get<T>(vRange->GetMax());
                            ImGui::SliderScalar(uiLabel, type, valuePtr, &min_, &max_);
                        };

                        auto ProcessControls = [&](auto valuePtr, ImGuiDataType type)
                        {
                            if (vRange->HasMinMax()) {
                                ProcessScalar(valuePtr, type);
                            }
                            else if (list.size()) {
                                ProcessCombo(valuePtr);
                            }
                            else {
                                ImGui::InputScalar(uiLabel, type, valuePtr);
                            }
                        };

                        /*
                         * Shown disabled with the reason beside it, rather than hidden.
                         *
                         * Hiding it would make the interface change shape when the renderer changes,
                         * which is disorienting and hides the fact that the option exists at all.
                         * Greyed out with a reason says both things at once: this is here, and it is
                         * not for the renderer you are running.
                         *
                         * The edit is blocked rather than merely discouraged - BeginDisabled stops
                         * input - so the temp-value mechanism cannot collect a change that the
                         * running backend would then ignore.
                         */
                        const SettingScope scope = ScopeOfSetting(key);
                        const bool applies = ScopeAppliesToCurrentRenderer(scope, engineIsVulkan, engineIsNew3D);
#if defined(SUPERMODEL_PURE_VULKAN)
                        /*
                         * This build has exactly one renderer and always will, so an option belonging to
                         * another one is not "disabled until you switch" - it can never apply. Greyed out
                         * with an explanation it only made the Video page look broken, with four rows of
                         * "this is not the one running" between the settings that do work.
                         */
                        if (!applies)
                            continue;
#endif
                        if (!applies)
                            ImGui::BeginDisabled();

                        switch (index) {
                        case 0:
                        {
                            auto p = std::get_if<bool>(&vRange->tempValue);
                            UiCheckbox(uiLabel, p);
                            break;
                        }
                        case 1:
                        {
                            auto p = std::get_if<unsigned>(&vRange->tempValue);
                            ProcessControls(p, ImGuiDataType_U32);
                            break;
                        }
                        case 2:
                        {
                            auto p = std::get_if<int>(&vRange->tempValue);
                            ProcessControls(p, ImGuiDataType_S32);
                            break;
                        }
                        case 3:
                        {
                            auto p = std::get_if<float>(&vRange->tempValue);
                            ProcessControls(p, ImGuiDataType_Float);
                            break;
                        }
                        case 4:
                        {
                            auto p = std::get_if<std::string>(&vRange->tempValue)->c_str();
                            auto& option = std::get<std::string>(vRange->tempValue);

                            auto& choices = vRange->GetList();

                            if (choices.size()) {

                                int selectedIndex = 0;
                                int loopCount = 0;
                                std::vector<const char*> sVector;

                                for (auto& l : choices) {
                                    auto& item = std::get<std::string>(l);
                                    if (option == item) {
                                        selectedIndex = loopCount;
                                    }
                                    sVector.emplace_back(item.c_str());
                                    loopCount++;
                                }

                                ComboRow(labelWithId.c_str(), &selectedIndex, sVector.data(), (int)sVector.size());
                                vRange->tempValue = choices[selectedIndex];
                            }
                            else {
                                char buffer[256];
                                std::strcpy(buffer, p);
                                ImGui::InputText(uiLabel, buffer, IM_ARRAYSIZE(buffer));
                                option = buffer;
                            }

                            break;
                        }
                        }

                        if (!applies)
                        {
                            ImGui::EndDisabled();
                            // The reason is a sentence, not a suffix: drawing it on the same line
                            // squeezed it into whatever width was left beside the widget, which on
                            // a narrow panel wrapped it one character per line.
                            UiTextDim("%s", Tr(ScopeReasonKey(scope)));
                        }
                        else if (const char* help = KeyHelpText(key); help != nullptr)
                        {
                            HelpMarker(help);
                        }

                        if (key == "PowerPCFrequency")
                            UiTextDim("0 = 按主板自动。Step 1.0 是 66 MHz，1.5 是 100 MHz，2.x 是 166 MHz。Daytona 2 是 2.1，保持 0 就是 166 MHz。");
                    }
                }
            }
        }
    }
}

static void SetDefaultKeyVal(std::shared_ptr<CInput> input)
{
    std::string key = std::string("Input") + input->id;
    auto defaultConfig = DefaultConfig();
    auto mapping = defaultConfig[key.c_str()].ValueAs<std::string>();
    input->SetMapping(mapping.c_str());
}

static std::vector<std::string> SplitByComma(const std::string& input)
{
    std::vector<std::string> result;
    size_t start = 0;
    size_t end;

    while ((end = input.find(',', start)) != std::string::npos) {
        result.emplace_back(input.substr(start, end - start));
        start = end + 1;
    }

    result.emplace_back(input.substr(start));
    return result;
}

struct KeyBindState
{
    std::shared_ptr<CInput> input;
    bool waitingForInput = false;
    int processKeyCount = 0;

    void Reset()
    {
        input = nullptr;
        waitingForInput = false;
        processKeyCount = 0;
    }
};

static void BindKeys(Util::Config::Node& config, KeyBindState& kb, bool openPopup)
{
    bool finish = false;
    bool appendPressed = false;

    if (openPopup) {
        ImGui::OpenPopup("Key Binding");
    }

    if (ImGui::BeginPopupModal("Key Binding", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

        if (ImGui::BeginTable("ShortcutTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {

            UiColumn("动作");
            UiColumn("按键");
            ImGui::TableHeadersRow();

            auto keyList = SplitByComma(kb.input->GetMapping());

            for (auto& k : keyList) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", kb.input->label);
                ImGui::TableNextColumn();
                ImGui::Text("%s", k.c_str());
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();

        if (kb.waitingForInput) {
            UiText("请按键。Esc 取消。");
            kb.processKeyCount++;
        }
        else {
            ImGui::NewLine();
        }
        ImGui::Spacing();

        if (kb.waitingForInput) {
            ImGui::BeginDisabled();
        }

        if (UiButton("重设", ImVec2(120, 0))) {
            kb.input->ClearMapping();
            appendPressed = true;
        }

        if (UiButton("追加", ImVec2(120, 0))) {
            appendPressed = true;
        }

        if (UiButton("清空", ImVec2(120, 0))) {
            kb.input->ClearMapping();
            kb.input->StoreToConfig(&config);
        }

        if (UiButton("默认", ImVec2(120, 0))) {
            SetDefaultKeyVal(kb.input);
            kb.input->StoreToConfig(&config);
        }

        if (UiButton("完成", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            finish = true;
        }

        if (kb.waitingForInput) {
            ImGui::EndDisabled();
        }

        ImGui::EndPopup();

        if (kb.processKeyCount > 2) {
            auto system = kb.input->GetInputSystem();
            system->UngrabMouse();
            kb.input->Configure(true);
            system->GrabMouse();
            kb.input->StoreToConfig(&config);
            kb.processKeyCount = 0;
            kb.waitingForInput = false;
        }

        if (appendPressed) {
            kb.waitingForInput = true;
        }

        if (finish) {
            kb.Reset();
        }
    }
}

static void AddKeys(Util::Config::Node& config, KeyBindState& kb, std::vector<std::shared_ptr<CInput>> keyInputs)
{
    bool openPopup = false;

    for (auto& k : keyInputs) {
        auto mapping = k->GetMapping();
        auto group = k->GetInputGroup();
        auto label = k->label;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        // Both strings come from CInput and are written in English there, because the input system
        // also prints them to the console and into the log. They are only translated for display,
        // which is why the lookup happens here rather than inside GetInputGroup().
        //
        // TrInputText rather than Tr: these are English source strings, and Tr() translates the
        // other way (Chinese source -> English) and only runs in English mode.
        ImGui::Text("%s", TrInputText(group));
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Selectable(TrInputText(label), false, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                kb.input = k;
                openPopup = true;
            }
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%s", mapping);
    }

    BindKeys(config, kb, openPopup);
}

static std::shared_ptr<CInputs> GetInputSystem(Util::Config::Node& config, OSD::Platform::Window* window)
{
    std::string selectedInputSystem = config["InputSystem"].ValueAs<std::string>();
    std::shared_ptr<CInputSystem> inputSystem;

#if !defined(SUPERMODEL_PLATFORM_SDL2)
    // The SDL3 binary has no DirectInput/Raw Input backend. Both executables share one Data/Config
    // tree, so a configuration written for the SDL2 build will name one of them - fall back rather
    // than starting a session with no controls at all.
    if (selectedInputSystem != "sdl" && selectedInputSystem != "sdlgamepad")
    {
        InfoLog("Input: '%s' is not available in the SDL3 build; using 'sdlgamepad'.",
            selectedInputSystem.c_str());
        selectedInputSystem = "sdlgamepad";
    }
#endif

    if (selectedInputSystem == "sdl")               { inputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(config, false)); }
    else if (selectedInputSystem == "sdlgamepad")   { inputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(config, true)); }

#if defined(_WIN32) && defined(SUPERMODEL_PLATFORM_SDL2)
    else if (selectedInputSystem == "dinput")       { inputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(config, window, false, false)); }
    else if (selectedInputSystem == "xinput")       { inputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(config, window, false, true)); }
    else if (selectedInputSystem == "rawinput")     { inputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(config, window, true, false)); }
#endif

#if !defined(_WIN32) || !defined(SUPERMODEL_PLATFORM_SDL2)
    (void)window;
#endif

    if (inputSystem) {
        auto inputs = std::shared_ptr<CInputs>(new CInputs(inputSystem));
        inputs->Initialize();
        inputs->LoadFromConfig(config);

        int x, y, w, h;
        OSD::Platform::GetWindowPosition(window, &x, &y);
        OSD::Platform::GetWindowPixelSize(window, &w, &h);
        inputSystem->SetDisplayGeom(x, y, w, h);
        return inputs;
    }

    return nullptr;
}

struct RomEntry
{
    std::string title;
    std::string version;    // region and revision, e.g. "Japan, Revision A"
    std::string setName;    // the archive name - what the ROM column shows and the row is keyed by
    // The set the archive was identified as, which can differ from the file name: an archive called
    // scud.zip may hold the Australian scudau ROMs, and a complete set may sit under a name that is
    // not in Games.xml. Empty when nothing could be identified.
    std::string resolvedSet;
    std::string path;
    int year = 0;
    // Whether Games.xml has a definition for this set. Every .zip in the folder is listed, but one
    // with no definition cannot be started at all - GameLoader refuses it with "unknown game" - so
    // the list has to say which ones those are rather than presenting them as ordinary entries.
    bool supported = false;
};

static std::string g_romFolderCached;
static std::vector<RomEntry> g_roms;

// The set an entry is really for. Box art and the game blurb are keyed by set name, and an archive
// whose contents were identified (scuda.zip holding the Export scud set, say) has to look them up
// under the name it resolves to rather than the name of the file.
static const std::string& EffectiveSet(const RomEntry& entry)
{
    return entry.resolvedSet.empty() ? entry.setName : entry.resolvedSet;
}

// Published by the interface each frame. The save manager needs both for questions the ROM folder
// cannot answer on its own: naming a set that has no zip of its own (a merged archive resolves to a
// regional child set such as scudau), and resolving a row's archive the way the game will run it.
static const std::map<std::string, Game>* g_gameDefinitions = nullptr;
static const GameLoader* g_gameLoader = nullptr;

// Fraction of the left column taken by the game preview. Written when the splitter is dragged and
// read back on the next launch (from the Global section, like GuiWindowWidth/Height). -1 means the
// stored value has not been read yet.
static float g_previewSplit = -1.0f;

/*
 * Game-list state that survives a restart.
 *
 * LastSelectedGame is the row's ROM set name rather than its index: the list is
 * rebuilt from whatever zips are in the ROM folder, and an index would silently
 * point at a different game as soon as one zip was added or removed. The filter
 * and the table's vertical scroll are stored alongside it, because "the list was
 * where I left it" is all three things at once.
 *
 * Scroll restore is deliberately two frames late: the table's content extent is
 * only known after its rows have been submitted, so SetScrollY() in the same frame
 * the child is first built would be clamped to a range that does not exist yet.
 * The pending flag keeps applying until the position sticks (or four frames have
 * passed, for a list that became shorter), and the saved value is never
 * overwritten in the meantime.
 */
static bool g_gameListStateRestored = false;
static bool g_gameListScrollRestorePending = false;
static int g_gameListScrollRestoreFrames = 0;
static float g_gameListScrollRestoreY = 0.0f;
static float g_gameListScrollY = 0.0f;
static char g_gameListFilter[128] = {};
// The last set the user actually had selected. The Confirm Exit popup clears
// selectedGameIndex to mean "do not start a game", so saving that field directly
// would forget the selection every time the program was quit from the button.
static std::string g_lastSelectedSet;

// Set when the running game was started from Manage Saves -> Load. The next
// GuiRunOnce() call is then a return from that game, and reopens the manager for
// the same row instead of dropping the user back at the top-level game list.
// Starting a game from the list clears it, so an ordinary session still returns
// to the list. Declared outside the PURE_VULKAN block because the list's launch
// paths clear it in every build.
static bool g_returnToManageSaves = false;
static std::string g_returnToManageSavesSet;

// Controller detection for the settings window. DetectController() enumerates SDL devices, which is
// not something to do every frame, so the result is cached. The cache is invalidated whenever a pad
// is plugged in or removed and by the 重新检测 button, so plugging a controller in while the window
// is open is picked up without a restart.
static ControllerInfo g_detectedController;
static bool g_controllerDetected = false;

static void RefreshDetectedController()
{
    g_detectedController = DetectController();
    g_controllerDetected = true;
}

static std::string ToLowerAscii(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return text;
}

#if !defined(_WIN32)
/*
 * Non-Windows counterpart of the foreground-raising helper below. There is no
 * foreground lock to defeat here, so bringing the window back is enough.
 */
static void RaiseSettingsWindow(OSD::Platform::Window* window)
{
    if (window != nullptr)
        OSD::Platform::ShowWindow(window);
}
#endif

#ifdef _WIN32
static std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 1)
        return {};
    std::wstring wide(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    return wide;
}

static std::string WideToUtf8(const wchar_t* text)
{
    if (!text || !text[0])
        return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string utf8(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

static std::filesystem::path PathFromUtf8(const std::string& text)
{
    return std::filesystem::path(Utf8ToWide(text));
}

static HWND WindowHandle(OSD::Platform::Window* window)
{
    return static_cast<HWND>(OSD::Platform::GetNativeWindowHandle(window));
}

/*
 * Brings the settings window to the foreground after startup or a game session.
 *
 * On a first run Windows SmartScreen owns the foreground while the process starts. The window is
 * created normally but is not activated: it appears on the taskbar, behind whatever had focus, and
 * the user has to click it before the interface shows. SDL_ShowWindow/SDL_RaiseWindow do not
 * override the foreground lock.
 *
 * AttachThreadInput is the documented way to take the foreground while another thread owns it. It
 * is attached only around the call and detached immediately; this runs at startup and when
 * returning from a game, not per frame.
 */
static void RaiseSettingsWindow(OSD::Platform::Window* window)
{
    if (window == nullptr)
        return;

#ifdef _WIN32
    HWND hwnd = WindowHandle(window);
    if (hwnd == nullptr)
        return;

    // Restore first: a process launched through SmartScreen can arrive minimized, in which case
    // setting the foreground alone would leave it on the taskbar.
    ShowWindow(hwnd, SW_RESTORE);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD currentThread = GetCurrentThreadId();
    const bool attached = foregroundThread != 0 && foregroundThread != currentThread &&
        AttachThreadInput(foregroundThread, currentThread, TRUE) != 0;
    SetForegroundWindow(hwnd);
    if (attached)
        AttachThreadInput(foregroundThread, currentThread, FALSE);
#else
    OSD::Platform::ShowWindow(window);
    OSD::Platform::RaiseWindow(window);
#endif
}

static bool BrowseFolder(OSD::Platform::Window* window, std::string& path)
{
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninit = SUCCEEDED(init);

    IFileDialog* dialog = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(Utf8ToWide(Tr("选择 Model 3 ROM 目录")).c_str());
        if (!path.empty())
        {
            IShellItem* start = nullptr;
            std::wstring wide = Utf8ToWide(path);
            if (!wide.empty() && SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(&start))))
            {
                dialog->SetFolder(start);
                start->Release();
            }
        }
        if (SUCCEEDED(dialog->Show(WindowHandle(window))))
        {
            IShellItem* result = nullptr;
            if (SUCCEEDED(dialog->GetResult(&result)))
            {
                PWSTR name = nullptr;
                if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &name)))
                {
                    path = WideToUtf8(name);
                    CoTaskMemFree(name);
                    ok = true;
                }
                result->Release();
            }
        }
        dialog->Release();
    }

    if (uninit)
        CoUninitialize();
    return ok;
}

/*
 * Picks a settings (.ini) file to load.
 *
 * Shares the COM setup with BrowseFolder above, and like it the dialog is modal to the window so the
 * interface cannot be driven while it is open. The .ini filter is only a convenience: the parser
 * accepts any file, so the "All files" entry stays available for a settings file that was renamed
 * or saved with a different extension.
 */
static bool BrowseSettingsFile(OSD::Platform::Window* window, std::string& path)
{
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninit = SUCCEEDED(init);

    IFileDialog* dialog = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        dialog->SetTitle(Utf8ToWide(Tr("选择要载入的设置文件")).c_str());

        // Tr() hands back UTF-8, but the shell API takes UTF-16, so the descriptions are converted
        // first and kept alive in locals: the filter array only borrows the pointers, and pointing
        // it at the temporaries returned by Utf8ToWide() would leave it holding freed memory.
        // The patterns are ASCII and need no translation.
        const std::wstring iniFilter = Utf8ToWide(Tr("Supermodel 设置 (*.ini)"));
        const std::wstring allFilter = Utf8ToWide(Tr("所有文件 (*.*)"));
        const COMDLG_FILTERSPEC filters[] = {
            { iniFilter.c_str(), L"*.ini" },
            { allFilter.c_str(), L"*.*" },
        };
        dialog->SetFileTypes(IM_ARRAYSIZE(filters), filters);
        dialog->SetFileTypeIndex(1);

        if (SUCCEEDED(dialog->Show(WindowHandle(window))))
        {
            IShellItem* result = nullptr;
            if (SUCCEEDED(dialog->GetResult(&result)))
            {
                PWSTR name = nullptr;
                if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &name)))
                {
                    path = WideToUtf8(name);
                    CoTaskMemFree(name);
                    ok = true;
                }
                result->Release();
            }
        }
        dialog->Release();
    }

    if (uninit)
        CoUninitialize();
    return ok;
}

/*
 * Asks for the picture to use as the bezel backdrop.
 *
 * Only PNG is offered as a filter, because that is the only format the decoder understands (see
 * Util::PNG::LoadRGBA). Listing JPEG as an option would be a trap: the shell would happily hand the
 * path over and the picture would then fail to load with no obvious reason. The description says so
 * as well, so the limitation is visible before the dialog is even opened.
 */
static bool BrowseImageFile(OSD::Platform::Window* window, std::string& path)
{
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninit = SUCCEEDED(init);

    IFileDialog* dialog = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        dialog->SetTitle(Utf8ToWide(Tr("选择背景图片（仅支持 PNG）")).c_str());

        // Tr() returns UTF-8 and the shell API wants UTF-16, so the strings are converted into locals
        // first - the filter array only borrows the pointers.
        const std::wstring pngFilter = Utf8ToWide(Tr("PNG 图片 (*.png)"));
        const std::wstring allFilter = Utf8ToWide(Tr("所有文件 (*.*)"));
        const COMDLG_FILTERSPEC filters[] = {
            { pngFilter.c_str(), L"*.png" },
            { allFilter.c_str(), L"*.*" },
        };
        dialog->SetFileTypes(IM_ARRAYSIZE(filters), filters);
        dialog->SetFileTypeIndex(1);

        if (SUCCEEDED(dialog->Show(WindowHandle(window))))
        {
            IShellItem* result = nullptr;
            if (SUCCEEDED(dialog->GetResult(&result)))
            {
                PWSTR name = nullptr;
                if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &name)))
                {
                    path = WideToUtf8(name);
                    CoTaskMemFree(name);
                    ok = true;
                }
                result->Release();
            }
        }
        dialog->Release();
    }

    if (uninit)
        CoUninitialize();
    return ok;
}

/*
 * Asks for a location to save the settings to.
 *
 * The counterpart to BrowseSettingsFile: same COM setup, but a save dialog, which brings the
 * overwrite confirmation and the "create this file?" prompt with it rather than leaving them to be
 * reimplemented. The offered name is the conventional one so that a plain "Save as" followed by
 * Enter lands somewhere sensible.
 */
static bool BrowseSaveFile(OSD::Platform::Window* window, std::string& path)
{
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninit = SUCCEEDED(init);

    IFileSaveDialog* dialog = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
        dialog->SetTitle(Utf8ToWide(Tr("设置另存为")).c_str());

        const std::wstring filterText = Utf8ToWide(Tr("Supermodel 设置 (*.ini)"));
        const COMDLG_FILTERSPEC filters[] = {
            { filterText.c_str(), L"*.ini" },
            { Utf8ToWide(Tr("所有文件 (*.*)")).c_str(), L"*.*" },
        };
        dialog->SetFileTypes(IM_ARRAYSIZE(filters), filters);
        dialog->SetFileTypeIndex(1);
        dialog->SetDefaultExtension(L"ini");
#if defined(SUPERMODEL_PURE_VULKAN)
        dialog->SetFileName(L"Model3-Vulkan.ini");
#else
        dialog->SetFileName(L"Supermodel.ini");
#endif

        // Start in the folder the current settings came from, so "Save as" defaults to a sibling
        // copy rather than dropping the user in Documents.
        if (!path.empty())
        {
            IShellItem* start = nullptr;
            const std::wstring wide = Utf8ToWide(path);
            if (!wide.empty() && SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(&start))))
            {
                dialog->SetFolder(start);
                start->Release();
            }
        }

        if (SUCCEEDED(dialog->Show(WindowHandle(window))))
        {
            IShellItem* result = nullptr;
            if (SUCCEEDED(dialog->GetResult(&result)))
            {
                PWSTR name = nullptr;
                if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &name)))
                {
                    path = WideToUtf8(name);
                    CoTaskMemFree(name);
                    ok = true;
                }
                result->Release();
            }
        }
        dialog->Release();
    }

    if (uninit)
        CoUninitialize();
    return ok;
}
#else
static std::filesystem::path PathFromUtf8(const std::string& text)
{
    return std::filesystem::path(text);
}
#endif

/*
 * Resolve a ROM folder the way the interface should: a relative value is looked for next to the
 * executable first, then through the platform's historical rule.  This is what lets the portable
 * layout keep "roms/" in the settings file: the folder can be moved with the emulator, and the same
 * setting still names it.  On Windows and macOS ResolvePath() already anchors to the executable,
 * so the first branch is mostly for Linux, where the settings themselves live under XDG.
 */
static std::string ResolveRomPath(const std::string& path)
{
    if (path.empty())
        return path;

    if (PathFromUtf8(path).is_absolute())
        return path;

    const std::string base = FileSystemPath::GetBasePath();
    if (!base.empty())
    {
        std::error_code error;
        const std::string besideExe = base + path;
        if (std::filesystem::is_directory(PathFromUtf8(besideExe), error) && !error)
            return besideExe;
    }

    return FileSystemPath::ResolvePath(path);
}

static std::string PathToUtf8(const std::filesystem::path& path);

static bool FolderHasZip(const std::filesystem::path& folder)
{
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error))
    {
        if (error)
            break;
        std::error_code typeError;
        if (entry.is_regular_file(typeError) && !typeError &&
            ToLowerAscii(entry.path().extension().string()) == ".zip")
            return true;
    }
    return false;
}

/*
 * The portable ROM layout: a rom/, roms/, romset/ or romsets/ folder next to the executable.
 *
 * case is ignored because the package is edited on Windows, macOS and Linux alike, and the point
 * of the scan is that the user should not have to care which spelling they ended up with.  A
 * folder that actually contains a zip wins over an empty one with a nicer name, so an empty
 * leftover roms/ cannot hide a populated rom/ next to it.
 *
 * requireZip is what keeps the shipped, empty roms/ folder from replacing a usable C:/Downloads:
 * only a folder with game data may take over from a path that already works.  When the configured
 * path is missing, the caller may ask again with requireZip=false to adopt an existing empty
 * folder rather than leaving the interface pointing at nothing.
 *
 * The result is the folder name with a trailing slash, relative to the executable: moving the
 * whole installation keeps the setting valid, and ResolveRomPath() anchors it at use time.
 */
static std::string FindPortableRomFolder(bool requireZip)
{
    const std::string base = FileSystemPath::GetBasePath();
    if (base.empty())
        return "";

    struct Candidate
    {
        int rank;
        std::string name;
    };

    std::vector<Candidate> candidates;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(PathFromUtf8(base), error))
    {
        if (error)
            break;

        std::error_code typeError;
        if (!entry.is_directory(typeError) || typeError)
            continue;

        const std::string name = PathToUtf8(entry.path().filename());
        const std::string lower = ToLowerAscii(name);
        int rank = -1;
        if (lower == "roms")
            rank = 0;
        else if (lower == "romsets")
            rank = 1;
        else if (lower == "romset")
            rank = 2;
        else if (lower == "rom")
            rank = 3;

        if (rank < 0)
            continue;

        const bool hasZip = FolderHasZip(entry.path());
        if (requireZip && !hasZip)
            continue;

        candidates.push_back({ (hasZip ? 0 : 10) + rank, name });
    }

    if (candidates.empty())
        return "";

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b)
        {
            return a.rank != b.rank ? a.rank < b.rank : ToLowerAscii(a.name) < ToLowerAscii(b.name);
        });

    return candidates.front().name + "/";
}

/*
 * Platform dispatch for the four pickers.
 *
 * Windows and macOS each have a native implementation under the same names. Linux has no portable
 * dialog API and the emulator does not want a GTK or Qt dependency, so it asks the two command-line
 * front ends that desktop environments provide: zenity (GTK) and kdialog (KDE). If neither is
 * installed, the text field remains the only way to set a path, exactly as before.
 */
#if defined(__APPLE__)
static bool BrowseFolder(OSD::Platform::Window* window, std::string& path) { return OSXFileDialogs::BrowseFolder(nullptr, path); }
static bool BrowseSettingsFile(OSD::Platform::Window* window, std::string& path) { return OSXFileDialogs::BrowseSettingsFile(nullptr, path); }
static bool BrowseImageFile(OSD::Platform::Window* window, std::string& path) { return OSXFileDialogs::BrowseImageFile(nullptr, path); }
static bool BrowseSaveFile(OSD::Platform::Window* window, std::string& path) { return OSXFileDialogs::BrowseSaveFile(nullptr, path); }
#elif !defined(_WIN32)
enum class DialogTool { None, Zenity, KDialog };

static DialogTool DetectDialogTool()
{
    static const DialogTool tool = []() -> DialogTool
    {
        if (std::system("command -v zenity >/dev/null 2>&1") == 0)
            return DialogTool::Zenity;
        if (std::system("command -v kdialog >/dev/null 2>&1") == 0)
            return DialogTool::KDialog;
        return DialogTool::None;
    }();
    return tool;
}

// Single-quotes an argument for /bin/sh. The dialogs receive titles and paths that may contain
// spaces, and a path may contain a quote; the '\'' sequence is the POSIX way to include one.
static std::string ShellQuote(const std::string& text)
{
    std::string quoted = "'";
    for (char c : text)
    {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += "'";
    return quoted;
}

static std::string RunDialogCommand(const std::string& command, int& status)
{
    status = -1;
    if (command.empty())
        return {};

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
        return {};

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        output += buffer;
    status = pclose(pipe);

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();
    return output;
}

static bool BrowseWithDialog(const std::string& command, std::string& path)
{
    int status = -1;
    const std::string picked = RunDialogCommand(command, status);
    if (status != 0 || picked.empty())
        return false;

    path = picked;
    return true;
}

static bool BrowseFolder(OSD::Platform::Window*, std::string& path)
{
    const DialogTool tool = DetectDialogTool();
    const std::string title = Tr("选择 Model 3 ROM 目录");
    if (tool == DialogTool::Zenity)
    {
        std::string command = "zenity --file-selection --directory --title=" + ShellQuote(title);
        if (!path.empty())
            command += " --filename=" + ShellQuote(path + "/");
        return BrowseWithDialog(command, path);
    }
    if (tool == DialogTool::KDialog)
    {
        const std::string command = "kdialog --getexistingdirectory " +
            ShellQuote(path.empty() ? "." : path) + " --title " + ShellQuote(title);
        return BrowseWithDialog(command, path);
    }
    return false;
}

static bool BrowseSettingsFile(OSD::Platform::Window*, std::string& path)
{
    const DialogTool tool = DetectDialogTool();
    const std::string title = Tr("选择要载入的设置文件");
    if (tool == DialogTool::Zenity)
        return BrowseWithDialog("zenity --file-selection --title=" + ShellQuote(title), path);

    if (tool == DialogTool::KDialog)
    {
        const std::string command = "kdialog --getopenfilename " +
            ShellQuote(path.empty() ? "." : path) + " --title " + ShellQuote(title);
        return BrowseWithDialog(command, path);
    }
    return false;
}

static bool BrowseImageFile(OSD::Platform::Window*, std::string& path)
{
    const DialogTool tool = DetectDialogTool();
    const std::string title = Tr("选择背景图片（仅支持 PNG）");
    if (tool == DialogTool::Zenity)
    {
        std::string command = "zenity --file-selection --title=" + ShellQuote(title) +
            " --file-filter=" + ShellQuote(std::string(Tr("PNG 图片 (*.png)")) + " | *.png");
        if (!path.empty())
            command += " --filename=" + ShellQuote(path);
        return BrowseWithDialog(command, path);
    }
    if (tool == DialogTool::KDialog)
    {
        const std::string command = "kdialog --getopenfilename " +
            ShellQuote(path.empty() ? "." : path) + " " +
            ShellQuote(std::string(Tr("PNG 图片 (*.png)")) + "|*.png") +
            " --title " + ShellQuote(title);
        return BrowseWithDialog(command, path);
    }
    return false;
}

static bool BrowseSaveFile(OSD::Platform::Window*, std::string& path)
{
    const DialogTool tool = DetectDialogTool();
    const std::string title = Tr("设置另存为");
#if defined(SUPERMODEL_PURE_VULKAN)
    const std::string suggested = path.empty() ? std::string("Model3-Vulkan.ini") : path;
#else
    const std::string suggested = path.empty() ? std::string("Supermodel.ini") : path;
#endif
    if (tool == DialogTool::Zenity)
    {
        return BrowseWithDialog("zenity --file-selection --save --title=" + ShellQuote(title) +
            " --filename=" + ShellQuote(suggested), path);
    }
    if (tool == DialogTool::KDialog)
    {
        const std::string command = "kdialog --getsavefilename " + ShellQuote(suggested) + " " +
            ShellQuote(std::string(Tr("Supermodel 设置 (*.ini)")) + "|*.ini") +
            " --title " + ShellQuote(title);
        return BrowseWithDialog(command, path);
    }
    return false;
}
#endif

static std::string PathToUtf8(const std::filesystem::path& path)
{
#ifdef _WIN32
    return WideToUtf8(path.c_str());
#else
    return path.string();
#endif
}

static std::vector<RomEntry> ScanRomFolder(const std::string& folder, const std::map<std::string, Game>& games,
    const GameLoader* loader)
{
    std::vector<RomEntry> entries;
    std::error_code error;
    // A relative setting is anchored to the executable's directory, the same convention the log and
    // the game XML use. Without this, typing a relative path appeared to do nothing unless the
    // program happened to have been started from exactly the right directory.
    std::filesystem::path directory = PathFromUtf8(ResolveRomPath(folder));
    if (folder.empty() || !std::filesystem::is_directory(directory, error))
        return entries;

    std::map<std::string, const Game*> byName;
    for (const auto& game : games)
        byName[ToLowerAscii(game.second.name)] = &game.second;

    for (const auto& item : std::filesystem::directory_iterator(directory, error))
    {
        if (error || !item.is_regular_file(error))
            continue;
        if (ToLowerAscii(item.path().extension().string()) != ".zip")
            continue;

        RomEntry entry;
        entry.path = PathToUtf8(item.path());
        entry.setName = PathToUtf8(item.path().stem());

        /*
         * Identify the archive by its contents rather than by its name.
         *
         * GameLoader picks the set from the CRC32s inside the zip, so a file called scud.zip can run
         * - and save as - the Australian scudau revision, and a perfectly good set can sit under a
         * name Games.xml has never heard of. Listing rows by file name made the first case start the
         * wrong game without saying so, and marked the second case unstartable when it was not.
         */
        Game identified;
        const bool identifiedByContent = loader != nullptr
            && !loader->Identify(&identified, entry.path) && !identified.name.empty();

        auto found = byName.find(ToLowerAscii(entry.setName));
        if (identifiedByContent)
        {
            entry.resolvedSet = identified.name;
            entry.title = identified.title.empty() ? identified.name : identified.title;
            entry.version = identified.version;
            entry.year = identified.year;
            entry.supported = true;
        }
        else if (found != byName.end())
        {
            entry.resolvedSet = found->second->name;
            entry.title = found->second->title.empty() ? entry.setName : found->second->title;
            entry.version = found->second->version;
            entry.year = found->second->year;
            entry.supported = true;
        }
        else
        {
            entry.title = entry.setName;
        }

        // Support-visible: a row whose file name says one thing and whose contents say another is the
        // most confusing thing this list can show, and nothing else leaves a trace of it.
        if (identifiedByContent && ToLowerAscii(entry.resolvedSet) != ToLowerAscii(entry.setName))
        {
            const std::string label = entry.version.empty() ? entry.title : entry.title + " (" + entry.version + ")";
            InfoLog("GUI: '%s' holds set '%s' - %s.", entry.setName.c_str(), entry.resolvedSet.c_str(), label.c_str());
        }
        entries.push_back(std::move(entry));
    }

    // Supported sets first, then the rest, each group alphabetical.
    //
    // Sorting purely by name would scatter the unrecognised archives through the list next to the
    // working version of the same game, which reads as though the list were inconsistent. Gathering
    // them at the bottom keeps the alphabetical run of playable games intact.
    std::sort(entries.begin(), entries.end(), [](const RomEntry& a, const RomEntry& b) {
        if (a.supported != b.supported)
            return a.supported;
        return ToLowerAscii(a.title) < ToLowerAscii(b.title);
    });

    // Report how much of the list the translation table covers.
    //
    // Under Chinese this should account for every entry. If it reports 0 while the interface itself
    // is Chinese, the titles reaching the lookup are not the ones in the table - a Games.xml whose
    // titles differ from the expected ones, for instance - and the list will keep reading English
    // for reasons that are invisible from the outside. Logging it turns that into a fact.
    int translated = 0;
    int unsupported = 0;
    for (const RomEntry& entry : entries)
    {
        if (!entry.supported)
            ++unsupported;
        else if (std::string(TrGameTitle(entry.title.c_str())) != entry.title)
            ++translated;
    }
    InfoLog("GUI: %d ROM(s) listed, %d title(s) translated for the current language, %d with no identifiable set.",
        (int)entries.size(), translated, unsupported);

    return entries;
}

static void RefreshRoms(const std::string& folder, const std::map<std::string, Game>& games)
{
    if (folder == g_romFolderCached)
        return;
    g_romFolderCached = folder;
    g_roms = ScanRomFolder(folder, games, g_gameLoader);
}

/*
 * Cover art for the game list.
 *
 * Every ROM set in Assets/Boxarts/ is named after the set and stored as WebP, which cuts the
 * archive to roughly a quarter of the size of the original PNGs. One texture per set is uploaded
 * on first use and kept for the lifetime of the window; failures are cached as well, so a set
 * without artwork costs a single directory lookup rather than one per frame.
 */
struct BoxartTexture
{
    // ImTextureID is a GL texture name on the SDL2/OpenGL build and a Vulkan
    // descriptor set on the Pure-Vulkan build.  Both are 64-bit values, and the
    // ImGui draw code treats them as opaque either way.
    ImTextureID texture = 0;
    int width = 0;
    int height = 0;
};

static std::map<std::string, BoxartTexture> g_boxartTextures;

/*
 * Whether a frame has actually reached the screen since the interface started.
 *
 * The start-up artwork covers the window until this is true: the window exists but showing it before
 * a frame has been presented is what caused the black flash this picture replaced. Reset by
 * GuiCreate(), set by GUI() (Vulkan, which presents from inside the draw function) or by
 * GuiRunOnce() (OpenGL, which swaps after it).
 */
static bool g_guiFramePresented = false;

#if defined(SUPERMODEL_PURE_VULKAN)
    /*
     * Set when a frame could not even acquire an image, so nothing was drawn or presented.
     *
     * GuiRunOnce() uses it as the trigger for revealing the window while the start-up artwork is still
     * up: the window is kept hidden until a frame has been presented so nothing empty is ever on
     * screen, but a driver that refuses to hand out an image for a hidden window would then never get
     * a frame at all. Seeing this flag is the only reliable way to tell the two cases apart.
     */
static bool g_guiFrameSkipped = false;
#endif

#if defined(SUPERMODEL_PURE_VULKAN)
// The settings window's Vulkan host.  One window, one backend, set by
// GuiCreate() and cleared by GuiDestroy().
static OSD::SDL3App::UiVulkanBackend* g_uiVulkan = nullptr;
#endif

#ifdef _WIN32
/*
 * Decodes WebP boxart with WIC.
 *
 * Windows 10 1809 and later ship a WIC codec for WebP, so no third-party decoder is needed. The
 * image is scaled while still on the CPU: the files are around 512x700, but the preview only ever
 * draws them about 200 pixels tall and there are dozens of them.
 */
static bool DecodeBoxartWIC(const std::filesystem::path& path, int maxWidth, std::vector<uint8_t>& pixels, int& width, int& height)
{
    // WIC is COM-based. The settings window has not necessarily initialised COM on this thread,
    // and repeated calls must not fail: S_FALSE means "already initialised" and
    // RPC_E_CHANGED_MODE means "initialised with the other threading model". Both are fine here.
    static IWICImagingFactory* factory = []() -> IWICImagingFactory* {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE)
            return nullptr;
        IWICImagingFactory* created = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&created))))
            return nullptr;
        return created;
    }();
    if (factory == nullptr)
        return false;

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, &decoder)))
        return false;

    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame)))
    {
        decoder->Release();
        return false;
    }

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    frame->GetSize(&sourceWidth, &sourceHeight);

    IWICBitmapSource* source = frame;
    IWICBitmapScaler* scaler = nullptr;
    if (maxWidth > 0 && sourceWidth > (UINT)maxWidth && sourceHeight > 0)
    {
        const UINT scaledWidth = (UINT)maxWidth;
        const long rounded = std::lround(sourceHeight * (double)scaledWidth / (double)sourceWidth);
        const UINT scaledHeight = (UINT)(std::max)(1L, rounded);
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame, scaledWidth, scaledHeight, WICBitmapInterpolationModeHighQualityCubic)))
        {
            source = scaler;
        }
        else if (scaler != nullptr)
        {
            scaler->Release();
            scaler = nullptr;
        }
    }

    bool ok = false;
    IWICFormatConverter* converter = nullptr;
    if (SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                        nullptr, 0.0, WICBitmapPaletteTypeMedianCut)))
    {
        UINT outWidth = 0;
        UINT outHeight = 0;
        if (SUCCEEDED(converter->GetSize(&outWidth, &outHeight)) && outWidth > 0 && outHeight > 0)
        {
            const UINT stride = outWidth * 4;
            pixels.resize((size_t)stride * outHeight);
            if (SUCCEEDED(converter->CopyPixels(nullptr, stride, (UINT)pixels.size(), pixels.data())))
            {
                width = (int)outWidth;
                height = (int)outHeight;
                ok = true;
            }
        }
    }

    if (converter != nullptr)
        converter->Release();
    if (scaler != nullptr)
        scaler->Release();
    frame->Release();
    decoder->Release();
    return ok;
}

/*
 * Fallback decoder: the vendored libwebp in Src/Pkgs/webp.
 *
 * WIC reads WebP only when the "Webp Image Extensions" package is installed. A machine without it
 * refused every cover, silently, which looked exactly like a missing folder. The bundled decoder has
 * no such dependency, so the covers work on a clean Windows install. Its C paths are slower than the
 * SIMD ones WIC uses, which does not matter for one 512x700 picture decoded per selected game.
 */
static bool DecodeBoxartWithLibwebp(const std::filesystem::path& path, int maxWidth,
    std::vector<uint8_t>& pixels, int& width, int& height)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    const std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (encoded.empty())
        return false;

    int sourceWidth = 0;
    int sourceHeight = 0;
    uint8_t *decoded = WebPDecodeRGBA(encoded.data(), encoded.size(), &sourceWidth, &sourceHeight);
    if (decoded == nullptr || sourceWidth <= 0 || sourceHeight <= 0)
    {
        if (decoded != nullptr)
            WebPFree(decoded);
        return false;
    }

    width = sourceWidth;
    height = sourceHeight;
    if (maxWidth > 0 && sourceWidth > maxWidth)
    {
        width = maxWidth;
        height = (std::max)(1, (int)std::lround(sourceHeight * (double)maxWidth / (double)sourceWidth));
    }

    if (width == sourceWidth && height == sourceHeight)
    {
        pixels.assign(decoded, decoded + (size_t)sourceWidth * (size_t)sourceHeight * 4);
        WebPFree(decoded);
        return true;
    }

    // Bilinear, sampled at pixel centres - the same job the WIC path hands to its scaler.
    pixels.resize((size_t)width * (size_t)height * 4);
    for (int y = 0; y < height; ++y)
    {
        const double sampleY = ((y + 0.5) * sourceHeight / height) - 0.5;
        int y0 = (int)std::floor(sampleY);
        double fy = sampleY - y0;
        if (y0 < 0)
        {
            y0 = 0;
            fy = 0.0;
        }
        const int y1 = (std::min)(y0 + 1, sourceHeight - 1);

        for (int x = 0; x < width; ++x)
        {
            const double sampleX = ((x + 0.5) * sourceWidth / width) - 0.5;
            int x0 = (int)std::floor(sampleX);
            double fx = sampleX - x0;
            if (x0 < 0)
            {
                x0 = 0;
                fx = 0.0;
            }
            const int x1 = (std::min)(x0 + 1, sourceWidth - 1);

            const uint8_t *p00 = decoded + ((size_t)y0 * sourceWidth + x0) * 4;
            const uint8_t *p10 = decoded + ((size_t)y0 * sourceWidth + x1) * 4;
            const uint8_t *p01 = decoded + ((size_t)y1 * sourceWidth + x0) * 4;
            const uint8_t *p11 = decoded + ((size_t)y1 * sourceWidth + x1) * 4;
            uint8_t *out = &pixels[((size_t)y * width + x) * 4];

            for (int channel = 0; channel < 4; ++channel)
            {
                const double top = p00[channel] + (p10[channel] - p00[channel]) * fx;
                const double bottom = p01[channel] + (p11[channel] - p01[channel]) * fx;
                out[channel] = (uint8_t)std::lround(top + (bottom - top) * fy);
            }
        }
    }

    WebPFree(decoded);
    return true;
}

static bool DecodeBoxart(const std::filesystem::path& path, int maxWidth, std::vector<uint8_t>& pixels, int& width, int& height)
{
    /*
     * WIC first (it has SIMD paths), the bundled decoder only when WIC refuses the file.
     *
     * SM3D_BOXART_DECODER=wic|webp forces one of them, which is the only way to exercise the
     * fallback on a machine that does have the Windows WebP codec. Purely diagnostic.
     */
    const char *forced = std::getenv("SM3D_BOXART_DECODER");
    const bool tryWic = forced == nullptr || _stricmp(forced, "webp") != 0;
    const bool tryWebp = forced == nullptr || _stricmp(forced, "wic") != 0;

    if (tryWic && DecodeBoxartWIC(path, maxWidth, pixels, width, height))
        return true;
    if (!tryWebp)
        return false;

    const bool ok = DecodeBoxartWithLibwebp(path, maxWidth, pixels, width, height);
    if (ok)
        InfoLog("Boxart: '%s' decoded with the bundled libwebp decoder.",
            path.filename().string().c_str());
    return ok;
}
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

/*
 * macOS: ImageIO reads WebP natively from Big Sur (11.0), which is the deployment target this
 * build already uses. CoreGraphics does the downscale, so there is no third-party decoder and no
 * extra library to ship.
 */
static bool DecodeBoxart(const std::filesystem::path& path, int maxWidth,
                         std::vector<uint8_t>& pixels, int& width, int& height)
{
    CFStringRef pathString = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    if (pathString == nullptr)
        return false;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, pathString, kCFURLPOSIXPathStyle, false);
    CFRelease(pathString);
    if (url == nullptr)
        return false;

    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (source == nullptr)
        return false;

    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (image == nullptr)
        return false;

    const size_t sourceWidth = CGImageGetWidth(image);
    const size_t sourceHeight = CGImageGetHeight(image);
    if (sourceWidth == 0 || sourceHeight == 0)
    {
        CGImageRelease(image);
        return false;
    }

    int outWidth = (int)sourceWidth;
    int outHeight = (int)sourceHeight;
    if (maxWidth > 0 && sourceWidth > (size_t)maxWidth)
    {
        outWidth = maxWidth;
        outHeight = (int)(std::max)(1L,
            std::lround(sourceHeight * (double)maxWidth / (double)sourceWidth));
    }

    std::vector<uint8_t> rgba((size_t)outWidth * outHeight * 4);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(rgba.data(), outWidth, outHeight, 8,
        (size_t)outWidth * 4, colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);
    if (context == nullptr)
    {
        CGImageRelease(image);
        return false;
    }

    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGContextDrawImage(context, CGRectMake(0, 0, outWidth, outHeight), image);
    CGContextRelease(context);
    CGImageRelease(image);

    pixels = std::move(rgba);
    width = outWidth;
    height = outHeight;
    return true;
}

#elif defined(__linux__)
#include <webp/decode.h>

/*
 * Linux has no system WebP decoder shared by every distribution, so this uses libwebp. The
 * decoder scales while decoding, which keeps the cached textures at the same size as the Windows
 * and macOS paths instead of decoding the full 512x700 images and shrinking them afterwards.
 */
static bool DecodeBoxart(const std::filesystem::path& path, int maxWidth,
                         std::vector<uint8_t>& pixels, int& width, int& height)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;

    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length <= 0)
    {
        std::fclose(file);
        return false;
    }

    std::vector<uint8_t> encoded((size_t)length);
    const size_t read = std::fread(encoded.data(), 1, encoded.size(), file);
    std::fclose(file);
    if (read != encoded.size())
        return false;

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config))
        return false;
    if (WebPGetFeatures(encoded.data(), encoded.size(), &config.input) != VP8_STATUS_OK)
        return false;

    config.output.colorspace = MODE_RGBA;
    if (maxWidth > 0 && config.input.width > maxWidth)
    {
        config.options.use_scaling = 1;
        config.options.scaled_width = maxWidth;
        config.options.scaled_height = (int)(std::max)(1L,
            std::lround(config.input.height * (double)maxWidth / (double)config.input.width));
    }

    if (WebPDecode(encoded.data(), encoded.size(), &config) != VP8_STATUS_OK)
    {
        WebPFreeDecBuffer(&config.output);
        return false;
    }

    width = config.output.width;
    height = config.output.height;
    const size_t stride = config.output.u.RGBA.stride;
    pixels.resize((size_t)width * height * 4);
    for (int y = 0; y < height; ++y)
        std::memcpy(pixels.data() + (size_t)y * width * 4,
                    config.output.u.RGBA.rgba + (size_t)y * stride,
                    (size_t)width * 4);

    WebPFreeDecBuffer(&config.output);
    return true;
}

#endif

/*
 * Decodes PNG/WebP bytes returned by the C# save-state list.  WIC takes a
 * filename in the existing boxart path, so the bytes go through a short-lived
 * temp file; this is a cold path and avoids duplicating the WIC stream setup.
 * Returns true on success.
 */
static bool DecodeImageBytes(const uint8_t* data, size_t size, int maxWidth,
    std::vector<uint8_t>& pixels, int& width, int& height)
{
    if (data == nullptr || size == 0)
        return false;

#if !defined(_WIN32) && !defined(__APPLE__)
    /*
     * DecodeBoxart() on Linux is a libwebp decoder, while save-state thumbnails are PNG (the
     * renderer's capture path). Util/PNGFile.h is header-only and uses zlib, which the Unix build
     * already links, so this is what makes save cards and the in-game overlay preview work there.
     * Windows (WIC) and macOS (ImageIO) keep the platform decoders, which also resize for us.
     */
    static const uint8_t pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (size >= sizeof(pngSignature) && std::memcmp(data, pngSignature, sizeof(pngSignature)) == 0)
    {
        std::vector<uint8_t> decoded;
        uint32_t decodedWidth = 0;
        uint32_t decodedHeight = 0;
        if (Util::PNG::DecodeRGBA(data, size, decoded, decodedWidth, decodedHeight, nullptr) &&
            decodedWidth > 0 && decodedHeight > 0 && !decoded.empty())
        {
            if (maxWidth > 0 && decodedWidth > static_cast<uint32_t>(maxWidth))
            {
                // Nearest-neighbour is enough for a UI thumbnail and keeps this path free of a
                // second resampler; the GPU filters it again when the card is drawn.
                const uint32_t targetWidth = static_cast<uint32_t>(maxWidth);
                const uint32_t targetHeight = (std::max)(1u, decodedHeight * targetWidth / decodedWidth);
                std::vector<uint8_t> scaled(static_cast<size_t>(targetWidth) * targetHeight * 4);
                for (uint32_t y = 0; y < targetHeight; ++y)
                {
                    const uint32_t sy = y * decodedHeight / targetHeight;
                    for (uint32_t x = 0; x < targetWidth; ++x)
                    {
                        const uint32_t sx = x * decodedWidth / targetWidth;
                        std::memcpy(&scaled[(static_cast<size_t>(y) * targetWidth + x) * 4],
                            &decoded[(static_cast<size_t>(sy) * decodedWidth + sx) * 4], 4);
                    }
                }
                decoded.swap(scaled);
                decodedWidth = targetWidth;
                decodedHeight = targetHeight;
            }
            pixels.swap(decoded);
            width = static_cast<int>(decodedWidth);
            height = static_cast<int>(decodedHeight);
            return true;
        }
        // A PNG this decoder rejects (interlaced, unusual bit depth) still goes to the temp-file
        // path, which is a no-op on Linux but keeps one fallback for every platform.
    }
#endif

    std::error_code error;
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path(error) / "supermodel-hostcore-thumb.png";
    if (error)
        return false;

    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(reinterpret_cast<const char*>(data), (std::streamsize)size);
        if (!out)
            return false;
    }

    const bool decoded = DecodeBoxart(temporary, maxWidth, pixels, width, height);
    std::filesystem::remove(temporary, error);
    return decoded;
}

bool GuiDecodeImageBytes(const uint8_t* data, size_t size, int maxWidth,
    std::vector<uint8_t>& pixels, int& width, int& height)
{
    return DecodeImageBytes(data, size, maxWidth, pixels, width, height);
}

/*
 * Logged once per session, before the first cover is looked up.
 *
 * "The covers do not show" has three causes that look identical from the interface: the folder is
 * not where the executable expects it, the file for that set is missing, or the decoder refused the
 * file. The first is answered by the path and the file count here; the other two by a line per set
 * in LoadBoxart().
 */
static void LogBoxartDirectory()
{
    static bool logged = false;
    if (logged)
        return;
    logged = true;

    const std::filesystem::path directory =
        PathFromUtf8(FileSystemPath::GetPath(FileSystemPath::Assets)) / "Boxarts";
    std::error_code error;
    std::filesystem::directory_iterator it(directory, error);
    if (error)
    {
        ErrorLog("Boxarts: cannot read '%s' (%s); covers will not be shown.",
            directory.string().c_str(), error.message().c_str());
        return;
    }

    std::size_t count = 0;
    for (const auto& entry : it)
    {
        std::error_code entryError;
        if (entry.is_regular_file(entryError) && entry.path().extension() == ".webp")
            ++count;
    }
    InfoLog("Boxarts: %zu file(s) in '%s'.", count, directory.string().c_str());
    if (count == 0)
        ErrorLog("Boxarts: no .webp files found; the covers will not be shown. The release package "
            "puts them in Data/Assets/Boxarts next to the executable.");
}

/*
 * Returns the cover texture for a set, creating it on first use.
 *
 * Called only from GUI(), which runs with the settings window's OpenGL context current, so the
 * texture can be uploaded directly. The result (including "no artwork") is cached in
 * g_boxartTextures and lives until GuiDestroy().
 */
static BoxartTexture LoadBoxart(const std::string& setName)
{
    auto cached = g_boxartTextures.find(setName);
    if (cached != g_boxartTextures.end())
        return cached->second;

    BoxartTexture result;
    g_boxartTextures.emplace(setName, result);

    LogBoxartDirectory();

    const std::filesystem::path path = PathFromUtf8(FileSystemPath::GetPath(FileSystemPath::Assets)) /
        "Boxarts" / (setName + ".webp");
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        InfoLog("Boxart: '%s' not found ('%s').", setName.c_str(), path.string().c_str());
        return result;
    }

    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    if (!DecodeBoxart(path, 384, pixels, width, height) || pixels.empty())
    {
        /*
         * On Windows this is almost always the missing WebP decoder rather than a corrupt file:
         * WIC only knows WebP when the 'Webp Image Extensions' package is installed, and a machine
         * without it refuses every one of these files. Said here because the failure is otherwise
         * completely silent - the preview simply has no picture in it.
         */
        ErrorLog("Boxart: '%s' could not be decoded ('%s') by either WIC or the bundled libwebp "
            "decoder; the file is probably not a WebP picture.",
            setName.c_str(), path.string().c_str());
        return result;
    }

#if defined(SUPERMODEL_PURE_VULKAN)
    if (g_uiVulkan == nullptr)
        return result;
    auto texture = g_uiVulkan->AddTexture(pixels.data(), width, height);
    if (!texture)
    {
        ErrorLog("GUI: cover '%s' could not be uploaded: %s", setName.c_str(), texture.error().c_str());
        return result;
    }
    result.texture = *texture;
#else
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    result.texture = (ImTextureID)(intptr_t)texture;
#endif

    result.width = width;
    result.height = height;
    g_boxartTextures[setName] = result;

    return result;
}

static void ReleaseBoxartTextures()
{
    for (auto& item : g_boxartTextures)
    {
        if (item.second.texture == 0)
            continue;
#if defined(SUPERMODEL_PURE_VULKAN)
        if (g_uiVulkan != nullptr)
            g_uiVulkan->RemoveTexture(item.second.texture);
#else
        GLuint texture = (GLuint)(intptr_t)item.second.texture;
        glDeleteTextures(1, &texture);
#endif
    }
    g_boxartTextures.clear();
}

/*
 * Short English descriptions for the preview panel.
 *
 * The interface language does not affect these: they are deliberately English-only, so there is
 * one table keyed by ROM set prefix rather than a field in Games.xml that would have to be
 * translated for every regional revision. A prefix covers the variants of one game (all the
 * Virtua Striker 2 revisions share an entry) and the missing entries fall back to a plain note.
 */
static const char* GameBlurb(const std::string& setName)
{
    struct Blurb
    {
        const char* prefix;
        const char* text;
    };
    static const Blurb kBlurbs[] = {
        { "bassdx",
          "Arcade bass fishing with a rod controller. Cast, pick a lure and fight each fish to the boat before time runs out; the Deluxe version adds more lakes and lures." },
        { "getbass",
          "Arcade bass fishing with a rod controller. Cast, pick a lure and fight each fish to the boat before time runs out; the Deluxe version adds more lakes and lures." },
        { "daytona2",
          "Sega's stock-car racing sequel. Battle on the Edge adds four cars, three courses and rival racers; Power Edition retunes the handling and adds the Challenge course." },
        { "dayto2pe",
          "Sega's stock-car racing sequel. Battle on the Edge adds four cars, three courses and rival racers; Power Edition retunes the handling and adds the Challenge course." },
        { "dirtdvls",
          "Off-road racing with buggies and trucks. Courses are built around jumps, water and shortcuts, and linked cabinets support four-player races." },
        { "eca",
          "A driving game with a difference: race an ambulance through traffic, pick up patients and get them to hospital before the timer expires." },
        { "fvipers2",
          "The sequel to Fighting Vipers, a 3D fighter with an armour system that lets fighters lose damaged parts mid-match, plus a roster of new challengers." },
        { "harley",
          "Motorcycle racing through a free-roaming Los Angeles. Choose a Harley-Davidson, pass checkpoints and earn extra time by weaving through traffic." },
        { "lamachin",
          "A light-gun shooter in which one or two players fight through a machine-overrun Los Angeles with mounted machine guns and a dodge pedal." },
        { "lemans24",
          "Endurance racing on the Le Mans circuit. Drive sports prototypes through day, night and changing weather in a condensed 24-hour race." },
        { "lostwsg",
          "A light-gun shooter based on The Lost World: Jurassic Park. Two players cross Isla Sorna, fighting dinosaurs with a shotgun and a lock-on weapon." },
        { "magtruck",
          "A two-player co-operative action game. One player steers the magical truck and the other pedals as the pair cross a series of fantasy stages." },
        { "mgtrkbad",
          "A two-player co-operative action game. One player steers the magical truck and the other pedals as the pair cross a series of fantasy stages." },
        { "oceanhun",
          "An underwater light-gun shooter. Dive through sunken ruins and caverns, fighting giant sea creatures with harpoons and special weapons." },
        { "scud",
          "A GT racing game known outside Japan as Sega Super GT. Four cars compete across three courses, with linked cabinets for multiplayer." },
        { "skichamp",
          "A winter sports racer. Race down a snow-covered mountain, pulling tricks and dodging obstacles on the way to the finish." },
        { "spikeout",
          "A 3D beat 'em up for up to four players, built around combos and guarding. Final Edition revises the moves and adds stages and characters." },
        { "spikeofe",
          "A 3D beat 'em up for up to four players, built around combos and guarding. Final Edition revises the moves and adds stages and characters." },
        { "srally2",
          "The sequel to Sega Rally Championship, with more cars, courses and surface types. Linked cabinets support multiplayer rally racing." },
        { "swtrilgy",
          "A rail shooter covering the original Star Wars trilogy. Fly an X-wing, a speeder bike and the Millennium Falcon through the key battles." },
        { "vf3",
          "The third Virtua Fighter game and the first on Model 3. It introduced sidestep evades and uneven ground; Team Battle adds tag matches." },
        { "von2",
          "A fast 3D mecha fighter. Pilot Virtuaroids with twin-stick controls, boosting, dashing and firing around enclosed arenas." },
        { "vs2",
          "Sega's arcade football series, played fast and exaggerated. National teams pass, shoot and tackle through short matches; later revisions update the teams." },
    };

    for (const Blurb& blurb : kBlurbs)
    {
        if (setName.rfind(blurb.prefix, 0) == 0)
            return blurb.text;
    }
    return nullptr;
}

/*
 * The upper half of the left column: cover, title and a short description for one set.
 *
 * A null entry means nothing is selected yet. The cover is drawn at the panel's full height but
 * capped at half its width, which keeps a portrait boxart from crowding out the text.
 */
static void DrawGamePreview(const RomEntry* entry, float width, float height)
{
    /*
     * A recessed panel rather than an ImGui child background.
     *
     * The preview sits inside the left column's sheet of glass, and a second opaque background there
     * would hide the blur the sheet exists to show. The well is a translucent dark rectangle with no
     * backdrop and no rim: a highlight on a recess reads as a raised edge, which is the opposite of
     * what it is for. The classic theme has no glass, so it keeps its ordinary bordered child.
     */
    const bool classic = UiThemeIsClassic();
    ImGui::BeginChild("GamePreview", ImVec2(width, height),
        classic ? ImGuiChildFlags_Borders : ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            (classic ? 0 : ImGuiWindowFlags_NoBackground));
    if (!classic)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        UiFrost::Plate(draw, 0, origin, ImVec2(origin.x + size.x, origin.y + size.y),
            ImGui::GetStyle().ChildRounding, UiFrost::Surface::Well);
    }
    if (entry == nullptr)
    {
        UiTextDim("把鼠标移到游戏上，或单击选中，这里会显示封面和英文简介。");
        ImGui::EndChild();
        return;
    }

    const BoxartTexture art = LoadBoxart(EffectiveSet(*entry));
    const bool hasImage = art.texture != 0;
    const ImVec2 available = ImGui::GetContentRegionAvail();

    float imageHeight = available.y;
    float imageWidth = hasImage ? imageHeight * (float)art.width / (float)art.height : available.x * 0.32f;
    const float maxImageWidth = available.x * 0.48f;
    if (imageWidth > maxImageWidth)
    {
        imageWidth = maxImageWidth;
        if (hasImage)
            imageHeight = imageWidth * (float)art.height / (float)art.width;
    }
    if (imageHeight > available.y)
        imageHeight = available.y;

    if (hasImage)
    {
        ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)art.texture), ImVec2(imageWidth, imageHeight));
    }
    else
    {
        // A framed blank keeps the text in the same place whether or not a cover exists.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(imageWidth, imageHeight));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRect(origin, ImVec2(origin.x + imageWidth, origin.y + imageHeight),
                      ImGui::GetColorU32(ImGuiCol_Border));
        const char* note = Tr("无封面");
        const ImVec2 size = ImGui::CalcTextSize(note);
        draw->AddText(ImVec2(origin.x + (imageWidth - size.x) * 0.5f, origin.y + (imageHeight - size.y) * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), note);
    }
    ImGui::SameLine();

    ImGui::BeginGroup();
    const std::string title = TrGameTitle(entry->title.c_str());
    ImGui::TextWrapped("%s", title.c_str());
    const std::string version = TrGameVersion(entry->version);
    if (!version.empty() && entry->year > 0)
        ImGui::TextDisabled("%s  -  %d", version.c_str(), entry->year);
    else if (!version.empty())
        ImGui::TextDisabled("%s", version.c_str());
    else if (entry->year > 0)
        ImGui::TextDisabled("%d", entry->year);

    // An unrecognised set is named after its .zip and may share a prefix with a real title, so the
    // blurb table is only consulted for sets Games.xml actually defines.
    const char* blurb = entry->supported ? GameBlurb(EffectiveSet(*entry)) : nullptr;
    if (blurb != nullptr)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        PushHelpWrap();
        ImGui::TextUnformatted(blurb);
        PopHelpWrap();
    }
    else if (!entry->supported)
    {
        UiTextDim("没能从这个包里认出 Model 3 游戏（可能缺文件）。");
    }
    else
    {
        UiTextDim("这个游戏还没有简介。");
    }
    ImGui::EndGroup();

    ImGui::EndChild();
}

/*
 * Interface scale.
 *
 * Same rule the settings window uses: everything that is a number of pixels rather than a font
 * metric has to grow with the text, or the layout drifts apart at high DPI. Defined outside the
 * pure-Vulkan block because the settings window itself uses it on both backends. The save cards
 * size their thumbnails from it so a picture stays in proportion with its title and buttons.
 */
static float UiScale()
{
    return (std::max)(1.0f, ImGui::GetFontSize() / 18.0f);
}

#if defined(SUPERMODEL_PURE_VULKAN)
/*
 * Save manager.
 *
 * The list, metadata, ZIP container and rename operation live in the C#
 * Native AOT module; this file only owns the ImGui presentation and the
 * Vulkan textures for the thumbnails.
 */

struct SaveCard
{
    HostSaveMeta meta{};
    std::filesystem::path path;
    std::vector<uint8_t> thumbnail;
    ImTextureID texture = 0;
    int width = 0;
    int height = 0;
};

static bool g_manageSavesOpen = false;
// Set once the popup has actually been drawn. Used to tell "the popup has not appeared yet" (the
// frame between OpenManageSaves() and the deferred OpenPopup) from "the popup went away" (Escape).
static bool g_manageSavesDrawn = false;
static std::string g_manageSavesGame;
// The row the manager was opened from. Usually the same as g_manageSavesGame, but a merged archive
// resolves to the regional set inside it (see OpenManageSaves), and only the row can select the game.
static std::string g_manageSavesRow;
static std::filesystem::path g_manageSavesDirectory;
static std::vector<SaveCard> g_saveCards;
// Legacy .st files found for the set the manager is open for. They are deliberately not listed as
// cards (the manager is the .zst view), but a set that only has them must not look empty.
static int g_manageSavesLegacyCount = 0;
// Other set names that have containers in the same folder, with their save counts. Shown only when
// the current set has none, because the usual reason is that the player opened the manager for a
// different regional release than the one they actually played.
static std::vector<std::pair<std::string, int>> g_manageSavesOtherSets;
static std::string g_manageSavesError;
static int g_saveRenameIndex = -1;
// Card whose rename box should take the keyboard on its next frame.
static int g_saveRenameFocus = -1;
static char g_saveRenameBuffer[160] = {};
static std::filesystem::path g_pendingVkState;
// Developer hook: path the manager should auto-load on the next drawn frame.
static std::string g_autoLoadSave;
// Card waiting for the second click of the delete confirmation. -1 when no card is confirming.
static int g_saveDeleteIndex = -1;
// Container to remove after the card table has been drawn: deleting inside the loop would invalidate
// the SaveCard reference the loop is holding.
static std::filesystem::path g_pendingSaveDelete;

/*
 * Where a game's saves live.
 *
 * The container keeps the classic slot-file layout: every game's states sit in the shared Saves
 * folder as <set>.zst<slot>, next to the <set>.st<slot> files the emulator has always written.  The
 * manager therefore scans that folder and keeps the entries that belong to this set, rather than
 * using a per-game subfolder - otherwise a state saved in-game would not show up here.
 */
static std::filesystem::path SaveFolder()
{
    return std::filesystem::path(FileSystemPath::GetPath(FileSystemPath::SavePathType()));
}

/*
 * Supermodel-LegacyOG.exe's save folder. The manager itself only writes .zst containers to
 * SaveFolder(), but "Import legacy .st" also looks here so the two executables' saves can be
 * brought together without the player moving files by hand.
 */
static std::filesystem::path LegacySaveFolder()
{
    return std::filesystem::path(FileSystemPath::GetPath(FileSystemPath::SavesLegacy));
}

/*
 * Whether a container belongs to a set.
 *
 * Slot containers are <set>.zst<slot>. The first builds used <set>.vkstate (and the module still
 * lists that pattern), so the manager has to accept both or those older files stay invisible.
 */
static bool SaveNameBelongsToSet(const std::string& name, const std::string& setName)
{
    if (name.rfind(setName + ".zst", 0) == 0)
        return true;
    return name.rfind(setName + ".vkstate", 0) == 0;
}

static std::string SetNameFromContainer(const std::string& name)
{
    std::size_t marker = name.find(".zst");
    if (marker == std::string::npos)
        marker = name.find(".vkstate");
    return marker == std::string::npos ? std::string() : name.substr(0, marker);
}

/*
 * The name the game list shows, for a set name.
 *
 * The manager used to be titled with the ROM set ("lamachin"), which is the folder/zip name rather
 * than anything a player recognises; the list entry carries the real title and its region, and
 * TrGameTitle() applies the interface language exactly as the list itself does.
 */
static std::string GameTitleForSet(const std::string& setName)
{
    for (const RomEntry& entry : g_roms)
    {
        if (entry.setName == setName)
        {
            if (entry.title.empty())
                break;

            const std::string title = TrGameTitle(entry.title.c_str());
            return entry.version.empty() ? title : title + " (" + entry.version + ")";
        }
    }

    // Sets with saves but no zip of their own are not in the ROM list at all - a regional child set
    // living inside a merged archive is the usual example - so fall back to the XML definitions.
    if (g_gameDefinitions != nullptr)
    {
        auto found = g_gameDefinitions->find(setName);
        if (found != g_gameDefinitions->end() && !found->second.title.empty())
        {
            const std::string title = TrGameTitle(found->second.title.c_str());
            return found->second.version.empty() ? title : title + " (" + found->second.version + ")";
        }
    }
    return setName;
}

static int RomIndexForSet(const std::string& setName)
{
    for (int i = 0; i < (int)g_roms.size(); ++i)
    {
        if (g_roms[i].setName == setName)
            return i;
    }
    return -1;
}

static void ReleaseSaveCards()
{
    if (g_uiVulkan != nullptr)
    {
        for (SaveCard& card : g_saveCards)
        {
            if (card.texture != 0)
                g_uiVulkan->RemoveTexture(card.texture);
        }
    }
    g_saveCards.clear();
}

static void LoadSaveCards()
{
    ReleaseSaveCards();
    g_manageSavesError.clear();
    g_manageSavesLegacyCount = 0;
    g_manageSavesOtherSets.clear();
    g_saveRenameIndex = -1;
    g_saveRenameFocus = -1;
    g_saveDeleteIndex = -1;

    auto bridge = OSD::HostCore::Shared();
    if (!bridge)
    {
        g_manageSavesError = bridge.error().message;
        return;
    }

    auto entries = (*bridge)->ListVkStates(g_manageSavesDirectory);
    if (!entries)
    {
        g_manageSavesError = entries.error().message;
        return;
    }

    std::map<std::string, int> otherSets;
    for (OSD::HostCore::SaveEntry& entry : *entries)
    {
        const std::string name = entry.path.filename().string();

        // The folder is shared by every game, so keep only the slots that belong to this set.
        if (!SaveNameBelongsToSet(name, g_manageSavesGame))
        {
            const std::string other = SetNameFromContainer(name);
            if (!other.empty())
                otherSets[other]++;
            continue;
        }

        SaveCard card;
        card.meta = entry.meta;
        card.path = std::move(entry.path);
        card.thumbnail = std::move(entry.thumbnail);

        if (!card.thumbnail.empty() && g_uiVulkan != nullptr)
        {
            std::vector<uint8_t> rgba;
            int width = 0;
            int height = 0;
            if (DecodeImageBytes(card.thumbnail.data(), card.thumbnail.size(), 220, rgba, width, height))
            {
                auto uploaded = g_uiVulkan->AddTexture(rgba.data(), width, height);
                if (uploaded)
                {
                    card.texture = *uploaded;
                    card.width = width;
                    card.height = height;
                }
            }
        }

        g_saveCards.push_back(std::move(card));
    }

    // Newest first: the card someone wants is almost always the one they just wrote. The stamp is
    // ISO-8601 with a fixed width, so a plain string comparison orders it correctly.
    std::stable_sort(g_saveCards.begin(), g_saveCards.end(),
        [](const SaveCard& a, const SaveCard& b) { return std::strcmp(a.meta.SavedAt, b.meta.SavedAt) > 0; });

    for (const auto& item : otherSets)
        g_manageSavesOtherSets.emplace_back(item.first, item.second);
    std::stable_sort(g_manageSavesOtherSets.begin(), g_manageSavesOtherSets.end(),
        [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b)
        {
            if (a.second != b.second)
                return a.second > b.second;
            return a.first < b.first;
        });

    // Old .st files for this set. They cannot be cards (no metadata, no thumbnail), but the set
    // having only those is a completely different answer to "where are my saves". Count both
    // folders: the Vulkan build's own .st fallbacks in Saves, and LegacyOG's new saves in
    // Saves_Legacy. The same slot name in both counts once.
    std::map<std::string, bool> legacyNames;
    for (const std::filesystem::path& folder : { g_manageSavesDirectory, LegacySaveFolder() })
    {
        std::error_code directoryError;
        for (const auto& item : std::filesystem::directory_iterator(folder, directoryError))
        {
            if (directoryError || !item.is_regular_file())
                continue;
            const std::string name = item.path().filename().string();
            if (name.rfind(g_manageSavesGame + ".st", 0) == 0)
                legacyNames[name] = true;
        }
    }
    g_manageSavesLegacyCount = (int)legacyNames.size();

    // Support-visible: "the manager is empty" is otherwise indistinguishable from "the saves are
    // there but under a different set name", which are very different problems.
    InfoLog("Manage saves: %zu card(s) for '%s' in '%s' (%d legacy .st, %zu other set(s)).",
        g_saveCards.size(), g_manageSavesGame.c_str(), g_manageSavesDirectory.string().c_str(),
        g_manageSavesLegacyCount, g_manageSavesOtherSets.size());
}

static void OpenManageSaves(const std::string& setName)
{
    g_manageSavesRow = setName;
    g_manageSavesGame = setName;

    /*
     * Resolve the row's archive the same way the game will.
     *
     * Saves are named after the set the emulator actually runs, which GameLoader identifies from the
     * CRC32s inside the archive rather than from its file name. A zip that opens as scud.zip can
     * therefore run - and save as - the Australian scudau revision, and the manager would then look
     * for saves that the session never wrote. Asking the loader answers the question the way the
     * game will answer it; anything else here is a guess.
     */
    if (g_gameLoader != nullptr)
    {
        for (const RomEntry& entry : g_roms)
        {
            if (entry.setName != setName || entry.path.empty())
                continue;

            Game resolved;
            if (!g_gameLoader->Identify(&resolved, entry.path) && !resolved.name.empty())
                g_manageSavesGame = resolved.name;
            break;
        }
    }

    if (g_manageSavesGame != g_manageSavesRow)
    {
        InfoLog("'%s' holds the ROMs of set '%s' (%s); listing that set's saves.", g_manageSavesRow.c_str(),
            g_manageSavesGame.c_str(), GameTitleForSet(g_manageSavesGame).c_str());
    }

    g_manageSavesDirectory = SaveFolder();
    LoadSaveCards();
    g_manageSavesOpen = true;
    g_manageSavesDrawn = false;
    g_pendingPopup = "###ManageSaves";
}

/*
 * A legacy .st file records nothing about when it was written, so its modification time is used as
 * the save stamp. Stored in the same ISO-8601 UTC form a fresh save uses, which keeps the cards
 * sortable and lets FormatSaveTime() present it in local time.
 */
static std::string IsoTimestamp(const std::filesystem::file_time_type& fileTime)
{
    // std::chrono::clock_cast is a C++20 library feature that libc++ does not
    // provide yet; the clock difference is the portable equivalent.
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t stamp = std::chrono::system_clock::to_time_t(systemTime);

    std::tm utc = {};
#if defined(_WIN32)
    if (gmtime_s(&utc, &stamp) != 0)
        return {};
#else
    if (gmtime_r(&stamp, &utc) == nullptr)
        return {};
#endif

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ", utc.tm_year + 1900,
        utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
    return buffer;
}

static void ImportLegacyStates()
{
    auto bridge = OSD::HostCore::Shared();
    if (!bridge)
    {
        g_manageSavesError = bridge.error().message;
        return;
    }

    /*
     * Collect the newest .st per slot across both folders first. A slot can exist in Saves (the
     * Vulkan build's old fallback and pre-split Legacy saves) and in Saves_Legacy (the Legacy
     * build's current saves); importing them in directory order would let the older one win,
     * because the first import gives the target .zst a fresh timestamp.
     */
    struct LegacyCandidate
    {
        std::filesystem::path source;
        std::filesystem::file_time_type written{};
        bool hasTime = false;
    };
    std::map<std::string, LegacyCandidate> candidates;

    const std::filesystem::path sources[] = {
        std::filesystem::path(FileSystemPath::GetPath(FileSystemPath::Saves)),
        LegacySaveFolder()
    };
    for (const std::filesystem::path& source : sources)
    {
        std::error_code error;
        for (const auto& item : std::filesystem::directory_iterator(source, error))
        {
            if (error || !item.is_regular_file())
                continue;

            const std::string name = item.path().filename().string();
            const std::string prefix = g_manageSavesGame + ".st";
            if (name.rfind(prefix, 0) != 0)
                continue;

            // Slot naming mirrors the legacy files: "daytona2.st1" becomes "daytona2.zst1", so the
            // slot survives the conversion and the two names differ by a single letter.
            const std::string slot = name.substr(prefix.size());
            const std::string targetName = g_manageSavesGame + ".zst" + slot;

            std::error_code timeError;
            const std::filesystem::file_time_type written =
                std::filesystem::last_write_time(item.path(), timeError);
            auto existing = candidates.find(targetName);
            const bool newer = existing == candidates.end() ||
                (!timeError && (!existing->second.hasTime || written > existing->second.written));
            if (newer)
            {
                LegacyCandidate candidate;
                candidate.source = item.path();
                candidate.written = written;
                candidate.hasTime = !timeError;
                candidates[targetName] = std::move(candidate);
            }
        }
    }

    int imported = 0;
    for (const auto& candidate : candidates)
    {
        const std::string& targetName = candidate.first;
        const LegacyCandidate& source = candidate.second;

        HostSaveMeta meta = {};
        meta.StructSize = sizeof(HostSaveMeta);
        std::snprintf(meta.GameName, sizeof(meta.GameName), "%s", g_manageSavesGame.c_str());
        std::snprintf(meta.Title, sizeof(meta.Title), "%s", targetName.c_str());
        std::snprintf(meta.Version, sizeof(meta.Version), "%s", SUPERMODEL_FORK_VERSION);

        // A legacy .st carries no metadata, so the file's own timestamp is the only record of when
        // it was written. Without this the imported card would show no date at all and sort last.
        if (source.hasTime)
            std::snprintf(meta.SavedAt, sizeof(meta.SavedAt), "%s", IsoTimestamp(source.written).c_str());

        const std::filesystem::path target = g_manageSavesDirectory / targetName;

        /*
         * Do not replace a newer .zst with an older .st just because the import button was
         * pressed: with the split folders a slot can hold a Vulkan save (newer) and a Legacy
         * save (older) at the same time. An .st that is newer than the card still wins, which
         * is what re-importing after a Legacy session is for.
         */
        std::error_code targetError;
        const std::filesystem::file_time_type targetTime =
            std::filesystem::last_write_time(target, targetError);
        if (source.hasTime && !targetError && targetTime >= source.written)
            continue;

        auto converted = (*bridge)->ConvertLegacyState(source.source, target, meta);
        if (converted)
            ++imported;
        else
            g_manageSavesError = converted.error().message;
    }

    if (imported > 0)
        LoadSaveCards();
}

/*
 * Save stamps are stored as ISO-8601 UTC, which is unambiguous and sorts correctly but reads as
 * machine output. The cards show local time to the minute: two saves within the same minute are
 * rare, and the full "2026-10-21T16:59:44Z" does not fit the card width.
 */
static std::string FormatSaveTime(const char* iso, const char* fallback)
{
    if (iso == nullptr || *iso == '\0')
        return fallback;

    std::tm utc = {};
    if (std::sscanf(iso, "%d-%d-%dT%d:%d:%d", &utc.tm_year, &utc.tm_mon, &utc.tm_mday, &utc.tm_hour,
            &utc.tm_min, &utc.tm_sec) != 6)
        return iso;

    utc.tm_year -= 1900;
    utc.tm_mon -= 1;

#if defined(_WIN32)
    const std::time_t stamp = _mkgmtime(&utc);
    std::tm local = {};
    if (stamp == (std::time_t)-1 || localtime_s(&local, &stamp) != 0)
        return iso;
#else
    const std::time_t stamp = timegm(&utc);
    std::tm local = {};
    if (stamp == (std::time_t)-1 || localtime_r(&stamp, &local) == nullptr)
        return iso;
#endif

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1,
        local.tm_mday, local.tm_hour, local.tm_min);
    return buffer;
}

/*
 * Deletes one save card.
 *
 * The manager lists <set>.zst<slot> containers (and the older <set>.vkstate). The emulator also
 * accepts <set>.st<slot> as a fallback for states written before the container existed, so removing
 * only the container would leave that file behind: the slot would still look occupied in the in-game
 * save menu, and "Import legacy .st" would bring the card back. Both are removed.
 *
 * Existing slots are never renumbered. The freed number simply becomes the first gap the next save
 * can use: NextFreeSaveSlot() in Main.cpp scans from the current slot for the lowest unoccupied one,
 * and the in-game menu lists all sixteen slots including the empty gap.
 *
 * Returns true when the container itself is gone. error is filled on failure; a failed legacy
 * removal is reported but does not keep the card on screen, because the card's own file is gone.
 */
static bool DeleteSaveCard(const std::filesystem::path& container, std::string& error)
{
    error.clear();

    std::error_code removeError;
    const bool removed = std::filesystem::remove(container, removeError);
    if (!removed)
    {
        error = Util::Format() << Tr("删除失败") << ": " << container.filename().string();
        if (removeError)
            error += Util::Format() << " (" << removeError.message() << ")";
        return false;
    }

    InfoLog("Manage saves: deleted '%s'.", container.filename().string().c_str());

    // <set>.zst<slot> has a <set>.st<slot> fallback; <set>.vkstate carries no slot number.
    const std::string name = container.filename().string();
    const std::size_t marker = name.find(".zst");
    if (marker != std::string::npos)
    {
        const std::string slot = name.substr(marker + 4);
        if (!slot.empty() && std::all_of(slot.begin(), slot.end(),
                [](unsigned char c) { return std::isdigit(c) != 0; }))
        {
            const std::filesystem::path legacy =
                container.parent_path() / (g_manageSavesGame + ".st" + slot);
            std::error_code legacyError;
            if (std::filesystem::exists(legacy, legacyError) && !legacyError)
            {
                if (std::filesystem::remove(legacy, legacyError) && !legacyError)
                {
                    InfoLog("Manage saves: deleted the legacy fallback '%s'.",
                        legacy.filename().string().c_str());
                }
                else
                {
                    error = Util::Format() << Tr("删除失败") << ": " << legacy.filename().string();
                    if (legacyError)
                        error += Util::Format() << " (" << legacyError.message() << ")";
                }
            }
        }
    }

    return true;
}

static void DrawManageSavesModal(int& selectedGameIndex, bool& exit, bool& saveSettings)
{
    if (!g_manageSavesOpen)
        return;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    // The manager is a workspace rather than a yes/no dialog, so it takes 75% of the display width
    // and 85% of its height; the game list and the settings tabs stay visible around it.  The taller
    // box fits one more row of save cards now that the quick-save menu offers sixteen slots.
    //
    // The size is re-applied every frame rather than only when the popup appears: the module is not
    // resizable (see NoResize below), and this also keeps the 75% relationship when the settings
    // window itself is resized. Leaving the old grip enabled once let a drag shrink the window until
    // the cards were a few pixels tall and nothing could be read or clicked.
    ImGui::SetNextWindowSize(ImVec2(displaySize.x * 0.75f, displaySize.y * 0.85f), ImGuiCond_Always);
    bool open = true;
    // The "###" suffix resets the hash, so the popup keeps the same identity while the part before it
    // stays visible and can follow the interface language. Opening it under one id and looking it up
    // under another is silent: the window simply never appears.
    const std::string popupTitle = std::string(Tr("管理存档")) + " - " + GameTitleForSet(g_manageSavesGame) + "###ManageSaves";
    if (!ImGui::BeginPopupModal(popupTitle.c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize))
    {
        // A modal closes on Escape without telling the owner, so the flag is cleared here rather than
        // only in the buttons. Without this the next frame would keep calling BeginPopupModal for a
        // popup that is no longer on the stack, which silently does nothing - and the "open" state
        // would then be stale the next time the menu item is chosen.
        if (g_manageSavesDrawn)
        {
            g_manageSavesOpen = false;
            g_manageSavesDrawn = false;
        }
        return;
    }
    g_manageSavesDrawn = true;

    UiTextDim("存档目录：%s", g_manageSavesDirectory.string().c_str());

    if (UiButton("刷新"))
        LoadSaveCards();
    ImGui::SameLine();
    if (UiButton("导入旧版 .st"))
        ImportLegacyStates();
    ImGui::SameLine();
    if (UiButton("关闭窗口"))
        open = false;

    if (!g_manageSavesError.empty())
        UiTextWarn("%s", g_manageSavesError.c_str());

    if (g_saveCards.empty())
    {
        UiTextDim("这个游戏还没有 .zst 存档。游戏内保存后，这里会显示缩略图卡片。");

        if (g_manageSavesLegacyCount > 0)
        {
            UiTextDim("这个 set 还有 %d 个旧版 .st 存档（含 LegacyOG 的 Saves_Legacy；这里只列 .zst）；点「导入旧版 .st」即可转换。",
                g_manageSavesLegacyCount);
        }

        if (!g_manageSavesOtherSets.empty())
        {
            UiTextWarn("这个 set 没有 .zst 存档，但存档目录里还有其他 set 的存档：");
            std::size_t shown = 0;
            for (const auto& other : g_manageSavesOtherSets)
            {
                if (shown++ >= 8)
                {
                    UiTextDim("……");
                    break;
                }
                UiTextDim("  %s（%d 个）", GameTitleForSet(other.first).c_str(), other.second);
            }
            UiTextDim("如果你玩的是另一个区域版本，请关闭本窗口，在游戏列表里右键那个版本再打开「管理存档」。");
        }
    }
    else
    {
        const ImGuiStyle& style = ImGui::GetStyle();

        /*
         * Reserve room for the action row before sizing the card.
         *
         * At 150% the three buttons plus their padding came to almost the whole content width, so
         * the row had no margin to distribute: Delete ended up against the right border even though
         * the row was nominally centred. Measuring the translated labels makes the card wide enough
         * for the buttons plus an ItemSpacing margin on each side, in both languages and at every
         * UI scale.
         */
        const auto buttonWidth = [&style](const char* label)
        {
            return ImGui::CalcTextSize(Tr(label)).x + style.FramePadding.x * 2.0f;
        };
        const float actionsWidth = buttonWidth("读取") + buttonWidth("改名") + buttonWidth("删除")
            + style.ItemSpacing.x * 2.0f;
        const float confirmWidth = buttonWidth("确认删除") + buttonWidth("取消") + style.ItemSpacing.x;
        const float actionsContentWidth =
            (std::max)(actionsWidth, confirmWidth) + style.ItemSpacing.x;

        // The picture is the point of a card, so it scales with the interface instead of staying at a
        // fixed pixel size while the text around it grows. 480x360 captures cover the largest card
        // this can produce (a 2x interface) with room to spare.
        const float imageHeight = (std::min)(150.0f * UiScale(), 320.0f);
        const float cardWidth = (std::max)({ 280.0f,
            imageHeight * 4.0f / 3.0f + style.WindowPadding.x * 2.0f,
            actionsContentWidth + style.WindowPadding.x * 2.0f });
        const float contentWidth = cardWidth - style.WindowPadding.x * 2.0f;
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        // Laid out from the font metrics rather than a fixed 250 px box: the fixed height clipped the
        // buttons at the default scale and at every DPI above it, and left a scrollbar inside a card.
        // Two lines for the title, one each for the date and the size, plus the action row. The date
        // and the size were once combined onto a single line, which did not fit the card width at
        // the larger interface scales.
        const float cardHeight = imageHeight + lineHeight * 4.0f + ImGui::GetFrameHeightWithSpacing()
            + style.WindowPadding.y * 2.0f + 4.0f;
        const int columns = (std::max)(1, (int)(ImGui::GetContentRegionAvail().x / (cardWidth + 16.0f)));

        if (ImGui::BeginTable("SaveCards", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                ImVec2(0, ImGui::GetContentRegionAvail().y - 8.0f)))
        {
            for (int i = 0; i < (int)g_saveCards.size(); ++i)
            {
                SaveCard& card = g_saveCards[i];
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                ImGui::BeginChild("##card", ImVec2(cardWidth, cardHeight), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                // Double-clicking anywhere on the card starts a rename. Loading is the explicit
                // button: a stray click over a card should not throw away the running game.
                bool beginRename = false;
                if (card.texture != 0)
                {
                    const float imageWidth = imageHeight * (float)card.width / (float)(std::max)(1, card.height);
                    const ImVec2 size = imageWidth > contentWidth
                        ? ImVec2(contentWidth, imageHeight * contentWidth / imageWidth)
                        : ImVec2(imageWidth, imageHeight);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (contentWidth - size.x) * 0.5f);
                    ImGui::Image(ImTextureRef(card.texture), size);
                    beginRename = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
                }
                else
                {
                    // Same total height as the thumbnail so the text below lines up across cards.
                    ImGui::Dummy(ImVec2(contentWidth, imageHeight - ImGui::GetTextLineHeightWithSpacing()));
                    const char* placeholder = Tr("无缩略图");
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                        + (contentWidth - ImGui::CalcTextSize(placeholder).x) * 0.5f);
                    ImGui::TextDisabled("%s", placeholder);
                    beginRename = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
                }

                const std::string title = card.meta.Title[0] != 0 ? std::string(card.meta.Title)
                                                                 : card.path.filename().string();

                if (g_saveRenameIndex == i)
                {
                    // Focus and select the whole title on the frame the editor appears, so the
                    // keyboard is usable straight after the button press: the box used to come up
                    // unfocused, which made a rename button look like it had done nothing.
                    if (g_saveRenameFocus == i)
                    {
                        ImGui::SetKeyboardFocusHere();
                        g_saveRenameFocus = -1;
                    }

                    ImGui::SetNextItemWidth(-1);
                    const bool submitted = ImGui::InputText("##newname", g_saveRenameBuffer,
                        sizeof(g_saveRenameBuffer),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (submitted || UiButton("确定"))
                    {
                        auto bridge = OSD::HostCore::Shared();
                        if (!bridge)
                            g_manageSavesError = bridge.error().message;
                        else
                        {
                            auto renamed = (*bridge)->RenameVkState(card.path, g_saveRenameBuffer);
                            if (renamed)
                            {
                                std::snprintf(card.meta.Title, sizeof(card.meta.Title), "%s", g_saveRenameBuffer);
                                g_saveRenameIndex = -1;
                            }
                            else
                                g_manageSavesError = renamed.error().message;
                        }
                    }
                    ImGui::SameLine();
                    if (UiButton("取消"))
                        g_saveRenameIndex = -1;
                }
                else
                {
                    // Two lines are reserved and the rest is clipped, so a long title cannot push
                    // the buttons out of the card or move them between cards. A title that fits is
                    // centred with the date and size below it; one that wraps uses the full width.
                    ImGui::BeginChild("##title", ImVec2(0.0f, lineHeight * 2.0f), ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    const float titleWidth = ImGui::GetContentRegionAvail().x;
                    const ImVec2 titleExtent = ImGui::CalcTextSize(title.c_str(), nullptr, false, titleWidth);
                    if (titleExtent.x < titleWidth)
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (titleWidth - titleExtent.x) * 0.5f);
                    ImGui::TextWrapped("%s", title.c_str());
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                        beginRename = true;
                    ImGui::EndChild();

                    const std::string savedAt = FormatSaveTime(card.meta.SavedAt, "-");
                    const float savedAtWidth = ImGui::CalcTextSize(savedAt.c_str()).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                        + (std::max)(0.0f, (contentWidth - savedAtWidth) * 0.5f));
                    ImGui::TextDisabled("%s", savedAt.c_str());

                    // Large states run to several megabytes, so the unit is picked to keep the line
                    // inside the card: "5653.0 KB" was clipped on a full 30 MB state.
                    const double sizeKb = (double)card.meta.FileSize / 1024.0;
                    char sizeText[32];
                    if (sizeKb >= 1024.0)
                        std::snprintf(sizeText, sizeof(sizeText), "%.1f MB", sizeKb / 1024.0);
                    else
                        std::snprintf(sizeText, sizeof(sizeText), "%.0f KB", sizeKb);
                    const float sizeWidth = ImGui::CalcTextSize(sizeText).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                        + (std::max)(0.0f, (contentWidth - sizeWidth) * 0.5f));
                    ImGui::TextDisabled("%s", sizeText);
                }

                if (beginRename)
                {
                    g_saveDeleteIndex = -1;
                    g_saveRenameIndex = i;
                    g_saveRenameFocus = i;
                    std::snprintf(g_saveRenameBuffer, sizeof(g_saveRenameBuffer), "%s", title.c_str());
                }

                // Pinned to the bottom edge so the buttons form one row across the grid whatever
                // the title wrapped to, and centred with equal margins on both sides.
                ImGui::SetCursorPosY(cardHeight - style.WindowPadding.y - ImGui::GetFrameHeight());
                const float rowWidth = g_saveDeleteIndex == i ? confirmWidth : actionsWidth;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                    + (std::max)(0.0f, (contentWidth - rowWidth) * 0.5f));
                if (g_saveDeleteIndex == i)
                {
                    // Two-step delete: the confirm button takes the place Load had, so the second
                    // click of a double-click on Delete lands on Cancel instead of confirming by
                    // accident.
                    if (UiButton("确认删除"))
                    {
                        g_pendingSaveDelete = card.path;
                        g_saveDeleteIndex = -1;
                    }
                    ImGui::SameLine();
                    if (UiButton("取消"))
                        g_saveDeleteIndex = -1;
                }
                else
                {
                    if (UiButton("读取"))
                    {
                        // Loading starts the game in the selected save, so the row it belongs to has to be
                        // the selection when the settings window closes. Without this the settings window
                        // simply quit: no game was selected, so no session was started.
                        const int row = RomIndexForSet(g_manageSavesRow);
                        if (row >= 0)
                            selectedGameIndex = row;

                        InfoLog("Manage saves: Load '%s' as set '%s' (row %d).",
                            card.path.string().c_str(), g_manageSavesGame.c_str(), row);

                        g_pendingVkState = card.path;
                        // Remember that this session came from the manager, so Escape from the game
                        // returns to these save cards rather than to the top-level game list.
                        g_returnToManageSaves = true;
                        g_returnToManageSavesSet = g_manageSavesRow;
                        exit = true;
                        saveSettings = true;
                        g_manageSavesOpen = false;
                        g_manageSavesDrawn = false;
                        open = false;
                    }
                    ImGui::SameLine();
                    if (UiButton("改名"))
                    {
                        g_saveDeleteIndex = -1;
                        g_saveRenameIndex = i;
                        g_saveRenameFocus = i;
                        std::snprintf(g_saveRenameBuffer, sizeof(g_saveRenameBuffer), "%s", title.c_str());
                    }
                    ImGui::SameLine();
                    if (UiButton("删除"))
                    {
                        g_saveDeleteIndex = i;
                        g_saveRenameIndex = -1;
                    }
                }

                ImGui::EndChild();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (!g_pendingSaveDelete.empty())
        {
            const std::filesystem::path target = std::move(g_pendingSaveDelete);
            g_pendingSaveDelete.clear();

            std::string deleteError;
            if (DeleteSaveCard(target, deleteError))
            {
                // Re-read the folder so the cards close the gap. The in-game save menu builds its list
                // from the same files on every frame, so it sees the deletion without a restart; the
                // slot simply becomes an empty row the next save can use.
                LoadSaveCards();
            }
            if (!deleteError.empty())
                g_manageSavesError = deleteError;
        }
    }

    if (!g_autoLoadSave.empty())
    {
        const std::filesystem::path wanted(g_autoLoadSave);
        const SaveCard* chosen = nullptr;
        for (const SaveCard& card : g_saveCards)
        {
            // Path separators differ between the GUI's saved paths and the literal environment
            // path, so compare the file name rather than the whole string.
            if (card.path.filename() == wanted.filename())
            {
                chosen = &card;
                break;
            }
        }
        if (chosen == nullptr && !g_saveCards.empty())
            chosen = &g_saveCards.front();

        if (chosen != nullptr)
        {
            const int row = RomIndexForSet(g_manageSavesRow);
            if (row >= 0)
                selectedGameIndex = row;
            InfoLog("GUI: auto-load save card '%s' (row %d).", chosen->path.string().c_str(), row);
            g_pendingVkState = chosen->path;
            g_returnToManageSaves = true;
            g_returnToManageSavesSet = g_manageSavesRow;
            g_autoLoadSave.clear();
            exit = true;
            saveSettings = true;
            g_manageSavesOpen = false;
            g_manageSavesDrawn = false;
            open = false;
        }
        else
        {
            ErrorLog("GUI: auto-load requested but no save cards are listed for '%s'.", g_manageSavesGame.c_str());
            g_autoLoadSave.clear();
        }
    }

    if (!open)
    {
        g_manageSavesOpen = false;
        g_manageSavesDrawn = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

std::filesystem::path GuiTakePendingVkState()
{
    std::filesystem::path path = std::move(g_pendingVkState);
    g_pendingVkState.clear();
    return path;
}

#else

std::filesystem::path GuiTakePendingVkState()
{
    return {};
}

#endif // SUPERMODEL_PURE_VULKAN

static std::string SelectedRomPath(int index)
{
    if (index < 0 || index >= (int)g_roms.size())
        return {};
    return g_roms[index].path;
}

/*
 * Copy the game list's position into `config`.
 *
 * The set name is stored rather than the row index so that adding or removing a zip
 * changes what the number means, not which game was last played. Collapsing the
 * three values into one helper keeps the two write paths at the end of GuiRunOnce
 * ("saved" and "window closed without saving") from drifting apart.
 */
static void SaveGameListPreferences(Util::Config::Node& config, int selectedGame)
{
    std::string selectedSet;
    if (selectedGame >= 0 && selectedGame < (int)g_roms.size())
        selectedSet = g_roms[selectedGame].setName;
    else
        selectedSet = g_lastSelectedSet;

    config.Set("GuiLastSelectedGame", selectedSet);
    config.Set("GuiGameListScrollY", g_gameListScrollY);
    config.Set("GuiGameListFilter", std::string(g_gameListFilter));
}

static Game GameForSelection(const std::map<std::string, Game>& games, int index)
{
    Game game;
    if (index < 0 || index >= (int)g_roms.size())
        return game;
    const std::string& name = EffectiveSet(g_roms[index]);
    auto found = games.find(name);
    if (found != games.end())
        return found->second;
    for (const auto& item : games)
        if (item.second.name == name)
            return item.second;
    return game;
}

static bool BoolSetting(Util::Config::Node& config, const char* label, const char* key)
{
    bool value = config[key].ValueAs<bool>();
    if (UiCheckbox(label, &value))
    {
        config.Set(key, value);
        return true;
    }
    return false;
}

/*
 * Whether the panels being drawn belong to the in-game overlay rather than the settings window.
 *
 * File-scope rather than threaded through every call: it changes how a handful of shared helpers
 * present themselves (the help markers, the explanatory paragraphs) and those helpers are called
 * from deep inside the control-drawing code, where a parameter would have to be passed down through
 * half a dozen signatures that otherwise have no interest in it.
 *
 * Set by DrawAdvancedSettingsPanel() and restored on the way out, so the settings window cannot
 * inherit it even if the two are ever drawn in the same frame.
 */
static bool g_overlayMode = false;

static void HelpMarker(const char* text)
{
    // The (?) markers are dropped in the overlay: they exist to explain controls to someone reading a
    // settings screen at their leisure, whereas the overlay is for adjusting one while watching the
    // picture. A marker there would take a click to reveal text and cover the game while doing it.
    if (g_overlayMode)
        return;

    ImGui::SameLine();
    UiTextDim("(?)");
    if (ImGui::IsItemHovered())
        // Through the wrapped-tooltip helper rather than ImGui::SetTooltip: these texts are sentences,
        // and SetTooltip lays them out unwrapped, so the popup grew as wide as the sentence and spilled
        // over the game list. The text goes through the same lookup as everything else, so translating
        // one is a table edit.
        DrawWrappedTooltip(Tr(text));
}

/*
 * Explanatory text, as opposed to a measurement.
 *
 * The distinction matters in the overlay: an explanation is there to be read once and is noise over a
 * running game, while a readout such as the scanline/mask beat frequency changes as a slider is
 * dragged and is the whole reason that section exists. Explanations go through here and disappear in
 * the overlay; readouts keep using UiTextDim/UiTextWarn and stay in both.
 */
static void UiExplain(const char* format, ...)
{
    if (g_overlayMode)
        return;

    static thread_local char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), Tr(format), args);
    va_end(args);

    ImGui::TextDisabled("%s", buffer);
}

static int RendererIndex(const Util::Config::Node& config)
{
#if defined(SUPERMODEL_OPENGL_ONLY)
    // There is no Vulkan renderer to index: the choice is only between the two
    // OpenGL engines.
    return config["New3DEngine"].ValueAs<bool>() ? 1 : 2;
#else
    if (config["Vulkan3DEngine"].ValueAs<bool>())
        return 0;
    if (config["New3DEngine"].ValueAs<bool>())
        return 1;
    return 2;
#endif
}

static void SetRenderer(Util::Config::Node& config, int index)
{
#if defined(SUPERMODEL_OPENGL_ONLY)
    config.Set("New3DEngine", index != 2);
#else
    config.Set("Vulkan3DEngine", index == 0);
    config.Set("New3DEngine", index != 2);
#endif
}

/*
 * A message posted from outside the settings window, drawn next to the renderer selection.
 *
 * The case it exists for: Vulkan was selected but could not start, so the session fell back to OpenGL.
 * The user needs to be told, and told where to fix it if they want to try Vulkan again - the settings
 * window is hidden while a game runs, so a log line alone would go unread.
 *
 * Held as a plain string rather than inside GuiState because it is posted by the game session, which
 * does not have (and should not need) access to the window's state.
 */
static std::string g_rendererNotice;

/*
 * The GPU OpenGL actually reported, read from GL_RENDERER when the settings window's context was made
 * current. Empty until then, which is why the interface treats "unknown" as its own case rather than
 * printing an empty line.
 *
 * The settings window has an OpenGL context of its own in every configuration - it is what ImGui draws
 * through - so this is filled in on any machine, whether or not a game has been run yet, and whether or
 * not the game itself uses Vulkan.
 */
static std::string s_glRendererName;

void GuiPostNotice(const std::string& text)
{
    g_rendererNotice = text;
}

#if !defined(SUPERMODEL_OPENGL_ONLY)
// Names of the Vulkan physical devices, queried once. Enumerating creates a throw-away
// Vulkan instance, so this is deliberately deferred until the tab is actually drawn.
static const std::vector<std::string>& GetDisplayDevices()
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
    /*
     * The startup preflight has already enumerated the physical devices, so reuse its list.
     *
     * Enumerating here meant creating a second throw-away Vulkan instance on the first UI frame.
     * With the overlay layers a normal desktop has installed (Steam, RTSS, ReShade, OBS, ...)
     * that costs about a second, and it happened inside startup, before the window was revealed -
     * which is why this build appeared to open much more slowly than the OpenGL one.
     */
    const std::vector<std::string>& probed = OSD::SDL3App::KnownVulkanDeviceNames();
    if (!probed.empty())
        return probed;
#endif
    static std::vector<std::string> devices;
    static bool queried = false;
    if (!queried)
    {
        queried = true;
        devices = Vulkan3D::EnumeratePhysicalDeviceNames();
    }
    return devices;
}
#endif

// Names of the host audio output devices, queried once. The platform layer brings the audio
// subsystem up, which is harmless here because nothing is playing yet.
static const std::vector<std::string>& GetAudioDevices()
{
    static std::vector<std::string> devices;
    static bool queried = false;
    if (!queried)
    {
        queried = true;
        const int count = OSD::Platform::GetAudioOutputDeviceCount();
        for (int i = 0; i < count; i++)
        {
            const char* name = OSD::Platform::GetAudioOutputDeviceName(i);
            if (name != nullptr && name[0] != '\0')
                devices.emplace_back(name);
        }
    }
    return devices;
}

// Shared device chooser. 'autoLabel' names the entry that clears the setting.
static void DeviceCombo(Util::Config::Node& config, const char* label, const char* key,
    const std::vector<std::string>& devices, const char* autoLabel, const char* emptyHint)
{
    const std::string current = config[key].ValueAsDefault<std::string>("");

    std::vector<std::string> labels;
    labels.reserve(devices.size() + 1);
    labels.emplace_back(Tr(autoLabel));
    for (size_t i = 0; i < devices.size(); i++)
    {
        // For audio devices, if device index 0 (Windows primary device) is the default, label it clearly
        if (std::string(key) == "AudioDevice" && i == 0)
            labels.emplace_back(devices[i] + Tr(" [系统默认]"));
        else
            labels.emplace_back(devices[i]);
    }

    int selected = 0;
    for (size_t i = 0; i < devices.size(); i++)
    {
        if (current == devices[i])
        {
            selected = (int)i + 1;
            break;
        }
    }

    std::vector<const char*> items;
    items.reserve(labels.size());
    for (const auto& text : labels)
        items.push_back(text.c_str());

    if (ComboRow(Tr(label), &selected, items.data(), (int)items.size()))
    {
        // If user selects 0 ("Auto/System Default") or selects index 1 (which is the default device),
        // we can store "" or the device name directly. Storing "" ensures 100% native fallback to SDL default route.
        if (selected == 0)
            config.Set(key, std::string());
        else
            config.Set(key, devices[selected - 1]);
    }

    if (devices.empty() && emptyHint != nullptr)
        UiTextDim("%s", Tr(emptyHint));
}

/*
 * Warning for the analogue trigger pairing.
 *
 * The gamepad layout puts the brake on the right trigger, but how that trigger is named depends on
 * the input system: XInput and SDL GameController give each trigger its own axis, so the right one
 * is RZAXIS, whereas the legacy systems (DirectInput, rawinput, plain SDL joystick) share a single
 * Z axis and put the right trigger on its positive half. Switching between the two therefore
 * invalidates the binding and the brake silently stops working - which is exactly the kind of
 * "the controls are broken" report this is here to prevent. The accelerator is a face button and
 * needs no such treatment.
 */
static void DrawTriggerPairingHint(Util::Config::Node& config)
{
    const std::string system = config["InputSystem"].ValueAsDefault<std::string>("sdlgamepad");
    const bool perTrigger = (system == "xinput" || system == "sdlgamepad");
    const std::string brake = config["InputBrake"].ValueAsDefault<std::string>("");

    const bool usesRz = brake.find("JOY1_RZAXIS") != std::string::npos;
    const bool usesSharedZ = brake.find("JOY1_ZAXIS_POS") != std::string::npos;
    // Not on a trigger at all (keyboard, face button, or bound to nothing): nothing to check.
    if (!usesRz && !usesSharedZ)
        return;

    // Explain what the selected system expects in the tooltip next to the warning, not in a line of
    // prose: the control it belongs to is the "Fix trigger binding" button below.
    const char* want = perTrigger ? "JOY1_RZAXIS" : "JOY1_ZAXIS_POS";

    if (perTrigger == usesRz)
        return;     // consistent: RZAXIS with a per-trigger system, ZAXIS_POS with a shared one

    UiTextColored(AttentionColour(),
        "刹车绑定的扳机形式与当前输入系统不符，刹车会没反应。");
    if (UiButton("修复扳机绑定##FixTrigger"))
        config.Set("InputBrake", perTrigger ? "KEY_DOWN,JOY1_RZAXIS" : "KEY_DOWN,JOY1_ZAXIS_POS");
    ImGui::SameLine();
    UiTextDim("(?)");
    if (ImGui::IsItemHovered())
        UiTooltip(
            "当前是 %s，需要 %s（现为 %s）\n"
            "或点「应用推荐键位」按当前系统重新配对。",
            system.c_str(), want, brake.c_str());
}

/*
 * The display facts the sync section needs, asked at a bounded rate.
 *
 * The mode check is re-done periodically rather than computed once, because the settings window lives
 * for the whole process and creating the custom timing is a manual step in the driver control panel.
 * The natural order is to read the note telling you to create one, create it, and come back - so a
 * value cached at startup would keep answering "unavailable" afterwards, which reads as a bug rather
 * than as a stale cache. Enumerating display modes is cheap, so this costs nothing at twice a second.
 *
 * The EDID range is different: it goes through WMI, which costs tens of milliseconds and would be
 * visible as a hitch if it ran on a timer. It is fetched once per output, on the first frame that
 * needs it and again when the output changes - a display's EDID does not change while it is
 * attached. It feeds both the range note (when the fixed-rate mode is unavailable) and the G-SYNC
 * auto-rate decision.
 */
struct ArcadeDisplayInfo
{
    bool modeAvailable = false;
    bool vrrArcadeRate = false;     // G-SYNC path: the display's range contains 57.5 Hz
    std::vector<DisplayTimingInfo> displays;
    bool fetched = false;
};

static const ArcadeDisplayInfo& GetArcadeDisplayInfo()
{
    static ArcadeDisplayInfo info;
    static int framesUntilCheck = 0;
    static int checkedDisplay = -1;

    /*
     * Also re-checked the moment the chosen output changes, so that picking a different monitor in
     * the combo below updates whether the fixed-rate option is selectable immediately. Without that
     * the answer would lag by up to the two-second interval, and on a two-monitor machine where only
     * one of them has a 57/58 Hz mode the option would appear to disable and enable itself at random.
     *
     * The display index is read here rather than passed in because it is derived from the
     * configuration, which this function has no other reason to see.
     */
    const int displayIndex = GetConfiguredDisplaySdlIndex();

    if (framesUntilCheck-- <= 0 || displayIndex != checkedDisplay)
    {
        framesUntilCheck = 120;
        const bool displayChanged = (displayIndex != checkedDisplay);
        checkedDisplay = displayIndex;
        info.modeAvailable = HasArcadeDisplayMode(57.5242f);

        // The EDID range is fetched once per output: WMI is slow enough to be visible as a hitch if
        // it ran on the timer, and a display's EDID does not change while it is attached. The
        // G-SYNC rate decision is made from that same cached data.
        if (displayChanged || !info.fetched)
        {
            info.displays = GetDisplayTimingInfo();
            info.fetched = true;
        }
        info.vrrArcadeRate =
            RateSupportForTiming(info.displays, 57.5242f) == DisplayRateSupport::Supported;
    }
    return info;
}

static bool VrrArcadeRateSupported()
{
    return GetArcadeDisplayInfo().vrrArcadeRate;
}

static bool ArcadeDisplayModeAvailable()
{
    return GetArcadeDisplayInfo().modeAvailable;
}

/*
 * Highest whole refresh rate a display's fullscreen mode list advertises.
 *
 * The desktop mode alone is not the answer: a 144 Hz panel usually runs at 60 on
 * the desktop and only reaches 144 in an exclusive fullscreen mode, which is
 * exactly the list this walks. Rates are rounded to the nearest whole Hz because
 * SDL reports 59.94 and 119.88 for panels everyone calls 60 and 120; treating
 * those as unsupported would grey out the only rate such a panel can take. Returns
 * 0 when the list cannot be read, which means "do not refuse anything" rather than
 * "nothing is supported".
 */
static int QueryMaxSupportedHz(OSD::Platform::DisplayId display)
{
    int maxHz = 0;
    const int modeCount = OSD::Platform::GetDisplayModeCount(display);
    for (int i = 0; i < modeCount; i++)
    {
        OSD::Platform::DisplayMode mode{};
        if (!OSD::Platform::GetDisplayMode(display, i, &mode) || !(mode.refreshHz > 1.0f))
            continue;
        const int hz = (int)std::lround((double)mode.refreshHz);
        if (hz > maxHz)
            maxHz = hz;
    }

    if (maxHz <= 0)
    {
        OSD::Platform::DisplayMode mode{};
        if (OSD::Platform::GetDesktopDisplayMode(display, &mode) && mode.refreshHz > 1.0f)
            maxHz = (int)std::lround((double)mode.refreshHz);
    }
    return maxHz;
}

/*
 * The same answer for the display the settings window is on, cached briefly.
 *
 * Enumerating a mode list allocates, and the fixed-rate row asks on every frame;
 * re-reading twice a second keeps a mode added in the driver control panel from
 * being invisible until a restart without paying for the enumeration per frame.
 */
static int MaxSupportedHzForWindow(OSD::Platform::Window* window)
{
    static OSD::Platform::DisplayId checkedDisplay = OSD::Platform::kPrimaryDisplay;
    static int checkedHz = -1;
    static double nextCheck = 0.0;

    const OSD::Platform::DisplayId display = window != nullptr
        ? OSD::Platform::GetWindowDisplay(window)
        : OSD::Platform::kPrimaryDisplay;
    const double now = ImGui::GetTime();
    if (display != checkedDisplay || checkedHz < 0 || now >= nextCheck)
    {
        checkedDisplay = display;
        checkedHz = QueryMaxSupportedHz(display);
        nextCheck = now + 2.0;
    }
    return checkedHz;
}

/*
 * Clamp a saved FixedRefreshRate to what the display the window is on can scan.
 *
 * Called once, after the window exists: the setting is written on one machine and
 * carried to another often enough that a 240 Hz value on a 60 Hz panel is a real
 * case, not a hand-edited one. The clamped value is put into the file copy as
 * well, so closing the window without pressing Save still leaves a usable value
 * behind. A display that reports no modes is left alone.
 */
static void ClampSavedFixedRefreshRate(Util::Config::Node& config, Util::Config::Node& fileConfig,
    OSD::Platform::Window* window)
{
    if (window == nullptr)
        return;

    const int maxHz = QueryMaxSupportedHz(OSD::Platform::GetWindowDisplay(window));
    if (maxHz <= 0)
        return;

    const int requested = config["FixedRefreshRate"].ValueAsDefault<int>(120);
    const int clamped = ClampFixedRefreshRate(requested, maxHz);
    if (clamped == requested)
        return;

    config.Set("FixedRefreshRate", clamped);
    fileConfig.Set("FixedRefreshRate", clamped);
    InfoLog("Pace: FixedRefreshRate %d -> %d Hz; the display reports modes up to %d Hz.",
        requested, clamped, maxHz);
}

/*
 * Fixed present rate combo with unsupported entries disabled.
 *
 * A plain ImGui::Combo cannot disable individual entries, and the list is the
 * whole point here: on a 60 Hz panel 120/144/240 are not choices, they are numbers
 * that would make the pacer present frames the display never scans. Each entry is
 * therefore its own Selectable inside BeginDisabled, with a tooltip naming the
 * panel's limit rather than leaving the reason implicit.
 */
static bool FixedRefreshRateCombo(Util::Config::Node& config, int maxSupportedHz, bool& changed)
{
    const int currentHz = config["FixedRefreshRate"].ValueAsDefault<int>(120);
    char preview[32];
    std::snprintf(preview, sizeof(preview), "%d Hz", currentHz);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float chrome = style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
    float width = kComboFloorWidth;
    for (int i = 0; i < kFixedRefreshRateCount; i++)
    {
        char label[32];
        std::snprintf(label, sizeof(label), "%d Hz", kFixedRefreshRates[i]);
        width = (std::max)(width, ImGui::CalcTextSize(label).x + chrome);
    }
    ImGui::SetNextItemWidth((std::min)(width, ImGui::GetContentRegionAvail().x));

    if (!ImGui::BeginCombo(Tr("呈现频率##FixedRefreshRate"), preview))
    {
        // BeginCombo still submits the closed combo, so the pointer can be set before bailing out.
        HandOver();
        return false;
    }
    HandOver();

    bool picked = false;
    for (int i = 0; i < kFixedRefreshRateCount; i++)
    {
        const int rate = kFixedRefreshRates[i];
        const bool supported = DisplaySupportsFixedRate(rate, maxSupportedHz);

        if (!supported)
            ImGui::BeginDisabled();

        char item[48];
        std::snprintf(item, sizeof(item), "%d Hz##FixedRate%d", rate, rate);
        if (ImGui::Selectable(item, rate == currentHz))
        {
            config.Set("FixedRefreshRate", rate);
            changed = true;
            picked = true;
        }
        HandOver();

        if (!supported)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                char tip[160];
                std::snprintf(tip, sizeof(tip), Tr("当前显示器最高支持 %d Hz，%d Hz 无法使用。"),
                    maxSupportedHz, rate);
                UiTooltip("%s", tip);
            }
        }
    }

    ImGui::EndCombo();
    return picked;
}

/*
 * Which output the emulator drives.
 *
 * Shown in the Video section rather than inside the fullscreen-sync block: the setting also decides
 * which monitor the game window is created on, so it is meaningful in windowed mode. Nesting it
 * under the Fullscreen checkbox made the option appear to have disappeared whenever fullscreen was
 * off. With a single output there is nothing to choose, so no control is drawn.
 */
static void DrawOutputDisplayCombo(Util::Config::Node& config)
{
    const std::vector<DisplayInfo>& outputs = GetDisplays();
    if (outputs.size() <= 1)
        return;

    const int currentNumber = config["DisplayNumber"].ValueAsDefault<int>(0);

    // How many outputs are not the main one, so that "secondary" can be used where it is
    // unambiguous. With three or more monitors there is no single secondary, and inventing an
    // ordinal would be worse than leaving the name to identify it.
    int nonPrimaryCount = 0;
    for (const DisplayInfo& display : outputs)
    {
        if (!display.primary)
            nonPrimaryCount++;
    }

    std::vector<std::string> labels;
    labels.reserve(outputs.size() + 1);
    labels.emplace_back(Tr("自动 · 跟随系统主显示器"));

    /*
     * Labelled by role rather than by Windows' own number, which is what this list used to
     * show ("5: Odyssey G70D"). That number is real and has to be stored - it is what the
     * helper script takes, and the only name for a display that survives a primary-display
     * change - but it is an implementation detail, not something a reader recognises. On the
     * machine this was written on the primary is DISPLAY5, because the numbering is
     * historical and gaps appear as outputs are added and removed. Leading with it made the
     * main monitor look like the odd one out.
     *
     * So: role first, then the monitor's own name and mode, and the number last in the
     * #n form. It is still visible for anyone who needs it for the script, and the tooltip
     * says what it is for.
     */
    int selected = 0;
    for (size_t i = 0; i < outputs.size(); i++)
    {
        const DisplayInfo& display = outputs[i];

        /*
         * The panel's own mode where Windows gives it, because that is the resolution the
         * user recognises from Display Settings and the monitor's own menu. SDL's figure is
         * quoted alongside only when the two differ - which happens when a monitor's scaling
         * differs from the primary display's, since this process is only system-DPI-aware and
         * Windows then reports that monitor's geometry in the primary's DPI space. Showing
         * the converted number alone would name a mode the panel has never been in.
         */
        const int shownWidth = display.panelWidth > 0 ? display.panelWidth : display.width;
        const int shownHeight = display.panelHeight > 0 ? display.panelHeight : display.height;

        std::string label;
        if (display.primary)
            label = Util::Format() << Tr("主显示器") << " · ";
        else if (nonPrimaryCount == 1)
            label = Util::Format() << Tr("副显示器") << " · ";

        label += display.name;
        if (shownWidth > 0)
            label += Util::Format() << "  " << shownWidth << "x" << shownHeight
                                    << " @" << display.desktopHz << "Hz";
        if (display.hasArcadeMode)
            label += Util::Format() << "  " << Tr("57/58Hz 可用");
        label += Util::Format() << "  · #" << display.number;
        labels.emplace_back(label);

        if (display.number == currentNumber)
            selected = static_cast<int>(i) + 1;
    }

    /*
     * With the setting left at its default the selection is the main display - which is what
     * the default means. Without this the combo would point at its first entry, which is only
     * the primary display by coincidence of ordering.
     */
    if (selected == 0)
    {
        for (size_t i = 0; i < outputs.size(); i++)
        {
            if (outputs[i].primary)
            {
                selected = static_cast<int>(i) + 1;
                break;
            }
        }
    }

    std::vector<const char*> items;
    items.reserve(labels.size());
    for (const std::string& text : labels)
        items.push_back(text.c_str());

    if (ComboRow(Tr("输出显示器##DisplayNumber"), &selected, items.data(), static_cast<int>(items.size())))
        config.Set("DisplayNumber", selected == 0 ? 0 : outputs[selected - 1].number);

    ImGui::SameLine();
    UiTextDim("(?)");
    if (ImGui::IsItemHovered())
        UiTooltip(
            "游戏窗口与独占全屏都出现在这台显示器上，下一局生效。\n"
            "「自动」跟随系统主显示器；末尾 #n 是 Windows 的显示器编号。");
}

static void DrawHardwarePresetSettings(Util::Config::Node& config)
{
    /*
     * Four combinations, not two.
     *
     * Most cards sit between "integrated" and "4K with 4x MSAA", and the four settings this sets are
     * not independent: per-pixel sorted transparency costs an extra pass per translucent priority
     * whose price scales with the pixel count, the two-subpass composite exists to remove MSAA
     * attachment bandwidth (so MSAA gets cheaper the more of it there is), and the derivative mip
     * modes only pay off once the surface is sharp enough to show the difference. A single high/low
     * split either wastes a mid-range card or overloads an entry-level one.
     *
     * The resolution is clamped to the desktop: a preset that asks for 4K on a 1080p panel would
     * create a window larger than the screen. The clamped size is remembered so the combo keeps
     * showing the tier that was picked rather than falling back to "custom".
     */
    struct QualityTier
    {
        const char* label;
        int width, height, msaa, alphaSort, texFilter;
        // The ultra-low tier is a whole-system choice rather than four picture knobs: it also
        // drops to the arcade cadence, turns interpolation and post-processing off, and uses the
        // cheap shader path. Matching it therefore has to look at those keys too, or the combo
        // would claim the tier while half of it had been changed by hand.
        bool ultraLow;
    };
    static const QualityTier tiers[] = {
        { "超低配极速（496×384，核显）", 496, 384, 0, 0, 0, true },
        { "核显 / 低配（1080p，1x）", 1920, 1080, 0, 0, 1, false },
        { "入门独显（1080p，2x）",    1920, 1080, 2, 0, 1, false },
        { "中端独显（2K，2x）",       2560, 1440, 2, 1, 2, false },
        { "高端独显（4K，4x）",       3840, 2160, 4, 1, 2, false },
    };
    constexpr int tierCount = IM_ARRAYSIZE(tiers);
    constexpr int customTierIndex = tierCount;

    OSD::Platform::DisplayMode desktopMode{};
    const bool haveDesktop = OSD::Platform::GetDesktopDisplayMode(OSD::Platform::kPrimaryDisplay, &desktopMode) &&
        desktopMode.width > 0 && desktopMode.height > 0;

    const int tierWidth = config["XResolution"].ValueAsDefault<int>(0);
    const int tierHeight = config["YResolution"].ValueAsDefault<int>(0);
    const int tierMsaa = config["MSAA"].ValueAsDefault<int>(0);
    const int tierAlphaSort = std::clamp(config["AlphaSortMode"].ValueAsDefault<int>(0), 0, 2);
    const int tierTexFilter = std::clamp(config["TexFilter"].ValueAsDefault<int>(0), 0, 2);
    const bool tierIntegerScale = config["IntegerScale"].ValueAsDefault<bool>(false);
#if defined(SUPERMODEL_OPENGL_ONLY)
    // Vulkan-only key: reading it in this build would create an empty node that the ini writer would
    // then persist into the file both executables share.
    constexpr int tierInternalRes = 0;
#else
    const int tierInternalRes = config["InternalResolution"].ValueAsDefault<int>(0);
#endif
    const bool tierQuadInterp = config["QuadInterpolation"].ValueAsDefault<bool>(true);
    const int tierRefreshMode = config["RefreshRateMode"].ValueAsDefault<int>(1);
    const bool tierInterp = config["MotionInterpolation"].ValueAsDefault<bool>(true) &&
        !config["LowLatency"].ValueAsDefault<bool>(false);
    const bool tierFxaa = config["PostFXAA"].ValueAsDefault<bool>(false);
    const int tierCrt = config["PostCRT"].ValueAsDefault<int>(0);
    const int tierGlow = config["PostGlow"].ValueAsDefault<int>(0);
    const bool tierFrost = config["UiFrostedGlass"].ValueAsDefault<bool>(true);

    const auto tierExtrasMatch = [&](const QualityTier& tier)
    {
        if (!tier.ultraLow)
            return true;
        // The OpenGL build does not offer low-resolution rendering (it is a Vulkan render-target
        // change), so its ultra-low tier is identified by the keys it does have.
#if defined(SUPERMODEL_OPENGL_ONLY)
        constexpr bool tierLowRes = true;
#else
        const bool tierLowRes = tierInternalRes == 1;
#endif
        return tierIntegerScale && tierLowRes && !tierQuadInterp && tierRefreshMode == 0 && !tierInterp &&
            !tierFxaa && tierCrt == 0 && tierGlow == 0 && !tierFrost;
    };
    const auto tierMatches = [&](const QualityTier& tier)
    {
        return tierWidth == tier.width && tierHeight == tier.height &&
            tierMsaa == tier.msaa && tierAlphaSort == tier.alphaSort &&
            tierTexFilter == tier.texFilter && tierExtrasMatch(tier);
    };

    int tierMatched = customTierIndex;
    for (int i = 0; i < tierCount; i++)
        if (tierMatches(tiers[i]))
            tierMatched = i;

    // A tier applied on a smaller display stores the clamped size, which matches no row by itself.
    // Remembering what was applied keeps the combo on that tier instead of snapping to "custom".
    static int appliedTier = -1;
    static int appliedWidth = 0, appliedHeight = 0;
    if (tierMatched == customTierIndex && appliedTier >= 0 &&
        tierWidth == appliedWidth && tierHeight == appliedHeight &&
        tierMsaa == tiers[appliedTier].msaa && tierAlphaSort == tiers[appliedTier].alphaSort &&
        tierTexFilter == tiers[appliedTier].texFilter && tierExtrasMatch(tiers[appliedTier]))
        tierMatched = appliedTier;

    // Same idea as the resolution combo: once the user asks for "custom" the selection stays there
    // even if the individual values happen to equal a tier, until a tier is picked again.
    static bool customSelected = false;
    static const char* tierItems[tierCount + 1] = {
        tiers[0].label, tiers[1].label, tiers[2].label, tiers[3].label, tiers[4].label,
        "自定义（按下面几项单独调）",
    };
    static const char* tierTr[tierCount + 1];
    int tierSelection = customSelected ? customTierIndex : tierMatched;

    UiSep("硬件预设");
    if (ComboRow(Tr("档位##QualityTier"), &tierSelection, UiItems(tierItems, tierCount + 1, tierTr), tierCount + 1))
    {
        if (tierSelection < tierCount)
        {
            const QualityTier& tier = tiers[tierSelection];
            int w = tier.width;
            int h = tier.height;
            if (haveDesktop)
            {
                if (w > desktopMode.width) w = desktopMode.width;
                if (h > desktopMode.height) h = desktopMode.height;
            }
            config.Set("XResolution", w);
            config.Set("YResolution", h);
#if !defined(SUPERMODEL_OPENGL_ONLY)
            // These keys exist only in builds with the Vulkan backend. Writing them from the
            // OpenGL-only executable would add root-level duplicates to the shared ini, where they
            // could shadow the real [Video] entries on the next Vulkan run.
            config.Set("MSAA", tier.msaa);
            config.Set("AlphaSortMode", tier.alphaSort);
            config.Set("TexFilter", tier.texFilter);
#endif
            if (tier.ultraLow)
            {
                /*
                 * The rest of the potato preset. None of these is a picture knob in the same sense
                 * as the four above, which is why the other tiers leave them alone: this tier is
                 * meant for a 24-EU integrated GPU where every per-pixel read and every extra
                 * present costs a frame.
                 *
                 *   - 496x384 is the native 2D tile size; IntegerScale turns it into a whole-number
                 *     magnification instead of a blurry non-integer stretch.
                 *   - Original 57.524 Hz halves the presents compared with a 120 Hz interpolated
                 *     setup, and turning interpolation off removes the sub-frame work entirely.
                 *   - QuadInterpolation off is the shader fast path (no four-corner pull, no
                 *     quadratic solve); TexFilter 0 keeps the radial single-tap lookup.
                 *   - The two post-processing switches are the most expensive full-screen passes, and
                 *     the frosted backdrop is a four-pass chain that only runs while the panel is
                 *     open - cheap, but on a 24-EU part the panel is the one moment the UI is in
                 *     front of a picture that cannot afford the extra bandwidth.
                 */
                config.Set("IntegerScale", true);
                config.Set("RefreshRateMode", 0);
                config.Set("MotionInterpolation", false);
                config.Set("LowLatency", true);
                config.Set("UiFrostedGlass", false);
#if !defined(SUPERMODEL_OPENGL_ONLY)
                // The 3D scene at the native 496x384, upscaled onto the picture. This is what keeps the
                // 24-EU class of integrated GPU at 60 Hz at 1080p; IntegerScale above stays set, but
                // this option takes precedence over it (see CalcScreenGeometry), so the preset's
                // picture fills the display instead of sitting in a 2x box.
                config.Set("InternalResolution", 1);
                config.Set("QuadInterpolation", false);
                config.Set("PostFXAA", false);
                config.Set("PostCRT", 0);
                config.Set("PostScanlines", 0);
                config.Set("PostGlow", 0);
#endif
                InfoLog("GUI: applied the ultra-low preset (496x384, no MSAA/interpolation/post, fast quad path).");
            }
            appliedTier = tierSelection;
            appliedWidth = w;
            appliedHeight = h;
            customSelected = false;
        }
        else
        {
            customSelected = true;
        }
    }
    HelpMarker(
        "一次配好分辨率、MSAA、半透明排序、贴图过滤等相关选项，下一局生效。\n"
        "超低配极速：496×384 原生 3D + 等比放大，关插值/后处理。\n"
        "核显 / 低配：1080p、1x、原版半透明与硬件贴图，最稳。\n"
        "入门独显：1080p + 2x MSAA。\n"
        "中端独显：2K + 2x MSAA + 逐像素排序 + 锐利贴图。\n"
        "高端独显：4K + 4x MSAA，同上最高画质。\n"
        "自定义：不套预设，只改下面各项。");

    // The ultra-low tier's low resolution comes from rendering the 3D scene at 496x384, not from the
    // number in XResolution/YResolution - and IntegerScale is not what keeps it alive any more.
    // Saying so here is the only place the two controls sit together.
    if (tierWidth == 496 && tierHeight == 384 && tierInternalRes == 0 && !tierIntegerScale)
        UiTextWarn("超低配的低开销靠「低分辨率渲染」（或「整数倍缩放」）；两者都关掉时全屏会按面板分辨率渲染，帧率会明显下降。");

}

#if !defined(SUPERMODEL_OPENGL_ONLY)
static void DrawPresentationCadenceSettings(Util::Config::Node& config, OSD::Platform::Window* window)
{
    /*
     * 呈现节奏 - how the arcade's 57.524 Hz relates to what the display scans out.
     *
     * Vulkan only: the OpenGL executable keeps the classic one-simulation-one-present loop, so
     * `highRefresh` is false there, the pacer is never configured, and the OpenGL renderers do not
     * implement PrepareFrame()/RenderFrame(alpha). Every control in this section would do nothing.
     *
     * This is the setting that removes the judder a 57.524 Hz game shows on a 60/120/144/240 Hz
     * panel, and it is one choice out of three rather than a checkbox, because the three modes
     * differ in *who* decides the next scan-out: the arcade (Original), the panel (Auto), or a
     * rate the user names (Fixed). Two of those are the same rate on paper and behave differently
     * in practice, which is exactly what a row of radio buttons can say and a checkbox cannot.
     *
     * Written straight to the configuration rather than through a temp value: these are read back
     * by the game loop every frame and the pacer is reconfigured when they change, so a change
     * applies to the next present without restarting the session.
     */
    UiSep("呈现节奏");
    {
        const int paceMode = config["RefreshRateMode"].ValueAsDefault<int>(1);
        bool paceChanged = false;

        if (UiRadioButton("原始 57.5Hz（与机台一致）##PaceOriginal", paceMode == 0))
        {
            config.Set("RefreshRateMode", 0);
            paceChanged = true;
        }
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "按真机的 57.524Hz 输出，一逻辑帧一画面，不插值。\n"
                "需要 57.5Hz 模式或 VRR 显示器，否则高刷屏会有规律顿挫。");

        if (UiRadioButton("跟随显示器（推荐）##PaceAuto", paceMode == 1))
        {
            config.Set("RefreshRateMode", 1);
            paceChanged = true;
        }
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "逻辑仍按 57.524Hz 推进，画面按显示器当前刷新率输出，\n"
                "亚帧用运动插值补平。60/75/90Hz 与高刷屏都适用。");

        if (UiRadioButton("固定呈现频率##PaceFixed", paceMode == 2))
        {
            config.Set("RefreshRateMode", 2);
            paceChanged = true;
        }
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "按下方指定频率输出，不切换显示器模式。\n"
                "60 Hz：最保守，任何屏都能跑。\n"
                "75 / 90 Hz：普通高刷或笔记本屏。\n"
                "120 / 144 / 165 Hz：主流电竞屏。\n"
                "240 / 480 Hz：需对应面板。\n"
                "低于实际刷新率只会重复帧；高了会被丢弃。");

        if (paceMode == 2)
        {
            ImGui::Indent();
            const int maxHz = MaxSupportedHzForWindow(window);

            // A value carried over from a faster display is clamped before the control is drawn,
            // so what the combo shows is what the pacer will actually use.
            const int configuredHz = config["FixedRefreshRate"].ValueAsDefault<int>(120);
            const int clampedHz = ClampFixedRefreshRate(configuredHz, maxHz);
            if (clampedHz != configuredHz)
            {
                config.Set("FixedRefreshRate", clampedHz);
                paceChanged = true;
            }

            FixedRefreshRateCombo(config, maxHz, paceChanged);
            if (maxHz > 0)
                UiTextDim("当前显示器最高支持 %d Hz；更高的档位已置灰。", maxHz);
            ImGui::Unindent();
        }

        UiTextDim("60/75/90Hz 显示器用「跟随显示器」即可；高刷屏请先在 Windows 或显卡控制面板固定 120/144/240Hz。");
        UiTextDim("G-SYNC：NVIDIA 控制面板 → 显示 → 设置 G-SYNC；「固定呈现频率」只改呈现目标，不切换模式。");

        /*
         * Exact multiples of the arcade rate, for a display that can scan that
         * fast.
         *
         * These are the only modes where the picture cannot land unevenly: two
         * presents per emulated frame at 115.048 Hz, four at 230.097, and so on,
         * with the weight between the two snapshots fixed by the slot rather than
         * measured from the clock. What a panel has to bring is headroom - the
         * target has to sit clearly under its own rate - and that is what the
         * greying out says.
         *
         * Kept on the same row and labelled with the multiple alone: the rates
         * are in the tooltip, because five numbers on one line is a table, not a
         * choice.
         */
        OSD::Platform::DisplayMode desktopMode{};
        const double panelHz =
            (OSD::Platform::GetDesktopDisplayMode(OSD::Platform::kPrimaryDisplay, &desktopMode) &&
             desktopMode.refreshHz > 1.0f) ? (double)desktopMode.refreshHz : 0.0;

        UiText("VRR 倍频");
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "向面板的整数倍频，每个仿真帧正好扫描 N 次。\n"
                "2x = 115.05Hz，需面板 120Hz 以上。\n"
                "3x = 172.57Hz，需 176Hz 以上。\n"
                "4x = 230.10Hz，需 235Hz 以上。\n"
                "8x = 460.19Hz，需 470Hz 以上。\n"
                "不够的档位置灰；都需 VRR 跟随 + 垂直同步。");

        static const struct { int phases; int mode; } kMultiples[] = {
            { 2, 3 }, { 3, 4 }, { 4, 5 }, { 8, 6 }
        };
        for (int i = 0; i < 4; i++)
        {
            const int phases = kMultiples[i].phases;
            const int value = kMultiples[i].mode;
            const bool supported = PanelSupportsPhaseCount(phases, panelHz);
            char label[32];
            snprintf(label, sizeof(label), "%dx##PaceVrr%d", phases, phases);

            if (i > 0)
                ImGui::SameLine();
            ImGui::BeginDisabled(!supported);
            if (UiRadioButton(label, paceMode == value))
            {
                config.Set("RefreshRateMode", value);
                paceChanged = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                /*
                 * Translated in pieces.
                 *
                 * UiTooltip() runs Tr() on its *format*, so a tooltip assembled with
                 * snprintf and handed over as "%s" never passes through the table -
                 * which is how this one ended up Chinese in the English interface
                 * while every string around it was translated.
                 */
                char tip[256];
                snprintf(tip, sizeof(tip),
                    Tr("%.2f Hz：每个仿真帧 %d 次扫描。\n%s"),
                    kModel3StepHz * phases, phases,
                    Tr(supported ? "本机面板支持。" : "本机面板刷新率不够，此项不可选。"));
                UiTooltip("%s", tip);
            }
        }

        if (panelHz > 1.0)
        {
            ImGui::Indent();
            if (PanelSupportsPhaseCount(2, panelHz))
            {
                UiTextDim("面板 %.0f Hz：2x 可用（%.2f Hz，余量 %.1f%%），倍频下的节奏与面板无关。",
                    panelHz, kModel3StepHz * 2.0,
                    (panelHz - kModel3StepHz * 2.0) * 100.0 / panelHz);
            }
            else
            {
                UiTextDim("面板 %.0f Hz：低于 120 Hz，无法使用整数倍频；可选择「跟随显示器」让插值接管。",
                    panelHz);
            }
            ImGui::Unindent();
        }

        /*
         * Interpolation is a separate switch because it trades latency for smoothness, and that is
         * a preference rather than a consequence of the cadence: someone on a 60 Hz panel may want
         * it off for the one frame of input lag, and someone on 240 Hz may want it on even in the
         * original rate's neighbourhood.
         *
         * The stored value is the resolved one: LowLatency is an alias that turns interpolation off
         * without changing the present rate, and leaving two switches that can disagree would give
         * a ticked box that does nothing. Writing both from one control keeps them consistent.
         */
        bool interp = config["MotionInterpolation"].ValueAsDefault<bool>(true) &&
            !config["LowLatency"].ValueAsDefault<bool>(false);
        ImGui::BeginDisabled(paceMode == 0);
        if (UiCheckbox("运动插值##MotionInterpolation", &interp))
        {
            config.Set("MotionInterpolation", interp);
            config.Set("LowLatency", !interp);
            paceChanged = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "在两次逻辑帧之间对相机和动态物体插值，高刷屏每帧都是新画面。\n"
                "关闭＝低延迟优先，比插值少约 17ms 延迟，但画面会重复。");

        if (interp && paceMode != 0)
        {
            ImGui::Indent();
            int scope = config["InterpolationScope"].ValueAsDefault<int>(0);
            const char* kScopeItems[] = { Tr("自动"), Tr("全部视口"), Tr("仅主视口") };
            if (UiCombo("插值范围##InterpolationScope", &scope, kScopeItems, 3))
            {
                config.Set("InterpolationScope", scope);
                paceChanged = true;
            }
            ImGui::SameLine();
            UiTextDim("(?)");
            if (ImGui::IsItemHovered())
                UiTooltip(
                    "自动：全屏与移动的小视口插值，固定的小视口离散更新。\n"
                    "全部视口：所有 3D 视口都插值。\n"
                    "仅主视口：只插值全屏视口。\n"
                    "2D 图层（计时、字幕）永不插值。");

            OSD::Platform::DisplayMode desktopMode{};
            if (OSD::Platform::GetDesktopDisplayMode(OSD::Platform::kPrimaryDisplay, &desktopMode) &&
                desktopMode.refreshHz > 0.0f)
            {
                const double stepHz = 57.524160;
                const double ratio = desktopMode.refreshHz / stepHz;
                UiTextDim("显示器当前 %.0f Hz：每 1 次逻辑步呈现 %.2f 帧，亚帧权重 0.00-1.00。",
                    desktopMode.refreshHz, ratio);
            }
            ImGui::Unindent();
        }

        if (paceChanged)
            InfoLog("Pace: presentation settings changed (mode %d, fixed %d Hz, interpolation %s).",
                config["RefreshRateMode"].ValueAsDefault<int>(1),
                config["FixedRefreshRate"].ValueAsDefault<int>(120),
                interp ? "on" : "off");
    }
}
#endif

static void DrawQuickSettings(Util::Config::Node& config, std::shared_ptr<CInputs>& inputs)
{
    // Language comes first because it changes every other label on the page.
    UiSep("语言");
    {
        // Each language is listed in its own language, which is what a language picker should do.
        const char* languageNames[] = { UiLanguageName(UiLanguage::English), UiLanguageName(UiLanguage::Chinese) };
        int languageIndex = GetUiLanguage() == UiLanguage::Chinese ? 1 : 0;
        if (ComboRow(Tr("界面语言##UiLanguage"), &languageIndex, languageNames, 2))
        {
            SetUiLanguage(languageIndex == 1 ? UiLanguage::Chinese : UiLanguage::English);
            config.Set("UiLanguage", languageIndex);
        }
    }

    /*
     * Interface appearance, directly under the language and in the same visual group: both decide how
     * the window itself is drawn rather than what the emulator does, and neither belongs on the video
     * page. The theme applies to this window immediately (see the watcher at the top of GUI()) and to
     * the in-game panel from the next session, because the overlay owns a second ImGui context.
     *
     * Light is listed first and is the default: 0 in the settings file, 1 for dark.
     */
    UiSep("界面外观");
    {
        static const char* themeItems[] = { "浅色（暖色护眼）", "深色", "原始界面（改动前）" };
        static const char* themeTr[3];
        const char* const* themeLabels = UiItems(themeItems, 3, themeTr);
        int theme = 0;
        if (UiThemeIsDark())
            theme = 1;
        else if (UiThemeIsClassic())
            theme = 2;
        if (ComboRow(Tr("界面主题##UITheme"), &theme, themeLabels, 3))
        {
            SetUiTheme(theme == 1 ? UiTheme::WarmDark
                : (theme == 2 ? UiTheme::Classic : UiTheme::WarmPaper));
            config.Set("UITheme", theme);
        }
        if (ImGui::IsItemHovered())
            UiTooltip("%s", Tr("浅色：温润羊皮纸色底 + 陶土琥珀强调色，默认。\n"
                "深色：暖岩灰玻璃底 + 琥珀强调色，晚上更不刺眼。\n"
                "原始界面：毛玻璃重做之前的平面暖纸样式（蓝色强调色、不透底、无背景图）。\n"
                "前两个主题都不使用纯黑与纯白；切换立即生效。"));

        /*
         * Frosted glass is a property of the two glass themes. The original theme paints opaque panes
         * and never records the blur chain, so the switch would be a no-op there - greyed out rather
         * than hidden, with the reason in the tooltip, so the row does not change shape.
         */
        const bool glassTheme = !UiThemeIsClassic();
        if (!glassTheme)
            ImGui::BeginDisabled();
        BoolSetting(config, "毛玻璃背景##UiFrostedGlass", "UiFrostedGlass");
        if (!glassTheme)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UiTooltip("%s", Tr("面板背景改用模糊后的画面（双重 Kawase 模糊），带 1px 玻璃高光边。\n"
                "关闭后改用不透明底色，适合核显或驱动不支持的情形。\n"
                "原始界面本来就不透明，此开关对它无效。"));
    }

    DrawHardwarePresetSettings(config);

    UiSep("画面");
    int renderer = RendererIndex(config);
#if defined(SUPERMODEL_PURE_VULKAN)
    /*
     * Nothing to choose: this executable contains exactly one renderer, and the two executables are
     * separate downloads.  The disabled radio and the line under it that used to say so were
     * explaining a choice the reader does not have.
     */
    renderer = 0;
#elif defined(SUPERMODEL_OPENGL_ONLY)
    /*
     * New 3D and Legacy 3D are the choice here; both are OpenGL engines, so the graphics API itself
     * is not a setting in this executable and is not mentioned.
     */
    if (UiRadioButton("New 3D", renderer == 1))
        SetRenderer(config, 1);
    ImGui::SameLine();
    if (UiRadioButton("Legacy 3D", renderer == 2))
        SetRenderer(config, 2);
#else
    if (UiRadioButton("Vulkan 3D", renderer == 0))
    {
        SetRenderer(config, 0);
        g_rendererNotice.clear();       // the user has made a deliberate choice; the notice is spent
    }
    ImGui::SameLine();
    if (UiRadioButton("New 3D", renderer == 1))
    {
        SetRenderer(config, 1);
        g_rendererNotice.clear();
    }
    ImGui::SameLine();
    if (UiRadioButton("Legacy 3D", renderer == 2))
    {
        SetRenderer(config, 2);
        g_rendererNotice.clear();
    }
#endif

#if !defined(SUPERMODEL_OPENGL_ONLY)
    /*
     * Shown right under the renderer radios, because that is where the remedy is. Dismissible so it
     * does not become permanent furniture for someone who is content to stay on OpenGL.
     *
     * The notice is translated here rather than where it is posted: the posting site is in the game
     * session, which has no business knowing the interface language. Tr() keys on the Chinese source
     * text, so the string posted is the key.
     */
    if (!g_rendererNotice.empty())
    {
        UiTextWarn("%s", Tr(g_rendererNotice.c_str()));
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr("知道了##RendererNotice")))
            g_rendererNotice.clear();
        HandOver();
    }
#endif

    /*
     * Nothing is said here about the in-game overlay, and the absence is deliberate.
     *
     * There used to be a line here explaining that Tab only worked under Vulkan 3D, because the panel
     * was drawn into the game's own swapchain and the OpenGL backends expose none. That is no longer
     * true - the overlay has an OpenGL backend as well, drawing into the default framebuffer just
     * before the buffer swap - so the panel and the statistics readout now work whichever renderer is
     * selected. A warning about a limitation that no longer exists is worse than no warning, because
     * it stops people trying the thing that works.
     */

#if !defined(SUPERMODEL_OPENGL_ONLY)
    if (renderer != 0)
        ImGui::BeginDisabled();
    BoolSetting(config, "Vulkan 校验层##VkValidation", "VkValidation");
    if (renderer != 0)
        ImGui::EndDisabled();
    HelpMarker(
        "让 Vulkan 校验层逐帧检查绘制调用、描述符和同步，发现的问题写进 Supermodel.log。"
        "正常游玩请关闭（会明显变慢）；排查花屏、闪烁或崩溃时临时打开。");

    if (renderer == 0)
    {
        DeviceCombo(config, "显示设备##VulkanDevice", "VulkanDevice", GetDisplayDevices(),
            "自动（默认高性能显卡）", "没有检测到可用的 Vulkan 显卡。");
    }
    else
#endif
    {
        /*
         * OpenGL has no API for choosing a GPU, so this is not a setting - and the panel no longer
         * pretends it is.
         *
         * A Vulkan physical device is enumerated and selected by the application; an OpenGL context
         * belongs to a window and the driver decides which adapter serves it. What this program can do
         * is *ask* to be put on the discrete GPU, through two exported symbols the driver reads at load
         * time (NvOptimusEnablement, AmdPowerXpressRequestHighPerformance - see the top of Main.cpp).
         * Those are process-wide flags read once at startup: no control can change them, which is why
         * the Combo that used to sit here was disabled with a single item in it.
         *
         * It also displayed a guess. The name came from the *Vulkan* device list, matched on "NVIDIA" or
         * "AMD", and was labelled "locked to the discrete GPU" - a claim about the rendering that
         * nothing had verified. GL_RENDERER answers the same question directly, and the settings window
         * has a context to ask it from, so this shows what OpenGL reported instead of what the request
         * was meant to achieve.
         */
        /*
         * The measured card stays on the page; the explanation of what can and cannot be changed moves
         * to the tooltip. Which GPU OpenGL landed on is data about the reader's own machine and the
         * whole reason this line exists, whereas the three lines of "how to change it" only matter to
         * someone who wants to change it.
         */
        if (s_glRendererName.empty())
        {
            UiTextDim("    %s", Tr("实际渲染卡：未知（未能读取 GL_RENDERER）"));
        }
        else
        {
            // The name is the driver's own string and is not translated, like the Vulkan device names
            // above it.
            const std::string line = Util::Format() << Tr("实际渲染卡：") << s_glRendererName;
            UiTextHighlight("    %s", line.c_str());
        }
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "OpenGL 无法在程序内选择显卡，只能「请求」独显。\n"
                "要换卡：Windows 设置 → 系统 → 显示 → 图形，为该程序指定「高性能」。");
    }

    /*
     * Only the two multiples a real panel can land on carry a marker:
     *
     *   3x  1488x1152  needs 1152 rows -> the one entry a 1440-row (2K) panel reaches
     *   5x  2480x1920  needs 1920 rows -> the one entry a 2160-row (4K) panel reaches
     *
     * Everything else is left unmarked on purpose. 4x needs 1536 rows and 6x/8x need 2304/3072, none
     * of which exist as a 16:9 panel, so a row-count suffix cannot tell a user what to pick - it only
     * adds noise to a list that is scanned to answer one question, "which one do I want". The row
     * requirement is not lost: the (?) tooltip below states the rule, and once integer scaling is on
     * the live readout says which multiple is actually in effect and what the next step would need.
     *
     * These labels are also the wrong place to look for that readout - the multiple is derived from
     * the panel at runtime, never from the preset chosen here.
     */
    struct Preset { int w; int h; const char* label; };
    static const Preset presets[] = {
        { 496,  384,  "1x 原生 (496 x 384)" },
        { 992,  768,  "2x 分辨率 (992 x 768)" },
        { 1488, 1152, "3x 分辨率 (1488 x 1152 - 2K 屏推荐)" },
        { 1984, 1536, "4x 分辨率 (1984 x 1536)" },
        { 2480, 1920, "5x 分辨率 (2480 x 1920 - 4K 屏推荐)" },
        { 2976, 2304, "6x 分辨率 (2976 x 2304)" },
        { 3968, 3072, "8x 分辨率 (3968 x 3072)" },
    };
    int width = config["XResolution"].ValueAs<int>();
    int height = config["YResolution"].ValueAs<int>();
    int matched = IM_ARRAYSIZE(presets);
    for (int i = 0; i < IM_ARRAYSIZE(presets); i++)
        if (presets[i].w == width && presets[i].h == height)
            matched = i;

    /*
     * Selecting "custom" changes neither XResolution nor YResolution, so the choice cannot be
     * recovered from them the way a preset can: on the next frame the values still match the preset
     * the user started on, the combo snaps back to it, and the width/height fields disappear as soon
     * as they appear. Remember the choice instead. The values still decide the display until the
     * user explicitly asks for custom, so a resolution set elsewhere is shown as the preset it is.
     */
    static bool customSelected = false;
    int preset = customSelected ? IM_ARRAYSIZE(presets) : matched;

    const char* presetLabels[IM_ARRAYSIZE(presets) + 1];
    for (int i = 0; i < IM_ARRAYSIZE(presets); i++)
        presetLabels[i] = Tr(presets[i].label);
    presetLabels[IM_ARRAYSIZE(presets)] = Tr("自定义分辨率");
    if (ComboRow(Tr("分辨率"), &preset, presetLabels, IM_ARRAYSIZE(presetLabels)))
    {
        if (preset < IM_ARRAYSIZE(presets))
        {
            width = presets[preset].w;
            height = presets[preset].h;
            customSelected = false;
        }
        else
        {
            customSelected = true;
        }
    }

    if (preset == IM_ARRAYSIZE(presets))
    {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
        UiInputInt("自定义宽度##XResolution", &width);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
        UiInputInt("自定义高度##YResolution", &height);
    }
    if (width < 160) width = 160;
    if (height < 120) height = 120;
    if (width > 7680) width = 7680;
    if (height > 4320) height = 4320;
    config.Set("XResolution", width);
    config.Set("YResolution", height);

    DrawOutputDisplayCombo(config);

    BoolSetting(config, "全屏##FullScreen", "FullScreen");
    ImGui::SameLine();
    BoolSetting(config, "拉伸##Stretch", "Stretch");
    ImGui::SameLine();
    BoolSetting(config, "宽屏##WideScreen", "WideScreen");
    BoolSetting(config, "垂直同步##VSync", "VSync");
    ImGui::SameLine();
    /*
     * Named for where it writes, not for what it measures.
     *
     * "Show frame rate" read as if it controlled the readout's FPS line, which is why it looked
     * redundant next to the Stats panel box. It does something else - the window title, plus the audio
     * buffer counters that the readout does not carry - and the label now says so.
     */
    BoolSetting(config, "标题栏帧率##ShowFrameRate", "ShowFrameRate");
    if (ImGui::IsItemHovered())
        UiTooltip("%s", Tr("每秒把帧率和音频缓冲欠载/溢出计数写进标题栏；独占全屏时看不到。"));
    ImGui::SameLine();

    /*
     * Colour presets for the readout. ImGui keeps the item pointers for as long as the popup is
     * open, so the translated labels live in a static array, like the other combos on this page.
     * The combo carries no visible caption: the row is already full and the current preset name is
     * self-explanatory. The explanation is a tooltip on the box itself, not another (?) marker.
     *
     * It sits before the checkbox that turns the readout on, and is disabled while that box is
     * clear: changing the colours of something that is not drawn only invites the question of why
     * nothing happened.
     *
     * Width matters here. At the 1280-pixel minimum this row is only 805 px wide and the three
     * checkboxes plus their help marker consume most of it; inserting the picker before the last
     * checkbox costs its width plus one gap. The English preset names are therefore single words
     * (the full names live in the tooltip) and "Statistics panel" becomes "Stats panel", so the
     * row fits without clipping or wrapping at the minimum width.
     */
    static const char* statsStyleItems[] = {
        "琥珀经典", "冷灰暖黄", "高对比", "极简描边"
    };
    static const char* statsStyleTr[4];
    const char* const* statsStyleLabels = UiItems(statsStyleItems, 4, statsStyleTr);
    int statsStyle = config["StatsOsdStyle"].ValueAsDefault<int>(0);
    if (statsStyle < 0 || statsStyle >= 4)
        statsStyle = 0;
    const bool statsStyleEnabled = config["ShowStatsOsd"].ValueAsDefault<bool>(false);
    if (!statsStyleEnabled)
        ImGui::BeginDisabled();
    if (ComboRow(Tr("##StatsOsdStyle"), &statsStyle, statsStyleLabels, 4))
        config.Set("StatsOsdStyle", statsStyle);
    if (!statsStyleEnabled)
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        UiTooltip("%s", Tr("统计面板的配色预设（先勾选「统计面板」）。\n"
            "琥珀经典：天青标签 + 琥珀数值，帧率按 57/50 fps 变色。\n"
            "冷灰暖黄：标签低调，数值暖黄。\n"
            "高对比：深底板 + 白标签，适合亮场景。\n"
            "极简描边：无底板，文字带黑描边。"));

    ImGui::SameLine();
    /*
     * The readout is toggled in play with the key above Tab, and saying so here is the only way anyone
     * would find that out - a keyboard shortcut with no mention anywhere may as well not exist.
     */
    BoolSetting(config, "统计面板##ShowStatsOsd", "ShowStatsOsd");
    HelpMarker(
        "在画面左上角显示引擎、刷新率、CPU/GPU 频率与占用、内存、帧率和 1% Low。\n"
        "游戏里按 ` 键开关；Vulkan 与 OpenGL 都支持。");

    /*
     * What integer scaling is actually doing, stated rather than left to be inferred.
     *
     * The multiple is derived from the panel's own mode and appears in no setting, so "why am I
     * getting 5x when I picked the 6x preset" has no answer anywhere in this window. It has two
     * causes and this says both: the panel is too short for the next step, and the resolution preset
     * is overwritten. Neither is visible from the controls that appear to decide them.
     *
     * Recomputed per frame from the cached display list, which costs a subtraction and a divide.
     */
    {
        const IntegerScaleInfo info = GetIntegerScaleInfo();
        if (info.active && info.valid)
        {
            ImGui::Indent();
            UiTextDim("整数倍缩放实际生效：%dx，可见区 %d x %d。",
                info.multiple, info.visibleWidth, info.visibleHeight);
            if (info.panelKnown)
            {
                UiTextDim("面板 %d x %d：按高度只能取到 %d 倍。",
                    info.panelWidth, info.panelHeight, info.multiple);
            }
            else
            {
                UiTextDim("面板自身尺寸未能读取，以上按窗口尺寸估算。");
            }
            if (info.nextVisibleHeight > info.panelHeight)
            {
                UiTextDim("下一档 %dx 需要面板至少 %d 行，本面板只有 %d 行，所以取不到。",
                    info.nextMultiple, info.nextVisibleHeight, info.panelHeight);
            }
            const int requestedWidth = config["XResolution"].ValueAs<int>();
            const int requestedHeight = config["YResolution"].ValueAs<int>();
            if (requestedWidth != info.visibleWidth || requestedHeight != info.visibleHeight)
            {
                UiTextDim("「分辨率」里的 %d x %d 在此模式下会被覆盖为 %d x %d。",
                    requestedWidth, requestedHeight, info.visibleWidth, info.visibleHeight);
            }
            ImGui::Unindent();
        }
    }


    // Arcade-exact sync: how the display is driven. One choice out of three rather than two
    // independent checkboxes.
    //
    // The two exclusive modes are not independent. A fixed refresh rate takes precedence in
    // ResolveFullScreenMode(), and the G-SYNC flag has no separate effect once it is set, so ticking
    // both meant one of them silently did nothing - and the one that lost was not the one the user
    // ticked last. "Only one of these can apply" is not something a pair of checkboxes can express.
    //
    // Only shown while fullscreen is enabled: none of these have any meaning in windowed mode, and
    // offering them anyway invites the reader to wonder what they do.
    if (config["FullScreen"].ValueAsDefault<bool>(false))
    {
        const bool fixedRateOn = config["ExclusiveRefreshRate"].ValueAsDefault<float>(0.0f) > 0.0f;
        const bool gsyncOn = config["ExclusiveFullscreen"].ValueAsDefault<bool>(false);
        int mode = fixedRateOn ? 2 : (gsyncOn ? 1 : 0);

        // Writes both underlying settings together so they can never disagree.
        const auto applyMode = [&](int picked)
        {
            config.Set("ExclusiveFullscreen", picked != 0);
            config.Set("ExclusiveRefreshRate", picked == 2 ? 57.5242f : 0.0f);
        };


        /*
         * One `(?)` beside the heading carries the "which one do I want" answer, rather than a line of
         * prose under it.
         *
         * The answer needs no detection, which is what makes it worth giving: a VRR display wants the
         * G-SYNC option, a display without VRR wants the fixed rate and needs a custom resolution to
         * get it, and someone with neither should stay on borderless rather than select an option that
         * will quietly do nothing.
         *
         * It moved into the tooltip because it is advice about the group of controls rather than part
         * of any one of them, and three settings do not need four lines of text above them. Wording
         * follows the check below rather than describing the ideal case, because "use the third one"
         * is unhelpful advice next to a greyed-out third one.
         */
        const bool arcadeModeAvailable = ArcadeDisplayModeAvailable();

        UiText("全屏刷新方式");
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
        {
            if (arcadeModeAvailable)
                UiTooltip(
                    "第 1 项：无边框，桌面合成器定节奏，最省事。\n"
                    "第 2 项：显示器跟随模拟器，需要 VRR。\n"
                    "第 3 项：把显示器锁到 57.5Hz，需已有 57/58Hz 模式。\n"
                    "怎么选：有 VRR 用第 2 项，否则第 1 项。");
            else
                UiTooltip(
                    "第 1 项：无边框，桌面合成器定节奏，最省事。\n"
                    "第 2 项：显示器跟随模拟器，需要 VRR。\n"
                    "第 3 项：把显示器锁到 57.5Hz，需先建自定义分辨率。\n"
                    "怎么选：有 VRR 用第 2 项，否则第 1 项。");
        }

        if (UiRadioButton("无边框全屏（默认）##SyncOff", mode == 0))
            applyMode(0);
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "与设置界面共用窗口，来回切换最顺，帧率可能略低。\n"
                "驱动已为窗口启用 G-SYNC 时，本模式也能让面板跟随。");

        if (UiRadioButton("独占全屏 · G-SYNC/VRR##SyncGSync", mode == 1))
            applyMode(1);
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
        {
#if defined(SUPERMODEL_OPENGL_ONLY)
            UiTooltip(
                "独占全屏，显示器跟随模拟器，需要 VRR（EDID 范围含 57.5Hz）。\n"
                "若读不到 EDID 或不包含，仍按「画面」页的刷新率呈现。");
#else
            UiTooltip(
                "独占全屏，显示器跟随呈现节奏，需要 VRR。\n"
                "面板的 VRR 范围必须包含当前呈现速率（见「画面」页）。");
#endif
        }

        if (mode == 1)
        {
            ImGui::Indent();
#if defined(SUPERMODEL_OPENGL_ONLY)
            if (VrrArcadeRateSupported())
                UiTextDim("已按街机 57.5Hz 自动呈现（显示器时序范围包含 57.5Hz）。");
            else
                UiTextDim("未确认显示器时序范围包含 57.5Hz，按「画面」页的刷新率呈现。");
#else
            /*
             * Name the rate the panel will actually follow. This is the line that keeps "Fixed 120 Hz"
             * and "exclusive G-SYNC" from reading as contradictory choices: the cadence group on the
             * Video tab owns the rate, this option only decides who drives the scan.
             */
            double presentHz = kModel3StepHz;
            const int paceMode = config["RefreshRateMode"].ValueAsDefault<int>(1);
            if (paceMode == 2)
                presentHz = config["FixedRefreshRate"].ValueAsDefault<int>(120);
            else if (paceMode >= 3 && paceMode <= 6)
            {
                static const int kPhases[] = { 0, 0, 0, 2, 3, 4, 8 };
                presentHz = kModel3StepHz * kPhases[paceMode];
            }
            else if (paceMode != 0)
            {
                // Auto, and anything unrecognised (Main.cpp maps the latter to Auto as well).
                OSD::Platform::DisplayMode desktopMode{};
                presentHz = (OSD::Platform::GetDesktopDisplayMode(OSD::Platform::kPrimaryDisplay, &desktopMode) &&
                    desktopMode.refreshHz > 1.0f) ? (double)desktopMode.refreshHz : 60.0;
            }
            UiTextDim("面板会跟随呈现节奏（%.2f Hz）扫描；节奏本身在「画面」页设置。", presentHz);
#endif
            ImGui::Unindent();
        }

        /*
         * Why the requirements above are a tooltip and not a paragraph.
         *
         * The two builds answer different questions here. In the OpenGL build the declared EDID
         * range still decides the frame limiter, so the tooltip is where the reader learns why the
         * rate did or did not switch to 57.5 Hz. In the Vulkan build the cadence group owns the rate
         * and this option only decides who drives the scan; the range still matters, but relative to
         * whatever rate that group produces. Either way "said" does not have to mean five lines on
         * the page: the sentence is there when someone is deciding, and out of the way when not.
         */

        /*
         * Why the option is not disabled when the EDID does not declare a VRR range.
         *
         * A declaration proves the capability, but its absence proves nothing: a display can support
         * VRR without announcing it (older AMD HDMI FreeSync lives in the driver rather than in the
         * EDID). Disabling on a missing declaration would take the option away from users who have
         * the capability and simply do not announce it. So the option stays selectable and the
         * consequence of a display that cannot do it is stated instead.
         *
         * Note the asymmetry with the fixed-rate option below, which *is* disabled. That one can be
         * checked and its failure is provable. This one's failure is merely a missed opportunity:
         * the session still runs, at the desktop mode, with frame pacing no worse than borderless.
         */

        /*
         * The fixed-rate option can be checked, so it is - and made unselectable when it cannot work.
         *
         * Creating a 57/58 Hz timing is a manual step in the driver control panel, so on most machines
         * it is simply missing. Left selectable in that state the option would quietly fall back to the
         * G-SYNC mode above it, meaning two choices that produce exactly the same result - and the one
         * that describes something else is the one that misleads. An unavailable option that says why
         * is more useful than an available one that does something different.
         *
         * arcadeModeAvailable is read once, near the top of this section, so that the "which one do I
         * want" line and this control cannot disagree about it.
         *
         * What stays on the page when the option is unavailable is deliberately narrow: one amber line
         * saying so, and the display's own declared timing range in the attention colour. The second of
         * those was asked for by name - a reader wants to see what their hardware says about itself -
         * and it is data rather than explanation, which is the line this cleanup draws. Everything
         * that interprets the data lives in the tooltip.
         */
        ImGui::BeginDisabled(!arcadeModeAvailable);
        if (UiRadioButton("独占全屏 · 固定街机刷新率 57.524Hz##SyncFixedRate", mode == 2))
            applyMode(2);
        ImGui::EndDisabled();
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
        {
            std::string tip;
            if (arcadeModeAvailable)
            {
                tip = "把显示器锁到真机 57.5Hz，与帧率 1:1。\n"
                      "少数显示器/驱动不接受该时序，黑屏就换回上一项。";
            }
            else
            {
                /*
                 * Worded to lead with the VRR case, because a display that supports VRR is the common
                 * one on a machine running a 3D emulator - and for it this option being unavailable is
                 * not a limitation at all, just a route it does not need.
                 *
                 * The check looks for a *discrete* display mode at 57 or 58 Hz, which lives in the
                 * panel's mode list or comes from a custom resolution. VRR is a different thing: a
                 * range the panel can scan anywhere within, without any mode existing for the specific
                 * rate. So "no 57/58 Hz mode" and "cannot do 57.5 Hz" are not the same statement, and
                 * read as one they sound like the display is at fault. Leading with "if you have VRR
                 * this does not matter" is what stops that reading - and moving it into the tooltip
                 * cost nothing, since the one line left on the page still says what is disabled.
                 */
                tip = "把显示器锁到真机 57.5Hz，与帧率 1:1。\n"
                      "不可用＝没有 57/58Hz 固定模式；支持 VRR 的话用上一项即可。\n"
                      "没有 VRR 时需先在驱动里创建 57.524Hz 自定义分辨率。";
            }
            // Same trap as the multiple buttons: the text was assembled from
            // literals into a std::string, so the format "%s" is all Tr() sees.
            UiTooltip("%s", Tr(tip.c_str()));
        }

        if (!arcadeModeAvailable || mode == 2)
        {
            ImGui::Indent();
            if (arcadeModeAvailable)
            {
                UiTextDim("已检测到显示器的 57/58 Hz 模式，可以 1:1 输出。");
            }
            else
            {
                UiTextColored(AttentionColour(),
                    "未检测到显示器的 57/58 Hz 模式，此项不可选。");

                /*
                 * The declared timing range, per attached display, which is the one piece of the
                 * explanation that is a measurement rather than advice.
                 *
                 * The comparison against 57.5 is done here rather than left to the reader: it is one
                 * subtraction, and the reader is looking at a settings window, not a calculator. The
                 * conclusion is kept to a single short line for the same reason the tooltip exists -
                 * what the "necessary condition, not proof" caveat means is spelled out on hover, and
                 * repeating it beside every display would undo the cleanup.
                 */
                const ArcadeDisplayInfo& info = GetArcadeDisplayInfo();
                if (!info.displays.empty())
                {
                    for (const DisplayTimingInfo& d : info.displays)
                    {
                        const bool fits = (57.5242f >= (float)d.minHz && 57.5242f <= (float)d.maxHz);

                        std::string detected = Util::Format() << d.name << "  " << d.minHz << "-" << d.maxHz << " Hz";
                        if (d.declaresVrr)
                            detected += Util::Format() << "  " << Tr("已声明 VRR") << " " << d.vrrMinHz << "-" << d.vrrMaxHz << " Hz";
                        UiTextHighlight("%s", detected.c_str());

                        if (fits)
                            UiTextDim("    %s", Tr("→ 57.5Hz 在时序范围内，可尝试创建自定义分辨率"));
                        else
                            UiTextDim("    %s", Tr("→ 57.5Hz 不在时序范围内，创建自定义分辨率无用"));
                    }
                }
                else
                {
                    UiTextDim("无法读取显示器 EDID，故不能判断 57.5Hz 是否在其时序范围内。");
                }
            }
            ImGui::Unindent();
        }
    }

    // The executable has no console by default (see SetConsoleVisible), so this is what brings one
    // up when the output is wanted. Goes through the same entry point the startup path uses, so the
    // rule about not hiding a console inherited from a shell is applied in one place.
    if (BoolSetting(config, "显示控制台窗口##ShowConsole", "ShowConsole"))
        SetConsoleVisible(config["ShowConsole"].ValueAsDefault<bool>(false));

    UiSep("声音");
    int sound = config["SoundVolume"].ValueAs<int>();
    if (UiSliderInt("音效##SoundVolume", &sound, 0, 200, "%d%%"))
        config.Set("SoundVolume", sound);
    int music = config["MusicVolume"].ValueAs<int>();
    if (UiSliderInt("音乐##MusicVolume", &music, 0, 200, "%d%%"))
        config.Set("MusicVolume", music);
    BoolSetting(config, "游戏音效##EmulateSound", "EmulateSound");
    ImGui::SameLine();
    BoolSetting(config, "MPEG 音乐##EmulateDSB", "EmulateDSB");
    DeviceCombo(config, "声卡设备##AudioDevice", "AudioDevice", GetAudioDevices(),
        "自动（系统默认）", "没有检测到可用的音频输出设备。");
    UiTextDim("Daytona 2 的背景音乐偏小，可以把音乐调到 150 以上。运行中 F9/F10 调音乐，F11/F12 调音效。");

    UiSep("手柄");
    struct InputChoice { const char* id; const char* label; };
#if defined(_WIN32) && defined(SUPERMODEL_PLATFORM_SDL2)
    static const InputChoice choices[] = {
        { "xinput", "XInput（Xbox 手柄）" },
        { "dinput", "DirectInput" },
        { "sdl", "SDL" },
        { "sdlgamepad", "SDL 手柄" },
        { "rawinput", "Raw Input" },
    };
#else
    static const InputChoice choices[] = {
        { "sdl", "SDL" },
        { "sdlgamepad", "SDL 手柄" },
    };
#endif
    std::string currentInput = config["InputSystem"].ValueAs<std::string>();
    int inputIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(choices); i++)
        if (currentInput == choices[i].id)
            inputIndex = i;
    const char* inputLabels[IM_ARRAYSIZE(choices)];
    for (int i = 0; i < IM_ARRAYSIZE(choices); i++)
        inputLabels[i] = Tr(choices[i].label);
    if (ComboRow(Tr("输入系统##InputSystem"), &inputIndex, inputLabels, IM_ARRAYSIZE(inputLabels)))
    {
        config.Set("InputSystem", std::string(choices[inputIndex].id));
        inputs.reset();
    }
    ImGui::SameLine();
    UiTextDim("(?)");
    if (ImGui::IsItemHovered())
    {
#if defined(_WIN32) && defined(SUPERMODEL_PLATFORM_SDL2)
        UiTooltip(
            "%s\n%s\n%s\n%s",
            Tr("XInput：Xbox 手柄，左右扳机分开。"),
            Tr("DirectInput / Raw Input：旧手柄或特殊设备。"),
            Tr("SDL：通用读取；SDL 手柄：按标准手柄映射，推荐。"),
            Tr("按键在「按键」页改；切换系统后旧绑定会失效。"));
#else
        UiTooltip(
            "%s\n%s\n%s",
            Tr("SDL 手柄：按标准手柄映射，推荐；Xbox 手柄请选它（Windows 上经 XInput 读取，左右扳机分开）。"),
            Tr("SDL：通用读取，适合键盘、鼠标或非标准设备。"),
            Tr("按键在「按键」页改；切换系统后旧绑定会失效。"));
#endif
    }
#if !defined(SUPERMODEL_PLATFORM_SDL2)
    /*
     * Say it where the choice is made. The SDL3 build has no DirectInput/XInput/Raw Input backend
     * (see GetInputSystem), and an Xbox pad owner looking for the old "XInput" entry otherwise
     * concludes the controller support is missing rather than renamed.
     */
    UiTextDim("Xbox 手柄选「SDL 手柄」；本版本不提供 XInput / DirectInput。");
#endif
    BoolSetting(config, "力反馈##ForceFeedback", "ForceFeedback");
    DrawTriggerPairingHint(config);

    // Controller preset: one standardised layout, with the detected pad shown so it is
    // obvious which device the preset will suit. Detection is cached; see RefreshDetectedController.
    if (!g_controllerDetected)
        RefreshDetectedController();
    ControllerInfo& detected = g_detectedController;

    struct PresetChoice { const char* id; const char* label; };
    static const PresetChoice controlPresets[] = {
        { "standard", "标准布局（Xbox / PS 通用）" },
        { "keyboard", "仅键盘" },
        { "custom",   "自定义（不改动）" },
    };
    static const PresetChoice families[] = {
        { "auto",        "自动检测" },
        { "xbox360",     "Xbox 360 手柄" },
        { "xboxone",     "Xbox One 手柄" },
        { "xboxseries",  "Xbox Series 手柄" },
        { "ps3",         "PS3 手柄 (DualShock 3)" },
        { "ps4",         "PS4 手柄 (DualShock 4)" },
        { "ps5",         "PS5 手柄 (DualSense)" },
        { "8bitdo",      "8BitDo 手柄" },
        { "generic",     "其他 PC 手柄" },
    };

    // Human-readable name for whatever was detected, resolved before the combo is built because
    // the "auto" entry uses it (see below).
    const char* detectedLabel = nullptr;
    if (!detected.name.empty())
    {
        if (detected.family == "xbox360")         detectedLabel = Tr("Xbox 360 手柄");
        else if (detected.family == "xboxone")    detectedLabel = Tr("Xbox One 手柄");
        else if (detected.family == "xboxseries") detectedLabel = Tr("Xbox Series 手柄");
        else if (detected.family == "ps3")        detectedLabel = Tr("PS3 手柄");
        else if (detected.family == "ps4")        detectedLabel = Tr("PS4 手柄");
        else if (detected.family == "ps5")        detectedLabel = Tr("PS5 手柄");
        else if (detected.family == "8bitdo")     detectedLabel = Tr("8BitDo 手柄");
        else                                      detectedLabel = Tr("其他 PC 手柄");
    }

    std::string presetFamily = config["ControllerFamily"].ValueAsDefault<std::string>("auto");
    int familyIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(families); i++)
        if (presetFamily == families[i].id)
            familyIndex = i;

    /*
     * The "auto" entry advertises what it resolved to: "自动检测（8BitDo 手柄）".
     *
     * Without this the combo read plain "自动检测" while the pad was correctly identified three
     * lines below, so it looked as though detection had not picked anything - the resolved family
     * existed only in the sentence underneath, not in the control that appears to report it.
     *
     * The stored value deliberately stays "auto". Writing the detected family back would freeze
     * the setting to whichever pad happened to be attached when the window was opened, and the
     * next controller plugged in would be read with the old family's expectations. That is exactly
     * what "auto" exists to prevent, so this changes the display only.
     *
     * No entry is added when the family is "generic": repeating "other PC pad" back to the user
     * says nothing they cannot see from the pad itself.
     */
    static thread_local char autoFamilyLabel[128];
    const char* familyLabels[IM_ARRAYSIZE(families)];
    for (int i = 0; i < IM_ARRAYSIZE(families); i++)
        familyLabels[i] = Tr(families[i].label);
    if (detectedLabel != nullptr && detected.family != "generic" && detected.family != "")
    {
        snprintf(autoFamilyLabel, sizeof(autoFamilyLabel), Tr("自动检测（%s）"), detectedLabel);
        familyLabels[0] = autoFamilyLabel;
    }

    if (ComboRow(Tr("手柄型号##ControllerFamily"), &familyIndex, familyLabels, IM_ARRAYSIZE(familyLabels)))
    {
        presetFamily = families[familyIndex].id;
        config.Set("ControllerFamily", presetFamily);
    }
    /*
     * The 8BitDo trigger note belongs to the controller model, so its marker sits on the combo. It
     * used to follow the "Detected: ..." line, and on a long pad name that line filled the column:
     * SameLine() then left the marker with a few pixels, so its three characters wrapped one per
     * line. The combo always has room, and the note is about the pad model either way.
     */
    if (detected.family == "8bitdo")
        HelpMarker("L2/R2 是按钮式扳机：绑定为刹车或油门可用，但是有/无两档，不是线性。");

    if (detected.name.empty())
        UiTextHighlight("未检测到手柄。请确认已插好，然后点下面的「重新检测」。");
    else
    {
        // The family is not repeated here: the combo above now reads "Auto-detect (8BitDo pad)",
        // so naming it again in this sentence said the same thing twice on the same screen.
        UiTextHighlight("检测到：%s（建议输入系统：%s）",
            detected.name.c_str(), detected.inputSystem.c_str());

        /*
         * The pad's Bluetooth mode changes what it presents itself as: an SN30 Pro+ in Switch mode
         * reports as a "Pro Controller", in XInput mode as an Xbox pad. The readout names what it
         * looks like because that decides whether XInput can see it at all. The L2/R2 button-note
         * lives on the controller-model combo above, where the marker has room for its three
         * characters even when the detected name is long.
         */
        if (detected.family == "8bitdo" && !detected.sdlKnowsDevice)
            UiTextWarn("SDL 未识别这块手柄，按钮编号无法保证与预设一致。请改用 SDL 手柄以外的输入系统，或用「输入设置」逐个重新绑定。");
    }

    /*
     * On its own line, below the readout rather than trailing after it.
     *
     * It used to be SameLine()'d onto the end of whatever text preceded it, which put a button in the
     * one place a reader looks last - and made it read as part of that sentence rather than as the
     * action belonging to it. Directly underneath, it is plainly the control for the line above.
     */
    if (UiSmallButton("重新检测"))
        RefreshDetectedController();
    ImGui::SameLine();
    UiTextDim("(?)");
    if (ImGui::IsItemHovered())
        UiTooltip(
            "分辨率变化后自动重新检测，也可以手动刷新一次。");

    std::string currentPreset = config["ControlPreset"].ValueAsDefault<std::string>("standard");
    int presetIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(controlPresets); i++)
        if (currentPreset == controlPresets[i].id)
            presetIndex = i;
    const char* controlPresetLabels[IM_ARRAYSIZE(controlPresets)];
    for (int i = 0; i < IM_ARRAYSIZE(controlPresets); i++)
        controlPresetLabels[i] = Tr(controlPresets[i].label);
    ComboRow(Tr("键位预设##ControlPreset"), &presetIndex, controlPresetLabels, IM_ARRAYSIZE(controlPresetLabels));

    if (UiButton("应用推荐键位"))
        ImGui::OpenPopup("ApplyPreset");
    HelpMarker("会覆盖「按键」页里已有的绑定。");

    // View cycling. Sits here rather than on the keys page because it changes how the view buttons
    // behave rather than what they are bound to.
    UiSep("视角切换");
    BoolSetting(config, "单键循环切换视角##VRCycle", "VRCycle");
    HelpMarker(
        "开启后一个键循环切换 4 个视角；只影响有 4 视角的游戏（Daytona 2、Scud Race、Le Mans 24）。");
    if (config["VRCycle"].ValueAsDefault<bool>(false))
    {
        const char* viewNames[] = { Tr("VR1（红）"), Tr("VR2（蓝）"), Tr("VR3（黄）"), Tr("VR4（绿）") };
        int def = config["VRDefaultView"].ValueAsDefault<int>(2);
        if (ComboRow(Tr("起始视角##VRDefaultView"), &def, viewNames, 4))
            config.Set("VRDefaultView", def);
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "进入游戏时的默认视角；Daytona 2 的第三人称跟车视角是 VR3（黄）。");
    }

    if (ImGui::BeginPopupModal("ApplyPreset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiText("把手柄和键盘键位重置为选定的预设？");
        UiTextDim("这会覆盖你在「按键」页里做过的所有改动。");
        if (UiButton("应用", ImVec2(120, 0)))
        {
            const std::string preset = controlPresets[presetIndex].id;
            config.Set("ControlPreset", preset);

            // When auto-detecting, also adopt the input system the pad works best with,
            // which is what makes the analogue triggers behave.
            std::string system = config["InputSystem"].ValueAsDefault<std::string>("sdlgamepad");
            if (presetFamily == "auto" && !detected.inputSystem.empty())
                system = detected.inputSystem;

            config.Set("InputSystem", system);
            ApplyControlPreset(config, preset, system);

            inputs.reset();     // bindings changed: rebuild on the next visit to the tab
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (UiButton("取消", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

/*
 * Network settings.
 *
 * Kept as its own function because it belongs on the 网络 tab, not buried at the bottom of 常用. It
 * used to be drawn there, and because these keys are listed in HasDedicatedUI the generic renderer
 * skipped them everywhere else - which left the 网络 tab showing nothing but three debug-output
 * options, while the settings anyone would actually look for sat at the end of an unrelated page.
 *
 * No section separator: the tab is already called 网络, and a heading repeating the tab name is noise.
 */
static void DrawNetworkSettings(Util::Config::Node& config)
{
    BoolSetting(config, "启用网络板##Network", "Network");
    ImGui::SameLine();
    BoolSetting(config, "模拟网络板##SimulateNet", "SimulateNet");
    int portIn = (int)config["PortIn"].ValueAs<unsigned>();
    int portOut = (int)config["PortOut"].ValueAs<unsigned>();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
    if (UiInputInt("本机端口##PortIn", &portIn))
    {
        if (portIn < 1) portIn = 1;
        if (portIn > 65535) portIn = 65535;
        config.Set("PortIn", (unsigned)portIn);
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
    if (UiInputInt("对方端口##PortOut", &portOut))
    {
        if (portOut < 1) portOut = 1;
        if (portOut > 65535) portOut = 65535;
        config.Set("PortOut", (unsigned)portOut);
    }
    static char address[256] = {};
    static std::string addressShadow;
    std::string addressValue = config["AddressOut"].ValueAs<std::string>();
    if (addressValue != addressShadow)
    {
        addressShadow = addressValue;
        std::snprintf(address, sizeof(address), "%s", addressValue.c_str());
    }
    if (ImGui::InputText(Tr("对方地址##AddressOut"), address, sizeof(address)))
    {
        addressShadow = address;
        config.Set("AddressOut", std::string(address));
    }
    if (UiButton("单机对连"))
    {
        unsigned port = config["PortIn"].ValueAs<unsigned>();
        config.Set("PortOut", port);
        config.Set("AddressOut", std::string("127.0.0.1"));
        config.Set("Network", true);
        config.Set("SimulateNet", true);
    }
    ImGui::SameLine();
    UiTextDim("Daytona 2 单机需要两个端口相同。");
}

/*
 * CRT filter presets.
 *
 * The scanline grid and the phosphor mask are two periodic patterns evaluated in the same
 * output-pixel space, so they beat against each other whenever their vertical periods are not in an
 * integer ratio. The beat period is 1 / |1/P_scan - 1/P_mask|, and that is what produces the
 * rainbow: 540 lines over 2160 rows is a 4-pixel pitch against a 6-pixel mask, which beats every
 * 12 pixels - a band about 2.2 mm tall on a 32" 4K panel. Setting either slider without the other
 * is exactly how that happens, so the presets exist to hand out combinations that cannot.
 *
 * Each entry is built so that
 *   - the mask's vertical period is an exact multiple of the scanline pitch, and
 *   - the mask pitch is a multiple of 3, so the three phosphor sub-holes land on whole pixels
 *     (a 4-pixel cell puts them 1.33 pixels apart, and the mask itself turns into colour noise).
 *
 * A slot mask's holes alternate up and down on successive rows, so its vertical period is twice the
 * cell pitch. That is why a 3-pixel scanline pitch pairs with either cell size but a 6-pixel pitch
 * only reaches a whole-number ratio with the smaller ones - and why 4 is not usable here at all:
 * it would need a 4-pixel cell to divide, and 4 is not a multiple of 3.
 */
struct CrtPreset
{
	const char* name;       // Chinese label, doubled as the translation key
	int scanPeriod;         // scanline pitch in output pixels
	int maskScale;          // phosphor cell pitch in output pixels (must be a multiple of 3)
	int scanIntensity;      // percent
	int maskIntensity;      // percent
	int gain;               // percent, wins back the light the mask removes
};

// Slot mask: the pattern Sega's arcade cabinets used, so every preset shares it.
static const int kCrtPresetMaskType = 1;

static const CrtPreset kCrtPresets[] =
{
	{ "细 · 现代 CRT（BVM 观感）",     3, 3, 70, 35, 167 },
	{ "细 · 点阵略粗",                 3, 6, 70, 45, 182 },
	{ "街机原机 · 扫描线最接近真机",   6, 3, 70, 35, 167 },
	{ "街机原机 · 粗点阵",             6, 6, 70, 50, 191 },
};

static const int kCrtPresetCount = (int)(sizeof(kCrtPresets) / sizeof(kCrtPresets[0]));

/*
 * Frame height the preset maths runs against. The settings window is drawn before the game starts,
 * so the desktop mode is the best available stand-in for the resolution the picture will use.
 */
static int CrtPresetOutputHeight()
{
	OSD::Platform::DisplayMode mode{};
	if (OSD::Platform::GetDesktopDisplayMode(OSD::Platform::kPrimaryDisplay, &mode) && mode.height > 0)
		return mode.height;
	return 2160;
}

/*
 * Line count that gives the preset's scanline pitch while remaining an exact multiple of the mask's
 * vertical period.
 *
 * Deriving the count from the frame height rather than storing it is what lets one preset hold its
 * appearance across resolutions: the periods are absolute in output pixels, so 6 pixels is 360
 * lines at 4K and 180 at 1080p. Storing a number would keep the count and halve the pitch.
 */
static int CrtPresetScanlines(const CrtPreset& preset, int outputHeight)
{
	const int maskPeriod = 2 * preset.maskScale;
	int cycles = maskPeriod / preset.scanPeriod;    // exact by construction of the table
	if (cycles < 1)
		cycles = 1;
	int base = outputHeight / maskPeriod;
	if (base < 1)
		base = 1;
	return cycles * base;
}

// Index of the preset the current settings match, or -1 once anything has been edited by hand.
static int MatchCrtPreset(const Util::Config::Node& config, int outputHeight)
{
	if (config["PostMaskType"].ValueAsDefault<int>(1) != kCrtPresetMaskType)
		return -1;

	const int scanlines = config["PostScanlines"].ValueAsDefault<int>(0);
	const int scanIntensity = config["PostScanlineIntensity"].ValueAsDefault<int>(60);
	const int maskScale = config["PostMaskScale"].ValueAsDefault<int>(2);
	const int maskIntensity = config["PostMaskIntensity"].ValueAsDefault<int>(50);
	const int gain = config["PostGain"].ValueAsDefault<int>(190);

	for (int i = 0; i < kCrtPresetCount; ++i)
	{
		const CrtPreset& p = kCrtPresets[i];
		if (scanIntensity == p.scanIntensity && maskScale == p.maskScale &&
			maskIntensity == p.maskIntensity && gain == p.gain &&
			scanlines == CrtPresetScanlines(p, outputHeight))
			return i;
	}
	return -1;
}

/*
 * Vertical period of the mask in output pixels. A slot mask staggers its holes on alternate rows, so
 * it repeats every two cells; grille and dot masks have no stagger and repeat every cell. The
 * distinction matters because it is the period, not the cell size, that has to divide the scanline
 * pitch.
 */
static int CrtMaskPeriod(int maskType, int maskScale)
{
	return maskType == 1 ? 2 * maskScale : maskScale;
}

/*
 * Bezel backdrop controls, drawn on the "画面" page.
 *
 * The three choices are one decision - off, the shipped picture, or a file - so they are a single
 * combo rather than a checkbox plus a path field whose disabled state would need explaining. The
 * brightness slider only appears once something is actually going to be drawn.
 *
 * Everything here is hand-written, which is why these keys are listed in HasDedicatedUI: otherwise
 * the generic renderer would draw them a second time from the Video group.
 */
#if !defined(SUPERMODEL_OPENGL_ONLY)
static void DrawBezelSettings(Util::Config::Node& config, OSD::Platform::Window* window)
{
	UiSep("机柜背景图");

	/*
	 * The backdrop fills the bars either side of the 4:3 picture, and widescreen removes those bars -
	 * so the two cannot both be doing anything. The controls are shown disabled rather than hidden so
	 * the combination is visible and explainable instead of one setting silently swallowing another.
	 */
	const bool wideScreen = config["WideScreen"].ValueAsDefault<bool>(false);
	// The backdrop is composed by Vulkan2D, so under OpenGL the controls do nothing at all - the
	// same reason the settings above are greyed out, applied to this section too.
	const bool vulkanEngine = config["Vulkan3DEngine"].ValueAsDefault<bool>(false);
	const bool greyOut = wideScreen || !vulkanEngine;
	if (greyOut)
		ImGui::BeginDisabled();

	int mode = config["BezelMode"].ValueAsDefault<int>(0);
	if (mode < 0 || mode > 2)
		mode = 0;

	const char* modes[] = { Tr("关闭"), Tr("内置机柜图"), Tr("自选图片") };
	if (ComboRow(Tr("背景图##BezelMode"), &mode, modes, 3))
		config.Set("BezelMode", mode);

	if (mode == 2)
	{
		const std::string imagePath = config["BezelImage"].ValueAsDefault<std::string>("");
		static char imageBuffer[1024] = {};
		static std::string imageShadow;
		// Re-seed the edit buffer whenever the value changes from anywhere else, the same way the ROM
		// path row does - otherwise a settings file that was just loaded would not show up here.
		if (imagePath != imageShadow)
		{
			imageShadow = imagePath;
			std::snprintf(imageBuffer, sizeof(imageBuffer), "%s", imagePath.c_str());
		}

		const float browseWidth = ImGui::CalcTextSize(Tr("浏览")).x + ImGui::GetStyle().FramePadding.x * 4.0f;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputText("##BezelImage", imageBuffer, sizeof(imageBuffer));
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			imageShadow = imageBuffer;
			config.Set("BezelImage", std::string(imageBuffer));
		}
		ImGui::SameLine();
		if (UiButton("浏览##BezelBrowse", ImVec2(browseWidth, 0)))
		{
			std::string picked = FileSystemPath::ResolvePath(imagePath);
			if (BrowseImageFile(window, picked))
			{
				imageShadow = picked;
				std::snprintf(imageBuffer, sizeof(imageBuffer), "%s", picked.c_str());
				config.Set("BezelImage", picked);
			}
		}
		UiTextDim("仅支持 PNG 格式。留空则使用内置机柜图。");
	}

	if (mode != 0)
	{
		int brightness = config["BezelBrightness"].ValueAsDefault<int>(100);
		ImGui::SetNextItemWidth(220.0f);
		if (UiSliderInt("背景图亮度##BezelBrightness", &brightness, 10, 200, "%d%%"))
			config.Set("BezelBrightness", brightness);
		HelpMarker("100% 为原图亮度。太亮会抢游戏画面，可以调低一些。");

		// The reason it does nothing under OpenGL is stated once, outside this block, so that it shows
		// whether or not a backdrop has been selected - a greyed-out control explains itself either way.
		UiTextDim("背景图只画在黑边上，不会盖住游戏画面。\n"
			"更改背景图或亮度需要重新开一局；在游戏内取消「宽屏」会立即显示。");
	}

	if (greyOut)
	{
		ImGui::EndDisabled();
		if (wideScreen)
		{
			// Said rather than left to be discovered: with widescreen on there are no side bars at all,
			// so a backdrop would have nothing to fill and would be drawn over the picture instead.
			UiTextWarn("已开启宽屏：画面占满左右，没有黑边可填，背景图不会显示。");
		}
		else
		{
			// Same reasoning: the controls are visibly inert, so the reason is stated next to them.
			UiTextWarn("当前渲染引擎是 OpenGL，背景图由 Vulkan 后端绘制，这里不起作用。");
		}
	}
}
#endif

/*
 * CRT colour and 2D upscaling.
 *
 * Both are picture decisions and both renderers honour them: OpenGL applies the colour matrices in
 * SuperAA and compiles the upscale filter into the tile shader, Vulkan applies them in its post and
 * composite passes. They used to be generic Core entries, which put two picture combos in a list of
 * runtime switches - the colour preset in particular read as an emulation-core option.
 */
void DrawPictureModeSettings(Util::Config::Node& config, bool overlayMode)
{
    // Same rule as DrawAdvancedSettingsPanel: the overlay drops the (?) markers, and the OpenGL
    // caveat has to be a visible line instead. Restored on exit so a later caller is unaffected.
    const bool previousOverlayMode = g_overlayMode;
    g_overlayMode = overlayMode;

    UiSep("色彩与缩放");

    // Values must match the allow-lists in Main.cpp and the labels special-cased in ProcessCombo().
    static const char* colourItems[] = { "关闭", "ARI/D93（日版）", "PVM", "BT.601 525/D93", "BT.601 525/D65（美版）", "BT.601 625/D65（欧版）" };
    int colour = config["CRTcolors"].ValueAsDefault<int>(0);
    static const char* colourTr[6];
    const std::string colourLabel = std::string(Tr("CRT 色彩")) + "##CRTcolors";
    if (ComboRow(colourLabel.c_str(), &colour, UiItems(colourItems, 6, colourTr), 6))
        config.Set("CRTcolors", colour);
    HelpMarker(
        "把当时街机 CRT 的色温换算到 sRGB，只改颜色。\n"
        "关闭：不转换，保持原始颜色。\n"
        "ARI/D93：日版机台的白点（偏冷）。\n"
        "PVM：专业监视器的白点。\n"
        "BT.601 525/D93、525/D65：美版 525 行制式（冷 / 标准白）。\n"
        "BT.601 625/D65：欧版 625 行制式。\n"
        "不确定就保持关闭；Vulkan 即时生效，OpenGL 下一局生效。");

    static const char* upscaleItems[] = { "锐利", "双五次", "双线性", "双三次" };
    int upscale = config["UpscaleMode"].ValueAsDefault<int>(2);
    static const char* upscaleTr[4];
    const std::string upscaleLabel = std::string(Tr("2D 放大")) + "##UpscaleMode";
    if (ComboRow(upscaleLabel.c_str(), &upscale, UiItems(upscaleItems, 4, upscaleTr), 4))
        config.Set("UpscaleMode", upscale);
    HelpMarker(
        "2D 层（HUD、文字）放大用的滤镜。\n"
        "锐利：最近邻，最硬但有锯齿。\n"
        "双五次 / 双三次：平滑且保留细节。\n"
        "双线性：最平滑、最软。\n"
        "Vulkan 即时生效；OpenGL 下一局生效。");

#if !defined(SUPERMODEL_OPENGL_ONLY)
    // The overlay hides the help markers, so the OpenGL caveat has to be visible in its own line.
    // In the settings window the (?) text above already says it, and repeating it would be noise.
    if (g_overlayMode && !config["Vulkan3DEngine"].ValueAsDefault<bool>(false))
        UiTextDim("OpenGL：这两项需要重新开一局生效。");
#else
    // The OpenGL-only build has no renderer choice: every session is OpenGL.
    if (g_overlayMode)
        UiTextDim("OpenGL：这两项需要重新开一局生效。");
#endif

    g_overlayMode = previousOverlayMode;
}

/*
 * CRT filter and glass glow.
 *
 * These are picture settings, so they are drawn on the Video page of the settings window; the
 * in-game panel calls this too, because a shadow mask is judged by eye against a moving picture.
 * They were the tail of the advanced page on Core, which put a picture decision two tabs away
 * from the picture options.
 *
 * Greyed out rather than hidden when the OpenGL renderer is running: the keys are read by
 * Vulkan2D only, so on that backend they can never apply.
 */
#if !defined(SUPERMODEL_OPENGL_ONLY)
static void DrawCrtFilterSettings(Util::Config::Node& config)
{
    const bool vulkan = config["Vulkan3DEngine"].ValueAsDefault<bool>(false);
    if (!vulkan)
        ImGui::BeginDisabled();

    /*
     * Collapsible because this section is long enough to push the save-state menu below the fold in
     * the in-game panel. Open by default so the filter is still the first thing found there; the
     * open/closed state is kept by ImGui for the session.
     */
    const bool crtOpen = ImGui::CollapsingHeader(Tr("CRT 滤镜##PostCRTSection"), ImGuiTreeNodeFlags_DefaultOpen);
    // The header is clickable whether the section is open or closed.
    HandOver();
    if (crtOpen)
    {
    // CRT mode. 0 off, 1 scanlines, 2 scanlines + shadow mask.
    static const char* crtItems[] = { "关闭", "扫描线", "扫描线 + 荫罩" };
    int crt = config["PostCRT"].ValueAsDefault<int>(0);
    static const char* crtTr[3];
    if (ComboRow(Tr("CRT 滤镜##PostCRT"), &crt, UiItems(crtItems, 3, crtTr), 3))
        config.Set("PostCRT", crt);
    HelpMarker(
        "模拟街机 CRT 的扫描线和荧光粉点阵。\n"
        "关闭：不处理。\n"
        "扫描线：只画横线。\n"
        "扫描线 + 荫罩：再加荧光粉点阵，最像真机；4K 效果最好。");

    // Recommended presets. Placed directly under the mode selector because this is the intended
    // entry point: every entry is a combination that is known not to beat, so nobody has to work out
    // the relationship between the scanline count and the mask pitch for themselves.
    {
        const int outputHeight = CrtPresetOutputHeight();
        const int matched = MatchCrtPreset(config, outputHeight);

        static const char* presetLabels[kCrtPresetCount + 1];
        presetLabels[0] = "自定义（手动）";
        for (int i = 0; i < kCrtPresetCount; ++i)
            presetLabels[i + 1] = kCrtPresets[i].name;

        static const char* presetTr[kCrtPresetCount + 1];
        int selection = matched + 1;
        if (ComboRow(Tr("推荐配置##PostPreset"), &selection,
                UiItems(presetLabels, kCrtPresetCount + 1, presetTr), kCrtPresetCount + 1))
        {
            if (selection > 0)
            {
                const CrtPreset& p = kCrtPresets[selection - 1];
                config.Set("PostCRT", 2);
                config.Set("PostScanlines", CrtPresetScanlines(p, outputHeight));
                config.Set("PostScanlineIntensity", p.scanIntensity);
                config.Set("PostMaskType", kCrtPresetMaskType);
                config.Set("PostMaskScale", p.maskScale);
                config.Set("PostMaskIntensity", p.maskIntensity);
                config.Set("PostGain", p.gain);
            }
        }
        HelpMarker(
            "扫描线与点阵的周期不成整数倍会打拍子（彩色横带），这些组合不会。\n"
            "只改扫描线/荫罩和亮度补偿，不改上面的 CRT 色彩。");

        // Spell out the two periods and whether they line up. This turns the artefact from something
        // the user has to recognise by eye into something the panel states outright.
        const int lines = config["PostScanlines"].ValueAsDefault<int>(0);
        if (config["PostCRT"].ValueAsDefault<int>(0) > 0 && lines > 0)
        {
            const double scanPitch = (double)outputHeight / (double)lines;
            const int maskType = config["PostMaskType"].ValueAsDefault<int>(1);
            const int maskScale = config["PostMaskScale"].ValueAsDefault<int>(2);
            const int maskPeriod = CrtMaskPeriod(maskType, maskScale);

            const double ratio = (double)maskPeriod / scanPitch;
            const double rounded = std::floor(ratio + 0.5);
            const bool aligned = rounded >= 1.0 && std::fabs(ratio - rounded) < 0.02;

            ImGui::Indent();
            UiTextDim("扫描线周期 %.1f px，点阵周期 %d px，比值 %.2f 倍",
                scanPitch, maskPeriod, ratio);
            if (aligned)
            {
                UiTextDim("整数倍对齐：不会出现彩色横带。");
            }
            else
            {
                const double beat = 1.0 / std::fabs(1.0 / scanPitch - 1.0 / (double)maskPeriod);
                UiTextWarn("不成整数倍：会出现周期约 %.0f px 的彩色横带。", beat);
            }
            ImGui::Unindent();
        }
    }

    if (config["PostCRT"].ValueAsDefault<int>(0) > 0)
    {
        int scanlines = config["PostScanlines"].ValueAsDefault<int>(0);
        ImGui::SetNextItemWidth(220.0f);
        if (UiSliderInt("扫描线数量##PostScanlines", &scanlines, 0, 1024,
                scanlines == 0 ? "自动 (384 行)" : "%d 行"))
            config.Set("PostScanlines", scanlines);
        HelpMarker(
            "画面高度上画多少条扫描线。0 = 按 Model 3 原始的 384 行。");

        // Integer scaling shrinks the visible picture below the window height, but the scanline count
        // is spread across the whole output frame (the shader uses vUv.y, which spans the frame, not
        // the picture), so fewer lines land inside the picture than the number set here. That
        // difference is invisible from the slider alone - someone aiming for the arcade's 384 lines
        // sets 384 and gets about 341 - so the result is stated outright.
        //
        // The pitch is unaffected: scanlines are counted against the frame, so 720 lines over a 2160
        // frame is 3.0 px whether the picture fills the frame or not. Only the count inside the
        // picture changes, which is exactly what is being reported here.
        //
        // Both heights come from GetIntegerScaleInfo() rather than being recomputed from the desktop
        // mode: a second copy of "which multiple did we get" would eventually disagree with the
        // geometry code, and this readout exists to explain that very number.
        if (config["IntegerScale"].ValueAsDefault<bool>(false) && config["FullScreen"].ValueAsDefault<bool>(false))
        {
            const IntegerScaleInfo info = GetIntegerScaleInfo();
            if (info.active && info.valid && info.visibleHeight > 0 && info.panelHeight > 0)
            {
                const int set = scanlines > 0 ? scanlines : 384;
                const int effective = set * info.visibleHeight / info.panelHeight;
                const int forArcade = (int)(384.0f * info.panelHeight / info.visibleHeight + 0.5f);

                ImGui::Indent();
                UiTextDim("整数倍缩放下可见区高 %d 像素，画面里实际约 %d 条扫描线。",
                    info.visibleHeight, effective);
                UiTextDim("想要正好 384 条（街机行数）请填 %d。", forArcade);
                ImGui::Unindent();
            }
        }

        int scanIntensity = config["PostScanlineIntensity"].ValueAsDefault<int>(60);
        ImGui::SetNextItemWidth(220.0f);
        if (UiSliderInt("扫描线强度##PostScanlineIntensity", &scanIntensity, 0, 100, "%d%%"))
            config.Set("PostScanlineIntensity", scanIntensity);

        if (config["PostCRT"].ValueAsDefault<int>(0) >= 2)
        {
            // Mask pattern. Keep in step with the PostMaskType allow-list.
            static const char* maskItems[] = { "栅条式（特丽珑类）", "槽孔式（世嘉街机）", "点阵式（普通彩显）" };
            int maskType = config["PostMaskType"].ValueAsDefault<int>(1);
            static const char* maskTr[3];
            if (ComboRow(Tr("点阵类型##PostMaskType"), &maskType, UiItems(maskItems, 3, maskTr), 3))
                config.Set("PostMaskType", maskType);
            HelpMarker(
                "荧光粉的排列方式。\n"
                "栅条式：特丽珑类显示器。\n"
                "槽孔式：世嘉街机，想还原原机选它。\n"
                "点阵式：普通彩显。");

            int maskScale = config["PostMaskScale"].ValueAsDefault<int>(2);
            ImGui::SetNextItemWidth(220.0f);
            if (UiSliderInt("点阵粗细##PostMaskScale", &maskScale, 1, 8, maskScale == 1 ? "1 像素 (1:1)" : "%d 像素"))
                config.Set("PostMaskScale", maskScale);
            HelpMarker(
                "一个荧光粉点占几个像素。4K 用 2~3，1080p 用 1。");

            int maskIntensity = config["PostMaskIntensity"].ValueAsDefault<int>(50);
            ImGui::SetNextItemWidth(220.0f);
            if (UiSliderInt("点阵强度##PostMaskIntensity", &maskIntensity, 0, 100, "%d%%"))
                config.Set("PostMaskIntensity", maskIntensity);
            HelpMarker(
                "红/绿/蓝荧光粉之间的明暗差异。调低更像普通显示器，调高颗粒感更强。");

            int gain = config["PostGain"].ValueAsDefault<int>(135);
            ImGui::SetNextItemWidth(220.0f);
            if (UiSliderInt("亮度补偿##PostGain", &gain, 100, 250, "%d%%"))
                config.Set("PostGain", gain);
            HelpMarker(
                "点阵和扫描线会让画面变暗，这里补回来；太暗就往上调。");
        }
    }
    }   // end of the CRT section

    UiSep("玻璃辉光");

    int glow = config["PostGlow"].ValueAsDefault<int>(0);
    ImGui::SetNextItemWidth(220.0f);
    if (UiSliderInt("辉光强度##PostGlow", &glow, 0, 100, glow == 0 ? "关闭" : "%d%%"))
        config.Set("PostGlow", glow);
    HelpMarker(
        "模拟 CRT 玻璃面板的柔和光晕：亮部向外晕开，暗部不受影响。");

    if (config["PostGlow"].ValueAsDefault<int>(0) > 0)
    {
        int levels = config["PostGlowLevels"].ValueAsDefault<int>(4);
        ImGui::SetNextItemWidth(220.0f);
        if (UiSliderInt("辉光级数##PostGlowLevels", &levels, 1, 8))
            config.Set("PostGlowLevels", levels);
        HelpMarker(
            "光晕铺开的范围；级数越多越大越淡。4K 用 5，1080p 用 4。");

        int threshold = config["PostGlowThreshold"].ValueAsDefault<int>(40);
        ImGui::SetNextItemWidth(220.0f);
        if (UiSliderInt("辉光阈值##PostGlowThreshold", &threshold, 0, 100, "%d%%"))
            config.Set("PostGlowThreshold", threshold);
        HelpMarker(
            "多亮才开始发光；调高更干净，调低整片会发蒙。");
    }

    if (!vulkan)
        ImGui::EndDisabled();
}
#endif

#if !defined(SUPERMODEL_OPENGL_ONLY)
/*
 * Sinden light-gun border.
 *
 * Its own section rather than part of the CRT filter: the border is not a picture effect, it is a
 * signal for the gun camera, and Vulkan2D draws it after the CRT pass so the two cannot interact.
 */
static void DrawLightGunBorderSettings(Util::Config::Node& config)
{
    const bool vulkan = config["Vulkan3DEngine"].ValueAsDefault<bool>(false);
    if (!vulkan)
        ImGui::BeginDisabled();

    const bool sindenOpen = ImGui::CollapsingHeader(Tr("光枪白边##SindenBorderSection"));
    HandOver();
    if (sindenOpen)
    {
        const std::string enableLabel = std::string(Tr("Sinden 光枪白边")) + "##SindenBorder";
        bool enabled = config["SindenBorder"].ValueAsDefault<bool>(false);
        if (ImGui::Checkbox(enableLabel.c_str(), &enabled))
            config.Set("SindenBorder", enabled);
        HandOver();
        HelpMarker(
            "在画面四周画白边，供 Sinden 光枪摄像头识别。\n"
            "画在最终输出上，不受 CRT/机柜背景影响；只对光枪游戏有意义。");

        if (enabled)
        {
            int width = config["SindenBorderWidth"].ValueAsDefault<int>(16);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::SliderInt(Tr("白边宽度##SindenBorderWidth"), &width, 0, 256, "%d px"))
                config.Set("SindenBorderWidth", width);
            HelpMarker(
                "边框像素宽度；1080p 建议 16-32，4K 建议 32-64。");

            int brightness = config["SindenBorderBrightness"].ValueAsDefault<int>(100);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::SliderInt(Tr("白边亮度##SindenBorderBrightness"), &brightness, 0, 100, "%d%%"))
                config.Set("SindenBorderBrightness", brightness);
            HelpMarker(
                "边框亮度；100% 纯白，环境光强可调低，太低会难识别。");
        }
    }

    if (!vulkan)
        ImGui::EndDisabled();
}
#endif

/*
 * HD texture pack switches, plus a reload button in the in-game panel.
 *
 * Drawn on the 核心 page (below the runtime options) and in the in-game panel. The
 * switches are read by the renderer every simulation frame, so they take effect
 * without a new session; the panel's reload button and the F4 binding rescans the
 * pack. While the panel is open the emulator is paused, so a reload completes when
 * the panel is closed and the game resumes - the hint says so rather than leaving
 * the button looking unresponsive.
 *
 * The settings window has no renderer running, so it offers the switches only; a
 * pack is scanned when the next session starts.
 */
void DrawHDTextureSettings(Util::Config::Node& config, bool overlayMode)
{
#if defined(SUPERMODEL_OPENGL_ONLY)
    (void)config;
    (void)overlayMode;
#else
    bool vulkan = true;
#if !defined(SUPERMODEL_PURE_VULKAN)
    // Dual build: the same page is reachable with either renderer selected. The switches and the
    // reload button are Vulkan-only, so grey them out rather than leaving controls that do nothing.
    vulkan = config["Vulkan3DEngine"].ValueAsDefault<bool>(false);
#endif

    UiSep("高清纹理");
    if (!vulkan)
        ImGui::BeginDisabled();

    bool load = config["LoadHDTextures"].ValueAsDefault<bool>(false);
    if (UiCheckbox("加载高清纹理包##LoadHDTextures", &load))
        config.Set("LoadHDTextures", load);
    if (!overlayMode)
        HelpMarker(
            "读取 Data/Assets/HDTextures/<游戏>/ 里的替换贴图，按内容哈希匹配。\n"
            "支持 PNG 和 WebP，分辨率可以是原图的 2x、4x、8x 等任意倍数，\n"
            "并支持完整的 8 位平滑 Alpha。找不到替换的贴图仍用原版。");

    bool dump = config["DumpHDTextures"].ValueAsDefault<bool>(false);
    if (UiCheckbox("导出当前游戏纹理##DumpHDTextures", &dump))
        config.Set("DumpHDTextures", dump);
    if (!overlayMode)
        HelpMarker(
            "把游戏实际使用的每块子贴图解码成 RGBA8 PNG，写到\n"
            "Data/Assets/DumpTextures/<游戏>/，文件名带内容哈希。\n"
            "同一张内容只写一次；超分后放进 HDTextures 目录即可替换。");

    if (overlayMode)
    {
        if (UiButton("重新加载高清纹理##ReloadHDTextures"))
        {
            Vulkan3D::RequestHDTextureReload();
            InfoLog("HD textures: reload requested from the in-game panel.");
        }
        ImGui::SameLine();
        UiTextDim("（也可按 F4；关闭面板后生效）");
    }
    else
    {
        UiTextDim("替换贴图：Data/Assets/HDTextures/<游戏>/（PNG 或 WebP）");
        UiTextDim("导出贴图：Data/Assets/DumpTextures/<游戏>/（PNG）");
        UiTextDim("游戏中按 F4 可重新扫描，无需重启。");
    }

    if (!vulkan)
    {
        ImGui::EndDisabled();
        UiTextDim("仅 Vulkan 渲染引擎有效，当前不是。");
    }
#endif
}

/*
 * Advanced rendering settings. These live in the Video group like the rest of the renderer
 * options, but are kept on their own page because they are only honoured by the Vulkan backend and
 * need a new session - the render targets and pipelines are built when a game starts.
 *
 * A new session, not a program restart: RunGameSession() is called once per game and tears the
 * renderer down afterwards, so returning to this window with Esc and starting a game is enough. The
 * wording in the panel deliberately says so, because "restart" reads as "close the program".
 */
void DrawAdvancedSettingsPanel(Util::Config::Node& config, bool overlayMode)
{
    // RAII-ish: restored on exit so a later caller cannot be affected. The settings window draws this
    // with the flag clear and the overlay with it set; the two never run in the same frame, but
    // leaving a global flipped would be the kind of thing that only shows up much later.
    const bool previousOverlayMode = g_overlayMode;
    g_overlayMode = overlayMode;

#if defined(SUPERMODEL_OPENGL_ONLY)
    // This executable has one renderer family; the Vulkan-only page sections below
    // are not compiled at all.
    const bool vulkan = false;
#else
    const bool vulkan = config["Vulkan3DEngine"].ValueAsDefault<bool>(false);
#endif

    /*
     * "Start a new game", not "restart the emulator".
     *
     * It said restart, which overstates it: the renderer, its render targets and its pipelines are all
     * built per session, and quitting a game with Esc returns to the settings window rather than
     * exiting. So the whole procedure is Esc, change the setting, start a game - and telling someone
     * to close and reopen the program sends them through a longer loop for no reason.
     *
     * The overlay cannot offer these because it runs inside a session that is already under way.
     */
    UiTextDim(overlayMode
        ? "滤镜和宽屏开关即时生效；抗锯齿等选项仍需要重新开一局。"
        : "这些选项需要重新开一局才会生效（不用退出程序：按 Esc 回到本窗口再启动即可）。");
#if !defined(SUPERMODEL_OPENGL_ONLY)
    if (!vulkan)
    {
        ImGui::Spacing();
        // The renderer radios are on the 常用 (Common) page - not on 画面 (Video), which is where this
        // text used to send people. 画面 holds the picture options (CRT colour, 2D upscaling, the CRT
        // filter and the backdrop), all of which work under OpenGL too; naming it here sent the
        // reader to a page that cannot change the renderer at all.
        UiText("当前渲染引擎是 OpenGL，本页的抗锯齿与滤镜都不起作用。");
        UiText("想用这些效果，请到「常用」页把渲染引擎切换到 Vulkan 3D。");
        ImGui::Spacing();
    }
#endif

#if !defined(SUPERMODEL_OPENGL_ONLY)
    if (!vulkan)
        ImGui::BeginDisabled();

    /*
     * Hardware MSAA is omitted from the overlay.
     *
     * Its sample count is baked into the pipelines and render targets when they are built - that is,
     * when a game starts - so a control here would appear to work and do nothing until the next one.
     * The settings window still offers it, because that is where starting a new game is the expected
     * next step.
     */
    if (!overlayMode)
    {
    UiSep("抗锯齿");
    // Values must stay in step with the MSAA config allow-list.
    static const char* msaaItems[] = { "关闭", "2x MSAA", "4x MSAA", "8x MSAA" };
    static const int msaaValues[] = { 0, 2, 4, 8 };
    int msaa = config["MSAA"].ValueAsDefault<int>(0);
    int msaaIndex = 0;
    for (int i = 0; i < 4; ++i)
        if (msaaValues[i] == msaa)
            msaaIndex = i;
    static const char* msaaTr[4];
    if (ComboRow(Tr("硬件 MSAA##MSAA"), &msaaIndex, UiItems(msaaItems, 4, msaaTr), 4))
        config.Set("MSAA", msaaValues[msaaIndex]);
    HelpMarker(
        "显卡对多边形边缘做抗锯齿，贴图保持锐利。\n"
        "关闭：最快。\n"
        "2x / 4x：画质与开销的常用折中。\n"
        "8x：更平滑，4K 下开销大，且需驱动支持。");

    UiSep("贴图过滤");

    /*
     * Where the 3D scene takes its mip level from.
     *
     * The default is what the hardware did: a radial estimate from the polygon's
     * texture-NP factor, isotropic across the polygon. The other two use the uv
     * derivatives the quad interpolation makes exact, so a surface running away
     * from the camera - a road, a wall - keeps the detail its pixel footprint can
     * support. That is sharper than the hardware rather than identical to it, and
     * can shimmer slightly where the hardware's blur hid aliasing, hence opt-in.
     */
    static const char* texFilterItems[] = { "硬件（按距离，原机行为）", "锐利（屏幕空间导数）", "锐利 + 4x 各向异性" };
    int texFilter = config["TexFilter"].ValueAsDefault<int>(0);
    if (texFilter < 0 || texFilter > 2)
        texFilter = 0;
    static const char* texFilterTr[3];
    if (ComboRow(Tr("贴图过滤##TexFilter"), &texFilter, UiItems(texFilterItems, 3, texFilterTr), 3))
        config.Set("TexFilter", texFilter);
    HelpMarker(
        "远处贴图用哪一级 mip。\n"
        "硬件：按原机径向公式估算，最忠实。\n"
        "锐利：按像素覆盖面积计算，倾斜路面/墙壁更清晰。\n"
        "锐利 + 4x 各向异性：再沿拉伸方向采 4 次样。\n"
        "后两项不是原机行为，可能轻微闪烁。");

    /*
     * Exact quad interpolation. This is the switch the ultra-low preset turns off: the fragment
     * stage then uses the rasteriser's interpolated attributes instead of pulling four corners and
     * solving the quad's own (s, t). It is a quality/performance trade rather than a correctness
     * option, so it lives next to the other per-pixel lookup choices rather than in the preset only.
     */
    bool quadInterpolation = config["QuadInterpolation"].ValueAsDefault<bool>(true);
    if (UiCheckbox("四边形精确插值##QuadInterpolation", &quadInterpolation))
        config.Set("QuadInterpolation", quadInterpolation);
    HelpMarker(
        "关闭后改用显卡三角形插值，跳过每像素 4 次顶点读取和二次求解。\n"
        "核显档位用；非平面四边形会有 0-2 对角线接缝。下一局生效。");

    UiSep("半透明排序");

    /*
     * Real3D 硬件只有两层半透明缓冲，New3D 也是这么还原的：每层每个像素只留
     * 最靠前的一个面，最后把两层合在一起。同一像素超过两层（尾烟、水花、树叶、
     * 玻璃）或同一层内提交顺序与深度相反时，就会出现半透明被吞、前后颈倒。
     *
     * 逐像素排序把当前 Priority 内每个通过深度测试的半透明片元存进固定槽位，按深度从远到
     * 近排序后再合成，能彻底解决这两类问题；代价是每个含半透明的 Priority 多两次最小
     * pass，以及 4 层/像素的 A-Buffer（4K 约 253 MB）。默认保持原版，因为它是逐位兼容的。
     *
     * 街机真机把两层半透明各自写进棋盘格的一半像素（偶格 / 奇格），所以密集的效果（Sega
     * Rally 2 的雪雾）总有一半像素留着后面的不透明画面，而不是两层一重叠就变成实心。代价
     * 是不再走双 Subpass 片上合成，半透明层要多存一遍再读回来。
     *
     * **这一档是试验模式。** 按像素拆层后，合成器只能从 2×2 组反推覆盖，而 LOD 交叉淡化
     * （Daytona 2 的瀑布：同一个面画两遍、alpha 互补）需要“每一像素两层都在 → 不透明”这个
     * 信息，它在半像素写入时就丢了。实测瀑布区蓝色像素 20.1%，原版 4.9%（详见
     * sdl3_reports/2026-10-27-checkerboard-hd）。所以下拉框和提示都标了“试验”，避免用户
     * 把它当成通用的高清选项。
     */
    static const char* alphaSortItems[] = { "原版（双层，逐位兼容）", "逐像素排序（OIT）",
        "街机真机（棋盘格，试验）" };
    int alphaSort = config["AlphaSortMode"].ValueAsDefault<int>(0);
    if (alphaSort < 0 || alphaSort > 2)
        alphaSort = 0;
    static const char* alphaSortTr[3];
    // No trailing label: the section header above already names this option, and at the minimum
    // window width the duplicate text pushed the (?) onto its own line.
    if (ComboRow("##AlphaSortMode", &alphaSort, UiItems(alphaSortItems, 3, alphaSortTr), 3))
        config.Set("AlphaSortMode", alphaSort);
    HelpMarker(
        "决定重叠的半透明表面怎么合成。下一局生效。\n"
        "AlphaSortMode = 0：Original (two layers, bit-exact) —— 原版 OG 的 2-Subpass 快速混合。\n"
        "AlphaSortMode = 1：Per-pixel sorted (OIT) —— 4 槽位 A-Buffer，现代高画质 OIT。\n"
        "AlphaSortMode = 2：Arcade hardware (checkerboard) —— 试验模式，只为雪雾/尘土；"
        "无法还原 LOD 交叉淡化（瀑布、水幕会变成闪动的蓝块），这类场景用 0 或 1。");
    }   // end of the sections omitted from the overlay
#endif

    /*
     * FXAA and integer scaling are also omitted from the overlay.
     *
     * FXAA is a matter of "on or off" for a whole session rather than something to dial in, and
     * integer scaling is worse than that: it is read where the viewport geometry is computed, which
     * happens on a resize rather than every frame, so a control here would change nothing until the
     * window was resized. Both stay in the settings window, which is where a restart-expecting
     * control belongs.
     *
     * What is left - and what the overlay exists for - is the CRT filter: a dozen values judged by
     * eye against a moving picture.
     */
    if (!overlayMode)
    {
#if !defined(SUPERMODEL_OPENGL_ONLY)
    BoolSetting(config, "FXAA 后处理抗锯齿##PostFXAA", "PostFXAA");
    HelpMarker(
        "开销极低的后处理抗锯齿，画面会略软；有独显优先用 MSAA。");
#endif

    UiSep("画面缩放");

    /*
     * The one control on this page that is not Vulkan-only. Integer scaling is applied in
     * CalcScreenGeometry, which every renderer goes through.
     */
#if !defined(SUPERMODEL_OPENGL_ONLY)
    if (!vulkan)
        ImGui::EndDisabled();
#endif

    /*
     * Low-resolution 3D rendering.
     *
     * Rasterises the scene at the Model 3's native 496x384 and upscales it onto the picture, which is
     * the one change that makes a 24-EU integrated GPU hold 60 Hz at 1080p: the scene is about seven
     * times cheaper while the 2D HUD keeps its own resolution. It sits next to IntegerScale because
     * both decide how the finished picture is scaled, and only one of them can be in charge.
     */
    	static const char* internalResItems[] = {
    		"关闭（按画面分辨率渲染）",
    		"496×384（原生，等比放大）",
    		"992×768（2x 原生，等比放大）",
    	};
    	int internalRes = config["InternalResolution"].ValueAsDefault<int>(0);
    	if (internalRes < 0 || internalRes > 2)
    		internalRes = 0;
    	static const char* internalResTr[3];
    	// No trailing label either: the options describe themselves, and the label was what pushed the
    	// (?) onto a second line at 1280 logical width.
    	if (ComboRow("##InternalResolution", &internalRes,
    			UiItems(internalResItems, 3, internalResTr), 3))
    		config.Set("InternalResolution", internalRes);
    	HelpMarker(
    		"3D 场景按固定小分辨率渲染，再等比放大到画面矩形；2D HUD 仍按原分辨率，保持清晰。\n"
    		"关闭：按画面分辨率渲染，最清晰也最慢。\n"
    		"496×384：街机原生分辨率，与画面矩形同比例；1080p 全屏仍是 1395×1080，左右黑边保留。\n"
    		"992×768：2 倍原生分辨率，比 496×384 清晰，填充开销仍远低于画面分辨率。\n"
    		"开启后「整数倍缩放」与「宽屏」不生效。下一局生效。");

    const bool lowRes = config["InternalResolution"].ValueAsDefault<int>(0) != 0;
    ImGui::BeginDisabled(lowRes);
    BoolSetting(config, "整数倍缩放（点对点）##IntegerScale", "IntegerScale");
    ImGui::EndDisabled();
    HelpMarker(
        "只按整数倍放大，余量留黑边；文字更均匀，但损失屏幕面积。\n"
        "倍率按面板真实分辨率算（496×384 的整数倍），勾选后会覆盖「分辨率」。\n"
        "关闭后全屏按面板分辨率渲染（1080p 上约 1395×1080），核显帧率会明显下降。");
    if (lowRes)
        UiTextDim("「低分辨率渲染」已开启，整数倍缩放被忽略。");

    // The same readout as the one on the 常用 page, repeated here because this is the page where the
    // option is switched on: having to leave the page to learn what it did is the whole problem.
    {
        const IntegerScaleInfo info = GetIntegerScaleInfo();
        if (info.active && info.valid)
        {
            ImGui::Indent();
            UiTextDim("实际生效 %dx：可见区 %d x %d（面板 %d x %d）。",
                info.multiple, info.visibleWidth, info.visibleHeight,
                info.panelWidth, info.panelHeight);
            if (info.nextVisibleHeight > info.panelHeight)
            {
                UiTextDim("下一档 %dx 需要 %d 行，本面板只有 %d 行。",
                    info.nextMultiple, info.nextVisibleHeight, info.panelHeight);
            }
            ImGui::Unindent();
        }
    }
    }   // end of the sections omitted from the overlay

    /*
     * Widescreen is the one display option the overlay exposes. It is live: Vulkan3D re-reads the
     * key every frame for the 3D frustum and Vulkan2D re-reads it for the compositor rectangle, so
     * an A/B comparison can be made in the same scene without starting a new game. OpenGL is
     * deliberately not offered because its viewport and scissor are computed on resize.
     */
    if (overlayMode && vulkan)
    {
        UiSep("宽屏");
        /*
         * Low-resolution rendering needs a scene target the width of the picture, so a widened frustum
         * has nowhere to go. It wins over widescreen (see CVulkan3D::RefreshSceneViewport), and the
         * switch is greyed out here rather than left looking live: this section exists to be toggled
         * while watching the picture, so a control that does nothing would be blamed on the renderer.
         */
        const bool lowResSession = config["InternalResolution"].ValueAsDefault<int>(0) != 0;
        ImGui::BeginDisabled(lowResSession);
        BoolSetting(config, "宽屏##WideScreen", "WideScreen");
        ImGui::EndDisabled();
        HelpMarker(
            "只加宽水平视锥，不拉伸画面。\n"
            "部分游戏的 2D 背景不延伸，四角可能露黑。");
        if (lowResSession)
            UiTextDim("「低分辨率渲染」已开启，本局宽屏不生效。");
        else
            UiTextDim("此开关即时生效，无需重新开局。");
        /*
         * The blue border that is not a bug.
         *
         * WideBackground stretches the bottom 2D layer across the whole window and is independent of
         * WideScreen. With the latter off, the 3D picture returns to 4:3 while that stretched layer
         * still fills the sides and the letterbox, so what looks like a coloured bar is game
         * background, not a clear colour - there is no setting here that can turn it black except
         * switching WideBackground off, which lives on the settings window's Video page.
         */
        if (!config["WideScreen"].ValueAsDefault<bool>(false) &&
            config["WideBackground"].ValueAsDefault<bool>(false))
        {
            UiTextWarn("「宽屏背景」已开启：2D 背景仍会拉伸铺满窗口，所以取消宽屏后四周不是黑边。要黑边请关掉「宽屏背景」。");
        }
    }

    /*
     * The CRT filter and the glass glow live on the settings window's Video page now, next to the
     * other picture options. The in-game panel still draws them here: that is the one place they
     * can be judged against a moving picture.
     */
#if !defined(SUPERMODEL_OPENGL_ONLY)
    if (overlayMode)
        DrawCrtFilterSettings(config);
    if (overlayMode)
        DrawLightGunBorderSettings(config);
#endif

    g_overlayMode = previousOverlayMode;
}

/*
 * Text shown in the confirmation dialog after a load. Built while loading because that is where the
 * file name, the setting count and the ROM-path decision are all known; the dialog itself is drawn
 * from the popup block, which has none of that information.
 */
static std::string g_loadReport;

/*
 * Replaces the live settings with those from a file the user picked.
 *
 * The file is merged over the built-in defaults rather than used verbatim, so a settings file that
 * only records the handful of values the user cared about still leaves everything else at a sane
 * value - without that, every absent key would fall back to whatever the ini reader does with a
 * missing node, which is not the same thing.
 *
 * Returns false when there is nothing to apply. On success g_loadReport describes what happened.
 */
static bool LoadSettingsFromFile(Util::Config::Node& config, const std::string& path)
{
    Util::Config::Node selected("Global");

    // FromINIFile() and FromXMLFile() report *failure* as true, which reads backwards at the call
    // site and is easy to get wrong: writing `if (!FromINIFile(...)) return;` looks like a sanity
    // check but actually discards every successful load, so the dialog closes and nothing changes.
    // The convention is confirmed by the other callers - Main.cpp ignores the result, and the two
    // FromXMLFile() callers treat true as "parse failed".
    const bool couldNotOpen = Util::Config::FromINIFile(&selected, path);
    if (couldNotOpen)
        return false;

#if defined(SUPERMODEL_OPENGL_ONLY)
    // A file written by the dual-renderer build carries settings this executable
    // cannot honour.  Dropping them here keeps them out of the count below and,
    // more importantly, out of the copy that is written back to disk.
    Util::Config::StripRetiredVulkanSettings(selected);
#endif

    int applied = 0;
    for (auto it = selected.begin(); it != selected.end(); ++it)
    {
        if (it->IsLeaf() && it->Exists())
            ++applied;
    }

    // An empty file parses "successfully" into an empty section. Applying it would quietly reset
    // every setting to the built-in defaults, which looks like data loss rather than a reload, so
    // it is reported instead.
    if (applied == 0)
        return false;

    Util::Config::Node merged("Global");
    Util::Config::MergeINISections(&merged, DefaultConfig(), selected);

    /*
     * The ROM directory needs more thought than the other settings because it is machine-specific
     * rather than a preference.
     *
     * Always keeping the current one is too blunt: someone who deliberately edited the path and
     * loaded the file back would see the path silently ignored. Always taking the file's is too
     * blunt the other way: a settings file carried over from another machine points at a directory
     * that does not exist here, and the game list comes up empty with no explanation.
     *
     * So the file's path wins when it exists on this machine, and the current one is kept when it
     * does not. Either way the outcome is stated in the confirmation, which is the part that was
     * missing: the load was working, but with the file's only differing setting being a path that no
     * longer existed, it looked like nothing had happened at all.
     */
    const std::string currentRom = config["ROMPath"].ValueAs<std::string>();
    const std::string fileRom = selected["ROMPath"].ValueAsDefault<std::string>("");
    std::string romNote;
    bool useFileRom = false;

    if (fileRom.empty())
    {
        romNote = Util::Format() << Tr("ROM 目录：文件里没有这一项，保留当前目录：") << currentRom;
    }
    else if (fileRom == currentRom)
    {
        romNote = Util::Format() << Tr("ROM 目录：与当前相同（") << currentRom << Tr("）。");
        useFileRom = true;
    }
    else if (std::filesystem::is_directory(PathFromUtf8(ResolveRomPath(fileRom))))
    {
        romNote = Util::Format() << Tr("ROM 目录：已改用文件里的目录：") << fileRom;
        useFileRom = true;
    }
    else
    {
        romNote = Util::Format() << Tr("ROM 目录：文件里是 ") << fileRom
                                 << Tr("，本机不存在，保留当前目录：") << currentRom;
    }

    config = merged;
    // The preview/list split is stored with the UI preferences, so a loaded file may carry a
    // different one. Drop the cached value and let GUI() read it from the new config.
    g_previewSplit = -1.0f;
    if (!useFileRom)
        config.Set("ROMPath", currentRom);

    /*
     * Games.xml describes the build, not the user's preferences, so the file being loaded must not
     * be able to redirect it. This matters in practice because a settings file written by an older
     * version records GameXMLFile as a path relative to the working directory ("Config/Games.xml"),
     * which named the file back when the data sat next to the executable and names nothing now that
     * it lives under Data\. Adopting it would leave the game list unloadable until the setting was
     * corrected by hand, with every ROM reported as unsupported in the meantime.
     *
     * ResolveDataFileSetting() keeps a path that actually points at a readable file and falls back
     * to the installed copy otherwise, so the value stored here is always one that works.
     */
    config.Set("GameXMLFile", ResolveDataFileSetting(merged["GameXMLFile"].ValueAsDefault<std::string>(""), "Games.xml"));

    /*
     * The interface language is read once at startup and stored in Localization's own state, so
     * changing the setting alone does nothing visible - the file would load, the setting would
     * change on disk, and the interface would stay in the language it started in. Applying it here
     * is what makes loading a file that differs only in the language do something the user can see,
     * and it is the same call the language dropdown makes for the same reason.
     */
    const int language = config["UiLanguage"].ValueAsDefault<int>(0);
    SetUiLanguage(language == 1 ? UiLanguage::Chinese : UiLanguage::English);

    g_loadReport = Util::Format() << Tr("来源：") << path << "\n"
                                  << Tr("已应用 ") << applied << Tr(" 项设置。") << "\n"
                                  << romNote;

    // Logged as well as shown, because the dialog only reports the outcome while a user is watching
    // it, and "the settings did not change" needs the before/after values to be diagnosable at all.
    InfoLog("Loaded settings from '%s': %d settings applied, ROM path %s.",
            path.c_str(), applied, useFileRom ? "taken from the file" : "kept as-is");
    InfoLog("  after load: UiLanguage=%d PostScanlines=%d PostCRT=%d InputSystem=%s ROMPath=%s",
            language,
            config["PostScanlines"].ValueAsDefault<int>(-1),
            config["PostCRT"].ValueAsDefault<int>(-1),
            config["InputSystem"].ValueAsDefault<std::string>("<none>").c_str(),
            config["ROMPath"].ValueAsDefault<std::string>("<none>").c_str());

    return true;
}

static float GetDPIScale(OSD::Platform::Window* window);
static void ApplyGuiWindowMinimumSize(OSD::Platform::Window* window, float previousScale = 0.0f);
static void RescaleUi(float scale);

static void GUI(const ImGuiIO& io, Util::Config::Node& config, const std::map<std::string, Game>& games, int& selectedGameIndex, bool& exit, bool& saveSettings, OSD::Platform::Window* window, std::shared_ptr<CInputs>& inputs, KeyBindState& kb)
{
#if !defined(SUPERMODEL_PURE_VULKAN)
    // Behind the ambient backdrop, so this is only ever seen for the one frame a resize can expose
    // it on. Taken from the theme anyway so that frame does not flash the wrong colour.
    const ImVec4 washTint = UiFrost::AmbientColours().tint;
    const ImVec4 clear_color = ImVec4(washTint.x, washTint.y, washTint.z, 1.0f);
#endif
    g_gameDefinitions = &games;

    /*
     * The pre-rework interface is a real theme, and it is the only one without the ambient backdrop,
     * the glass plates and the extra pane inset - those are what the rework added. Every other
     * difference (spacing, radii, borders, colours) comes from ApplyUiTheme().
     */
    const bool classicUi = UiThemeIsClassic();

    /*
     * Theme changes take effect here rather than at the next start.
     *
     * ApplyUiTheme() resets every size to its unscaled value before rebuilding the palette, which is
     * exactly the pair RescaleUi() performs - so a change mid-session neither loses the display's
     * DPI scaling nor compounds it. The overlay owns a second ImGui context and picks the new theme
     * up when it is created for the next session.
     */
    {
        static UiTheme appliedTheme = GetUiTheme();
        if (appliedTheme != GetUiTheme())
        {
            appliedTheme = GetUiTheme();
            RescaleUi((std::max)(1.0f, GetDPIScale(window)));
            InfoLog("GUI: interface theme set to %s.", UiThemeIsDark() ? "warm dark" : "warm paper");
        }
    }

    /*
     * The resize floor belongs to a display, not to the process. Windows gives every monitor its
     * own content scale, and SDL stores the minimum in window coordinates, so moving the window to
     * another monitor has to recompute it - see ApplyGuiWindowMinimumSize(). The work area also
     * changes when the taskbar moves or the resolution changes, so it is part of the comparison.
     */
    {
        static OSD::Platform::DisplayId lastDisplay = OSD::Platform::kPrimaryDisplay;
        static float lastScale = 0.0f;
        static int lastUsableWidth = 0;
        static int lastUsableHeight = 0;

        const OSD::Platform::DisplayId display = OSD::Platform::GetWindowDisplay(window);
        const float scale = GetDPIScale(window);
        OSD::Platform::DisplayInfo info{};
        const bool haveInfo = OSD::Platform::GetDisplayInfoById(display, &info);
        const int usableWidth = haveInfo ? info.usableBounds.width : 0;
        const int usableHeight = haveInfo ? info.usableBounds.height : 0;
        if (display != lastDisplay || scale != lastScale ||
            usableWidth != lastUsableWidth || usableHeight != lastUsableHeight)
        {
            const float previousScale = lastScale;
            // The interface scale is per monitor too. Rebuild the style and the font size before
            // NewFrame(): the text is dynamically baked at the requested size in ImGui 1.92, so
            // this does not touch the font atlas file or the glyph sources.
            if (previousScale > 0.0f && scale != previousScale)
                RescaleUi(scale);
            ApplyGuiWindowMinimumSize(window, previousScale);
            lastDisplay = display;
            lastScale = scale;
            lastUsableWidth = usableWidth;
            lastUsableHeight = usableHeight;
        }
    }

    const std::string romPath = config["ROMPath"].ValueAs<std::string>();
    RefreshRoms(romPath, games);
    if (selectedGameIndex >= (int)g_roms.size())
        selectedGameIndex = -1;

    /*
     * Restore the list's position on the first frame this window is drawn.
     *
     * The selection is restored by set name rather than index, and only when nothing has been
     * selected yet: returning from a game leaves the previous selection in place, which is what
     * the user was looking at when they left. The filter and scroll are restored together with
     * it, so the row that was highlighted is where the list opens.
     */
    if (!g_gameListStateRestored)
    {
        g_gameListStateRestored = true;

        const std::string savedFilter = config["GuiGameListFilter"].ValueAsDefault<std::string>("");
        std::snprintf(g_gameListFilter, sizeof(g_gameListFilter), "%s", savedFilter.c_str());

        const std::string savedSet = config["GuiLastSelectedGame"].ValueAsDefault<std::string>("");
        if (!savedSet.empty())
        {
            // Keep the saved name even if the set is not in the list right now (the ROM folder may
            // be unavailable), so an exit without a selection does not erase it.
            g_lastSelectedSet = savedSet;

            if (selectedGameIndex < 0)
            {
                for (int i = 0; i < (int)g_roms.size(); i++)
                {
                    if (g_roms[i].setName == savedSet)
                    {
                        selectedGameIndex = i;
                        break;
                    }
                }
            }
        }

        const float savedScroll = config["GuiGameListScrollY"].ValueAsDefault<float>(0.0f);
        if (savedScroll > 0.5f)
        {
            g_gameListScrollRestoreY = savedScroll;
            g_gameListScrollRestoreFrames = 0;
            g_gameListScrollRestorePending = true;
        }

        InfoLog("GUI: game list restored (set '%s', scroll %.1f px, filter '%s').",
            selectedGameIndex >= 0 ? g_roms[selectedGameIndex].setName.c_str() : "<none>",
            savedScroll, g_gameListFilter);
    }

#if defined(SUPERMODEL_PURE_VULKAN)
    /*
     * Developer hook, alongside SM3D_OVERLAY_OPEN in the game loop: opens the save manager for a
     * named set on the first frame.  Useful for checking the card layout and thumbnails without
     * driving the mouse through the game list.
     */
    if (!g_manageSavesOpen)
    {
        static bool checkedEnvironment = false;
        if (!checkedEnvironment)
        {
            checkedEnvironment = true;
            if (const char* setName = std::getenv("SM3D_SAVE_MANAGER"); setName != nullptr && *setName != '\0')
                OpenManageSaves(setName);
        }
    }
    /*
     * Developer hook, the counterpart of SM3D_SAVE_MANAGER below: starts a named set on the first
     * frame.  Scripted tests need to reach "the game is running" without driving the mouse through
     * a list whose row positions depend on the window size, and this is that hook.
     */
    {
        static bool checkedAutostart = false;
        if (!checkedAutostart)
        {
            checkedAutostart = true;
            if (const char* setName = std::getenv("SM3D_AUTOSTART"); setName != nullptr && *setName != '\0')
            {
                for (int i = 0; i < (int)g_roms.size(); ++i)
                {
                    if (g_roms[i].setName == setName)
                    {
                        selectedGameIndex = i;
                        g_returnToManageSaves = false;
                        saveSettings = true;
                        exit = true;
                        break;
                    }
                }
            }
        }
    }

    /*
     * Developer hook: start a set from a save without clicking Manage Saves -> Load.  Combined with
     * SM3D_SAVE_MANAGER (which opens the manager first and therefore loads the card thumbnails into
     * the settings window's Vulkan context), this reproduces the settings-window -> game transition
     * exactly.  The path is left as an absolute path; GuiTakePendingVkState() consumes it.
     */
    {
        static bool checkedLoadSave = false;
        if (!checkedLoadSave)
        {
            checkedLoadSave = true;
            if (const char* path = std::getenv("SM3D_LOAD_SAVE"); path != nullptr && *path != '\0')
            {
                const std::filesystem::path savePath(path);
                std::string set = savePath.filename().string();
                const auto dot = set.rfind('.');
                if (dot != std::string::npos)
                    set.resize(dot);

                const int row = RomIndexForSet(set);
                if (row >= 0)
                {
                    selectedGameIndex = row;
                    g_autoLoadSave = savePath.string();
                    if (!g_manageSavesOpen)
                        OpenManageSaves(set);
                    InfoLog("GUI: SM3D_LOAD_SAVE queued set '%s' from '%s'.", set.c_str(), path);
                }
                else
                {
                    ErrorLog("GUI: SM3D_LOAD_SAVE cannot find set '%s' for '%s'.", set.c_str(), path);
                }
            }
        }
    }
#endif

#if defined(SUPERMODEL_PURE_VULKAN)
    // A minimised window or a swapchain recreation skips this frame entirely:
    // there is no acquired image to draw into, and calling ImGui::NewFrame()
    // without a matching Render() would leave ImGui mid-frame.
    if (g_uiVulkan == nullptr)
        return;
    if (!g_uiVulkan->BeginFrame())
    {
        g_guiFrameSkipped = true;
        return;
    }
    ImGui_ImplVulkan_NewFrame();
#else
    ImGui_ImplOpenGL3_NewFrame();
#endif
    OSD::Platform::ImGuiPlatformNewFrame(window);
    ImGui::NewFrame();

    /*
     * Ambient backdrop, OpenGL path.
     *
     * The Vulkan build paints this with its own pass (UiVulkanBackend::DrawAmbientPass) because the
     * frosted panels have to be blurred from it, which cannot be done if it is part of the same
     * draw data they are in. There is no blur here, so the same picture is simply drawn into the
     * background draw list and the panels fall back to a translucent tint.
     */
#if !defined(SUPERMODEL_PURE_VULKAN)
    if (!classicUi)
    {
        const UiFrost::AmbientWash wash = UiFrost::AmbientColours();
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImDrawList* background = ImGui::GetBackgroundDrawList();
        const ImVec2 origin(0.0f, 0.0f);
        background->AddRectFilled(origin, display,
            ImGui::GetColorU32(ImVec4(wash.tint.x, wash.tint.y, wash.tint.z, 1.0f)));

        if (selectedGameIndex >= 0 && selectedGameIndex < (int)g_roms.size())
        {
            const BoxartTexture art = LoadBoxart(EffectiveSet(g_roms[selectedGameIndex]));
            if (art.texture != 0 && art.width > 0 && art.height > 0 && display.x > 0.0f && display.y > 0.0f)
            {
                // The same cover fit the Vulkan pass uses, so the two backends crop alike.
                const float imageAspect = (float)art.width / (float)art.height;
                const float screenAspect = display.x / display.y;
                ImVec2 uv0(0.0f, 0.0f);
                ImVec2 uv1(1.0f, 1.0f);
                if (screenAspect > imageAspect)
                {
                    const float scale = imageAspect / screenAspect;
                    uv0.y = (1.0f - scale) * 0.5f;
                    uv1.y = 1.0f - uv0.y;
                }
                else
                {
                    const float scale = screenAspect / imageAspect;
                    uv0.x = (1.0f - scale) * 0.5f;
                    uv1.x = 1.0f - uv0.x;
                }
                background->AddImage(art.texture, origin, display, uv0, uv1);
                // The wash, mixed over the cover the way ui_ambient.frag mixes it.
                background->AddRectFilled(origin, display, ImGui::GetColorU32(wash.tint));
            }
        }
    }
#endif

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    /*
     * Transparent, unless the classic theme is selected.
     *
     * Everything visible between the glass panels is the ambient backdrop behind the window, which
     * is what makes the interface read as panes floating over a picture instead of a full-screen
     * form. The panels themselves paint their own background - see the three plates below. The
     * classic theme is that full-screen form, so it keeps an opaque window background.
     */
    if (!classicUi)
        ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("Supermodel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // Open a popup requested by the previous frame's action. Done here, once the main window is the
    // current window, so the popup is parented correctly and its BeginPopupModal call later in this
    // frame finds it in the stack.
    if (g_pendingPopup != nullptr)
    {
        ImGui::OpenPopup(g_pendingPopup);
        g_pendingPopup = nullptr;
    }

/*
 * Layout.
 *
 * The two columns are drawn as two separate stacks rather than a full-width header row followed by
 * a pair of panels. The ROM path belongs to the game list - it is where that list comes from - so
 * it sits at the top of the left column, which lets the settings panel on the right use the full
 * height. Previously both panels started below a full-width ROM row, which cost the settings tabs a
 * whole row for a path field that rarely needs more than a third of the window.
 *
 * The left column is itself split vertically: the ROM row, then a preview of the hovered/selected
 * game (cover art and an English description), then the scrolling list. The list is drawn before
 * the preview even though it appears below it, so the preview can use this frame's hover result
 * instead of showing the previous frame's.
 *
 * Y coordinates are reasoned about explicitly because the two columns start at different offsets and
 * ImGui's cursor advances in one direction: the right column is positioned back up at the top after
 * the left one has been laid out. Everything is measured from topY, which is where the content area
 * begins, so nothing below depends on absolute window metrics.
 */
    // Footer height is derived from the style instead of being a magic number. It has to hold two
    // rows: the button row and, below it, the transient "saved" confirmation. The confirmation line
    // is reserved whether or not it is showing, so the panels never change height and the buttons
    // never move when it appears - the previous fixed 44 px was also short enough that the
    // confirmation was drawn past the bottom of the content area and came out cut in half.
    // Interface scale, from the font size LoadUiFont() selected for the display DPI. Everything
    // below sizes itself from this rather than from raw pixels, so the same layout works at 100%
    // and at 200% Windows scaling.
    const float uiScale = UiScale();

    const float buttonHeight = ImGui::GetFrameHeight();
    const float statusTextHeight = ImGui::GetTextLineHeight();
    // The status line is given a little more height than its text so the ROM count is not jammed
    // against the pane's bottom edge; the buttons and the badge are measured separately above it.
    const float statusHeight = (std::max)(statusTextHeight, 30.0f * uiScale);
    const float gap = ImGui::GetStyle().ItemSpacing.x;

    const float contentWidth = ImGui::GetContentRegionAvail().x;

    /*
     * Compact mode.
     *
     * Below roughly 1100 logical pixels the two columns no longer leave the settings panel enough
     * width for its controls: the tab bar truncates labels and the right-hand column is clipped.
     * In that case the game list is narrowed to a smaller share of the width, the footer uses two
     * rows and the tab bar scrolls, which hands the rest of the width to the settings.
     *
     * The preview is not part of that trade. It is the only place the cover art and the description
     * appear, so hiding it removes information instead of adapting the layout - at the minimum size
     * the left column was left with an empty band above the list. It stays visible at every window
     * size, and the divider lets the user shrink it when the height is tight.
     *
     * The threshold and every size below it are in logical pixels, so a 150% display switches at
     * 1650 physical pixels and everything stays proportional.
     */
    const bool compact = contentWidth < 1100.0f * uiScale;

    // Compact mode also needs a second footer row: five buttons do not fit across a narrow window
    // once the labels are measured, and clipping "Restore Defaults" is worse than using two rows.
    //
    // The footer sheet also has to be tall enough to hold its own inner margin, or the buttons and
    // the status line sit on the rounded edge of the pane - see panePad below. The classic theme has
    // no sheet, so it keeps the original height.
    const float footerPadding = classicUi ? 0.0f : 28.0f * uiScale;
    const float footer = compact
        ? buttonHeight * 2.0f + statusHeight + gap * 3.0f + footerPadding
        : buttonHeight + statusHeight + gap * 2.0f + footerPadding;

    const float topY = ImGui::GetCursorPosY();
    const float topX = ImGui::GetCursorPosX();
    const float available = ImGui::GetContentRegionAvail().y;
    const float columnWidth = compact
        ? (std::max)(360.0f * uiScale, contentWidth * 0.40f)
        : contentWidth * 0.52f - gap * 0.5f;
    const float panelHeight = available - footer;

    /*
     * The three sheets of glass.
     *
     * Left column, settings column, footer. Drawn here - into the root window's draw list, before
     * any child exists - which is what puts them underneath: a child window's draw data is appended
     * to its parent's list when the child is closed, so anything already in that list stays below it.
     *
     * A gap is left between the sheets and the window edge on every side, and the panels are what the
     * ambient backdrop shows through: that gap is the whole visual difference between "a form" and
     * "panes floating over a picture".
     */
#if defined(SUPERMODEL_PURE_VULKAN)
    const ImTextureID frost = g_uiVulkan != nullptr ? g_uiVulkan->FrostTexture() : 0;
#else
    const ImTextureID frost = 0;
#endif
    const float panelRounding = ImGui::GetStyle().WindowRounding;
    if (!classicUi)
    {
        ImDrawList* root = ImGui::GetWindowDrawList();
        const ImVec2 windowOrigin = ImGui::GetWindowPos();
        const ImVec2 columnOrigin(windowOrigin.x + topX, windowOrigin.y + topY);
        const float rightOriginX = columnOrigin.x + columnWidth + gap;
        const float rightWidth = (std::max)(1.0f, contentWidth - columnWidth - gap);

        UiFrost::Plate(root, frost, columnOrigin,
            ImVec2(columnOrigin.x + columnWidth, columnOrigin.y + panelHeight),
            panelRounding, UiFrost::Surface::Glass);
        UiFrost::Plate(root, frost, ImVec2(rightOriginX, columnOrigin.y),
            ImVec2(rightOriginX + rightWidth, columnOrigin.y + panelHeight),
            panelRounding, UiFrost::Surface::Glass);

        // The footer's own sheet. Its top edge is half a gap below the columns so the three read as
        // separate panes; its bottom edge is the content area's, which is where the status line ends.
        const float footerTop = columnOrigin.y + available - footer + gap * 0.5f;
        UiFrost::Plate(root, frost, ImVec2(columnOrigin.x, footerTop),
            ImVec2(columnOrigin.x + contentWidth, columnOrigin.y + available),
            panelRounding, UiFrost::Surface::Glass);
    }

    static char romBuffer[1024] = {};
    static std::string romShadow;
    if (romPath != romShadow)
    {
        romShadow = romPath;
        std::snprintf(romBuffer, sizeof(romBuffer), "%s", romPath.c_str());
    }

    /*
     * Inner margin inside each pane.
     *
     * The plates are drawn on the content rectangle, so without this everything inside them - the ROM
     * field, the table, the tab bar, the footer buttons - would start exactly on the rounded border.
     * A pane whose contents touch its own edge stops reading as a sheet of glass with something on it
     * and starts reading as a drawn box around a form, in both themes.
     *
     * The classic theme has no plates: its content sits directly on the window background, which
     * already provides the margin through WindowPadding.
     */
    const float panePad = classicUi ? 0.0f : 16.0f * uiScale;
    const float paneX = topX + panePad;
    const float paneY = topY + panePad;
    const float paneWidth = (std::max)(1.0f, columnWidth - panePad * 2.0f);
    const float paneHeight = (std::max)(1.0f, panelHeight - panePad * 2.0f);
    const float rightPaneX = topX + columnWidth + gap + panePad;
    const float rightPaneWidth = (std::max)(1.0f, contentWidth - columnWidth - gap - panePad * 2.0f);

    // ROM row. Kept in a group so its height can be measured and the list placed below it.
    ImGui::SetCursorPos(ImVec2(paneX, paneY));
    ImGui::BeginGroup();
    const float browseWidth = ImGui::CalcTextSize(Tr("浏览")).x + ImGui::GetStyle().FramePadding.x * 4.0f;
    ImGui::SetNextItemWidth(paneWidth - browseWidth - gap);
    ImGui::InputText("##ROMPath", romBuffer, sizeof(romBuffer));
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        romShadow = romBuffer;
        config.Set("ROMPath", std::string(romBuffer));
    }
    ImGui::SameLine();
    if (UiButton("浏览", ImVec2(browseWidth, 0)))
    {
        // Seed the dialog with the directory the setting resolves to, so a relative value still
        // opens the picker in the right place.
        std::string picked = ResolveRomPath(romPath);
        if (BrowseFolder(window, picked))
        {
            romShadow = picked;
            std::snprintf(romBuffer, sizeof(romBuffer), "%s", picked.c_str());
            config.Set("ROMPath", picked);
        }
    }
    ImGui::EndGroup();
    const float romRowHeight = ImGui::GetItemRectSize().y;

    // Split the left column into a cover/description panel and the scrolling list below it.
    // The preview reads the row under the pointer, so hovering browses the list; the selection is
    // the fallback, which keeps a clicked game on screen after the pointer leaves it.
    //
    // The split is stored as a ratio rather than a pixel height: dragging the divider sets how
    // much of the column the preview takes, and resizing the window then scales both parts. The
    // preview is capped at half the column, so the list always keeps half of it: a cover and a
    // paragraph stop being useful well before that, while the list is the part being used.
    // The bounds follow the interface scale the same way the style does (LoadUiFont() uses 18 px
    // as the unscaled size).
    if (g_previewSplit < 0.0f)
    {
        g_previewSplit = (std::max)(0.10f, (std::min)(0.50f,
            config["GuiPreviewSplit"].ValueAsDefault<float>(0.42f)));
    }
    const float splitHeight = (std::max)(1.0f, paneHeight - romRowHeight - gap * 2.0f);
    const float minPreviewHeight = (std::min)(150.0f * uiScale, splitHeight * 0.45f);
    // Half the column, not a pixel minimum: a fixed floor let the preview grow past the point where
    // the list could still show three rows, which is what the divider is being dragged for.
    const float minListHeight = splitHeight * 0.5f;
    float previewHeight = g_previewSplit * splitHeight;
    previewHeight = (std::max)(minPreviewHeight, (std::min)(splitHeight - minListHeight, previewHeight));
    g_previewSplit = previewHeight / splitHeight;

    const float previewY = paneY + romRowHeight + gap;
    const float listY = previewY + previewHeight + gap;
    const float listHeight = splitHeight - previewHeight;

    int hoveredRow = -1;
    // The list is drawn before the preview even though it sits below it, because the preview shows
    // whatever this frame's hit test found. Drawing it first avoids a one-frame lag on hover.
    ImGui::SetCursorPos(ImVec2(paneX, listY));
    ImGui::BeginChild("GameList", ImVec2(paneWidth, listHeight),
        classicUi ? ImGuiChildFlags_Borders : ImGuiChildFlags_AlwaysUseWindowPadding,
        classicUi ? 0 : ImGuiWindowFlags_NoBackground);
    if (!classicUi)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        UiFrost::Plate(draw, 0, origin, ImVec2(origin.x + size.x, origin.y + size.y),
            ImGui::GetStyle().ChildRounding, UiFrost::Surface::Well);
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", Tr("搜索游戏"), g_gameListFilter, sizeof(g_gameListFilter));
    const std::string needle = ToLowerAscii(g_gameListFilter);

    if (romPath.empty())
        UiText("请选择 ROM 目录。");
    else if (!std::filesystem::is_directory(PathFromUtf8(ResolveRomPath(romPath))))
        UiText("这个目录不存在。");
    else if (g_roms.empty())
        UiText("目录里没有 .zip。");
    else
    {
        // The whole remaining height goes to the table: the ROM count that used to reserve a line
        // below it now lives in the footer status line.
        const float tableHeight = (std::max)(80.0f, ImGui::GetContentRegionAvail().y);

        /*
         * Measure the ROM column instead of guessing a width. The set names are all lower-case
         * ASCII and about ten characters, so the column only needs the widest of them plus the
         * cell padding; everything left over goes to the game name. Measuring also keeps the
         * column exact at 100%, 150% and 200% scaling, where a fixed pixel value would either
         * waste space or clip the longest name.
         */
        float romColumnWidth = ImGui::CalcTextSize(Tr("ROM")).x;
        for (const RomEntry& entry : g_roms)
            romColumnWidth = (std::max)(romColumnWidth, ImGui::CalcTextSize(entry.setName.c_str()).x);
        romColumnWidth += ImGui::GetStyle().CellPadding.x * 2.0f + 6.0f;

        if (ImGui::BeginTable("Games", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0, tableHeight)))
        {
            // The name column carries the version too, so it needs the lion's share of the width.
            UiColumn("游戏", 0, 3.0f);
            UiColumn("ROM", ImGuiTableColumnFlags_WidthFixed, romColumnWidth);
            UiColumn("年份", ImGuiTableColumnFlags_WidthFixed, 56.0f * uiScale);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (int row = 0; row < (int)g_roms.size(); row++)
            {
                const RomEntry& entry = g_roms[row];
                // Translated for display. The English original stays in entry.title and is still
                // searched, so looking for "dirt" keeps working even with Chinese names showing.
                const std::string title = TrGameTitle(entry.title.c_str());
                const std::string version = TrGameVersion(entry.version);
                // The version is what separates the variants: without it most of these read as the
                // same game several times over. Searching it as well means "japan" or "revision a"
                // narrows the list, which is usually how someone knows the set they want.
                if (!needle.empty() &&
                    ToLowerAscii(title).find(needle) == std::string::npos &&
                    ToLowerAscii(version).find(needle) == std::string::npos &&
                    ToLowerAscii(entry.title).find(needle) == std::string::npos &&
                    ToLowerAscii(entry.version).find(needle) == std::string::npos &&
                    ToLowerAscii(entry.setName).find(needle) == std::string::npos)
                    continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                // A set with no definition in Games.xml gets called out rather than being shown as an
                // ordinary entry, because it cannot actually be started. Left as the raw set name:
                // translating it would make it look like the working version of the same game.
                const std::string display = !entry.supported
                    ? entry.setName + Tr("  （未收录）")
                    : (version.empty() ? title : title + " (" + version + ")");
                const std::string rowLabel = display + "##" + entry.setName;
                // A set that cannot be started is dimmed with the theme's own secondary text colour,
                // not a literal grey: a literal tuned for one palette is unreadable in the other,
                // and this row is exactly where someone looks for the reason a game will not launch.
                const bool dimRow = !entry.supported;
                if (dimRow)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                if (ImGui::Selectable(rowLabel.c_str(), selectedGameIndex == row, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                    selectedGameIndex = row;
                // Unsupported sets look like rows but cannot be started, so only supported ones
                // advertise themselves with the hand pointer.
                if (!dimRow)
                    HandOver();
#if defined(SUPERMODEL_PURE_VULKAN)
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem(Tr("管理存档...")))
                    {
                        selectedGameIndex = row;
                        OpenManageSaves(entry.setName);
                    }
                    HandOver();
                    ImGui::EndPopup();
                }
#endif
                if (dimRow)
                    ImGui::PopStyleColor();
                // The row can be clipped in a narrow window, so hovering shows the whole line. The
                // supported case stays on one line on purpose - the point is to reveal the clipped row
                // as a row. The unsupported case is a sentence, so it wraps like every other
                // explanation rather than growing a popup as wide as the sentence.
                const bool rowHovered = ImGui::IsItemHovered();
                if (rowHovered)
                {
                    hoveredRow = row;
                    if (entry.supported)
                        ImGui::SetTooltip("%s", display.c_str());
                    else
                        DrawWrappedTooltip(Tr("没能从这个包里认出 Model 3 游戏（可能缺文件）。\n同系列的其他版本可能已经在列表里了。"));
                }
                if (rowHovered && ImGui::IsMouseDoubleClicked(0))
                {
                    selectedGameIndex = row;
                    g_returnToManageSaves = false;
                    saveSettings = true;
                    exit = true;
                }
                ImGui::TableSetColumnIndex(1);
                UiText(entry.setName.c_str());
                ImGui::TableSetColumnIndex(2);
                if (entry.year > 0)
                    ImGui::Text("%d", entry.year);
            }

            /*
             * Restore the scroll position now that the rows above have told the table how tall it
             * is. On the first frame after the window opens the table's content extent is still
             * unknown at BeginTable(), so a SetScrollY() there would clamp to zero; applying it
             * after the rows and keeping the pending flag until the position sticks is what makes
             * the list open exactly where it was left. Nothing is recorded until the restore is
             * finished, or the zero from the first frame would overwrite the saved value.
             */
            if (g_gameListScrollRestorePending)
            {
                ImGui::SetScrollY(g_gameListScrollRestoreY);
                g_gameListScrollRestoreFrames++;
                const float applied = ImGui::GetScrollY();
                if ((g_gameListScrollRestoreFrames >= 2 &&
                        std::fabs(applied - g_gameListScrollRestoreY) <= 1.0f) ||
                    g_gameListScrollRestoreFrames >= 4)
                    g_gameListScrollRestorePending = false;
            }
            if (!g_gameListScrollRestorePending)
                g_gameListScrollY = ImGui::GetScrollY();

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    if (selectedGameIndex >= 0 && selectedGameIndex < (int)g_roms.size())
        g_lastSelectedSet = g_roms[selectedGameIndex].setName;

    const int previewIndex = hoveredRow >= 0 ? hoveredRow : selectedGameIndex;
    const RomEntry* previewEntry =
        (previewIndex >= 0 && previewIndex < (int)g_roms.size()) ? &g_roms[previewIndex] : nullptr;
    ImGui::SetCursorPos(ImVec2(paneX, previewY));
    DrawGamePreview(previewEntry, paneWidth, previewHeight);

#if defined(SUPERMODEL_PURE_VULKAN)
    /*
     * The ambient backdrop follows the selection, not the hover.
     *
     * Hovering would make the wallpaper change every time the pointer crosses a row, and each change
     * re-records the whole blur chain - a flickering background for a list that is being browsed.
     * Clicking is deliberate, rare, and worth one blur pass. Set here rather than at the top of the
     * frame so that the backdrop this frame is drawn against already belongs to the new selection.
     */
    if (g_uiVulkan != nullptr)
    {
        const RomEntry* ambient = (!classicUi && selectedGameIndex >= 0 && selectedGameIndex < (int)g_roms.size())
            ? &g_roms[selectedGameIndex] : nullptr;
        g_uiVulkan->SetAmbientTexture(
            ambient != nullptr ? LoadBoxart(EffectiveSet(*ambient)).texture : 0);
    }
#endif

    /*
     * Divider between the preview and the list.
     *
     * It fills the gap the two children already leave between them, so it adds no rows to the
     * layout. InvisibleButton takes mouse capture while held, which is what lets the drag continue
     * after the pointer leaves the thin strip. The jump is applied to the ratio for the next frame;
     * the children themselves use the height computed above, so the resize is one frame behind the
     * pointer at worst.
     */
    const float handleHeight = (std::max)(gap, 10.0f * uiScale);
    ImGui::SetCursorPos(ImVec2(paneX, previewY + previewHeight + (gap - handleHeight) * 0.5f));
    ImGui::InvisibleButton("##PreviewSplitter", ImVec2(paneWidth, handleHeight));
    const bool splitterHovered = ImGui::IsItemHovered();
    const bool splitterActive = ImGui::IsItemActive();
    if (splitterHovered || splitterActive)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (splitterActive)
    {
        const float next = previewHeight + io.MouseDelta.y;
        g_previewSplit = (std::max)(minPreviewHeight,
            (std::min)(splitHeight - minListHeight, next)) / splitHeight;
    }
    {
        // A short centred bar makes the otherwise invisible strip discoverable, and brightens
        // while the pointer is over it or the drag is in progress.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 rectMin = ImGui::GetItemRectMin();
        const ImVec2 rectMax = ImGui::GetItemRectMax();
        const float centreX = (rectMin.x + rectMax.x) * 0.5f;
        const float centreY = (rectMin.y + rectMax.y) * 0.5f;
        const float gripWidth = (std::min)(paneWidth * 0.12f, 48.0f * uiScale);
        const ImU32 colour = ImGui::GetColorU32(splitterActive ? ImGuiCol_SeparatorActive
            : (splitterHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
        draw->AddLine(ImVec2(centreX - gripWidth, centreY), ImVec2(centreX + gripWidth, centreY),
            colour, (std::max)(2.0f, 2.0f * uiScale));
    }

    // Right column: back up to the top of the content area, which is what moves the tab bar up past
    // the ROM row. The width is left as "the rest" so the two columns always meet the right edge.
    ImGui::SetCursorPos(ImVec2(rightPaneX, paneY));
    /*
     * Always reserve the vertical scrollbar.
     *
     * The scrollbar is part of this child's width, so letting it appear and disappear with the
     * selected tab moved everything at the right edge - including the tab bar's scroll arrows,
     * which then jumped sideways between tabs and made the same spot stop being the same control.
     * Being permanently present keeps the content width, the tabs and their arrows fixed across
     * tab switches. An unused scrollbar is a small price for a stable target.
     */
    /*
     * No recess behind the settings column.
     *
     * The whole column is one scrolling body, so a translucent well over its entire sheet would
     * simply darken the glass it is already sitting on - stacking a 34% tint over the sheet's 72%,
     * which leaves almost none of the blur visible. The tab bar and the rows provide the structure
     * here instead. The two left-hand children do get wells: they are sub-regions of their sheet, and
     * a recess is what separates "the cover" from "the list" inside one pane.
     */
    ImGui::BeginChild("Settings", ImVec2(rightPaneWidth, paneHeight),
        classicUi ? ImGuiChildFlags_Borders : ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_AlwaysVerticalScrollbar | (classicUi ? 0 : ImGuiWindowFlags_NoBackground));
    /*
     * Shrink the tabs to fit rather than scrolling them.
     *
     * With the scroll policy, selecting one of the last tabs scrolled the bar and left the first
     * tab half cut off at the leading edge - a blank rounded block, which reads as a rendering
     * fault. Seven tabs fit here once narrowed to TabMinWidthShrink; Mixed only falls back to
     * scrolling if the window is made narrower than that.
     */
    const ImGuiTabBarFlags tabFlags = compact
        ? ImGuiTabBarFlags_FittingPolicyScroll
        : ImGuiTabBarFlags_FittingPolicyMixed;
    if (ImGui::BeginTabBar("SettingsTabs", tabFlags))
    {
        if (UiTabItem("常用"))
        {
            DrawQuickSettings(config, inputs);
            ImGui::EndTabItem();
        }
        if (UiTabItem("核心"))
        {
            UpdateTempValues(config, "Core", true);
            CreateControls(config, "Core");
            UpdateTempValues(config, "Core", false);

            // The advanced rendering options used to live on their own tab. They are part of the
            // same "how the emulator runs" decision as the core settings, and a separate page made
            // them easy to miss, so they now sit below the core controls under their own heading.
            UiSep("高级设置");
            DrawAdvancedSettingsPanel(config, false);

            /*
             * The remaining Video-group keys - window behaviour, frame limit, refresh rate, display
             * number, console, white flash - are runtime options rather than picture options. They
             * sat on the 画面 page only because that is the group they belong to in the ini, which
             * left that page a list of unrelated switches with the actual picture settings at the
             * bottom. The picture settings have hand-written controls and stay on 画面.
             */
            UiSep("显示与运行");
            UpdateTempValues(config, "Video", true);
            CreateControls(config, "Video");
            UpdateTempValues(config, "Video", false);

#if !defined(SUPERMODEL_OPENGL_ONLY)
            // Last on the page: it is the one section here that can be switched while a game is
            // running, so it belongs next to the runtime options rather than above them.
            DrawHDTextureSettings(config, false);
#endif

            ImGui::EndTabItem();
        }
        if (UiTabItem("画面"))
        {
#if !defined(SUPERMODEL_OPENGL_ONLY)
            DrawPresentationCadenceSettings(config, window);
#endif
            // Hand-written picture settings only: colour, 2D scaling, the CRT filter, the widescreen
            // background (which decides whether there are side bars at all) and the cabinet backdrop
            // that fills them.
            DrawPictureModeSettings(config);

#if !defined(SUPERMODEL_OPENGL_ONLY)
            DrawCrtFilterSettings(config);
            DrawLightGunBorderSettings(config);
#endif

            /*
             * Above the backdrop, because the two are alternatives for the same area: with this on,
             * the game's own 2D background is stretched across the bars, so the backdrop has nothing
             * left to fill.
             */
            UiSep("宽屏背景");
            {
                // Labelled with what the switch does rather than repeating the section title, and built
                // like WidgetLabel() does so the visible part follows the interface language while the
                // ##id keeps the widget's identity stable across a language change.
                const std::string wideLabel = std::string(Tr("用游戏背景填满两侧")) + "##WideBackground";
                bool wideBackground = config["WideBackground"].ValueAsDefault<bool>(false);
                if (UiCheckbox(wideLabel.c_str(), &wideBackground))
                    config.Set("WideBackground", wideBackground);
            }
            HelpMarker(
                "把游戏底部的 2D 背景层横向拉满，填掉宽屏两侧的黑边。");
            // Same warning the in-game advanced panel shows, repeated here because this page is
            // where the setting is actually changed: the blue bars look like a renderer fault
            // until it is clear that the stretched 2D layer is what fills them.
            if (!config["WideScreen"].ValueAsDefault<bool>(false) &&
                config["WideBackground"].ValueAsDefault<bool>(false))
            {
                UiTextWarn("「宽屏背景」已开启：2D 背景仍会拉伸铺满窗口，所以取消宽屏后四周不是黑边。要黑边请关掉「宽屏背景」。");
            }

#if !defined(SUPERMODEL_OPENGL_ONLY)
            DrawBezelSettings(config, window);
#endif

            ImGui::EndTabItem();
        }
        if (UiTabItem("声音"))
        {
            UpdateTempValues(config, "Sound", true);
            CreateControls(config, "Sound");
            UpdateTempValues(config, "Sound", false);
            ImGui::EndTabItem();
        }
        if (UiTabItem("按键"))
        {
            /*
             * The input system is cached across tab switches.
             *
             * Constructing one enumerates every DirectInput/SDL device and their haptic axes, which
             * is long enough to be visible as a hitch when the tab is selected. It used to be
             * released at the end of every other tab, so coming back to Controls rebuilt it each
             * time. It is still invalidated whenever the configuration makes it stale (input system
             * changed, preset applied, file loaded, defaults restored) and before a game starts,
             * where the session opens its own set of devices.
             */
            if (inputs == nullptr)
                inputs = GetInputSystem(config, window);

            if (!inputs)
            {
                UiText("这个输入系统没有初始化成功，换一个再打开这一页。");
            }
            else if (ImGui::BeginTable("KeyTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing())))
            {
                UiColumn("分组");
                UiColumn("动作");
                UiColumn("按键");
                ImGui::TableHeadersRow();
                AddKeys(config, kb, inputs->GetGameInputs(GameForSelection(games, selectedGameIndex)));
                ImGui::EndTable();
            }
            UiTextDim("双击一行来修改。选中左边的游戏后，这里只显示那款游戏用到的按键。");
            UiTextDim("手柄编号：1=A/× 2=B/○ 3=X/□ 4=Y/△ 5=LB 6=RB 7=Back/Select 8=Start");
            ImGui::EndTabItem();
        }
        if (UiTabItem("网络"))
        {
            /*
             * The hand-drawn settings first, and deliberately *before* the temp-value snapshot below.
             *
             * Before, not after: every key these controls touch lives in the "Network" group, and the
             * write-back at the end of this block restores whatever the snapshot captured. Editing a
             * Network-group key in between the snapshot and the write-back gets the edit reverted -
             * the same trap the 画面 tab documents around DrawBezelSettings. Taking the snapshot after
             * these controls have run means it captures their new values rather than undoing them.
             *
             * Everything in this block is listed in HasDedicatedUI, so without calling it explicitly
             * the generic renderer skips it and the tab shows only the debug-output keys that are not
             * - which is why these controls used to sit at the bottom of 常用 instead.
             */
            DrawNetworkSettings(config);
            UiSep("调试输出");
            UpdateTempValues(config, "Network", true);
            CreateControls(config, "Network");
            UpdateTempValues(config, "Network", false);
            ImGui::EndTabItem();
        }
        // Force feedback and sensitivity share one page. Both are short lists of sliders for the same
        // subject - how the controls feel - and together they were a dozen rows in a panel that holds
        // far more, so keeping them apart cost a tab click and two nearly empty pages.
        if (UiTabItem("力反馈 / 灵敏度"))
        {
            UpdateTempValues(config, "ForceFeedback", true);
            UpdateTempValues(config, "Sensitivity", true);

            UiSep("力反馈");
            /*
             * The master switch lives on the Common page, and without it nothing on this page does
             * anything: each drive board calls Disable() in Init() when ForceFeedback is off, so the
             * emulated board never sends a single effect. Say so here rather than leaving a page of
             * sliders that look live and do nothing.
             */
            if (!config["ForceFeedback"].ValueAsDefault<bool>(false))
                UiTextWarn("先在「常用」页勾选「力反馈」，这一页的设置才会生效。");
            CreateControls(config, "ForceFeedback");

            UiSep("灵敏度");
            CreateControls(config, "Sensitivity");

            UpdateTempValues(config, "ForceFeedback", false);
            UpdateTempValues(config, "Sensitivity", false);

            UiSep("手柄转向");
            {
                static const char* smoothingItems[] = {
                    "关闭（原始 1:1）", "轻度（保留转向手感）",
                    "中度（推荐手柄）", "强力（最平稳）"
                };
                static const char* smoothingTr[4];
                int smoothing = config["GamepadSteeringSmoothing"].ValueAsDefault<int>(0);
                if (smoothing < 0 || smoothing > 3)
                    smoothing = 0;
                if (ComboRow(Tr("摇杆转向平滑##GamepadSteeringSmoothing"), &smoothing,
                    UiItems(smoothingItems, 4, smoothingTr), 4))
                    config.Set("GamepadSteeringSmoothing", smoothing);
            }
            HelpMarker(
                "把手柄摇杆的阶跃输入压平，模拟方向盘阻尼。\n"
                "关闭：原始输入。\n"
                "轻度 / 中度 / 强力：约 35 / 50 / 75ms 平滑，依次更稳。\n"
                "只影响转向轴；真实方向盘建议关闭。");

            ImGui::Spacing();
            /*
             * There used to be a "Calibrate Sticks" button here that called
             * CInputs::CalibrateJoysticks() directly. That routine prompts on stdout and reads keys
             * through the input system, and a GUI build has no console to put the prompts in: from
             * here it printed into nothing and waited for a reply the user could not see. Say where
             * it lives instead of leaving a button that appears to do nothing.
             */
            UiTextDim("摇杆/方向盘轴校准是命令行功能：用 -config-inputs 启动后按 B。手柄的轴由 SDL 归一化，通常不需要校准。");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // Footer row. Buttons are sized from their own label rather than a fixed width: the labels are
    // laid out in Chinese, which is wider than the Latin text they replace in English, and a fixed
    // 120 px clipped the longer ones at higher DPI scales while leaving the short ones swimming in
    // padding. Everything is measured first, the widest label sets the width for all of them, and
    // the result is capped so five buttons plus their gaps still fit the window.
    {
        static const char* const footerLabels[] = { "启动游戏", "保存设置", "载入设置", "恢复默认", "退出" };
        const int footerCount = IM_ARRAYSIZE(footerLabels);

        float widest = 0.0f;
        for (int i = 0; i < footerCount; ++i)
            widest = (std::max)(widest, ImGui::CalcTextSize(Tr(footerLabels[i])).x);

        const float padding = ImGui::GetStyle().FramePadding.x * 2.0f + 24.0f;
        const float minWidth = widest + padding;

        // Compact mode puts three buttons on the first row and two on the second, so the share is
        // computed per row rather than across all five.
        const int perRow = compact ? 3 : footerCount;
        const float footerAvail = (std::max)(1.0f, contentWidth - panePad * 2.0f);
        const float evenShare = (footerAvail - gap * (perRow - 1)) / (float)perRow;

        // Text completeness comes first, so the labels set the floor. Above that the buttons grow to
        // share the row evenly, which is what the window has room for; the cap keeps them from
        // stretching into long bars on a wide display.
        float buttonWidth = minWidth;
        if (evenShare > buttonWidth)
            buttonWidth = (std::min)(evenShare, 200.0f);
        if (buttonWidth < minWidth)
            buttonWidth = minWidth;

        // Put the cursor on the button line explicitly. The panels above were sized against the
        // footer height, and letting the cursor follow the child windows would place the row
        // wherever they happened to end. The pane's own inner margin is added here so the buttons
        // and the status line sit inside the sheet rather than on its rounded edge.
        const float buttonsY = topY + available - footer + gap * 0.5f + panePad;
        const float secondRowY = buttonsY + buttonHeight + gap;
        const float statusY = topY + available - panePad - statusHeight;
        const float statusTextY = statusY + (statusHeight - statusTextHeight) * 0.5f;
        ImGui::SetCursorPos(ImVec2(paneX, buttonsY));

        const ImVec2 buttonSize(buttonWidth, 0);

        if (UiButton(footerLabels[0], buttonSize))
        {
            if (selectedGameIndex < 0)
                ImGui::OpenPopup("NoGame");
            else
            {
                // Started from the list, so Escape returns to the list - not to a manager that was
                // not part of this launch.
                g_returnToManageSaves = false;
                saveSettings = true;
                exit = true;
            }
        }
        ImGui::SameLine();

        if (UiButton(footerLabels[1], buttonSize))
            ImGui::OpenPopup("SaveSettings");
        ImGui::SameLine();
        if (UiButton(footerLabels[2], buttonSize))
            ImGui::OpenPopup("LoadSettings");
        // Right edge of the first footer row. Non-compact mode has every button here; compact mode
        // has three, and the badge shares this row rather than the shorter second one.
        const float firstRowRight = ImGui::GetItemRectMax().x;
        if (compact)
            ImGui::SetCursorPos(ImVec2(topX, secondRowY));
        else
            ImGui::SameLine();
        if (UiButton(footerLabels[3], buttonSize))
            ImGui::OpenPopup("ConfirmDefaults");
        ImGui::SameLine();
        if (UiButton(footerLabels[4], buttonSize))
            ImGui::OpenPopup("ConfirmExit");

        // The ROM count lives on the reserved line below the buttons rather than inside the list,
        // where it cost the table a row. A transient save/load note shares the same line, to its
        // right. The line is reserved whether or not the note is showing, so nothing moves.
        //
        // The Ko-fi badge goes on the button line, right-aligned in the pane, so it shares the
        // buttons' baseline instead of hanging lower on the status line. The first row's right edge
        // is the test for whether that row has room: in the compact two-row layout the badge sits
        // beside the three buttons of the first row, not beside the shorter second one. When even
        // that is too tight the badge falls back to the status line, where the text is clipped so it
        // cannot run underneath.
        const float kofiWidth = KofiBadgeWidth();
        const float kofiX = paneX + (contentWidth - panePad * 2.0f) - kofiWidth;
        const float kofiScreenX = ImGui::GetWindowPos().x + kofiX;
        const float buttonsRight = compact ? firstRowRight : ImGui::GetItemRectMax().x;
        const bool kofiOnButtonRow = kofiScreenX > buttonsRight + gap;
        const bool showKofi = kofiOnButtonRow || kofiX > paneX + 80.0f * uiScale;

        ImGui::SetCursorPos(ImVec2(paneX, statusTextY));
        const ImVec2 statusScreen = ImGui::GetCursorScreenPos();
        if (showKofi && !kofiOnButtonRow)
        {
            ImGui::PushClipRect(statusScreen,
                ImVec2(kofiScreenX - gap, statusScreen.y + statusTextHeight), true);
        }
        UiTextDim("找到 %d 个 ROM", (int)g_roms.size());
        if (ImGui::GetTime() - g_statusAt < 3.0 && !g_statusNote.empty())
        {
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
            UiTextDim("%s", g_statusNote.c_str());
        }
        if (showKofi && !kofiOnButtonRow)
            ImGui::PopClipRect();

        if (kofiOnButtonRow)
        {
            ImGui::SetCursorPos(ImVec2(kofiX, buttonsY));
            DrawKofiBadge(kofiWidth, buttonHeight);
        }
        else if (showKofi)
        {
            ImGui::SetCursorPos(ImVec2(kofiX, statusY));
            DrawKofiBadge(kofiWidth, statusHeight);
        }
    }

    if (ImGui::BeginPopupModal("NoGame", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiText("先在左边选一个游戏。");
        if (UiButton("确定", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("ConfirmDefaults", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiText("恢复默认设置？ROM 目录会保留。");
        if (UiButton("恢复", ImVec2(120, 0)))
        {
            std::string keepRom = config["ROMPath"].ValueAs<std::string>();
            config = DefaultConfig();
            config.Set("ROMPath", keepRom);
            g_previewSplit = -1.0f;     // back to the default 42/58 split
            inputs.reset();
            // Write immediately rather than waiting for "保存设置": this button exists precisely as
            // a way out of a broken configuration, so it must not be possible to lose the reset by
            // quitting without saving afterwards.
            g_writeSettings = true;
            saveSettings = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (UiButton("取消", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("SaveSettings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiText("保存到哪个文件？");
        UiTextDim("默认位置：程序目录下的 Data\\Config\\Supermodel.ini");
        if (UiButton("保存到默认位置", ImVec2(190, 0)))
        {
            g_writeSettings = true;
            saveSettings = true;
            g_statusNote = Tr("已保存");
            g_statusAt = ImGui::GetTime();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (UiButton("另存为...", ImVec2(140, 0)))
        {
            // BrowseSaveFile() writes the chosen path back through its parameter, so the same
            // variable is both the suggested starting point and the result. Seeding it with the
            // settings file currently in use puts the dialog in the right folder and offers the
            // existing name, rather than dropping the user somewhere unrelated.
            std::string target = FileSystemPath::ResolvePath(g_settingsPath);
            if (BrowseSaveFile(window, target))
            {
                g_saveAsPath = target;
                g_writeSettings = true;
                saveSettings = true;
                g_statusNote = Tr("已保存到指定文件");
                g_statusAt = ImGui::GetTime();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (UiButton("取消", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("LoadSettings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiText("从文件载入设置？");
        UiTextDim("会覆盖当前设置。ROM 目录只在文件里的目录存在时才会替换。");
        if (UiButton("选择文件", ImVec2(140, 0)))
        {
            std::string picked;
            if (BrowseSettingsFile(window, picked))
            {
                if (LoadSettingsFromFile(config, picked))
                {
                    // Input bindings are part of the settings, so the cached input system has to be
                    // rebuilt; leaving it in place would keep the previous bindings alive on the
                    // Controls tab and in game.
                    inputs.reset();
                    // Written straight away rather than waiting for "保存设置", for the same reason
                    // the reset button does: the user asked for this state to be in effect now, and
                    // it should survive quitting without a second confirmation.
                    g_writeSettings = true;
                    saveSettings = true;
                    g_statusNote = Tr("已载入设置");
                    g_statusAt = ImGui::GetTime();
                    // A dialog rather than only the status line. The status line is small, grey and
                    // easy to miss, and a load can legitimately produce no visible change - the
                    // file may differ only in settings on another tab, or only in the ROM path. That
                    // combination is what made a working load look like a dead button.
                    g_pendingPopup = "LoadDone";
                }
                else
                {
                    // Reported instead of closing silently: the two ways this can happen (file
                    // could not be opened, file had no settings in it) are both invisible
                    // otherwise, and the dialog closing on its own reads as "it worked".
                    g_pendingPopup = "LoadFailed";
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (UiButton("取消", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("LoadDone", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiText("设置已载入。");
        ImGui::Spacing();
        ImGui::TextUnformatted(g_loadReport.c_str());
        ImGui::Spacing();
        if (UiButton("确定", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("LoadFailed", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiText("这个文件里没有可用的设置。");
        UiTextDim("可能是文件无法打开，或者文件是空的。");
        if (UiButton("确定", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("ConfirmExit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiText("退出前保存设置？");

        // Fixed pixel widths were sized for the Chinese labels. In English the translated
        // text ("Save and Quit", "Don't Save") is wider and gets clipped inside the button,
        // especially at higher DPI. Size the whole row from the translated labels instead,
        // keeping the old 120 px as a floor so short labels do not make tiny buttons.
        const char* confirmLabels[] = { "保存并退出", "不保存", "取消" };
        float confirmButtonWidth = 120.0f;
        for (const char* label : confirmLabels)
            confirmButtonWidth = (std::max)(confirmButtonWidth,
                ImGui::CalcTextSize(Tr(label)).x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f);

        if (UiButton("保存并退出", ImVec2(confirmButtonWidth, 0)))
        {
            selectedGameIndex = -1;
            saveSettings = true;
            exit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (UiButton("不保存", ImVec2(confirmButtonWidth, 0)))
        {
            selectedGameIndex = -1;
            saveSettings = false;
            exit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (UiButton("取消", ImVec2(confirmButtonWidth, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

#if defined(SUPERMODEL_PURE_VULKAN)
    // Drawn inside the settings window, next to the other modals. A popup's identity is derived from
    // the ID of the window that is current when it is opened, so opening it under "Supermodel" and
    // then looking it up after ImGui::End() made the lookup miss every frame and the window never
    // appeared.
    DrawManageSavesModal(selectedGameIndex, exit, saveSettings);
#endif

    ImGui::End();

    ImGui::Render();
#if defined(SUPERMODEL_PURE_VULKAN)
    if (g_uiVulkan != nullptr)
        g_uiVulkan->Render(ImGui::GetDrawData());
    g_guiFramePresented = true;
#else
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}

static float GetDPIScale(OSD::Platform::Window* window)
{
#if defined(__APPLE__)
    /*
     * macOS windows are measured in points, and the Retina difference is reported separately as
     * the drawable/window ratio that ImGui's framebuffer scale already uses. Applying the display
     * DPI here as well scaled every control a second time (about 1.5x on a Retina panel), which
     * made the interface too large for its layout and clipped the right-hand column.
     */
    (void)window;
    return 1.0f;
#else
    if (window == nullptr)
        return 1.0f;

    const float scale = OSD::Platform::GetDisplayContentScale(OSD::Platform::GetWindowDisplay(window));
    return scale > 0.0f ? scale : 1.0f;
#endif
}

/*
 * Re-apply the settings window's minimum size for the display it currently occupies.
 *
 * 1280x720 are the layout's own minimum (the smallest two-column layout that does not clip);
 * 1280x800 is the default window size, used when the display has room for it. On Windows the
 * interface is scaled by the monitor's content scale, so the pixel floor grows with it; on macOS
 * the window is measured in points and the factor stays 1.0. The floor is a client-area size,
 * while the limit that matters is the outer window: the work area has to hold the client area
 * plus the title bar and borders.
 *
 * Both halves have to be recomputed when the window moves or the display changes. The minimum is
 * set once at creation otherwise, so a window dragged from a 150% 4K monitor to a 100% 1080p one
 * keeps the 150% floor (1920x1080 physical). That is taller than the second screen's work area,
 * and because the platform enforces it the window can no longer be made to fit. On a content
 * scale change the window is reset to the default 1280x800 in layout units, scaled for the new
 * display - a window that merely moved between two displays of the same scale keeps its size
 * and is only clamped to the new work area.
 */
static void ApplyGuiWindowMinimumSize(OSD::Platform::Window* window, float previousScale)
{
    if (!OSD::Platform::IsWindowValid(window))
        return;

    const float scale = (std::max)(1.0f, GetDPIScale(window));
    int minWidth = (int)(1280.0f * scale);
    int minHeight = (int)(720.0f * scale);

    int borderLeft = 0, borderTop = 0, borderRight = 0, borderBottom = 0;
    OSD::Platform::GetWindowBordersSize(window, &borderLeft, &borderTop, &borderRight, &borderBottom);

    OSD::Platform::DisplayInfo info{};
    const bool haveArea =
        OSD::Platform::GetDisplayInfoById(OSD::Platform::GetWindowDisplay(window), &info) &&
        info.usableBounds.width > 0 && info.usableBounds.height > 0;

    int areaWidth = 0;
    int areaHeight = 0;
    if (haveArea)
    {
        areaWidth = info.usableBounds.width - borderLeft - borderRight;
        areaHeight = info.usableBounds.height - borderTop - borderBottom;
        if (areaWidth > 0)
            minWidth = (std::min)(minWidth, areaWidth);
        if (areaHeight > 0)
            minHeight = (std::min)(minHeight, areaHeight);
    }

    OSD::Platform::SetWindowMinimumSize(window, minWidth, minHeight);

    if (haveArea && areaWidth > 0 && areaHeight > 0 && !OSD::Platform::IsWindowMaximized(window))
    {
        int width = 0, height = 0;
        OSD::Platform::GetWindowSize(window, &width, &height);

        int targetWidth = width;
        int targetHeight = height;

        /*
         * A DPI change is not a resize. The window was comfortable at its old logical size, and
         * keeping its physical size across the move would make it fill the new screen (150% to
         * 100%) or shrink to a postage stamp (100% to 200%). Reset to the default size in layout
         * units, scaled for the new display. A pure move between two displays of the same scale
         * keeps the user's size and is only clamped to the work area.
         */
        if (previousScale > 0.0f && std::fabs(previousScale - scale) > 0.01f)
        {
            targetWidth = (int)(1280.0f * scale);
            targetHeight = (int)(800.0f * scale);
        }

        targetWidth = (std::max)(minWidth, (std::min)(targetWidth, areaWidth));
        targetHeight = (std::max)(minHeight, (std::min)(targetHeight, areaHeight));
        if (targetWidth != width || targetHeight != height)
            OSD::Platform::SetWindowSize(window, targetWidth, targetHeight);
    }

    InfoLog("GUI minimum window size: %dx%d (scale %.2f, work area %dx%d, borders %d/%d/%d/%d).",
        minWidth, minHeight, scale, info.usableBounds.width, info.usableBounds.height,
        borderLeft, borderTop, borderRight, borderBottom);
}

/*
 * The palette, shared by ApplyUiTheme() and the frosted-glass drawing below.
 *
 * A struct rather than a pile of locals because two of the things the interface needs are not ImGui
 * style colours and cannot be read back out of the style: the glass tint applied over the blurred
 * backdrop, and the specular rim. Keeping them beside the rest of the palette is what stops the two
 * from drifting apart the first time one of them is retuned.
 *
 * Neither palette contains pure black or pure white. Both are tuned to be looked at for an hour next
 * to a running arcade game: #EAE6DF on #1A1C20 rather than #FFFFFF on #000000, an amber accent rather
 * than the saturated blue a stock ImGui theme ships, and borders at 6-10% white so they read as the
 * edge of a sheet of glass instead of a drawn line.
 */
namespace
{
struct UiPalette
{
	// Over the blurred backdrop. The alpha is how much of the blur survives, so this is the single
	// knob that decides whether the effect reads as frosted glass or as a slightly see-through panel.
	ImVec4 glassTint;
	ImVec4 popupTint;
	ImVec4 wellTint;
	// What ImGui fills a window with when the caller has not drawn a plate: modals, tooltips and the
	// statistics readout. Alpha is high because there is no backdrop behind those.
	ImVec4 windowSolid;
	ImVec4 rimTop;
	ImVec4 rimSide;

	ImVec4 surface;
	ImVec4 popup;
	ImVec4 field;
	ImVec4 ink;
	ImVec4 inkDim;
	ImVec4 line;
	ImVec4 lineSoft;
	ImVec4 accent;
	ImVec4 accentSoft;
	ImVec4 hover;
	ImVec4 pressed;
	ImVec4 button;
	// Button hover is its own token because the shared `hover` is only a few units away from
	// `button` in the light theme - the tabs highlight with the accent, the footer and Browse
	// buttons looked completely inert. The classic theme keeps the old value.
	ImVec4 buttonHover;

	// Ambient backdrop wash for the settings window; see UiFrost::AmbientColours().
	ImVec4 backdropTint;
	float backdropSaturation = 0.55f;
	float backdropExposure = 0.85f;
	float backdropVignette = 0.55f;
};

UiPalette g_palette;
// Light by default: the interface is a game launcher that people browse for a while, and a warm
// paper pane is easier to sit in front of than a dark one for that. The dark theme is one combo box
// away and takes effect immediately.
UiTheme g_theme = UiTheme::WarmPaper;

UiPalette MakePalette(UiTheme theme)
{
	UiPalette p;
	if (theme == UiTheme::Classic)
	{
		/*
		 * The interface as it was before the frosted-glass rework, value for value.
		 *
		 * Not a compromise between the old and new looks: this is the old one, so that somebody who
		 * preferred it gets exactly what they had - the same paper, the same blue accent, the same
		 * opaque panels. The glass tokens are filled in as opaque paper so nothing that reads them by
		 * accident can produce a translucent panel, and the rim is transparent because no plate is
		 * drawn in this theme at all.
		 */
		p.glassTint      = ImVec4(0.965f, 0.957f, 0.937f, 1.00f); // #F6F4EF
		p.popupTint      = ImVec4(0.992f, 0.988f, 0.973f, 1.00f);
		p.wellTint       = ImVec4(0.984f, 0.980f, 0.965f, 1.00f);
		p.windowSolid    = ImVec4(0.965f, 0.957f, 0.937f, 1.00f);
		p.rimTop         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		p.rimSide        = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		p.surface        = ImVec4(0.984f, 0.980f, 0.965f, 1.00f); // #FBFAF6
		p.popup          = ImVec4(0.992f, 0.988f, 0.973f, 1.00f); // #FDFCF8
		p.field          = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
		p.ink            = ImVec4(0.165f, 0.157f, 0.141f, 1.00f); // #2A2824
		p.inkDim         = ImVec4(0.490f, 0.475f, 0.447f, 1.00f); // #7D796F
		p.line           = ImVec4(0.867f, 0.847f, 0.808f, 1.00f); // #DDD8CE
		p.lineSoft       = ImVec4(0.906f, 0.890f, 0.855f, 1.00f); // #E7E3DA
		p.accent         = ImVec4(0.102f, 0.388f, 0.780f, 1.00f); // #1A63C7
		p.accentSoft     = ImVec4(0.890f, 0.929f, 0.988f, 1.00f); // #E3EDFC
		p.hover          = ImVec4(0.941f, 0.933f, 0.914f, 1.00f); // #F0EEE9
		p.pressed        = ImVec4(0.902f, 0.890f, 0.863f, 1.00f); // #E6E3DC
		p.button         = ImVec4(0.933f, 0.922f, 0.898f, 1.00f); // #EEEBE5
		p.buttonHover    = p.hover;
		// No backdrop at all in this theme; the window is an opaque sheet.
		p.backdropTint   = ImVec4(0.965f, 0.957f, 0.937f, 0.0f);
		p.backdropSaturation = 0.0f;
		p.backdropExposure = 1.0f;
		p.backdropVignette = 0.0f;
		return p;
	}

	if (theme == UiTheme::WarmDark)
	{
		// Warm dark glass. A rock-grey tint at 72% over the blur, muted amber accent, and a warm
		// off-white for text.
		p.glassTint      = ImVec4(0.102f, 0.110f, 0.125f, 0.72f); // rgba(26, 28, 32, 0.72)
		p.popupTint      = ImVec4(0.118f, 0.129f, 0.153f, 0.86f);
		p.wellTint       = ImVec4(0.055f, 0.063f, 0.078f, 0.34f);
		p.windowSolid    = ImVec4(0.094f, 0.102f, 0.122f, 0.95f);
		p.rimTop         = ImVec4(1.000f, 1.000f, 1.000f, 0.14f);
		p.rimSide        = ImVec4(1.000f, 1.000f, 1.000f, 0.06f);
		p.surface        = ImVec4(0.149f, 0.161f, 0.188f, 0.55f); // rgba(38, 41, 48, 0.55)
		p.popup          = ImVec4(0.125f, 0.137f, 0.161f, 0.98f);
		p.field          = ImVec4(0.173f, 0.188f, 0.220f, 0.55f);
		p.ink            = ImVec4(0.918f, 0.902f, 0.875f, 1.00f); // #EAE6DF
		p.inkDim         = ImVec4(0.604f, 0.588f, 0.557f, 1.00f); // #9A968E
		p.line           = ImVec4(1.000f, 1.000f, 1.000f, 0.10f);
		p.lineSoft       = ImVec4(1.000f, 1.000f, 1.000f, 0.06f);
		p.accent         = ImVec4(0.878f, 0.624f, 0.353f, 1.00f); // #E09F5A
		p.accentSoft     = ImVec4(0.878f, 0.624f, 0.353f, 0.20f);
		p.hover          = ImVec4(0.204f, 0.220f, 0.259f, 0.70f); // rgba(52, 56, 66, 0.70)
		p.pressed        = ImVec4(0.259f, 0.278f, 0.322f, 0.85f);
		p.button         = ImVec4(0.188f, 0.204f, 0.235f, 0.62f);
		p.buttonHover    = ImVec4(0.255f, 0.270f, 0.300f, 0.82f);
		p.backdropTint   = ImVec4(0.102f, 0.110f, 0.125f, 0.62f);
		p.backdropSaturation = 0.55f;
		p.backdropExposure = 0.80f;
		p.backdropVignette = 0.55f;
		return p;
	}

	/*
	 * Warm eye-care light. Alabaster rather than white.
	 *
	 * Pure white is the obvious light-theme choice and the wrong one for a list somebody reads for ten
	 * minutes at a time: it glares, and every grey rule becomes a hard line. These are the values from
	 * the design note - a warm off-white pane at 78%, linen containers, and a terracotta accent that
	 * stays legible on all of it. Nothing in the palette is #FFFFFF and nothing is #000000.
	 */
	p.glassTint      = ImVec4(0.957f, 0.941f, 0.910f, 0.78f); // rgba(244, 240, 232, 0.78)
	p.popupTint      = ImVec4(0.980f, 0.969f, 0.945f, 0.93f);
	// A recess inside a paper pane: slightly deeper than the pane itself, not darker in hue.
	p.wellTint       = ImVec4(0.886f, 0.863f, 0.820f, 0.42f);
	p.windowSolid    = ImVec4(0.957f, 0.941f, 0.910f, 0.97f);
	// Warm grey inward line plus a bright top edge: on a pale pane the outline has to come from the
	// dark side or it disappears, while the 1px highlight still reads as a reflection.
	p.rimTop         = ImVec4(1.000f, 1.000f, 1.000f, 0.65f);
	p.rimSide        = ImVec4(0.235f, 0.216f, 0.176f, 0.10f); // rgba(60, 55, 45, 0.10)
	p.surface        = ImVec4(0.925f, 0.906f, 0.867f, 0.65f); // rgba(236, 231, 221, 0.65)
	p.popup          = ImVec4(0.980f, 0.969f, 0.945f, 0.98f);
	p.field          = ImVec4(0.886f, 0.863f, 0.820f, 0.70f); // rgba(226, 220, 209, 0.70)
	p.ink            = ImVec4(0.149f, 0.145f, 0.133f, 1.00f); // #262522
	p.inkDim         = ImVec4(0.431f, 0.416f, 0.388f, 1.00f); // #6E6A63
	p.line           = ImVec4(0.235f, 0.216f, 0.176f, 0.10f);
	p.lineSoft       = ImVec4(0.235f, 0.216f, 0.176f, 0.06f);
	p.accent         = ImVec4(0.784f, 0.427f, 0.231f, 1.00f); // #C86D3B
	p.accentSoft     = ImVec4(0.784f, 0.427f, 0.231f, 0.16f);
	p.hover          = ImVec4(0.847f, 0.820f, 0.769f, 0.88f); // rgba(216, 209, 196, 0.88)
	p.pressed        = ImVec4(0.800f, 0.769f, 0.714f, 0.95f); // rgba(204, 196, 182, 0.95)
	p.button         = ImVec4(0.863f, 0.835f, 0.788f, 0.80f); // rgba(220, 213, 201, 0.80)
	// 12% of the terracotta accent: the same hue the tab hover uses, without turning the button
	// into the selected state. The old `hover` was only ~4/255 away from `button` here.
	p.buttonHover    = ImVec4(0.854f, 0.786f, 0.721f, 0.92f);
	/*
	 * The cover wash stays light: it is the pane that frosts the picture, not this.
	 *
	 * Washing the backdrop toward paper at a high weight and then putting a 78% paper pane over it
	 * leaves the cover contributing a few percent - which is exactly the flat, textureless result the
	 * frosted panes are supposed to avoid. The wash only has to make the artwork abstract; the blur
	 * and the pane tint do the rest.
	 */
	p.backdropTint   = ImVec4(0.957f, 0.941f, 0.910f, 0.30f);
	p.backdropSaturation = 0.55f;
	p.backdropExposure = 0.92f;
	p.backdropVignette = 0.20f;
	return p;
}
} // namespace

void SetUiTheme(UiTheme theme)
{
	if (theme == UiTheme::WarmDark)
		g_theme = UiTheme::WarmDark;
	else if (theme == UiTheme::Classic)
		g_theme = UiTheme::Classic;
	else
		g_theme = UiTheme::WarmPaper;
}

UiTheme GetUiTheme()
{
	return g_theme;
}

bool UiThemeIsDark()
{
	return g_theme == UiTheme::WarmDark;
}

bool UiThemeIsClassic()
{
	return g_theme == UiTheme::Classic;
}

void ApplyUiTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;
    g_palette = MakePalette(g_theme);
    const UiPalette& p = g_palette;
    const bool classic = (g_theme == UiTheme::Classic);

    c[ImGuiCol_Text]                  = p.ink;
    c[ImGuiCol_TextDisabled]          = p.inkDim;
    c[ImGuiCol_WindowBg]              = p.windowSolid;
    c[ImGuiCol_ChildBg]               = p.surface;
    c[ImGuiCol_PopupBg]               = p.popup;
    c[ImGuiCol_Border]                = p.line;
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]               = p.field;
    c[ImGuiCol_FrameBgHovered]        = p.hover;
    c[ImGuiCol_FrameBgActive]         = p.pressed;
    c[ImGuiCol_TitleBg]               = p.windowSolid;
    c[ImGuiCol_TitleBgActive]         = p.windowSolid;
    c[ImGuiCol_TitleBgCollapsed]      = p.windowSolid;
    c[ImGuiCol_MenuBarBg]             = p.windowSolid;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(p.inkDim.x, p.inkDim.y, p.inkDim.z, 0.45f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(p.inkDim.x, p.inkDim.y, p.inkDim.z, 0.70f);
    c[ImGuiCol_ScrollbarGrabActive]   = p.accent;
    c[ImGuiCol_CheckMark]             = p.accent;
    c[ImGuiCol_SliderGrab]            = p.accent;
    c[ImGuiCol_SliderGrabActive]      = ImVec4(p.accent.x * 1.10f, p.accent.y * 1.10f, p.accent.z * 1.10f, 1.00f);
    c[ImGuiCol_Button]                = p.button;
    c[ImGuiCol_ButtonHovered]         = p.buttonHover;
    c[ImGuiCol_ButtonActive]          = p.pressed;
    c[ImGuiCol_Header]                = p.accentSoft;
    c[ImGuiCol_HeaderHovered]         = ImVec4(p.accent.x, p.accent.y, p.accent.z, UiThemeIsDark() ? 0.30f : 0.22f);
    c[ImGuiCol_HeaderActive]          = ImVec4(p.accent.x, p.accent.y, p.accent.z, UiThemeIsDark() ? 0.40f : 0.30f);
    c[ImGuiCol_Separator]             = p.line;
    c[ImGuiCol_SeparatorHovered]      = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.60f);
    c[ImGuiCol_SeparatorActive]       = p.accent;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ResizeGripHovered]     = p.accentSoft;
    c[ImGuiCol_ResizeGripActive]      = p.accent;
    c[ImGuiCol_InputTextCursor]       = p.ink;
    c[ImGuiCol_TabHovered]            = c[ImGuiCol_HeaderHovered];
    c[ImGuiCol_Tab]                   = p.button;
    c[ImGuiCol_TabSelected]           = p.surface;
    c[ImGuiCol_TabSelectedOverline]   = p.accent;
    c[ImGuiCol_TabDimmed]             = ImVec4(p.button.x, p.button.y, p.button.z, p.button.w * 0.55f);
    c[ImGuiCol_TabDimmedSelected]     = p.surface;
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(p.inkDim.x, p.inkDim.y, p.inkDim.z, 0.55f);
    c[ImGuiCol_PlotLines]             = p.accent;
    c[ImGuiCol_PlotLinesHovered]      = ImVec4(p.accent.x * 1.10f, p.accent.y * 1.10f, p.accent.z * 1.10f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = p.accent;
    c[ImGuiCol_PlotHistogramHovered]  = ImVec4(p.accent.x * 1.10f, p.accent.y * 1.10f, p.accent.z * 1.10f, 1.00f);
    c[ImGuiCol_TableHeaderBg]         = ImVec4(p.ink.x, p.ink.y, p.ink.z, UiThemeIsDark() ? 0.06f : 0.05f);
    c[ImGuiCol_TableBorderStrong]     = p.line;
    c[ImGuiCol_TableBorderLight]      = p.lineSoft;
    c[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(p.ink.x, p.ink.y, p.ink.z, 0.035f);
    c[ImGuiCol_TextLink]              = p.accent;
    c[ImGuiCol_TextSelectedBg]        = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.30f);
    c[ImGuiCol_TreeLines]             = p.line;
    c[ImGuiCol_DragDropTarget]        = p.accent;
    c[ImGuiCol_DragDropTargetBg]      = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.15f);
    c[ImGuiCol_UnsavedMarker]         = p.accent;
    c[ImGuiCol_NavCursor]             = p.accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.02f, 0.02f, 0.03f, UiThemeIsDark() ? 0.25f : 0.18f);
    // Warm brown rather than black behind a modal: a black scrim over warm paper reads as a hole.
    // The classic theme keeps the near-black it always had, because that is what it looked like.
    c[ImGuiCol_ModalWindowDimBg]      = UiThemeIsDark()
        ? ImVec4(0.02f, 0.02f, 0.03f, 0.45f)
        : (classic ? ImVec4(0.100f, 0.090f, 0.080f, 0.28f) : ImVec4(0.235f, 0.216f, 0.176f, 0.22f));

    // Airy but not loose: more spacing than ImGui's defaults, which are tuned for dense tool UIs.
    // The vertical figures are what give a form breathing room without making it scroll, and the
    // padding is generous because every panel here is a rounded sheet with content on it - anything
    // closer to the border reads as a box drawn around a form.
    //
    // The classic theme keeps the tighter figures it was designed with: it has no glass panes, so
    // its content sits directly on the window's own background and does not need the extra ring.
    style.WindowPadding = classic ? ImVec2(20.0f, 18.0f) : ImVec2(18.0f, 16.0f);
    style.FramePadding = classic ? ImVec2(12.0f, 7.0f) : ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(10.0f, 8.0f);
    style.ItemSpacing = classic ? ImVec2(12.0f, 9.0f) : ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 12.0f;

    style.WindowBorderSize = 0.0f;
    /*
     * No borders on children or popups in the glass themes: the specular rim drawn by
     * UiFrost::Plate() is the edge, and an ImGui border on top of it would be a second, hard line 1px
     * outside it. The classic theme has no rim, so it keeps the 1px child border it was built with -
     * that border is what separates the list and the settings panes from the window.
     *
     * Controls do get a hairline in the glass themes. Without one a combo, an input field and a small
     * button are told apart from the pane behind them by fill colour alone, and on the light theme
     * the two fills are within a few percent of each other - the save-state table's Save/Load buttons
     * all but vanish. The line is the theme's border token: 10% warm grey on paper, 10% white on glass.
     */
    style.ChildBorderSize = classic ? 1.0f : 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = classic ? 0.0f : 1.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 0.0f;
    style.TabBarOverlineSize = 2.0f;

    /*
     * Corner radii, following the proportions Apple's own panels use: the larger the surface, the
     * larger the radius, so a window's corner is visibly rounder than a button's rather than the
     * same arc scaled up. These are unscaled; RescaleUi() multiplies them by the display scale.
     */
    style.WindowRounding = 14.0f;
    style.ChildRounding = classic ? 12.0f : 10.0f;
    style.FrameRounding = classic ? 8.0f : 7.0f;
    style.GrabRounding = classic ? 8.0f : 6.0f;
    style.PopupRounding = classic ? 12.0f : 10.0f;
    style.ScrollbarRounding = classic ? 8.0f : 6.0f;
    style.TabRounding = classic ? 9.0f : 8.0f;

    /*
     * Antialiasing and curve smoothness.
     *
     * The default tessellation budget (0.30) is chosen for tool UIs where a corner is eight pixels
     * across; at a 14px radius it leaves visible facets, which is exactly the cheap look the rounded
     * corners are supposed to avoid. 0.10 costs a few more vertices per arc and no measurable time.
     *
     * The three AntiAliased flags are ImGui's defaults, set explicitly because the theme is also
     * applied to the in-game overlay's context and a future default change should not silently make
     * the two surfaces disagree.
     */
    style.CircleTessellationMaxError = 0.10f;
    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill = true;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = classic ? ImVec2(0.0f, 8.0f) : ImVec2(0.0f, 9.0f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
    style.DisabledAlpha = 0.45f;
}

UiFrost::AmbientWash UiFrost::AmbientColours()
{
	AmbientWash wash;
	wash.tint = g_palette.backdropTint;
	wash.saturation = g_palette.backdropSaturation;
	wash.exposure = g_palette.backdropExposure;
	wash.vignette = g_palette.backdropVignette;
	return wash;
}

void UiFrost::Plate(ImDrawList* draw, ImTextureID backdrop, const ImVec2& pMin, const ImVec2& pMax,
	float rounding, Surface surface, float alpha)
{
	if (draw == nullptr)
		return;

	const float width = pMax.x - pMin.x;
	const float height = pMax.y - pMin.y;
	if (width <= 0.5f || height <= 0.5f)
		return;

	// ImGui builds the corner arcs from a radius it does not clamp; a radius larger than half the
	// shorter side makes the path fold back on itself.
	const float radius = (std::max)(0.0f, (std::min)(rounding, 0.5f * (std::min)(width, height)));

	/*
	 * The plate replaces the window background, and a window's body runs with the draw list's clip
	 * rect already narrowed to the padded inner rectangle. Pushing the plate's own rectangle is what
	 * stops the corners and the 1px rim from being clipped away - the symptom otherwise is a rounded
	 * panel with square inner corners.
	 */
	draw->PushClipRect(pMin, pMax, false);

	/*
	 * The blurred frame, sampled with screen-mapped uv.
	 *
	 * This is the whole trick that lets one texture serve every panel: the backdrop is a copy of the
	 * frame in screen space, so a panel at (x, y) reads exactly the part of it that was behind that
	 * panel. The alternative - a per-panel texture or a separate capture - would need one pass per
	 * panel and a scissor per pass.
	 */
	if (backdrop != 0 && surface != Surface::Well && alpha > 0.0f)
	{
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		if (display.x > 0.0f && display.y > 0.0f)
		{
			const ImVec2 uvMin(pMin.x / display.x, pMin.y / display.y);
			const ImVec2 uvMax(pMax.x / display.x, pMax.y / display.y);
			const ImU32 tint = IM_COL32(255, 255, 255, (int)(255.0f * (std::min)(1.0f, (std::max)(0.0f, alpha))));
			draw->AddImageRounded(backdrop, pMin, pMax, uvMin, uvMax, tint, radius, ImDrawFlags_None);
		}
	}

	ImVec4 tint = surface == Surface::Well ? g_palette.wellTint
		: (surface == Surface::Popup ? g_palette.popupTint : g_palette.glassTint);
	tint.w *= alpha;
	draw->AddRectFilled(pMin, pMax, ImGui::GetColorU32(tint), radius);

	/*
	 * Specular rim.
	 *
	 * A 1px border all round at 6% white plus a brighter line along the top edge at 14%: the light
	 * in the reference material comes from above, so the top edge catches it and the sides barely
	 * reflect anything. Drawn inside the plate, so the tint does not cover it. A well has no rim -
	 * it is a recess, and a highlight on a recess reads as a raised edge.
	 */
	if (surface != Surface::Well && alpha > 0.0f)
	{
		const float inset = 0.5f;
		const ImVec2 rimMin(pMin.x + inset, pMin.y + inset);
		const ImVec2 rimMax(pMax.x - inset, pMax.y - inset);
		ImVec4 side = g_palette.rimSide;
		side.w *= alpha;
		draw->AddRect(rimMin, rimMax, ImGui::GetColorU32(side),
			(std::max)(0.0f, radius - inset), ImDrawFlags_None, 1.0f);

		// The top edge, stopping where the corner arcs begin: running it into the arc draws a hard
		// horizontal stub across the curve.
		const float topFrom = rimMin.x + radius;
		const float topTo = rimMax.x - radius;
		if (topTo > topFrom)
		{
			ImVec4 top = g_palette.rimTop;
			top.w *= alpha;
			draw->AddLine(ImVec2(topFrom, rimMin.y), ImVec2(topTo, rimMin.y),
				ImGui::GetColorU32(top), 1.0f);
		}
	}

	draw->PopClipRect();
}

namespace
{
// Per-control hover weights, keyed by ImGui ID. Bounded so a long session that creates IDs
// dynamically cannot grow it without limit; the map is only a set of 0..1 floats, so clearing it
// costs one frame of transition rather than anything visible.
std::unordered_map<ImGuiID, float>& HoverWeights()
{
	static std::unordered_map<ImGuiID, float> weights;
	return weights;
}
}

float UiHoverBegin(const char* label)
{
	if (label == nullptr)
		return 0.0f;

	auto& weights = HoverWeights();
	const ImGuiID id = ImGui::GetID(label);
	auto it = weights.find(id);
	if (it == weights.end())
	{
		if (weights.size() > 4096)
			weights.clear();
		it = weights.emplace(id, 0.0f).first;
	}
	return it->second;
}

void UiHoverEnd(const char* label, bool hovered)
{
	if (label == nullptr)
		return;

	auto& weights = HoverWeights();
	const ImGuiID id = ImGui::GetID(label);
	auto it = weights.find(id);
	if (it == weights.end())
		return;

	/*
	 * Exponential approach with a 40 ms time constant, which reaches 95% in 120 ms - the figure the
	 * interface is specified in. Frame-rate independent, so a 240 Hz present and a 57 Hz simulation
	 * produce the same fade, and dt is clamped so a hitch cannot snap it.
	 */
	const float dt = (std::min)(ImGui::GetIO().DeltaTime, 0.1f);
	const float k = 1.0f - std::exp(-dt / 0.040f);
	const float target = hovered ? 1.0f : 0.0f;
	it->second += (target - it->second) * k;
	if (std::fabs(target - it->second) < 0.002f)
		it->second = target;
}

ImVec4 UiHoverBlend(const ImVec4& base, const ImVec4& hot, float weight)
{
	if (weight <= 0.0f)
		return base;
	if (weight >= 1.0f)
		return hot;
	return ImVec4(base.x + (hot.x - base.x) * weight,
		base.y + (hot.y - base.y) * weight,
		base.z + (hot.z - base.z) * weight,
		base.w + (hot.w - base.w) * weight);
}

/*
 * Re-apply the interface at a new monitor scale, between frames.
 *
 * ImGui 1.92 bakes glyphs at the requested size on demand, so the font atlas does not have to be
 * rebuilt: setting FontSizeBase is enough, and the next NewFrame() rasterises the sizes the layout
 * actually asks for. ApplyUiTheme() puts every padding and rounding back to its base value first,
 * otherwise ScaleAllSizes() would compound on every monitor change.
 */
static void RescaleUi(float scale)
{
    ApplyUiTheme();
    if (scale > 1.05f)
        ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontSizeBase = 18.0f * scale;
    ImGui::GetStyle().FontScaleMain = 1.0f;
    InfoLog("GUI: interface rescaled to %.2fx for the new display.", scale);
}

/*
 * Glyph ranges shared by every platform.
 *
 * The common-Chinese table covers the 2,500 characters the interface mostly uses, but not the
 * arrows and geometric marks in the explanatory text, and not a handful of technical characters
 * (frame, grille, phosphor, threshold...). Those used to render as the fallback '?' on Windows for
 * the same reason; asking for them explicitly costs nothing in atlas size.
 */
static const ImWchar* UiGlyphRanges(ImFontAtlas* atlas)
{
    static ImVector<ImWchar> ranges;
    if (ranges.empty())
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(atlas->GetGlyphRangesChineseSimplifiedCommon());
        builder.AddText("←↑→↓↔↕·—–…×○□△▲▼●◆★☆✓✔✕⚠");
        // The interface characters absent from the common-2,500 table. Written as universal
        // character names rather than as the characters themselves so the translation audit does
        // not mistake this lookup data for an untranslated label.
        builder.AddText("\u5239\u576A\u5C4F\u5E27\u6208\u6273\u62DF\u64CE\u6749\u6756\u679A\u6805\u6D1B\u6D4F\u6E32\u6FB3\u73D1\u77F6\u8367\u836B\u8C15\u8F74\u94AE\u9608\u9891\u9988");
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

#if !defined(_WIN32) && !defined(__APPLE__)
/*
 * Asks Fontconfig for every Simplified Chinese sans-serif face it knows, best first.
 *
 * The short list below covers the usual Noto and WenQuanYi locations, but a distribution is free to
 * install its CJK fonts anywhere - and to rank something this build cannot read first. That is not
 * hypothetical: on openSUSE Tumbleweed the best match is NotoSansCJKsc-VF.otf, a CFF2 variable font,
 * and handing it to the rasteriser aborted the program at start-up (Issue #5).
 *
 * So the ranked list is walked and the first face that passes the probe wins: Fontconfig still decides
 * the order, compatibility decides where to stop. Bounded, because every candidate is a file read and
 * a parse, and a machine with a hundred CJK faces should not spend a second on them.
 */
static std::vector<std::string> FindCjkFontsWithFontconfig(int limit)
{
    std::vector<std::string> fonts;

    FILE* pipe = popen("fc-match -s -f '%{file}\\n' 'sans-serif:lang=zh-cn' 2>/dev/null", "r");
    if (pipe == nullptr)
        return fonts;

    char buffer[1024];
    while ((int)fonts.size() < limit && fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        std::string path(buffer);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' '))
            path.pop_back();
        if (!path.empty())
            fonts.push_back(std::move(path));
    }

    pclose(pipe);
    return fonts;
}
#endif

void LoadUiFont(ImGuiIO& io, float scale)
{
    const float size = 18.0f * scale;

    // One CJK-capable file per platform, picked for a clean interface look. All three are UI faces
    // rather than document faces: PingFang on macOS, Microsoft YaHei on Windows, Noto Sans CJK on
    // Linux. The interface is otherwise entirely custom-drawn, so there is nothing to match
    // metrically - only the text has to be readable and to have Chinese glyphs.
#ifdef _WIN32
    const char* paths[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
    };
#elif defined(__APPLE__)
    const char* paths[] = {
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/Library/Fonts/Arial Unicode.ttf",
    };
#else
    /*
     * TrueType outlines first, then CFF, then whatever Fontconfig offers.
     *
     * The probe below checks every candidate before use, so this order is about quality of fit rather
     * than compatibility: WenQuanYi, Droid and the AR PL collections are TrueType and stay crisp at
     * interface sizes, while the Noto CJK files are CFF. Variable fonts - the "-VF" files - are
     * deliberately absent: they use the CFF2 table, which the rasteriser cannot read at all.
     */
    const char* paths[] = {
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
        "/usr/share/fonts/truetype/arphic/ukai.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
    };
#endif

    const ImWchar* ranges = UiGlyphRanges(io.Fonts);

    auto addFont = [&](const char* path) -> bool
    {
        if (path == nullptr || path[0] == '\0' || !std::filesystem::exists(path))
            return false;

        /*
         * Ask the rasteriser before handing the file over.
         *
         * ImGui asserts when stb_truetype cannot parse the data, and that assert is fatal: one
         * unsupported system font ended the program during start-up, before any window appeared
         * (Issue #5, a CFF2 variable font chosen by Fontconfig). Skipping the file and trying the next
         * candidate is the only reasonable answer, and naming the file and the reason in the log means
         * a report like that one is answered by the log alone.
         */
        const FontProbeResult probe = ProbeFontFile(path, 0);
        if (!probe.usable)
        {
            InfoLog("GUI: skipping font '%s' - %s.", path, probe.reason);
            return false;
        }

        ImFontConfig cfg;
        cfg.FontNo = 0;              // first face in a .ttc; PingFang.ttc starts with SC Regular
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = false;
        if (io.Fonts->AddFontFromFileTTF(path, size, &cfg, ranges) == nullptr)
            return false;

        // One line naming the face in use. A font report can then be answered from the log, which is
        // also how the probe's skips above make sense next to a launch that worked.
        InfoLog("GUI: interface font '%s'.", path);
        return true;
    };

    for (const char* path : paths)
        if (addFont(path))
            return;

#if !defined(_WIN32) && !defined(__APPLE__)
    // Tried last: Fontconfig knows where this distribution put its CJK fonts and in what order it
    // wants them used; the probe decides which of those this build can actually read.
    for (const std::string& candidate : FindCjkFontsWithFontconfig(8))
        if (addFont(candidate.c_str()))
            return;
#endif

    /*
     * Nothing usable. The built-in face has no CJK glyphs, so Chinese text would be blank boxes -
     * which looks like an encoding bug unless the reason is stated.
     */
    InfoLog("GUI: no usable CJK system font found; the built-in face has no Chinese glyphs, so Chinese "
            "text will not render.");
#if !defined(_WIN32) && !defined(__APPLE__)
    InfoLog("GUI: install one to fix it - for example fonts-wqy-microhei, or a non-variable Noto Sans CJK.");
#endif
    io.Fonts->AddFontDefaultVector();
}

struct GuiState
{
    GuiState() : fullConfig("Global"), config("Global") {}

    OSD::Platform::Window* window = nullptr;
#if defined(SUPERMODEL_PURE_VULKAN)
    // Owns the device and swapchain for the settings window for the whole run.
    Vulkan3D::CVulkanContext* vkContext = nullptr;
#else
    OSD::Platform::GLContext* glContext = nullptr;
#endif
    std::string configPath;
    Util::Config::Node fullConfig;
    Util::Config::Node config;
    // The loader owns the game list, so it has to outlive a single GuiRunOnce() call.
    std::unique_ptr<GameLoader> loader;
    int selectedGame = -1;
    std::shared_ptr<CInputs> inputs;
    KeyBindState kb{};
    bool saveSettings = true;
    // False after the first GuiRunOnce() call. A later call means the player has returned from a
    // game, and the in-game panel may have written settings straight to the file in the meantime.
    bool firstRun = true;
    // True while the start-up artwork is on screen over the still hidden window. Cleared once the
    // first frame has been presented and the window has been revealed.
    bool splashPending = false;
    // True once the window has been put on screen during start-up. Only meaningful while
    // splashPending is set: it keeps the early-reveal fallback from showing the window twice.
    bool windowShown = false;
};

/*
 * Fill the window's two config nodes from a settings file.
 *
 * GuiCreate uses this for the initial read. GuiRunOnce uses it again after a game, because the
 * in-game panel's Apply writes straight to the file: without this the window would keep showing the
 * values it had before the game, and on the next save it would write those stale values back over
 * what the panel persisted.
 *
 * The renderer override and the portable ROM-path repair live here rather than at the two call
 * sites, so a reload cannot silently drop them.
 */
static void LoadGuiConfig(GuiState& state, const Util::Config::Node& fileConfig)
{
    state.fullConfig = fileConfig;
    state.config = Util::Config::Node("Global");
    Util::Config::MergeINISections(&state.config, DefaultConfig(), state.fullConfig);

#if defined(SUPERMODEL_PURE_VULKAN)
    // There is no OpenGL renderer in this executable.  Force the saved configuration rather than
    // carrying a stale New3DEngine/Legacy3D choice back into the file on the next save.
    state.config.Set("Vulkan3DEngine", true);
#endif

    /*
     * Portable ROM folder.  A user who keeps the game zips in a rom/, roms/, romset/ or romsets/
     * folder next to the executable should not have to point the interface at it again after every
     * update - and on a fresh install the historical C:/Downloads default is often not where they
     * put them.  The folder is only adopted when the configured path is missing or is still the
     * old stock default; a path the user chose deliberately is left alone even when the volume is
     * temporarily unavailable.
     *
     * The value stored is relative ("roms/") so the whole installation can be moved.  ResolveRomPath()
     * anchors it to the executable on every platform, including Linux, where the settings file
     * itself lives under XDG.
     */
    {
        const std::string configured = state.config["ROMPath"].ValueAsDefault<std::string>("");
        std::string trimmed = configured;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
            trimmed.pop_back();

        const std::string lower = ToLowerAscii(trimmed);
        const bool legacyDefault = lower == "c:/downloads" || lower == "c:\\downloads";
#if defined(__APPLE__)
        // A drive-letter path can never name anything on macOS. Treating it as a stale setting
        // covers both the old stock default (C:/Downloads) and a configuration file copied over
        // from a Windows machine.
        const bool windowsDrivePath = lower.size() >= 2 && lower[1] == ':' &&
            lower[0] >= 'a' && lower[0] <= 'z';
#else
        const bool windowsDrivePath = false;
#endif
        std::error_code error;
        const bool usable = !trimmed.empty() &&
            std::filesystem::is_directory(PathFromUtf8(ResolveRomPath(trimmed)), error) && !error;

        if (!usable || legacyDefault)
        {
            std::string portable = FindPortableRomFolder(true);
            if (portable.empty() && !usable)
                portable = FindPortableRomFolder(false);
            if (!portable.empty())
            {
                if (portable != configured)
                {
                    InfoLog("GUI: using the portable ROM folder '%s' instead of '%s'.",
                        portable.c_str(), configured.c_str());
                }
                state.config.Set("ROMPath", portable);
                // Also correct the copy that is written back when the window is merely closed, so
                // the repair survives even without an explicit save.
                state.fullConfig.Set("ROMPath", portable);
            }
            else if (!usable && (trimmed.empty() || legacyDefault || windowsDrivePath))
            {
                const std::string fallback = FileSystemPath::GetDefaultRomPath();
                if (!fallback.empty() && fallback != configured)
                {
                    InfoLog("GUI: ROM path '%s' is unavailable; using '%s'.",
                        configured.c_str(), fallback.c_str());
                    state.config.Set("ROMPath", fallback);
                    state.fullConfig.Set("ROMPath", fallback);
                }
            }
        }
    }
}

GuiState* GuiCreate(const std::string& configPath, const Util::Config::Node& fileConfig)
{
    std::unique_ptr<GuiState> state(new GuiState());
    // Startup milestones, in milliseconds from entry. Cheap, and the only way to tell a slow
    // Vulkan device from a slow font or game-list step when someone reports a slow launch.
    const auto startupClock = std::chrono::steady_clock::now();
    const auto startupMs = [&startupClock]()
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startupClock).count();
    };
    state->configPath = configPath;
    g_settingsPath = configPath;        // seeds the "Save as" dialog with the file in use
    LoadGuiConfig(*state, fileConfig);

    // Before any ImGui context exists, so the first ApplyUiTheme() builds the right palette.
    // 0 = warm paper (light), 1 = warm dark, 2 = the interface as it was before the glass rework.
    {
        const int themeValue = state->config["UITheme"].ValueAsDefault<int>(0);
        SetUiTheme(themeValue == 1 ? UiTheme::WarmDark
            : (themeValue == 2 ? UiTheme::Classic : UiTheme::WarmPaper));
    }

    /*
     * Answer "the covers do not show" before anything asks for one.
     *
     * The directory is derived from the executable's own path, so this line also states where the
     * program is looking - which distinguishes a copied-out executable from a machine whose WIC has
     * no WebP decoder. Logged here rather than on the first lookup because the preview only asks for
     * a cover once a game is selected, and the answer is wanted even when nothing is selected.
     */
    LogBoxartDirectory();

    OSD::Platform::SetDPIAwareness();
    if (!OSD::Platform::Initialize(OSD::Platform::Subsystem_Video)) {
        std::cerr << "SDL could not initialize! Error: " << OSD::Platform::GetError() << std::endl;
        return nullptr;
    }

#ifdef __APPLE__
    // SDL has created NSApplication by now. Match the native title bar and the file pickers to the
    // interface theme instead of letting them follow the system independently.
    if (UiThemeIsDark())
        OSXFileDialogs::ApplyDarkAppearance();
    else
        OSXFileDialogs::ApplyLightAppearance();
#endif

#if !defined(SUPERMODEL_PURE_VULKAN)
    OSD::Platform::GLAttributes glAttributes;
    glAttributes.majorVersion = 4;
    glAttributes.minorVersion = 1;
    glAttributes.coreProfile = true;
    glAttributes.doubleBuffer = true;
    glAttributes.depthSize = 24;
    glAttributes.stencilSize = 8;
    OSD::Platform::SetGLAttributes(glAttributes);
#endif

    // Default window size, in client-area pixels (the title bar and borders are added on top, so
    // the frame ends up a little larger). 1280x800 is the comfortable size when the work area has
    // room for it; on a 1080p display at 150% the whole logical screen is only 1280x720, so the
    // per-display clamp in ApplyGuiWindowMinimumSize() brings this down instead.
    int windowWidth = 1280;
    int windowHeight = 800;
    {
        int displayIndex = 0;
        // Query the display DPI up front so the diagnostic line below always reports the real
        // value, even when a saved size means the scaling is not used.
        float ddpiScale = OSD::Platform::GetDisplayContentScale(
            OSD::Platform::GetDisplayIdForIndex(displayIndex));
        if (ddpiScale < 1.0f)
            ddpiScale = 1.0f;
#if defined(__APPLE__)
        // SDL sizes macOS windows in points; the Retina factor applies to the drawable, not to the
        // window. Scaling the request by the display DPI would ask for a window 1.5x too large.
        ddpiScale = 1.0f;
#endif

        // Reuse the size from the previous run if there is one, so a window the user has sized to
        // taste stays that way. Read through a const reference so that looking up a key the config
        // does not have yet (the size is only added once the GUI first saves) cannot create an
        // empty node in the runtime config.
        //
        // A saved size is in physical pixels and is used exactly as stored: it must not be scaled
        // again, or every launch would multiply the window by the DPI factor.
        const Util::Config::Node& savedConfig = state->config;
        const int savedWidth = savedConfig["GuiWindowWidth"].ValueAsDefault<int>(0);
        const int savedHeight = savedConfig["GuiWindowHeight"].ValueAsDefault<int>(0);
        if (savedWidth > 0 && savedHeight > 0)
        {
            windowWidth = savedWidth;
            windowHeight = savedHeight;
        }
        else
        {
            // First run: scale the default by the display DPI. SDL sizes windows in physical pixels
            // on a DPI-aware process, so asking for 1280x800 verbatim on a high-DPI display would
            // produce a window that looks small. On a display with no scaling this factor is 1.0 and
            // the window stays exactly 1280x800.
            windowWidth = (int)(windowWidth * ddpiScale);
            windowHeight = (int)(windowHeight * ddpiScale);
        }

        // Clamp to something usable: never smaller than a sane minimum, never larger than the
        // desktop work area (the saved size may come from a since-disconnected display).
        //
        // 1280x720 logical pixels is the smallest layout the two columns can render without
        // clipping; 1280x800 is the default size, used when the display has room for it. The
        // same floor is applied to a size saved on another display, so a window restored from
        // a 1024x768 machine is grown instead of opening below the minimum.
        const int minWidth = (int)(1280.0f * ddpiScale);
        const int minHeight = (int)(720.0f * ddpiScale);
        if (windowWidth < minWidth) windowWidth = minWidth;
        if (windowHeight < minHeight) windowHeight = minHeight;

        OSD::Platform::Rect usable{};
        OSD::Platform::DisplayInfo displayInfo;
        if (OSD::Platform::GetDisplayInfo(displayIndex, &displayInfo))
            usable = displayInfo.usableBounds;
        if (usable.width > 0 && usable.height > 0)
        {
            if (windowWidth > usable.width) windowWidth = usable.width;
            if (windowHeight > usable.height) windowHeight = usable.height;
        }

        // Record how the size was arrived at. Window geometry problems are otherwise hard to
        // diagnose remotely, and this shows at a glance whether DPI scaling or the work-area clamp
        // is responsible for a size the user did not expect.
        InfoLog("GUI window: %dx%d (saved %dx%d, display DPI scale %.2f, usable area %dx%d).",
            windowWidth, windowHeight, savedWidth, savedHeight, (float)ddpiScale, usable.width, usable.height);
    }

    OSD::Platform::WindowDesc windowDesc;
    windowDesc.title = SUPERMODEL_TITLE_FULL;
    windowDesc.width = windowWidth;
    windowDesc.height = windowHeight;
    windowDesc.flags =
#if defined(SUPERMODEL_PURE_VULKAN)
        OSD::Platform::WindowFlag_Vulkan |
#else
        OSD::Platform::WindowFlag_OpenGL |
#endif
#ifdef _WIN32
        // Created hidden, and shown once the start-up artwork has been replaced by a real frame.
        // Renderer set-up takes a moment, and a visible window during it is a black rectangle.
        OSD::Platform::WindowFlag_Hidden |
#endif
        OSD::Platform::WindowFlag_Resizable |
        OSD::Platform::WindowFlag_HighDPI;
    state->window = OSD::Platform::CreatePlatformWindow(windowDesc);
    if (!state->window) {
        std::cerr << "Window could not be created! Error: " << OSD::Platform::GetError() << std::endl;
        OSD::Platform::Shutdown();
        return nullptr;
    }
    // Has to come after creation: SDL installs its own default icon while creating the window.
    SetWindowIcon(state->window);
    InfoLog("GUI: window created (%.0f ms).", startupMs());

    /*
     * Minimum window size.
     *
     * The interface is two columns of fixed minimum content: the game list (with the cover-art
     * preview above it) and the settings panel. Below 1280x720 logical pixels the settings column
     * clips its controls and the list runs out of room for a readable game name, so the platform
     * enforces that floor and the window cannot be dragged into a state the layout cannot render.
     *
     * The floor is per display and is capped to the work area minus the window frame; see
     * ApplyGuiWindowMinimumSize(). This call covers the display the window is created on, and
     * the frame loop re-applies it when the window moves to another one.
     */
    ApplyGuiWindowMinimumSize(state->window);

    /*
     * Process-lifetime UI state. GuiCreate() runs once, but resetting it here makes a second
     * create (a test, or a future restart-in-process path) behave like a fresh launch instead of
     * inheriting the previous window's list position.
     */
    g_gameListStateRestored = false;
    g_gameListScrollRestorePending = false;
    g_gameListScrollRestoreFrames = 0;
    g_gameListScrollY = 0.0f;
    g_gameListFilter[0] = '\0';
    g_lastSelectedSet.clear();
    g_returnToManageSaves = false;
    g_returnToManageSavesSet.clear();

    // A rate saved on a faster display is clamped once, now that the window's display is known.
    ClampSavedFixedRefreshRate(state->config, state->fullConfig, state->window);

    g_guiFramePresented = false;
#if defined(SUPERMODEL_PURE_VULKAN)
    g_guiFrameSkipped = false;
#endif
#ifdef _WIN32
    // The artwork is sized to this window's frame and owned by it, so nothing has to be undone if
    // one of the set-ups below fails. When it cannot be shown the window is simply revealed now,
    // which is the behaviour before any of this existed.
    state->splashPending = OSD::Windows::ShowStartupSplash(state->window);
    if (!state->splashPending)
        OSD::Platform::ShowWindow(state->window);
#endif
    InfoLog("GUI: splash %s (%.0f ms).", state->splashPending ? "shown" : "unavailable", startupMs());

#if defined(SUPERMODEL_PURE_VULKAN)
    /*
     * The settings window owns its own Vulkan context for the whole run, the same way the GL build
     * keeps one GL context alive across game sessions.  It is independent of the game's context:
     * this window is hidden while a game runs, and its swapchain is not touched then.
     */
    state->vkContext = new Vulkan3D::CVulkanContext();
    if (state->vkContext->InitDevice(state->window,
            state->config["VkValidation"].ValueAsDefault<bool>(false),
            true,
            state->config["VulkanDevice"].ValueAsDefault<std::string>(""),
            true,
            "ui") != Result::OKAY)
    {
        ErrorLog("GUI: the Vulkan settings window could not create its device.");
        delete state->vkContext;
        state->vkContext = nullptr;
        OSD::Platform::DestroyWindow(state->window);
        OSD::Platform::Shutdown();
        return nullptr;
    }

    VkPhysicalDeviceProperties deviceProperties = {};
    vkGetPhysicalDeviceProperties(state->vkContext->PhysicalDevice(), &deviceProperties);
    s_glRendererName.assign(deviceProperties.deviceName);
    InfoLog("Vulkan: settings window context is on \"%s\".", s_glRendererName.c_str());
#else
    state->glContext = OSD::Platform::CreateGLContext(state->window);
    if (!state->glContext) {
        std::cerr << "OpenGL context could not be created! Error: " << OSD::Platform::GetError() << std::endl;
        OSD::Platform::DestroyWindow(state->window);
        OSD::Platform::Shutdown();
        return nullptr;
    }

    OSD::Platform::MakeGLContextCurrent(state->window, state->glContext);
    OSD::Platform::SetGLSwapInterval(1);
    // Skipped while the start-up artwork is up: this is what would put the still empty window on
    // screen. GuiRunOnce() reveals it after the first frame instead.
    if (!state->splashPending)
        RaiseSettingsWindow(state->window);
    glewExperimental = GL_TRUE;
    glewInit();

    /*
     * Read which GPU OpenGL actually landed on, while this context is current.
     *
     * This is a measurement, not an inference, and it is the answer to a question the settings window
     * used to answer by guessing: that panel named a card picked out of the *Vulkan* device list by
     * matching "NVIDIA"/"AMD" in the name. The process asks to be put on the discrete GPU through
     * NvOptimusEnablement / AmdPowerXpressRequestHighPerformance, but that is a request - a driver is
     * free to ignore it, and an OS-level per-application preference can override it. Reporting the
     * guess would then name a GPU that is not doing the rendering, with nothing to reveal it.
     *
     * The settings window's own context is the right one to ask, because it is created by the same
     * process with the same (default) attributes a game session's OpenGL context uses, so it lands on
     * the same GPU. Note that it is queried for display only - nothing here steers which card is used,
     * because OpenGL offers no API for that; see the comment on the setting below.
     */
    if (const GLubyte* glRenderer = glGetString(GL_RENDERER))
    {
        s_glRendererName.assign(reinterpret_cast<const char*>(glRenderer));
        InfoLog("OpenGL: settings window context is on \"%s\".", s_glRendererName.c_str());
    }
#endif
    InfoLog("GUI: renderer context ready (%.0f ms).", startupMs());
    /*
     * The window stays hidden while the start-up artwork is up.
     *
     * It used to be revealed here, because a Vulkan swapchain cannot reliably acquire an image from
     * a hidden window and on some drivers the first frame then failed until the splash's five-second
     * deadline. That cost every other driver a black window standing behind the artwork: the frame
     * needs a few hundred milliseconds to arrive, and the artwork is smaller than the window, so the
     * empty client area showed around it. GuiRunOnce() now reveals the window after the first
     * presented frame, and falls back to showing it early only if acquiring really does fail.
     */

    // The settings window is driven with the mouse and keyboard only. Gamepad navigation is
    // deliberately not enabled: with a pad connected, ImGui's gamepad support takes over the
    // interface and the mouse pointer disappears, which is worse than the convenience it adds.
    // Pads are still detected below, only to report which device the button presets will suit.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr;
    ApplyUiTheme();

    float scale = GetDPIScale(state->window);
    if (scale < 1.0f)
        scale = 1.0f;
    if (scale > 2.5f)
        scale = 2.5f;
    LoadUiFont(io, scale);
    if (scale > 1.05f)
        ImGui::GetStyle().ScaleAllSizes(scale);

    OSD::Platform::ImGuiPlatformInit(state->window);
#if defined(SUPERMODEL_PURE_VULKAN)
    g_uiVulkan = new OSD::SDL3App::UiVulkanBackend();
    auto uiBackend = g_uiVulkan->Init(state->window, *state->vkContext);
    if (!uiBackend)
    {
        ErrorLog("GUI: %s", uiBackend.error().c_str());
        delete g_uiVulkan;
        g_uiVulkan = nullptr;
        OSD::Platform::ImGuiPlatformShutdown();
        ImGui::DestroyContext();
        delete state->vkContext;
        state->vkContext = nullptr;
        OSD::Platform::DestroyWindow(state->window);
        OSD::Platform::Shutdown();
        return nullptr;
    }
#else
    ImGui_ImplOpenGL3_Init("#version 410");
#endif

    std::string xmlFile = ResolveDataFileSetting(state->config["GameXMLFile"].ValueAs<std::string>(), "Games.xml");
    state->loader.reset(new GameLoader(xmlFile));

    // Reset caches that must not survive a restart of the interface.
    g_writeSettings = false;
    g_romFolderCached.clear();
    g_roms.clear();

    InfoLog("GUI: interface ready (%.0f ms).", startupMs());
    return state.release();
}

std::vector<std::string> GuiRunOnce(GuiState* state)
{
    std::vector<std::string> romFiles;
    if (!state || !state->window)
        return romFiles;

    /*
     * Returning from a game: re-read the settings file before drawing anything.
     *
     * The in-game panel's Apply writes its values straight to the file, and the window's config nodes
     * still hold the values from before the session. Without this the Picture tab showed stale
     * numbers and the next save wrote them back over the panel's changes. The first call already has
     * the state GuiCreate built; reading it again would only be redundant work.
     */
#if defined(SUPERMODEL_PURE_VULKAN)
    const bool returningFromGame = !state->firstRun;
#endif
    if (!state->firstRun)
    {
        Util::Config::Node fileConfig("Global");
        if (!Util::Config::FromINIFile(&fileConfig, state->configPath))
            LoadGuiConfig(*state, fileConfig);
    }
    state->firstRun = false;

    // The game session leaves its own GL context current (or none at all), so re-establish ours
    // before touching any ImGui/GL state.
#if !defined(SUPERMODEL_PURE_VULKAN)
    OSD::Platform::MakeGLContextCurrent(state->window, state->glContext);
#else
    /*
     * Point volk back at this window's instance and device.
     *
     * volk's entry points are process-wide. The game session created its own context, which
     * rebound them, and that context has just been destroyed - so they still refer to the loader
     * trampolines of a dead instance, and the first instance-level call this window makes (present)
     * faults inside ntdll. Doing it here covers every return to the settings window: after a game,
     * after the artwork, after an aborted session.
     */
    if (state->vkContext != nullptr)
        state->vkContext->RebindFunctionPointers();
#endif
    // On the first call the start-up artwork is still covering the window. Showing the window here
    // would expose the empty client area it exists to hide, so the reveal waits until a frame has
    // been drawn - see the end of the loop below.
    bool splashPending = state->splashPending;
    if (!splashPending)
        RaiseSettingsWindow(state->window);
    // Safety net: if no frame is presented at all (a minimised window, say), the artwork must not
    // stay in front of the interface forever.
    const auto splashStart = std::chrono::steady_clock::now();
    const auto splashDeadline = splashStart + std::chrono::seconds(5);
    // Make sure the pointer is usable when the window comes back. ImGui's gamepad support used to
    // hide it, and that is disabled now.
    //
    // This alone would not be enough. SetCursorVisible only moves SDL's own visibility flag, and a
    // fullscreen game hides the pointer through the Win32 display count instead (see
    // CDirectInputSystem::SetMouseVisibility), which SDL cannot influence. RunGameSession restores
    // that count once the game's session ends, which is where it is safe to reach the input system.
    OSD::Platform::SetCursorVisible(true);

    // A pad may have been plugged in while the game was running; make sure it is picked up.
    g_controllerDetected = false;

    ImGuiIO& io = ImGui::GetIO();
    auto& games = state->loader->GetGames();
    g_gameLoader = state->loader.get();

#if defined(SUPERMODEL_PURE_VULKAN)
    /*
     * Return to Manage Saves when the session was started from it.
     *
     * The popup is closed on the way into the game (a modal over a hidden window would keep the
     * list from being used when the user comes back), so reopening it is what restores the
     * context. OpenManageSaves() also reloads the cards, which is what refreshes their thumbnails
     * and timestamps after a session that may have written a new save or renamed one.
     */
    if (returningFromGame && g_returnToManageSaves && !g_returnToManageSavesSet.empty())
    {
        g_returnToManageSaves = false;
        InfoLog("GUI: returning from a session started in Manage Saves; reopening '%s'.",
            g_returnToManageSavesSet.c_str());
        OpenManageSaves(g_returnToManageSavesSet);
    }
#endif

    bool running = true;
    bool exit = false;
    bool closed = false;
#if defined(SUPERMODEL_PURE_VULKAN)
    bool windowResized = false;
#endif
    while (running) {

        OSD::Platform::BeginInputFrame();
        OSD::Platform::Event event;
        while (OSD::Platform::PollEvent(&event)) {
            OSD::Platform::ImGuiPlatformProcessEvent(event);
            if (event.type == OSD::Platform::EventType::Quit ||
                event.type == OSD::Platform::EventType::WindowClose) {
                // Closing the GUI window ends the application; settings are still written out
                // unless the user was mid-edit.
                closed = true;
                running = false;
                break;
            }
            // Plugging or unplugging a pad invalidates the cached detection; the platform event
            // drain reports it, so navigation picks the device up without a restart.
            if (event.type == OSD::Platform::EventType::GamepadAdded ||
                event.type == OSD::Platform::EventType::GamepadRemoved ||
                event.type == OSD::Platform::EventType::GamepadRemapped)
                g_controllerDetected = false;
#if defined(SUPERMODEL_PURE_VULKAN)
            // Rebuilding immediately would invalidate an image that might still be in flight.  The
            // flag is applied once the event queue has been drained, before the next frame begins.
            if (event.type == OSD::Platform::EventType::WindowResized)
                windowResized = true;
#endif
        }
        if (closed)
            break;

#if defined(SUPERMODEL_PURE_VULKAN)
        if (windowResized && state->vkContext != nullptr)
        {
            windowResized = false;
            state->vkContext->RecreateSwapchain();
        }
#endif

        GUI(io, state->config, games, state->selectedGame, exit, state->saveSettings, state->window, state->inputs, state->kb);

        if (g_writeSettings) {
            Util::Config::CopyINISection(&state->fullConfig, state->config);
            // Same redirection as the save-and-quit path below: "Save as" sets the destination for
            // one write only, so that a later ordinary save returns to the standard file.
            const std::string target = g_saveAsPath.empty() ? state->configPath : g_saveAsPath;
            g_saveAsPath.clear();
            Util::Config::WriteINIFile(target, state->fullConfig, TimestampedConfigFileComment("Updated from GUI."));
            g_writeSettings = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
#if !defined(SUPERMODEL_PURE_VULKAN)
        OSD::Platform::SwapWindow(state->window);
        g_guiFramePresented = true;
#endif

#if defined(SUPERMODEL_PURE_VULKAN)
        /*
         * Reveal the window early only if the driver will not allocate an image while it is hidden.
         *
         * The normal path keeps it hidden until a frame has been presented, so the artwork is never
         * replaced by an empty client area. A driver that refuses to acquire from a hidden window
         * never gets that far, and without this it would sit here until the five-second deadline - the
         * slow launch the early reveal used to cause for everyone. Doing it on the first skipped
         * frame keeps both cases to one reveal.
         */
        if (splashPending && !state->windowShown && g_guiFrameSkipped)
        {
            state->windowShown = true;
            OSD::Platform::ShowWindow(state->window);
            InfoLog("Splash: revealing the window early; it could not acquire a frame while hidden.");
        }
        g_guiFrameSkipped = false;
#endif

        if (splashPending &&
            (g_guiFramePresented || std::chrono::steady_clock::now() > splashDeadline))
        {
            splashPending = false;
            state->splashPending = false;
            state->windowShown = true;
            // Removed first, then the window is raised over where it was: both happen without
            // returning to the message loop, so nothing flashes between the two.
#ifdef _WIN32
            OSD::Windows::HideStartupSplash();
#endif
            RaiseSettingsWindow(state->window);
            InfoLog("Splash: %s after %.2f s.",
                g_guiFramePresented ? "replaced by the first presented frame"
                                    : "removed by the safety deadline",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - splashStart).count());
        }

        if (exit)
            break;
    }

    if (!closed && !SelectedRomPath(state->selectedGame).empty())
        romFiles.emplace_back(SelectedRomPath(state->selectedGame));

    /*
     * Hand the input devices back before the game opens its own.
     *
     * The settings window caches an input system while its tabs are open - see the note on the
     * Controls tab - and the game session constructs a separate one. Releasing here keeps the two
     * from holding the same DirectInput/SDL devices at the same time. The binding editor's
     * references go with it: kb.input holds a shared_ptr into the same system.
     */
    if (!closed && !romFiles.empty())
    {
        state->kb.Reset();
        state->inputs.reset();
    }

    // Remember the window geometry. This is read back in GuiCreate() so the window reopens at the
    // size the user last left it. Note this is the client area, which is what SDL_CreateWindow()
    // takes, so the value round-trips exactly.
    int windowWidth = 0;
    int windowHeight = 0;
    OSD::Platform::GetWindowSize(state->window, &windowWidth, &windowHeight);

    if (!closed && state->saveSettings) {
        if (windowWidth > 0 && windowHeight > 0) {
            state->config.Set("GuiWindowWidth", windowWidth);
            state->config.Set("GuiWindowHeight", windowHeight);
        }
        if (g_previewSplit > 0.0f)
            state->config.Set("GuiPreviewSplit", g_previewSplit);
        SaveGameListPreferences(state->config, state->selectedGame);
        Util::Config::CopyINISection(&state->fullConfig, state->config);
        // "Save as" redirects just this one write. The destination is cleared afterwards so a later
        // plain save goes back to the standard file instead of silently continuing to overwrite the
        // copy the user exported once.
        const std::string target = g_saveAsPath.empty() ? state->configPath : g_saveAsPath;
        g_saveAsPath.clear();
        Util::Config::WriteINIFile(target, state->fullConfig, TimestampedConfigFileComment("Updated from GUI."));
    }
    else if (windowWidth > 0 && windowHeight > 0) {
        // Settings are not being written this time (the user declined, or the window was simply
        // closed), but the window size and the preview/list split are harmless UI preferences worth
        // keeping. Write them into the file's global section directly rather than copying the
        // edited runtime config, so nothing the user chose not to save leaks into the file.
        state->fullConfig.Set("GuiWindowWidth", windowWidth);
        state->fullConfig.Set("GuiWindowHeight", windowHeight);
        if (g_previewSplit > 0.0f)
            state->fullConfig.Set("GuiPreviewSplit", g_previewSplit);
        // Same idea for the game-list position: it is a UI preference, not part of the edited
        // settings, so it goes into the file directly rather than through the config the user
        // declined to save.
        SaveGameListPreferences(state->fullConfig, state->selectedGame);
        Util::Config::WriteINIFile(state->configPath, state->fullConfig, TimestampedConfigFileComment("Updated window size."));
    }

    // Hand the screen back to the game. The window is hidden rather than destroyed so that the
    // interface is still there when the game is quit.
    if (!closed)
    {
#if !defined(SUPERMODEL_PURE_VULKAN)
        OSD::Platform::MakeGLContextCurrent(state->window, nullptr);
#endif
        OSD::Platform::HideWindow(state->window);
    }

    return romFiles;
}

void GuiDestroy(GuiState* state)
{
    if (!state)
        return;

    if (state->window)
    {
#ifdef _WIN32
        // Normally already gone by now; this covers the window being closed while the artwork was
        // still up, and the early returns in GuiCreate() that never reach GuiRunOnce().
        OSD::Windows::HideStartupSplash();
#endif
#if defined(SUPERMODEL_PURE_VULKAN)
        ReleaseSaveCards();
        ReleaseBoxartTextures();
        if (g_uiVulkan != nullptr)
        {
            g_uiVulkan->Shutdown();
            delete g_uiVulkan;
            g_uiVulkan = nullptr;
        }
        OSD::Platform::ImGuiPlatformShutdown();
        ImGui::DestroyContext();
        delete state->vkContext;
        state->vkContext = nullptr;
#else
        OSD::Platform::MakeGLContextCurrent(state->window, state->glContext);
        ReleaseBoxartTextures();
        ImGui_ImplOpenGL3_Shutdown();
        OSD::Platform::ImGuiPlatformShutdown();
        ImGui::DestroyContext();
        OSD::Platform::DestroyGLContext(state->glContext);
#endif
        OSD::Platform::DestroyWindow(state->window);
    }

    delete state;
}
