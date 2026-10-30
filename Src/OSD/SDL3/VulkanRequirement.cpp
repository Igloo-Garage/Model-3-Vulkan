/**
 ** Model 3-Vulkan
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
 **
 ** Additional Terms under GNU GPLv3 Section 7 apply to this file
 ** (see LICENSE-ADDITIONAL-TERMS.md).
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
 * VulkanRequirement.cpp
 *
 * See VulkanRequirement.h.  This file is part of the C++23 SDL3 target:
 * std::expected models success/failure, <print> writes type-safe diagnostics
 * and <stacktrace> records where a fatal startup check stopped.
 */

#define VK_NO_PROTOTYPES
#include "volk/volk.h"

#include "OSD/SDL3/VulkanRequirement.h"
#include "OSD/SDL3/StartupLog.h"

#include "OSD/FileSystemPath.h"

#include <SDL3/SDL_messagebox.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#if defined(_WIN32) && __has_include(<stacktrace>)
#include <stacktrace>
#define SUPERMODEL_HAS_STACKTRACE 1
#else
// libc++ on macOS does not provide <stacktrace>; libstdc++ on Linux does, but
// its symbols live in libstdc++_libbacktrace, which this target does not link.
#define SUPERMODEL_HAS_STACKTRACE 0
#endif
#include <string>
#include <utility>
#include <vector>

namespace OSD
{
namespace SDL3App
{

namespace
{
// Filled by ProbeVulkan13(); read by the settings window so it does not have to
// create another Vulkan instance just to list the devices.
std::vector<std::string> g_knownDeviceNames;
}

namespace
{

/*
 * Owns the throw-away instance.  Destruction has to go through the
 * instance-level function pointer because volk's global instance table is not
 * loaded here (the renderer does that with its own instance later).
 */
struct InstanceOwner
{
    VkInstance instance = VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroy = nullptr;

    ~InstanceOwner()
    {
        if (instance != VK_NULL_HANDLE && destroy != nullptr)
            destroy(instance, nullptr);
    }
};

bool HasGraphicsQueue(
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilies,
    VkPhysicalDevice device)
{
    uint32_t count = 0;
    getQueueFamilies(device, &count, nullptr);
    if (count == 0)
        return false;

    std::vector<VkQueueFamilyProperties> families(count);
    getQueueFamilies(device, &count, families.data());
    return std::any_of(families.begin(), families.end(),
        [](const VkQueueFamilyProperties& family)
        {
            return (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        });
}

#if defined(__APPLE__)
/*
 * Whether the implementation advertises an instance extension.
 *
 * On macOS VK_KHR_portability_enumeration belongs to the Vulkan loader, which adds it when an ICD
 * declares itself a portability driver. volk opens Frameworks/libMoltenVK.dylib directly here, and
 * MoltenVK does not list the extension itself, so requesting it unconditionally made
 * vkCreateInstance fail with VK_ERROR_EXTENSION_NOT_PRESENT (-7) on every Mac.
 */
bool HasInstanceExtension(const char* name)
{
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
        return false;

    std::vector<VkExtensionProperties> properties(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, properties.data()) != VK_SUCCESS)
        return false;

    return std::any_of(properties.begin(), properties.end(),
        [name](const VkExtensionProperties& property)
        {
            return std::strcmp(property.extensionName, name) == 0;
        });
}
#endif

/*
 * Device-side counterpart of the check above. The instance-level volk table is not loaded at this
 * point - ProbeVulkan13() deliberately keeps its throw-away instance separate from the renderer's -
 * so the entry point is passed in from there.
 */
bool HasDeviceExtension(PFN_vkEnumerateDeviceExtensionProperties enumerate,
    VkPhysicalDevice device,
    const char* name)
{
    if (enumerate == nullptr)
        return false;

    uint32_t count = 0;
    if (enumerate(device, nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
        return false;

    std::vector<VkExtensionProperties> properties(count);
    if (enumerate(device, nullptr, &count, properties.data()) != VK_SUCCESS)
        return false;

    return std::any_of(properties.begin(), properties.end(),
        [name](const VkExtensionProperties& property)
        {
            return std::strcmp(property.extensionName, name) == 0;
        });
}

int DeviceScore(const VkPhysicalDeviceProperties& props)
{
    switch (props.deviceType)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 1000;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 100;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 50;
    default:                                     return 10;
    }
}

/*
 * Software rasterisers and the D3D12 mapping layer are not "the GPU" the user
 * launched the performance build for.  Same filter as CVulkanContext uses for
 * its device list.
 */
bool IsRealDevice(const VkPhysicalDeviceProperties& props)
{
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
        return false;

    static const char d3d12Prefix[] = "Microsoft Direct3D12";
    return std::strncmp(props.deviceName, d3d12Prefix, sizeof(d3d12Prefix) - 1) != 0;
}

/*
 * The normal logger does not exist yet at this point in startup, and when it
 * does open the file it truncates it.  A fatal check therefore appends its
 * record directly; the process exits before any logger can overwrite it.
 */
void AppendToStartupLog(const std::string& text)
{
    std::error_code error;
    const std::filesystem::path logPath =
        std::filesystem::path(FileSystemPath::GetPath(FileSystemPath::Log)) / "Supermodel.log";

    std::ofstream out(logPath, std::ios::app);
    if (out)
        out << text;
}

} // namespace

std::expected<VulkanProbe, VulkanRequirementError> ProbeVulkan13()
{
    const auto clock = std::chrono::steady_clock::now();
    const auto ms = [&clock]()
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count();
    };

