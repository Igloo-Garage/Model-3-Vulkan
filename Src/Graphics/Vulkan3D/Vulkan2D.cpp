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
#include "Vulkan2D.h"
#include "VulkanContext.h"
#include "BezelLayout.h"
#include "OSD/Logger.h"
#include "OSD/FileSystemPath.h"
#include "Util/Format.h"
#include "Util/PNGFile.h"
#include "Shaders/composite_vert.spv.h"
#include "Shaders/composite_frag.spv.h"
#include "Shaders/post_frag.spv.h"
#include "Shaders/crosshair_vert.spv.h"
#include "Shaders/crosshair_frag.spv.h"
#include "Shaders/border_vert.spv.h"
#include "Shaders/border_frag.spv.h"

#include <cstring>
#include <cstdlib>
#include <string>
#include <algorithm>

namespace Vulkan3D {

// Mirrors the push constant block in composite.frag. Keep the two in step.
struct LayerPush
{
	uint32_t layer;
	float brightness;
	int32_t upscaleMode;    // UpscaleMode: 0 nearest, 1 biquintic, 2 bilinear, 3 bicubic
	// 1 when the 3D pass folded Real3D's transparency layers into the scene target already. The
	// shader then skips its own two-layer block; the layer's blend is the premultiplied one either
	// way (see m_pipelineScene).
	uint32_t sceneComposited;
	// 1 for AlphaSortMode = 2: the transparency targets hold a checkerboard, so the shader
	// reconstructs them at half resolution instead of treating both as full coverage.
	uint32_t checkerboard;
	// 1 skips that reconstruction and blends the pixel's own cell, which leaves the checkerboard
	// visible (SM3D_CHECKER_RAW). Test hook only.
	uint32_t checkerRaw;
};

/*
 * Sampled images the composite descriptor set provides.
 *
 *   0 bottom tile layer   1 3D scene   2 top tile layer   3 bezel backdrop
 *
 * Four places have to agree on this number: the descriptor set layout's binding count, the descriptor
 * pool's count for the same type, the descriptor write, and the `uLayers[]` array in composite.frag.
 * Naming it is not decoration - the layout is the one that is easy to forget, and forgetting it is
 * close to silent:
 *
 *   - the extra descriptor write is out of range, so it is dropped;
 *   - the shader's index into the array still compiles and still runs;
 *   - the sampled image is whatever lies beyond the array, which on this driver behaves as a
 *     completely transparent texture.
 *
 * The visible symptom is therefore "the backdrop does not appear", with no error anywhere - the
 * draw call happens, the bars are simply blended with nothing. Validation layers report it
 * (VUID-VkWriteDescriptorSet-descriptorCount-00311), which is the only reason to prefer them over
 * reading the code.
 *
 * The pool is the fourth place, and it fails differently: a pool smaller than the layout it has to
 * serve makes vkAllocateDescriptorSets fail outright, so nothing draws at all. It went unnoticed here
 * because the fail is not logged, the desktop GPU's driver tolerates the shortfall, and the count was
 * left at its pre-bezel value of 3 when the backdrop slot was added. On a driver that enforces the
 * limit, Vulkan2D cannot initialise and the session ends before the first frame - see the note on
 * CreatePipeline()'s failure logging.
 */
constexpr uint32_t kCompositeLayerCount = 6;

// Mirrors the push constant block in post.frag. Keep the two in step.
struct PostPush
{
	float resolution[2];
	float fxaaEnabled;
	float scanlineIntensity;
	float maskIntensity;
	float scanlineCount;
	int32_t crtMode;
	int32_t maskType;
	float maskScale;
	float glowStrength;
	float gain;
	int32_t glowLevels;
	int32_t glowThreshold;
	int32_t crtColors;          // Regional CRT colour preset (0 = off, 1..5), see post.frag.
};

/*
 * Mirrors the push constant block in crosshair.vert. Keep the two in step.
 *
 * The trailing padding is deliberate: the shader block ends with a float after a vec4, so its size
 * rounds up to 16 bytes. Declaring the same size here makes the push constant range cover exactly
 * what the shader declares instead of relying on the driver accepting a short one.
 */
struct CrosshairPush
{
	float centre[2];
	float extent[2];
	float colour[4];
	float scale;
	float padding[3];
};

// Mirrors the push constant block in border.vert / border.frag. Keep the three in step.
struct BorderPush
{
	float extent[2];
	float thickness;
	float brightness;
};

/*
 * ReadPostSettingsFromConfig():
 *
 * Pulls every post-processing parameter out of the configuration and into the members the render pass
 * uses, applying the same clamps the settings UI does.
 *
 * Shared by the constructor and RefreshPostSettings() rather than duplicated, because the two must
 * agree: a clamp applied in one and not the other would make a value behave differently after a live
 * edit than after a restart, which is exactly the kind of discrepancy that is blamed on "the overlay
 * is wrong" rather than on the clamp.
 *
 * Sets m_postReady, which is what decides whether the offscreen-and-post path is taken at all.
 */
void CVulkan2D::ReadPostSettingsFromConfig()
{
	const Util::Config::Node& config = m_config;

	m_fxaa = config["PostFXAA"].ValueAsDefault<bool>(false);
	m_crtMode = config["PostCRT"].ValueAsDefault<int>(0);

	// Scanline count defaults to the game's own vertical resolution so that, when the image is scaled
	// up to the display, one scanline lands on each original Model 3 row - which is what a 384-line
	// arcade monitor actually showed.
	int scanlines = config["PostScanlines"].ValueAsDefault<int>(0);
	m_scanlineCount = scanlines > 0 ? (float)scanlines : 384.0f;
	m_scanlineIntensity = (float)config["PostScanlineIntensity"].ValueAsDefault<int>(60) / 100.0f;
	m_maskIntensity = (float)config["PostMaskIntensity"].ValueAsDefault<int>(50) / 100.0f;
	m_maskType = config["PostMaskType"].ValueAsDefault<int>(1);
	// Pixels per mask cell. At 4K a 1:1 cell is finer than the eye can resolve, so this defaults to
	// 2 (see the comment in post.frag).
	m_maskScale = (float)config["PostMaskScale"].ValueAsDefault<int>(2);
	if (m_maskScale < 1.0f) m_maskScale = 1.0f;
	m_glowStrength = (float)config["PostGlow"].ValueAsDefault<int>(0) / 100.0f;
	// How many levels of the composite's mip chain the glow accumulates. More levels means a wider
	// halo; 4 is roughly a tenth of the screen height, which is in the region a real tube's scattered
	// light occupies.
	m_glowLevels = config["PostGlowLevels"].ValueAsDefault<int>(4);
	if (m_glowLevels < 1) m_glowLevels = 1;
	if (m_glowLevels > 8) m_glowLevels = 8;
	// Luma a level has to exceed before it contributes to the glow, as a percentage. Raising it keeps
	// the effect to genuine highlights; a low value lets broad bright areas such as grass, snow or a
	// bright sky glow too, which reads as the whole picture being lifted rather than as a halo.
	m_glowThreshold = config["PostGlowThreshold"].ValueAsDefault<int>(40);
	if (m_glowThreshold < 0) m_glowThreshold = 0;
	if (m_glowThreshold > 100) m_glowThreshold = 100;
	// The mask and scanlines remove light, so by default the CRT stage lifts the result back up to
	// roughly its original level. A real shadow mask passes only about a third of the light that
	// reaches it; a genuine tube makes that back up with an extremely high peak brightness (which is
	// exactly why an arcade monitor looks so punchy), and without an HDR pipeline the only honest
	// stand-in is a flat gain. Ignored when the CRT stage is off, where it stays neutral.
	m_crtGain = (float)config["PostGain"].ValueAsDefault<int>(190) / 100.0f;
	if (m_crtMode <= 0)
		m_crtGain = 1.0f;

	// The same five regional presets the OpenGL path passes to SuperAA. Out of range is treated as
	// off rather than clamped to a preset the user did not choose: a hand-edited value should not
	// silently change the picture, and a menu can only ever write 0..5 anyway.
	m_crtColors = config["CRTcolors"].ValueAsDefault<int>(0);
	if (m_crtColors < 0 || m_crtColors > 5)
		m_crtColors = 0;

	// Sinden light-gun border. Width is in output pixels and has to leave room for the picture, so
	// it is clamped against the smaller dimension at draw time as well as here.
	m_sindenBorder = config["SindenBorder"].ValueAsDefault<bool>(false);
	m_sindenBorderWidth = config["SindenBorderWidth"].ValueAsDefault<int>(16);
	if (m_sindenBorderWidth < 0)
		m_sindenBorderWidth = 0;
	if (m_sindenBorderWidth > 256)
		m_sindenBorderWidth = 256;
	m_sindenBorderBrightness = (float)config["SindenBorderBrightness"].ValueAsDefault<int>(100) / 100.0f;
	m_sindenBorderBrightness = std::clamp(m_sindenBorderBrightness, 0.0f, 1.0f);

	m_postReady = m_fxaa || m_crtMode > 0 || m_glowStrength > 0.0f || m_crtColors != 0;
}

/*
 * RefreshPostSettings():
 *
 * Re-reads the parameters so an edit takes effect on the next frame instead of after a restart.
 *
 * This is what makes the in-game overlay worth having: CRT settings are judged by eye against a
 * moving picture, and "change a value, quit, relaunch, look" is not a loop anybody can tune in.
 *
 * The only structural work is on the transition from "no filters" to "some filter". The post pipeline
 * is created at start-up only if something was already enabled, so a session that began with
 * everything off has no pipeline to bind - and binding VK_NULL_HANDLE would be a driver error, not a
 * visible failure. It is therefore created here on first need; the offscreen target it renders into
 * is the same one EnsureComposite() builds lazily every frame. Nothing is torn down on the way back,
 * because an unused offscreen image costs nothing and a later re-enable should not have to rebuild.
 *
 * MSAA is deliberately absent: the sample count is fixed when the render targets are created, so it
 * cannot change while they exist. The overlay says so next to the control.
 */
void CVulkan2D::RefreshPostSettings()
{
	ReadPostSettingsFromConfig();

	/*
	 * Widescreen and the wide background are not post-processing, but they are re-read on the same
	 * live path so the in-game overlay can toggle them without starting a new game. The compositor
	 * uses them for the picture rectangle (and therefore the bars and the backdrop geometry), so a
	 * stale value here would leave the 2D layer clipping a widened 3D scene back to 4:3.
	 */
	const bool wasWide = m_wide;
	// Low-resolution rendering needs the scene target to be the width of the picture, which a widened
	// frustum is not; it wins over widescreen, and the settings window greys that option out while it
	// is on. Same rule as CVulkan3D, from the same key, so the two cannot disagree about the picture.
	m_wide = m_config["WideScreen"].ValueAs<bool>() && !m_lowRes;
	m_wideBg = m_config["WideBackground"].ValueAs<bool>();

	// Frosted glass for the interface panels. Re-read here so the in-game panel can switch it off
	// without a restart; when it is off the overlay draws a solid tint over the picture instead.
	m_uiFrostEnabled = m_config["UiFrostedGlass"].ValueAsDefault<bool>(true);
	/*
	 * Which of the two interface palettes the blurred backdrop is graded for. Read from the
	 * configuration rather than asked of the interface layer: this is the renderer, and the theme is
	 * a setting like any other it already reads for the post pass. 0 is the warm light theme.
	 */
	m_uiBlur.SetLightMode(m_config["UITheme"].ValueAsDefault<int>(0) == 0);

	// The upscale filter is a per-draw push constant rather than part of the pipeline, so re-reading
	// it here is what makes the in-game control live. The native-size exception still applies: at
	// 496x384 a filter would only blur a 1:1 image.
	int upscale = m_config["UpscaleMode"].ValueAsDefault<int>((int)UpscaleMode::Bilinear);
	if (upscale < 0)
		upscale = 0;
	if (upscale > 3)
		upscale = 3;
	m_upscaleMode = m_native2D ? UpscaleMode::Nearest : (UpscaleMode)upscale;

	// A backdrop is not loaded at all while widescreen is on (no bars to fill), so turning the option
	// off mid-session has to bring it back. UploadBezel() copies it in on the next frame.
	if (wasWide && !m_wide && m_bezel.image == VK_NULL_HANDLE)
		LoadBezelImage(true);

	if (m_postReady && m_postPipeline == VK_NULL_HANDLE)
	{
		if (!CreatePostPipeline())
		{
			ErrorLog("Vulkan2D: could not create the post-processing pipeline; filters stay off.");
			m_postReady = false;
		}
	}
}

CVulkan2D::CVulkan2D(const Util::Config::Node& config, CVulkanContext& context)
	: m_config(config), m_context(context), m_uiBlur(context)
{
	m_wideBg = config["WideBackground"].ValueAs<bool>();
	m_wide = config["WideScreen"].ValueAs<bool>();

	// Post-processing settings. Any of these being active switches the compositor to the offscreen +
	// post pass path; with all of them off nothing about the old behaviour changes.
	ReadPostSettingsFromConfig();
}

CVulkan2D::~CVulkan2D()
{
	VkDevice device = m_context.Device();
	if (!device)
		return;
	vkDeviceWaitIdle(device);
	m_uiBlur.Destroy();
	DestroyPost();
	if (m_crosshairPipeline)
		vkDestroyPipeline(device, m_crosshairPipeline, nullptr);
	if (m_crosshairLayout)
		vkDestroyPipelineLayout(device, m_crosshairLayout, nullptr);
	if (m_crosshairVert)
		vkDestroyShaderModule(device, m_crosshairVert, nullptr);
	if (m_crosshairFrag)
		vkDestroyShaderModule(device, m_crosshairFrag, nullptr);
	if (m_borderPipeline)
		vkDestroyPipeline(device, m_borderPipeline, nullptr);
	if (m_borderLayout)
		vkDestroyPipelineLayout(device, m_borderLayout, nullptr);
	if (m_borderVert)
		vkDestroyShaderModule(device, m_borderVert, nullptr);
	if (m_borderFrag)
		vkDestroyShaderModule(device, m_borderFrag, nullptr);
	for (auto& layer : m_layers)
	{
		m_context.DestroyView(layer.view);
		layer.view = VK_NULL_HANDLE;
		m_context.Memory().DestroyImage(layer);
	}
	if (m_bezel.view != VK_NULL_HANDLE)
	{
		m_context.DestroyView(m_bezel.view);
		m_bezel.view = VK_NULL_HANDLE;
	}
	m_context.Memory().DestroyImage(m_bezel);
	if (m_pipeline)
		vkDestroyPipeline(device, m_pipeline, nullptr);
	if (m_pipelineScene)
		vkDestroyPipeline(device, m_pipelineScene, nullptr);
	if (m_pipeLayout)
		vkDestroyPipelineLayout(device, m_pipeLayout, nullptr);
	if (m_vert)
		vkDestroyShaderModule(device, m_vert, nullptr);
	if (m_frag)
		vkDestroyShaderModule(device, m_frag, nullptr);
	if (m_pool)
		vkDestroyDescriptorPool(device, m_pool, nullptr);
	if (m_layout)
		vkDestroyDescriptorSetLayout(device, m_layout, nullptr);
	if (m_sampler)
		vkDestroySampler(device, m_sampler, nullptr);
	if (m_nearestSampler)
		vkDestroySampler(device, m_nearestSampler, nullptr);
}

Result CVulkan2D::Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned, UpscaleMode upscaleMode)
{
	m_xOff = xOffset;
	m_yOff = yOffset;
	m_xRes = xRes;
	m_yRes = yRes;
	m_totalX = totalXRes;
	m_totalY = totalYRes;
	m_correction = (unsigned)((yRes / 384.0) * 2.0 + 0.5);
	m_wideBg = m_config["WideBackground"].ValueAs<bool>();
	m_lowRes = m_config["InternalResolution"].ValueAsDefault<int>(0) != 0;
	m_wide = m_config["WideScreen"].ValueAs<bool>() && !m_lowRes;
	// At the tile layer's native size a filter can only blur: there is no magnification to smooth,
	// and the offset correction is a whole number of pixels. CRender2D makes the same exception.
	m_native2D = (xRes == 496 && yRes == 384);
	m_upscaleMode = m_native2D ? UpscaleMode::Nearest : upscaleMode;

	if (m_layers[0].image)
		return Result::OKAY;

	for (int i = 0; i < 2; i++)
	{
	if (!m_context.Memory().CreateImage(496, 384, 1, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, m_layers[i]))
	{
		ErrorLog("Vulkan2D: failed to allocate the tile layer image.");
		return Result::FAIL;
	}
		m_layers[i].view = m_context.CreateView(m_layers[i].image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
	}
	if (!CreatePipeline())
	{
		ErrorLog("Vulkan2D: failed to create the composite pipeline.");
		return Result::FAIL;
	}
	// The crosshair is optional: a driver that cannot build this pipeline should cost the crosshair,
	// not the game. DrawCrosshairPass() skips itself while the pipeline is null.
	if (!CreateCrosshairPipeline())
		ErrorLog("Vulkan2D: failed to create the crosshair pipeline; crosshairs are unavailable.");
	if (!CreateBorderPipeline())
		ErrorLog("Vulkan2D: failed to create the light-gun border pipeline; the border is unavailable.");
	// The post-processing pipeline is only needed when a filter is actually active; creating it
	// lazily keeps the untouched path exactly as it was.
	if (m_postReady && !CreatePostPipeline())
	{
		ErrorLog("Vulkan2D: failed to create the post-processing pipeline; filters disabled.");
		m_postReady = false;
	}

	// Slot 3 is the bezel backdrop. The image is created here - its size comes from the decoded file
	// - but only filled on the first frame, because the upload needs a command buffer.
	LoadBezelImage();

	/*
	 * Build the frosted-backdrop chain now rather than on first use.
	 *
	 * The overlay registers the half-resolution view with ImGui before it builds its panel, and it
	 * can only do that if the image already exists. Creating it here means the very first Tab press
	 * draws glass rather than one frame of plain tint; a resize still costs one such frame, because
	 * the new view is only published once the chain has been rebuilt.
	 */
	if (!m_uiBlur.Ensure(m_context.Extent()))
		ErrorLog("Vulkan2D: frosted-glass backdrop unavailable; panels fall back to a plain tint.");

	VkDescriptorImageInfo infos[kCompositeLayerCount] = {};
	for (uint32_t i = 0; i < kCompositeLayerCount; i++)
	{
		infos[i].sampler = VK_NULL_HANDLE;
		infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
	infos[0].imageView = m_layers[0].view;
	infos[1].imageView = m_layers[0].view;
	infos[2].imageView = m_layers[1].view;
	// A descriptor has to name a real view even when the backdrop is off, so it falls back to a
	// tile layer that is known to exist. Nothing samples slot 3 in that case.
	infos[3].imageView = m_bezel.image != VK_NULL_HANDLE ? m_bezel.view : m_layers[0].view;
	// Slots 4 and 5 are the two transparency targets. SetTransColor() rewrites them whenever the
	// scene targets are rebuilt; until then they must still name a valid view.
	infos[4].imageView = m_layers[0].view;
	infos[5].imageView = m_layers[0].view;
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_set;
	write.dstBinding = 1;
	write.descriptorCount = kCompositeLayerCount;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	write.pImageInfo = infos;
	vkUpdateDescriptorSets(m_context.Device(), 1, &write, 0, nullptr);
	return Result::OKAY;
}

/*
 * Every failure in here is reported.
 *
 * The caller logs one line - "failed to create the composite pipeline" - and until this was added that
 * was the only trace of a failure that has several distinct causes: a shader module the driver will
 * not load, a descriptor pool too small for the set it serves, a pipeline layout the device rejects,
 * a graphics pipeline it will not compile. They cannot be told apart from the outside, and two of them
 * are silent by construction - vkAllocateDescriptorSets returns a code rather than logging, and a
 * shortfall in the pool count is easy to read as "the descriptor write will absorb it".
 *
 * A VkResult in the log turns the next occurrence into a one-line diagnosis instead of an experiment
 * on somebody else's hardware.
 */
bool CVulkan2D::CreatePipeline()
{
	VkDevice device = m_context.Device();
	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = composite_vert_spv_size;
	moduleInfo.pCode = composite_vert_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_vert) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: composite vertex shader failed to load.");
		return false;
	}
	moduleInfo.codeSize = composite_frag_spv_size;
	moduleInfo.pCode = composite_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_frag) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: composite fragment shader failed to load.");
		return false;
	}

	VkSamplerCreateInfo sampler = {};
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	// Required for the bloom: Vulkan defaults maxLod to 0, which would clamp every sample to mip
	// level 0 and make the glow do nothing. VK_LOD_CLAMP_NONE lets the shader pick any level.
	sampler.maxLod = VK_LOD_CLAMP_NONE;
	if (VkResult r = vkCreateSampler(device, &sampler, nullptr, &m_sampler); r != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: vkCreateSampler failed (%d).", (int)r);
		return false;
	}
	// Nearest, for UpscaleMode::Nearest and for the native-resolution case. Both are bound to the
	// composite set; the draw selects between them, so switching the mode costs nothing.
	sampler.magFilter = VK_FILTER_NEAREST;
	sampler.minFilter = VK_FILTER_NEAREST;
	if (VkResult r = vkCreateSampler(device, &sampler, nullptr, &m_nearestSampler); r != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: vkCreateSampler (nearest) failed (%d).", (int)r);
		return false;
	}

	VkDescriptorSetLayoutBinding bindings[3] = {};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	bindings[1].descriptorCount = kCompositeLayerCount;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	// binding 2 is the nearest sampler used by the tile-layer upscale modes.
	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.bindingCount = 3;
	setInfo.pBindings = bindings;
	if (VkResult r = vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &m_layout); r != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: vkCreateDescriptorSetLayout failed (%d).", (int)r);
		return false;
	}

	VkDescriptorPoolSize sizes[2] = {};
	sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
	sizes[0].descriptorCount = 2;
	sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	// Must match the layout's binding count above, not the number of layers that happen to be filled:
	// a pool smaller than the set it has to serve fails the allocation below, and the failure is total
	// (nothing is drawn) rather than partial.
	sizes[1].descriptorCount = kCompositeLayerCount;
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = sizes;
	if (VkResult r = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool); r != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: vkCreateDescriptorPool failed (%d).", (int)r);
		return false;
	}
	VkDescriptorSetAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.descriptorPool = m_pool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &m_layout;
	if (VkResult r = vkAllocateDescriptorSets(device, &alloc, &m_set); r != VK_SUCCESS)
	{
		// VK_ERROR_OUT_OF_POOL_MEMORY (or, on newer drivers, the INITIALIZATION_FAILED that a validation
		// error about pool sizes turns into) means the pool is smaller than the set it has to serve.
		ErrorLog("Vulkan2D: vkAllocateDescriptorSets failed (%d) - the descriptor pool is too small for "
			"the layout (%u sampled images per set).", (int)r, kCompositeLayerCount);
		return false;
	}

	VkDescriptorImageInfo sampInfo[2] = {};
	sampInfo[0].sampler = m_sampler;
	sampInfo[1].sampler = m_nearestSampler;
	VkWriteDescriptorSet sampWrite[2] = {};
	sampWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	sampWrite[0].dstSet = m_set;
	sampWrite[0].dstBinding = 0;
	sampWrite[0].descriptorCount = 1;
	sampWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	sampWrite[0].pImageInfo = &sampInfo[0];
	sampWrite[1] = sampWrite[0];
	sampWrite[1].dstBinding = 2;
	sampWrite[1].pImageInfo = &sampInfo[1];
	vkUpdateDescriptorSets(device, 2, sampWrite, 0, nullptr);

	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	range.size = sizeof(LayerPush);
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &m_layout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	if (VkResult r = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipeLayout); r != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: vkCreatePipelineLayout failed (%d).", (int)r);
		return false;
	}

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = m_vert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = m_frag;
	stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertex = {};
	vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	VkPipelineInputAssemblyStateCreateInfo assembly = {};
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	VkPipelineViewportStateCreateInfo viewport = {};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo raster = {};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multi = {};
	multi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState blend = {};
	blend.blendEnable = VK_TRUE;
	blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.colorBlendOp = VK_BLEND_OP_ADD;
	blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alphaBlendOp = VK_BLEND_OP_ADD;
	blend.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo blending = {};
	blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blending.attachmentCount = 1;
	blending.pAttachments = &blend;

	/*
	 * The scene layer needs the premultiplied source-over instead.
	 *
	 * The 3D scene target stores a premultiplied colour: opaque geometry has alpha 1 so nothing
	 * changes there, and the fog quad scales its colour by the fog amount before storing it. Both
	 * the subpass composite (scene_composite.glsl) and the compositor's own two-layer block leave the
	 * result premultiplied, so SRC_ALPHA here would multiply by the scene alpha a second time -
	 * darkening every pixel the sky shows through. Only layer 1 uses this; the tile layers and the
	 * bezel backdrop are straight alpha and keep the pipeline above.
	 */
	VkPipelineColorBlendAttachmentState blendScene = blend;
	blendScene.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	VkPipelineColorBlendStateCreateInfo blendingScene = blending;
	blendingScene.pAttachments = &blendScene;
	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamics;
	VkPipelineRenderingCreateInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	VkFormat format = m_context.SwapFormat();
	rendering.pColorAttachmentFormats = &format;

	VkGraphicsPipelineCreateInfo pipe = {};
	pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipe.pNext = &rendering;
	pipe.stageCount = 2;
	pipe.pStages = stages;
	pipe.pVertexInputState = &vertex;
	pipe.pInputAssemblyState = &assembly;
	pipe.pViewportState = &viewport;
	pipe.pRasterizationState = &raster;
	pipe.pMultisampleState = &multi;
	pipe.pColorBlendState = &blending;
	pipe.pDynamicState = &dynamic;
	pipe.layout = m_pipeLayout;
	VkResult pipeResult = vkCreateGraphicsPipelines(device, m_context.PipelineCache(), 1, &pipe, nullptr, &m_pipeline);
	if (pipeResult != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: vkCreateGraphicsPipelines failed (%d).", (int)pipeResult);
		return false;
	}
	pipe.pColorBlendState = &blendingScene;
	pipeResult = vkCreateGraphicsPipelines(device, m_context.PipelineCache(), 1, &pipe, nullptr, &m_pipelineScene);
	if (pipeResult != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: vkCreateGraphicsPipelines (scene layer) failed (%d).", (int)pipeResult);
		return false;
	}
	return true;
}

