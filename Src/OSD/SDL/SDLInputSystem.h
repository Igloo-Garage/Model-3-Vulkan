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
#include "SDLIncludes.h"

#include <vector>

#define NUM_SDL_KEYS (sizeof(s_keyMap) / sizeof(SDLKeyMapStruct))

struct SDLKeyMapStruct
{
	const char *keyName;
	SDL_Scancode sdlKey;
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

	// Vector to keep track of attached joysticks
	std::vector<SDL_Joystick*> m_joysticks;

	// Vector to keep track of attached gamepads
	std::vector<SDL_GameController*> m_gamepads;

	// Vector of joystick details
	std::vector<JoyDetails> m_joyDetails;

	// SDL instance id of each open device, index-aligned with the vectors above.
	//
	// Device indices are only meaningful at enumeration time and are renumbered when anything is
	// unplugged, whereas instance ids are stable for the life of a device - which is what
	// SDL_JOYDEVICEREMOVED reports. Without this the removal event could only be matched by
	// position, and a pad pulled out of the middle would close the wrong one.
	std::vector<SDL_JoystickID> m_instanceIds;

	// Current key state obtained from SDL
	const Uint8 *m_keyState;

	// Current mouse state obtained from SDL
	int m_mouseX;
	int m_mouseY;
	int m_mouseZ;
	short m_mouseWheelDir;
	Uint8 m_mouseButtons;

	// SDL2 ffb
	SDL_HapticEffect eff;
	unsigned sdlConstForceMax;
	unsigned sdlSelfCenterMax;
	unsigned sdlFrictionMax;
	unsigned sdlVibrateMax;
	struct hapticInfo
	{
		SDL_Haptic* SDLhaptic = NULL;
		int effectConstantForceID = -1;
		int effectVibrationID = -1;
		int effectSpringForceID = -1;
		int effectFrictionForceID = -1;
	};
	std::vector<hapticInfo> m_SDLHapticDatas;

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
	 * Closes the device with the given SDL instance id, if it is open. Returns whether anything
	 * was closed.
	 */
	bool CloseJoystickAt(SDL_JoystickID instanceId);

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

	bool HasBasicForce(SDL_Haptic* hap);

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

	void SetMouseVisibility(bool visible);
};

#endif	// INCLUDED_SDLINPUTSYSTEM_H
