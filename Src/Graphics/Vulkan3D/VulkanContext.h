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

namespace OSD
{
namespace Platform
{
struct Window;
}
}

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
	//
	// pipelineCacheTag names the on-disk pipeline cache (Data/Cache/pipelines-<tag>.bin). The settings
	// window and a game session each own a context and are alive at the same time, so they must not
	// share one file: whichever was destroyed last would write its older copy over the other's
	// entries. Two files cost a second warm-up and lose nothing.
	Result InitDevice(OSD::Platform::Window* window, bool validation, bool vsync, const std::string& deviceName = std::string(), bool disableOverlayLayers = false, const std::string& pipelineCacheTag = std::string("game"));
	void Shutdown();

	// Acquire the next swapchain image and begin the frame command buffer.
	// Returns false when the frame should be skipped (window minimised or
	// swapchain out of date).
	bool BeginFrame();
	void EndFrame();
	bool RecreateSwapchain();
	// volk's entry points are process-wide, not per context.  Creating a second CVulkanContext
	// (a game session) rebinds them to that instance and device, and destroying it leaves them
	// pointing at the loader trampolines of a destroyed instance - the next instance-level call,
	// which is present, then faults inside ntdll.  Call this on a context that has to keep working
	// after another one has been created and destroyed.
	void RebindFunctionPointers();
	// CVulkan2D calls this after the swapchain image is in PRESENT_SRC.
	// If a frame is acquired but nothing renders, EndFrame still presents it.
	void MarkSwapchainReady();

	/*
	 * Display feedback through VK_KHR_present_wait.
	 *
	 * Every present carries an id, and vkWaitForPresentKHR blocks until the display has scanned that
	 * image out. Without it the only timing information available is when vkQueuePresentKHR returned,
	 * which is a queue insertion rather than a picture - the difference between pacing against the
	 * display and pacing against a guess.
	 *
	 * The wait is bounded by the caller, so a compositor that does not report, or a minimised window
	 * where nothing is scanned out, can cost the caller only the timeout it was going to spend anyway.
	 * Returns true when the image reached the screen, false on timeout, when the extension is
	 * unavailable, or when no present has been submitted on the current swapchain.
	 */
	bool WaitForPresentDisplay(uint64_t timeoutNs);
	bool PresentWaitAvailable() const { return m_presentWait; }
	// Whether anything has been presented on the current swapchain, i.e. whether a wait has an id
	// to ask about. Before the first present the answer is always "not displayed", which would
	// otherwise be read as "the phase is wrong".
	bool HasPresented() const { return m_swapchainPresentId != 0; }

	bool IsRecording() const { return m_recording; }
	VkCommandBuffer Cmd() const;
	VkDevice Device() const { return m_device; }
	// Read by every pipeline creation call: the driver stores compiled pipelines here and the whole
	// cache is written back on shutdown, so a session does not recompile the same shaders from cold.
	VkPipelineCache PipelineCache() const { return m_pipelineCache; }
	VkPhysicalDevice PhysicalDevice() const { return m_physical; }
	// The device's own name, so the statistics readout can label its GPU line. Empty until the device is
	// created; it comes from the physical-device properties, which is where the driver's marketing
	// string lives.
	const std::string& DeviceName() const { return m_deviceName; }
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
	// Whether VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE is legal on this device. The texture cache
	// builds one sampler per wrap-mode pair and has to fall back when the feature is absent.
	bool SamplerMirrorClampSupported() const { return m_samplerMirrorClamp; }
	// Required by the sorted-transparency pass: it allocates nodes with atomics and writes to an
	// image from the fragment stage. A device without these cannot run AlphaSortMode = 1.
	bool FragmentStoresAndAtomicsSupported() const { return m_fragmentStoresAndAtomics; }
	// Whether the resolve can run once per sample instead of once per pixel. Only used to keep
	// antialiased translucent edges under MSAA; without it the mode still works at pixel rate.
	bool SampleRateShadingSupported() const { return m_sampleRateShading; }
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

	/*
	 * Synchronous 4:3 thumbnail capture for the save-state container.
	 *
	 * Called from the paused game loop, so vkDeviceWaitIdle is acceptable
	 * here.  The output is 8-bit RGB PNG, scaled from the centre 4:3 region of
	 * the last presented swapchain image.  Returns true on success.
	 */
	bool CaptureThumbnail(const std::string& pngPath, uint32_t width, uint32_t height);
	// Records the copy and the required layout transitions. Leaves the image back in
	// COLOR_ATTACHMENT_OPTIMAL so the caller's transition to PRESENT_SRC still applies.
	void RecordScreenshotCopy(VkCommandBuffer cmd);

	/*
	 * Save-state thumbnail.
	 *
	 * Uses the same deferred readback as a screenshot, with two differences. The copy is recorded
	 * before the in-game panel, the crosshair and the cabinet backdrop are drawn, so the thumbnail
	 * shows the picture rather than the interface - RecordScreenshotCopy() runs at the end of the
	 * frame and deliberately includes them. And the worker scales the frame down to the requested
	 * size instead of writing a full-resolution PNG.
	 *
	 * The file is written under a ".part" name and renamed when complete, so the caller polling for
	 * it can never read a half-written PNG.
	 */
	void RequestThumbnail(const std::string& path, uint32_t width, uint32_t height);
	void RecordThumbnailCopy(VkCommandBuffer cmd);

