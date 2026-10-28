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
#include "VulkanContext.h"

#include "OSD/Logger.h"
#include "OSD/FileSystemPath.h"
#include "OSD/Platform/Platform.h"
#include "OSD/Platform/PlatformVulkan.h"
#include "Util/PNGFile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace Vulkan3D
{

namespace
{

uint64_t SurfaceToBits(VkSurfaceKHR surface)
{
	uint64_t bits = 0;
	std::memcpy(&bits, &surface, sizeof(surface));
	return bits;
}

VkSurfaceKHR SurfaceFromBits(uint64_t bits)
{
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	std::memcpy(&surface, &bits, sizeof(surface));
	return surface;
}

/*
 * Whether an instance layer provides a given extension.
 *
 * VK_EXT_validation_features belongs to VK_LAYER_KHRONOS_validation, not to the loader or the driver,
 * so it has to be looked up with the layer name. Enumerating with VK_NULL_HANDLE misses it on most
 * setups, which would silently turn off the feature the caller asked for.
 */
bool LayerProvidesInstanceExtension(const char* layer, const char* name)
{
	uint32_t count = 0;
	if (vkEnumerateInstanceExtensionProperties(layer, &count, nullptr) != VK_SUCCESS || count == 0)
		return false;
	std::vector<VkExtensionProperties> props(count);
	if (vkEnumerateInstanceExtensionProperties(layer, &count, props.data()) != VK_SUCCESS)
		return false;
	for (const auto& prop : props)
	{
		if (std::strcmp(prop.extensionName, name) == 0)
			return true;
	}
	return false;
}

bool EnabledByEnvironment(const char* name)
{
	const char* value = std::getenv(name);
	return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

} // namespace

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT,
	const VkDebugUtilsMessengerCallbackDataEXT* data,
	void*)
{
	// Synchronization validation reports one message per hazard per command buffer, so a systematic
	// mistake (a missing barrier in the scene pass) can produce thousands per second. The first
	// thousand are the ones that identify it; the rest only make the log unreadable.
	static std::atomic<uint32_t> messageCount{ 0 };
	static std::atomic<bool> suppressed{ false };
	if (messageCount.fetch_add(1) >= 1000)
	{
		if (!suppressed.exchange(true))
			ErrorLog("Vulkan: 1000 validation messages reached; further messages are suppressed.");
		return VK_FALSE;
	}
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		ErrorLog("Vulkan: %s", data->pMessage);
	else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		ErrorLog("Vulkan warning: %s", data->pMessage);
	return VK_FALSE;
}

/*
 * Returns false for Vulkan devices that are not real GPU drivers and would only clutter the
 * device list:
 *
 *  - software rasterisers (VK_PHYSICAL_DEVICE_TYPE_CPU), such as "Microsoft Basic Render Driver"
 *  - the Direct3D 12 mapping layer, which re-exports every physical GPU under a name like
 *    "Microsoft Direct3D12 (NVIDIA GeForce RTX 5060 Ti)". These are duplicates of devices
 *    that already appear under their real driver.
 */
static bool IsDisplayDevice(const VkPhysicalDeviceProperties& props)
{
	if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
		return false;

	static const char d3d12DriverPrefix[] = "Microsoft Direct3D12";
	if (std::strncmp(props.deviceName, d3d12DriverPrefix, sizeof(d3d12DriverPrefix) - 1) == 0)
		return false;

	return true;
}

static const char* PresentModeName(VkPresentModeKHR mode)
{
	switch (mode)
	{
	case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE (no vsync)";
	case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX (vsync off, latest frame)";
	case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO (vsync on)";
	case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
	default:                               return "other";
	}
}

CVulkanContext::CVulkanContext() = default;

CVulkanContext::~CVulkanContext()
{
	Shutdown();
}

VkCommandBuffer CVulkanContext::Cmd() const
{
	return m_frames[m_frame].cmd;
}

VkImage CVulkanContext::SwapImage() const
{
	return m_imageIndex < m_images.size() ? m_images[m_imageIndex].image : VK_NULL_HANDLE;
}

VkImageView CVulkanContext::SwapView() const
{
	return m_imageIndex < m_images.size() ? m_images[m_imageIndex].view : VK_NULL_HANDLE;
}

Result CVulkanContext::InitDevice(OSD::Platform::Window* window, bool validation, bool vsync, const std::string& deviceName, bool disableOverlayLayers, const std::string& pipelineCacheTag)
{
	m_window = window;
	m_validation = validation;
	m_vsync = vsync;
	m_devicePreference = deviceName;
	m_disableOverlayLayers = disableOverlayLayers;

	/*
	 * Synchronization validation and GPU-assisted validation are developer tools, not options a
	 * player should be able to switch on by accident: the first multiplies draw cost by tracking
	 * every barrier, the second instruments every shader. They are requested through the environment
	 * and both imply the validation layer itself.
	 */
	m_syncValidation = EnabledByEnvironment("SM3D_VK_SYNC_VALIDATION");
	m_gpuValidation = EnabledByEnvironment("SM3D_VK_GPU_VALIDATION");
	if ((m_syncValidation || m_gpuValidation) && !m_validation)
	{
		m_validation = true;
		InfoLog("Vulkan: validation enabled by SM3D_VK_SYNC_VALIDATION/SM3D_VK_GPU_VALIDATION.");
	}

	// Same milestones as the settings window logs: instance, device, swapchain. They are the
	// difference between a slow driver and a slow font, and there is no other way to tell.
	const auto initClock = std::chrono::steady_clock::now();
	const auto initMs = [&initClock]()
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initClock).count();
	};

	if (volkInitialize() != VK_SUCCESS)
	{
#ifdef __APPLE__
		ErrorLog("Vulkan: no Vulkan implementation found. Install MoltenVK with "
			"'brew install molten-vk' or the Vulkan SDK, or choose the New 3D renderer.");
#else
		ErrorLog("Vulkan: volkInitialize failed. Is vulkan-1.dll installed?");
#endif
		return Result::FAIL;
	}
	if (!CreateInstance())
		return Result::FAIL;
	volkLoadInstance(m_instance);
	InfoLog("Vulkan: instance created (%.0f ms).", initMs());

	uint64_t surfaceBits = 0;
	if (!OSD::Platform::CreateVulkanSurface(window, m_instance, &surfaceBits))
	{
		ErrorLog("Vulkan: creating the window surface failed: %s", OSD::Platform::GetError());
		return Result::FAIL;
	}
	m_surface = SurfaceFromBits(surfaceBits);
	if (!PickDevice() || !CreateDevice())
		return Result::FAIL;
	volkLoadDevice(m_device);
	InfoLog("Vulkan: device created (%.0f ms).", initMs());

	// Best effort: a cache that cannot be created only means the pipelines are compiled again.
	CreatePipelineCache(pipelineCacheTag);
	InfoLog("Vulkan: pipeline cache ready (%.0f ms).", initMs());

	/*
	 * The entry points the renderer reaches through the wrappers in VulkanCommon.h have to exist by
	 * now, and this is the only place that can tell.
	 *
	 * volk fills the core name on a 1.3 device and the VK_KHR_ name on a 1.2 device with the extension
	 * enabled, but it fills neither for a device that has neither - and the wrappers are written to do
	 * nothing rather than crash when both are null. That is the right behaviour for a draw command and
	 * the wrong thing to leave undetected, so it is checked once here, where the failure can still be
	 * reported as a device problem instead of appearing later as an empty screen.
	 */
	if (!Rendering2EntryPointsAvailable())
	{
		ErrorLog("Vulkan: the device was accepted but none of dynamic rendering or synchronization2 was "
			"loaded; this driver cannot run the backend. The OpenGL renderer (New 3D or Legacy 3D) can be "
			"chosen on the Video tab.");
		return Result::FAIL;
	}

	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(m_physical, &props);
	m_uniformAlign = props.limits.minUniformBufferOffsetAlignment;
	m_storageAlign = props.limits.minStorageBufferOffsetAlignment;
	if (m_uniformAlign == 0)
		m_uniformAlign = 256;
	if (m_storageAlign == 0)
		m_storageAlign = 256;

	/*
	 * The one line worth reading first in any log from unfamiliar hardware.
	 *
	 * It states which of the two routes the backend is running through, because the same logic executes
	 * through different entry points depending on it - so a report that names the route narrows down
	 * immediately whether the 1.2 path is involved at all.
	 */
	InfoLog("Vulkan: using %s (API %u.%u, %s).", props.deviceName,
		VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
		m_vulkan13 ? "Vulkan 1.3 core path" : "Vulkan 1.2 extension path");

	if (!m_memory.Init(m_physical, m_device, m_memoryBudget))
		return Result::FAIL;
	m_memory.LogMemoryBudget("startup");
	if (!m_ring.Init(m_memory, kMaxFramesInFlight, kRingSliceBytes))
		return Result::FAIL;
	if (!CreateSwapchain() || !CreateFrames())
		return Result::FAIL;
	InfoLog("Vulkan: swapchain ready (%.0f ms).", initMs());
	return Result::OKAY;
}

