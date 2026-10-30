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
 * SDLInputSystem.cpp
 *
 * Implementation of SDL input system.
 *
 * TODO:
 * -----
 * - Implement multiple keyboard and mouse support.
 */

#include "SDLInputSystem.h"

#include "Supermodel.h"
#include "Inputs/Input.h"
#include "OSD/Platform/PlatformInput.h"
// Only for the SDL_SCANCODE_* constants in the key table below. They number USB HID usage ids in
// both SDL versions, so the table itself is backend-neutral.
#include "SDLIncludes.h"

#include <vector>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
using namespace std;

/*
 * Optional light-gun probe, enabled with SM3D_GUN_PROBE=1.
 *
 * The crosshair is built from the late pointer sample, while the game is fed by the snapshot taken
 * at the simulation step. Those are two different code paths, and on Windows they can disagree
 * without anything looking wrong: the late sample asks the OS for the cursor, the per-device table
 * is maintained from raw-input deltas. This prints both, once a second, so the disagreement is a
 * number in the log instead of a guess made from a screenshot.
 */
static const bool s_logGunProbe = (std::getenv("SM3D_GUN_PROBE") != nullptr);

static bool GunProbeDue()
{
  static OSD::Platform::Nanoseconds last = 0;
  const OSD::Platform::Nanoseconds now = OSD::Platform::Now();
  if (last != 0 && now - last < 1000000000)
    return false;
  last = now;
  return true;
}

#if defined(SUPERMODEL_PLATFORM_SDL3)
//
// How long a pad rumble pulse lasts. The value is re-issued once per frame for as long as the drive
// board keeps the vibration level above zero (see ServicePadVibration), so this only has to outlast
// the longest frame gap - a stalled frame must not let the motor stop and restart.
static constexpr unsigned kVibrationPulseMs = 500;

/*
 * Optional input probe, enabled with SM3D_PAD_PROBE=1.
 *
 * Prints every button, hat and significant axis change with the pad index and, in game-controller
 * mode, the normalised name SDL mapped the press to. "The pad is connected but the game does not
 * react" is otherwise a guess: this shows whether the device reports the press at all and whether
 * it reached the standard layout the "standard" preset is written against. Axes are reported in
 * 0.25 steps so a sweeping stick cannot fill the log faster than it can be read.
 *
 * Each pad is also rumbled once when it is opened, so the same run answers "which pad is this line"
 * and "do its motors work" without waiting for a game to drive them.
 */
static const bool s_logPadInput = (std::getenv("SM3D_PAD_PROBE") != nullptr);
#endif

SDLKeyMapStruct CSDLInputSystem::s_keyMap[] =
{
  // General keys
  { "BACKSPACE",      SDL_SCANCODE_BACKSPACE },
  { "TAB",            SDL_SCANCODE_TAB },
  { "CLEAR",          SDL_SCANCODE_CLEAR },
  { "RETURN",         SDL_SCANCODE_RETURN },
  { "PAUSE",          SDL_SCANCODE_PAUSE },
  { "ESCAPE",         SDL_SCANCODE_ESCAPE },
  { "SPACE",          SDL_SCANCODE_SPACE },
  { "QUOTE",          SDL_SCANCODE_APOSTROPHE },
  { "LEFTPAREN",      SDL_SCANCODE_KP_LEFTPAREN },
  { "RIGHTPAREN",     SDL_SCANCODE_KP_RIGHTPAREN },
  { "COMMA",          SDL_SCANCODE_COMMA },
  { "MINUS",          SDL_SCANCODE_MINUS },
  { "PERIOD",         SDL_SCANCODE_PERIOD },
  { "SLASH",          SDL_SCANCODE_SLASH },
  { "0",              SDL_SCANCODE_0 },
  { "1",              SDL_SCANCODE_1 },
  { "2",              SDL_SCANCODE_2 },
  { "3",              SDL_SCANCODE_3 },
  { "4",              SDL_SCANCODE_4 },
  { "5",              SDL_SCANCODE_5 },
  { "6",              SDL_SCANCODE_6 },
  { "7",              SDL_SCANCODE_7 },
  { "8",              SDL_SCANCODE_8 },
  { "9",              SDL_SCANCODE_9 },
  { "SEMICOLON",      SDL_SCANCODE_SEMICOLON },
  { "EQUALS",         SDL_SCANCODE_EQUALS },
  { "LEFTBRACKET",    SDL_SCANCODE_LEFTBRACKET },
  { "BACKSLASH",      SDL_SCANCODE_BACKSLASH },
  { "RIGHTBRACKET",   SDL_SCANCODE_RIGHTBRACKET },
  { "BACKQUOTE",      SDL_SCANCODE_GRAVE },
  { "A",              SDL_SCANCODE_A },
  { "B",              SDL_SCANCODE_B },
  { "C",              SDL_SCANCODE_C },
  { "D",              SDL_SCANCODE_D },
  { "E",              SDL_SCANCODE_E },
  { "F",              SDL_SCANCODE_F },
  { "G",              SDL_SCANCODE_G },
  { "H",              SDL_SCANCODE_H },
  { "I",              SDL_SCANCODE_I },
  { "J",              SDL_SCANCODE_J },
  { "K",              SDL_SCANCODE_K },
  { "L",              SDL_SCANCODE_L },
  { "M",              SDL_SCANCODE_M },
  { "N",              SDL_SCANCODE_N },
  { "O",              SDL_SCANCODE_O },
  { "P",              SDL_SCANCODE_P },
  { "Q",              SDL_SCANCODE_Q },
  { "R",              SDL_SCANCODE_R },
  { "S",              SDL_SCANCODE_S },
  { "T",              SDL_SCANCODE_T },
  { "U",              SDL_SCANCODE_U },
  { "V",              SDL_SCANCODE_V },
  { "W",              SDL_SCANCODE_W },
  { "X",              SDL_SCANCODE_X },
  { "Y",              SDL_SCANCODE_Y },
  { "Z",              SDL_SCANCODE_Z },
  { "DEL",            SDL_SCANCODE_DELETE },

  // Keypad
  { "KEYPAD0",        SDL_SCANCODE_KP_0 },
  { "KEYPAD1",        SDL_SCANCODE_KP_1 },
  { "KEYPAD2",        SDL_SCANCODE_KP_2 },
  { "KEYPAD3",        SDL_SCANCODE_KP_3 },
  { "KEYPAD4",        SDL_SCANCODE_KP_4 },
  { "KEYPAD5",        SDL_SCANCODE_KP_5 },
  { "KEYPAD6",        SDL_SCANCODE_KP_6 },
  { "KEYPAD7",        SDL_SCANCODE_KP_7 },
  { "KEYPAD8",        SDL_SCANCODE_KP_8 },
  { "KEYPAD9",        SDL_SCANCODE_KP_9 },
  { "KEYPADPERIOD",   SDL_SCANCODE_KP_PERIOD },
  { "KEYPADDIVIDE",   SDL_SCANCODE_KP_DIVIDE },
  { "KEYPADMULTIPLY", SDL_SCANCODE_KP_MULTIPLY },
  { "KEYPADMINUS",    SDL_SCANCODE_KP_MINUS },
  { "KEYPADPLUS",     SDL_SCANCODE_KP_PLUS },
  { "KEYPADENTER",    SDL_SCANCODE_KP_ENTER },
  { "KEYPADEQUALS",   SDL_SCANCODE_KP_EQUALS },

  // Arrows + Home/End Pad
  { "UP",             SDL_SCANCODE_UP },
  { "DOWN",           SDL_SCANCODE_DOWN },
  { "RIGHT",          SDL_SCANCODE_RIGHT },
  { "LEFT",           SDL_SCANCODE_LEFT },
  { "INSERT",         SDL_SCANCODE_INSERT },
  { "HOME",           SDL_SCANCODE_HOME },
  { "END",            SDL_SCANCODE_END },
  { "PGUP",           SDL_SCANCODE_PAGEUP },
  { "PGDN",           SDL_SCANCODE_PAGEDOWN },

  // Function Key
  { "F1",             SDL_SCANCODE_F1 },
  { "F2",             SDL_SCANCODE_F2 },
  { "F3",             SDL_SCANCODE_F3 },
  { "F4",             SDL_SCANCODE_F4 },
  { "F5",             SDL_SCANCODE_F5 },
  { "F6",             SDL_SCANCODE_F6 },
  { "F7",             SDL_SCANCODE_F7 },
  { "F8",             SDL_SCANCODE_F8 },
  { "F9",             SDL_SCANCODE_F9 },
  { "F10",            SDL_SCANCODE_F10 },
  { "F11",            SDL_SCANCODE_F11 },
  { "F12",            SDL_SCANCODE_F12 },
  { "F13",            SDL_SCANCODE_F13 },
  { "F14",            SDL_SCANCODE_F14 },
  { "F15",            SDL_SCANCODE_F15 },

  // Modifier Keys
  // Removed Numlock, Capslock and Scrollock as don't seem to be handled well by SDL
  //{ "NUMLOCK",      SDLK_NUMLOCK },
  //{ "CAPSLOCK",     SDL_SCANCODE_CAPSLOCK },
  //{ "SCROLLLOCK",   SDLK_SCROLLOCK },
  { "RIGHTSHIFT",     SDL_SCANCODE_RSHIFT },
  { "LEFTSHIFT",      SDL_SCANCODE_LSHIFT },
  { "RIGHTCTRL",      SDL_SCANCODE_RCTRL },
  { "LEFTCTRL",       SDL_SCANCODE_LCTRL },
  { "RIGHTALT",       SDL_SCANCODE_RALT },
  { "LEFTALT",        SDL_SCANCODE_LALT },
  { "RIGHTMETA",      SDL_SCANCODE_RGUI },
  { "LEFTMETA",       SDL_SCANCODE_LGUI },
  { "ALTGR",          SDL_SCANCODE_MODE },

  // Other
  { "HELP",           SDL_SCANCODE_HELP },
  { "SYSREQ",         SDL_SCANCODE_SYSREQ },
  { "MENU",           SDL_SCANCODE_MENU },
  { "POWER",          SDL_SCANCODE_POWER },
  { "UNDO",           SDL_SCANCODE_UNDO }
};

