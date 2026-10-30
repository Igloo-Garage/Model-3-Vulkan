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
#include <cstdlib>
#include <climits>
#include <chrono>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cctype>
#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>
#if defined(SUPERMODEL_PLATFORM_SDL3)
// C++23 startup layer: std::format for the messages and std::println for the
// pre-window diagnostics.  The SDL2 target remains C++17 and never sees these.
#include <exception>
#include <format>
#include <print>
#endif
#if defined(SUPERMODEL_PURE_VULKAN)
#include <fstream>
#endif
#if !defined(SUPERMODEL_PURE_VULKAN)
#include <GL/glew.h>
#endif

#if defined(SUPERMODEL_WIN32) && defined(SUPERMODEL_PLATFORM_SDL2)
// DirectInput/Raw Input is one of the SDL2 binary's reasons to exist; the SDL3 binary has no such
// backend, so it must not even see the header.
#include "DirectInputSystem.h"
#endif
#ifdef SUPERMODEL_WIN32
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
#include "OSD/Platform/Platform.h"
#include "OSD/Platform/PlatformInput.h"
#if defined(SUPERMODEL_PLATFORM_SDL3)
#include "OSD/SDL3/AppRuntime.h"
#include "OSD/SDL3/StartupLog.h"
#include "OSD/SDL3/VulkanRequirement.h"
#if defined(_WIN32)
// SDL_SetWindowsMessageHook lives in SDL_system.h, which the SDL3 build does not otherwise pull
// in (only the SDL2 build includes the umbrella header). Used by the modal-loop pause below.
#include <SDL3/SDL_system.h>
#endif
#endif
#if defined(SUPERMODEL_PURE_VULKAN)
#include "OSD/SDL3/HostCoreBridge.h"
#endif
#if defined(SUPERMODEL_PLATFORM_SDL2)
// SDL2 only: the Win32 message hook below, and the DirectInput window adapter.
// Neither has an SDL3 equivalent that shared code needs.
#include "SDLIncludes.h"
#include "OSD/Platform/SDL2/PlatformSDL2Internal.h"
#endif
#include "Debugger/SupermodelDebugger.h"
#if defined(SUPERMODEL_PURE_VULKAN)
// Only the interfaces: the concrete OpenGL renderers are not part of this
// executable and their headers include <GL/glew.h>.
#include "Graphics/IRender2D.h"
#include "Graphics/IRender3D.h"
#else
#ifndef SUPERMODEL_OSX
#include "Graphics/Legacy3D/Legacy3D.h"
#endif
#include "Graphics/New3D/New3D.h"
#include "Graphics/Render2D.h"
#include "Graphics/New3D/VBO.h"
#include "Graphics/SuperAA.h"
#endif
#if defined(SUPERMODEL_PURE_VULKAN) && defined(SUPERMODEL_OPENGL_ONLY)
#error "A target cannot be both Pure-Vulkan and OpenGL-only."
#endif

#if !defined(SUPERMODEL_OPENGL_ONLY)
#include "Graphics/Vulkan3D/VulkanContext.h"
#include "Graphics/Vulkan3D/Vulkan2D.h"
#include "Graphics/Vulkan3D/Vulkan3D.h"
#include "Graphics/Vulkan3D/HDTextures.h"
#endif
#include "Model3/IEmulator.h"
#include "Model3/Model3.h"
#include "Model3/Model3Timing.h"
#include "OSD/Audio.h"
#include "OSD/DisplayPacer.h"
#include "Sound/MPEG/MpegAudio.h"

#include <iostream>
#include "Util/BMPFile.h"

#if !defined(SUPERMODEL_PURE_VULKAN)
#include "Crosshair.h"
#endif
#include "OSD/ConfigFile.h"
#include "Gui.h"
#include "Overlay.h"
#include "StatsOsd.h"
#include "Localization.h"
#include "Main.h"

#ifdef _WIN32
#include <windows.h>    // AttachConsole()/AllocConsole()/GetConsoleWindow() - see ShowConsoleWindow()
#include "OSD/Windows/SplashScreen.h"
// WMI, used to enumerate the attached displays so their EDID timing ranges can be read. The project
// already links wbemuuid.lib because DirectInputSystem uses the same API to spot XInput devices.
#include <wbemidl.h>
#include <oleauto.h>

// Force discrete GPU on hybrid / multi-GPU systems (NVIDIA Optimus & AMD PowerXpress)
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// Matches IDI_MODEL3VULKAN in both resource scripts (MSVC/Model3VulkanResources.rc and
// Src/OSD/Windows/Model3VulkanResources.rc). They all have to agree or the window icon silently goes
// missing while the one in Explorer still looks right.
#define IDI_MODEL3VULKAN 101
#endif


/******************************************************************************
 Generic
******************************************************************************/

void SetWindowIcon(OSD::Platform::Window* window)
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
    HICON iconBig = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_MODEL3VULKAN), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    HICON iconSmall = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_MODEL3VULKAN), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    // Without the HWND there is nothing to set the icon on; the executable icon still applies.
    HWND hwnd = static_cast<HWND>(OSD::Platform::GetNativeWindowHandle(window));
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
#if defined(SUPERMODEL_PLATFORM_SDL3)
// The SDL3 build binds its settings to its own executable name, so the file is
// derived at startup (Model3-Vulkan.ini) instead of being hard-coded.
// InitializeSDL3Startup() fills this before anything can read it.
static std::string s_configFilePath;
#else
static const std::string s_configFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Supermodel.ini";
#endif
static const std::string s_gameXMLFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Games.xml";
static const std::string s_musicXMLFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Music.xml";

// Written into the configuration file as a path relative to the installation, so that moving the
// folder to another drive does not leave the setting pointing at where it used to live. The logger
// anchors it to the executable's directory when it opens the file (see FileSystemPath::ResolvePath).
// Only the shown default in the command-line help uses the resolved form.
//
// macOS has no Data/ directory: the package is a flat folder, so the log belongs next to the
// executable instead. Without this the default resolved to <exe>/Data/Supermodel.log, a directory
// that does not exist, and the log was silently not written.
#ifdef SUPERMODEL_OSX
static const std::string s_logOutputSetting = "Supermodel.log";
#else
static const std::string s_logOutputSetting = "Data/Supermodel.log";
#endif
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

static OSD::Platform::Window *s_platformWindow = nullptr;
#if !defined(SUPERMODEL_OPENGL_ONLY)
static Vulkan3D::CVulkanContext *s_vkContext = nullptr;

/*
 * The 2D renderer of the running session, for the overlay's frosted backdrop.
 *
 * The blur chain lives in CVulkan2D because that is the class holding the finished frame; the
 * overlay only samples the result. Kept as a file-level pointer rather than captured because the
 * hand-over is two plain function pointers, matching the existing SetOverlayPass() arrangement.
 */
static Vulkan3D::CVulkan2D *s_overlayFrostRenderer = nullptr;

static OverlayFrostTexture OverlayFrostTextureFromRenderer()
{
  OverlayFrostTexture texture;
  if (s_overlayFrostRenderer != nullptr)
  {
    texture.view = s_overlayFrostRenderer->UiFrostView();
    texture.generation = s_overlayFrostRenderer->UiFrostGeneration();
  }
  return texture;
}
#endif

/*
 * The game window is created hidden and revealed once a frame has reached the display.
 *
 * A window created visible is on screen while the device, the shaders and the ROM are being set up,
 * which is an empty black rectangle for a second or two - the same gap the settings window covers
 * with its start-up artwork. There is no artwork here, so the window is simply kept invisible until
 * there is a picture to put in it. Set by CreateGLScreen()/CreateVulkanScreen(), cleared at the
 * first present in EndFrameVideo().
 *
 * The reveal has to be in EndFrameVideo() rather than in the set-up code: that is the one place
 * both renderers pass through (the Vulkan backend presents in CVulkanContext::EndFrame, the OpenGL
 * one in SwapWindow), and it is the first moment the contents of the window are worth showing.
 */
static bool s_windowRevealPending = false;
// Whether the game window was asked to take the foreground. The raise moves with the reveal: raising
// a hidden window can put an empty one on screen, which is what this is here to avoid.
static bool s_windowRevealRaise = false;

#if !defined(SUPERMODEL_OPENGL_ONLY)
/*
 * The Vulkan 3D renderer, kept in its concrete type as well as the interface.
 *
 * It is created here and handed to the emulator as an IRender3D, which is all the emulator needs. The
 * statistics readout needs one thing the interface does not carry - the MSAA level actually in force,
 * which the backend may have reduced from the requested one - and this is simpler than widening
 * IRender3D with a method that only one of its three implementations has anything to say about.
 */
static Vulkan3D::CVulkan3D *s_vk3dRenderer = nullptr;

/*
 * The Vulkan 2D renderer, kept for the same reason as the 3D one: the light-gun crosshair is drawn
 * by it rather than by the ImGui overlay, and the game loop needs its concrete type to hand it the
 * current position each frame. Null on the OpenGL backends, where CCrosshair draws the crosshair.
 */
static Vulkan3D::CVulkan2D *s_vk2dRenderer = nullptr;
#endif
// GL context of the game window. Kept separate from the settings GUI's own context, which stays
// alive across game sessions (see DestroyGLScreen()).
#if !defined(SUPERMODEL_PURE_VULKAN)
static OSD::Platform::GLContext *s_glContext = nullptr;
#endif

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
// Mode reserved for the exclusive paths by ResolveFullScreenMode() and applied by
// ApplyFullScreenMode(). Kept separate from the window because the window does not exist yet when
// the flavour is resolved, and because applying the mode and entering fullscreen is one call in the
// platform layer.
static OSD::Platform::DisplayMode s_exclusiveMode{};
static bool s_haveExclusiveMode = false;

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
#if !defined(SUPERMODEL_PURE_VULKAN)
static CCrosshair* s_crosshair = nullptr;
#endif

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
// The hook itself is Windows-only, and so is the SDL2 path that needs it: SDL2's own message pump is
// bypassed when DirectInput pumps Win32 messages itself. UINT_PTR, HWND, the WM_* messages and
// SetTimer()/KillTimer() are Win32 types and constants that do not exist under SDL's X11, Wayland or
// Cocoa back ends, so the declarations have to sit inside the guard as well as the code that uses
// them. Guarding only the body of SetModalLoopHook() - as an earlier revision did - left the
// declarations at file scope and made a GCC/Clang build fail with "'UINT_PTR' does not name a type"
// before it ever reached the function. Everything Windows-specific is now inside one block, and
// other targets compile a stub.
//
// The SDL3 target pauses instead, which is what dragging a window should do anyway: see
// ModalLoopPauseHook below for why running frames is not an option there.
#if defined(_WIN32) && defined(SUPERMODEL_PLATFORM_SDL2)
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

#if defined(_WIN32) && defined(SUPERMODEL_PLATFORM_SDL3)
static IEmulator* s_moveLoopEmulator = nullptr;
static bool* s_moveLoopPaused = nullptr;
// Set only while this hook is the reason the game is paused, so a pause the player asked for is
// never undone by the end of a drag.
static bool s_moveLoopSuspended = false;

/*
 * Pausing while the window is dragged or resized.
 *
 * Windows runs its own modal loop for a title-bar drag, entered from inside DispatchMessage. The
 * frame loop is parked there for as long as the drag lasts, so no frames are emulated - but the
 * sound board thread is woken by the audio device, keeps producing in real time, and the ring buffer
 * under-runs over and over. Under-runs replay from where writing stopped, so a drag turns the audio
 * into a short loop.
 *
 * Emulating frames from the hook (the SDL2 route above) is not an option here: this build's frame
 * loop pumps SDL's event queue, and re-entering SDL's pump from inside its own modal loop is exactly
 * the situation SDL_processing_messages exists to prevent. Pausing is both safer and what the player
 * expects from grabbing the title bar.
 *
 * Entering the loop happens before any frame is in progress - the main thread is in the event pump -
 * so stopping the board threads here is the same operation Alt+P performs, just triggered by the OS.
 */
static void OnWindowModalLoop(bool entered)
{
  if (s_moveLoopEmulator == nullptr || s_moveLoopPaused == nullptr)
    return;

  if (entered)
  {
    // Already paused by Alt+P or by the overlay: nothing to stop, and resuming later must not
    // override the player's decision.
    if (s_moveLoopSuspended || *s_moveLoopPaused)
      return;

    s_moveLoopSuspended = true;
    *s_moveLoopPaused = true;
    s_moveLoopEmulator->PauseThreads();
    SetAudioEnabled(false);
    // The frame loop is parked in the modal loop, so nothing else can report this; without the line a
    // stalled sound buffer during a drag looks like an audio bug rather than the intended pause.
    InfoLog("Window: drag/resize started; pausing the emulator until it ends.");
  }
  else if (s_moveLoopSuspended)
  {
    s_moveLoopSuspended = false;
    *s_moveLoopPaused = false;
    // The device queued whatever was left before the pause; dropping it keeps the resume in step
    // with the picture instead of replaying the tail of the last buffer.
    FlushAudioBuffer();
    s_moveLoopEmulator->ResumeThreads();
    SetAudioEnabled(true);
    InfoLog("Window: drag/resize finished; resuming.");
  }
}

static bool SDLCALL ModalLoopPauseHook(void* /*userdata*/, MSG* msg)
{
  switch (msg->message)
  {
  case WM_ENTERSIZEMOVE:
  case WM_ENTERMENULOOP:
    OnWindowModalLoop(true);
    break;

  case WM_EXITSIZEMOVE:
  case WM_EXITMENULOOP:
    OnWindowModalLoop(false);
    break;

  default:
    break;
  }

  // SDL3 reads true as "not handled, carry on": anything else swallows the message, and returning
  // false for WM_ENTERSIZEMOVE stops SDL from starting its own live-resize timer.
  return true;
}
#endif  // _WIN32

