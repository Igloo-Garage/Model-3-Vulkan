/**
 ** Model 3-Vulkan
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan, and is derived from Supermodel,
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
 * ImguiPlatform.h
 *
 * A small ImGui platform backend that consumes OSD::Platform events instead
 * of SDL_Event.  The renderer backend (OpenGL3 or Vulkan) still comes from
 * upstream ImGui; only the window/input half is ours, which is what lets the
 * same Gui.cpp compile against SDL2 and SDL3.
 */

#ifndef INCLUDED_IMGUIPLATFORM_H
#define INCLUDED_IMGUIPLATFORM_H

#include "OSD/Platform/Platform.h"
#include "OSD/Platform/PlatformInput.h"

namespace OSD
{
namespace Platform
{

/*
 * Call after ImGui::CreateContext() and before the renderer backend's Init().
 * The context is already current.
 */
bool ImGuiPlatformInit(Window* window);

/*
 * Call before ImGui::DestroyContext().  Does not destroy the window.
 */
void ImGuiPlatformShutdown();

/*
 * Call immediately before ImGui::NewFrame(), after the renderer backend's
 * NewFrame().  Updates DisplaySize, framebuffer scale and DeltaTime.
 */
void ImGuiPlatformNewFrame(Window* window);

/*
 * Translate one neutral event into ImGuiIO calls.  Returns true when the event
 * produced input; the caller may still forward the same event to game input.
 */
bool ImGuiPlatformProcessEvent(const Event& event);

} // Platform
} // OSD

#endif // INCLUDED_IMGUIPLATFORM_H
