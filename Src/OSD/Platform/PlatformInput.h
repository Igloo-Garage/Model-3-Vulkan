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
 * PlatformInput.h
 *
 * Backend-neutral keyboard, mouse and gamepad state plus the event queue
 * that produces it.
 *
 * The design keeps two things separate that SDL2 and SDL3 report very
 * differently:
 *
 *   - Absolute state (keyboard bits, mouse position and buttons) is a plain
 *     snapshot.  The consumer reads fields directly; there is no call per
 *     key and no allocation per frame.
 *
 *   - Relative motion is accumulated by the backend across every event
 *     between two BeginInputFrame() calls.  A 1000 Hz mouse therefore
 *     contributes all of its sub-frame movement to one float pair instead of
 *     losing all but the last event, which is the whole point of the SDL3
 *     target.  SDL2 only has integer deltas; the backend converts them, and
 *     kHasFloatMouseMotion tells the consumer whether sub-pixel precision is
 *     real or reconstructed.
 *
 * Scancodes are USB HID usage IDs (page 0x07), which is the numbering both
 * SDL versions use for SDL_Scancode.  Keycodes remain backend keycodes so
 * shortcut handling and text input keep working without a second mapping.
 */

#ifndef INCLUDED_PLATFORMINPUT_H
#define INCLUDED_PLATFORMINPUT_H

#include "Platform.h"

#include <cstdint>

namespace OSD
{
namespace Platform
{

using Scancode = uint16_t;
using Keycode = uint32_t;

// SDL allows scancodes up to 511.  Keeping the array this size means a
// backend can index it with the raw SDL scancode without a bounds test in
// the common path.
constexpr unsigned kScancodeCount = 512;

/*
 * A gamepad id is stable while the device stays connected, unlike an
 * enumeration index.  Declared here because Event carries one.
 */
using GamepadId = uint32_t;
constexpr GamepadId kInvalidGamepad = 0;

enum MouseButtonFlag : uint32_t
{
    MouseButton_Left = 1u << 0,
    MouseButton_Middle = 1u << 1,
    MouseButton_Right = 1u << 2,
    MouseButton_X1 = 1u << 3,
    MouseButton_X2 = 1u << 4
};

struct InputSnapshot
{
    /*
     * Pointer to the backend's key table, kScancodeCount bytes, updated by
     * the event drain.  Valid until the next BeginInputFrame().
     */
    const uint8_t* keys = nullptr;

    // Latest absolute position in window pixels.  SDL3 reports this as a
    // float; SDL2 reports integers and the backend widens them.
    float mouseX = 0.0f;
    float mouseY = 0.0f;

    // Motion accumulated since the previous BeginInputFrame().  In SDL3 this
    // is a sum of float xrel/yrel values; in SDL2 it is a sum of integers.
    float mouseDX = 0.0f;
    float mouseDY = 0.0f;

    // Wheel movement accumulated since the previous frame.  Wheel events are
    // discrete in both SDL versions; keeping them as floats here avoids a
    // second integer path in the consumer and leaves room for high-resolution
    // wheels later.
    float wheelX = 0.0f;
    float wheelY = 0.0f;

    uint32_t mouseButtons = 0;

    // The window the pointer/keyboard was last seen in.  Needed because the
    // settings window and the game window coexist.
    Window* mouseWindow = nullptr;
    Window* keyboardWindow = nullptr;

    bool relativeMouse = false;
    bool textInputActive = false;

    // Set when SDL_QUIT or a window close event was seen during this drain.
    bool quitRequested = false;

