/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
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

/*
 * Main.cpp
 *
 * Main program driver for the SDL port.
 *
 * Bugs and Issues to Address
 * --------------------------
 * - -gfx-state crashes when ENABLE_DEBUGGER is also enabled:
 *
 *    #0  0x00007ff6586392a0 in CSoundBoard::GetDSB (this=0x1128) at Src/Model3/SoundBoard.cpp:552
 *    #1  0x00007ff6587072cf in Debugger::CSupermodelDebugger::CreateDSBCPUDebug (model3=0x0) at Src/Debugger/SupermodelDebugger.cpp:269
 *    #2  0x00007ff6587076e9 in Debugger::CSupermodelDebugger::AddCPUs (this=0x185da75df40) at Src/Debugger/SupermodelDebugger.cpp:361
 *    #3  0x00007ff6586f91a6 in Debugger::CDebugger::Attach (this=0x185da75df40) at Src/Debugger/Debugger.cpp:263
 *    #4  0x00007ff658705312 in Debugger::CConsoleDebugger::Attach (this=0x185da75df40) at Src/Debugger/ConsoleDebugger.cpp:2707
 *    #5  0x00007ff65862c6fc in Supermodel (game=..., rom_set=0x6cc8dff0e0, Model3=0x185da7598d0, Inputs=0x185da752590, Outputs=0x0, Debugger=std::shared_ptr<Debugger::CDebugger> (use count 3, weak count 0) = {...})
 *        at Src/OSD/SDL/Main.cpp:978
 *    #6  0x00007ff658634ba3 in SDL_main (argc=3, argv=0x185da4e68a0) at Src/OSD/SDL/Main.cpp:2056
 *    #7  0x00007ff658720141 in main_getcmdline ()
 *    #8  0x00007ff6585b13b1 in __tmainCRTStartup () at C:/M/mingw-w64-crt-git/src/mingw-w64/mingw-w64-crt/crt/crtexe.c:321
 *    #9  0x00007ff6585b14e6 in mainCRTStartup () at C:/M/mingw-w64-crt-git/src/mingw-w64/mingw-w64-crt/crt/crtexe.c:202
 * - Need to address all the stale TO-DOs below and clear them out :)
 *
 * To Do Before Next Release
 * -------------------------
 * - Thoroughly test config system (do overrides work as expected? XInput
 *   force settings?)
 * - Remove all occurrences of "using namespace std" from Nik's code.
 * - Standardize variable naming (recently introduced vars_like_this should be
 *   converted back to varsLikeThis).
 * - Update save state file revision (strings > 1024 chars are now supported).
 * - Fix BlockFile.cpp to use fstream!
 * - Check to make sure save states use explicitly-sized types for 32/64-bit
 *   compatibility (i.e., size_t, int, etc. not allowed).
 * - Make sure quitting while paused works.
 * - Add UI keys for balance setting?
 * - 5.1 audio support?
 *
 * Compile-Time Options
 * --------------------
 * - SUPERMODEL_WIN32: Define this if compiling on Windows.
 * - SUPERMODEL_OSX: Define this if compiling on Mac OS X.
 * - SUPERMODEL_DEBUGGER: Enable the debugger.
 * - DEBUG: Debug mode (use with caution, produces large logs of game behavior)
 */

#include <new>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cctype>
#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <GL/glew.h>

#ifdef SUPERMODEL_WIN32
#include "DirectInputSystem.h"
#include "WinOutputs.h"
#endif
#include "NetOutputs.h"

#include "Supermodel.h"
#include "Util/Format.h"
#include "Util/NewConfig.h"
#include "Util/ConfigBuilders.h"
#include "OSD/FileSystemPath.h"
#include "GameLoader.h"
#include "SDLInputSystem.h"
#include "SDLIncludes.h"
#include "Debugger/SupermodelDebugger.h"
#ifndef SUPERMODEL_OSX
#include "Graphics/Legacy3D/Legacy3D.h"
#endif
#include "Graphics/New3D/New3D.h"
#include "Graphics/Render2D.h"
#include "Graphics/Vulkan3D/VulkanContext.h"
#include "Graphics/Vulkan3D/Vulkan2D.h"
#include "Graphics/Vulkan3D/Vulkan3D.h"
#include "Model3/IEmulator.h"
#include "Model3/Model3.h"
#include "OSD/Audio.h"
#include "Graphics/New3D/VBO.h"
#include "Graphics/SuperAA.h"
#include "Sound/MPEG/MpegAudio.h"

#include <iostream>
#include "Util/BMPFile.h"

#include "Crosshair.h"
#include "OSD/ConfigFile.h"
#include "Gui.h"
#include "Overlay.h"
#include "StatsOsd.h"
#include "Localization.h"
#include "Main.h"

#ifdef _WIN32
#include <windows.h>    // AttachConsole()/AllocConsole()/GetConsoleWindow() - see ShowConsoleWindow()
#include <SDL_syswm.h>  // SDL_GetWindowWMInfo(), used to find the HWND for the window icon
// WMI, used to enumerate the attached displays so their EDID timing ranges can be read. The project
// already links wbemuuid.lib because DirectInputSystem uses the same API to spot XInput devices.
#include <wbemidl.h>
#include <oleauto.h>

// Force discrete GPU on hybrid / multi-GPU systems (NVIDIA Optimus & AMD PowerXpress)
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// Matches IDI_SUPERMODEL in MSVC/SupermodelResources.rc. The two have to agree or the window icon
// silently goes missing while the one in Explorer still looks right.
#define IDI_SUPERMODEL 101
#endif


/******************************************************************************
 Generic
******************************************************************************/

void SetWindowIcon(SDL_Window* window)
{
#ifdef _WIN32
    if (window == nullptr)
        return;

    // The icon comes out of this executable's resources, so no image file has to be shipped or
    // decoded. LoadImage rather than LoadIcon because it can pick the size that suits the title bar
    // and the Alt-Tab thumbnail from the multi-resolution .ico; letting Windows choose with
    // LR_DEFAULTSIZE would give the 32x32 artwork to both.
    //
    // The locals are not called "small" and "large": rpcndr.h defines those as macros meaning
    // "char" and "long long", so a variable with either name expands into a syntax error.
    HINSTANCE instance = GetModuleHandle(nullptr);
    HICON iconBig = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_SUPERMODEL), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    HICON iconSmall = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_SUPERMODEL), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    // Without the HWND there is nothing to set the icon on; the executable icon still applies.
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) != SDL_TRUE)
        return;

    HWND hwnd = info.info.win.window;
    if (hwnd == nullptr)
        return;

    // SDL sets its own default icon when it creates the window, so this has to run afterwards.
    if (iconBig != nullptr)
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconBig);
    if (iconSmall != nullptr)
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);
#else
    // Other platforms take the icon from the bundle or desktop entry; SDL's own API needs an
    // SDL_Surface, which would mean shipping the artwork a second time in a second format.
    (void)window;
#endif
}


/******************************************************************************
 Global Run-time Config
******************************************************************************/

static const std::string s_analysisPath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Analysis);
static const std::string s_configFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Supermodel.ini";
static const std::string s_gameXMLFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Games.xml";
static const std::string s_musicXMLFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Music.xml";

// Written into the configuration file as a path relative to the installation, so that moving the
// folder to another drive does not leave the setting pointing at where it used to live. The logger
// anchors it to the executable's directory when it opens the file (see FileSystemPath::ResolvePath).
// Only the shown default in the command-line help uses the resolved form.
static const std::string s_logOutputSetting = "Data/Supermodel.log";
static const std::string s_logFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Log) << "Supermodel.log";

std::string ResolveDataFileSetting(const std::string& configuredPath, const std::string& fileName)
{
  const std::string installed = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << fileName;

  if (!configuredPath.empty())
  {
    // A relative value is relative to the executable rather than the working directory, the same
    // convention the logger uses for LogFile (see FileSystemPath::ResolvePath). That is deliberately
    // not enough to make the old location work again - "Config/Games.xml" now resolves next to the
    // executable, where the file no longer sits - so the fallback below still applies.
    const std::string candidate = FileSystemPath::ResolvePath(configuredPath);
    std::error_code error;
    if (std::filesystem::exists(std::filesystem::path(candidate), error) && !error)
      return candidate;

    ErrorLog("The settings point %s at '%s', which cannot be read. Using '%s' instead.",
             fileName.c_str(), configuredPath.c_str(), installed.c_str());
  }

  return installed;
}

static Util::Config::Node s_runtime_config("Global");


/******************************************************************************
 Display Management
******************************************************************************/

SDL_Window *s_window = nullptr;
static Vulkan3D::CVulkanContext *s_vkContext = nullptr;

/*
 * The Vulkan 3D renderer, kept in its concrete type as well as the interface.
 *
 * It is created here and handed to the emulator as an IRender3D, which is all the emulator needs. The
 * statistics readout needs one thing the interface does not carry - the MSAA level actually in force,
 * which the backend may have reduced from the requested one - and this is simpler than widening
 * IRender3D with a method that only one of its three implementations has anything to say about.
 */
static Vulkan3D::CVulkan3D *s_vk3dRenderer = nullptr;
// GL context of the game window. Kept separate from the settings GUI's own context, which stays
// alive across game sessions (see DestroyGLScreen()).
static SDL_GLContext s_glContext = nullptr;

// How the fullscreen window is set up. Resolved once at startup by ResolveFullScreenMode().
enum class FullScreenMode
{
  Borderless,         // general purpose: desktop-sized composited window. VRR does not engage through
                      // the compositor *unless* the driver is configured to apply it to windowed
                      // output too - see the note in ResolveFullScreenMode()
  ExclusiveDesktop,   // exclusive fullscreen at the desktop mode, letting G-SYNC/FreeSync vary the
                      // panel rate to match our frame rate
  ExclusiveArcade,    // exclusive fullscreen at a fixed 57/58 Hz display mode (custom timing)
};

static FullScreenMode s_fullScreenMode = FullScreenMode::Borderless;

/*
 * Position and size of rectangular region within OpenGL display to render to.
 * Unlike the config tree, these end up containing the actual resolution (and
 * computed offsets within the viewport) that will be rendered based on what
 * was obtained from SDL.
 */
static unsigned  xOffset, yOffset;      // offset of renderer output within OpenGL viewport
static unsigned  xRes, yRes;            // renderer output resolution (can be smaller than GL viewport)
static unsigned  totalXRes, totalYRes;  // total resolution (the whole GL viewport)
static int aaValue = 1;                 // default is 1 which is no aa
static CRTcolor CRTcolors = CRTcolor::None; // default to no gamma/color adaption being done

/*
 * Crosshair stuff
 */
static CCrosshair* s_crosshair = nullptr;

/*
 * RaiseProcessPriority():
 *
 * Windows boosts the foreground application and throttles the ones behind it, so clicking another
 * window takes CPU away from the emulator exactly when its once-per-frame audio production needs it.
 * That is why the sound stutters in a loop as soon as focus moves away, and why a capture
 * application makes it worse. Running at above-normal priority keeps the frame loop fed while the
 * window is in the background.
 *
 * Deliberately not the highest class: REALTIME_PRIORITY_CLASS would let the emulator starve the rest
 * of the system, which is a worse problem than the one being fixed. Unsupported elsewhere, where it
 * is a no-op.
 */
static void RaiseProcessPriority()
{
#ifdef _WIN32
  if (!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS))
  {
    InfoLog("Could not raise process priority (error %lu).", GetLastError());
    return;
  }
  // Read it back: the class can be reset during start-up, so the effective value matters more than
  // the fact that the call succeeded.
  const DWORD actual = GetPriorityClass(GetCurrentProcess());
  InfoLog("Process priority class set, now 0x%08lX (%s).", actual,
    actual == ABOVE_NORMAL_PRIORITY_CLASS ? "above normal" :
    actual == HIGH_PRIORITY_CLASS ? "high" :
    actual == NORMAL_PRIORITY_CLASS ? "normal - not raised" : "other");
#endif
}

/*
 * Keeping the game running while the window is dragged or resized.
 *
 * Dragging the title bar puts Windows into a modal message loop, entered from inside
 * DispatchMessage. Our frame loop is blocked there, so no frames run - and because emulated audio is
 * produced once per frame, the sound stutters in a loop as the buffer drains. SDL notices the modal
 * loop but only sends redraw events; it does not run the game.
 *
 * The way out is to hook the window procedure. WM_ENTERSIZEMOVE is delivered before the loop starts,
 * which is the moment to ask Windows for a timer: WM_TIMER messages are among the few that keep
 * arriving during a modal loop. Running one emulator frame from that timer keeps both the picture
 * and the audio going for as long as the drag lasts.
 *
 * This is not reentrancy in the dangerous sense. The frame loop is not part way through a frame when
 * this happens - it is parked inside the event pump - so the emulation state is not being mutated
 * from two places at once.
 *
 * LIMITATION: this only fires for the SDL input systems ("sdl", "sdlgamepad"). The callback is
 * invoked by SDL's own message pump, and CDirectInputSystem (the "dinput"/"xinput"/"rawinput"
 * options, and the Windows default) pumps the Windows message queue itself so that RawInput handles
 * do not arrive stale - which means SDL's pump, and therefore this hook, is bypassed entirely. With
 * those input systems the window can still be dragged without the game stalling only if the frame
 * loop stops depending on the message pump, i.e. runs on its own thread. Do not assume this hook
 * covers every configuration.
 */
// The hook itself is Windows-only. UINT_PTR, HWND, the WM_* messages and SetTimer()/KillTimer() are
// Win32 types and constants that do not exist under SDL's X11, Wayland or Cocoa back ends, so the
// declarations have to sit inside the guard as well as the code that uses them. Guarding only the
// body of SetModalLoopHook() - as an earlier revision did - left the declarations at file scope and
// made a GCC/Clang build fail with "'UINT_PTR' does not name a type" before it ever reached the
// function. Everything Windows-specific is now inside one block, and other targets compile a stub.
#ifdef _WIN32
static IEmulator* s_modalEmulator = nullptr;
// Must not collide with SDL's own modal-loop timer, which uses a pointer as its id.
static const UINT_PTR kModalFrameTimerId = 0x5F4D;

static void SDLCALL ModalLoopMessageHook(void* /*userdata*/, void* hWnd, unsigned int message, Uint64 wParam, Sint64 /*lParam*/)
{
  HWND hwnd = reinterpret_cast<HWND>(hWnd);

  switch (message)
  {
  case WM_ENTERSIZEMOVE:
  case WM_ENTERMENULOOP:
    if (s_modalEmulator != nullptr)
      SetTimer(hwnd, kModalFrameTimerId, USER_TIMER_MINIMUM, nullptr);
    break;

  case WM_TIMER:
    if (wParam == kModalFrameTimerId && s_modalEmulator != nullptr)
      s_modalEmulator->RunFrame();
    break;

  case WM_EXITSIZEMOVE:
  case WM_EXITMENULOOP:
    KillTimer(hwnd, kModalFrameTimerId);
    break;

  default:
    break;
  }
}
#endif  // _WIN32

// No-op away from Windows: there is no modal loop to keep alive there, because the frame loop is not
// parked inside a Win32 DispatchMessage() call. Kept unconditional so the call sites stay free of
// #ifdef noise.
static void SetModalLoopHook(IEmulator* emulator)
{
#ifdef _WIN32
  s_modalEmulator = emulator;
  SDL_SetWindowsMessageHook(emulator != nullptr ? ModalLoopMessageHook : nullptr, nullptr);
#else
  (void)emulator;
#endif
}

/*
 * Console handling.
 *
 * The executable is built for the Windows subsystem rather than the console one, and that is what
 * keeps a black window from flashing up on launch: a console-subsystem binary makes Windows allocate
 * and display a console during process start-up, before any code of ours runs, so hiding it
 * afterwards - which is all this used to do - can only ever be a race. On a fast machine the window
 * is drawn before the hide takes effect. With no console subsystem there is nothing to appear.
 *
 * The price is that printf() and std::cout have nowhere to go unless a console is arranged, so the
 * two cases where output is actually wanted are handled explicitly:
 *
 *   - started from a shell (cmd, PowerShell): attach to that console, so command-line use such as
 *     -print-games prints where the user is already looking. No window is created, and from Explorer
 *     there is no parent console to attach to, so this simply does nothing.
 *   - -show-console / ShowConsole = 1: allocate a console of our own, for when the output is wanted
 *     but the program was started from Explorer.
 *
 * Logging to Data\Supermodel.log is unaffected either way, which is the path that matters.
 */
static bool s_consoleIsOurs = false;    // true only for a console this process allocated itself

/*
 * Whether the process was started with a usable stdout - an inherited console or a redirection to a
 * file or pipe.
 *
 * Captured before any console can be created, because it decides whether the standard streams may
 * ever be rebound. If the caller redirected output, the streams are already connected and replacing
 * them with CONOUT$ would silently discard everything that was asked to be written, so
 * "app.exe -print-games > list.txt" would produce an empty file.
 */
static bool s_stdoutProvided = false;

// Points the C runtime's standard streams at the console, so printf and iostream reach it rather
// than a handle that was never opened. Does nothing when output was redirected or inherited.
static void BindStandardStreams()
{
#ifdef _WIN32
  if (s_stdoutProvided)
    return;
  FILE* unused = nullptr;
  freopen_s(&unused, "CONOUT$", "w", stdout);
  freopen_s(&unused, "CONOUT$", "w", stderr);
  freopen_s(&unused, "CONIN$", "r", stdin);
#endif
}

/*
 * Attaches to the console this process was started from, if there is one, so that command-line output
 * still appears. Called as early as possible: anything printed before this goes nowhere.
 *
 * Deliberately does not set s_consoleIsOurs - that console belongs to the shell, and hiding it would
 * take the user's command prompt away with it.
 */
static void AttachParentConsole()
{
#ifdef _WIN32
  // Recorded first, and before a console can exist, because BindStandardStreams() consults it.
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  s_stdoutProvided = (output != nullptr && output != INVALID_HANDLE_VALUE);

  if (GetConsoleWindow() != nullptr)
    return;                                     // one is already attached
  if (AttachConsole(ATTACH_PARENT_PROCESS))
    BindStandardStreams();
#endif
}

static void ShowConsoleWindow(bool show)
{
#ifdef _WIN32
  if (!show)
  {
    // Only hide a console this process owns. For one attached from a parent shell, hiding would
    // close the window the user's command prompt is in.
    if (s_consoleIsOurs)
    {
      if (HWND console = GetConsoleWindow())
        ShowWindow(console, SW_HIDE);
    }
    return;
  }

  if (GetConsoleWindow() == nullptr)
  {
    // Prefer the console we were started from, so no second window appears; only make one when there
    // is none. AllocConsole failing is not fatal - it means there is nothing to show.
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
      s_consoleIsOurs = (AllocConsole() != FALSE);
    BindStandardStreams();
  }
  if (HWND console = GetConsoleWindow())
    ShowWindow(console, SW_SHOW);
#endif
}

void SetConsoleVisible(bool show)
{
  ShowConsoleWindow(show);
}

static void CalcScreenGeometry(unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio)
{
  int actualWidth = 0;
  int actualHeight = 0;
  SDL_GetWindowSize(s_window, &actualWidth, &actualHeight);
  *totalXResPtr = (unsigned)actualWidth;
  *totalYResPtr = (unsigned)actualHeight;

  /*
   * The panel's own size, for the integer-scaling test below.
   *
   * Not the same as the window's when the monitors on this machine use different scaling factors.
   * Windows reports the geometry of any monitor that is not at the system DPI converted into the
   * system DPI's space - this process asks for system DPI awareness - so a 1920x1080 panel beside a
   * 4K panel at 150% is reported to SDL as 2880x1620, and the window inherits that. On a machine
   * without mixed scaling the two agree and none of this matters.
   *
   * It matters for this option specifically because of what it promises. "Integer scaling" means every
   * texel of the 496x384 layer lands on an exact n by n block of panel pixels. Choosing n from the
   * virtualised height breaks that promise: on the machine above it picks 4 rather than 2, produces an
   * area of 1984x1536, and the panel then has to scale 1536 logical rows down to 1080 physical ones -
   * so the picture fills 95% of the screen instead of 71%, which looks better, while the texels it was
   * supposed to align are not aligned at all. The option was quietly doing the opposite of its name.
   *
   * Falls back to the window size where the panel's own mode could not be read, which is the previous
   * behaviour and the best available answer when nothing better is known.
   */
  int panelWidth = actualWidth;
  int panelHeight = actualHeight;
  {
    const int displayIndex = GetConfiguredDisplaySdlIndex();
    for (const DisplayInfo& display : GetDisplays())
    {
      if (display.sdlIndex == displayIndex && display.panelWidth > 0 && display.panelHeight > 0)
      {
        panelWidth = display.panelWidth;
        panelHeight = display.panelHeight;
        break;
      }
    }
  }

  float xResF = float(*xResPtr);
  float yResF = float(*yResPtr);

  if (keepAspectRatio)
  {
    // Clamp the requested resolution to the Model 3 aspect ratio (4:3).
    float model3Ratio = float(496.0/384.0);
    if (yResF < (xResF/model3Ratio))
      xResF = yResF*model3Ratio;
    if (xResF < (yResF*model3Ratio))
      yResF = xResF/model3Ratio;

    // Scale the viewable area up so that it fills as much of the display as possible
    // while keeping the 4:3 Model 3 aspect ratio. In fullscreen the window is the size
    // of the monitor, so this turns a small centred box into a 4:3 image that fills the
    // screen vertically with black bars either side. Windowed mode is unaffected because
    // the window is already the requested size (fit == 1).
    if (xResF > 0.0f && yResF > 0.0f && actualWidth > 0 && actualHeight > 0)
    {
      if (s_runtime_config["IntegerScale"].ValueAsDefault<bool>(false))
      {
        // Whole-number magnification, with the leftover screen area left as black bars.
        //
        // The factor is measured against the native 496x384 signal rather than the requested
        // resolution, because that is the size the 2D tile layer's texels really are: it is a fixed
        // 496x384 texture stretched over the visible area, whatever resolution the 3D scene renders
        // at. A whole-number factor puts every one of its texels on an exact n by n block of output
        // pixels; any other factor interpolates, and because the ratio is not whole some texels come
        // out a pixel wider than their neighbours, which is what makes scaled-up 2D look uneven.
        //
        // Measured against the panel, not the window - see the note on panelWidth above, which is
        // where the two differ and where using the wrong one defeats the purpose of the option.
        float nativeFit = float(panelWidth) / 496.0f;
        float nativeFitY = float(panelHeight) / 384.0f;
        if (nativeFitY < nativeFit)
          nativeFit = nativeFitY;
        int multiple = (int)nativeFit;
        if (multiple < 1)
          multiple = 1;

        xResF = 496.0f * float(multiple);
        yResF = 384.0f * float(multiple);

        static int s_loggedMultiple = -1;
        if (multiple != s_loggedMultiple)
        {
          s_loggedMultiple = multiple;
          // Both sizes are named when they differ, because that is the case where the factor no longer
          // follows from the window and a reader would otherwise be unable to check the arithmetic.
          if (panelWidth != actualWidth || panelHeight != actualHeight)
          {
            InfoLog("Video: integer scaling %dx - visible area %ux%u, panel %dx%d in a %ux%u window.",
              multiple, (unsigned)(xResF + 0.5f), (unsigned)(yResF + 0.5f),
              panelWidth, panelHeight, (unsigned)actualWidth, (unsigned)actualHeight);
          }
          else
          {
            InfoLog("Video: integer scaling %dx - visible area %ux%u in a %ux%u window.",
              multiple, (unsigned)(xResF + 0.5f), (unsigned)(yResF + 0.5f),
              (unsigned)actualWidth, (unsigned)actualHeight);
          }
        }
      }
      else
      {
        float fit = float(actualWidth) / xResF;
        float fitY = float(actualHeight) / yResF;
        if (fitY < fit)
          fit = fitY;
        if (fit > 1.0f)
        {
          xResF *= fit;
          yResF *= fit;
        }
      }
    }
  }
  else
  {
    // Stretch: cover the entire display.
    xResF = float(actualWidth);
    yResF = float(actualHeight);
  }

  int viewWidth = (int)(xResF + 0.5f);
  int viewHeight = (int)(yResF + 0.5f);
  if (viewWidth < 1) viewWidth = 1;
  if (viewHeight < 1) viewHeight = 1;
  if (actualWidth > 0 && viewWidth > actualWidth) viewWidth = actualWidth;
  if (actualHeight > 0 && viewHeight > actualHeight) viewHeight = actualHeight;

  int vx = (actualWidth - viewWidth) / 2;
  int vy = (actualHeight - viewHeight) / 2;
  if (vx < 0) vx = 0;
  if (vy < 0) vy = 0;

  *xOffsetPtr = (unsigned)vx;
  *yOffsetPtr = (unsigned)vy;
  *xResPtr = (unsigned)viewWidth;
  *yResPtr = (unsigned)viewHeight;
}

static Result SetGLGeometry(unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio)
{
  CalcScreenGeometry(xOffsetPtr, yOffsetPtr, xResPtr, yResPtr, totalXResPtr, totalYResPtr, keepAspectRatio);

  // OpenGL initialization
  glViewport(0,0,*totalXResPtr,*totalYResPtr);
  glClearColor(0.0,0.0,0.0,0.0);
  glClearDepth(1.0);
  glDepthFunc(GL_LESS);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  // Clear both buffers to ensure a black border
  for (int i = 0; i < 3; i++)
  {
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    SDL_GL_SwapWindow(s_window);
  }

  UINT32 correction = (UINT32)(((*yResPtr / 384.) * 2.) + 0.5); // due to the 2D layer compensation (2 pixels off)

  glEnable(GL_SCISSOR_TEST);

  // Scissor box (to clip visible area)
  if (s_runtime_config["WideScreen"].ValueAsDefault<bool>(false))
  {
    glScissor(0* aaValue, correction* aaValue, *totalXResPtr * aaValue, (*totalYResPtr - (correction * 2)) * aaValue);
  }
  else
  {
    glScissor((*xOffsetPtr + correction) * aaValue, (*yOffsetPtr + correction) * aaValue, (*xResPtr - (correction * 2)) * aaValue, (*yResPtr - (correction * 2)) * aaValue);
  }
  return Result::OKAY;
}

// static void GLAPIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
// {
//     printf("OGLDebug:: 0x%X: %s\n", id, message);
// }

// Opt-in exclusive fullscreen.
//
// With both ExclusiveRefreshRate and ExclusiveFullscreen at their defaults (0 / off) nothing here
// happens: the renderers use borderless ("desktop") fullscreen, the general-purpose path that
// works on any display. Two opt-in routes reach a 1:1 frame-to-scanline mapping, in which the panel
// scans once per emulated frame so there is no tearing, no cadence judder and no queueing latency:
//
//   ExclusiveFullscreen = 1        exclusive fullscreen at the desktop mode. G-SYNC/FreeSync varies
//                                  the panel rate to follow our present rate, so no custom
//                                  resolution is needed - only a VRR-capable display and VSync.
//   ExclusiveRefreshRate = 57.5242 exclusive fullscreen at a fixed 57/58 Hz display mode, for
//                                  displays (or tastes) without VRR. Requires a custom timing
//                                  created in the NVIDIA Control Panel; if absent we degrade to
//                                  the G-SYNC route above rather than failing.
//
// Borderless (the default) does not ask for either, and normally cannot reach them: Windows DWM
// composites the window at the desktop refresh rate, and the frames are fitted onto that rate.
//
// Normally, not always, and the exception matters. NVIDIA offers G-SYNC for windowed output as well
// as fullscreen ("Enable for windowed and full screen mode" in the control panel; the driver default
// is fullscreen only). With that selected, the display can follow a composited window's present rate,
// so borderless fullscreen reaches the same 1:1 mapping this comment says it cannot - and the
// observation below, that a borderless session cannot be smooth, stops being true.
//
// Nothing here can tell which is the case. Whether VRR is engaging has no application-visible signal:
// with the panel on its own cadence and with the panel following us, the program presents at the same
// rate and reads the same frame rate. So the cadence figure reported in this mode is documented as an
// assumption - see the note on ClassifyCadence() in StatsOsd.cpp - and the display's own on-screen
// refresh readout is the only authority.
//
// Either route also sets the frame pacer to the arcade rate so the game runs at the speed the
// panel scans. Scripts/ArcadeDisplay.ps1 enumerates the available modes, can save/restore the
// desktop mode and can launch the emulator with these options already set.
//
// Looks for a display mode at (roughly) the given refresh rate, preferring the exact render
// resolution, then the desktop resolution, then anything on the output. SDL reports integer
// refresh rates, so a custom 57.524 Hz timing shows up as 57 or 58; both are accepted.
//
// displayIndex is an SDL display index, not the \\.\DISPLAYn number - see GetDisplays() for the
// mapping between the two. Every SDL call in here is per-display, so which monitor is being asked
// matters: a custom 57.524 Hz timing is a property of one output's mode list, and asking the wrong
// one reports the mode as missing when it exists.
static bool FindArcadeDisplayModeImpl(float refreshRateHz, int displayIndex, SDL_DisplayMode& best, bool quiet)
{
    const int display_mode_count = SDL_GetNumDisplayModes(displayIndex);
    if (display_mode_count < 1) {
        if (!quiet)
            InfoLog("Exclusive fullscreen: no display modes reported.");
        return false;
    }

    const int wantedMilliHz = (int)(refreshRateHz * 1000.0f + 0.5f);

    auto isArcadeRate = [&](const SDL_DisplayMode& mode, int& delta) {
        if (SDL_BITSPERPIXEL(mode.format) < 24)
            return false;
        if (mode.refresh_rate != 57 && mode.refresh_rate != 58)
            return false;
        delta = std::abs(mode.refresh_rate * 1000 - wantedMilliHz);
        return true;
    };

    SDL_DisplayMode desktop{};
    const bool haveDesktop = SDL_GetDesktopDisplayMode(displayIndex, &desktop) == 0;

    bool haveBest = false;
    int bestDelta = 1 << 30;
    std::string availableRates;

    // Three passes, most specific first. Requiring the exact render resolution would mean creating
    // a custom timing for every resolution the user might pick, so the desktop resolution is also
    // accepted (the game renders at its own resolution and is scaled into that mode).
    for (int pass = 0; pass < 3 && !haveBest; pass++) {
        for (int i = 0; i < display_mode_count; ++i) {

            SDL_DisplayMode mode;

            if (SDL_GetDisplayMode(displayIndex, i, &mode) != 0)
                continue;

            // Collect the rates offered at the render resolution, for the failure message.
            if (pass == 0 && (unsigned)mode.w == totalXRes && (unsigned)mode.h == totalYRes) {
                if (!availableRates.empty())
                    availableRates.append(", ");
                availableRates.append(std::to_string(mode.refresh_rate));
            }

            int delta = 0;
            if (!isArcadeRate(mode, delta))
                continue;

            if (pass == 0) {
                if ((unsigned)mode.w != totalXRes || (unsigned)mode.h != totalYRes)
                    continue;
            }
            else if (pass == 1) {
                if (!haveDesktop || mode.w != desktop.w || mode.h != desktop.h)
                    continue;
            }

            if (delta < bestDelta || !haveBest) {
                bestDelta = delta;
                best = mode;
                haveBest = true;
            }
        }
    }

    if (!haveBest && !quiet)
        InfoLog("Exclusive fullscreen: no 57/58 Hz display mode at %ux%u on display %d (rates there: %s).",
            totalXRes, totalYRes, displayIndex, availableRates.empty() ? "none" : availableRates.c_str());

    return haveBest;
}