CSDLInputSystem::CSDLInputSystem(const Util::Config::Node& config, bool useGameController)
  : CInputSystem("SDL"),
    m_config(config),
    m_useGameController(useGameController),
    m_keyState(nullptr),
    m_mouseX(0),
    m_mouseY(0),
    m_mouseXF(0.0f),
    m_mouseYF(0.0f),
    m_mouseZ(0),
    m_mouseWheelDir(0),
    m_mouseButtons(0)
#if defined(SUPERMODEL_PLATFORM_SDL2)
    , sdlConstForceMax(0),
    sdlSelfCenterMax(0),
    sdlFrictionMax(0),
    sdlVibrateMax(0)
#endif
{
#if defined(SUPERMODEL_PLATFORM_SDL2)
  memset(&eff, 0, sizeof(SDL_HapticEffect));
#endif
}

CSDLInputSystem::~CSDLInputSystem()
{
  CloseJoysticks();
}

#if defined(SUPERMODEL_PLATFORM_SDL2)

void CSDLInputSystem::OpenJoysticks()
{
  // Open all available joysticks
  int numJoys = SDL_NumJoysticks();

  //allow joystick to be fetch if windows has no focus.
  //this is usefull for multiple instance networked on the same machine
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");


  for (int joyNum = 0; joyNum < numJoys; joyNum++)
    OpenJoystickAt(joyNum);
}

bool CSDLInputSystem::OpenJoystickAt(int joyNum)
{
  int numHapticAxes = 0;
  int possibleEffect = 0;

  {
    SDL_GameController *gamepad = nullptr;
    SDL_Joystick *joystick = nullptr;

    // Already open? A hot-plug event can arrive for a device that was enumerated at start-up,
    // because SDL emits DEVICEADDED for everything present when the joystick subsystem comes up.
    // Opening it twice would add a second entry for the same pad, and every later number would
    // then refer to the wrong device.
    const SDL_JoystickID instanceId = SDL_JoystickGetDeviceInstanceID(joyNum);
    for (size_t i = 0; i < m_instanceIds.size(); i++)
    {
      if (m_instanceIds[i] == instanceId)
        return false;
    }

    if (m_useGameController)
    {
	// SDL_GameControllerOpen() returns NULL for a device the database has no mapping for, and
	// SDL_GameControllerGetJoystick(NULL) dereferences it. Check before asking for the handle -
	// without this, plugging in a pad SDL does not know crashed the emulator on the spot.
	gamepad = SDL_GameControllerOpen(joyNum);
	if (gamepad == nullptr)
	{
	  InfoLog("Joystick %d is not a recognised game controller; skipping it. Per-device bindings "
	          "need an input system that reports raw buttons.\n", joyNum + 1);
	  return false;
	}
	joystick = SDL_GameControllerGetJoystick(gamepad);
    } else
    {
	joystick = SDL_JoystickOpen(joyNum);
    }

    if (joystick == nullptr)
    {
      ErrorLog("Unable to open joystick device %d with SDL - skipping joystick.\n", joyNum + 1);
      return false;
    }

    // Gather joystick details (name, num POVs & buttons and which axes are available)
    JoyDetails joyDetails;
    hapticInfo hapticDatas;

    if (m_useGameController)
    {
      const char *pName = SDL_GameControllerName(gamepad);
      strncpy(joyDetails.name, pName, MAX_NAME_LENGTH);
      joyDetails.name[MAX_NAME_LENGTH] = '\0';
      joyDetails.numAxes = 6;
      joyDetails.numPOVs = 4;
      joyDetails.numButtons = 16;
    } else
    {
      const char *pName = SDL_JoystickName(joystick);
      strncpy(joyDetails.name, pName, MAX_NAME_LENGTH);
      joyDetails.name[MAX_NAME_LENGTH] = '\0';
      joyDetails.numAxes = SDL_JoystickNumAxes(joystick);
      joyDetails.numPOVs = SDL_JoystickNumHats(joystick);
      joyDetails.numButtons = SDL_JoystickNumButtons(joystick);
    }

    if (SDL_JoystickIsHaptic(joystick))
        joyDetails.hasFFeedback = true;
    else
        joyDetails.hasFFeedback = false;

    if (joyDetails.hasFFeedback)
    {
      hapticDatas.SDLhaptic = SDL_HapticOpenFromJoystick(joystick);

      if (hapticDatas.SDLhaptic == NULL)
      {
        ErrorLog("Unable to obtain haptic interface for joystick %s. Force feedback will be disabled for this joystick.", joyDetails.name);
        joyDetails.hasFFeedback = false;
        numHapticAxes = 0;
      }
      else
      {
        numHapticAxes = SDL_HapticNumAxes(hapticDatas.SDLhaptic);

        // depending device, SDL_HapticNumAxes return wrong number of ffb axes (bug on device driver on windows or other ?)
        // ie : saitek cyborg evo force joystick returns 3 ffb axes instead of 2 ffb axes, this leads to unable to create effects on this device
        // generally, none of commercial ffb products have more than 2 ffb axes
        // in this case, we need to force the correct number of ffb axes by adding manually a new sdl2 function
        // see https://forums.libsdl.org/viewtopic.php?t=5195
        // enabled this code if you have a saitek cyborg evo force or if you find another device that return wrong number of ffb axis
        // don't forget to edit sdl2 source code in Supermodel project
//#define HAPTIC_MOD
#if (defined _WIN32 && defined HAPTIC_MOD)
        if (numHapticAxes > 2)
        {
          DebugLog("Joystick : %s return more than 2 ffb axes, need sdl2 addon SDL_HapticSetAxes() to bypass\n", SDL_HapticName(joyNum));
          if (strcmp(joyDetails.name, "Saitek Cyborg Evo Force") == 0) // remove if any other particular case
            SDL_HapticSetAxes(hapticDatas.SDLhaptic, 2);   // /!\ function manually added to SDL2 project
        }
#endif


        // sdl2 bug on linux only ?
        // SDL_HapticNumAxes() : on win it reports good number of haptic axe, on linux 18.04 it reports always 2 haptic axes (bad) whatever the device
        // file : SDL2-2.0.10\src\haptic\linux\SDL_syshaptic.c
        // function : static int SDL_SYS_HapticOpenFromFD(SDL_Haptic * haptic, int fd)
        // haptic->naxes = 2;          // Hardcoded for now, not sure if it's possible to find out. <- note from the sdl2 devs
#ifndef _WIN32
        if (!HasBasicForce(hapticDatas.SDLhaptic)) numHapticAxes = 0;
#endif
        DebugLog("joy num %d haptic num axe %d name : %s\n", joyNum, numHapticAxes, SDL_HapticName(joyNum));
      }
    }

    for (int axisNum = 0; axisNum < NUM_JOY_AXES; axisNum++)
    {
      joyDetails.hasAxis[axisNum] = joyDetails.numAxes > axisNum;
      if (numHapticAxes > 0 && joyDetails.hasAxis[axisNum]) // unable to know which axes have ffb
        joyDetails.axisHasFF[axisNum] = true;
      else
        joyDetails.axisHasFF[axisNum] = false;

      char *axisName = joyDetails.axisName[axisNum];
      strcpy(axisName, CInputSystem::GetDefaultAxisName(axisNum));
    }

    if (joyDetails.hasFFeedback && hapticDatas.SDLhaptic != NULL && numHapticAxes > 0) // not a pad but wheel or joystick
    {
      SDL_HapticSetAutocenter(hapticDatas.SDLhaptic, 0);
      SDL_HapticSetGain(hapticDatas.SDLhaptic, 100);

      possibleEffect = SDL_HapticQuery(hapticDatas.SDLhaptic);

      // constant effect
      if (possibleEffect & SDL_HAPTIC_CONSTANT)
      {
        memset(&eff, 0, sizeof(SDL_HapticEffect));
        eff.type = SDL_HAPTIC_CONSTANT;
        eff.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        eff.constant.direction.dir[0] = 0;
        eff.constant.length = 30;
        eff.constant.delay = 0;
        eff.constant.level = 0;
        hapticDatas.effectConstantForceID = SDL_HapticNewEffect(hapticDatas.SDLhaptic, &eff);;
        if (hapticDatas.effectConstantForceID < 0)
          ErrorLog("Unable to create constant force effect for joystick %s (joy id=%d). Constant force will not be applied.", joyDetails.name, joyNum + 1);
      }

      // vibration effect
      if (possibleEffect & SDL_HAPTIC_SINE)
      {
        memset(&eff, 0, sizeof(SDL_HapticEffect));
        eff.type = SDL_HAPTIC_SINE;
        eff.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        eff.constant.length = 500;
        eff.constant.delay = 0;
        eff.periodic.period = 50;
        eff.periodic.magnitude = 0;
        hapticDatas.effectVibrationID = SDL_HapticNewEffect(hapticDatas.SDLhaptic, &eff);
        if (hapticDatas.effectVibrationID < 0)
          ErrorLog("Unable to create vibration effect for joystick %s (joy id=%d). Vibration will not be applied.", joyDetails.name, joyNum + 1);
      }

      // spring effect
      if (possibleEffect & SDL_HAPTIC_SPRING)
      {
        memset(&eff, 0, sizeof(SDL_HapticEffect));
        eff.type = SDL_HAPTIC_SPRING;
        eff.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        eff.condition.delay = 0;
        eff.condition.length = SDL_HAPTIC_INFINITY;
        eff.condition.left_sat[0] = 0xFFFF;
        eff.condition.right_sat[0] = 0xFFFF;
        eff.condition.left_coeff[0] = 0;
        eff.condition.right_coeff[0] = 0;
        hapticDatas.effectSpringForceID = SDL_HapticNewEffect(hapticDatas.SDLhaptic, &eff);
        if (hapticDatas.effectSpringForceID < 0)
          ErrorLog("Unable to create spring force effect for joystick %s (joy id=%d). Spring force will not be applied.", joyDetails.name, joyNum + 1);
      }

      // friction effect
      if (possibleEffect & SDL_HAPTIC_FRICTION)
      {
        memset(&eff, 0, sizeof(SDL_HapticEffect));
        eff.type = SDL_HAPTIC_FRICTION;
        eff.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        eff.condition.delay = 0;
        eff.condition.length = SDL_HAPTIC_INFINITY;
        eff.condition.left_sat[0] = 0xFFFF;
        eff.condition.right_sat[0] = 0xFFFF;
        eff.condition.left_coeff[0] = 0;
        eff.condition.right_coeff[0] = 0;
        hapticDatas.effectFrictionForceID = SDL_HapticNewEffect(hapticDatas.SDLhaptic, &eff);
        if (hapticDatas.effectFrictionForceID < 0)
          ErrorLog("Unable to create friction force effect for joystick %s (joy id=%d). Friction force will not be applied.", joyDetails.name, joyNum + 1);
      }
    }

    if (joyDetails.hasFFeedback && hapticDatas.SDLhaptic != NULL && numHapticAxes == 0) // pad with rumble. Note : SDL_HapticRumbleSupported() is not enough to detect rumble pad only because joystick or wheel may have also rumble
    {
      if (SDL_HapticRumbleInit(hapticDatas.SDLhaptic) < 0)
      {
        ErrorLog("Unable to create rumble effect for pad %s (pad id=%d). Rumble will not be applied.SDL_HapticRumbleInit failed : %s", joyDetails.name, joyNum + 1, SDL_GetError());
        joyDetails.axisHasFF[AXIS_X] = false;
        joyDetails.axisHasFF[AXIS_Y] = false;
      }
      else
      {
        joyDetails.axisHasFF[AXIS_X] = true; // Force feedback simulated on X axis sticks (fake)
        joyDetails.axisHasFF[AXIS_Y] = true; // Force feedback simulated on Y axis sticks (fake)
      }
    }

    if (m_useGameController)
        m_gamepads.push_back(gamepad);
    else
        m_joysticks.push_back(joystick);

    m_joyDetails.push_back(joyDetails);
    m_SDLHapticDatas.push_back(hapticDatas);
    m_instanceIds.push_back(SDL_JoystickInstanceID(joystick));
  }

  return true;
}