void CVulkanContext::Shutdown()
{
	if (m_device)
		vkDeviceWaitIdle(m_device);

	// The worker reads the staging buffer, so it has to be finished before that memory is freed.
	if (m_screenshotThread.joinable())
		m_screenshotThread.join();
	m_screenshotBusy = false;

	DestroyFrames();
	DestroySwapchain();
	if (m_screenshotBuffer.buffer != VK_NULL_HANDLE)
	{
		m_memory.DestroyBuffer(m_screenshotBuffer);
		m_screenshotBuffer = AllocatedBuffer();
		m_screenshotSize = 0;
	}
	m_ring.Shutdown(m_memory);
	m_memory.Shutdown();
	if (m_device)
	{
		// Before the device goes: this is the only point at which the compiled pipelines can still be
		// read back, and the next run skips recompiling them.
		SavePipelineCache();
		vkDestroyDevice(m_device, nullptr);
		m_device = VK_NULL_HANDLE;
	}
	if (m_surface && m_instance)
	{
		OSD::Platform::DestroyVulkanSurface(m_instance, SurfaceToBits(m_surface));
		m_surface = VK_NULL_HANDLE;
	}
	if (m_debug && m_instance)
	{
		vkDestroyDebugUtilsMessengerEXT(m_instance, m_debug, nullptr);
		m_debug = VK_NULL_HANDLE;
	}
	if (m_instance)
	{
		vkDestroyInstance(m_instance, nullptr);
		m_instance = VK_NULL_HANDLE;
	}
	m_window = nullptr;
	m_recording = false;
}

bool CVulkanContext::CreateInstance()
{
	std::vector<const char*> extensions;
	if (!OSD::Platform::GetVulkanInstanceExtensions(m_window, extensions))
	{
		ErrorLog("Vulkan: querying the window-system instance extensions failed: %s",
			OSD::Platform::GetError());
		return false;
	}
	// Everything below this point is optional and must be removed as a block if the validation layer
	// cannot be created.
	const size_t baseExtensionCount = extensions.size();

	std::vector<const char*> layers;
	VkValidationFeaturesEXT validationFeatures = {};
	VkValidationFeatureEnableEXT enabledFeatures[2] = {};
	uint32_t enabledFeatureCount = 0;
	bool haveValidationFeatureChain = false;
	if (m_validation)
	{
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		layers.push_back("VK_LAYER_KHRONOS_validation");

		/*
		 * Synchronization validation and GPU-assisted validation live behind
		 * VK_EXT_validation_features, an instance extension the layer itself provides. Plain
		 * VK_LAYER_KHRONOS_validation only checks API usage; it cannot see a read that races a write,
		 * and it cannot see a shader sampling an image outside its declared range.
		 *
		 * Requesting the struct without the extension is an error, so the extension is looked up on the
		 * layer first. A machine with an old layer simply gets the core checks and a line saying so.
		 */
		if (m_syncValidation || m_gpuValidation)
		{
			if (LayerProvidesInstanceExtension("VK_LAYER_KHRONOS_validation", VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME))
			{
				extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
				if (m_syncValidation)
					enabledFeatures[enabledFeatureCount++] = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
				if (m_gpuValidation)
					enabledFeatures[enabledFeatureCount++] = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
				validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
				validationFeatures.enabledValidationFeatureCount = enabledFeatureCount;
				validationFeatures.pEnabledValidationFeatures = enabledFeatures;
				haveValidationFeatureChain = true;
			}
			else
			{
				ErrorLog("Vulkan: the validation layer does not provide %s; synchronization/GPU-assisted "
					"validation is unavailable. Update the Vulkan SDK/layer.", VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
				m_syncValidation = false;
				m_gpuValidation = false;
			}
		}
	}

	VkApplicationInfo app = {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "Model 3-Vulkan";
	app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 3, 0);
	app.pEngineName = "Model 3-Vulkan";
	app.engineVersion = app.applicationVersion;
	app.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.pNext = haveValidationFeatureChain ? &validationFeatures : nullptr;
	info.pApplicationInfo = &app;
	info.enabledExtensionCount = (uint32_t)extensions.size();
	info.ppEnabledExtensionNames = extensions.data();
	info.enabledLayerCount = (uint32_t)layers.size();
	info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

	VkResult result = VK_SUCCESS;
#if defined(_WIN32)
	/*
	 * The settings window does not want the desktop's implicit overlay layers in its instance, and
	 * they are expensive to load: six registered layers (Steam, RTSS, ReShade, OBS, GOG, Fossilize)
	 * add about a second to vkCreateInstance on the machine this was measured on.  The setting is
	 * scoped to this call and restored immediately, so a later game-session instance creates with
	 * the normal layer set and overlays attach there.
	 *
	 * Skipped when validation is requested: VK_LOADER_LAYERS_DISABLE=* would filter that explicit
	 * layer out as well.
	 */
	const bool filterLayers = m_disableOverlayLayers && !m_validation;
	char* previousLayers = nullptr;
	size_t previousLayersSize = 0;
	if (filterLayers)
	{
		_dupenv_s(&previousLayers, &previousLayersSize, "VK_LOADER_LAYERS_DISABLE");
		_putenv_s("VK_LOADER_LAYERS_DISABLE", "*");
	}
#endif
	result = vkCreateInstance(&info, nullptr, &m_instance);
#if defined(_WIN32)
	if (filterLayers)
	{
		_putenv_s("VK_LOADER_LAYERS_DISABLE", previousLayers != nullptr ? previousLayers : "");
		free(previousLayers);
	}
#endif
	if (result != VK_SUCCESS && m_validation)
	{
		ErrorLog("Vulkan: validation layer unavailable, retrying without it.");
		m_validation = false;
		m_syncValidation = false;
		m_gpuValidation = false;
		haveValidationFeatureChain = false;
		// Everything the validation path added, including VK_EXT_validation_features, goes away as
		// one block; the window-system extensions before baseExtensionCount stay.
		extensions.resize(baseExtensionCount);
		info.pNext = nullptr;
		info.enabledExtensionCount = (uint32_t)extensions.size();
		info.ppEnabledExtensionNames = extensions.data();
		info.enabledLayerCount = 0;
		info.ppEnabledLayerNames = nullptr;
		result = vkCreateInstance(&info, nullptr, &m_instance);
	}
	if (result != VK_SUCCESS)
	{
		/*
		 * There is deliberately no retry with a lower apiVersion here any more.
		 *
		 * That retry used to be the entry point to the Vulkan 1.2 path, which could not actually work:
		 * the shaders are compiled to SPIR-V 1.6, which needs Vulkan 1.3 or VK_KHR_maintenance4, and
		 * maintenance4 was never in the required extension list. A machine that reached the 1.2 path
		 * would have failed a few steps later at shader-module creation, with a message that pointed at
		 * the shader rather than at the real problem. Reporting the requirement plainly is more useful.
		 */
		ErrorLog("Vulkan: vkCreateInstance failed (%d) for a Vulkan 1.3 instance. This backend requires a "
			"Vulkan 1.3 driver; the OpenGL renderer (New 3D or Legacy 3D) can be chosen on the Video tab.",
			(int)result);
		return false;
	}

	if (m_validation)
	{
		VkDebugUtilsMessengerCreateInfoEXT dbg = {};
		dbg.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		dbg.pfnUserCallback = DebugCallback;
		// volkLoadInstance has not run yet on the first successful 1.3 path; load now if needed.
		volkLoadInstance(m_instance);
		if (vkCreateDebugUtilsMessengerEXT)
			vkCreateDebugUtilsMessengerEXT(m_instance, &dbg, nullptr, &m_debug);
		if (m_syncValidation || m_gpuValidation)
		{
			InfoLog("Vulkan: validation features - synchronization validation %s, GPU-assisted validation %s.",
				m_syncValidation ? "on (expect a large slowdown)" : "off",
				m_gpuValidation ? "on (shaders instrumented)" : "off");
		}
	}
	return true;
}

/*
 * Whether a device already advertises a given device-level extension.
 *
 * Asked per candidate rather than once for the chosen device, because extension availability is a
 * property of the device and the candidates are being compared before one is chosen.
 */
static bool HasDeviceExtension(VkPhysicalDevice device, const char* name)
{
	uint32_t count = 0;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> available(count);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());
	for (const auto& ext : available)
		if (std::strcmp(ext.extensionName, name) == 0)
			return true;
	return false;
}

