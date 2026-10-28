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
    // MoltenVK is a portability implementation; without this flag it exposes
    // no physical devices at all.
    static const char* const portabilityExtension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = &portabilityExtension;
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
            std::format("vkCreateInstance failed for a Vulkan 1.3 instance (VkResult {}). "
                        "The installed loader or graphics driver is too old.", static_cast<int>(createResult)),
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

    if (!enumerateDevices || !getProperties || !getFeatures2 || !getQueueFamilies || !owner.destroy)
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

        if (properties.apiVersion < VK_API_VERSION_1_3)
        {
            reject(1, VulkanRequirement::ApiVersionTooLow,
                std::format("'{}' reports Vulkan {}.{}, but this build requires Vulkan 1.3.",
                    properties.deviceName,
                    VK_API_VERSION_MAJOR(properties.apiVersion),
                    VK_API_VERSION_MINOR(properties.apiVersion)));
            continue;
        }

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

    const std::string message = std::format(
        "Model3-Vulkan requires a graphics device with Vulkan 1.3 and the "
        "core dynamicRendering and synchronization2 features.\n\n"
        "{}\n\n"
        "Please update the graphics driver. If this machine cannot provide "
        "Vulkan 1.3, use the OpenGL build Supermodel-LegacyOG.exe instead.",
        error.detail.empty() ? detected : error.detail + "\n\n" + detected);

    ShowFatalError("Model3-Vulkan - Vulkan 1.3 required", message);
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