#else  // SDL3

/*
 * SDL3 device path.
 *
 * The platform layer owns the SDL gamepad handles; this class only keeps the ids and the binding
 * metadata. Everything the SDL2 code does with raw SDL calls - opening, counting axes, rumble -
 * is a Platform call here, which is what keeps the two backends from drifting apart.
 */
void CSDLInputSystem::OpenJoysticks()
{
  const int count = OSD::Platform::GetGamepadCount();
  InfoLog("Input: %d game controller(s) reported by SDL.", count);
  for (int joyNum = 0; joyNum < count; joyNum++)
    OpenJoystickAt(joyNum);
}

bool CSDLInputSystem::OpenJoystickAt(int joyNum)
{
  OSD::Platform::GamepadInfo info;
  if (!OSD::Platform::GetGamepadInfo(joyNum, &info) || info.id == OSD::Platform::kInvalidGamepad)
    return false;

  // Every device is opened, whether SDL classifies it as a game controller or not: a standalone
  // shifter or handbrake is a raw joystick, and skipping it made those devices impossible to bind in
  // the default "sdlgamepad" mode. Standard layout below is then chosen per device.

  // Already open? A hot-plug event can arrive for a device enumerated at start-up, and opening it
  // twice would add a second entry for the same pad.
  for (OSD::Platform::GamepadId open : m_instanceIds)
  {
    if (open == info.id)
      return false;
  }

  if (!OSD::Platform::OpenGamepad(info.id))
  {
    ErrorLog("Unable to open joystick device %d - skipping joystick.\n", joyNum + 1);
    return false;
  }

  JoyDetails joyDetails;
  strncpy(joyDetails.name, info.name, MAX_NAME_LENGTH);
  joyDetails.name[MAX_NAME_LENGTH] = '\0';

  // A controller is read through SDL's standard layout; anything else through its own raw axes,
  // hats and buttons. InfoLog below still reports the raw counts so the two cases stay diagnosable.
  const bool useStandard = m_useGameController && info.isGamepad;
  if (useStandard)
  {
    // Same figure the SDL2 game-controller path reports: the standard layout, not the raw device.
    // 0..16 are the standard buttons; anything above that is the device's own numbering, which some
    // pads expose in addition to the mapping.
    joyDetails.numAxes = 6;
    joyDetails.numPOVs = 4;
    joyDetails.numButtons = info.numButtons > 17 ? info.numButtons : 17;
  }
  else
  {
    joyDetails.numAxes = info.numAxes;
    joyDetails.numPOVs = info.numHats;
    joyDetails.numButtons = info.numButtons;
  }

  // SDL3 exposes rumble but not the effect-based haptics SDL2 has, so force feedback is limited to
  // what SetGamepadRumble() can express. The bindings still report the capability so that the
  // settings UI can say what will actually happen.
  joyDetails.hasFFeedback = info.isGamepad && info.hasRumble;

  for (int axisNum = 0; axisNum < NUM_JOY_AXES; axisNum++)
  {
    joyDetails.hasAxis[axisNum] = joyDetails.numAxes > axisNum;
    joyDetails.axisHasFF[axisNum] = joyDetails.hasFFeedback && (axisNum == AXIS_X || axisNum == AXIS_Y);
    strcpy(joyDetails.axisName[axisNum], CInputSystem::GetDefaultAxisName(axisNum));
  }

  m_joyDetails.push_back(joyDetails);
  m_instanceIds.push_back(info.id);
  m_gamepadIds.push_back(info.id);
  m_isGamepad.push_back(info.isGamepad);
  m_padVibration.push_back(0.0f);

  // One line per device, because "the pad does nothing" has two very different causes: SDL never saw
  // it (nothing here), or it was opened but the bindings do not match what the pad reports. The raw
  // capability figures are what makes the second case diagnosable - a pad with no X/Y is not a
  // driving wheel, and 16 buttons on the standard layout is the synthetic count, not the real one.
  InfoLog("Input: joystick %d '%s' - %u axes, %u buttons, %u hat(s), %s, rumble %s.",
    (int)m_joyDetails.size(), joyDetails.name, (unsigned)info.numAxes, (unsigned)info.numButtons,
    (unsigned)info.numHats, info.isGamepad ? "game controller" : "raw joystick",
    info.hasRumble ? "yes" : "no");

  // Seed the cached state without reporting it: the first diff would otherwise log every resting
  // hat as if it had just moved there.
  OSD::Platform::GamepadState initial;
  if (!OSD::Platform::GetGamepadState(info.id, &initial))
    initial = OSD::Platform::GamepadState();
  m_gamepadStates.resize(m_gamepadIds.size());
  m_gamepadStates.back() = initial;

  if (s_logPadInput && joyDetails.hasFFeedback)
  {
    OSD::Platform::SetGamepadRumble(info.id, 0.6f, 0.6f, 400);
    InfoLog("Input: pad %d rumble test fired (400 ms).", (int)m_joyDetails.size());
  }
  return true;
}

/*
 * Optional input probe. See the declaration at the top of the SDL3 block.
 */
static const char* PadButtonName(unsigned button)
{
  using OSD::Platform::PadButton;
  switch (static_cast<PadButton>(button))
  {
    case PadButton::South:         return "A/South";
    case PadButton::East:          return "B/East";
    case PadButton::West:          return "X/West";
    case PadButton::North:         return "Y/North";
    case PadButton::Back:          return "Back";
    case PadButton::Guide:         return "Guide";
    case PadButton::Start:         return "Start";
    case PadButton::LeftStick:     return "LStick";
    case PadButton::RightStick:    return "RStick";
    case PadButton::LeftShoulder:  return "LB";
    case PadButton::RightShoulder: return "RB";
    case PadButton::DPadUp:        return "Up";
    case PadButton::DPadDown:      return "Down";
    case PadButton::DPadLeft:      return "Left";
    case PadButton::DPadRight:     return "Right";
    case PadButton::Misc1:         return "Misc1";
    case PadButton::Paddle1:       return "Paddle1";
    case PadButton::Paddle2:       return "Paddle2";
    case PadButton::Paddle3:       return "Paddle3";
    case PadButton::Paddle4:       return "Paddle4";
    case PadButton::Touchpad:      return "Touchpad";
    default:                       return "?";
  }
}

