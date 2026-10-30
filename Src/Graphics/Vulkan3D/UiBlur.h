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
#ifndef INCLUDED_VULKAN3D_UI_BLUR_H
#define INCLUDED_VULKAN3D_UI_BLUR_H

#include "VulkanMemory.h"

#include <cstdint>
#include <vector>

namespace Vulkan3D {

class CVulkanContext;

/*
 * Dual Kawase blur of the finished frame, for frosted-glass interface panels.
 *
 * A frosted panel is a blurred copy of whatever is behind it. Getting that copy is the expensive
 * half of the effect, and a single large gaussian over a 4K frame is not affordable on the frame the
 * player presses Tab. This builds it as a two-level pyramid instead:
 *
 *     frame (1x, the swapchain) --blit, linear--> level0 (1/2)
 *     level0 --5-tap diagonal box--> level1 (1/4)
 *     level1 --8-tap tent, merged--> level0
 *     level0 --8-tap tent + grade--> output (1/2)   <- what ImGui samples
 *
 * The only full-resolution work is the blit that starts it, and the hardware does that one. Every
 * pass after it is at half resolution or below, which is why the whole chain is a few hundred
 * microseconds at 4K rather than the milliseconds a full-resolution separable gaussian costs.
 *
 * Two details are deliberate and easy to get wrong:
 *
 *  - The capture is a vkCmdBlitImage rather than a first downsample pass that samples the swapchain
 *    directly. A sampled capture would need a descriptor naming the current swapchain view, which
 *    changes with the acquired image index; blitting needs no descriptor at all, and its linear
 *    filter at exactly 0.5 is a clean 2x2 box average - a better first level than a 5-tap would give
 *    anyway, since aliasing introduced at the top of the chain cannot be removed later.
 *
 *  - Every image here uses the swapchain's own format. Not for the colours - they are all plain
 *    UNORM - but because a scaling blit with VK_FILTER_LINEAR is only allowed between *compatible*
 *    formats, and BGRA8 is not compatible with RGBA8. See Ensure().
 *
 * Ownership is deliberately narrow: this owns its images, its three pipelines and its descriptor
 * sets, and it knows nothing about ImGui, about who draws the panels or about when they are open.
 * The caller decides that, and the caller must skip Record() entirely on frames where nothing
 * samples the result - that is what makes the feature free when the in-game panel is closed.
 */
class CUiBlur final
{
public:
	CUiBlur(CVulkanContext& context);
	~CUiBlur();

	CUiBlur(const CUiBlur&) = delete;
	CUiBlur& operator=(const CUiBlur&) = delete;

	/*
	 * Creates (or re-creates) the chain for a full-resolution extent of fullExtent / 2.
	 *
	 * Cheap and idempotent once the size matches. Re-creating waits for the device, because the
	 * previous images may still be referenced by a descriptor the caller has registered with ImGui -
	 * the same reason CVulkan2D::EnsureComposite() does. Resizes are rare enough not to care.
	 *
	 * Returns false when an allocation or a pipeline failed; the caller then draws solid panels,
	 * which is a cosmetic loss and must never end a session.
	 */
	bool Ensure(VkExtent2D fullExtent);

	/*
	 * Records the whole chain: captures `source` (which must be in COLOR_ATTACHMENT_OPTIMAL on
	 * entry, and is left that way), blurs it, and leaves the result in View() in
	 * SHADER_READ_ONLY_OPTIMAL, ready to be sampled in the same command buffer.
	 *
	 * Recorded between render scopes - it opens and closes its own.
	 */
	void Record(VkCommandBuffer cmd, VkImage source);

	// The half-resolution result. VK_NULL_HANDLE until Ensure() has succeeded.
	VkImageView View() const { return m_output.view; }
	VkExtent2D Extent() const { return VkExtent2D{ m_output.width, m_output.height }; }
	// The sampler registered alongside View() with ImGui: linear, clamp-to-edge, no mips.
	VkSampler Sampler() const { return m_sampler; }
	/*
	 * Bumped every time the images are replaced.
	 *
	 * Callers cache a descriptor registered against View() and have to re-register when the image
	 * is rebuilt. Comparing the view handle is not enough: the allocator reuses addresses, so the
	 * replacement can land on the same handle while naming completely different memory.
	 */
	uint32_t Generation() const { return m_generation; }
	bool Ready() const { return m_ready; }

	// Releases everything. Waits for the device; call on teardown and after the ImGui backend that
	// registered View() has been shut down.
	void Destroy();

