#ifndef _GUI_H_
#define _GUI_H_

#include <string>
#include <vector>

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
 * Applies the shared interface theme: the warm off-white palette, rounded corners and spacing used
 * by both the settings window and the in-game overlay.
 *
 * Called on each ImGui context after it is created, before the font atlas is built. Keeping it in
 * one function is what makes the two surfaces look like the same program - the overlay used to call
 * StyleColorsDark() separately, which meant a change here would have left it dark.
 */
void ApplyUiTheme();

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

#endif // !_GUI_H_
