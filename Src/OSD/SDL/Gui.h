#ifndef _GUI_H_
#define _GUI_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Gui.h exposes two small drawing helpers to the in-game overlay (see UiFrost below), and those
// take ImDrawList and ImTextureID. Only three translation units include this header, two of which
// already pull in the whole of ImGui, so the include costs nothing worth avoiding.
#include "../Pkgs/imgui/imgui.h"

// Declared rather than included: Gui.h is pulled in by files that have no business seeing the whole
// of ImGui, and both uses below are references.
struct ImGuiIO;

namespace Util
{
  namespace Config
  {
    class Node;
  }
}

// Persistent settings GUI.
//
// The GUI is the application shell rather than something created and thrown away around each
// game: the window is created once and stays alive, so quitting a game (Esc) returns to the
// settings screen instead of exiting Supermodel, as other emulator front-ends do.
//
//   GuiCreate()   creates the window, GL context and ImGui state. Returns null on failure.
//   GuiRunOnce()  runs the interface until the user starts a game or closes the window. Returns
//                 the game to run, or an empty vector if the window was closed (the caller should
//                 then shut the application down).
//   GuiDestroy()  releases everything GuiCreate() allocated.
//
// fileConfig is the complete contents of the config file; only its global section is edited, all
// other sections are preserved when the file is written back out.
struct GuiState;

GuiState* GuiCreate(const std::string& configPath, const Util::Config::Node& fileConfig);
std::vector<std::string> GuiRunOnce(GuiState* state);
void GuiDestroy(GuiState* state);

/*
 * Queues a message to be shown in the settings window the next time it is drawn.
 *
 * Exists because the settings window is hidden while a game runs and the message that matters most is
 * produced there: a renderer that failed to start and was silently replaced. Writing that only to the
 * log would hide it from exactly the user who needs it - the one whose GPU could not do Vulkan and who
 * will keep hitting the problem until they change the setting.
 *
 * The message is shown next to the renderer selection because that is where the cure is. It stays
 * until the user changes the renderer or dismisses it, so a transient look away does not lose it.
 *
 * Safe to call when no settings window exists (command-line runs): the text is simply never drawn.
 */
void GuiPostNotice(const std::string& text);

/*
 * Consumes the .zst save the user picked in Manage Saves, if any.
 *
 * The settings window cannot start the game itself; it returns the ROM path and
 * this pending state to the caller, which extracts core.zst and loads it after
 * the emulator is created.  The value is cleared on read, so a failed load does
 * not silently retry on the next session.
 */
std::filesystem::path GuiTakePendingVkState();

/*
 * Pieces of the settings interface the in-game overlay reuses.
 *
 * Declared rather than duplicated: the overlay needs the same font (the interface is bilingual, so
 * the CJK paths and the DPI scaling have to match) and the same body of controls, and a second copy
 * of either would drift. Both are written against a plain ImGui context, so they work in the
 * overlay's own context as well as the settings window's.
 */

// Adds a CJK-capable font to the given atlas, scaled for the display's DPI. Falls back to ImGui's
// built-in font when none of the system fonts can be loaded.
void LoadUiFont(ImGuiIO& io, float scale);

/*
 * Decodes PNG/WebP bytes into tightly packed RGBA8 pixels. Exposed for the in-game save-state menu,
 * which reads thumb.png straight out of the .zst container and cannot see Gui.cpp's file-local
 * helper. WIC/ImageIO/libwebp do the actual decoding, so the caller does not need a decoder of its
 * own. Returns true on success; width and height describe the decoded image.
 */
bool GuiDecodeImageBytes(const uint8_t* data, size_t size, int maxWidth,
    std::vector<uint8_t>& pixels, int& width, int& height);

/*
 * Applies the shared interface theme: the palette, rounded corners and spacing used by both the
 * settings window and the in-game overlay.
 *
 * Called on each ImGui context after it is created, before the font atlas is built. Keeping it in
 * one function is what makes the two surfaces look like the same program - the overlay used to call
 * StyleColorsDark() separately, which meant a change here would have left it dark.
 *
 * Reads the theme selected by SetUiTheme(), so it has to be called *after* that. Resets every size to
 * its unscaled value first, which is what makes it safe to call again from RescaleUi().
 */
void ApplyUiTheme();

/*
 * Interface theme.
 *
 * Three palettes, all tuned to be looked at for a long time next to a running arcade game rather than
 * for a screenshot:
 *
 *   WarmPaper (0) - light, and the default. Warm alabaster panes with terracotta accents and charcoal
 *                   ink, over a frosted backdrop. Chosen as the default because the launcher is a list
 *                   that gets browsed, and a dark pane is harder to sit in front of for that.
 *   WarmDark  (1) - warm rock-grey glass with an amber accent, for a dim room or a very bright game.
 *   Classic   (2) - the interface exactly as it was before the frosted-glass rework: flat opaque warm
 *                   paper, blue accent, ImGui child borders, tighter spacing, no backdrop and no
 *                   blur. Kept as a real theme, not a hidden switch, because the rework changed the
 *                   look of every page and somebody who preferred the old one has to be able to get
 *                   it back.
 *
 * No theme uses pure black or pure white. The theme is read whenever ApplyUiTheme() runs, so a change
 * takes effect immediately in the settings window and at the start of the next game session in the
 * overlay.
 */
enum class UiTheme
{
	WarmPaper = 0,
	WarmDark = 1,
	Classic = 2,
};