	// Grade knobs, applied by the final pass. Kept here rather than passed to Record() so the
	// renderer can read them from the configuration once per frame without threading five floats
	// through every call site.
	void SetGrade(float saturation, float grain, float highlightKnee);
	/*
	 * Which interface theme the grade is built for.
	 *
	 * Read from the configuration every frame by the renderer rather than pushed on a theme change,
	 * so flipping the combo box is visible on the next frame - including in a running game, where the
	 * in-game panel is the only interface there is.
	 */
	void SetLightMode(bool light) { m_lightMode = light; }
	float Saturation() const { return m_saturation; }
	float Grain() const { return m_grain; }
	float HighlightKnee() const { return m_knee; }
	bool LightMode() const { return m_lightMode; }

private:
	// Mirrors the push constant block shared by ui_blur_down.frag and ui_blur_up.frag.
	struct BlurPush
	{
		float texelSize[2];
		float padding[2];
	};

	// Mirrors the push constant block in ui_frost.frag.
	struct FrostPush
	{
		float texelSize[2];
		float saturation;
		float grain;
		float knee;
		// 1 for the warm light theme, 0 for the dark one. The two need opposite grades of the same
		// blurred picture - see ui_frost.frag.
		float lightMode;
		float padding[2];
	};

	/*
	 * An image set that has been replaced but may still be named by a descriptor the caller has
	 * registered with ImGui.
	 *
	 * Re-creating the chain on a resize has to publish a new view, but the draw data for the current
	 * frame was built before the resize was noticed and still names the old one. Destroying it there
	 * would leave ImGui sampling a freed image for one frame, which validation reports as a
	 * descriptor error and a driver is allowed to make fatal. Retiring for a few frames keeps the
	 * handle valid until the caller has had a chance to re-register.
	 */
	struct RetiredSet
	{
		AllocatedImage level0;
		AllocatedImage level1;
		AllocatedImage output;
		uint32_t framesLeft = 0;
	};

	bool CreatePipelines();
	void DestroyObjects();
	void DestroyImages();
	void RetireImages();
	void DrainRetired();
	// `load` keeps what the target already holds. The merge pass needs that: it blends the tent of
	// the coarse level over the fine level, so clearing the target first would blend against black.
	void RenderFullscreen(VkCommandBuffer cmd, VkPipeline pipeline, VkDescriptorSet set,
		VkImageView targetView, VkExtent2D extent, bool load, const void* pushData, uint32_t pushSize);
	void Transition(VkCommandBuffer cmd, AllocatedImage& image, VkImageLayout& layout, VkImageLayout next);

	CVulkanContext& m_context;

	// level0 carries the captured frame and, after the merge, the result of the coarse level.
	// level1 is the quarter-resolution copy. output is what ImGui samples, kept separate from level0
	// so the final grade and the grain are applied once rather than accumulating into an image that
	// is also an input.
	AllocatedImage m_level0;
	AllocatedImage m_level1;
	AllocatedImage m_output;
	VkImageLayout m_level0Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout m_level1Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout m_outputLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkFormat m_format = VK_FORMAT_UNDEFINED;
	// The swapchain extent the chain was built for. Kept separately because the half-resolution
	// levels round down: doubling their width back is one pixel short on an odd-sized window, and a
	// blit source rect has to stay inside the source image.
	VkExtent2D m_sourceExtent = {};
	uint32_t m_generation = 0;
	VkSampler m_sampler = VK_NULL_HANDLE;

	VkShaderModule m_vert = VK_NULL_HANDLE;
	VkShaderModule m_downFrag = VK_NULL_HANDLE;
	VkShaderModule m_upFrag = VK_NULL_HANDLE;
	VkShaderModule m_frostFrag = VK_NULL_HANDLE;

	VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_pool = VK_NULL_HANDLE;
	// One per pass, because the sampled image differs and a bound descriptor set may not be
	// rewritten while the command buffer that names it is still in flight.
	VkDescriptorSet m_downSet = VK_NULL_HANDLE;
	VkDescriptorSet m_upSet = VK_NULL_HANDLE;
	VkDescriptorSet m_frostSet = VK_NULL_HANDLE;

	VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
	VkPipeline m_downPipeline = VK_NULL_HANDLE;
	VkPipeline m_upPipeline = VK_NULL_HANDLE;
	VkPipeline m_frostPipeline = VK_NULL_HANDLE;

	float m_saturation = 1.16f;
	float m_grain = 1.5f / 255.0f;
	float m_knee = 0.72f;
	bool m_lightMode = true;
	std::vector<RetiredSet> m_retired;
	bool m_pipelinesReady = false;
	bool m_ready = false;
};

} // Vulkan3D

#endif