/*
 * How a device can run this backend, which decides both whether it is a candidate and how it is set up.
 *
 *   Vulkan13           all three entry points the renderer needs are core, nothing to enable but the
 *                      features themselves.
 *   Vulkan12Extensions Vulkan 1.2 with VK_KHR_dynamic_rendering and VK_KHR_synchronization2 - the
 *                      same two capabilities by their extension names. This is what keeps a whole
 *                      generation of hardware usable: 1.3 was required for what turned out to be two
 *                      optional extensions, and the devices that lost out are the older ones this
 *                      renderer has the most to offer - it is the cheaper path on integrated graphics,
 *                      because it uploads less.
 *   Unsupported        too old, or missing one of the two.
 *
 * The shaders are built for SPIR-V 1.5 so that neither route needs VK_KHR_maintenance4, which is the
 * detail that made an earlier attempt at this fail at shader creation rather than here.
 */
enum class BackendRoute { Unsupported, Vulkan13, Vulkan12Extensions };

static BackendRoute ClassifyDevice(VkPhysicalDevice device, const VkPhysicalDeviceProperties& props)
{
	if (props.apiVersion >= VK_API_VERSION_1_3)
		return BackendRoute::Vulkan13;

	if (props.apiVersion < VK_API_VERSION_1_2)
		return BackendRoute::Unsupported;

	if (HasDeviceExtension(device, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) &&
		HasDeviceExtension(device, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME))
		return BackendRoute::Vulkan12Extensions;

	return BackendRoute::Unsupported;
}

bool CVulkanContext::PickDevice()
{
	uint32_t count = 0;
	vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
	if (count == 0)
	{
		ErrorLog("Vulkan: no physical devices.");
		return false;
	}
	std::vector<VkPhysicalDevice> devices(count);
	vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

	// Returns the queue family that can both render and present on this device, or UINT32_MAX.
	auto presentFamily = [&](VkPhysicalDevice device) -> uint32_t
	{
		uint32_t qCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &qCount, nullptr);
		std::vector<VkQueueFamilyProperties> queues(qCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &qCount, queues.data());
		for (uint32_t i = 0; i < qCount; i++)
		{
			if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
				continue;
			VkBool32 present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &present);
			if (present)
				return i;
		}
		return UINT32_MAX;
	};

	// If a specific device was requested in the configuration, honour it when it is usable.
	if (!m_devicePreference.empty())
	{
		for (auto device : devices)
		{
			VkPhysicalDeviceProperties props = {};
			vkGetPhysicalDeviceProperties(device, &props);
			if (m_devicePreference != props.deviceName)
				continue;
			if (props.limits.maxPushConstantsSize < 256)
			{
				ErrorLog("Vulkan: requested device '%s' has insufficient push constant space.", m_devicePreference.c_str());
				break;
			}
			uint32_t family = presentFamily(device);
			if (family == UINT32_MAX)
			{
				ErrorLog("Vulkan: requested device '%s' cannot render to this window.", m_devicePreference.c_str());
				break;
			}
			if (ClassifyDevice(device, props) == BackendRoute::Unsupported)
			{
				ErrorLog("Vulkan: requested device '%s' reports API %u.%u without the extensions the backend "
					"needs (Vulkan 1.3, or 1.2 with dynamic rendering and synchronization2).",
					m_devicePreference.c_str(),
					VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion));
				break;
			}
			m_physical = device;
			m_queueFamily = family;
			return true;
		}
		ErrorLog("Vulkan: device '%s' was not found. Falling back to automatic selection.", m_devicePreference.c_str());
	}

	// Automatic selection skips the devices hidden from the settings window (see
	// IsDisplayDevice()) so that a real driver is preferred over a D3D12 shim. If that
	// leaves nothing usable, every device is considered rather than failing outright.
	int bestScore = -1;
	for (int pass = 0; pass < 2 && m_physical == VK_NULL_HANDLE; pass++)
	{
		const bool includeHidden = (pass == 1);
		bestScore = -1;
		for (auto device : devices)
		{
			VkPhysicalDeviceProperties props = {};
			vkGetPhysicalDeviceProperties(device, &props);
			if (!includeHidden && !IsDisplayDevice(props))
				continue;
			if (props.limits.maxPushConstantsSize < 256)
				continue;

			uint32_t family = presentFamily(device);
			if (family == UINT32_MAX)
				continue;

			/*
			 * A device that cannot run the backend is not a candidate at all rather than merely scoring
			 * lower - letting one through would only move the failure somewhere less obvious. Vulkan 1.3
			 * or 1.2 with the two extensions behind the rendering and submission paths both qualify; see
			 * ClassifyDevice for why the extension route is worth having.
			 */
			if (ClassifyDevice(device, props) == BackendRoute::Unsupported)
				continue;

			int score = 100;
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				score += 1000;
			else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
				score += 100;
			if (score > bestScore)
			{
				bestScore = score;
				m_physical = device;
				m_queueFamily = family;
			}
		}
	}
	if (m_physical == VK_NULL_HANDLE)
	{
		ErrorLog("Vulkan: no device offers a usable API version (Vulkan 1.3, or 1.2 with dynamic "
			"rendering and synchronization2) together with graphics, present and 256-byte push constants. "
			"The OpenGL renderer (New 3D or Legacy 3D) can be chosen on the Video tab; a driver update "
			"may also help.");
		return false;
	}
	return true;
}