/*
 * CreateCrosshairPipeline():
 *
 * The smallest pipeline in this file: twelve vertices per crosshair, generated in the vertex shader
 * from gl_VertexIndex, no vertex buffer, no descriptor set, no sampler. It exists so that a frame
 * whose only overlay content is a crosshair does not have to build an ImGui frame - which is what it
 * used to do, once per frame, for a shape that does not need text, input or draw lists.
 *
 * A failure here is logged and otherwise ignored: the game runs without a crosshair rather than
 * refusing to start. DrawCrosshairPass() checks the pipeline before recording anything.
 */
bool CVulkan2D::CreateCrosshairPipeline()
{
	VkDevice device = m_context.Device();

	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = crosshair_vert_spv_size;
	moduleInfo.pCode = crosshair_vert_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_crosshairVert) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: crosshair vertex shader failed to load.");
		return false;
	}
	moduleInfo.codeSize = crosshair_frag_spv_size;
	moduleInfo.pCode = crosshair_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_crosshairFrag) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: crosshair fragment shader failed to load.");
		return false;
	}

	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = sizeof(CrosshairPush);
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_crosshairLayout) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: crosshair pipeline layout creation failed.");
		return false;
	}

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = m_crosshairVert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = m_crosshairFrag;
	stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertex = {};
	vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	VkPipelineInputAssemblyStateCreateInfo assembly = {};
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewport = {};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo raster = {};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multi = {};
	multi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Alpha blending, because the caller may hand over a translucent colour later; today both
	// crosshairs are fully opaque, and this keeps them identical to the ImGui triangles.
	VkPipelineColorBlendAttachmentState blend = {};
	blend.blendEnable = VK_TRUE;
	blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.colorBlendOp = VK_BLEND_OP_ADD;
	blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alphaBlendOp = VK_BLEND_OP_ADD;
	blend.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo blending = {};
	blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blending.attachmentCount = 1;
	blending.pAttachments = &blend;

	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamics;

	VkFormat format = m_context.SwapFormat();
	VkPipelineRenderingCreateInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = &format;

	VkGraphicsPipelineCreateInfo pipe = {};
	pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipe.pNext = &rendering;
	pipe.stageCount = 2;
	pipe.pStages = stages;
	pipe.pVertexInputState = &vertex;
	pipe.pInputAssemblyState = &assembly;
	pipe.pViewportState = &viewport;
	pipe.pRasterizationState = &raster;
	pipe.pMultisampleState = &multi;
	pipe.pColorBlendState = &blending;
	pipe.pDynamicState = &dynamic;
	pipe.layout = m_crosshairLayout;
	if (vkCreateGraphicsPipelines(device, m_context.PipelineCache(), 1, &pipe, nullptr, &m_crosshairPipeline) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: crosshair pipeline creation failed.");
		return false;
	}
	return true;
}

