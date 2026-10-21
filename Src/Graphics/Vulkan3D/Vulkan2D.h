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
#ifndef INCLUDED_VULKAN3D_2D_H
#define INCLUDED_VULKAN3D_2D_H

#include "Graphics/IRender2D.h"
#include "VulkanMemory.h"
#include "Util/NewConfig.h"

namespace Vulkan3D {

class CVulkanContext;

class CVulkan2D final : public IRender2D
{
public:
	CVulkan2D(const Util::Config::Node& config, CVulkanContext& context);
	~CVulkan2D() override;

	void BeginFrame(void) override;
	void PreRenderFrame(void) override;
	void RenderFrameBottom(void) override;
	void RenderFrameTop(void) override;
	void EndFrame(void) override;
	void WriteVRAM(unsigned addr, uint32_t data) override;
	void AttachRegisters(const uint32_t* regPtr) override;
	void AttachPalette(const uint32_t* palPtr[2]) override;
	void AttachVRAM(const uint8_t* vramPtr) override;
	void AttachDrawBuffers(std::shared_ptr<TileGenBuffer> bottom, std::shared_ptr<TileGenBuffer> top) override;
	Result Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned aaTarget, UpscaleMode upscaleMode) override;

	void SetSceneColor(VkImageView view);

	// Re-reads the post-processing parameters so an edit applies on the next frame. Safe to call
	// every frame; see the comment on the implementation.
	void RefreshPostSettings();

	/*
	 * Where an extra layer is drawn over the finished frame - the in-game settings overlay.
	 *
	 * A bare function pointer rather than a std::function: there is exactly one overlay for the life
	 * of the process, it is set once at start-up, and the renderer has no business owning or copying
	 * anything to call it.
	 *
	 * The renderer opens a rendering scope around the call, because the ImGui backend only issues
	 * draw commands and expects the caller to have set the target up.
	 */
	void SetOverlayPass(void (*pass)(VkCommandBuffer)) { m_overlayPass = pass; }

private:
	bool CreatePipeline();
	bool CreatePostPipeline();
	bool EnsureComposite();
	void DestroyPost();
	void ReadPostSettingsFromConfig();
	void UploadLayer(int index);
	void DrawLayer(uint32_t layer, int x, int y, int w, int h);
	void DrawCompositePass(VkCommandBuffer cmd, VkImageView targetView);
	void DrawPostPass(VkCommandBuffer cmd);
	void DrawOverlayPass(VkCommandBuffer cmd);
	void GenerateCompositeMips(VkCommandBuffer cmd);

	// Bezel backdrop.
	bool LoadBezelImage(bool updateDescriptor = false);
	bool UploadBezel(VkCommandBuffer cmd);
	void UpdateBezelDescriptor();
	void DrawBezelPass(VkCommandBuffer cmd);
	void BezelGameRect(int& x, int& y, int& w, int& h) const;
	void PictureRect(int& x, int& y, int& w, int& h) const;

	const Util::Config::Node& m_config;
	CVulkanContext& m_context;
	unsigned m_xOff = 0, m_yOff = 0, m_xRes = 496, m_yRes = 384, m_totalX = 496, m_totalY = 384;
	unsigned m_correction = 0;
	bool m_wideBg = false;

	// The widescreen option, read here because the compositor needs it as well as the 3D renderer:
	// it decides how much of the output the picture is allowed to occupy, and therefore what the
	// scissor clips every layer to. Kept in step with CVulkan3D's copy of the same setting - both read
	// it from the same configuration at setup, so they cannot disagree.
	bool m_wide = false;
	bool m_drawBottom = false;
	bool m_drawTop = false;
	bool m_uploaded[2] = {};
	std::shared_ptr<TileGenBuffer> m_buffers[2];

	AllocatedImage m_layers[2];
	VkImageView m_sceneView = VK_NULL_HANDLE;

	/*
	 * Bezel backdrop.
	 *
	 * An image the user can put behind the black bars so the screen does not sit in a void. The
	 * picture is decoded on the CPU at startup and handed to the GPU on the first frame, because a
	 * single 4K-ish backdrop is far too large for the per-frame ring slice's comfort and never
	 * changes afterwards.
	 *
	 * Nothing here draws over the game: the pass that paints it is scissored to the four regions
	 * outside the picture. That is deliberately independent of the scene's alpha - the bars happen
	 * to be transparent today (the 3D target is cleared with alpha 0 and the composite blends), but
	 * relying on that would make the bezel disappear the day someone changes the clear value.
	 */
	AllocatedImage m_bezel;
	std::vector<uint8_t> m_bezelPixels;         // decoded, waiting to be uploaded
	uint32_t m_bezelWidth = 0;
	uint32_t m_bezelHeight = 0;
	bool m_bezelUploaded = false;
	bool m_bezelVisible = false;                // false when disabled or the file could not be read
	float m_bezelBrightness = 1.0f;
	VkSampler m_sampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
	VkDescriptorPool m_pool = VK_NULL_HANDLE;
	VkDescriptorSet m_set = VK_NULL_HANDLE;
	VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkShaderModule m_vert = VK_NULL_HANDLE;
	VkShaderModule m_frag = VK_NULL_HANDLE;
	std::vector<char> m_swapSeen;

	// Post-processing. When any filter is active the layers are composited into m_composite and the
	// post pass reads that image, so the filters see the finished frame rather than only the 3D
	// scene. With every filter off the compositor draws straight to the swapchain as before.
	AllocatedImage m_composite;
	// Level-0-only view, used as the colour attachment (see EnsureComposite).
	VkImageView m_compositeRenderView = VK_NULL_HANDLE;
	VkImageLayout m_compositeLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	// The composite carries a full mip chain. Generating it with vkCmdBlitImage gives a hardware box
	// filter at every level for free, so sampling several levels in the post pass produces a real
	// multi-scale glow without any blur passes of our own - which is what makes bloom affordable
	// without a general multi-pass framework.
	uint32_t m_compositeMips = 1;
	// Per-level mip layouts, so each can be transitioned correctly the first time it is used.
	std::vector<VkImageLayout> m_mipLayouts;
	VkDescriptorSetLayout m_postLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_postPool = VK_NULL_HANDLE;
	VkDescriptorSet m_postSet = VK_NULL_HANDLE;
	VkPipelineLayout m_postPipeLayout = VK_NULL_HANDLE;
	VkPipeline m_postPipeline = VK_NULL_HANDLE;
	VkShaderModule m_postFrag = VK_NULL_HANDLE;
	uint32_t m_postGeneration = ~0u;
	bool m_postReady = false;

	// Post-processing settings, resolved from the config in the constructor.
	bool m_fxaa = false;
	int m_crtMode = 0;
	float m_scanlineIntensity = 0.0f;
	float m_maskIntensity = 0.0f;
	float m_scanlineCount = 288.0f;
	int m_maskType = 1;                 // 0 aperture grille, 1 slot mask, 2 shadow mask
	float m_maskScale = 2.0f;           // output pixels per mask cell
	float m_glowStrength = 0.0f;
	int m_glowLevels = 4;
	int m_glowThreshold = 40;
	float m_crtGain = 1.0f;

	// Drawn over the finished frame, after the backdrop and before the screenshot copy. Null when the
	// overlay is not in use.
	void (*m_overlayPass)(VkCommandBuffer) = nullptr;
};

} // Vulkan3D

#endif
