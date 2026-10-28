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
 * PlatformSDL2.cpp
 *
 * SDL2 implementation of the window, display, clock and GL-context API.
 *
 * This is a direct translation of the SDL2 calls that used to be spread
 * through Main.cpp and Gui.cpp.  The behaviour is deliberately identical:
 * the only change is that the SDL types are hidden behind the opaque handles
 * declared in Platform.h.
 */

#include "PlatformSDL2Internal.h"

#include <SDL_syswm.h>
#include <SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace OSD
{
namespace Platform
{

namespace
{

constexpr const char* kWindowDataKey = "SupermodelVK.PlatformWindow";

int DisplayIndexFromId(DisplayId id)
{
    if (id == kPrimaryDisplay)
        return 0;

    const int count = SDL_GetNumVideoDisplays();
    const int index = static_cast<int>(id) - 1;
    if (count <= 0)
        return 0;

    return std::max(0, std::min(index, count - 1));
}

DisplayId DisplayIdFromIndex(int index)
{
    return static_cast<DisplayId>(std::max(0, index) + 1);
}

} // namespace

bool Initialize(uint32_t subsystems)
{
    Uint32 flags = 0;
    if (subsystems & Subsystem_Video)
        flags |= SDL_INIT_VIDEO;
    if (subsystems & Subsystem_Audio)
        flags |= SDL_INIT_AUDIO;
    if (subsystems & Subsystem_Gamepad)
        flags |= SDL_INIT_GAMECONTROLLER;
    if (subsystems & Subsystem_Events)
        flags |= SDL_INIT_EVENTS;

    return SDL_Init(flags) == 0;
}

void Shutdown()
{
    SDL_Quit();
}

const char* GetError()
{
    return SDL_GetError();
}

void SetDPIAwareness()
{
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "system");
#endif
}

Window* CreatePlatformWindow(const WindowDesc& desc)
{
    Uint32 flags = SDL_WINDOW_SHOWN;

    if (desc.flags & WindowFlag_OpenGL)
        flags |= SDL_WINDOW_OPENGL;
    if (desc.flags & WindowFlag_Vulkan)
        flags |= SDL_WINDOW_VULKAN;
    if (desc.flags & WindowFlag_Resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (desc.flags & WindowFlag_HighDPI)
        flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    if (desc.flags & WindowFlag_Hidden)
        flags = (flags & ~SDL_WINDOW_SHOWN) | SDL_WINDOW_HIDDEN;
    if (desc.fullscreenDesktop)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    const int displayIndex = DisplayIndexFromId(desc.display);

    SDL_Window* sdl = SDL_CreateWindow(desc.title,
        SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
        SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
        desc.width, desc.height, flags);
    if (sdl == nullptr)
        return nullptr;

    Window* window = new Window();
    window->sdl = sdl;
    window->display = DisplayIdFromIndex(displayIndex);
    window->flags = desc.flags;
    window->fullscreenDesktop = desc.fullscreenDesktop;
    window->ownsSDL = true;
    SDL_SetWindowData(sdl, kWindowDataKey, window);
    return window;
}

void DestroyWindow(Window* window)
{
    if (window == nullptr)
        return;

    if (window->sdl != nullptr)
    {
        SDL_SetWindowData(window->sdl, kWindowDataKey, nullptr);
        if (window->ownsSDL)
            SDL_DestroyWindow(window->sdl);
        window->sdl = nullptr;
    }

    delete window;
}

bool IsWindowValid(const Window* window)
{
    return window != nullptr && window->sdl != nullptr;
}

Window* WindowFromSDLWindow(SDL_Window* window)
{
    if (window == nullptr)
        return nullptr;

    return static_cast<Window*>(SDL_GetWindowData(window, kWindowDataKey));
}

SDL_Window* SDLWindowFromWindow(Window* window)
{
    return window != nullptr ? window->sdl : nullptr;
}

Window* AdoptSDLWindow(SDL_Window* sdl, uint32_t flags)
{
    if (sdl == nullptr)
        return nullptr;

    if (Window* existing = WindowFromSDLWindow(sdl))
        return existing;

    Window* window = new Window();
    window->sdl = sdl;
    window->ownsSDL = false;

    const int displayIndex = SDL_GetWindowDisplayIndex(sdl);
    window->display = displayIndex >= 0 ? DisplayIdFromIndex(displayIndex) : kPrimaryDisplay;

    const Uint32 sdlFlags = SDL_GetWindowFlags(sdl);
    if (flags == WindowFlag_None)
    {
        if (sdlFlags & SDL_WINDOW_VULKAN)
            flags |= WindowFlag_Vulkan;
        if (sdlFlags & SDL_WINDOW_OPENGL)
            flags |= WindowFlag_OpenGL;
        if (sdlFlags & SDL_WINDOW_RESIZABLE)
            flags |= WindowFlag_Resizable;
        if (sdlFlags & SDL_WINDOW_ALLOW_HIGHDPI)
            flags |= WindowFlag_HighDPI;
    }

    window->flags = flags;
    window->fullscreenDesktop = (sdlFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowData(sdl, kWindowDataKey, window);
    return window;
}

void SetWindowTitle(Window* window, const char* title)
{
    if (IsWindowValid(window))
        SDL_SetWindowTitle(window->sdl, title);
}

void SetWindowPosition(Window* window, int x, int y)
{
    if (IsWindowValid(window))
        SDL_SetWindowPosition(window->sdl, x, y);
}

void GetWindowPosition(const Window* window, int* x, int* y)
{
    if (x != nullptr)
        *x = 0;
    if (y != nullptr)
        *y = 0;

    if (IsWindowValid(window))
        SDL_GetWindowPosition(window->sdl, x, y);
}

void CenterWindowOnDisplay(Window* window, DisplayId display)
{
    if (!IsWindowValid(window))
        return;

    const int displayIndex = DisplayIndexFromId(display);
    SDL_SetWindowPosition(window->sdl,
        SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
        SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex));
}

void SetWindowSize(Window* window, int width, int height)
{
    if (IsWindowValid(window))
        SDL_SetWindowSize(window->sdl, width, height);
}

void SetWindowMinimumSize(Window* window, int width, int height)
{
    if (IsWindowValid(window))
        SDL_SetWindowMinimumSize(window->sdl, width, height);
}

void GetWindowBordersSize(const Window* window, int* left, int* top, int* right, int* bottom)
{
    int l = 0, t = 0, r = 0, b = 0;
    if (IsWindowValid(window))
        SDL_GetWindowBordersSize(window->sdl, &t, &l, &b, &r);
    if (left != nullptr) *left = l;
    if (top != nullptr) *top = t;
    if (right != nullptr) *right = r;
    if (bottom != nullptr) *bottom = b;
}

bool IsWindowMaximized(const Window* window)
{
    return IsWindowValid(window) &&
        (SDL_GetWindowFlags(window->sdl) & SDL_WINDOW_MAXIMIZED) != 0;
}

void GetWindowSize(const Window* window, int* width, int* height)
{
    if (width != nullptr)
        *width = 0;
    if (height != nullptr)
        *height = 0;

    if (!IsWindowValid(window))
        return;

    SDL_GetWindowSize(window->sdl, width, height);
}

void GetWindowPixelSize(const Window* window, int* width, int* height)
{
    if (width != nullptr)
        *width = 0;
    if (height != nullptr)
        *height = 0;

    if (!IsWindowValid(window))
        return;

    if (window->flags & WindowFlag_Vulkan)
        SDL_Vulkan_GetDrawableSize(window->sdl, width, height);
    else if (window->flags & WindowFlag_OpenGL)
        SDL_GL_GetDrawableSize(window->sdl, width, height);
    else
        SDL_GetWindowSize(window->sdl, width, height);
}

DisplayId GetWindowDisplay(const Window* window)
{
    if (!IsWindowValid(window))
        return kPrimaryDisplay;

    const int index = SDL_GetWindowDisplayIndex(window->sdl);
    return index >= 0 ? DisplayIdFromIndex(index) : window->display;
}

void ShowWindow(Window* window)
{
    if (IsWindowValid(window))
        SDL_ShowWindow(window->sdl);
}

void HideWindow(Window* window)
{
    if (IsWindowValid(window))
        SDL_HideWindow(window->sdl);
}

void RaiseWindow(Window* window)
{
    if (IsWindowValid(window))
        SDL_RaiseWindow(window->sdl);
}

void SetWindowBordered(Window* window, bool bordered)
{
    if (IsWindowValid(window))
        SDL_SetWindowBordered(window->sdl, bordered ? SDL_TRUE : SDL_FALSE);
}

void SetWindowResizable(Window* window, bool resizable)
{
    if (IsWindowValid(window))
        SDL_SetWindowResizable(window->sdl, resizable ? SDL_TRUE : SDL_FALSE);
}

void SetWindowMouseGrab(Window* window, bool grabbed)
{
    if (IsWindowValid(window))
        SDL_SetWindowMouseGrab(window->sdl, grabbed ? SDL_TRUE : SDL_FALSE);
}

namespace
{
/*
 * System cursors outlive the frame that asked for them.
 *
 * g_currentSystemCursor records what has already been installed: SDL_SetCursor does no early-out
 * of its own (SDL issue #6113) and the settings window asks for the same shape on every frame,
 * so the call has to be made only when the shape actually changes.
 */
SDL_Cursor* g_systemCursors[(int)SystemCursor::Count] = {};
int g_currentSystemCursor = -1;

// Whether the pointer is currently meant to be visible. SetCursorVisible is driven by the input
// system when it toggles between play and the in-game panel, so the edge is worth remembering.
bool g_cursorShown = true;
}

void SetCursorVisible(bool visible)
{
    const bool wasShown = g_cursorShown;
    g_cursorShown = visible;
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);

    /*
     * Bring the pointer back to the arrow when it reappears.
     *
     * The settings window leaves behind whatever shape was last asked for - an I-beam over a text
     * field, the resize arrow over the game-list splitter - and the game itself never updates the
     * shape, so without this the pointer would come back into play as a resize handle.
     */
    if (visible && !wasShown && g_currentSystemCursor > (int)SystemCursor::Arrow)
    {
        g_currentSystemCursor = -1;
        SetSystemCursor(nullptr, SystemCursor::Arrow);
    }
}

void SetSystemCursor(Window* window, SystemCursor cursor)
{
    (void)window;
    const int index = (int)cursor;
    if (index < 0 || index >= (int)SystemCursor::Count)
        return;

    if (g_systemCursors[index] == nullptr)
    {
        static const SDL_SystemCursor ids[(int)SystemCursor::Count] = {
            SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_IBEAM, SDL_SYSTEM_CURSOR_SIZEALL,
            SDL_SYSTEM_CURSOR_SIZENS, SDL_SYSTEM_CURSOR_SIZEWE, SDL_SYSTEM_CURSOR_SIZENESW,
            SDL_SYSTEM_CURSOR_SIZENWSE, SDL_SYSTEM_CURSOR_HAND, SDL_SYSTEM_CURSOR_NO};
        g_systemCursors[index] = SDL_CreateSystemCursor(ids[index]);
    }
    if (g_systemCursors[index] != nullptr && g_currentSystemCursor != index)
    {
        SDL_SetCursor(g_systemCursors[index]);
        g_currentSystemCursor = index;
    }
}

void SetRelativeMouseMode(Window* window, bool enabled)
{
    (void)window;
    SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

void WarpMouseInWindow(Window* window, float x, float y)
{
    if (IsWindowValid(window))
        SDL_WarpMouseInWindow(window->sdl, static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
}

bool SamplePointerNow(float* x, float* y)
{
    // SDL2 only reports the pointer through the event queue, so there is nothing cheaper to ask.
    // The SDL2 target is the compatibility build; the low-latency path belongs to SDL3.
    (void)x;
    (void)y;
    return false;
}

bool SetFullscreen(Window* window, bool fullscreen)
{
    if (!IsWindowValid(window))
        return false;

    const int result = SDL_SetWindowFullscreen(window->sdl,
        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (result != 0)
        return false;

    window->fullscreenDesktop = fullscreen;
    if (!fullscreen)
        window->exclusiveFullscreen = false;
    return true;
}

bool SetExclusiveFullscreen(Window* window, DisplayId display, const DisplayMode& mode)
{
    if (!IsWindowValid(window))
        return false;

    const int displayIndex = DisplayIndexFromId(display == kPrimaryDisplay ? window->display : display);
    const int modeCount = SDL_GetNumDisplayModes(displayIndex);
    if (modeCount <= 0)
        return false;

    SDL_DisplayMode chosen = {};
    bool found = false;
    int bestDelta = 0;

    for (int i = 0; i < modeCount; ++i)
    {
        SDL_DisplayMode candidate = {};
        if (SDL_GetDisplayMode(displayIndex, i, &candidate) != 0)
            continue;
        if (candidate.w != mode.width || candidate.h != mode.height)
            continue;

        // SDL2 reports refresh_rate as an integer, so 57.524 Hz arrives as 57
        // or 58.  Accept the nearest whole rate rather than requiring an exact
        // match that cannot exist.
        const int delta = std::abs(candidate.refresh_rate - static_cast<int>(std::lround(mode.refreshHz)));
        if (delta > 1)
            continue;

        if (!found || delta < bestDelta)
        {
            chosen = candidate;
            bestDelta = delta;
            found = true;
        }
    }

    if (!found)
        return false;

    if (SDL_SetWindowDisplayMode(window->sdl, &chosen) != 0)
        return false;
    if (SDL_SetWindowFullscreen(window->sdl, SDL_WINDOW_FULLSCREEN) != 0)
        return false;

    window->exclusiveMode = chosen;
    window->exclusiveFullscreen = true;
    window->fullscreenDesktop = false;
    window->display = DisplayIdFromIndex(displayIndex);
    return true;
}

int GetDisplayCount()
{
    return SDL_GetNumVideoDisplays();
}

bool GetDisplayInfo(int index, DisplayInfo* info)
{
    if (info == nullptr)
        return false;

    const int count = SDL_GetNumVideoDisplays();
    if (index < 0 || index >= count)
        return false;

    SDL_Rect bounds = {};
    SDL_Rect usable = {};
    if (SDL_GetDisplayBounds(index, &bounds) != 0)
        return false;
    if (SDL_GetDisplayUsableBounds(index, &usable) != 0)
        usable = bounds;

    *info = DisplayInfo();
    info->id = DisplayIdFromIndex(index);
    info->index = index;
    info->name = SDL_GetDisplayName(index);
    info->bounds = { bounds.x, bounds.y, bounds.w, bounds.h };
    info->usableBounds = { usable.x, usable.y, usable.w, usable.h };
    info->primary = (index == 0);
    return true;
}

bool GetDisplayInfoById(DisplayId id, DisplayInfo* info)
{
    const int count = SDL_GetNumVideoDisplays();
    if (count <= 0)
        return false;

    return GetDisplayInfo(DisplayIndexFromId(id), info);
}

DisplayId GetDisplayIdForIndex(int index)
{
    return DisplayIdFromIndex(index);
}

int GetDisplayIndex(DisplayId id)
{
    return DisplayIndexFromId(id);
}

int GetDisplayModeCount(DisplayId display)
{
    return SDL_GetNumDisplayModes(DisplayIndexFromId(display));
}

bool GetDisplayMode(DisplayId display, int index, DisplayMode* mode)
{
    if (mode == nullptr)
        return false;

    SDL_DisplayMode sdlMode = {};
    if (SDL_GetDisplayMode(DisplayIndexFromId(display), index, &sdlMode) != 0)
        return false;

    mode->width = sdlMode.w;
    mode->height = sdlMode.h;
    mode->refreshHz = static_cast<float>(sdlMode.refresh_rate);
    return true;
}

bool GetDesktopDisplayMode(DisplayId display, DisplayMode* mode)
{
    if (mode == nullptr)
        return false;

    SDL_DisplayMode sdlMode = {};
    if (SDL_GetDesktopDisplayMode(DisplayIndexFromId(display), &sdlMode) != 0)
        return false;

    mode->width = sdlMode.w;
    mode->height = sdlMode.h;
    mode->refreshHz = static_cast<float>(sdlMode.refresh_rate);
    return true;
}

bool GetCurrentDisplayMode(Window* window, DisplayMode* mode)
{
    if (mode == nullptr || !IsWindowValid(window))
        return false;

    const int displayIndex = SDL_GetWindowDisplayIndex(window->sdl);
    if (displayIndex < 0)
        return false;

    SDL_DisplayMode sdlMode = {};
    if (SDL_GetCurrentDisplayMode(displayIndex, &sdlMode) != 0)
        return false;

    mode->width = sdlMode.w;
    mode->height = sdlMode.h;
    mode->refreshHz = static_cast<float>(sdlMode.refresh_rate);
    return true;
}

float GetDisplayContentScale(DisplayId display)
{
    float diagonal = 0.0f;
    float horizontal = 0.0f;
    float vertical = 0.0f;
    if (SDL_GetDisplayDPI(DisplayIndexFromId(display), &diagonal, &horizontal, &vertical) == 0 && horizontal > 0.0f)
        return horizontal / 96.0f;

    return 1.0f;
}

bool GetDisplayDPI(DisplayId display, float* diagonal, float* horizontal, float* vertical)
{
    return SDL_GetDisplayDPI(DisplayIndexFromId(display), diagonal, horizontal, vertical) == 0;
}

Nanoseconds Now()
{
    static const Nanoseconds frequency = static_cast<Nanoseconds>(SDL_GetPerformanceFrequency());
    const Nanoseconds counter = static_cast<Nanoseconds>(SDL_GetPerformanceCounter());
    const Nanoseconds seconds = counter / frequency;
    const Nanoseconds remainder = counter % frequency;
    return seconds * kNanosecondsPerSecond +
        (remainder * kNanosecondsPerSecond) / frequency;
}

void Sleep(Nanoseconds duration)
{
    if (duration <= 0)
        return;

    const Nanoseconds milliseconds = (duration + 999999) / 1000000;
    SDL_Delay(static_cast<Uint32>(milliseconds));
}

void CpuRelax()
{
#ifdef SDL_CPUPauseInstruction
    SDL_CPUPauseInstruction();
#endif
}

bool SetGLAttributes(const GLAttributes& attributes)
{
    int result = 0;
    result |= SDL_GL_SetAttribute(SDL_GL_RED_SIZE, attributes.redSize);
    result |= SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, attributes.greenSize);
    result |= SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, attributes.blueSize);
    result |= SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, attributes.depthSize);
    result |= SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, attributes.stencilSize);
    result |= SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, attributes.doubleBuffer ? 1 : 0);

    if (attributes.coreProfile)
    {
        result |= SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        result |= SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, attributes.majorVersion);
        result |= SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, attributes.minorVersion);
    }

    return result == 0;
}

