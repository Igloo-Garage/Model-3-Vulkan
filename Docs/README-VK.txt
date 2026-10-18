Supermodel-VK
=============

An independent rework of the open-source Sega Model 3 arcade emulator
"Supermodel", with a new Vulkan rendering backend and a new settings
application.

------------------------------------------------------------------------
WHAT THIS IS
------------------------------------------------------------------------

This package contains Supermodel-VK: a modified build of Supermodel, the
open-source emulator of Sega's Model 3 arcade hardware. Model 3 was
Sega's 3D arcade platform from 1996 to 1999, and emulating it lets the
original arcade games be played on a modern PC.

It is based on Supermodel 0.3a (release 0.3a-2026.09.28), from:

    https://github.com/trzy/Supermodel

This fork lives at:

    https://github.com/Igloo-Garage/Supermodel-VK

Supermodel is free software, distributed under the GNU General Public
License, version 3. See Data\LICENSE.txt for the full text. This build
is distributed under the same licence.

This is a personal, non-commercial project. It is not affiliated with,
sponsored by or endorsed by the Supermodel team. Credit for the
emulation core - the Model 3 hardware emulation, the CPU and sound
cores, the ROM loader, the original OpenGL renderer and the command-line
tool - belongs entirely to the Supermodel developers and contributors.

No game data is included in this package. See "ROM FILES" below.

------------------------------------------------------------------------
WHAT IS NEW IN THIS BUILD
------------------------------------------------------------------------

The original program is a command-line tool that renders through
OpenGL. This build keeps all of that and adds two things: a second,
new rendering backend written for Vulkan, and a real settings
application. Around those, a number of long-standing rendering issues
were fixed and several quality-of-life changes were made.

RENDERING - a new Vulkan backend
  * A complete Vulkan 3D renderer, used alongside the original OpenGL
    one. It is selected on the main page of the settings window and is
    the default.
  * Correct Real3D texture handling. The hardware packs an entire mip
    pyramid into a single texture page at fixed offsets rather than as
    a conventional mip chain, which the original backend did not
    account for. Distant scenery and backgrounds no longer render
    black, and the pack/offset lookup now matches the OpenGL backend.
  * Transparency and blending for all texture formats the hardware
    uses (T1RGB5 with the contour bit, interleaved A4L4, RGBA4), with
    transparent texels discarded so they cannot pollute the depth
    buffer.
  * Coplanar decals and an overlay pipeline. Sponsor decals, logos,
    numbers and light details on car bodies are drawn in the right
    pass, and no longer fight with the body paint underneath.
  * Fog matched to the OpenGL backend, so the horizon and open areas
    take the correct colour instead of staying black.
  * The 4:3 picture is scaled to fill the display height with black
    bars at the sides. Previously it could end up as a small image in
    the middle of a fullscreen display.
  * The white screen flash the hardware produces while it culls
    geometry is confined to the picture area, so it no longer paints
    the letterbox bars white.
  * If the Vulkan backend cannot start - a GPU or driver that offers
    neither Vulkan 1.3 nor the Vulkan 1.2 extensions it can use instead
    - the session falls back to the OpenGL renderer instead of closing.
    The settings window says so, next to the renderer setting that
    would change it back. A driver update is often what is actually
    needed, and the message says that too.

DISPLAY
  * Hardware MSAA at 2x or 4x. It applies to polygon coverage only, so
    textures and decals stay sharp rather than being blurred, which is
    the main advantage over a post-process anti-aliasing method.
  * FXAA post-process anti-aliasing, as a low-cost option for
    integrated graphics.
  * Integer scaling (point-perfect). The 2D layer is a fixed 496x384
    texture stretched over the screen, so only whole-number scaling
    leaves every one of its texels an equally sized block.
  * Exclusive fullscreen, either with G-SYNC / VRR tracking the
    emulator's frame rate, or at a fixed 57.524 Hz to match the
    original arcade refresh rate.
  * Optional cabinet backdrop: a picture can be drawn behind the black
    bars, so the picture does not sit in an empty void on a widescreen
    display. A built-in cabinet image is included, and any PNG of your
    own can be used instead.
  * WIDESCREEN: widens the view frustum horizontally so the game fills a
    16:9 display, the way a widescreen patch does - the scene genuinely
    extends outwards rather than being stretched, so nothing is
    distorted. The vertical field of view is untouched, which means the
    middle of the picture looks exactly as it did before and the extra
    detail appears at the sides. Only games whose full-screen viewport
    is the one being widened are affected, and it has no visible effect
    in a window that is already 4:3 (there is no extra width to use).
    The backdrop above is skipped while this is on, since there are no
    side bars left for it to fill.

