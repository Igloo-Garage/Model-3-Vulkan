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
 * PlatformInputSDL2.cpp
 *
 * SDL2 input backend: one event drain, a POD snapshot, and a gamepad registry
 * keyed by the stable joystick instance id.
 *
 * The event loop used to live inside CSDLInputSystem::Poll().  It is here now
 * so the settings window and the game loop can share one drain and so the SDL3
 * backend has a shape to implement.  The mapping/configuration logic stays in
 * CSDLInputSystem; this file only turns SDL events into neutral events and
 * device state.
 */

#include "PlatformSDL2Internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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
    SDL_GameController* controller = nullptr;
    SDL_Joystick* joystick = nullptr;
    GamepadInfo info;
};

std::vector<OpenGamepadEntry> g_gamepads;

uint32_t MouseButtonMaskFromSDL(Uint32 state)
{
    uint32_t mask = 0;
    if (state & SDL_BUTTON(SDL_BUTTON_LEFT))
        mask |= MouseButton_Left;
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE))
        mask |= MouseButton_Middle;
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT))
        mask |= MouseButton_Right;
    if (state & SDL_BUTTON(SDL_BUTTON_X1))
        mask |= MouseButton_X1;
    if (state & SDL_BUTTON(SDL_BUTTON_X2))
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

Window* WindowFromID(Uint32 windowID)
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

GamepadId DeviceInstanceId(int index)
{
    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    return id >= 0 ? static_cast<GamepadId>(id) : kInvalidGamepad;
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
    for (OpenGamepadEntry& gamepad : g_gamepads)
    {
        if (gamepad.id == id)
            return &gamepad;
    }
    return nullptr;
}

int DeviceIndexFromId(GamepadId id)
{
    const int count = SDL_NumJoysticks();
    for (int i = 0; i < count; ++i)
    {
        if (static_cast<GamepadId>(SDL_JoystickGetDeviceInstanceID(i)) == id)
            return i;
    }
    return -1;
}

void FillGamepadInfoFromDevice(int index, GamepadInfo* info)
{
    *info = GamepadInfo();
    info->index = index;
    info->id = DeviceInstanceId(index);
    info->isGamepad = SDL_IsGameController(index) == SDL_TRUE;

    const char* name = info->isGamepad ? SDL_GameControllerNameForIndex(index)
                                       : SDL_JoystickNameForIndex(index);
    CopyName(info->name, sizeof(info->name), name);

    info->vendor = SDL_JoystickGetDeviceVendor(index);
    info->product = SDL_JoystickGetDeviceProduct(index);

    SDL_Joystick* joystick = SDL_JoystickOpen(index);
    if (joystick != nullptr)
    {
        info->numAxes = static_cast<uint8_t>(std::max(0, SDL_JoystickNumAxes(joystick)));
        info->numButtons = static_cast<uint8_t>(std::max(0, SDL_JoystickNumButtons(joystick)));
        info->numHats = static_cast<uint8_t>(std::max(0, SDL_JoystickNumHats(joystick)));
        info->hasHaptic = SDL_JoystickIsHaptic(joystick) == 1;
        SDL_JoystickClose(joystick);
    }

    if (info->isGamepad)
    {
        SDL_GameController* controller = SDL_GameControllerOpen(index);
        if (controller != nullptr)
        {
            info->hasRumble = SDL_GameControllerHasRumble(controller) == SDL_TRUE;
            info->hasTriggerRumble = SDL_GameControllerHasRumbleTriggers(controller) == SDL_TRUE;
            SDL_GameControllerClose(controller);
        }
    }
}

} // namespace

const InputSnapshot& Input()
{
    return g_input;
}