GLContext* CreateGLContext(Window* window)
{
    if (!IsWindowValid(window))
        return nullptr;

    SDL_GLContext sdl = SDL_GL_CreateContext(window->sdl);
    if (sdl == nullptr)
        return nullptr;

    GLContext* context = new GLContext();
    context->sdl = sdl;
    return context;
}

void DestroyGLContext(GLContext* context)
{
    if (context == nullptr)
        return;

    if (context->sdl != nullptr)
        SDL_GL_DeleteContext(context->sdl);
    delete context;
}

void MakeGLContextCurrent(Window* window, GLContext* context)
{
    if (IsWindowValid(window) && context != nullptr)
        SDL_GL_MakeCurrent(window->sdl, context->sdl);
}

void SwapWindow(Window* window)
{
    if (IsWindowValid(window))
        SDL_GL_SwapWindow(window->sdl);
}

void SetGLSwapInterval(int interval)
{
    SDL_GL_SetSwapInterval(interval);
}

void* GetNativeWindowHandle(const Window* window)
{
#ifdef _WIN32
    if (!IsWindowValid(window))
        return nullptr;

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window->sdl, &info) == SDL_TRUE &&
        info.subsystem == SDL_SYSWM_WINDOWS)
        return info.info.win.window;
#else
    (void)window;
#endif

    return nullptr;
}

bool GetClipboardText(std::string& text)
{
    char* value = SDL_GetClipboardText();
    if (value == nullptr)
        return false;

    text = value;
    SDL_free(value);
    return true;
}

bool SetClipboardText(const char* text)
{
    return SDL_SetClipboardText(text != nullptr ? text : "") == 0;
}

bool OpenURL(const std::string& url)
{
    return !url.empty() && SDL_OpenURL(url.c_str()) == 0;
}

int GetAudioOutputDeviceCount()
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return 0;

    const int count = SDL_GetNumAudioDevices(0);   // 0 = playback devices
    return count > 0 ? count : 0;
}

const char* GetAudioOutputDeviceName(int index)
{
    if (index < 0)
        return nullptr;

    return SDL_GetAudioDeviceName(index, 0);
}

} // Platform
} // OSD