static bool FindArcadeDisplayMode(float refreshRateHz, int displayIndex, SDL_DisplayMode& best)
{
    return FindArcadeDisplayModeImpl(refreshRateHz, displayIndex, best, false);
}

/*
 * Availability check for the settings window.
 *
 * This answers the same question ResolveFullScreenMode() answers at startup - whether the fixed
 * arcade rate is actually usable - so what the interface reports matches what the emulator will do.
 * Silent, because the window draws continuously and a log line per frame would be noise.
 */
bool HasArcadeDisplayMode(float refreshRateHz)
{
    SDL_DisplayMode mode{};
    return FindArcadeDisplayModeImpl(refreshRateHz, GetConfiguredDisplaySdlIndex(), mode, true);
}

#ifdef _WIN32

/*
 * The \\.\DISPLAYn number Windows gives the monitor covering a rectangle.
 *
 * The rectangle is what makes the two enumerations comparable. SDL reports a display's screen area
 * through SDL_GetDisplayBounds(), and the same area comes back from GetMonitorInfo() as rcMonitor
 * alongside the GDI device name - and geometry is the only field the two APIs share.
 *
 * It is exact rather than approximate, and it stays exact even where the figures are virtualised:
 * this process is system-DPI-aware, so Windows hands it GetMonitorInfo rectangles converted into the
 * system DPI's space, and SDL - which does not enable its own DPI scaling - reports those same
 * converted figures instead of the panel's. Both sides carry the same conversion, so matching one
 * against the other is still sound; it is comparing either of them against the panel's real mode
 * that would not be. See ReadPanelMode() for that side of it.
 *
 * Returns 0 when no monitor covers the rectangle, which the caller treats as "not determined".
 */
/*
 * Narrow copy of a wide string, for the device names above.
 *
 * CP_UTF8 rather than a byte truncation, even though "\\.\DISPLAYn" is plain ASCII by construction:
 * the rule that non-ASCII text does not go through a lossy conversion is worth keeping without
 * exceptions, and the cost here is nothing.
 */
static std::string NarrowFromWide(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0')
        return std::string();

    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1)
        return std::string();

    std::string result(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], needed, nullptr, nullptr);
    return result;
}

struct DisplayNumberQuery
{
    const SDL_Rect* rect;
    int number;
    bool primary;               // MONITORINFOF_PRIMARY on the monitor that matched
    std::string deviceName;     // "\\\\.\\DISPLAYn", for the per-display mode query below
    int panelWidth;             // current mode from the display driver: physical pixels
    int panelHeight;
};

/*
 * DEVMODEW is declared by wingdi.h, which windows.h pulls in, so the driver's own view of a
 * display's mode needs no interop of its own here.
 *
 * Worth having beside the SDL figures because the two are not always the same numbers, and the
 * reason is narrower than it first looks. This process asks for *system* DPI awareness
 * (SDL_HINT_WINDOWS_DPI_AWARENESS, set to "system" in main()), and Windows only reports a monitor's
 * true geometry to such a process when that monitor is at the system DPI - which the primary display
 * defines. Any monitor at a different scaling has its coordinates converted into the system DPI's
 * space before the process ever sees them.
 *
 * On the machine this was found on: a 4K primary at 150% sets the system DPI to 144, so a 1080p
 * secondary at 100% is reported as 2880x1620 - 1920 x 144/96. Note what that example means: the
 * secondary's own scaling is irrelevant, 100% or otherwise. What matters is that it differs from the
 * primary's, and the direction follows whichever is the system DPI.
 *
 * EnumDisplaySettings is not subject to that conversion, so it returns the panel's own mode. That is
 * the number to size a window with, and the number the display's OSD and Display Settings agree on.
 * SDL does not compensate for it either: its own DPI scaling is off unless SDL_HINT_WINDOWS_DPI_SCALING
 * is set, which this program does not set, so SDL passes the system's virtualised figures through.
 */
static void ReadPanelMode(const wchar_t* deviceName, int& width, int& height)
{
    DEVMODEW devmode{};
    devmode.dmSize = sizeof(devmode);

    if (EnumDisplaySettingsW(deviceName, ENUM_CURRENT_SETTINGS, &devmode))
    {
        width = static_cast<int>(devmode.dmPelsWidth);
        height = static_cast<int>(devmode.dmPelsHeight);
    }
}

static BOOL CALLBACK FindDisplayNumberProc(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
{
    DisplayNumberQuery* query = reinterpret_cast<DisplayNumberQuery*>(userData);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return TRUE;        // keep looking

    const RECT& r = info.rcMonitor;
    if (query->rect->x != r.left || query->rect->y != r.top ||
        query->rect->w != (r.right - r.left) || query->rect->h != (r.bottom - r.top))
        return TRUE;

    // "\\.\DISPLAY12" -> 12. The prefix carries no digits, so the digits in the name are the number.
    int number = 0;
    for (const wchar_t* c = info.szDevice; *c != L'\0'; ++c)
    {
        if (*c >= L'0' && *c <= L'9')
            number = number * 10 + (*c - L'0');
    }
    query->number = number;
    query->primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

    query->deviceName = NarrowFromWide(info.szDevice);
    ReadPanelMode(info.szDevice, query->panelWidth, query->panelHeight);

    return FALSE;           // found it; stop enumerating
}

static DisplayNumberQuery WindowsDisplayInfoForRect(const SDL_Rect& rect)
{
    DisplayNumberQuery query{ &rect, 0, false, std::string(), 0, 0 };
    EnumDisplayMonitors(nullptr, nullptr, FindDisplayNumberProc, reinterpret_cast<LPARAM>(&query));
    return query;
}

#endif // _WIN32

const std::vector<DisplayInfo>& GetDisplays()
{
    static std::vector<DisplayInfo> cache;
    static Uint32 cacheTick = 0;

    /*
     * Rebuilt on the same two-second cadence as the EDID info, and for a related reason: the mode
     * search behind hasArcadeMode walks every mode of every output, which is hundreds of driver
     * calls and not something to repeat on every frame. Two seconds is short enough that attaching
     * or removing a monitor while the settings window is open is picked up without a restart.
     *
     * An empty result is not cached, because the usual cause is that SDL's video subsystem is not
     * up yet rather than that the machine has no outputs.
     */
    const Uint32 now = SDL_GetTicks();
    if (!cache.empty() && (now - cacheTick) < 2000)
        return cache;

    const int count = SDL_GetNumVideoDisplays();
    if (count < 1)
        return cache;

    // The rate the arcade-exact routes look for. SDL reports integer rates, so this only decides
    // which of 57 and 58 is preferred - see FindArcadeDisplayModeImpl().
    const float seekRateHz = 57.5242f;

    std::vector<DisplayInfo> found;
    found.reserve(count);

    bool everyNumberKnown = true;

    for (int i = 0; i < count; i++)
    {
        DisplayInfo info;
        info.sdlIndex = i;

        const char* name = SDL_GetDisplayName(i);
        info.name = (name != nullptr && *name != '\0') ? name : ("Display " + std::to_string(i + 1));

        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(i, &bounds) == 0)
        {
            info.width = bounds.w;
            info.height = bounds.h;
#ifdef _WIN32
            const DisplayNumberQuery match = WindowsDisplayInfoForRect(bounds);
            info.number = match.number;
            info.primary = match.primary;
            info.deviceName = match.deviceName;
            info.panelWidth = match.panelWidth;
            info.panelHeight = match.panelHeight;
#else
            // SDL orders the system's main display first, and documents index 0 as the primary; there
            // is no per-display flag to ask for outside Windows.
            info.primary = (i == 0);
#endif
        }

        SDL_DisplayMode desktop{};
        if (SDL_GetDesktopDisplayMode(i, &desktop) == 0)
            info.desktopHz = desktop.refresh_rate;

        SDL_DisplayMode arcade{};
        info.hasArcadeMode = FindArcadeDisplayModeImpl(seekRateHz, i, arcade, true);

        if (info.number == 0)
            everyNumberKnown = false;

        found.push_back(info);
    }

    /*
     * If any output could not be matched to a Windows number then none of them can be used. A list
     * holding a mixture of real numbers and zeros cannot be stored in a single setting or handed to
     * the helper script without the two disagreeing about which monitor is meant, because a zero is
     * the same value as "not chosen". Numbering them 1..n in SDL's order instead keeps the setting
     * self-consistent for the run; those numbers are this program's own rather than Windows', which
     * is why it is the fallback and not the normal case.
     */
    if (!everyNumberKnown)
    {
        for (size_t i = 0; i < found.size(); i++)
            found[i].number = static_cast<int>(i) + 1;
    }

    std::sort(found.begin(), found.end(),
        [](const DisplayInfo& a, const DisplayInfo& b) { return a.number < b.number; });

    /*
     * Logged only when the list actually changes, not on every rebuild.
     *
     * The cache below expires every couple of seconds so that plugging a monitor in is noticed without a
     * restart, which means this code runs repeatedly during a session. Printing unconditionally would
     * write a block of lines every two seconds - enough noise to bury anything else in the log, and a
     * log nobody reads is worse than no log. A signature of the fields that matter gives the useful
     * behaviour instead: silence while the setup is unchanged, one block when it is not.
     */
    std::string signature;
    for (const DisplayInfo& display : found)
    {
        signature += Util::Format() << display.number << '|' << display.name << '|'
                                    << display.width << 'x' << display.height << '|'
                                    << display.panelWidth << 'x' << display.panelHeight << '|'
                                    << display.desktopHz << '|' << (display.primary ? 1 : 0) << '|'
                                    << (display.hasArcadeMode ? 1 : 0) << '\n';
    }

    static std::string loggedSignature;
    const bool changed = (signature != loggedSignature);
    loggedSignature = signature;

    /*
     * One line per output, at info level.
     *
     * The two resolutions are both printed when they differ, because the difference is the whole
     * reason both are kept: SDL's figure is in the coordinate space Windows virtualises to for this
     * process (system DPI aware, so scaled by the primary monitor's scaling), and the driver's is the
     * panel's own. A reader comparing a screenshot of Display Settings against the settings window
     * needs to know which of the two they are looking at, and a support log is where that question
     * gets answered.
     */
    if (changed)
    {
        for (const DisplayInfo& display : found)
        {
            if (display.panelWidth > 0 && (display.panelWidth != display.width || display.panelHeight != display.height))
            {
                InfoLog("Display %d: %s  %dx%d @%d Hz  (Windows space %dx%d)%s%s",
                    display.number, display.name.c_str(),
                    display.panelWidth, display.panelHeight, display.desktopHz,
                    display.width, display.height,
                    display.primary ? "  [primary]" : "",
                    display.hasArcadeMode ? "  [57/58 Hz mode available]" : "");
            }
            else
            {
                InfoLog("Display %d: %s  %dx%d @%d Hz%s%s",
                    display.number, display.name.c_str(),
                    display.width, display.height, display.desktopHz,
                    display.primary ? "  [primary]" : "",
                    display.hasArcadeMode ? "  [57/58 Hz mode available]" : "");
            }
        }
    }

    cache = found;
    cacheTick = now;
    return cache;
}

int GetConfiguredDisplaySdlIndex()
{
    const int number = s_runtime_config["DisplayNumber"].ValueAsDefault<int>(0);
    if (number <= 0)
        return 0;

    for (const DisplayInfo& display : GetDisplays())
    {
        if (display.number == number)
            return display.sdlIndex;
    }

    /*
     * The configured output is not attached - a configuration written with a second monitor
     * connected, opened later on a machine without it. Falling back to the primary display rather
     * than refusing to start is the whole point of having a fallback: the alternative is a program
     * that will not run at all until the user finds and edits this setting.
     */
    return 0;
}

/*
 * The size a fullscreen window on the configured output should be.
 *
 * This is deliberately not the requested resolution. That setting says how many pixels to render at,
 * and it is also used as the window size in windowed mode, where it is the user's business how much
 * of the screen the window takes. Applied to a fullscreen window it is a bug waiting to happen: the
 * two are only the same number by coincidence, and a render resolution above the panel's (a
 * supersampled one, which is the normal case on a large monitor) makes a window larger than the screen
 * it is going fullscreen on. The picture is then cropped - the top and bottom cut off, because the
 * oversized window is centred - and nothing warns about it.
 *
 * Measured, on a 4K primary with a 1080p secondary, render resolution 2976x2304:
 *
 *   primary (3840x2160)    window 3840x2160   integer scaling 5x   correct
 *   secondary (1920x1080)  window 2976x2304   integer scaling 6x   cropped
 *
 * The primary worked only because entering fullscreen happened to resize the window to the display
 * mode; the secondary did not, because the mode it was asked for was not a size the panel has. Which
 * is the second half of the problem - see the caller in ResolveFullScreenMode(), where SDL's own
 * figure for a display's desktop mode can be a DPI-virtualised size rather than the panel's.
 *
 * So the panel's own mode is used where it could be read, and SDL's desktop mode otherwise. Returns
 * false when neither is available, in which case the caller leaves the window as it is.
 */
static bool GetConfiguredFullscreenSize(int& width, int& height)
{
    const int displayIndex = GetConfiguredDisplaySdlIndex();

    for (const DisplayInfo& display : GetDisplays())
    {
        if (display.sdlIndex == displayIndex && display.panelWidth > 0 && display.panelHeight > 0)
        {
            width = display.panelWidth;
            height = display.panelHeight;
            return true;
        }
    }

    SDL_DisplayMode desktop{};
    if (SDL_GetDesktopDisplayMode(displayIndex, &desktop) == 0 && desktop.w > 0 && desktop.h > 0)
    {
        width = desktop.w;
        height = desktop.h;
        return true;
    }

    return false;
}

IntegerScaleInfo GetIntegerScaleInfo()
{
    IntegerScaleInfo info;

    /*
     * The same panel override CalcScreenGeometry applies, in the same order, so that what is reported
     * here is what the geometry code actually used rather than an independent guess. The window size
     * is the fallback in both places, which is why it is read the same way.
     */
    const int displayIndex = GetConfiguredDisplaySdlIndex();
    int panelWidth = 0;
    int panelHeight = 0;
    for (const DisplayInfo& display : GetDisplays())
    {
        if (display.sdlIndex == displayIndex && display.panelWidth > 0 && display.panelHeight > 0)
        {
            panelWidth = display.panelWidth;
            panelHeight = display.panelHeight;
            info.panelKnown = true;
            break;
        }
    }

    if (panelWidth <= 0 || panelHeight <= 0)
    {
        // No panel mode: fall back to the size a fullscreen window would be, which is what the
        // geometry code ends up measuring against in this case.
        if (!GetConfiguredFullscreenSize(panelWidth, panelHeight))
            return info;
    }

    if (panelWidth <= 0 || panelHeight <= 0)
        return info;

    float nativeFit = float(panelWidth) / 496.0f;
    const float nativeFitY = float(panelHeight) / 384.0f;
    if (nativeFitY < nativeFit)
        nativeFit = nativeFitY;

    int multiple = (int)nativeFit;
    if (multiple < 1)
        multiple = 1;

    info.valid = true;
    info.active = s_runtime_config["IntegerScale"].ValueAsDefault<bool>(false)
        && !s_runtime_config["Stretch"].ValueAsDefault<bool>(false);
    info.multiple = multiple;
    info.visibleWidth = 496 * multiple;
    info.visibleHeight = 384 * multiple;
    info.panelWidth = panelWidth;
    info.panelHeight = panelHeight;
    info.nextMultiple = multiple + 1;
    info.nextVisibleHeight = 384 * info.nextMultiple;

    return info;
}

#ifdef _WIN32

/*
 * What each attached display declares about itself, for the settings window's sync notes.
 *
 * Read at run time, never assumed: the model name comes from the display's own EDID (descriptor 0xFC)
 * and the ranges from its timing descriptor (0xFD) and any adaptive-sync vendor block. Three displays
 * on three machines give three different answers, which is the point - nothing here is tied to any
 * particular model.
 *
 * WHY THIS EXISTS, AND WHAT IT IS NOT
 *
 * It is not a variable-refresh-rate check, and it must not be presented as one. Two things are worth
 * keeping straight:
 *
 *   * The 0xFD descriptor this reads is the display's *supported timing range* - the span of vertical
 *     rates the panel will lock to. Every monitor has one; a 144 Hz panel without any VRR at all
 *     reports 48-144 just as a VRR one does. So it cannot distinguish them, and reporting it as
 *     "VRR supported" would be wrong.
 *
 *   * VRR capability is declared by most modern displays in an EDID vendor block - the AMD/VESA
 *     Adaptive-Sync block, OUI 00-00-1A, which carries the range. That declaration is worth reading,
 *     and the settings window does read it, but a declaration proves support while its absence proves
 *     nothing: a display may support VRR without saying so in its EDID, and older AMD HDMI
 *     implementations are the known case. So a range found this way is reported as fact, and its
 *     absence is reported as nothing at all.
 *
 * What it *is* good for is the one thing the fixed-arcade-rate option cannot currently answer: when no
 * 57/58 Hz display mode exists, would creating one work? A custom resolution below the panel's timing
 * range will be refused, so the range turns "go and create one" into either "this should be accepted"
 * or "this display cannot do it". That is a necessary condition, not a sufficient one, and it is
 * worded that way in the interface.
 *
 * Read through WMI rather than the registry directly because the registry keeps an entry for every
 * display that has ever been connected - this machine has seven, of which two are attached - and
 * WmiMonitorBasicDisplayParams lists only the active ones.
 */
static bool ReadMonitorEdid(const std::wstring& instanceName, std::vector<uint8_t>& edid)
{
    // WMI names an instance as "DISPLAY\SAME06F\5&15db56b1&0&UID8449_0"; the registry path is the same
    // without the trailing "_0", under the enum root.
    std::wstring path = L"SYSTEM\\CurrentControlSet\\Enum\\" + instanceName;
    const size_t underscore = path.find_last_of(L'_');
    if (underscore != std::wstring::npos)
        path.resize(underscore);
    path += L"\\Device Parameters";

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    DWORD type = 0;
    DWORD size = 0;
    bool ok = false;
    if (RegQueryValueExW(key, L"EDID", nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        type == REG_BINARY && size >= 128)
    {
        edid.resize(size);
        ok = RegQueryValueExW(key, L"EDID", nullptr, &type, edid.data(), &size) == ERROR_SUCCESS;
        if (ok)
            edid.resize(size);
    }
    RegCloseKey(key);
    return ok;
}

/*
 * Pulls the monitor name (0xFC) and the vertical timing range (0xFD) out of a base EDID block.
 *
 * Both live in the four 18-byte descriptors at fixed offsets 54, 72, 90 and 108, each identified by a
 * tag in its fourth byte. The range fields carry offset flags in EDID 1.4 for rates above 255 Hz - none
 * of which a Model 3 build will meet, but a display reporting it would otherwise be read as a lower
 * maximum than it has, so it is handled.
 */
static bool ParseEdidRange(const std::vector<uint8_t>& edid, std::string& name, int& vMin, int& vMax)
{
    if (edid.size() < 128)
        return false;

    bool haveRange = false;
    for (int i = 54; i <= 108; i += 18)
    {
        if (edid[i] != 0 || edid[i + 1] != 0 || edid[i + 2] != 0)
            continue;

        const uint8_t tag = edid[i + 3];
        if (tag == 0xFD)
        {
            const uint8_t flags = edid[i + 4];
            vMin = edid[i + 5];
            vMax = edid[i + 6];
            if (flags & 0x02)
                vMax += 255;
            if ((flags & 0x03) == 0x03)
                vMin += 255;
            haveRange = true;
        }
        else if (tag == 0xFC)
        {
            // 13 bytes of text, terminated by a newline and padded with spaces.
            for (int k = 5; k < 18; k++)
            {
                const uint8_t c = edid[i + k];
                if (c == 0x0A || c == 0)
                    break;
                name += (char)c;
            }
            while (!name.empty() && name.back() == ' ')
                name.pop_back();
        }
    }
    return haveRange;
}

/*
 * The variable-refresh range a display declares, if it declares one.
 *
 * Some displays state it in a CEA-861 vendor-specific data block (data block tag 3) carrying the
 * AMD/VESA Adaptive-Sync OUI 00-00-1A, whose payload holds the minimum and maximum vertical rate. A
 * monitor connected over DisplayPort and advertising adaptive sync normally has one.
 *
 * WHAT THIS CAN AND CANNOT SAY - the asymmetry matters more than the value.
 *
 * A declared range is solid: the display is telling the source it can vary, and within what bounds.
 * The *absence* of a declared range means nothing at all. VRR can be supported without being declared
 * here, and the known case is older AMD HDMI FreeSync, where the capability lived in the driver rather
 * than in the display's EDID. So this must never be used to conclude that a display cannot do VRR -
 * only to state, when a range is present, that it can and over what span.
 *
 * That is the opposite of the 0xFD timing range above, which every display has and which says nothing
 * about VRR. Reporting either one as "VRR supported" in the general case would be wrong, for different
 * reasons.
 */
static bool ParseEdidVrrRange(const std::vector<uint8_t>& edid, int& vMin, int& vMax)
{
    // Walk every 128-byte block: the CEA-861 extension carries the vendor blocks.
    for (size_t base = 0; base + 128 <= edid.size(); base += 128)
    {
        // Only the CEA-861 extension (block tag 0x02 by convention, and [0] is its own tag).
        if (base == 0 || edid[base] != 0x02)
            continue;

        const uint8_t dtdOffset = edid[base + 2];
        size_t p = base + 4;
        const size_t end = base + (dtdOffset < 4 ? 127 : dtdOffset);

        while (p < end && p + 3 <= edid.size())
        {
            const uint8_t header = edid[p];
            if (header == 0)
                break;

            const uint8_t tag = (header & 0xE0) >> 5;
            const uint8_t len = header & 0x1F;
            if (len == 0 || p + 1 + len > edid.size())
                break;

            // tag 3 is a top-level vendor-specific data block. The extended-tag family (tag 7) also has
            // vendor blocks, but the adaptive-sync declaration is the top-level one - looking only at
            // tag 7 misses it, which is a mistake worth naming because it is easy to make.
            if (tag == 3 && len >= 8)
            {
                // OUI is stored least-significant byte first.
                const uint8_t* d = &edid[p + 1];
                if (d[0] == 0x1A && d[1] == 0x00 && d[2] == 0x00)
                {
                    // Payload: OUI(3), version, flags, min rate, max rate.
                    vMin = d[5];
                    vMax = d[6];
                    if (vMin > 0 && vMax >= vMin)
                        return true;
                }
            }
            p += 1 + len;
        }
    }
    return false;
}

#endif  // _WIN32

std::vector<DisplayTimingInfo> GetDisplayTimingInfo()
{
#ifndef _WIN32
    // The EDID path below is Windows-specific. Nothing else needs it, and the caller omits the line
    // when this returns empty, so there is no need for a second implementation.
    return std::vector<DisplayTimingInfo>();
#else
    std::vector<std::wstring> instances;
    std::vector<DisplayTimingInfo> result;

    /*
     * COM has to be available. The apartment model is left alone if the thread already chose one -
     * CoInitializeEx reports RPC_E_CHANGED_MODE in that case, which is not a failure here because
     * something else initialised COM and the calls below still work.
     */
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool weInitialisedCom = SUCCEEDED(coInit);
    if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE)
        return std::vector<DisplayTimingInfo>();

    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* enumerator = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<LPVOID*>(&locator));
    if (SUCCEEDED(hr) && locator != nullptr)
    {
        BSTR ns = SysAllocString(L"\\\\.\\root\\wmi");
        if (ns != nullptr)
        {
            hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0L, nullptr, nullptr, &services);
            SysFreeString(ns);
        }
        else
            hr = E_OUTOFMEMORY;

        if (SUCCEEDED(hr) && services != nullptr)
        {
            CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

            BSTR cls = SysAllocString(L"WmiMonitorBasicDisplayParams");
            if (cls != nullptr)
            {
                hr = services->CreateInstanceEnum(cls, 0, nullptr, &enumerator);
                SysFreeString(cls);
            }

            if (SUCCEEDED(hr) && enumerator != nullptr)
            {
                IWbemClassObject* objects[8] = {};
                for (;;)
                {
                    DWORD returned = 0;
                    if (FAILED(enumerator->Next(10000, 8, objects, &returned)) || returned == 0)
                        break;

                    for (DWORD i = 0; i < returned; i++)
                    {
                        VARIANT active;
                        VariantInit(&active);
                        VARIANT name;
                        VariantInit(&name);

                        bool isActive = true;   // absent means the class did not report it; assume yes
                        if (SUCCEEDED(objects[i]->Get(L"Active", 0, &active, nullptr, nullptr)) &&
                            active.vt == VT_BOOL)
                            isActive = (active.boolVal != VARIANT_FALSE);

                        // Only the attached ones. A stale EDID would describe a display that is not
                        // there, and the range it reports would be about the wrong panel.
                        if (isActive &&
                            SUCCEEDED(objects[i]->Get(L"InstanceName", 0, &name, nullptr, nullptr)) &&
                            name.vt == VT_BSTR && name.bstrVal != nullptr)
                            instances.emplace_back(name.bstrVal);

                        VariantClear(&name);
                        VariantClear(&active);
                        objects[i]->Release();
                    }
                }
            }
        }
    }

    if (enumerator != nullptr) enumerator->Release();
    if (services != nullptr) services->Release();
    if (locator != nullptr) locator->Release();
    if (weInitialisedCom)
        CoUninitialize();

    /*
     * One entry per display, in the order WMI listed them.
     *
     * Nothing is concluded here beyond what the EDID states: the timing range, and a declared
     * adaptive-sync range when there is one. Whether 57.5 Hz fits inside the timing range is worked out
     * by the caller, which also owns the wording.
     */
    for (const std::wstring& instance : instances)
    {
        std::vector<uint8_t> edid;
        if (!ReadMonitorEdid(instance, edid))
            continue;

        std::string name;
        int vMin = 0;
        int vMax = 0;
        if (!ParseEdidRange(edid, name, vMin, vMax))
            continue;

        DisplayTimingInfo info;
        info.name = name.empty() ? "Display" : name;
        info.minHz = vMin;
        info.maxHz = vMax;
        info.declaresVrr = ParseEdidVrrRange(edid, info.vrrMinHz, info.vrrMaxHz);
        result.push_back(info);
    }

    /*
     * Logged once per fetch. It is the kind of figure that belongs in a bug report - "what does the
     * emulator think my display's timing range is" is not answerable from a screenshot - and the fetch
     * happens at most once per run, so it cannot flood the log.
     */
    if (!result.empty())
    {
        std::string line;
        for (const DisplayTimingInfo& info : result)
        {
            if (!line.empty())
                line += "; ";
            line += Util::Format() << info.name << " " << info.minHz << "-" << info.maxHz << " Hz";
            if (info.declaresVrr)
                line += Util::Format() << " (declares VRR " << info.vrrMinHz << "-" << info.vrrMaxHz << " Hz)";
        }
        InfoLog("Display EDID: %s.", line.c_str());
    }
    else
    {
        InfoLog("Display EDID: none could be read.");
    }

    return result;
