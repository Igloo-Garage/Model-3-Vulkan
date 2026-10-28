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
 * Inputs.h
 *
 * Header file for CInputs, a class which manages all individual inputs.
 */
 
#ifndef INCLUDED_INPUTS_H
#define INCLUDED_INPUTS_H

#include "InputTypes.h"
#include "Types.h"
#include "Util/NewConfig.h"
#include <vector>
#include <memory>

class CInputSystem;
class CInput;
struct Game;

/*
 * Represents the collection of Model3 inputs.
 */
class CInputs
{
private:
  // Assigned input system
  std::shared_ptr<CInputSystem> m_system;

  // Vector of all created inputs
  std::vector<std::shared_ptr<CInput>> m_inputs;

  /*
   * Steering axis smoothing: 0 = off, 1 = light, 2 = medium, 3 = strong.
   *
   * The arcade cabinet has a 270-degree wheel with physical damping; a thumbstick is a step input
   * and the driving games' chase cameras react to the first derivative of the steering value, so a
   * short flick shows up as a one-frame camera snap. The filter runs once per emulated frame in
   * PollGameInputsLate(), after the raw axis has been sampled and before the CPU sees it.
   */
  int m_gamepadSteeringSmoothing = 0;
  bool m_steeringFilterValid = false;
  double m_steeringFiltered = 0.0;
  bool m_steeringProbe = false;
  UINT64 m_steeringProbeFrame = 0;
  void FilterSteering();

  /*
   * Adds a switch input (eg button) to this collection.
   */ 
  std::shared_ptr<CSwitchInput> AddSwitchInput(const char *id, const char *label, unsigned gameFlags, const char *defaultMapping,
    UINT16 offVal = 0x00, UINT16 onVal = 0x01);

  /*
   * Adds an analog input (eg pedal) to this collection.
   */
  std::shared_ptr<CAnalogInput> AddAnalogInput(const char *id, const char *label, unsigned gameFlags, const char *defaultMapping,
    UINT16 minVal = 0x00, UINT16 maxVal = 0xFF);

  /*
   * Adds an axis input (eg jostick axis, light gun axis or steering wheel) to this collection.
   */
  std::shared_ptr<CAxisInput> AddAxisInput(const char *id, const char *label, unsigned gameFlags, const char *defaultMapping,
      std::shared_ptr<CAnalogInput> axisNeg, std::shared_ptr<CAnalogInput> axisPos, UINT16 minVal = 0x00, UINT16 offVal = 0x80, UINT16 maxVal = 0xFF);

  /*
   * Adds a 4-gear shifter input to this collection.
   */
  std::shared_ptr<CGearShift4Input> AddGearShift4Input(const char *id, const char *label, unsigned gameFlags,
      std::shared_ptr<CSwitchInput> shift1, std::shared_ptr<CSwitchInput> shift2, std::shared_ptr<CSwitchInput> shift3, std::shared_ptr<CSwitchInput> shift4, std::shared_ptr<CSwitchInput> shiftN, std::shared_ptr<CSwitchInput> shiftUp, std::shared_ptr<CSwitchInput> shiftDown);

  /*
   * Adds a lightgun trigger input to this collection.
   */
  std::shared_ptr<CTriggerInput> AddTriggerInput(const char *id, const char *label, unsigned gameFlags,
      std::shared_ptr<CSwitchInput> trigger, std::shared_ptr<CSwitchInput> offscreen, UINT16 offVal = 0x00, UINT16 onVal = 0x01);

  void PrintHeader(const char *fmt, ...);

