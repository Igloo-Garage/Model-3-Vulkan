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
 * PlatformInputSDL3.cpp
 *
 * SDL3 input backend: one event drain, a POD snapshot, and a gamepad registry
 * keyed by SDL_JoystickID.
 */

#include "PlatformSDL3Internal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace OSD
{
namespace Platform
{

namespace
{

InputSnapshot g_input;

struct OpenGamepadEntry
{
    GamepadId id = kInvalidGamepad;
    SDL_Gamepad* gamepad = nullptr;
    SDL_Joystick* joystick = nullptr;
    GamepadInfo info;

    /*
     * Effect-based haptics. Opened lazily on the first force-feedback command: SDL_Haptic has to be
     * opened from the joystick handle, and a pad that never receives a command should not pay for a
     * haptic handle. -1 means "not created yet".
     */
    SDL_Haptic* haptic = nullptr;
    int hapticConstant = -1;
    int hapticSpring = -1;
    int hapticFriction = -1;
    bool hapticConstantSupported = true;
    bool hapticSpringSupported = true;
    bool hapticFrictionSupported = true;
    // Level the constant-force envelope starts from, so an update ramps instead of stepping.
    Sint16 hapticConstantLevel = 0;
};

std::vector<OpenGamepadEntry> g_gamepads;

/*
 * Per-device mouse state.
 *
 * Keyed by SDL_MouseID. SDL reports the same id for a device enumerated through its own RawInput
 * path as Windows' hDevice handle does, so the SDL events and the raw thread below feed one table.
 *
 * Zero is SDL's global pseudo-device id (SDL_mouse_c.h spells it SDL_GLOBAL_MOUSE_ID). It is not
 * public API, so it is named here; events tagged with it describe the shared system cursor rather
 * than one physical device.
 */
constexpr MouseId kGlobalMouseId = 0;

struct MouseEntry
{
    MouseId id = kInvalidMouse;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    uint32_t buttons = 0;
    bool seeded = false;
};

std::vector<MouseEntry> g_mice;

MouseEntry* FindMouse(MouseId id)
{
    for (MouseEntry& entry : g_mice)
    {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

MouseEntry& EnsureMouse(MouseId id)
{
    MouseEntry* existing = FindMouse(id);
    if (existing != nullptr)
        return *existing;

    g_mice.emplace_back();
    MouseEntry& entry = g_mice.back();
    entry.id = id;

    const char* name = SDL_GetMouseNameForID(static_cast<SDL_MouseID>(id));
    if (name != nullptr)
        entry.name = name;
    return entry;
}

void RefreshMouseList()
{
    int count = 0;
    SDL_MouseID* ids = SDL_GetMice(&count);
    if (ids == nullptr)
        return;

    for (int i = 0; i < count; ++i)
        EnsureMouse(static_cast<MouseId>(ids[i]));

    SDL_free(ids);
}

void ClampMouseToWindow(MouseEntry& entry)
{
    SDL_Window* window = SDL_GetMouseFocus();
    if (window == nullptr)
        window = SDL_GetKeyboardFocus();
    if (window == nullptr)
        return;

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (width > 0)
        entry.x = std::clamp(entry.x, 0.0f, static_cast<float>(width - 1));
    if (height > 0)
        entry.y = std::clamp(entry.y, 0.0f, static_cast<float>(height - 1));
}

/*
 * Feeds one motion sample into a device. The first sample seeds the position from the event's
 * absolute coordinates (the system cursor at that instant, which for an absolute gun is where it
 * points); every later sample integrates the device's own deltas. Integrating rather than copying
 * the event position is what makes two guns independent: the system cursor is shared, the deltas
 * are not.
 */
MouseId TargetMouseId(MouseId eventId)
{
    if (eventId != kGlobalMouseId)
        return eventId;

    // SDL tags events with the global id in normal mode. Attribute them to the first real device so
    // a single gun still tracks when the platform has no per-device source.
    for (const MouseEntry& entry : g_mice)
    {
        if (entry.id != kGlobalMouseId)
            return entry.id;
    }
    return kGlobalMouseId;
}

void ApplyMouseMotion(MouseId id, float eventX, float eventY, float dx, float dy)
{
    MouseEntry& entry = EnsureMouse(id);

    if (!entry.seeded)
    {
        entry.x = eventX;
        entry.y = eventY;
        entry.seeded = true;
    }
    else
    {
        entry.x += dx;
        entry.y += dy;
    }

    entry.dx += dx;
    entry.dy += dy;
    ClampMouseToWindow(entry);
}

void ApplyMouseButton(MouseId id, uint32_t bit, bool down)
{
    MouseEntry& entry = EnsureMouse(id);
    if (down)
        entry.buttons |= bit;
    else
        entry.buttons &= ~bit;
}

#if defined(_WIN32)

/*
 * RawInput source for per-device mice.
 *
 * SDL3 only turns its own raw mouse path on while relative mouse mode is active; in a normal
 * window every WM_MOUSEMOVE is tagged with the global mouse id, which cannot tell two light guns
 * apart. This thread therefore owns a message-only window and registers the mouse usage itself.
 * SDL's registration and this one replace each other (Windows keeps one target per usage), which
 * is why EnsurePerDeviceMouseInput() re-registers whenever relative mode hands it back.
 */
struct RawMousePacket
{
    MouseId id = kInvalidMouse;
    LONG x = 0;
    LONG y = 0;
    USHORT flags = 0;
    USHORT buttonFlags = 0;
};

constexpr USHORT kRawMouseAbsolute = 0x01;      // MOUSE_MOVE_ABSOLUTE
constexpr USHORT kRawMouseVirtualDesktop = 0x02;
constexpr USHORT kRawMouseRawCoordinates = 0x40; // absolute values are already screen pixels

std::mutex g_rawMutex;
std::vector<RawMousePacket> g_rawPackets;
std::atomic<HWND> g_rawWindow{ nullptr };
HANDLE g_rawThread = nullptr;
DWORD g_rawThreadId = 0;
HANDLE g_rawReady = nullptr;
std::atomic_bool g_rawRunning{ false };

LRESULT CALLBACK RawMouseWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INPUT)
    {
        UINT size = sizeof(RAWINPUT);
        RAWINPUT input;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size,
                sizeof(RAWINPUTHEADER)) == sizeof(RAWINPUT) && input.header.dwType == RIM_TYPEMOUSE)
        {
            RawMousePacket packet;
            packet.id = static_cast<MouseId>(reinterpret_cast<uintptr_t>(input.header.hDevice));
            packet.x = input.data.mouse.lLastX;
            packet.y = input.data.mouse.lLastY;
            packet.flags = input.data.mouse.usFlags;
            packet.buttonFlags = input.data.mouse.usButtonFlags;

            std::lock_guard<std::mutex> lock(g_rawMutex);
            g_rawPackets.push_back(packet);
        }
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

DWORD WINAPI RawMouseThread(LPVOID)
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = RawMouseWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"SupermodelRawMouse";
    RegisterClassExW(&windowClass);

    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
    g_rawWindow = window;

    if (window != nullptr)
    {
        RAWINPUTDEVICE device = {};
        device.usUsagePage = 0x01;      // Generic Desktop
        device.usUsage = 0x02;          // Mouse
        device.dwFlags = 0;
        device.hwndTarget = window;

        if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
        {
            DestroyWindow(window);
            g_rawWindow = nullptr;
        }
    }

    SetEvent(g_rawReady);

    MSG message;
    while (g_rawRunning && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    // Remove the registration on the way out; leaving it bound to a destroyed window would fail
    // the next RegisterRawInputDevices() call in this process.
    RAWINPUTDEVICE remove = {};
    remove.usUsagePage = 0x01;
    remove.usUsage = 0x02;
    remove.dwFlags = RIDEV_REMOVE;
    remove.hwndTarget = nullptr;
    RegisterRawInputDevices(&remove, 1, sizeof(remove));

    if (g_rawWindow != nullptr)
    {
        DestroyWindow(g_rawWindow.load());
        g_rawWindow = nullptr;
    }

    return 0;
}

void StartRawMouse()
{
    if (g_rawThread != nullptr)
        return;

    g_rawRunning = true;
    g_rawReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_rawThread = CreateThread(nullptr, 0, RawMouseThread, nullptr, 0, &g_rawThreadId);

    if (g_rawThread == nullptr)
    {
        g_rawRunning = false;
        return;
    }

    WaitForSingleObject(g_rawReady, 2000);
}

void StopRawMouse()
{
    if (g_rawThread == nullptr)
        return;

    g_rawRunning = false;
    PostThreadMessageW(g_rawThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(g_rawThread, 2000);
    CloseHandle(g_rawThread);
    g_rawThread = nullptr;

    if (g_rawReady != nullptr)
    {
        CloseHandle(g_rawReady);
        g_rawReady = nullptr;
    }
}

SDL_Window* FocusedMouseWindow()
{
    SDL_Window* window = SDL_GetMouseFocus();
    return window != nullptr ? window : SDL_GetKeyboardFocus();
}

void ProcessRawMouse()
{
    std::vector<RawMousePacket> packets;
    {
        std::lock_guard<std::mutex> lock(g_rawMutex);
        packets.swap(g_rawPackets);
    }

    if (packets.empty())
        return;

    SDL_Window* window = FocusedMouseWindow();
    if (window == nullptr)
        return;

    SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));

    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    for (const RawMousePacket& packet : packets)
    {
        const MouseId id = packet.id;

        if ((packet.flags & kRawMouseAbsolute) != 0 && hwnd != nullptr)
        {
            const bool virtualDesktop = (packet.flags & kRawMouseVirtualDesktop) != 0;
            const bool rawCoordinates = (packet.flags & kRawMouseRawCoordinates) != 0;

            const int originX = virtualDesktop ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0;
            const int originY = virtualDesktop ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0;
            const int width = virtualDesktop ? GetSystemMetrics(SM_CXVIRTUALSCREEN) : GetSystemMetrics(SM_CXSCREEN);
            const int height = virtualDesktop ? GetSystemMetrics(SM_CYVIRTUALSCREEN) : GetSystemMetrics(SM_CYSCREEN);

            POINT point = {};
            if (rawCoordinates || width <= 0 || height <= 0)
            {
                point.x = packet.x;
                point.y = packet.y;
            }
            else
            {
                point.x = originX + static_cast<LONG>(packet.x * (width - 1) / 65535.0f);
                point.y = originY + static_cast<LONG>(packet.y * (height - 1) / 65535.0f);
            }

            ScreenToClient(hwnd, &point);

            MouseEntry& entry = EnsureMouse(id);
            // Absolute devices report a position, not a delta; keep the reported position and let
            // the caller see the jump as motion so trigger/offscreen logic still updates.
            const float dx = entry.seeded ? static_cast<float>(point.x) - entry.x : 0.0f;
            const float dy = entry.seeded ? static_cast<float>(point.y) - entry.y : 0.0f;
            entry.x = static_cast<float>(point.x);
            entry.y = static_cast<float>(point.y);
            entry.dx += dx;
            entry.dy += dy;
            entry.seeded = true;
            ClampMouseToWindow(entry);
        }
        else
        {
            ApplyMouseMotion(id, g_input.mouseX, g_input.mouseY,
                static_cast<float>(packet.x), static_cast<float>(packet.y));
        }

        struct RawButton
        {
            USHORT flag;
            uint32_t bit;
            bool down;
        };
        static const RawButton kButtons[] = {
            { RI_MOUSE_LEFT_BUTTON_DOWN,   MouseButton_Left,   true },
            { RI_MOUSE_LEFT_BUTTON_UP,     MouseButton_Left,   false },
            { RI_MOUSE_MIDDLE_BUTTON_DOWN, MouseButton_Middle, true },
            { RI_MOUSE_MIDDLE_BUTTON_UP,   MouseButton_Middle, false },
            { RI_MOUSE_RIGHT_BUTTON_DOWN,  MouseButton_Right,  true },
            { RI_MOUSE_RIGHT_BUTTON_UP,    MouseButton_Right,  false },
            { RI_MOUSE_BUTTON_4_DOWN,      MouseButton_X1,     true },
            { RI_MOUSE_BUTTON_4_UP,        MouseButton_X1,     false },
            { RI_MOUSE_BUTTON_5_DOWN,      MouseButton_X2,     true },
            { RI_MOUSE_BUTTON_5_UP,        MouseButton_X2,     false },
        };

        for (const RawButton& button : kButtons)
        {
            if ((packet.buttonFlags & button.flag) != 0)
                ApplyMouseButton(id, button.bit, button.down);
        }
    }
}

#else

void ProcessRawMouse() {}
void StopRawMouse() {}

#endif

bool RawInputActive()
{
#if defined(_WIN32)
    return g_rawWindow != nullptr;
#else
    return false;
#endif
}

uint32_t MouseButtonMaskFromSDL(SDL_MouseButtonFlags state)
{
    uint32_t mask = 0;
    if (state & SDL_BUTTON_LMASK)
        mask |= MouseButton_Left;
    if (state & SDL_BUTTON_MMASK)
        mask |= MouseButton_Middle;
    if (state & SDL_BUTTON_RMASK)
        mask |= MouseButton_Right;
    if (state & SDL_BUTTON_X1MASK)
        mask |= MouseButton_X1;
    if (state & SDL_BUTTON_X2MASK)
        mask |= MouseButton_X2;
    return mask;
}

uint32_t MouseButtonMaskFromEvent(Uint8 button)
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:   return MouseButton_Left;
    case SDL_BUTTON_MIDDLE: return MouseButton_Middle;
    case SDL_BUTTON_RIGHT:  return MouseButton_Right;
    case SDL_BUTTON_X1:     return MouseButton_X1;
    case SDL_BUTTON_X2:     return MouseButton_X2;
    default:                return 0;
    }
}