#endif  // _WIN32
}

/*
 * CreateGLScreen():
 *
 * Creates an OpenGL display surface of the requested size. xOffset and yOffset
 * are used to return a display surface offset (for OpenGL viewport commands)
 * because the actual drawing area may need to be adjusted to preserve the
 * Model 3 aspect ratio. The new resolution will be passed back as well -- both
 * the adjusted viewable area resolution and the total resolution.
 *
 * NOTE: keepAspectRatio should always be true. It has not yet been tested with
 * the wide screen hack.
 */
static Result CreateGLScreen(bool coreContext, const std::string &caption, bool focusWindow, unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio, bool fullScreen)
// The quadRendering parameter is gone: it existed only to decide between an OpenGL 4.5 and a 4.1
// context, and since the quad shader no longer needs 4.30 features there is nothing to decide.
// Removed rather than left unused so the signature does not advertise a choice that no longer exists.
{
  GLenum err;

  // Call only once per program session (this is because of issues with
  // DirectInput when the window is destroyed and a new one created). Use
  // ResizeGLScreen() to change resolutions instead.
  if (s_window != nullptr)
  {
    return ErrorLog("Internal error: CreateGLScreen() called more than once");
  }

  // Initialize video subsystem
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    return ErrorLog("Unable to initialize SDL video subsystem: %s\n", SDL_GetError());

  // Important GL attributes
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);
  if(s_runtime_config["New3DEngine"].ValueAsDefault<bool>(true))
  {
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
  }
  else
  {
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,8);
  }
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);

  if (coreContext) {
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

      /*
       * 4.1, unconditionally.
       *
       * This used to ask for 4.5 when quad rendering was on, because the quad shader used arrays of
       * arrays - a GLSL 4.30 feature. That shader now indexes a flat array instead (see
       * R3DShaderQuads.h), so 4.1 is enough for both modes and the stricter request is gone.
       *
       * Worth having as low as it can be, because this is the renderer the failed-Vulkan fallback
       * lands on: the machines that cannot provide Vulkan 1.3 are disproportionately the older ones,
       * and asking for 4.5 would shut out GPUs that are perfectly capable of running the emulator.
       * It also makes the "OpenGL 4.1" figure in README.txt true rather than aspirational.
       */
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  }

  // Created on the configured output rather than on the primary one. The window's display is what
  // the fullscreen calls act on, so for the exclusive modes this has to be decided here: resolving
  // the flavour afterwards would otherwise apply a custom timing to whichever monitor the window
  // landed on, which with SDL_WINDOWPOS_CENTERED is always the primary display.
  const int displayIndex = GetConfiguredDisplaySdlIndex();

  // Set video mode
  s_window = SDL_CreateWindow(caption.c_str(), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), *totalXResPtr, *totalYResPtr, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | (fullScreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
  if (nullptr == s_window)
  {
    ErrorLog("Unable to create an OpenGL display: %s\n", SDL_GetError());
    return Result::FAIL;
  }
  SetWindowIcon(s_window);

  if (focusWindow)
  {
    SDL_RaiseWindow(s_window);
  }

  // Create OpenGL context
  SDL_GLContext context = SDL_GL_CreateContext(s_window);
  if (nullptr == context)
  {
    ErrorLog("Unable to create OpenGL context: %s\n", SDL_GetError());
    return Result::FAIL;
  }
  s_glContext = context;

  // Set vsync
  SDL_GL_SetSwapInterval(s_runtime_config["VSync"].ValueAsDefault<bool>(false) ? 1 : 0);

  // Set the context as the current window context
  SDL_GL_MakeCurrent(s_window, context);

  // Initialize GLEW, allowing us to use features beyond OpenGL 1.2
  err = glewInit();
  if (GLEW_OK != err)
  {
    ErrorLog("OpenGL initialization failed: %s\n", glewGetErrorString(err));
    return Result::FAIL;
  }

  // print some basic GPU info
  GLint profile = 0;
  glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);

  const char *glVendor = (const char *)glGetString(GL_VENDOR);
  const char *glRenderer = (const char *)glGetString(GL_RENDERER);
  const char *glVersion = (const char *)glGetString(GL_VERSION);

  InfoLog("OpenGL Video Card: %s (%s)", glRenderer ? glRenderer : "Unknown", glVendor ? glVendor : "Unknown");
  InfoLog("OpenGL Version: %s", glVersion ? glVersion : "Unknown");
  printf("GPU info: %s - %s (%s) ", glRenderer ? glRenderer : "Unknown", glVersion ? glVersion : "Unknown", glVendor ? glVendor : "Unknown");

  if (profile & GL_CONTEXT_CORE_PROFILE_BIT) {
      printf("(core profile)");
  }

  if (profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) {
      printf("(compatibility profile)");
  }

  printf("\n\n");

  //glDebugMessageCallback(DebugCallback, NULL);
  //glDebugMessageControl(GL_DONT_CARE,GL_DONT_CARE,GL_DONT_CARE, 0, 0, GL_TRUE);
  //glEnable(GL_DEBUG_OUTPUT);

  return SetGLGeometry(xOffsetPtr, yOffsetPtr, xResPtr, yResPtr, totalXResPtr, totalYResPtr, keepAspectRatio);
}

static void DestroyGLScreen()
{
  if (s_vkContext != nullptr)
  {
    delete s_vkContext;
    s_vkContext = nullptr;
    if (s_window != nullptr)
      SDL_DestroyWindow(s_window);
    s_window = nullptr;
    return;
  }
  if (s_window != nullptr)
  {
    // Delete the context belonging to the game window, not whichever context happens to be
    // current: the settings GUI keeps its own window and GL context alive across sessions.
    if (s_glContext != nullptr)
    {
      SDL_GL_DeleteContext(s_glContext);
      s_glContext = nullptr;
    }
    SDL_DestroyWindow(s_window);
    s_window = nullptr;
  }
}

static Result CreateVulkanScreen(const std::string &caption, bool focusWindow, unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio, bool fullScreen)
{
  if (s_window != nullptr)
    return ErrorLog("Internal error: CreateVulkanScreen() called more than once");
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    return ErrorLog("Unable to initialize SDL video subsystem: %s\n", SDL_GetError());

  // Same reasoning as CreateGLScreen(): the window has to be on the configured output before any
  // fullscreen mode is applied, because the mode applies to the window's display.
  const int displayIndex = GetConfiguredDisplaySdlIndex();
  s_window = SDL_CreateWindow(caption.c_str(), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), *totalXResPtr, *totalYResPtr, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | (fullScreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
  if (nullptr == s_window)
  {
    ErrorLog("Unable to create a Vulkan display: %s\n", SDL_GetError());
    return Result::FAIL;
  }
  SetWindowIcon(s_window);
  if (focusWindow)
    SDL_RaiseWindow(s_window);

  s_vkContext = new Vulkan3D::CVulkanContext();
  if (Result::OKAY != s_vkContext->InitDevice(s_window, s_runtime_config["VkValidation"].ValueAsDefault<bool>(false), s_runtime_config["VSync"].ValueAsDefault<bool>(false), s_runtime_config["VulkanDevice"].ValueAsDefault<std::string>("")))
    return Result::FAIL;
  CalcScreenGeometry(xOffsetPtr, yOffsetPtr, xResPtr, yResPtr, totalXResPtr, totalYResPtr, keepAspectRatio);
  return Result::OKAY;
}

// Chooses the fullscreen flavour actually used. Exclusive fullscreen is only entered when a
// 57/58 Hz mode was successfully prepared by SetFullScreenRefreshRate(); everything else keeps
// the borderless desktop path that has always been used here (and that avoids the mode-switch
// hangs seen on some drivers). If the exclusive switch is refused we fall back to borderless
// rather than failing.
static const char* FullScreenModeName(FullScreenMode mode)
{
    switch (mode)
    {
    case FullScreenMode::ExclusiveDesktop: return "exclusive fullscreen (G-SYNC/VRR)";
    case FullScreenMode::ExclusiveArcade:  return "exclusive fullscreen (fixed arcade rate)";
    default:                               return "borderless fullscreen (composited)";
    }
}

// The presenting mode as the statistics readout should describe it.
//
// Built from the mode that was actually entered rather than the one the configuration asked for,
// because the two come apart whenever the driver refuses the exclusive switch - see the fallback in
// ApplyFullScreenMode. A readout that repeated the configuration would tell a user with an unsupported
// display that they have G-SYNC while the session is running borderless, which is worse than saying
// nothing at all.
static StatsSyncMode StatsSyncModeForCurrentSession()
{
    if (!s_runtime_config["FullScreen"].ValueAs<bool>())
        return StatsSyncMode::Windowed;

    switch (s_fullScreenMode)
    {
    case FullScreenMode::ExclusiveArcade:  return StatsSyncMode::ExclusiveFixedRate;
    case FullScreenMode::ExclusiveDesktop: return StatsSyncMode::ExclusiveVrr;
    default:                               return StatsSyncMode::Borderless;
    }
}

// Decides which fullscreen flavour to use, once, from the configuration:
//
//   ExclusiveRefreshRate > 0   prefer a fixed 57/58 Hz display mode (custom timing). If none
//                              exists we still go exclusive so G-SYNC can match the rate.
//   ExclusiveFullscreen = 1    no custom timing wanted; go exclusive and let G-SYNC/FreeSync do
//                              the rate matching. This is the simpler route for a VRR panel: the
//                              panel tracks whatever rate we present at, so no custom resolution
//                              is needed at all.
//   both off (default)         borderless fullscreen, exactly as before.
//
// Note that VRR only holds a frame if vsync is on (FIFO present). With vsync off the driver is
// free to tear and the panel will not track our rate, so a warning is logged in that case.
static FullScreenMode ResolveFullScreenMode()
{
    const float refreshRateHz = s_runtime_config["ExclusiveRefreshRate"].ValueAsDefault<float>(0.0f);
    const bool wantExclusive = s_runtime_config["ExclusiveFullscreen"].ValueAsDefault<bool>(false);

    // Which output this session drives. Resolved once here and reported in the log, because every
    // check below is a property of one monitor's mode list rather than of "the display" - a custom
    // 57.524 Hz timing belongs to the output it was created for, and looking for it on the wrong one
    // reports it as missing when it is not.
    const int displayIndex = GetConfiguredDisplaySdlIndex();
    const int displayNumber = s_runtime_config["DisplayNumber"].ValueAsDefault<int>(0);

    if (displayNumber > 0)
        InfoLog("Display: using output %d (SDL display %d).", displayNumber, displayIndex);

    if (refreshRateHz <= 0.0f && !wantExclusive)
        return FullScreenMode::Borderless;

    // Emulate at the arcade rate when a fixed arcade timing was asked for, so the game runs at the
    // speed the panel will scan at and the two are 1:1.
    //
    // Deliberately not applied to the G-SYNC/VRR path, even though that path also ends up in exclusive
    // fullscreen. There the panel follows whatever rate is presented, so pacing at 57.524 Hz would work
    // too - but the Refresh Rate setting on the Video page is what says which rate the user wants, and
    // it offers 57.5 Hz and 60 Hz with 60 marked as recommended. Overriding it here would silently
    // discard that choice and leave the setting looking broken.
    //
    // Note what is deliberately *not* derived from this setting: the emulated CPU's per-frame cycle
    // budget is fixed at the arcade's 57.524160 Hz (see CModel3::RunMainBoardFrame). So the two are
    // independent, and choosing 60 Hz here makes the game run 4.3% fast rather than running at the
    // right speed with a different frame cadence. That is why the statistics readout prints the target
    // rate beside the arcade's and works out the difference - the number is the honest description of
    // a choice, and it should be a visible one.
    if (refreshRateHz > 0.0f)
        s_runtime_config.Get("RefreshRate").SetValue(refreshRateHz);

    if (!s_runtime_config["VSync"].ValueAsDefault<bool>(true))
        InfoLog("Exclusive fullscreen: VSync is off; G-SYNC/VRR will not hold frames. Enable VSync for a locked refresh rate.");

    if (refreshRateHz > 0.0f)
    {
        SDL_DisplayMode mode{};
        if (FindArcadeDisplayMode(refreshRateHz, displayIndex, mode) && SDL_SetWindowDisplayMode(s_window, &mode) == 0)
        {
            InfoLog("Exclusive fullscreen: fixed mode %ux%u@%i Hz selected (arcade rate %.4f Hz).",
                mode.w, mode.h, mode.refresh_rate, refreshRateHz);
            return FullScreenMode::ExclusiveArcade;
        }
        InfoLog("Exclusive fullscreen: no usable 57/58 Hz display mode; falling back to G-SYNC/VRR at the desktop mode.");
    }

    // Exclusive at the desktop mode. G-SYNC varies the panel to match our present rate, so this
    // needs no custom resolution - only a VRR-capable panel and vsync.
    //
    // The desktop mode is applied explicitly rather than letting SDL pick a mode matching the
    // window size: otherwise SDL chooses whatever it considers closest to the (arbitrary) render
    // resolution, which would run a non-native mode that the display then has to scale. Using the
    // desktop mode keeps the image native and lets us pillarbox the 4:3 picture inside it.
    SDL_DisplayMode desktop{};
    if (SDL_GetDesktopDisplayMode(displayIndex, &desktop) == 0)
    {
        /*
         * The size is replaced with the panel's own where it could be read, keeping SDL's refresh
         * rate. SDL reports this display's desktop mode as 2880x1620 on a machine with a 4K primary
         * at 150% scaling and a 1080p secondary at 100%: the process asks for system DPI awareness,
         * so Windows converts the geometry of any monitor whose scaling differs from the primary's
         * into the system DPI's space, and 1920x1080 becomes 2880x1620. Nothing is wrong with SDL's
         * arithmetic - the number is simply in a different space from the panel's pixels. Asking the
         * driver for a mode of that size fails, and the failure is silent: the window keeps whatever
         * size it had, which for a supersampled render resolution is larger than the screen.
         *
         * GetConfiguredFullscreenSize() explains the rest, and is also what sizes the window.
         */
        int panelWidth = 0;
        int panelHeight = 0;
        if (GetConfiguredFullscreenSize(panelWidth, panelHeight))
        {
            desktop.w = panelWidth;
            desktop.h = panelHeight;
        }

        if (SDL_SetWindowDisplayMode(s_window, &desktop) == 0)
            InfoLog("Exclusive fullscreen: desktop mode %ux%u@%i Hz set on display %d; G-SYNC/VRR should track the emulator rate.",
                desktop.w, desktop.h, desktop.refresh_rate, displayIndex);
        else
            InfoLog("Exclusive fullscreen: could not set the desktop mode (%s); letting SDL choose.", SDL_GetError());
    }
    else
        InfoLog("Exclusive fullscreen: desktop mode unavailable; G-SYNC/VRR will track the emulator rate.");

    return FullScreenMode::ExclusiveDesktop;
}

static Result ApplyFullScreenMode(bool fullScreen)
{
    if (!fullScreen)
    {
        if (SDL_SetWindowFullscreen(s_window, 0) < 0)
        {
            ErrorLog("Unable to enter windowed mode: %s\n", SDL_GetError());
            return Result::FAIL;
        }
        return Result::OKAY;
    }

    if (s_fullScreenMode != FullScreenMode::Borderless &&
        SDL_SetWindowFullscreen(s_window, SDL_WINDOW_FULLSCREEN) == 0)
    {
        return Result::OKAY;
    }

    if (s_fullScreenMode != FullScreenMode::Borderless)
    {
        // The exclusive switch was refused (for example the display configuration changed).
        InfoLog("Exclusive fullscreen: refused by the display driver, falling back to borderless fullscreen.");
        s_fullScreenMode = FullScreenMode::Borderless;
    }

    if (SDL_SetWindowFullscreen(s_window, SDL_WINDOW_FULLSCREEN_DESKTOP) < 0)
    {
        ErrorLog("Unable to enter fullscreen mode: %s\n", SDL_GetError());
        return Result::FAIL;
    }
    return Result::OKAY;
}

// ---------------------------------------------------------------------------
// Window sizing
// ---------------------------------------------------------------------------

/*
 * Sizes the window before the fullscreen mode is applied.
 *
 * Windowed keeps the requested resolution: it is the window the user asked for, and how much of the
 * screen it covers is their business.
 *
 * Fullscreen takes the display's own size instead, and the two are only the same number by
 * coincidence. A render resolution above the panel's - a supersampled one, the normal case on a large
 * monitor - would otherwise produce a window larger than the screen, centred, with the top and bottom
 * of the picture cut off. See GetConfiguredFullscreenSize().
 */
static void ApplyWindowSize(bool fullScreen, unsigned *totalXResPtr, unsigned *totalYResPtr)
{
  if (fullScreen)
  {
    int width = 0;
    int height = 0;
    if (GetConfiguredFullscreenSize(width, height))
    {
      SDL_SetWindowSize(s_window, width, height);
      return;
    }
  }

  SDL_SetWindowSize(s_window, *totalXResPtr, *totalYResPtr);
}

/*
 * The window's size, logged beside the swapchain extent the backend reports separately.
 *
 * They are the same number on a healthy setup, and a pair that disagrees is the signature of the
 * problem above: a window bigger than the display, which crops the picture without any other
 * symptom. Cheap to print and the one fact needed to tell those two cases apart from a log.
 */
static void LogWindowSize(const char *backend)
{
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(s_window, &width, &height);
  InfoLog("%s: window size %dx%d.", backend, width, height);
}

static void ReportRenderInfo(uint32_t aaFactor, unsigned totalXRes, unsigned totalYRes)
{
  StatsRenderInfo info;

  if (s_vkContext)
  {
    /*
     * Vulkan renders at the swapchain extent - the window size, which in fullscreen is the display's
     * and not XResolution - and antialiases with hardware MSAA rather than a scaled buffer. The two
     * figures below are therefore the same number, which is why the readout only prints the second
     * when they differ.
     *
     * The sample count comes from the renderer rather than the configuration because Init() may have
     * reduced it: a driver that cannot support the requested count for both attachments silently ends
     * up at 1x, and reporting the request would show antialiasing that is not happening.
     */
    const VkExtent2D extent = s_vkContext->Extent();
    info.width = extent.width;
    info.height = extent.height;
    info.outputWidth = extent.width;
    info.outputHeight = extent.height;
    info.samples = s_vk3dRenderer ? (int)s_vk3dRenderer->SampleCount() : 1;
    info.method = (info.samples > 1) ? StatsAntiAliasing::Msaa : StatsAntiAliasing::None;
    StatsOsdSetRenderInfo(info);
    return;
  }

  /*
   * OpenGL: supersampling. SuperAA owns a framebuffer at the output size multiplied by the factor -
   * the same aaFactor that was passed to the renderers' Init() just above, so the buffer really is
   * totalXRes * aaFactor wide. That is the resolution the scene is rasterised into, and the one worth
   * reporting; the output size is what it is scaled back down to.
   *
   * With a factor of 1 and no CRT filter SuperAA allocates nothing and the renderers draw straight
   * into the window, so both numbers are the same and no AA is happening. The CRT path is left out of
   * the method here on purpose: it reuses the same framebuffer for the colour filter, which is a
   * different job from antialiasing even though it happens to need the same buffer.
   */
  info.width = totalXRes * aaFactor;
  info.height = totalYRes * aaFactor;
  info.outputWidth = totalXRes;
  info.outputHeight = totalYRes;
  info.samples = (int)aaFactor;
  info.method = (aaFactor > 1) ? StatsAntiAliasing::Supersampling : StatsAntiAliasing::None;
  StatsOsdSetRenderInfo(info);
}

static Result ResizeVulkanScreen(unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio, bool fullScreen)
{
  if (s_vkContext && s_vkContext->Device())
    vkDeviceWaitIdle(s_vkContext->Device());

  ApplyWindowSize(fullScreen, totalXResPtr, totalYResPtr);

  if (Result::OKAY != ApplyFullScreenMode(fullScreen))
    return Result::FAIL;
  CalcScreenGeometry(xOffsetPtr, yOffsetPtr, xResPtr, yResPtr, totalXResPtr, totalYResPtr, keepAspectRatio);
  InfoLog("Vulkan: window mode = %s.",
    fullScreen ? FullScreenModeName(s_fullScreenMode) : "windowed");
  LogWindowSize("Vulkan");
  if (s_vkContext && !s_vkContext->RecreateSwapchain())
    return Result::FAIL;
  return Result::OKAY;
}

static Result ResizeGLScreen(unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio, bool fullScreen)
{
  ApplyWindowSize(fullScreen, totalXResPtr, totalYResPtr);

  if (Result::OKAY != ApplyFullScreenMode(fullScreen))
    return Result::FAIL;

  const Result result = SetGLGeometry(xOffsetPtr, yOffsetPtr, xResPtr, yResPtr, totalXResPtr, totalYResPtr, keepAspectRatio);
  LogWindowSize("OpenGL");
  return result;
}

/*
 * PrintGLInfo():
 *
 * Queries and prints OpenGL information. A full list of extensions can
 * optionally be printed.
 */
static void PrintGLInfo(bool createScreen, bool infoLog, bool printExtensions)
{
  unsigned xOffset, yOffset, xRes=496, yRes=384, totalXRes, totalYRes;
  if (createScreen)
  {
    if (Result::OKAY != CreateGLScreen(s_runtime_config["New3DEngine"].ValueAs<bool>(), SUPERMODEL_TITLE_FULL " - Querying OpenGL Information...", false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false))
    {
      ErrorLog("Unable to query OpenGL.\n");
      return;
    }
  }

  GLint value;
  if (infoLog)  InfoLog("OpenGL information:");
  else             puts("OpenGL information:\n");
  const GLubyte *str = glGetString(GL_VENDOR);
  if (infoLog)  InfoLog("  Vendor                   : %s", str);
  else           printf("  Vendor                   : %s\n", str);
  str = glGetString(GL_RENDERER);
  if (infoLog)  InfoLog("  Renderer                 : %s", str);
  else           printf("  Renderer                 : %s\n", str);
  str = glGetString(GL_VERSION);
  if (infoLog)  InfoLog("  Version                  : %s", str);
  else           printf("  Version                  : %s\n", str);
  str = glGetString(GL_SHADING_LANGUAGE_VERSION);
  if (infoLog)  InfoLog("  Shading Language Version : %s", str);
  else           printf("  Shading Language Version : %s\n", str);
  glGetIntegerv(GL_MAX_ELEMENTS_VERTICES, &value);
  if (infoLog)  InfoLog("  Maximum Vertex Array Size: %d vertices", value);
  else           printf("  Maximum Vertex Array Size: %d vertices\n", value);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
  if (infoLog)  InfoLog("  Maximum Texture Size     : %d texels", value);
  else           printf("  Maximum Texture Size     : %d texels\n", value);
  glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &value);
  if (infoLog)  InfoLog("  Maximum Vertex Attributes: %d", value);
  else           printf("  Maximum Vertex Attributes: %d\n", value);
  glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &value);
  if (infoLog)  InfoLog("  Maximum Vertex Uniforms  : %d", value);
  else           printf("  Maximum Vertex Uniforms  : %d\n", value);
  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value);
  if (infoLog)  InfoLog("  Maximum Texture Img Units: %d", value);
  else           printf("  Maximum Texture Img Units: %d\n", value);
  if (printExtensions)
  {
    str = glGetString(GL_EXTENSIONS);
    char *strLocal = (char *) malloc((strlen((char *) str)+1)*sizeof(char));
    if (NULL == strLocal)
    {
      if (infoLog)  InfoLog("  Supported Extensions     : %s", str);
      else           printf("  Supported Extensions     : %s\n", str);
    }
    else
    {
      strcpy(strLocal, (char *) str);
      if (infoLog)  InfoLog("  Supported Extensions     : %s", (strLocal = strtok(strLocal, " \t\n")));
      else           printf("  Supported Extensions     : %s\n", (strLocal = strtok(strLocal, " \t\n")));
      char* strLocalTmp = strLocal;
      while ((strLocalTmp = strtok(NULL, " \t\n")) != NULL)
      {
        if (infoLog)  InfoLog("                             %s", strLocalTmp);
        else           printf("                             %s\n", strLocalTmp);
      }
    }
    free(strLocal);
  }
  if (infoLog)  InfoLog("");
  else      printf("\n");
}

#ifdef DEBUG
static void PrintBAT(unsigned regu, unsigned regl)
{
  uint32_t batu = ppc_read_spr(regu);
  uint32_t batl = ppc_read_spr(regl);
  uint32_t bepi = batu >> (31 - 14);
  uint32_t bl = (batu >> (31 - 29)) & 0x7ff;
  bool vs = batu & 2;
  bool vp = batu & 1;
  uint32_t brpn = batl >> (31 - 14);
  uint32_t wimg = (batl >> (31 - 28)) & 0xf;
  uint32_t pp = batl & 3;
  uint32_t size = (bl + 1) * 128 * 1024;
  uint32_t ea_base = bepi << (31 - 14);
  uint32_t ea_limit = ea_base + size - 1;
  uint32_t pa_base = brpn << (31 - 14);
  uint32_t pa_limit = pa_base + size - 1;
  printf("%08X-%08X -> %08X-%08X ", ea_base, ea_limit, pa_base, pa_limit);
  printf("%c%c%c%c ", (wimg&8)?'W':'-', (wimg&4)?'I':'-', (wimg&2)?'M':'-', (wimg&1)?'G':'-');
  printf("PP=");
  if (pp == 0)
    printf("NA");
  else if (pp == 2)
    printf("RW");
  else
    printf("RO");
  printf(" Vs=%d Vp=%d", vs, vp);
}
#endif

#ifdef DEBUG
static void DumpPPCRegisters(IBus *bus)
{
  for (int i = 0; i < 32; i += 4)
  {
    printf("R%d=%08X\tR%d=%08X\tR%d=%08X\tR%d=%08X\n",
           i + 0, ppc_get_gpr(i + 0),
           i + 1, ppc_get_gpr(i + 1),
           i + 2, ppc_get_gpr(i + 2),
           i + 3, ppc_get_gpr(i + 3));
  }
  printf("PC =%08X\n", ppc_get_pc());
  printf("LR =%08X\n", ppc_get_lr());
  printf("DBAT0U=%08X\tIBAT0U=%08X\n", ppc_read_spr(SPR603E_DBAT0U), ppc_read_spr(SPR603E_IBAT0U));
  printf("DBAT0L=%08X\tIBAT0L=%08X\n", ppc_read_spr(SPR603E_DBAT0L), ppc_read_spr(SPR603E_IBAT0L));
  printf("DBAT1U=%08X\tIBAT1U=%08X\n", ppc_read_spr(SPR603E_DBAT1U), ppc_read_spr(SPR603E_IBAT1U));
  printf("DBAT1L=%08X\tIBAT1L=%08X\n", ppc_read_spr(SPR603E_DBAT1L), ppc_read_spr(SPR603E_IBAT1L));
  printf("DBAT2U=%08X\tIBAT2U=%08X\n", ppc_read_spr(SPR603E_DBAT2U), ppc_read_spr(SPR603E_IBAT2U));
  printf("DBAT2L=%08X\tIBAT2L=%08X\n", ppc_read_spr(SPR603E_DBAT2L), ppc_read_spr(SPR603E_IBAT2L));
  printf("DBAT3U=%08X\tIBAT3U=%08X\n", ppc_read_spr(SPR603E_DBAT3U), ppc_read_spr(SPR603E_IBAT3U));
  printf("DBAT3L=%08X\tIBAT3L=%08X\n", ppc_read_spr(SPR603E_DBAT3L), ppc_read_spr(SPR603E_IBAT3L));
  for (int i = 0; i < 10; i++)
    printf("SR%d =%08X VSID=%06X\n", i, ppc_read_sr(i), ppc_read_sr(i) & 0x00ffffff);
  for (int i = 10; i < 16; i++)
    printf("SR%d=%08X VSID=%06X\n", i, ppc_read_sr(i), ppc_read_sr(i) & 0x00ffffff);
  printf("SDR1=%08X\n", ppc_read_spr(SPR603E_SDR1));
  printf("\n");
  printf("DBAT0: "); PrintBAT(SPR603E_DBAT0U, SPR603E_DBAT0L); printf("\n");
  printf("DBAT1: "); PrintBAT(SPR603E_DBAT1U, SPR603E_DBAT1L); printf("\n");
  printf("DBAT2: "); PrintBAT(SPR603E_DBAT2U, SPR603E_DBAT2L); printf("\n");
  printf("DBAT3: "); PrintBAT(SPR603E_DBAT3U, SPR603E_DBAT3L); printf("\n");
  printf("IBAT0: "); PrintBAT(SPR603E_IBAT0U, SPR603E_IBAT0L); printf("\n");
  printf("IBAT1: "); PrintBAT(SPR603E_IBAT1U, SPR603E_IBAT1L); printf("\n");
  printf("IBAT2: "); PrintBAT(SPR603E_IBAT2U, SPR603E_IBAT2L); printf("\n");
  printf("IBAT3: "); PrintBAT(SPR603E_IBAT3U, SPR603E_IBAT3L); printf("\n");
  printf("\n");
  /*
  printf("First PTEG:\n");
  uint32_t ptab = ppc_read_spr(SPR603E_SDR1) & 0xffff0000;
  for (int i = 0; i < 65536/8; i++)
  {
    uint64_t pte = bus->Read64(ptab + i*8);
    uint32_t vsid = (pte >> (32 + (31 - 24))) & 0x00ffffff;
    uint32_t rpn = pte & 0xfffff000;
    int wimg = (pte >> 3) & 0xf;
    bool v = pte & 0x8000000000000000ULL;
    printf("  %d: %016llX V=%d VSID=%06X RPN=%08X WIMG=%c%c%c%c\n", i, pte, v, vsid, rpn, (wimg&8)?'W':'-', (wimg&4)?'I':'-', (wimg&2)?'M':'-', (wimg&1)?'G':'-');
  }
  */
}
#endif