/*
 * CreateBorderPipeline():
 *
 * The Sinden light-gun border: four quads, no vertex buffer, no descriptor set, one push constant.
 * It is kept out of the crosshair pipeline because the crosshair is alpha-blended and takes its
 * geometry in window pixels from a different push constant block; sharing one layout would tie two
 * unrelated features to the same constants.
 */
bool CVulkan2D::CreateBorderPipeline()
{
	VkDevice device = m_context.Device();

	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = border_vert_spv_size;
	moduleInfo.pCode = border_vert_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_borderVert) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: border vertex shader failed to load.");
		return false;
	}
	moduleInfo.codeSize = border_frag_spv_size;
	moduleInfo.pCode = border_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_borderFrag) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: border fragment shader failed to load.");
		return false;
	}

	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	range.size = sizeof(BorderPush);
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_borderLayout) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: border pipeline layout creation failed.");
		return false;
	}

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = m_borderVert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = m_borderFrag;
	stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertex = {};
	vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	VkPipelineInputAssemblyStateCreateInfo assembly = {};
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewport = {};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo raster = {};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multi = {};
	multi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Opaque: the border is meant to be the brightest thing on the screen for the camera.
	VkPipelineColorBlendAttachmentState blend = {};
	blend.blendEnable = VK_FALSE;
	blend.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo blending = {};
	blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blending.attachmentCount = 1;
	blending.pAttachments = &blend;

	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamics;

	VkFormat format = m_context.SwapFormat();
	VkPipelineRenderingCreateInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = &format;

	VkGraphicsPipelineCreateInfo pipe = {};
	pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipe.pNext = &rendering;
	pipe.stageCount = 2;
	pipe.pStages = stages;
	pipe.pVertexInputState = &vertex;
	pipe.pInputAssemblyState = &assembly;
	pipe.pViewportState = &viewport;
	pipe.pRasterizationState = &raster;
	pipe.pMultisampleState = &multi;
	pipe.pColorBlendState = &blending;
	pipe.pDynamicState = &dynamic;
	pipe.layout = m_borderLayout;
	if (vkCreateGraphicsPipelines(device, m_context.PipelineCache(), 1, &pipe, nullptr, &m_borderPipeline) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: border pipeline creation failed.");
		return false;
	}

	return true;
}