CRT FILTER
  * Scanlines whose beam profile is integrated analytically over each
    output pixel. Sampling a scanline pattern point-by-point produces
    moire patterns whenever the scanline pitch is not a whole number of
    pixels, which at 4K it never is; integrating removes that entirely
    and also makes very dense scanlines fade out gracefully instead of
    turning into noise.
  * A shadow mask with three cell types - aperture grille, slot mask
    (the type Sega's own cabinets used) and dot mask - with an
    adjustable cell size. A distance-field calculation with a
    brightness-dependent hole radius makes the mask read as glowing
    phosphor rather than as a printed grid.
  * Multi-scale glow built on the hardware mip chain, which reproduces
    the soft scattered light of a real tube at a fraction of the cost
    of a hand-written blur chain.
  * Brightness compensation for the light the mask absorbs, and a
    highlight soft-compression stage so bright areas roll off instead
    of clipping flat to white.
  * Recommended presets, and a live readout of the scanline and mask
    pitches that warns when the two would beat against each other and
    produce colour banding.

INTERFACE - a new settings application
  * A persistent settings window, instead of configuring everything on
    the command line. Closing a game returns to the window rather than
    exiting the program.
  * No console window appears at start-up.
  * English and Chinese, switchable while the program is running.
  * Every setting has a description shown when the mouse rests on it.
  * The game list shows each set's version, so regional revisions of
    the same game can be told apart.
  * Settings can be saved, saved to a file of your choosing, and loaded
    back.
  * The window size and position are remembered between runs.
  * IN-GAME SETTINGS OVERLAY: press Tab while a game is running for a
    panel of the picture settings over the left of the screen. The
    emulator keeps running and every change takes effect immediately,
    so a filter can be tuned by watching the picture rather than by
    editing a value, quitting and relaunching. Apply, Cancel and Close
    decide whether the values are kept.
  * LIVE STATISTICS READOUT: press the key above Tab (` or ~) while a
    game is running for a column of figures in the top-left corner.
    It reports the renderer, the display mode and sync method, the
    emulated frame rate against the arcade's own, CPU and GPU clocks
    and loads, video and system memory, the frame rate and the 1% low.
    Press the key again to hide it, or tick "Statistics panel" on the
    Common tab. Works with either renderer. See "STATISTICS READOUT"
    below.

AUDIO
  * The emulated sound board runs on its own thread by default. Audio
    is produced once per frame from the main loop, so previously
    dragging the window or clicking another application would starve
    it and make the sound stutter.

INPUT
  * XInput is the default controller system, which suits the standard
    Xbox-layout pads that most PCs use. SDL and DirectInput remain
    available, and a raw-input option is included.
  * Coin and Start are no longer bound to pressing the analogue sticks,
    which made them easy to trigger by accident.
  * A single key cycles the camera view.

------------------------------------------------------------------------
SYSTEM REQUIREMENTS
------------------------------------------------------------------------

  * Windows 10 or Windows 11, 64-bit.
  * macOS 11 or later, Apple Silicon or Intel.
  * For the Vulkan renderer (the default): a GPU and driver that
    provide Vulkan 1.3, or Vulkan 1.2 together with the two extensions
    VK_KHR_dynamic_rendering and VK_KHR_synchronization2. The 1.2 route
    exists for the older hardware that lost out when 1.3 was required,
    and it is the same renderer either way - there is nothing to choose
    and nothing that behaves differently. In practice this covers an
    NVIDIA driver from June 2021 or later, an AMD driver from 2022, or a
    current Intel driver. Either route also needs 256 bytes of push
    constant space.
  * For the OpenGL renderers: OpenGL 4.1, which is far less demanding,
    and which also covers hardware too old for either Vulkan route.

Both the renderer selection and the two in-game features - the Tab
settings panel and the statistics readout - work with either family.
The panel is drawn by the renderer that is running, so there is no
combination of settings that leaves it unavailable.

If the Vulkan renderer cannot start on your machine, the game still
runs: the session falls back to the OpenGL renderer automatically and
says so in the settings window, where the renderer can also be changed
by hand. Your saved choice is not altered, so Vulkan is tried again next
time - useful if what you actually need is a driver update.

No runtime needs to be installed. This is a self-contained build: the
C++ runtime, SDL2 and SDL2_net are all linked statically, so there is no
Visual C++ Redistributable, DirectX, .NET or Vulkan SDK dependency, and
the folder contains no DLLs. The only external binary involved is
vulkan-1.dll, which is installed by the GPU driver.

On macOS the package is a bare `supermodel` executable (universal,
Apple Silicon and Intel) rather than an `.app`. Run it from Terminal in
the extracted folder so it can find `Frameworks/` and `Config/`.
MoltenVK is bundled in `Frameworks/`, so Vulkan 3D works without
Homebrew or the Vulkan SDK; a system MoltenVK is detected too if
present. Without it the emulator falls back to the OpenGL New 3D
renderer. The bundled MoltenVK is Apache-2.0; its licence is in
`Frameworks/LICENSE-MoltenVK.txt`.

------------------------------------------------------------------------
QUICK START
------------------------------------------------------------------------

  1. Unpack the whole folder anywhere, or move it once unpacked. It is
     self-contained and contains no absolute paths.
  2. Run Supermodel-VK.exe.
  3. Choose the folder that holds your game .zip files in the ROM
     directory field. The default is C:\Downloads, and it is
     remembered afterwards.
  4. Select a game and start it. Double-clicking a game in the list
     also starts it.
  5. In a game:

        Esc          return to the settings window
        Alt+P        pause
        Alt+R        reset
        F5           save a state
        F6           change the save slot
        F7           load a state
        Alt+S        take a screenshot
        Alt+F11/F12  decrease / increase sound volume
        Alt+F9/F10   decrease / increase music volume
        Tab          open the in-game settings overlay
        ` (above Tab)  show/hide the statistics readout
        Alt+Return   toggle fullscreen

     The full list, including every game control, is in
     Data\README.txt.

------------------------------------------------------------------------
ROM FILES
------------------------------------------------------------------------

No game data is included in this package, and it cannot be obtained
from here. You need your own ROM set files, in .zip format, matching the
set names listed in Data\Config\Games.xml.

Any .zip in the ROM directory is listed, but a set with no definition in
Games.xml is shown as "unsupported" and cannot be started. If every game
in your folder appears that way, the game list itself failed to load -
check Data\Supermodel.log, and make sure Data\Config\Games.xml is still
present.

------------------------------------------------------------------------
FOLDER LAYOUT
------------------------------------------------------------------------

  Supermodel-VK.exe   the program
  README.txt          this file
  (a setup note in Chinese is also provided at the top level)

  Data\               everything else
      Config\         Supermodel.ini (written on first run), Games.xml,
                      Music.xml
      Assets\         crosshair images and the cabinet backdrop
      NVRAM\          per-game board state and settings
      Saves\          save states
      Screenshots\    screenshots taken with Alt+S
      Analysis\       polygon analysis output, when enabled
      Supermodel.log  the run log - check this first if anything fails
      README.txt      the full Supermodel manual: every option, every
                      command-line flag, every key binding

Settings and game data all live under Data\, next to the program, so the
whole folder can be moved between drives without losing anything.

------------------------------------------------------------------------
KNOWN LIMITATIONS
------------------------------------------------------------------------

  * The CRT filter is an independent implementation, not a port of
    CRT-Royale or a similar package. It has no screen curvature, no
    chromatic aberration and no phosphor persistence.
  * There is no HDR output. The mask absorbs roughly two thirds of the
    light and the only compensation available without an HDR pipeline is
    a flat gain.
  * The in-game overlay and the CRT filter require the Vulkan renderer;
    the OpenGL renderers ignore them.
  * Most settings take effect when the next game starts rather than
    immediately, because the renderer, its render targets and its
    pipelines are all built at that point: the renderer choice itself,
    MSAA, supersampling, the resolution, the widescreen and backdrop
    options and integer scaling are all in this group.

    That means a new game, not a restart - press Esc to return to this
    window and start a game again; there is no need to close the program.
    They are left out of the in-game overlay for the same reason: a
    control there could not take effect while a game is already running.

    The exception is the picture filters - the CRT filter, its scanlines
    and mask, the glow and FXAA - which are re-read every frame and so
    change live. That is what the in-game overlay exists for.
  * Some games may show a thin line of colour along the edges of the
    picture. This comes from content the hardware drew just outside the
    visible area, which a real cabinet's overscan hid.
  * Widescreen only widens viewports that cover the whole screen. Many
    games use several smaller viewports within a frame (split-screen
    multiplayer, rear-view mirrors, some HUD elements) and those stay
    at their original proportions; that is intentional, since widening
    them would distort them. In a window that is already 4:3 the option
    has nothing to add and can look like it does nothing.
  * Only a limited number of games have been tested extensively.

------------------------------------------------------------------------
SETTINGS IN DETAIL
------------------------------------------------------------------------

The interface shows a one- or two-line reminder for each setting, because a
paragraph in a popup does not get read. The fuller reasoning lives here, for
anyone who wants to understand a setting before changing it.

Entries that are self-explanatory in one line are not repeated.

Brake and trigger axes, and the input system
--------------------------------------------

  Trigger (brake) bindings depend on the input system:
      XInput / SDL pads -> the triggers take one axis each, so the right trigger is JOY1_RZAXIS
      DirectInput / rawinput / SDL -> both triggers share the Z axis, so the right trigger is JOY1_ZAXIS_POS
  
  After switching input systems the old bindings stop working and the brake does nothing.
  Currently %s needs %s.

WHICH ONE TO PICK
-----------------

  A VRR display wants "Exclusive fullscreen - G-SYNC/VRR" (option 2).
  A display without VRR wants "Exclusive fullscreen - fixed arcade rate"
  (option 3), and needs a custom 57.524 Hz resolution created first.
  A display with neither should stay on "Borderless window" (option 1) -
  selecting the G-SYNC option there is not an error and is no worse, it
  simply falls back to the panel's own rate, with the same slight unevenness
  borderless has.

  The settings window states the same line above the three options, worded to
  match what it has detected on the machine.

Borderless window
-----------------

  Ordinary windowed fullscreen, presented by the desktop compositor.
  The most compatible option, but the extra composition step means the picture is not synced to the display, so slight judder can be visible.

  This also means VRR cannot engage here whatever the display supports, so
  this is not a weaker version of the exclusive modes - it is the one mode
  where the display is guaranteed to keep its own rate and the emulator's
  frames are fitted onto it. The judder that results is worse when the two
  rates do not divide evenly: a 57.5 Hz emulated rate on a 120 Hz panel spells
  some frames shown twice and others three times. The statistics readout prints
  the emulated rate and the panel's own mode side by side for exactly this -
  if one does not divide into the other, the visible unevenness is explained
  and an exclusive mode is the fix.

Exclusive fullscreen with G-SYNC / VRR
--------------------------------------

  Exclusive fullscreen + G-SYNC: the display scans at the emulator's rate, one to one, removing judder and tearing.
  Needs a G-SYNC/FreeSync display and V-Sync turned on. No custom resolution required.

  The Refresh Rate on the Video tab still applies here - it is the rate the emulator presents at and the display follows it.
  If your display does not support VRR, use the fixed arcade rate below instead.

  THE RANGE MATTERS, NOT JUST THE FEATURE.
  A display that "supports G-SYNC" and a display that can sync at 57.5 Hz are
  not the same thing. Variable refresh only works between the display's own
  minimum and maximum rates, and the minimum is often 48 Hz but can be higher -
  a panel whose range starts at 60 Hz cannot sync to 57.5 Hz at all.

  When the rate asked for falls below that minimum, one of two things happens,
  and neither is visible from the emulator's side:

    * the driver repeats frames until the display's minimum is reached (low
      frame rate compensation). Motion stays smooth, but the panel is no longer
      scanning once per emulated frame;
    * or the display simply holds its lowest rate, which looks the same as the
      judder described under "Borderless window" above.

  To find out which you have, look outside the emulator. Three places answer it,
  and each is worth knowing because they answer slightly different questions:

    Windows 11   Settings > System > Display > Advanced display. Reports whether
                 the display supports variable refresh at all. The quickest check.
    NVIDIA       Control Panel > "Set up G-SYNC". Lists the refresh *range* of
    Control      each display, which is the figure that actually matters here.
    Panel
    Display OSD  Many gaming panels show their live refresh rate in their own
                 menu. This is the only one of the three that says what the panel
                 is doing right now: a reading of about 57 or 58 Hz while a game
                 runs means it is syncing; a reading pinned at the range minimum
                 means it is not.

  None of these can be read by the emulator, and the emulator does not try to.
  A display that declares its variable refresh range usually does so in an
  EDID vendor block (AMD/VESA Adaptive-Sync, OUI 00-00-1A), and the settings
  window does read that - but only as supporting information for the fixed-rate
  option, never as a verdict on VRR. The reason is that a declaration proves
  support while its absence proves nothing: a display can support VRR without
  saying so in its EDID, and the older AMD HDMI implementations are exactly
  that case. Positive is reliable, negative is not, so nothing here claims a
  display cannot do VRR.

  The statistics readout (below) reports "exclusive VRR (preconditions met)"
  in this mode, and that wording is deliberate: the emulator can confirm
  exclusive fullscreen, V-Sync and a panel rate above the emulated one, and
  cannot confirm the fourth thing - whether the driver is actually varying the
  panel - which is what the display's own menu is for.

  A borderless window cannot use VRR at all, because the desktop compositor
  keeps presenting at the desktop rate. See "Borderless window" above.

Exclusive fullscreen at the fixed arcade rate
---------------------------------------------

  Scan the display at a fixed 57.524 Hz, matching the Model 3's original frame rate one to one.
  Requires a custom 57.524 Hz (or 57/58 Hz) resolution created in the GPU control panel first.

  This takes precedence over the Refresh Rate on the Video tab, which shows 57.5 Hz and is greyed out.

  WHEN NO SUCH MODE EXISTS THIS OPTION IS UNAVAILABLE, and the settings window says so.
  It is not merely discouraged: with no 57/58 Hz mode the exclusive fullscreen would
  fall back to the G-SYNC mode above it, so both choices would produce the same
  result - and two options that do the same thing, one of them describing something
  else, is worse than one that is unavailable and explains why.

  The settings window also reports what the display's own EDID says about this, which
  answers a question the option used to leave open - whether creating the resolution
  is worth attempting at all. Two things are printed, and they are not equally strong:

    The timing range (EDID 0xFD) applies to every display. If 57.5 Hz falls outside
    it, the panel will not lock to that timing and a custom resolution would be
    wasted effort - that verdict is solid. If it falls inside, that is a necessary
    condition only: the panel accepts the timing, and whether a custom resolution can
    actually be created still depends on the graphics driver offering that at all,
    which many laptop panels do not.

    A declared VRR range, when the display announces one, is a positive statement
    that it can vary its rate. Its absence means nothing - see the G-SYNC section.

  Note that 57.5 Hz is barely below 60, so it sits inside the timing range of nearly
  every panel made this century and the range check rarely produces the negative
  verdict. Its value is in the rare case where a panel's range starts higher, and in
  the figures themselves, which are otherwise awkward to find.

  Horizontal frequency is deliberately not reported or checked. Model 3's own
  horizontal rate is 24.39 kHz - 57.524160 Hz over 424 lines - which is medium
  resolution, not the 15.72 kHz of standard-resolution cabinets (Atari, CPS, Neo Geo,
  Irem) and not the 31.5 kHz of VGA. But nothing here drives a display at 496x384:
  the picture is scaled into the display's own resolution, so a 1600x1200 @ 57.5 Hz
  mode implies about 71.8 kHz horizontal, comfortably inside a modern panel's range.
  The figure follows automatically from the two numbers already checked, so it is a
  derived quantity rather than an independent criterion - and some panels report
  degenerate values for it (a 4K 120 Hz panel advertising a single 69 kHz rate),
  which is reason enough not to put it on screen.

  Refresh rates below 57.5 Hz are not offered, because Model 3 has only one: the
  emulation's per-frame cycle budget is fixed at 57.524160 Hz, and the audio, drive
  board and blanking timings all derive from it. Other arcade platforms do run at
  other rates - Irem's R-Type at 55 Hz, for instance - but they are other platforms,
  and this emulator is Model 3 only.

  The check is repeated about twice a second, so creating the mode while the emulator
  is running is picked up on its own; there is nothing to restart.

  Creating a custom 57.524 Hz resolution (one-off, manual; NVIDIA exposes no public API):

    1. Right-click the desktop -> NVIDIA Control Panel -> "Change resolution"
    2. "Customize..." -> tick "Enable resolutions not exposed by the display" ->
       "Create Custom Resolution"
    3. Set the resolution to the display's native one
    4. Set "Refresh rate" to 57 (or 58)
    5. Choose CVT for Timing, save, then select it back in "Change resolution" and
       confirm the picture is stable. If the display rejects the timing, retry with
       "CVT - Reduced blanking" or a lower resolution.

  A helper in the Scripts folder does the mode listing and switching from the
  command line. It comes with a launcher for each interface language, and either
  of them can simply be double-clicked - which is the intended way to use it:

      Scripts\\ArcadeDisplay.bat       - English
      Scripts\\ArcadeDisplay-zh.bat    - 中文

  With no arguments they list every resolution, mark the arcade-capable ones in
  green, and say which one they would pick. Both hold the window open so the
  output can be read, and both pass arguments through:

      Scripts\\ArcadeDisplay.bat -Action Run -Game "E:\\roms\\daytona2.zip"

  -Action Save records the current display settings and -Action Restore puts them
  back, which is useful if you want to switch modes by hand and undo it later.

  The work is done by Scripts\\ArcadeDisplay.ps1, which the launchers call with
  the execution policy bypassed for that one invocation. Calling it directly works
  too, if you prefer - drop the -Lang argument to get English output:

      powershell -ExecutionPolicy Bypass -File Scripts\\ArcadeDisplay.ps1 -Action List

  The bypass is needed because a script that arrived with a download is untrusted
  by default and would otherwise be refused outright. It applies to that single
  invocation and changes nothing system-wide.

  A note for anyone editing these files: the launchers are deliberately pure
  ASCII, and the Chinese text lives in the .ps1, which carries a UTF-8 byte order
  mark. cmd.exe re-reads a .bat at a byte offset computed under the code page in
  effect at the time, so a "chcp 65001" part-way through a file that contains
  multi-byte characters makes later lines lose their "rem" keyword and run as
  commands. Keeping the launchers ASCII-only avoids that entirely.

Statistics readout
------------------

  Press the key above Tab (` or ~) while a game is running to show a
  column of live figures in the top-left corner; press it again to hide
  them. It can also be switched on from the Common tab, and the setting
  is remembered between runs.

  Works with whichever renderer is selected. The readout is drawn by the
  renderer that is running - into the swapchain image under Vulkan, and
  into the default framebuffer under OpenGL - through the same mechanism
  as the settings overlay.

  What it shows, top to bottom:

    Renderer    which backend is running - Vulkan 3D, New 3D or Legacy 3D.
    Sync        the presenting mode that was actually entered, not the one
                the settings asked for.
                "exclusive VRR (preconditions met)" means exclusive
                fullscreen was entered at the desktop mode with vsync on,
                and the panel's own mode is above the rate being presented
                at - the three things a variable refresh rate needs, all of
                which this program can confirm. Whether the driver then
                actually varies the panel cannot be confirmed from inside
                the program, and the reason is worth knowing: the emulator
                deliberately paces itself below the panel's maximum, so the
                panel never has to throttle it and there is no observable
                difference. If the picture is still not smooth with those
                preconditions met, check the driver's own G-SYNC setting, or
                the panel's on-screen readout of its live refresh rate -
                neither of which this program can inspect.
                "exclusive fixed rate" means the display was switched to a
                57/58 Hz mode, which is definite. "borderless, VRR off" is
                worth reading: no variable refresh rate can engage there.
                A configured exclusive mode that the driver refuses shows
                up here as borderless, because that is where you ended up.
    Rate        the rate the emulator paces itself to, beside the arcade's
                own 57.52 Hz and the difference between them. Model 3
                hardware runs at 57.524160 Hz, so a rate of 60 Hz shows
                as "+4.30%" - the game is emulated that much fast. This is
                not a fault, but it is a choice: pick 57.5 Hz on the
                Video tab for the original speed, which shows as "-0.04%"
                (the dropdown offers 57.5, and the true figure is
                57.524160).
    Panel       the display mode in use. Under G-SYNC this is the panel's
                own mode (for example 120 Hz) and the panel follows the
                emulator below it; under the fixed arcade rate it is the
                57/58 Hz mode that was switched to. Note that a Panel
                figure equal to the emulated rate is not a sign that VRR
                is working - it just means the panel's maximum is what you
                are already presenting at.
    CPU         clock, thread count and load. On a CPU that mixes fast and
                slow cores - an Intel part with performance and efficiency
                cores - these describe the performance cores only and are
                labelled "8P+10E threads", because the emulator is
                scheduled on those and their load is the figure that
                decides whether it keeps up. Averaging in idle efficiency
                cores understates both the clock and the load. On a CPU
                whose cores are all alike the full count is shown with no
                such label.
    GPU         clock, load and video memory used out of total. Read
                through NVML, which ships with the NVIDIA driver; on a
                GPU whose vendor provides no equivalent these lines are
                left out rather than shown as zero.
    RAM         this process's working set.
    FPS         the emulated frame rate, measured over one second.
    1% low      the 99th percentile frame time over roughly the last ten
                seconds, inverted. A single number for how uneven the
                picture is: an emulator that averages 60 while stuttering
                has a smooth average and a visibly unstable picture, and
                this is what shows it.

  The labels are deliberately left in English in both interface
  languages. CPU, GPU, VRAM and FPS are the words these figures are
  looked up by and they do not translate usefully.

Resolution re-detection
-----------------------

  Pads are rescanned automatically when plugged in or unplugged; you can also press Detect again
  to refresh manually.
  
  This interface is navigated with the d-pad / stick, A to confirm and B to go back, which needs the
  pad to be recognised by the system as a standard gamepad.

VR4 view change on a single key
-------------------------------

  On: one button - Y on a pad, D on the keyboard - cycles the four views, so a single key covers
            all four. Each press steps to the next view and wraps around.
  Off: back to four separate view keys (B / Y / L3 / R3).
  
  Only affects the games with four views (Daytona 2, Scud Race, Le Mans 24).

Hardware MSAA
-------------

  The GPU multisamples polygon edges during rasterisation. Edges get noticeably smoother while
      textures stay exactly as sharp - decals and small signage on the car body are not softened
      the way a post-process antialiaser softens them.
  
  Model 3 polygon edges are very hard, so this gives the most benefit; prefer it on a discrete GPU.

FXAA
----

  One extra full-screen pass that finds edges from the luma gradient and smooths them. Very cheap
  and runs on any GPU, but the whole picture softens slightly, texture detail included.
  
  Good for integrated graphics and thin laptops. With a discrete GPU prefer the hardware MSAA
  above, or turn both on to cover each other's gaps.

Integer scaling (pixel-exact)
-----------------------------

  Scales the picture by whole numbers only and leaves black borders where it does not fill.
  
  The 2D tile layer (menus, text, sprites) is fixed at 496x384 in hardware; however high the 3D
  resolution, it is stretched over the top. With a fractional scale some source pixels get 5
  screen pixels and others 6, which makes interface text look uneven; integer scaling gives every
  source pixel an exact n x n block.
  
  At 4K that is 5x: a visible area of 2480x1920. Because the panel is 16:9 and the game is 4:3,
  about 680 pixels of border remain on each side and 120 above and below - narrower than with it off.
  At 1080p it is 2x (992x768).
  
  The cost is some screen area, so it is off by default. Turn it on for pixel-exact output.

CRT filter
----------

  Simulates the scanlines of an arcade CRT and its phosphor mask (shadow mask).
  
  The mask needs enough physical resolution to read as structure: at 4K it looks most like the
  real thing, while at 1080p use scanlines alone or it tends to turn into moire. The filter
  multiplies, so the side borders are not tinted.

CRT recommended presets
-----------------------

  Pairs the scanline count with a mask pitch so the two do not interfere with each other.
  
  Both are periodic patterns laid out in output pixels, and they beat whenever their periods are
  not in a whole-number ratio, producing colour bands. The classic case is 540 lines: a 4-pixel
  period at 4K against a mask pitch of 3 (a slot mask really repeats every 6 pixels vertically)
  gives a 12-pixel rainbow, about 2.2 mm per band on a 32 inch panel.
  
  The important part: moving either slider on its own is how you get there.
  
  The scanline count in a preset is derived from the current output resolution rather than stored,
  because the periods are fixed in output pixels - so the look carries across resolutions (360
  lines at 4K is the same appearance as 180 at 1080p).
  
  The mask pitch can only be a multiple of 3 (3, 6, 9), otherwise the three phosphor sub-holes
  fall inside less than one pixel and the mask itself becomes colour noise. Add the requirement
  that the vertical period divide evenly and only the combinations below qualify. Every preset
  uses the slot mask (the pattern in Sega's arcade cabinets).
  
  Once anything below is edited by hand this reads Custom (manual) and states outright whether
  the two periods are in a whole-number ratio.

Scanline count
--------------

  How many scanlines are laid out across the picture height. 0 = use the Model 3's own 384 rows,
  so one scanline lines up with one original row, which is closest to the arcade cabinet.
  
  Note that this counts against the whole window height. With integer scaling the picture occupies
  only part of it, so proportionally fewer scanlines land inside the picture - the actual number is
  shown below.

Mask type
---------

  How the phosphor is arranged:
      Aperture grille - continuous vertical red/green/blue stripes, no vertical structure (Sony Trinitron)
      Slot mask - stripes broken into short slots, staggered half a triad on alternate rows (common in Sega cabinets)
      Shadow mask - a 2x2 dot triad with the fourth cell empty (cheaper colour monitors)
  
  Sega cabinets mostly used the slot mask; choose it to match the original.

Mask pitch
----------

  How many physical pixels one phosphor dot spans.
  
  At 4K a single pixel is finer than the eye can resolve, so it degrades into noise and the picture
  darkens noticeably. At 4K use 2 (close up) or 3 (a large screen across the room). At 1080p,
  1 is about right.

Mask intensity
--------------

  Strength of the brightness difference between the red, green and blue phosphors. Lower it to look
  more like an ordinary monitor; raise it to look more like the grain of an old colour monitor with
  your eye up against the glass.

Brightness compensation
-----------------------

  The mask and the scanlines both block light (a shadow mask passes roughly a third of it), so the
  picture comes out dim and dull. A real tube compensates with enormous peak brightness, which is
  why an arcade monitor looks so luminous.
  
  Gain stands in for that here: raise it if turning the mask on makes the picture too dark.

Glow
----

  Simulates the soft halo light forms as it scatters inside the tube's glass faceplate.
  
  On a real CRT the bright areas bloom outwards, and that glassy translucency is what keeps the
  picture from reading as sharp pixels behind a grid, making it read as glowing glass instead.
  
  The algorithm averages the bright areas only, so dark areas are not lifted and black stays black.
  Independent of the CRT filter.

Glow levels
-----------

  How many downscaled levels the halo accumulates over. More levels spread the halo wider.
  
  Each level is half the resolution of the one above, so the count essentially sets the halo radius:
  3 levels is about 1/16 of the screen height, 6 is about 1/2 - very wide but very faint.
  
  At 4K there are more pixels so the halo is relatively smaller; 5 is a good choice. At 1080p around
  4 is enough. Too high greys the whole screen and loses the glow-around-the-highlight look.

Glow threshold
--------------

  Only areas brighter than this glow. It is the control that decides how clean the glow looks.
  
  Lower: broad bright areas such as grass, snow or a bright sky glow too, which lifts the whole
         picture and makes it look washed out and hazy.
  Higher: only genuine highlights (lamps, metal reflections, white lines) glow, which keeps the
         picture clean.
  
  Grass pitches and snow tracks want 60-70; for night or dark scenes lower it to around 30 so the lights halo more strongly.


---
LICENCE
------------------------------------------------------------------------

GNU General Public License, version 3 or later.

The full licence text is in Data\LICENSE.txt. This program is free
software, and comes with ABSOLUTELY NO WARRANTY, not even for
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

Supermodel, from which this is derived:

    https://github.com/trzy/Supermodel

This fork:

    https://github.com/Igloo-Garage/Supermodel-VK