static const char* PadAxisName(unsigned axis)
{
  using OSD::Platform::PadAxis;
  switch (static_cast<PadAxis>(axis))
  {
    case PadAxis::LeftX:        return "LX";
    case PadAxis::LeftY:        return "LY";
    case PadAxis::RightX:       return "RX";
    case PadAxis::RightY:       return "RY";
    case PadAxis::LeftTrigger:  return "LT";
    case PadAxis::RightTrigger: return "RT";
    default:                    return "?";
  }
}

static void LogPadChanges(int pad, bool standardLayout,
  const OSD::Platform::GamepadState& before, const OSD::Platform::GamepadState& after)
{
  if (!s_logPadInput)
    return;

  const auto rawChanged = before.buttons ^ after.buttons;
  if (rawChanged.any())
  {
    for (unsigned b = 0; b < OSD::Platform::kMaxGamepadButtons; b++)
    {
      if (rawChanged.test(b))
        InfoLog("Input: pad %d raw button %u %s.", pad + 1, b,
          after.buttons.test(b) ? "down" : "up");
    }
  }

  if (standardLayout)
  {
    const uint32_t changed = before.standardButtons ^ after.standardButtons;
    for (unsigned b = 0; b < OSD::Platform::kPadButtonCount; b++)
    {
      if (changed & (1u << b))
        InfoLog("Input: pad %d %s %s.", pad + 1, PadButtonName(b),
          (after.standardButtons & (1u << b)) ? "down" : "up");
    }

    for (unsigned a = 0; a < OSD::Platform::kPadAxisCount; a++)
    {
      if (std::fabs(after.standardAxes[a] - before.standardAxes[a]) >= 0.25f)
        InfoLog("Input: pad %d %s = %.2f.", pad + 1, PadAxisName(a), after.standardAxes[a]);
    }
  }
  else
  {
    for (unsigned a = 0; a < OSD::Platform::kMaxGamepadAxes; a++)
    {
      if (std::fabs(after.axes[a] - before.axes[a]) >= 0.25f)
        InfoLog("Input: pad %d axis %u = %.2f.", pad + 1, a, after.axes[a]);
    }
  }

  for (unsigned h = 0; h < OSD::Platform::kMaxGamepadHats; h++)
  {
    if (before.hats[h] != after.hats[h])
      InfoLog("Input: pad %d hat %u = %d.", pad + 1, h, (int)after.hats[h]);
  }
}

/*
 * Reads every open device once.
 *
 * Platform::GetGamepadState() is not a cached getter: it walks the device's axes, buttons and hats.
 * Doing that per binding meant dozens of device reads per frame, so this is the one place that
 * touches the devices and everything else reads m_gamepadStates.
 */
void CSDLInputSystem::RefreshGamepadStates()
{
  m_gamepadStates.resize(m_gamepadIds.size());
  for (size_t i = 0; i < m_gamepadIds.size(); i++)
  {
    OSD::Platform::GamepadState next;
    if (!OSD::Platform::GetGamepadState(m_gamepadIds[i], &next))
      next = OSD::Platform::GamepadState();
    LogPadChanges((int)i, m_useGameController && i < m_isGamepad.size() && m_isGamepad[i],
      m_gamepadStates[i], next);
    m_gamepadStates[i] = next;
  }
}

#endif  // SUPERMODEL_PLATFORM_SDL2

/*
 * Closes the entry at 'index', removing it from every parallel vector.
 *
 * Written as erase() on each vector rather than clear(): the old CloseJoysticks() called clear()
 * inside the loop, which emptied the vectors on the first iteration and then read past the end on
 * the second. That went unnoticed because it only ran at shutdown, where a crash is easy to mistake
 * for a crash during teardown - but the same code now runs whenever a pad is unplugged mid-game.
 */
void CSDLInputSystem::CloseJoystickEntry(int index)
{
  if (index < 0 || index >= (int)m_joyDetails.size())
    return;

#if defined(SUPERMODEL_PLATFORM_SDL2)
  const JoyDetails& joyDetails = m_joyDetails[index];
  if (joyDetails.hasFFeedback && m_SDLHapticDatas[index].SDLhaptic != nullptr)
  {
    hapticInfo& haptic = m_SDLHapticDatas[index];
    if (haptic.effectConstantForceID >= 0)
      SDL_HapticDestroyEffect(haptic.SDLhaptic, haptic.effectConstantForceID);
    if (haptic.effectVibrationID >= 0)
      SDL_HapticDestroyEffect(haptic.SDLhaptic, haptic.effectVibrationID);
    if (haptic.effectSpringForceID >= 0)
      SDL_HapticDestroyEffect(haptic.SDLhaptic, haptic.effectSpringForceID);
    if (haptic.effectFrictionForceID >= 0)
      SDL_HapticDestroyEffect(haptic.SDLhaptic, haptic.effectFrictionForceID);

    SDL_HapticClose(haptic.SDLhaptic);
    haptic.SDLhaptic = nullptr;
  }

  if (m_useGameController)
  {
    if (m_gamepads[index] != nullptr)
      SDL_GameControllerClose(m_gamepads[index]);
    m_gamepads.erase(m_gamepads.begin() + index);
  }
  else
  {
    if (m_joysticks[index] != nullptr)
      SDL_JoystickClose(m_joysticks[index]);
    m_joysticks.erase(m_joysticks.begin() + index);
  }

  m_joyDetails.erase(m_joyDetails.begin() + index);
  m_SDLHapticDatas.erase(m_SDLHapticDatas.begin() + index);
  m_instanceIds.erase(m_instanceIds.begin() + index);

#else

  OSD::Platform::CloseGamepad(m_gamepadIds[index]);
  m_gamepadIds.erase(m_gamepadIds.begin() + index);
  if (index < (int)m_isGamepad.size())
    m_isGamepad.erase(m_isGamepad.begin() + index);
  if (index < (int)m_padVibration.size())
    m_padVibration.erase(m_padVibration.begin() + index);
  if (index < (int)m_gamepadStates.size())
    m_gamepadStates.erase(m_gamepadStates.begin() + index);
  m_joyDetails.erase(m_joyDetails.begin() + index);
  m_instanceIds.erase(m_instanceIds.begin() + index);

#endif
}

bool CSDLInputSystem::CloseJoystickAt(OSD::Platform::GamepadId instanceId)
{
  for (int i = 0; i < (int)m_instanceIds.size(); i++)
  {
    if (m_instanceIds[i] != instanceId)
      continue;

    // Name before the entry disappears: it is what makes the log line useful.
    const char* name = m_joyDetails[i].name;
    InfoLog("Joystick removed: %s.\n", (name != nullptr && name[0] != '\0') ? name : "unknown device");
    CloseJoystickEntry(i);
    m_devicesChanged = true;
    return true;
  }
  return false;
}

void CSDLInputSystem::CloseJoysticks()
{
  // Backwards: each removal shifts the entries after it.
  for (int i = (int)m_joyDetails.size() - 1; i >= 0; i--)
    CloseJoystickEntry(i);
}

bool CSDLInputSystem::InitializeSystem()
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
  if (!OSD::Platform::Initialize(OSD::Platform::Subsystem_Gamepad))
  {
    ErrorLog("Unable to initialize the gamepad subsystem (%s).\n", OSD::Platform::GetError());
    return false;
  }

  // Haptics are optional: a machine with no force-feedback device must still be able to run, and
  // OpenHaptics() reports the absence per device when a game actually asks for an effect.
  if (!OSD::Platform::Initialize(OSD::Platform::Subsystem_Haptic))
    InfoLog("Force feedback is unavailable (%s).\n", OSD::Platform::GetError());
#else
  // Make sure joystick subsystem is initialized and joystick events are enabled
  if (m_useGameController)
  {
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0)
    {
        ErrorLog("Unable to initialize SDL joystick subsystem (%s).\n", SDL_GetError());
        return false;
    }
    SDL_GameControllerEventState(SDL_ENABLE);
  } else {
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) != 0)
    {
        ErrorLog("Unable to initialize SDL joystick subsystem (%s).\n", SDL_GetError());
        return false;
    }
    SDL_JoystickEventState(SDL_ENABLE);
  }
#endif

  // Open attached joysticks
  OpenJoysticks();
  return true;
}

int CSDLInputSystem::GetKeyIndex(const char *keyName)
{
  for (size_t i = 0; i < NUM_SDL_KEYS; i++)
  {
    if (stricmp(keyName, s_keyMap[i].keyName) == 0)
      return (int)i;
  }
  return -1;
}

const char *CSDLInputSystem::GetKeyName(int keyIndex)
{
  if (keyIndex < 0 || keyIndex >= (int)NUM_SDL_KEYS)
    return nullptr;
  return s_keyMap[keyIndex].keyName;
}

bool CSDLInputSystem::IsKeyPressed(int kbdNum, int keyIndex) const
{
  if (m_keyState == nullptr || keyIndex < 0 || keyIndex >= (int)NUM_SDL_KEYS)
    return false;

  OSD::Platform::Scancode sdlKey = s_keyMap[keyIndex].sdlKey;
  return m_keyState[sdlKey] != 0;
}

int CSDLInputSystem::GetMouseAxisValue(int mseNum, int axisNum) const
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
  // A concrete mouse number reads that device; ANY_MOUSE keeps using the system cursor below.
  if (mseNum >= 0)
  {
    if (mseNum >= (int)m_mouseStates.size())
      return 0;

    switch (axisNum)
    {
      case AXIS_X: return (int)m_mouseStates[mseNum].x;
      case AXIS_Y: return (int)m_mouseStates[mseNum].y;
      default:     return 0;
    }
  }
