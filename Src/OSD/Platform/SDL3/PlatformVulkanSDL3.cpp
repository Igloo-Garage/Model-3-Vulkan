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
 * PlatformVulkanSDL3.cpp
 *
 * The only SDL3 file that knows about SDL_vulkan.h.
 */

#include "PlatformSDL3Internal.h"
#include "OSD/Platform/PlatformVulkan.h"

#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>

static_assert(sizeof(VkSurfaceKHR) <= sizeof(uint64_t),
    "PlatformVulkan.h passes VkSurfaceKHR as uint64_t");

namespace OSD
{
namespace Platform
{

bool GetVulkanInstanceExtensions(Window* window, std::vector<const char*>& extensions)
{
    (void)window;

    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (names == nullptr)
        return false;

    extensions.insert(extensions.end(), names, names + count);
    return true;
}

bool CreateVulkanSurface(Window* window, void* instance, uint64_t* surface)
{
    if (!IsWindowValid(window) || instance == nullptr || surface == nullptr)
        return false;

    VkSurfaceKHR created = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window->sdl, static_cast<VkInstance>(instance), nullptr, &created))
        return false;

    uint64_t bits = 0;
    std::memcpy(&bits, &created, sizeof(created));
    *surface = bits;
    return true;
}

void DestroyVulkanSurface(void* instance, uint64_t surface)
{
    if (instance == nullptr || surface == 0)
        return;

    VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
    std::memcpy(&vkSurface, &surface, sizeof(vkSurface));
    SDL_Vulkan_DestroySurface(static_cast<VkInstance>(instance), vkSurface, nullptr);
}

} // Platform
} // OSD