void CVulkan2D::SetSceneColor(VkImageView view)
{
	if (view == m_sceneView || !m_set)
		return;
	m_sceneView = view;
	VkDescriptorImageInfo info = {};
	info.imageView = view ? view : m_layers[0].view;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_set;
	write.dstBinding = 1;
	write.dstArrayElement = 1;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	write.pImageInfo = &info;
	vkUpdateDescriptorSets(m_context.Device(), 1, &write, 0, nullptr);
}

void CVulkan2D::SetTransColor(VkImageView trans1, VkImageView trans2)
{
	if ((trans1 == m_trans1View && trans2 == m_trans2View) || !m_set)
		return;
	m_trans1View = trans1;
	m_trans2View = trans2;
	VkDescriptorImageInfo info[2] = {};
	info[0].imageView = trans1 ? trans1 : m_layers[0].view;
	info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	info[1].imageView = trans2 ? trans2 : m_layers[0].view;
	info[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_set;
	write.dstBinding = 1;
	write.dstArrayElement = 4;
	write.descriptorCount = 2;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	write.pImageInfo = info;
	vkUpdateDescriptorSets(m_context.Device(), 1, &write, 0, nullptr);
}

void CVulkan2D::BeginFrame(void)
{
	m_drawBottom = false;
	m_drawTop = false;
}

void CVulkan2D::UploadLayer(int index)
{
	if (!m_buffers[index] || !m_context.IsRecording())
		return;
	VkCommandBuffer cmd = m_context.Cmd();
	VkImageLayout oldLayout = m_uploaded[index] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	CmdTransition(cmd, m_layers[index].image, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
	VkDeviceSize offset = 0;
	void* ptr = nullptr;
	VkDeviceSize bytes = 512ull * 384ull * 4ull;
	if (!m_context.Ring().Alloc(bytes, 4, offset, ptr))
		return;
	std::memcpy(ptr, m_buffers[index]->data, (size_t)bytes);
	VkBufferImageCopy copy = {};
	copy.bufferOffset = offset;
	copy.bufferRowLength = 512;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent = { 496, 384, 1 };
	vkCmdCopyBufferToImage(cmd, m_context.Ring().Buffer(), m_layers[index].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	CmdTransition(cmd, m_layers[index].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
	m_uploaded[index] = true;
}

void CVulkan2D::PreRenderFrame(void)
{
	UploadLayer(0);
	UploadLayer(1);
}

void CVulkan2D::RenderFrameBottom(void) { m_drawBottom = true; }
void CVulkan2D::RenderFrameTop(void) { m_drawTop = true; }

/*
 * DrawLayer():
 *
 * Draws one layer into the given rectangle.
 *
 * The rectangle is a *viewport*, not a clip: the layer quad carries uv 0..1 across it, so the
 * rectangle is what maps the texture onto the screen. Anything that has to be cropped must be cropped
 * with the scissor instead - shrinking the rectangle scales the picture down rather than cutting it,
 * which is not the same thing at all and is exactly what went wrong the first time this clip was
 * added.
 *
 * The crop itself is the same one the OpenGL backend applies: the 2D tile layers are drawn correction
 * pixels away from the 3D scene (the two are known to disagree by that much - see the comment on
 * m_correction), and OpenGL hides the overhang with a scissor box inset by the same amount. Without it
 * the overhang is visible as a thin line of pixels along the edges, carrying whatever colour the game
 * drew just outside its active area - which a real cabinet's overscan hid.
 *
 * Applied to every layer, not just the 2D ones, because OpenGL's scissor is pipeline state and
 * therefore affects all of them.
 */
void CVulkan2D::DrawLayer(uint32_t layer, int x, int y, int w, int h, int upscaleMode)
{
	if (w <= 0 || h <= 0)
		return;

	VkExtent2D extent = m_context.Extent();

	// Viewport: the caller's rectangle, untouched.
	VkViewport viewport = {};
	viewport.x = (float)x;
	viewport.y = (float)y;
	viewport.width = (float)w;
	viewport.height = (float)h;
	viewport.maxDepth = 1.0f;

	// Scissor: the rectangle cropped to the visible picture, then clamped to the swapchain (Vulkan
	// rejects a negative scissor offset).
	int cx = x, cy = y, cw = w, ch = h;
	int px = 0, py = 0, pw = 0, ph = 0;
	PictureRect(px, py, pw, ph);
	if (cx < px) { cw -= px - cx; cx = px; }
	if (cy < py) { ch -= py - cy; cy = py; }
	if (cx + cw > px + pw) cw = px + pw - cx;
	if (cy + ch > py + ph) ch = py + ph - cy;

	if (cx < 0) { cw += cx; cx = 0; }
	if (cy < 0) { ch += cy; cy = 0; }
	if (cx >= (int)extent.width || cy >= (int)extent.height)
		return;
	if (cx + cw > (int)extent.width) cw = (int)extent.width - cx;
	if (cy + ch > (int)extent.height) ch = (int)extent.height - cy;
	if (cw <= 0 || ch <= 0)
		return;

	VkCommandBuffer cmd = m_context.Cmd();
	VkRect2D scissor = {};
	scissor.offset = { cx, cy };
	scissor.extent = { (uint32_t)cw, (uint32_t)ch };
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	// The scene target is premultiplied, so its layer blends with ONE / ONE_MINUS_SRC_ALPHA; the
	// tile layers and the backdrop are straight alpha and keep the SRC_ALPHA pipeline.
	VkPipeline pipeline = (layer == 1u && m_pipelineScene != VK_NULL_HANDLE) ? m_pipelineScene : m_pipeline;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	// Unattenuated: the scene and tile layers are the picture itself, not a backdrop.
	LayerPush push = { layer, 1.0f, upscaleMode,
		(layer == 1u && m_scenePrecomposited) ? 1u : 0u,
		(layer == 1u && m_arcadeCheckerboard) ? 1u : 0u,
		(layer == 1u && m_checkerRaw) ? 1u : 0u };
	vkCmdPushConstants(cmd, m_pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
	vkCmdDraw(cmd, 4, 1, 0, 0);
}

void CVulkan2D::DestroyPost()
{
	VkDevice device = m_context.Device();
	if (!device)
		return;
	if (m_postPipeline)
		vkDestroyPipeline(device, m_postPipeline, nullptr);
	if (m_postPipeLayout)
		vkDestroyPipelineLayout(device, m_postPipeLayout, nullptr);
	if (m_postFrag)
		vkDestroyShaderModule(device, m_postFrag, nullptr);
	if (m_postPool)
		vkDestroyDescriptorPool(device, m_postPool, nullptr);
	if (m_postLayout)
		vkDestroyDescriptorSetLayout(device, m_postLayout, nullptr);
	m_context.DestroyView(m_composite.view);
	m_composite.view = VK_NULL_HANDLE;
	m_context.DestroyView(m_compositeRenderView);
	m_compositeRenderView = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_composite);
	m_postGeneration = ~0u;
}

/*
 * EnsureComposite():
 *
 * Creates (or recreates after a resize) the offscreen image the layers are composited into when
 * post-processing is active, and points the post descriptor set at it.
 */
bool CVulkan2D::EnsureComposite()
{
	VkExtent2D extent = m_context.Extent();
	if (extent.width == 0 || extent.height == 0)
		return false;
	if (m_composite.image && m_composite.width == extent.width && m_composite.height == extent.height &&
		m_composite.format == m_context.SwapFormat())
		return true;

	if (m_context.Device())
		vkDeviceWaitIdle(m_context.Device());

	// Full mip chain down to 1x1. The level count is what determines how diffuse the widest glow
	// sample can be.
	m_compositeMips = 1;
	for (uint32_t size = (std::max)(extent.width, extent.height); size > 1; size >>= 1)
		m_compositeMips++;
	m_mipLayouts.assign(m_compositeMips, VK_IMAGE_LAYOUT_UNDEFINED);

	m_context.DestroyView(m_composite.view);
	m_composite.view = VK_NULL_HANDLE;
	m_context.DestroyView(m_compositeRenderView);
	m_compositeRenderView = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_composite);

	if (!m_context.Memory().CreateImage(extent.width, extent.height, m_compositeMips, m_context.SwapFormat(),
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, m_composite))
		return false;
	// The composite is created in the swapchain's own format. It is the target of the composite pipeline,
	// which is built for that format, and the source of the post pass - a different format here would
	// make the declared attachment format disagree with the real one (and on a BGRA surface would
	// exchange red and blue on every path through the offscreen image).
	//
	// Two views onto the same image, because the two uses have opposite requirements:
	//
	//   m_compositeRenderView - level 0 only. Vulkan requires a colour attachment view to cover
	//                           exactly one mip level, so a full-chain view cannot be rendered into;
	//                           doing so left the picture black.
	//   m_composite.view      - the whole chain, which the post pass needs in order to sample the
	//                           coarser levels for the glow.
	m_compositeRenderView = m_context.CreateView(m_composite.image, m_context.SwapFormat(), VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
	m_composite.view = m_context.CreateView(m_composite.image, m_context.SwapFormat(), VK_IMAGE_ASPECT_COLOR_BIT, 0, m_compositeMips);
	m_compositeLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkDescriptorImageInfo info = {};
	info.imageView = m_composite.view;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_postSet;
	write.dstBinding = 1;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	write.pImageInfo = &info;
	vkUpdateDescriptorSets(m_context.Device(), 1, &write, 0, nullptr);
	return m_postSet != VK_NULL_HANDLE;
}

bool CVulkan2D::CreatePostPipeline()
{
	VkDevice device = m_context.Device();
	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = post_frag_spv_size;
	moduleInfo.pCode = post_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_postFrag) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: post-processing fragment shader failed to load.");
		return false;
	}

	VkDescriptorSetLayoutBinding bindings[2] = {};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = bindings;
	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_postLayout) != VK_SUCCESS)
		return false;

	VkDescriptorPoolSize sizes[2] = {};
	sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
	sizes[0].descriptorCount = 1;
	sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	sizes[1].descriptorCount = 1;
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = sizes;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_postPool) != VK_SUCCESS)
		return false;

	VkDescriptorSetAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.descriptorPool = m_postPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &m_postLayout;
	if (vkAllocateDescriptorSets(device, &alloc, &m_postSet) != VK_SUCCESS)
		return false;

	VkDescriptorImageInfo samplerInfo = {};
	samplerInfo.sampler = m_sampler;
	VkWriteDescriptorSet samplerWrite = {};
	samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	samplerWrite.dstSet = m_postSet;
	samplerWrite.dstBinding = 0;
	samplerWrite.descriptorCount = 1;
	samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	samplerWrite.pImageInfo = &samplerInfo;
	vkUpdateDescriptorSets(device, 1, &samplerWrite, 0, nullptr);

	VkPushConstantRange pushRange = {};
	pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof(PostPush);
	VkPipelineLayoutCreateInfo pipeLayoutInfo = {};
	pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeLayoutInfo.setLayoutCount = 1;
	pipeLayoutInfo.pSetLayouts = &m_postLayout;
	pipeLayoutInfo.pushConstantRangeCount = 1;
	pipeLayoutInfo.pPushConstantRanges = &pushRange;
	if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &m_postPipeLayout) != VK_SUCCESS)
		return false;

	// Vertex module is shared with the composite pipeline; the shader is a plain full-screen quad
	// generated from gl_VertexIndex, so the same code works for both passes.
	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = m_vert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = m_postFrag;
	stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	VkPipelineViewportStateCreateInfo viewport = {};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo raster = {};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multi = {};
	multi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAtt = {};
	blendAtt.blendEnable = VK_FALSE;
	blendAtt.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo blend = {};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blendAtt;

	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamics;

	VkFormat colorFormat = m_context.SwapFormat();
	VkPipelineRenderingCreateInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = &colorFormat;

	VkGraphicsPipelineCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.pNext = &rendering;
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &vertexInput;
	info.pInputAssemblyState = &inputAssembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multi;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = m_postPipeLayout;

	if (vkCreateGraphicsPipelines(device, m_context.PipelineCache(), 1, &info, nullptr, &m_postPipeline) != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: post-processing pipeline creation failed.");
		return false;
	}
	return true;
}

