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
 * IEmulator.h
 * 
 * Header file defining the emulator interface, IEmulator.
 */

#ifndef INCLUDED_IEMULATOR_H
#define INCLUDED_IEMULATOR_H

#include <string>

class CBlockFile;
struct Game;
struct ROMSet;
class IRender2D;
class IRender3D;
class CInputs;
class COutputs;
class SuperAA;

/*
 * IEmulator:
 *
 * Interface (abstract base class) for emulation.
 */
class IEmulator
{
public:
  /*
   * SaveState(SaveState):
   *
   * Saves an image of the current state. Must never be called while emulator
   * is running (i.e., inside RunFrame()).
   *
   * Parameters:
   *    SaveState   Block file to save state information to.
   */
  virtual void SaveState(CBlockFile *SaveState) = 0;

  /*
   * LoadState(SaveState):
   *
   * Loads and resumes execution from a state image. Modifies data that may
   * be used by multiple threads -- use with caution and ensure threads are
   * not accessing data that will be touched, this can be done by calling
   * PauseThreads beforehand. Must never be called while emulator is running
   * (inside RunFrame()).
   *
   * Parameters:
   *    SaveState   Block file to load state information from.
   */
  virtual void LoadState(CBlockFile *SaveState) = 0;

  /*
   * DebugSnapshot(tag):
   *
   * Read-only diagnostic hook used by the optional SM3D_LOAD_DEBUG watchdog in the SDL front end.
   * Returns one line describing the state that matters after a load; the default is empty for
   * emulators that do not implement it. Must not modify any state.
   */
  virtual std::string DebugSnapshot(const char *tag)
  {
    (void)tag;
    return std::string();
  }
  
  /*
   * SaveNVRAM(NVRAM):
   *
   * Saves an image of the current non-volatile memory state.
   *
   * Parameters:
   *    NVRAM   Block file to save NVRAM to.
   */
  virtual void SaveNVRAM(CBlockFile *NVRAM) = 0;

  /*
   * LoadNVRAM(NVRAM):
   *
   * Loads a non-volatile memory state.
   *
   * Parameters:
   *    NVRAM   Block file to load NVRAM state from.
   */
  virtual void LoadNVRAM(CBlockFile *NVRAM) = 0;
  
  /*
   * ClearNVRAM(void):
   *
   * Clears all non-volatile memory.
   */
  virtual void ClearNVRAM(void) = 0;
  
  /*
   * RunFrame(void):
   *
   * Runs one video frame: simulate it and render it. Kept for the modal-loop
   * path and for callers that still want the two halves together.
   */
  virtual void RunFrame(void) = 0;

  /*
   * RunSimulationFrame(void):
   *
   * Runs one video frame of emulation without touching the swapchain. The
   * high-refresh loop calls this when the fixed-step accumulator releases a
   * step, then presents as many times as the display asks for.
   *
   * The default is empty: an emulator that has nothing to simulate (the
   * graphics-state viewer) still renders, it just has no clock to advance.
   */
  virtual void RunSimulationFrame(void) {}

  /*
   * Split form of RunSimulationFrame() for the high-refresh loop.
   *
   * BeginSimulationFrame() starts one emulated frame and returns; the PPC, sound
   * and drive threads run while the renderer presents the previous snapshot.
   * EndSimulationFrame() waits for that work to finish, publishes the read-only
   * snapshots and marks the scene dirty. RunSimulationFrame() is the blocking
   * combination of the two.
   *
   * An emulator with no background work can implement Begin as the whole frame
   * and End as a no-op.
   */
  virtual void BeginSimulationFrame() {}
  virtual void EndSimulationFrame() {}

  /*
   * Traverses the scene database of the step that just ended into the renderer's
   * current snapshot.
   *
   * Called between EndSimulationFrame() and BeginSimulationFrame(), which is the
   * only point at which no emulated hardware is writing to the scene memory:
   * after Begin, the CPU is already building the next frame. Building the scene
   * there is what keeps a snapshot from mixing a completed frame with a
   * half-written one.
   *
   * The default does nothing, for emulators whose renderer has no such split.
   */
  virtual void PrepareSceneSnapshot() {}

  /*
   * RenderFrame(void):
   *
   * Renders one video frame from the current state.
   */
  virtual void RenderFrame(void) = 0;

  /*
   * RenderFrame(alpha, newSimulation):
   *
   * Renders one presentation frame. alpha is the fraction of a simulation
   * step since the last one (0 = previous state, 1 = current); it is ignored
   * by renderers that cannot interpolate. newSimulation says the emulation
   * state advanced since the previous call, so scene building, texture
   * synchronisation and dynamic geometry uploads belong to this call only.
   */
  virtual void RenderFrame(double alpha, bool newSimulation)
  {
    (void)alpha;
    (void)newSimulation;
    RenderFrame();
  }

  /*
   * Reset(void):
   *
   * Resets the system. Does not modify non-volatile memory.
   */
  virtual void Reset(void) = 0;

  /* 
   * GetGame(void):
   *
   * Returns:
   *    A reference to the presently loaded game's information structure (which
   *    will be blank if no game has yet been loaded).
   */
  virtual const Game &GetGame(void) const = 0;

  /*
   * LoadGame(game, rom_set):
   *
   * Loads a game, copying in the provided ROMs and setting the hardware
   * stepping.
   *
    * Parameters:
   *    game      Game information.
   *    rom_set   ROMs.
   *
   * Returns:
   *    OKAY if successful, FAIL otherwise. Prints errors.
   */
  virtual Result LoadGame(const Game &game, const ROMSet &rom_set) = 0;
  
  /*
   * AttachRenderers(Render2DPtr, Render3DPtr):
   *
   * Attaches the renderers to the appropriate device objects.
   *
   * Parameters:
   *    Render2DPtr   Pointer to a tile renderer object.
   *    Render3DPtr   Same as above but for a 3D renderer.
   */
  virtual void AttachRenderers(IRender2D *Render2DPtr, IRender3D *Render3DPtr, SuperAA *superAA) = 0;
  
  /*
   * AttachInputs(InputsPtr):
   *
   * Attaches OSD-managed inputs.
   *
   * Parameters:
   *    InputsPtr Pointer to the object containing input states.
   */
  virtual void AttachInputs(CInputs *InputsPtr) = 0;
  
  /*
   * AttachOutputs(InputsPtr):
   *
   * Attaches OSD-managed outputs (cabinet lamps, etc.)
   *
   * Parameters:
   *    OutputsPtr  Pointer to the object containing output states.
   */
  virtual void AttachOutputs(COutputs *OutputsPtr) = 0;

  /*
   * Init(void):
   *
   * One-time initialization of the context. Must be called prior to all
   * other members.
   *
   * NOTE: Command line settings will not have been applied here yet.
   *
   * Returns:
   *    OKAY is successful, otherwise FAILED if a non-recoverable error
   *    occurred. Prints own error messages.
   */
  virtual Result Init(void) = 0;

  /*
   * PauseThreads(void):
   *
   * Flags that any running threads should pause and waits for them to do so.
   * Should be used before invoking any method that accesses the internal 
   * state, eg LoadState or SaveState.
   */
  virtual bool PauseThreads(void) = 0;

  /*
   * ResumeThreads(void):
   *
   * Flags that any paused threads should resume running.
   */
  virtual bool ResumeThreads(void) = 0;

  /*
   * ~IEmulator(void):
   *
   * Destructor. Must free all resources.
   */
  
  virtual ~IEmulator(void)
  {
  }
};

#endif  // INCLUDED_IEMULATOR_H
