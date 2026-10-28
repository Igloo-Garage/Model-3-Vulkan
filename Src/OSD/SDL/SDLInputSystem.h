/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011 Bart Trzynadlowski, Nik Henson
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
 * SDLInputSystem.h
 *
 * Header file for SDL input system.
 */

#ifndef INCLUDED_SDLINPUTSYSTEM_H
#define INCLUDED_SDLINPUTSYSTEM_H

#include "Types.h"
#include "Inputs/InputSource.h"
#include "Inputs/InputSystem.h"
#include "OSD/Platform/PlatformInput.h"

#if defined(SUPERMODEL_PLATFORM_SDL2)
// The SDL2 path keeps raw joystick/controller and SDL_Haptic handles. The SDL3 path reaches the
// same information through PlatformInput.h, so this header stays SDL-free for that build.
#include "SDLIncludes.h"
#endif

#include <cstdint>
#include <vector>

#define NUM_SDL_KEYS (sizeof(s_keyMap) / sizeof(SDLKeyMapStruct))

/*
 * Key-name to scancode mapping.
 *
 * The scancode type is the platform layer's, not SDL's: SDL2 and SDL3 both number
 * scancodes as USB HID usage ids (page 0x07), so one table serves both backends and the
 * header stays free of SDL types.
 */
struct SDLKeyMapStruct
{
	const char *keyName;
	OSD::Platform::Scancode sdlKey;
};

/*
 * Input system that uses SDL.
 */
class CSDLInputSystem : public CInputSystem
{
private:
	const Util::Config::Node& m_config;

	// use gamecontroller api
	bool m_useGameController;

	// Lookup table to map key names to SDLKeys
	static SDLKeyMapStruct s_keyMap[];

#if defined(SUPERMODEL_PLATFORM_SDL2)
	// Vector to keep track of attached joysticks
	std::vector<SDL_Joystick*> m_joysticks;

	// Vector to keep track of attached gamepads
	std::vector<SDL_GameController*> m_gamepads;
#else
	// SDL3 hands out opaque, reference-counted gamepad handles by id; the platform layer owns them,
	// so this vector holds the ids whose state the input system reads.
	std::vector<OSD::Platform::GamepadId> m_gamepadIds;

	/*
	 * One state snapshot per open device, refreshed once per frame by Poll().
	 *
	 * Platform::GetGamepadState() reads the device: every axis, every button, every hat. Calling it
	 * from IsJoyButPressed() and GetJoyAxisValue() meant one of those per binding, dozens per frame,
	 * all between the event drain and the emulated frame. A read is a cheap lookup into this vector.
	 */
	std::vector<OSD::Platform::GamepadState> m_gamepadStates;

	/*
	 * Pad vibration, held as a level rather than an edge.
	 *
	 * The drive board only sends a vibrate command when the value changes (see
	 * CWheelBoard::SendVibrate), which matches a motor that keeps spinning while the value is
	 * non-zero. SDL's gamepad rumble is a timed pulse, so a single command would buzz for 100 ms and
	 * stop while the game still expects the motor to be running. Poll() re-issues the pulse for as
	 * long as the stored value stays above zero.
	 */
	std::vector<float> m_padVibration;

	void ServicePadVibration();

	// Commands seen since the last summary, indexed by EForceFeedback. Logged every few seconds so a
	// "the pad does not rumble" report can be answered from the log: nothing here means the game
	// never asked, everything here means the request is leaving the emulator.
	unsigned m_ffbCounts[4] = {};
	void ReportForceFeedback();

	void RefreshGamepadStates();
#endif

	// Vector of joystick details
	std::vector<JoyDetails> m_joyDetails;

	// Stable device id of each open device, index-aligned with the vectors above.
	//
	// Enumeration indices are only meaningful at enumeration time and are renumbered when anything is
	// unplugged, whereas ids are stable for the life of a device - which is what a removal event
	// reports. Without this the removal event could only be matched by position, and a pad pulled out
	// of the middle would close the wrong one.
	std::vector<OSD::Platform::GamepadId> m_instanceIds;

	// Current key state obtained from the platform layer
	const uint8_t *m_keyState;

	    // Current mouse state obtained from the platform layer
	    int m_mouseX;
	    int m_mouseY;
	    // Same position, kept unrounded. The light-gun axes read this so that a slow, precise aim does
	    // not step from pixel to pixel; SDL3 reports float coordinates, SDL2 integers widened here.
	    float m_mouseXF;
	    float m_mouseYF;
	    int m_mouseZ;
	    short m_mouseWheelDir;
	    uint8_t m_mouseButtons;

	#if defined(SUPERMODEL_PLATFORM_SDL3)
	    // Per-device state, index-aligned with the mouse numbers the bindings name. Refreshed once per
	    // frame in Poll(); GetMouseAxisValue() and IsMouseButPressed() read it for MOUSE1_/MOUSE2_
	    // bindings, while ANY_MOUSE keeps the aggregate above.
	    std::vector<OSD::Platform::MouseState> m_mouseStates;
	    MouseDetails m_mouseDetails;
	#endif