Window* WindowFromID(SDL_WindowID windowID)
{
    return WindowFromSDLWindow(SDL_GetWindowFromID(windowID));
}

void CopyName(char* destination, size_t destinationSize, const char* source)
{
    if (destinationSize == 0)
        return;
    if (source == nullptr)
        source = "";
    std::strncpy(destination, source, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
}

float NormalizeAxis(Sint16 value)
{
    return value < 0 ? static_cast<float>(value) / 32768.0f
                     : static_cast<float>(value) / 32767.0f;
}

uint32_t DecodeFirstUtf8(const char* text)
{
    if (text == nullptr || text[0] == '\0')
        return 0;

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text);
    if (bytes[0] < 0x80)
        return bytes[0];
    if ((bytes[0] & 0xE0) == 0xC0)
        return ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
    if ((bytes[0] & 0xF0) == 0xE0)
        return ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
    if ((bytes[0] & 0xF8) == 0xF0)
        return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) |
               ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
    return 0;
}

int16_t HatToAngle(Uint8 hat)
{
    switch (hat)
    {
    case SDL_HAT_UP:        return 0;
    case SDL_HAT_RIGHT:     return 90;
    case SDL_HAT_DOWN:      return 180;
    case SDL_HAT_LEFT:      return 270;
    case SDL_HAT_RIGHTUP:   return 45;
    case SDL_HAT_RIGHTDOWN: return 135;
    case SDL_HAT_LEFTDOWN:  return 225;
    case SDL_HAT_LEFTUP:    return 315;
    default:                return -1;
    }
}