static void SaveFrameBuffer(const std::string& file)
{
    std::shared_ptr<uint8_t> pixels(new uint8_t[totalXRes * totalYRes * 4], std::default_delete<uint8_t[]>());
    glReadPixels(0, 0, totalXRes, totalYRes, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
    Util::WriteSurfaceToBMP<Util::RGBA8>(file, pixels.get(), totalXRes, totalYRes, true);
}

void Screenshot()
{
    // Make a screenshot
    time_t now = std::time(nullptr);
    tm* ltm = std::localtime(&now);
    std::string file = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Screenshots)
        << "Screenshot_"
        << std::setfill('0') << std::setw(4) << (1900 + ltm->tm_year)
        << '-'
        << std::setw(2) << (1 + ltm->tm_mon)
        << '-'
        << std::setw(2) << ltm->tm_mday
        << "_("
        << std::setw(2) << ltm->tm_hour
        << '-'
        << std::setw(2) << ltm->tm_min
        << '-'
        << std::setw(2) << ltm->tm_sec
        << ").png";

	// The name only has second resolution, so several screenshots taken within the same second would
	// otherwise collide. Uniqueness is resolved by the writer, at the moment it creates the file:
	// checking here would not help, because the file does not exist yet while the capture is still
	// queued.
    std::cout << "Screenshot created: " << file << std::endl;

    // In Vulkan the frame lives in the swapchain and there is no glReadPixels, so the image is
    // copied out by the backend on the next frame instead.
    if (s_vkContext != nullptr)
        s_vkContext->RequestScreenshot(file);
    else
        SaveFrameBuffer(file);
}
/******************************************************************************
 Render State Analysis
******************************************************************************/

#ifdef DEBUG

#include "Model3/Model3GraphicsState.h"
#include "OSD/SDL/PolyAnalysis.h"
#include <fstream>

static std::string s_gfxStatePath;
static const std::string k_gfxAnalysisPath = "GraphicsAnalysis/";

static std::string GetFileBaseName(const std::string &file)
{
  std::string base = file;
  size_t pos = file.find_last_of('/');
  if (pos != std::string::npos)
    base = file.substr(pos + 1);
  pos = file.find_last_of('\\');
  if (pos != std::string::npos)
    base = file.substr(pos + 1);
  return base;
}

static void TestPolygonHeaderBits(IEmulator *Emu)
{
  const static std::vector<uint32_t> unknownPolyBits
  {
    0xffffffff,
    0x000000ab, // actual color
    0x000000fc,
    0x000000c0,
    0x000000a0,
    0xffffff60,
    0xff0300ff  // contour, luminous, etc.
  };

  const std::vector<uint32_t> unknownCullingNodeBits
  {
    0xffffffff,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000
  };

  GLint readBuffer;
  glGetIntegerv(GL_READ_BUFFER, &readBuffer);
  glReadBuffer(GL_FRONT);

  // Render separate image for each unknown bit
  s_runtime_config.Set("Debug/ForceFlushModels", true);
  for (int idx = 0; idx < 7; idx++)
  {
    for (int bit = 0; bit < 32; bit++)
    {
      uint32_t mask = 1 << bit;
      s_runtime_config.Set("Debug/HighlightPolyHeaderIdx", idx);
      s_runtime_config.Set("Debug/HighlightPolyHeaderMask", mask);
      if ((unknownPolyBits[idx] & mask))
      {
        Emu->RenderFrame();
        std::string file = Util::Format() << k_gfxAnalysisPath << GetFileBaseName(s_gfxStatePath) << "." << "poly" << "." << idx << "_" << Util::Hex(mask) << ".bmp";
        SaveFrameBuffer(file);
      }
    }
  }

  for (int idx = 0; idx < 10; idx++)
  {
    for (int bit = 0; bit < 32; bit++)
    {
      uint32_t mask = 1 << bit;
      s_runtime_config.Set("Debug/HighlightCullingNodeIdx", idx);
      s_runtime_config.Set("Debug/HighlightCullingNodeMask", mask);
      if ((unknownCullingNodeBits[idx] & mask))
      {
        Emu->RenderFrame();
        std::string file = Util::Format() << k_gfxAnalysisPath << GetFileBaseName(s_gfxStatePath) << "." << "culling" << "." << idx << "_" << Util::Hex(mask) << ".bmp";
        SaveFrameBuffer(file);
      }
    }
  }

  glReadBuffer(readBuffer);

  // Generate the HTML GUI
  std::string file = Util::Format() << k_gfxAnalysisPath << "_" << GetFileBaseName(s_gfxStatePath) << ".html";
  std::ofstream fs(file);
  if (!fs.good())
    ErrorLog("Unable to open '%s' for writing.", file.c_str());
  else
  {
    std::string contents = s_polyAnalysisHTMLPrologue;
    contents += "    var g_file_base_name = '" + GetFileBaseName(s_gfxStatePath) + "';\n";
    contents += "    var g_unknown_poly_bits = [" + std::string(Util::Format(",").Join(unknownPolyBits)) + "];\n";
    contents += "    var g_unknown_culling_bits = [" + std::string(Util::Format(",").Join(unknownCullingNodeBits)) + "];\n";
    contents += s_polyAnalysisHTMLEpilogue;
    fs << contents;
    printf("Produced: %s\n", file.c_str());
  }
}

#endif


/******************************************************************************
 Save States and NVRAM

 Save states and NVRAM use the same basic format. When anything changes that
 breaks compatibility with previous versions of Supermodel, the save state
 and NVRAM version numbers must be incremented as needed.

 Header block name: "Supermodel Save State" or "Supermodel NVRAM State"
 Data: Save state file version (4-byte integer), ROM set ID (up to 9 bytes,
 including terminating \0).

 Different subsystems output their own blocks.
******************************************************************************/

static const int STATE_FILE_VERSION = 6;  // save state file version
static const int NVRAM_FILE_VERSION = 0;  // NVRAM file version
static unsigned s_saveSlot = 0;           // save state slot #

static void SaveState(IEmulator *Model3)
{
  CBlockFile  SaveState;

  std::string file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << Model3->GetGame().name << ".st" << s_saveSlot;
  if (Result::OKAY != SaveState.Create(file_path, "Supermodel Save State", "Supermodel Version " SUPERMODEL_VERSION))
  {
    ErrorLog("Unable to save state to '%s'.", file_path.c_str());
    return;
  }

  // Write file format version and ROM set ID to header block
  int32_t fileVersion = STATE_FILE_VERSION;
  SaveState.Write(&fileVersion, sizeof(fileVersion));
  SaveState.Write(Model3->GetGame().name);

  // Save state
  Model3->SaveState(&SaveState);
  SaveState.Close();
  printf("Saved state to '%s'.\n", file_path.c_str());
  InfoLog("Saved state to '%s'.", file_path.c_str());
}

static void LoadState(IEmulator *Model3, std::string file_path = std::string())
{
  CBlockFile  SaveState;

  // Generate file path
  if (file_path.empty())
    file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << Model3->GetGame().name << ".st" << s_saveSlot;

  // Open and check to make sure format is correct
  if (Result::OKAY != SaveState.Load(file_path))
  {
    ErrorLog("Unable to load state from '%s'.", file_path.c_str());
    return;
  }

  if (Result::OKAY != SaveState.FindBlock("Supermodel Save State"))
  {
    ErrorLog("'%s' does not appear to be a valid save state file.", file_path.c_str());
    return;
  }

  int32_t fileVersion;
  SaveState.Read(&fileVersion, sizeof(fileVersion));
  if (fileVersion != STATE_FILE_VERSION)
  {
    ErrorLog("'%s' is incompatible with this version of Supermodel.", file_path.c_str());
    return;
  }

  // Load
  Model3->LoadState(&SaveState);
  SaveState.Close();
  printf("Loaded state from '%s'.\n", file_path.c_str());
  InfoLog("Loaded state from '%s'.", file_path.c_str());
}

static void SaveNVRAM(IEmulator *Model3)
{
  CBlockFile  NVRAM;

  std::string file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::NVRAM) << Model3->GetGame().name << ".nv";
  if (Result::OKAY != NVRAM.Create(file_path, "Supermodel NVRAM State", "Supermodel Version " SUPERMODEL_VERSION))
  {
    ErrorLog("Unable to save NVRAM to '%s'. Make sure directory exists!", file_path.c_str());
    return;
  }

  // Write file format version and ROM set ID to header block
  int32_t fileVersion = NVRAM_FILE_VERSION;
  NVRAM.Write(&fileVersion, sizeof(fileVersion));
  NVRAM.Write(Model3->GetGame().name);

  // Save NVRAM
  Model3->SaveNVRAM(&NVRAM);
  NVRAM.Close();
  InfoLog("Saved NVRAM to '%s'.", file_path.c_str());
}

static void LoadNVRAM(IEmulator *Model3)
{
  CBlockFile  NVRAM;

  // Generate file path
  std::string file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::NVRAM) << Model3->GetGame().name << ".nv";

  // Open and check to make sure format is correct
  if (Result::OKAY != NVRAM.Load(file_path))
  {
    //ErrorLog("Unable to restore NVRAM from '%s'.", filePath);
    return;
  }

  if (Result::OKAY != NVRAM.FindBlock("Supermodel NVRAM State"))
  {
    ErrorLog("'%s' does not appear to be a valid NVRAM file.", file_path.c_str());
    return;
  }

  int32_t fileVersion;
  NVRAM.Read(&fileVersion, sizeof(fileVersion));
  if (fileVersion != NVRAM_FILE_VERSION)
  {
    ErrorLog("'%s' is incompatible with this version of Supermodel.", file_path.c_str());
    return;
  }

  // Load
  Model3->LoadNVRAM(&NVRAM);
  NVRAM.Close();
  InfoLog("Loaded NVRAM from '%s'.", file_path.c_str());
}


/*
static void PrintGLError(GLenum error)
{
  switch (error)
  {
  case GL_INVALID_ENUM:       printf("invalid enum\n"); break;
  case GL_INVALID_VALUE:      printf("invalid value\n"); break;
  case GL_INVALID_OPERATION:  printf("invalid operation\n"); break;
  case GL_STACK_OVERFLOW:     printf("stack overflow\n"); break;
  case GL_STACK_UNDERFLOW:    printf("stack underflow\n"); break;
  case GL_OUT_OF_MEMORY:      printf("out of memory\n"); break;
  case GL_TABLE_TOO_LARGE:    printf("table too large\n"); break;
  case GL_NO_ERROR:           break;
  default:                    printf("unknown error\n"); break;
  }
}
*/

/******************************************************************************
 Video Callbacks
******************************************************************************/

static CInputs *videoInputs = NULL;
static uint32_t currentInputs = 0;

bool BeginFrameVideo()
{
  if (s_vkContext)
    return s_vkContext->BeginFrame();
  return true;
}

void EndFrameVideo()
{
  // Show crosshairs for light gun games
  if (s_crosshair && videoInputs)
    s_crosshair->Update(currentInputs, videoInputs, xOffset, yOffset, xRes, yRes);

  if (s_vkContext)
  {
    s_vkContext->EndFrame();
  }
  else
  {
    /*
     * The OpenGL overlay is recorded here, and this is the only place it can be.
     *
     * The game's own drawing is finished (this is the last thing before the buffer swap), the window's
     * context is current, and the default framebuffer still holds the completed frame - which is
     * exactly the state the Vulkan path reaches inside its renderer instead. It has to happen after
     * every other pass, including the crosshairs above and the CRT filter's own pass, or the panel
     * would be drawn over and then partly overwritten.
     */
    OverlayRenderGL();
    SDL_GL_SwapWindow(s_window);
  }
}


/******************************************************************************
 Frame Timing
******************************************************************************/

static uint64_t s_perfCounterFrequency = 0;

static uint64_t GetDesiredRefreshRateMilliHz()
{
  // The refresh rate is expressed as mHz (millihertz -- Hz * 1000) in order to
  // be expressable as an integer. E.g.: 57.524 Hz -> 57524 mHz.
  float refreshRateHz = std::abs(s_runtime_config["RefreshRate"].ValueAs<float>());
  uint64_t refreshRateMilliHz = uint64_t(1000.0 * refreshRateHz);
  return refreshRateMilliHz;
}

static void SuperSleepUntil(const uint64_t target)
{
  uint64_t time = SDL_GetPerformanceCounter();

  // If we're ahead of the target, we're done
  if (time >= target)
  {
    return;
  }

  // Because OS sleep is not accurate,
  // we actually sleep until a maximum of 2 milliseconds are left.
  while (int64_t(target - time) * 1000 > 2 * int64_t(s_perfCounterFrequency))
  {
    SDL_Delay(1);
    time = SDL_GetPerformanceCounter();
  }

  // Spin until requested time
  int64_t remain;
  do
  {
    // according to all available processor documentation for x86 and arm,
    // spinning should pause the processor for a short while for better
    // power efficiency and (surprisingly) overall faster system performance
    #ifdef SDL_CPUPauseInstruction
    SDL_CPUPauseInstruction();
    #endif
    remain = target - SDL_GetPerformanceCounter();
  } while (remain > 0);
}


/******************************************************************************
 Main Program Loop
******************************************************************************/

/*
 * Writes the values the in-game overlay edits back to the settings file.
 *
 * Deliberately a read-modify-write of the file rather than a dump of the runtime configuration. The
 * runtime configuration is one flattened global section, so writing it out would replace the whole
 * file with it and take every game-specific section with it - someone who had set up per-game
 * controls would lose them by pressing Apply on a CRT slider.
 *
 * Failure is reported and otherwise ignored: the values are already in effect for this session, and
 * losing them at exit is a far smaller problem than refusing to close the panel.
 */
static void SaveOverlaySettings()
{
  Util::Config::Node fileConfig("Global");
  if (Util::Config::FromINIFile(&fileConfig, s_configFilePath))
  {
    ErrorLog("Overlay: could not read '%s' to save the panel settings.", s_configFilePath.c_str());
    return;
  }

  // Only the keys the panel offers are taken from the runtime configuration, so an Apply cannot
  // quietly persist an unrelated change that something else made at run time.
  static const char* const keys[] = {
    "PostFXAA", "PostCRT", "PostScanlines", "PostScanlineIntensity",
    "PostMaskType", "PostMaskScale", "PostMaskIntensity",
    "PostGlow", "PostGlowLevels", "PostGlowThreshold", "PostGain",
    // Written when the readout is toggled in play, so the setting survives the session.
    "ShowStatsOsd"
  };
  for (const char* key : keys)
  {
    if (s_runtime_config[key].Exists())
      fileConfig.Set(key, s_runtime_config[key].GetValue()->MakeCopy());
  }

  // WriteINIFile reports its own failure through the log, so there is no result to test here.
  Util::Config::WriteINIFile(s_configFilePath, fileConfig,
    TimestampedConfigFileComment("Updated from the in-game settings panel."));
  InfoLog("Overlay: settings saved to '%s'.", s_configFilePath.c_str());
}

#ifdef SUPERMODEL_DEBUGGER
int Supermodel(const Game &game, ROMSet *rom_set, IEmulator *Model3, CInputs *Inputs, COutputs *Outputs, std::shared_ptr<Debugger::CDebugger> Debugger)
{
  std::shared_ptr<CLogger> oldLogger;
#else
int Supermodel(const Game &game, ROMSet *rom_set, IEmulator *Model3, CInputs *Inputs, COutputs *Outputs)
{
#endif // SUPERMODEL_DEBUGGER
  std::string initialState = s_runtime_config["InitStateFile"].ValueAs<std::string>();
  uint64_t    prevFPSTicks;
  unsigned    fpsFramesElapsed;
  bool        gameHasLightguns = false;
  bool        quit = false;
  bool        paused = false;
  bool        dumpTimings = false;
  // Previous frame's overlay state, so the mouse visibility and input suppression are only touched
  // when it changes. See the note in the loop.
  bool        s_overlayWasVisible = false;

  // Whether the borderless cadence warning has been given for this session. One-shot, because the
  // condition holds for the whole session and repeating it every frame would be noise rather than
  // information.
  bool        s_borderlessJudderNoticed = false;

  // Initialize and load ROMs
  if (Result::OKAY != Model3->Init())
    return 1;
  if (Model3->LoadGame(game, *rom_set) != Result::OKAY)
    return 1;
  *rom_set = ROMSet();  // free up this memory we won't need anymore

  // Customized music for games with MPEG boards
  MpegDec::LoadCustomTracks(s_musicXMLFilePath, game);

  // Load NVRAM
  LoadNVRAM(Model3);

  // Set the video mode. Sized for the versioned title plus a long game title plus the frame-rate
  // suffix that gets appended to it - snprintf truncates rather than overflowing, but a truncated
  // title bar is a needless way to find out the buffer was tight.
  char baseTitleStr[256];
  char titleStr[256];
  totalXRes = xRes = s_runtime_config["XResolution"].ValueAs<unsigned>();
  totalYRes = yRes = s_runtime_config["YResolution"].ValueAs<unsigned>();
  snprintf(baseTitleStr, sizeof(baseTitleStr), SUPERMODEL_TITLE_FULL " - %s", game.title.c_str());
  SDL_SetWindowTitle(s_window, baseTitleStr);
  SDL_SetWindowSize(s_window, totalXRes, totalYRes);

  // Centred on the configured output rather than the primary one, so that windowed mode also opens
  // where the display setting says - otherwise a user who picked a second monitor gets the exclusive
  // modes on it and the window everywhere else. An explicit position still wins.
  const int configuredDisplay = GetConfiguredDisplaySdlIndex();
  int xpos = s_runtime_config["WindowXPosition"].ValueAsDefault<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(configuredDisplay));
  int ypos = s_runtime_config["WindowYPosition"].ValueAsDefault<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(configuredDisplay));
  SDL_SetWindowPosition(s_window, xpos, ypos);

  if (s_runtime_config["BorderlessWindow"].ValueAs<bool>())
  {
    SDL_SetWindowBordered(s_window, SDL_FALSE);
  }

  // Pick the fullscreen flavour (borderless vs exclusive/G-SYNC vs fixed arcade timing) before the
  // resize below applies it.
  s_fullScreenMode = ResolveFullScreenMode();

  bool stretch = s_runtime_config["Stretch"].ValueAs<bool>();
  bool fullscreen = s_runtime_config["FullScreen"].ValueAs<bool>();
  Result resized = s_vkContext
    ? ResizeVulkanScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretch, fullscreen)
    : ResizeGLScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretch, fullscreen);
  if (Result::OKAY != resized)
    return 1;

  // Info log GL information
  if (s_vkContext)
    InfoLog("Vulkan 3D engine enabled.");
  else
    PrintGLInfo(false, true, false);

  // Initialize audio system
  SetAudioType(game.audio);
  if (Result::OKAY != OpenAudio(s_runtime_config))
    return 1;

  // Hide mouse if fullscreen, enable crosshairs for gun games
  Inputs->GetInputSystem()->SetMouseVisibility(!s_runtime_config["FullScreen"].ValueAs<bool>());
  gameHasLightguns = !!(game.inputs & (Game::INPUT_GUN1|Game::INPUT_GUN2));
  gameHasLightguns |= game.name == "lostwsga";
  currentInputs = game.inputs;
  if (gameHasLightguns)
    videoInputs = Inputs;
  else
    videoInputs = nullptr;

  // Attach the inputs to the emulator
  Model3->AttachInputs(Inputs);

  // Attach the outputs to the emulator
  if (Outputs != nullptr)
    Model3->AttachOutputs(Outputs);

  // Frame timing
  s_perfCounterFrequency = SDL_GetPerformanceFrequency();
  uint64_t perfCountPerFrame = s_perfCounterFrequency * 1000 / GetDesiredRefreshRateMilliHz();
  // Absolute target for the next frame. This is advanced by exactly one frame period
  // each time (see the main loop) instead of being rebased on the current time.
  uint64_t nextTime = SDL_GetPerformanceCounter() + perfCountPerFrame;

  // Initialize the renderers
  SuperAA* superAA = nullptr;
  IRender2D *Render2D = nullptr;
  IRender3D *Render3D = nullptr;
  UpscaleMode upscaleMode = (UpscaleMode)s_runtime_config["UpscaleMode"].ValueAs<int>();
  unsigned aaTarget = 0;
  if (s_vkContext)
  {
    if (aaValue != 1)
    {
      InfoLog("Vulkan: supersampling is not available in this backend; using 1x.");
      aaValue = 1;
    }
    Vulkan3D::CVulkan2D* vk2d = new Vulkan3D::CVulkan2D(s_runtime_config, *s_vkContext);
    Render2D = vk2d;
    s_vk3dRenderer = new Vulkan3D::CVulkan3D(s_runtime_config, Model3->GetGame().name, *s_vkContext, *vk2d);
    Render3D = s_vk3dRenderer;
  }
  else
  {
    superAA = new SuperAA(aaValue, CRTcolors);
    superAA->Init(totalXRes, totalYRes);  // pass actual frame sizes here
    Render2D = new CRender2D(s_runtime_config);
    aaTarget = superAA->GetTargetID();
#ifndef SUPERMODEL_OSX
    Render3D = s_runtime_config["New3DEngine"].ValueAs<bool>() ? ((IRender3D *) new New3D::CNew3D(s_runtime_config, Model3->GetGame().name)) : ((IRender3D *) new Legacy3D::CLegacy3D(s_runtime_config));
#else
    // Legacy renderer is not supported on Mac, always use new engine
    Render3D = (IRender3D *) new New3D::CNew3D(s_runtime_config, Model3->GetGame().name);
#endif
  }

  if (Result::OKAY != Render2D->Init(xOffset*aaValue, yOffset*aaValue, xRes*aaValue, yRes*aaValue, totalXRes*aaValue, totalYRes*aaValue, aaTarget, upscaleMode))
    goto QuitError;
  if (Result::OKAY != Render3D->Init(xOffset*aaValue, yOffset*aaValue, xRes*aaValue, yRes*aaValue, totalXRes*aaValue, totalYRes*aaValue, aaTarget))
    goto QuitError;

  Model3->AttachRenderers(Render2D,Render3D, superAA);

  // Tells the statistics readout what this session is really rendering at, and with what
  // antialiasing. Done here because this is the only point where every input is known: the effective
  // supersampling factor (Vulkan has just forced it to 1), the window size, and - for Vulkan - the
  // sample count the driver actually granted.
  ReportRenderInfo(aaValue, totalXRes, totalYRes);

  /*
   * In-game settings overlay.
   *
   * Both renderer families can host it now: Vulkan draws it into the swapchain image it already owns,
   * and the OpenGL renderers draw it into the default framebuffer just before the buffer swap. The
   * panel and the statistics readout therefore work whichever renderer is selected, which matters most
   * on a machine whose driver is too old for Vulkan - that used to lose both, silently, with the
   * settings showing them as enabled.
   *
   * Failure is logged and otherwise ignored - the overlay is a convenience and must not stop a game
   * from running.
   *
   * The scale is taken from the display DPI so the panel is the same size as the settings window's
   * controls rather than tiny on a high-DPI screen. Clamped like the settings window clamps it.
   */
  {
    float overlayScale = 1.0f;
#ifdef _WIN32
    if (HDC screen = GetDC(nullptr))
    {
      overlayScale = (float)GetDeviceCaps(screen, LOGPIXELSY) / 96.0f;
      ReleaseDC(nullptr, screen);
    }
#endif
    if (overlayScale < 1.0f) overlayScale = 1.0f;
    if (overlayScale > 2.5f) overlayScale = 2.5f;

    if (s_vkContext)
    {
      if (OverlayInit(*s_vkContext, s_window, overlayScale))
        static_cast<Vulkan3D::CVulkan2D*>(Render2D)->SetOverlayPass(&OverlayRender);
    }
    else
    {
      // Requires the game window's context to be current, which CreateGLScreen left it as.
      OverlayInitGL(s_window, overlayScale);
    }

    /*
     * The readout's initial state comes from the settings file, so it survives a restart; the key
     * still turns it on and off while a game runs.
     *
     * Set after the overlay is initialised because a readout with no ImGui backend behind it would be
     * enabled and invisible - see the note on StatsOsdEnabled in Overlay.cpp.
     */
    StatsOsdSetWindow(s_window);

    /*
     * The renderer is named from what is actually running, not from the configuration.
     *
     * The two come apart in a way worth guarding against: selecting Vulkan on a driver that cannot
     * provide it falls back to OpenGL, and a readout that repeated the configuration would name a
     * backend that is not the one drawing the picture. Which renderer this session got is known here
     * and nowhere else, so it is decided here.
     */
    StatsOsdSetRenderer(s_vkContext ? StatsRenderer::Vulkan3D
      : (s_runtime_config["New3DEngine"].ValueAs<bool>() ? StatsRenderer::New3D : StatsRenderer::Legacy3D));

    StatsOsdApplyConfig(s_runtime_config["ShowStatsOsd"].ValueAsDefault<bool>(false));
  }

  // Reset emulator
  Model3->Reset();

  // Load initial save state if requested
  if (!initialState.empty())
    LoadState(Model3, initialState);

#ifdef SUPERMODEL_DEBUGGER
  // If debugger was supplied, set it as logger and attach it to system
  oldLogger = GetLogger();
  if (Debugger != NULL)
  {
    SetLogger(Debugger);
    Debugger->Attach();
  }
#endif // SUPERMODEL_DEBUGGER

  // Emulate!
  fpsFramesElapsed = 0;
  prevFPSTicks = SDL_GetPerformanceCounter();
  quit = false;
  paused = false;
  dumpTimings = false;
#ifdef DEBUG
  if (dynamic_cast<CModel3GraphicsState *>(Model3))
  {
    TestPolygonHeaderBits(Model3);
    quit = true;
  }