// No-op away from Windows: there is no modal loop to keep alive there, because the frame loop is not
// parked inside a Win32 DispatchMessage() call. Kept unconditional so the call sites stay free of
// #ifdef noise.
static void SetModalLoopHook(IEmulator* emulator, bool* pausedFlag)
{
#if defined(_WIN32) && defined(SUPERMODEL_PLATFORM_SDL2)
  (void)pausedFlag;
  s_modalEmulator = emulator;
  SDL_SetWindowsMessageHook(emulator != nullptr ? ModalLoopMessageHook : nullptr, nullptr);
#elif defined(_WIN32) && defined(SUPERMODEL_PLATFORM_SDL3)
  s_moveLoopEmulator = emulator;
  s_moveLoopPaused = emulator != nullptr ? pausedFlag : nullptr;
  if (emulator == nullptr)
    s_moveLoopSuspended = false;
  SDL_SetWindowsMessageHook(emulator != nullptr ? ModalLoopPauseHook : nullptr, nullptr);
#else
  (void)emulator;
  (void)pausedFlag;
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

/*
 * Whether the 3D scene is rasterised at the Model 3's native 496x384 instead of the visible picture's
 * resolution.
 *
 * Vulkan only, and read only to decide how the picture rectangle is computed and what the settings
 * window reports. The key does not exist in the OpenGL-only build's defaults, and reading it there
 * would create an empty node that the ini writer would then persist into the shared file.
 *
 * It takes precedence over IntegerScale: both decide how the picture is scaled, and running them at
 * once means one of the two silently does nothing. The settings window greys IntegerScale out while
 * this is on, so the precedence is visible where it matters.
 */
static bool LowResolutionRenderingEnabled()
{
#if defined(SUPERMODEL_OPENGL_ONLY)
  return false;
#else
  return s_runtime_config["InternalResolution"].ValueAsDefault<int>(0) != 0;
#endif
}

static void CalcScreenGeometry(unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio)
{
  int actualWidth = 0;
  int actualHeight = 0;
  OSD::Platform::GetWindowSize(s_platformWindow, &actualWidth, &actualHeight);
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
      /*
       * Low-resolution rendering leaves the ordinary 4:3 fit alone.
       *
       * The picture stays the same size as it is without the option - what shrinks is the resolution
       * the 3D scene is rasterised at, which is upscaled onto this rectangle (see CVulkan2D). An
       * integer multiple of the rectangle is neither needed nor useful there: the picture would get
       * smaller while the 3D cost stayed the same, which is the opposite of what the option is for.
       */
      if (!LowResolutionRenderingEnabled() && s_runtime_config["IntegerScale"].ValueAsDefault<bool>(false))
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

        /*
         * The window is the surface actually being rendered into, and it can be smaller than the
         * panel's integer area: a 1920x1080 window on a 4K panel picks multiple 5 from the panel
         * alone, which is 2480x1920. The clamps at the end of this function then shrank width and
         * height independently - 1920x1080 - which turned the 4:3 picture into 16:9 instead of
         * leaving bars. Taking the smaller of the panel's and the window's multiple keeps the area
         * inside the window with its aspect intact.
         *
         * It cannot regress the mixed-scaling case the note above is about: there the panel's
         * reported size is *smaller* than the window's, so the panel's multiple is already the
         * smaller one and nothing changes.
         */
        float windowFit = float(actualWidth) / 496.0f;
        const float windowFitY = float(actualHeight) / 384.0f;
        if (windowFitY < windowFit)
          windowFit = windowFitY;
        if (windowFit < nativeFit)
          nativeFit = windowFit;
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

#if !defined(SUPERMODEL_PURE_VULKAN)
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
    OSD::Platform::SwapWindow(s_platformWindow);
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
#endif

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
// Either route also sets RefreshRate to the arcade rate, which is what the original OpenGL loop
// paces from. The Vulkan loop paces from RefreshRateMode instead, so there the presentation
// cadence is whatever the user chose and this route only decides how the display is driven.
// Scripts/ArcadeDisplay.ps1 enumerates the available modes, can save/restore the desktop mode
// and can launch the emulator with these options already set.
//
// Looks for a display mode at (roughly) the given refresh rate, preferring the exact render
// resolution, then the desktop resolution, then anything on the output. SDL reports integer
// refresh rates, so a custom 57.524 Hz timing shows up as 57 or 58; both are accepted.
//
// displayIndex is a platform display index, not the \\.\DISPLAYn number - see GetDisplays() for the
// mapping between the two. Every mode query in here is per-display, so which monitor is being asked
// matters: a custom 57.524 Hz timing is a property of one output's mode list, and asking the wrong
// one reports the mode as missing when it exists.
//
// The bit-depth test the SDL2 version had (SDL_BITSPERPIXEL) is gone: Platform::DisplayMode carries
// geometry and rate only, and both back ends already list the fullscreen-capable formats, which are
// all 24 or 32 bpp in practice.
static bool FindArcadeDisplayModeImpl(float refreshRateHz, int displayIndex, OSD::Platform::DisplayMode& best, bool quiet)
{
    const OSD::Platform::DisplayId display = OSD::Platform::GetDisplayIdForIndex(displayIndex);

    const int display_mode_count = OSD::Platform::GetDisplayModeCount(display);
    if (display_mode_count < 1) {
        if (!quiet)
            InfoLog("Exclusive fullscreen: no display modes reported.");
        return false;
    }

    const int wantedMilliHz = (int)(refreshRateHz * 1000.0f + 0.5f);

    auto isArcadeRate = [&](const OSD::Platform::DisplayMode& mode, int& delta) {
        const int rate = (int)(mode.refreshHz + 0.5f);
        if (rate != 57 && rate != 58)
            return false;
        delta = std::abs(rate * 1000 - wantedMilliHz);
        return true;
    };

    OSD::Platform::DisplayMode desktop{};
    const bool haveDesktop = OSD::Platform::GetDesktopDisplayMode(display, &desktop);

    bool haveBest = false;
    int bestDelta = 1 << 30;
    std::string availableRates;

    // Three passes, most specific first. Requiring the exact render resolution would mean creating
    // a custom timing for every resolution the user might pick, so the desktop resolution is also
    // accepted (the game renders at its own resolution and is scaled into that mode).
    for (int pass = 0; pass < 3 && !haveBest; pass++) {
        for (int i = 0; i < display_mode_count; ++i) {

            OSD::Platform::DisplayMode mode{};

            if (!OSD::Platform::GetDisplayMode(display, i, &mode))
                continue;

            // Collect the rates offered at the render resolution, for the failure message.
            if (pass == 0 && (unsigned)mode.width == totalXRes && (unsigned)mode.height == totalYRes) {
                if (!availableRates.empty())
                    availableRates.append(", ");
                availableRates.append(std::to_string((int)(mode.refreshHz + 0.5f)));
            }

            int delta = 0;
            if (!isArcadeRate(mode, delta))
                continue;

            if (pass == 0) {
                if ((unsigned)mode.width != totalXRes || (unsigned)mode.height != totalYRes)
                    continue;
            }
            else if (pass == 1) {
                if (!haveDesktop || mode.width != desktop.width || mode.height != desktop.height)
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

static bool FindArcadeDisplayMode(float refreshRateHz, int displayIndex, OSD::Platform::DisplayMode& best)
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
    OSD::Platform::DisplayMode mode{};
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
    const OSD::Platform::Rect* rect;
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
        query->rect->width != (r.right - r.left) || query->rect->height != (r.bottom - r.top))
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

static DisplayNumberQuery WindowsDisplayInfoForRect(const OSD::Platform::Rect& rect)
{
    DisplayNumberQuery query{ &rect, 0, false, std::string(), 0, 0 };
    EnumDisplayMonitors(nullptr, nullptr, FindDisplayNumberProc, reinterpret_cast<LPARAM>(&query));
    return query;
}

#endif // _WIN32

const std::vector<DisplayInfo>& GetDisplays()
{
    static std::vector<DisplayInfo> cache;
    static OSD::Platform::Nanoseconds cacheTick = 0;

    /*
     * Rebuilt on the same two-second cadence as the EDID info, and for a related reason: the mode
     * search behind hasArcadeMode walks every mode of every output, which is hundreds of driver
     * calls and not something to repeat on every frame. Two seconds is short enough that attaching
     * or removing a monitor while the settings window is open is picked up without a restart.
     *
     * An empty result is not cached, because the usual cause is that SDL's video subsystem is not
     * up yet rather than that the machine has no outputs.
     */
    const OSD::Platform::Nanoseconds now = OSD::Platform::Now();
    if (!cache.empty() && (now - cacheTick) < 2000000000LL)
        return cache;

    const int count = OSD::Platform::GetDisplayCount();
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
        OSD::Platform::DisplayInfo platform{};
        if (!OSD::Platform::GetDisplayInfo(i, &platform))
            continue;

        DisplayInfo info;
        info.sdlIndex = i;
        info.name = (platform.name != nullptr && *platform.name != '\0') ? platform.name : ("Display " + std::to_string(i + 1));

        info.width = platform.bounds.width;
        info.height = platform.bounds.height;
#ifdef _WIN32
        const DisplayNumberQuery match = WindowsDisplayInfoForRect(platform.bounds);
        info.number = match.number;
        info.primary = match.primary;
        info.deviceName = match.deviceName;
        info.panelWidth = match.panelWidth;
        info.panelHeight = match.panelHeight;
#else
        // SDL orders the system's main display first and documents index 0 as the primary; the
        // platform layer reads the real flag where the backend exposes one.
        info.primary = platform.primary;
#endif

        OSD::Platform::DisplayMode desktop{};
        if (OSD::Platform::GetDesktopDisplayMode(platform.id, &desktop))
            info.desktopHz = (int)(desktop.refreshHz + 0.5f);

        OSD::Platform::DisplayMode arcade{};
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

    OSD::Platform::DisplayMode desktop{};
    if (OSD::Platform::GetDesktopDisplayMode(OSD::Platform::GetDisplayIdForIndex(displayIndex), &desktop) &&
        desktop.width > 0 && desktop.height > 0)
    {
        width = desktop.width;
        height = desktop.height;
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
    // Low-resolution rendering wins: IntegerScale is ignored while it is on, so reporting it as
    // active would describe a rectangle the geometry code did not use.
    info.active = s_runtime_config["IntegerScale"].ValueAsDefault<bool>(false)
        && !s_runtime_config["Stretch"].ValueAsDefault<bool>(false)
        && !LowResolutionRenderingEnabled();
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

DisplayRateSupport RateSupportForTiming(const std::vector<DisplayTimingInfo>& timing, float hz)
{
    if (timing.empty())
        return DisplayRateSupport::Unknown;

    /*
     * Match the EDID entry to the output the emulator is configured to drive. On a single-display
     * machine there is nothing to match, but on a multi-monitor desk the timing range of the wrong
     * monitor would answer the wrong question - and silently enable the arcade rate on a panel that
     * cannot scan at it.
     *
     * The EDID path reports one entry per monitor without an SDL index, so the friendly name is the
     * join key. The names come from different APIs (SDL's monitor name and EDID 0xFC), which is why
     * the comparison is trimmed and case-insensitive. If they cannot be matched, Unknown is the
     * honest answer; it is not treated as Unsupported.
     */
    const DisplayTimingInfo* match = nullptr;
    const std::vector<DisplayInfo>& outputs = GetDisplays();
    if (timing.size() == 1 && outputs.size() == 1)
        match = &timing[0];
    else
    {
        auto normalize = [](std::string value)
        {
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
                value.pop_back();
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                value.erase(value.begin());
            for (char& c : value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        };

        const int displayIndex = GetConfiguredDisplaySdlIndex();
        std::string wanted;
        for (const DisplayInfo& display : outputs)
        {
            if (display.sdlIndex == displayIndex)
            {
                wanted = normalize(display.name);
                break;
            }
        }

        if (!wanted.empty())
        {
            for (const DisplayTimingInfo& info : timing)
            {
                if (normalize(info.name) == wanted)
                {
                    match = &info;
                    break;
                }
            }
        }
    }

    if (match == nullptr)
        return DisplayRateSupport::Unknown;

    const bool hasRange = match->minHz > 0 && match->maxHz > 0;
    const bool hasVrr = match->declaresVrr && match->vrrMinHz > 0 && match->vrrMaxHz > 0;
    if (!hasRange && !hasVrr)
        return DisplayRateSupport::Unknown;

    if (hasRange && hz >= (float)match->minHz && hz <= (float)match->maxHz)
        return DisplayRateSupport::Supported;
    if (hasVrr && hz >= (float)match->vrrMinHz && hz <= (float)match->vrrMaxHz)
        return DisplayRateSupport::Supported;
    return DisplayRateSupport::Unsupported;
}

DisplayRateSupport ConfiguredDisplayRateSupport(float hz)
{
    return RateSupportForTiming(GetDisplayTimingInfo(), hz);
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
#if !defined(SUPERMODEL_PURE_VULKAN)
static Result CreateGLScreen(bool coreContext, const std::string &caption, bool focusWindow, unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio, bool fullScreen)
// The quadRendering parameter is gone: it existed only to decide between an OpenGL 4.5 and a 4.1
// context, and since the quad shader no longer needs 4.30 features there is nothing to decide.
// Removed rather than left unused so the signature does not advertise a choice that no longer exists.
{
  GLenum err;

  // Call only once per program session (this is because of issues with
  // DirectInput when the window is destroyed and a new one created). Use
  // ResizeGLScreen() to change resolutions instead.
  if (s_platformWindow != nullptr)
  {
    return ErrorLog("Internal error: CreateGLScreen() called more than once");
  }

  // Initialize video subsystem
  if (!OSD::Platform::Initialize(OSD::Platform::Subsystem_Video))
    return ErrorLog("Unable to initialize SDL video subsystem: %s\n", OSD::Platform::GetError());

  // Important GL attributes
  OSD::Platform::GLAttributes attributes;
  attributes.redSize = 8;
  attributes.greenSize = 8;
  attributes.blueSize = 8;
  if(s_runtime_config["New3DEngine"].ValueAsDefault<bool>(true))
  {
    attributes.depthSize = 0;
    attributes.stencilSize = 0;
  }
  else
  {
    attributes.depthSize = 24;
    attributes.stencilSize = 8;
  }
  attributes.doubleBuffer = true;

  if (coreContext) {
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
      attributes.coreProfile = true;
      attributes.majorVersion = 4;
      attributes.minorVersion = 1;
  }
  else
  {
      attributes.coreProfile = false;
  }
  OSD::Platform::SetGLAttributes(attributes);

  // Created on the configured output rather than on the primary one. The window's display is what
  // the fullscreen calls act on, so for the exclusive modes this has to be decided here: resolving
  // the flavour afterwards would otherwise apply a custom timing to whichever monitor the window
  // landed on, which with a centred position is always the primary display.
  const int displayIndex = GetConfiguredDisplaySdlIndex();

  // Set video mode
  OSD::Platform::WindowDesc desc;
  desc.title = caption.c_str();
  desc.width = (int)*totalXResPtr;
  desc.height = (int)*totalYResPtr;
  desc.display = OSD::Platform::GetDisplayIdForIndex(displayIndex);
  desc.fullscreenDesktop = fullScreen;
  // Hidden until the first frame; see the note on s_windowRevealPending.
  desc.flags = OSD::Platform::WindowFlag_OpenGL | OSD::Platform::WindowFlag_Hidden;

  s_platformWindow = OSD::Platform::CreatePlatformWindow(desc);
  if (nullptr == s_platformWindow)
  {
    ErrorLog("Unable to create an OpenGL display: %s\n", OSD::Platform::GetError());
    return Result::FAIL;
  }
  SetWindowIcon(s_platformWindow);

  // Deferred to the first present, together with showing the window.
  s_windowRevealPending = true;
  s_windowRevealRaise = focusWindow;

  // Create OpenGL context
  s_glContext = OSD::Platform::CreateGLContext(s_platformWindow);
  if (nullptr == s_glContext)
  {
    ErrorLog("Unable to create OpenGL context: %s\n", OSD::Platform::GetError());
    return Result::FAIL;
  }

  // Set vsync
  OSD::Platform::SetGLSwapInterval(s_runtime_config["VSync"].ValueAsDefault<bool>(false) ? 1 : 0);

  // Set the context as the current window context
  OSD::Platform::MakeGLContextCurrent(s_platformWindow, s_glContext);

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
#endif

static void DestroyGLScreen()
{
  // A session that ends before it ever presented (a load failure, say) leaves the flag set; clearing
  // it here keeps the next window from inheriting it.
  s_windowRevealPending = false;
  s_windowRevealRaise = false;
#ifdef _WIN32
  // The splash shown while a deferred state was pending is owned by this window; hiding it here as
  // well covers the session ending (or failing) before the state was ever applied.
  OSD::Windows::HideStartupSplash();
#endif
#if !defined(SUPERMODEL_OPENGL_ONLY)
  if (s_vkContext != nullptr)
  {
    delete s_vkContext;
    s_vkContext = nullptr;
    if (s_platformWindow != nullptr)
    {
      OSD::Platform::DestroyWindow(s_platformWindow);
      s_platformWindow = nullptr;
    }
    return;
  }
#endif
  if (s_platformWindow != nullptr)
  {
#if !defined(SUPERMODEL_PURE_VULKAN)
    // Delete the context belonging to the game window, not whichever context happens to be
    // current: the settings GUI keeps its own window and GL context alive across sessions.
    if (s_glContext != nullptr)
    {
      OSD::Platform::DestroyGLContext(s_glContext);
      s_glContext = nullptr;
    }
#endif
    OSD::Platform::DestroyWindow(s_platformWindow);
    s_platformWindow = nullptr;
  }
}

#if !defined(SUPERMODEL_OPENGL_ONLY)
static Result CreateVulkanScreen(const std::string &caption, bool focusWindow, unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio, bool fullScreen)
{
  if (s_platformWindow != nullptr)
    return ErrorLog("Internal error: CreateVulkanScreen() called more than once");
  if (!OSD::Platform::Initialize(OSD::Platform::Subsystem_Video))
    return ErrorLog("Unable to initialize SDL video subsystem: %s\n", OSD::Platform::GetError());

  // Same reasoning as CreateGLScreen(): the window has to be on the configured output before any
  // fullscreen mode is applied, because the mode applies to the window's display.
  const int displayIndex = GetConfiguredDisplaySdlIndex();

  OSD::Platform::WindowDesc desc;
  desc.title = caption.c_str();
  desc.width = (int)*totalXResPtr;
  desc.height = (int)*totalYResPtr;
  desc.display = OSD::Platform::GetDisplayIdForIndex(displayIndex);
  desc.fullscreenDesktop = fullScreen;
  // Hidden until the first frame; see the note on s_windowRevealPending.
  desc.flags = OSD::Platform::WindowFlag_Vulkan | OSD::Platform::WindowFlag_Hidden;

  s_platformWindow = OSD::Platform::CreatePlatformWindow(desc);
  if (nullptr == s_platformWindow)
  {
    ErrorLog("Unable to create a Vulkan display: %s\n", OSD::Platform::GetError());
    return Result::FAIL;
  }
  SetWindowIcon(s_platformWindow);
  // Deferred to the first present, together with showing the window.
  s_windowRevealPending = true;
  s_windowRevealRaise = focusWindow;

  s_vkContext = new Vulkan3D::CVulkanContext();
  if (Result::OKAY != s_vkContext->InitDevice(s_platformWindow, s_runtime_config["VkValidation"].ValueAsDefault<bool>(false), s_runtime_config["VSync"].ValueAsDefault<bool>(false), s_runtime_config["VulkanDevice"].ValueAsDefault<std::string>("")))
    return Result::FAIL;
  CalcScreenGeometry(xOffsetPtr, yOffsetPtr, xResPtr, yResPtr, totalXResPtr, totalYResPtr, keepAspectRatio);
  return Result::OKAY;
}
#endif

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

    // A session may leave without entering the mode it resolved (the driver refusing the switch is
    // the normal case). Clearing here means a later session can never apply the previous one's mode.
    s_exclusiveMode = OSD::Platform::DisplayMode();
    s_haveExclusiveMode = false;

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
    // The G-SYNC/VRR path also ends up in exclusive fullscreen, and there the panel follows whatever
    // rate is presented. RefreshRate is set to 57.5242 so the OpenGL loop runs at the arcade rate;
    // the Vulkan loop takes its cadence from RefreshRateMode, so the user's choice there wins and
    // this block only makes the display follow it. The stored RefreshRate preference is left
    // untouched: switching back to borderless restores it.
    //
    // Note what is deliberately *not* derived from this setting: the emulated CPU's per-frame cycle
    // budget is fixed at the arcade's 57.524160 Hz (see CModel3::RunMainBoardFrame). Pacing at 60 Hz
    // therefore makes the game run 4.3% fast, which is why the statistics readout prints the target
    // rate beside the arcade's and works out the difference.
    if (refreshRateHz > 0.0f)
        s_runtime_config.Get("RefreshRate").SetValue(refreshRateHz);
    else
    {
        const DisplayRateSupport support = ConfiguredDisplayRateSupport(57.5242f);
        // RefreshRate is still written because the OpenGL loop paces from it. The Vulkan loop paces
        // from RefreshRateMode, so there the number is only a legacy fallback and the log must not
        // describe it as the rate the session is actually presenting at.
        if (support == DisplayRateSupport::Supported)
        {
            s_runtime_config.Get("RefreshRate").SetValue(57.5242f);
#if defined(SUPERMODEL_OPENGL_ONLY)
            InfoLog("Exclusive fullscreen: G-SYNC/VRR path; the display timing range includes 57.524 Hz, pacing at the arcade rate.");
#else
            InfoLog("Exclusive fullscreen: G-SYNC/VRR path; the display timing range includes 57.524 Hz; "
                "the panel follows the rate chosen under Presentation cadence.");
#endif
        }
        else if (support == DisplayRateSupport::Unsupported)
        {
#if defined(SUPERMODEL_OPENGL_ONLY)
            InfoLog("Exclusive fullscreen: G-SYNC/VRR path; the display timing range excludes 57.524 Hz, keeping RefreshRate %.4f Hz.",
                s_runtime_config["RefreshRate"].ValueAs<float>());
#else
            InfoLog("Exclusive fullscreen: G-SYNC/VRR path; the display timing range excludes 57.524 Hz; "
                "the panel follows the rate chosen under Presentation cadence.");
#endif
        }
        else
        {
#if defined(SUPERMODEL_OPENGL_ONLY)
            InfoLog("Exclusive fullscreen: G-SYNC/VRR path; the display timing range is unknown, keeping RefreshRate %.4f Hz.",
                s_runtime_config["RefreshRate"].ValueAs<float>());
#else
            InfoLog("Exclusive fullscreen: G-SYNC/VRR path; the display timing range is unknown; "
                "the panel follows the rate chosen under Presentation cadence.");
#endif
        }
    }

    if (!s_runtime_config["VSync"].ValueAsDefault<bool>(true))
        InfoLog("Exclusive fullscreen: VSync is off; G-SYNC/VRR will not hold frames. Enable VSync for a locked refresh rate.");

    if (refreshRateHz > 0.0f)
    {
        OSD::Platform::DisplayMode mode{};
        if (FindArcadeDisplayMode(refreshRateHz, displayIndex, mode))
        {
            s_exclusiveMode = mode;
            s_haveExclusiveMode = true;
            InfoLog("Exclusive fullscreen: fixed mode %ux%u@%i Hz selected (arcade rate %.4f Hz).",
                mode.width, mode.height, (int)(mode.refreshHz + 0.5f), refreshRateHz);
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
    OSD::Platform::DisplayMode desktop{};
    if (OSD::Platform::GetDesktopDisplayMode(OSD::Platform::GetDisplayIdForIndex(displayIndex), &desktop))
    {
        /*
         * The size is replaced with the panel's own where it could be read, keeping the desktop
         * refresh rate. SDL reports this display's desktop mode as 2880x1620 on a machine with a 4K
         * primary at 150% scaling and a 1080p secondary at 100%: the process asks for system DPI
         * awareness, so Windows converts the geometry of any monitor whose scaling differs from the
         * primary's into the system DPI's space, and 1920x1080 becomes 2880x1620. Nothing is wrong
         * with SDL's arithmetic - the number is simply in a different space from the panel's pixels.
         * Asking the driver for a mode of that size fails, and the failure is silent: the window
         * keeps whatever size it had, which for a supersampled render resolution is larger than the
         * screen.
         *
         * GetConfiguredFullscreenSize() explains the rest, and is also what sizes the window.
         */
        int panelWidth = 0;
        int panelHeight = 0;
        if (GetConfiguredFullscreenSize(panelWidth, panelHeight))
        {
            desktop.width = panelWidth;
            desktop.height = panelHeight;
        }

        s_exclusiveMode = desktop;
        s_haveExclusiveMode = true;
        InfoLog("Exclusive fullscreen: desktop mode %ux%u@%i Hz reserved for display %d; G-SYNC/VRR should track the emulator rate.",
            desktop.width, desktop.height, (int)(desktop.refreshHz + 0.5f), displayIndex);
    }
    else
        InfoLog("Exclusive fullscreen: desktop mode unavailable; G-SYNC/VRR will track the emulator rate.");

    return FullScreenMode::ExclusiveDesktop;
}

static Result ApplyFullScreenMode(bool fullScreen)
{
    if (!fullScreen)
    {
        if (!OSD::Platform::SetFullscreen(s_platformWindow, false))
        {
            ErrorLog("Unable to enter windowed mode: %s\n", OSD::Platform::GetError());
            return Result::FAIL;
        }
        return Result::OKAY;
    }

    if (s_fullScreenMode != FullScreenMode::Borderless && s_haveExclusiveMode &&
        OSD::Platform::SetExclusiveFullscreen(s_platformWindow,
            OSD::Platform::GetDisplayIdForIndex(GetConfiguredDisplaySdlIndex()), s_exclusiveMode))
    {
        return Result::OKAY;
    }

    if (s_fullScreenMode != FullScreenMode::Borderless)
    {
        // The exclusive switch was refused (for example the display configuration changed).
        InfoLog("Exclusive fullscreen: refused by the display driver, falling back to borderless fullscreen.");
        s_fullScreenMode = FullScreenMode::Borderless;
    }

    if (!OSD::Platform::SetFullscreen(s_platformWindow, true))
    {
        ErrorLog("Unable to enter fullscreen mode: %s\n", OSD::Platform::GetError());
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
      OSD::Platform::SetWindowSize(s_platformWindow, width, height);
      return;
    }
  }

  OSD::Platform::SetWindowSize(s_platformWindow, *totalXResPtr, *totalYResPtr);
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
  OSD::Platform::GetWindowSize(s_platformWindow, &width, &height);
  InfoLog("%s: window size %dx%d.", backend, width, height);
}

static void ReportRenderInfo(uint32_t aaFactor, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes)
{
  StatsRenderInfo info;

#if !defined(SUPERMODEL_OPENGL_ONLY)
  if (s_vkContext)
  {
    /*
     * Vulkan rasterises the scene into the visible viewport (xRes*yRes) inside a render target the
     * size of the swapchain. In fullscreen the swapchain is the display, not XResolution, and with
     * integer scaling the viewport is the multiple the panel allows - so reporting only the extent
     * showed 3840x2160 for a 3x preset that was actually drawing 2480x1920. Both are reported now:
     * the scene size first, the target after the arrow.
     *
     * The sample count comes from the renderer rather than the configuration because Init() may have
     * reduced it: a driver that cannot support the requested count for both attachments silently ends
     * up at 1x, and reporting the request would show antialiasing that is not happening.
     */
    const VkExtent2D extent = s_vkContext->Extent();
    /*
     * With low-resolution rendering the scene is rasterised at a fixed 496x384 and upscaled onto the
     * picture rectangle, so the picture's size is the *output* rather than the render size. Reporting
     * the viewport as the render size would claim 1395x1080 worth of work where 496x384 is being
     * done, which is exactly the number this line exists to get right.
     */
    const VkExtent2D render = s_vk3dRenderer ? s_vk3dRenderer->RenderExtent() : VkExtent2D{};
    if (render.width > 0 && render.height > 0)
    {
      info.width = render.width;
      info.height = render.height;
      info.outputWidth = xRes;
      info.outputHeight = yRes;
    }
    else
    {
      info.width = xRes;
      info.height = yRes;
      info.outputWidth = extent.width;
      info.outputHeight = extent.height;
    }
    info.samples = s_vk3dRenderer ? (int)s_vk3dRenderer->SampleCount() : 1;
    info.method = (info.samples > 1) ? StatsAntiAliasing::Msaa : StatsAntiAliasing::None;
    StatsOsdSetRenderInfo(info);
    return;
  }
#else
  (void)xRes;
  (void)yRes;
#endif

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

#if !defined(SUPERMODEL_OPENGL_ONLY)
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
#endif

#if !defined(SUPERMODEL_PURE_VULKAN)
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
    if (Result::OKAY != CreateGLScreen(s_runtime_config["New3DEngine"].ValueAs<bool>(), MODEL3VULKAN_TITLE_FULL " - Querying OpenGL Information...", false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false))
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
#endif // SUPERMODEL_PURE_VULKAN

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

#if !defined(SUPERMODEL_PURE_VULKAN)
static void SaveFrameBuffer(const std::string& file)
{
    std::shared_ptr<uint8_t> pixels(new uint8_t[totalXRes * totalYRes * 4], std::default_delete<uint8_t[]>());
    glReadPixels(0, 0, totalXRes, totalYRes, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
    Util::WriteSurfaceToBMP<Util::RGBA8>(file, pixels.get(), totalXRes, totalYRes, true);
}
#endif

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
#if defined(SUPERMODEL_OPENGL_ONLY)
    SaveFrameBuffer(file);
#elif defined(SUPERMODEL_PURE_VULKAN)
    if (s_vkContext != nullptr)
        s_vkContext->RequestScreenshot(file);
#else
    if (s_vkContext != nullptr)
        s_vkContext->RequestScreenshot(file);
    else
        SaveFrameBuffer(file);
#endif
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

#if !defined(SUPERMODEL_PURE_VULKAN)
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
#endif // SUPERMODEL_PURE_VULKAN

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

/*
 * Quick-save slots exposed by the in-game menu and cycled with F6.  The file naming has always
 * allowed two digits and the save manager lists every container it finds, but the menu and the
 * hotkey cycle stop here so the list stays a page rather than a directory dump.
 */
static constexpr unsigned SAVE_SLOT_COUNT = 16;

static unsigned s_saveSlot = 1;           // save state slot # (1..SAVE_SLOT_COUNT)

/*
 * Slot file names.
 *
 * The container keeps the classic slot numbering, one letter apart from it: slot 1 of daytona2 is
 * "daytona2.zst1" where the plain block file is "daytona2.st1".  Both names are produced here so the
 * save, load and overlay paths cannot disagree about where a slot lives.
 */
static std::string SaveSlotContainer(const std::string& game, unsigned slot)
{
  return Util::Format() << FileSystemPath::GetPath(FileSystemPath::SavePathType()) << game << ".zst" << slot;
}

static std::string SaveSlotLegacyFile(const std::string& game, unsigned slot)
{
  return Util::Format() << FileSystemPath::GetPath(FileSystemPath::SavePathType()) << game << ".st" << slot;
}

#if defined(SUPERMODEL_OPENGL_ONLY)
/*
 * Where the OpenGL-only build found its saves before the folders were split, and where the Vulkan
 * build still keeps its .st fallback files. Reading from it keeps old slots visible after the split;
 * new saves always go to SavePathType().
 */
static std::string SaveSlotLegacyFallback(const std::string& game, unsigned slot)
{
  return Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << game << ".st" << slot;
}
#endif

/* True when either form of the slot already holds a save. */
static bool SaveSlotOccupied(const std::string& game, unsigned slot)
{
  std::error_code error;
  if (std::filesystem::exists(SaveSlotContainer(game, slot), error) && !error)
    return true;

  error.clear();
  if (std::filesystem::exists(SaveSlotLegacyFile(game, slot), error) && !error)
    return true;

#if defined(SUPERMODEL_OPENGL_ONLY)
  error.clear();
  return std::filesystem::exists(SaveSlotLegacyFallback(game, slot), error) && !error;
#else
  return false;
#endif
}

/*
 * The slot F6 should select.
 *
 * F5 on its own deliberately writes back to the slot that was loaded, which is what makes
 * "load, play, F5" overwrite the save the player resumed. F6 is the explicit request for a
 * different slot, so it is the one place that should skip saves that already exist; writing over
 * the next numeric save is not what that request means.
 *
 * The search starts after the current slot and wraps, so a session that started from slot 7 finds
 * slot 8 rather than dropping back to 1. When all sixteen are occupied the next numeric slot is
 * returned so F6 still cycles; choosing a save to overwrite deliberately is what the in-game menu
 * and the Manage saves window are for.
 *
 * Slot 0 is outside the quick-save range: it is where imported legacy .st0 files live, and zst0 is
 * not one of the sixteen entries the menu lists.
 */
static unsigned NextFreeSaveSlot(const std::string& game, unsigned current)
{
  const unsigned start = current <= SAVE_SLOT_COUNT ? current : 0;
  for (unsigned step = 1; step <= SAVE_SLOT_COUNT; ++step)
  {
    const unsigned slot = ((start + step - 1) % SAVE_SLOT_COUNT) + 1;
    if (!SaveSlotOccupied(game, slot))
      return slot;
  }

  return (start % SAVE_SLOT_COUNT) + 1;
}

/*
 * The newest slot that actually holds a save, for quick-load.
 *
 * A fresh session starts on the first free slot because that is where F5 should write. That slot is
 * empty by definition, so F7 must not stop there: it means "load my save", and the save the player
 * wants is almost always the newest one. The in-game menu remains the way to pick an older slot
 * deliberately.
 *
 * Returns 0 when the quick-save range has no saves at all (zst0/st0 imported files are outside it).
 */
static unsigned NewestSaveSlot(const std::string& game)
{
  for (unsigned slot = SAVE_SLOT_COUNT; slot > 0; --slot)
  {
    if (SaveSlotOccupied(game, slot))
      return slot;
  }
  return 0;
}

/*
 * The slot a save file belongs to: "lamachin.zst3" and "lamachin.st3" both give 3.
 *
 * Returns -1 when the name carries no slot number, so slot 0 (which the imported .st0 files use) is
 * still distinguishable from "not a slot at all".  The result is used to make F5 write back to the
 * save the player actually loaded instead of the slot the hotkeys happened to be pointing at.
 */
static int SaveSlotFromPath(const std::filesystem::path& path)
{
  const std::string name = path.filename().string();
  const auto dot = name.rfind('.');
  if (dot == std::string::npos || dot + 1 >= name.size())
    return -1;

  const std::string suffix = name.substr(dot + 1);
  std::string digits;
  if (suffix.rfind("zst", 0) == 0)
    digits = suffix.substr(3);
  else if (suffix.rfind("st", 0) == 0)
    digits = suffix.substr(2);
  else
    return -1;

  if (digits.empty() || digits.size() > 2)
    return -1;
  for (const char c : digits)
  {
    if (c < '0' || c > '9')
      return -1;
  }
  return std::stoi(digits);
}

#if defined(SUPERMODEL_PURE_VULKAN)
/*
 * Reads a file into memory.  Used for the staged block file and for the thumbnail the container
 * carries; both are small and are read once per save.
 */
static bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& contents)
{
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return false;

  const std::streamsize size = input.tellg();
  if (size < 0)
    return false;

  contents.resize((std::size_t)size);
  input.seekg(0, std::ios::beg);
  if (size > 0 && !input.read(reinterpret_cast<char*>(contents.data()), size))
  {
    contents.clear();
    return false;
  }
  return true;
}

/*
 * Unpacks a .zst container (or a legacy .st, which the module passes through unchanged) into the
 * temporary block file the upstream loader expects.  Returns false and logs on any failure, so the
 * caller can fall back to the classic path.
 */
static bool ExtractStateContainer(const std::filesystem::path& container, std::string& temporaryPath)
{
  auto bridge = OSD::HostCore::Shared();
  if (!bridge)
  {
    ErrorLog("HostCore: %s", bridge.error().message.c_str());
    return false;
  }

  auto loaded = (*bridge)->LoadVkState(container);
  if (!loaded)
  {
    ErrorLog("HostCore: cannot load '%s': %s", container.string().c_str(),
      loaded.error().message.c_str());
    return false;
  }

  const std::string temporary =
    Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << ".vktmp.st";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(loaded->bytes.data()),
    (std::streamsize)loaded->bytes.size());
  output.close();
  if (!output)
  {
    ErrorLog("HostCore: cannot write the temporary save state '%s'.", temporary.c_str());
    return false;
  }

  temporaryPath = temporary;
  return true;
}

/*
 * A save that is waiting for its thumbnail.
 *
 * The picture cannot be grabbed from inside the UI callback: this frame's swapchain image has not
 * been drawn yet, and the previous one still shows the panel whose Save button was just pressed.
 * So the state bytes are staged in memory, the renderer is asked for a thumbnail of the next frame
 * (captured before the interface is drawn), and the container is packaged a frame or two later when
 * the PNG shows up. The emulator is paused for the whole sequence, so nothing moves in between, and
 * a capture that never arrives simply produces a save without a thumbnail.
 */
struct PendingSave
{
  bool active = false;
  int framesWaited = 0;
  std::string game;
  std::string target;
  std::string legacyTarget;    // written instead if the container cannot be produced
  std::string thumbnailPath;   // empty when no capture was requested
  std::vector<std::uint8_t> raw;
};

static PendingSave s_pendingSave;
// Unique per save: a fixed ".vkthumb.png" could be left over from an earlier save when a
// capture fails or the renderer still holds the old file, and the container would then be
// packaged with a thumbnail from a different frame.
static unsigned s_thumbnailSerial = 0;

static void PackagePendingSave(const std::vector<std::uint8_t>& thumbnail)
{
  auto bridge = OSD::HostCore::Shared();
  if (!bridge)
  {
    ErrorLog("HostCore: %s", bridge.error().message.c_str());
    return;
  }

  HostSaveMeta meta = {};
  meta.StructSize = sizeof(HostSaveMeta);
  std::snprintf(meta.GameName, sizeof(meta.GameName), "%s", s_pendingSave.game.c_str());
  std::snprintf(meta.Title, sizeof(meta.Title), "%s",
    std::filesystem::path(s_pendingSave.target).filename().string().c_str());
  std::snprintf(meta.Version, sizeof(meta.Version), "%s", MODEL3VULKAN_VERSION);

  if (auto saved = (*bridge)->SaveVkState(s_pendingSave.target, meta, s_pendingSave.raw, thumbnail); saved)
  {
    std::error_code sizeError;
    const auto written = std::filesystem::file_size(s_pendingSave.target, sizeError);
    printf("Saved state to '%s'.\n", s_pendingSave.target.c_str());
    InfoLog("Saved state to '%s' (zstd container, %zu bytes from %zu, thumbnail %zu bytes).",
      s_pendingSave.target.c_str(), (std::size_t)(sizeError ? 0 : written), s_pendingSave.raw.size(),
      thumbnail.size());
  }
  else
  {
    // The state bytes are what the classic loader expects, so a packaging failure still leaves the
    // player with a working save rather than with nothing.
    ErrorLog("Unable to write '%s': %s. Falling back to a plain .st file.", s_pendingSave.target.c_str(),
      saved.error().message.c_str());

    std::ofstream fallback(s_pendingSave.legacyTarget, std::ios::binary | std::ios::trunc);
    fallback.write(reinterpret_cast<const char*>(s_pendingSave.raw.data()),
      (std::streamsize)s_pendingSave.raw.size());
    fallback.close();
    if (fallback)
    {
      printf("Saved state to '%s'.\n", s_pendingSave.legacyTarget.c_str());
      InfoLog("Saved state to '%s' (plain block file).", s_pendingSave.legacyTarget.c_str());
    }
    else
    {
      ErrorLog("Unable to save state to '%s' either.", s_pendingSave.legacyTarget.c_str());
    }
  }
}

/*
 * Called once per frame. Picks up the thumbnail when the renderer's worker has finished writing it,
 * or gives up after half a second - a session where the swapchain cannot be read (or a paused loop
 * that stops presenting) must still produce a save.
 */
static void ServicePendingSave()
{
  if (!s_pendingSave.active)
    return;

  std::vector<std::uint8_t> thumbnail;
  bool ready = s_pendingSave.thumbnailPath.empty();
  if (!ready)
  {
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(s_pendingSave.thumbnailPath, sizeError);
    if (!sizeError && size > 0)
      ready = ReadWholeFile(s_pendingSave.thumbnailPath, thumbnail);
  }

  if (!ready && ++s_pendingSave.framesWaited < 30)
    return;

  if (!s_pendingSave.thumbnailPath.empty())
  {
    std::error_code removeError;
    std::filesystem::remove(s_pendingSave.thumbnailPath, removeError);
  }

  PackagePendingSave(thumbnail);
  s_pendingSave = PendingSave();
}
#endif

static void SaveState(IEmulator *Model3)
{
#if defined(SUPERMODEL_PURE_VULKAN)
  /*
   * Package the state through the C# module.
   *
   * The upstream serialiser writes into a CBlockFile, which is file based; changing that would touch
   * the state layout, so the block file is written to a staging path first and then handed over as
   * bytes.  On any failure the classic .st below is still written, so a save is never silently lost
   * because the cold-path module is missing.
   */
  if (auto bridge = OSD::HostCore::Shared(); bridge)
  {
    const std::string game = Model3->GetGame().name;
    const std::string staging =
      Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << ".vkstmp.st";

    CBlockFile state;
    if (Result::OKAY == state.Create(staging, "Supermodel Save State",
          "Supermodel Version " SUPERMODEL_VERSION))
    {
      int32_t fileVersion = STATE_FILE_VERSION;
      state.Write(&fileVersion, sizeof(fileVersion));
      state.Write(game);
      Model3->SaveState(&state);
      state.Close();

      std::vector<std::uint8_t> raw;
      const bool staged = ReadWholeFile(staging, raw);
      std::error_code removeError;
      std::filesystem::remove(staging, removeError);

      if (staged)
      {
        const std::string target = SaveSlotContainer(game, s_saveSlot);

        // A save still waiting for its thumbnail is flushed first, so pressing F5 twice in quick
        // succession cannot drop the first one. With no thumbnail to wait for it is written here.
        if (s_pendingSave.active)
        {
          s_pendingSave.framesWaited = 30;
          ServicePendingSave();
        }

        s_pendingSave.active = true;
        s_pendingSave.framesWaited = 0;
        s_pendingSave.game = game;
        s_pendingSave.target = target;
        s_pendingSave.legacyTarget = SaveSlotLegacyFile(game, s_saveSlot);
        s_pendingSave.raw = std::move(raw);

        if (s_vkContext != nullptr)
        {
          char thumbName[48];
          std::snprintf(thumbName, sizeof(thumbName), ".vkthumb-%u.png", ++s_thumbnailSerial);
          const std::string thumbnail =
            Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << thumbName;
          // A unique name means a leftover from a previous save cannot be mistaken for this
          // frame's picture, even if the renderer cannot overwrite the old file.
          std::error_code removeError;
          std::filesystem::remove(thumbnail, removeError);
          s_vkContext->RequestThumbnail(thumbnail, 480, 360);
          s_pendingSave.thumbnailPath = thumbnail;
        }

        // Packaged by ServicePendingSave(): immediately when no capture was requested, otherwise as
        // soon as the renderer has written the thumbnail.
        ServicePendingSave();
        return;
      }
      else
      {
        ErrorLog("Unable to read back the staged save state '%s'.", staging.c_str());
      }
    }
    else
    {
      ErrorLog("Unable to save state to '%s'.", staging.c_str());
    }
  }
#endif

  CBlockFile  SaveState;

  std::string file_path = SaveSlotLegacyFile(Model3->GetGame().name, s_saveSlot);
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

static bool LoadStateFromFile(IEmulator *Model3, const std::string &file_path)
{
  CBlockFile  SaveState;

  // Open and check to make sure format is correct
  if (Result::OKAY != SaveState.Load(file_path))
  {
    ErrorLog("Unable to load state from '%s'.", file_path.c_str());
    return false;
  }

  if (Result::OKAY != SaveState.FindBlock("Supermodel Save State"))
  {
    ErrorLog("'%s' does not appear to be a valid save state file.", file_path.c_str());
    return false;
  }

  int32_t fileVersion;
  SaveState.Read(&fileVersion, sizeof(fileVersion));
  if (fileVersion != STATE_FILE_VERSION)
  {
    ErrorLog("'%s' is incompatible with this version of Supermodel.", file_path.c_str());
    return false;
  }

  // Load
  Model3->LoadState(&SaveState);
  SaveState.Close();
  printf("Loaded state from '%s'.\n", file_path.c_str());
  InfoLog("Loaded state from '%s' (PPC PC=%08X, LR=%08X).", file_path.c_str(),
    ppc_get_pc(), ppc_get_lr());
  return true;
}

/*
 * Loads the current slot.
 *
 * The container is preferred; a slot that only has the classic .st file still loads, so saves made
 * before this format existed - or by the SDL2 build - keep working.  An explicitly supplied path is
 * used as given (that is how the session's initial state is loaded).
 *
 * A quick-load starts on the slot F5 would write to. On a fresh session that is the first free
 * slot, which is empty, so F7 falls back to the newest save instead of reporting a load failure.
 * Without this, F6 (which deliberately skips occupied slots) made the existing saves unreachable
 * from the keyboard: the session began on slot 4 and F7 kept trying 4, 5, 6...
 */
static void LoadState(IEmulator *Model3, std::string file_path = std::string())
{
  std::string temporary;

#if defined(SUPERMODEL_PURE_VULKAN)
  // A save still waiting for its thumbnail has not been written yet; finish it first so loading a
  // slot right after saving it sees the new state rather than the previous one. This has to run
  // before the empty-slot fallback, otherwise a just-requested save would not be counted yet and
  // F7 would load an older slot instead.
  if (s_pendingSave.active)
  {
    s_pendingSave.framesWaited = 30;
    ServicePendingSave();
  }
#endif

  if (file_path.empty() && !SaveSlotOccupied(Model3->GetGame().name, s_saveSlot))
  {
    const unsigned newest = NewestSaveSlot(Model3->GetGame().name);
    if (newest == 0)
    {
      ErrorLog("No save state in slot %u for '%s'.", s_saveSlot, Model3->GetGame().name.c_str());
      return;
    }

    InfoLog("Save slot %u is empty; loading the newest save in slot %u.", s_saveSlot, newest);
    printf("Save slot: %u\n", newest);
    s_saveSlot = newest;
  }

#if defined(SUPERMODEL_PURE_VULKAN)
  if (file_path.empty())
  {
    const std::string container = SaveSlotContainer(Model3->GetGame().name, s_saveSlot);
    std::error_code existsError;
    if (std::filesystem::exists(container, existsError) && !existsError)
    {
      if (ExtractStateContainer(container, temporary))
        file_path = temporary;
    }
  }
  else
  {
    /*
     * An explicit path may point at a .zst container.
     *
     * That is the format this build writes, and -load-state (and the "start from
     * this state" setting behind it) used to accept only the raw block file
     * inside it - so the one file a user is most likely to hand over was the one
     * it refused.  The module passes a legacy .st through unchanged, so trying
     * the container first is safe for both formats; a path that is neither falls
     * through to the classic loader below, which reports it.
     */
    std::error_code existsError;
    if (std::filesystem::exists(file_path, existsError) && !existsError)
    {
      if (ExtractStateContainer(file_path, temporary))
        file_path = temporary;
    }
  }
#endif

  if (file_path.empty())
  {
    file_path = SaveSlotLegacyFile(Model3->GetGame().name, s_saveSlot);
#if defined(SUPERMODEL_OPENGL_ONLY)
    /*
     * Files written before the two executables were given separate save folders still live in
     * Data/Saves. Read them when the new folder has nothing for this slot, so splitting the folder
     * does not hide a player's existing states from the OpenGL build.
     */
    std::error_code existsError;
    if (!std::filesystem::exists(file_path, existsError) || existsError)
    {
      const std::string fallback = SaveSlotLegacyFallback(Model3->GetGame().name, s_saveSlot);
      existsError.clear();
      if (std::filesystem::exists(fallback, existsError) && !existsError)
        file_path = fallback;
    }
#endif
  }

  LoadStateFromFile(Model3, file_path);

  /*
   * The unpacked state is a temporary file - except when a tool asks to keep it.
   *
   * The OpenGL-only executable has no HostCore and cannot read the .zst container, so comparing
   * the two renderers against one save means turning the container into the raw block file it
   * wraps. Tools\Capture-Frame.ps1 -RawState does that by setting this and copying .vktmp.st while
   * the session runs.
   */
  if (!temporary.empty() && std::getenv("SM3D_KEEP_STATE_TEMP") == nullptr)
  {
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
  }
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

/*
 * Present timestamp for the SM3D_FRAME_TIMING probe.
 *
 * EndFrameVideo() runs inside the renderer while the accumulator lives in the game loop, so the two
 * halves talk through these file-scope values. The sequence number tells the loop whether this frame
 * actually reached a present, which matters for frames that skip rendering.
 */
static bool s_frameTiming = false;
static uint64_t s_framePresentNs = 0;
static uint64_t s_framePresentSeq = 0;

bool BeginFrameVideo()
{
#if !defined(SUPERMODEL_OPENGL_ONLY)
  if (s_vkContext)
    return s_vkContext->BeginFrame();
#endif
  return true;
}

void EndFrameVideo()
{
#if !defined(SUPERMODEL_OPENGL_ONLY)
  if (s_vkContext)
  {
    s_vkContext->EndFrame();
  }
  else
#endif
  {
    /*
     * The game's own drawing is finished. The overlay is recorded first so the crosshair can be
     * drawn after it: the crosshair is the one thing that must never be covered, including by the
     * settings panel and the statistics readout. Vulkan reaches the same result inside its renderer,
     * where DrawCrosshairPass() runs after DrawOverlayPass().
     */
    OverlayRenderGL();

    // Show crosshairs for light gun games
#if !defined(SUPERMODEL_PURE_VULKAN)
    if (s_crosshair && videoInputs)
      s_crosshair->Update(currentInputs, videoInputs, xOffset, yOffset, xRes, yRes);
#endif

    OSD::Platform::SwapWindow(s_platformWindow);
  }

  /*
   * The first frame has been presented, so the window is worth showing. Both backends have passed
   * their present call by this point, and the flag makes this happen exactly once per window.
   */
  if (s_windowRevealPending && s_platformWindow != nullptr)
  {
    s_windowRevealPending = false;
    OSD::Platform::ShowWindow(s_platformWindow);
    if (s_windowRevealRaise)
      OSD::Platform::RaiseWindow(s_platformWindow);
    InfoLog("Window: shown once the first frame had been presented.");
  }

  // Timestamped after the present/swap call returns, so poll-to-present includes any blocking the
  // backend did waiting for an image or for vblank. It is not the moment the image is scanned out.
  if (s_frameTiming)
  {
    s_framePresentNs = OSD::Platform::Now();
    s_framePresentSeq++;
  }
}

#if !defined(SUPERMODEL_OPENGL_ONLY)
/*
 * Builds the crosshair state for the Vulkan renderer.
 *
 * Vulkan draws the crosshair natively (see CVulkan2D::SetCrosshair); the OpenGL renderer draws its
 * own. Positions are window pixels mapped through the game viewport, which is the same geometry the
 * OpenGL crosshair uses. Clamping keeps the crosshair visible at the edge when the pointer is pushed
 * against the window border.
 */
static void UpdateOverlayCrosshair(CInputs *Inputs, uint32_t currentInputs, unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, float uiScale)
{
  Vulkan3D::CrosshairState state;
  state.uiScale = uiScale;

  const unsigned crosshairs = s_runtime_config["Crosshairs"].ValueAs<unsigned>() & 3;
  if (s_vkContext && Inputs != nullptr && crosshairs != 0)
  {
    auto set = [&](int player, float nx, float ny)
    {
      nx = (std::max)(0.0f, (std::min)(1.0f, nx));
      ny = (std::max)(0.0f, (std::min)(1.0f, ny));
      state.x[player] = (float)xOffset + nx * (float)xRes;
      state.y[player] = (float)yOffset + ny * (float)yRes;
      state.players |= 1u << player;
    };

    if ((crosshairs & 1) && (currentInputs & Game::INPUT_ANALOG_GUN1))
      set(0, (float)Inputs->analogGunX[0]->ValueAsFraction(),
        (float)Inputs->analogGunY[0]->ValueAsFraction());
    else if ((crosshairs & 1) && (currentInputs & Game::INPUT_GUN1))
      set(0, ((float)Inputs->gunX[0]->value - 150.0f) / (651.0f - 150.0f),
        ((float)Inputs->gunY[0]->value - 80.0f) / (465.0f - 80.0f));

    if ((crosshairs & 2) && (currentInputs & Game::INPUT_ANALOG_GUN2))
      set(1, (float)Inputs->analogGunX[1]->ValueAsFraction(),
        (float)Inputs->analogGunY[1]->ValueAsFraction());
    else if ((crosshairs & 2) && (currentInputs & Game::INPUT_GUN2))
      set(1, ((float)Inputs->gunX[1]->value - 150.0f) / (651.0f - 150.0f),
        ((float)Inputs->gunY[1]->value - 80.0f) / (465.0f - 80.0f));

    state.visible = state.players != 0;

    /*
     * Light-gun probe (SM3D_GUN_PROBE=1).
     *
     * The crosshair is the only thing that reads the axis values at this point in the frame; the
     * game reads them again just before the simulation step. Printing both the raw value and the
     * pixel the crosshair was given makes an offset between the two sides measurable instead of
     * something inferred from a screenshot.
     */
    static const bool logGun = (std::getenv("SM3D_GUN_PROBE") != nullptr);
    static uint64_t lastGunLog = 0;
    if (logGun && (lastGunLog == 0 || OSD::Platform::Now() - lastGunLog > 1000000000ull))
    {
      lastGunLog = OSD::Platform::Now();
      InfoLog("GunProbe: axis X %u (%.4f), Y %u (%.4f); viewport %ux%u at %u,%u -> crosshair %.1f,%.1f.",
        (unsigned)Inputs->analogGunX[0]->value, Inputs->analogGunX[0]->ValueAsFraction(),
        (unsigned)Inputs->analogGunY[0]->value, Inputs->analogGunY[0]->ValueAsFraction(),
        xRes, yRes, xOffset, yOffset, state.x[0], state.y[0]);
    }
  }

  if (s_vk2dRenderer)
    s_vk2dRenderer->SetCrosshair(state);
}
#endif


/******************************************************************************
 Frame Timing
******************************************************************************/

static uint64_t GetDesiredFramePeriodNs()
{
  // Derived from the rate itself rather than from whole millihertz. Rounding 57.52416 Hz to
  // 57,524 mHz yields 17,384,048 ns, which is 2.8 ppm longer than the emulator's own step
  // (17,384,000 ns) - that is exactly the slow drift the arcade preset used to have, and it would
  // come back through this door. The configured range keeps the rate positive; the guard is for a
  // hand-edited file.
  const float refreshRateHz = std::abs(s_runtime_config["RefreshRate"].ValueAs<float>());
  if (!(refreshRateHz > 0.0f))
    return OSD::Platform::kNanosecondsPerSecond / 60;
  return (uint64_t)((double)OSD::Platform::kNanosecondsPerSecond / (double)refreshRateHz);
}

/*
 * The rate the window's display is scanning at right now.
 *
 * GetDesktopDisplayMode() would report the desktop's mode, which is correct for
 * borderless but wrong in exclusive fullscreen - there the panel is on the mode
 * the session selected, and it need not match the desktop. The pacer asks for
 * this instead. Returns 0 when the mode cannot be read, which makes Auto fall
 * back to 60 Hz rather than to a divide by zero.
 */
static double GetCurrentPanelRefreshHz()
{
  if (s_platformWindow == nullptr)
    return 0.0;
  OSD::Platform::DisplayMode mode = {};
  if (!OSD::Platform::GetCurrentDisplayMode(s_platformWindow, &mode))
    return 0.0;
  return mode.refreshHz > 1.0f ? (double)mode.refreshHz : 0.0;
}

// True when the ini, a [game] section or the command line named RefreshRateMode. Set while the
// arguments are processed; see ResolveConfiguredRefreshRateMode() for why it matters.
static bool s_refreshModeExplicit = true;

static RefreshRateMode GetConfiguredRefreshRateMode()
{
  const int mode = s_runtime_config["RefreshRateMode"].ValueAsDefault<int>(1);
  switch (mode)
  {
  case 0: return RefreshRateMode::Original;
  case 2: return RefreshRateMode::Fixed;
  case 3: return RefreshRateMode::Vrr2x;
  case 4: return RefreshRateMode::Vrr3x;
  case 5: return RefreshRateMode::Vrr4x;
  case 6: return RefreshRateMode::Vrr8x;
  default: return RefreshRateMode::Auto;
  }
}

/*
 * The panel rate the pacer is allowed to see.
 *
 * PaceAssumePanelHz exists for testing only: the multiples have hard rate
 * requirements (2x needs 120 Hz, 4x needs 235, 8x needs 470), and a machine with
 * one panel cannot exercise the fallback paths otherwise. 0 means "ask the
 * display", which is what every shipped configuration uses.
 */
static double GetEffectivePanelRefreshHz()
{
  const double assumed = s_runtime_config["PaceAssumePanelHz"].ValueAsDefault<float>(0.0f);
  if (assumed > 1.0)
    return assumed;
  return GetCurrentPanelRefreshHz();
}

/*
 * Whether this display can present at N times the arcade rate.
 *
 * The rule lives in DisplayPacer.h so that the settings window greys out exactly
 * what this refuses; see PanelSupportsPhaseCount there.
 */
static bool PanelSupportsPhaseMode(RefreshRateMode mode, double panelHz)
{
  return PanelSupportsPhaseCount(PhaseCountForMode(mode), panelHz);
}

/*
 * The requested mode, or the fastest phase mode the display can actually take.
 *
 * Falling back rather than refusing keeps a configuration written on one machine
 * usable on another: a user who picks 4x and moves the game to a 120 Hz panel
 * gets 2x and a log line, not a black screen or a silent reversion to 60 Hz.
 */
static RefreshRateMode ResolvePhaseModeForPanel(RefreshRateMode requested, double panelHz)
{
  if (PhaseCountForMode(requested) <= 0)
    return requested;
  if (PanelSupportsPhaseMode(requested, panelHz))
    return requested;

  static const RefreshRateMode kCandidates[] = {
    RefreshRateMode::Vrr8x, RefreshRateMode::Vrr4x,
    RefreshRateMode::Vrr3x, RefreshRateMode::Vrr2x
  };
  for (RefreshRateMode candidate : kCandidates)
  {
    if (PhaseCountForMode(candidate) < PhaseCountForMode(requested) &&
        PanelSupportsPhaseMode(candidate, panelHz))
      return candidate;
  }

  return RefreshRateMode::Original;
}

/*
 * Whether the display really offers an arcade-rate fullscreen mode, cached.
 *
 * Enumerating a display's modes is not free and the answer only changes when the output does, so
 * the pacing block - which re-resolves the mode every frame - cannot ask this directly.
 */
static bool ArcadeDisplayModeAvailableCached()
{
  static bool fetched = false;
  static bool available = false;
  static int checkedDisplay = -1;

  const int display = GetConfiguredDisplaySdlIndex();
  if (!fetched || display != checkedDisplay)
  {
    fetched = true;
    checkedDisplay = display;
    available = HasArcadeDisplayMode(57.5242f);
  }
  return available;
}

/*
 * The mode the pacer should run, after the automatic rules.
 *
 * Auto means "follow the display". Two cases where that is not what the player wants:
 *
 *   - the display is already running at the arcade rate (a native 57/58 Hz mode, or VRR following
 *     the emulator). Presenting at 57.524 Hz with interpolation would then hold the picture half a
 *     frame behind for nothing, so it becomes Original.
 *
 *   - nothing in the configuration names a mode (a fresh installation) and the display offers a
 *     mode at the arcade rate. Presenting at it is the arcade original's own cadence: every
 *     emulated frame shown exactly once, no interpolation. Only a real mode counts, not an EDID
 *     range that happens to contain 57.5 - a panel that merely lists it still scans at 60 unless
 *     such a mode is selected, and 57.5 Hz into a 60 Hz scan judders more than interpolating.
 */
static RefreshRateMode ResolveConfiguredRefreshRateMode(double panelHz)
{
  RefreshRateMode mode = GetConfiguredRefreshRateMode();

  if (mode == RefreshRateMode::Auto && panelHz > 1.0 &&
      std::fabs(panelHz - kModel3StepHz) <= kModel3StepHz * 0.005)
  {
    static bool reported = false;
    if (!reported)
    {
      reported = true;
      InfoLog("Pace: the display is running at %.3f Hz, the arcade rate; using Original presentation.", panelHz);
    }
    return RefreshRateMode::Original;
  }

  if (mode == RefreshRateMode::Auto && !s_refreshModeExplicit && ArcadeDisplayModeAvailableCached())
  {
    static bool reported = false;
    if (!reported)
    {
      reported = true;
      InfoLog("Pace: the display offers a 57.524 Hz mode and no refresh rate mode was chosen; using Original.");
    }
    return RefreshRateMode::Original;
  }

  return mode;
}

/*
 * Logs a fallback once per distinct (requested, resolved) pair.
 *
 * The mode is re-resolved every frame so that plugging in a different display is
 * noticed immediately, and a fallback is exactly the state that persists - so
 * the message has to be emitted on change rather than per evaluation.
 */
static void ReportPaceFallback(RefreshRateMode requested, RefreshRateMode resolved, double panelHz)
{
  if (resolved == requested)
    return;

  static int lastRequested = -1;
  static int lastResolved = -1;
  if (lastRequested == (int)requested && lastResolved == (int)resolved)
    return;
  lastRequested = (int)requested;
  lastResolved = (int)resolved;

  InfoLog("Pace: %s (%.2f Hz) does not fit this display's %.1f Hz; presentation falls back to %s.",
    RefreshRateModeName(requested), kModel3StepHz * PhaseCountForMode(requested),
    panelHz, RefreshRateModeName(resolved));
}

/*
 * Highest whole refresh rate the configured output advertises.
 *
 * The desktop mode is not enough: exclusive fullscreen can select a mode the
 * desktop is not running, which is exactly how a 120/144 Hz panel is usually
 * driven. The fullscreen mode list is therefore the authority, and the desktop
 * rate is only a fallback when the driver reports no modes at all. Cached per
 * output because enumerating modes allocates and the answer cannot change while
 * a display is attached.
 *
 * Rates are rounded to the nearest whole Hz on purpose: SDL reports 59.94 and
 * 119.88 for panels everyone calls 60 and 120, and refusing those entries would
 * grey out the only rate the panel can actually take.
 */
static int MaxSupportedRefreshHz()
{
  static int fetchedDisplay = -2;
  static int maxHz = 0;

  const int displayIndex = GetConfiguredDisplaySdlIndex();
  if (displayIndex == fetchedDisplay)
    return maxHz;

  fetchedDisplay = displayIndex;
  maxHz = 0;
  const OSD::Platform::DisplayId display = OSD::Platform::GetDisplayIdForIndex(displayIndex);
  const int modeCount = OSD::Platform::GetDisplayModeCount(display);
  for (int i = 0; i < modeCount; ++i)
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

  InfoLog("Pace: display %d reports modes up to %d Hz; fixed-rate choices above that are refused.",
    displayIndex, maxHz);
  return maxHz;
}

static double GetConfiguredFixedRefreshHz()
{
  const int configured = s_runtime_config["FixedRefreshRate"].ValueAsDefault<int>(120);
  const int requested = configured > 1 ? configured : 60;

  /*
   * Refuse a rate this display never scans. The settings window greys the same
   * choices out and clamps the saved value at start-up; doing it here as well
   * covers a [game] section, a command line or a value carried over from another
   * machine without relying on the interface having run first.
   */
  const int clamped = ClampFixedRefreshRate(requested, MaxSupportedRefreshHz());
  if (clamped != requested)
  {
    static int lastRequested = -1;
    static int lastClamped = -1;
    if (lastRequested != requested || lastClamped != clamped)
    {
      lastRequested = requested;
      lastClamped = clamped;
      InfoLog("Pace: fixed %d Hz is above this display's maximum; using %d Hz instead.",
        requested, clamped);
    }
    return (double)clamped;
  }
  return (double)requested;
}

static bool GetConfiguredInterpolation()
{
  return s_runtime_config["MotionInterpolation"].ValueAsDefault<bool>(true)
    && !s_runtime_config["LowLatency"].ValueAsDefault<bool>(false);
}

static void SuperSleepUntil(const uint64_t target)
{
  OSD::Platform::SleepUntil(static_cast<OSD::Platform::Nanoseconds>(target));
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

#if defined(SUPERMODEL_OPENGL_ONLY)
  // The file is written back wholesale, so a key left over from the dual-renderer
  // build would survive every "Apply" in the in-game panel.  Remove them before
  // anything is merged into it.
  Util::Config::StripRetiredVulkanSettings(fileConfig);
#endif

  // Only the keys the panel offers are taken from the runtime configuration, so an Apply cannot
  // quietly persist an unrelated change that something else made at run time.
  // Keep in step with kOverlayKeys in Overlay.cpp: that list is what Cancel restores, this one is
  // what Apply writes back.
  static const char* const keys[] = {
    "CRTcolors", "UpscaleMode",
    // Vulkan2D's light-gun border pass.
    "SindenBorder", "SindenBorderWidth", "SindenBorderBrightness",
    "WideScreen",
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

/*
 * Optional read-only watchdog for save-state load diagnosis.
 *
 * Inert unless SM3D_LOAD_DEBUG is set. It exists because the interesting failure is a hang: the
 * main thread stops inside EndSimulationFrame() and the periodic PACE line never appears again, so
 * a log written only at load time cannot say whether the PPC is spinning. This thread polls the
 * emulator once a second, which keeps producing lines while the main loop is stuck.
 *
 * The destructor stops the thread on every normal return from Supermodel(). If the process is hard
 * killed instead, the thread dies with it; the log lines already written are what matter.
 */
class LoadDebugWatchdog
{
public:
  explicit LoadDebugWatchdog(IEmulator *emulator) : m_emulator(emulator)
  {
    const char *enabled = std::getenv("SM3D_LOAD_DEBUG");
    if (enabled == nullptr || *enabled == '\0')
      return;

    m_thread = std::thread([this]() {
      while (!m_stop.load())
      {
        // Ten short sleeps rather than one long one, so shutdown is not delayed by a second.
        for (int i = 0; i < 10 && !m_stop.load(); ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (m_stop.load())
          break;

        const std::string snapshot = m_emulator->DebugSnapshot("watchdog");
        if (!snapshot.empty())
          InfoLog("%s", snapshot.c_str());
      }
    });
  }

  ~LoadDebugWatchdog()
  {
    m_stop.store(true);
    if (m_thread.joinable())
      m_thread.join();
  }

  LoadDebugWatchdog(const LoadDebugWatchdog &) = delete;
  LoadDebugWatchdog &operator=(const LoadDebugWatchdog &) = delete;

private:
  IEmulator *m_emulator = nullptr;
  std::atomic<bool> m_stop{false};
  std::thread m_thread;
};

#ifdef SUPERMODEL_DEBUGGER
int Supermodel(const Game &game, ROMSet *rom_set, IEmulator *Model3, CInputs *Inputs, COutputs *Outputs, std::shared_ptr<Debugger::CDebugger> Debugger)
{
  std::shared_ptr<CLogger> oldLogger;
#else
int Supermodel(const Game &game, ROMSet *rom_set, IEmulator *Model3, CInputs *Inputs, COutputs *Outputs)
{
#endif // SUPERMODEL_DEBUGGER
  std::string initialState = s_runtime_config["InitStateFile"].ValueAs<std::string>();
  // Whether s_saveSlot already describes the state this session starts from.
  bool slotKnown = false;
#if defined(SUPERMODEL_PURE_VULKAN)
  std::string deferredState;
  /*
   * How long after the session starts the pending state is applied.
   *
   * A save state may only be dropped into a machine that has finished its power-on initialisation.
   * Applied to a machine that is still in its boot ROM (one second in, the old value), Virtua Fighter
   * 3 Team Battle resumes and then runs at about one twenty-fifth of its normal speed: 0.3 s of game
   * time in 8 s of wall clock, measured against 7.7 s for the same state applied three seconds in.
   * Three seconds of emulated machine time is where the boot handover is comfortably complete here.
   *
   * The wait is counted in emulated frames rather than wall-clock time, so a machine that cannot keep
   * up does not shorten the boot the state is waiting for. SM3D_DEFER_LOAD_SECONDS overrides it for
   * diagnostics (0 = apply immediately, before the first frame).
   */
  constexpr double kDeferredLoadSeconds = 3.0;
  double deferredLoadSeconds = kDeferredLoadSeconds;
  if (const char *delay = std::getenv("SM3D_DEFER_LOAD_SECONDS"))
  {
    const double parsed = std::atof(delay);
    if (parsed >= 0.0)
      deferredLoadSeconds = parsed;
  }
  const uint64_t deferredLoadFrames = (uint64_t)(deferredLoadSeconds * kModel3RefreshHz);
  uint64_t simFramesThisSession = 0;
  // True while the start-up artwork is covering the boot sequence the pending state waits out.
  bool deferredLoadSplash = false;

  /*
   * A save chosen in the settings window's Manage Saves dialog arrives as a
   * .zst path (or a legacy .st). It is applied from the frame loop once the
   * machine has booted, with the same PauseThreads/LoadState/ResumeThreads
   * sequence F7 uses; see kDeferredLoadSeconds for why the boot has to happen
   * first.
   */
  if (const std::filesystem::path pending = GuiTakePendingVkState(); !pending.empty())
  {
    /*
     * F5 writes to the slot that was loaded, not to the one the hotkeys were left on: the file name
     * carries the slot number, so a session started from "lamachin.zst3" overwrites .zst3 rather
     * than dropping the new state into slot 1 alongside it.
     */
    if (const int slot = SaveSlotFromPath(pending); slot >= 0 && slot <= 99)
    {
      s_saveSlot = (unsigned)slot;
      slotKnown = true;
    }

    deferredState = pending.string();
    InfoLog("Manage saves: deferring load of '%s' until the game is running.",
      pending.string().c_str());
  }

  /*
   * -load-state goes through the same deferred path as Manage Saves → Load.
   *
   * It used to be applied before the first frame, with one frame run first so that gpusReady and the
   * read-only snapshots existed. That put the state into a machine that had not booted yet, which is
   * the case the delay exists to avoid (see kDeferredLoadSeconds); the pre-loop frame is no longer
   * needed because the machine now boots normally first. The GUI's own pending state wins if both
   * are present.
   */
  if (!initialState.empty() && deferredState.empty())
  {
    deferredState = initialState;
    InfoLog("Init state: deferring load of '%s' until the game has started.", initialState.c_str());
  }

#ifdef _WIN32
  /*
   * The machine is about to spend the next three seconds booting, and none of that is what the
   * player asked to see. Cover the whole monitor with the start-up artwork - the same picture the
   * settings window shows while its own renderer comes up - and take it down once the state has
   * been applied. It is a topmost layered window owned by the game window, so the game window can
   * be revealed underneath it without showing through.
   *
   * Shown here rather than just before the frame loop so that the ROM load and renderer set-up are
   * covered too, which would otherwise be several seconds of black.
   */
  if (!deferredState.empty() && s_platformWindow != nullptr)
  {
    deferredLoadSplash = OSD::Windows::ShowStartupSplash(s_platformWindow, true);
    InfoLog("Save state: covering the boot sequence with the start-up artwork (%s).",
      deferredLoadSplash ? "shown" : "unavailable");
  }
#endif
#endif

  /*
   * Which slot F5 writes to.
   *
   * A session started from a save keeps that save's slot, which is what makes "load, play, F5"
   * overwrite the state the player resumed (the .zst path above already set it; the legacy
   * -load-state path may name a slot too). A session that did not start from a save has no slot to
   * write back to, so it fills the lowest free one instead of reusing whatever slot the previous
   * session left selected - deleting a save leaves a gap that the next new save should take.
   */
  if (!slotKnown)
  {
    const int loadedSlot = initialState.empty() ? -1 : SaveSlotFromPath(initialState);
    s_saveSlot = (loadedSlot >= 0 && loadedSlot <= 99)
      ? (unsigned)loadedSlot
      : NextFreeSaveSlot(game.name, 0);
  }

  // Support-visible: F7 starts on this slot, and on a fresh session it is the first free one.
  InfoLog("Quick-save slot %u (%s).", s_saveSlot,
    SaveSlotOccupied(game.name, s_saveSlot) ? "occupied" : "empty");

  uint64_t    prevFPSTicks;
  unsigned    fpsFramesElapsed;
  bool        gameHasLightguns = false;
  bool        quit = false;
  bool        paused = false;
  // True when the emulator is paused because the in-game panel is open, rather than by Alt+P.
  // Closing the panel resumes only in that case; a manual pause survives it.
  bool        overlayPausedEmulator = false;
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
  // The region belongs in the title: a zip can hold a different revision than its name suggests, and
  // the title bar is the only place the running revision is visible.
  const std::string gameTitle = game.version.empty() ? game.title : game.title + " (" + game.version + ")";
  snprintf(baseTitleStr, sizeof(baseTitleStr), MODEL3VULKAN_TITLE_FULL " - %s", gameTitle.c_str());
  OSD::Platform::SetWindowTitle(s_platformWindow, baseTitleStr);
  OSD::Platform::SetWindowSize(s_platformWindow, totalXRes, totalYRes);

  // Centred on the configured output rather than the primary one, so that windowed mode also opens
  // where the display setting says - otherwise a user who picked a second monitor gets the exclusive
  // modes on it and the window everywhere else. An explicit position still wins.
  //
  // The centring value used to be SDL's own SDL_WINDOWPOS_CENTERED_DISPLAY() sentinel, which leaked
  // an SDL encoding into the configuration default. INT_MIN is the neutral spelling: no real window
  // coordinate can take it, and it means the same thing to both backends.
  constexpr int kWindowPositionCentered = INT_MIN;
  const int configuredDisplay = GetConfiguredDisplaySdlIndex();
  const int xpos = s_runtime_config["WindowXPosition"].ValueAsDefault<int>(kWindowPositionCentered);
  const int ypos = s_runtime_config["WindowYPosition"].ValueAsDefault<int>(kWindowPositionCentered);
  if (xpos == kWindowPositionCentered || ypos == kWindowPositionCentered)
    OSD::Platform::CenterWindowOnDisplay(s_platformWindow, OSD::Platform::GetDisplayIdForIndex(configuredDisplay));
  else
    OSD::Platform::SetWindowPosition(s_platformWindow, xpos, ypos);

  if (s_runtime_config["BorderlessWindow"].ValueAs<bool>())
  {
    OSD::Platform::SetWindowBordered(s_platformWindow, false);
  }

  // Pick the fullscreen flavour (borderless vs exclusive/G-SYNC vs fixed arcade timing) before the
  // resize below applies it.
  s_fullScreenMode = ResolveFullScreenMode();

  bool stretch = s_runtime_config["Stretch"].ValueAs<bool>();
  bool fullscreen = s_runtime_config["FullScreen"].ValueAs<bool>();
#if defined(SUPERMODEL_OPENGL_ONLY)
  Result resized = ResizeGLScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretch, fullscreen);
#elif defined(SUPERMODEL_PURE_VULKAN)
  Result resized = ResizeVulkanScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretch, fullscreen);
#else
  Result resized = s_vkContext
    ? ResizeVulkanScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretch, fullscreen)
    : ResizeGLScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretch, fullscreen);
#endif
  if (Result::OKAY != resized)
    return 1;

  // Info log GL information
#if defined(SUPERMODEL_PURE_VULKAN)
  if (s_vkContext)
    InfoLog("Vulkan 3D engine enabled.");
#elif defined(SUPERMODEL_OPENGL_ONLY)
  PrintGLInfo(false, true, false);
#else
  if (s_vkContext)
    InfoLog("Vulkan 3D engine enabled.");
  else
    PrintGLInfo(false, true, false);
#endif

  /*
   * Which loop this session runs.
   *
   * The high-refresh path needs a Vulkan swapchain to present into from the
   * renderer; the OpenGL backends keep the original one-simulation-one-present
   * loop, including their own frame limiter. This is a runtime decision in the
   * dual binary and a compile-time one elsewhere.
   */
#if !defined(SUPERMODEL_OPENGL_ONLY)
  const bool highRefresh = (s_vkContext != nullptr);
#else
  const bool highRefresh = false;
#endif

  CDisplayPacer pacer;
  {
    const double panelHz = GetEffectivePanelRefreshHz();
    const RefreshRateMode requested = ResolveConfiguredRefreshRateMode(panelHz);
    const RefreshRateMode resolved = ResolvePhaseModeForPanel(requested, panelHz);
    ReportPaceFallback(requested, resolved, panelHz);

    DisplayPacerConfig pacerConfig;
    pacerConfig.mode = resolved;
    pacerConfig.fixedHz = GetConfiguredFixedRefreshHz();
    pacerConfig.panelHz = panelHz;
    pacerConfig.vsync = s_runtime_config["VSync"].ValueAsDefault<bool>(true);
    pacerConfig.throttle = s_runtime_config["Throttle"].ValueAsDefault<bool>(true);
    pacerConfig.interpolation = GetConfiguredInterpolation();
    if (highRefresh)
      pacer.Configure(pacerConfig, OSD::Platform::Now());
  }

  // Initialize audio system
  SetAudioType(game.audio);
  if (Result::OKAY != OpenAudio(s_runtime_config))
    return 1;

  // Hide mouse if fullscreen, enable crosshairs for gun games
  Inputs->GetInputSystem()->SetMouseVisibility(!s_runtime_config["FullScreen"].ValueAs<bool>());
  gameHasLightguns = !!(game.inputs & (Game::INPUT_GUN1|Game::INPUT_GUN2|Game::INPUT_ANALOG_GUN1|Game::INPUT_ANALOG_GUN2));

  // Keep the simulated gun cursor on the game window while playing. SDL reports the pointer
  // relative to the focused window, so without the grab it can wander onto another monitor and
  // the crosshair maps outside the view. SDL releases the grab itself when the window loses
  // focus; RunGameSession drops it explicitly on the way out.
  if (gameHasLightguns)
  {
    OSD::Platform::SetWindowMouseGrab(s_platformWindow, true);
    InfoLog("Input: mouse confined to the game window for light gun aiming.");
  }

  currentInputs = game.inputs;
  if (gameHasLightguns)
    videoInputs = Inputs;
  else
    videoInputs = nullptr;

  // Gun games get a player 1 crosshair by default so the mouse fallback is visible without
  // any setup. Player 2 is left off: the SDL backend exposes a single mouse, and an unmapped P2
  // gun would otherwise leave a stationary crosshair in the middle of the screen.
  if (gameHasLightguns && s_runtime_config["Crosshairs"].ValueAs<unsigned>() == 0)
  {
    s_runtime_config.Get("Crosshairs").SetValue(1);
    InfoLog("Crosshairs: enabled for player 1 light gun.");
  }

  // Attach the inputs to the emulator
  Model3->AttachInputs(Inputs);

  // Attach the outputs to the emulator
  if (Outputs != nullptr)
    Model3->AttachOutputs(Outputs);

  // Frame timing
  uint64_t perfCountPerFrame = GetDesiredFramePeriodNs();
  // Absolute target for the next frame. This is advanced by exactly one frame period
  // each time (see the main loop) instead of being rebased on the current time.
  uint64_t nextTime = OSD::Platform::Now() + perfCountPerFrame;

  // Initialize the renderers
  SuperAA* superAA = nullptr;
  IRender2D *Render2D = nullptr;
  IRender3D *Render3D = nullptr;
  UpscaleMode upscaleMode = (UpscaleMode)s_runtime_config["UpscaleMode"].ValueAs<int>();
  unsigned aaTarget = 0;
#if !defined(SUPERMODEL_OPENGL_ONLY)
  if (s_vkContext)
  {
    if (aaValue != 1)
    {
      InfoLog("Vulkan: supersampling is not available in this backend; using 1x.");
      aaValue = 1;
    }
    Vulkan3D::CVulkan2D* vk2d = new Vulkan3D::CVulkan2D(s_runtime_config, *s_vkContext);
    s_vk2dRenderer = vk2d;
    Render2D = vk2d;
    s_vk3dRenderer = new Vulkan3D::CVulkan3D(s_runtime_config, Model3->GetGame().name, *s_vkContext, *vk2d);
    Render3D = s_vk3dRenderer;
  }
  else
#endif
#if !defined(SUPERMODEL_PURE_VULKAN)
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
#else
  {
    // Pure-Vulkan build only: the context cannot be null once a session is running.
  }
#endif

  /*
   * Init() is where the pipelines are actually created, and that is the cost the on-disk pipeline
   * cache is meant to remove: from cold, every shader in the backend is compiled on every start.
   */
  const uint64_t rendererInitStart = OSD::Platform::Now();
  if (Result::OKAY != Render2D->Init(xOffset*aaValue, yOffset*aaValue, xRes*aaValue, yRes*aaValue, totalXRes*aaValue, totalYRes*aaValue, aaTarget, upscaleMode))
    goto QuitError;
  if (Result::OKAY != Render3D->Init(xOffset*aaValue, yOffset*aaValue, xRes*aaValue, yRes*aaValue, totalXRes*aaValue, totalYRes*aaValue, aaTarget))
    goto QuitError;
#if !defined(SUPERMODEL_OPENGL_ONLY)
  if (s_vkContext)
    InfoLog("Vulkan: pipelines ready (%.0f ms).", (OSD::Platform::Now() - rendererInitStart) / 1e6);
#endif

  Model3->AttachRenderers(Render2D,Render3D, superAA);

  // Tells the statistics readout what this session is really rendering at, and with what
  // antialiasing. Done here because this is the only point where every input is known: the effective
  // supersampling factor (Vulkan has just forced it to 1), the window size, and - for Vulkan - the
  // sample count the driver actually granted.
  ReportRenderInfo(aaValue, xRes, yRes, totalXRes, totalYRes);

  /*
   * C++23 (unlike the C++17 mode MSVC used to accept this in) rejects a goto
   * that jumps over an initialized declaration in the same scope.  The two
   * QuitError branches above and the ones inside the frame loop all target the
   * label below it, so the loop and its locals live in their own scope:
   * leaving a block with goto is well-formed, jumping over its initializers is
   * not.  Nothing is reordered, and the cleanup at QuitError sees exactly the
   * same objects as before.
   */
  {
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
  float overlayScale = 1.0f;

  /*
   * Match the in-game panel to the interface theme the settings window is using.
   *
   * The overlay owns a second ImGui context and applies the theme when it is created, so this has to
   * be set before OverlayInit()/OverlayInitGL() - and it is why a theme change takes effect in the
   * panel from the next session rather than immediately. 0 = light, 1 = dark, 2 = the pre-rework
   * interface; anything else reads as the default rather than leaving the window unstyled.
   */
  {
    const int themeValue = s_runtime_config["UITheme"].ValueAsDefault<int>(0);
    SetUiTheme(themeValue == 1 ? UiTheme::WarmDark
      : (themeValue == 2 ? UiTheme::Classic : UiTheme::WarmPaper));
  }
  {
#ifdef _WIN32
    if (HDC screen = GetDC(nullptr))
    {
      overlayScale = (float)GetDeviceCaps(screen, LOGPIXELSY) / 96.0f;
      ReleaseDC(nullptr, screen);
    }
#endif
    if (overlayScale < 1.0f) overlayScale = 1.0f;
    if (overlayScale > 2.5f) overlayScale = 2.5f;

#if !defined(SUPERMODEL_OPENGL_ONLY)
    if (s_vkContext)
    {
      if (OverlayInit(*s_vkContext, s_platformWindow, overlayScale))
      {
        Vulkan3D::CVulkan2D* renderer2D = static_cast<Vulkan3D::CVulkan2D*>(Render2D);
        renderer2D->SetOverlayPass(&OverlayRender);
        /*
         * Frosted glass. The gate is asked once per frame and decides whether the blur chain runs
         * at all, which is what keeps a session that never opens the panel at exactly zero cost; the
         * source hands the overlay the half-resolution result to register with its own ImGui context.
         */
        renderer2D->SetUiFrostGate(&OverlayWantsFrost);
        s_overlayFrostRenderer = renderer2D;
        OverlaySetFrostTextureSource(&OverlayFrostTextureFromRenderer);
      }
    }
    else
#endif
    {
      // Requires the game window's context to be current, which CreateGLScreen left it as.
      OverlayInitGL(s_platformWindow, overlayScale);
    }

    /*
     * The readout's initial state comes from the settings file, so it survives a restart; the key
     * still turns it on and off while a game runs.
     *
     * Set after the overlay is initialised because a readout with no ImGui backend behind it would be
     * enabled and invisible - see the note on StatsOsdEnabled in Overlay.cpp.
     */
    StatsOsdSetWindow(s_platformWindow);

    /*
     * The renderer is named from what is actually running, not from the configuration.
     *
     * The two come apart in a way worth guarding against: selecting Vulkan on a driver that cannot
     * provide it falls back to OpenGL, and a readout that repeated the configuration would name a
     * backend that is not the one drawing the picture. Which renderer this session got is known here
     * and nowhere else, so it is decided here.
     */
#if defined(SUPERMODEL_OPENGL_ONLY)
    StatsOsdSetRenderer(s_runtime_config["New3DEngine"].ValueAs<bool>()
      ? StatsRenderer::New3D : StatsRenderer::Legacy3D);
#elif defined(SUPERMODEL_PURE_VULKAN)
    StatsOsdSetRenderer(StatsRenderer::Vulkan3D);
#else
    StatsOsdSetRenderer(s_vkContext ? StatsRenderer::Vulkan3D
      : (s_runtime_config["New3DEngine"].ValueAs<bool>() ? StatsRenderer::New3D : StatsRenderer::Legacy3D));
#endif

    /*
     * The GPU's name for the readout, from the context that actually opened the device.
     *
     * Only Vulkan can supply one; the OpenGL backends leave the line unnamed rather than guess from
     * a device string they never asked for. NVML overrides this with its own name on NVIDIA machines.
     */
#if !defined(SUPERMODEL_OPENGL_ONLY)
    if (s_vkContext)
      StatsOsdSetGpuName(s_vkContext->DeviceName().c_str());
#endif

    StatsOsdApplyConfig(s_runtime_config["ShowStatsOsd"].ValueAsDefault<bool>(false));
  }

  // Reset emulator
  Model3->Reset();

  // Starts only when SM3D_LOAD_DEBUG is set; otherwise this constructor does nothing.
  LoadDebugWatchdog loadDebugWatchdog(Model3);

#if defined(SUPERMODEL_PURE_VULKAN)
  // The pending state was set up at the top of the function; nothing to do here on this build.
#else
  // Load initial save state if requested
  if (!initialState.empty())
    LoadState(Model3, initialState);
#endif

#if defined(SUPERMODEL_PURE_VULKAN)
  // Leaving the game while a save is still waiting for its thumbnail: finish it now rather than
  // losing it with the session.
  if (s_pendingSave.active)
  {
    s_pendingSave.framesWaited = 30;
    ServicePendingSave();
  }
#endif

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
  prevFPSTicks = OSD::Platform::Now();
  quit = false;
  paused = false;
  overlayPausedEmulator = false;
  dumpTimings = false;
#ifdef DEBUG
  if (dynamic_cast<CModel3GraphicsState *>(Model3))
  {
#if !defined(SUPERMODEL_PURE_VULKAN)
    TestPolygonHeaderBits(Model3);
#endif
    quit = true;
  }
#endif

  // Re-assert the priority here. Setting it once at start-up is not enough: the class is reset
  // somewhere during initialisation, so it has to be applied again before the frame loop begins.
  if (s_runtime_config["HighPriority"].ValueAsDefault<bool>(true))
    RaiseProcessPriority();

  // Keep the game running while the window is dragged (SDL2), or pause it there (SDL3); see
  // ModalLoopMessageHook / ModalLoopPauseHook.
  SetModalLoopHook(Model3, &paused);

  /*
   * Save-state menu handlers for the in-game overlay.
   *
   * Registered here, where both the emulator and the paused flag are in scope, and cleared after the
   * loop so a callback cannot outlive the session. The list is built from the same files the F5/F7
   * hotkeys use, so the menu and the hotkeys always agree on the slot count and numbering.
   */
  {
    OverlaySaveHandlers saveHandlers;
    saveHandlers.list = [Model3]() {
      std::vector<OverlaySaveSlot> slots;
      const std::string game = Model3->GetGame().name;
      for (unsigned slot = 1; slot <= SAVE_SLOT_COUNT; ++slot)
      {
        OverlaySaveSlot info;
        info.slot = slot;
        info.active = (slot == s_saveSlot);

        // A slot normally holds the container; the classic name is kept as the fallback so states
        // written before it existed are still listed and still load.
        std::string path = SaveSlotLegacyFile(game, slot);
        bool exists = false;
        std::error_code ec = {};
        exists = std::filesystem::exists(path, ec) && !ec;
#if defined(SUPERMODEL_OPENGL_ONLY)
        /*
         * The OpenGL build used to share Data/Saves with the Vulkan build. Keep those old .st files
         * visible in the panel (and therefore selectable) even though new saves go to Saves_Legacy.
         */
        if (!exists)
        {
          const std::string fallback = SaveSlotLegacyFallback(game, slot);
          std::error_code fallbackError = {};
          if (std::filesystem::exists(fallback, fallbackError) && !fallbackError)
          {
            path = fallback;
            exists = true;
          }
        }
#endif
#if defined(SUPERMODEL_PURE_VULKAN)
        {
          const std::string container = SaveSlotContainer(game, slot);
          std::error_code containerError = {};
          if (std::filesystem::exists(container, containerError) && !containerError)
          {
            path = container;
            exists = true;
          }
        }
#endif
		info.exists = exists;
		info.path = path;
        if (exists)
        {
          const auto written = std::filesystem::last_write_time(path, ec);
          if (!ec)
          {
            info.revision = (std::uint64_t)written.time_since_epoch().count();
            const auto age = std::filesystem::file_time_type::clock::now() - written;
            info.ageSeconds = std::chrono::duration<double>(age).count();
            if (info.ageSeconds < 0.0)
              info.ageSeconds = 0.0;
          }
        }
        slots.push_back(info);
      }
      return slots;
    };
#if defined(SUPERMODEL_PURE_VULKAN)
    saveHandlers.apply = [Model3, &paused, &deferredState, &deferredLoadSplash](unsigned slot, bool save) {
#else
    saveHandlers.apply = [Model3, &paused](unsigned slot, bool save) {
#endif
      // The same pause protocol the F5/F7 hotkeys use. When the emulator is already paused there is
      // nothing to do: the threads are stopped and the state is stable.
      if (!paused)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
      }

      s_saveSlot = slot;
      if (save)
      {
        SaveState(Model3);
      }
      else
      {
#if defined(SUPERMODEL_PURE_VULKAN)
        // The Tab panel's Load is a manual load; it wins over a deferred Manage Saves load.
        deferredState.clear();
#ifdef _WIN32
        if (deferredLoadSplash)
        {
          deferredLoadSplash = false;
          OSD::Windows::HideStartupSplash();
        }
#endif
#endif
        InfoLog("Overlay: loading save slot %u.", s_saveSlot);
        LoadState(Model3);
#ifdef SUPERMODEL_DEBUGGER
        if (Debugger != NULL)
          Debugger->Reset();
#endif // SUPERMODEL_DEBUGGER
      }

      if (!paused)
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
      }
    };
    OverlaySetSaveHandlers(std::move(saveHandlers));
  }

  /*
   * Test hook: open the in-game panel shortly after a session starts.
   *
   * Deferred rather than immediate. Opening it at frame zero pauses the emulator before it has
   * rendered anything, and the window then shows a black frame with no panel in it - so the hook
   * would photograph exactly nothing. The delay is counted in main-loop iterations, which is the
   * only tick available here that does not depend on a debug switch.
   */
  int overlayOpenCountdown = std::getenv("SM3D_OVERLAY_OPEN") != nullptr ? 120 : -1;

  /*
   * Frame timing, off unless SM3D_FRAME_TIMING is in the environment.
   *
   * Two figures, averaged over one second: the ImGui frame the overlay builds (ui), and the gap from
   * the input poll to the moment the frame is handed to the emulator for rendering (poll-to-render).
   * The second is the part that matters for a light gun: it is time the player's aim spends waiting
   * before the frame it belongs to is submitted.
   *
   * Gated by an environment variable rather than compiled out, so the same release binary can be
   * measured before and after a change. When it is not set the loop pays one branch per frame.
   */
  const bool timing = std::getenv("SM3D_FRAME_TIMING") != nullptr;
  s_frameTiming = timing;
  uint64_t timingFrames = 0;
  uint64_t timingOverlayNs = 0;
  uint64_t timingRunNs = 0;
  uint64_t timingGapNs = 0;
  uint64_t timingPresentNs = 0;
  uint64_t timingPresentFrames = 0;
  uint64_t timingNextReport = OSD::Platform::Now() + OSD::Platform::kNanosecondsPerSecond;

  /*
   * Whether the picture handed to the swapchain reached the screen, from VK_KHR_present_wait.
   *
   * Counted over the same one-second window as the rest of the probe. Not a rate and not a fault
   * when it is low: a loop that prepares the next frame while the previous one is still queued
   * is pipelining normally. It is the figure that says whether the compositor consumed what it
   * was given, which is otherwise invisible from in here.
   */
  uint64_t displayAttempts = 0;
  uint64_t displayConfirms = 0;
  uint64_t displayLate = 0;

  /*
   * Present phase calibration. The first presents after the pacer is configured block on the scan-out
   * so its distance from the deadline can be measured; thereafter the wait costs nothing. Reset
   * whenever the pacer is reconfigured, because a new mode or panel has a different vblank phase.
   */
  int presentCalibrationFrames = 16;
  int64_t presentLeadSum = 0;
  int presentLeadSamples = 0;

  /*
   * Sleep-lateness samples for the same probe: how far past the deadline the frame loop
   * actually woke, in nanoseconds. A fixed-size sliding window (about ten seconds at 60 fps)
   * gives a meaningful p99 without allocating per frame.
   */
  constexpr size_t kSleepLateSamples = 600;
  int64_t sleepLate[kSleepLateSamples] = {};
  size_t sleepLateCount = 0;
  size_t sleepLateNext = 0;

  // Last pacing values applied, so an edit in the in-game panel is noticed
  // without rebuilding the pacer (and discarding the accumulator) every frame.
  int lastPacerMode = (int)ResolvePhaseModeForPanel(GetConfiguredRefreshRateMode(), GetEffectivePanelRefreshHz());
  int lastPacerFixed = (int)GetConfiguredFixedRefreshHz();
  bool lastPacerInterp = GetConfiguredInterpolation();
  OSD::Platform::Nanoseconds nextPanelCheck = OSD::Platform::Now() + OSD::Platform::kNanosecondsPerSecond;
  OSD::Platform::Nanoseconds nextPaceLog = OSD::Platform::Now() + 5 * OSD::Platform::kNanosecondsPerSecond;
  uint64_t perfSimNs = 0;
  uint64_t perfRenderNs = 0;
  uint64_t perfSubNs = 0;
  uint64_t perfSubFrames = 0;
  uint64_t perfFrames = 0;

  /*
   * Developer hook: SM3D_SCREENSHOT_AT=<seconds> requests one screenshot that many seconds into the
   * session.
   *
   * The UI hotkey needs the game window to have keyboard focus. A script that starts the emulator
   * does not get it (Windows' foreground lock keeps the launching shell in front), and an exclusive
   * fullscreen session cannot hand it over at all - so screenshots for bug reports and A/B
   * comparisons need a path that does not depend on focus. The clock starts here rather than at
   * process start, so the delay means "after the state is loaded and the first frame is up".
   */
  const char *screenshotAt = std::getenv("SM3D_SCREENSHOT_AT");
  const auto screenshotClock = std::chrono::steady_clock::now();
  bool screenshotTaken = false;

  /*
   * Developer hook: SM3D_SCREENSHOT_FRAME=<n> is the same request counted in emulated frames.
   *
   * The seconds hook is wall clock, and start-up overhead differs by a few milliseconds between
   * runs - which is several emulated frames. An A/B pair captured that way shows two different
   * moments of the scene, so a pixel diff between them measures the camera as much as the change.
   * Counting the same frames the deferred load counts makes two runs capture the same emulated
   * frame. 0 or unset disables it.
   *
   * The shot is taken on the present that carries the simulation step. In the phase-locked modes
   * one emulated frame is presented two to eight times with different interpolation weights, and a
   * trigger that does not say which of those it wants lands on whichever one the clock produced.
   *
   * The counter is maintained by the Vulkan loop only; on the OpenGL build this hook never fires.
   */
  const char *screenshotFrameEnv = std::getenv("SM3D_SCREENSHOT_FRAME");
  const uint64_t screenshotFrame =
    (screenshotFrameEnv != nullptr && *screenshotFrameEnv != '\0') ? std::strtoull(screenshotFrameEnv, nullptr, 10) : 0;
#if defined(SUPERMODEL_OPENGL_ONLY)
  if (screenshotFrame != 0)
    ErrorLog("Screenshot: SM3D_SCREENSHOT_FRAME is counted by the Vulkan frame loop; use SM3D_SCREENSHOT_AT with this build.");
#endif

  /*
   * Developer hook: SM3D_SAVE_STATE_FRAME=<n> writes a save state at emulated frame n, and
   * SM3D_SAVE_STATE_SLOT=<n> names the slot. Loading a state, letting the game reach a known
   * frame, saving there and loading the new file in a second run is how the save-state blocks are
   * exercised without a human at the keyboard. The slot override matters: a session started from
   * "scudplus.zst1" would otherwise write back to .zst1, which is F5's normal behaviour.
   *
   * Vulkan only, like SM3D_SCREENSHOT_FRAME: the counter it compares against belongs to that loop.
   */
#if !defined(SUPERMODEL_OPENGL_ONLY)
  const char *saveStateFrameEnv = std::getenv("SM3D_SAVE_STATE_FRAME");
  const uint64_t saveStateFrame =
    (saveStateFrameEnv != nullptr && *saveStateFrameEnv != '\0') ? std::strtoull(saveStateFrameEnv, nullptr, 10) : 0;
  const char *saveStateSlotEnv = std::getenv("SM3D_SAVE_STATE_SLOT");
  const int saveStateSlot =
    (saveStateSlotEnv != nullptr && *saveStateSlotEnv != '\0') ? std::atoi(saveStateSlotEnv) : -1;
  bool saveStateFrameDone = false;
#endif

  while (!quit)
  {
#if defined(SUPERMODEL_PURE_VULKAN)
    // Packages a save that is waiting for its thumbnail. Runs at the top of the frame so the PNG
    // written by the renderer's worker thread during the previous frames is picked up as soon as it
    // is complete; with no thumbnail requested this writes the container immediately.
    ServicePendingSave();

    /*
     * Manage Saves → Load (and -load-state) is applied here, not before the first frame.
     *
     * The machine has to have finished booting first, for the reason in the note on
     * kDeferredLoadSeconds: a state restored into a machine that is still in its boot ROM leaves it
     * running at about one twenty-fifth speed. The wait is counted in emulated frames, and the
     * start-up artwork covers the boot sequence in the meantime, so the player sees the picture and
     * then the resumed game rather than the game's own boot screen.
     */
    if (!deferredState.empty() && simFramesThisSession >= deferredLoadFrames)
    {
      const bool wasPaused = paused;
      if (!wasPaused)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
      }

      InfoLog("Manage saves: loading deferred state '%s' (PPC PC=%08X LR=%08X after %llu emulated frame(s)).",
        deferredState.c_str(), ppc_get_pc(), ppc_get_lr(),
        (unsigned long long)simFramesThisSession);
      LoadState(Model3, deferredState);

      if (!wasPaused)
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
      }

      // The game is in the state the player asked for, so the cover comes down now.
#ifdef _WIN32
      if (deferredLoadSplash)
      {
        deferredLoadSplash = false;
        OSD::Windows::HideStartupSplash();
        InfoLog("Manage saves: start-up artwork removed after the state was applied.");
      }
#endif

      std::error_code removeError;
      const std::string deferredTemp =
        Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << ".vktmp.st";
      std::filesystem::remove(deferredTemp, removeError);
      deferredState.clear();
    }
#endif

    // Marks the boundary of one presented frame, so the statistics readout reports the rate frames
    // actually reach the display at - including the interpolation-only presents of the 2x/3x/4x/8x
    // modes, which is why it reads ~115 at 2x rather than the 57.5 Hz logic rate. Before the input
    // poll, so a frame that ends the loop is still counted.
    StatsOsdFrameTick();

    // Re-sent every frame rather than once at startup: a fullscreen toggle or a display change can make
    // the driver refuse the exclusive switch mid-session, and the readout has to describe where the
    // session ended up rather than where it was intended to go.
    StatsOsdSetSyncMode(StatsSyncModeForCurrentSession());

    /*
     * Keep the pacer in step with edits made in the in-game panel, and with a
     * display mode change after a fullscreen toggle. The mode/rate/interpolation
     * values are cheap to read; the panel mode is queried once a second because
     * it involves SDL and cannot change faster than the user can switch modes.
     */
    if (highRefresh)
    {
      /*
       * The resolved mode, not the configured one: a phase multiple the display
       * cannot take has to be downgraded here, before it reaches the pacer, and
       * the downgrade has to be re-evaluated when the panel changes (a game moved
       * to another monitor, an exclusive mode switch).
       */
      const double panelHz = GetEffectivePanelRefreshHz();
      const RefreshRateMode requested = ResolveConfiguredRefreshRateMode(panelHz);
      const RefreshRateMode resolved = ResolvePhaseModeForPanel(requested, panelHz);
      ReportPaceFallback(requested, resolved, panelHz);

      const int mode = (int)resolved;
      const int fixed = (int)GetConfiguredFixedRefreshHz();
      const bool interp = GetConfiguredInterpolation();
      if (mode != lastPacerMode || fixed != lastPacerFixed || interp != lastPacerInterp)
      {
        lastPacerMode = mode;
        lastPacerFixed = fixed;
        lastPacerInterp = interp;
        DisplayPacerConfig pacerConfig;
        pacerConfig.mode = resolved;
        pacerConfig.fixedHz = GetConfiguredFixedRefreshHz();
        pacerConfig.panelHz = panelHz;
        pacerConfig.vsync = s_runtime_config["VSync"].ValueAsDefault<bool>(true);
        pacerConfig.throttle = s_runtime_config["Throttle"].ValueAsDefault<bool>(true);
        pacerConfig.interpolation = interp;
        pacer.Configure(pacerConfig, OSD::Platform::Now());
        // A new mode or panel has a different vblank phase, so the previous lead says nothing and
        // the phase has to be measured again.
        presentCalibrationFrames = 16;
        presentLeadSum = 0;
        presentLeadSamples = 0;

        /*
         * A VRR multiple only reaches the panel if the driver is allowed to hold
         * the frame until it is scanned. Without vsync the swapchain is MAILBOX
         * or IMMEDIATE, the panel keeps its own rate, and a 115 Hz presentation
         * is quantised onto a 120 Hz scan - the uneven cadence this mode exists
         * to remove, with extra work.
         */
        if (PhaseCountForMode(resolved) > 0 && !pacerConfig.vsync)
          InfoLog("Pace: VSync is off, so the display will not follow a %.2f Hz presentation; "
            "enable VSync for the multiple to hold.", pacer.TargetPresentHz());
      }

      const OSD::Platform::Nanoseconds now = OSD::Platform::Now();
      if (now >= nextPanelCheck)
      {
        nextPanelCheck = now + OSD::Platform::kNanosecondsPerSecond;
        pacer.SetPanelHz(panelHz, (uint64_t)now);
      }
    }

    // The pacing rate, for the same reason and so that the borderless cadence check works even with the
    // readout switched off - which is the state most people are in while wondering why the picture
    // stutters.
    StatsOsdSetTargetRate(highRefresh
      ? (float)pacer.TargetPresentHz()
      : s_runtime_config["RefreshRate"].ValueAsDefault<float>(0.0f));

    /*
     * The pacing line, from the last completed present. One frame of staleness is invisible in a text
     * readout and it avoids reordering the loop: the readout is built below, before this frame's present.
     */
    {
      StatsPaceInfo paceInfo;
      paceInfo.valid = highRefresh;
      if (highRefresh)
      {
        paceInfo.logicHz = pacer.MeasuredLogicHz();
        paceInfo.presentHz = pacer.MeasuredPresentHz();
        paceInfo.targetHz = pacer.TargetPresentHz();
        paceInfo.panelHz = GetEffectivePanelRefreshHz();
        paceInfo.alpha = pacer.LastAlpha();
        paceInfo.ratio = pacer.Ratio();
        paceInfo.interpolating = pacer.InterpolationEnabled();
        paceInfo.modeLabel = RefreshRateModeName(pacer.Mode());
        paceInfo.phaseLocked = pacer.IsPhaseLocked();
        paceInfo.phaseCount = pacer.PhaseCount();
        paceInfo.phaseIndex = pacer.PhaseIndex();
        pacer.PhaseAlphas(paceInfo.phaseAlpha, 8);
		        paceInfo.subFrameMs = perfSubFrames ? (double)perfSubNs / 1.0e6 / (double)perfSubFrames : 0.0;
#if !defined(SUPERMODEL_OPENGL_ONLY)
        paceInfo.cuts = s_vk3dRenderer ? (unsigned)s_vk3dRenderer->TotalCuts() : 0u;
#endif
      }
      StatsOsdSetPaceInfo(paceInfo);
    }

    if (highRefresh && OSD::Platform::Now() >= nextPaceLog)
    {
      nextPaceLog = OSD::Platform::Now() + 5 * OSD::Platform::kNanosecondsPerSecond;
      // Sampled from the main thread while the PPC may be running; enough to tell a wait loop
      // from a moving PC when a session is reported as frozen.
      InfoLog("PPC: PC=%08X LR=%08X (sample).", ppc_get_pc(), ppc_get_lr());
      const double simMs = perfFrames ? (double)perfSimNs / 1.0e6 / (double)perfFrames : 0.0;
      const double renderMs = perfFrames ? (double)perfRenderNs / 1.0e6 / (double)perfFrames : 0.0;
      const double subMs = perfSubFrames ? (double)perfSubNs / 1.0e6 / (double)perfSubFrames : 0.0;
#if !defined(SUPERMODEL_OPENGL_ONLY)
      const unsigned lastDraws = s_vk3dRenderer ? s_vk3dRenderer->LastDrawCount() : 0u;
      const unsigned long long totalCuts = s_vk3dRenderer ? s_vk3dRenderer->TotalCuts() : 0ull;
#else
      const unsigned lastDraws = 0u;
      const unsigned long long totalCuts = 0ull;
#endif
      if (pacer.IsPhaseLocked())
      {
        InfoLog("PACE: mode %s, logic %.2f Hz, present %.2f Hz, target %.2f Hz, phase %d/%d (alpha %.3f), "
          "sim %llu, presents %llu, dropped %llu, sim %.2f ms/frame, render %.2f ms/frame, "
          "sub %.2f ms/frame, draws %u, cuts %llu, lead %.2f ms.",
          RefreshRateModeName(pacer.Mode()), pacer.MeasuredLogicHz(), pacer.MeasuredPresentHz(),
          pacer.TargetPresentHz(), pacer.PhaseIndex() + 1, pacer.PhaseCount(), pacer.LastAlpha(),
          (unsigned long long)pacer.SimSteps(), (unsigned long long)pacer.Presents(),
          (unsigned long long)pacer.DroppedSteps(), simMs, renderMs, subMs, lastDraws, totalCuts,
          (double)pacer.PresentLead() / 1.0e6);
      }
      else
      {
        InfoLog("PACE: logic %.2f Hz, present %.2f Hz, target %.2f Hz, alpha %.3f, sim %llu, presents %llu, dropped %llu, sim %.2f ms/frame, render %.2f ms/frame, draws %u, cuts %llu, lead %.2f ms.",
          pacer.MeasuredLogicHz(), pacer.MeasuredPresentHz(), pacer.TargetPresentHz(), pacer.LastAlpha(),
          (unsigned long long)pacer.SimSteps(), (unsigned long long)pacer.Presents(),
          (unsigned long long)pacer.DroppedSteps(), simMs, renderMs, lastDraws, totalCuts,
          (double)pacer.PresentLead() / 1.0e6);
      }
      perfSimNs = 0;
      perfRenderNs = 0;
      perfSubNs = 0;
      perfSubFrames = 0;
      perfFrames = 0;
    }

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

    // Read every frame: the in-game panel edits s_runtime_config while the session is running.
    Inputs->SetGamepadSteeringSmoothing(
      s_runtime_config["GamepadSteeringSmoothing"].ValueAsDefault<int>(0));

    // Poll the inputs
    if (!Inputs->Poll(&game, xOffset, yOffset, xRes, yRes))
      quit = true;

    const uint64_t timingPollDone = timing ? OSD::Platform::Now() : 0;

    /*
     * Build the in-game panel before the frame is rendered, so a value changed here reaches the
     * picture on this very frame rather than the next one. The panel's own drawing is recorded later,
     * inside the renderer, because that is the only place the swapchain is set up for it.
     *
     * The statistics readout shares that frame and that draw pass; see OverlayNewFrame.
     */
    const uint64_t timingUiStart = timing ? OSD::Platform::Now() : 0;
    if (overlayOpenCountdown > 0)
    {
      --overlayOpenCountdown;
    }
    else if (overlayOpenCountdown == 0)
    {
      overlayOpenCountdown = -1;
      if (!OverlayVisible())
      {
        OverlaySetVisible(s_runtime_config, true);
        InfoLog("Overlay: panel opened by SM3D_OVERLAY_OPEN.");
      }
    }
    if (OverlayActive() && OverlayNewFrame(s_runtime_config))
      SaveOverlaySettings();
    if (timing)
      timingOverlayNs += OSD::Platform::Now() - timingUiStart;

    // The readout's on/off state is written back the moment it is toggled rather than on exit, so a
    // session that ends abnormally does not lose the setting. One-shot: the flag is consumed here.
    if (StatsOsdConsumeStateChanged())
    {
      s_runtime_config.Set("ShowStatsOsd", StatsOsdEnabled());
      SaveOverlaySettings();
    }

    /*
     * Late pointer sample, then the crosshair that depends on it.
     *
     * Inputs->Poll() ran at the top of the frame, but everything between it and this point - the
     * menu bar's own state, the panel build - costs time, and for a light gun that time is lag.
     * PollPointerLate() asks the OS for the pointer's position again and re-polls only the gun axes,
     * so the emulated aim this frame uses is as fresh as the frame boundary allows.
     *
     * The crosshair is handed to the renderer here, after that fresh sample, so it describes the aim
     * values the frame will actually use. On Vulkan this is a small native draw in the same frame;
     * no ImGui frame is built for it. On OpenGL the CCrosshair object draws it during EndFrameVideo.
     */
    Inputs->PollPointerLate(&game);
#if !defined(SUPERMODEL_OPENGL_ONLY)
    UpdateOverlayCrosshair(Inputs, currentInputs, xOffset, yOffset, xRes, yRes, overlayScale);
#endif

    const uint64_t timingRenderStart = timing ? OSD::Platform::Now() : 0;
    const uint64_t presentSeqBefore = s_framePresentSeq;

    // Render if paused, otherwise run a frame. The high-refresh path releases
    // at most one simulation step per iteration, then presents it (and any
    // interpolation frame) through the independent present clock.
    const uint64_t timingRunStart = timing ? OSD::Platform::Now() : 0;
    bool newSimulation = false;

    if (highRefresh)
    {
      const uint64_t now = OSD::Platform::Now();
      pacer.SetPaused(paused, now);

      if (!paused)
      {
        const CDisplayPacer::TickResult tick = pacer.Tick(now);
        newSimulation = tick.step;
        if (tick.step)
        {
          const uint64_t simStart = OSD::Platform::Now();

          /*
           * Close the previous simulation before starting the next one. Its
           * read-only snapshots become the scene this iteration presents, while
           * the new frame's PPC/audio work runs in the background. At 240/480 Hz
           * the simulation step (6-8 ms) is far longer than a present period, so
           * doing it inline in front of the present would miss vblanks; this
           * order keeps the present path to scene matching and draw submission.
           */
          Model3->EndSimulationFrame();
          /*
           * Traverse the finished step's scene database before the next one
           * starts. This is the one instant at which Culling RAM holds a whole
           * frame and nothing is writing to it: BeginSimulationFrame() posts the
           * PPC thread, and everything after that - the sleep until the present
           * deadline included - overlaps with the CPU rewriting the next frame's
           * lists. Traversing later therefore blended a completed scene with a
           * half-written one, which is invisible in a still frame and shows up
           * as objects appearing a sub-frame early once interpolation is on.
           *
           * The cost is the traversal itself (well under a millisecond) added to
           * the step, and the GPU half still happens at present time.
           */
          Model3->PrepareSceneSnapshot();
#if !defined(SUPERMODEL_OPENGL_ONLY)
          /*
           * A camera cut has to be handed to the pacer before the render, not
           * inferred from the frame it already rendered: the interpolated
           * presents of this step would otherwise blend the old and new camera.
           */
          pacer.NoteFrameCut(s_vk3dRenderer != nullptr &&
            s_vk3dRenderer->LastMotionStats().cuts > 0);
#endif
          //
          // Last moment to sample the controls: BeginSimulationFrame() posts the PPC thread, and
          // everything before this point in the loop (panel, command recording) would otherwise
          // have been sampled at the top of the frame.
          Inputs->PollGameInputsLate(&game);
          Model3->BeginSimulationFrame();
#if defined(SUPERMODEL_PURE_VULKAN)
          simFramesThisSession++;
#endif
          perfSimNs += OSD::Platform::Now() - simStart;
        }
      }

      // The deadline this present is built against. Kept so the scan-out measurement below can
      // relate the display time to the instant rendering started from.
      const uint64_t presentDeadline = pacer.PresentDeadline(OSD::Platform::Now());

      // Sleep to the display's cadence, then sample alpha at the deadline
      // rather than at the top of the loop: the sleep is part of the interval
      // the frame describes.
      pacer.WaitForPresent(OSD::Platform::Now());

      /*
       * Whether the *previous* picture made it to the display, asked at the deadline rather than
       * straight after the present. One period has passed since that present was submitted, so a
       * confirmed scan-out is exactly what a correctly phased lead produces; a timeout means the
       * frame was still queued and the phase has slipped. Non-blocking, so it costs nothing.
       */
#if !defined(SUPERMODEL_OPENGL_ONLY)
      if (s_vkContext && s_vkContext->PresentWaitAvailable() && s_vkContext->HasPresented())
      {
        displayAttempts++;
        if (s_vkContext->WaitForPresentDisplay(0))
          displayConfirms++;
        else
          displayLate++;
      }
#endif
      const double alpha = paused ? 1.0 : pacer.AlphaAt(OSD::Platform::Now());
      const uint64_t renderStart = OSD::Platform::Now();
      Model3->RenderFrame(alpha, newSimulation);
      const uint64_t renderNs = OSD::Platform::Now() - renderStart;
      perfRenderNs += renderNs;
      perfFrames++;
      // Presents that carry no simulation are the ones a phase-locked mode adds,
      // and they are the whole point of it: this figure is what has to fit in a
      // 2 ms slot at 460 Hz.
      if (!newSimulation && pacer.IsPhaseLocked())
      {
        perfSubNs += renderNs;
        perfSubFrames++;
      }
      pacer.RecordPresent(OSD::Platform::Now(), alpha);

      /*
       * Scan-out feedback and the present-phase calibration (VK_KHR_present_wait).
       *
       * A present only reaches the display at a vblank. Submitted a fraction of a millisecond too
       * late and the frame waits a whole period; submitted just before the vblank and it is shown
       * there. The pacer's deadline has no fixed relationship to that grid, so which case a session
       * lands in is luck - and on this hardware it lands in the bad one, reported as "0 of 121
       * confirmed" and costing a full period (8.3 ms at 120 Hz) of latency.
       *
       * For the first few presents after the pacer is configured the wait is given two periods, so it
       * blocks until the picture really is scanned. That one number is the whole calibration: the
       * distance from the deadline to the display is one period minus how far past the vblank the
       * deadline sits, so aiming that much earlier puts the present in front of the vblank instead of
       * behind it. Afterwards the wait is given no time at all, so it only counts what made it.
       *
       * The lead is a phase shift, not a rate change: the cadence is untouched. In the phase-locked
       * modes alpha is a constant per slot and does not move either; in the clock-driven modes it is
       * sampled a little earlier, which is the price of the period gained.
       */
#if !defined(SUPERMODEL_OPENGL_ONLY)
      if (s_vkContext && s_vkContext->PresentWaitAvailable() && s_vkContext->HasPresented())
      {
        const int64_t presentPeriodNs = (int64_t)pacer.PresentPeriodNs();
        /*
         * The phase is a property of the display's scan-out grid, not of our present period. Those
         * are the same at the panel's own rate and differ by a factor of two or more below it, where
         * measuring against our period would aim the frame a whole panel period too early.
         */
        const double panelHz = pacer.PanelHz();
        const int64_t vblankNs = panelHz > 1.0 ? (int64_t)(1.0e9 / panelHz) : presentPeriodNs;
        const bool calibrating = presentCalibrationFrames > 0 && presentPeriodNs > 0 && vblankNs > 0;
        const uint64_t timeout = calibrating ? (uint64_t)(2 * presentPeriodNs) : 0;
        const bool onScreen = s_vkContext->WaitForPresentDisplay(timeout);
        if (calibrating)
        {
          if (onScreen)
          {
            const int64_t latency = (int64_t)OSD::Platform::Now() - (int64_t)presentDeadline;
            if (vblankNs > 0 && latency >= 0 && latency < 2 * vblankNs)
            {
              /*
               * Only a display that took more than half a period to appear is evidence of a missed
               * vblank. A short latency means the deadline already sits in front of one - or that the
               * panel is following us (VRR), where a lead would only make the picture older without
               * gaining a scan-out. Both of those want no correction at all.
               */
              int64_t past = 0;
              if (latency > vblankNs / 2)
              {
                past = vblankNs - latency;
                if (past < 0)
                  past = 0;
              }
              presentLeadSum += past;
              presentLeadSamples++;
            }
          }
          presentCalibrationFrames--;
          if (presentCalibrationFrames == 0)
          {
            if (presentLeadSamples > 4)
            {
              // Plus the render+submit cost, so the queue insertion lands before the vblank rather
              // than exactly on it; one millisecond covers 0.8 ms of render and the jitter around it.
              // Never more than one vblank, or the picture would move two of them instead of one.
              // A mean of zero means the phase was already right, so nothing is added at all.
              int64_t lead = presentLeadSum / presentLeadSamples;
              if (lead > 0)
              {
                lead += 1000000;
                if (vblankNs > 0 && lead > vblankNs - 500000)
                  lead = vblankNs - 500000;
              }
              pacer.SetPresentLead(lead);
              InfoLog("Pace: present phase calibrated - display was %.2f ms after the deadline, "
                "lead %.2f ms.",
                (double)(presentLeadSum / presentLeadSamples) / 1.0e6,
                (double)pacer.PresentLead() / 1.0e6);
            }
            else
            {
              InfoLog("Pace: present phase calibration got no usable samples; lead left at 0.");
            }
          }
        }
      }
#endif
    }
    else if (paused)
    {
      Model3->RenderFrame();
    }
    else
    {
      // Sampled at the step boundary rather than at the top of the frame; see PollGameInputsLate().
      Inputs->PollGameInputsLate(&game);
      Model3->RunFrame();
#if defined(SUPERMODEL_PURE_VULKAN)
      // Counted here too: the deferred load waits on machine time, and a session running at the
      // original 57.5 Hz never takes the high-refresh branch above.
      simFramesThisSession++;
#endif
    }
    if (timing)
      timingRunNs += OSD::Platform::Now() - timingRunStart;

    if (timing)
    {
      const uint64_t now = OSD::Platform::Now();
      timingGapNs += timingRenderStart - timingPollDone;
      timingFrames++;
      if (s_framePresentSeq != presentSeqBefore)
      {
        timingPresentNs += s_framePresentNs - timingPollDone;
        timingPresentFrames++;
      }
      if (now >= timingNextReport)
      {
        char displayText[96];
        if (displayAttempts > 0)
          std::snprintf(displayText, sizeof(displayText), "%llu/%llu confirmed by the display, %llu late",
            (unsigned long long)displayConfirms, (unsigned long long)displayAttempts,
            (unsigned long long)displayLate);
        else
          std::snprintf(displayText, sizeof(displayText), "n/a");
        InfoLog("Timing: ui %.1f us/frame, run %.1f us/frame, poll-to-render %.1f us/frame, poll-to-present %.1f us/frame, display %s, frames %llu presents %llu",
          (double)timingOverlayNs / 1000.0 / (double)timingFrames,
          (double)timingRunNs / 1000.0 / (double)timingFrames,
          (double)timingGapNs / 1000.0 / (double)timingFrames,
          timingPresentFrames ? (double)timingPresentNs / 1000.0 / (double)timingPresentFrames : 0.0,
          displayText,
          (unsigned long long)timingFrames, (unsigned long long)timingPresentFrames);

        std::vector<int64_t> late(sleepLate, sleepLate + sleepLateCount);
        std::sort(late.begin(), late.end());
        const auto pick = [&late](double percentile) -> int64_t
        {
          if (late.empty())
            return 0;
          size_t index = (size_t)((late.size() - 1) * percentile);
          if (index >= late.size())
            index = late.size() - 1;
          return late[index];
        };
        const int64_t maxLate = late.empty() ? 0 : late.back();
        unsigned audioUnder = 0, audioOver = 0;
        GetAudioBufferStats(&audioUnder, &audioOver);
        InfoLog("Sleep timing: p50 %.1f us, p99 %.1f us, max %.1f us, samples %llu, audio u%u/o%u",
          (double)pick(0.50) / 1000.0, (double)pick(0.99) / 1000.0, (double)maxLate / 1000.0,
          (unsigned long long)late.size(), audioUnder, audioOver);

        timingFrames = 0;
        timingOverlayNs = 0;
        timingRunNs = 0;
        timingGapNs = 0;
        timingPresentNs = 0;
        timingPresentFrames = 0;
        displayAttempts = 0;
        displayConfirms = 0;
        displayLate = 0;
        timingNextReport = now + OSD::Platform::kNanosecondsPerSecond;
      }
    }

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
      {
        OverlayToggle(s_runtime_config);
        // Logged because the panel is also closed by its own button and by Apply, so "I did not press
        // anything and it opened" needs an answer: this line says whether the toggle switch fired.
        InfoLog("Overlay: panel %s by the toggle key.", OverlayVisible() ? "opened" : "closed");
      }
    }
    else if (Inputs->uiToggleStatsOsd->Pressed())
    {
      // Same ImGui context and same draw pass as the panel, so the same condition applies.
      if (OverlayActive())
        StatsOsdToggle();
      else
        InfoLog("Statistics readout: no overlay backend is available in this session.");
    }
    else if (Inputs->uiToggleVrr->Pressed())
    {
      /*
       * A/B between the arcade rate and twice it.
       *
       * This is the comparison the mode exists for, and having to leave the game
       * to make it - restarting into a different ini - makes it impossible to
       * judge: the two have to be seen back to back on the same scene. The switch
       * writes the setting, so the choice survives the session, and the loop's
       * own change detection reconfigures the pacer on the next frame.
       *
       * Only the pair is toggled. 3x/4x/8x stay in the settings panel: they are
       * for a display that can actually scan that fast, and cycling through five
       * modes on one key would make the A/B slower rather than faster.
       */
      const RefreshRateMode mode = GetConfiguredRefreshRateMode();
      const double panelHz = GetEffectivePanelRefreshHz();
      if (mode == RefreshRateMode::Vrr2x)
      {
        s_runtime_config.Get("RefreshRateMode").SetValue(0);
        printf("Pace: VRR 1x (%.2f Hz)\n", kModel3StepHz);
        InfoLog("Pace: hotkey switched to VRR 1x (%.2f Hz).", kModel3StepHz);
      }
      else if (!PanelSupportsPhaseMode(RefreshRateMode::Vrr2x, panelHz))
      {
        printf("Pace: VRR 2x needs a 120 Hz or faster display; this one reports %.1f Hz.\n", panelHz);
        InfoLog("Pace: hotkey ignored - VRR 2x needs 120 Hz, display reports %.1f Hz.", panelHz);
      }
      else
      {
        s_runtime_config.Get("RefreshRateMode").SetValue(3);
        printf("Pace: VRR 2x (%.2f Hz)\n", kModel3StepHz * 2.0);
        InfoLog("Pace: hotkey switched to VRR 2x (%.2f Hz).", kModel3StepHz * 2.0);
      }
    }
    else if (Inputs->uiToggleHDTextures->Pressed())
    {
      /*
       * A/B between the replacement pack and the original artwork.
       *
       * This is the same value the in-game panel's checkbox writes, and the renderer reads it once
       * per simulation frame (CVulkan3D::BuildDrawList), so the switch takes effect on the next
       * frame. Nothing is reloaded or freed: the loaded images stay resident, which is what makes
       * flipping back and forth instant. F3 is the rescan, for a pack author replacing files while
       * playing.
       */
#if !defined(SUPERMODEL_OPENGL_ONLY)
      const bool enable = !s_runtime_config["LoadHDTextures"].ValueAsDefault<bool>(false);
      s_runtime_config.Set("LoadHDTextures", enable);
      printf("HD textures: %s.\n", enable ? "replacement pack on" : "original artwork");
      InfoLog("HD textures: %s with the F4 shortcut.",
        enable ? "replacements enabled" : "replacements disabled");
#else
      InfoLog("HD textures: this build has no Vulkan renderer; the F4 shortcut does nothing.");
#endif
    }
    else if (Inputs->uiReloadHDTextures->Pressed())
    {
      /*
       * Rescan the running game's HD texture folder. The renderer applies it at the start of the
       * next simulation frame, so while the in-game panel has the emulator paused this only takes
       * effect once the panel is closed - the panel says as much next to its own reload button.
       */
#if !defined(SUPERMODEL_OPENGL_ONLY)
      Vulkan3D::RequestHDTextureReload();
      printf("HD textures: reload requested.\n");
      InfoLog("HD textures: reload requested with the F3 shortcut.");
#else
      InfoLog("HD textures: this build has no Vulkan renderer; the F3 shortcut does nothing.");
#endif
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

        /*
         * Pause while the panel is open.
         *
         * The panel is for adjusting a settings value while watching the picture, and for
         * saving/loading a state. With the game still running, a slider drag or a slot choice
         * happened while the car kept moving - the player had to fight the game to read the
         * menu. Pausing makes the picture stop on the frame the panel describes.
         *
         * Only the pause this code caused is undone on close: a game the player paused
         * themselves with Alt+P stays paused, and Alt+P while the panel is open hands control
         * back to the player (see the hotkey handler).
         */
        if (overlayNow && !paused)
        {
          paused = true;
          overlayPausedEmulator = true;
          Model3->PauseThreads();
          SetAudioEnabled(false);
          snprintf(titleStr, sizeof(titleStr), "%s (Paused)", baseTitleStr);
          OSD::Platform::SetWindowTitle(s_platformWindow, titleStr);
          if (Outputs != NULL)
            Outputs->SetValue(OutputPause, paused);
        }
        else if (!overlayNow && overlayPausedEmulator)
        {
          overlayPausedEmulator = false;
          paused = false;
          Model3->ResumeThreads();
          SetAudioEnabled(true);
          OSD::Platform::SetWindowTitle(s_platformWindow, baseTitleStr);
          if (Outputs != NULL)
            Outputs->SetValue(OutputPause, paused);
        }
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
      // The player has taken over the pause state: closing the panel must not decide it for them.
      overlayPausedEmulator = false;

      if (paused)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
        snprintf(titleStr, sizeof(titleStr), "%s (Paused)", baseTitleStr);
        OSD::Platform::SetWindowTitle(s_platformWindow, titleStr);
      }
      else
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
        OSD::Platform::SetWindowTitle(s_platformWindow, baseTitleStr);
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
#if defined(SUPERMODEL_OPENGL_ONLY)
      delete Render2D;
      Render2D = nullptr;
#elif !defined(SUPERMODEL_PURE_VULKAN)
      if (!s_vkContext)
      {
        delete Render2D;
        Render2D = nullptr;
      }
#endif

      // Resize screen
      totalXRes = xRes = s_runtime_config["XResolution"].ValueAs<unsigned>();
      totalYRes = yRes = s_runtime_config["YResolution"].ValueAs<unsigned>();
      bool stretchc = s_runtime_config["Stretch"].ValueAs<bool>();
      bool fullscreenc = s_runtime_config["FullScreen"].ValueAs<bool>();
#if defined(SUPERMODEL_OPENGL_ONLY)
      Result resized = ResizeGLScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretchc, fullscreenc);
#elif defined(SUPERMODEL_PURE_VULKAN)
      Result resized = ResizeVulkanScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretchc, fullscreenc);
#else
      Result resized = s_vkContext
        ? ResizeVulkanScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretchc, fullscreenc)
        : ResizeGLScreen(&xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, !stretchc, fullscreenc);
#endif
      if (Result::OKAY != resized)
        goto QuitError;

      // Recreate renderers and attach to the emulator
      unsigned aaTarget = 0;
#if !defined(SUPERMODEL_PURE_VULKAN)
      if (superAA)
      {
        superAA->Init(totalXRes, totalYRes);
        aaTarget = superAA->GetTargetID();
      }
#if defined(SUPERMODEL_OPENGL_ONLY)
      Render2D = new CRender2D(s_runtime_config);
#else
      if (!s_vkContext)
        Render2D = new CRender2D(s_runtime_config);
#endif
#endif

      if (Result::OKAY != Render2D->Init(xOffset * aaValue, yOffset * aaValue, xRes * aaValue, yRes * aaValue, totalXRes * aaValue, totalYRes * aaValue, aaTarget, upscaleMode))
        goto QuitError;
      if (Result::OKAY != Render3D->Init(xOffset * aaValue, yOffset * aaValue, xRes * aaValue, yRes * aaValue, totalXRes * aaValue, totalYRes * aaValue, aaTarget))
        goto QuitError;

      Model3->AttachRenderers(Render2D, Render3D, superAA);

      // The renderers were just re-initialised, possibly at a different size, so the readout's
      // Render line has to be told again - this is the resolution-change path.
      ReportRenderInfo(aaValue, xRes, yRes, totalXRes, totalYRes);

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
      // Change save slot. Skipping occupied slots here is what makes F6 "next new slot" rather
      // than "next number"; F5 without F6 still writes back to the loaded slot. F7 handles the
      // empty slot this selects by falling back to the newest save (see LoadState).
      s_saveSlot = NextFreeSaveSlot(Model3->GetGame().name, s_saveSlot);
      printf("Save slot: %u\n", s_saveSlot);
      InfoLog("Quick-save slot %u (%s).", s_saveSlot,
        SaveSlotOccupied(Model3->GetGame().name, s_saveSlot) ? "occupied" : "empty");
    }
    else if (Inputs->uiLoadState->Pressed())
    {
      if (!paused)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
      }

      // Load game state
#if defined(SUPERMODEL_PURE_VULKAN)
      // A manual load wins over a Manage Saves load that is still waiting for the game to start.
      deferredState.clear();
#ifdef _WIN32
      if (deferredLoadSplash)
      {
        deferredLoadSplash = false;
        OSD::Windows::HideStartupSplash();
      }
#endif
#endif
      InfoLog("F7: loading save slot %u.", s_saveSlot);
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

    /*
     * Frame-counted screenshot (SM3D_SCREENSHOT_FRAME). Vulkan only: the counter belongs to the
     * deferred-load path, and the OpenGL build never increments it (see the warning at start-up).
     */
#if defined(SUPERMODEL_PURE_VULKAN)
    if (!screenshotTaken && screenshotFrame != 0 && simFramesThisSession >= screenshotFrame &&
      (!highRefresh || newSimulation))
    {
      screenshotTaken = true;
      InfoLog("Screenshot: SM3D_SCREENSHOT_FRAME fired at emulated frame %llu (requested %llu).",
        (unsigned long long)simFramesThisSession, (unsigned long long)screenshotFrame);
      Screenshot();
    }
#endif

#if !defined(SUPERMODEL_OPENGL_ONLY)
    if (!saveStateFrameDone && saveStateFrame != 0 && simFramesThisSession >= saveStateFrame &&
      (!highRefresh || newSimulation))
    {
      saveStateFrameDone = true;
      InfoLog("Save state: SM3D_SAVE_STATE_FRAME fired at emulated frame %llu (requested %llu).",
        (unsigned long long)simFramesThisSession, (unsigned long long)saveStateFrame);

      const unsigned previousSlot = s_saveSlot;
      if (saveStateSlot >= 0 && saveStateSlot <= 99)
        s_saveSlot = (unsigned)saveStateSlot;
      SaveState(Model3);
      s_saveSlot = previousSlot;
    }
#endif

    if (screenshotAt != nullptr && !screenshotTaken && *screenshotAt != '\0')
    {
      const double at = std::atof(screenshotAt);
      const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - screenshotClock).count();
      if (elapsed >= at)
      {
        screenshotTaken = true;
        InfoLog("Screenshot: SM3D_SCREENSHOT_AT fired at %.3f s (requested %.3f s).", elapsed, at);
        Screenshot();
      }
    }

    // Refresh rate (frame limiting). The high-refresh path paces itself in
    // DisplayPacer::WaitForPresent(), so running both would sleep twice.
    if (!highRefresh && (paused || s_runtime_config["Throttle"].ValueAs<bool>()))
    {
        const uint64_t sleepDeadline = nextTime;
        SuperSleepUntil(nextTime);

        if (timing)
        {
            const int64_t late = (int64_t)(OSD::Platform::Now() - sleepDeadline);
            sleepLate[sleepLateNext] = late;
            sleepLateNext = (sleepLateNext + 1) % kSleepLateSamples;
            if (sleepLateCount < kSleepLateSamples)
                sleepLateCount++;
        }

        // Advance the target by exactly one frame period. The previous code rebased the
        // target on OSD::Platform::Now() every frame, which folded the sleep
        // overshoot into the next deadline. That made each emulated frame slightly longer
        // than 1/60s, so audio was produced a little slower than the 44.1kHz device consumed
        // it and the sound buffer slowly drained until it under-ran (audible stutter).
        nextTime += perfCountPerFrame;

        // If a frame took longer than its budget we are already behind; resync to now
        // rather than sprinting to catch up.
        uint64_t nowTicks = OSD::Platform::Now();
        if (nextTime < nowTicks)
            nextTime = nowTicks;
    }

    // Measure frame rate
    uint64_t currentFPSTicks = OSD::Platform::Now();
    if (s_runtime_config["ShowFrameRate"].ValueAs<bool>())
    {
      fpsFramesElapsed += 1;
      uint64_t measurementTicks = currentFPSTicks - prevFPSTicks;
      if (measurementTicks >= static_cast<uint64_t>(OSD::Platform::kNanosecondsPerSecond)) // update FPS every 1 second
      {
        double fps = double(fpsFramesElapsed) / (double(measurementTicks) / double(OSD::Platform::kNanosecondsPerSecond));
        unsigned audioUnderruns = 0, audioOverruns = 0;
        GetAudioBufferStats(&audioUnderruns, &audioOverruns);
        snprintf(titleStr, sizeof(titleStr), "%s - %1.3f FPS%s [audio u%u/o%u]",
          baseTitleStr, fps, paused ? " (Paused)" : "", audioUnderruns, audioOverruns);
        OSD::Platform::SetWindowTitle(s_platformWindow, titleStr);
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

  OverlayClearSaveHandlers();

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
  SetModalLoopHook(nullptr, nullptr);

  // Before the renderers: the overlay's ImGui backend waits for the device to go idle, and it has to
  // stop being referenced by the renderer before the renderer is destroyed.
  //
  // The statistics collection goes with it: it holds an NVML library and device handle that should not
  // outlive the session, or the next one would find them already loaded and skip setting them up.
  StatsOsdShutdown();
  OverlayShutdown();
#if !defined(SUPERMODEL_OPENGL_ONLY)
  s_overlayFrostRenderer = nullptr;
#endif
  InfoLog("Session: overlay and statistics shut down; destroying the renderers.");

  // Shut down renderers
  delete Render2D;
#if !defined(SUPERMODEL_OPENGL_ONLY)
  s_vk2dRenderer = nullptr;
#endif
  InfoLog("Session: 2D renderer destroyed.");
  delete Render3D;
  InfoLog("Session: 3D renderer destroyed.");
#if !defined(SUPERMODEL_PURE_VULKAN)
  delete superAA;
#endif
  InfoLog("Session: released. Returning to the settings window.");

  return 0;
  }

  // Quit with an error
QuitError:
  SetModalLoopHook(nullptr, nullptr);

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
#if !defined(SUPERMODEL_OPENGL_ONLY)
  s_overlayFrostRenderer = nullptr;
#endif
  delete Render2D;
#if !defined(SUPERMODEL_OPENGL_ONLY)
  s_vk2dRenderer = nullptr;
#endif
  delete Render3D;
#if !defined(SUPERMODEL_PURE_VULKAN)
  delete superAA;
#endif

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
    // The old code resolved paths against the working directory, and the executable directory is
    // where most people launched from, so both are worth trying.
    const std::string base = FileSystemPath::GetBasePath();

    /*
     * Assets/ is migrated separately, before (and independently of) the settings file check below.
     *
     * Two reasons it cannot go in the loop: it is the only legacy folder with a subdirectory
     * (Boxarts/), which the flat copy would skip, and an installation made by an earlier build
     * already has Data/Config/Supermodel.ini, so it would return early and never copy the artwork.
     * Without this, every archive downloaded from the releases page shows empty cover slots: the
     * package puts Assets/ beside the executable, while this build reads Data/Assets/.
     *
     * Only when Data/Assets/ is still empty. After the first run it holds the crosshairs and the
     * covers, and re-copying on every start would resurrect artwork the user deleted on purpose.
     */
    {
        const std::string from = base + "Assets/";
        const std::string to = FileSystemPath::GetPath(FileSystemPath::Assets);
        std::error_code error;
        bool populated = false;
        if (std::filesystem::is_directory(to, error) && !error)
        {
            // Any entry counts, not just files: Data/Assets/ may legitimately hold only the Boxarts/
            // subdirectory, and treating that as empty would re-copy deleted artwork on every start.
            std::filesystem::directory_iterator first(to, error);
            if (!error && first != std::filesystem::directory_iterator())
                populated = true;
        }

        if (!populated && from != to)
        {
            std::error_code sourceError;
            if (std::filesystem::is_directory(from, sourceError) && !sourceError)
            {
                int copied = 0;
                std::error_code walkError;
                for (auto it = std::filesystem::recursive_directory_iterator(from,
                         std::filesystem::directory_options::skip_permission_denied, walkError);
                     it != std::filesystem::recursive_directory_iterator();
                     it.increment(walkError))
                {
                    if (walkError)
                        break;

                    std::error_code entryError;
                    if (!it->is_regular_file(entryError) || entryError)
                        continue;

                    const std::filesystem::path relative =
                        std::filesystem::relative(it->path(), from, entryError);
                    if (entryError)
                        continue;

                    const std::filesystem::path target = std::filesystem::path(to) / relative;
                    std::error_code existsError;
                    if (std::filesystem::exists(target, existsError) && !existsError)
                        continue;

                    std::error_code copyError;
                    std::filesystem::create_directories(target.parent_path(), copyError);
                    if (std::filesystem::copy_file(it->path(), target,
                            std::filesystem::copy_options::skip_existing, copyError))
                        ++copied;
                }

                if (copied > 0)
                {
                    // printf for the same reason as the copies below: the logger does not exist yet.
                    printf("Moved %d file(s) from '%s' into '%s'.\n", copied, from.c_str(), to.c_str());
                }
            }
        }
    }

    // Nothing to do if the current layout already has a settings file.
    FILE* existing = fopen(s_configFilePath.c_str(), "r");
    if (existing != nullptr)
    {
        fclose(existing);
        return;
    }

    const char* const legacyDirs[] = { "Config/", "NVRAM/", "Saves/" };

    for (const char* dir : legacyDirs)
    {
        const std::string from = base + dir;
        const std::string to = (dir == std::string("Config/"))
            ? FileSystemPath::GetPath(FileSystemPath::Config)
            : (dir == std::string("NVRAM/") ? FileSystemPath::GetPath(FileSystemPath::NVRAM)
                                            : FileSystemPath::GetPath(FileSystemPath::SavePathType()));

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
  Inputs->ApplyGameSpecificInputRanges(game);

  // If the user wants to configure the inputs, do that now
  if (configure)
  {
    std::string title(MODEL3VULKAN_TITLE_FULL " - ");
    if (game.name.empty())
      title.append("Configuring Default Inputs...");
    else
      title.append(Util::Format() << "Configuring Inputs for: " << game.title);
    OSD::Platform::SetWindowTitle(s_platformWindow, title.c_str());

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

  // Light gun games with no explicit mapping fall back to the mouse. Only player 1 is
  // wired up: the SDL backend exposes a single pointer. Applied after the interactive
  // configuration so these mappings stay in memory and are never written back.
  if (game.inputs & (Game::INPUT_GUN1|Game::INPUT_GUN2|Game::INPUT_ANALOG_GUN1|Game::INPUT_ANALOG_GUN2))
  {
    auto appendMissing = [](CInput *input, const char *mapping)
    {
      if (input != nullptr && strstr(input->GetMapping(), "MOUSE_") == nullptr)
        input->AppendMapping(mapping);
    };

    appendMissing(Inputs->gunX[0].get(), "MOUSE_XAXIS");
    appendMissing(Inputs->gunY[0].get(), "MOUSE_YAXIS");
    appendMissing(Inputs->analogGunX[0].get(), "MOUSE_XAXIS");
    appendMissing(Inputs->analogGunY[0].get(), "MOUSE_YAXIS");
    appendMissing(Inputs->trigger[0].get(), "MOUSE_LEFT_BUTTON");
    appendMissing(Inputs->analogTriggerLeft[0].get(), "MOUSE_LEFT_BUTTON");
    appendMissing(Inputs->analogTriggerRight[0].get(), "MOUSE_RIGHT_BUTTON");
    InfoLog("Input: mouse fallback mappings applied to player 1 light gun.");
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
  // Where the setting window looks for ROM sets on a fresh install. macOS points at the ROMs
  // folder next to the executable (the release archive ships one) and creates it if needed;
  // Windows keeps the historical Downloads folder. An existing Supermodel.ini keeps whatever
  // path it already had, so this only decides where a new installation starts looking.
  config.Set<std::string>("ROMPath", FileSystemPath::GetDefaultRomPath(), "ROM", "", "");
  // ROM set started automatically on the first frame after start-up, chosen from the game list's
  // right-click menu ("Auto-Launch on Startup"). Empty means the list is shown. Deliberately in
  // the shared section rather than the Vulkan-only one: the settings window reads it
  // unconditionally and a missing node there is fatal.
  config.Set<std::string>("AutoLaunchGame", "", "Core", "", "");
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
  // CDriveBoard
  config.Set("ForceFeedback", false, "ForceFeedback");
  
  // Platform-specific/UI
  config.Set("New3DEngine", true, "Video");
#if !defined(SUPERMODEL_OPENGL_ONLY)
  // Renderer selection and Vulkan device options.  This executable has a single
  // renderer family, so the keys are neither defined nor written back.
  config.Set("Vulkan3DEngine", true, "Video");
  config.Set("VkValidation", false, "Video");
  // Name of the Vulkan physical device to render with. Empty picks the best available.
  config.Set<std::string>("VulkanDevice", "", "Video", "", "");
#endif
  config.Set("QuadRendering", false, "Video");
  config.Set("XResolution", 1280, "Video");
  config.Set("YResolution", 960, "Video");
  // Snap the visible image to a whole-number magnification of the native 496x384 signal, leaving the
  // remainder as black bars. Off by default because filling the display is what most setups want;
  // this is the option to reach for when pixel-exact scaling matters more than screen coverage (the
  // 2D tile layer is a fixed 496x384 texture, so only whole-number scaling leaves it undistorted).
  config.Set("IntegerScale", false, "Video");
#if !defined(SUPERMODEL_OPENGL_ONLY)
  /*
   * Low-resolution rendering (Vulkan only).
   *
   * 0 leaves the 3D scene at the visible picture's resolution. 1 rasterises it at the Model 3's
   * native 496x384 and upscales the result onto the picture rectangle: the 2D tile layers keep their
   * own resolution, so the HUD stays as sharp as before while the 3D fill rate drops to a small
   * fraction. At 1080p fullscreen that is 1395x1080 worth of scene pixels replaced by 496x384 - about
   * 7.5 times less - which is what lets an integrated GPU hold 60 Hz. The second entry, 992x768, is
   * the same path at twice the native size: a middle ground for a GPU that cannot fill the picture
   * resolution but does not need the 1x image.
   *
   * Takes precedence over IntegerScale, which also decides how the picture is scaled; the settings
   * window greys that option out while this is on. The allow-list has to match the settings window's
   * combo, or the generic writer treats a value it offers as out of range.
   */
  config.Set("InternalResolution", 0, "Video", 0, 0, { 0,1,2 });
#endif
#if !defined(SUPERMODEL_OPENGL_ONLY)
  /*
   * Bezel backdrop.
   *
  	 * Fills the black bars with a picture so the picture does not sit in a void on a widescreen
  	 * display. 0 = off, 1 = the image shipped with the build, 2 = a PNG of the user's choosing. This
  	 * is a display preference rather than a game setting, hence the Video group. On by default: the
  	 * built-in cabinet is what a new install should show when the picture is narrower than the
  	 * window, and it can be switched off on the same page.
  	 *
  	 * The image is drawn behind the emulated picture, never over it - the bars are the only part of
  	 * the screen it touches (see CVulkan2D::DrawBezelPass). Only the Vulkan backend implements it;
  	 * the OpenGL one ignores the setting.
  	 */
  	config.Set("BezelMode", 1, "Video", 0, 2, { 0, 1, 2 });
  // Path used by BezelMode 2. Empty behaves like the built-in image rather than failing silently.
  config.Set<std::string>("BezelImage", "", "Video", "", "");
  // Percentage multiplier on the backdrop's rgb. The default is deliberately unattenuated: an
  // undimmed picture is what the person who supplied it expects to see, and dimming is one drag
  // away for anyone who finds it distracting next to the game.
  config.Set("BezelBrightness", 100, "Video", 10, 200);
#endif
  // Interface language: 0 English (default), 1 Chinese. The label texts are picked up by
  // Localization, so the interface can switch without restarting.
  config.Set("UiLanguage", 0, "Video", 0, 0, { 0,1 });
  // Interface appearance: 0 warm eye-care light (default), 1 warm dark, 2 the interface as it was
  // before the frosted-glass rework. All three avoid pure black and pure white except the classic
  // theme, which is a faithful copy of what shipped before and is kept for exactly that reason.
  config.Set("UITheme", 0, "Video", 0, 0, { 0,1,2 });
  // Frosted-glass panels: the blurred backdrop behind the settings window's panes and the in-game
  // panel. On by default; turning it off costs nothing visually beyond losing the blur, and is the
  // first thing to try on a GPU that cannot keep up (the hardware-preset "fastest" tier sets it).
  config.Set("UiFrostedGlass", true, "Video");
  config.SetEmpty("WindowXPosition");
  config.SetEmpty("WindowYPosition");
  config.Set("FullScreen", false, "Video");
  config.Set("BorderlessWindow", false, "Video");
  config.Set("Supersampling", 1, "Video", 1, 8);
  // View cycling for the games with four VR camera buttons. With this on, one button steps through
  // the views instead of needing one button each; VRDefaultView is the view the cycle starts on
  // (2 is the default third-person view in Daytona 2).
  //
  // Input settings, so they are defined for both executables: the settings window draws them from
  // the Controls tab regardless of renderer, and a key that exists in the defaults only for one
  // build is a missing node - and a thrown exception - in the other.
  config.Set("VRCycle", true, "Input");
  config.Set("VRDefaultView", 2, "Input", 0, 3);
#if !defined(SUPERMODEL_OPENGL_ONLY)
  // Hardware MSAA for the Vulkan backend. 0 disables it; 2, 4 and 8 select that many samples.
  // Smooths polygon edges at rasterisation time, leaving texture detail untouched. The list has to
  // match the settings window's combo - an option offered there but missing here is a setting the
  // generic writer treats as out of range.
  config.Set("MSAA", 0, "Video", 0, 0, { 0,2,4,8 });
  /*
   * Where the 3D scene gets its mip level.
   *
   * 0 is New3D's radial estimate, which is what the Real3D hardware does: the
   * polygon's texture-NP factor divided by w squared, isotropic across the
   * polygon. 1 takes the level from the derivatives of the interpolated uv
   * instead, so a surface seen at a grazing angle keeps the detail its own pixel
   * footprint supports; 2 also takes four taps along a stretched footprint.
   *
   * Both alternatives are sharper than the hardware rather than identical to it,
   * so the faithful behaviour stays the default. Clamped to 0..2 in CVulkan3D::Init.
   */
  config.Set("TexFilter", 0, "Video", 0, 0, { 0,1,2 });
  /*
   * Exact quad interpolation for the Vulkan scene shader.
   *
   * On is the default: pulling four corners and solving the quad's own (s, t) is
   * what removes the diagonal crease the two-triangle split would otherwise leave
   * on a non-planar quad. Off takes the rasteriser's interpolated attributes
   * instead, which skips four 64-byte storage reads and a quadratic solve per
   * pixel - the ultra-low preset's choice for integrated GPUs. Read once, at
   * session start, like TexFilter.
   */
  config.Set("QuadInterpolation", true, "Video");
  /*
   * Transparency ordering.
   *
   * 0 is New3D's two fixed transparency layers: each layer keeps one surface per pixel, both are
   * composited at the end of the frame, and it is bit-for-bit the picture this backend has always
   * produced. 1 replaces them with a per-pixel sorted list: every translucent fragment that passes
   * the depth test against the current priority's opaque geometry is stored, sorted back to front
   * and composited before the next priority begins. 2 reproduces the board itself: the two layers
   * own opposite halves of a checkerboard, so half of the pixels keep the opaque surface behind a
   * dense effect instead of the effect turning solid where both layers cover it.
   *
   * The sorted mode fixes two things the two-layer scheme cannot: more than two translucent surfaces
   * at one pixel (smoke, spray, foliage, glass) and translucent polygons submitted in the wrong
   * depth order. It costs one full-screen resolve per priority that contains transparency, plus a
   * node pool of one node per scene pixel. Read once, at session start, like MSAA.
   */
  config.Set("AlphaSortMode", 0, "Video", 0, 0, { 0,1,2 });
  /*
   * HD texture packs.
   *
   * LoadHDTextures looks for replacements under Data/Assets/HDTextures/<romset>/.
   * DumpHDTextures exports every base-level sub-texture that is drawn to
   * Data/Assets/DumpTextures/<romset>/ as a PNG named after its content hash, so
   * the same file can be dropped into an HD pack unchanged. Both are per-session
   * switches: the renderer reads them every simulation frame, and the panel's
   * Reload button (or F3) rescans the folder without restarting the game.
   * F4 itself switches the replacements on and off for an A/B against the original.
   *
   * The key is DumpHDTextures rather than DumpTextures: the older key already
   * means "write the raw packed texture RAM on exit" and is still honoured.
   */
  config.Set("LoadHDTextures", false, "Core");
  config.Set("DumpHDTextures", false, "Core");
  // Post-processing filters, applied to the finished frame by the Vulkan backend. FXAA smooths
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
#endif
  config.Set("CRTcolors", int(0), "Video", 0, 0, { 0,1,2,3,4,5 });      // these might be more user friendly as strings
  config.Set("UpscaleMode", 2, "Video", 0, 0, { 0,1,2,3 });             // to do make strings
  // Sinden light-gun border. Width is in output pixels (the Vulkan pass clamps it to half the
  // smaller dimension); brightness is a percentage like the other picture controls.
  config.Set("SindenBorder", false, "Video");
  config.Set("SindenBorderWidth", 16, "Video", 0, 256);
  config.Set("SindenBorderBrightness", 100, "Video", 0, 100);
  config.Set("WideScreen", false, "Video");
  config.Set("Stretch", false, "Video");
  config.Set("WideBackground", false, "Video");
  config.Set("VSync", true, "Video");
  config.Set("Throttle", true, "Video");
  config.Set("RefreshRate", 60.0f, "Video", 0.0f, 0.0f, { 57.52416f,60.f });
  // Presentation pacing. Original keeps the arcade rate (and needs a 57.5 Hz-capable display or
  // VRR); Auto follows the display's current mode and interpolates; Fixed uses FixedRefreshRate.
  // RefreshRate remains only as the legacy migration source and is no longer what paces the loop.
  config.Set("RefreshRateMode", 1, "Video", 0, 6, { 0,1,2,3,4,5,6 });
  config.Set("FixedRefreshRate", 120, "Video", 0, 0, { 60,75,90,120,144,165,240,480 });
  // Test hook: pretend the display runs at this rate, for exercising the multiples
  // a machine cannot actually reach. 0 uses the real display mode.
  config.Set("PaceAssumePanelHz", 0.0f, "Video", 0.0f, 1000.0f);
  // Motion interpolation is Vulkan-only. LowLatency disables it without changing the present rate;
  // the default is smooth (one simulation step of latency).
  config.Set("MotionInterpolation", true, "Video");
  config.Set("LowLatency", false, "Video");
  // 0 = auto (full-screen and moving viewports), 1 = all viewports, 2 = main viewport only.
  config.Set("InterpolationScope", 0, "Video", 0, 2, { 0,1,2 });
  config.Set("CameraCutAngleDegrees", 15.0f, "Video", 1.0f, 90.0f);
  config.Set("InstanceCutAngleDegrees", 30.0f, "Video", 1.0f, 180.0f);
  config.Set("CutDistanceScale", 8.0f, "Video", 1.0f, 64.0f);
  // Interpolate meshes that move under their own matrix (cars, wheels) as well as the camera.
  // Off by default: interpolating each instance independently opens sub-pixel seams between models
  // that share an edge but not a matrix slot, which shows up as a white line or a fog-coloured
  // triangle on the road. Pairs are still validated - same geometry, unambiguous in both frames,
  // continuous motion - so turning it on can only fall back to the current matrix, never draw a tile
  // somewhere it never was.
  config.Set("InstanceInterpolation", false, "Video");
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
  /*
   * Colour preset for the statistics readout. 0 is the two-colour amber scheme, 1 is slate labels
   * with warm sand values, 2 is the high-contrast plate, 3 is the no-plate outline. An allow-list
   * rather than a free integer: a hand-edited file cannot select a palette that does not exist.
   */
  config.Set("StatsOsdStyle", 0, "Video", 0, 0, { 0,1,2,3 });
  config.Set("Crosshairs", int(0), "Video", 0, 0, { 0,1,2,3 });
  config.Set<std::string>("CrosshairStyle", "vector", "Video", "", "", { "bmp","vector" });
  config.Set("NoWhiteFlash", false, "Video");
  config.Set("FlipStereo", false, "Sound");
#if defined(SUPERMODEL_WIN32) && defined(SUPERMODEL_PLATFORM_SDL2)
  // XInput by default: nearly every pad on a modern PC presents itself as an Xbox controller, and on
  // XInput each trigger gets its own axis, so the brake binds without the pairing workaround
  // DirectInput needs (see the trigger notice on the Controllers tab). DirectInput is still there
  // for older wheels and flight sticks that XInput cannot see at all. The SDL3 build has no such
  // backend and gets its default below.
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
#else
  // The SDL3 and non-Windows builds compile only the SDL input backends. Defaulting to "xinput"
  // here made every session log a fallback to SDL Gamepad while the Controllers tab showed "SDL",
  // because the stored value did not match any chooser entry.
  #if defined(SUPERMODEL_PLATFORM_SDL3)
  config.Set<std::string>("InputSystem", "sdlgamepad", "Core", "", "", { "sdl","sdlgamepad" });
  #else
  config.Set<std::string>("InputSystem", "sdl", "Core", "", "", { "sdl","sdlgamepad" });
  #endif
#endif
  // SDL ForceFeedback (the only backend shared by every build)
  config.Set("SDLConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLConstForceThreshold", 30, "ForceFeedback", 0, 100);
  // NetBoard
  //
  // On by default, and PortIn == PortOut on the loopback address. That pairing is what lets a single
  // machine satisfy its own link check: CSimNetBoard recognises it as a self-link and loops the
  // shared segment back in process, so no socket, listener or network stack is involved. That also
  // covers the SDL3 executable, which is built without SDL3_net.
  //
  // Daytona 2, Scud Race, Sega Rally 2, Le Mans 24, Von 2, Dirt Devils and the Harley and Spikeout
  // boards will not boot past the network self-test without this, so leaving Network off is not an
  // option; do NOT give the two ports different values to "fix" anything: that is exactly what
  // breaks it.
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

  // Steering smoothing for gamepad players. The arcade cabinet has a 270-degree wheel with
  // physical damping; a thumbstick is a step input and the driving games' chase cameras react to
  // it. 0 = off, 1 = light, 2 = medium, 3 = strong.
  config.Set("GamepadSteeringSmoothing", 0, "Sensitivity", 0, 3, { 0,1,2,3 });

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
		/*
		 * The four gun/conversion triggers below are switch inputs, and a switch source only accepts
		 * one half of an axis. "JOY1_RZAXIS" parses as a full axis and is rejected - for P1 that was
		 * invisible because the keyboard and mouse alternatives in the same list survived, but P2 had
		 * nothing else and became NONE (with an "Unable to map" line in the log).
		 */
		{ "InputAnalogJoyTrigger",     "KEY_A,JOY1_RZAXIS_POS,MOUSE_LEFT_BUTTON" },
		{ "InputAnalogJoyEvent",       "KEY_S,JOY1_ZAXIS_POS,MOUSE_RIGHT_BUTTON" },
		{ "InputAnalogJoyTrigger2",    "JOY2_BUTTON1" },

		// Light guns (Lost World): aimed with the left stick
		{ "InputGunLeft",              "KEY_LEFT" },
		{ "InputGunRight",             "KEY_RIGHT" },
		{ "InputGunUp",                "KEY_UP" },
		{ "InputGunDown",              "KEY_DOWN" },
		{ "InputGunX",                 "MOUSE_XAXIS,JOY1_XAXIS" },
		{ "InputGunY",                 "MOUSE_YAXIS,JOY1_YAXIS" },
		{ "InputTrigger",              "KEY_A,JOY1_RZAXIS_POS,MOUSE_LEFT_BUTTON" },
		{ "InputOffscreen",            "KEY_S,JOY1_ZAXIS_POS,MOUSE_RIGHT_BUTTON" },
		{ "InputGunX2",                "JOY2_XAXIS" },
		{ "InputGunY2",                "JOY2_YAXIS" },
		{ "InputTrigger2",             "JOY2_RZAXIS_POS" },
		{ "InputOffscreen2",           "JOY2_ZAXIS_POS" },

		// Analog guns (Ocean Hunter, LA Machineguns)
		{ "InputAnalogGunLeft",        "KEY_LEFT" },
		{ "InputAnalogGunRight",       "KEY_RIGHT" },
		{ "InputAnalogGunUp",          "KEY_UP" },
		{ "InputAnalogGunDown",        "KEY_DOWN" },
		{ "InputAnalogGunX",           "MOUSE_XAXIS,JOY1_XAXIS" },
		{ "InputAnalogGunY",           "MOUSE_YAXIS,JOY1_YAXIS" },
		{ "InputAnalogTriggerLeft",    "KEY_A,JOY1_ZAXIS_POS,MOUSE_LEFT_BUTTON" },
		{ "InputAnalogTriggerRight",   "KEY_S,JOY1_RZAXIS_POS,MOUSE_RIGHT_BUTTON" },

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

	if (!OSD::Platform::Initialize(OSD::Platform::Subsystem_Gamepad))
		return info;

	const int count = OSD::Platform::GetGamepadCount();
	if (count <= 0)
		return info;

	// Prefer a pad the platform recognises as a standard game controller, otherwise take the first
	// joystick, so that wheels and other odd devices still get an answer.
	int chosen = -1;
	for (int i = 0; i < count; i++)
	{
		OSD::Platform::GamepadInfo pad{};
		if (OSD::Platform::GetGamepadInfo(i, &pad) && pad.isGamepad)
		{
			chosen = i;
			break;
		}
	}
	if (chosen < 0)
		chosen = 0;

	OSD::Platform::GamepadInfo pad{};
	if (OSD::Platform::GetGamepadInfo(chosen, &pad))
	{
		info.sdlKnowsDevice = pad.isGamepad;
		if (pad.name[0] != '\0')
			info.name = pad.name;
	}

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
	// SDL's XInput backend names every wired Xbox 360-class pad "XInput Controller #N": the product
	// string lives on the XInput device, not on the HID interface SDL can read. Being on that backend
	// is itself proof that the pad is XInput-compatible (a Razer Onza reports exactly this name), and
	// the generation cannot be told apart from it, so it lands on 360.
	else if (contains("xinput controller"))
		info.family = "xbox360";
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
	 * On the SDL2 build Xbox pads are best driven through XInput: that is where force feedback and
	 * the separate triggers come from. The SDL3 build has no such backend, so it must recommend a
	 * value the Controllers tab can actually select - otherwise the detection line proposes "xinput"
	 * while the dropdown offers only "SDL" and "SDL Gamepad".
	 *
	 * 8BitDo pads are the opposite case - XInput does not see them in their Bluetooth modes at all,
	 * and SDL has explicit mapping entries for the whole family including the SN30 Pro+ (three
	 * GUIDs, plus the Switch Pro Controller entries it uses in Switch mode), which is what
	 * normalises their buttons to the standard numbering the "standard" preset is written against.
	 * So they go to sdlgamepad even though they are not Sony hardware.
	 *
	 * Everything else goes to the GameController API when SDL recognises it, and falls back to the
	 * raw joystick path when it does not - a raw joystick has no normalised button numbering, so the
	 * preset's JOY1_BUTTONn bindings cannot be trusted there.
	 */
	const bool xboxFamily = (info.family == "xbox360" || info.family == "xboxone" || info.family == "xboxseries");
#if defined(SUPERMODEL_PLATFORM_SDL2)
	if (xboxFamily)
		info.inputSystem = "xinput";
	else if (info.sdlKnowsDevice)
		info.inputSystem = "sdlgamepad";
	else
		info.inputSystem = "dinput";
#else
	// Only the SDL backends exist here. An Xbox pad is still read through SDL's XInput backing
	// (separate triggers, rumble); "sdlgamepad" is the name of that path in this build.
	(void)xboxFamily;
	info.inputSystem = info.sdlKnowsDevice ? "sdlgamepad" : "sdl";
#endif

	return info;
}

static void Title(void)
{
#if defined(SUPERMODEL_PURE_VULKAN)
  // Deliberately one line: this is the banner users see in the console window. The version stays in
  // the string because this line is the quickest way to identify a build from a pasted console log;
  // the former project name is kept in parentheses so the rename is unambiguous.
  puts("Model 3-Vulkan v" MODEL3VULKAN_VERSION " - Igloo-Garage");
  puts(MODEL3VULKAN_REPO_URL "  |  " MODEL3VULKAN_LEGAL_SHORT);
  puts(MODEL3VULKAN_TRADEMARK_NOTICE);
#else
  puts(MODEL3VULKAN_TITLE_FULL " - a Sega Model 3 arcade emulator.");
  puts("Based on Supermodel " MODEL3VULKAN_UPSTREAM_RELEASE " (core version " SUPERMODEL_VERSION ").");
  puts("Copyright 2003-2026 by The Supermodel Team");
  puts(MODEL3VULKAN_COPYRIGHT);
  puts(MODEL3VULKAN_REPO_URL "  |  " MODEL3VULKAN_LEGAL_SHORT);
  puts(MODEL3VULKAN_TRADEMARK_NOTICE);
#endif
}

static void Help(void)
{
  Util::Config::Node defaultConfig = DefaultConfig();
#if defined(SUPERMODEL_PURE_VULKAN)
  puts("Usage: Model3-Vulkan <romset> [options]");
#else
  puts("Usage: Supermodel <romset> [options]");
#endif
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
#if !defined(SUPERMODEL_OPENGL_ONLY)
  puts("  -vulkan3d               Vulkan 1.3 Real3D backend (bindless textures)");
  puts("  -vulkan-device=<name>   Vulkan device name to render with (see the settings window)");
  puts("  -vk-validation          Enable Vulkan validation layers with -vulkan3d");
#endif
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
#if !defined(SUPERMODEL_OPENGL_ONLY)
    { "-vulkan-device",         "VulkanDevice"            },
#endif
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
#if !defined(SUPERMODEL_OPENGL_ONLY)
    { "-vulkan3d",            { "Vulkan3DEngine",   true } },
    { "-vk-validation",       { "VkValidation",     true } },
#endif
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
#if defined(SUPERMODEL_OPENGL_ONLY)
      /*
       * Options retired with the Vulkan renderer.
       *
       * Swallowed with one explanatory line instead of falling through to
       * "Unrecognized option" and exiting: command lines are copied between the
       * two executables, and a launch script written for the Vulkan build must
       * still start a game here.  The option's value is ignored; the renderer is
       * always OpenGL.
       */
      {
        const std::string option = arg.substr(0, arg.find_first_of('='));
        static const char* const retired[] = {
          "-vulkan3d", "-vulkan", "-vk-validation", "-vulkan-device"
        };
        bool swallowed = false;
        for (const char* candidate : retired)
        {
          if (option == candidate)
          {
            std::cout << "Note: " << candidate
                      << " is not available in this build (OpenGL only); "
                         "continuing with the OpenGL renderer." << std::endl;
            swallowed = true;
            break;
          }
        }
        if (swallowed)
          continue;
      }
#endif
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
      /*
       * Presentation multiples, spelled the way they are talked about.
       *
       * These are switches rather than valued options because the value is the
       * spelling itself: "-refresh-rate-mode 4" is the storage format, but nobody
       * remembers which number is 4x. The numeric form still works - it is what
       * the ini and the settings window use.
       */
      else if (arg == "-vrr-1x" || arg == "-vrr-2x" || arg == "-vrr-3x" ||
               arg == "-vrr-4x" || arg == "-vrr-8x")
      {
        int mode = 0;
        if (arg == "-vrr-2x") mode = 3;
        else if (arg == "-vrr-3x") mode = 4;
        else if (arg == "-vrr-4x") mode = 5;
        else if (arg == "-vrr-8x") mode = 6;
        cmd_line.config.Set("RefreshRateMode", mode);
      }
      else if (arg == "-refresh-rate-mode" || arg.find("-refresh-rate-mode=") == 0)
      {
        // Parsed as an integer rather than through valued_options, which stores
        // strings: RefreshRateMode is read as an int everywhere, and a string
        // node would not answer ValueAs<int>().
        const std::string::size_type eq = arg.find('=');
        const int mode = eq == std::string::npos ? -1 : atoi(arg.c_str() + eq + 1);
        if (mode < 0 || mode > 6)
        {
          ErrorLog("'-refresh-rate-mode' takes 0-6 (0=arcade, 1=auto, 2=fixed, 3..6 = 2x/3x/4x/8x).");
          cmd_line.error = true;
        }
        else
          cmd_line.config.Set("RefreshRateMode", mode);
      }
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
        cmd_line.config.Set("RefreshRate", 57.52416f);
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
#if defined(SUPERMODEL_OPENGL_ONLY)
/*
 * Settings that belonged to the Vulkan renderer, removed from every section of
 * a configuration file this build reads.
 *
 * Neither the default configuration nor the settings window creates these keys
 * any more; this covers an existing Supermodel.ini written by the previous
 * dual-renderer build.  MergeINISections() keeps unknown keys from the file, so
 * without this they would stay in the file (and in a game section they would be
 * merged back into the run-time configuration) forever, describing a renderer
 * this executable does not contain.
 *
 * Bezel* and Post* are Vulkan2D's compositor and post-processing chain; MSAA is
 * created with the Vulkan render targets.  Supersampling and CRTcolors are not
 * in the list: those are the OpenGL anti-aliasing and colour paths and remain.
 */
static void RemoveRetiredVulkanSettings(Util::Config::Node &config)
{
  Util::Config::StripRetiredVulkanSettings(config);
}
#endif

/*
 * Switch inputs the old standard preset bound to a whole joystick axis.
 *
 * A switch source takes one half of an axis ("JOY1_RZAXIS_POS"); a bare "JOY1_RZAXIS" parses as a
 * full axis and is rejected. Player 1's gun trigger only looked fine because the keyboard and mouse
 * alternatives in the same list still parsed - the pad half was silently dropped. Player 2's entries
 * had no alternative at all, so they became NONE and the log carried
 * "Unable to map input Trigger2 ...". The preset itself is fixed; this repairs an .ini written by an
 * older build, which the settings window would otherwise write back unchanged.
 */
static bool IsBareJoyAxisName(const std::string &name)
{
  std::string upper;
  upper.reserve(name.size());
  for (char c : name)
    upper.push_back((char)toupper((unsigned char)c));

  if (upper.rfind("JOY", 0) != 0)
    return false;

  const size_t underscore = upper.find('_');
  if (underscore == std::string::npos)
    return false;

  const std::string part = upper.substr(underscore + 1);
  static const char *axes[] = {
    "XAXIS", "YAXIS", "ZAXIS", "RXAXIS", "RYAXIS", "RZAXIS",
    "AXIS1", "AXIS2", "AXIS3", "AXIS4", "AXIS5", "AXIS6", "AXIS7", "AXIS8",
    "SLIDER1", "SLIDER2",
  };
  for (const char *axis : axes)
    if (part == axis)
      return true;
  return false;
}

// Rewrites only the joystick tokens of an and/or list, preserving separators and key/mouse tokens.
static std::string RepairSwitchAxisMapping(const std::string &mapping, bool *changed)
{
  std::string result;
  size_t i = 0;
  while (i < mapping.size())
  {
    size_t end = mapping.find_first_of("+,", i);
    if (end == std::string::npos)
      end = mapping.size();

    const std::string token = mapping.substr(i, end - i);
    const size_t begin = token.find_first_not_of(" \t");
    const size_t last = token.find_last_not_of(" \t");
    std::string core = (begin == std::string::npos) ? std::string()
                       : token.substr(begin, last - begin + 1);

    if (IsBareJoyAxisName(core))
    {
      core += "_POS";
      *changed = true;
    }

    result += core;
    if (end < mapping.size())
      result += mapping[end];
    i = end + 1;
  }
  return result;
}

static void RepairSwitchAxisMappings(Util::Config::Node &config)
{
  static const char *keys[] = {
    "InputTrigger", "InputOffscreen", "InputTrigger2", "InputOffscreen2",
    "InputAnalogTriggerLeft", "InputAnalogTriggerRight",
    "InputAnalogTriggerLeft2", "InputAnalogTriggerRight2",
    "InputAnalogJoyTrigger", "InputAnalogJoyTrigger2",
  };

  for (const char *key : keys)
  {
    Util::Config::Node *node = config.TryGet(key);
    if (node == nullptr || node->Empty())
      continue;

    const std::string mapping = node->ValueAs<std::string>();
    bool changed = false;
    const std::string repaired = RepairSwitchAxisMapping(mapping, &changed);
    if (!changed)
      continue;

    node->SetValue(repaired);
    InfoLog("Input: '%s' was '%s' - a switch input cannot use a whole axis; using '%s'.",
      key, mapping.c_str(), repaired.c_str());
  }
}

/*
 * The "arcade" RefreshRate preset used to store 57.5 Hz.
 *
 * The old OpenGL loop paces itself from this key, so 57.5 made every presented frame 17,391,304 ns
 * long while the emulator advances 17,384,000 ns of emulated time per frame (see kModel3StepNs).
 * The game therefore ran 0.04% slow against the wall clock, and - because the audio board also
 * produces one frame's worth of samples per frame - the output ring drained at about 19 samples per
 * second. That is invisible for a single lap and turns into a periodic under-run on a long session.
 *
 * The preset now stores the arcade rate itself, and GetDesiredFramePeriodNs() turns it back into
 * 17,384,000 ns without rounding to whole millihertz. An ini written by an older build still says
 * 57.5, so it is rewritten here
 * in memory (and saved the next time the settings window writes the file). Anything else the user
 * chose is left alone.
 */
static void AlignLegacyArcadeRefreshRate(Util::Config::Node &config, bool report)
{
  Util::Config::Node *node = config.TryGet("RefreshRate");
  if (node == nullptr || node->Empty())
    return;

  const float hz = node->ValueAs<float>();
  if (std::fabs(hz - 57.5f) > 0.01f)
    return;

  node->SetValue(57.52416f);
  if (report)
  {
    InfoLog("Pace: RefreshRate %.2f -> 57.52416 Hz (the old arcade preset stored an approximation; "
      "the loop ran 0.04%% slow and slowly drained the audio buffer).", hz);
  }
}

/*
 * The SDL3 build compiles only the SDL input backends. An ini written by the SDL2 build can name
 * DirectInput/XInput/Raw Input; GetInputSystem() already falls back to SDL Gamepad, but leaving the
 * stored value alone made the Controllers tab show "SDL" (the combo had no matching entry) while
 * the file said something else. Rewrite it once, in memory, and let the next settings save persist
 * it. A per-game section can still name an old backend, so GetInputSystem() keeps its own fallback.
 */
static void AlignLegacyInputSystem(Util::Config::Node &config, bool report)
{
#if !defined(SUPERMODEL_PLATFORM_SDL2)
  Util::Config::Node *node = config.TryGet("InputSystem");
  if (node == nullptr || node->Empty())
    return;

  const std::string system = node->ValueAs<std::string>();
  if (system == "sdl" || system == "sdlgamepad")
    return;

  node->SetValue(std::string("sdlgamepad"));
  if (report)
    InfoLog("Input: '%s' is not available in the SDL3 build; using 'sdlgamepad'.", system.c_str());
#else
  (void)config;
  (void)report;
#endif
}

/*
 * Per-game default audio balance.
 *
 * On the real cabinet the DSB sub-board's output is attenuated before it reaches the mixer, so the
 * flat 100/100 split the emulator has always used is wrong for the games whose sound ROM expects
 * that divider: engine effects sit far too loud against the music in Scud Race and Daytona 2, and
 * the opposite in Star Wars Trilogy.
 *
 * These are defaults only. Anything the user has chosen wins, per key: a value in the global .ini
 * section, a value in the game's own section, or -sound-volume / -music-volume on the command line.
 * That is why the check reads the raw sources rather than the resolved configuration: DefaultConfig()
 * always puts SoundVolume and MusicVolume there, so their presence proves nothing.
 *
 * The global section is only an override when it differs from the stock default. The settings window
 * writes the whole merged global section back when it saves, so after one save from the GUI an
 * untouched 100 appears in the file and would otherwise silently disable every preset. A game that
 * really should keep 100 can say so in its own [game] section, which is always taken literally.
 */
struct PerGameAudioBalance
{
  const char *set;
  int soundVolume;
  int musicVolume;
};

static constexpr PerGameAudioBalance s_perGameAudioBalance[] =
{
  { "scud",      65, 100 },
  { "scudplus",  65, 100 },
  { "daytona2",  70, 100 },
  { "dayto2pe",  70, 100 },
  { "swtrilgy", 100,  70 },
  { "spikeout",  75, 100 },
  { "spikeofe",  75, 100 },
};

static const PerGameAudioBalance *FindPerGameAudioBalance(const Game &game)
{
  for (const PerGameAudioBalance &entry : s_perGameAudioBalance)
  {
    if (game.name == entry.set || (!game.parent.empty() && game.parent == entry.set))
      return &entry;
  }
  return nullptr;
}

static constexpr int kStockVolume = 100;   // DefaultConfig()'s value for both volume keys

static bool HasExplicitAudioVolume(const Util::Config::Node &ini, const std::string &gameName,
                                   const Util::Config::Node &cmdLine, const char *key)
{
  if (cmdLine.TryGet(key) != nullptr)
    return true;                                                  // -sound-volume / -music-volume
  if (const Util::Config::Node *section = ini.TryGet(gameName); section != nullptr &&
      section->TryGet(key) != nullptr)
    return true;                                                  // [game] section of the .ini
  if (const Util::Config::Node *global = ini.TryGet(key); global != nullptr)
    return global->ValueAsDefault<int>(kStockVolume) != kStockVolume;   // global section, changed
  return false;
}

static void ApplyPerGameAudioBalance(Util::Config::Node &config, const Game &game,
                                     const Util::Config::Node &ini, const Util::Config::Node &cmdLine)
{
  const PerGameAudioBalance *balance = FindPerGameAudioBalance(game);
  if (balance == nullptr)
    return;

  const bool soundSet = HasExplicitAudioVolume(ini, game.name, cmdLine, "SoundVolume");
  const bool musicSet = HasExplicitAudioVolume(ini, game.name, cmdLine, "MusicVolume");
  if (soundSet && musicSet)
    return;

  if (!soundSet)
    config.Set("SoundVolume", balance->soundVolume);
  if (!musicSet)
    config.Set("MusicVolume", balance->musicVolume);

  InfoLog("Sound: per-game balance for %s (SoundVolume=%d%%, MusicVolume=%d%%).",
      game.name.c_str(),
      soundSet ? config["SoundVolume"].ValueAs<int>() : balance->soundVolume,
      musicSet ? config["MusicVolume"].ValueAs<int>() : balance->musicVolume);
}

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
#if defined(SUPERMODEL_PURE_VULKAN)
    // This executable has no OpenGL renderer to query.  Say so rather than
    // failing inside a screen-creation path that was compiled out.
    puts("This build has no OpenGL renderer. Use -vulkan3d for the Vulkan backend.");
    return 0;
#else
    // Use command line options so we can select the 3D engine for which to print GL info 
    Util::Config::MergeINISections(&s_runtime_config, DefaultConfig(), cmd_line.config);

    // We must exit after this because CreateGLScreen() is used
    PrintGLInfo(true, false, false);
    return 0;
#endif
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
    // This is a second read of the same file (the settings window keeps its own copy from main()),
    // so the repair has to run here too - otherwise this merged configuration still carries the
    // rejected mappings.
    RepairSwitchAxisMappings(fileConfig);
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
      // The set comes from the archive's contents, not its file name, so a zip named after one
      // revision can run another. Saves and NVRAM are named after the set that actually runs, which
      // looks like a save under an unrelated name unless the resolution is stated somewhere.
      InfoLog("Game: '%s' resolves to set '%s' (%s, %s).", cmd_line.rom_files.begin()->c_str(),
          game.name.c_str(), game.title.c_str(), game.version.c_str());
      Util::Config::MergeINISections(&config4, config3, fileConfig[game.name]);   // apply game-specific config
      ApplyPerGameAudioBalance(config4, game, fileConfig, cmd_line.config);       // per-game default balance
    }
    else
      config4 = config3;
    Util::Config::MergeINISections(&s_runtime_config, config4, cmd_line.config);  // apply command line overrides once more
    // Same repair as the settings window's copy: a legacy 57.5 must not pace this session either.
    AlignLegacyArcadeRefreshRate(s_runtime_config, true);
    AlignLegacyInputSystem(s_runtime_config, true);
  }

  /*
   * Migrate the two-value RefreshRate preference into the new pacing model.
   *
   * RefreshRateMode is a new key, so an ini written by an older build does not
   * have it and the merged configuration shows the default (Auto). That would
   * silently change a 57.5 Hz user's presentation rate to wherever their panel
   * happens to run. If none of the three sources named the key, derive it from
   * what the old build would have done:
   *
   *   - exclusive arcade timing, or RefreshRate at/below the arcade rate -> Original;
   *   - otherwise the old loop was presenting at that rate -> Fixed at that rate.
   *
   * The simulation rate is 57.524 Hz in every case; only presentation changes.
   */
  {
    bool modeSpecified = false;
    if (fileConfig.TryGet("RefreshRateMode") != nullptr)
      modeSpecified = true;
    if (const Util::Config::Node* gameNode = fileConfig.TryGet(game.name); gameNode != nullptr && gameNode->TryGet("RefreshRateMode") != nullptr)
      modeSpecified = true;
    if (cmd_line.config.TryGet("RefreshRateMode") != nullptr)
      modeSpecified = true;

    // Remembered for the session: a mode nobody chose may still be replaced by Original once the
    // window exists and the display can be asked what it can actually present at.
    s_refreshModeExplicit = modeSpecified;

    if (!modeSpecified)
    {
      const float legacyHz = s_runtime_config["RefreshRate"].ValueAsDefault<float>(60.0f);
      const float exclusiveHz = s_runtime_config["ExclusiveRefreshRate"].ValueAsDefault<float>(0.0f);
      if (exclusiveHz > 0.0f || legacyHz <= 57.6f)
      {
        s_runtime_config.Set("RefreshRateMode", 0);
        InfoLog("Pace: migrated legacy RefreshRate %.4f to Original (arcade 57.524 Hz presentation).", legacyHz);
      }
      else
      {
        int hz = (int)std::lround(legacyHz);
        if (hz <= 60)
          hz = 60;
        else
        {
          static const int kRates[] = { 60, 120, 144, 165, 240, 480 };
          int best = kRates[0];
          int bestDelta = std::abs(hz - best);
          for (int candidate : kRates)
          {
            const int delta = std::abs(hz - candidate);
            if (delta < bestDelta)
            {
              best = candidate;
              bestDelta = delta;
            }
          }
          hz = best;
        }
        s_runtime_config.Set("RefreshRateMode", 2);
        s_runtime_config.Set("FixedRefreshRate", hz);
        InfoLog("Pace: migrated legacy RefreshRate %.4f to Fixed %d Hz presentation (logic stays 57.524 Hz).",
          legacyHz, hz);
      }
    }
  }

  // There is only one SCSP DSP implementation now: the reserved 0x7C0-0x7FF hole is discarded on
  // every write path (see devdocs/DSP.md), which is the behaviour that made Fighting Vipers 2 work on the
  // old path. The option and its -new-scsp/-legacy-scsp spellings are still parsed so existing
  // shortcuts, front-ends and .ini files keep working, but they no longer select anything.
  if (const Util::Config::Node *legacySoundDSP = s_runtime_config.TryGet("LegacySoundDSP");
      legacySoundDSP != nullptr && legacySoundDSP->ValueAsDefault<bool>(false))
    InfoLog("Sound: LegacySoundDSP is retired and ignored; the corrected SCSP DSP path is always used.");

#if defined(SUPERMODEL_OPENGL_ONLY)
  RemoveRetiredVulkanSettings(s_runtime_config);
#endif

#if defined(SUPERMODEL_PLATFORM_SDL3)
  /*
   * The SDL3 executable is locked to Vulkan: its settings surface is the Vulkan 1.3 backend, which
   * the startup check has already verified.  A -legacy3d, New3DEngine=0 or Vulkan3DEngine=0 from the
   * command line, the .ini or a game section is ignored here rather than being allowed to reach
   * CreateGLScreen().  The explicit log line matters because the setting still exists in the legacy
   * .ini files users will copy over.
   */
  if (!s_runtime_config["Vulkan3DEngine"].ValueAsDefault<bool>(true))
  {
    InfoLog("SDL3 build: OpenGL renderers are not part of this executable; forcing the Vulkan 1.3 renderer.");
  }
  s_runtime_config.Set("Vulkan3DEngine", true);
#endif

  // A control preset named on the command line overrides the individual input settings.
  ApplyPresetFromCommandLine(s_runtime_config, cmd_line.config["ControlPresetCLI"].ValueAsDefault<std::string>(""));

  LogConfig(s_runtime_config);

  // Flag as DPI-aware, otherwise the window content might be scaled by some graphics drivers. This
  // has to be set before the video subsystem comes up.
  OSD::Platform::SetDPIAwareness();

  // Initialize the platform layer with no subsystems; individual ones are brought up later.
  if (!OSD::Platform::Initialize(0))
  {
    ErrorLog("Unable to initialize SDL: %s\n", OSD::Platform::GetError());
    return 1;
  }

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
#if !defined(SUPERMODEL_OPENGL_ONLY)
  if (s_runtime_config["Vulkan3DEngine"].ValueAsDefault<bool>(false) && aaValue != 1)
  {
    InfoLog("Vulkan: supersampling is not available in this backend; using 1x.");
    aaValue = 1;
  }
#endif

  // Create a window
  xRes = s_runtime_config["XResolution"].ValueAsDefault<unsigned>(496);
  yRes = s_runtime_config["YResolution"].ValueAsDefault<unsigned>(384);

#if defined(SUPERMODEL_OPENGL_ONLY)
  // One renderer family, no run-time choice.  Any Vulkan3DEngine value left in
  // a hand-edited file is ignored.
  Result screen = CreateGLScreen(s_runtime_config["New3DEngine"].ValueAs<bool>(),
    MODEL3VULKAN_TITLE_FULL, false, &xOffset, &yOffset, &xRes, &yRes,
    &totalXRes, &totalYRes, false, false);
#elif defined(SUPERMODEL_PURE_VULKAN)
  const bool vulkanRequested = true;
  // The renderer is forced to Vulkan above; there is no GL path to select.
  (void)vulkanRequested;
  Result screen = CreateVulkanScreen(MODEL3VULKAN_TITLE_FULL, false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false);
#else
  const bool vulkanRequested = s_runtime_config["Vulkan3DEngine"].ValueAsDefault<bool>(false);
  Result screen = vulkanRequested
    ? CreateVulkanScreen(MODEL3VULKAN_TITLE_FULL, false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false)
    : CreateGLScreen(s_runtime_config["New3DEngine"].ValueAs<bool>(), MODEL3VULKAN_TITLE_FULL, false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false);
#endif

#if defined(SUPERMODEL_PLATFORM_SDL3)
  /*
   * No OpenGL fallback exists in this executable.  The startup probe accepted the machine, so a
   * failure here is a session-level problem (surface, swapchain, or a device the renderer refused
   * for another reason).  Say so and end cleanly instead of pretending to fall back to a renderer
   * that was not compiled into this binary.
   */
  if (Result::OKAY != screen && vulkanRequested)
  {
    const std::string message = std::format(
      "Vulkan 1.3 passed the startup check, but the game window could not be created.\n\n"
      "{}\n\n"
      "See Supermodel.log for the Vulkan device details. The OpenGL-compatible build "
      "(Supermodel-LegacyOG.exe) can be used if the problem persists.",
      OSD::Platform::GetError());
    ErrorLog("Vulkan: the session could not create its window or swapchain: %s", OSD::Platform::GetError());
    OSD::SDL3App::StartupPrint("Model3-Vulkan: {}", message);
    OSD::SDL3App::ShowFatalError("Model3-Vulkan - Vulkan session failed", message);
  }
#elif !defined(SUPERMODEL_OPENGL_ONLY)
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

    screen = CreateGLScreen(s_runtime_config["New3DEngine"].ValueAs<bool>(), MODEL3VULKAN_TITLE_FULL, false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false);
  }
#endif

  if (Result::OKAY != screen)
  {
    exitCode = 1;
    goto Exit;
  }

  // Create Crosshair (OpenGL only; the Vulkan backend draws its own)
#if !defined(SUPERMODEL_PURE_VULKAN)
#if !defined(SUPERMODEL_OPENGL_ONLY)
  if (!s_vkContext)
#endif
  {
    s_crosshair = new CCrosshair(s_runtime_config);
    if (s_crosshair->Init() != Result::OKAY)
    {
        ErrorLog("Unable to load bitmap crosshair texture\n");
        exitCode = 1;
        goto Exit;
    }
  }
#endif

  // Create Model 3 emulator
#ifdef DEBUG
  Model3 = s_gfxStatePath.empty() ? static_cast<IEmulator *>(new CModel3(s_runtime_config)) : static_cast<IEmulator *>(new CModel3GraphicsState(s_runtime_config, s_gfxStatePath));
#else
  Model3 = new CModel3(s_runtime_config);
#endif

  // Create input system
#if !defined(SUPERMODEL_PLATFORM_SDL2)
  // The SDL3 binary has no DirectInput/Raw Input backend, but both executables share one
  // Data/Config tree, so a configuration written for the SDL2 build may name one of them.
  if (selectedInputSystem != "sdl" && selectedInputSystem != "sdlgamepad")
  {
    InfoLog("Input: '%s' is not available in the SDL3 build; using 'sdlgamepad'.", selectedInputSystem.c_str());
    selectedInputSystem = "sdlgamepad";
  }
#endif

  if (selectedInputSystem == "sdl")
    InputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(s_runtime_config, false));
  else if (selectedInputSystem == "sdlgamepad")
    InputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(s_runtime_config, true));
#if defined(SUPERMODEL_WIN32) && defined(SUPERMODEL_PLATFORM_SDL2)
  else if (selectedInputSystem == "dinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_platformWindow, false, false));
  else if (selectedInputSystem == "xinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_platformWindow, false, true));
  else if (selectedInputSystem == "rawinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_platformWindow, true, false));
#endif // SUPERMODEL_WIN32 && SUPERMODEL_PLATFORM_SDL2
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
  if (s_platformWindow != nullptr)
    OSD::Platform::SetWindowMouseGrab(s_platformWindow, false);

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
#if !defined(SUPERMODEL_PURE_VULKAN)
  delete s_crosshair;
  s_crosshair = nullptr;
#endif
  InfoLog("Session: crosshair released.");

  // Note: SDL_Quit() is deliberately not called here. The settings GUI keeps its own window and
  // GL context alive across sessions, so SDL is shut down once in main() instead.
  DestroyGLScreen();
#if defined(SUPERMODEL_PURE_VULKAN)
  InfoLog("Session: game window and Vulkan context released.");
#else
  InfoLog("Session: game window and GL context released.");
#endif

  return exitCode;
}

/*
 * SDL3 startup gate.
 *
 * Runs before a ROM is loaded and before the main window is created:
 *
 *   1. resolve the per-executable settings file (Model3-Vulkan.ini),
 *   2. verify that the machine can run the Vulkan 1.3 pipeline at all.
 *
 * Both failures are fatal and reported with a modal message box.  The SDL2
 * build has neither step: it keeps Supermodel.ini and the Vulkan 1.2
 * extension fallback inside CVulkanContext.
 */
#if defined(SUPERMODEL_PLATFORM_SDL3)
static bool InitializeSDL3Startup()
{
  const auto startupClock = std::chrono::steady_clock::now();
  const auto startupMs = [&startupClock]()
  {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startupClock).count();
  };

  auto paths = OSD::SDL3App::DeriveRuntimePaths();
  if (!paths)
  {
    const std::string message = std::format(
      "The settings file for this build could not be resolved.\n\n{}\n\n"
      "The OpenGL-compatible build (Supermodel-LegacyOG.exe) is not affected.",
      paths.error().detail);
    OSD::SDL3App::StartupPrint("Model3-Vulkan: {}", message);
    OSD::SDL3App::ShowFatalError("Model3-Vulkan - settings path error", message);
    return false;
  }

  s_configFilePath = paths->configFile.string();

  /*
   * First run of this executable on an existing installation: seed its own settings file from the
   * shared Supermodel.ini so the ROM path, control mappings and display preferences carry over.
   * The OpenGL-only keys in that copy are ignored by this build (Vulkan is forced at session
   * start), but losing every setting on upgrade would look exactly like a factory reset.
   */
  {
    std::error_code existsError;
    const bool haveOwnSettings = std::filesystem::exists(paths->configFile, existsError) && !existsError;
    if (!haveOwnSettings)
    {
      const std::filesystem::path configDirectory = paths->configFile.parent_path();
      const char* const previousNames[] = { "Supermodel.ini" };
      for (const char* name : previousNames)
      {
        const std::filesystem::path source = configDirectory / name;
        std::error_code sourceError;
        if (!std::filesystem::is_regular_file(source, sourceError) || sourceError)
          continue;

        std::error_code copyError;
        std::filesystem::copy_file(source, paths->configFile,
          std::filesystem::copy_options::none, copyError);
        if (copyError)
        {
          OSD::SDL3App::StartupPrint("Model3-Vulkan: could not seed settings from '{}': {}",
            source.string(), copyError.message());
          break;
        }
        OSD::SDL3App::StartupPrint("Model3-Vulkan: seeded settings from '{}'.", source.string());
        break;
      }
    }
  }

  OSD::SDL3App::StartupPrint("Model3-Vulkan: settings file: {}", s_configFilePath);
  OSD::SDL3App::StartupPrint("Model3-Vulkan: settings resolved ({:.0f} ms).", startupMs());

#if defined(SUPERMODEL_PURE_VULKAN)
  /*
   * Cold-path module check.  The C++ configuration system remains the source of truth in this stage;
   * this call verifies that the native module loads and can parse the same file, and records what it
   * saw.  Moving individual settings over to HostCoreConfig is the next migration step and can be
   * done one setting at a time without touching the engine.
   */
  if (auto host = OSD::HostCore::Shared(); !host)
  {
    OSD::SDL3App::StartupPrint("Model3-Vulkan: host core unavailable: {}", host.error().message);
  }
  else
  {
    OSD::SDL3App::StartupPrint("Model3-Vulkan: host core: {}", (*host)->Version());
    auto hostConfig = (*host)->LoadIni(paths->configFile);
    if (hostConfig)
    {
      OSD::SDL3App::StartupPrint(
        "Model3-Vulkan: host core ini: {}x{}, MSAA {}, mouse {} Hz",
        hostConfig->XResolution, hostConfig->YResolution, hostConfig->Msaa, hostConfig->MousePollingHz);
    }
    else
    {
      OSD::SDL3App::StartupPrint("Model3-Vulkan: host core ini failed: {}",
        hostConfig.error().message);
    }
  }
#endif
  OSD::SDL3App::StartupPrint("Model3-Vulkan: startup layer ready ({:.0f} ms).", startupMs());

  auto probe = OSD::SDL3App::ProbeVulkan13();
  if (!probe)
  {
    OSD::SDL3App::ReportVulkanRequirementFailure(probe.error());
    return false;
  }

  OSD::SDL3App::StartupPrint("Model3-Vulkan: startup checks done ({:.0f} ms).", startupMs());
  return true;
}
#endif

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

#if defined(SUPERMODEL_PLATFORM_SDL3)
  // The executable-specific .ini and the Vulkan 1.3 requirement are both decided here, while there
  // is still nothing to tear down and no window the user could mistake for a working session.
  //
  // Caught as a last line of defence: a diagnostic write must never be able to take the process down
  // before the message box (see StartupLog.h).
  try
  {
    if (!InitializeSDL3Startup())
      return 1;
  }
  catch (const std::exception& error)
  {
    const std::string message = std::format(
      "Model3-Vulkan could not complete its startup checks.\n\n{}", error.what());
    OSD::SDL3App::ShowFatalError("Model3-Vulkan - startup error", message);
    return 1;
  }
#endif

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
  // Before the copy is handed to the settings window (which writes it back) and before it is merged
  // into the run-time configuration, so both the session and the next save see the repaired values.
  RepairSwitchAxisMappings(fileConfig);
  AlignLegacyArcadeRefreshRate(fileConfig, false);
  AlignLegacyInputSystem(fileConfig, false);
#if defined(SUPERMODEL_OPENGL_ONLY)
  // Must happen before the file is handed to the settings window: that copy is
  // what gets written back, so cleaning it here is what keeps retired Vulkan
  // keys out of the file instead of merely ignoring them at run time.
  RemoveRetiredVulkanSettings(fileConfig);
#endif

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

  OSD::Platform::Shutdown();

  if (exitCode)
    InfoLog("Program terminated due to an error.");
  else
    InfoLog("Program terminated normally.");

  return exitCode;
}