#endif

  // Return value for given mouse axis
  switch (axisNum)
  {
    case AXIS_X: return m_mouseX;
    case AXIS_Y: return m_mouseY;
    case AXIS_Z: return m_mouseZ;
    default:     return 0;
  }
}

float CSDLInputSystem::GetMouseAxisValueFloat(int mseNum, int axisNum) const
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
  if (mseNum >= 0)
  {
    if (mseNum >= (int)m_mouseStates.size())
      return 0.0f;

    switch (axisNum)
    {
      case AXIS_X: return m_mouseStates[mseNum].x;
      case AXIS_Y: return m_mouseStates[mseNum].y;
      default:     return 0.0f;
    }
  }
#endif

  // Sub-pixel variant, used only by the absolute (light-gun) axes. m_mouseXF/m_mouseYF hold the
  // unrounded position, so a slow aim moves in fractions of a pixel instead of whole ones.
  switch (axisNum)
  {
    case AXIS_X: return m_mouseXF;
    case AXIS_Y: return m_mouseYF;
    case AXIS_Z: return (float)m_mouseZ;
    default:     return 0.0f;
  }
}

void CSDLInputSystem::RefreshPointer()
{
  /*
   * Called immediately before the frame is emulated, after the in-game panel has been built and
   * the renderer has recorded its commands - the last moment at which a pointer position can
   * still reach this frame.
   *
   * Relative mode is skipped: there the pointer is being fed back by the application and the OS
   * cursor position is meaningless, so the accumulated deltas in the snapshot are the truth.
   */
  if (OSD::Platform::Input().relativeMouse)
    return;

#if defined(SUPERMODEL_PLATFORM_SDL3)
  // Drain packets from the raw thread first, then refresh the per-device snapshots below.
  OSD::Platform::RefreshPerDeviceMouse();
#endif

  float x = 0.0f;
  float y = 0.0f;
  if (!OSD::Platform::SamplePointerNow(&x, &y))
    return;

  	m_mouseXF = x;
  	m_mouseYF = y;
  	m_mouseX = static_cast<int>(x);
  	m_mouseY = static_cast<int>(y);

  #if defined(SUPERMODEL_PLATFORM_SDL3)
  	if (s_logGunProbe && GunProbeDue())
  	{
  		char device[64] = "(none)";
  		if (!m_mouseStates.empty())
  			snprintf(device, sizeof(device), "%.1f,%.1f", m_mouseStates[0].x, m_mouseStates[0].y);
  		InfoLog("GunProbe: late pointer %.1f,%.1f; per-device %s (%d); display %ux%u at %u,%u.",
  			x, y, device, (int)m_mouseStates.size(), m_dispW, m_dispH, m_dispX, m_dispY);
  	}
  #endif

#if defined(SUPERMODEL_PLATFORM_SDL3)
  // With one device the per-device table follows the cursor, so late sampling has to update it too
  // or MOUSE1_* bindings would read the position from the top of the frame. With several devices
  // the raw path has just updated them; copy the fresh values into the frame's snapshot.
  if (m_mouseStates.size() == 1)
  {
    m_mouseStates[0].x = x;
    m_mouseStates[0].y = y;
  }
  else
  {
    for (size_t i = 0; i < m_mouseStates.size(); i++)
      OSD::Platform::GetMouseStateByIndex((int)i, &m_mouseStates[i]);
  }
#endif
}

int CSDLInputSystem::GetMouseWheelDir(int mseNum) const
{
  // Return wheel value
  return m_mouseWheelDir;
}

bool CSDLInputSystem::IsMouseButPressed(int mseNum, int butNum) const
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
  if (mseNum >= 0)
  {
    if (mseNum >= (int)m_mouseStates.size())
      return false;

    const uint32_t mask = m_mouseStates[mseNum].buttons;
    switch (butNum)
    {
      case 0:  return !!(mask & OSD::Platform::MouseButton_Left);
      case 1:  return !!(mask & OSD::Platform::MouseButton_Middle);
      case 2:  return !!(mask & OSD::Platform::MouseButton_Right);
      case 3:  return !!(mask & OSD::Platform::MouseButton_X1);
      case 4:  return !!(mask & OSD::Platform::MouseButton_X2);
      default: return false;
    }
  }
#endif

  // Return value for given mouse button
  switch (butNum)
  {
    case 0:  return !!(m_mouseButtons & OSD::Platform::MouseButton_Left);
    case 1:  return !!(m_mouseButtons & OSD::Platform::MouseButton_Middle);
    case 2:  return !!(m_mouseButtons & OSD::Platform::MouseButton_Right);
    case 3:  return !!(m_mouseButtons & OSD::Platform::MouseButton_X1);
    case 4:  return !!(m_mouseButtons & OSD::Platform::MouseButton_X2);
    default: return false;
  }
}

/*
 * Bounds guard shared by the three joystick readers below.
 *
 * They used to index the vectors directly, which was safe only while the set of joysticks was
 * fixed at start-up. Hot-plug makes the out-of-range case reachable: unplug the pad and every
 * binding naming JOY1_* still holds a source that refers to joystick 0, which no longer exists.
 * The read then walks off the end of the vector and the crash surfaces inside SDL, where it looks
 * unrelated to input at all.
 */
bool CSDLInputSystem::JoyNumValid(int joyNum) const
{
  return joyNum >= 0 && joyNum < GetNumJoysticks();
}

int CSDLInputSystem::GetJoyAxisValue(int joyNum, int axisNum) const
{
  if (!JoyNumValid(joyNum))
    return 0;

#if defined(SUPERMODEL_PLATFORM_SDL3)
  // The platform layer normalises every axis to [-1, 1] and triggers to [0, 1]; the input system's
  // contract is SDL2's raw range, so scale back on the way out.
  if (joyNum >= (int)m_gamepadStates.size())
    return 0;
  const OSD::Platform::GamepadState& state = m_gamepadStates[joyNum];
  const bool useStandard =
    m_useGameController && joyNum >= 0 && joyNum < (int)m_isGamepad.size() && m_isGamepad[joyNum];

  if (useStandard)
  {
    switch (axisNum) {
      case AXIS_X:  return (int)(state.standardAxes[(unsigned)OSD::Platform::PadAxis::LeftX] * 32767.0f);
      case AXIS_Y:  return (int)(state.standardAxes[(unsigned)OSD::Platform::PadAxis::LeftY] * 32767.0f);
      case AXIS_Z:  return (int)(state.standardAxes[(unsigned)OSD::Platform::PadAxis::LeftTrigger] * 32767.0f);
      case AXIS_RX: return (int)(state.standardAxes[(unsigned)OSD::Platform::PadAxis::RightX] * 32767.0f);
      case AXIS_RY: return (int)(state.standardAxes[(unsigned)OSD::Platform::PadAxis::RightY] * 32767.0f);
      case AXIS_RZ: return (int)(state.standardAxes[(unsigned)OSD::Platform::PadAxis::RightTrigger] * 32767.0f);
      default:      return 0;
    }
  }

  if (axisNum >= (int)OSD::Platform::kMaxGamepadAxes)
    return 0;
  return (int)(state.axes[axisNum] * 32767.0f);
#else
  // Get raw joystick axis value for given joystick from SDL (values range from -32768 to 32767)
  if (m_useGameController)
  {
    SDL_GameController *gamepad = m_gamepads[joyNum];
    switch (axisNum) {
      case AXIS_X:  return SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTX);
      case AXIS_Y:  return SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTY);
      case AXIS_Z:  return SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
      case AXIS_RX: return SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_RIGHTX);
      case AXIS_RY: return SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_RIGHTY);
      case AXIS_RZ: return SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
      default:      return 0;
    }
  } else {
    SDL_Joystick *joystick = m_joysticks[joyNum];
    return SDL_JoystickGetAxis(joystick, axisNum);
  }
#endif
}

bool CSDLInputSystem::IsJoyPOVInDir(int joyNum, int povNum, int povDir) const
{
  if (!JoyNumValid(joyNum))
    return false;

#if defined(SUPERMODEL_PLATFORM_SDL3)
  if (joyNum >= (int)m_gamepadStates.size())
    return false;
  const OSD::Platform::GamepadState& state = m_gamepadStates[joyNum];
  const bool useStandard =
    m_useGameController && joyNum >= 0 && joyNum < (int)m_isGamepad.size() && m_isGamepad[joyNum];

  if (useStandard)
  {
    using OSD::Platform::PadButton;
    switch (povDir)
    {
      case POV_UP:    return !!(state.standardButtons & OSD::Platform::PadButtonBit(PadButton::DPadUp));
      case POV_DOWN:  return !!(state.standardButtons & OSD::Platform::PadButtonBit(PadButton::DPadDown));
      case POV_LEFT:  return !!(state.standardButtons & OSD::Platform::PadButtonBit(PadButton::DPadLeft));
      case POV_RIGHT: return !!(state.standardButtons & OSD::Platform::PadButtonBit(PadButton::DPadRight));
      default:        return false;
    }
  }

  if (povNum < 0 || povNum >= (int)OSD::Platform::kMaxGamepadHats)
    return false;

  // The backend reports the hat as an angle, so a diagonal counts for both of its directions -
  // which is what SDL's hat bitmask does on the SDL2 path.
  const int hat = state.hats[povNum];
  switch (povDir)
  {
    case POV_UP:    return hat == 0 || hat == 45 || hat == 315;
    case POV_DOWN:  return hat == 180 || hat == 135 || hat == 225;
    case POV_LEFT:  return hat == 270 || hat == 225 || hat == 315;
    case POV_RIGHT: return hat == 90 || hat == 45 || hat == 135;
    default:        return false;
  }
#else
  // Get current joystick POV-hat value for given joystick and POV number from SDL and check if pointing in required direction
  if (m_useGameController)
  {
    SDL_GameController *gamepad = m_gamepads[joyNum];
    switch (povDir)
    {
      case POV_UP:    return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_UP);
      case POV_DOWN:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
      case POV_LEFT:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
      case POV_RIGHT: return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
      default:        return false;
    }
    return false;
  } else {
    SDL_Joystick *joystick = m_joysticks[joyNum];
    int hatVal = SDL_JoystickGetHat(joystick, povNum);
    switch (povDir)
    {
      case POV_UP:    return !!(hatVal & SDL_HAT_UP);
      case POV_DOWN:  return !!(hatVal & SDL_HAT_DOWN);
      case POV_LEFT:  return !!(hatVal & SDL_HAT_LEFT);
      case POV_RIGHT: return !!(hatVal & SDL_HAT_RIGHT);
      default:        return false;
    }
    return false;
  }