OpenGamepadEntry* FindGamepad(GamepadId id)
{
    for (OpenGamepadEntry& entry : g_gamepads)
    {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

std::vector<SDL_JoystickID> GetJoystickIds()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    std::vector<SDL_JoystickID> result;
    if (ids != nullptr)
    {
        result.assign(ids, ids + count);
        SDL_free(ids);
    }
    return result;
}

void FillGamepadInfo(SDL_JoystickID id, int index, GamepadInfo* info)
{
    *info = GamepadInfo();
    info->id = static_cast<GamepadId>(id);
    info->index = index;
    info->isGamepad = SDL_IsGamepad(id);

    const char* name = info->isGamepad ? SDL_GetGamepadNameForID(id)
                                       : SDL_GetJoystickNameForID(id);
    CopyName(info->name, sizeof(info->name), name);

    info->vendor = SDL_GetJoystickVendorForID(id);
    info->product = SDL_GetJoystickProductForID(id);

    SDL_Joystick* joystick = SDL_OpenJoystick(id);
    if (joystick != nullptr)
    {
        info->numAxes = static_cast<uint8_t>(std::max(0, SDL_GetNumJoystickAxes(joystick)));
        info->numButtons = static_cast<uint8_t>(std::max(0, SDL_GetNumJoystickButtons(joystick)));
        info->numHats = static_cast<uint8_t>(std::max(0, SDL_GetNumJoystickHats(joystick)));

        SDL_PropertiesID props = SDL_GetJoystickProperties(joystick);
        info->hasHaptic = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
        info->hasRumble = info->hasHaptic;
        info->hasTriggerRumble = SDL_GetBooleanProperty(
            props, SDL_PROP_JOYSTICK_CAP_TRIGGER_RUMBLE_BOOLEAN, false);
        SDL_CloseJoystick(joystick);
    }
}

int DeviceIndexFromId(GamepadId id)
{
    const std::vector<SDL_JoystickID> ids = GetJoystickIds();
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (static_cast<GamepadId>(ids[i]) == id)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace

const InputSnapshot& Input()
{
    return g_input;
}

int GetMouseCount()
{
    // Enumerating here as well as in BeginInputFrame() matters at start-up: the input system asks
    // for the device count once, before the first frame, and a zero would leave it with no mouse
    // sources at all - every gun binding would be dead for the whole session.
    RefreshMouseList();

    int real = 0;
    for (const MouseEntry& entry : g_mice)
    {
        if (entry.id != kGlobalMouseId)
            real++;
    }

    if (real > 0)
        return real;

    // A single cursor is still one mouse even when the platform did not enumerate it.
    EnsureMouse(kGlobalMouseId);
    return 1;
}

namespace
{
MouseEntry* MouseAt(int index)
{
    int real = 0;
    for (MouseEntry& entry : g_mice)
    {
        if (entry.id == kGlobalMouseId)
            continue;
        if (real++ == index)
            return &entry;
    }

    if (index == 0)
    {
        for (MouseEntry& entry : g_mice)
        {
            if (entry.id == kGlobalMouseId)
                return &entry;
        }
    }

    return nullptr;
}

void CopyMouseState(const MouseEntry& entry, MouseState* state)
{
    *state = MouseState();
    state->id = entry.id;
    state->connected = true;
    state->x = entry.x;
    state->y = entry.y;
    state->dx = entry.dx;
    state->dy = entry.dy;
    state->buttons = entry.buttons;
}
}

bool GetMouseInfo(int index, MouseInfo* info)
{
    if (info == nullptr)
        return false;

    MouseEntry* entry = MouseAt(index);
    if (entry == nullptr)
        return false;

    *info = MouseInfo();
    info->id = entry->id;
    info->index = index;
    CopyName(info->name, sizeof(info->name), entry->name.c_str());
    return true;
}

bool GetMouseStateByIndex(int index, MouseState* state)
{
    if (state == nullptr)
        return false;

    MouseEntry* entry = MouseAt(index);
    if (entry == nullptr)
        return false;

    CopyMouseState(*entry, state);
    return true;
}

bool GetMouseStateById(MouseId id, MouseState* state)
{
    if (state == nullptr)
        return false;

    MouseEntry* entry = FindMouse(id);
    if (entry == nullptr)
        return false;

    CopyMouseState(*entry, state);
    return true;
}

void EnsurePerDeviceMouseInput()
{
#if defined(_WIN32)
    StartRawMouse();

    // With no focused window there is nothing to aim at, and re-registering while another window
    // owns the mouse usage could take it away from SDL's relative-mode thread.
    if (SDL_GetMouseFocus() == nullptr)
        return;

    static bool wasRelative = false;
    static Nanoseconds lastRegistration = 0;

    if (g_input.relativeMouse)
    {
        // SDL registers the mouse usage for its own raw thread while relative mode is active; our
        // registration would replace it and break relative motion. Leave it alone until it ends.
        wasRelative = true;
        return;
    }

    const Nanoseconds now = Now();
    const bool returningFromRelative = wasRelative;
    wasRelative = false;

    // Registration is process-wide and cheap, but not free enough for every frame; one second is
    // also how long a hand-back from relative mode can take to be noticed.
    if (!returningFromRelative && now - lastRegistration < 1000000000)
        return;

    lastRegistration = now;

    const HWND window = g_rawWindow.load();
    if (window == nullptr)
        return;

    RAWINPUTDEVICE device = {};
    device.usUsagePage = 0x01;      // Generic Desktop
    device.usUsage = 0x02;          // Mouse
    device.dwFlags = 0;
    device.hwndTarget = window;
    RegisterRawInputDevices(&device, 1, sizeof(device));
#endif
}

void ShutdownPerDeviceMouseInput()
{
#if defined(_WIN32)
    StopRawMouse();
#endif
}

void RefreshPerDeviceMouse()
{
    // The raw thread runs independently of the frame, so a packet can arrive between the event
    // drain at the top of the frame and RunFrame(). Draining it here is what keeps the late-sampled
    // gun position as fresh as the aggregate cursor path.
    ProcessRawMouse();
}

void BeginInputFrame()
{
    g_input.mouseDX = 0.0f;
    g_input.mouseDY = 0.0f;
    g_input.wheelX = 0.0f;
    g_input.wheelY = 0.0f;
    g_input.quitRequested = false;

    SDL_PumpEvents();

    int keyCount = 0;
    const bool* keys = SDL_GetKeyboardState(&keyCount);
    g_input.keys = reinterpret_cast<const uint8_t*>(keys);

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouseX, &mouseY);
    g_input.mouseX = mouseX;
    g_input.mouseY = mouseY;
    g_input.mouseButtons = MouseButtonMaskFromSDL(buttons);

    SDL_Window* focus = SDL_GetMouseFocus();
    g_input.mouseWindow = WindowFromSDLWindow(focus);
    g_input.keyboardWindow = WindowFromSDLWindow(SDL_GetKeyboardFocus());
    g_input.relativeMouse = focus != nullptr && SDL_GetWindowRelativeMouseMode(focus);
    g_input.textInputActive = focus != nullptr && SDL_TextInputActive(focus);

    RefreshMouseList();

    for (MouseEntry& entry : g_mice)
    {
        entry.dx = 0.0f;
        entry.dy = 0.0f;
    }

    ProcessRawMouse();
    EnsurePerDeviceMouseInput();

    if (g_mice.empty())
    {
        // No enumeration (or RawInput unavailable): keep a single device fed by the system cursor,
        // so MOUSE1_* bindings behave exactly as they did before per-device support.
        MouseEntry& entry = EnsureMouse(kGlobalMouseId);
        entry.x = g_input.mouseX;
        entry.y = g_input.mouseY;
        entry.buttons = g_input.mouseButtons;
        entry.seeded = true;
    }
    else if (g_mice.size() == 1)
    {
        /*
         * One device: the cursor is the truth, absolute and unrounded, with or without raw input.
         *
         * Raw input reports unaccelerated device counts, so accumulating them into a position
         * drifts away from the pointer - the more so as pointer acceleration, screen-edge clamping
         * and pointer warps all apply to the cursor but not to the accumulated value. That was
         * harmless while only relative motion read it, but the absolute light-gun axes read the
         * per-device position, and it made the emulated gun disagree with the crosshair, which the
         * late sample takes straight from the cursor. With a single mouse there is nothing to tell
         * apart, so the cursor wins.
         */
        MouseEntry& entry = g_mice.front();
        entry.x = g_input.mouseX;
        entry.y = g_input.mouseY;
        entry.dx = g_input.mouseDX;
        entry.dy = g_input.mouseDY;
        entry.buttons = g_input.mouseButtons;
        entry.seeded = true;
    }
}

bool PollEvent(Event* event)
{
    if (event == nullptr)
        return false;

    SDL_Event sdlEvent;
    if (!SDL_PollEvent(&sdlEvent))
        return false;

    *event = Event();
    event->timestampNs = static_cast<Nanoseconds>(sdlEvent.common.timestamp);

    switch (sdlEvent.type)
    {
    case SDL_EVENT_QUIT:
        event->type = EventType::Quit;
        g_input.quitRequested = true;
        return true;

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        event->type = EventType::WindowClose;
        event->window = WindowFromID(sdlEvent.window.windowID);
        return true;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        event->type = EventType::WindowFocusGained;
        event->window = WindowFromID(sdlEvent.window.windowID);
        g_input.keyboardWindow = event->window;
        return true;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        event->type = EventType::WindowFocusLost;
        event->window = WindowFromID(sdlEvent.window.windowID);
        return true;

    case SDL_EVENT_WINDOW_RESIZED:
        event->type = EventType::WindowResized;
        event->window = WindowFromID(sdlEvent.window.windowID);
        event->width = sdlEvent.window.data1;
        event->height = sdlEvent.window.data2;
        return true;

    case SDL_EVENT_WINDOW_SHOWN:
        event->type = EventType::WindowShown;
        event->window = WindowFromID(sdlEvent.window.windowID);
        return true;

    case SDL_EVENT_WINDOW_HIDDEN:
        event->type = EventType::WindowHidden;
        event->window = WindowFromID(sdlEvent.window.windowID);
        return true;

    case SDL_EVENT_MOUSE_MOTION:
        event->type = EventType::MouseMotion;
        event->window = WindowFromID(sdlEvent.motion.windowID);
        event->x = sdlEvent.motion.x;
        event->y = sdlEvent.motion.y;
        event->dx = sdlEvent.motion.xrel;
        event->dy = sdlEvent.motion.yrel;
        event->buttons = MouseButtonMaskFromSDL(sdlEvent.motion.state);
        g_input.mouseX = event->x;
        g_input.mouseY = event->y;
        g_input.mouseDX += event->dx;
        g_input.mouseDY += event->dy;
        g_input.mouseButtons = event->buttons;
        g_input.mouseWindow = event->window;
        if (!RawInputActive() || g_input.relativeMouse)
            ApplyMouseMotion(TargetMouseId(static_cast<MouseId>(sdlEvent.motion.which)),
                event->x, event->y, event->dx, event->dy);
        return true;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        event->type = sdlEvent.button.down ? EventType::MouseButtonDown : EventType::MouseButtonUp;
        event->window = WindowFromID(sdlEvent.button.windowID);
        event->x = sdlEvent.button.x;
        event->y = sdlEvent.button.y;
        event->button = sdlEvent.button.button;
        event->clicks = sdlEvent.button.clicks;
        event->down = sdlEvent.button.down;
        const uint32_t bit = MouseButtonMaskFromEvent(sdlEvent.button.button);
        if (sdlEvent.button.down)
            g_input.mouseButtons |= bit;
        else
            g_input.mouseButtons &= ~bit;
        event->buttons = g_input.mouseButtons;
        g_input.mouseX = event->x;
        g_input.mouseY = event->y;
        g_input.mouseWindow = event->window;
        if (!RawInputActive() || g_input.relativeMouse)
            ApplyMouseButton(TargetMouseId(static_cast<MouseId>(sdlEvent.button.which)), bit, sdlEvent.button.down);
        return true;
    }

    case SDL_EVENT_MOUSE_WHEEL:
        event->type = EventType::MouseWheel;
        event->window = WindowFromID(sdlEvent.wheel.windowID);
        event->wheelX = sdlEvent.wheel.x;
        event->wheelY = sdlEvent.wheel.y;
        if (sdlEvent.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        {
            event->wheelX = -event->wheelX;
            event->wheelY = -event->wheelY;
        }
        g_input.wheelX += event->wheelX;
        g_input.wheelY += event->wheelY;
        return true;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        event->type = sdlEvent.key.down ? EventType::KeyDown : EventType::KeyUp;
        event->window = WindowFromID(sdlEvent.key.windowID);
        event->scancode = static_cast<Scancode>(sdlEvent.key.scancode);
        event->keycode = static_cast<Keycode>(sdlEvent.key.key);
        event->repeat = sdlEvent.key.repeat;
        g_input.keyboardWindow = event->window;
        return true;

    case SDL_EVENT_TEXT_INPUT:
        event->type = EventType::TextInput;
        event->window = WindowFromID(sdlEvent.text.windowID);
        CopyName(event->text, sizeof(event->text), sdlEvent.text.text);
        event->codepoint = DecodeFirstUtf8(sdlEvent.text.text);
        return true;

    case SDL_EVENT_MOUSE_REMOVED:
        {
            // Drop the device from the per-device table so the mouse numbers the bindings name do
            // not keep referring to hardware that is no longer plugged in.
            const MouseId removed = static_cast<MouseId>(sdlEvent.mdevice.which);
            g_mice.erase(std::remove_if(g_mice.begin(), g_mice.end(),
                [removed](const MouseEntry& entry) { return entry.id == removed; }), g_mice.end());
            event->type = EventType::None;
        }
        return true;

    case SDL_EVENT_JOYSTICK_ADDED:
        event->type = EventType::GamepadAdded;
        event->gamepadId = static_cast<GamepadId>(sdlEvent.jdevice.which);
        return true;

    case SDL_EVENT_JOYSTICK_REMOVED:
        event->type = EventType::GamepadRemoved;
        event->gamepadId = static_cast<GamepadId>(sdlEvent.jdevice.which);
        return true;

    case SDL_EVENT_GAMEPAD_REMAPPED:
        event->type = EventType::GamepadRemapped;
        event->gamepadId = static_cast<GamepadId>(sdlEvent.gdevice.which);
        return true;

    default:
        event->type = EventType::None;
        return true;
    }
}

int GetGamepadCount()
{
    return static_cast<int>(GetJoystickIds().size());
}

bool GetGamepadInfo(int index, GamepadInfo* info)
{
    if (info == nullptr)
        return false;

    const std::vector<SDL_JoystickID> ids = GetJoystickIds();
    if (index < 0 || index >= static_cast<int>(ids.size()))
        return false;

    FillGamepadInfo(ids[index], index, info);
    return true;
}

bool GetGamepadInfoById(GamepadId id, GamepadInfo* info)
{
    const int index = DeviceIndexFromId(id);
    if (index < 0)
        return false;
    return GetGamepadInfo(index, info);
}

bool OpenGamepad(GamepadId id)
{
    if (id == kInvalidGamepad || FindGamepad(id) != nullptr)
        return false;

    const int index = DeviceIndexFromId(id);
    if (index < 0)
        return false;

    OpenGamepadEntry entry;
    entry.id = id;
    FillGamepadInfo(static_cast<SDL_JoystickID>(id), index, &entry.info);

    if (entry.info.isGamepad)
    {
        entry.gamepad = SDL_OpenGamepad(static_cast<SDL_JoystickID>(id));
        if (entry.gamepad == nullptr)
            return false;
        entry.joystick = SDL_GetGamepadJoystick(entry.gamepad);
    }
    else
    {
        entry.joystick = SDL_OpenJoystick(static_cast<SDL_JoystickID>(id));
        if (entry.joystick == nullptr)
            return false;
    }

    g_gamepads.push_back(entry);
    return true;
}

void CloseGamepad(GamepadId id)
{
    CloseHaptics(id);

    for (auto it = g_gamepads.begin(); it != g_gamepads.end(); ++it)
    {
        if (it->id != id)
            continue;

        if (it->gamepad != nullptr)
            SDL_CloseGamepad(it->gamepad);
        else if (it->joystick != nullptr)
            SDL_CloseJoystick(it->joystick);

        g_gamepads.erase(it);
        return;
    }
}

bool IsGamepadOpen(GamepadId id)
{
    return FindGamepad(id) != nullptr;
}

bool GetGamepadState(GamepadId id, GamepadState* state)
{
    if (state == nullptr)
        return false;

    OpenGamepadEntry* entry = FindGamepad(id);
    if (entry == nullptr || entry->joystick == nullptr)
        return false;

    *state = GamepadState();
    state->id = id;
    state->connected = true;

    const int axisCount = SDL_GetNumJoystickAxes(entry->joystick);
    for (unsigned i = 0; i < kMaxGamepadAxes; ++i)
    {
        if (static_cast<int>(i) < axisCount)
            state->axes[i] = NormalizeAxis(SDL_GetJoystickAxis(entry->joystick, static_cast<int>(i)));
    }

    const int buttonCount = SDL_GetNumJoystickButtons(entry->joystick);
    for (unsigned i = 0; i < kMaxGamepadButtons && static_cast<int>(i) < buttonCount; ++i)
    {
        if (SDL_GetJoystickButton(entry->joystick, static_cast<int>(i)))
            state->buttons |= (1u << i);
    }

    const int hatCount = SDL_GetNumJoystickHats(entry->joystick);
    for (unsigned i = 0; i < kMaxGamepadHats; ++i)
    {
        if (static_cast<int>(i) < hatCount)
            state->hats[i] = HatToAngle(SDL_GetJoystickHat(entry->joystick, static_cast<int>(i)));
        else
            state->hats[i] = -1;
    }

    if (entry->gamepad != nullptr)
    {
        state->standardAxes[static_cast<unsigned>(PadAxis::LeftX)] =
            NormalizeAxis(SDL_GetGamepadAxis(entry->gamepad, SDL_GAMEPAD_AXIS_LEFTX));
        state->standardAxes[static_cast<unsigned>(PadAxis::LeftY)] =
            NormalizeAxis(SDL_GetGamepadAxis(entry->gamepad, SDL_GAMEPAD_AXIS_LEFTY));
        state->standardAxes[static_cast<unsigned>(PadAxis::RightX)] =
            NormalizeAxis(SDL_GetGamepadAxis(entry->gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
        state->standardAxes[static_cast<unsigned>(PadAxis::RightY)] =
            NormalizeAxis(SDL_GetGamepadAxis(entry->gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
        state->standardAxes[static_cast<unsigned>(PadAxis::LeftTrigger)] =
            static_cast<float>(SDL_GetGamepadAxis(entry->gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) / 32767.0f;
        state->standardAxes[static_cast<unsigned>(PadAxis::RightTrigger)] =
            static_cast<float>(SDL_GetGamepadAxis(entry->gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) / 32767.0f;

        static const SDL_GamepadButton kButtons[] = {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
            SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE,
            SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
            SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
            SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
            SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
            SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_MISC1,
            SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
            SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
            SDL_GAMEPAD_BUTTON_TOUCHPAD
        };

        for (unsigned i = 0; i < static_cast<unsigned>(PadButton::Count) &&
             i < sizeof(kButtons) / sizeof(kButtons[0]); ++i)
        {
            if (SDL_GetGamepadButton(entry->gamepad, kButtons[i]))
                state->standardButtons |= PadButtonBit(static_cast<PadButton>(i));
        }
    }

    return true;
}

/*
 * Effect-based haptics.
 *
 * A constant-force update ramps from the previous level through the effect's attack envelope.
 * DirectInput has no notion of a torque slew rate, and the game only produces a new target at
 * 57 Hz; a 20 ms attack turns that stair-step into a continuous change, which is what a direct
 * drive wheel needs to feel like a load rather than a series of clicks.
 */
namespace
{
constexpr int kHapticSmoothingMs = 20;

bool UpdateConstantForce(OpenGamepadEntry& entry, float strength)
{
    if (!entry.hapticConstantSupported)
        return false;

    SDL_HapticEffect effect = {};
    effect.type = SDL_HAPTIC_CONSTANT;
    effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.constant.direction.dir[0] = strength >= 0.0f ? 1 : -1;
    effect.constant.length = SDL_HAPTIC_INFINITY;
    effect.constant.level = static_cast<Sint16>(std::fabs(strength) * 32767.0f);
    effect.constant.attack_length = kHapticSmoothingMs;
    effect.constant.attack_level = entry.hapticConstantLevel;
    effect.constant.fade_length = 0;
    effect.constant.fade_level = effect.constant.level;

    if (entry.hapticConstant < 0)
    {
        // Only ask the driver once: this runs at the game's 57 Hz command rate.
        if (!SDL_HapticEffectSupported(entry.haptic, &effect))
        {
            entry.hapticConstantSupported = false;
            return false;
        }
        entry.hapticConstant = SDL_CreateHapticEffect(entry.haptic, &effect);
    }
    else
    {
        SDL_UpdateHapticEffect(entry.haptic, entry.hapticConstant, &effect);
    }

    if (entry.hapticConstant < 0)
    {
        entry.hapticConstantSupported = false;
        return false;
    }

    SDL_RunHapticEffect(entry.haptic, entry.hapticConstant, 1);
    entry.hapticConstantLevel = effect.constant.level;
    return true;
}

bool UpdateConditionForce(OpenGamepadEntry& entry, HapticEffect type, float strength)
{
    const bool spring = (type == HapticEffect::Spring);
    bool& supported = spring ? entry.hapticSpringSupported : entry.hapticFrictionSupported;
    int& effectId = spring ? entry.hapticSpring : entry.hapticFriction;

    if (!supported)
        return false;

    SDL_HapticEffect effect = {};
    effect.type = spring ? SDL_HAPTIC_SPRING : SDL_HAPTIC_FRICTION;
    effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.condition.direction.dir[0] = 0;
    effect.condition.direction.dir[1] = 0;
    effect.condition.length = SDL_HAPTIC_INFINITY;
    effect.condition.left_sat[0] = 0xFFFF;
    effect.condition.right_sat[0] = 0xFFFF;

    const Sint16 coefficient = static_cast<Sint16>(std::clamp(strength, 0.0f, 1.0f) * 32767.0f);
    effect.condition.left_coeff[0] = coefficient;
    effect.condition.right_coeff[0] = coefficient;

    if (effectId < 0)
    {
        // Driver query is not free, and this is called at the game's command rate.
        if (!SDL_HapticEffectSupported(entry.haptic, &effect))
        {
            supported = false;
            return false;
        }
        effectId = SDL_CreateHapticEffect(entry.haptic, &effect);
    }
    else
    {
        SDL_UpdateHapticEffect(entry.haptic, effectId, &effect);
    }

    if (effectId < 0)
    {
        supported = false;
        return false;
    }

    SDL_RunHapticEffect(entry.haptic, effectId, 1);
    return true;
}
}

bool OpenHaptics(GamepadId id)
{
    OpenGamepadEntry* entry = FindGamepad(id);
    if (entry == nullptr || entry->joystick == nullptr)
        return false;

    if (entry->haptic != nullptr)
        return true;

    entry->haptic = SDL_OpenHapticFromJoystick(entry->joystick);
    if (entry->haptic == nullptr)
        return false;

    SDL_SetHapticGain(entry->haptic, 100);
    return true;
}

void CloseHaptics(GamepadId id)
{
    OpenGamepadEntry* entry = FindGamepad(id);
    if (entry == nullptr || entry->haptic == nullptr)
        return;

    if (entry->hapticConstant >= 0)
        SDL_DestroyHapticEffect(entry->haptic, entry->hapticConstant);
    if (entry->hapticSpring >= 0)
        SDL_DestroyHapticEffect(entry->haptic, entry->hapticSpring);
    if (entry->hapticFriction >= 0)
        SDL_DestroyHapticEffect(entry->haptic, entry->hapticFriction);

    SDL_CloseHaptic(entry->haptic);

    entry->haptic = nullptr;
    entry->hapticConstant = -1;
    entry->hapticSpring = -1;
    entry->hapticFriction = -1;
    entry->hapticConstantSupported = true;
    entry->hapticSpringSupported = true;
    entry->hapticFrictionSupported = true;
    entry->hapticConstantLevel = 0;
}

bool HapticsAvailable(GamepadId id)
{
    OpenGamepadEntry* entry = FindGamepad(id);
    return entry != nullptr && entry->haptic != nullptr;
}

bool SetHapticEffect(GamepadId id, HapticEffect effect, float strength)
{
    OpenGamepadEntry* entry = FindGamepad(id);
    if (entry == nullptr || entry->haptic == nullptr)
        return false;

    if (effect == HapticEffect::Constant)
    {
        const float clamped = std::clamp(strength, -1.0f, 1.0f);
        if (clamped == 0.0f)
        {
            if (entry->hapticConstant >= 0)
                SDL_StopHapticEffect(entry->haptic, entry->hapticConstant);
            entry->hapticConstantLevel = 0;
            return true;
        }
        return UpdateConstantForce(*entry, clamped);
    }

    const float clamped = std::clamp(strength, 0.0f, 1.0f);
    if (clamped == 0.0f)
    {
        const int effectId = (effect == HapticEffect::Spring) ? entry->hapticSpring : entry->hapticFriction;
        if (effectId >= 0)
            SDL_StopHapticEffect(entry->haptic, effectId);
        return true;
    }

    return UpdateConditionForce(*entry, effect, clamped);
}

void StopHapticEffect(GamepadId id, HapticEffect effect)
{
    OpenGamepadEntry* entry = FindGamepad(id);
    if (entry == nullptr || entry->haptic == nullptr)
        return;

    if (effect == HapticEffect::Constant)
    {
        if (entry->hapticConstant >= 0)
            SDL_StopHapticEffect(entry->haptic, entry->hapticConstant);
        entry->hapticConstantLevel = 0;
        return;
    }

    const int effectId = (effect == HapticEffect::Spring) ? entry->hapticSpring : entry->hapticFriction;
    if (effectId >= 0)
        SDL_StopHapticEffect(entry->haptic, effectId);
}

void StopAllHapticEffects(GamepadId id)
{
    OpenGamepadEntry* entry = FindGamepad(id);
    if (entry == nullptr || entry->haptic == nullptr)
        return;

    SDL_StopHapticEffects(entry->haptic);
    entry->hapticConstantLevel = 0;
}

bool SetGamepadRumble(GamepadId id, float lowFrequency, float highFrequency, uint32_t durationMs)
{
    OpenGamepadEntry* entry = FindGamepad(id);
    if (entry == nullptr || entry->gamepad == nullptr || !entry->info.hasRumble)
        return false;

    const auto toIntensity = [](float value) -> Uint16 {
        const float clamped = std::max(0.0f, std::min(1.0f, value));
        return static_cast<Uint16>(clamped * 65535.0f + 0.5f);
    };

    return SDL_RumbleGamepad(entry->gamepad, toIntensity(lowFrequency),
        toIntensity(highFrequency), durationMs);
}

void SetTextInputActive(Window* window, bool active)
{
    SDL_Window* sdl = SDLWindowFromWindow(window);
    if (sdl == nullptr)
        return;

    // SDL3 only delivers SDL_EVENT_TEXT_INPUT once text input has been started, and it is off by
    // default. Asked for on every frame, so the state is compared first to keep this cheap.
    if (active == SDL_TextInputActive(sdl))
        return;

    if (active)
        SDL_StartTextInput(sdl);
    else
        SDL_StopTextInput(sdl);
}

} // Platform
} // OSD