std::vector<std::string> EnumeratePhysicalDeviceNames()
{
	std::vector<std::string> names;
	if (volkInitialize() != VK_SUCCESS)
		return names;

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Model 3-Vulkan";
	appInfo.applicationVersion = VK_MAKE_VERSION(0, 3, 0);
	appInfo.pEngineName = "Model 3-Vulkan";
	appInfo.engineVersion = VK_MAKE_VERSION(0, 3, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;

	VkInstance instance = VK_NULL_HANDLE;
	if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
		return names;

	// volkInitialize() only loads the loader-level entry points (vkCreateInstance and
	// friends). Everything else has to be fetched from the instance, and since this
	// throw-away instance has nothing to do with the one the renderer will create, the
	// pointers are resolved locally rather than through volk's global instance table.
	PFN_vkEnumeratePhysicalDevices enumDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
	PFN_vkGetPhysicalDeviceProperties getProps = (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
	PFN_vkDestroyInstance destroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");

	if (enumDevices != nullptr && getProps != nullptr)
	{
		uint32_t count = 0;
		if (enumDevices(instance, &count, nullptr) == VK_SUCCESS && count > 0)
		{
			std::vector<VkPhysicalDevice> devices(count);
			if (enumDevices(instance, &count, devices.data()) == VK_SUCCESS)
			{
				for (auto device : devices)
				{
					VkPhysicalDeviceProperties props = {};
					getProps(device, &props);
					if (!IsDisplayDevice(props))
						continue;
					names.emplace_back(props.deviceName);
				}
			}
		}
	}

	if (destroyInstance != nullptr)
		destroyInstance(instance, nullptr);

	return names;
}

bool CVulkanContext::CreateDevice()
{
	uint32_t extCount = 0;
	vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &extCount, nullptr);
	std::vector<VkExtensionProperties> available(extCount);
	vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &extCount, available.data());
	auto hasExt = [&](const char* name)
	{
		for (const auto& ext : available)
			if (std::strcmp(ext.extensionName, name) == 0)
				return true;
		return false;
	};

	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(m_physical, &props);
	// Kept for the statistics readout, which has no other way to name the GPU: the settings window and the
	// game session each hold their own context, and only this one knows which device was picked.
	m_deviceName = props.deviceName;
	const BackendRoute route = ClassifyDevice(m_physical, props);
	if (route == BackendRoute::Unsupported)
	{
		// Not reachable through PickDevice, which only keeps devices that classify as supported. Said
		// rather than assumed: a device that slipped through would fail at the first draw, which is a
		// much worse place to find out.
		ErrorLog("Vulkan: the selected device cannot run this backend.");
		return false;
	}
	m_vulkan13 = (route == BackendRoute::Vulkan13);

	std::vector<const char*> extensions;
	extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

	/*
	 * On a 1.3 device the extensions behind dynamic rendering and synchronization2 are core, and their
	 * names need not even be advertised - which is why they are not requested there. On 1.2 they are
	 * the only way to get those two capabilities, so they are requested by name.
	 *
	 * descriptor_indexing, maintenance2, create_renderpass_2 and depth_stencil_resolve are core in 1.2
	 * and are likewise not requested. VK_KHR_maintenance4 is not requested either: it exists to allow
	 * SPIR-V 1.6 on a 1.2 device, and the shaders are built for SPIR-V 1.5, so nothing needs it.
	 */
	if (route == BackendRoute::Vulkan12Extensions)
	{
		extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
		extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
	}

	m_pushDescriptors = hasExt(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	if (m_pushDescriptors)
		extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);

	/*
	 * VK_EXT_memory_budget, when present, turns "vkAllocateMemory failed" from a bare number into
	 * "the heap has X MB of budget left and Y MB is already in use". That is the same information VMA
	 * exposes as VmaBudget and the only way to tell fragmentation apart from a genuinely full device
	 * after a failure. Optional: everything works without it, the log is just less specific.
	 */
	m_memoryBudget = hasExt(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	if (m_memoryBudget)
		extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

	/*
	 * VK_KHR_present_wait and VK_KHR_present_id, when the driver offers both.
	 *
	 * Requested rather than required: a driver or compositor without them renders exactly the same
	 * picture, it simply cannot say when that picture reached the display. The two are only useful
	 * together - present_id numbers the presents, present_wait waits on the number - so one without
	 * the other is treated as absent.
	 *
	 * Both features are queried through the extension structs rather than assumed from the extension
	 * name. An implementation may advertise the extension and still report the feature as false, and
	 * chaining a feature the driver did not claim fails vkCreateDevice.
	 */
	VkPhysicalDevicePresentIdFeaturesKHR presentIdQuery = {};
	presentIdQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
	VkPhysicalDevicePresentWaitFeaturesKHR presentWaitQuery = {};
	presentWaitQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
	presentIdQuery.pNext = &presentWaitQuery;
	{
		VkPhysicalDeviceFeatures2 query = {};
		query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		query.pNext = &presentIdQuery;
		vkGetPhysicalDeviceFeatures2(m_physical, &query);
	}
	if (presentIdQuery.presentId && presentWaitQuery.presentWait &&
		hasExt(VK_KHR_PRESENT_ID_EXTENSION_NAME) && hasExt(VK_KHR_PRESENT_WAIT_EXTENSION_NAME))
	{
		extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
		extensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
		m_presentWait = true;
	}

	/*
	 * The two capabilities the renderer cannot do without are queried and enabled through different
	 * structs depending on the route.
	 *
	 * On 1.3 they are core, so VkPhysicalDeviceVulkan13Features is the right thing to chain. On 1.2
	 * chaining the 1.3 feature struct is not valid - the same two booleans come from the extension
	 * feature structs instead. Reading the wrong one returns zero, and the backend would then reject a
	 * device that is perfectly capable of running it.
	 */
	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	VkPhysicalDeviceVulkan13Features features13 = {};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	VkPhysicalDeviceDynamicRenderingFeaturesKHR featuresDyn = {};
	featuresDyn.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
	VkPhysicalDeviceSynchronization2FeaturesKHR featuresSync = {};
	featuresSync.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;

	features2.pNext = &features12;
	if (route == BackendRoute::Vulkan13)
	{
		features12.pNext = &features13;
	}
	else
	{
		features12.pNext = &featuresDyn;
		featuresDyn.pNext = &featuresSync;
	}

	vkGetPhysicalDeviceFeatures2(m_physical, &features2);

	/*
	 * The texture cache builds one sampler per pair of wrap modes, including
	 * VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE. Using that mode without samplerMirrorClampToEdge
	 * is invalid and the validation layer reports it for every sampler created; the driver may accept
	 * it, which is exactly the kind of "works on this machine" behaviour that breaks on another.
	 */
	m_samplerMirrorClamp = features12.samplerMirrorClampToEdge != 0;
	m_fragmentStoresAndAtomics = features2.features.fragmentStoresAndAtomics != 0;
	m_sampleRateShading = features2.features.sampleRateShading != 0;

	if (!features12.descriptorIndexing || !features12.runtimeDescriptorArray ||
		!features12.shaderSampledImageArrayNonUniformIndexing || !features12.descriptorBindingPartiallyBound ||
		!features12.descriptorBindingSampledImageUpdateAfterBind || !features12.descriptorBindingVariableDescriptorCount)
	{
		ErrorLog("Vulkan: descriptor indexing features are missing.");
		return false;
	}

	const bool hasDynamicRendering = (route == BackendRoute::Vulkan13) ? features13.dynamicRendering : featuresDyn.dynamicRendering;
	const bool hasSynchronization2 = (route == BackendRoute::Vulkan13) ? features13.synchronization2 : featuresSync.synchronization2;
	if (!hasDynamicRendering || !hasSynchronization2)
	{
		ErrorLog("Vulkan: dynamic rendering or synchronization2 is missing.");
		return false;
	}

	VkPhysicalDeviceVulkan12Features enable12 = {};
	enable12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	enable12.descriptorIndexing = VK_TRUE;
	enable12.runtimeDescriptorArray = VK_TRUE;
	enable12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	enable12.descriptorBindingPartiallyBound = VK_TRUE;
	enable12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	enable12.descriptorBindingVariableDescriptorCount = VK_TRUE;
	enable12.timelineSemaphore = features12.timelineSemaphore;
	enable12.samplerMirrorClampToEdge = m_samplerMirrorClamp ? VK_TRUE : VK_FALSE;

	// Enabled through the same struct that was queried, for the same reason as above.
	VkPhysicalDeviceVulkan13Features enable13 = {};
	enable13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	VkPhysicalDeviceDynamicRenderingFeaturesKHR enableDyn = {};
	enableDyn.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
	VkPhysicalDeviceSynchronization2FeaturesKHR enableSync = {};
	enableSync.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;

	if (route == BackendRoute::Vulkan13)
	{
		enable13.dynamicRendering = VK_TRUE;
		enable13.synchronization2 = VK_TRUE;
		enable12.pNext = &enable13;
	}
	else
	{
		enableDyn.dynamicRendering = VK_TRUE;
		enableSync.synchronization2 = VK_TRUE;
		enableDyn.pNext = &enableSync;
		enable12.pNext = &enableDyn;
	}

	VkPhysicalDevicePresentIdFeaturesKHR enablePresentId = {};
	enablePresentId.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
	VkPhysicalDevicePresentWaitFeaturesKHR enablePresentWait = {};
	enablePresentWait.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
	if (m_presentWait)
	{
		enablePresentId.presentId = VK_TRUE;
		enablePresentWait.presentWait = VK_TRUE;
		enablePresentId.pNext = &enablePresentWait;
		// Appended to whichever chain the route built above, so the structs the renderer needs stay
		// first in pNext order and the optional one is last.
		if (route == BackendRoute::Vulkan13)
			enable13.pNext = &enablePresentId;
		else
			enableSync.pNext = &enablePresentId;
	}

	VkPhysicalDeviceFeatures enabled = {};
	/*
	 * The three scene targets need different colour write masks (the colour layer writes all three,
	 * each transparency layer writes only its own), and independentBlend is what makes per-attachment
	 * state legal. Without it the pAttachments array must be identical and the MRT erase is undefined.
	 * Every desktop Vulkan implementation supports it; a device that does not cannot run this backend.
	 */
	if (!features2.features.independentBlend)
	{
		ErrorLog("Vulkan: independentBlend is missing; the three scene colour attachments cannot be written "
			"independently. The OpenGL renderer can be chosen on the Video tab.");
		return false;
	}
	enabled.independentBlend = VK_TRUE;
	enabled.samplerAnisotropy = features2.features.samplerAnisotropy;
	enabled.fragmentStoresAndAtomics = features2.features.fragmentStoresAndAtomics;
	// The sorted-transparency resolve runs once per sample to keep MSAA edges on translucent
	// geometry. This is a core feature and enabled whenever it exists, so turning the mode on does
	// not require recreating the device; a device without it resolves per pixel instead.
	enabled.sampleRateShading = features2.features.sampleRateShading;
	enabled.shaderInt16 = features2.features.shaderInt16;
	// GPU-assisted validation instruments every shader and writes its access records through
	// storage images, which requires vertex-stage stores as well. Requested only in that mode, so a
	// normal session enables exactly the features it did before.
	if (m_gpuValidation && features2.features.vertexPipelineStoresAndAtomics)
		enabled.vertexPipelineStoresAndAtomics = VK_TRUE;
	VkPhysicalDeviceFeatures2 enable2 = {};
	enable2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	enable2.pNext = &enable12;
	enable2.features = enabled;

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = m_queueFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;

	VkDeviceCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	info.pNext = &enable2;
	info.queueCreateInfoCount = 1;
	info.pQueueCreateInfos = &queueInfo;
	info.enabledExtensionCount = (uint32_t)extensions.size();
	info.ppEnabledExtensionNames = extensions.data();

	if (vkCreateDevice(m_physical, &info, nullptr, &m_device) != VK_SUCCESS)
	{
		ErrorLog("Vulkan: vkCreateDevice failed.");
		return false;
	}
	vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
	InfoLog("Vulkan: VK_KHR_present_wait %s.", m_presentWait
		? "available - presentation is timed against the display"
		: "unavailable - presentation is timed against the queue");
	return true;
}

/*
 * Persistent pipeline cache.
 *
 * Every pipeline in the backend - the 3D passes, the post chain, the 2D layers, the ImGui and
 * overlay pipelines - is created against this one object, and the driver keeps the compiled shader
 * blobs in it. Creating it from the file left by the previous run turns the first seconds of a
 * session from "compile everything again" into a few milliseconds of reading.
 *
 * Incompatible data is not an error. A cache written by another driver, another GPU, or a run that
 * was killed mid-write is rejected and the object is created empty - which is exactly the state a
 * fresh installation is in anyway.
 */
bool CVulkanContext::CreatePipelineCache(const std::string& tag)
{
	m_pipelineCachePath = FileSystemPath::GetPath(FileSystemPath::Cache) + "pipelines-" + tag + ".bin";

	std::vector<uint8_t> initial;
	{
		std::ifstream in(m_pipelineCachePath, std::ios::binary | std::ios::ate);
		if (in)
		{
			const std::streamoff size = in.tellg();
			if (size > 0)
			{
				initial.resize((size_t)size);
				in.seekg(0);
				in.read(reinterpret_cast<char*>(initial.data()), (std::streamsize)size);
				if (!in)
					initial.clear();
			}
		}
	}

	VkPipelineCacheCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	info.initialDataSize = initial.size();
	info.pInitialData = initial.empty() ? nullptr : initial.data();

	VkResult result = vkCreatePipelineCache(m_device, &info, nullptr, &m_pipelineCache);
	if (result != VK_SUCCESS)
	{
		InfoLog("Vulkan: pipeline cache '%s' was rejected (%d); starting empty.", m_pipelineCachePath.c_str(), (int)result);
		info.initialDataSize = 0;
		info.pInitialData = nullptr;
		if (vkCreatePipelineCache(m_device, &info, nullptr, &m_pipelineCache) != VK_SUCCESS)
		{
			m_pipelineCache = VK_NULL_HANDLE;
			return false;
		}
	}
	else if (!initial.empty())
	{
		InfoLog("Vulkan: pipeline cache loaded from '%s' (%.1f KB).", m_pipelineCachePath.c_str(), initial.size() / 1024.0);
	}
	return true;
}

void CVulkanContext::SavePipelineCache()
{
	if (m_pipelineCache == VK_NULL_HANDLE)
		return;

	size_t size = 0;
	if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, nullptr) == VK_SUCCESS && size > 0)
	{
		std::vector<uint8_t> data(size);
		if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, data.data()) == VK_SUCCESS)
		{
			// Written under a temporary name and renamed over the old file: a crash in the middle of
			// this must not leave a truncated cache that is rejected on every following start.
			const std::string temporary = m_pipelineCachePath + ".part";
			std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
			bool written = out && out.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)size);
			out.close();

			std::error_code error;
			if (written)
				std::filesystem::rename(temporary, m_pipelineCachePath, error);
			if (written && !error)
				InfoLog("Vulkan: pipeline cache saved to '%s' (%.1f KB).", m_pipelineCachePath.c_str(), size / 1024.0);
			else
				InfoLog("Vulkan: could not write pipeline cache '%s'.", m_pipelineCachePath.c_str());
		}
	}

	vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
	m_pipelineCache = VK_NULL_HANDLE;
}