#endif
}

bool CSDLInputSystem::IsJoyButPressed(int joyNum, int butNum) const
{
  if (!JoyNumValid(joyNum))
    return false;

#if defined(SUPERMODEL_PLATFORM_SDL3)
  if (joyNum >= (int)m_gamepadStates.size())
    return false;
  const OSD::Platform::GamepadState& state = m_gamepadStates[joyNum];
  const bool useStandard =
    m_useGameController && joyNum >= 0 && joyNum < (int)m_isGamepad.size() && m_isGamepad[joyNum];

  if (useStandard && butNum >= 0 && butNum <= 16)
  {
    /*
     * Same button order the SDL2 path exposes, so an existing JOY1_BUTTONn binding keeps meaning
     * the same physical button on a pad when the SDL2 binary is swapped for the SDL3 one. The order
     * is deliberately not PadButton's enum order.
     */
    using OSD::Platform::PadButton;
    PadButton button;
    switch (butNum)
    {
      case 0:  button = PadButton::South;          break;
      case 1:  button = PadButton::East;           break;
      case 2:  button = PadButton::West;           break;
      case 3:  button = PadButton::North;          break;
      case 4:  button = PadButton::LeftShoulder;   break;
      case 5:  button = PadButton::RightShoulder;  break;
      case 6:  button = PadButton::Back;           break;
      case 7:  button = PadButton::Start;          break;
      case 8:  button = PadButton::LeftStick;      break;
      case 9:  button = PadButton::RightStick;     break;
      case 10: button = PadButton::Paddle1;        break;
      case 11: button = PadButton::Paddle2;        break;
      case 12: button = PadButton::Guide;          break;
      case 13: button = PadButton::Paddle3;        break;
      case 14: button = PadButton::Paddle4;        break;
      case 15: button = PadButton::Misc1;          break;
      case 16: button = PadButton::Touchpad;       break;
      default: return false;
    }
    return !!(state.standardButtons & OSD::Platform::PadButtonBit(button));
  }

  if (butNum < 0 || butNum >= (int)OSD::Platform::kMaxGamepadButtons)
    return false;
  return state.buttons.test(butNum);
#else
  // Get current joystick button state for given joystick and button number from SDL
  if (m_useGameController)
  {
    SDL_GameController *gamepad = m_gamepads[joyNum];
    switch (butNum)
    {
      case 0:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A);
      case 1:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_B);
      case 2:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_X);
      case 3:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_Y);
      case 4:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
      case 5:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
      case 6:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_BACK);
      case 7:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_START);
      case 8:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_LEFTSTICK);
      case 9:  return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
      case 10: return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_PADDLE1);
      case 11: return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_PADDLE2);
      case 12: return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_GUIDE);
      case 13: return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_PADDLE3);
      case 14: return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_PADDLE4);
      case 15: return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_MISC1);
      case 16: return SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_TOUCHPAD);
    }
    return false;
  }
  else
  {
    SDL_Joystick *joystick = m_joysticks[joyNum];
    return !!SDL_JoystickGetButton(joystick, butNum);
  }
#endif
}

bool CSDLInputSystem::ProcessForceFeedbackCmd(int joyNum, int axisNum, ForceFeedbackCmd ffCmd)
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
  /*
   * SDL3 splits rumble and effect-based haptics into two subsystems. The wheel effects go through
   * PlatformInput's haptic API, which opens SDL_Haptic lazily on the first command; rumble stays
   * on the gamepad API. The scaling keys are the same ones the SDL2 path uses, so an existing ini
   * keeps its tuning.
   */
  if (!JoyNumValid(joyNum))
    return false;

  // Counted for the periodic summary: it is the only way to tell "the game never asks for force
  // feedback" from "the request is being sent and the device ignores it".
  if (ffCmd.id >= 0 && ffCmd.id < 4)
    ++m_ffbCounts[ffCmd.id];

  switch (ffCmd.id)
  {
    case FFStop:
      StopAllEffect(joyNum);
      return true;

    case FFConstantForce:
      sdlConstForceMax = m_config["SDLConstForceMax"].ValueAs<unsigned>();
      if (sdlConstForceMax == 0)
        return false;
      if (ffCmd.force == 0.0f)
        StopConstanteforce(joyNum);
      else if (ffCmd.force > 0.0f)
        ConstantForceEffect(ffCmd.force * (float)(sdlConstForceMax / 100.0f), -1, 0, joyNum);
      else
        ConstantForceEffect(-ffCmd.force * (float)(sdlConstForceMax / 100.0f), 1, 0, joyNum);
      return true;

    case FFSelfCenter:
      sdlSelfCenterMax = m_config["SDLSelfCenterMax"].ValueAs<unsigned>();
      if (sdlSelfCenterMax == 0)
        return false;
      SpringForceEffect(ffCmd.force * (float)(sdlSelfCenterMax / 100.0f), joyNum);
      return true;

    case FFFriction:
      sdlFrictionMax = m_config["SDLFrictionMax"].ValueAs<unsigned>();
      if (sdlFrictionMax == 0)
        return false;
      FrictionForceEffect(ffCmd.force * (float)(sdlFrictionMax / 100.0f), joyNum);
      return true;

    case FFVibrate:
      sdlVibrateMax = m_config["SDLVibrateMax"].ValueAs<unsigned>();
      if (sdlVibrateMax == 0)
        return false;
      VibrationEffect(ffCmd.force * (float)(sdlVibrateMax / 100.0f), joyNum);
      return true;

    default:
      return false;
  }
#else
  switch (ffCmd.id)
  {
    case FFStop:
      StopAllEffect(joyNum);
      break;

    case FFConstantForce:
      sdlConstForceMax = m_config["SDLConstForceMax"].ValueAs<unsigned>();
      if (sdlConstForceMax == 0)
        return false;
      // note sr2 centering val=0.047244 and val=0.062992 alternatively (const value)
      //          max val between -1 to 1 (left right)
      if (ffCmd.force == 0.0f)
        StopConstanteforce(joyNum);
      else if (ffCmd.force > 0.0f)
        ConstantForceEffect(ffCmd.force * (float)(sdlConstForceMax / 100.0f), -1, SDL_HAPTIC_INFINITY, joyNum);
      else if (ffCmd.force < 0.0f)
        ConstantForceEffect(-ffCmd.force * (float)(sdlConstForceMax / 100.0f), 1, SDL_HAPTIC_INFINITY, joyNum);
      break;

    case FFSelfCenter:
      sdlSelfCenterMax = m_config["SDLSelfCenterMax"].ValueAs<unsigned>();
      if (sdlSelfCenterMax == 0)
        return false;
      SpringForceEffect(ffCmd.force * (float)(sdlSelfCenterMax / 100.0f), joyNum);
      break;

    case FFFriction:
      sdlFrictionMax = m_config["SDLFrictionMax"].ValueAs<unsigned>();
      if (sdlFrictionMax == 0)
        return false;
      FrictionForceEffect(ffCmd.force * (float)(sdlFrictionMax / 100.0f), joyNum);
      break;

    case FFVibrate:
      sdlVibrateMax = m_config["SDLVibrateMax"].ValueAs<unsigned>();
      if (sdlVibrateMax == 0)
        return false;
      VibrationEffect(ffCmd.force * (float)(sdlVibrateMax / 100.0f), joyNum);
      break;
    }
    return true;
#endif
}

int CSDLInputSystem::GetNumKeyboards() const
{
  // Return ANY_KEYBOARD as SDL 1.2 cannot handle multiple keyboards
  return ANY_KEYBOARD;
}

int CSDLInputSystem::GetNumMice() const
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
  // SDL3 enumerates mice individually and the platform layer tracks one state per device, so the
  // input system can expose real MOUSE1_/MOUSE2_ bindings instead of the merged ANY_MOUSE only.
  return OSD::Platform::GetMouseCount();
#else
  // Return ANY_MOUSE as SDL 1.2 cannot handle multiple mice
  return ANY_MOUSE;
#endif
}

int CSDLInputSystem::GetNumJoysticks() const
{
  // Return number of joysticks found. Both backends keep one entry per open device, in the order
  // the bindings' JOYn_* names refer to.
#if defined(SUPERMODEL_PLATFORM_SDL2)
  if (m_useGameController)
    return (int)m_gamepads.size();
  else
    return (int)m_joysticks.size();
#else
  return (int)m_gamepadIds.size();
#endif
}

bool CSDLInputSystem::ConsumeDevicesChanged()
{
  const bool changed = m_devicesChanged;
  m_devicesChanged = false;
  return changed;
}

