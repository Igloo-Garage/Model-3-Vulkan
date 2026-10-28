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
 * PlatformVulkan.h
 *
 * The only place where window creation meets Vulkan.  The renderer sees
 * opaque handles and never includes SDL_vulkan.h; the backend owns the
 * SDL2/SDL3 difference:
 *
 *   SDL2: SDL_Vulkan_GetInstanceExtensions(window, &count, names)
 *         SDL_Vulkan_CreateSurface(window, instance, &surface)
 *
 *   SDL3: SDL_Vulkan_GetInstanceExtensions(&count)
 *         SDL_Vulkan_CreateSurface(window, instance, allocator, &surface)
 *
 * The surface is deliberately passed as uint64_t rather than VkSurfaceKHR so
 * this header can be included by files that do not have Vulkan headers yet.
 * VkSurfaceKHR is a uint64_t handle on every 64-bit platform this project
 * builds for, and the implementation static_asserts that.
 */

#ifndef INCLUDED_PLATFORMVULKAN_H
#define INCLUDED_PLATFORMVULKAN_H

#include "Platform.h"

#include <cstdint>
#include <vector>

namespace OSD
{
namespace Platform
{

/*
 * Appends the instance extensions required by the window system to the given
 * vector.  The caller owns the storage, which avoids a lifetime question:
 * SDL2 fills a caller-provided array while SDL3 returns an internal one, and
 * the two cannot share a borrowed-pointer API safely.
 *
 * Called once, while creating VkInstance.
 */
bool GetVulkanInstanceExtensions(Window* window, std::vector<const char*>& extensions);

/*
 * Creates the presentation surface for a window.  instance is a VkInstance
 * passed as void* because this header does not include Vulkan.  Returns
 * false and leaves *surface untouched on failure; GetError() describes why.
 */
bool CreateVulkanSurface(Window* window, void* instance, uint64_t* surface);

/*
 * Destroys a surface created above.  SDL3 performs additional bookkeeping
 * that vkDestroySurfaceKHR does not know about, so the renderer must go
 * through this function on both backends rather than calling Vulkan directly.
 */
void DestroyVulkanSurface(void* instance, uint64_t surface);

} // Platform
} // OSD

#endif // INCLUDED_PLATFORMVULKAN_H