bool CVulkanContext::CreateSwapchain()
{
	VkSurfaceCapabilitiesKHR caps = {};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical, m_surface, &caps);

	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &formatCount, formats.data());
	VkSurfaceFormatKHR chosen = formats[0];
	for (const auto& format : formats)
	{
		if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			chosen = format;
			break;
		}
		if (format.format == VK_FORMAT_R8G8B8A8_UNORM)
			chosen = format;
	}
	m_swapFormat = chosen.format;

	uint32_t modeCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical, m_surface, &modeCount, nullptr);
	std::vector<VkPresentModeKHR> modes(modeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical, m_surface, &modeCount, modes.data());
	m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if (!m_vsync)
	{
		for (auto mode : modes)
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
				m_presentMode = mode;
		if (m_presentMode == VK_PRESENT_MODE_FIFO_KHR)
		{
			for (auto mode : modes)
				if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
					m_presentMode = mode;
		}
	}

	// Report the full set of present modes once. This tells us up front whether
	// MAILBOX / FIFO_RELAXED are available on this GPU+driver, which decides
	// whether the low-latency / anti-judder options are even reachable.
	static bool s_loggedPresentModes = false;
	if (!s_loggedPresentModes)
	{
		s_loggedPresentModes = true;
		std::string list;
		for (auto mode : modes)
		{
			if (!list.empty())
				list += ", ";
			list += PresentModeName(mode);
		}
		InfoLog("Vulkan: available present modes: %s.", list.c_str());
	}

	if (caps.currentExtent.width != UINT32_MAX)
		m_extent = caps.currentExtent;
	else
	{
		int w = 0, h = 0;
		OSD::Platform::GetWindowSize(m_window, &w, &h);
		m_extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, (uint32_t)w));
		m_extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, (uint32_t)h));
	}
	if (m_extent.width == 0 || m_extent.height == 0)
		return false;

	uint32_t imageCount = caps.minImageCount + 1;
	// MAILBOX needs a spare image to write into while the previous one is still
	// queued for scan-out, so three is its minimum for a stable image.
	if (m_presentMode == VK_PRESENT_MODE_MAILBOX_KHR && imageCount < 3)
		imageCount = 3;
	if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
		imageCount = caps.maxImageCount;

	for (auto& image : m_images)
	{
		if (image.view)
			vkDestroyImageView(m_device, image.view, nullptr);
		if (image.renderFinished)
			vkDestroySemaphore(m_device, image.renderFinished, nullptr);
		image.view = VK_NULL_HANDLE;
		image.renderFinished = VK_NULL_HANDLE;
	}
	m_images.clear();
	m_imageSeen.clear();

	VkSwapchainCreateInfoKHR info = {};
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = m_surface;
	info.minImageCount = imageCount;
	info.imageFormat = m_swapFormat;
	info.imageColorSpace = chosen.colorSpace;
	info.imageExtent = m_extent;
	info.imageArrayLayers = 1;
	// TRANSFER_SRC is requested so the final composited frame can be copied out for screenshots.
	// It is optional: if the surface does not advertise it the swapchain is created without it and
	// screenshots are simply unavailable rather than the window failing to appear.
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
	{
		info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		m_swapTransferSrc = true;
	}
	else
	{
		m_swapTransferSrc = false;
		InfoLog("Vulkan: swapchain cannot be a transfer source; screenshots are disabled.");
	}
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform = caps.currentTransform;
	info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if ((caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0)
		info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	info.presentMode = m_presentMode;
	info.clipped = VK_TRUE;
	info.oldSwapchain = m_swapchain;

	VkSwapchainKHR fresh = VK_NULL_HANDLE;
	if (vkCreateSwapchainKHR(m_device, &info, nullptr, &fresh) != VK_SUCCESS)
	{
		ErrorLog("Vulkan: vkCreateSwapchainKHR failed.");
		return false;
	}
	if (m_swapchain)
		vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	m_swapchain = fresh;
	// The new swapchain has had no presents yet, so there is no id a wait may refer to until EndFrame()
	// submits one on it. Ids in m_presentId keep counting up, which the spec requires of a swapchain's
	// ids and costs nothing across a recreation.
	m_swapchainPresentId = 0;

	uint32_t got = 0;
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &got, nullptr);
	std::vector<VkImage> images(got);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &got, images.data());

	m_images.resize(got);
	m_imageSeen.assign(got, 0);
	for (uint32_t i = 0; i < got; i++)
	{
		m_images[i].image = images[i];
		m_images[i].view = CreateView(images[i], m_swapFormat, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
		VkSemaphoreCreateInfo sem = {};
		sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vkCreateSemaphore(m_device, &sem, nullptr, &m_images[i].renderFinished);
	}
	m_generation++;
	InfoLog("Vulkan: swapchain %ux%u, %u images, present mode %s.",
		m_extent.width, m_extent.height, got, PresentModeName(m_presentMode));
	return true;
}