const KeyDetails *CSDLInputSystem::GetKeyDetails(int kbdNum)
{
  // Return nullptr as SDL 1.2 cannot handle multiple keyboards
  return nullptr;
}

const MouseDetails *CSDLInputSystem::GetMouseDetails(int mseNum)
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
  if (mseNum >= 0)
  {
    OSD::Platform::MouseInfo info;
    if (!OSD::Platform::GetMouseInfo(mseNum, &info))
      return nullptr;

    std::strncpy(m_mouseDetails.name, info.name, MAX_NAME_LENGTH);
    m_mouseDetails.name[MAX_NAME_LENGTH] = '\0';
    m_mouseDetails.isAbsolute = true;
    return &m_mouseDetails;
  }
#endif
  // Return nullptr as SDL 1.2 cannot handle multiple mice
  return nullptr;
}

const JoyDetails *CSDLInputSystem::GetJoyDetails(int joyNum)
{
  return &m_joyDetails[joyNum];
}

bool CSDLInputSystem::Poll()
{
  // Reset mouse wheel direction
  m_mouseWheelDir = 0;

  /*
   * The event drain and the keyboard/mouse snapshot live in the platform layer
   * now.  CSDLInputSystem still owns the mapping, calibration and gamepad
   * handles; it only consumes events whose meaning it needs (quit, close,
   * wheel and hot-plug) and reads the rest from the snapshot.
   */
  OSD::Platform::BeginInputFrame();

  OSD::Platform::Event event;
  while (OSD::Platform::PollEvent(&event))
  {
    switch (event.type)
    {
    case OSD::Platform::EventType::Quit:
      return false;

    case OSD::Platform::EventType::WindowClose:
      // Closing the window must work like quitting. SDL only raises QUIT when
      // the window being closed is its *only* window, and since the settings
      // GUI keeps its own window alive this one never is.
      return false;

    case OSD::Platform::EventType::MouseWheel:
      if (event.wheelY != 0.0f)
      {
        const int notches = static_cast<int>(event.wheelY);
        m_mouseZ += 5 * notches;
        m_mouseWheelDir = notches > 0 ? 1 : -1;
      }
      break;

    // Hot-plug. ADDED carries the stable instance id in gamepadId and the
    // enumeration index in deviceIndex; REMOVED carries the instance id.
    case OSD::Platform::EventType::GamepadAdded:
      {
        const int index = event.deviceIndex;
        if (index >= 0 && OpenJoystickAt(index))
        {
          m_devicesChanged = true;
          OSD::Platform::GamepadInfo info;
          if (OSD::Platform::GetGamepadInfo(index, &info))
          {
            InfoLog("Joystick added: %s (%s).\n",
              info.name[0] != '\0' ? info.name : "unknown device",
              info.isGamepad ? "game controller API" : "raw joystick");
          }
        }
      }
      break;

    case OSD::Platform::EventType::GamepadRemoved:
      CloseJoystickAt(event.gamepadId);
      break;

    default:
      break;
    }
  }

  ReadSnapshot();

  // Update joystick state (not required as called implicitly by the event drain)
  return true;
}

/*
 * Copies the platform snapshot into the caches the bindings read.
 *
 * Shared by Poll() - which calls it after draining the frame's events - and RefreshState(), which
 * calls it after re-pumping the devices but takes no events.
 */
void CSDLInputSystem::ReadSnapshot()
{
  const OSD::Platform::InputSnapshot& input = OSD::Platform::Input();

  // Get key state from the platform snapshot
  m_keyState = input.keys;

  // Get mouse state from the platform snapshot (wheel was handled by Poll())
  m_mouseX = static_cast<int>(input.mouseX);
  m_mouseY = static_cast<int>(input.mouseY);
  m_mouseXF = input.mouseX;
  m_mouseYF = input.mouseY;
  m_mouseButtons = static_cast<uint8_t>(input.mouseButtons);

#if defined(SUPERMODEL_PLATFORM_SDL3)
  // One read per device per frame, the same model as the gamepad snapshots below.
  m_mouseStates.clear();
  const int mouseCount = OSD::Platform::GetMouseCount();
  for (int i = 0; i < mouseCount; i++)
  {
    OSD::Platform::MouseState state;
    if (OSD::Platform::GetMouseStateByIndex(i, &state))
      m_mouseStates.push_back(state);
  }

  if (s_logGunProbe && GunProbeDue())
  {
    char device[64] = "(none)";
    if (!m_mouseStates.empty())
      snprintf(device, sizeof(device), "%.1f,%.1f", m_mouseStates[0].x, m_mouseStates[0].y);
    InfoLog("GunProbe: snapshot pointer %.1f,%.1f; per-device %s.",
      input.mouseX, input.mouseY, device);
  }

  // One device read per frame for every pad, rather than one per binding (see m_gamepadStates).
  RefreshGamepadStates();

  // Keep a vibration the drive board is still holding alive; see m_padVibration.
  ServicePadVibration();
  ReportForceFeedback();
#endif
}

/*
 * Late device sample.
 *
 * The frame's events are taken at the top of the frame, but everything between that and the
 * emulation step - building the in-game panel, recording the renderer's commands - costs time, and
 * a wheel or a pad input sampled at the top of the frame is that much older than the frame it ends
 * up in. This re-pumps and re-reads the snapshot at the step boundary.
 *
 * No events are taken here on purpose: the queue belongs to Poll(), and draining it twice would
 * deliver wheel notches and hot-plug notifications twice. Re-pumping still updates the keyboard
 * array in place, so the second read sees keys pressed after the first sample.
 */
void CSDLInputSystem::RefreshState()
{
  OSD::Platform::BeginInputFrame();
  ReadSnapshot();
}

void CSDLInputSystem::SetMouseVisibility(bool visible)
{
  OSD::Platform::SetCursorVisible(visible);
}

#if defined(SUPERMODEL_PLATFORM_SDL3)

/*
 * Rumble-only force feedback.
 *
 * Strengths arrive pre-scaled by the SDL2 path's config percentages; here they are simply clamped
 * to what SetGamepadRumble() accepts. The duration is short because every command is refreshed
 * while the effect is meant to last - a long duration would keep vibrating after the game stopped
 * asking for it.
 */
void CSDLInputSystem::StopAllEffect(int joyNum)
{
  if (JoyNumValid(joyNum))
    OSD::Platform::StopAllHapticEffects(m_gamepadIds[joyNum]);

  VibrationEffect(0.0f, joyNum);
}

void CSDLInputSystem::StopConstanteforce(int joyNum)
{
  if (JoyNumValid(joyNum))
    OSD::Platform::StopHapticEffect(m_gamepadIds[joyNum], OSD::Platform::HapticEffect::Constant);
}

void CSDLInputSystem::StopVibrationforce(int joyNum)
{
  VibrationEffect(0.0f, joyNum);
}

void CSDLInputSystem::StopSpringforce(int joyNum)
{
  if (JoyNumValid(joyNum))
    OSD::Platform::StopHapticEffect(m_gamepadIds[joyNum], OSD::Platform::HapticEffect::Spring);
}

void CSDLInputSystem::StopFrictionforce(int joyNum)
{
  if (JoyNumValid(joyNum))
    OSD::Platform::StopHapticEffect(m_gamepadIds[joyNum], OSD::Platform::HapticEffect::Friction);
}

void CSDLInputSystem::VibrationEffect(float strength, int joyNum)
{
  if (!JoyNumValid(joyNum))
    return;

  float clamped = strength;
  if (clamped < 0.0f)
    clamped = 0.0f;
  if (clamped > 1.0f)
    clamped = 1.0f;

  /*
   * Stored as a level and re-issued every frame by ServicePadVibration().
   *
   * The drive board only writes this value when it changes, and on real hardware that means "the
   * motor runs at this strength until further notice". SDL_RumbleGamepad() is a timed pulse, so a
   * single command produced one 100 ms buzz and then silence while the game still expected the
   * motor to be running - which is why a wall scrape felt like nothing at all.
   */
  if (joyNum < (int)m_padVibration.size())
    m_padVibration[joyNum] = clamped;

  const bool sent = OSD::Platform::SetGamepadRumble(m_gamepadIds[joyNum], clamped, clamped,
    clamped > 0.0f ? kVibrationPulseMs : 0);

  // Once per session: "the pad never vibrates" and "the game never asks it to" look identical from
  // the outside, and this is the last place where the two can be told apart.
  static bool reported = false;
  if (!reported)
  {
    reported = true;
    InfoLog("FFB: vibration force %.2f %s by the SDL rumble path.", clamped,
      sent ? "accepted" : "rejected");
  }
}

void CSDLInputSystem::ServicePadVibration()
{
  for (size_t i = 0; i < m_padVibration.size() && i < m_gamepadIds.size(); ++i)
  {
    if (m_padVibration[i] <= 0.0f)
      continue;
    OSD::Platform::SetGamepadRumble(m_gamepadIds[i], m_padVibration[i], m_padVibration[i],
      kVibrationPulseMs);
  }
}

void CSDLInputSystem::ReportForceFeedback()
{
  // Counted in frames rather than wall time: this runs from the frame's input sample, so 300 calls
  // is about five seconds at the arcade rate and nothing here depends on a timer being initialised.
  static unsigned framesSinceReport = 0;
  if (++framesSinceReport < 300)
    return;
  framesSinceReport = 0;

  unsigned total = 0;
  for (unsigned count : m_ffbCounts)
    total += count;
  if (total == 0)
    return;

  InfoLog("FFB: commands in the last ~5 s - constant %u, self-centering %u, friction %u, vibration %u.",
    m_ffbCounts[FFConstantForce], m_ffbCounts[FFSelfCenter], m_ffbCounts[FFFriction], m_ffbCounts[FFVibrate]);
  for (unsigned& count : m_ffbCounts)
    count = 0;
}

