#ifndef _MAIN_H_
#define _MAIN_H_

#include <string>
#include <vector>

Util::Config::Node DefaultConfig();

/*
 * Applies one of the built-in control presets to the given configuration, writing new
 * assignments for every configurable game input (keyboard keys plus gamepad).
 *
 * preset is one of: "standard", "keyboard". Anything else leaves the mappings untouched
 * (used for "custom", where the user has bound the controls by hand).
 *
 * inputSystem is the InputSystem setting the mappings have to suit. The standardised pad
 * layout is identical for every controller family; only the analogue triggers differ,
 * because legacy DirectInput reports them as one shared axis while XInput and SDL
 * GameController give each trigger its own axis.
 */
void ApplyControlPreset(Util::Config::Node& config, const std::string& preset, const std::string& inputSystem);

/*
 * Description of the first game controller found, for the settings window's preset chooser.
 */
struct ControllerInfo
{
	std::string name;           // human readable device name, empty when none was found
	// "xbox360", "xboxone", "xboxseries", "ps3", "ps4", "ps5", "8bitdo", "generic" or ""
	std::string family;
	std::string inputSystem;    // recommended InputSystem value for this device
	// True when SDL's built-in database has a mapping for the device, so the GameController API
	// normalises its buttons and "sdlgamepad" is the right input system for it. False means SDL
	// only sees a raw joystick and the bindings would have to name raw button numbers.
	bool sdlKnowsDevice = false;
};

ControllerInfo DetectController();

/*
 * Whether the display actually offers a 57/58 Hz mode.
 *
 * The fixed arcade refresh rate needs one to exist, and creating it is a manual step in the driver
 * control panel, so this is false for most setups. The settings window uses it to say so instead of
 * offering a mode that would silently fall back to G-SYNC at startup.
 *
 * Silent and free of side effects, but it enumerates display modes, so callers should cache it.
 */
bool HasArcadeDisplayMode(float refreshRateHz);

/*
 * What each attached display declares about itself, for the settings window's sync notes.
 *
 * Returned as data rather than as a formatted sentence so the caller can put it through the
 * translation table and work out the one conclusion that matters - whether 57.5 Hz falls inside the
 * display's timing range, which is what decides whether a custom resolution for the fixed arcade rate
 * can be created at all. That comparison is trivial and is exactly the kind of arithmetic a reader
 * should not have to do while looking at two numbers.
 *
 * Two facts per display, and they must not be conflated:
 *
 *   minHz / maxHz          the timing range from EDID 0xFD. Every monitor has one. It says what the
 *                          panel will lock to, which answers whether a custom 57.5 Hz resolution would
 *                          be accepted - and says nothing whatsoever about variable refresh.
 *
 *   declaresVrr            a CEA-861 vendor block declaring AMD/VESA Adaptive-Sync was found, and
 *                          vrrMinHz/vrrMaxHz are its range. Present only on displays that announce it.
 *                          When present it is a positive statement that the display can vary its rate;
 *                          when absent it means nothing at all, because a display can support VRR
 *                          without declaring it.
 *
 * Empty when the information is unavailable, in which case the caller omits the line entirely.
 */
struct DisplayTimingInfo
{
	std::string name;           // model name from EDID 0xFC, or "Display" when it gives none
	int minHz = 0;              // vertical timing range, EDID 0xFD
	int maxHz = 0;
	bool declaresVrr = false;   // a declared adaptive-sync range was found
	int vrrMinHz = 0;
	int vrrMaxHz = 0;
};

std::vector<DisplayTimingInfo> GetDisplayTimingInfo();

/*
 * Whether the configured output can scan at a given rate.
 *
 * This is the VRR question, not the fixed-mode question: VRR needs no discrete mode, only a
 * timing range that contains the rate. RateSupportForTiming works on information the caller
 * already has (the settings window caches it); ConfiguredDisplayRateSupport fetches it for
 * callers that do not.
 *
 * Unknown is a distinct answer from Unsupported: a display whose EDID could not be read, or that
 * could not be matched to the configured output, must not be treated as "cannot do 57.5 Hz".
 */
enum class DisplayRateSupport
{
    Unknown,
    Supported,
    Unsupported,
};

DisplayRateSupport RateSupportForTiming(const std::vector<DisplayTimingInfo>& timing, float hz);
DisplayRateSupport ConfiguredDisplayRateSupport(float hz);

/*
 * The outputs SDL can drive, each with the number Windows knows it by.
 *
 * Needed because the two halves of display selection speak different languages. SDL addresses
 * displays by a zero-based index assigned in enumeration order, which is what everything in this
 * file has to pass to SDL_GetDisplayMode/SDL_SetWindowDisplayMode; Windows addresses them as
 * \\.\DISPLAYn, which is the number shown by "Identify" in Display Settings and the one the helper
 * script uses. Neither can be derived from the other, so the mapping is resolved by matching the
 * monitor rectangle SDL reports against the one GetMonitorInfo() reports - the only field both
 * sides agree on.
 *
 * The number is what gets stored in the configuration and shown in the interface: it is stable
 * across the reordering that changes SDL's indices, and it is the number the user can see and can
 * pass to Scripts/ArcadeDisplay.ps1. Zero means no match was found, which is also what a
 * non-Windows build reports - the number is then only ever used for display.
 *
 * hasArcadeMode is the per-display form of HasArcadeDisplayMode(): whether that output offers a
 * 57/58 Hz mode, and therefore whether the fixed arcade rate can be used on it. This is the one
 * question worth asking of a display other than the one in use, because a second monitor is very
 * often the arcade-capable one and the primary is not.
 */