void BeginInputFrame()
{
    g_input.mouseDX = 0.0f;
    g_input.mouseDY = 0.0f;
    g_input.wheelX = 0.0f;
    g_input.wheelY = 0.0f;
    g_input.quitRequested = false;

    SDL_PumpEvents();

    g_input.keys = SDL_GetKeyboardState(nullptr);

    int mouseX = 0;
    int mouseY = 0;
    const Uint32 buttons = SDL_GetMouseState(&mouseX, &mouseY);
    g_input.mouseX = static_cast<float>(mouseX);
    g_input.mouseY = static_cast<float>(mouseY);
    g_input.mouseButtons = MouseButtonMaskFromSDL(buttons);
    g_input.relativeMouse = SDL_GetRelativeMouseMode() == SDL_TRUE;
    g_input.textInputActive = SDL_IsTextInputActive() == SDL_TRUE;
}

bool PollEvent(Event* event)
{
    if (event == nullptr)
        return false;

    SDL_Event sdlEvent;
    if (SDL_PollEvent(&sdlEvent) == 0)
        return false;

    *event = Event();
    event->timestampNs = Now();

    switch (sdlEvent.type)
    {
    case SDL_QUIT:
        event->type = EventType::Quit;
        g_input.quitRequested = true;
        return true;

    case SDL_WINDOWEVENT:
        event->window = WindowFromID(sdlEvent.window.windowID);
        switch (sdlEvent.window.event)
        {
        case SDL_WINDOWEVENT_CLOSE:
            event->type = EventType::WindowClose;
            break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            event->type = EventType::WindowFocusGained;
            g_input.keyboardWindow = event->window;
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            event->type = EventType::WindowFocusLost;
            break;
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            event->type = EventType::WindowResized;
            event->width = sdlEvent.window.data1;
            event->height = sdlEvent.window.data2;
            break;
        case SDL_WINDOWEVENT_SHOWN:
            event->type = EventType::WindowShown;
            break;
        case SDL_WINDOWEVENT_HIDDEN:
            event->type = EventType::WindowHidden;
            break;
        default:
            event->type = EventType::None;
            break;
        }
        return true;

    case SDL_MOUSEMOTION:
        event->type = EventType::MouseMotion;
        event->window = WindowFromID(sdlEvent.motion.windowID);
        event->x = static_cast<float>(sdlEvent.motion.x);
        event->y = static_cast<float>(sdlEvent.motion.y);
        event->dx = static_cast<float>(sdlEvent.motion.xrel);
        event->dy = static_cast<float>(sdlEvent.motion.yrel);
        event->buttons = MouseButtonMaskFromSDL(sdlEvent.motion.state);
        g_input.mouseX = event->x;
        g_input.mouseY = event->y;
        g_input.mouseDX += event->dx;
        g_input.mouseDY += event->dy;
        g_input.mouseButtons = event->buttons;
        g_input.mouseWindow = event->window;
        return true;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        event->type = sdlEvent.type == SDL_MOUSEBUTTONDOWN ? EventType::MouseButtonDown
                                                           : EventType::MouseButtonUp;
        event->window = WindowFromID(sdlEvent.button.windowID);
        event->x = static_cast<float>(sdlEvent.button.x);
        event->y = static_cast<float>(sdlEvent.button.y);
        event->button = sdlEvent.button.button;
        event->clicks = sdlEvent.button.clicks;
        event->down = sdlEvent.type == SDL_MOUSEBUTTONDOWN;
        event->buttons = g_input.mouseButtons;
        {
            const uint32_t bit = MouseButtonMaskFromEvent(sdlEvent.button.button);
            if (event->down)
                g_input.mouseButtons |= bit;
            else
                g_input.mouseButtons &= ~bit;
            event->buttons = g_input.mouseButtons;
        }
        g_input.mouseX = event->x;
        g_input.mouseY = event->y;
        g_input.mouseWindow = event->window;
        return true;

    case SDL_MOUSEWHEEL:
        event->type = EventType::MouseWheel;
        event->window = WindowFromID(sdlEvent.wheel.windowID);
        event->wheelX = static_cast<float>(sdlEvent.wheel.x);
        event->wheelY = static_cast<float>(sdlEvent.wheel.y);
        if (sdlEvent.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        {
            event->wheelX = -event->wheelX;
            event->wheelY = -event->wheelY;
        }
        g_input.wheelX += event->wheelX;
        g_input.wheelY += event->wheelY;
        return true;

    case SDL_KEYDOWN:
    case SDL_KEYUP:
        event->type = sdlEvent.type == SDL_KEYDOWN ? EventType::KeyDown : EventType::KeyUp;
        event->window = WindowFromID(sdlEvent.key.windowID);
        event->scancode = static_cast<Scancode>(sdlEvent.key.keysym.scancode);
        event->keycode = static_cast<Keycode>(sdlEvent.key.keysym.sym);
        event->repeat = sdlEvent.key.repeat != 0;
        g_input.keyboardWindow = event->window;
        return true;

    case SDL_TEXTINPUT:
        event->type = EventType::TextInput;
        event->window = WindowFromID(sdlEvent.text.windowID);
        CopyName(event->text, sizeof(event->text), sdlEvent.text.text);
        event->codepoint = DecodeFirstUtf8(sdlEvent.text.text);
        return true;

    case SDL_JOYDEVICEADDED:
        event->type = EventType::GamepadAdded;
        event->deviceIndex = sdlEvent.jdevice.which;
        event->gamepadId = DeviceInstanceId(sdlEvent.jdevice.which);
        return true;

    case SDL_JOYDEVICEREMOVED:
        event->type = EventType::GamepadRemoved;
        event->gamepadId = static_cast<GamepadId>(sdlEvent.jdevice.which);
        return true;

    case SDL_CONTROLLERDEVICEREMAPPED:
        event->type = EventType::GamepadRemapped;
        event->gamepadId = static_cast<GamepadId>(sdlEvent.cdevice.which);
        return true;

    default:
        // The event has been consumed.  Returning false here would stop the
        // drain at the first event type the platform does not model, leaving
        // later events (including a quit) queued for the next frame.
        event->type = EventType::None;
        return true;
    }
}

int GetGamepadCount()
{
    return SDL_NumJoysticks();
}

bool GetGamepadInfo(int index, GamepadInfo* info)
{
    if (info == nullptr || index < 0 || index >= SDL_NumJoysticks())
        return false;

    FillGamepadInfoFromDevice(index, info);
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

    OpenGamepadEntry gamepad;
    gamepad.id = id;
    FillGamepadInfoFromDevice(index, &gamepad.info);

    if (gamepad.info.isGamepad)
    {
        gamepad.controller = SDL_GameControllerOpen(index);
        if (gamepad.controller == nullptr)
            return false;
        gamepad.joystick = SDL_GameControllerGetJoystick(gamepad.controller);
    }
    else
    {
        gamepad.joystick = SDL_JoystickOpen(index);
        if (gamepad.joystick == nullptr)
            return false;
    }

    g_gamepads.push_back(gamepad);
    return true;
}

void CloseGamepad(GamepadId id)
{
    for (auto it = g_gamepads.begin(); it != g_gamepads.end(); ++it)
    {
        if (it->id != id)
            continue;

        if (it->controller != nullptr)
            SDL_GameControllerClose(it->controller);
        else if (it->joystick != nullptr)
            SDL_JoystickClose(it->joystick);

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

    OpenGamepadEntry* gamepad = FindGamepad(id);
    if (gamepad == nullptr || gamepad->joystick == nullptr)
        return false;

    *state = GamepadState();
    state->id = id;
    state->connected = true;

    const int axisCount = SDL_JoystickNumAxes(gamepad->joystick);
    for (unsigned i = 0; i < kMaxGamepadAxes; ++i)
    {
        if (static_cast<int>(i) < axisCount)
            state->axes[i] = NormalizeAxis(SDL_JoystickGetAxis(gamepad->joystick, static_cast<int>(i)));
    }

    const int buttonCount = SDL_JoystickNumButtons(gamepad->joystick);
    for (unsigned i = 0; i < kMaxGamepadButtons && static_cast<int>(i) < buttonCount; ++i)
    {
        if (SDL_JoystickGetButton(gamepad->joystick, static_cast<int>(i)) != 0)
            state->buttons |= (1u << i);
    }

    const int hatCount = SDL_JoystickNumHats(gamepad->joystick);
    for (unsigned i = 0; i < kMaxGamepadHats; ++i)
    {
        if (static_cast<int>(i) < hatCount)
            state->hats[i] = HatToAngle(SDL_JoystickGetHat(gamepad->joystick, static_cast<int>(i)));
        else
            state->hats[i] = -1;
    }

    if (gamepad->controller != nullptr)
    {
        state->standardAxes[static_cast<unsigned>(PadAxis::LeftX)] =
            NormalizeAxis(SDL_GameControllerGetAxis(gamepad->controller, SDL_CONTROLLER_AXIS_LEFTX));
        state->standardAxes[static_cast<unsigned>(PadAxis::LeftY)] =
            NormalizeAxis(SDL_GameControllerGetAxis(gamepad->controller, SDL_CONTROLLER_AXIS_LEFTY));
        state->standardAxes[static_cast<unsigned>(PadAxis::RightX)] =
            NormalizeAxis(SDL_GameControllerGetAxis(gamepad->controller, SDL_CONTROLLER_AXIS_RIGHTX));
        state->standardAxes[static_cast<unsigned>(PadAxis::RightY)] =
            NormalizeAxis(SDL_GameControllerGetAxis(gamepad->controller, SDL_CONTROLLER_AXIS_RIGHTY));
        state->standardAxes[static_cast<unsigned>(PadAxis::LeftTrigger)] =
            static_cast<float>(SDL_GameControllerGetAxis(gamepad->controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)) / 32767.0f;
        state->standardAxes[static_cast<unsigned>(PadAxis::RightTrigger)] =
            static_cast<float>(SDL_GameControllerGetAxis(gamepad->controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) / 32767.0f;

        static const SDL_GameControllerButton kButtons[] = {
            SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
            SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
            SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE,
            SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK,
            SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_DPAD_UP,
            SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
            SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_MISC1,
            SDL_CONTROLLER_BUTTON_PADDLE1, SDL_CONTROLLER_BUTTON_PADDLE2,
            SDL_CONTROLLER_BUTTON_PADDLE3, SDL_CONTROLLER_BUTTON_PADDLE4,
            SDL_CONTROLLER_BUTTON_TOUCHPAD
        };

        for (unsigned i = 0; i < static_cast<unsigned>(PadButton::Count) &&
             i < sizeof(kButtons) / sizeof(kButtons[0]); ++i)
        {
            if (SDL_GameControllerGetButton(gamepad->controller, kButtons[i]) != 0)
                state->standardButtons |= PadButtonBit(static_cast<PadButton>(i));
        }
    }

    return true;
}

bool SetGamepadRumble(GamepadId id, float lowFrequency, float highFrequency, uint32_t durationMs)
{
    OpenGamepadEntry* gamepad = FindGamepad(id);
    if (gamepad == nullptr || gamepad->controller == nullptr)
        return false;
    if (!gamepad->info.hasRumble)
        return false;

    const auto toIntensity = [](float value) -> Uint16 {
        const float clamped = std::max(0.0f, std::min(1.0f, value));
        return static_cast<Uint16>(clamped * 65535.0f + 0.5f);
    };

    return SDL_GameControllerRumble(gamepad->controller,
        toIntensity(lowFrequency), toIntensity(highFrequency), durationMs) == 0;
}

void SetTextInputActive(Window* window, bool active)
{
    // SDL2 enables text input with the video subsystem, which is why the settings window on that
    // backend has always accepted typing. Nothing to do; the call exists so the shared ImGui layer
    // can stay backend-agnostic.
    (void)window;
    (void)active;
}

} // Platform
} // OSD
