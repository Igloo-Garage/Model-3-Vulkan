#include "SDLIncludes.h"
#include <GL/glew.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <thread>
#include "GameLoader.h"
#include "../Pkgs/imgui/imgui.h"
#include "../Pkgs/imgui/imgui_impl_sdl2.h"
#include "../Pkgs/imgui/imgui_impl_opengl3.h"
#include "../Src/Util/NewConfig.h"
#include "Util/ConfigBuilders.h"
#include "../Src/OSD/SDL/SDLInputSystem.h"
#include "../Src/Inputs/Inputs.h"
#include "OSD/ConfigFile.h"
#include "OSD/FileSystemPath.h"
#include "OSD/Logger.h"
#include "Graphics/Vulkan3D/VulkanContext.h"
#include "Main.h"
#include "Gui.h"
#include "Localization.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <SDL_syswm.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#include "../Src/OSD/Windows/DirectInputSystem.h"
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
static bool UiButton(const char* label, const ImVec2& size = ImVec2(0, 0)) { return ImGui::Button(Tr(label), size); }
static bool UiSmallButton(const char* label) { return ImGui::SmallButton(Tr(label)); }
static bool UiCheckbox(const char* label, bool* value) { return ImGui::Checkbox(Tr(label), value); }
static bool UiRadioButton(const char* label, bool active) { return ImGui::RadioButton(Tr(label), active); }
static bool UiTabItem(const char* label) { return ImGui::BeginTabItem(Tr(label)); }
static void UiColumn(const char* label, ImGuiTableColumnFlags flags = 0, float width = 0.0f)
{
	ImGui::TableSetupColumn(Tr(label), flags, width);
}
static bool UiInputInt(const char* label, int* value) { return ImGui::InputInt(Tr(label), value); }
static bool UiSliderInt(const char* label, int* value, int min, int max, const char* format = "%d")
{
	return ImGui::SliderInt(Tr(label), value, min, max, Tr(format));
}
static bool UiCombo(const char* label, int* current, const char* const items[], int count)
{
	return ImGui::Combo(Tr(label), current, items, count);
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
static const ImVec4 kAttentionColour(0.620f, 0.360f, 0.000f, 1.00f); // #9E5C00 on warm paper

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
	ImGui::TextColored(kAttentionColour, "%s", buffer);
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
	ImGui::TextColored(kAttentionColour, "%s", buffer);
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
        "Vulkan3DEngine", "New3DEngine", "VkValidation", "VulkanDevice",
        "XResolution", "YResolution", "FullScreen", "Stretch", "WideScreen", "VSync", "ShowFrameRate", "ShowStatsOsd",
        "IntegerScale", "UiLanguage",
        // Fullscreen refresh mode. The radio group on the 常用 page writes both keys together,
        // because they are one three-way choice and not two independent switches - the generic
        // list showed them as a bare 0.000000 float and a checkbox beside it, which invites setting
        // them to a combination the resolver then has to silently reinterpret.
        "ExclusiveFullscreen", "ExclusiveRefreshRate",
        // Bezel backdrop. Hand-drawn on the "画面" page, where a display option belongs.
        "BezelMode", "BezelImage", "BezelBrightness",
        // Anti-aliasing and post-processing filters. These live in "高级设置" only.
        "MSAA", "PostFXAA",
        "PostCRT", "PostScanlines", "PostScanlineIntensity",
        "PostMaskType", "PostMaskScale", "PostMaskIntensity",
        "PostGlow", "PostGlowLevels", "PostGlowThreshold", "PostGain",
        "SoundVolume", "MusicVolume", "EmulateSound", "EmulateDSB", "AudioDevice",
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
        { "LegacySoundDSP", "旧版音效 DSP" },
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
        // The whole post-processing chain is Vulkan2D's filter pass.
        "PostFXAA", "PostCRT", "PostScanlines", "PostScanlineIntensity",
        "PostMaskType", "PostMaskScale", "PostMaskIntensity",
        "PostGlow", "PostGlowLevels", "PostGlowThreshold", "PostGain",
        // The cabinet backdrop is composed into the black bars by Vulkan2D.
        "BezelMode", "BezelImage", "BezelBrightness",
    };
    static const char* const openGlOnly[] = {
        // Supersampling is the OpenGL anti-aliasing path. Main.cpp forces it to 1x under Vulkan.
        "Supersampling",
        // CRT colour temperature is consumed by SuperAA, which is the OpenGL screen setup.
        "CRTcolors",
        // The crosshair object is only created when there is no Vulkan context.
        "Crosshairs", "CrosshairStyle",
        // Passed to both renderers, but the Vulkan Init() signatures take it unnamed and ignore it;
        // only CRender2D acts on it.
        "UpscaleMode",
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
                        const bool stackLabel = index != 0 &&
                            (availableWidth - desiredItemWidth - style.ItemInnerSpacing.x) <
                                ImGui::CalcTextSize(displayLabel.c_str()).x;

                        const char* uiLabel = stackLabel ? idLabel.c_str() : labelWithId.c_str();
                        if (stackLabel)
                        {
                            ImGui::TextWrapped("%s", displayLabel.c_str());
                            ImGui::SetNextItemWidth(-FLT_MIN);
                        }
                        else if (index != 0)
                        {
                            ImGui::SetNextItemWidth(desiredItemWidth);
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
                                static const char* names[] = { "57.5 Hz（真实机台）", "60 Hz（非 VRR / 60Hz 面板）" };
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
                                selectedIndex = 0;      // 57.5 Hz, the arcade rate
                                ImGui::BeginDisabled();
                            }
                            ImGui::Combo(uiLabel, &selectedIndex, sVectorChar.data(), (int)sVectorChar.size());
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

                                ImGui::Combo(uiLabel, &selectedIndex, sVector.data(), (int)sVector.size());
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

static std::shared_ptr<CInputs> GetInputSystem(Util::Config::Node& config, SDL_Window* window)
{
    std::string selectedInputSystem = config["InputSystem"].ValueAs<std::string>();
    std::shared_ptr<CInputSystem> inputSystem;

    if (selectedInputSystem == "sdl")               { inputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(config, false)); }
    else if (selectedInputSystem == "sdlgamepad")   { inputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(config, true)); }

#ifdef _WIN32
    else if (selectedInputSystem == "dinput")       { inputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(config, window, false, false)); }
    else if (selectedInputSystem == "xinput")       { inputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(config, window, false, true)); }
    else if (selectedInputSystem == "rawinput")     { inputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(config, window, true, false)); }
#else
    (void)window;
#endif

    if (inputSystem) {
        auto inputs = std::shared_ptr<CInputs>(new CInputs(inputSystem));
        inputs->Initialize();
        inputs->LoadFromConfig(config);

        int x, y, w, h;
        SDL_GetWindowPosition(window, &x, &y);
        SDL_GL_GetDrawableSize(window, &w, &h);
        inputSystem->SetDisplayGeom(x, y, w, h);
        return inputs;
    }

    return nullptr;
}

struct RomEntry
{
    std::string title;
    std::string version;    // region and revision, e.g. "Japan, Revision A"
    std::string setName;
    std::string path;
    int year = 0;
    // Whether Games.xml has a definition for this set. Every .zip in the folder is listed, but one
    // with no definition cannot be started at all - GameLoader refuses it with "unknown game" - so
    // the list has to say which ones those are rather than presenting them as ordinary entries.
    bool supported = false;
};

static std::string g_romFolderCached;
static std::vector<RomEntry> g_roms;

// Fraction of the left column taken by the game preview. Written when the splitter is dragged and
// read back on the next launch (from the Global section, like GuiWindowWidth/Height). -1 means the
// stored value has not been read yet.
static float g_previewSplit = -1.0f;

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

static HWND WindowHandle(SDL_Window* window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info))
        return nullptr;
    return info.info.win.window;
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
static void RaiseSettingsWindow(SDL_Window* window)
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
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
#endif
}

