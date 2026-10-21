/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2025 The Supermodel Team
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
 * ConfigFile.h
 *
 * Default Supermodel.ini and generation of the comment block that heads every
 * configuration file we write out.
 */

#ifndef INCLUDED_CONFIGFILE_H
#define INCLUDED_CONFIGFILE_H

#include <ctime>
#include <string>

#include "FileSystemPath.h"

#define CONFIG_FILE_BANNER_HEAD \
   ";;\n" \
   ";; Supermodel Configuration File\n"

#define CONFIG_FILE_BANNER_TAIL \
   ";;\n"

#define CONFIG_FILE_QUICK_OVERVIEW \
   ";\n" \
   "; Quick Overview\n" \
   "; --------------\n" \
   ";\n" \
   "; All settings are case sensitive.  Numbers must be integers.  Check your\n" \
   "; spelling carefully because invalid settings are silently ignored.  To verify\n" \
   "; that your settings are being parsed correctly, check the contents of\n" \
   "; Supermodel.log.\n" \
   ";\n" \
   "; Global options apply to all games.  To create configuration profiles for\n" \
   "; individual games, place settings under sections with the same name as the\n" \
   "; corresponding MAME ROM set, like so:\n" \
   ";\n" \
   ";       ; Scud Race\n" \
   ";       [ scudau ]\n" \
   ";\n" \
   ";       SoundVolume = 50\n" \
   ";       MusicVolume = 200\n" \
   ";       ; ... etc. ...\n" \
   ";\n" \
   "; For a list of all valid settings, please consult README.txt.  Only default\n" \
   "; inputs are assigned here.\n" \
   ";\n"

// Current local date and time in a human readable form, e.g.
// "2026-09-20 14:31:07".
inline std::string CurrentDateTimeString()
{
   std::time_t now = std::time(nullptr);
   std::tm local{};
#ifdef _WIN32
   localtime_s(&local, &now);
#else
   localtime_r(&now, &local);
#endif
   char buffer[64];
   if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) == 0)
      return "unknown time";
   return buffer;
}

// Wraps the given banner lines in the comment block that heads every
// configuration file we write out
inline std::string MakeConfigFileComment(const std::string &bannerLines)
{
   return std::string(CONFIG_FILE_BANNER_HEAD)
      + bannerLines
      + CONFIG_FILE_BANNER_TAIL
      + "\n"
      + "\n"
      + CONFIG_FILE_QUICK_OVERVIEW;
}

// Generates the comment block placed at the top of a configuration file
// whenever one is written out. Configuration files are regenerated from the
// parse tree, so this is all that survives of the original comments.
// 'description' is a complete sentence describing what produced the file, e.g.
// "Default settings."
inline std::string ConfigFileComment(const std::string &description)
{
   return MakeConfigFileComment(";; " + description + "\n");
}

// As ConfigFileComment() but additionally records when the file was written, on
// a line of its own beneath the description:
//
//    ;; Updated from GUI.
//    ;; Written at: 2026-09-20 14:31:07
//
inline std::string TimestampedConfigFileComment(const std::string &description)
{
   return MakeConfigFileComment(
      ";; " + description + "\n"
      + ";; Written at: " + CurrentDateTimeString() + "\n");
}