struct DisplayInfo
{
	int sdlIndex = 0;
	int number = 0;             // the n in \\.\DISPLAYn, or 0 when it could not be determined
	std::string name;           // monitor friendly name, or "Display N" when the driver reports none
	std::string deviceName;     // "\\.\DISPLAYn", Windows only; empty elsewhere
	bool primary = false;       // the system's main display, from MONITORINFOF_PRIMARY
	int width = 0;              // desktop size in the coordinate space SDL works in
	int height = 0;             // (the system DPI's space - see ReadPanelMode in Main.cpp)
	int panelWidth = 0;         // the panel's own mode, from the display driver; Windows only
	int panelHeight = 0;
	int desktopHz = 0;
	bool hasArcadeMode = false;
};

/*
 * All attached outputs. Cheap except for the per-display mode search, so the result is cached for a
 * couple of seconds the same way the EDID info is - long enough that the settings window does not
 * re-enumerate several hundred modes per display on every frame, short enough that plugging a
 * monitor in is picked up without restarting.
 */
const std::vector<DisplayInfo>& GetDisplays();

/*
 * The SDL index to use, resolved from the "Display Number" setting.
 *
 * Returns 0 - the primary display, and the behaviour before this setting existed - when the
 * setting is 0 or names an output that is not attached. Falling back rather than failing is
 * deliberate: a configuration saved with a second monitor connected must still start on a laptop
 * that is later used on its own.
 */
int GetConfiguredDisplaySdlIndex();

/*
 * What integer scaling is actually doing on the configured output.
 *
 * The factor is not a setting and cannot be read from one - it is derived from the panel's own mode
 * and nothing else - so the only way to see it is to compute it. Two consequences are surprising
 * enough to be worth stating in the interface:
 *
 *   - The multiple is min(panelWidth/496, panelHeight/384) truncated. On a 3840x2160 panel that is
 *     2160/384 = 5.625, so 5 - not 6, and no 16:9 panel can reach 6 because it would need 2304 rows.
 *   - XResolution/YResolution does not decide it. CalcScreenGeometry overwrites the requested
 *     resolution with 496*n by 384*n, so a "6x" preset chosen in the settings window comes out as 5x
 *     here and nothing says so.
 *
 * Reported as numbers rather than as a formatted sentence so the caller can word it in the active
 * language. valid is false when the panel size could not be read at all, in which case the other
 * fields describe the window instead and should be treated as an estimate.
 */
struct IntegerScaleInfo
{
 bool valid = false;         // the panel size was known, rather than falling back to the window
 bool panelKnown = false;    // true when the size came from the panel and not from the window
 bool active = false;        // IntegerScale is on and no stretch, so the geometry code applies it
 int multiple = 1;
 int visibleWidth = 0;       // 496 * multiple
 int visibleHeight = 0;      // 384 * multiple
 int panelWidth = 0;         // the output the multiple was derived from
 int panelHeight = 0;
 int nextMultiple = 2;
 int nextVisibleHeight = 0;  // 384 * nextMultiple, to say what the next step would need
};

IntegerScaleInfo GetIntegerScaleInfo();

/*
 * Gives a window the application icon in its title bar and taskbar entry.
 *
 * SDL has SDL_SetWindowIcon(), but it takes an SDL_Surface and SDL2 cannot read .ico files, so the
 * icon would have to be shipped a second time as a separate bitmap. Instead this pulls the icon
 * straight out of the executable's own resources - the same one Explorer shows - which keeps a
 * single source of truth for it and adds no files to the distribution.
 *
 * Windows only, and a no-op elsewhere: other platforms get their icon from the bundle or the
 * desktop entry, and SDL's generic path needs the surface form described above.
 */
void SetWindowIcon(SDL_Window* window);

/*
 * Shows or hides the console window.
 *
 * The executable is built for the Windows subsystem, so there is no console unless one is asked for
 * or inherited from a shell - see the comment on the implementation in Main.cpp. Hiding only affects
 * a console this process allocated; a console inherited from the parent shell is left alone, because
 * hiding it would take the user's command prompt with it.
 *
 * Exposed so the settings window's "show console" checkbox goes through the same path, rather than
 * poking the Win32 window directly and getting the ownership rules wrong.
 */
void SetConsoleVisible(bool show);

/*
 * Path to one of the data files that ship beside the executable (Games.xml, Music.xml).
 *
 * These describe the build rather than the user's preferences, so the copy installed next to the
 * executable is authoritative and the setting exists only to let it be moved deliberately (the
 * command line has -game-xml-file for that). Reading the setting blindly is wrong in two ways:
 *
 *   - Settings files written by earlier versions record the path as it was relative to the working
 *     directory, e.g. "Config/Games.xml". Once the data moved under Data\ that names nothing, and
 *     because it is an explicit setting it still overrides the built-in path.
 *   - A settings file carried over from another machine names that machine's location.
 *
 * Either way the file the user needs is sitting next to the executable the whole time, and the
 * failure is hard to attribute from the outside: the window comes up normally and reports every
 * .zip in the ROM folder as unsupported, which reads as a missing or corrupt game list rather than
 * a path that points somewhere else. A configured path is therefore honoured only when it resolves
 * to something that exists, and the substitution is logged.
 */
std::string ResolveDataFileSetting(const std::string& configuredPath, const std::string& fileName);

#endif // _MAIN_H_