  void PrintConfigureInputsHelp();

public:
  // UI controls
  std::shared_ptr<CSwitchInput> uiExit;
  std::shared_ptr<CSwitchInput> uiReset;
  std::shared_ptr<CSwitchInput> uiPause;
  std::shared_ptr<CSwitchInput> uiFullScreen;
  std::shared_ptr<CSwitchInput> uiSaveState;
  std::shared_ptr<CSwitchInput> uiChangeSlot;
  std::shared_ptr<CSwitchInput> uiLoadState;
  std::shared_ptr<CSwitchInput> uiMusicVolUp;
  std::shared_ptr<CSwitchInput> uiMusicVolDown;
  std::shared_ptr<CSwitchInput> uiSoundVolUp;
  std::shared_ptr<CSwitchInput> uiSoundVolDown;
  std::shared_ptr<CSwitchInput> uiClearNVRAM;
  std::shared_ptr<CSwitchInput> uiSelectCrosshairs;
  std::shared_ptr<CSwitchInput> uiToggleFrLimit;
  std::shared_ptr<CSwitchInput> uiDumpInpState;
  std::shared_ptr<CSwitchInput> uiDumpTimings;
  std::shared_ptr<CSwitchInput> uiScreenshot;
  // Opens and closes the in-game settings overlay. Bound to Tab, which is deliberately not
  // offered to game bindings - see the note on s_reservedKeyNames in InputSystem.cpp. Fixed rather
  // than user-editable: the Controls page only lists game inputs, so this never appears there, and
  // the key it uses is kept out of the capture path.
  std::shared_ptr<CSwitchInput> uiToggleOverlay;
  // Shows and hides the statistics readout. Bound to the key above Tab (backquote), withheld from
  // game bindings the same way Tab is.
  std::shared_ptr<CSwitchInput> uiToggleStatsOsd;
  // Switches presentation between the arcade rate (VRR 1x) and twice it (VRR 2x), for an A/B of
  // smoothness without leaving the game. F8 is next to the volume keys and is not bound to anything.
  std::shared_ptr<CSwitchInput> uiToggleVrr;
  // Rescans the HD texture folder of the running game. F4: the function keys around it are already
  // taken (F5-F7 save states, F8 VRR, F9-F12 volume), and a pack author replacing files while
  // playing needs this without leaving the session.
  std::shared_ptr<CSwitchInput> uiReloadHDTextures;
#ifdef SUPERMODEL_DEBUGGER
  std::shared_ptr<CSwitchInput> uiEnterDebugger;
#endif

  // Common controls between all games
  std::shared_ptr<CSwitchInput> coin[2];
  std::shared_ptr<CSwitchInput> start[2];
  std::shared_ptr<CSwitchInput> test[2];
  std::shared_ptr<CSwitchInput> service[2];

  // Joysticks (players 1 and 2)
  std::shared_ptr<CSwitchInput> up[2];
  std::shared_ptr<CSwitchInput> down[2];
  std::shared_ptr<CSwitchInput> left[2];
  std::shared_ptr<CSwitchInput> right[2];

  // Fighting game controls (players 1 and 2)
  std::shared_ptr<CSwitchInput> punch[2];
  std::shared_ptr<CSwitchInput> kick[2];
  std::shared_ptr<CSwitchInput> guard[2];
  std::shared_ptr<CSwitchInput> escape[2];
  
  // Spikeout controls
  std::shared_ptr<CSwitchInput> shift;
  std::shared_ptr<CSwitchInput> beat;
  std::shared_ptr<CSwitchInput> charge;
  std::shared_ptr<CSwitchInput> jump;

  // Soccer game controls (players 1 and 2)
  std::shared_ptr<CSwitchInput> shortPass[2];
  std::shared_ptr<CSwitchInput> longPass[2];
  std::shared_ptr<CSwitchInput> shoot[2];

  // Vehicle controls
  std::shared_ptr<CAxisInput> steering;
  std::shared_ptr<CAnalogInput> accelerator;
  std::shared_ptr<CAnalogInput> brake;

  // VR view buttons: VR1 Red, VR2 Blue, VR3 Yellow, VR4 Green
  std::shared_ptr<CSwitchInput> vr[4];

  // Optional single-button view cycling. When enabled, pressing vrNext advances to the next of the
  // four views, so one button does the job of four. The trade is that you can no longer jump
  // straight to a particular view - reaching one may take up to three presses. Enabled by the
  // VRCycle option; when it is off, vr[] behaves exactly as before and vrNext does nothing.
  std::shared_ptr<CSwitchInput> vrNext;

  // Which view is active. With cycling on this is the cycle position; with it off it is simply
  // whichever vr[] button is held. The Model 3 board reads it when writing the VR bits, so the
  // cycle replaces button state without the board needing to know it exists.
  bool IsVRSelected(int index) const;

  bool m_vrCycle = false;      // cycling enabled
  int  m_vrCycleIndex = 2;     // active view (0-3); 2 is the default third-person view
  int  m_vrDefaultIndex = 2;   // view the cycle starts from
  bool m_gameInputSuppressed = false;  // see SetGameInputSuppressed()

 
  // Up/down gear shift
  std::shared_ptr<CSwitchInput> gearShiftUp;
  std::shared_ptr<CSwitchInput> gearShiftDown;
  