void CSDLInputSystem::ConstantForceEffect(float force, int dir, int length, int joyNum)
{
  (void)length;   // every effect is infinite and stopped by FFStop / a zero force

  if (!JoyNumValid(joyNum))
    return;
  if (!OSD::Platform::OpenHaptics(m_gamepadIds[joyNum]))
    return;

  OSD::Platform::SetHapticEffect(m_gamepadIds[joyNum], OSD::Platform::HapticEffect::Constant,
    force * (float)dir);
}

void CSDLInputSystem::SpringForceEffect(float force, int joyNum)
{
  if (!JoyNumValid(joyNum))
    return;
  if (!OSD::Platform::OpenHaptics(m_gamepadIds[joyNum]))
    return;

  OSD::Platform::SetHapticEffect(m_gamepadIds[joyNum], OSD::Platform::HapticEffect::Spring, force);
}

void CSDLInputSystem::FrictionForceEffect(float force, int joyNum)
{
  if (!JoyNumValid(joyNum))
    return;
  if (!OSD::Platform::OpenHaptics(m_gamepadIds[joyNum]))
    return;

  OSD::Platform::SetHapticEffect(m_gamepadIds[joyNum], OSD::Platform::HapticEffect::Friction, force);
}

#else

void CSDLInputSystem::StopAllEffect(int joyNum)
{
  StopConstanteforce(joyNum);
  StopVibrationforce(joyNum);
  StopSpringforce(joyNum);
  StopFrictionforce(joyNum);
}

void CSDLInputSystem::StopConstanteforce(int joyNum)
{
  // stop constante effect or rumble constant effect
  memset(&eff, 0, sizeof(SDL_HapticEffect));
  eff.type = SDL_HAPTIC_CONSTANT;
  eff.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
  eff.constant.direction.dir[0] = 0;
  eff.constant.length = 30;
  eff.constant.delay = 0;
  eff.constant.level = 0;

  if (SDL_HapticEffectSupported(m_SDLHapticDatas[joyNum].SDLhaptic, &eff))
  {
    SDL_HapticUpdateEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectConstantForceID, &eff);
  }
  else
  {
    SDL_HapticRumbleStop(m_SDLHapticDatas[joyNum].SDLhaptic);
  }
}

void CSDLInputSystem::StopVibrationforce(int joyNum)
{
  // stop vibration-rumble effect
  memset(&eff, 0, sizeof(SDL_HapticEffect));
  eff.type = SDL_HAPTIC_SINE;
  eff.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
  eff.constant.length = 500;
  eff.constant.delay = 0;
  eff.periodic.period = 50;
  eff.periodic.magnitude = 0;

  if (SDL_HapticEffectSupported(m_SDLHapticDatas[joyNum].SDLhaptic, &eff))
  {
    SDL_HapticUpdateEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectVibrationID, &eff);
  }
  else
  {
    SDL_HapticRumbleStop(m_SDLHapticDatas[joyNum].SDLhaptic);
  }

}

void CSDLInputSystem::StopSpringforce(int joyNum)
{
  // stop spring effect
  memset(&eff, 0, sizeof(SDL_HapticEffect));
  eff.type = SDL_HAPTIC_SPRING;
  eff.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
  eff.condition.delay = 0;
  eff.condition.length = SDL_HAPTIC_INFINITY;
  eff.condition.left_sat[0] = 0xFFFF;
  eff.condition.right_sat[0] = 0xFFFF;
  eff.condition.left_coeff[0] = 0;
  eff.condition.right_coeff[0] = 0;

  if (SDL_HapticEffectSupported(m_SDLHapticDatas[joyNum].SDLhaptic, &eff))
  {
    SDL_HapticUpdateEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectSpringForceID, &eff);
  }

}

void CSDLInputSystem::StopFrictionforce(int joyNum)
{
  // stop friction effect
  memset(&eff, 0, sizeof(SDL_HapticEffect));
  eff.type = SDL_HAPTIC_FRICTION;
  eff.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
  eff.condition.delay = 0;
  eff.condition.length = SDL_HAPTIC_INFINITY;
  eff.condition.left_sat[0] = 0xFFFF;
  eff.condition.right_sat[0] = 0xFFFF;
  eff.condition.left_coeff[0] = 0;
  eff.condition.right_coeff[0] = 0;

  if (SDL_HapticEffectSupported(m_SDLHapticDatas[joyNum].SDLhaptic, &eff))
  {
    SDL_HapticUpdateEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectFrictionForceID, &eff);
  }

}

void CSDLInputSystem::VibrationEffect(float strength, int joyNum)
{
  memset(&eff, 0, sizeof(SDL_HapticEffect));
  eff.type = SDL_HAPTIC_SINE;
  eff.constant.direction.type = SDL_HAPTIC_CARTESIAN;
  eff.periodic.delay = 0;
  eff.periodic.length = SDL_HAPTIC_INFINITY;
  eff.periodic.period = 50;
  eff.periodic.magnitude = (int)(strength * INT16_MAX);

  if (SDL_HapticEffectSupported(m_SDLHapticDatas[joyNum].SDLhaptic, &eff))
  {
    SDL_HapticUpdateEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectVibrationID, &eff);

    SDL_HapticRunEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectVibrationID, 1);
  }
  else
  {
    if (strength != 0.0f)
      SDL_HapticRumblePlay(m_SDLHapticDatas[joyNum].SDLhaptic, strength, 0);
    else
      SDL_HapticRumbleStop(m_SDLHapticDatas[joyNum].SDLhaptic);
  }

}

void CSDLInputSystem::ConstantForceEffect(float force, int dir, int length, int joyNum)
{
  memset(&eff, 0, sizeof(SDL_HapticEffect));
  eff.type = SDL_HAPTIC_CONSTANT;
  eff.constant.direction.type = SDL_HAPTIC_CARTESIAN;
  eff.constant.direction.dir[0] = 0; // in cartesian mode dir on x set 0 on y set 1
  eff.constant.length = length;
  eff.constant.level = (int)(dir * (force * INT16_MAX));

  if (SDL_HapticEffectSupported(m_SDLHapticDatas[joyNum].SDLhaptic, &eff))
  {
    SDL_HapticUpdateEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectConstantForceID, &eff);

    SDL_HapticRunEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectConstantForceID, 1);
  }
  else
  {
    float threshold = (float)m_config["SDLConstForceThreshold"].ValueAs<unsigned>() / 100.0f;
    if (force != 0.0f && force > threshold)
      SDL_HapticRumblePlay(m_SDLHapticDatas[joyNum].SDLhaptic, force, 200);
    else
      SDL_HapticRumbleStop(m_SDLHapticDatas[joyNum].SDLhaptic);
  }

}

void CSDLInputSystem::SpringForceEffect(float force, int joyNum)
{
  memset(&eff, 0, sizeof(SDL_HapticEffect));
  eff.type = SDL_HAPTIC_SPRING;
  eff.constant.direction.type = SDL_HAPTIC_CARTESIAN;
  eff.condition.delay = 0;
  eff.condition.length = SDL_HAPTIC_INFINITY;
  eff.condition.left_sat[0] = 0xffff;
  eff.condition.right_sat[0] = 0xffff;
  eff.condition.left_coeff[0] = (int)(force * INT16_MAX);
  eff.condition.right_coeff[0] = (int)(force * INT16_MAX);

  if (SDL_HapticEffectSupported(m_SDLHapticDatas[joyNum].SDLhaptic, &eff))
  {
    SDL_HapticUpdateEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectSpringForceID, &eff);

    SDL_HapticRunEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectSpringForceID, 1);
  }

}

void CSDLInputSystem::FrictionForceEffect(float force, int joyNum)
{
  memset(&eff, 0, sizeof(SDL_HapticEffect));
  eff.type = SDL_HAPTIC_FRICTION;
  eff.constant.direction.type = SDL_HAPTIC_CARTESIAN;
  eff.condition.delay = 0;
  eff.condition.length = SDL_HAPTIC_INFINITY;
  eff.condition.left_sat[0] = 0xffff;
  eff.condition.right_sat[0] = 0xffff;
  eff.condition.left_coeff[0] = (int)(force * INT16_MAX);
  eff.condition.right_coeff[0] = (int)(force * INT16_MAX);

  if (SDL_HapticEffectSupported(m_SDLHapticDatas[joyNum].SDLhaptic, &eff))
  {
    SDL_HapticUpdateEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectFrictionForceID, &eff);

    SDL_HapticRunEffect(m_SDLHapticDatas[joyNum].SDLhaptic, m_SDLHapticDatas[joyNum].effectFrictionForceID, 1);
  }

}

// due to the sdl2 SDL_HapticNumAxes() bug in linux (always return 2 in linux)
// test if haptic controller has the most basic constant force effect
// if it has -> ffb wheel or ffb joystick
// if it hasn't -> pad
bool CSDLInputSystem::HasBasicForce(SDL_Haptic* hap)
{
  memset(&eff, 0, sizeof(SDL_HapticEffect));
  eff.type = SDL_HAPTIC_CONSTANT;
  eff.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
  eff.constant.direction.dir[0] = 0;
  eff.constant.length = 30;
  eff.constant.delay = 0;
  eff.constant.level = 0;

  if (SDL_HapticEffectSupported(hap, &eff))
    return true;
  else
    return false;
}

#endif  // SUPERMODEL_PLATFORM_SDL2
