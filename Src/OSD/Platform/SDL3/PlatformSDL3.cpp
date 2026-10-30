/**
 ** Model 3-Vulkan
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan, and is derived from Supermodel,
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
 * PlatformSDL3.cpp
 *
 * SDL3 implementation of the window, display, clock and GL-context API.
 */

#include "PlatformSDL3Internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace OSD
{
namespace Platform
{

namespace
{

constexpr const char* kWindowPointerProperty = "Model3Vulkan.PlatformWindow";

DisplayId PrimaryDisplayId()
{
    return static_cast<DisplayId>(SDL_GetPrimaryDisplay());
}

std::vector<SDL_DisplayID> GetDisplayIds()
{
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    std::vector<SDL_DisplayID> result;
    if (ids != nullptr)
    {
        result.assign(ids, ids + count);
        SDL_free(ids);
    }
    return result;
}

SDL_DisplayID DisplayIdToSDL(DisplayId id)
{
    return id == kPrimaryDisplay ? SDL_GetPrimaryDisplay() : static_cast<SDL_DisplayID>(id);
}

DisplayId DisplayIdFromSDL(SDL_DisplayID id)
{
    return static_cast<DisplayId>(id);
}

} // namespace

bool Initialize(uint32_t subsystems)
{
    SDL_InitFlags flags = 0;
    if (subsystems & Subsystem_Video)
        flags |= SDL_INIT_VIDEO;
    if (subsystems & Subsystem_Audio)
        flags |= SDL_INIT_AUDIO;
    if (subsystems & Subsystem_Gamepad)
        flags |= SDL_INIT_GAMEPAD;
    if (subsystems & Subsystem_Haptic)
        flags |= SDL_INIT_HAPTIC;
    if (subsystems & Subsystem_Events)
        flags |= SDL_INIT_EVENTS;

    return SDL_Init(flags);
}

void Shutdown()
{
    ShutdownPerDeviceMouseInput();
    SDL_Quit();
}

const char* GetError()
{
    return SDL_GetError();
}

void SetDPIAwareness()
{
    // SDL3 enables per-monitor DPI awareness on Windows itself, so there is no hint to set. The call
    // exists so shared code does not need to know that.
}

Window* CreatePlatformWindow(const WindowDesc& desc)
{
    SDL_WindowFlags flags = 0;
    if (desc.flags & WindowFlag_OpenGL)
        flags |= SDL_WINDOW_OPENGL;
    if (desc.flags & WindowFlag_Vulkan)
        flags |= SDL_WINDOW_VULKAN;
    if (desc.flags & WindowFlag_Resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (desc.flags & WindowFlag_HighDPI)
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.flags & WindowFlag_Hidden)
        flags |= SDL_WINDOW_HIDDEN;
    if (desc.fullscreenDesktop)
        flags |= SDL_WINDOW_FULLSCREEN;

    SDL_Window* sdl = SDL_CreateWindow(desc.title, desc.width, desc.height, flags);
    if (sdl == nullptr)
        return nullptr;

    const SDL_DisplayID displayId = DisplayIdToSDL(desc.display);
    SDL_SetWindowPosition(sdl, SDL_WINDOWPOS_CENTERED_DISPLAY(displayId),
        SDL_WINDOWPOS_CENTERED_DISPLAY(displayId));

    Window* window = new Window();
    window->sdl = sdl;
    window->display = DisplayIdFromSDL(displayId);
    window->flags = desc.flags;
    window->fullscreen = desc.fullscreenDesktop;
    window->ownsSDL = true;
    SDL_SetPointerProperty(SDL_GetWindowProperties(sdl), kWindowPointerProperty, window);
    return window;
}

void DestroyWindow(Window* window)
{
    if (window == nullptr)
        return;

    if (window->sdl != nullptr)
    {
        SDL_SetPointerProperty(SDL_GetWindowProperties(window->sdl), kWindowPointerProperty, nullptr);
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

    return static_cast<Window*>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), kWindowPointerProperty, nullptr));
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
    window->display = DisplayIdFromSDL(SDL_GetDisplayForWindow(sdl));

    const SDL_WindowFlags sdlFlags = SDL_GetWindowFlags(sdl);
    if (flags == WindowFlag_None)
    {
        if (sdlFlags & SDL_WINDOW_VULKAN)
            flags |= WindowFlag_Vulkan;
        if (sdlFlags & SDL_WINDOW_OPENGL)
            flags |= WindowFlag_OpenGL;
        if (sdlFlags & SDL_WINDOW_RESIZABLE)
            flags |= WindowFlag_Resizable;
        if (sdlFlags & SDL_WINDOW_HIGH_PIXEL_DENSITY)
            flags |= WindowFlag_HighDPI;
    }
    window->flags = flags;
    SDL_SetPointerProperty(SDL_GetWindowProperties(sdl), kWindowPointerProperty, window);
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

    const SDL_DisplayID displayId = DisplayIdToSDL(display);
    SDL_SetWindowPosition(window->sdl,
        SDL_WINDOWPOS_CENTERED_DISPLAY(displayId),
        SDL_WINDOWPOS_CENTERED_DISPLAY(displayId));
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
#if defined(__APPLE__)
    /*
     * SDL3's Cocoa backend does not implement SDL_GetWindowBordersSize, so it returns all zeroes
     * here. The settings window has a standard title bar (28 points on current macOS) and no
     * side borders. Without this the work-area cap would let the outer window hang under the
     * menu bar or dock on a screen whose usable height is close to the minimum.
     */
    if (IsWindowValid(window) && t == 0 && l == 0 && r == 0 && b == 0 &&
        (SDL_GetWindowFlags(window->sdl) & SDL_WINDOW_BORDERLESS) == 0)
        t = 28;
#endif
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
    if (IsWindowValid(window))
        SDL_GetWindowSize(window->sdl, width, height);
}