void CVulkanContext::DestroySwapchain()
{
	if (!m_device)
		return;
	for (auto& image : m_images)
	{
		if (image.view)
			vkDestroyImageView(m_device, image.view, nullptr);
		if (image.renderFinished)
			vkDestroySemaphore(m_device, image.renderFinished, nullptr);
	}
	m_images.clear();
	m_imageSeen.clear();
	m_swapchainPresentId = 0;
	if (m_swapchain)
	{
		vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
		m_swapchain = VK_NULL_HANDLE;
	}
}

bool CVulkanContext::CreateFrames()
{
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = m_queueFamily;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VkSemaphoreCreateInfo semInfo = {};
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	for (uint32_t i = 0; i < kMaxFramesInFlight; i++)
	{
		if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_frames[i].pool) != VK_SUCCESS)
			return false;
		VkCommandBufferAllocateInfo alloc = {};
		alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc.commandPool = m_frames[i].pool;
		alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc.commandBufferCount = 1;
		if (vkAllocateCommandBuffers(m_device, &alloc, &m_frames[i].cmd) != VK_SUCCESS)
			return false;
		vkCreateFence(m_device, &fenceInfo, nullptr, &m_frames[i].fence);
		vkCreateSemaphore(m_device, &semInfo, nullptr, &m_frames[i].imageAvailable);
	}
	return true;
}

void CVulkanContext::DestroyFrames()
{
	if (!m_device)
		return;
	for (uint32_t i = 0; i < kMaxFramesInFlight; i++)
	{
		if (m_frames[i].pool)
			vkDestroyCommandPool(m_device, m_frames[i].pool, nullptr);
		if (m_frames[i].fence)
			vkDestroyFence(m_device, m_frames[i].fence, nullptr);
		if (m_frames[i].imageAvailable)
			vkDestroySemaphore(m_device, m_frames[i].imageAvailable, nullptr);
		m_frames[i] = Frame();
	}
}

bool CVulkanContext::RecreateSwapchain()
{
	if (!m_device)
		return false;
	vkDeviceWaitIdle(m_device);
	int w = 0, h = 0;
	OSD::Platform::GetWindowSize(m_window, &w, &h);
	if (w == 0 || h == 0)
		return false;
	return CreateSwapchain();
}

void CVulkanContext::MarkSwapchainReady()
{
	m_swapReady = true;
	if (m_imageIndex < m_imageSeen.size())
		m_imageSeen[m_imageIndex] = 1;
}

bool CVulkanContext::BeginFrame()
{
	if (!m_device)
		return false;
	Frame& frame = m_frames[m_frame];
	vkWaitForFences(m_device, 1, &frame.fence, VK_TRUE, UINT64_MAX);

	// A capture recorded the last time this frame slot was used has now completed, so it can be read
	// back without waiting on anything. Doing it here rather than in EndFrame() is what keeps a
	// screenshot from stalling the main loop and starving the audio.
	//
	// The busy check means a rapid burst of requests is throttled instead of queueing up joins: the
	// capture stays pending and is picked up when the slot next comes round.
	if (m_screenshotPendingSlot == (int32_t)m_frame && !m_screenshotBusy)
		WriteScreenshot();

	int w = 0, h = 0;
	OSD::Platform::GetWindowSize(m_window, &w, &h);
	if (w == 0 || h == 0)
		return false;

	VkResult acquired = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &m_imageIndex);
	if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapchain();
		return false;
	}
	if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
	{
		ErrorLog("Vulkan: vkAcquireNextImageKHR failed (%d).", (int)acquired);
		return false;
	}

	vkResetFences(m_device, 1, &frame.fence);
	vkResetCommandPool(m_device, frame.pool, 0);
	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(frame.cmd, &begin);
	m_ring.BeginFrame(m_frame);
	m_recording = true;
	m_swapReady = false;
	if (acquired == VK_SUBOPTIMAL_KHR)
		m_generation = m_generation; // recreated after present
	return true;
}

void CVulkanContext::RequestScreenshot(const std::string& path)
{
	if (!m_swapTransferSrc)
	{
		ErrorLog("Vulkan: screenshots are not available on this swapchain.");
		return;
	}
	m_screenshotPath = path;
	m_screenshotRequests++;
}

void CVulkanContext::RecordScreenshotCopy(VkCommandBuffer cmd)
{
	if (m_screenshotRequests <= 0 || !m_swapTransferSrc)
		return;
	if (!RecordFrameCopy(cmd))
		return;

	m_captureIsThumbnail = false;
	// The request is consumed here; the capture now waits for its frame slot to come round again,
	// which is when the copy is guaranteed complete (see BeginFrame).
	m_screenshotRequests--;
}

void CVulkanContext::RequestThumbnail(const std::string& path, uint32_t width, uint32_t height)
{
	m_thumbnailPath = path;
	m_thumbnailWidth = (width > 0) ? width : 320;
	m_thumbnailHeight = (height > 0) ? height : 240;
	m_thumbnailRequests++;
}

void CVulkanContext::RecordThumbnailCopy(VkCommandBuffer cmd)
{
	if (m_thumbnailRequests <= 0 || !m_swapTransferSrc)
		return;
	if (!RecordFrameCopy(cmd))
		return;

	m_captureIsThumbnail = true;
	m_thumbnailRequests--;
}