static bool BrowseFolder(SDL_Window* window, std::string& path)
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
static bool BrowseSettingsFile(SDL_Window* window, std::string& path)
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
static bool BrowseImageFile(SDL_Window* window, std::string& path)
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
static bool BrowseSaveFile(SDL_Window* window, std::string& path)
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
        dialog->SetFileName(L"Supermodel.ini");

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
static void RaiseSettingsWindow(SDL_Window* window)
{
    if (window == nullptr)
        return;
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
}

static std::filesystem::path PathFromUtf8(const std::string& text)
{
    return std::filesystem::path(text);
}
#endif

/*
 * Platform dispatch for the four pickers.
 *
 * Windows and macOS each have a native implementation under the same names. Linux has no portable
 * dialog API and the emulator does not want a GTK or Qt dependency, so it asks the two command-line
 * front ends that desktop environments provide: zenity (GTK) and kdialog (KDE). If neither is
 * installed, the text field remains the only way to set a path, exactly as before.
 */
#if defined(__APPLE__)
static bool BrowseFolder(SDL_Window* window, std::string& path) { return OSXFileDialogs::BrowseFolder(window, path); }
static bool BrowseSettingsFile(SDL_Window* window, std::string& path) { return OSXFileDialogs::BrowseSettingsFile(window, path); }
static bool BrowseImageFile(SDL_Window* window, std::string& path) { return OSXFileDialogs::BrowseImageFile(window, path); }
static bool BrowseSaveFile(SDL_Window* window, std::string& path) { return OSXFileDialogs::BrowseSaveFile(window, path); }
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

static bool BrowseFolder(SDL_Window*, std::string& path)
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

static bool BrowseSettingsFile(SDL_Window*, std::string& path)
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

static bool BrowseImageFile(SDL_Window*, std::string& path)
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

static bool BrowseSaveFile(SDL_Window*, std::string& path)
{
    const DialogTool tool = DetectDialogTool();
    const std::string title = Tr("设置另存为");
    const std::string suggested = path.empty() ? std::string("Supermodel.ini") : path;
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

static std::vector<RomEntry> ScanRomFolder(const std::string& folder, const std::map<std::string, Game>& games)
{
    std::vector<RomEntry> entries;
    std::error_code error;
    // A relative setting is anchored to the executable's directory, the same convention the log and
    // the game XML use. Without this, typing a relative path appeared to do nothing unless the
    // program happened to have been started from exactly the right directory.
    std::filesystem::path directory = PathFromUtf8(FileSystemPath::ResolvePath(folder));
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
        auto found = byName.find(ToLowerAscii(entry.setName));
        if (found != byName.end())
        {
            entry.title = found->second->title.empty() ? entry.setName : found->second->title;
            entry.version = found->second->version;
            entry.setName = found->second->name;
            entry.year = found->second->year;
            entry.supported = true;
        }
        else
        {
            entry.title = entry.setName;
        }
        entries.push_back(std::move(entry));
    }

    // Supported sets first, then the rest, each group alphabetical.
    //
    // Sorting purely by name would scatter the unsupported sets through the list next to the working
    // version of the same game - scuda would sit right beside 飞弹赛车 - which reads as though the
    // list were inconsistent. Gathering them at the bottom keeps the alphabetical run of playable
    // games intact and makes the unusable ones easy to skip.
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
    InfoLog("GUI: %d ROM(s) listed, %d title(s) translated for the current language, %d without a definition in %s.",
        (int)entries.size(), translated, unsupported, "Games.xml");

    return entries;
}

static void RefreshRoms(const std::string& folder, const std::map<std::string, Game>& games)
{
    if (folder == g_romFolderCached)
        return;
    g_romFolderCached = folder;
    g_roms = ScanRomFolder(folder, games);
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
    unsigned texture = 0;
    int width = 0;
    int height = 0;
};

static std::map<std::string, BoxartTexture> g_boxartTextures;

#ifdef _WIN32
/*
 * Decodes WebP boxart with WIC.
 *
 * Windows 10 1809 and later ship a WIC codec for WebP, so no third-party decoder is needed. The
 * image is scaled while still on the CPU: the files are around 512x700, but the preview only ever
 * draws them about 200 pixels tall and there are dozens of them.
 */
static bool DecodeBoxart(const std::filesystem::path& path, int maxWidth, std::vector<uint8_t>& pixels, int& width, int& height)
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
            SUCCEEDED(scaler->Initialize(frame, scaledWidth, scaledHeight, WICBitmapInterpolationModeCubic)))
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
#endif

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

    const std::filesystem::path path = PathFromUtf8(FileSystemPath::GetPath(FileSystemPath::Assets)) /
        "Boxarts" / (setName + ".webp");
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return result;

#ifdef _WIN32
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    if (!DecodeBoxart(path, 384, pixels, width, height) || pixels.empty())
        return result;

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

    result.texture = texture;
    result.width = width;
    result.height = height;
    g_boxartTextures[setName] = result;
#endif

    return result;
}

