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

/*
 * Platform.h
 *
 * Backend-neutral window, display, GL-context and monotonic-clock API.
 *
 * This is the seam that lets one OSD layer compile against either SDL2 or
 * SDL3.  It is deliberately a namespace of free functions rather than an
 * abstract base class: exactly one backend translation unit is linked into a
 * binary, so every call is a direct call that LTO can inline.  There is no
 * vtable, no pimpl, no std::function and no runtime backend switch.
 *
 * Rules for this header:
 *   - It must not include any SDL header, and no SDL type may appear in it.
 *   - Window, GLContext and gamepad handles are opaque.
 *   - Anything that differs between SDL2 and SDL3 in a way shared code must
 *     care about is represented by a capability constant below, so the
 *     difference is resolved with `if constexpr`, not with a run-time test.
 *
 * See PlatformInput.h for keyboard/mouse/gamepad state and PlatformVulkan.h
 * for the Vulkan surface handshake.
 */

#ifndef INCLUDED_PLATFORM_H
#define INCLUDED_PLATFORM_H

#include <cstdint>
#include <string>

/*
 * Exactly one backend has to be selected by the build system.  This is an
 * error rather than a default because a binary that silently picked the wrong
 * backend would link SDL2 calls against SDL3 symbols - they have the same
 * names and different ABIs.
 */
#if defined(SUPERMODEL_PLATFORM_SDL2) == defined(SUPERMODEL_PLATFORM_SDL3)
#error "Select exactly one platform backend: SUPERMODEL_PLATFORM_SDL2 or SUPERMODEL_PLATFORM_SDL3"
#endif