bool CVulkanContext::RecordFrameCopy(VkCommandBuffer cmd)
{
	// A capture that has not been read back yet already owns the staging buffer, and a worker may
	// still be reading it.
	if (m_screenshotPendingSlot >= 0 || m_screenshotBusy)
		return false;
	if (m_extent.width == 0 || m_extent.height == 0)
		return false;

	const VkDeviceSize needed = (VkDeviceSize)m_extent.width * m_extent.height * 4;

	// (Re)create the staging buffer when it is missing or the size changed. The swapchain calls
	// vkDeviceWaitIdle before recreating, so replacing it here is safe.
	if (m_screenshotSize != needed || m_screenshotBuffer.buffer == VK_NULL_HANDLE)
	{
		if (m_screenshotBuffer.buffer != VK_NULL_HANDLE)
			m_memory.DestroyBuffer(m_screenshotBuffer);
		m_screenshotBuffer = AllocatedBuffer();
		if (!m_memory.CreateBuffer(needed, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_screenshotBuffer))
		{
			ErrorLog("Vulkan: could not allocate the screenshot staging buffer.");
			m_screenshotRequests = 0;
			m_thumbnailRequests = 0;
			return false;
		}
		m_screenshotSize = needed;
	}

	VkImage image = m_images[m_imageIndex].image;
	// Out of the attachment layout, copy, and back again so the caller can still transition from
	// COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC as it always does.
	CmdTransition(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { m_extent.width, m_extent.height, 1 };
	vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_screenshotBuffer.buffer, 1, &region);

	CmdTransition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	m_screenshotPendingSlot = (int32_t)m_frame;
	return true;
}

/*
 * Writes the captured frame out as a PNG, on a worker thread.
 *
 * Called from BeginFrame() once the frame slot that holds the capture comes round again, at which
 * point vkWaitForFences() has already established that the copy finished.
 *
 * Everything expensive - the channel swizzle over 8 million pixels, the zlib compression and the
 * file write - is handed to a thread. Doing any of it inline would stall the main loop, and because
 * emulated audio is produced one frame at a time from that loop, a stall drains the sound buffer and
 * the music stutters. The staging buffer is read by the worker, so m_screenshotBusy keeps a new
 * capture from overwriting it until the worker is done.
 */
void CVulkanContext::WriteScreenshot()
{
	m_screenshotPendingSlot = -1;

	if (m_screenshotBuffer.mapped == nullptr)
	{
		ErrorLog("Vulkan: screenshot buffer is not mapped.");
		return;
	}

	// BeginFrame only calls this once the previous worker has finished, so joining here is immediate
	// and cannot stall the main loop. Joining is still required because the thread object has to be
	// released before it can be assigned to again.
	if (m_screenshotThread.joinable())
		m_screenshotThread.join();

	const uint8_t* src = static_cast<const uint8_t*>(m_screenshotBuffer.mapped);
	const uint32_t width = m_extent.width;
	const uint32_t height = m_extent.height;
	const bool swapRedBlue = (m_swapFormat == VK_FORMAT_B8G8R8A8_UNORM ||
	                          m_swapFormat == VK_FORMAT_B8G8R8A8_SRGB);

	if (m_captureIsThumbnail)
	{
		/*
		 * Save-state thumbnail: a small 4:3 crop of the frame that was captured before the interface
		 * was drawn. The same worker and staging buffer as a screenshot are used; only the output
		 * differs, so the two paths cannot disagree about when the buffer is safe to read.
		 */
		const uint32_t outWidth = m_thumbnailWidth;
		const uint32_t outHeight = m_thumbnailHeight;
		const std::string path = m_thumbnailPath;

		uint32_t cropWidth = width;
		uint32_t cropHeight = height;
		uint32_t cropX = 0;
		uint32_t cropY = 0;
		if ((uint64_t)cropWidth * 3 > (uint64_t)cropHeight * 4)
		{
			cropWidth = cropHeight * 4 / 3;
			cropX = (width - cropWidth) / 2;
		}
		else if ((uint64_t)cropWidth * 3 < (uint64_t)cropHeight * 4)
		{
			cropHeight = cropWidth * 3 / 4;
			cropY = (height - cropHeight) / 2;
		}

		m_screenshotBusy = true;
		m_screenshotThread = std::thread([this, src, width, cropX, cropY, cropWidth, cropHeight, outWidth, outHeight,
			path, swapRedBlue]()
		{
			std::vector<uint8_t> rgb((size_t)outWidth * outHeight * 3);
			for (uint32_t y = 0; y < outHeight; ++y)
			{
				const uint32_t sourceY = cropY + y * cropHeight / outHeight;
				for (uint32_t x = 0; x < outWidth; ++x)
				{
					const uint32_t sourceX = cropX + x * cropWidth / outWidth;
					const uint8_t* texel = src + ((size_t)sourceY * width + sourceX) * 4;
					uint8_t* target = rgb.data() + ((size_t)y * outWidth + x) * 3;
					target[0] = swapRedBlue ? texel[2] : texel[0];
					target[1] = texel[1];
					target[2] = swapRedBlue ? texel[0] : texel[2];
				}
			}

			// Written under a temporary name and renamed when complete: the caller polls for the
			// file and must never pick up a half-written PNG.
			const std::string partial = path + ".part";
			std::error_code error;
			std::filesystem::remove(partial, error);
			if (!Util::PNG::WriteRGB(partial, rgb.data(), (int32_t)outWidth, (int32_t)outHeight))
			{
				std::filesystem::remove(path, error);
				std::filesystem::rename(partial, path, error);
				if (error)
					ErrorLog("Vulkan: could not move the save thumbnail into place (%s).", error.message().c_str());
			}
			else
			{
				std::filesystem::remove(partial, error);
				ErrorLog("Vulkan: failed to write the save thumbnail to %s.", path.c_str());
			}

			m_screenshotBusy = false;
		});
		return;
	}

	m_screenshotBusy = true;
	const std::string path = m_screenshotPath;
	m_screenshotThread = std::thread([this, src, width, height, path, swapRedBlue]()
	{
		const size_t pixels = (size_t)width * height;

		// RGB rather than RGBA: a screenshot has no meaningful alpha, and this is 25% less data to
		// compress. The swapchain is almost always B8G8R8A8, whose byte order differs from the RGB
		// the writer expects, so the channels are swapped here rather than teaching the writer about
		// Vulkan formats.
		std::vector<uint8_t> out(pixels * 3);
		for (size_t i = 0; i < pixels; ++i)
		{
			out[i * 3 + 0] = swapRedBlue ? src[i * 4 + 2] : src[i * 4 + 0];
			out[i * 3 + 1] = src[i * 4 + 1];
			out[i * 3 + 2] = swapRedBlue ? src[i * 4 + 0] : src[i * 4 + 2];
		}

		// The requested name only has second resolution, so several screenshots taken in the same
		// second would collide. Resolving it here rather than at request time is what makes it work:
		// at request time the file does not exist yet, because it is this thread that creates it.
		std::string finalPath = path;
		if (FILE* existing = fopen(finalPath.c_str(), "rb"))
		{
			fclose(existing);
			const std::string base = finalPath.size() > 4 ? finalPath.substr(0, finalPath.size() - 4) : finalPath;
			for (int suffix = 2; suffix < 10000; suffix++)
			{
				std::string candidate = Util::Format() << base << "_" << suffix << ".png";
				if (FILE* taken = fopen(candidate.c_str(), "rb"))
				{
					fclose(taken);
					continue;
				}
				finalPath = candidate;
				break;
			}
		}

		// Vulkan renders with y=0 at the top and PNG also stores rows top-down, so unlike the BMP
		// path no vertical flip is needed.
		//
		// NOTE: the writer returns true on FAILURE, matching the convention of WriteSurfaceToBMP().
		const bool writeFailed = Util::PNG::WriteRGB(finalPath, out.data(), (int32_t)width, (int32_t)height);
		if (!writeFailed)
			InfoLog("Vulkan: screenshot written to %s.", finalPath.c_str());
		else
			ErrorLog("Vulkan: failed to write screenshot to %s.", finalPath.c_str());

		m_screenshotBusy = false;
	});
}

void CVulkanContext::RebindFunctionPointers()
{
	if (m_instance == VK_NULL_HANDLE)
		return;
	// Instance first: volkLoadInstance resets the device-level entries to the uninitialised stubs,
	// so the device binding has to come after it.
	volkLoadInstance(m_instance);
	if (m_device != VK_NULL_HANDLE)
		volkLoadDevice(m_device);
}

