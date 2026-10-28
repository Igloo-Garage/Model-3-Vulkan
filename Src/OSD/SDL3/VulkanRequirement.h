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
 * VulkanRequirement.h
 *
 * The Vulkan 1.3 gate for the SDL3 build.
 *
 * This runs before a ROM is loaded and before the main window exists.  It
 * answers one question - can this machine run the Pure-Vulkan pipeline at all -
 * and returns the reason as data rather than a bool, so the caller cannot
 * forget to handle it.
 *
 * Only the SDL3 executable is gated.  The SDL2 build deliberately keeps the
 * Vulkan 1.2 + VK_KHR_dynamic_rendering/VK_KHR_synchronization2 fallback that
 * CVulkanContext already implements.
 */

#ifndef INCLUDED_OSD_SDL3_VULKANREQUIREMENT_H
#define INCLUDED_OSD_SDL3_VULKANREQUIREMENT_H

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace OSD
{
namespace SDL3App
{

enum class VulkanRequirement
{
    LoaderUnavailable,      // vulkan-1.dll / libvulkan could not be loaded
    InstanceFailed,         // a Vulkan 1.3 instance could not be created
    NoPhysicalDevice,       // no non-software device at all
    ApiVersionTooLow,       // a device exists, but it reports < 1.3
    MissingFeature,         // 1.3, but dynamicRendering or synchronization2 is absent
    NoGraphicsQueue,        // 1.3 and the features, but no graphics queue family
};

struct VulkanRequirementError
{
    VulkanRequirement reason = VulkanRequirement::NoPhysicalDevice;
    std::string detail;             // English, shown to the user and printed
    std::string deviceName;         // the most capable device that was examined
    std::uint32_t apiVersion = 0;   // that device's apiVersion
};

struct VulkanProbe
{
    std::string deviceName;
    std::uint32_t apiVersion = 0;
    std::uint32_t apiMajor = 0;
    std::uint32_t apiMinor = 0;
    // Every non-software device the probe saw, in enumeration order.  The
    // settings window's device picker uses this instead of creating a second
    // throw-away Vulkan instance on its first frame (which costs about a second
    // on a machine with overlay layers installed).
    std::vector<std::string> deviceNames;
};

/*
 * Loads the Vulkan loader through volk, creates a throw-away 1.3 instance and
 * checks every non-software physical device for:
 *
 *   apiVersion >= VK_API_VERSION_1_3
 *   VkPhysicalDeviceVulkan13Features::dynamicRendering
 *   VkPhysicalDeviceVulkan13Features::synchronization2
 *   at least one VK_QUEUE_GRAPHICS_BIT family
 *
 * The instance is destroyed before returning, so the renderer creates its own
 * instance later exactly as it does today.
 */
std::expected<VulkanProbe, VulkanRequirementError> ProbeVulkan13();

/*
 * The device names from the last successful ProbeVulkan13() call, for the
 * settings window.  Empty until the probe has run, in which case the caller
 * falls back to its own enumeration.
 */
const std::vector<std::string>& KnownVulkanDeviceNames();

/*
 * Prints the failure (with a stack trace) to stderr and then shows a modal SDL
 * message box telling the user to update the driver or run the SDL2 build.
 * Safe to call before SDL_Init() and before any window exists.
 */
void ReportVulkanRequirementFailure(const VulkanRequirementError& error);

/*
 * General startup error box for the other fatal checks in this layer.
 */
void ShowFatalError(std::string_view title, std::string_view message);

} // SDL3App
} // OSD

#endif // INCLUDED_OSD_SDL3_VULKANREQUIREMENT_H