void SetUiTheme(UiTheme theme);
UiTheme GetUiTheme();
// True for the dark palette. Used where a colour cannot come from the style (the glass rim, the
// ambient backdrop wash, the frosted-backdrop grade) and has to be picked by hand.
bool UiThemeIsDark();
/*
 * True for the pre-rework interface.
 *
 * The layout code has to know this: that theme is the only one with no ambient backdrop, no glass
 * plates and no extra pane inset, because those are what the rework added. Everything else keys off
 * style colours and sizes, which ApplyUiTheme() already sets per theme.
 */
bool UiThemeIsClassic();

/*
 * Frosted-glass panels.
 *
 * A plate is the glass itself: a blurred copy of whatever is behind the panel, a translucent tint
 * over it, and a 1px specular rim along the top edge. The blurred backdrop is produced by the
 * renderer (Dual Kawase, see Graphics/Vulkan3D/UiBlur.h) and is passed in as an ordinary ImGui
 * texture, so the only thing this has to know is where the panel is on screen - the texture is
 * sampled with screen-mapped uv, which is what makes one backdrop serve every panel at once.
 *
 * Everything is drawn into the panel's own draw list before its contents, so no state has to be
 * saved or restored around it.
 */
namespace UiFrost
{
	/*
	 * Which kind of surface a plate is.
	 *
	 * Glass is the main sheet; Well is a recessed container inside glass (no backdrop, no rim, just a
	 * darker tint) and Popup is a floating panel, which gets a stronger tint because it covers content
	 * rather than sitting over the game.
	 */
	enum class Surface
	{
		Glass,
		Well,
		Popup,
	};

	/*
	 * Draws one plate into `draw`.
	 *
	 * `backdrop` is the blurred frame texture, or 0 when there is none (the OpenGL renderer, a GPU that
	 * could not build the chain, or the first frame after a resize). A glass plate then falls back to a
	 * solid tint, which is legible if less pretty - the reason this is a fallback rather than a failure.
	 *
	 * The rectangle is in ImGui screen coordinates and is expected to be a window's outer rect, so the
	 * caller should push a clip rect when the current one is the padded inner rect.
	 */
	void Plate(ImDrawList* draw, ImTextureID backdrop, const ImVec2& pMin, const ImVec2& pMax,
		float rounding, Surface surface, float alpha = 1.0f);

	/*
	 * Ambient backdrop wash colours for the settings window, for whichever renderer paints it.
	 *
	 * Returned rather than applied because the two backends want different things: the Vulkan one
	 * feeds these to a shader that samples the cover, and the OpenGL one hands the cover to ImGui as
	 * an image. Keeping the numbers here means the two cannot drift apart.
	 */
	struct AmbientWash
	{
		ImVec4 tint;            // rgb = wash colour, a = how far the cover is pulled toward it
		float saturation;       // multiplier on the cover's saturation before the wash
		float exposure;         // multiplier on the cover's brightness
		float vignette;         // corner darkening, 0..1
	};
	AmbientWash AmbientColours();
}

/*
 * Hover transition for one control.
 *
 * ImGui switches colours the instant the pointer arrives, which on a large panel reads as a flicker
 * rather than as feedback. These two functions turn it into a 120 ms fade: Begin() returns the weight
 * to use for this frame's colours, End() records whether the widget was actually hovered so the next
 * frame can move toward it. The one-frame delay is invisible next to a fade this long and is what
 * makes the trick possible without a shader - ImGui's own hover colours cannot be interpolated,
 * because by the time the answer is known the colours have already been written.
 *
 * `label` must be the untranslated label (the same string the widget is created from, including its
 * ##id), so the key stays stable when the interface language changes.
 */
float UiHoverBegin(const char* label);
void UiHoverEnd(const char* label, bool hovered);
// Blends between two colours by the weight Begin() returned. Component-wise, alpha included.
ImVec4 UiHoverBlend(const ImVec4& base, const ImVec4& hot, float weight);

/*
 * Draws the advanced rendering settings (MSAA, CRT filter, glow).
 *
 * overlayMode selects the presentation for the in-game panel: the long explanatory text and the (?)
 * help markers are dropped, because there is no room for them over a running game and no need for
 * them - the panel exists to be dragged around while watching the picture.
 *
 * MSAA is omitted in overlayMode: its sample count is fixed when the render targets are created, so
 * it cannot change without a restart and would be a control that does nothing.
 *
 * The scanline/mask beat-frequency readout is kept in both modes. It is not an explanation but a live
 * measurement, and it is what stops someone from tuning their way into colour banding.
 */
void DrawAdvancedSettingsPanel(Util::Config::Node& config, bool overlayMode);

/*
 * HD texture pack switches and, in the in-game panel, the reload button.
 *
 * Kept out of DrawAdvancedSettingsPanel because these are not rendering options that need a new
 * session: both switches are re-read by the renderer every simulation frame, and the reload button
 * applies immediately. The function draws its own section heading.
 */
void DrawHDTextureSettings(Util::Config::Node& config, bool overlayMode);

/*
 * CRT colour and 2D upscaling, shared by the settings window's Video page and the top of the
 * in-game panel.
 *
 * Both are live under Vulkan: the post pass re-reads the colour preset every frame and the
 * compositor re-reads the upscale mode every frame, so dragging them in the overlay changes the
 * picture immediately. OpenGL builds its colour matrix and tile shader once per session, so there
 * they still need a new game - the help text says so, and the overlay adds a line when the OpenGL
 * renderer is the one running.
 */
void DrawPictureModeSettings(Util::Config::Node& config, bool overlayMode = false);

#endif // !_GUI_H_
