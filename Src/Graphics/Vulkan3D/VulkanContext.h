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
#ifndef INCLUDED_VULKAN3D_CONTEXT_H
#define INCLUDED_VULKAN3D_CONTEXT_H

#include "VulkanRingBuffer.h"
#include "Types.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

struct SDL_Window;

namespace Vulkan3D {

// Names of every Vulkan physical device on the system, in enumeration order. Used by the
// settings window to offer a GPU choice. Creates and destroys a throw-away VkInstance, so it
// is independent of any CVulkanContext.
std::vector<std::string> EnumeratePhysicalDeviceNames();

class CVulkanContext
{
public:
	CVulkanContext();
	~CVulkanContext();

	// deviceName selects the Vulkan physical device by name. An empty string picks the best
	// available device automatically.
	Result InitDevice(SDL_Window* window, bool validation, bool vsync, const std::string& deviceName = std::string());
	void Shutdown();

	// Acquire the next swapchain image and begin the frame command buffer.
	// Returns false when the frame should be skipped (window minimised or
	// swapchain out of date).
	bool BeginFrame();
	void EndFrame();
	bool RecreateSwapchain();
	// CVulkan2D calls this after the swapchain image is in PRESENT_SRC.
	// If a frame is acquired but nothing renders, EndFrame still presents it.
	void MarkSwapchainReady();

	bool IsRecording() const { return m_recording; }
	VkCommandBuffer Cmd() const;
	VkDevice Device() const { return m_device; }
	VkPhysicalDevice PhysicalDevice() const { return m_physical; }
	VkQueue Queue() const { return m_queue; }
	uint32_t QueueFamily() const { return m_queueFamily; }
	// Needed by the ImGui Vulkan backend, which fills in ImGui_ImplVulkan_InitInfo from these.
	VkInstance Instance() const { return m_instance; }
	uint32_t SwapImageCount() const { return (uint32_t)m_images.size(); }
	uint32_t FrameIndex() const { return m_frame; }
	uint32_t ImageIndex() const { return m_imageIndex; }
	VkExtent2D Extent() const { return m_extent; }
	VkFormat SwapFormat() const { return m_swapFormat; }
	VkImage SwapImage() const;
	VkImageView SwapView() const;
	CVulkanMemory& Memory() { return m_memory; }
	CVulkanRingBuffer& Ring() { return m_ring; }
	uint64_t SwapchainGeneration() const { return m_generation; }
	VkDeviceSize UniformAlignment() const { return m_uniformAlign; }
	VkDeviceSize StorageAlignment() const { return m_storageAlign; }
	bool HasPushDescriptors() const { return m_pushDescriptors; }
	bool Validation() const { return m_validation; }
	/*
	 * Whether the device is running the backend through Vulkan 1.3 core or through the 1.2 extensions
	 * behind the same two capabilities. Purely informational - both routes do the same work - but it is
	 * the difference between a bug report that can be acted on and one that cannot.
	 */
	bool IsVulkan13() const { return m_vulkan13; }

	VkImageView CreateView(VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t baseMip, uint32_t mipCount);
	void DestroyView(VkImageView view);

	/*
	 * Screenshot support.
	 *
	 * Reading the swapchain back is the only way to capture the finished frame: the presented image
	 * is what the player sees, after compositing and post-processing. The Vulkan backend has no
	 * equivalent of glReadPixels, so the image is copied into a host-visible buffer instead.
	 *
	 * The whole capture is arranged so that it never stalls the main loop. Emulated audio is
	 * generated once per frame from that loop, so any pause long enough to drain the sound buffer
	 * makes the music stutter - which is exactly what happened when the GPU wait and the PNG
	 * compression were done inline. So:
	 *
	 *   - the GPU copy is recorded into the current frame and read back when that frame slot comes
	 *     round again, by which time BeginFrame()'s fence wait has already established it completed;
	 *   - the pixel conversion, compression and file write happen on a worker thread.
	 *
	 * Because the worker reads the staging buffer, a new capture is not started while one is in
	 * flight. Screenshots are taken by hand and are seconds apart, so that costs nothing.
	 *
	 * Call RequestScreenshot() with the path to write. On a later frame the compositor calls
	 * RecordScreenshotCopy() (before the image goes to PRESENT_SRC).
	 */
	void RequestScreenshot(const std::string& path);
	bool ScreenshotsAvailable() const { return m_swapTransferSrc; }
	// Records the copy and the required layout transitions. Leaves the image back in
	// COLOR_ATTACHMENT_OPTIMAL so the caller's transition to PRESENT_SRC still applies.
	void RecordScreenshotCopy(VkCommandBuffer cmd);

private:
	void WriteScreenshot();
	struct Frame
	{
		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
		VkSemaphore imageAvailable = VK_NULL_HANDLE;
	};
	struct SwapchainImage
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkSemaphore renderFinished = VK_NULL_HANDLE;
	};

	bool CreateInstance();
	bool PickDevice();
	bool CreateDevice();
	bool CreateSwapchain();
	void DestroySwapchain();
	bool CreateFrames();
	void DestroyFrames();

	SDL_Window* m_window = nullptr;
	bool m_validation = false;
	bool m_vsync = true;
	std::string m_devicePreference;   // requested physical device name (empty = automatic)
	bool m_recording = false;
	bool m_swapReady = false;
	bool m_pushDescriptors = false;
	bool m_vulkan13 = false;
	uint64_t m_generation = 0;
	uint32_t m_frame = 0;
	uint32_t m_imageIndex = 0;
	uint32_t m_queueFamily = 0;
	VkDeviceSize m_uniformAlign = 256;
	VkDeviceSize m_storageAlign = 256;
	VkExtent2D m_extent = {};
	VkFormat m_swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
	VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;

	VkInstance m_instance = VK_NULL_HANDLE;	VkDebugUtilsMessengerEXT m_debug = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkPhysicalDevice m_physical = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_queue = VK_NULL_HANDLE;
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;

	Frame m_frames[kMaxFramesInFlight];
	std::vector<SwapchainImage> m_images;
	std::vector<uint8_t> m_imageSeen;
	CVulkanMemory m_memory;
	CVulkanRingBuffer m_ring;

	// Screenshot state. The buffer is created on first use and reused; it is recreated when the
	// swapchain size changes.
	//
	// The copy is read back from the frame slot it was recorded in, on the next pass through that
	// same slot. BeginFrame() already waits on that slot's fence, so by then the copy has completed
	// and no additional stall is needed - which matters, because the emulated audio is generated once
	// per frame from the main loop and any stall long enough to drain the sound buffer makes the
	// music stutter.
	bool m_swapTransferSrc = false;
	// Counts outstanding requests rather than flagging one, so that a screenshot asked for while
	// another is still being written is not silently dropped.
	int m_screenshotRequests = 0;
	int32_t m_screenshotPendingSlot = -1;    // frame slot holding an unread capture, -1 = none
	std::string m_screenshotPath;
	AllocatedBuffer m_screenshotBuffer;
	VkDeviceSize m_screenshotSize = 0;
	// Compression and file writing run here, off the main loop, so a capture cannot starve the audio.
	std::thread m_screenshotThread;
	std::atomic<bool> m_screenshotBusy{ false };
};

} // Vulkan3D

#endif
