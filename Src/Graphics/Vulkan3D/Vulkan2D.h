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
#include "UiBlur.h"
#include "VulkanMemory.h"
#include "Util/NewConfig.h"

namespace Vulkan3D {

class CVulkanContext;

/*
 * A light-gun crosshair, in window pixels.
 *
 * Feeds a pipeline of its own rather than the ImGui overlay. A crosshair is present on every frame of
 * a light-gun game, so drawing it through ImGui meant building a complete ImGui frame - new frame,
 * input, render, then a draw-data traversal on the GPU - for a shape made of two dozen vertices. The
 * native path costs one small draw call and leaves the ImGui frame to the panel and the statistics
 * readout, which are the things that actually need it.
 *
 * Coordinates already include the game viewport's offset; the renderer maps them straight onto the
 * swapchain. uiScale is the display scale the overlay would have applied, so the on-screen size does
 * not change from what the ImGui version drew.
 */
struct CrosshairState
{
	bool visible = false;
	unsigned players = 0;       // Bit 0 = player 1, bit 1 = player 2
	float x[2] = {};
	float y[2] = {};
	float uiScale = 1.0f;
};

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
	// The two transparency targets (slots 4 and 5), combined with the scene layer in composite.frag
	// using New3D's two-layer formula.
	void SetTransColor(VkImageView trans1, VkImageView trans2);
	/*
	 * Set when the 3D pass already folded the transparency layers into the scene target (see
	 * scene_composite.frag). The scene target is premultiplied either way; this only tells the
	 * compositor not to apply the two-layer block a second time.
	 */
	void SetScenePrecomposited(bool precomposited) { m_scenePrecomposited = precomposited; }
	/*
	 * Set for AlphaSortMode = 2. The two transparency targets then hold a checkerboard (trans1 on the
	 * even cells, trans2 on the odd ones) and the compositor reconstructs them accordingly instead of
	 * treating both as full-resolution coverage.
	 */
	void SetArcadeCheckerboard(bool checkerboard) { m_arcadeCheckerboard = checkerboard; }
	/*
	 * Test hook for the checkerboard mode: skip the 2x2 reconstruction so the one-pixel pattern stays
	 * visible instead of resolving into a half-strength blend (SM3D_CHECKER_RAW).
	 */
	void SetCheckerRaw(bool raw) { m_checkerRaw = raw; }

	// Replaces the crosshair drawn on the next frame. Safe to call every frame; an invisible state
	// simply skips the pass.
	void SetCrosshair(const CrosshairState& state) { m_crosshair = state; }

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

	/*
	 * Frosted-glass backdrop for whatever the overlay draws.
	 *
	 * The gate is asked once per frame, after the picture and the backdrop are finished and before
	 * the overlay is recorded, and answers "will something over this frame sample a blurred copy of
	 * it?". When it is null or returns false the whole blur chain is skipped: no passes, no barriers,
	 * no bandwidth. That is what keeps a closed in-game panel at exactly zero cost, which is the only
	 * way a purely cosmetic effect belongs in the present path at all.
	 *
	 * It has to be asked per frame rather than set on open/close because the panel can also be closed
	 * by its own button, and because the statistics readout is drawn whether or not the panel is.
	 */
	void SetUiFrostGate(bool (*gate)()) { m_uiFrostGate = gate; }

	/*
	 * The half-resolution blurred copy of the last frame the chain ran, for ImGui to sample.
	 *
	 * VK_NULL_HANDLE until the first frame has run the chain, and again for a frame after a resize
	 * (the old view is retired for a few frames so a descriptor that still names it stays valid).
	 * Callers register it with ImGui_ImplVulkan_AddTexture and re-register when the handle changes.
	 */
	VkImageView UiFrostView() const { return m_uiBlur.View(); }
	VkSampler UiFrostSampler() const { return m_uiBlur.Sampler(); }
	// Changes whenever the images are replaced, so a cached descriptor can be spotted as stale. A
	// resize can hand back the same view handle for different memory, so the handle alone is not a
	// safe identity for the image.
	uint32_t UiFrostGeneration() const { return m_uiBlur.Generation(); }

private:
	bool CreatePipeline();
	bool CreatePostPipeline();
	bool CreateCrosshairPipeline();
	bool CreateBorderPipeline();
	bool EnsureComposite();
	void DestroyPost();
	void ReadPostSettingsFromConfig();
	void UploadLayer(int index);
	void DrawLayer(uint32_t layer, int x, int y, int w, int h, int upscaleMode);
	void DrawCompositePass(VkCommandBuffer cmd, VkImageView targetView);
	void DrawPostPass(VkCommandBuffer cmd);
	void DrawOverlayPass(VkCommandBuffer cmd);
	void DrawCrosshairPass(VkCommandBuffer cmd);
	void DrawBorderPass(VkCommandBuffer cmd);
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