  // 4-speed gear shift
  std::shared_ptr<CGearShift4Input> gearShift4;

  // Rally controls
  std::shared_ptr<CSwitchInput> viewChange;
  std::shared_ptr<CSwitchInput> handBrake;

  // Harley Davidson controls
  std::shared_ptr<CAnalogInput> rearBrake;
  std::shared_ptr<CSwitchInput> musicSelect;

  // Twin joysticks (individually mapped version; 1 = left stick, 2 = right stick)
  std::shared_ptr<CSwitchInput> twinJoyLeft1;
  std::shared_ptr<CSwitchInput> twinJoyLeft2;
  std::shared_ptr<CSwitchInput> twinJoyRight1;
  std::shared_ptr<CSwitchInput> twinJoyRight2;
  std::shared_ptr<CSwitchInput> twinJoyUp1;
  std::shared_ptr<CSwitchInput> twinJoyUp2;
  std::shared_ptr<CSwitchInput> twinJoyDown1;
  std::shared_ptr<CSwitchInput> twinJoyDown2;
  std::shared_ptr<CSwitchInput> twinJoyShot1;
  std::shared_ptr<CSwitchInput> twinJoyShot2;
  std::shared_ptr<CSwitchInput> twinJoyTurbo1;
  std::shared_ptr<CSwitchInput> twinJoyTurbo2;
  
  // Twin joysticks (macro mapping, for users w/out dual joysticks) 
  std::shared_ptr<CSwitchInput> twinJoyTurnLeft;
  std::shared_ptr<CSwitchInput> twinJoyTurnRight;
  std::shared_ptr<CSwitchInput> twinJoyStrafeLeft;
  std::shared_ptr<CSwitchInput> twinJoyStrafeRight;
  std::shared_ptr<CSwitchInput> twinJoyForward;
  std::shared_ptr<CSwitchInput> twinJoyReverse;
  std::shared_ptr<CSwitchInput> twinJoyJump;
  std::shared_ptr<CSwitchInput> twinJoyCrouch;
  
  // Analog joystick
  std::shared_ptr<CAxisInput> analogJoyX;
  std::shared_ptr<CAxisInput> analogJoyY;
  std::shared_ptr<CSwitchInput> analogJoyTrigger1;
  std::shared_ptr<CSwitchInput> analogJoyTrigger2;
  std::shared_ptr<CSwitchInput> analogJoyEvent1;
  std::shared_ptr<CSwitchInput> analogJoyEvent2;

  // Light gun controls (players 1 and 2)
  std::shared_ptr<CAxisInput> gunX[2];
  std::shared_ptr<CAxisInput> gunY[2];
  std::shared_ptr<CTriggerInput> trigger[2];
  
  // Analog gun controls (players 1 and 2)
  std::shared_ptr<CAxisInput> analogGunX[2];
  std::shared_ptr<CAxisInput> analogGunY[2];
  std::shared_ptr<CSwitchInput> analogTriggerLeft[2];
  std::shared_ptr<CSwitchInput> analogTriggerRight[2];

  // Ski Champ controls
  std::shared_ptr<CAxisInput> skiX;
  std::shared_ptr<CAxisInput> skiY;
  std::shared_ptr<CSwitchInput> skiPollLeft;
  std::shared_ptr<CSwitchInput> skiPollRight;
  std::shared_ptr<CSwitchInput> skiSelect1;
  std::shared_ptr<CSwitchInput> skiSelect2;
  std::shared_ptr<CSwitchInput> skiSelect3;

  // Magical Truck Adventure controls
  std::shared_ptr<CAxisInput> magicalLever1;
  std::shared_ptr<CAxisInput> magicalLever2;
  std::shared_ptr<CSwitchInput> magicalPedal1;
  std::shared_ptr<CSwitchInput> magicalPedal2;

  // Sega Bass Fishing controls
  std::shared_ptr<CAxisInput> fishingRodX;
  std::shared_ptr<CAxisInput> fishingRodY;
  std::shared_ptr<CAnalogInput> fishingReel;
  std::shared_ptr<CAxisInput> fishingStickX;
  std::shared_ptr<CAxisInput> fishingStickY;
  std::shared_ptr<CSwitchInput> fishingCast;
  std::shared_ptr<CSwitchInput> fishingSelect;
  std::shared_ptr<CAnalogInput> fishingTension;

  /*
   * Creates a set of inputs with the given input system.
   */
  CInputs(std::shared_ptr<CInputSystem> system);
  