    bool KeyDown(Scancode scancode) const
    {
        return keys != nullptr && scancode < kScancodeCount && keys[scancode] != 0;
    }
};

/*
 * The snapshot filled by BeginInputFrame()/PollEvent().  There is one per
 * process because the input system is one per process; making it a reference
 * return keeps the hot path free of copies and ownership questions.
 */
const InputSnapshot& Input();

/*
 * Mouse devices.
 *
 * SDL3 enumerates mice separately and tags their events with a stable SDL_MouseID, which is what
 * makes two light guns possible at all. InputSnapshot still carries the system cursor (the
 * aggregate the single-mouse path has always used); the calls below expose the per-device stream
 * alongside it.
 *
 * Coordinates are window-relative, in the same pixel space as InputSnapshot::mouseX/Y. A device
 * that reports relative motion is integrated into a virtual position, seeded from the first event
 * it produces, so a Sinden-style gun and an absolute Gun4IR can share one code path. On Windows
 * SDL only delivers raw per-device events while relative mouse mode is on; the SDL3 backend
 * therefore also registers its own RawInput source, so absolute guns work in a normal window too.
 */
using MouseId = uint32_t;
// Zero is SDL's "global" pseudo-device id, so "no mouse" has to be a value SDL never reports.
constexpr MouseId kInvalidMouse = 0xFFFFFFFFu;

struct MouseInfo
{
    MouseId id = kInvalidMouse;
    int index = -1;                 // enumeration order, for the settings window
    char name[64] = {};
};

struct MouseState
{
    MouseId id = kInvalidMouse;
    bool connected = false;
    float x = 0.0f;                 // window-relative position
    float y = 0.0f;
    float dx = 0.0f;                // motion accumulated since BeginInputFrame()
    float dy = 0.0f;
    uint32_t buttons = 0;           // MouseButtonFlag mask
};

int GetMouseCount();
bool GetMouseInfo(int index, MouseInfo* info);
bool GetMouseStateByIndex(int index, MouseState* state);
bool GetMouseStateById(MouseId id, MouseState* state);

/*
 * Wakes the Windows RawInput source after relative mouse mode has handed the registration back to
 * SDL. Harmless on other platforms. Called once per frame by the input system.
 */
void EnsurePerDeviceMouseInput();

// Stops the raw input thread, if one was started. Called from Platform::Shutdown().
void ShutdownPerDeviceMouseInput();

/*
 * Drains any raw mouse packets that arrived after BeginInputFrame(). Called from the light-gun
 * late-sampling path, so a gun that moved while the panel was being built is seen by this frame
 * rather than the next one.
 */
void RefreshPerDeviceMouse();

/*
 * Event queue.
 *
 * The intended per-frame sequence is:
 *
 *     BeginInputFrame();
 *     Event ev;
 *     while (PollEvent(&ev)) { ... }
 *     // read Input() and gamepad state here
 *
 * BeginInputFrame() resets the per-frame motion accumulators and asks the OS
 * for pending events.  PollEvent() drains one event at a time, updates the
 * snapshot as a side effect and converts backend event types to EventType.
 *
 * PollEvent() must be the only event drain in a process.  The settings GUI
 * and the game loop each own a window, but they run at different times; a
 * second SDL_PollEvent loop would steal events from the first one.
 */
enum class EventType : uint8_t
{
    None = 0,
    Quit,
    WindowClose,
    WindowFocusGained,
    WindowFocusLost,
    WindowResized,
    WindowShown,
    WindowHidden,
    MouseMotion,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    KeyDown,
    KeyUp,
    TextInput,
    GamepadAdded,
    GamepadRemoved,
    GamepadRemapped
};

struct Event
{
    EventType type = EventType::None;
    Nanoseconds timestampNs = 0;

    Window* window = nullptr;
    DisplayId display = kPrimaryDisplay;

    // Mouse/position fields.  dx/dy are the deltas of this particular event,
    // not the accumulated frame delta in Input().
    float x = 0.0f;
    float y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float wheelX = 0.0f;
    float wheelY = 0.0f;

    // Keyboard fields.
    Scancode scancode = 0;
    Keycode keycode = 0;
    uint32_t codepoint = 0;       // TextInput, first codepoint
    char text[32] = {};           // TextInput, UTF-8 (for ImGui and text fields)
    bool repeat = false;

    // Mouse button fields.
    uint8_t button = 0;
    uint8_t clicks = 0;
    uint32_t buttons = 0;         // button bitmask at the time of the event
    bool down = false;

    // Gamepad hot-plug.  gamepadId is the stable instance id, not an
    // enumeration index, so it can be passed straight to OpenGamepad().
    // deviceIndex is the enumeration index and is only valid for GamepadAdded;
    // SDL2 opening APIs take an index while removal events carry an id.
    GamepadId gamepadId = 0;
    int deviceIndex = -1;