#endif

  // Re-assert the priority here. Setting it once at start-up is not enough: the class is reset
  // somewhere during initialisation, so it has to be applied again before the frame loop begins.
  if (s_runtime_config["HighPriority"].ValueAsDefault<bool>(true))
    RaiseProcessPriority();

  // Keep the game running while the window is dragged; see ModalLoopMessageHook.
  SetModalLoopHook(Model3);

  while (!quit)
  {
    // Marks the boundary of one emulated frame, so the statistics readout measures the emulator's real
    // frame rate rather than the rate it is presenting at. Before the input poll, so a frame that ends
    // the loop is still counted.
    StatsOsdFrameTick();

    // Re-sent every frame rather than once at startup: a fullscreen toggle or a display change can make
    // the driver refuse the exclusive switch mid-session, and the readout has to describe where the
    // session ended up rather than where it was intended to go.
    StatsOsdSetSyncMode(StatsSyncModeForCurrentSession());

    // The pacing rate, for the same reason and so that the borderless cadence check works even with the
    // readout switched off - which is the state most people are in while wondering why the picture
    // stutters.
    StatsOsdSetTargetRate(s_runtime_config["RefreshRate"].ValueAsDefault<float>(0.0f));

    /*
     * Reported in borderless fullscreen when the panel's rate does not divide evenly into the emulated
     * rate - which is a problem *if the panel is running at its own rate*, and the wording says so.
     *
     * It may not be. A driver configured to apply VRR to windowed output (NVIDIA's "windowed and full
     * screen mode") lets the display follow the composited window instead, in which case there is
     * nothing to fix and this notice would be crying wolf. There is no way to find out from in here:
     * with the panel on its own cadence and with the panel following us, the program presents at the
     * same rate and reads the same frame rate, so the two are indistinguishable from the application
     * side. Rather than assert the bad case, the notice gives the reader the one test that settles it -
     * their own display's refresh readout - because that is where the answer actually lives.
     *
     * Said once per session rather than every frame, and in the settings window rather than only in
     * the readout, because the readout is off by default and the explanation is worth more than the
     * measurement here.
     */
    if (StatsOsdBorderlessJudderPending() && !s_borderlessJudderNoticed)
    {
      s_borderlessJudderNoticed = true;
      InfoLog("Frame cadence: borderless fullscreen, and the panel rate (%d Hz) does not divide evenly "
        "into the emulated rate, so frames are shown unevenly if the panel is running at its own rate. "
        "An exclusive sync mode on the Common tab removes this. Note that a driver applying VRR to "
        "windowed output would remove it as well; this cannot be detected from here, so check the "
        "display's own refresh readout.",
        StatsOsdPanelRefreshHz());      GuiPostNotice("当前是窗口化全屏：若显示器按自身刷新率输出，它与 57.5Hz 不能整除，"
        "会出现轻微的帧节奏不均 —— 在「常用」页改用独占全屏可以消除。\n"
        "但如果驱动已为「窗口和全屏模式」启用 G-SYNC/FreeSync，显示器会跟随模拟器，"
        "这种情况本程序无法检测：请看显示器自己的实时刷新率（约 57-58Hz 即已生效，"
        "此时本条提示可忽略）。");
    }

    // Poll the inputs
    if (!Inputs->Poll(&game, xOffset, yOffset, xRes, yRes))
      quit = true;

    /*
     * Build the in-game panel before the frame is rendered, so a value changed here reaches the
     * picture on this very frame rather than the next one. The panel's own drawing is recorded later,
     * inside the renderer, because that is the only place the swapchain is set up for it.
     *
     * The statistics readout shares that frame and that draw pass; see OverlayNewFrame.
     */
    if (OverlayActive() && OverlayNewFrame(s_runtime_config))
      SaveOverlaySettings();

    // The readout's on/off state is written back the moment it is toggled rather than on exit, so a
    // session that ends abnormally does not lose the setting. One-shot: the flag is consumed here.
    if (StatsOsdConsumeStateChanged())
    {
      s_runtime_config.Set("ShowStatsOsd", StatsOsdEnabled());
      SaveOverlaySettings();
    }

    // Render if paused, otherwise run a frame
    if (paused)
      Model3->RenderFrame();
    else
      Model3->RunFrame();

#ifdef SUPERMODEL_DEBUGGER
    bool processUI = true;
    if (Debugger != NULL)
    {
      Debugger->Poll();

      // Check if debugger requests exit or pause
      if (Debugger->CheckExit())
      {
        quit = true;
        processUI = false;
      }
      else if (Debugger->CheckPause())
      {
        paused = true;
        processUI = false;
      }
    }
    if (processUI)
    {
#endif // SUPERMODEL_DEBUGGER

    // Check UI controls
    if (Inputs->uiToggleOverlay->Pressed())
    {
      // Only meaningful where the overlay has a backend, which is now either renderer family - so the
      // test is whether one came up, not which one it is.
      if (OverlayActive())
        OverlayToggle(s_runtime_config);
    }
    else if (Inputs->uiToggleStatsOsd->Pressed())
    {
      // Same ImGui context and same draw pass as the panel, so the same condition applies.
      if (OverlayActive())
        StatsOsdToggle();
      else
        InfoLog("Statistics readout: no overlay backend is available in this session.");
    }

    /*
     * Keep the side effects of the panel being open in step with it, every frame rather than only
     * when the toggle key is pressed.
     *
     * The panel can also be closed from its own Close button, and acting only on the key would leave
     * the game's inputs suppressed after that - the car would stop responding with nothing on screen
     * to explain it. Comparing against the previous frame makes both ways of closing behave the same.
     *
     * SetMouseVisibility is idempotent (see CDirectInputSystem), so a redundant call is harmless;
     * the game hides the pointer in fullscreen and the panel cannot be used without it.
     */
    if (OverlayActive())
    {
      const bool overlayNow = OverlayVisible();
      if (overlayNow != s_overlayWasVisible)
      {
        s_overlayWasVisible = overlayNow;
        Inputs->GetInputSystem()->SetMouseVisibility(overlayNow || !s_runtime_config["FullScreen"].ValueAs<bool>());
        Inputs->SetGameInputSuppressed(overlayNow);
      }
    }

    if (Inputs->uiExit->Pressed())
    {
      // Quit emulator
      quit = true;
    }
    else if (Inputs->uiReset->Pressed())
    {
      if (!paused)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
      }

      // Reset emulator
      Model3->Reset();

#ifdef SUPERMODEL_DEBUGGER
      // If debugger was supplied, reset it too
      if (Debugger != NULL)
        Debugger->Reset();
#endif // SUPERMODEL_DEBUGGER

      if (!paused)
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
      }

      puts("Model 3 reset.");
    }
    else if (Inputs->uiPause->Pressed())
    {
      // Toggle emulator paused flag
      paused = !paused;

      if (paused)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
        snprintf(titleStr, sizeof(titleStr), "%s (Paused)", baseTitleStr);
        SDL_SetWindowTitle(s_window, titleStr);
      }
      else
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
        SDL_SetWindowTitle(s_window, baseTitleStr);
      }

      // Send paused value as output
      if (Outputs != NULL)
        Outputs->SetValue(OutputPause, paused);
    }
    else if (Inputs->uiFullScreen->Pressed())
    {
      // Toggle emulator fullscreen
      s_runtime_config.Get("FullScreen").SetValue(!s_runtime_config["FullScreen"].ValueAs<bool>());

      // OpenGL loses its context across a fullscreen toggle. The Vulkan
      // renderers own the swapchain through CVulkanContext and stay alive.
      if (!s_vkContext)
      {
        delete Render2D;
        Render2D = nullptr;
      }

      // Resize screen
      totalXRes = xRes = s_runtime_config["XResolution"].ValueAs<unsigned>();
      totalYRes = yRes = s_runtime_config["YResolution"].ValueAs<unsigned>();
      bool stretchc = s_runtime_config["Stretch"].ValueAs<bool>();
      bool fullscreenc = s_runtime_config["FullScreen"].ValueAs<bool>();
      Result resized = s_vkContext
        ? ResizeVulkanScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretchc, fullscreenc)
        : ResizeGLScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretchc, fullscreenc);
      if (Result::OKAY != resized)
        goto QuitError;

      // Recreate renderers and attach to the emulator
      unsigned aaTarget = 0;
      if (superAA)
      {
        superAA->Init(totalXRes, totalYRes);
        aaTarget = superAA->GetTargetID();
      }
      if (!s_vkContext)
        Render2D = new CRender2D(s_runtime_config);

      if (Result::OKAY != Render2D->Init(xOffset * aaValue, yOffset * aaValue, xRes * aaValue, yRes * aaValue, totalXRes * aaValue, totalYRes * aaValue, aaTarget, upscaleMode))
        goto QuitError;
      if (Result::OKAY != Render3D->Init(xOffset * aaValue, yOffset * aaValue, xRes * aaValue, yRes * aaValue, totalXRes * aaValue, totalYRes * aaValue, aaTarget))
        goto QuitError;

      Model3->AttachRenderers(Render2D, Render3D, superAA);

      // The renderers were just re-initialised, possibly at a different size, so the readout's
      // Render line has to be told again - this is the resolution-change path.
      ReportRenderInfo(aaValue, totalXRes, totalYRes);

      Inputs->GetInputSystem()->SetMouseVisibility(!s_runtime_config["FullScreen"].ValueAs<bool>());
    }
    else if (Inputs->uiSaveState->Pressed())
    {
      if (!paused)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
      }

      // Save game state
      SaveState(Model3);

      if (!paused)
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
      }
    }
    else if (Inputs->uiChangeSlot->Pressed())
    {
      // Change save slot
      ++s_saveSlot;
      s_saveSlot %= 10; // clamp to [0,9]
      printf("Save slot: %d\n", s_saveSlot);
    }
    else if (Inputs->uiLoadState->Pressed())
    {
      if (!paused)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
      }

      // Load game state
      LoadState(Model3);

#ifdef SUPERMODEL_DEBUGGER
      // If debugger was supplied, reset it after loading state
      if (Debugger != NULL)
        Debugger->Reset();
#endif // SUPERMODEL_DEBUGGER

      if (!paused)
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
      }
    }
    else if (Inputs->uiMusicVolUp->Pressed())
    {
      // Increase music volume by 10%
      if (!Model3->GetGame().mpeg_board.empty())
      {
        int vol = (std::min)(200, s_runtime_config["MusicVolume"].ValueAs<int>() + 10);
        s_runtime_config.Get("MusicVolume").SetValue(vol);
        printf("Music volume: %d%%", vol);
        if (200 == vol)
          puts(" (maximum)");
        else
          printf("\n");
      }
      else
        puts("This game does not have an MPEG music board.");
    }
    else if (Inputs->uiMusicVolDown->Pressed())
    {
      // Decrease music volume by 10%
      if (!Model3->GetGame().mpeg_board.empty())
      {
        int vol = (std::max)(0, s_runtime_config["MusicVolume"].ValueAs<int>() - 10);
        s_runtime_config.Get("MusicVolume").SetValue(vol);
        printf("Music volume: %d%%", vol);
        if (0 == vol)
          puts(" (muted)");
        else
          printf("\n");
      }
      else
        puts("This game does not have an MPEG music board.");
    }
    else if (Inputs->uiSoundVolUp->Pressed())
    {
      // Increase sound volume by 10%
    int vol = (std::min)(200, s_runtime_config["SoundVolume"].ValueAs<int>() + 10);
      s_runtime_config.Get("SoundVolume").SetValue(vol);
      printf("Sound volume: %d%%", vol);
      if (200 == vol)
        puts(" (maximum)");
      else
        printf("\n");
    }
    else if (Inputs->uiSoundVolDown->Pressed())
    {
      // Decrease sound volume by 10%
      int vol = (std::max)(0, s_runtime_config["SoundVolume"].ValueAs<int>() - 10);
      s_runtime_config.Get("SoundVolume").SetValue(vol);
      printf("Sound volume: %d%%", vol);
      if (0 == vol)
        puts(" (muted)");
      else
        printf("\n");
    }
#ifdef SUPERMODEL_DEBUGGER
    else if (Inputs->uiDumpInpState->Pressed())
    {
      // Dump input states
      Inputs->DumpState(&game);
    }
    else if (Inputs->uiDumpTimings->Pressed())
    {
      dumpTimings = !dumpTimings;
    }
#endif
    else if (Inputs->uiSelectCrosshairs->Pressed() && gameHasLightguns)
    {
      int crosshairs = (s_runtime_config["Crosshairs"].ValueAs<unsigned>() + 1) & 3;
      s_runtime_config.Get("Crosshairs").SetValue(crosshairs);
      switch (crosshairs)
      {
      case 0: puts("Crosshairs disabled.");             break;
      case 3: puts("Crosshairs enabled.");              break;
      case 1: puts("Showing Player 1 crosshair only."); break;
      case 2: puts("Showing Player 2 crosshair only."); break;
      }
    }
    else if (Inputs->uiClearNVRAM->Pressed())
    {
      // Clear NVRAM
      Model3->ClearNVRAM();
      puts("NVRAM cleared.");
    }
    else if (Inputs->uiToggleFrLimit->Pressed())
    {
      // Toggle frame limiting
      s_runtime_config.Get("Throttle").SetValue(!s_runtime_config["Throttle"].ValueAs<bool>());
      printf("Frame limiting: %s\n", s_runtime_config["Throttle"].ValueAs<bool>() ? "On" : "Off");
    }
    else if (Inputs->uiScreenshot->Pressed())
    {
      // Make a screenshot
      Screenshot();
    }
#ifdef SUPERMODEL_DEBUGGER
      else if (Debugger != NULL && Inputs->uiEnterDebugger->Pressed())
      {
        // Break execution and enter debugger
        Debugger->ForceBreak(true);
      }
    }
#endif // SUPERMODEL_DEBUGGER

    // Refresh rate (frame limiting)
    if (paused || s_runtime_config["Throttle"].ValueAs<bool>())
    {
        SuperSleepUntil(nextTime);

        // Advance the target by exactly one frame period. The previous code rebased the
        // target on SDL_GetPerformanceCounter() every frame, which folded the sleep
        // overshoot into the next deadline. That made each emulated frame slightly longer
        // than 1/60s, so audio was produced a little slower than the 44.1kHz device consumed
        // it and the sound buffer slowly drained until it under-ran (audible stutter).
        nextTime += perfCountPerFrame;

        // If a frame took longer than its budget we are already behind; resync to now
        // rather than sprinting to catch up.
        uint64_t nowTicks = SDL_GetPerformanceCounter();
        if (nextTime < nowTicks)
            nextTime = nowTicks;
    }

    // Measure frame rate
    uint64_t currentFPSTicks = SDL_GetPerformanceCounter();
    if (s_runtime_config["ShowFrameRate"].ValueAs<bool>())
    {
      fpsFramesElapsed += 1;
      uint64_t measurementTicks = currentFPSTicks - prevFPSTicks;
      if (measurementTicks >= s_perfCounterFrequency) // update FPS every 1 second (s_perfCounterFrequency is how many perf ticks in one second)
      {
        double fps = double(fpsFramesElapsed) / (double(measurementTicks) / double(s_perfCounterFrequency));
        unsigned audioUnderruns = 0, audioOverruns = 0;
        GetAudioBufferStats(&audioUnderruns, &audioOverruns);
        snprintf(titleStr, sizeof(titleStr), "%s - %1.3f FPS%s [audio u%u/o%u]",
          baseTitleStr, fps, paused ? " (Paused)" : "", audioUnderruns, audioOverruns);
        SDL_SetWindowTitle(s_window, titleStr);
        prevFPSTicks = currentFPSTicks;   // reset tick count
        fpsFramesElapsed = 0;             // reset frame count
      }
    }

    if (dumpTimings && !paused)
    {
      CModel3 *M = dynamic_cast<CModel3 *>(Model3);
      if (M)
        M->DumpTimings();
    }
  }

  // Make sure all threads are paused before shutting down
  Model3->PauseThreads();

#ifdef SUPERMODEL_DEBUGGER
  // If debugger was supplied, detach it from system and restore old logger
  if (Debugger != NULL)
  {
    Debugger->Detach();
    SetLogger(oldLogger);
  }
#endif // SUPERMODEL_DEBUGGER

  // Save NVRAM
  SaveNVRAM(Model3);

  // Close audio
  CloseAudio();

  // Uninstall before the emulator goes away: the hook holds a pointer to it, and a stray WM_TIMER
  // arriving afterwards would call into freed memory.
  SetModalLoopHook(nullptr);

  // Before the renderers: the overlay's ImGui backend waits for the device to go idle, and it has to
  // stop being referenced by the renderer before the renderer is destroyed.
  //
  // The statistics collection goes with it: it holds an NVML library and device handle that should not
  // outlive the session, or the next one would find them already loaded and skip setting them up.
  StatsOsdShutdown();
  OverlayShutdown();
  InfoLog("Session: overlay and statistics shut down; destroying the renderers.");

  // Shut down renderers
  delete Render2D;
  InfoLog("Session: 2D renderer destroyed.");
  delete Render3D;
  InfoLog("Session: 3D renderer destroyed.");
  delete superAA;
  InfoLog("Session: released. Returning to the settings window.");

  return 0;

  // Quit with an error
QuitError:
  SetModalLoopHook(nullptr);

  /*
   * Release the audio device before parking the process.
   *
   * This is reached after OpenAudio() has succeeded, so the device really is open - and the settings
   * window stays up afterwards, which means the failure is recoverable: the user picks another renderer
   * or another game and a new session starts in this same process. Leaving the device open made that
   * impossible. Every retry died with "Audio device is already opened", which looks like a second,
   * unrelated fault and hides the first one behind it - and it is why a renderer that could not
   * initialise appeared to break all three renderers.
   */
  CloseAudio();

  StatsOsdShutdown();
  OverlayShutdown();
  delete Render2D;
  delete Render3D;
  delete superAA;

  return 1;
}


/******************************************************************************
 Entry Point and Command Line Processing
******************************************************************************/

/*
 * Moves a settings file from the pre-Data layout into Data\Config\, once.
 *
 * Every path used to be relative to the working directory, so an existing installation has its
 * settings, NVRAM and save states sitting next to the executable rather than under Data\. Without
 * this the move to Data\ looks exactly like a factory reset: the new location has no ini, one is
 * written from the defaults, and the user's tuned CRT settings, ROM path and language are all
 * silently gone while their files are still on disk.
 *
 * Copy rather than move: the source is left alone so that downgrading to an older build still
 * works, and so that a failed copy cannot destroy the only record of someone's settings.
 */
static void MigrateLegacyDataLayout()
{
#ifdef SUPERMODEL_WIN32
    // Nothing to do if the current layout already has a settings file.
    FILE* existing = fopen(s_configFilePath.c_str(), "r");
    if (existing != nullptr)
    {
        fclose(existing);
        return;
    }

    // The old code resolved paths against the working directory, and the executable directory is
    // where most people launched from, so both are worth trying.
    const std::string base = FileSystemPath::GetBasePath();
    const char* const legacyDirs[] = { "Config/", "NVRAM/", "Saves/" };

    for (const char* dir : legacyDirs)
    {
        const std::string from = base + dir;
        const std::string to = (dir == std::string("Config/"))
            ? FileSystemPath::GetPath(FileSystemPath::Config)
            : (dir == std::string("NVRAM/") ? FileSystemPath::GetPath(FileSystemPath::NVRAM)
                                            : FileSystemPath::GetPath(FileSystemPath::Saves));

        if (from == to || !FileSystemPath::PathExists(from))
            continue;

        // Create the destination and copy whatever is not already there.
        FileSystemPath::MakeDir(to);
        WIN32_FIND_DATAA findData;
        const std::string pattern = from + "*";
        HANDLE find = FindFirstFileA(pattern.c_str(), &findData);
        if (find == INVALID_HANDLE_VALUE)
            continue;

        int copied = 0;
        do
        {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                continue;
            const std::string target = to + findData.cFileName;
            if (GetFileAttributesA(target.c_str()) != INVALID_FILE_ATTRIBUTES)
                continue;       // never overwrite something the new layout already has
            if (CopyFileA((from + findData.cFileName).c_str(), target.c_str(), TRUE))
                ++copied;
        } while (FindNextFileA(find, &findData) != 0);
        FindClose(find);

        if (copied > 0)
        {
            // printf rather than InfoLog: this runs before the logger is created (see main), so a
            // log message would go to the console-error logger and be dropped - the same reason the
            // old "Wrote default configuration file" message never reached Supermodel.log.
            printf("Moved %d file(s) from '%s' into '%s'.\n", copied, from.c_str(), to.c_str());
        }
    }
#endif
}

// Configuration file is generated whenever it is not present. We no longer
// distribute it with Supermodel to make it easier to upgrade Supermodel from
// .zip archives without accidentally overwriting the configuration.
static void WriteDefaultConfigurationFileIfNotPresent()
{
    MigrateLegacyDataLayout();

    // Test whether file exists by opening it
    FILE* fp = fopen(s_configFilePath.c_str(), "r");
    if (fp)
    {
        fclose(fp);
        return;
    }

    // Write config
    fp = fopen(s_configFilePath.c_str(), "w");
    if (!fp)
    {
        ErrorLog("Unable to write default configuration file to %s", s_configFilePath.c_str());
        return;
    }
    fputs(DefaultConfigFileContents().c_str(), fp);
    fclose(fp);
    InfoLog("Wrote default configuration file to %s", s_configFilePath.c_str());
}

// Create and configure inputs
static Result ConfigureInputs(CInputs *Inputs, Util::Config::Node *fileConfig, Util::Config::Node *runtimeConfig, const Game &game, bool configure)
{
  Inputs->LoadFromConfig(*runtimeConfig);

  // If the user wants to configure the inputs, do that now
  if (configure)
  {
    std::string title(SUPERMODEL_TITLE_FULL " - ");
    if (game.name.empty())
      title.append("Configuring Default Inputs...");
    else
      title.append(Util::Format() << "Configuring Inputs for: " << game.title);
    SDL_SetWindowTitle(s_window, title.c_str());

    // Extract the relevant INI section (which will be the global section if no
    // game was specified, otherwise the game's node) in the file config, which
    // will be written back to disk
    Util::Config::Node *fileConfigRoot = game.name.empty() ? fileConfig : fileConfig->TryGet(game.name);
    if (fileConfigRoot == nullptr)
    {
      fileConfigRoot = &fileConfig->Add(game.name);
    }

    // Configure the inputs
    if (Inputs->ConfigureInputs(game, xOffset, yOffset, xRes, yRes))
    {
      // Write input configuration and input system settings to config file
      Inputs->StoreToConfig(fileConfigRoot);
      Util::Config::WriteINIFile(s_configFilePath, *fileConfig, TimestampedConfigFileComment("Updated from input configuration."));

      // Also save to runtime configuration in case we proceed and play
      Inputs->StoreToConfig(runtimeConfig);
    }
    else
      puts("Configuration aborted...");
    puts("");
  }

  return Result::OKAY;
}

// Print game list
static void PrintGameList(const std::string &xml_file, const std::map<std::string, Game> &games)
{
  if (games.empty())
  {
    puts("No games defined.");
    return;
  }
  printf("Games defined in %s:\n", xml_file.c_str());
  puts("");
  puts("    ROM Set          Title");
  puts("    -------          -----");
  for (auto &v: games)
  {
    const Game &game = v.second;
    printf("    %s", game.name.c_str());
    for (size_t i = game.name.length(); i < 10; i++)  // pad for alignment
      printf(" ");
    if (!game.version.empty())
      printf("       %s (%s)\n", game.title.c_str(), game.version.c_str());
    else
      printf("       %s\n", game.title.c_str());
  }
}

static void LogConfig(const Util::Config::Node &config)
{
  InfoLog("Runtime configuration:");
  for (auto &child: config)
  {
    if (child.Empty())
      InfoLog("  %s=<empty>", child.Key().c_str());
    else
      InfoLog("  %s=%s", child.Key().c_str(), child.ValueAs<std::string>().c_str());
  }
  InfoLog("");
}