    if (volkInitialize() != VK_SUCCESS)
    {
        return std::unexpected(VulkanRequirementError{
            VulkanRequirement::LoaderUnavailable,
            "The Vulkan loader (vulkan-1.dll / libvulkan) could not be loaded.",
            {}, 0});
    }

    VkApplicationInfo application = {};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Model 3-Vulkan";
    application.applicationVersion = VK_MAKE_API_VERSION(0, 0, 3, 0);
    application.pEngineName = "Model 3-Vulkan";
    application.engineVersion = application.applicationVersion;
    application.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &application;

#if defined(__APPLE__)
    /*
     * Ask for the portability extension only when the implementation actually has it. It is a
     * loader-provided extension, and the MoltenVK dylib this build opens directly does not
     * advertise it, so requesting it unconditionally failed instance creation with
     * VK_ERROR_EXTENSION_NOT_PRESENT (-7). The flag is only valid together with the extension,
     * so both are conditional.
     */
    static const char* const portabilityExtension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    if (HasInstanceExtension(portabilityExtension))
    {
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = &portabilityExtension;
    }
#endif

    VkInstance instance = VK_NULL_HANDLE;
#if defined(_WIN32)
    /*
     * The preflight asks the driver a hardware question, and implicit overlay layers (Steam,
     * RTSS, ReShade, OBS, ...) cannot answer it - they only add their own load time to every
     * vkCreateInstance call.  Measured on a desktop with six of them registered, that is about a
     * second for this throw-away instance alone.
     *
     * Scoped to the creation call: the renderer creates its own instance later, and the
     * environment is restored before that, so overlays keep working in the game.
     */
    char* previousLayers = nullptr;
    size_t previousLayersSize = 0;
    _dupenv_s(&previousLayers, &previousLayersSize, "VK_LOADER_LAYERS_DISABLE");
    _putenv_s("VK_LOADER_LAYERS_DISABLE", "*");
#endif
    const VkResult createResult = vkCreateInstance(&createInfo, nullptr, &instance);
#if defined(_WIN32)
    _putenv_s("VK_LOADER_LAYERS_DISABLE", previousLayers != nullptr ? previousLayers : "");
    free(previousLayers);
#endif
    if (createResult != VK_SUCCESS)
    {
        return std::unexpected(VulkanRequirementError{
            VulkanRequirement::InstanceFailed,
            std::format("vkCreateInstance failed for a Vulkan 1.3 instance (VkResult {}).",
                static_cast<int>(createResult)),
            {}, 0});
    }

    OSD::SDL3App::StartupPrint("Model3-Vulkan: vkCreateInstance done ({:.0f} ms).", ms());

    InstanceOwner owner;
    owner.instance = instance;
    owner.destroy = reinterpret_cast<PFN_vkDestroyInstance>(
        vkGetInstanceProcAddr(instance, "vkDestroyInstance"));

    const auto enumerateDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
    const auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"));
    const auto getFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    const auto getQueueFamilies = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto enumerateDeviceExtensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        vkGetInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties"));

    if (!enumerateDevices || !getProperties || !getFeatures2 || !getQueueFamilies ||
        !enumerateDeviceExtensions || !owner.destroy)
    {
        return std::unexpected(VulkanRequirementError{
            VulkanRequirement::LoaderUnavailable,
            "The Vulkan loader did not provide the entry points needed by the "
            "Vulkan 1.3 preflight check.",
            {}, 0});
    }