private:
	void WriteScreenshot();
	// Shared body of the two capture paths above: copies the current swapchain image into the staging
	// buffer and marks the frame slot for readback. Returns false when a capture is already in flight.
	bool RecordFrameCopy(VkCommandBuffer cmd);
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
	bool CreatePipelineCache(const std::string& tag);
	void SavePipelineCache();

	OSD::Platform::Window* m_window = nullptr;
	bool m_validation = false;
	// Developer-only validation modes, requested through the environment so they never end up in a
	// release configuration: SM3D_VK_SYNC_VALIDATION=1 (race detection) and
	// SM3D_VK_GPU_VALIDATION=1 (shader/descriptor access checking). Both are far too slow to leave on.
	bool m_syncValidation = false;
	bool m_gpuValidation = false;
	// Set for the settings window: implicit overlay layers (Steam, RTSS, ReShade, ...) are not
	// useful on an ImGui surface and each one adds its load time to vkCreateInstance.  The game
	// window passes false, so overlays still attach to the picture that matters.
	bool m_disableOverlayLayers = false;
	bool m_vsync = true;
	std::string m_devicePreference;   // requested physical device name (empty = automatic)
	bool m_recording = false;
	bool m_swapReady = false;
	bool m_pushDescriptors = false;
	bool m_memoryBudget = false;
	bool m_samplerMirrorClamp = false;
	bool m_fragmentStoresAndAtomics = false;
	bool m_sampleRateShading = false;
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

	/*
	 * VK_KHR_present_wait state.
	 *
	 * m_presentId counts every present since the device was created and never goes backwards: the spec
	 * requires an id to be larger than every id previously submitted for the same swapchain, and a
	 * global counter stays valid across a recreation. m_swapchainPresentId is the last id submitted on
	 * the swapchain that exists now, which is the only one a wait may reference - after a recreation the
	 * old presents belong to a destroyed handle.
	 */
	bool m_presentWait = false;
	uint64_t m_presentId = 0;
	uint64_t m_swapchainPresentId = 0;

	VkInstance m_instance = VK_NULL_HANDLE;	VkDebugUtilsMessengerEXT m_debug = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkPhysicalDevice m_physical = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_queue = VK_NULL_HANDLE;
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
	std::string m_pipelineCachePath;
	std::string m_deviceName;

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

	// Thumbnail state. Shares the staging buffer and the worker with screenshots: only one capture is
	// ever in flight, and m_captureIsThumbnail says how the pending one should be written out.
	int m_thumbnailRequests = 0;
	std::string m_thumbnailPath;
	uint32_t m_thumbnailWidth = 320;
	uint32_t m_thumbnailHeight = 240;
	bool m_captureIsThumbnail = false;
};

} // Vulkan3D

#endif