namespace OSD
{
namespace Platform
{

enum class Backend
{
    SDL2,
    SDL3
};

#if defined(SUPERMODEL_PLATFORM_SDL3)
constexpr Backend kBackend = Backend::SDL3;
#else
constexpr Backend kBackend = Backend::SDL2;
#endif

/*
 * Capabilities that shared code is allowed to exploit.
 *
 * These are compile-time constants on purpose.  A shared file can write
 *
 *     if constexpr (kHasFloatMouseMotion) { ... }
 *
 * and the branch that does not apply to the linked backend is removed by the
 * compiler.  Nothing in the hot path tests a backend enum at run time.
 */
constexpr bool kHasNanosecondClock = (kBackend == Backend::SDL3);
constexpr bool kHasFloatMouseMotion = (kBackend == Backend::SDL3);
constexpr bool kHasFloatMousePosition = (kBackend == Backend::SDL3);
constexpr bool kHasPerWindowRelativeMouse = (kBackend == Backend::SDL3);

using Nanoseconds = int64_t;
constexpr Nanoseconds kNanosecondsPerSecond = 1000000000;

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct DisplayMode
{
    int width = 0;
    int height = 0;
    float refreshHz = 0.0f;
};

/*
 * A display id is stable for the life of the process, unlike an enumeration
 * index: SDL2 renumbers indices when a monitor is plugged in, and SDL3 uses
 * ids for the same reason.  kPrimaryDisplay is accepted anywhere an id is
 * expected and means "the system's primary output".
 */
using DisplayId = uint32_t;
constexpr DisplayId kPrimaryDisplay = 0;

struct DisplayInfo
{
    DisplayId id = kPrimaryDisplay;
    int index = 0;                  // enumeration order, for legacy call sites
    const char* name = nullptr;     // owned by the backend, valid until the next query
    Rect bounds;
    Rect usableBounds;
    bool primary = false;
};

/*
 * Opaque handles.  The definitions live in the selected backend's .cpp file,
 * which is the only place that knows what an SDL_Window or SDL_GLContext is.
 */
struct Window;
struct GLContext;

enum WindowFlag : uint32_t
{
    WindowFlag_None = 0,
    WindowFlag_OpenGL = 1u << 0,
    WindowFlag_Vulkan = 1u << 1,
    WindowFlag_Resizable = 1u << 2,
    WindowFlag_HighDPI = 1u << 3,
    WindowFlag_Hidden = 1u << 4
};

struct WindowDesc
{
    const char* title = "";
    int width = 0;
    int height = 0;
    DisplayId display = kPrimaryDisplay;
    bool fullscreenDesktop = false;
    uint32_t flags = WindowFlag_None;
};

struct GLAttributes
{
    int majorVersion = 4;
    int minorVersion = 1;
    int redSize = 8;
    int greenSize = 8;
    int blueSize = 8;
    int depthSize = 24;
    int stencilSize = 8;
    bool coreProfile = true;
    bool doubleBuffer = true;
};

enum SubsystemFlag : uint32_t
{
    Subsystem_Video = 1u << 0,
    Subsystem_Audio = 1u << 1,
    Subsystem_Gamepad = 1u << 2,
    Subsystem_Events = 1u << 3,
    // Effect-based haptics (constant force, spring, friction). SDL3 keeps this separate from the
    // gamepad subsystem, and a wheel with no rumble motors still needs it.
    Subsystem_Haptic = 1u << 4
};

/*
 * Process lifetime.
 *
 * Initialize() is idempotent in the sense SDL's own subsystem initialisation
 * is: calling it again for a subsystem that is already up succeeds.  The
 * backend owns SDL_Init/SDL_Quit; shared code must not call them directly.
 */
bool Initialize(uint32_t subsystems = Subsystem_Video);
void Shutdown();
const char* GetError();

/*
 * Ask the OS for DPI awareness before the video subsystem initialises.
 *
 * SDL2 leaves the process DPI-unaware unless this hint is set, which makes
 * Windows scale the window's contents behind the application's back.  SDL3
 * enables per-monitor awareness itself, so its backend does nothing here; the
 * call site stays free of #ifdef noise.
 */
void SetDPIAwareness();

/*
 * Windows.
 *
 * The OpenGL/Vulkan flag has to be present at creation time in both SDL
 * versions: it decides which window-system resources are allocated.  The
 * caller may not rely on the window being shown unless WindowFlag_Hidden is
 * absent.
 */
Window* CreatePlatformWindow(const WindowDesc& desc);
void DestroyWindow(Window* window);
bool IsWindowValid(const Window* window);

void SetWindowTitle(Window* window, const char* title);
void SetWindowPosition(Window* window, int x, int y);
void GetWindowPosition(const Window* window, int* x, int* y);

/* Centres the window on the given display.  Separate from SetWindowPosition()
 * because the "centred" position is an SDL-side sentinel in SDL2 and a
 * different one in SDL3; keeping it here stops that encoding reaching the
 * configuration file. */
void CenterWindowOnDisplay(Window* window, DisplayId display);
void SetWindowSize(Window* window, int width, int height);
void GetWindowSize(const Window* window, int* width, int* height);

/* Floor for user resizing.  Logical pixels on macOS, physical pixels elsewhere -
 * the caller scales by the display factor and caps the result to the work area. */
void SetWindowMinimumSize(Window* window, int width, int height);

/* Title bar and border thickness, in the same units as GetWindowSize().  The minimum size is a
 * client-area floor, so the work-area cap has to subtract these to keep the outer window on
 * screen.  Zeroes when the platform cannot report them. */
void GetWindowBordersSize(const Window* window, int* left, int* top, int* right, int* bottom);

/* True while the window manager owns the size.  Size clamping has to leave such a window alone. */
bool IsWindowMaximized(const Window* window);

/* Client area in pixels, after high-DPI scaling.  This is the size the
 * swapchain and the GL drawable use, not the logical window size. */
void GetWindowPixelSize(const Window* window, int* width, int* height);

DisplayId GetWindowDisplay(const Window* window);

void ShowWindow(Window* window);
void HideWindow(Window* window);
void RaiseWindow(Window* window);
void SetWindowBordered(Window* window, bool bordered);
void SetWindowResizable(Window* window, bool resizable);

void SetWindowMouseGrab(Window* window, bool grabbed);
void SetCursorVisible(bool visible);

/*
 * OS pointer shape for the settings window.  The ImGui platform layer calls this
 * once per frame from ImGui::GetMouseCursor(); the stock SDL backends do the same.
 * Without it, SetMouseCursor() in widgets has no visible effect - the splitter's
 * NS-resize cursor and the I-beam over text fields were the visible losses.
 */
enum class SystemCursor
{
    Arrow,
    TextInput,
    ResizeAll,
    ResizeNS,
    ResizeEW,
    ResizeNESW,
    ResizeNWSE,
    Hand,
    NotAllowed,
    Count
};
void SetSystemCursor(Window* window, SystemCursor cursor);
void SetRelativeMouseMode(Window* window, bool enabled);
void WarpMouseInWindow(Window* window, float x, float y);

/*
 * Sample the pointer's position right now, bypassing the event queue.
 *
 * The event-driven snapshot is only as fresh as the last drain, and the drain
 * happens early in the frame - before the in-game panel is built and before the
 * renderer records its commands. For a light gun that gap is visible as lag, so
 * this asks the OS directly at the last possible moment. Coordinates are the
 * same window-relative ones the snapshot carries.
 *
 * Returns false where no cheaper source than the event queue exists; callers
 * then keep the value they already have.
 */
bool SamplePointerNow(float* x, float* y);

/*
 * Fullscreen.
 *
 * SetFullscreen() is the borderless desktop path.  SetExclusiveFullscreen()
 * additionally selects a mode; it is used for the fixed 57.524 Hz arcade
 * timing.  The mode is matched by size first and refresh rate second, and the
 * backend reports failure rather than silently leaving the wrong mode in
 * place - a wrong refresh rate is the one error the picture cannot show.
 */
bool SetFullscreen(Window* window, bool fullscreen);
bool SetExclusiveFullscreen(Window* window, DisplayId display, const DisplayMode& mode);

int GetDisplayCount();
bool GetDisplayInfo(int index, DisplayInfo* info);
bool GetDisplayInfoById(DisplayId id, DisplayInfo* info);

/*
 * Conversion between the opaque id and the enumeration index.  New code
 * should pass ids around; these two exist for the legacy display-number
 * settings and for the settings window, which still speaks in indices.
 */
DisplayId GetDisplayIdForIndex(int index);
int GetDisplayIndex(DisplayId id);

int GetDisplayModeCount(DisplayId display);
bool GetDisplayMode(DisplayId display, int index, DisplayMode* mode);
bool GetDesktopDisplayMode(DisplayId display, DisplayMode* mode);

/*
 * The mode the window's display is running right now.
 *
 * GetDesktopDisplayMode() describes the desktop, which is the right answer for a
 * borderless window but not for exclusive fullscreen: there the panel is on the
 * selected mode, and the desktop mode may be a different geometry or rate. The
 * high-refresh pacer needs the rate actually being scanned, so it asks through
 * this call instead. Returns false when the window or display is not available.
 */
bool GetCurrentDisplayMode(Window* window, DisplayMode* mode);

/*
 * Scaling.  SDL2 reports DPI and derives a scale from it; SDL3 reports a
 * content scale directly.  Crosshair and ImGui sizing need the scale, so it
 * is the primary call; DPI is kept for the few places that still want the
 * physical figure.
 */
float GetDisplayContentScale(DisplayId display);
bool GetDisplayDPI(DisplayId display, float* diagonal, float* horizontal, float* vertical);

/*
 * Time.
 *
 * Now() is monotonic and never goes backwards.  SDL3 implements it with
 * SDL_GetTicksNS(); SDL2 has no nanosecond clock, so the backend scales the
 * performance counter.  The unit is nanoseconds on both, which is what makes
 * the main loop source-identical.
 */
Nanoseconds Now();
void Sleep(Nanoseconds duration);
void CpuRelax();

/*
 * Sleep until a deadline, then spin for the last part of a frame.
 *
 * Operating-system sleep is not accurate enough for a 17 ms frame, but spinning for the whole
 * frame burns a core. The two backends differ in what the spin has to absorb:
 *
 *   SDL3  SDL_DelayNS uses a high-resolution waitable timer on Windows. A 0.5 ms spin was the
 *         smallest budget that kept the lateness at the level of the old 2 ms spin in the A/B:
 *         0.3 ms left a p99 of roughly 240-430 us, while 0.5 ms measured 26-197 us at the same
 *         CPU cost. The sleep request is (remaining - spin), because asking for a fixed 1 ms at
 *         a time let a short remainder overshoot by the whole spin budget.
 *
 *   SDL2  SDL_Delay has millisecond granularity, so the original 1 ms step and 2 ms spin are
 *         kept unchanged. A sub-millisecond budget cannot absorb a millisecond sleep's overshoot.
 *
 * Remaining is signed, so a deadline that is already in the past simply skips both loops.
 */
#if defined(SUPERMODEL_PLATFORM_SDL3)
constexpr Nanoseconds kSleepSpinNs = 500000;

inline void SleepUntil(Nanoseconds deadline)
{
    for (;;)
    {
        const Nanoseconds remaining = deadline - Now();
        if (remaining <= kSleepSpinNs)
            break;
        Sleep(remaining - kSleepSpinNs);
    }

    while (Now() < deadline)
        CpuRelax();
}
#else
inline void SleepUntil(Nanoseconds deadline)
{
    while (deadline - Now() > 2000000)
        Sleep(1000000);

    while (Now() < deadline)
        CpuRelax();
}
#endif

/*
 * OpenGL context for the settings window and for the OpenGL game renderer.
 *
 * Attributes are process-global in both SDL versions and have to be set
 * before the window is created, which is why they are not part of
 * WindowDesc.  SetGLAttributes() may be called more than once; the last call
 * before CreatePlatformWindow() wins.
 */
bool SetGLAttributes(const GLAttributes& attributes);
GLContext* CreateGLContext(Window* window);
void DestroyGLContext(GLContext* context);
void MakeGLContextCurrent(Window* window, GLContext* context);
void SwapWindow(Window* window);
void SetGLSwapInterval(int interval);

/*
 * The native window handle: HWND on Windows, X11 Window or Wayland surface
 * elsewhere.  Used by DirectInput, the Win32 file dialogs and the Windows
 * message hook.  Returns nullptr where the concept does not apply.
 */
void* GetNativeWindowHandle(const Window* window);

/*
 * Clipboard.  The ImGui platform backend uses these for Ctrl+C/V in text
 * fields.  Get returns the text through std::string so the caller does not
 * have to know whether the backend returned an owned buffer (SDL2) or a
 * pointer into SDL's storage (SDL3).
 */
bool GetClipboardText(std::string& text);
bool SetClipboardText(const char* text);

/*
 * Hands a URL to the desktop's default browser.  Returns false when the
 * platform could not start it; the caller decides whether to log or show a
 * message.  The Ko-fi badge in the settings window is the only user at the
 * moment, but keeping it here avoids an SDL include in the GUI code.
 */
bool OpenURL(const std::string& url);

/*
 * Audio outputs, for the device chooser in the settings window.
 *
 * The names are what the user sees in the operating system's own sound
 * settings, which is what the stored AudioDevice setting is matched against
 * when the device is opened later.  SDL3 addresses devices by id rather than
 * by index, so the index here is simply this enumeration's order.  Returns -1
 * for an out-of-range index.
 */
int GetAudioOutputDeviceCount();
const char* GetAudioOutputDeviceName(int index);

} // Platform
} // OSD

#endif // INCLUDED_PLATFORM_H