static void ReleaseBoxartTextures()
{
    for (auto& item : g_boxartTextures)
    {
        if (item.second.texture != 0)
            glDeleteTextures(1, &item.second.texture);
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
    ImGui::BeginChild("GamePreview", ImVec2(width, height), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (entry == nullptr)
    {
        UiTextDim("把鼠标移到游戏上，或单击选中，这里会显示封面和英文简介。");
        ImGui::EndChild();
        return;
    }

    const BoxartTexture art = LoadBoxart(entry->setName);
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
    const char* blurb = entry->supported ? GameBlurb(entry->setName) : nullptr;
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
        UiTextDim("这个 ROM 集在 Games.xml 里没有定义。");
    }
    else
    {
        UiTextDim("这个游戏还没有简介。");
    }
    ImGui::EndGroup();

    ImGui::EndChild();
}

static std::string SelectedRomPath(int index)
{
    if (index < 0 || index >= (int)g_roms.size())
        return {};
    return g_roms[index].path;
}

static Game GameForSelection(const std::map<std::string, Game>& games, int index)
{
    Game game;
    if (index < 0 || index >= (int)g_roms.size())
        return game;
    const std::string& name = g_roms[index].setName;
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
    if (config["Vulkan3DEngine"].ValueAs<bool>())
        return 0;
    if (config["New3DEngine"].ValueAs<bool>())
        return 1;
    return 2;
}

static void SetRenderer(Util::Config::Node& config, int index)
{
    config.Set("Vulkan3DEngine", index == 0);
    config.Set("New3DEngine", index != 2);
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

// Names of the Vulkan physical devices, queried once. Enumerating creates a throw-away
// Vulkan instance, so this is deliberately deferred until the tab is actually drawn.
static const std::vector<std::string>& GetDisplayDevices()
{
    static std::vector<std::string> devices;
    static bool queried = false;
    if (!queried)
    {
        queried = true;
        devices = Vulkan3D::EnumeratePhysicalDeviceNames();
    }
    return devices;
}

// Names of the host audio output devices, queried once. SDL needs its audio subsystem up to
// answer this, which is harmless here because nothing is playing yet.
static const std::vector<std::string>& GetAudioDevices()
{
    static std::vector<std::string> devices;
    static bool queried = false;
    if (!queried)
    {
        queried = true;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
        {
            int count = SDL_GetNumAudioDevices(0);   // 0 = playback devices
            for (int i = 0; i < count; i++)
            {
                const char* name = SDL_GetAudioDeviceName(i, 0);
                if (name != nullptr && name[0] != '\0')
                    devices.emplace_back(name);
            }
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

    if (ImGui::Combo(Tr(label), &selected, items.data(), (int)items.size()))
    {
        // If user selects 0 ("Auto/System Default") or selects index 1 (which is the default device),
        // we can store "" or the device name directly. Storing "" ensures 100% native fallback to SDL default route.
        if (selected == 0)
            config.Set(key, std::string());
        else
            config.Set(key, devices[selected - 1]);
    }

    if (devices.empty() && emptyHint != nullptr)
        UiTextDim("%s", emptyHint);
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
    const std::string system = config["InputSystem"].ValueAsDefault<std::string>("xinput");
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

    UiTextColored(kAttentionColour,
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

    ImGui::SetNextItemWidth(520.0f);
    if (ImGui::Combo(Tr("输出显示器##DisplayNumber"), &selected, items.data(), static_cast<int>(items.size())))
        config.Set("DisplayNumber", selected == 0 ? 0 : outputs[selected - 1].number);

    ImGui::SameLine();
    UiTextDim("(?)");
    if (ImGui::IsItemHovered())
        UiTooltip(
            "游戏窗口与两种独占全屏都出现在所选显示器上，下次启动游戏时生效。\n"
            "选「自动」则始终跟随系统主显示器。\n"
            "\n"
            "每项末尾的 #n 是 Windows 给该显示器的编号（即「显示设置 → 标识」里的数字）。\n"
            "这个编号也是 Scripts\\ArcadeDisplay.ps1 的 -Monitor 参数 —— 两边必须填同一个，\n"
            "否则脚本在某一台上切换了 57.524Hz 模式，而游戏在另一台上全屏，结果是\n"
            "静默退回桌面刷新率（不报错）。");
}

static void DrawQuickSettings(Util::Config::Node& config, std::shared_ptr<CInputs>& inputs)
{
    // Language comes first because it changes every other label on the page.
    UiSep("语言");
    {
        // Each language is listed in its own language, which is what a language picker should do.
        const char* languageNames[] = { UiLanguageName(UiLanguage::English), UiLanguageName(UiLanguage::Chinese) };
        int languageIndex = GetUiLanguage() == UiLanguage::Chinese ? 1 : 0;
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo(Tr("界面语言##UiLanguage"), &languageIndex, languageNames, 2))
        {
            SetUiLanguage(languageIndex == 1 ? UiLanguage::Chinese : UiLanguage::English);
            config.Set("UiLanguage", languageIndex);
        }
    }

    UiSep("画面");
    int renderer = RendererIndex(config);
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
    }

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

    if (renderer != 0)
        ImGui::BeginDisabled();
    BoolSetting(config, "Vulkan 校验层##VkValidation", "VkValidation");
    if (renderer != 0)
        ImGui::EndDisabled();

    if (renderer == 0)
    {
        DeviceCombo(config, "显示设备##VulkanDevice", "VulkanDevice", GetDisplayDevices(),
            "自动（默认高性能显卡）", "没有检测到可用的 Vulkan 显卡。");
    }
    else
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
                "OpenGL 无法在程序内选择显卡（OpenGL 没有设备枚举接口）。\n"
                "本程序只能“请求”使用独立显卡，驱动可以不理会。\n"
                "\n"
                "要改用另一块显卡：\n"
                "Windows 设置 → 系统 → 显示 → 显卡（或“高级显示器设置”里的图形设置），\n"
                "为 Supermodel-VK.exe 指定“高性能”，然后重新启动本程序。\n"
                "\n"
                "注意：该设置是否对 OpenGL 生效由驱动决定，且本程序无法检测它是否被采纳，\n"
                "以上一行 GL_RENDERER 为准。");
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
    if (ImGui::Combo(Tr("分辨率"), &preset, presetLabels, IM_ARRAYSIZE(presetLabels)))
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
    BoolSetting(config, "显示帧率##ShowFrameRate", "ShowFrameRate");
    ImGui::SameLine();
    /*
     * The readout is toggled in play with the key above Tab, and saying so here is the only way anyone
     * would find that out - a keyboard shortcut with no mention anywhere may as well not exist.
     */
    BoolSetting(config, "统计面板##ShowStatsOsd", "ShowStatsOsd");
    HelpMarker(
        "在游戏画面左上角显示一串实时数据：渲染引擎、屏幕刷新率与同步方式、CPU 频率与占用、"
        "GPU 频率与占用、显存与内存占用、帧率和 1% Low 帧率。\n"
        "游戏里按 Tab 上面那个键（`）也能开关。\n"
        "只有 Vulkan 渲染引擎下有效。");

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
                    "怎么选：\n"
                    "  · 显示器支持 VRR（G-SYNC/FreeSync）→ 选第 2 项\n"
                    "  · 显示器不支持 VRR，但已检测到 57/58Hz 模式 → 选第 3 项\n"
                    "  · 两者都没有 → 留在第 1 项（第 3 项不可用）\n"
                    "\n"
                    "三种方式的区别只在于「谁定下一次扫描」：\n"
                    "第 1 项由桌面合成器定（面板按自身刷新率），\n"
                    "第 2 项由显示器跟随模拟器，第 3 项把显示器锁到 57.5Hz。");
            else
                UiTooltip(
                    "怎么选：\n"
                    "  · 显示器支持 VRR（G-SYNC/FreeSync）→ 选第 2 项\n"
                    "  · 否则留在第 1 项；想用第 3 项需先在显卡控制面板创建 57.524Hz 自定义分辨率\n"
                    "\n"
                    "三种方式的区别只在于「谁定下一次扫描」：\n"
                    "第 1 项由桌面合成器定（面板按自身刷新率），\n"
                    "第 2 项由显示器跟随模拟器，第 3 项把显示器锁到 57.5Hz。");
        }

        if (UiRadioButton("无边框全屏（默认）##SyncOff", mode == 0))
            applyMode(0);
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "游戏和设置界面共用同一个窗口，来回切换更顺，\n"
                "代价是有时帧率略低。\n"
                "\n"
                "【例外】若驱动已为「窗口和全屏模式」启用 G-SYNC/FreeSync\n"
                "（NVIDIA 控制面板 → 设置 G-SYNC），本模式也能让显示器跟随模拟器，\n"
                "效果与下面的独占模式相同，且无需自定义分辨率。\n"
                "这一点本程序无法检测，只能看显示器自己的实时刷新率：\n"
                "游戏运行时显示约 57-58Hz 表示已生效；显示桌面刷新率（如 120Hz）则未生效，\n"
                "此时画面会有轻微帧节奏不均，用下面的独占模式可以消除。");

        if (UiRadioButton("独占全屏 · G-SYNC/VRR##SyncGSync", mode == 1))
            applyMode(1);
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "让显示器的 G-SYNC/FreeSync 跟着模拟器的帧率走，画面不撕裂。\n"
                "需要 VRR 显示器，驱动里也要开启；不需要自定义分辨率。\n"
                "\n"
                "【必须满足】显示器支持 VRR，且其刷新率范围包含 57.5Hz。\n"
                "本程序会读取 EDID 的时序范围：\n"
                "若包含 57.5Hz，就直接按街机 57.5Hz 呈现（「画面」页的「刷新率」显示 57.5 并置灰）；\n"
                "若读不到 EDID 或不包含，则仍按「画面」页的「刷新率」呈现。\n"
                "EDID 没写 VRR 范围时，可以在这些地方确认\n"
                "（都看「范围」而不是「是否支持」）：\n"
                "  · Windows 11：设置 → 系统 → 显示 → 高级显示器设置\n"
                "  · NVIDIA 控制面板 → 设置 G-SYNC（会列出该显示器的刷新率范围）\n"
                "  · 显示器自带的 OSD 菜单（多数游戏屏会显示当前实时刷新率）\n"
                "若显示器不支持，本项不会报错也不会更差，只是退回按面板自身的刷新率输出，\n"
                "画面会有和「无边框全屏」类似的轻微帧节奏不均。\n"
                "\n"
                "【笔记本内屏】多数笔记本内置屏不支持 VRR（外接电竞屏通常支持）。\n"
                "这类机器上本项与「无边框全屏」等价 —— 面板仍按自身刷新率，\n"
                "既不会更差也不会更好，可以留在第 1 项。");

        if (mode == 1)
        {
            ImGui::Indent();
            if (VrrArcadeRateSupported())
                UiTextDim("已按街机 57.5Hz 自动呈现（显示器时序范围包含 57.5Hz）。");
            else
                UiTextDim("未确认显示器时序范围包含 57.5Hz，按「画面」页的刷新率呈现。");
            ImGui::Unindent();
        }

        /*
         * Why the requirements above are a tooltip and not a paragraph.
         *
         * The declared timing range now decides the pacing rate, not just the wording: this tooltip
         * is where the reader learns why the rate did or did not switch to the arcade one. But
         * "said" does not have to mean "occupies five lines of the page": the sentence is there when
         * someone is deciding, and out of the way when they are not. The same reasoning applies to
         * the fixed-rate notes below.
         */

        /*
         * Said here rather than only in the README, because the declared range is what decides the
         * pacing rate - and the usual reason someone believes G-SYNC is on while the picture is
         * still uneven.
         *
         * "The display supports G-SYNC" and "the display will sync at 57.5 Hz" are different claims.
         * Variable refresh only works between the display's own minimum and maximum rates, and the
         * minimum is often 48 Hz but can be 60 - in which case 57.5 Hz is outside the range entirely
         * and the driver either repeats frames to reach the minimum or stops varying altogether.
         * ResolveFullScreenMode() reads the declared range and only switches to the arcade rate when
         * 57.5 Hz is inside it; otherwise the configured rate is kept. The sentence stays because a
         * display that declares nothing still gets the configured rate, and it is worth knowing why.
         *
         * Deliberately not used to disable the option, and the reason is not that detection is
         * impossible - a display's declared range IS read, and shown above when it states one. The
         * reason is the asymmetry of that evidence: a declaration proves the capability, but its
         * absence proves nothing, because a display can support VRR without declaring it (older AMD
         * HDMI FreeSync lives in the driver rather than in the EDID). Disabling on a missing
         * declaration would take the option away from users who have the capability and simply do not
         * announce it - which is the worst possible reading of the data. So the option stays
         * selectable and the consequence of a display that cannot do it is stated instead.
         *
         * Note the asymmetry with the fixed-rate option above, which *is* disabled. That one can be
         * checked and its failure is provable - it would do exactly what G-SYNC does. This one's
         * failure is merely a missed opportunity: the session still runs, at the desktop mode, with
         * frame pacing no worse than borderless.
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
                tip = "把显示器锁到真机的 57.5Hz，与帧率 1:1，画面最顺滑。\n"
                      "少数显示器或驱动不接受该时序，遇到黑屏就换回上面的选项。";
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
                tip = "把显示器锁到真机的 57.5Hz，与帧率 1:1，画面最顺滑。\n"
                      "这是给没有 VRR 的显示器用的替代方案，需要先创建自定义分辨率。\n"
                      "少数显示器或驱动不接受该时序，遇到黑屏就换回上面的选项。\n"
                      "\n"
                      "【本项为何不可用】\n"
                      "本项需要一个 57/58Hz 的「固定模式」，而 VRR 是另一回事：\n"
                      "VRR 是一段可以落在其中的范围，不需要存在某个具体速率对应的模式。\n"
                      "所以「没有 57/58Hz 模式」并不等于「做不到 57.5Hz」——\n"
                      "若显示器支持 VRR，用上面的 G-SYNC 即可，不受此影响。\n"
                      "\n"
                      "【不支持 VRR 时怎么办】\n"
                      "需在显卡控制面板创建一个 57.524Hz 自定义分辨率\n"
                      "（见 Scripts\\ArcadeDisplay.ps1，创建后自动生效）。\n"
                      "创建前请对照下方列出的该显示器「时序范围」：\n"
                      "若 57.5Hz 不在其中，创建也不会被接受。\n"
                      "\n"
                      "注意「在时序范围内」只是必要条件 —— 它说明面板会接受该时序，\n"
                      "是否真能创建还取决于显卡驱动是否开放自定义分辨率（多数笔记本面板不开放）。\n"
                      "另：未标「已声明 VRR」不代表不支持，只是 EDID 里没写。";
            }
            UiTooltip("%s", tip.c_str());
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
                UiTextColored(kAttentionColour,
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
#ifdef _WIN32
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
    if (ImGui::Combo(Tr("输入系统##InputSystem"), &inputIndex, inputLabels, IM_ARRAYSIZE(inputLabels)))
    {
        config.Set("InputSystem", std::string(choices[inputIndex].id));
        inputs.reset();
    }
    ImGui::SameLine();
    UiTextDim("(?)");
    if (ImGui::IsItemHovered())
        UiTooltip(
            "%s\n%s\n%s\n\n%s",
            Tr("按键在「按键」页里改。Xbox 手柄选 XInput，左右扳机才能分开。"),
            Tr("手柄按键按「位置」对应，不看你手柄上印的字："),
            Tr("    Back / Select / Share 位置 = 投币      Start / Options / Menu 位置 = 启动 / 确认"),
            Tr("不同输入系统下扳机轴的名字不同，切换后原来的绑定会失效：XInput / SDL 手柄用 JOY1_RZAXIS，DirectInput / Raw Input / SDL 用 JOY1_ZAXIS_POS。"));
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

    if (ImGui::Combo(Tr("手柄型号##ControllerFamily"), &familyIndex, familyLabels, IM_ARRAYSIZE(familyLabels)))
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
    ImGui::Combo(Tr("键位预设##ControlPreset"), &presetIndex, controlPresetLabels, IM_ARRAYSIZE(controlPresetLabels));

    if (UiButton("应用推荐键位"))
        ImGui::OpenPopup("ApplyPreset");
    HelpMarker("会覆盖「按键」页里已有的绑定。");

    // View cycling. Sits here rather than on the keys page because it changes how the view buttons
    // behave rather than what they are bound to.
    UiSep("视角切换");
    BoolSetting(config, "单键循环切换视角##VRCycle", "VRCycle");
    HelpMarker(
        "开启后用一个键循环切换 4 个视角，不再占用 4 个键位。\n"
        "只影响有 4 视角的游戏（Daytona 2、Scud Race、Le Mans 24）。");
    if (config["VRCycle"].ValueAsDefault<bool>(false))
    {
        const char* viewNames[] = { Tr("VR1（红）"), Tr("VR2（蓝）"), Tr("VR3（黄）"), Tr("VR4（绿）") };
        int def = config["VRDefaultView"].ValueAsDefault<int>(2);
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo(Tr("起始视角##VRDefaultView"), &def, viewNames, 4))
            config.Set("VRDefaultView", def);
        ImGui::SameLine();
        UiTextDim("(?)");
        if (ImGui::IsItemHovered())
            UiTooltip(
                "进入游戏时默认使用的视角。\n"
                "Daytona 2 的默认第三人称跟车视角是 VR3（黄）。");
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
            std::string system = config["InputSystem"].ValueAsDefault<std::string>("xinput");
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
	SDL_DisplayMode mode{};
	if (SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.h > 0)
		return mode.h;
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
static void DrawBezelSettings(Util::Config::Node& config, SDL_Window* window)
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
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::Combo(Tr("背景图##BezelMode"), &mode, modes, 3))
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

    const bool vulkan = config["Vulkan3DEngine"].ValueAsDefault<bool>(false);

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
    if (!vulkan)
    {
        ImGui::Spacing();
        // The renderer radios are on the 常用 (Common) page - not on 画面 (Video), which is where this
        // text used to send people. 画面 holds resolution, fullscreen, CRT colour and 2D upscaling,
        // all of which work under OpenGL too; naming it here sent the reader to a page that cannot
        // change the renderer at all.
        UiText("当前渲染引擎是 OpenGL，本页的抗锯齿与滤镜都不起作用。");
        UiText("想用这些效果，请到「常用」页把渲染引擎切换到 Vulkan 3D。");
        ImGui::Spacing();
    }

    /*
     * The page is disabled as a whole when Vulkan is not running, rather than control by control.
     *
     * Everything here is Vulkan-only with exactly one exception - integer scaling, which is applied
     * where the viewport geometry is computed and therefore works under either renderer. That
     * exception steps back out of the disabled block below, which is less code and much harder to get
     * wrong than wrapping each of the two dozen controls individually: a section added later is
     * covered automatically, and there is no list of keys to keep in step.
     */
    if (!vulkan)
        ImGui::BeginDisabled();

    UiSep("抗锯齿");

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
    // Values must stay in step with the MSAA config allow-list.
    static const char* msaaItems[] = { "关闭", "2x MSAA", "4x MSAA", "8x MSAA" };
    static const int msaaValues[] = { 0, 2, 4, 8 };
    int msaa = config["MSAA"].ValueAsDefault<int>(0);
    int msaaIndex = 0;
    for (int i = 0; i < 4; ++i)
        if (msaaValues[i] == msaa)
            msaaIndex = i;
    ImGui::SetNextItemWidth(220.0f);
    // The translated list lives in a static array: ImGui holds on to the item pointers for as long
    // as the popup is open, so it has to survive the frame.
    static const char* msaaTr[4];
    if (ImGui::Combo(Tr("硬件 MSAA##MSAA"), &msaaIndex, UiItems(msaaItems, 4, msaaTr), 4))
        config.Set("MSAA", msaaValues[msaaIndex]);
    HelpMarker(
        "显卡对多边形边缘做抗锯齿，贴图本身保持锐利。\n"
        "效果最好，有独显优先用它。8x 需要驱动支持，4K 下开销很大。");
    }   // end of the sections omitted from the overlay

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
    BoolSetting(config, "FXAA 后处理抗锯齿##PostFXAA", "PostFXAA");
    HelpMarker(
        "后处理抗锯齿，开销极低但整个画面会略软。\n"
        "适合集显；有独显优先用上面的 MSAA。");

    UiSep("画面缩放");

    /*
     * The one control on this page that is not Vulkan-only, so it steps back out of the disabled
     * block. Integer scaling is applied in CalcScreenGeometry, which both renderers go through, so
     * greying it out with the rest would be wrong - and it is the option most likely to be wanted on
     * an older machine, which is exactly where OpenGL is being used.
     */
    if (!vulkan)
        ImGui::EndDisabled();

    BoolSetting(config, "整数倍缩放（点对点）##IntegerScale", "IntegerScale");
    HelpMarker(
        "只按整数倍放大，放不满的部分留黑边。\n"
        "2D 层的文字会更均匀，代价是损失一些屏幕面积。\n"
        "倍率由面板的真实分辨率算出来，不是由上面的「分辨率」决定：\n"
        "384 x 倍率 必须不超过面板行数，所以 2160 行的 4K 屏只能到 5 倍（2480 x 1920）。\n"
        "勾选后「分辨率」会被覆盖，实际倍率见本页下面的读数。");

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
        BoolSetting(config, "宽屏##WideScreen", "WideScreen");
        HelpMarker(
            "只加宽水平视锥，不拉伸画面。\n"
            "适合在同一场景里对比宽屏开关的效果。\n"
            "部分游戏的 2D 背景不会延伸到加宽区域，四角可能露黑。");
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

    // Back into the disabled block: the CRT and glow sections below are Vulkan-only like the rest.
    if (!vulkan)
        ImGui::BeginDisabled();

    /*
     * Collapsible because this section is long enough to push the save-state menu below the fold in
     * the in-game panel. Open by default so the filter is still the first thing found there; the
     * open/closed state is kept by ImGui for the session.
     */
    if (ImGui::CollapsingHeader(Tr("CRT 滤镜##PostCRTSection"), ImGuiTreeNodeFlags_DefaultOpen))
    {
    // CRT mode. 0 off, 1 scanlines, 2 scanlines + shadow mask.
    static const char* crtItems[] = { "关闭", "扫描线", "扫描线 + 荫罩" };
    int crt = config["PostCRT"].ValueAsDefault<int>(0);
    ImGui::SetNextItemWidth(220.0f);
    static const char* crtTr[3];
    if (ImGui::Combo(Tr("CRT 滤镜##PostCRT"), &crt, UiItems(crtItems, 3, crtTr), 3))
        config.Set("PostCRT", crt);
    HelpMarker(
        "模拟街机 CRT 的扫描线和荧光粉点阵。\n"
        "4K 下最像真机；1080p 建议只开扫描线。");

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
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo(Tr("推荐配置##PostPreset"), &selection,
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
            "扫描线和点阵都是周期图案，周期不成整数倍时会打拍子，\n"
            "出现彩色横带。这里列出的组合不会。");

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
            ImGui::SetNextItemWidth(220.0f);
            static const char* maskTr[3];
            if (ImGui::Combo(Tr("点阵类型##PostMaskType"), &maskType, UiItems(maskItems, 3, maskTr), 3))
                config.Set("PostMaskType", maskType);
            HelpMarker(
                "荧光粉的排列方式。世嘉街机用「槽孔式」，想还原原机选它。");

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
                "点阵和扫描线会挡光、让画面变暗，这里补回来。\n"
                "觉得太暗就往上调。");
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
            "光晕铺开的范围。级数越多、光晕越大越淡。\n"
            "4K 用 5，1080p 用 4。");

        int threshold = config["PostGlowThreshold"].ValueAsDefault<int>(40);
        ImGui::SetNextItemWidth(220.0f);
        if (UiSliderInt("辉光阈值##PostGlowThreshold", &threshold, 0, 100, "%d%%"))
            config.Set("PostGlowThreshold", threshold);
        HelpMarker(
            "多亮的地方才会发光。调高更干净，\n"
            "调低整片画面会被抬亮、发蒙。");
    }

    if (!vulkan)
        ImGui::EndDisabled();

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
    else if (std::filesystem::is_directory(PathFromUtf8(FileSystemPath::ResolvePath(fileRom))))
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