void GetWindowPixelSize(const Window* window, int* width, int* height)
{
    if (width != nullptr)
        *width = 0;
    if (height != nullptr)
        *height = 0;
    if (IsWindowValid(window))
        SDL_GetWindowSizeInPixels(window->sdl, width, height);
}

DisplayId GetWindowDisplay(const Window* window)
{
    if (!IsWindowValid(window))
        return kPrimaryDisplay;

    return DisplayIdFromSDL(SDL_GetDisplayForWindow(window->sdl));
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
        SDL_SetWindowBordered(window->sdl, bordered);
}

void SetWindowResizable(Window* window, bool resizable)
{
    if (IsWindowValid(window))
        SDL_SetWindowResizable(window->sdl, resizable);
}

void SetWindowMouseGrab(Window* window, bool grabbed)
{
    if (IsWindowValid(window))
        SDL_SetWindowMouseGrab(window->sdl, grabbed);
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
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();

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
            SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_TEXT, SDL_SYSTEM_CURSOR_MOVE,
            SDL_SYSTEM_CURSOR_NS_RESIZE, SDL_SYSTEM_CURSOR_EW_RESIZE,
            SDL_SYSTEM_CURSOR_NESW_RESIZE, SDL_SYSTEM_CURSOR_NWSE_RESIZE,
            SDL_SYSTEM_CURSOR_POINTER, SDL_SYSTEM_CURSOR_NOT_ALLOWED};
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
    if (IsWindowValid(window))
        SDL_SetWindowRelativeMouseMode(window->sdl, enabled);
}

void WarpMouseInWindow(Window* window, float x, float y)
{
    if (IsWindowValid(window))
        SDL_WarpMouseInWindow(window->sdl, x, y);
}

bool SamplePointerNow(float* x, float* y)
{
    if (x == nullptr || y == nullptr)
        return false;

#ifdef _WIN32
    /*
     * GetCursorPos is the shortest path there is: no event, no queue, no SDL
     * state to synchronise. Converted through the focused window's client area,
     * which is the coordinate space the event snapshot uses.
     *
     * The focused window is the right one to ask about: while a game runs it is
     * the game window, and while the settings window is up the game is not
     * being emulated.
     */
    SDL_Window* target = SDL_GetMouseFocus();
    if (target == nullptr)
        target = SDL_GetKeyboardFocus();
    if (target == nullptr)
        return false;

    POINT cursor = {};
    if (!GetCursorPos(&cursor))
        return false;

    HWND hwnd = static_cast<HWND>(GetNativeWindowHandle(WindowFromSDLWindow(target)));
    if (hwnd != nullptr && !ScreenToClient(hwnd, &cursor))
        return false;

    // Without a native handle the screen coordinates are all we have; for a
    // fullscreen window at the origin they are the client coordinates too.
    *x = static_cast<float>(cursor.x);
    *y = static_cast<float>(cursor.y);
    return true;
#else
    // Elsewhere the cheapest source is SDL's own state, which needs a pump to be current. That is
    // still one call cheaper than a full event drain, and it does not consume anything.
    SDL_PumpEvents();

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);

    *x = mouseX;
    *y = mouseY;
    return true;
#endif
}

bool SetFullscreen(Window* window, bool fullscreen)
{
    if (!IsWindowValid(window))
        return false;

    if (!SDL_SetWindowFullscreenMode(window->sdl, nullptr))
        return false;
    if (!SDL_SetWindowFullscreen(window->sdl, fullscreen))
        return false;

    window->fullscreen = fullscreen;
    window->exclusiveFullscreen = false;
    return true;
}

