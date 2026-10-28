#ifndef INCLUDED_IRENDER3D_H
#define INCLUDED_IRENDER3D_H

#include <cstdint>
#include "Types.h"

/*
 * IRender3D:
 *
 * Interface (abstract base class) for Real3D rendering engine.
 */
class IRender3D
{
public:
  virtual void RenderFrame(void) = 0;

  /*
   * PrepareFrame(void):
   *
   * Builds the renderer's CPU-side view of the simulation state for the frame
   * about to be presented: scene traversal, draw list, material table and the
   * previous/current motion pair. Called once per emulated frame, before the
   * swapchain image is acquired, and performs no GPU work.
   *
   * Renderers that rebuild inside RenderFrame() can leave this empty.
   */
  virtual void PrepareFrame(void) {}

  /*
   * Discards the previous/current motion pair. Called when the emulation state
   * is reset or loaded: interpolating across a state discontinuity would show a
   * camera flying between two unrelated scenes.
   */
  virtual void ResetInterpolation() {}

  /*
   * RenderFrame(alpha, newSimulation):
   *
   * Presents one frame. alpha selects between the previous and current
   * simulation state; newSimulation says PrepareFrame() has new state to upload.
   * When newSimulation is false the renderer must replay its existing geometry,
   * textures and materials and only update the per-instance matrices.
   */
  virtual void RenderFrame(double alpha, bool newSimulation)
  {
    (void)alpha;
    (void)newSimulation;
    RenderFrame();
  }

  virtual void BeginFrame(void) = 0;
  virtual void EndFrame(void) = 0;
  virtual void UploadTextures(unsigned level, unsigned x, unsigned y, unsigned width, unsigned height) = 0;
  virtual void AttachMemory(const uint32_t *cullingRAMLoPtr, const uint32_t *cullingRAMHiPtr, const uint32_t *polyRAMPtr, const uint32_t *vromPtr, const uint16_t *textureRAMPtr) = 0;
  virtual void SetStepping(int stepping) = 0;
  virtual Result Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned aaTarget) = 0;
  virtual void SetSunClamp(bool enable) = 0;
  virtual void SetBlockCulling(bool enable) = 0;
  virtual float GetLosValue(int layer) = 0;

  virtual ~IRender3D()
  {
  }
};

#endif  // INCLUDED_IRENDER3D_H