	/*
	 * Low-resolution rendering.
	 *
	 * When set, the 3D renderer owns a fixed 496x384 scene target instead of a swapchain-sized one,
	 * so the scene layer has to be upscaled onto the picture rectangle rather than drawn 1:1 across
	 * the frame. Read once, in Init: it also decides the size of the scene targets, and those cannot
	 * change under a recording command buffer - same lifetime as MSAA.
	 *
	 * Widescreen is disabled while it is on: a widened frustum needs a scene target the width of the
	 * output, which is exactly what this option removes. The settings window greys it out so the
	 * precedence is visible rather than silent.
	 */
	bool m_lowRes = false;

	// The widescreen option, read here because the compositor needs it as well as the 3D renderer:
	// it decides how much of the output the picture is allowed to occupy, and therefore what the
	// scissor clips every layer to. Kept in step with CVulkan3D's copy of the same setting - both read
	// it from the same configuration at setup, so they cannot disagree.
	bool m_wide = false;
	// How the tile layers are magnified. Init() seeds it and forces Nearest at the native resolution,
	// matching CRender2D so a 1:1 image is not softened by a linear filter. RefreshPostSettings()
	// re-reads it every frame so the in-game panel can change it live.
	UpscaleMode m_upscaleMode = UpscaleMode::Bilinear;
	bool m_native2D = false;
	bool m_drawBottom = false;
	bool m_drawTop = false;
	bool m_uploaded[2] = {};
	std::shared_ptr<TileGenBuffer> m_buffers[2];

	AllocatedImage m_layers[2];
	VkImageView m_sceneView = VK_NULL_HANDLE;
	VkImageView m_trans1View = VK_NULL_HANDLE;
	VkImageView m_trans2View = VK_NULL_HANDLE;

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
	// Nearest filtering for UpscaleMode::Nearest. Both samplers are bound to the composite set, so
	// the mode can change per layer from one draw to the next without rebuilding anything.
	VkSampler m_nearestSampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
	VkDescriptorPool m_pool = VK_NULL_HANDLE;
	VkDescriptorSet m_set = VK_NULL_HANDLE;
	VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
	// Same shader and state, different colour blend: ONE / ONE_MINUS_SRC_ALPHA instead of
	// SRC_ALPHA / ONE_MINUS_SRC_ALPHA. Used only for layer 1, whose 3D scene is stored
	// premultiplied (the tile layers and the backdrop are straight-alpha and keep the other one).
	VkPipeline m_pipelineScene = VK_NULL_HANDLE;
	bool m_scenePrecomposited = false;
	bool m_arcadeCheckerboard = false;
	bool m_checkerRaw = false;
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
	// Regional CRT colour adaptation (SuperAA's 0..5 presets), applied in post.frag in the linear
	// domain before the scanline and mask stages.
	int m_crtColors = 0;

	// Drawn over the finished frame, after the backdrop and before the screenshot copy. Null when the
	// overlay is not in use.
	void (*m_overlayPass)(VkCommandBuffer) = nullptr;

	// Frosted-glass backdrop. Owned here because this is the class that has the finished frame in
	// hand; the overlay only reads the result. See UiBlur.h.
	CUiBlur m_uiBlur;
	bool (*m_uiFrostGate)() = nullptr;
	// UiFrostedGlass from the configuration, re-read every frame so the in-game panel can turn the
	// effect off. Off is a plain tinted panel, which is what a GPU that cannot afford the passes
	// wants - and what the OpenGL renderer draws anyway.
	bool m_uiFrostEnabled = true;

	// The light-gun crosshair. Its own pipeline because it has no texture, no descriptor set and no
	// dependency on the overlay: it must still work when the in-game panel is not available.
	CrosshairState m_crosshair;
	VkPipelineLayout m_crosshairLayout = VK_NULL_HANDLE;
	VkPipeline m_crosshairPipeline = VK_NULL_HANDLE;
	VkShaderModule m_crosshairVert = VK_NULL_HANDLE;
	VkShaderModule m_crosshairFrag = VK_NULL_HANDLE;

	// Sinden light-gun border. Drawn in swapchain coordinates after the picture and the backdrop,
	// so neither the CRT filter nor the cabinet picture can move or soften it.
	bool m_sindenBorder = false;
	int m_sindenBorderWidth = 16;
	float m_sindenBorderBrightness = 1.0f;
	VkPipelineLayout m_borderLayout = VK_NULL_HANDLE;
	VkPipeline m_borderPipeline = VK_NULL_HANDLE;
	VkShaderModule m_borderVert = VK_NULL_HANDLE;
	VkShaderModule m_borderFrag = VK_NULL_HANDLE;
};

} // Vulkan3D

#endif