bool SetExclusiveFullscreen(Window* window, DisplayId display, const DisplayMode& mode)
{
    if (!IsWindowValid(window))
        return false;

    const SDL_DisplayID displayId = DisplayIdToSDL(display == kPrimaryDisplay ? window->display : display);
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displayId, &count);
    if (modes == nullptr)
        return false;

    SDL_DisplayMode chosen = {};
    bool found = false;
    int bestDelta = 0;
    for (int i = 0; i < count; ++i)
    {
        const SDL_DisplayMode* candidate = modes[i];
        if (candidate == nullptr || candidate->w != mode.width || candidate->h != mode.height)
            continue;

        const int delta = std::abs(static_cast<int>(std::lround(candidate->refresh_rate)) -
            static_cast<int>(std::lround(mode.refreshHz)));
        if (delta > 1)
            continue;

        if (!found || delta < bestDelta)
        {
            chosen = *candidate;
            bestDelta = delta;
            found = true;
        }
    }
    SDL_free(modes);

    if (!found)
        return false;
    if (!SDL_SetWindowFullscreenMode(window->sdl, &chosen))
        return false;
    if (!SDL_SetWindowFullscreen(window->sdl, true))
        return false;

    window->exclusiveMode = chosen;
    window->exclusiveFullscreen = true;
    window->fullscreen = true;
    window->display = DisplayIdFromSDL(displayId);
    return true;
}

int GetDisplayCount()
{
    return static_cast<int>(GetDisplayIds().size());
}

bool GetDisplayInfo(int index, DisplayInfo* info)
{
    if (info == nullptr)
        return false;

    const std::vector<SDL_DisplayID> ids = GetDisplayIds();
    if (index < 0 || index >= static_cast<int>(ids.size()))
        return false;

    SDL_Rect bounds = {};
    SDL_Rect usable = {};
    if (!SDL_GetDisplayBounds(ids[index], &bounds))
        return false;
    if (!SDL_GetDisplayUsableBounds(ids[index], &usable))
        usable = bounds;

    *info = DisplayInfo();
    info->id = DisplayIdFromSDL(ids[index]);
    info->index = index;
    info->name = SDL_GetDisplayName(ids[index]);
    info->bounds = { bounds.x, bounds.y, bounds.w, bounds.h };
    info->usableBounds = { usable.x, usable.y, usable.w, usable.h };
    info->primary = ids[index] == SDL_GetPrimaryDisplay();
    return true;
}

bool GetDisplayInfoById(DisplayId id, DisplayInfo* info)
{
    const std::vector<SDL_DisplayID> ids = GetDisplayIds();
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (DisplayIdFromSDL(ids[i]) == id || id == kPrimaryDisplay)
        {
            return GetDisplayInfo(static_cast<int>(i), info);
        }
    }
    return false;
}

DisplayId GetDisplayIdForIndex(int index)
{
    const std::vector<SDL_DisplayID> ids = GetDisplayIds();
    if (index < 0 || index >= static_cast<int>(ids.size()))
        return PrimaryDisplayId();
    return DisplayIdFromSDL(ids[index]);
}

int GetDisplayIndex(DisplayId id)
{
    const std::vector<SDL_DisplayID> ids = GetDisplayIds();
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (DisplayIdFromSDL(ids[i]) == id)
            return static_cast<int>(i);
    }
    return 0;
}

int GetDisplayModeCount(DisplayId display)
{
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(DisplayIdToSDL(display), &count);
    if (modes != nullptr)
        SDL_free(modes);
    return count;
}

bool GetDisplayMode(DisplayId display, int index, DisplayMode* mode)
{
    if (mode == nullptr)
        return false;

    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(DisplayIdToSDL(display), &count);
    if (modes == nullptr || index < 0 || index >= count)
    {
        if (modes != nullptr)
            SDL_free(modes);
        return false;
    }

    mode->width = modes[index]->w;
    mode->height = modes[index]->h;
    mode->refreshHz = modes[index]->refresh_rate;
    SDL_free(modes);
    return true;
}

bool GetDesktopDisplayMode(DisplayId display, DisplayMode* mode)
{
    if (mode == nullptr)
        return false;

    const SDL_DisplayMode* current = SDL_GetDesktopDisplayMode(DisplayIdToSDL(display));
    if (current == nullptr)
        return false;

    mode->width = current->w;
    mode->height = current->h;
    mode->refreshHz = current->refresh_rate;
    return true;
}