Util::Config::Node DefaultConfig()
{
  Util::Config::Node config("Global");
  
  config.Set("GameXMLFile", s_gameXMLFilePath);
  config.Set("InitStateFile", "");
  // Where the setting window looks for ROM sets on a fresh install. C:\Downloads because a
  // downloaded romset zip is almost always still sitting there; the user can point it anywhere from
  // the Browse button, and an existing Supermodel.ini keeps whatever it already had.
  config.Set<std::string>("ROMPath", "C:/Downloads", "ROM", "", "");
  // Control preset: "standard" (one layout shared by Xbox/PS pads), "keyboard" or "custom".
  // ControllerFamily records which pad family the user picked ("auto" for detection).
  // "8bitdo" is in the allow-list because DetectController() can return it - without the entry the
  // detection line named a family the combo box could not display, so the pad was recognised and
  // then still shown as unselected.
  config.Set<std::string>("ControlPreset", "standard", "Input", "", "", { "standard", "keyboard", "custom" });
  config.Set<std::string>("ControllerFamily", "auto", "Input", "", "",
    { "auto", "xbox360", "xboxone", "xboxseries", "ps3", "ps4", "ps5", "8bitdo", "generic" });
  // CModel3
  config.Set("PowerPCFrequency", 0u, "Core", 0u, 200u);
  config.Set("MultiThreaded", true,"Core");
  config.Set("GPUMultiThreaded", true, "Core");
  // Drive sound board emulation from the audio callback rather than the render loop, so that a stall
  // in the frame loop (dragging the window, a busy GPU) freezes the picture without interrupting the
  // sound. See the note in CModel3::Reset().
  config.Set("AsyncSoundBoardThread", true, "Core");
  // 2D and 3D graphics engines
#ifndef SUPERMODEL_OSX
  config.Set("MultiTexture", false, "Legacy3D");
  config.Set<std::string>("VertexShader", "", "Legacy3D", "", "");
  config.Set<std::string>("FragmentShader", "", "Legacy3D", "", "");
#endif
  
  // CSoundBoard
  config.Set("EmulateSound", true, "Sound");
  config.Set("Balance", 0.0f, "Sound", -100.f, 100.f);
  config.Set("BalanceLeftRight", 0.0f, "Sound", -100.f, 100.f);
  config.Set("BalanceFrontRear", 0.0f, "Sound", -100.f, 100.f);
  config.Set("NbSoundChannels", 4, "Sound", 0, 0, { 1,2,4 });
  config.Set("SoundFreq", 57.6f, "Sound", 0.0f, 0.0f, { 57.524160f, 60.f }); // 60.0f? 57.524160f?
  // CDSB
  
  config.Set("EmulateDSB", true, "Sound");
  config.Set("SoundVolume", 100, "Sound", 0, 200);
  config.Set("MusicVolume", 100, "Sound", 0, 200);
  // Name of the host audio output device to use. Empty selects the system default.
  config.Set<std::string>("AudioDevice", "", "Sound", "", "");
  // Size of the audio output buffer, as a percentage of the maximum. Emulated audio is produced once
  // per frame from the main loop, so the buffer has to ride out any pause in that loop - under vsync
  // a present can wait on the GPU, and competing with a capture application for the GPU makes that
  // wait much longer. Raise this if the sound still stutters when other applications are busy; lower
  // it to reduce output latency.
  config.Set("AudioLatency", 40, "Sound", 5, 100);
  // Run at above-normal process priority so that the once-per-frame audio production is not starved
  // when the window is in the background. See RaiseProcessPriority().
  config.Set("HighPriority", true, "Sound");
  // Other sound options
  config.Set("LegacySoundDSP", false, "Sound"); // New config option for games that do not play correctly with MAME's SCSP sound core.
  // CDriveBoard
  config.Set("ForceFeedback", false, "ForceFeedback");
  
  // Platform-specific/UI
  config.Set("New3DEngine", true, "Video");
  config.Set("Vulkan3DEngine", true, "Video");
  config.Set("VkValidation", false, "Video");
  // Name of the Vulkan physical device to render with. Empty picks the best available.
  config.Set<std::string>("VulkanDevice", "", "Video", "", "");
  config.Set("QuadRendering", false, "Video");
  config.Set("XResolution", 1280, "Video");
  config.Set("YResolution", 960, "Video");
  // Snap the visible image to a whole-number magnification of the native 496x384 signal, leaving the
  // remainder as black bars. Off by default because filling the display is what most setups want;
  // this is the option to reach for when pixel-exact scaling matters more than screen coverage (the
  // 2D tile layer is a fixed 496x384 texture, so only whole-number scaling leaves it undistorted).
  config.Set("IntegerScale", false, "Video");
  /*
   * Bezel backdrop.
   *
   * Fills the black bars with a picture so the picture does not sit in a void on a widescreen
   * display. 0 = off, 1 = the image shipped with the build, 2 = a PNG of the user's choosing. This
   * is a display preference rather than a game setting, hence the Video group.
   *
   * The image is drawn behind the emulated picture, never over it - the bars are the only part of
   * the screen it touches (see CVulkan2D::DrawBezelPass). Only the Vulkan backend implements it;
   * the OpenGL one ignores the setting.
   */
  config.Set("BezelMode", 0, "Video", 0, 2, { 0, 1, 2 });
  // Path used by BezelMode 2. Empty behaves like the built-in image rather than failing silently.
  config.Set<std::string>("BezelImage", "", "Video", "", "");
  // Percentage multiplier on the backdrop's rgb. The default is deliberately unattenuated: an
  // undimmed picture is what the person who supplied it expects to see, and dimming is one drag
  // away for anyone who finds it distracting next to the game.
  config.Set("BezelBrightness", 100, "Video", 10, 200);
  // Interface language: 0 English (default), 1 Chinese. The label texts are picked up by
  // Localization, so the interface can switch without restarting.
  config.Set("UiLanguage", 0, "Video", 0, 0, { 0,1 });
  config.SetEmpty("WindowXPosition");
  config.SetEmpty("WindowYPosition");
  config.Set("FullScreen", false, "Video");
  config.Set("BorderlessWindow", false, "Video");
  config.Set("Supersampling", 1, "Video", 1, 8);
  // Hardware MSAA for the Vulkan backend. 0 disables it; 2 and 4 select that many samples. Smooths
  // polygon edges at rasterisation time, leaving texture detail untouched.
  config.Set("MSAA", 0, "Video", 0, 0, { 0,2,4 });
  // View cycling for the games with four VR camera buttons. With this on, one button steps through
  // the views instead of needing one button each; VRDefaultView is the view the cycle starts on
  // (2 is the default third-person view in Daytona 2).
  config.Set("VRCycle", true, "Input");
  config.Set("VRDefaultView", 2, "Input", 0, 3);  // Post-processing filters, applied to the finished frame by the Vulkan backend. FXAA smooths
  // edges in a shader (cheap, works everywhere, slightly softens detail); the CRT stage adds
  // scanlines and an aperture-grille shadow mask, which needs a high-resolution display to read as
  // structure rather than noise.
  config.Set("PostFXAA", false, "Video");
  config.Set("PostCRT", 0, "Video", 0, 0, { 0,1,2 });
  config.Set("PostScanlines", 0, "Video", 0, 1024);       // 0 = use the game's own 384 rows
  config.Set("PostScanlineIntensity", 60, "Video", 0, 100);
  config.Set("PostMaskIntensity", 50, "Video", 0, 100);
  // Phosphor mask pattern: 0 aperture grille (Trinitron-style stripes), 1 slot mask (the Sega
  // arcade favourite), 2 shadow mask (dot triad).
  config.Set("PostMaskType", 1, "Video", 0, 0, { 0,1,2 });
  // Output pixels per mask cell. 1 is a 1:1 mapping; at 4K that is finer than the eye can resolve,
  // so 2 or 3 is what actually reads as phosphor rather than noise.
  config.Set("PostMaskScale", 2, "Video", 1, 8);
  // Soft glass glow around highlights, and the brightness compensation for the light the mask and
  // scanlines remove.
  config.Set("PostGlow", 0, "Video", 0, 100);
  // How many mip levels the glow accumulates: a wider halo with more. 1-8.
  config.Set("PostGlowLevels", 4, "Video", 1, 8);
  // Luma percentage a region must exceed before it glows. Raising it restricts the glow to highlights
  // and stops broadly bright areas such as sports-field grass from being lifted along with them.
  config.Set("PostGlowThreshold", 40, "Video", 0, 100);
  config.Set("PostGain", 190, "Video", 100, 250);
  config.Set("CRTcolors", int(0), "Video", 0, 0, { 0,1,2,3,4,5 });      // these might be more user friendly as strings
  config.Set("UpscaleMode", 2, "Video", 0, 0, { 0,1,2,3 });             // to do make strings
  config.Set("WideScreen", false, "Video");
  config.Set("Stretch", false, "Video");
  config.Set("WideBackground", false, "Video");
  config.Set("VSync", true, "Video");
  config.Set("Throttle", true, "Video");
  config.Set("RefreshRate", 60.0f, "Video", 0.0f, 0.0f, { 57.5f,60.f });
  // Opt-in arcade-exact refresh rate. 0 keeps the default, general-purpose path (borderless
  // fullscreen, FIFO or MAILBOX as the swapchain offers). A non-zero value asks for exclusive
  // fullscreen at the closest 57/58 Hz display mode and paces the emulator at that rate, which
  // gives a 1:1 frame-to-scanline mapping. Requires a custom 57.524 Hz resolution (see
  // Scripts/ArcadeDisplay.ps1); if none exists we fall back to the G-SYNC/VRR route below.
  config.Set("ExclusiveRefreshRate", 0.0f, "Video", 0.0f, 0.0f, { 0.0f,57.5242f });
  // Opt-in exclusive fullscreen for VRR panels. The panel scans at whatever rate we present, so
  // G-SYNC/FreeSync reaches the same 1:1 mapping without any custom resolution. Needs VSync on.
  config.Set("ExclusiveFullscreen", false, "Video");
  // Which output to drive. 0 is the primary display, which is what the emulator used before this
  // setting existed and what it falls back to when the chosen output is not attached - a machine
  // that alternates between a multi-monitor desk and a laptop on its own must keep starting.
  //
  // The number is the one Windows reports for the monitor (the "Identify" number in Display
  // Settings), so the same value can be given to Scripts/ArcadeDisplay.ps1 and both halves of the
  // setup end up on the same screen. The maximum is a sanity bound, not a real limit: the interface
  // lists whatever is actually attached.
  config.Set("DisplayNumber", 0, "Video", 0, 16);
  // Keep the console window visible while the settings GUI is running. Off by default so that
  // launching from Explorer only shows the GUI; logging still goes to the log file.
  config.Set("ShowConsole", false, "Video");
  config.Set("ShowFrameRate", false, "Video");
  /*
   * The statistics readout, toggled in play with the key above Tab. Persisted rather than fixed, and
   * off by default: it covers part of the picture, so it is something to turn on when wanted rather
   * than something to notice and have to switch off.
   */
  config.Set("ShowStatsOsd", false, "Video");
  config.Set("Crosshairs", int(0), "Video", 0, 0, { 0,1,2,3 });
  config.Set<std::string>("CrosshairStyle", "vector", "Video", "", "", { "bmp","vector" });
  config.Set("NoWhiteFlash", false, "Video");
  config.Set("FlipStereo", false, "Sound");
#ifdef SUPERMODEL_WIN32
  // XInput by default: nearly every pad on a modern PC presents itself as an Xbox controller, and on
  // XInput each trigger gets its own axis, so the brake binds without the pairing workaround
  // DirectInput needs (see the trigger notice on the Controllers tab). DirectInput is still there
  // for older wheels and flight sticks that XInput cannot see at all.
  config.Set<std::string>("InputSystem", "xinput", "Core", "", "", { "sdl","sdlgamepad","dinput","xinput","rawinput" });
  // DirectInput ForceFeedback
  config.Set("DirectInputConstForceLeftMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputConstForceRightMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputVibrateMax", 100, "ForceFeedback", 0, 100);
  // XInput ForceFeedback
  config.Set("XInputConstForceThreshold", 30, "ForceFeedback", 0, 100);
  config.Set("XInputConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("XInputVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("XInputStereoVibration", true, "ForceFeedback");
  // SDL ForceFeedback
  config.Set("SDLConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLConstForceThreshold", 30, "ForceFeedback", 0, 100);
#else
  config.Set<std::string>("InputSystem", "sdl", "Core", "", "", { "sdl","sdlgamepad" });
  // SDL ForceFeedback
  config.Set("SDLConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLConstForceThreshold", 30, "ForceFeedback", 0, 100);
#endif
  // NetBoard
  //
  // On by default, and PortIn == PortOut on the loopback address. That pairing is what lets a single
  // machine satisfy its own link check: TCPSend connects to 127.0.0.1:PortOut while TCPReceive
  // listens on PortIn, so with one shared port the two meet and the board reports connected.
  //
  // Daytona 2, Scud Race, Sega Rally 2, Le Mans 24, Von 2, Dirt Devils and the Harley and Spikeout
  // boards will not boot past the network self-test without this, so leaving Network off is not a
  // safe default - it looks like a hang on a black screen. Games without a network board ignore
  // these settings entirely.
  //
  // Do NOT give the two ports different values to "fix" anything: that is exactly what breaks it.
  config.Set("Network", true, "Network");
  config.Set("SimulateNet", true, "Network");
  config.Set("PortIn", unsigned(1970), "Network");
  config.Set("PortOut", unsigned(1970), "Network");
  config.Set<std::string>("AddressOut", "127.0.0.1", "Network", "", "");

#ifdef SUPERMODEL_WIN32
  // Outputs and the memory/texture dumps share the Network page. Their old "Misc" group was four
  // controls in a page of its own; both groups are about linking and feeding data out of the
  // emulator, and they fit together without crowding.
  config.Set<std::string>("Outputs", "none", "Network", "", "", { "none","win","net" });
#else
  config.Set<std::string>("Outputs", "none", "Network", "", "", { "none","net" });
#endif
  config.Set<bool>("OutputsWithLF", false, "Network");
  config.Set<unsigned int>("OutputsTCPPort", 8000, "Network", 1024, 65535);
  config.Set<unsigned int>("OutputsUDPBroadcastPort", 8001, "Network", 1024, 65535);

  config.Set("DumpMemory", false, "Network");
  config.Set("DumpTextures", false, "Network");

  //
  // Input sensitivity
  //
  config.Set<unsigned>("InputDigitalSensitivity", DEFAULT_DIGITAL_SENSITIVITY, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputDigitalDecaySpeed", DEFAULT_DIGITAL_DECAYSPEED, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputKeySensitivity", DEFAULT_DIGITAL_SENSITIVITY, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputKeyDecaySpeed", DEFAULT_DIGITAL_DECAYSPEED, "Sensitivity", 0, 100);

  config.Set<unsigned>("InputMouseXDeadZone", DEFAULT_MSE_DEADZONE, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputMouseYDeadZone", DEFAULT_MSE_DEADZONE, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputMouseZDeadZone", DEFAULT_MSE_DEADZONE, "Sensitivity", 0, 100);

  //
  // controls
  //

  // Common
  //
  // Coin and Start follow the standardised pad positions rather than whatever a pad prints on the
  // buttons: button index 6 is the Back/Select/Share position and index 7 is the
  // Start/Options/Menu position. Pads differ in labelling (Xbox prints "Back" and "Start", Sony
  // prints "Select"/"Option", newer pads print "Share"/"Menu") but the position is the same, which
  // is what these indices refer to.
  //
  // These values previously pointed at indices 8 and 9 - the stick clicks - so that pressing a
  // thumbstick inserted a coin and pressing the other started the game. That only bit anyone who
  // never applied a control preset, because the standard preset overwrites both entries.
  config.Set<std::string>("InputStart1", "KEY_1,JOY1_BUTTON8", "Input", "", "");
  config.Set<std::string>("InputStart2", "KEY_2,JOY2_BUTTON8", "Input", "", "");
  config.Set<std::string>("InputCoin1", "KEY_3,JOY1_BUTTON7", "Input", "", "");
  config.Set<std::string>("InputCoin2", "KEY_4,JOY2_BUTTON7", "Input", "", "");
  config.Set<std::string>("InputServiceA", "KEY_5", "Input", "", "");
  config.Set<std::string>("InputServiceB", "KEY_7", "Input", "", "");
  config.Set<std::string>("InputTestA", "KEY_6", "Input", "", "");
  config.Set<std::string>("InputTestB", "KEY_8", "Input", "", "");

  // 4-way digital joysticks
  // 4-way digital joysticks. Both the left stick and the D-pad drive the stick: the D-pad is the
  // natural control for the arcade games that used a lever, and having the stick work as well
  // costs nothing. The D-pad is hat 0, which the config names POV1 (the joy part table maps
  // "POV1_UP" onto JoyPOV0Up).
  config.Set<std::string>("InputJoyUp", "KEY_UP,JOY1_UP,JOY1_POV1_UP", "Input", "", "");
  config.Set<std::string>("InputJoyDown", "KEY_DOWN,JOY1_DOWN,JOY1_POV1_DOWN", "Input", "", "");
  config.Set<std::string>("InputJoyLeft", "KEY_LEFT,JOY1_LEFT,JOY1_POV1_LEFT", "Input", "", "");
  config.Set<std::string>("InputJoyRight", "KEY_RIGHT,JOY1_RIGHT,JOY1_POV1_RIGHT", "Input", "", "");
  config.Set<std::string>("InputJoyUp2", "JOY2_UP,JOY2_POV1_UP", "Input", "", "");
  config.Set<std::string>("InputJoyDown2", "JOY2_DOWN,JOY2_POV1_DOWN", "Input", "", "");
  config.Set<std::string>("InputJoyLeft2", "JOY2_LEFT,JOY2_POV1_LEFT", "Input", "", "");
  config.Set<std::string>("InputJoyRight2", "JOY2_RIGHT,JOY2_POV1_RIGHT", "Input", "", "");

  // Fighting game buttons
  config.Set<std::string>("InputPunch", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputKick", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputGuard", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputEscape", "KEY_F,JOY1_BUTTON4", "Input", "", "");
  config.Set<std::string>("InputPunch2", "JOY2_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputKick2", "JOY2_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputGuard2", "JOY2_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputEscape2", "JOY2_BUTTON4", "Input", "", "");

  // Spikeout buttons
  config.Set<std::string>("InputShift", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputBeat", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputCharge", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputJump", "KEY_F,JOY1_BUTTON4", "Input", "", "");

  // Virtua Striker buttons
  config.Set<std::string>("InputShortPass", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputLongPass", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputShoot", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputShortPass2", "JOY2_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputLongPass2", "JOY2_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputShoot2", "JOY2_BUTTON3", "Input", "", "");

  // Steering wheel. The D-pad is a digital fallback for the stick.
  config.Set<std::string>("InputSteeringLeft", "KEY_LEFT,JOY1_POV1_LEFT", "Input", "", "");
  config.Set<std::string>("InputSteeringRight", "KEY_RIGHT,JOY1_POV1_RIGHT", "Input", "", "");
  config.Set<std::string>("InputSteering", "JOY1_XAXIS", "Input", "", "");

  // Pedals. The accelerator is a face button (the X position, index 2) so it behaves the same
  // whatever the input system; the brake is the right analogue trigger. XInput and SDL
  // GameController give each trigger its own axis, so the right one is RZ there, whereas the
  // legacy DirectInput (and plain SDL joystick) path shares a single Z axis and puts the right
  // trigger on its positive half. The default input system is the legacy one on every platform,
  // hence ZAXIS_POS here; ApplyControlPreset() picks the right variant when a preset is applied.
  //
  // These were previously bound to the left stick (JOY1_UP / JOY1_DOWN), which made the car
  // accelerate by pushing the stick forward rather than with a button.
  config.Set<std::string>("InputAccelerator", "KEY_UP,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputBrake", "KEY_DOWN,JOY1_ZAXIS_POS", "Input", "", "");

  // Up/down shifter manual transmission (all racers): right bumper up, left bumper down
  config.Set<std::string>("InputGearShiftUp", "KEY_Y,JOY1_BUTTON6", "Input", "", "");
  config.Set<std::string>("InputGearShiftDown", "KEY_H,JOY1_BUTTON5", "Input", "", "");

  // 4-Speed manual transmission (Daytona 2, Sega Rally 2, Scud Race). The four individual gears
  // are keyboard-only: the sequential pair above covers every Model 3 gearbox, which leaves the
  // pad buttons free for the view buttons. Neutral sits on A. Binding these to pad buttons would
  // collide with the view buttons and, on the old values, with coin/start as well.
  config.Set<std::string>("InputGearShift1", "KEY_Q", "Input", "", "");
  config.Set<std::string>("InputGearShift2", "KEY_W", "Input", "", "");
  config.Set<std::string>("InputGearShift3", "KEY_E", "Input", "", "");
  config.Set<std::string>("InputGearShift4", "KEY_R", "Input", "", "");
  config.Set<std::string>("InputGearShiftN", "KEY_T,JOY1_BUTTON1", "Input", "", "");

  // VR4 view change buttons (Daytona 2, Le Mans 24, Scud Race). These must not share buttons with
  // the shifter, or the car would change gear while braking, so they use B/Y and the stick clicks.
  config.Set<std::string>("InputVR1", "KEY_A,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputVR2", "KEY_S,JOY1_BUTTON4", "Input", "", "");
  config.Set<std::string>("InputVR3", "KEY_D,JOY1_BUTTON9", "Input", "", "");
  config.Set<std::string>("InputVR4", "KEY_F,JOY1_BUTTON10", "Input", "", "");

  // Single-button view cycling. With VRCycle on, pressing this advances through the four views and
  // vr1..vr4 above are ignored; with it off this does nothing and the four buttons behave as before.
  // Bound to the Y position on a pad, which for these games is otherwise only VR2 - and VR2 is inert
  // while cycling is on, so the two can share the button without conflicting. Keyboarding uses D,
  // which is likewise only VR3's key. The point of the whole thing is that one button then does the
  // job of four, freeing the others.
  config.Set<std::string>("InputVRNext", "KEY_D,JOY1_BUTTON4", "Input", "", "");

  // Single view change button (Dirt Devils, ECA, Harley-Davidson, Sega Rally 2). Those games use
  // this instead of VR1..VR4, so it could share a button with one of them, but the D-pad up is free
  // on every racer (only left/right are used, as a digital steering fallback) and giving it its own
  // binding keeps the two functions visibly distinct. The joystick inputs that would otherwise use
  // the D-pad are gated to INPUT_JOYSTICK1 games, which no racer is.
  config.Set<std::string>("InputViewChange", "KEY_A,JOY1_POV1_UP", "Input", "", "");

  // Handbrake (Sega Rally 2)
  config.Set<std::string>("InputHandBrake", "KEY_S,JOY1_POV1_DOWN", "Input", "", "");

  // Harley-Davidson controls. That game has no VR buttons, so the face button Y is free here.
  // RearBrake used to sit on the right bumper, which is GearShiftUp: Harley declares shiftupdown
  // as well as its own controls, so the bumper shifted up and applied the rear brake at once.
  config.Set<std::string>("InputRearBrake", "KEY_S,JOY1_BUTTON4", "Input", "", "");
  config.Set<std::string>("InputMusicSelect", "KEY_D,JOY1_BUTTON1", "Input", "", "");

  // Virtual On macros
  config.Set<std::string>("InputTwinJoyTurnLeft", "KEY_Q,JOY1_RXAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputTwinJoyTurnRight", "KEY_W,JOY1_RXAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputTwinJoyForward", "KEY_UP,JOY1_YAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputTwinJoyReverse", "KEY_DOWN,JOY1_YAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputTwinJoyStrafeLeft", "KEY_LEFT,JOY1_XAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputTwinJoyStrafeRight", "KEY_RIGHT,JOY1_XAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputTwinJoyJump", "KEY_E,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputTwinJoyCrouch", "KEY_R,JOY1_BUTTON2", "Input", "", "");

  // Virtual On individual joystick mapping
  config.Set<std::string>("InputTwinJoyLeft1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyRight1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyRight2", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyUp1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyDown1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyDown2", "NONE", "Input", "", "");

  // Virtual On buttons
  config.Set<std::string>("InputTwinJoyShot1", "KEY_A,JOY1_BUTTON5", "Input", "", "");
  config.Set<std::string>("InputTwinJoyShot2", "KEY_S,JOY1_BUTTON6", "Input", "", "");
  // Virtual On macros. Turbo used to sit on JOY1_BUTTON7/8, which are the Back/Select and Start
  // positions - i.e. inserting a coin and starting the game. The face buttons X and Y are what the
  // standard layout uses instead; A and B are the jump/crouch pair and the bumpers are the shots.
  config.Set<std::string>("InputTwinJoyTurbo1", "KEY_Z,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputTwinJoyTurbo2", "KEY_X,JOY1_BUTTON4", "Input", "", "");

  // Analog joystick (Star Wars Trilogy)
  config.Set<std::string>("InputAnalogJoyLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyX", "JOY1_XAXIS,MOUSE_XAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyY", "JOY1_YAXIS,MOUSE_YAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyTrigger", "KEY_A,JOY1_ZAXIS_POS,MOUSE_LEFT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyEvent", "KEY_S,JOY1_ZAXIS_NEG,MOUSE_RIGHT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyTrigger2", "JOY2_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyEvent2", "NONE", "Input", "", "");

  // Light guns (Lost World)
  config.Set<std::string>("InputGunLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputGunRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputGunUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputGunDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputGunX", "MOUSE_XAXIS,JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputGunY", "MOUSE_YAXIS,JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputTrigger", "KEY_A,JOY1_ZAXIS_POS,MOUSE_LEFT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputOffscreen", "KEY_S,JOY1_ZAXIS_NEG,MOUSE_RIGHT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAutoTrigger", "0", "Input", "", "");
  config.Set<std::string>("InputGunLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunRight2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunDown2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunX2", "JOY2_XAXIS", "Input", "", "");
  config.Set<std::string>("InputGunY2", "JOY2_YAXIS", "Input", "", "");
  config.Set<std::string>("InputTrigger2", "JOY2_ZAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputOffscreen2", "JOY2_ZAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputAutoTrigger2", "0", "Input", "", "");

  // Analog guns (Ocean Hunter, LA Machineguns)
  config.Set<std::string>("InputAnalogGunLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputAnalogGunRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputAnalogGunUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputAnalogGunDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputAnalogGunX", "MOUSE_XAXIS,JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogGunY", "MOUSE_YAXIS,JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerLeft", "KEY_A,JOY1_ZAXIS_NEG,MOUSE_LEFT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerRight", "KEY_S,JOY1_ZAXIS_POS,MOUSE_RIGHT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogGunLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunRight2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunDown2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunX2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunY2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerRight2", "NONE", "Input", "", "");

  // Ski Champ controls
  config.Set<std::string>("InputSkiLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputSkiRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputSkiUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputSkiDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputSkiX", "JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputSkiY", "JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputSkiPollLeft", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputSkiPollRight", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputSkiSelect1", "KEY_Q,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputSkiSelect2", "KEY_W,JOY1_BUTTON4", "Input", "", "");
  config.Set<std::string>("InputSkiSelect3", "KEY_E,JOY1_BUTTON5", "Input", "", "");

  // Magical Truck Adventure controls
  config.Set<std::string>("InputMagicalLeverUp1", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputMagicalLeverDown1", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputMagicalLeverUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputMagicalLeverDown2", "NONE", "Input", "", "");
  config.Set<std::string>("InputMagicalLever1", "JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputMagicalLever2", "JOY2_YAXIS", "Input", "", "");
  config.Set<std::string>("InputMagicalPedal1", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputMagicalPedal2", "KEY_S,JOY2_BUTTON1", "Input", "", "");

  // Sega Bass Fishing / Get Bass controls
  config.Set<std::string>("InputFishingRodLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputFishingRodRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputFishingRodUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputFishingRodDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputFishingStickLeft", "KEY_A", "Input", "", "");
  config.Set<std::string>("InputFishingStickRight", "KEY_D", "Input", "", "");
  config.Set<std::string>("InputFishingStickUp", "KEY_W", "Input", "", "");
  config.Set<std::string>("InputFishingStickDown", "KEY_S", "Input", "", "");
  config.Set<std::string>("InputFishingRodX", "JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingRodY", "JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingStickX", "JOY1_RXAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingStickY", "JOY1_RYAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingReel", "KEY_SPACE,JOY1_ZAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputFishingCast", "KEY_Z,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputFishingSelect", "KEY_X,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputFishingTension", "KEY_T,JOY1_ZAXIS_NEG", "Input", "", "");

  // Lay the built-in control preset over the mappings above so that the defaults, the
  // settings window's preset chooser and the documentation all agree on one layout.
  ApplyControlPreset(config, "standard", config["InputSystem"].ValueAs<std::string>());

  return config;
}

/*
 * Control preset
 * --------------
 * The preset is normally chosen in the settings window, but it can also be named on the
 * command line so that a setup can be produced (or repaired) without opening the GUI:
 *
 *     Supermodel game.zip -preset=standard -input-system=xinput
 *
 * It is applied to the fully merged run-time configuration, so it overrides whatever the
 * configuration file says about the individual inputs.
 */
static void ApplyPresetFromCommandLine(Util::Config::Node& config, const std::string& preset)
{
  if (preset.empty() || preset == "custom")
    return;

  if (preset != "standard" && preset != "keyboard")
  {
    ErrorLog("Unknown control preset '%s'. Use standard, keyboard or custom.", preset.c_str());
    return;
  }

  config.Set("ControlPreset", preset);
  ApplyControlPreset(config, preset, config["InputSystem"].ValueAs<std::string>());
  InfoLog("Applied the '%s' control preset.", preset.c_str());
}

/*
 * Control presets
 * ---------------
 *
 * The standardised gamepad layout follows the button numbering that both XInput and SDL's
 * GameController API use (the "standard" mapping every modern pad is normalised to):
 *
 *    1 = A / Cross        2 = B / Circle      3 = X / Square      4 = Y / Triangle
 *    5 = LB / L1          6 = RB / R1         7 = Back / Select   8 = Start / Options
 *    9 = L3 (stick click) 10 = R3 (stick click)
 *
 * Because both APIs normalise to this numbering, one table covers Xbox 360, Xbox One,
 * Xbox Series and DualShock/DualSense pads. The families differ in their printed labels,
 * not in the indices Supermodel sees. Only the analogue triggers need special treatment:
 * XInput and SDL give each trigger its own axis (Z and RZ), whereas legacy DirectInput
 * reports them as a single shared Z axis that is negative for left and positive for right.
 */

namespace
{
	struct ControlMapping
	{
		const char* key;
		const char* value;
	};

	// Mapping for every configurable game input, suited to a standardised gamepad.
	const ControlMapping s_standardMappings[] =
	{
		// Common controls
		{ "InputStart1",               "KEY_1,JOY1_BUTTON8" },
		{ "InputStart2",               "KEY_2,JOY2_BUTTON8" },
		{ "InputCoin1",                "KEY_3,JOY1_BUTTON7" },
		{ "InputCoin2",                "KEY_4,JOY2_BUTTON7" },
		{ "InputServiceA",             "KEY_5" },
		{ "InputServiceB",             "KEY_7" },
		{ "InputTestA",                "KEY_6" },
		{ "InputTestB",                "KEY_8" },

		// 4-way digital joysticks: left stick and D-pad both drive the stick
		{ "InputJoyUp",                "KEY_UP,JOY1_UP,JOY1_POV1_UP" },
		{ "InputJoyDown",              "KEY_DOWN,JOY1_DOWN,JOY1_POV1_DOWN" },
		{ "InputJoyLeft",              "KEY_LEFT,JOY1_LEFT,JOY1_POV1_LEFT" },
		{ "InputJoyRight",             "KEY_RIGHT,JOY1_RIGHT,JOY1_POV1_RIGHT" },
		{ "InputJoyUp2",               "JOY2_UP,JOY2_POV1_UP" },
		{ "InputJoyDown2",             "JOY2_DOWN,JOY2_POV1_DOWN" },
		{ "InputJoyLeft2",             "JOY2_LEFT,JOY2_POV1_LEFT" },
		{ "InputJoyRight2",            "JOY2_RIGHT,JOY2_POV1_RIGHT" },

		// Fighting game buttons
		{ "InputPunch",                "KEY_A,JOY1_BUTTON1" },
		{ "InputKick",                 "KEY_S,JOY1_BUTTON2" },
		{ "InputGuard",                "KEY_D,JOY1_BUTTON3" },
		{ "InputEscape",               "KEY_F,JOY1_BUTTON4" },
		{ "InputPunch2",               "JOY2_BUTTON1" },
		{ "InputKick2",                "JOY2_BUTTON2" },
		{ "InputGuard2",               "JOY2_BUTTON3" },
		{ "InputEscape2",              "JOY2_BUTTON4" },

		// Spikeout buttons
		{ "InputShift",                "KEY_A,JOY1_BUTTON1" },
		{ "InputBeat",                 "KEY_S,JOY1_BUTTON2" },
		{ "InputCharge",               "KEY_D,JOY1_BUTTON3" },
		{ "InputJump",                 "KEY_F,JOY1_BUTTON4" },

		// Virtua Striker buttons
		{ "InputShortPass",            "KEY_A,JOY1_BUTTON1" },
		{ "InputLongPass",             "KEY_S,JOY1_BUTTON2" },
		{ "InputShoot",                "KEY_D,JOY1_BUTTON3" },
		{ "InputShortPass2",           "JOY2_BUTTON1" },
		{ "InputLongPass2",            "JOY2_BUTTON2" },
		{ "InputShoot2",               "JOY2_BUTTON3" },

		// Steering. Left stick steers, with the D-pad as a digital fallback.
		{ "InputSteeringLeft",         "KEY_LEFT,JOY1_POV1_LEFT" },
		{ "InputSteeringRight",        "KEY_RIGHT,JOY1_POV1_RIGHT" },
		{ "InputSteering",             "JOY1_XAXIS" },

		// Pedals: right trigger accelerates, left trigger brakes (filled in below)
		{ "InputAccelerator",          nullptr },
		{ "InputBrake",                nullptr },

		// Sequential shifter: right bumper up, left bumper down
		{ "InputGearShiftUp",          "KEY_Y,JOY1_BUTTON6" },
		{ "InputGearShiftDown",        "KEY_H,JOY1_BUTTON5" },

		// The 4-speed shifter has no dedicated pad buttons by default: the sequential
		// pair above covers the gearbox of every Model 3 racer, which leaves the face
		// buttons free for other things. Bind these in the settings window if you would
		// rather have one button per gear. Neutral sits on A, which the Sega Rally 2
		// handbrake also uses - those games never run together, and Neutral must not
		// share the shifter bumpers or the pad would change gear while braking.
		{ "InputGearShift1",           "KEY_Q" },
		{ "InputGearShift2",           "KEY_W" },
		{ "InputGearShift3",           "KEY_E" },
		{ "InputGearShift4",           "KEY_R" },
		{ "InputGearShiftN",           "KEY_T,JOY1_BUTTON1" },

		// Camera buttons. VR1..VR4 (Daytona 2, Scud Race, Le Mans) and the single View Change
		// button (Sega Rally 2, Dirt Devils, ECA, Harley) are never used by the same game, so
		// they may legally share. They no longer need to, though: the D-pad up/down are free on
		// every racer (only left/right are used, as a digital steering fallback) and the joystick
		// inputs that would otherwise claim them are gated to INPUT_JOYSTICK1 games. Sharing a
		// button across games that never run together is harmless, but distinct bindings make the
		// key list readable and the intent obvious, so ViewChange and HandBrake get their own.
		{ "InputVR1",                  "KEY_A,JOY1_BUTTON2" },
		{ "InputVR2",                  "KEY_S,JOY1_BUTTON4" },
		{ "InputVR3",                  "KEY_D,JOY1_BUTTON9" },
		{ "InputVR4",                  "KEY_F,JOY1_BUTTON10" },
		{ "InputVRNext",               "KEY_D,JOY1_BUTTON4" },
		{ "InputViewChange",           "KEY_A,JOY1_POV1_UP" },

		// Sega Rally 2 handbrake. That game uses ViewChange but not VR, so the D-pad down is free.
		{ "InputHandBrake",            "KEY_S,JOY1_POV1_DOWN" },

		// Harley-Davidson. It has no VR buttons, so the face button Y is free here. RearBrake used
		// to sit on the right bumper, which is GearShiftUp: Harley declares shiftupdown as well as
		// its own controls, so the same bumper shifted up *and* applied the rear brake.
		{ "InputRearBrake",            "KEY_S,JOY1_BUTTON4" },
		{ "InputMusicSelect",          "KEY_D,JOY1_BUTTON1" },

		// Virtual On macros (right stick plus face buttons)
		{ "InputTwinJoyTurnLeft",      "KEY_Q,JOY1_RXAXIS_NEG" },
		{ "InputTwinJoyTurnRight",     "KEY_W,JOY1_RXAXIS_POS" },
		{ "InputTwinJoyForward",       "KEY_UP,JOY1_YAXIS_NEG" },
		{ "InputTwinJoyReverse",       "KEY_DOWN,JOY1_YAXIS_POS" },
		{ "InputTwinJoyStrafeLeft",    "KEY_LEFT,JOY1_XAXIS_NEG" },
		{ "InputTwinJoyStrafeRight",   "KEY_RIGHT,JOY1_XAXIS_POS" },
		{ "InputTwinJoyJump",          "KEY_E,JOY1_BUTTON1" },
		{ "InputTwinJoyCrouch",        "KEY_R,JOY1_BUTTON2" },
		{ "InputTwinJoyShot1",         "KEY_A,JOY1_BUTTON5" },
		{ "InputTwinJoyShot2",         "KEY_S,JOY1_BUTTON6" },
		{ "InputTwinJoyTurbo1",        "KEY_Z,JOY1_BUTTON3" },
		{ "InputTwinJoyTurbo2",        "KEY_X,JOY1_BUTTON4" },

		// Analog joystick (Star Wars Trilogy)
		{ "InputAnalogJoyLeft",        "KEY_LEFT" },
		{ "InputAnalogJoyRight",       "KEY_RIGHT" },
		{ "InputAnalogJoyUp",          "KEY_UP" },
		{ "InputAnalogJoyDown",        "KEY_DOWN" },
		{ "InputAnalogJoyX",           "JOY1_XAXIS,MOUSE_XAXIS" },
		{ "InputAnalogJoyY",           "JOY1_YAXIS,MOUSE_YAXIS" },
		{ "InputAnalogJoyTrigger",     "KEY_A,JOY1_RZAXIS,MOUSE_LEFT_BUTTON" },
		{ "InputAnalogJoyEvent",       "KEY_S,JOY1_ZAXIS,MOUSE_RIGHT_BUTTON" },
		{ "InputAnalogJoyTrigger2",    "JOY2_BUTTON1" },

		// Light guns (Lost World): aimed with the left stick
		{ "InputGunLeft",              "KEY_LEFT" },
		{ "InputGunRight",             "KEY_RIGHT" },
		{ "InputGunUp",                "KEY_UP" },
		{ "InputGunDown",              "KEY_DOWN" },
		{ "InputGunX",                 "MOUSE_XAXIS,JOY1_XAXIS" },
		{ "InputGunY",                 "MOUSE_YAXIS,JOY1_YAXIS" },
		{ "InputTrigger",              "KEY_A,JOY1_RZAXIS,MOUSE_LEFT_BUTTON" },
		{ "InputOffscreen",            "KEY_S,JOY1_ZAXIS,MOUSE_RIGHT_BUTTON" },
		{ "InputGunX2",                "JOY2_XAXIS" },
		{ "InputGunY2",                "JOY2_YAXIS" },
		{ "InputTrigger2",             "JOY2_RZAXIS" },
		{ "InputOffscreen2",           "JOY2_ZAXIS" },

		// Analog guns (Ocean Hunter, LA Machineguns)
		{ "InputAnalogGunLeft",        "KEY_LEFT" },
		{ "InputAnalogGunRight",       "KEY_RIGHT" },
		{ "InputAnalogGunUp",          "KEY_UP" },
		{ "InputAnalogGunDown",        "KEY_DOWN" },
		{ "InputAnalogGunX",           "MOUSE_XAXIS,JOY1_XAXIS" },
		{ "InputAnalogGunY",           "MOUSE_YAXIS,JOY1_YAXIS" },
		{ "InputAnalogTriggerLeft",    "KEY_A,JOY1_ZAXIS,MOUSE_LEFT_BUTTON" },
		{ "InputAnalogTriggerRight",   "KEY_S,JOY1_RZAXIS,MOUSE_RIGHT_BUTTON" },

		// Ski Champ
		{ "InputSkiLeft",              "KEY_LEFT" },
		{ "InputSkiRight",             "KEY_RIGHT" },
		{ "InputSkiUp",                "KEY_UP" },
		{ "InputSkiDown",              "KEY_DOWN" },
		{ "InputSkiX",                 "JOY1_XAXIS" },
		{ "InputSkiY",                 "JOY1_YAXIS" },
		{ "InputSkiPollLeft",          "KEY_A,JOY1_BUTTON1" },
		{ "InputSkiPollRight",         "KEY_S,JOY1_BUTTON2" },
		{ "InputSkiSelect1",           "KEY_Q,JOY1_BUTTON3" },
		{ "InputSkiSelect2",           "KEY_W,JOY1_BUTTON4" },
		{ "InputSkiSelect3",           "KEY_E,JOY1_BUTTON5" },

		// Magical Truck Adventure
		{ "InputMagicalLeverUp1",      "KEY_UP" },
		{ "InputMagicalLeverDown1",    "KEY_DOWN" },
		{ "InputMagicalLever1",        "JOY1_YAXIS" },
		{ "InputMagicalLever2",        "JOY2_YAXIS" },
		{ "InputMagicalPedal1",        "KEY_A,JOY1_BUTTON1" },
		{ "InputMagicalPedal2",        "KEY_S,JOY2_BUTTON1" },

		// Sega Bass Fishing
		{ "InputFishingRodLeft",       "KEY_LEFT" },
		{ "InputFishingRodRight",      "KEY_RIGHT" },
		{ "InputFishingRodUp",         "KEY_UP" },
		{ "InputFishingRodDown",       "KEY_DOWN" },
		{ "InputFishingStickLeft",     "KEY_A" },
		{ "InputFishingStickRight",    "KEY_D" },
		{ "InputFishingStickUp",       "KEY_W" },
		{ "InputFishingStickDown",     "KEY_S" },
		{ "InputFishingRodX",          "JOY1_XAXIS" },
		{ "InputFishingRodY",          "JOY1_YAXIS" },
		{ "InputFishingStickX",        "JOY1_RXAXIS" },
		{ "InputFishingStickY",        "JOY1_RYAXIS" },
		{ "InputFishingReel",          "KEY_SPACE,JOY1_RZAXIS" },
		{ "InputFishingCast",          "KEY_Z,JOY1_BUTTON1" },
		{ "InputFishingSelect",        "KEY_X,JOY1_BUTTON2" },
		{ "InputFishingTension",       "KEY_T,JOY1_ZAXIS" },
	};

	// Same table with the gamepad assignments stripped out, for keyboard-only play.
	const ControlMapping s_keyboardMappings[] =
	{
		{ "InputStart1",               "KEY_1" },
		{ "InputStart2",               "KEY_2" },
		{ "InputCoin1",                "KEY_3" },
		{ "InputCoin2",                "KEY_4" },
		{ "InputServiceA",             "KEY_5" },
		{ "InputServiceB",             "KEY_7" },
		{ "InputTestA",                "KEY_6" },
		{ "InputTestB",                "KEY_8" },
		{ "InputJoyUp",                "KEY_UP" },
		{ "InputJoyDown",              "KEY_DOWN" },
		{ "InputJoyLeft",              "KEY_LEFT" },
		{ "InputJoyRight",             "KEY_RIGHT" },
		{ "InputJoyUp2",               "NONE" },
		{ "InputJoyDown2",             "NONE" },
		{ "InputJoyLeft2",             "NONE" },
		{ "InputJoyRight2",            "NONE" },
		{ "InputPunch",                "KEY_A" },
		{ "InputKick",                 "KEY_S" },
		{ "InputGuard",                "KEY_D" },
		{ "InputEscape",               "KEY_F" },
		{ "InputPunch2",               "NONE" },
		{ "InputKick2",                "NONE" },
		{ "InputGuard2",               "NONE" },
		{ "InputEscape2",              "NONE" },
		{ "InputShift",                "KEY_A" },
		{ "InputBeat",                 "KEY_S" },
		{ "InputCharge",               "KEY_D" },
		{ "InputJump",                 "KEY_F" },
		{ "InputShortPass",            "KEY_A" },
		{ "InputLongPass",             "KEY_S" },
		{ "InputShoot",                "KEY_D" },
		{ "InputShortPass2",           "NONE" },
		{ "InputLongPass2",            "NONE" },
		{ "InputShoot2",               "NONE" },
		{ "InputSteeringLeft",         "KEY_LEFT" },
		{ "InputSteeringRight",        "KEY_RIGHT" },
		{ "InputSteering",             "NONE" },
		{ "InputAccelerator",          "KEY_UP" },
		{ "InputBrake",                "KEY_DOWN" },
		{ "InputGearShiftUp",          "KEY_Y" },
		{ "InputGearShiftDown",        "KEY_H" },
		{ "InputGearShift1",           "KEY_Q" },
		{ "InputGearShift2",           "KEY_W" },
		{ "InputGearShift3",           "KEY_E" },
		{ "InputGearShift4",           "KEY_R" },
		{ "InputGearShiftN",           "KEY_T" },
		{ "InputVR1",                  "KEY_A" },
		{ "InputVR2",                  "KEY_S" },
		{ "InputVR3",                  "KEY_D" },
		{ "InputVR4",                  "KEY_F" },
		{ "InputViewChange",           "KEY_A" },
		{ "InputHandBrake",            "KEY_S" },
		{ "InputRearBrake",            "KEY_S" },
		{ "InputMusicSelect",          "KEY_D" },
		{ "InputTwinJoyTurnLeft",      "KEY_Q" },
		{ "InputTwinJoyTurnRight",     "KEY_W" },
		{ "InputTwinJoyForward",       "KEY_UP" },
		{ "InputTwinJoyReverse",       "KEY_DOWN" },
		{ "InputTwinJoyStrafeLeft",    "KEY_LEFT" },
		{ "InputTwinJoyStrafeRight",   "KEY_RIGHT" },
		{ "InputTwinJoyJump",          "KEY_E" },
		{ "InputTwinJoyCrouch",        "KEY_R" },
		{ "InputTwinJoyShot1",         "KEY_A" },
		{ "InputTwinJoyShot2",         "KEY_S" },
		{ "InputTwinJoyTurbo1",        "KEY_Z" },
		{ "InputTwinJoyTurbo2",        "KEY_X" },
		{ "InputAnalogJoyLeft",        "KEY_LEFT" },
		{ "InputAnalogJoyRight",       "KEY_RIGHT" },
		{ "InputAnalogJoyUp",          "KEY_UP" },
		{ "InputAnalogJoyDown",        "KEY_DOWN" },
		{ "InputAnalogJoyX",           "MOUSE_XAXIS" },
		{ "InputAnalogJoyY",           "MOUSE_YAXIS" },
		{ "InputAnalogJoyTrigger",     "KEY_A,MOUSE_LEFT_BUTTON" },
		{ "InputAnalogJoyEvent",       "KEY_S,MOUSE_RIGHT_BUTTON" },
		{ "InputAnalogJoyTrigger2",    "NONE" },
		{ "InputGunLeft",              "KEY_LEFT" },
		{ "InputGunRight",             "KEY_RIGHT" },
		{ "InputGunUp",                "KEY_UP" },
		{ "InputGunDown",              "KEY_DOWN" },
		{ "InputGunX",                 "MOUSE_XAXIS" },
		{ "InputGunY",                 "MOUSE_YAXIS" },
		{ "InputTrigger",              "KEY_A,MOUSE_LEFT_BUTTON" },
		{ "InputOffscreen",            "KEY_S,MOUSE_RIGHT_BUTTON" },
		{ "InputGunX2",                "NONE" },
		{ "InputGunY2",                "NONE" },
		{ "InputTrigger2",             "NONE" },
		{ "InputOffscreen2",           "NONE" },
		{ "InputAnalogGunLeft",        "KEY_LEFT" },
		{ "InputAnalogGunRight",       "KEY_RIGHT" },
		{ "InputAnalogGunUp",          "KEY_UP" },
		{ "InputAnalogGunDown",        "KEY_DOWN" },
		{ "InputAnalogGunX",           "MOUSE_XAXIS" },
		{ "InputAnalogGunY",           "MOUSE_YAXIS" },
		{ "InputAnalogTriggerLeft",    "KEY_A,MOUSE_LEFT_BUTTON" },
		{ "InputAnalogTriggerRight",   "KEY_S,MOUSE_RIGHT_BUTTON" },
		{ "InputSkiLeft",              "KEY_LEFT" },
		{ "InputSkiRight",             "KEY_RIGHT" },
		{ "InputSkiUp",                "KEY_UP" },
		{ "InputSkiDown",              "KEY_DOWN" },
		{ "InputSkiX",                 "MOUSE_XAXIS" },
		{ "InputSkiY",                 "MOUSE_YAXIS" },
		{ "InputSkiPollLeft",          "KEY_A" },
		{ "InputSkiPollRight",         "KEY_S" },
		{ "InputSkiSelect1",           "KEY_Q" },
		{ "InputSkiSelect2",           "KEY_W" },
		{ "InputSkiSelect3",           "KEY_E" },
		{ "InputMagicalLeverUp1",      "KEY_UP" },
		{ "InputMagicalLeverDown1",    "KEY_DOWN" },
		{ "InputMagicalLever1",        "MOUSE_YAXIS" },
		{ "InputMagicalLever2",        "NONE" },
		{ "InputMagicalPedal1",        "KEY_A" },
		{ "InputMagicalPedal2",        "NONE" },
		{ "InputFishingRodLeft",       "KEY_LEFT" },
		{ "InputFishingRodRight",      "KEY_RIGHT" },
		{ "InputFishingRodUp",         "KEY_UP" },
		{ "InputFishingRodDown",       "KEY_DOWN" },
		{ "InputFishingStickLeft",     "KEY_A" },
		{ "InputFishingStickRight",    "KEY_D" },
		{ "InputFishingStickUp",       "KEY_W" },
		{ "InputFishingStickDown",     "KEY_S" },
		{ "InputFishingRodX",          "MOUSE_XAXIS" },
		{ "InputFishingRodY",          "MOUSE_YAXIS" },
		{ "InputFishingStickX",        "MOUSE_XAXIS" },
		{ "InputFishingStickY",        "MOUSE_YAXIS" },
		{ "InputFishingReel",          "KEY_SPACE" },
		{ "InputFishingCast",          "KEY_Z" },
		{ "InputFishingSelect",        "KEY_X" },
		{ "InputFishingTension",       "KEY_T" },
	};

	// Legacy DirectInput has no per-trigger axes, so the trigger-style inputs fall back to
	// the two halves of the shared Z axis. The accelerator and brake are not listed here
	// because they are set per input system in the two tables below.
	const ControlMapping s_legacyTriggerMappings[] =
	{
		{ "InputAnalogJoyTrigger",     "KEY_A,JOY1_ZAXIS_POS,MOUSE_LEFT_BUTTON" },
		{ "InputAnalogJoyEvent",       "KEY_S,JOY1_ZAXIS_NEG,MOUSE_RIGHT_BUTTON" },
		{ "InputTrigger",              "KEY_A,JOY1_ZAXIS_POS,MOUSE_LEFT_BUTTON" },
		{ "InputOffscreen",            "KEY_S,JOY1_ZAXIS_NEG,MOUSE_RIGHT_BUTTON" },
		{ "InputTrigger2",             "JOY2_ZAXIS_POS" },
		{ "InputOffscreen2",           "JOY2_ZAXIS_NEG" },
		{ "InputAnalogTriggerLeft",    "KEY_A,JOY1_ZAXIS_NEG,MOUSE_LEFT_BUTTON" },
		{ "InputAnalogTriggerRight",   "KEY_S,JOY1_ZAXIS_POS,MOUSE_RIGHT_BUTTON" },
		{ "InputFishingReel",          "KEY_SPACE,JOY1_ZAXIS_POS" },
		{ "InputFishingTension",       "KEY_T,JOY1_ZAXIS_NEG" },
	};

	// Pedals. The accelerator is a face button (X on an Xbox pad) so that it behaves the
	// same whatever the input system. The brake uses the right analogue trigger where the
	// input system provides separate trigger axes (XInput, SDL GameController) and falls
	// back to the positive half of the shared Z axis under legacy DirectInput.
	const ControlMapping s_standardPedals[] =
	{
		{ "InputAccelerator",          "KEY_UP,JOY1_BUTTON3" },
		{ "InputBrake",                "KEY_DOWN,JOY1_RZAXIS" },
	};

	const ControlMapping s_legacyPedals[] =
	{
		{ "InputAccelerator",          "KEY_UP,JOY1_BUTTON3" },
		{ "InputBrake",                "KEY_DOWN,JOY1_ZAXIS_POS" },
	};

	void ApplyMappingTable(Util::Config::Node& config, const ControlMapping* table, size_t count)
	{
		for (size_t i = 0; i < count; i++)
		{
			if (table[i].value != nullptr)
				config.Set(table[i].key, std::string(table[i].value));
		}
	}
}

void ApplyControlPreset(Util::Config::Node& config, const std::string& preset, const std::string& inputSystem)
{
	if (preset == "keyboard")
	{
		ApplyMappingTable(config, s_keyboardMappings, sizeof(s_keyboardMappings) / sizeof(s_keyboardMappings[0]));
		return;
	}

	if (preset != "standard")
		return;      // "custom": leave the user's own bindings alone

	ApplyMappingTable(config, s_standardMappings, sizeof(s_standardMappings) / sizeof(s_standardMappings[0]));

	// XInput and SDL GameController report each trigger separately; DirectInput and the
	// plain SDL joystick path share a single Z axis between them.
	const bool perTriggerAxes = (inputSystem == "xinput" || inputSystem == "sdlgamepad");
	if (perTriggerAxes)
		ApplyMappingTable(config, s_standardPedals, sizeof(s_standardPedals) / sizeof(s_standardPedals[0]));
	else
	{
		ApplyMappingTable(config, s_legacyTriggerMappings, sizeof(s_legacyTriggerMappings) / sizeof(s_legacyTriggerMappings[0]));
		ApplyMappingTable(config, s_legacyPedals, sizeof(s_legacyPedals) / sizeof(s_legacyPedals[0]));
	}
}

ControllerInfo DetectController()
{
	ControllerInfo info;

	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
		return info;

	const int count = SDL_NumJoysticks();
	if (count <= 0)
		return info;

	// Prefer a pad SDL recognises as a standard game controller, otherwise take the first
	// joystick, so that wheels and other odd devices still get an answer.
	int chosen = -1;
	for (int i = 0; i < count; i++)
	{
		if (SDL_IsGameController(i))
		{
			chosen = i;
			break;
		}
	}
	if (chosen < 0)
		chosen = 0;

	info.sdlKnowsDevice = SDL_IsGameController(chosen) != SDL_FALSE;

	const char* name = SDL_IsGameController(chosen) ? SDL_GameControllerNameForIndex(chosen) : SDL_JoystickNameForIndex(chosen);
	if (name != nullptr)
		info.name = name;

	// Classify by name. SDL reports the product string, which is enough to tell the
	// families apart for the purpose of picking an input system.
	std::string lower;
	for (const char* p = info.name.c_str(); *p != '\0'; p++)
		lower.push_back((char)tolower((unsigned char)*p));

	auto contains = [&](const char* needle) { return lower.find(needle) != std::string::npos; };

	// Every Xbox pad reports "Xbox" in its product string (360, One and Series alike), so
	// that is tested first: the DualShock 4 also calls itself "Wireless Controller", which
	// would otherwise capture the Xbox 360 Wireless Controller too.
	if (contains("xbox"))
	{
		if (contains("series"))
			info.family = "xboxseries";
		else if (contains("one"))
			info.family = "xboxone";
		else
			info.family = "xbox360";
	}
	else if (contains("dualsense") || contains("ps5") || contains("playstation 5"))
		info.family = "ps5";
	else if (contains("dualshock 4") || contains("ps4") || contains("playstation 4") || contains("wireless controller"))
		info.family = "ps4";
	else if (contains("dualshock 3") || contains("ps3") || contains("playstation 3"))
		info.family = "ps3";
	// 8BitDo pads, by whichever name they report.
	//
	// This is before the generic fallback but *after* the console families, and the ordering is the
	// whole point. An SN30 Pro+ has three switchable modes and it does not use its own name in two
	// of them:
	//
	//   XInput mode      reports "8BitDo SN30 Pro+"      -> matched by "8bitdo"
	//   Switch mode      reports "Nintendo Switch Pro Controller"   -> matched by the line below
	//   macOS mode       reports "8BitDo ..." (usually)
	//
	// The Switch mode is the common one over Bluetooth, and it is why a pad that the user knows is
	// an 8BitDo showed up as "Other PC controller": matching on the vendor string alone misses it.
	// Recognising the Switch Pro Controller name is safe here because Supermodel is not a Switch -
	// a real one is never attached to a PC, so the name can only be a third-party pad emulating it.
	else if (contains("8bitdo") ||
	         (contains("nintendo") && contains("pro controller")))
		info.family = "8bitdo";
	else
		info.family = "generic";

	/*
	 * Which input system to recommend.
	 *
	 * Xbox pads are best driven through XInput: that is where force feedback and the separate
	 * triggers come from. 8BitDo pads are the opposite case - XInput does not see them in their
	 * Bluetooth modes at all, and SDL has explicit mapping entries for the whole family including
	 * the SN30 Pro+ (three GUIDs, plus the Switch Pro Controller entries it uses in Switch mode),
	 * which is what normalises their buttons to the standard numbering the "standard" preset is
	 * written against. So they go to sdlgamepad even though they are not Sony hardware.
	 *
	 * Everything else goes to the GameController API when SDL recognises it, and falls back to
	 * DirectInput when it does not - a raw joystick has no normalised button numbering, so the
	 * preset's JOY1_BUTTONn bindings cannot be trusted there.
	 */
	const bool xboxFamily = (info.family == "xbox360" || info.family == "xboxone" || info.family == "xboxseries");
	if (xboxFamily)
		info.inputSystem = "xinput";
	else if (info.sdlKnowsDevice)
		info.inputSystem = "sdlgamepad";
	else
		info.inputSystem = "dinput";

	return info;
}

static void Title(void)
{
  puts(SUPERMODEL_TITLE_FULL " - a Sega Model 3 arcade emulator.");
  puts("Based on Supermodel " SUPERMODEL_BASE_RELEASE " (core version " SUPERMODEL_VERSION ").");
  puts("Copyright 2003-2026 by The Supermodel Team");
}

static void Help(void)
{
  Util::Config::Node defaultConfig = DefaultConfig();
  puts("Usage: Supermodel <romset> [options]");
  puts("ROM set must be a valid ZIP file containing a single game.");
  puts("");
  puts("General Options:");
  puts("  -?, -h, -help, --help   Print this help text");
  puts("  -gui                    Open the settings window");
  puts("  -print-games            List supported games and quit");
  printf("  -game-xml-file=<file>   ROM set definition file [Default: %s]\n", s_gameXMLFilePath.c_str());
  printf("  -log-output=<outputs>   Log output destination(s) [Default: %s]\n", s_logFilePath.c_str());
  puts("  -log-level=<level>      Logging threshold [Default: info]");
  puts("");
  puts("Core Options:");
  puts("  -ppc-frequency=<mhz>    PowerPC frequency (default varies by stepping)");
  puts("  -no-threads             Disable multi-threading entirely");
  puts("  -gpu-multi-threaded     Run graphics rendering in separate thread [Default]");
  puts("  -no-gpu-thread          Run graphics rendering in main thread");
  puts("  -load-state=<file>      Load save state after starting");
  puts("");
  puts("Video Options:");
  puts("  -res=<x>,<y>            Resolution [Default: 496,384]");
  puts("  -ss=<n>                 Supersampling (range 1-8)");
  puts("  -window-pos=<x>,<y>     Window position [Default: centered]");
  puts("  -window                 Windowed mode [Default]");
  puts("  -borderless             Windowed mode with no border");
  puts("  -fullscreen             Full screen mode");
  puts("  -wide-screen            Expand 3D field of view to screen width");
  puts("  -wide-bg                When wide-screen mode is enabled, also expand the 2D");
  puts("                          background layer to screen width");
  puts("  -stretch                Fit viewport to resolution, ignoring aspect ratio");
  puts("  -upscalemode=<n>        2D layer upscaling filter mode (range 0-3)");
  puts("  -crtcolors=<n>          CRT color emulation (range 0-5)");
  puts("  -no-throttle            Disable frame rate lock");
  puts("  -vsync                  Lock to vertical refresh rate [Default]");
  puts("  -no-vsync               Do not lock to vertical refresh rate");
  puts("  -true-hz                Use true Model 3 refresh rate of 57.524 Hz");
  puts("  -show-fps               Display frame rate in window title bar");
  puts("  -crosshairs=<n>         Crosshairs configuration for gun games:");
  puts("                          0=none [Default], 1=P1 only, 2=P2 only, 3=P1 & P2");
  puts("  -crosshair-style=<s>    Crosshair style: vector or bmp. [Default: vector]");
#ifndef SUPERMODEL_OSX
  puts("  -new3d                  New 3D engine by Ian Curtis [Default]");
#endif
  puts("  -quad-rendering         Enable proper quad rendering");
#ifndef SUPERMODEL_OSX
  puts("  -legacy3d               Legacy 3D engine (faster but less accurate)");
  puts("  -multi-texture          Use 8 texture maps for decoding (legacy engine)");
  puts("  -no-multi-texture       Decode to single texture (legacy engine) [Default]");
#endif
  puts("  -vulkan3d               Vulkan 1.3 Real3D backend (bindless textures)");
  puts("  -vulkan-device=<name>   Vulkan device name to render with (see the settings window)");
  puts("  -vk-validation          Enable Vulkan validation layers with -vulkan3d");
  puts("  -no-white-flash         Disables white flash when games disable 3D rendering");
#ifndef SUPERMODEL_OSX
  puts("  -vert-shader=<file>     Load Real3D vertex shader for 3D rendering (legacy");
  puts("                          engine)");
  puts("  -frag-shader=<file>     Load Real3D fragment shader for 3D rendering (legacy");
  puts("                          engine)");
#endif
  puts("  -print-gl-info          Print OpenGL driver information and quit");
  puts("");
  puts("Audio Options:");
  puts("  -sound-volume=<vol>     Volume of SCSP-generated sound in %, applies only");
  puts("                          when Digital Sound Board is present [Default: 100]");
  puts("  -music-volume=<vol>     Digital Sound Board volume in % [Default: 100]");
  puts("  -balance=<bal>          Relative front/rear balance in % [Default: 0]");
  puts("  -audio-device=<name>    Host audio output device (see the settings window)");
  puts("  -channels=<c>           Number of sound channels to use on host [Default: 4]");
  puts("  -flip-stereo            Swap left and right audio channels");
  puts("  -no-sound               Disable sound board emulation (sound effects)");
  puts("  -no-dsb                 Disable Digital Sound Board (MPEG music)");
  puts("  -new-scsp               New SCSP engine based on MAME [Default]");
  puts("  -legacy-scsp            Legacy SCSP engine by ElSemi");
  puts("");
  puts("Net Options:");
  puts("  -no-net                 Disable net board [Default]");
  puts("  -net                    Enable net board");
  puts("  -simulate-netboard      Simulate the net board [Default]");
  puts("  -emulate-netboard       Emulate the net board (requires -no-threads)");
  puts("");
  puts("Input Options:");
  puts("  -force-feedback         Enable force feedback (DirectInput, XInput)");
  puts("  -config-inputs          Configure keyboards, mice, and game controllers");
  puts("  -preset=<s>             Apply a control preset: standard, keyboard or custom");
#ifdef SUPERMODEL_WIN32
  printf("  -input-system=<s>       Input system [Default: %s]\n", defaultConfig["InputSystem"].ValueAs<std::string>().c_str());
#endif
  puts("  -print-inputs           Prints current input configuration");
  puts("");
  puts("Output Options:");
  printf("  -outputs=<s>            Outputs [Default: %s]\n", defaultConfig["Outputs"].ValueAs<std::string>().c_str());
  puts("");
  puts("Debug Options:");
  puts("  -dump-memory            Write memory regions to files on exit");
  puts("  -dump-textures          Write textures to bitmap image files on exit");
#ifdef SUPERMODEL_DEBUGGER
  puts("  -disable-debugger       Completely disable debugger functionality");
  puts("  -enter-debugger         Enter debugger at start of emulation");
#endif // SUPERMODEL_DEBUGGER
#ifdef DEBUG
  puts("  -gfx-state=<file>       Produce graphics analysis for save state (works only");
  puts("                          with the legacy 3D engine and requires a");
  puts("                          GraphicsAnalysis directory to exist)");
#endif
  puts("");
}

struct ParsedCommandLine
{
  Util::Config::Node config = Util::Config::Node("CommandLine");
  std::vector<std::string> rom_files;
  bool error = false;
  bool print_help = false;
  bool open_gui = false;
  bool print_games = false;
  bool print_gl_info = false;
  bool config_inputs = false;
  bool print_inputs = false;
  bool disable_debugger = false;
  bool enter_debugger = false;
#ifdef DEBUG
  std::string gfx_state;
#endif

  ParsedCommandLine()
  {
    // Logging is special: it is only parsed from the command line and
    // therefore, defaults are needed early
    config.Set("LogOutput", s_logOutputSetting.c_str());
    config.Set("LogLevel", "info");
  }
};

static ParsedCommandLine ParseCommandLine(int argc, char **argv)
{
  ParsedCommandLine cmd_line;
  static const std::map<std::string, std::string> valued_options
  { // -option=value
    { "-game-xml-file",         "GameXMLFile"             },
    { "-load-state",            "InitStateFile"           },
    { "-ppc-frequency",         "PowerPCFrequency"        },
    { "-crosshairs",            "Crosshairs"              },
    { "-crosshair-style",       "CrosshairStyle"          },
    { "-vert-shader",           "VertexShader"            },
    { "-frag-shader",           "FragmentShader"          },
    { "-sound-volume",          "SoundVolume"             },
    { "-music-volume",          "MusicVolume"             },
    { "-balance",               "Balance"                 },
    { "-channels", 	            "NbSoundChannels"         },
    { "-soundfreq",             "SoundFreq"               },
    { "-refresh-rate",          "RefreshRate"             },
    { "-exclusive-refresh-rate","ExclusiveRefreshRate"    },
    { "-display",               "DisplayNumber"           },
    { "-input-system",          "InputSystem"             },
    { "-preset",                "ControlPresetCLI"        },
    { "-vulkan-device",         "VulkanDevice"            },
    { "-audio-device",          "AudioDevice"             },
    { "-outputs",               "Outputs"                 },
    { "-log-output",            "LogOutput"               },
    { "-log-level",             "LogLevel"                }
  };
  static const std::map<std::string, std::pair<std::string, bool>> bool_options
  { // -option
    { "-threads",             { "MultiThreaded",    true } },
    { "-no-threads",          { "MultiThreaded",    false } },
    { "-gpu-multi-threaded",  { "GPUMultiThreaded", true } },
    { "-no-gpu-thread",       { "GPUMultiThreaded", false } },
    { "-async-sound",         { "AsyncSoundBoardThread", true } },
    { "-sync-sound",          { "AsyncSoundBoardThread", false } },
    { "-window",              { "FullScreen",       false } },
    { "-fullscreen",          { "FullScreen",       true } },
    { "-borderless",          { "BorderlessWindow", true } },
    { "-exclusive-fullscreen",{ "ExclusiveFullscreen", true } },
    { "-show-console",        { "ShowConsole", true } },
    { "-no-wide-screen",      { "WideScreen",       false } },
    { "-wide-screen",         { "WideScreen",       true } },
    { "-stretch",             { "Stretch",          true } },
    { "-no-stretch",          { "Stretch",          false } },
    { "-wide-bg",             { "WideBackground",   true } },
    { "-no-wide-bg",          { "WideBackground",   false } },
#ifndef SUPERMODEL_OSX
    { "-no-multi-texture",    { "MultiTexture",     false } },
    { "-multi-texture",       { "MultiTexture",     true } },
#endif
    { "-throttle",            { "Throttle",         true } },
    { "-no-throttle",         { "Throttle",         false } },
    { "-vsync",               { "VSync",            true } },
    { "-no-vsync",            { "VSync",            false } },
    { "-show-fps",            { "ShowFrameRate",    true } },
    { "-no-fps",              { "ShowFrameRate",    false } },
    { "-new3d",               { "New3DEngine",      true } },
    { "-quad-rendering",      { "QuadRendering",    true } },
#ifndef SUPERMODEL_OSX
    { "-legacy3d",            { "New3DEngine",      false } },
#endif
    { "-vulkan3d",            { "Vulkan3DEngine",   true } },
    { "-vk-validation",       { "VkValidation",     true } },
    { "-no-flip-stereo",      { "FlipStereo",       false } },
    { "-flip-stereo",         { "FlipStereo",       true } },
    { "-sound",               { "EmulateSound",     true } },
    { "-no-sound",            { "EmulateSound",     false } },
    { "-dsb",                 { "EmulateDSB",       true } },
    { "-no-dsb",              { "EmulateDSB",       false } },
    { "-legacy-scsp",         { "LegacySoundDSP",   true } },
    { "-new-scsp",            { "LegacySoundDSP",   false } },
    { "-no-white-flash",      { "NoWhiteFlash",     true } },
    { "-white-flash",         { "NoWhiteFlash",     false } },
    { "-net",                 { "Network",          true } },
    { "-no-net",              { "Network",          false } },
    { "-simulate-netboard",   { "SimulateNet",      true } },
    { "-emulate-netboard",    { "SimulateNet",      false } },
    { "-no-force-feedback",   { "ForceFeedback",    false } },
    { "-force-feedback",      { "ForceFeedback",    true } },
    { "-dump-memory",         { "DumpMemory",       true } },
    { "-dump-textures",       { "DumpTextures",     true } },
  };
  for (int i = 1; i < argc; i++)
  {
    std::string arg(argv[i]);
    if (arg[0] == '-')
    {
      // First, check maps
      size_t idx_equals = arg.find_first_of('=');
      if (idx_equals != std::string::npos)
      {
        std::string option(arg.begin(), arg.begin() + idx_equals);
        std::string value(arg.begin() + idx_equals + 1, arg.end());
        if (value.empty())
        {
          ErrorLog("Argument to '%s' cannot be blank.", option.c_str());
          cmd_line.error = true;
          continue;
        }
        auto it = valued_options.find(option);
        if (it != valued_options.end())
        {
          const std::string &config_key = it->second;
          cmd_line.config.Set(config_key, value);
          continue;
        }
      }
      else
      {
        auto it = bool_options.find(arg);
        if (it != bool_options.end())
        {
          const std::string &config_key = it->second.first;
          bool value = it->second.second;
          cmd_line.config.Set(config_key, value);
          continue;
        }
        else if (valued_options.find(arg) != valued_options.end())
        {
          ErrorLog("'%s' requires an argument.", argv[i]);
          cmd_line.error = true;
          continue;
        }
      }
      // Fell through -- handle special cases
      if (arg == "-?" || arg == "-h" || arg == "-help" || arg == "--help")
        cmd_line.print_help = true;
      else if (arg == "-gui")
        cmd_line.open_gui = true;
      else if (arg == "-print-games")
        cmd_line.print_games = true;
      else if (arg == "-res" || arg.find("-res=") == 0)
      {
        std::vector<std::string> parts = Util::Format(arg).Split('=');
        if (parts.size() != 2)
        {ErrorLog("'-res' requires both a width and height (e.g., '-res=496,384').");
          cmd_line.error = true;
        }
        else
        {
          unsigned  x, y;
          if (2 == sscanf(&argv[i][4],"=%u,%u", &x, &y))
          {
            std::string xres = Util::Format() << x;
            std::string yres = Util::Format() << y;
            cmd_line.config.Set("XResolution", xres);
            cmd_line.config.Set("YResolution", yres);
          }
          else
          {
            ErrorLog("'-res' requires both a width and height (e.g., '-res=496,384').");
            cmd_line.error = true;
          }
        }
      }
      else if (arg == "-window-pos" || arg.find("-window-pos=") == 0)
      {
          std::vector<std::string> parts = Util::Format(arg).Split('=');
          if (parts.size() != 2)
          {
              ErrorLog("'-window-pos' requires both an X and Y position (e.g., '-window-pos=10,0').");
              cmd_line.error = true;
          }
          else
          {
              int xpos, ypos;
              if (2 == sscanf(&argv[i][11], "=%d,%d", &xpos, &ypos))
              {
                  cmd_line.config.Set("WindowXPosition", xpos);
                  cmd_line.config.Set("WindowYPosition", ypos);
              }
              else
              {
                  ErrorLog("'-window-pos' requires both an X and Y position (e.g., '-window-pos=10,0').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-ss" || arg.find("-ss=") == 0) {

          std::vector<std::string> parts = Util::Format(arg).Split('=');

          if (parts.size() != 2)
          {
              ErrorLog("'-ss' requires an integer argument (e.g., '-ss=2').");
              cmd_line.error = true;
          }
          else {

              try {
                  int val = std::stoi(parts[1]);
                  val = std::clamp(val, 1, 8);

                  cmd_line.config.Set("Supersampling", val);
              }
              catch (...) {
                  ErrorLog("'-ss' requires an integer argument (e.g., '-ss=2').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-crtcolors" || arg.find("-crtcolors=") == 0) {

          std::vector<std::string> parts = Util::Format(arg).Split('=');

          if (parts.size() != 2)
          {
              ErrorLog("'-crtcolors' requires an integer argument (e.g., '-crtcolors=1').");
              cmd_line.error = true;
          }
          else {

              try {
                  int val = std::stoi(parts[1]);
                  val = std::clamp(val, 0, 5);

                  cmd_line.config.Set("CRTcolors", val);
              }
              catch (...) {
                  ErrorLog("'-crtcolors' requires an integer argument (e.g., '-crtcolors=1').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-upscalemode" || arg.find("-upscalemode=") == 0) {

          std::vector<std::string> parts = Util::Format(arg).Split('=');

          if (parts.size() != 2)
          {
              ErrorLog("'-upscalemode' requires an integer argument (e.g., '-upscalemode=1').");
              cmd_line.error = true;
          }
          else {

              try {
                  int val = std::stoi(parts[1]);
                  val = std::clamp(val, 0, 3);

                  cmd_line.config.Set("UpscaleMode", val);
              }
              catch (...) {
                  ErrorLog("'-upscalemode' requires an integer argument (e.g., '-upscalemode=1').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-true-hz")
        cmd_line.config.Set("RefreshRate", 57.524f);
      else if (arg == "-print-gl-info")
        cmd_line.print_gl_info = true;
      else if (arg == "-config-inputs")
        cmd_line.config_inputs = true;
      else if (arg == "-print-inputs")
        cmd_line.print_inputs = true;
#ifdef SUPERMODEL_DEBUGGER
      else if (arg == "-disable-debugger")
        cmd_line.disable_debugger = true;
      else if (arg == "-enter-debugger")
        cmd_line.enter_debugger = true;
#endif
#ifdef DEBUG
      else if (arg == "-gfx-state" || arg.find("-gfx-state=") == 0)
      {
        std::vector<std::string> parts = Util::Format(arg).Split('=');
        if (parts.size() != 2)
        {
          ErrorLog("'-gfx-state' requires a file name.");
          cmd_line.error = true;
        }
        else
          cmd_line.gfx_state = parts[1];
      }
#endif
      else
      {
        ErrorLog("Ignoring unrecognized option: %s", argv[i]);
        cmd_line.error = true;
      }
    }
    else
      cmd_line.rom_files.emplace_back(arg);
  }
  return cmd_line;
}

/*
 * main(argc, argv):
 *
 * Program entry point.
 */
/*
 * RunGameSession(cmd_line, logger):
 *
 * Runs one complete emulation session: resolves the configuration for the game named on the
 * command line, creates the window and the emulator, runs frames until the user quits (Esc) or
 * the emulator stops, then tears everything down again. Returning instead of exiting is what
 * allows the settings GUI to start a game, take control back when it ends, and start another one.
 * Returns the process exit code.
 */
static int RunGameSession(const ParsedCommandLine& cmd_line, std::shared_ptr<CLogger> logger)
{
  // Finish processing command line
  if (cmd_line.print_help)
  {
    Help();
    return 0;
  }
  if (cmd_line.print_gl_info)
  {
    // Use command line options so we can select the 3D engine for which to print GL info 
    Util::Config::MergeINISections(&s_runtime_config, DefaultConfig(), cmd_line.config);

    // We must exit after this because CreateGLScreen() is used
    PrintGLInfo(true, false, false);
    return 0;
  }

#ifdef DEBUG
  s_gfxStatePath.assign(cmd_line.gfx_state);
#endif
  bool print_games = cmd_line.print_games;
  bool rom_specified = !cmd_line.rom_files.empty();
  if (!rom_specified && !print_games && !cmd_line.config_inputs && !cmd_line.print_inputs)
  {
    ErrorLog("No ROM file specified.");
    return 0;
  }

  // Load game and resolve run-time config
  Game game;
  ROMSet rom_set;
  
  Util::Config::Node fileConfig("Global");
  {
    Util::Config::Node fileConfigWithDefaults("Global");
    Util::Config::Node config3("Global");
    Util::Config::Node config4("Global");
    Util::Config::FromINIFile(&fileConfig, s_configFilePath);
    Util::Config::MergeINISections(&fileConfigWithDefaults, DefaultConfig(), fileConfig); // apply .ini file's global section over defaults
    Util::Config::MergeINISections(&config3, fileConfigWithDefaults, cmd_line.config);    // apply command line overrides
    if (rom_specified || print_games)
    {
      std::string xml_file = ResolveDataFileSetting(config3["GameXMLFile"].ValueAs<std::string>(), "Games.xml");
      GameLoader loader(xml_file);
      if (print_games)
      {
        PrintGameList(xml_file, loader.GetGames());
        return 0;
      }
      if (loader.Load(&game, &rom_set, *cmd_line.rom_files.begin()))
        return 1;
      Util::Config::MergeINISections(&config4, config3, fileConfig[game.name]);   // apply game-specific config
    }
    else
      config4 = config3;
    Util::Config::MergeINISections(&s_runtime_config, config4, cmd_line.config);  // apply command line overrides once more
  }

  // A control preset named on the command line overrides the individual input settings.
  ApplyPresetFromCommandLine(s_runtime_config, cmd_line.config["ControlPresetCLI"].ValueAsDefault<std::string>(""));

  LogConfig(s_runtime_config);

  // Initialize SDL (individual subsystems get initialized later)
  if (SDL_Init(0) != 0)
  {
    ErrorLog("Unable to initialize SDL: %s\n", SDL_GetError());
    return 1;
  }

  // Flag as DPI-aware, otherwise the window content might be scaled by some graphics drivers
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "system");

  // Begin initializing various subsystems...
  int exitCode = 0;
  IEmulator *Model3 = nullptr;
  std::shared_ptr<CInputSystem> InputSystem;
  CInputs *Inputs = nullptr;
  COutputs *Outputs = nullptr;
#ifdef SUPERMODEL_DEBUGGER
  std::shared_ptr<Debugger::CSupermodelDebugger> Debugger;
#endif // SUPERMODEL_DEBUGGER
  std::string selectedInputSystem = s_runtime_config["InputSystem"].ValueAs<std::string>();

  aaValue = s_runtime_config["Supersampling"].ValueAs<int>();
  CRTcolors = (CRTcolor)s_runtime_config["CRTcolors"].ValueAs<int>();
  if (s_runtime_config["Vulkan3DEngine"].ValueAsDefault<bool>(false) && aaValue != 1)
  {
    InfoLog("Vulkan: supersampling is not available in this backend; using 1x.");
    aaValue = 1;
  }

  // Create a window
  xRes = s_runtime_config["XResolution"].ValueAsDefault<unsigned>(496);
  yRes = s_runtime_config["YResolution"].ValueAsDefault<unsigned>(384);

  const bool vulkanRequested = s_runtime_config["Vulkan3DEngine"].ValueAsDefault<bool>(false);
  Result screen = vulkanRequested
    ? CreateVulkanScreen(SUPERMODEL_TITLE_FULL, false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false)
    : CreateGLScreen(s_runtime_config["New3DEngine"].ValueAs<bool>(), SUPERMODEL_TITLE_FULL, false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false);

  /*
   * Vulkan was selected and could not start - the GPU or its driver does not meet what the backend
   * needs. Falling back to OpenGL rather than ending the session, because the alternative is a user
   * who picked a game in the settings window and got a window that closed on them, with the reason
   * buried in a log they have no reason to open.
   *
   * The change is deliberately confined to this session. s_runtime_config is rebuilt from the .ini at
   * the start of every session, so nothing is written back and the user's choice stands: once their
   * driver is updated, Vulkan comes back on its own rather than being silently stuck on OpenGL.
   *
   * Both halves of the message are needed. The log records what happened for anyone debugging it, and
   * the notice reaches the settings window, which is the only place the user actually looks - and it
   * sits next to the renderer selection that would change it back.
   */
  if (Result::OKAY != screen && vulkanRequested)
  {
    ErrorLog("Vulkan: the backend could not start on this machine. Falling back to OpenGL for this "
      "session; the saved renderer setting is left unchanged, so Vulkan will be tried again next time.");
    GuiPostNotice("Vulkan 无法在此机器上启动（显卡或驱动不支持），本局已自动改用 OpenGL 渲染器。若想重试 Vulkan，请更新显卡驱动后在这里重新选择。");

    // Kept in step with what actually got created, so the configuration does not claim a backend that
    // is not running. Nothing later in this session currently consults the key - the switch to the
    // OpenGL paths is driven by s_vkContext being null - but leaving it saying "Vulkan" would be a
    // trap for the next person to read it.
    s_runtime_config.Set("Vulkan3DEngine", false);

    // Supersampling was forced to 1x above on the assumption Vulkan would be used. OpenGL supports it,
    // so restore whatever the user asked for rather than silently downgrading the picture.
    aaValue = s_runtime_config["Supersampling"].ValueAs<int>();
    CRTcolors = (CRTcolor)s_runtime_config["CRTcolors"].ValueAs<int>();

    /*
     * Undo the half-built Vulkan attempt before retrying.
     *
     * CreateVulkanScreen leaves the SDL window and the context object in place when the device is
     * rejected - which is the common failure - and CreateGLScreen refuses to run while a window
     * exists ("called more than once"). Without this the fallback would fail for exactly the reason
     * it is meant to handle, and the user would be back to a session that closes on them.
     *
     * DestroyGLScreen already does the right thing for a context that never finished initialising:
     * its shutdown path skips every step whose Vulkan handle is null, so nothing is destroyed twice
     * and no null handle reaches the driver.
     */
    DestroyGLScreen();

    InfoLog("Vulkan: retrying with the OpenGL renderer (%s).",
      s_runtime_config["New3DEngine"].ValueAs<bool>() ? "New 3D" : "Legacy 3D");

    screen = CreateGLScreen(s_runtime_config["New3DEngine"].ValueAs<bool>(), SUPERMODEL_TITLE_FULL, false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false);
  }

  if (Result::OKAY != screen)
  {
    exitCode = 1;
    goto Exit;
  }

  // Create Crosshair (OpenGL only; the Vulkan backend does not draw it yet)
  if (!s_vkContext)
  {
    s_crosshair = new CCrosshair(s_runtime_config);
    if (s_crosshair->Init() != Result::OKAY)
    {
        ErrorLog("Unable to load bitmap crosshair texture\n");
        exitCode = 1;
        goto Exit;
    }
  }

  // Create Model 3 emulator
#ifdef DEBUG
  Model3 = s_gfxStatePath.empty() ? static_cast<IEmulator *>(new CModel3(s_runtime_config)) : static_cast<IEmulator *>(new CModel3GraphicsState(s_runtime_config, s_gfxStatePath));
#else
  Model3 = new CModel3(s_runtime_config);
#endif

  // Create input system
  if (selectedInputSystem == "sdl")
    InputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(s_runtime_config, false));
  else if (selectedInputSystem == "sdlgamepad")
    InputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(s_runtime_config, true));
#ifdef SUPERMODEL_WIN32
  else if (selectedInputSystem == "dinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_window, false, false));
  else if (selectedInputSystem == "xinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_window, false, true));
  else if (selectedInputSystem == "rawinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_window, true, false));
#endif // SUPERMODEL_WIN32
  else
  {
    ErrorLog("Unknown input system: %s\n", selectedInputSystem.c_str());
    exitCode = 1;
    goto Exit;
  }

  // Create inputs from input system (configuring them if required)
  Inputs = new CInputs(InputSystem);
  if (!Inputs->Initialize())
  {
    ErrorLog("Unable to initalize inputs.\n");
    exitCode = 1;
    goto Exit;
  }

  // NOTE: fileConfig is passed so that the global section is used for input settings
  // and because this function may write out a new config file, which must preserve
  // all sections. We don't want to pollute the output with built-in defaults.
  if (ConfigureInputs(Inputs, &fileConfig, &s_runtime_config, game, cmd_line.config_inputs) != Result::OKAY)
  {
    exitCode = 1;
    goto Exit;
  }

  if (cmd_line.print_inputs)
  {
    Inputs->PrintInputs(NULL);
    InputSystem->PrintSettings();
  }

  if (!rom_specified)
    goto Exit;

  // Create outputs
  {
    std::string outputs = s_runtime_config["Outputs"].ValueAs<std::string>();
    if (outputs == "none")
      Outputs = NULL;
    else if (outputs == "net") 
    {
        CNetOutputs* netOutputs = new CNetOutputs();
        if (s_runtime_config["OutputsWithLF"].ValueAs<bool>())
            netOutputs->SetFrameEnding(std::string("\r\n"));
        netOutputs->SetTcpPort(s_runtime_config["OutputsTCPPort"].ValueAs<unsigned int>());
        netOutputs->SetUdpBroadcastPort(s_runtime_config["OutputsUDPBroadcastPort"].ValueAs<unsigned int>());
        Outputs = (COutputs*)netOutputs;
#ifdef SUPERMODEL_WIN32
    } else if (outputs == "win") {
      Outputs = new CWinOutputs();
#endif // SUPERMODEL_WIN32
    }
    else
    {
      ErrorLog("Unknown outputs: %s\n", outputs.c_str());
      exitCode = 1;
      goto Exit;
    }
  }

  // Initialize outputs
  if (Outputs != NULL && !Outputs->Initialize())
  {
    ErrorLog("Unable to initialize outputs.\n");
    exitCode = 1;
    goto Exit;
  }

#ifdef SUPERMODEL_DEBUGGER
  // Create Supermodel debugger unless debugging is disabled
  if (!cmd_line.disable_debugger)
  {
    Debugger = std::make_shared<Debugger::CSupermodelDebugger>(dynamic_cast<CModel3 *>(Model3), Inputs, logger);
    // If -enter-debugger option was set force debugger to break straightaway
    if (cmd_line.enter_debugger)
      Debugger->ForceBreak(true);
  }
  // Fire up Supermodel with debugger
  exitCode = Supermodel(game, &rom_set, Model3, Inputs, Outputs, Debugger);
#else
  // Fire up Supermodel
  exitCode = Supermodel(game, &rom_set, Model3, Inputs, Outputs);
#endif // SUPERMODEL_DEBUGGER
  delete Model3;

Exit:
  // Give the pointer back to the settings window.
  //
  // A fullscreen game hides it through the Win32 ShowCursor() display count, and the settings window
  // cannot undo that on its own: it can only move SDL's own visibility flag, which has no influence
  // over the count. So the pointer stayed invisible there even though the mouse still hovered and
  // clicked - those come from window messages, which do not depend on the pointer being drawn.
  //
  // Ungrab rather than just asking for visibility, because the raw-input backend ties the two
  // together: while the mouse is grabbed it keeps the pointer hidden whatever visibility is
  // requested, so releasing the grab is what actually brings it back on that backend. On the others
  // this is equivalent. Done here rather than in the settings window because that window has no
  // reliable handle on the input system, and because the session is over either way - the input
  // system is destroyed on the next line.
  if (Inputs != nullptr)
    Inputs->GetInputSystem()->UngrabMouse();

  /*
   * Detach the two static pointers that outlive this function.
   *
   * videoInputs is read by EndFrameVideo() every frame, and the input collection it points at is
   * deleted below. It was only ever set for light-gun games, so anything else left it null and this
   * went unnoticed - but a gun game left a dangling pointer behind that the next frame (or the next
   * session) would dereference.
   *
   * Cleared here rather than after the delete, so the window in which the pointer is stale is as
   * short as it can be.
   */
  videoInputs = nullptr;
  currentInputs = 0;

  /*
   * One log line per step from here to the return.
   *
   * The session teardown used to end with a single "returning to the settings window" line, which
   * made a crash in any of these destructors indistinguishable from a crash on the way back to the
   * GUI: the last line in the log was the same either way. These four are the only things left on
   * this path, and each of them owns something that has to be torn down in a particular order, so
   * naming them is what turns "it crashes after Esc" into a specific line to look at.
   */
  delete Inputs;
  InfoLog("Session: input system released.");
  delete Outputs;
  InfoLog("Session: outputs released.");
  delete s_crosshair;
  s_crosshair = nullptr;
  InfoLog("Session: crosshair released.");

  // Note: SDL_Quit() is deliberately not called here. The settings GUI keeps its own window and
  // GL context alive across sessions, so SDL is shut down once in main() instead.
  DestroyGLScreen();
  InfoLog("Session: game window and GL context released.");

  return exitCode;
}

/*
 * main(argc, argv):
 *
 * Program entry point. When no game is named on the command line Supermodel opens its settings
 * GUI, which then acts as the application shell: it stays alive for the whole run so that
 * quitting a game with Esc returns to it, as other emulator front-ends do.
 */
int main(int argc, char **argv)
{
  // Before anything can print. Attaches to the console this was launched from, if any, so that
  // command-line output such as -print-games appears where the user is looking. Does nothing when
  // started from Explorer, which is how the flash-free default is kept.
  AttachParentConsole();

  Title();
  WriteDefaultConfigurationFileIfNotPresent();

  bool loadGUI = false;
  if (argc <= 1)
  {
    //Help();
    loadGUI = true;
  }

  // Before command line is parsed, console logging only
  SetLogger(std::make_shared<CConsoleErrorLogger>());

  // Load config and parse command line
  auto cmd_line = ParseCommandLine(argc, argv);
  if (cmd_line.error)
  {
    return 1;
  }

  if (cmd_line.open_gui)
    loadGUI = true;

  // Create logger as specified by command line. One logger serves the whole run so that the log
  // file is not restarted every time a game is launched from the GUI.
  auto logger = CreateLogger(cmd_line.config);
  if (!logger)
  {
    ErrorLog("Unable to initialize logging system.");
    return 1;
  }
  SetLogger(logger);
  InfoLog("Supermodel Version " SUPERMODEL_VERSION);
  InfoLog("Started as:");
  for (int i = 0; i < argc; i++)
    InfoLog("  argv[%d] = %s", i, argv[i]);

  // Load up what settings we have so far. The complete file is passed to the GUI, which edits
  // only the global section and preserves any game-specific sections. Editing the game-specific
  // sections themselves is not supported atm but we can look at this later.
  Util::Config::Node fileConfig("Global");
  Util::Config::FromINIFile(&fileConfig, s_configFilePath);

  // Settings that apply to the whole run rather than to a session, so they are handled once here.
  // The console window is only relevant in GUI mode; priority matters in both.
  Util::Config::Node startupDefaults("Global");
  Util::Config::Node startupConfig("Global");
  Util::Config::MergeINISections(&startupDefaults, DefaultConfig(), fileConfig);
  Util::Config::MergeINISections(&startupConfig, startupDefaults, cmd_line.config);

  // Keep the frame loop fed while the window is in the background; see RaiseProcessPriority.
  if (startupConfig["HighPriority"].ValueAsDefault<bool>(true))
    RaiseProcessPriority();

  if (loadGUI)
  {
    // Supermodel is built as a console application, so Windows creates a console window for it.
    // Launched from Explorer that window is just noise beside the settings GUI, so hide it by
    // default. Logging still goes to the log file; ShowConsole=on (or -show-console) brings the
    // window back for anyone who wants to watch it.
    ShowConsoleWindow(startupConfig["ShowConsole"].ValueAsDefault<bool>(false));
  }

  // The settings GUI is the application shell: it is created once and stays alive for the whole
  // run, so quitting a game with Esc returns to it (as with PCSX2 and other emulators) instead of
  // exiting Supermodel. With a game named on the command line the GUI is skipped and a single
  // session is run, exactly as before.
  GuiState* gui = nullptr;
  if (loadGUI)
  {
    // Apply the saved interface language before the window is created, so the very first frame is
    // already drawn in the right language rather than switching a frame later.
    const UiLanguage language = startupConfig["UiLanguage"].ValueAsDefault<int>(0) == 1
      ? UiLanguage::Chinese : UiLanguage::English;
    SetUiLanguage(language);
    InfoLog("GUI: interface language = %s (UiLanguage = %d).",
      UiLanguageName(language), startupConfig["UiLanguage"].ValueAsDefault<int>(0));

    gui = GuiCreate(s_configFilePath, fileConfig);
    if (gui == nullptr)
      return 1;
  }

  int exitCode = 0;
  for (;;)
  {
    if (loadGUI)
    {
      cmd_line.rom_files = GuiRunOnce(gui);

      if (cmd_line.rom_files.empty())
        break;      // the GUI window was closed: quit Supermodel

      // Settings may have been changed in the GUI; reload so the game picks them up.
      Util::Config::FromINIFile(&fileConfig, s_configFilePath);
    }

    exitCode = RunGameSession(cmd_line, logger);
    InfoLog("Session: returned with code %d; back to the settings window.", exitCode);

    if (!loadGUI)
      break;        // command line mode runs a single session and then exits
  }

  if (gui != nullptr)
    GuiDestroy(gui);

  SDL_Quit();

  if (exitCode)
    InfoLog("Program terminated due to an error.");
  else
    InfoLog("Program terminated normally.");

  return exitCode;
}