static void GUI(const ImGuiIO& io, Util::Config::Node& config, const std::map<std::string, Game>& games, int& selectedGameIndex, bool& exit, bool& saveSettings, SDL_Window* window, std::shared_ptr<CInputs>& inputs, KeyBindState& kb)
{
    const ImVec4 clear_color = ImVec4(0.965f, 0.957f, 0.937f, 1.00f); // warm paper, matches the theme
    const std::string romPath = config["ROMPath"].ValueAs<std::string>();
    RefreshRoms(romPath, games);
    if (selectedGameIndex >= (int)g_roms.size())
        selectedGameIndex = -1;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
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
    const float buttonHeight = ImGui::GetFrameHeight();
    const float statusHeight = ImGui::GetTextLineHeight();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float footer = buttonHeight + statusHeight + gap * 2.0f;
    const float topY = ImGui::GetCursorPosY();
    const float topX = ImGui::GetCursorPosX();
    const float available = ImGui::GetContentRegionAvail().y;
    const float columnWidth = ImGui::GetContentRegionAvail().x * 0.52f - gap * 0.5f;
    const float panelHeight = available - footer;

    static char romBuffer[1024] = {};
    static std::string romShadow;
    if (romPath != romShadow)
    {
        romShadow = romPath;
        std::snprintf(romBuffer, sizeof(romBuffer), "%s", romPath.c_str());
    }

    // ROM row. Kept in a group so its height can be measured and the list placed below it.
    ImGui::BeginGroup();
    const float browseWidth = ImGui::CalcTextSize(Tr("浏览")).x + ImGui::GetStyle().FramePadding.x * 4.0f;
    ImGui::SetNextItemWidth(columnWidth - browseWidth - gap);
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
        std::string picked = FileSystemPath::ResolvePath(romPath);
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
    // much of the column the preview takes, and resizing the window then scales both parts
    // proportionally. The bounds keep either side from collapsing, and follow the interface scale
    // the same way the style does (LoadUiFont() uses 18 px as the unscaled size).
    const float uiScale = (std::max)(1.0f, ImGui::GetFontSize() / 18.0f);
    if (g_previewSplit < 0.0f)
    {
        g_previewSplit = (std::max)(0.10f, (std::min)(0.90f,
            config["GuiPreviewSplit"].ValueAsDefault<float>(0.42f)));
    }
    const float splitHeight = (std::max)(1.0f, panelHeight - romRowHeight - gap * 2.0f);
    const float minPreviewHeight = (std::min)(150.0f * uiScale, splitHeight * 0.45f);
    const float minListHeight = (std::min)(140.0f * uiScale, splitHeight * 0.45f);
    float previewHeight = g_previewSplit * splitHeight;
    previewHeight = (std::max)(minPreviewHeight, (std::min)(splitHeight - minListHeight, previewHeight));
    g_previewSplit = previewHeight / splitHeight;

    const float previewY = topY + romRowHeight + gap;
    const float listY = previewY + previewHeight + gap;
    const float listHeight = splitHeight - previewHeight;

    int hoveredRow = -1;
    // The list is drawn before the preview even though it sits below it, because the preview shows
    // whatever this frame's hit test found. Drawing it first avoids a one-frame lag on hover.
    ImGui::SetCursorPos(ImVec2(topX, listY));
    ImGui::BeginChild("GameList", ImVec2(columnWidth, listHeight), ImGuiChildFlags_Borders);
    static char filter[128] = {};
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", Tr("搜索游戏"), filter, sizeof(filter));
    const std::string needle = ToLowerAscii(filter);

    if (romPath.empty())
        UiText("请选择 ROM 目录。");
    else if (!std::filesystem::is_directory(PathFromUtf8(FileSystemPath::ResolvePath(romPath))))
        UiText("这个目录不存在。");
    else if (g_roms.empty())
        UiText("目录里没有 .zip。");
    else
    {
        // The whole remaining height goes to the table: the ROM count that used to reserve a line
        // below it now lives in the footer status line.
        const float tableHeight = (std::max)(80.0f, ImGui::GetContentRegionAvail().y);
        if (ImGui::BeginTable("Games", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0, tableHeight)))
        {
            // The name column carries the version too, so it needs the lion's share of the width.
            UiColumn("游戏", 0, 3.0f);
            UiColumn("ROM", 0, 1.6f);
            UiColumn("年份", ImGuiTableColumnFlags_WidthFixed, 56.0f);
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
                const bool dimRow = !entry.supported;
                if (dimRow)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.490f, 0.475f, 0.447f, 1.00f));
                if (ImGui::Selectable(rowLabel.c_str(), selectedGameIndex == row, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                    selectedGameIndex = row;
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
                        DrawWrappedTooltip(Tr("Supermodel 的 Games.xml 里没有这个 ROM 集的定义，无法启动。\n同系列的其他版本可能已经在列表里了。"));
                }
                if (rowHovered && ImGui::IsMouseDoubleClicked(0))
                {
                    selectedGameIndex = row;
                    saveSettings = true;
                    exit = true;
                }
                ImGui::TableSetColumnIndex(1);
                UiText(entry.setName.c_str());
                ImGui::TableSetColumnIndex(2);
                if (entry.year > 0)
                    ImGui::Text("%d", entry.year);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    const int previewIndex = hoveredRow >= 0 ? hoveredRow : selectedGameIndex;
    const RomEntry* previewEntry =
        (previewIndex >= 0 && previewIndex < (int)g_roms.size()) ? &g_roms[previewIndex] : nullptr;
    ImGui::SetCursorPos(ImVec2(topX, previewY));
    DrawGamePreview(previewEntry, columnWidth, previewHeight);

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
    ImGui::SetCursorPos(ImVec2(topX, previewY + previewHeight + (gap - handleHeight) * 0.5f));
    ImGui::InvisibleButton("##PreviewSplitter", ImVec2(columnWidth, handleHeight));
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
        const float gripWidth = (std::min)(columnWidth * 0.12f, 48.0f * uiScale);
        const ImU32 colour = ImGui::GetColorU32(splitterActive ? ImGuiCol_SeparatorActive
            : (splitterHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
        draw->AddLine(ImVec2(centreX - gripWidth, centreY), ImVec2(centreX + gripWidth, centreY),
            colour, (std::max)(2.0f, 2.0f * uiScale));
    }

    // Right column: back up to the top of the content area, which is what moves the tab bar up past
    // the ROM row. The width is left as "the rest" so the two columns always meet the right edge.
    ImGui::SetCursorPos(ImVec2(topX + columnWidth + gap, topY));
    /*
     * Always reserve the vertical scrollbar.
     *
     * The scrollbar is part of this child's width, so letting it appear and disappear with the
     * selected tab moved everything at the right edge - including the tab bar's scroll arrows,
     * which then jumped sideways between tabs and made the same spot stop being the same control.
     * Being permanently present keeps the content width, the tabs and their arrows fixed across
     * tab switches. An unused scrollbar is a small price for a stable target.
     */
    ImGui::BeginChild("Settings", ImVec2(0, panelHeight), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    /*
     * Shrink the tabs to fit rather than scrolling them.
     *
     * With the scroll policy, selecting one of the last tabs scrolled the bar and left the first
     * tab half cut off at the leading edge - a blank rounded block, which reads as a rendering
     * fault. Seven tabs fit here once narrowed to TabMinWidthShrink; Mixed only falls back to
     * scrolling if the window is made narrower than that.
     */
    if (ImGui::BeginTabBar("SettingsTabs", ImGuiTabBarFlags_FittingPolicyMixed))
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

            ImGui::EndTabItem();
        }
        if (UiTabItem("画面"))
        {
            UpdateTempValues(config, "Video", true);
            CreateControls(config, "Video");
            UpdateTempValues(config, "Video", false);

            // After the write-back above, deliberately: UpdateTempValues(..., false) copies every
            // Video key out of the temp-value store it snapshotted a moment ago, so anything this
            // section sets before that point would be silently reverted to its pre-edit value.
            DrawBezelSettings(config, window);

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
            CreateControls(config, "ForceFeedback");

            UiSep("灵敏度");
            CreateControls(config, "Sensitivity");

            UpdateTempValues(config, "ForceFeedback", false);
            UpdateTempValues(config, "Sensitivity", false);

            ImGui::Spacing();
            if (UiButton("校准摇杆"))
            {
                if (inputs == nullptr)
                    inputs = GetInputSystem(config, window);
                if (inputs)
                {
                    inputs->CalibrateJoysticks();
                    inputs->StoreToConfig(&config);
                }
            }
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
        const float evenShare = (ImGui::GetContentRegionAvail().x - gap * (footerCount - 1)) / (float)footerCount;

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
        // wherever they happened to end.
        const float buttonsY = topY + available - footer + gap * 0.5f;
        const float statusY = topY + available - statusHeight - gap * 0.5f;
        ImGui::SetCursorPos(ImVec2(topX, buttonsY));

        const ImVec2 buttonSize(buttonWidth, 0);

        if (UiButton(footerLabels[0], buttonSize))
        {
            if (selectedGameIndex < 0)
                ImGui::OpenPopup("NoGame");
            else
            {
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
        ImGui::SameLine();
        if (UiButton(footerLabels[3], buttonSize))
            ImGui::OpenPopup("ConfirmDefaults");
        ImGui::SameLine();
        if (UiButton(footerLabels[4], buttonSize))
            ImGui::OpenPopup("ConfirmExit");

        // The ROM count lives on the reserved line below the buttons rather than inside the list,
        // where it cost the table a row. A transient save/load note shares the same line, to its
        // right. The line is reserved whether or not the note is showing, so nothing moves.
        ImGui::SetCursorPos(ImVec2(topX, statusY));
        UiTextDim("找到 %d 个 ROM", (int)g_roms.size());
        if (ImGui::GetTime() - g_statusAt < 3.0 && !g_statusNote.empty())
        {
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
            UiTextDim("%s", g_statusNote.c_str());
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

    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static float GetDPIScale(SDL_Window* window)
{
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    float ddpi = 96.0f;
    if (SDL_GetDisplayDPI(displayIndex, &ddpi, nullptr, nullptr) != 0 || ddpi < 1.0f)
        ddpi = 96.0f;
    return ddpi / 96.0f;
}

void ApplyUiTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    /*
     * Warm paper rather than pure white.
     *
     * Pure white is the obvious light-theme choice and the wrong one for a window someone keeps
     * open in a dim room next to a running game: it glares, and every grey border becomes a hard
     * line. A very slightly warm off-white lowers the brightness without tinting the game artwork
     * shown alongside it, and white input fields still read as raised.
     */
    const ImVec4 paper      = ImVec4(0.965f, 0.957f, 0.937f, 1.00f); // #F6F4EF
    const ImVec4 surface    = ImVec4(0.984f, 0.980f, 0.965f, 1.00f); // #FBFAF6
    const ImVec4 popup      = ImVec4(0.992f, 0.988f, 0.973f, 1.00f); // #FDFCF8
    const ImVec4 field      = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
    const ImVec4 ink        = ImVec4(0.165f, 0.157f, 0.141f, 1.00f); // #2A2824
    const ImVec4 inkDim     = ImVec4(0.490f, 0.475f, 0.447f, 1.00f); // #7D796F
    const ImVec4 line       = ImVec4(0.867f, 0.847f, 0.808f, 1.00f); // #DDD8CE
    const ImVec4 lineSoft   = ImVec4(0.906f, 0.890f, 0.855f, 1.00f); // #E7E3DA
    const ImVec4 accent     = ImVec4(0.102f, 0.388f, 0.780f, 1.00f); // #1A63C7
    const ImVec4 accentSoft = ImVec4(0.890f, 0.929f, 0.988f, 1.00f); // #E3EDFC
    const ImVec4 hover      = ImVec4(0.941f, 0.933f, 0.914f, 1.00f); // #F0EEE9
    const ImVec4 pressed    = ImVec4(0.902f, 0.890f, 0.863f, 1.00f); // #E6E3DC
    const ImVec4 button     = ImVec4(0.933f, 0.922f, 0.898f, 1.00f); // #EEEBE5

    c[ImGuiCol_Text]                  = ink;
    c[ImGuiCol_TextDisabled]          = inkDim;
    c[ImGuiCol_WindowBg]              = paper;
    c[ImGuiCol_ChildBg]               = surface;
    c[ImGuiCol_PopupBg]               = popup;
    c[ImGuiCol_Border]                = line;
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]               = field;
    c[ImGuiCol_FrameBgHovered]        = hover;
    c[ImGuiCol_FrameBgActive]         = pressed;
    c[ImGuiCol_TitleBg]               = paper;
    c[ImGuiCol_TitleBgActive]         = paper;
    c[ImGuiCol_TitleBgCollapsed]      = paper;
    c[ImGuiCol_MenuBarBg]             = paper;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.788f, 0.769f, 0.729f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.706f, 0.686f, 0.643f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.078f, 0.310f, 0.639f, 1.00f);
    c[ImGuiCol_Button]                = button;
    c[ImGuiCol_ButtonHovered]         = accentSoft;
    c[ImGuiCol_ButtonActive]          = ImVec4(0.831f, 0.886f, 0.973f, 1.00f);
    c[ImGuiCol_Header]                = accentSoft;
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.851f, 0.902f, 0.976f, 1.00f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.800f, 0.867f, 0.961f, 1.00f);
    c[ImGuiCol_Separator]             = line;
    c[ImGuiCol_SeparatorHovered]      = ImVec4(0.510f, 0.659f, 0.878f, 1.00f);
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ResizeGripHovered]     = accentSoft;
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_InputTextCursor]       = ink;
    c[ImGuiCol_TabHovered]            = accentSoft;
    c[ImGuiCol_Tab]                   = button;
    c[ImGuiCol_TabSelected]           = surface;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = ImVec4(0.949f, 0.941f, 0.922f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]     = surface;
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.710f, 0.686f, 0.635f, 1.00f);
    c[ImGuiCol_PlotLines]             = accent;
    c[ImGuiCol_PlotLinesHovered]      = ImVec4(0.078f, 0.310f, 0.639f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = accent;
    c[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.078f, 0.310f, 0.639f, 1.00f);
    c[ImGuiCol_TableHeaderBg]         = ImVec4(0.941f, 0.933f, 0.914f, 1.00f);
    c[ImGuiCol_TableBorderStrong]     = line;
    c[ImGuiCol_TableBorderLight]      = lineSoft;
    c[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(0.165f, 0.157f, 0.141f, 0.035f);
    c[ImGuiCol_TextLink]              = accent;
    c[ImGuiCol_TextSelectedBg]        = ImVec4(0.812f, 0.878f, 0.973f, 1.00f);
    c[ImGuiCol_TreeLines]             = line;
    c[ImGuiCol_DragDropTarget]        = accent;
    c[ImGuiCol_DragDropTargetBg]      = ImVec4(0.102f, 0.388f, 0.780f, 0.15f);
    c[ImGuiCol_UnsavedMarker]         = accent;
    c[ImGuiCol_NavCursor]             = accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(0.102f, 0.388f, 0.780f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.100f, 0.090f, 0.080f, 0.20f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.100f, 0.090f, 0.080f, 0.28f);

    // Airy but not loose: more spacing than ImGui's defaults, which are tuned for dense tool UIs.
    style.WindowPadding = ImVec2(20.0f, 18.0f);
    style.FramePadding = ImVec2(12.0f, 7.0f);
    style.CellPadding = ImVec2(10.0f, 8.0f);
    style.ItemSpacing = ImVec2(12.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 12.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 0.0f;
    style.TabBarOverlineSize = 2.0f;

    style.WindowRounding = 14.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 12.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 9.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(0.0f, 8.0f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
    style.DisabledAlpha = 0.45f;
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
 * Asks Fontconfig for a Simplified Chinese sans-serif face.
 *
 * The short list below covers the usual Noto and WenQuanYi locations, but a distribution is free to
 * install its CJK fonts anywhere. Fontconfig already knows where they are - it is what the desktop
 * itself uses - so querying it catches those layouts without linking against Fontconfig or
 * shipping a font. Returns an empty string when fc-match is not installed or finds nothing.
 */
static std::string FindCjkFontWithFontconfig()
{
    FILE* pipe = popen("fc-match -f '%{file}' 'sans-serif:lang=zh-cn' 2>/dev/null", "r");
    if (pipe == nullptr)
        return {};

    std::string path;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        path += buffer;

    if (pclose(pipe) != 0)
        return {};

    while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' '))
        path.pop_back();
    return path;
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
    const char* paths[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
        "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc",
    };
#endif

    const ImWchar* ranges = UiGlyphRanges(io.Fonts);

    auto addFont = [&](const char* path) -> bool
    {
        if (path == nullptr || path[0] == '\0' || !std::filesystem::exists(path))
            return false;

        ImFontConfig cfg;
        cfg.FontNo = 0;              // first face in a .ttc; PingFang.ttc starts with SC Regular
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = false;
        return io.Fonts->AddFontFromFileTTF(path, size, &cfg, ranges) != nullptr;
    };

    for (const char* path : paths)
        if (addFont(path))
            return;

#if !defined(_WIN32) && !defined(__APPLE__)
    // Tried last: a known-good Noto/WenQuanYi path wins, and an absent Fontconfig just returns an
    // empty string. This is what catches a distribution that installs its CJK fonts elsewhere.
    const std::string fontconfigFont = FindCjkFontWithFontconfig();
    if (addFont(fontconfigFont.c_str()))
        return;
#endif

    io.Fonts->AddFontDefaultVector();
}

struct GuiState
{
    GuiState() : fullConfig("Global"), config("Global") {}

    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    std::string configPath;
    Util::Config::Node fullConfig;
    Util::Config::Node config;
    // The loader owns the game list, so it has to outlive a single GuiRunOnce() call.
    std::unique_ptr<GameLoader> loader;
    int selectedGame = -1;
    std::shared_ptr<CInputs> inputs;
    KeyBindState kb{};
    bool saveSettings = true;
};

GuiState* GuiCreate(const std::string& configPath, const Util::Config::Node& fileConfig)
{
    std::unique_ptr<GuiState> state(new GuiState());
    state->configPath = configPath;
    g_settingsPath = configPath;        // seeds the "Save as" dialog with the file in use
    state->fullConfig = fileConfig;
    Util::Config::MergeINISections(&state->config, DefaultConfig(), state->fullConfig);

    /*
     * A settings file written before this build recorded the old Windows default, "C:/Downloads".
     * On macOS that is a relative path that does not exist, so the game list started out empty and
     * the reason was invisible. Replace that one value - and only that one - with the ROMs folder
     * next to the executable; a path the user chose deliberately is left alone even when the volume
     * is temporarily unavailable.
     */
#if defined(__APPLE__)
    {
        const std::string configured = state->config["ROMPath"].ValueAsDefault<std::string>("");
        std::string trimmed = configured;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
            trimmed.pop_back();
        const std::string lower = ToLowerAscii(trimmed);
        // A drive-letter path can never name anything on this platform. Treating it as a stale
        // setting covers both the old stock default (C:/Downloads) and a configuration file copied
        // over from a Windows machine.
        const bool windowsDrivePath = lower.size() >= 2 && lower[1] == ':' &&
            lower[0] >= 'a' && lower[0] <= 'z';
        std::error_code error;
        const bool usable = !trimmed.empty() &&
            std::filesystem::is_directory(PathFromUtf8(FileSystemPath::ResolvePath(trimmed)), error) && !error;
        if (!usable && (trimmed.empty() || windowsDrivePath))
        {
            const std::string fallback = FileSystemPath::GetDefaultRomPath();
            if (!fallback.empty())
            {
                state->config.Set("ROMPath", fallback);
                // Also correct the copy that is written back when the window is merely closed, so
                // the repair survives even without an explicit save.
                state->fullConfig.Set("ROMPath", fallback);
            }
        }
    }
#endif

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "system");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL could not initialize! Error: " << SDL_GetError() << std::endl;
        return nullptr;
    }

#ifdef __APPLE__
    // SDL has created NSApplication by now. The interface is always the light theme, so make the
    // native title bar and the file pickers light as well instead of following Dark Mode.
    OSXFileDialogs::ApplyLightAppearance();
#endif

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_INPUT_FOCUS);

    // Default window size, in client-area pixels (the title bar and borders are added on top, so
    // the frame ends up a little larger). 1280x800 fits comfortably on a 1080p screen without
    // needing to be resized on first run.
    int windowWidth = 1280;
    int windowHeight = 800;
    {
        int displayIndex = 0;
        // Query the display DPI up front so the diagnostic line below always reports the real
        // value, even when a saved size means the scaling is not used.
        float ddpi = 96.0f;
        float ddpiScale = 1.0f;
        if (SDL_GetDisplayDPI(displayIndex, &ddpi, nullptr, nullptr) == 0 && ddpi >= 1.0f)
            ddpiScale = ddpi / 96.0f;

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
        const int minWidth = 640;
        const int minHeight = 480;
        if (windowWidth < minWidth) windowWidth = minWidth;
        if (windowHeight < minHeight) windowHeight = minHeight;

        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(displayIndex, &usable) == 0)
        {
            if (windowWidth > usable.w) windowWidth = usable.w;
            if (windowHeight > usable.h) windowHeight = usable.h;
        }

        // Record how the size was arrived at. Window geometry problems are otherwise hard to
        // diagnose remotely, and this shows at a glance whether DPI scaling or the work-area clamp
        // is responsible for a size the user did not expect.
        InfoLog("GUI window: %dx%d (saved %dx%d, display DPI scale %.2f, usable area %dx%d).",
            windowWidth, windowHeight, savedWidth, savedHeight, (float)ddpiScale, usable.w, usable.h);
    }

    state->window = SDL_CreateWindow(SUPERMODEL_TITLE_FULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, window_flags);
    if (!state->window) {
        std::cerr << "Window could not be created! Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return nullptr;
    }
    // Has to come after creation: SDL installs its own default icon while creating the window.
    SetWindowIcon(state->window);

    state->glContext = SDL_GL_CreateContext(state->window);
    if (!state->glContext) {
        std::cerr << "OpenGL context could not be created! Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(state->window);
        SDL_Quit();
        return nullptr;
    }

    SDL_GL_MakeCurrent(state->window, state->glContext);
    SDL_GL_SetSwapInterval(1);
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

    ImGui_ImplSDL2_InitForOpenGL(state->window, state->glContext);
    ImGui_ImplOpenGL3_Init("#version 410");

    std::string xmlFile = ResolveDataFileSetting(state->config["GameXMLFile"].ValueAs<std::string>(), "Games.xml");
    state->loader.reset(new GameLoader(xmlFile));

    // Reset caches that must not survive a restart of the interface.
    g_writeSettings = false;
    g_romFolderCached.clear();
    g_roms.clear();

    return state.release();
}