bool GetCurrentDisplayMode(Window* window, DisplayMode* mode)
{
    if (mode == nullptr || !IsWindowValid(window))
        return false;

    const SDL_DisplayID display = SDL_GetDisplayForWindow(window->sdl);
    const SDL_DisplayMode* current = SDL_GetCurrentDisplayMode(display);
    if (current == nullptr)
        return false;

    mode->width = current->w;
    mode->height = current->h;
    mode->refreshHz = current->refresh_rate;
    return true;
}

float GetDisplayContentScale(DisplayId display)
{
    const float scale = SDL_GetDisplayContentScale(DisplayIdToSDL(display));
    return scale > 0.0f ? scale : 1.0f;
}

bool GetDisplayDPI(DisplayId display, float* diagonal, float* horizontal, float* vertical)
{
    const float dpi = GetDisplayContentScale(display) * 96.0f;
    if (diagonal != nullptr)
        *diagonal = dpi;
    if (horizontal != nullptr)
        *horizontal = dpi;
    if (vertical != nullptr)
        *vertical = dpi;
    return true;
}

Nanoseconds Now()
{
    return static_cast<Nanoseconds>(SDL_GetTicksNS());
}

void Sleep(Nanoseconds duration)
{
    if (duration > 0)
        SDL_DelayNS(static_cast<Uint64>(duration));
}

void CpuRelax()
{
    SDL_CPUPauseInstruction();
}

bool SetGLAttributes(const GLAttributes& attributes)
{
    bool ok = true;
    ok = ok && SDL_GL_SetAttribute(SDL_GL_RED_SIZE, attributes.redSize);
    ok = ok && SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, attributes.greenSize);
    ok = ok && SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, attributes.blueSize);
    ok = ok && SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, attributes.depthSize);
    ok = ok && SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, attributes.stencilSize);
    ok = ok && SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, attributes.doubleBuffer ? 1 : 0);

    if (attributes.coreProfile)
    {
        ok = ok && SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        ok = ok && SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, attributes.majorVersion);
        ok = ok && SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, attributes.minorVersion);
    }
    return ok;
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
        SDL_GL_DestroyContext(context->sdl);
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
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window->sdl),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    (void)window;
    return nullptr;
#endif
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
    return SDL_SetClipboardText(text != nullptr ? text : "");
}

bool OpenURL(const std::string& url)
{
    return !url.empty() && SDL_OpenURL(url.c_str());
}

int GetAudioOutputDeviceCount()
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return 0;

    int count = 0;
    SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&count);
    if (devices != nullptr)
        SDL_free(devices);
    return count > 0 ? count : 0;
}

const char* GetAudioOutputDeviceName(int index)
{
    if (index < 0)
        return nullptr;

    const char* name = nullptr;
    int count = 0;
    SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&count);
    if (devices != nullptr)
    {
        if (index < count)
            name = SDL_GetAudioDeviceName(devices[index]);
        SDL_free(devices);
    }

    // SDL3 returns a pointer into the device's own storage, which stays valid until the device is
    // removed; the caller copies it immediately (see GetAudioDevices() in Gui.cpp).
    return name;
}

} // Platform
} // OSD

#if defined(_WIN32)

/*
 * Windows-subsystem entry point.
 *
 * The executable is linked with /SUBSYSTEM:WINDOWS (the console is attached or hidden inside the
 * program), so the CRT looks for WinMain. SDL2 ships SDL2main, which supplies one and calls main();
 * SDL3's equivalent expects the application symbol to be named SDL_main, which only happens if the
 * translation unit that defines it also includes SDL_main.h - precisely the coupling the platform
 * layer exists to avoid, and the reason shared code no longer includes any SDL header.
 *
 * The command line is rebuilt from UTF-16 into UTF-8 rather than using the CRT's __argv, which is in
 * the process's ANSI code page: ROM paths outside that code page would otherwise arrive mangled.
 */
extern int main(int argc, char** argv);

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    int argc = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wide == nullptr || argc <= 0)
        return main(0, nullptr);

    std::vector<std::string> storage;
    std::vector<char*> argv;
    storage.reserve(static_cast<size_t>(argc));
    argv.reserve(static_cast<size_t>(argc));

    for (int i = 0; i < argc; ++i)
    {
        const int needed = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
            storage.emplace_back();
        else
        {
            std::string converted(static_cast<size_t>(needed - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, &converted[0], needed, nullptr, nullptr);
            storage.push_back(std::move(converted));
        }
    }

    LocalFree(wide);

    for (std::string& arg : storage)
        argv.push_back(&arg[0]);

    return main(argc, argv.data());
}

#endif  // _WIN32
