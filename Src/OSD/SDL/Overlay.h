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
#ifndef _OVERLAY_H_
#define _OVERLAY_H_

#if !defined(SUPERMODEL_OPENGL_ONLY)
#include "Graphics/Vulkan3D/VulkanCommon.h"
#endif

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace Util
{
  namespace Config
  {
    class Node;
  }
}

namespace Vulkan3D
{
  class CVulkanContext;
}

namespace OSD
{
namespace Platform
{
struct Window;
}
}

/*
 * In-game settings overlay.
 *
 * A panel of the renderer's post-processing controls drawn over the running game, so a CRT filter can
 * be tuned by watching the picture change instead of editing a value, quitting, relaunching and
 * looking. Every control takes effect on the next frame; there is nothing to apply for the effect to
 * be visible, which is the whole point - Apply and Cancel only decide whether the values are kept.
 *
 * It is drawn by the game's own renderer rather than in its own window, because the game can run
 * exclusive fullscreen where no other window is visible. That also means it cannot reuse the settings
 * window's ImGui setup: this owns a second ImGui context, and its input is fed by hand.
 *
 * Opened and closed with Tab (see CInputs::uiToggleOverlay).
 *
 * Typical frame, from the game loop:
 *
 *     if (Inputs->uiToggleOverlay->Pressed())
 *         OverlayToggle(s_runtime_config);
 *     bool save = OverlayNewFrame(s_runtime_config);   // builds the panel
 *     ...
 *     // and, from inside the renderer while the swapchain is a colour attachment:
 *     OverlayRender(cmd);
 */

/*
 * Creates the ImGui context and the Vulkan backend for the overlay.
 *
 * Safe to call more than once: a change in the swapchain (a resize, a fullscreen toggle) recreates
 * what the backend needs, and this detects that and redoes the work. Returns false when the overlay
 * is unavailable, in which case the game runs without it - it is a convenience, not something to fail
 * a launch over.
 */
#if !defined(SUPERMODEL_OPENGL_ONLY)
bool OverlayInit(Vulkan3D::CVulkanContext& context, OSD::Platform::Window* window, float uiScale);
#endif

/*
 * Creates the OpenGL backend for the overlay, for the New 3D and Legacy 3D renderers.
 *
 * Same ImGui context, same panel, same statistics readout - only the way the finished draw data
 * reaches the screen differs, and the OpenGL route is the simpler of the two: there is no swapchain to
 * own, so the draw data goes into whatever framebuffer is bound when the game's frame is complete,
 * immediately before the buffer swap.
 *
 * Called *instead of* OverlayInit(), not in addition to it: a session uses one backend or the other.
 * Must be called with the game window's OpenGL context already current.
 */
bool OverlayInitGL(OSD::Platform::Window* window, float uiScale);

void OverlayShutdown();

bool OverlayVisible();

/*
 * True when a backend is up, whichever one it is.
 *
 * The distinction matters because this used to be "is Vulkan running": the panel and the readout were
 * tied to the Vulkan renderer, so on a machine whose driver was too old for it they were unavailable
 * with nothing said, and a setting the user had ticked silently did nothing. Callers that need to know
 * whether a frame must be built should ask this rather than testing for a Vulkan context.
 */
bool OverlayActive();

/*
 * Shows or hides the panel. Opening takes a snapshot of the values it can edit so that Cancel has
 * something to go back to; closing is just the panel going away, leaving whatever is on screen in
 * place (the changes are already live, and reverting them is what Cancel is for).
 */
void OverlaySetVisible(Util::Config::Node& config, bool visible);
void OverlayToggle(Util::Config::Node& config);

/*
 * Builds the panel for this frame and feeds it input. Call once per frame; does nothing when the
 * panel is closed.
 *
 * Returns true when the user pressed Apply, meaning the values should be written to the settings
 * file. The caller owns that because it owns the file and the sections around the global one.
 */
bool OverlayNewFrame(Util::Config::Node& config);

/*
 * In-game save-state menu.
 *
 * The emulator owns the save files and the thread-pause protocol, so the overlay only draws the list
 * and calls back. The game loop registers the handlers for the duration of a session and clears them
 * on the way out; when they are not set (the settings window), the section is not drawn at all.
 */
struct OverlaySaveSlot
{
  unsigned slot = 0;
  bool exists = false;
  bool active = false;        // the slot F5/F7 currently use
  double ageSeconds = 0.0;    // age of the file; 0 when it does not exist

  /*
   * The file the slot was found in and a revision that changes when it is rewritten. The overlay
   * reads thumb.png from this path only when the row is hovered, and keeps the uploaded texture
   * until the revision changes; passing the path through the callback is what lets the UI stay
   * independent of the container format and of HostCore.
   */
  std::filesystem::path path;
  std::uint64_t revision = 0;
};

struct OverlaySaveHandlers
{
  std::function<std::vector<OverlaySaveSlot>()> list;
  std::function<void(unsigned slot, bool save)> apply;   // save = true, load = false
};

void OverlaySetSaveHandlers(OverlaySaveHandlers handlers);
void OverlayClearSaveHandlers();

/*
 * Records the panel into the frame.
 *
 * Must be called with a rendering scope already open on the swapchain image, since the ImGui backend
 * only issues draw commands. The renderer wraps this in its own scope; see CVulkan2D::OverlayPass.
 */
#if !defined(SUPERMODEL_OPENGL_ONLY)
void OverlayRender(VkCommandBuffer cmd);

/*
 * Blurred copy of the frame behind the panel, published by the game's renderer.
 *
 * The panel draws its own background instead of a flat fill, and that background is the frame the
 * player is looking at, blurred. Producing it needs the swapchain, so it is the renderer's job; the
 * overlay only samples the result.
 *
 * `generation` changes whenever the image is replaced. Comparing it rather than the view handle
 * matters because the allocator reuses addresses: a resize can hand back the same VkImageView while
 * it names different memory.
 */
struct OverlayFrostTexture
{
  VkImageView view = VK_NULL_HANDLE;
  std::uint32_t generation = 0;
};

/*
 * Installed once per session by the game loop, and called once per built frame. Returning an empty
 * texture (a null view) makes the panel fall back to a solid tint.
 */
using OverlayFrostTextureFn = OverlayFrostTexture (*)();
void OverlaySetFrostTextureSource(OverlayFrostTextureFn fn);
#endif

/*
 * True when the panel is drawing frosted glass this frame, so the renderer should run its blur chain.
 *
 * Asked per frame rather than set on open and close, because the panel can also be dismissed by its
 * own Close button and the config option can be switched while it is open. A false answer is what
 * keeps a session that never opens the panel at exactly zero cost.
 */
bool OverlayWantsFrost();

/*
 * Records the panel into the currently bound framebuffer, for the OpenGL renderers.
 *
 * Call with the game's OpenGL context current, after the game has finished drawing the frame and
 * before the buffer swap. Nothing has to be set up first: the OpenGL backend draws into the default
 * framebuffer, so unlike the Vulkan path there is no scope for the caller to open.
 */
void OverlayRenderGL();

#endif // !_OVERLAY_H_