    uint32_t deviceCount = 0;
    if (enumerateDevices(instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0)
    {
        return std::unexpected(VulkanRequirementError{
            VulkanRequirement::NoPhysicalDevice,
            "No Vulkan physical device is present.",
            {}, 0});
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (enumerateDevices(instance, &deviceCount, devices.data()) != VK_SUCCESS)
    {
        return std::unexpected(VulkanRequirementError{
            VulkanRequirement::NoPhysicalDevice,
            "The list of Vulkan physical devices could not be read.",
            {}, 0});
    }
    OSD::SDL3App::StartupPrint("Model3-Vulkan: {} physical device(s) enumerated ({:.0f} ms).",
        deviceCount, ms());

    std::optional<VulkanProbe> best;
    int bestScore = -1;
    std::vector<std::string> deviceNames;

    // Why the most capable device was rejected, so the error can name a
    // concrete cause instead of a generic "no suitable GPU".
    std::optional<VulkanRequirementError> rejection;
    int rejectionPriority = -1;

    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties = {};
        getProperties(device, &properties);

        if (!IsRealDevice(properties))
            continue;

        deviceNames.emplace_back(properties.deviceName);

        const auto reject = [&](int priority, VulkanRequirement reason, std::string detail)
        {
            const int score = priority * 1000000 + static_cast<int>(properties.apiVersion);
            if (score > rejectionPriority)
            {
                rejectionPriority = score;
                rejection = VulkanRequirementError{
                    reason,
                    std::move(detail),
                    properties.deviceName,
                    properties.apiVersion};
            }
        };

        /*
         * The renderer accepts two routes (see ClassifyDevice in VulkanContext.cpp): Vulkan 1.3
         * core, or Vulkan 1.2 with the dynamic rendering and synchronization2 extensions. The
         * preflight used to demand 1.3 only, which rejected Intel Macs whose MoltenVK reports 1.2
         * even though the renderer could run there - macOS was the visible casualty.
         */
        const bool api13 = properties.apiVersion >= VK_API_VERSION_1_3;
        const bool api12 = properties.apiVersion >= VK_API_VERSION_1_2;

        if (!api12)
        {
            reject(1, VulkanRequirement::ApiVersionTooLow,
                std::format("'{}' reports Vulkan {}.{}; this build needs Vulkan 1.3, or Vulkan 1.2 "
                    "with the dynamic rendering and synchronization2 extensions.",
                    properties.deviceName,
                    VK_API_VERSION_MAJOR(properties.apiVersion),
                    VK_API_VERSION_MINOR(properties.apiVersion)));
            continue;
        }

        if (api13)
        {
            VkPhysicalDeviceVulkan13Features features13 = {};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

            VkPhysicalDeviceFeatures2 features2 = {};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.pNext = &features13;
            getFeatures2(device, &features2);

            if (!features13.dynamicRendering || !features13.synchronization2)
            {
                std::string missing;
                if (!features13.dynamicRendering)
                    missing += "dynamicRendering";
                if (!features13.synchronization2)
                    missing += missing.empty() ? "synchronization2" : " and synchronization2";

                reject(3, VulkanRequirement::MissingFeature,
                    std::format("'{}' reports Vulkan {}.{} but does not expose {}.",
                        properties.deviceName,
                        VK_API_VERSION_MAJOR(properties.apiVersion),
                        VK_API_VERSION_MINOR(properties.apiVersion),
                        missing));
                continue;
            }
        }
        else
        {
            const bool hasDynamicRendering = HasDeviceExtension(enumerateDeviceExtensions, device,
                VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            const bool hasSynchronization2 = HasDeviceExtension(enumerateDeviceExtensions, device,
                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

            if (!hasDynamicRendering || !hasSynchronization2)
            {
                std::string missing;
                if (!hasDynamicRendering)
                    missing += "VK_KHR_dynamic_rendering";
                if (!hasSynchronization2)
                    missing += missing.empty() ? "VK_KHR_synchronization2"
                                               : " and VK_KHR_synchronization2";

                reject(1, VulkanRequirement::ApiVersionTooLow,
                    std::format("'{}' reports Vulkan {}.{} without {}; this build needs Vulkan 1.3, "
                        "or Vulkan 1.2 with both extensions.",
                        properties.deviceName,
                        VK_API_VERSION_MAJOR(properties.apiVersion),
                        VK_API_VERSION_MINOR(properties.apiVersion),
                        missing));
                continue;
            }

            /*
             * Chaining VkPhysicalDeviceVulkan13Features is not valid on a 1.2 device - the same
             * two booleans come from the extension feature structs, which is exactly what the
             * renderer does when it enables them.
             */
            VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering = {};
            dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
            VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2 = {};
            synchronization2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
            dynamicRendering.pNext = &synchronization2;

            VkPhysicalDeviceFeatures2 features2 = {};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.pNext = &dynamicRendering;
            getFeatures2(device, &features2);

            if (!dynamicRendering.dynamicRendering || !synchronization2.synchronization2)
            {
                std::string missing;
                if (!dynamicRendering.dynamicRendering)
                    missing += "dynamicRendering";
                if (!synchronization2.synchronization2)
                    missing += missing.empty() ? "synchronization2" : " and synchronization2";

                reject(3, VulkanRequirement::MissingFeature,
                    std::format("'{}' reports Vulkan {}.{} but does not expose {}.",
                        properties.deviceName,
                        VK_API_VERSION_MAJOR(properties.apiVersion),
                        VK_API_VERSION_MINOR(properties.apiVersion),
                        missing));
                continue;
            }
        }

        if (!HasGraphicsQueue(getQueueFamilies, device))
        {
            reject(2, VulkanRequirement::NoGraphicsQueue,
                std::format("'{}' has no graphics queue family.", properties.deviceName));
            continue;
        }

        const int score = DeviceScore(properties);
        if (score > bestScore)
        {
            bestScore = score;
            best = VulkanProbe{
                properties.deviceName,
                properties.apiVersion,
                VK_API_VERSION_MAJOR(properties.apiVersion),
                VK_API_VERSION_MINOR(properties.apiVersion)};
        }
    }

    if (!best)
    {
        if (rejection)
            return std::unexpected(*rejection);

        return std::unexpected(VulkanRequirementError{
            VulkanRequirement::NoPhysicalDevice,
            "Only software rasterisers or compatibility layers were found; a real "
            "graphics device with Vulkan 1.3 is required.",
            {}, 0});
    }

    g_knownDeviceNames = deviceNames;
    best->deviceNames = std::move(deviceNames);

    StartupPrint("Model3-Vulkan: Vulkan 1.3 preflight passed: {} (Vulkan {}.{}).",
        best->deviceName, best->apiMajor, best->apiMinor);
    return *best;
}

void ReportVulkanRequirementFailure(const VulkanRequirementError& error)
{
    StartupPrint("Model3-Vulkan: Vulkan 1.3 requirement not met: {}", error.detail);

    if (!error.deviceName.empty())
    {
        StartupPrint("  most capable device: {} (Vulkan {}.{})",
            error.deviceName,
            VK_API_VERSION_MAJOR(error.apiVersion),
            VK_API_VERSION_MINOR(error.apiVersion));
    }

    // Only on this fatal path: a stack trace is useful in a bug report and too
    // expensive to capture during normal startup.
#if SUPERMODEL_HAS_STACKTRACE
    const std::string trace = std::to_string(std::stacktrace::current(0, 16));
#else
    const std::string trace = "(stack traces are not available in this standard library)";
#endif
    StartupPrint("  startup stack trace:\n{}", trace);
    AppendToStartupLog(std::format(
        "[Fatal] Vulkan 1.3 requirement not met: {}\n{}\n", error.detail, trace));

    const std::string detected = error.deviceName.empty()
        ? std::string("No usable Vulkan device was found.")
        : std::format("Detected device: {} (Vulkan {}.{}).",
            error.deviceName,
            VK_API_VERSION_MAJOR(error.apiVersion),
            VK_API_VERSION_MINOR(error.apiVersion));

    /*
     * What to tell the user differs by platform. Windows and Linux have an updatable driver and
     * ship an OpenGL build alongside; macOS carries its own MoltenVK and has no OpenGL executable
     * in the package, so "update the driver" and "use LegacyOG" are both wrong there.
     */
#if defined(__APPLE__)
    static const char* const advice =
        "The macOS package already includes MoltenVK, so there is no driver to update. This usually "
        "means the system could not provide a Metal device: a virtual machine without GPU "
        "acceleration, or a GPU that Metal does not support.";
#else
    static const char* const advice =
        "Please update the graphics driver. If this machine cannot provide Vulkan 1.3, use the "
        "OpenGL build Supermodel-LegacyOG.exe instead.";
#endif

    const std::string message = std::format(
        "Model3-Vulkan requires Vulkan 1.3, or Vulkan 1.2 with the dynamic rendering "
        "(VK_KHR_dynamic_rendering) and synchronization2 (VK_KHR_synchronization2) extensions.\n\n"
        "{}\n\n{}",
        error.detail.empty() ? detected : error.detail + "\n\n" + detected,
        advice);

    ShowFatalError("Model3-Vulkan - Vulkan requirements not met", message);
}

const std::vector<std::string>& KnownVulkanDeviceNames()
{
    return g_knownDeviceNames;
}

void ShowFatalError(std::string_view title, std::string_view message)
{
    // The normal logger has not been created yet, so record the failure directly.
    AppendToStartupLog(std::format("[Fatal] {}\n{}\n", title, message));

    // SDL_ShowSimpleMessageBox is explicitly safe before SDL_Init() and needs
    // no window, which is what makes it usable this early.
    const std::string titleText(title);
    const std::string messageText(message);
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR, titleText.c_str(), messageText.c_str(), nullptr);
}

} // SDL3App
} // OSD