std::vector<std::string> GuiRunOnce(GuiState* state)
{
    std::vector<std::string> romFiles;
    if (!state || !state->window)
        return romFiles;

    // The game session leaves its own GL context current (or none at all), so re-establish ours
    // before touching any ImGui/GL state.
    SDL_GL_MakeCurrent(state->window, state->glContext);
    RaiseSettingsWindow(state->window);
    // Make sure the pointer is usable when the window comes back. ImGui's gamepad support used to
    // hide it, and that is disabled now.
    //
    // This alone would not be enough. SDL_ShowCursor only moves SDL's own visibility flag, and a
    // fullscreen game hides the pointer through the Win32 display count instead (see
    // CDirectInputSystem::SetMouseVisibility), which SDL cannot influence. RunGameSession restores
    // that count once the game's session ends, which is where it is safe to reach the input system.
    SDL_ShowCursor(SDL_ENABLE);

    // A pad may have been plugged in while the game was running; make sure it is picked up.
    g_controllerDetected = false;

    ImGuiIO& io = ImGui::GetIO();
    auto& games = state->loader->GetGames();

    bool running = true;
    bool exit = false;
    bool closed = false;
    SDL_Event event{};
    while (running) {

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                // Closing the GUI window ends the application; settings are still written out
                // unless the user was mid-edit.
                closed = true;
                running = false;
                break;
            }
            // Plugging or unplugging a pad invalidates the cached detection; ImGui's backend rescans
            // on these events too, so navigation picks the device up without a restart.
            if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED)
                g_controllerDetected = false;
        }
        if (closed)
            break;

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
        SDL_GL_SwapWindow(state->window);

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
    SDL_GetWindowSize(state->window, &windowWidth, &windowHeight);

    if (!closed && state->saveSettings) {
        if (windowWidth > 0 && windowHeight > 0) {
            state->config.Set("GuiWindowWidth", windowWidth);
            state->config.Set("GuiWindowHeight", windowHeight);
        }
        if (g_previewSplit > 0.0f)
            state->config.Set("GuiPreviewSplit", g_previewSplit);
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
        Util::Config::WriteINIFile(state->configPath, state->fullConfig, TimestampedConfigFileComment("Updated window size."));
    }

    // Hand the screen back to the game. The window is hidden rather than destroyed so that the
    // interface is still there when the game is quit.
    if (!closed)
    {
        SDL_GL_MakeCurrent(state->window, nullptr);
        SDL_HideWindow(state->window);
    }

    return romFiles;
}

void GuiDestroy(GuiState* state)
{
    if (!state)
        return;

    if (state->window)
    {
        SDL_GL_MakeCurrent(state->window, state->glContext);
        ReleaseBoxartTextures();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(state->glContext);
        SDL_DestroyWindow(state->window);
    }

    delete state;
}