    // WindowResized.
    int width = 0;
    int height = 0;
};

void BeginInputFrame();
bool PollEvent(Event* event);

/*
 * Text input on/off.
 *
 * SDL2 and SDL3 disagree here: SDL2 delivers SDL_TEXTINPUT once the video subsystem is up, while
 * SDL3 has text input disabled by default and only sends the event after SDL_StartTextInput().
 * The ImGui layer drives this from io.WantTextInput, so a focused text field receives characters
 * on both backends and a running game sees none of them.
 */
void SetTextInputActive(Window* window, bool active);

/*
 * Gamepads.
 *
 * Hot-plug is safe because every event and query uses the stable id above:
 * an SDL2 removal event carries an instance id and an addition event carries
 * an index, and the backend normalises both.  The consumer never has to know
 * which one it received.
 */
constexpr unsigned kMaxGamepadAxes = 8;
constexpr unsigned kMaxGamepadButtons = 32;
constexpr unsigned kMaxGamepadHats = 4;

enum class PadAxis : uint8_t
{
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count
};

constexpr unsigned kPadAxisCount = static_cast<unsigned>(PadAxis::Count);

/*
 * The standard gamepad layout, named by position rather than by the label
 * printed on any one vendor's pad.  This is the same normalisation SDL's
 * GameController/Gamepad API performs; keeping our own enum means shared
 * code never includes SDL and the SDL2 and SDL3 mappings cannot drift apart
 * silently.
 */
enum class PadButton : uint8_t
{
    South = 0,      // A / Cross
    East,           // B / Circle
    West,           // X / Square
    North,          // Y / Triangle
    Back,           // Back / Share / Select
    Guide,          // Xbox / PS button
    Start,          // Start / Options
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Misc1,          // Share / Capture, where the mapping distinguishes it
    Paddle1,
    Paddle2,
    Paddle3,
    Paddle4,
    Touchpad,
    Count
};

constexpr unsigned kPadButtonCount = static_cast<unsigned>(PadButton::Count);

constexpr uint32_t PadButtonBit(PadButton button)
{
    return 1u << static_cast<unsigned>(button);
}

struct GamepadInfo
{
    GamepadId id = kInvalidGamepad;
    int index = -1;                 // enumeration order, for the settings window
    char name[64] = {};
    uint16_t vendor = 0;
    uint16_t product = 0;
    uint8_t numAxes = 0;
    uint8_t numButtons = 0;
    uint8_t numHats = 0;

    // True when SDL has a mapping for the device and the standard axes and
    // buttons below are meaningful.  False means it is a raw joystick and
    // only axes[]/buttons[] carry data.
    bool isGamepad = false;

    bool hasRumble = false;
    bool hasTriggerRumble = false;
    bool hasHaptic = false;
};

struct GamepadState
{
    GamepadId id = kInvalidGamepad;
    bool connected = false;

    // Raw device state.  Axis values are normalised to [-1, 1]; triggers are
    // [0, 1] where the backend can identify them.
    float axes[kMaxGamepadAxes] = {};
    uint32_t buttons = 0;
    int16_t hats[kMaxGamepadHats] = {};   // -1 centred, else SDL POV angle (0, 90, 180, 270)

    // Standard layout, valid when GamepadInfo::isGamepad is true.
    float standardAxes[kPadAxisCount] = {};
    uint32_t standardButtons = 0;         // PadButtonBit() mask
};

int GetGamepadCount();
bool GetGamepadInfo(int index, GamepadInfo* info);
bool GetGamepadInfoById(GamepadId id, GamepadInfo* info);

bool OpenGamepad(GamepadId id);
void CloseGamepad(GamepadId id);
bool IsGamepadOpen(GamepadId id);
bool GetGamepadState(GamepadId id, GamepadState* state);

/*
 * Rumble only. Effect-based force feedback (constant force, spring, friction) lives in the haptic
 * calls below: on SDL3 rumble and haptics are separate subsystems, and a force-feedback wheel has
 * no rumble motors at all.
 */
bool SetGamepadRumble(GamepadId id, float lowFrequency, float highFrequency, uint32_t durationMs);

/*
 * Effect-based haptics.
 *
 * Rumble drives a gamepad's vibration motors; constant force, spring and friction are the
 * DirectInput effects a wheel or joystick uses. SDL3 exposes them through SDL_Haptic, opened from
 * the joystick handle that the gamepad API already owns. The backend owns the haptic handle and
 * the effect ids; callers name the effect and a strength in [-1, 1].
 *
 * Strength is signed for Constant (the two directions of torque) and a magnitude for Spring and
 * Friction. The implementation smooths level changes with the effect's own attack envelope, so
 * callers may update at the game's 57 Hz rate without producing an audible or tactile stair-step.
 */
enum class HapticEffect : uint8_t
{
    Constant = 0,
    Spring,
    Friction
};

bool OpenHaptics(GamepadId id);
void CloseHaptics(GamepadId id);
bool HapticsAvailable(GamepadId id);

bool SetHapticEffect(GamepadId id, HapticEffect effect, float strength);
void StopHapticEffect(GamepadId id, HapticEffect effect);
void StopAllHapticEffects(GamepadId id);

} // Platform
} // OSD

#endif // INCLUDED_PLATFORMINPUT_H