	#if defined(SUPERMODEL_PLATFORM_SDL2)
	// SDL2 haptics: the only force-feedback API in the tree that can express constant force, spring
	// and friction, which is why these effects live in the SDL2 binary. The SDL3 target uses rumble
	// only (see PlatformInput.h).
	SDL_HapticEffect eff;
	#endif
	// Strength percentages. Shared by both backends: the SDL3 path scales its haptic effects with
	// the same ini keys, so an existing configuration keeps its tuning.
	unsigned sdlConstForceMax;
	unsigned sdlSelfCenterMax;
	unsigned sdlFrictionMax;
	unsigned sdlVibrateMax;

	#if defined(SUPERMODEL_PLATFORM_SDL2)
	struct hapticInfo
	{
		SDL_Haptic* SDLhaptic = NULL;
		int effectConstantForceID = -1;
		int effectVibrationID = -1;
		int effectSpringForceID = -1;
		int effectFrictionForceID = -1;
	};
	std::vector<hapticInfo> m_SDLHapticDatas;
	#endif

	/*
	 * Opens all attached joysticks.
	 */
	void OpenJoysticks();

	/*
	 * Opens one device by its SDL device index.
	 *
	 * Split out of OpenJoysticks() so that the enumeration at start-up and a pad plugged in while a
	 * game is running run exactly the same code - the hot-plug path differs only in which index it
	 * passes. Returns false when the device could not be opened.
	 */
	bool OpenJoystickAt(int deviceIndex);

	/*
	 * Closes the entry at the given index and removes it from every parallel vector.
	 *
	 * The vectors are kept index-aligned, so removing one device has to remove its slot from all of
	 * them or every later joystick number would refer to the wrong pad.
	 */
	void CloseJoystickEntry(int index);

	/*
	 * Closes the device with the given stable device id, if it is open. Returns whether anything
	 * was closed.
	 */
	bool CloseJoystickAt(OSD::Platform::GamepadId instanceId);

	/*
	 * Closes all attached joysticks.
	 */
	void CloseJoysticks();

	/*
	 * Set when a device appeared or disappeared in Poll().
	 *
	 * The count alone is not enough to decide this: one pad can be swapped for another between
	 * frames, which changes the device but not the number of devices.
	 */
	bool m_devicesChanged = false;

protected:
	/*
	 * Initializes the SDL input system.
	 */
	bool InitializeSystem();

	int GetKeyIndex(const char *keyName);

	const char *GetKeyName(int keyIndex);

	bool IsKeyPressed(int kbdNum, int keyIndex) const;

	int GetMouseAxisValue(int mseNum, int axisNum) const;

	float GetMouseAxisValueFloat(int mseNum, int axisNum) const;

	int GetMouseWheelDir(int mseNum) const;

	bool IsMouseButPressed(int mseNum, int butNum) const;

	int GetJoyAxisValue(int joyNum, int axisNum) const;

	bool IsJoyPOVInDir(int joyNum, int povNum, int povDir) const;

	bool IsJoyButPressed(int joyNum, int butNum) const;

	bool ProcessForceFeedbackCmd(int joyNum, int axisNum, ForceFeedbackCmd ffCmd);

	void StopAllEffect(int joyNum);

	void StopConstanteforce(int joyNum);

	void StopVibrationforce(int joyNum);

	void StopSpringforce(int joyNum);

	void StopFrictionforce(int joyNum);

	void VibrationEffect(float strength, int joyNum);

	void ConstantForceEffect(float force, int dir, int length, int joyNum);

	void SpringForceEffect(float force, int joyNum);

	void FrictionForceEffect(float force, int joyNum);

#if defined(SUPERMODEL_PLATFORM_SDL2)
	bool HasBasicForce(SDL_Haptic* hap);
#endif

public:
	/*
	 * Constructs an SDL input system.
	 */
	CSDLInputSystem(const Util::Config::Node& config, bool useGameController);

	~CSDLInputSystem();

	int GetNumKeyboards() const;

	int GetNumMice() const;

	int GetNumJoysticks() const;

	bool ConsumeDevicesChanged();

	/*
	 * Whether joyNum refers to a joystick that is currently open.
	 *
	 * Every reader below has to check this rather than assume the bindings are still satisfiable:
	 * a pad can be unplugged while a game is running, and the bindings that named it are not
	 * rewritten until the next attach.
	 */
	bool JoyNumValid(int joyNum) const;

	const KeyDetails *GetKeyDetails(int kbdNum);

	const MouseDetails *GetMouseDetails(int mseNum);

	const JoyDetails *GetJoyDetails(int joyNum);

	bool Poll();

	void RefreshPointer();
	void RefreshState();

	// Copies the platform snapshot into the caches the bindings read; shared by Poll() and
	// RefreshState().
	void ReadSnapshot();

	void SetMouseVisibility(bool visible);
};

#endif	// INCLUDED_SDLINPUTSYSTEM_H