void CVulkanContext::EndFrame()
{
	if (!m_recording)
		return;
	Frame& frame = m_frames[m_frame];
	if (!m_swapReady)
	{
		// Acquired a swapchain image but the 2D compositor did not run.
		// Present still requires PRESENT_SRC. After the first present the
		// image comes back in that layout; a brand new image is UNDEFINED.
		VkImageLayout oldLayout = (m_imageIndex < m_imageSeen.size() && m_imageSeen[m_imageIndex])
			? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
			: VK_IMAGE_LAYOUT_UNDEFINED;
		CmdTransition(frame.cmd, m_images[m_imageIndex].image, oldLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT,
			0, 1, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
		if (m_imageIndex < m_imageSeen.size())
			m_imageSeen[m_imageIndex] = 1;
		m_swapReady = true;
	}
	vkEndCommandBuffer(frame.cmd);

	VkCommandBufferSubmitInfo cmdInfo = {};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.commandBuffer = frame.cmd;

	VkSemaphoreSubmitInfo waitInfo = {};
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitInfo.semaphore = frame.imageAvailable;
	waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSemaphoreSubmitInfo signalInfo = {};
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.semaphore = m_images[m_imageIndex].renderFinished;
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	VkSubmitInfo2 submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.waitSemaphoreInfoCount = 1;
	submit.pWaitSemaphoreInfos = &waitInfo;
	submit.signalSemaphoreInfoCount = 1;
	submit.pSignalSemaphoreInfos = &signalInfo;
	submit.commandBufferInfoCount = 1;
	submit.pCommandBufferInfos = &cmdInfo;
	QueueSubmit2(m_queue, 1, &submit, frame.fence);

	VkPresentInfoKHR present = {};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &m_images[m_imageIndex].renderFinished;
	present.swapchainCount = 1;
	present.pSwapchains = &m_swapchain;
	present.pImageIndices = &m_imageIndex;

	/*
	 * Number the present so that WaitForPresentDisplay() can ask about this exact image.
	 *
	 * The id has to be larger than every id previously submitted for the swapchain, which a counter
	 * that never resets satisfies after a recreation as well.
	 */
	VkPresentIdKHR presentId = {};
	if (m_presentWait)
	{
		m_swapchainPresentId = ++m_presentId;
		presentId.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
		presentId.swapchainCount = 1;
		presentId.pPresentIds = &m_swapchainPresentId;
		present.pNext = &presentId;
	}

	VkResult presented = vkQueuePresentKHR(m_queue, &present);
	m_recording = false;
	m_frame = (m_frame + 1) % kMaxFramesInFlight;
	if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
		RecreateSwapchain();
}

bool CVulkanContext::WaitForPresentDisplay(uint64_t timeoutNs)
{
	if (!m_presentWait || m_swapchainPresentId == 0 || m_swapchain == VK_NULL_HANDLE || !vkWaitForPresentKHR)
		return false;

	const VkResult result = vkWaitForPresentKHR(m_device, m_swapchain, m_swapchainPresentId, timeoutNs);
	if (result == VK_SUCCESS)
		return true;

	// A timeout is the expected answer whenever the deadline arrives before the compositor does: the
	// caller has spent exactly the time it was going to sleep, so it says nothing about the feature.
	if (result == VK_TIMEOUT)
		return false;

	// The swapchain is about to be rebuilt and its ids with it, so the next wait has nothing to ask
	// about yet. Not an error.
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
		return false;

	// Anything else means the driver cannot answer for present ids on this device. Asking again every
	// frame would turn a one-off into a per-frame stall, so the feature is dropped and the caller
	// falls back to the wall clock.
	InfoLog("Vulkan: vkWaitForPresentKHR returned %d; presentation timing falls back to the queue.", (int)result);
	m_presentWait = false;
	return false;
}

bool CVulkanContext::CaptureThumbnail(const std::string& pngPath, uint32_t width, uint32_t height)
{
	if (!m_device || m_swapchain == VK_NULL_HANDLE || m_images.empty() ||
		m_extent.width == 0 || m_extent.height == 0 || width == 0 || height == 0)
		return false;

	// The save path is a cold path and the emulator is paused here, so waiting
	// for the device is safe and makes the readback self-contained.
	vkDeviceWaitIdle(m_device);

	VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = m_queueFamily;
	if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
		return false;

	VkCommandBuffer command = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo allocate = {};
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1;
	if (vkAllocateCommandBuffers(m_device, &allocate, &command) != VK_SUCCESS)
	{
		vkDestroyCommandPool(m_device, pool, nullptr);
		return false;
	}

	const VkDeviceSize bytes = (VkDeviceSize)m_extent.width * m_extent.height * 4;
	AllocatedBuffer staging;
	if (!m_memory.CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging))
	{
		vkDestroyCommandPool(m_device, pool, nullptr);
		return false;
	}

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence = VK_NULL_HANDLE;
	if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS)
	{
		m_memory.DestroyBuffer(staging);
		vkDestroyCommandPool(m_device, pool, nullptr);
		return false;
	}

	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(command, &begin);

	const VkImage image = m_images[m_imageIndex].image;
	CmdTransition(command, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { m_extent.width, m_extent.height, 1 };
	vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);

	CmdTransition(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

	vkEndCommandBuffer(command);

	VkCommandBufferSubmitInfo commandInfo = {};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	commandInfo.commandBuffer = command;
	VkSubmitInfo2 submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.commandBufferInfoCount = 1;
	submit.pCommandBufferInfos = &commandInfo;
	QueueSubmit2(m_queue, 1, &submit, fence);
	vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

	bool ok = false;
	if (staging.mapped != nullptr)
	{
		const uint8_t* source = static_cast<const uint8_t*>(staging.mapped);
		const bool swapRedBlue = (m_swapFormat == VK_FORMAT_B8G8R8A8_UNORM ||
		                          m_swapFormat == VK_FORMAT_B8G8R8A8_SRGB);

		// Keep the thumbnail 4:3 even when the game is displayed widescreen:
		// crop the centre of the presented image first, then scale it.
		uint32_t cropWidth = m_extent.width;
		uint32_t cropHeight = m_extent.height;
		uint32_t cropX = 0;
		uint32_t cropY = 0;
		if ((uint64_t)cropWidth * 3 > (uint64_t)cropHeight * 4)
		{
			cropWidth = cropHeight * 4 / 3;
			cropX = (m_extent.width - cropWidth) / 2;
		}
		else if ((uint64_t)cropWidth * 3 < (uint64_t)cropHeight * 4)
		{
			cropHeight = cropWidth * 3 / 4;
			cropY = (m_extent.height - cropHeight) / 2;
		}

		std::vector<uint8_t> rgb((size_t)width * height * 3);
		for (uint32_t y = 0; y < height; ++y)
		{
			const uint32_t sourceY = cropY + y * cropHeight / height;
			for (uint32_t x = 0; x < width; ++x)
			{
				const uint32_t sourceX = cropX + x * cropWidth / width;
				const uint8_t* texel = source + ((size_t)sourceY * m_extent.width + sourceX) * 4;
				uint8_t* target = rgb.data() + ((size_t)y * width + x) * 3;
				target[0] = swapRedBlue ? texel[2] : texel[0];
				target[1] = texel[1];
				target[2] = swapRedBlue ? texel[0] : texel[2];
			}
		}

		ok = !Util::PNG::WriteRGB(pngPath, rgb.data(), (int32_t)width, (int32_t)height);
	}

	vkDestroyFence(m_device, fence, nullptr);
	m_memory.DestroyBuffer(staging);
	vkDestroyCommandPool(m_device, pool, nullptr);
	return ok;
}

VkImageView CVulkanContext::CreateView(VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t baseMip, uint32_t mipCount)
{
	VkImageViewCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.image = image;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.format = format;
	info.subresourceRange.aspectMask = aspect;
	info.subresourceRange.baseMipLevel = baseMip;
	info.subresourceRange.levelCount = mipCount;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = 1;
	VkImageView view = VK_NULL_HANDLE;
	vkCreateImageView(m_device, &info, nullptr, &view);
	return view;
}

void CVulkanContext::DestroyView(VkImageView view)
{
	if (m_device && view)
		vkDestroyImageView(m_device, view, nullptr);
}

} // Vulkan3D