void CVulkan2D::EndFrame(void)
{
	if (!m_context.IsRecording() || !m_pipeline)
		return;
	VkCommandBuffer cmd = m_context.Cmd();
	uint32_t imageIndex = m_context.ImageIndex();
	if (m_swapSeen.size() <= imageIndex)
		m_swapSeen.resize(imageIndex + 1, 0);
	VkImageLayout oldLayout = m_swapSeen[imageIndex] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
	// The submit waits on the acquire semaphore at COLOR_ATTACHMENT_OUTPUT; the transition has to be
	// ordered after that wait or it races vkAcquireNextImageKHR (see CmdTransition's override).
	CmdTransition(cmd, m_context.SwapImage(), oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
		0, 1, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

	// One-shot staging copy of the backdrop. Attempted every frame until it succeeds so that a ring
	// allocation failure simply retries rather than losing the picture for the whole session.
	UploadBezel(cmd);

	/*
	 * Pick up any edits made since the last frame - the in-game overlay writes straight into the
	 * config. This has to happen before the branch below, not inside the post pass: with every filter
	 * off the post pass is not called at all, so refreshing from there would make "switch a filter on
	 * mid-session" impossible to trigger - the one transition that actually needs work done.
	 */
	RefreshPostSettings();

	if (m_postReady && EnsureComposite())
	{
		// Compose into the offscreen image first, then let the filters work on the finished frame on
		// the way to the swapchain. The filters see the HUD and both tile layers as well as the 3D
		// scene, which is what makes a shadow mask look like it belongs to the display rather than
		// to the game.
		DrawCompositePass(cmd, m_compositeRenderView);
		// Only worth building the mip chain when something is actually going to sample it.
		if (m_glowStrength > 0.0f)
			GenerateCompositeMips(cmd);
		DrawPostPass(cmd);
	}
	else
	{
		// No filters: compose straight to the swapchain, exactly as before.
		DrawCompositePass(cmd, m_context.SwapView());
	}

	// A save-state thumbnail is captured here: the game is composited (and filtered, when CRT effects
	// are on) but the cabinet backdrop, the in-game panel and the crosshair have not been drawn yet,
	// so the thumbnail shows the picture rather than the interface. Screenshots deliberately capture
	// the finished frame instead - RecordScreenshotCopy() is called at the end.
	m_context.RecordThumbnailCopy(cmd);

	// After the filters, so the backdrop reads as the bezel around a screen rather than as part of
	// the picture: a shadow mask belongs on the glass, not on the cabinet behind it. With no filters
	// active this still runs, which is why it is outside the branch above.
	DrawBezelPass(cmd);

	// The Sinden border is the edge of the glass, not part of the picture: after the backdrop so a
	// cabinet image cannot hide it, before the panel so the panel can still be read over it.
	DrawBorderPass(cmd);

	/*
	 * Frosted backdrop for the panel, built from the frame as it stands now.
	 *
	 * After the picture, the CRT filter (inside DrawPostPass) and the cabinet backdrop, so the glass
	 * shows what is actually behind the panel rather than the 3D scene alone; before the overlay,
	 * because the overlay's draw commands sample it.
	 *
	 * Skipped entirely - no passes, no barriers - unless something is about to draw a frosted panel.
	 * That gate is the whole reason the effect is affordable: a session that never opens the panel
	 * pays for one boolean per frame.
	 */
	static bool frostRunning = false;
	if (m_uiFrostEnabled && m_uiFrostGate != nullptr && m_uiFrostGate() && m_uiBlur.Ensure(m_context.Extent()))
	{
		m_uiBlur.Record(cmd, m_context.SwapImage());
		// Logged on the transitions rather than per frame, so "does this cost anything when the panel
		// is closed?" is answerable from the log instead of from a profiler.
		if (!frostRunning)
		{
			frostRunning = true;
			InfoLog("Vulkan2D: frosted backdrop active (%ux%u, half of the %ux%u frame).",
				m_uiBlur.Extent().width, m_uiBlur.Extent().height,
				m_context.Extent().width, m_context.Extent().height);
		}
	}
	else if (frostRunning)
	{
		frostRunning = false;
		InfoLog("Vulkan2D: frosted backdrop idle; nothing is sampling it, so the chain is not recorded.");
	}

	// Last of all: the in-game settings panel, over everything, including the backdrop.
	DrawOverlayPass(cmd);

	// The crosshair is drawn after the panel because it must never be covered - the same ordering the
	// ImGui foreground draw list gave it before this became a pass of its own.
	DrawCrosshairPass(cmd);

	// Capture the finished frame if one was requested. Done here, while the image is still a colour
	// attachment, so RecordScreenshotCopy() can put it back the way the transition below expects.
	m_context.RecordScreenshotCopy(cmd);

	CmdTransition(cmd, m_context.SwapImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);
	m_swapSeen[imageIndex] = 1;
	m_context.MarkSwapchainReady();
}
/*
 * GenerateCompositeMips():
 *
 * Builds the mip chain of the composited frame with vkCmdBlitImage.
 *
 * This is what makes bloom affordable. The obvious way to get a wide soft glow is a chain of blur
 * passes, each with its own shader and render target, which is why the well-known CRT shaders are
 * fifteen to twenty passes. The hardware already knows how to downsample: blitting level n-1 into
 * level n applies a box filter, and after a few levels that is a very wide blur for a handful of
 * blit calls. The post pass then samples several levels at once, which is a multi-scale blur - the
 * same thing a hand-written chain would compute, at a fraction of the cost.
 *
 * Levels are transitioned individually because they are in different layouts part way through the
 * chain, which a whole-image barrier cannot express.
 */
void CVulkan2D::GenerateCompositeMips(VkCommandBuffer cmd)
{
	if (m_compositeMips <= 1 || m_composite.image == VK_NULL_HANDLE)
		return;

	VkImage img = m_composite.image;
	uint32_t w = m_composite.width;
	uint32_t h = m_composite.height;

	// Level 0 was just rendered as a colour attachment; it becomes the source of the first blit.
	CmdTransition(cmd, img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);

	for (uint32_t level = 1; level < m_compositeMips; ++level)
	{
		const uint32_t prevW = w;
		const uint32_t prevH = h;
		w = (w > 1) ? w / 2 : 1;
		h = (h > 1) ? h / 2 : 1;

		CmdTransition(cmd, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, level, 1);

		VkImageBlit region = {};
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.mipLevel = level - 1;
		region.srcSubresource.layerCount = 1;
		region.srcOffsets[0] = { 0, 0, 0 };
		region.srcOffsets[1] = { (int32_t)prevW, (int32_t)prevH, 1 };
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.mipLevel = level;
		region.dstSubresource.layerCount = 1;
		region.dstOffsets[0] = { 0, 0, 0 };
		region.dstOffsets[1] = { (int32_t)w, (int32_t)h, 1 };
		vkCmdBlitImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);

		// The level just written is the source for the next one.
		CmdTransition(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, level, 1);
	}

	// Hand the whole chain to the post pass.
	for (uint32_t level = 0; level < m_compositeMips; ++level)
		CmdTransition(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, level, 1);

	m_compositeLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

/*
 * DrawCompositePass():
 *
 * Draws the two tile layers and the 3D scene into targetView, which is either the swapchain or the
 * offscreen composite image when post-processing is active.
 */
void CVulkan2D::DrawCompositePass(VkCommandBuffer cmd, VkImageView targetView)
{
	if (targetView == m_compositeRenderView && m_compositeLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		CmdTransition(cmd, m_composite.image, m_compositeLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		m_compositeLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	VkRenderingAttachmentInfo color = {};
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = targetView;
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.clearValue.color = VkClearColorValue{ { 0, 0, 0, 1 } };
	VkRenderingInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea.extent = m_context.Extent();
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &color;
	CmdBeginRendering(cmd, &rendering);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &m_set, 0, nullptr);

	VkExtent2D extent = m_context.Extent();
	int bottomX = 0, bottomY = 0, bottomW = (int)extent.width, bottomH = (int)extent.height;
	if (!(m_wideBg && m_drawBottom))
	{
		bottomX = (int)m_xOff - (int)m_correction;
		int glY = (int)m_yOff + (int)m_correction;
		bottomW = (int)m_xRes;
		bottomH = (int)m_yRes;
		bottomY = (int)extent.height - (glY + bottomH);
	}
	if (m_drawBottom && m_uploaded[0])
		DrawLayer(0, bottomX, bottomY, bottomW, bottomH, (int)m_upscaleMode);
	if (m_sceneView)
	{
		if (m_lowRes)
		{
			/*
			 * Low-resolution rendering: the scene target is a fixed small image (496x384 or 992x768,
			 * see InternalResolution) covering the whole picture, so it is upscaled onto the visible
			 * rectangle. DrawLayer carries uv 0..1 across the rectangle, which is exactly what maps
			 * the whole small image onto the whole picture - no sub-rectangle, no offset, and therefore
			 * no way for it to drift out of step with the 2D layers drawn over it.
			 *
			 * Linear filtering regardless of the tile-layer mode: the upscale factor is 2-3x, where
			 * nearest would turn every 3D pixel into a block and bicubic would cost four taps per
			 * pixel on the one path that exists to be cheap. The scissor inside DrawLayer still crops
			 * the edges by the 2D/3D correction, as it does in every other mode.
			 */
			const int sceneY = (int)extent.height - (int)(m_yOff + m_yRes);
			DrawLayer(1, (int)m_xOff, sceneY, (int)m_xRes, (int)m_yRes, (int)UpscaleMode::Bilinear);
		}
		else
		{
			// The 3D scene is rendered into an image the size of the swapchain and is already
			// positioned/scaled correctly inside it (see CVulkan3D), so it is drawn 1:1. Linear filtering
			// is used regardless of the tile-layer mode: at an exact size it makes no difference, and on
			// a mismatched size during a resize smooth beats point sampling.
			DrawLayer(1, 0, 0, (int)extent.width, (int)extent.height, (int)UpscaleMode::Bilinear);
		}
	}
	if (m_drawTop && m_uploaded[1])
	{
		int glY = (int)m_yOff + (int)m_correction;
		int topH = (int)m_yRes;
		int topY = (int)extent.height - (glY + topH);
		DrawLayer(2, (int)m_xOff - (int)m_correction, topY, (int)m_xRes, topH, (int)m_upscaleMode);
	}
	CmdEndRendering(cmd);
}

/*
 * DrawPostPass():
 *
 * Runs the filter pass over the composite image into the swapchain. One full-screen quad, so the
 * whole cost is a handful of texture fetches per output pixel.
 */
void CVulkan2D::DrawPostPass(VkCommandBuffer cmd)
{
	if (m_compositeLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		CmdTransition(cmd, m_composite.image, m_compositeLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		m_compositeLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	VkExtent2D extent = m_context.Extent();
	VkRenderingAttachmentInfo color = {};
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = m_context.SwapView();
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.clearValue.color = { { 0, 0, 0, 1 } };
	VkRenderingInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea.extent = extent;
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &color;
	CmdBeginRendering(cmd, &rendering);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postPipeLayout, 0, 1, &m_postSet, 0, nullptr);

	// The FXAA stage works in texels of the image it samples, so the resolution it is given is the
	// composite size, not the output size.
	PostPush push = {};
	push.resolution[0] = (float)m_composite.width;
	push.resolution[1] = (float)m_composite.height;
	push.fxaaEnabled = m_fxaa ? 1.0f : 0.0f;
	push.scanlineIntensity = m_scanlineIntensity;
	push.maskIntensity = m_maskIntensity;
	push.scanlineCount = m_scanlineCount;
	push.crtMode = m_crtMode;
	push.maskType = m_maskType;
	push.maskScale = m_maskScale;
	push.glowStrength = m_glowStrength;
	push.gain = m_crtGain;
	// Never ask for more levels than the chain actually has, or the high levels would clamp and
	// return the base level's contents.
	int glowLevels = m_glowLevels;
	if (glowLevels > (int)m_compositeMips - 1)
		glowLevels = (int)m_compositeMips - 1;
	push.glowLevels = glowLevels;
	push.glowThreshold = m_glowThreshold;
	push.crtColors = m_crtColors;
	vkCmdPushConstants(cmd, m_postPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

	VkViewport viewport = {};
	viewport.width = (float)extent.width;
	viewport.height = (float)extent.height;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor = { { 0, 0 }, extent };
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdDraw(cmd, 4, 1, 0, 0);
	CmdEndRendering(cmd);
}

/*
 * LoadBezelImage():
 *
 * Decodes the backdrop picture and allocates its image. The pixels stay on the CPU until
 * UploadBezel() runs on the first frame.
 *
 * Nothing here is fatal: a backdrop that cannot be read is a cosmetic loss, and the game must still
 * start. Every failure path therefore logs and returns false, and the caller turns the feature off
 * rather than propagating an error.
 */
bool CVulkan2D::LoadBezelImage(bool updateDescriptor)
{
	const int mode = m_config["BezelMode"].ValueAsDefault<int>(0);
	if (mode == 0)
		return false;

	/*
	 * Not loaded at all when the widescreen option is on.
	 *
	 * The backdrop exists to fill the black bars either side of the 4:3 picture. Widescreen removes
	 * those bars - the picture spans the full width - so there is nothing left for it to fill, and
	 * anything it did draw would land on the game itself. Declining to load it also skips a texture
	 * decode and a few tens of megabytes of device memory for an image that could not be used.
	 *
	 * Checked here rather than where the backdrop is drawn, so the log says why it is absent instead of
	 * leaving it to be inferred from geometry that came out empty.
	 */
	if (m_wide)
	{
		InfoLog("Bezel: not loaded - the widescreen option is on, so there are no side bars to fill.");
		return false;
	}

	int brightness = m_config["BezelBrightness"].ValueAsDefault<int>(100);
	if (brightness < 10)
		brightness = 10;
	if (brightness > 200)
		brightness = 200;
	m_bezelBrightness = (float)brightness / 100.0f;

	std::string path = m_config["BezelImage"].ValueAsDefault<std::string>("");

	// "Built-in" means the picture shipped next to the executable. An empty path with the custom
	// mode selected is treated the same way rather than leaving the user with a blank screen and no
	// explanation, which a settings file edited by hand easily produces.
	if (mode == 1 || path.empty())
		path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Assets) << "Model3VulkanBezel.png";

	std::string error;
	if (!Util::PNG::LoadRGBA(path, m_bezelPixels, m_bezelWidth, m_bezelHeight, &error))
	{
		ErrorLog("Bezel: '%s' cannot be used (%s). No backdrop will be drawn.", path.c_str(), error.c_str());
		m_bezelPixels.clear();
		return false;
	}

	if (!m_context.Memory().CreateImage(m_bezelWidth, m_bezelHeight, 1, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, m_bezel))
	{
		ErrorLog("Bezel: failed to allocate a %ux%u image. No backdrop will be drawn.", m_bezelWidth, m_bezelHeight);
		m_bezelPixels.clear();
		return false;
	}
	m_bezel.view = m_context.CreateView(m_bezel.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);

	if (updateDescriptor)
	{
		/*
		 * The set was written at startup with the bottom tile layer in slot 3, because no backdrop
		 * existed then - widescreen was on and the image was deliberately not loaded. Naming the new
		 * view here is what actually makes the backdrop appear: without it the bars keep sampling that
		 * tile layer, so they turn into a stretched copy of the game's background rather than showing
		 * the cabinet picture.
		 *
		 * The set may still be read by a submitted frame, so wait before changing it. This runs at most
		 * once per session - when widescreen is turned off with the panel open - which makes the stall
		 * preferable to a second descriptor set and its per-frame bookkeeping.
		 */
		vkDeviceWaitIdle(m_context.Device());
		UpdateBezelDescriptor();
	}

	InfoLog("Bezel: '%s' loaded (%ux%u, brightness %d%%).", path.c_str(), m_bezelWidth, m_bezelHeight, brightness);
	m_bezelVisible = true;
	return true;
}

/*
 * UploadBezel():
 *
 * Copies the decoded backdrop into its image, once.
 *
 * A 4K-ish picture is a few tens of megabytes of texels, which is more than the per-frame ring slice
 * is meant for, and it never changes - so it is uploaded exactly once and the CPU copy is released.
 * The copy still goes through the ring because that is the only staging path available; the slice is
 * 64 MB, so a single backdrop fits with room to spare. If the allocation ever fails, the next frame
 * tries again rather than dropping the feature.
 */
bool CVulkan2D::UploadBezel(VkCommandBuffer cmd)
{
	if (m_bezelUploaded || m_bezel.image == VK_NULL_HANDLE)
		return m_bezelUploaded;

	const VkDeviceSize bytes = (VkDeviceSize)m_bezelWidth * (VkDeviceSize)m_bezelHeight * 4;
	if (m_bezelPixels.size() < (size_t)bytes)
		return false;

	VkDeviceSize offset = 0;
	void* pointer = nullptr;
	if (!m_context.Ring().Alloc(bytes, 4, offset, pointer))
		return false;

	std::memcpy(pointer, m_bezelPixels.data(), (size_t)bytes);

	CmdTransition(cmd, m_bezel.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	VkBufferImageCopy copy = {};
	copy.bufferOffset = offset;
	// Rows and layers are tightly packed in the decoded buffer, so both "0" values mean "use the
	// extent", which is what avoids having to pad the CPU copy to 256-byte boundaries.
	copy.bufferRowLength = 0;
	copy.bufferImageHeight = 0;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent = { m_bezelWidth, m_bezelHeight, 1 };
	vkCmdCopyBufferToImage(cmd, m_context.Ring().Buffer(), m_bezel.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

	CmdTransition(cmd, m_bezel.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	m_bezelUploaded = true;
	// The GPU has its copy and the ring memory it came from is reused next frame, so the CPU-side
	// pixels are dead weight from here on. A backdrop is several megabytes and this runs long after
	// startup, which makes it worth releasing rather than leaving for the destructor.
	std::vector<uint8_t>().swap(m_bezelPixels);
	InfoLog("Bezel: uploaded %ux%u (%llu bytes) to the GPU.",
		m_bezelWidth, m_bezelHeight, (unsigned long long)bytes);
	return true;
}

/*
 * Points composite slot 3 at the current backdrop image.
 *
 * Slot 3 is where the shader reads the backdrop, and it is a single descriptor in the set every
 * composite draw binds. Init() writes the whole set when the image exists at startup; this is the
 * path for an image created later, because LoadBezelImage() declines to run while widescreen is on
 * and the overlay can turn that option off mid-session.
 *
 * Writing one array element rather than all four leaves the scene and tile slots untouched, which
 * matters: those views change independently (SetSceneColor).
 */
void CVulkan2D::UpdateBezelDescriptor()
{
	if (m_set == VK_NULL_HANDLE || m_bezel.view == VK_NULL_HANDLE)
		return;

	VkDescriptorImageInfo info = {};
	info.imageView = m_bezel.view;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_set;
	write.dstBinding = 1;
	write.dstArrayElement = 3;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	write.pImageInfo = &info;
	vkUpdateDescriptorSets(m_context.Device(), 1, &write, 0, nullptr);
}

/*
 * BezelGameRect():
 *
 * The area of the swapchain the emulated picture actually occupies, in Vulkan coordinates (y down
 * from the top). Everything outside it is black bar. The geometry itself lives in BezelLayout.h so
 * that it can be tested without a GPU - see the property test in devdocs/TECHLOG.md.
 */
void CVulkan2D::BezelGameRect(int& x, int& y, int& w, int& h) const
{
	ComputeBezelGameRect((int)m_context.Extent().width, (int)m_context.Extent().height,
		(int)m_xOff, (int)m_yOff, (int)m_xRes, (int)m_yRes,
		(int)m_correction, m_wideBg && m_drawBottom, m_wide,
		x, y, w, h);
}

/*
 * PictureRect():
 *
 * The area of the output that actually shows the emulated picture, in Vulkan coordinates.
 *
 * Same rectangle as BezelGameRect(), and deliberately the same function: the layers are clipped to it
 * and the backdrop is kept out of it, and the two must agree or a band of either black or backdrop
 * appears between them. Only the arguments differ - here the wide-background case is decided by the
 * configuration rather than by whether a bottom layer happens to have been drawn yet, because the clip
 * has to be right on the very first frame.
 */
void CVulkan2D::PictureRect(int& x, int& y, int& w, int& h) const
{
	ComputeBezelGameRect((int)m_context.Extent().width, (int)m_context.Extent().height,
		(int)m_xOff, (int)m_yOff, (int)m_xRes, (int)m_yRes,
		(int)m_correction, m_wideBg, m_wide,
		x, y, w, h);
}

/*
 * DrawBezelPass():
 *
 * Paints the backdrop into the black bars, after post-processing so that the filters treat it as part
 * of the bezel rather than part of the screen.
 *
 * The bars are drawn as four separate passes, one per edge, each with a single scissor rectangle.
 * One pass with four scissors is not possible: the scissor count is fixed when a pipeline is created
 * and the composite pipeline declares one. Four draws of a four-vertex quad cost nothing, and it
 * keeps the pipeline untouched - which matters, because that pipeline is also what composites the
 * scene, and giving it a different scissor count would need a second pipeline and a second layout.
 */
void CVulkan2D::DrawBezelPass(VkCommandBuffer cmd)
{
	/*
	 * Widescreen draws no backdrop at all.
	 *
	 * The picture spans the full width, so there are no side bars for a cabinet picture to fill. The
	 * wide picture rectangle is still inset by `correction` at the top and bottom for the 2D layer's
	 * known offset, and treating those thin strips as bars meant the backdrop was sampled into them -
	 * which is the bright line that appeared along the top edge once widescreen was switched on. The
	 * letterbox above and below stays black, which is what this option is supposed to show.
	 */
	if (m_wide)
		return;
	/*
	 * Diagnostic, logged once and again whenever the answer changes.
	 *
	 * "The backdrop does not appear" has several causes that are indistinguishable from the outside -
	 * the feature is off, the upload never happened, the picture covers the whole output so there is
	 * nothing to fill, or the geometry collapsed - and each needs a different fix. Rather than guess,
	 * the values that decide it are reported.
	 *
	 * The scalars are compared before any string is built: this runs every frame, and the state it
	 * describes changes only on a resize, so a frame that changes nothing costs a few comparisons
	 * and no allocation.
	 */
	int gx = 0, gy = 0, gw = 0, gh = 0;
	BezelGameRect(gx, gy, gw, gh);
	BezelBar bars[4];
	const int barCount = ComputeBezelBars((int)m_context.Extent().width, (int)m_context.Extent().height,
		gx, gy, gw, gh, bars);

	static int s_lastVisible = -1, s_lastUploaded = -1, s_lastW = -1, s_lastH = -1;
	static int s_lastX = -1, s_lastY = -1, s_lastGw = -1, s_lastGh = -1, s_lastBars = -1;
	const int visible = m_bezelVisible ? 1 : 0;
	const int uploaded = m_bezelUploaded ? 1 : 0;
	const int extentW = (int)m_context.Extent().width;
	const int extentH = (int)m_context.Extent().height;
	if (visible != s_lastVisible || uploaded != s_lastUploaded || extentW != s_lastW || extentH != s_lastH ||
		gx != s_lastX || gy != s_lastY || gw != s_lastGw || gh != s_lastGh || barCount != s_lastBars)
	{
		s_lastVisible = visible; s_lastUploaded = uploaded; s_lastW = extentW; s_lastH = extentH;
		s_lastX = gx; s_lastY = gy; s_lastGw = gw; s_lastGh = gh; s_lastBars = barCount;
		InfoLog("Bezel: enabled=%d uploaded=%d extent=%dx%d game=(%d,%d %dx%d) bars=%d brightness=%.2f",
			visible, uploaded, extentW, extentH, gx, gy, gw, gh, barCount, m_bezelBrightness);
		for (int i = 0; i < barCount; i++)
			InfoLog("Bezel:   bar %d = x %d y %d w %u h %u", i, bars[i].x, bars[i].y, bars[i].w, bars[i].h);
	}

	if (!m_bezelVisible || !m_bezelUploaded || m_bezel.image == VK_NULL_HANDLE)
		return;

	// Nothing to fill: the picture already covers the whole output.
	if (barCount == 0)
		return;

	VkRenderingAttachmentInfo color = {};
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = m_context.SwapView();
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	// The finished frame is already there; this pass only adds to the bars.
	color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkRenderingInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea.extent = m_context.Extent();
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &color;

	CmdBeginRendering(cmd, &rendering);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &m_set, 0, nullptr);

	// Slot 3 is the backdrop; see kCompositeLayerCount. A backdrop should always be smooth, so it
	// uses linear filtering whichever upscale mode the tile layers are set to.
	LayerPush push = { 3u, m_bezelBrightness, (int)UpscaleMode::Bilinear, 0u, 0u, 0u };
	vkCmdPushConstants(cmd, m_pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

	// The quad covers the whole screen and carries uv 0..1 across it, so a scissor selects which part
	// of the backdrop lands in which bar without any per-bar geometry.
	VkViewport viewport = {};
	viewport.width = (float)extentW;
	viewport.height = (float)extentH;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	for (int i = 0; i < barCount; i++)
	{
		VkRect2D scissor = { { bars[i].x, bars[i].y }, { bars[i].w, bars[i].h } };
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdDraw(cmd, 4, 1, 0, 0);
	}
	CmdEndRendering(cmd);
}

/*
 * DrawOverlayPass():
 *
 * Draws whatever the overlay callback records on top of the finished frame.
 *
 * The rendering scope is opened here rather than in the callback because the ImGui Vulkan backend
 * only issues draw commands - it has no idea which image or attachment it is drawing into, and
 * expects the caller to have set that up. This is the same division the official SDL/Vulkan example
 * uses, just with this renderer's own scope in place of its helper object.
 *
 * Load rather than clear: the frame is already complete and the panel is drawn over it. That is also
 * why this runs last, after the backdrop - the panel must not end up behind anything.
 */
void CVulkan2D::DrawOverlayPass(VkCommandBuffer cmd)
{
	if (m_overlayPass == nullptr)
		return;

	VkRenderingAttachmentInfo color = {};
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = m_context.SwapView();
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkRenderingInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea.extent = m_context.Extent();
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &color;

	CmdBeginRendering(cmd, &rendering);
	m_overlayPass(cmd);
	CmdEndRendering(cmd);
}

/*
 * DrawCrosshairPass():
 *
 * Draws the light-gun crosshair over the finished frame.
 *
 * Load rather than clear, like the overlay pass and for the same reason: the frame is complete and
 * this only adds the shape. It runs after the overlay so a crosshair is never hidden by the in-game
 * panel - the ordering the ImGui foreground draw list used to provide.
 *
 * The shape is generated entirely in the vertex shader; each player is one push constant write and a
 * twelve-vertex draw. The scale matches what the ImGui version applied (window height relative to
 * 480 lines, times the UI scale), so moving the crosshair out of ImGui does not change its size.
 */
void CVulkan2D::DrawCrosshairPass(VkCommandBuffer cmd)
{
	if (!m_crosshair.visible || m_crosshair.players == 0 || m_crosshairPipeline == VK_NULL_HANDLE)
		return;

	const VkExtent2D extent = m_context.Extent();
	if (extent.width == 0 || extent.height == 0)
		return;

	VkRenderingAttachmentInfo color = {};
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = m_context.SwapView();
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkRenderingInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea.extent = extent;
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &color;

	CmdBeginRendering(cmd, &rendering);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_crosshairPipeline);

	VkViewport viewport = {};
	viewport.width = (float)extent.width;
	viewport.height = (float)extent.height;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor = { { 0, 0 }, extent };
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	const float scale = ((float)extent.height / 480.0f) *
		(m_crosshair.uiScale > 0.0f ? m_crosshair.uiScale : 1.0f);
	const float colours[2][4] = {
		{ 1.0f, 0.0f, 0.0f, 1.0f },     // player 1: red, as before
		{ 0.0f, 1.0f, 0.0f, 1.0f }      // player 2: green, as before
	};

	for (int player = 0; player < 2; player++)
	{
		if ((m_crosshair.players & (1u << player)) == 0)
			continue;

		CrosshairPush push = {};
		push.centre[0] = m_crosshair.x[player];
		push.centre[1] = m_crosshair.y[player];
		push.extent[0] = (float)extent.width;
		push.extent[1] = (float)extent.height;
		push.colour[0] = colours[player][0];
		push.colour[1] = colours[player][1];
		push.colour[2] = colours[player][2];
		push.colour[3] = colours[player][3];
		push.scale = scale;
		vkCmdPushConstants(cmd, m_crosshairLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		vkCmdDraw(cmd, 12, 1, 0, 0);
	}
	CmdEndRendering(cmd);
}

/*
 * DrawBorderPass():
 *
 * Draws the optional Sinden light-gun border as four opaque quads around the finished frame.
 * Swapchain coordinates and no post-processing: the camera looks for the edge of the lit area, so
 * the border must be exactly at the edge of the output and at full brightness regardless of the
 * CRT filter or the backdrop.
 */
void CVulkan2D::DrawBorderPass(VkCommandBuffer cmd)
{
	if (!m_sindenBorder || m_sindenBorderWidth <= 0 || m_borderPipeline == VK_NULL_HANDLE)
		return;

	const VkExtent2D extent = m_context.Extent();
	if (extent.width == 0 || extent.height == 0)
		return;

	// Half the smaller dimension is the largest border that still leaves a picture; a wider value
	// would invert the left/right or top/bottom quads and draw garbage.
	const int limit = (int)std::min(extent.width, extent.height) / 2;
	const float thickness = (float)std::min(m_sindenBorderWidth, limit);
	if (thickness <= 0.0f)
		return;

	VkRenderingAttachmentInfo color = {};
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = m_context.SwapView();
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkRenderingInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea.extent = extent;
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &color;

	CmdBeginRendering(cmd, &rendering);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_borderPipeline);

	VkViewport viewport = {};
	viewport.width = (float)extent.width;
	viewport.height = (float)extent.height;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor = { { 0, 0 }, extent };
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	BorderPush push = {};
	push.extent[0] = (float)extent.width;
	push.extent[1] = (float)extent.height;
	push.thickness = thickness;
	push.brightness = m_sindenBorderBrightness;
	vkCmdPushConstants(cmd, m_borderLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

	// Four edges, six vertices each: top, bottom, left, right (see border.vert).
	vkCmdDraw(cmd, 24, 1, 0, 0);
	CmdEndRendering(cmd);
}

void CVulkan2D::WriteVRAM(unsigned, uint32_t) {}
void CVulkan2D::AttachRegisters(const uint32_t*) {}
void CVulkan2D::AttachPalette(const uint32_t**) {}
void CVulkan2D::AttachVRAM(const uint8_t*) {}
void CVulkan2D::AttachDrawBuffers(std::shared_ptr<TileGenBuffer> bottom, std::shared_ptr<TileGenBuffer> top)
{
	m_buffers[0] = bottom;
	m_buffers[1] = top;
}

} // Vulkan3D