// Settings of the default configuration file, sans comment block (see
// DefaultConfigFileContents())
static const char s_defaultConfigFileSettings[] =
{
   "[ Global ]\n"
   "\n"
   "; Where the ROM sets (.zip) are kept. Point this at your own folder from the setting\n"
   "; window, or edit it here. Each .zip must contain a single game.\n"
   "ROMPath = \"@ROMPATH@\"\n"
   "\n"
   "; Graphics\n"
   "New3DEngine = true\n"
   "QuadRendering = false\n"
   "WideScreen = false\n"
   "Stretch = false\n"
   "WideBackground = false\n"
   "; Cabinet backdrop behind the black bars: 0=off, 1=built-in art, 2=a PNG of your own.\n"
   "BezelMode = 1\n"
   "BezelImage = \"\"\n"
   "BezelBrightness = 100\n"
   "Supersampling = 1\n"
   "; CRT-like color adaption: 0=none, 1=ARI/D93 (recommended for all JP developed games),\n"
   "; 2=PVM_20M2U/D93, 3=BT601_525/D93, 4=BT601_525/D65 (recommended for all US developed games)\n"
   "; 5=BT601_625/D65 (recommended for all EUR/AUS developed games)\n"
   "CRTcolors = 0\n"
   "; Upscale filter used for the 2D layers: 0=none/sharp pixels, 1=biquintic,\n"
   "; 2=bilinear, 3=bicubic\n"
   "UpscaleMode = 2\n"
   "\n"
   "; Refresh rate (milliHertz accuracy). Actual Model 3 refresh rate is 57.524 Hz\n"
   "; but this can cause judder so we use 60 Hz by default.\n"
   "RefreshRate = 60.000\n"
   "\n"
   "; Legacy SCSP DSP implementation for games that do not play well with the newer\n"
   "; one (e.g., Fighting Vipers 2)\n"
   "LegacySoundDSP = false\n"
   "\n"
   "; Network board\n"
   "; Daytona 2, Scud Race, Sega Rally 2, Le Mans 24, Von 2, Dirt Devils and the Harley and\n"
   "; Spikeout boards refuse to boot past their network self-test unless the board answers, so\n"
   "; this stays on even for single-player. PortIn and PortOut MUST be the same port, and the\n"
   "; address must be the loopback one: that makes the send and receive sockets meet, which is\n"
   "; how a single machine links to itself. With different ports nothing ever connects and the\n"
   "; game hangs on the self-test screen instead of starting.\n"
   "; See develop.md section 11 for the full explanation.\n"
   "Network = true\n"
   "SimulateNet = true\n"
   "PortIn = 1970\n"
   "PortOut = 1970\n"
   "AddressOut = \"127.0.0.1\"\n"
   "\n"
   "; Common\n"
   "InputStart1 = \"KEY_1,JOY1_BUTTON8\"\n"
   "InputStart2 = \"KEY_2,JOY2_BUTTON8\"\n"
   "InputCoin1 = \"KEY_3,JOY1_BUTTON7\"\n"
   "InputCoin2 = \"KEY_4,JOY2_BUTTON7\"\n"
   "InputServiceA = \"KEY_5\"\n"
   "InputServiceB = \"KEY_7\"\n"
   "InputTestA = \"KEY_6\"\n"
   "InputTestB = \"KEY_8\"\n"
   "\n"
   "; 4-way digital joysticks (left stick and D-pad both drive the stick)\n"
   "InputJoyUp = \"KEY_UP,JOY1_UP,JOY1_POV1_UP\"\n"
   "InputJoyDown = \"KEY_DOWN,JOY1_DOWN,JOY1_POV1_DOWN\"\n"
   "InputJoyLeft = \"KEY_LEFT,JOY1_LEFT,JOY1_POV1_LEFT\"\n"
   "InputJoyRight = \"KEY_RIGHT,JOY1_RIGHT,JOY1_POV1_RIGHT\"\n"
   "InputJoyUp2 = \"JOY2_UP,JOY2_POV1_UP\"\n"
   "InputJoyDown2 = \"JOY2_DOWN,JOY2_POV1_DOWN\"\n"
   "InputJoyLeft2 = \"JOY2_LEFT,JOY2_POV1_LEFT\"\n"
   "InputJoyRight2 = \"JOY2_RIGHT,JOY2_POV1_RIGHT\"\n"
   "\n"
   "; Fighting game buttons\n"
   "InputPunch = \"KEY_A,JOY1_BUTTON1\"\n"
   "InputKick = \"KEY_S,JOY1_BUTTON2\"\n"
   "InputGuard = \"KEY_D,JOY1_BUTTON3\"\n"
   "InputEscape = \"KEY_F,JOY1_BUTTON4\"\n"
   "InputPunch2 = \"JOY2_BUTTON1\"\n"
   "InputKick2 = \"JOY2_BUTTON2\"\n"
   "InputGuard2 = \"JOY2_BUTTON3\"\n"
   "InputEscape2 = \"JOY2_BUTTON4\"\n"
   "\n"
   "; Spikeout buttons\n"
   "InputShift = \"KEY_A,JOY1_BUTTON1\"\n"
   "InputBeat = \"KEY_S,JOY1_BUTTON2\"\n"
   "InputCharge = \"KEY_D,JOY1_BUTTON3\"\n"
   "InputJump = \"KEY_F,JOY1_BUTTON4\"\n"
   "\n"
   "; Virtua Striker buttons\n"
   "InputShortPass = \"KEY_A,JOY1_BUTTON1\"\n"
   "InputLongPass = \"KEY_S,JOY1_BUTTON2\"\n"
   "InputShoot = \"KEY_D,JOY1_BUTTON3\"\n"
   "InputShortPass2 = \"JOY2_BUTTON1\"\n"
   "InputLongPass2 = \"JOY2_BUTTON2\"\n"
   "InputShoot2 = \"JOY2_BUTTON3\"\n"
   "\n"
   "; Steering wheel\n"
   "InputSteeringLeft = \"KEY_LEFT,JOY1_POV1_LEFT\"   ; digital, turn wheel left\n"
   "InputSteeringRight = \"KEY_RIGHT,JOY1_POV1_RIGHT\" ; digital, turn wheel right\n"
   "InputSteering = \"JOY1_XAXIS\"        ; analog, full steering range\n"
   "\n"
   "; Pedals. The accelerator is a face button - the X position, index 2 - so it behaves\n"
   "; the same on every input system. The brake is the right analogue trigger: XInput and\n"
   "; SDL GameController give each trigger its own axis, so the right one is JOY1_RZAXIS\n"
   "; there, whereas legacy DirectInput and the plain SDL joystick path share a single Z\n"
   "; axis and put the right trigger on its positive half. The default input system is the\n"
   "; legacy one, hence ZAXIS_POS here.\n"
   "InputAccelerator = \"KEY_UP,JOY1_BUTTON3\"\n"
   "InputBrake = \"KEY_DOWN,JOY1_ZAXIS_POS\"\n"
   "\n"
   "; Up/down shifter manual transmission (all racers)\n"
   "InputGearShiftUp = \"KEY_Y,JOY1_BUTTON6\"     ; sequential shift up (right bumper)\n"
   "InputGearShiftDown = \"KEY_H,JOY1_BUTTON5\"   ; sequential shift down (left bumper)\n"
   "\n"
   "; 4-Speed manual transmission (Daytona 2, Sega Rally 2, Scud Race). Keyboard only: the\n"
   "; sequential pair above covers every Model 3 gearbox, which leaves the pad buttons free\n"
   "; for the view buttons below.\n"
   "InputGearShift1 = \"KEY_Q\"\n"
   "InputGearShift2 = \"KEY_W\"\n"
   "InputGearShift3 = \"KEY_E\"\n"
   "InputGearShift4 = \"KEY_R\"\n"
   "InputGearShiftN = \"KEY_T,JOY1_BUTTON1\"\n"
   "\n"
   "; VR4 view change buttons (Daytona 2, Le Mans 24, Scud Race). Must not share buttons\n"
   "; with the shifter, or the car would change gear while braking.\n"
   "InputVR1 = \"KEY_A,JOY1_BUTTON2\"\n"
   "InputVR2 = \"KEY_S,JOY1_BUTTON4\"\n"
   "InputVR3 = \"KEY_D,JOY1_BUTTON9\"\n"
   "InputVR4 = \"KEY_F,JOY1_BUTTON10\"\n"
   "\n"
   "; Single-button view cycling. With VRCycle on, this steps through the four views and the\n"
   "; VR1..VR4 bindings above are ignored, so one button does the job of four. Y on a pad is\n"
   "; otherwise only VR2, which is inert while cycling is on.\n"
   "InputVRNext = \"KEY_D,JOY1_BUTTON4\"\n"
   "\n"
   "; Single view change button (Dirt Devils, ECA, Harley-Davidson, Sega Rally 2). The D-pad\n"
   "; up is free on every racer - only left/right are used, as a digital steering fallback.\n"
   "InputViewChange = \"KEY_A,JOY1_POV1_UP\"\n"
   "\n"
   "; Handbrake (Sega Rally 2)\n"
   "InputHandBrake = \"KEY_S,JOY1_POV1_DOWN\"\n"
   "\n"
   "; Harley-Davidson controls. That game has no VR buttons, so face button Y is free. Note it\n"
   "; does have a shifter, so the rear brake must not sit on a bumper (GearShiftUp/Down).\n"
   "InputRearBrake = \"KEY_S,JOY1_BUTTON4\"\n"
   "InputMusicSelect = \"KEY_D,JOY1_BUTTON1\"\n"
   "\n"
   "; Virtual On macros\n"
   "InputTwinJoyTurnLeft = \"KEY_Q,JOY1_RXAXIS_NEG\"\n"
   "InputTwinJoyTurnRight = \"KEY_W,JOY1_RXAXIS_POS\"\n"
   "InputTwinJoyForward = \"KEY_UP,JOY1_YAXIS_NEG\"\n"
   "InputTwinJoyReverse = \"KEY_DOWN,JOY1_YAXIS_POS\"\n"
   "InputTwinJoyStrafeLeft = \"KEY_LEFT,JOY1_XAXIS_NEG\"\n"
   "InputTwinJoyStrafeRight = \"KEY_RIGHT,JOY1_XAXIS_POS\"\n"
   "InputTwinJoyJump = \"KEY_E,JOY1_BUTTON1\"\n"
   "InputTwinJoyCrouch = \"KEY_R,JOY1_BUTTON2\"\n"
   "\n"
   "; Virtual On individual joystick mapping\n"
   "InputTwinJoyLeft1 = \"NONE\"\n"
   "InputTwinJoyLeft2 = \"NONE\"\n"
   "InputTwinJoyRight1 = \"NONE\"\n"
   "InputTwinJoyRight2 = \"NONE\"\n"
   "InputTwinJoyUp1 = \"NONE\"\n"
   "InputTwinJoyUp2 = \"NONE\"\n"
   "InputTwinJoyDown1 = \"NONE\"\n"
   "InputTwinJoyDown2 = \"NONE\"\n"
   "\n"
   "; Virtual On buttons\n"
   "InputTwinJoyShot1 = \"KEY_A,JOY1_BUTTON5\"\n"
   "InputTwinJoyShot2 = \"KEY_S,JOY1_BUTTON6\"\n"
   "InputTwinJoyTurbo1 = \"KEY_Z,JOY1_BUTTON3\"   ; not Back/Select - that is coin\n"
   "InputTwinJoyTurbo2 = \"KEY_X,JOY1_BUTTON4\"   ; not Start\n"
   "\n"
   "; Analog joystick (Star Wars Trilogy)\n"
   "InputAnalogJoyLeft = \"KEY_LEFT\"             ; digital, move left\n"
   "InputAnalogJoyRight = \"KEY_RIGHT\"           ; digital, move right\n"
   "InputAnalogJoyUp = \"KEY_UP\"                 ; digital, move up\n"
   "InputAnalogJoyDown = \"KEY_DOWN\"             ; digital, move down\n"
   "InputAnalogJoyX = \"JOY1_XAXIS,MOUSE_XAXIS\"  ; analog, full X axis\n"
   "InputAnalogJoyY = \"JOY1_YAXIS,MOUSE_YAXIS\"  ; analog, full Y axis\n"
   "InputAnalogJoyTrigger = \"KEY_A,JOY1_ZAXIS_POS,MOUSE_LEFT_BUTTON\"\n"
   "InputAnalogJoyEvent = \"KEY_S,JOY1_ZAXIS_NEG,MOUSE_RIGHT_BUTTON\"\n"
   "InputAnalogJoyTrigger2 = \"JOY2_BUTTON1\"\n"
   "InputAnalogJoyEvent2 = \"NONE\"\n"
   "\n"
   "; Light guns (Lost World)\n"
   "InputGunLeft = \"KEY_LEFT\"               ; digital, move gun left\n"
   "InputGunRight = \"KEY_RIGHT\"             ; digital, move gun right\n"
   "InputGunUp = \"KEY_UP\"                   ; digital, move gun up\n"
   "InputGunDown = \"KEY_DOWN\"               ; digital, move gun down\n"
   "InputGunX = \"MOUSE_XAXIS,JOY1_XAXIS\"    ; analog, full X axis\n"
   "InputGunY = \"MOUSE_YAXIS,JOY1_YAXIS\"    ; analog, full Y axis\n"
   "InputTrigger = \"KEY_A,JOY1_ZAXIS_POS,MOUSE_LEFT_BUTTON\"\n"
   "InputOffscreen = \"KEY_S,JOY1_ZAXIS_NEG,MOUSE_RIGHT_BUTTON\"  ; point off-screen\n"
   "InputAutoTrigger = 0                    ; automatic reload when off-screen\n"
   "InputGunLeft2 = \"NONE\"\n"
   "InputGunRight2 = \"NONE\"\n"
   "InputGunUp2 = \"NONE\"\n"
   "InputGunDown2 = \"NONE\"\n"
   "InputGunX2 = \"JOY2_XAXIS\"\n"
   "InputGunY2 = \"JOY2_YAXIS\"\n"
   "InputTrigger2 = \"JOY2_ZAXIS_POS\"\n"
   "InputOffscreen2 = \"JOY2_ZAXIS_NEG\"\n"
   "InputAutoTrigger2 = 0\n"
   "\n"
   "; Analog guns (Ocean Hunter, LA Machineguns)\n"
   "InputAnalogGunLeft = \"KEY_LEFT\"               ; digital, move gun left\n"
   "InputAnalogGunRight = \"KEY_RIGHT\"             ; digital, move gun right\n"
   "InputAnalogGunUp = \"KEY_UP\"                   ; digital, move gun up\n"
   "InputAnalogGunDown = \"KEY_DOWN\"               ; digital, move gun down\n"
   "InputAnalogGunX = \"MOUSE_XAXIS,JOY1_XAXIS\"    ; analog, full X axis\n"
   "InputAnalogGunY = \"MOUSE_YAXIS,JOY1_YAXIS\"    ; analog, full Y axis\n"
   "InputAnalogTriggerLeft = \"KEY_A,JOY1_ZAXIS_NEG,MOUSE_LEFT_BUTTON\"\n"
   "InputAnalogTriggerRight = \"KEY_S,JOY1_ZAXIS_POS,MOUSE_RIGHT_BUTTON\"\n"
   "InputAnalogGunLeft2 = \"NONE\"\n"
   "InputAnalogGunRight2 = \"NONE\"\n"
   "InputAnalogGunUp2 = \"NONE\"\n"
   "InputAnalogGunDown2 = \"NONE\"\n"
   "InputAnalogGunX2 = \"NONE\"\n"
   "InputAnalogGunY2 = \"NONE\"\n"
   "InputAnalogTriggerLeft2 = \"NONE\"\n"
   "InputAnalogTriggerRight2 = \"NONE\"\n"
   "\n"
   "; Ski Champ controls\n"
   "InputSkiLeft = \"KEY_LEFT\"\n"
   "InputSkiRight = \"KEY_RIGHT\"\n"
   "InputSkiUp = \"KEY_UP\"\n"
   "InputSkiDown = \"KEY_DOWN\"\n"
   "InputSkiX = \"JOY1_XAXIS\"\n"
   "InputSkiY = \"JOY1_YAXIS\"\n"
   "InputSkiPollLeft = \"KEY_A,JOY1_BUTTON1\"\n"
   "InputSkiPollRight = \"KEY_S,JOY1_BUTTON2\"\n"
   "InputSkiSelect1 = \"KEY_Q,JOY1_BUTTON3\"\n"
   "InputSkiSelect2 = \"KEY_W,JOY1_BUTTON4\"\n"
   "InputSkiSelect3 = \"KEY_E,JOY1_BUTTON5\"\n"
   "\n"
   "; Magical Truck Adventure controls\n"
   "InputMagicalLeverUp1 = \"KEY_UP\"\n"
   "InputMagicalLeverDown1 = \"KEY_DOWN\"\n"
   "InputMagicalLeverUp2 = \"NONE\"\n"
   "InputMagicalLeverDown2 = \"NONE\"\n"
   "InputMagicalLever1 = \"JOY1_YAXIS\"\n"
   "InputMagicalLever2 = \"JOY2_YAXIS\"\n"
   "InputMagicalPedal1 = \"KEY_A,JOY1_BUTTON1\"\n"
   "InputMagicalPedal2 = \"KEY_S,JOY2_BUTTON1\"\n"
   "\n"
   "; Sega Bass Fishing / Get Bass controls\n"
   "InputFishingRodLeft = \"KEY_LEFT\"\n"
   "InputFishingRodRight = \"KEY_RIGHT\"\n"
   "InputFishingRodUp = \"KEY_UP\"\n"
   "InputFishingRodDown = \"KEY_DOWN\"\n"
   "InputFishingStickLeft = \"KEY_A\"\n"
   "InputFishingStickRight = \"KEY_D\"\n"
   "InputFishingStickUp = \"KEY_W\"\n"
   "InputFishingStickDown = \"KEY_S\"\n"
   "InputFishingRodX = \"JOY1_XAXIS\"\n"
   "InputFishingRodY = \"JOY1_YAXIS\"\n"
   "InputFishingStickX = \"JOY1_RXAXIS\"\n"
   "InputFishingStickY = \"JOY1_RYAXIS\"\n"
   "InputFishingReel = \"KEY_SPACE,JOY1_ZAXIS_POS\"\n"
   "InputFishingCast = \"KEY_Z,JOY1_BUTTON1\"\n"
   "InputFishingSelect = \"KEY_X,JOY1_BUTTON2\"\n"
   "InputFishingTension = \"KEY_T,JOY1_ZAXIS_NEG\"\n"
};

// Complete contents of the default configuration file, written out whenever no
// configuration file is present. The ROM path is filled in at runtime because the
// correct default differs by platform (see FileSystemPath::GetDefaultRomPath()).
inline std::string DefaultConfigFileContents()
{
   std::string settings(s_defaultConfigFileSettings);
   const std::string placeholder = "@ROMPATH@";
   const std::string::size_type at = settings.find(placeholder);
   if (at != std::string::npos)
      settings.replace(at, placeholder.size(), FileSystemPath::GetDefaultRomPath());

   return ConfigFileComment("Default settings.") + "\n" + "\n" + settings;
}

#endif  // INCLUDED_CONFIGFILE_H