  /*
   * CInputs destructor.
   */
  ~CInputs();

  /*
   * Returns the number of available inputs.
   */
  unsigned Count();

  /*
   * Returns the input with the given index.
   */
  std::shared_ptr<CInput> operator[](const unsigned index);

  /*
   * Returns the input with the given id or label.
   */
  std::shared_ptr<CInput> operator[](const char *idOrLabel);

  /*
   * Returns the assigned input system.
   */
  std::shared_ptr<CInputSystem> GetInputSystem();

  /*
   * Initializes the inputs.  Must be called before any other methods are used.
   */ 
  bool Initialize();

  /*
   * Loads the input mapping assignments from the given config object.
   */
  void LoadFromConfig(const Util::Config::Node &config);

  /*
   * Adjusts input value ranges that differ from the defaults for a particular game.
   */
  void ApplyGameSpecificInputRanges(const Game &game);

  /*
   * Selects steering smoothing: 0 = off, 1 = light, 2 = medium, 3 = strong.
   * Called from LoadFromConfig() and from the main loop so an in-game change is live.
   */
  void SetGamepadSteeringSmoothing(int preset);
  
  /*
   * Stores the current input mapping assignments to the given config object.
   */
  void StoreToConfig(Util::Config::Node *config);

  /*
   * Stops game inputs from being polled, leaving UI inputs (the overlay's own key, pause, reset)
   * working.
   *
   * Used while the in-game settings panel is open: its sliders and combo boxes are driven with the
   * mouse and keyboard, and without this every adjustment would also be fed to the game - dragging a
   * slider would steer the car and clicking a list would fire a light gun.
   *
   * The inputs are not zeroed, only left unpolled, so they resume from the physical state on close.
   */
  void SetGameInputSuppressed(bool suppressed);

  /*
   * Configures the current input mapping assignments for the given game, or all inputs if game is NULL, by asking the user for input.
   * Returns true if the inputs were configured okay or false if the user exited without requesting to save changes.
   */
  bool ConfigureInputs(const Game &game);
  
  /*
   * Configures the current input mapping assignments for the given game, or all inputs if game is NULL, by asking the user for input.
   * Takes display geometry if this has not been set previously by a call to Poll().
   * Returns true if the inputs were configured okay or false if the user exited without requesting to save changes.
   */
  bool ConfigureInputs(const Game &game, unsigned dispX, unsigned dispY, unsigned dispW, unsigned dispH);

  std::vector<std::shared_ptr<CInput>> GetGameInputs(const Game& game);

  void CalibrateJoysticks();

  void CalibrateJoystick(int joyNum);

  /*
   * Prints to stdout the current input mapping assignments for the given game, or all inputs if game is NULL.
   */
  void PrintInputs(const Game *game);

  /*
   * Polls (updates) the inputs for the given game, or all inputs if game is NULL, updating their values from their respective input sources.
   * First the input system is polled (CInputSystem.Poll()) and then each input is polled (CInput.Poll()).
   */
  bool Poll(const Game *game, unsigned dispX, unsigned dispY, unsigned dispW, unsigned dispH);

  /*
   * Re-samples the pointer as late as possible and re-polls the inputs that follow it.
   *
   * Called immediately before the frame is emulated. Poll() already ran, but everything between
   * it and the frame boundary - building the in-game panel, recording the render commands - costs
   * time that a light gun shows up as lag. Only the pointer-following axes are re-polled, so no
   * button edge can be seen twice and no switch state can change; the panel being open still
   * suppresses them exactly as Poll() does.
   */
  void PollPointerLate(const Game *game);

  /*
   * Re-samples every game input immediately before the emulation step.
   *
   * Poll() runs at the top of the frame, and everything between it and the step - building the
   * in-game panel, recording the renderer's command lists - delays the values the emulated CPUs
   * actually see. This refreshes the device state and re-polls the inputs the running game uses at
   * the boundary, so a wheel or a pad is sampled as late as the frame allows. UI inputs are left
   * alone: their edges belong to the top of the frame, where the panel and the hotkeys read them.
   * While the panel is open nothing is re-polled, exactly as in Poll().
   */
  void PollGameInputsLate(const Game *game);

  /*
   * Prints the current values of the inputs for the given game, or all inputs if game is NULL, to stdout for debugging purposes.
   */
  void DumpState(const Game *game);
};

#endif  // INCLUDED_INPUTS_H
