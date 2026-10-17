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

#include <cstring>

namespace Vulkan3D {

// Mirrors the push constant block in composite.frag. Keep the two in step.
struct LayerPush
{
	uint32_t layer;
	float brightness;
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
constexpr uint32_t kCompositeLayerCount = 4;

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

	m_postReady = m_fxaa || m_crtMode > 0 || m_glowStrength > 0.0f;
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
	: m_config(config), m_context(context)
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
	DestroyPost();
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
}

Result CVulkan2D::Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned, UpscaleMode)
{
	m_xOff = xOffset;
	m_yOff = yOffset;
	m_xRes = xRes;
	m_yRes = yRes;
	m_totalX = totalXRes;
	m_totalY = totalYRes;
	m_correction = (unsigned)((yRes / 384.0) * 2.0 + 0.5);
	m_wideBg = m_config["WideBackground"].ValueAs<bool>();
	m_wide = m_config["WideScreen"].ValueAs<bool>();

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

	VkDescriptorSetLayoutBinding bindings[2] = {};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	bindings[1].descriptorCount = kCompositeLayerCount;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.bindingCount = 2;
	setInfo.pBindings = bindings;
	if (VkResult r = vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &m_layout); r != VK_SUCCESS)
	{
		ErrorLog("Vulkan2D: vkCreateDescriptorSetLayout failed (%d).", (int)r);
		return false;
	}

	VkDescriptorPoolSize sizes[2] = {};
	sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
	sizes[0].descriptorCount = 1;
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

	VkDescriptorImageInfo sampInfo = {};
	sampInfo.sampler = m_sampler;
	VkWriteDescriptorSet sampWrite = {};
	sampWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	sampWrite.dstSet = m_set;
	sampWrite.dstBinding = 0;
	sampWrite.descriptorCount = 1;
	sampWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	sampWrite.pImageInfo = &sampInfo;
	vkUpdateDescriptorSets(device, 1, &sampWrite, 0, nullptr);

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
	VkResult pipeResult = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe, nullptr, &m_pipeline);
	if (pipeResult != VK_SUCCESS)
		ErrorLog("Vulkan2D: vkCreateGraphicsPipelines failed (%d).", (int)pipeResult);
	return pipeResult == VK_SUCCESS;
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
void CVulkan2D::DrawLayer(uint32_t layer, int x, int y, int w, int h)
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
	// Unattenuated: the scene and tile layers are the picture itself, not a backdrop.
	LayerPush push = { layer, 1.0f };
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
	if (m_composite.image && m_composite.width == extent.width && m_composite.height == extent.height)
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

	if (!m_context.Memory().CreateImage(extent.width, extent.height, m_compositeMips, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, m_composite))
		return false;
	// Two views onto the same image, because the two uses have opposite requirements:
	//
	//   m_compositeRenderView - level 0 only. Vulkan requires a colour attachment view to cover
	//                           exactly one mip level, so a full-chain view cannot be rendered into;
	//                           doing so left the picture black.
	//   m_composite.view      - the whole chain, which the post pass needs in order to sample the
	//                           coarser levels for the glow.
	m_compositeRenderView = m_context.CreateView(m_composite.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
	m_composite.view = m_context.CreateView(m_composite.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, m_compositeMips);
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

	VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
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

	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_postPipeline) != VK_SUCCESS)
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
	CmdTransition(cmd, m_context.SwapImage(), oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

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

	// After the filters, so the backdrop reads as the bezel around a screen rather than as part of
	// the picture: a shadow mask belongs on the glass, not on the cabinet behind it. With no filters
	// active this still runs, which is why it is outside the branch above.
	DrawBezelPass(cmd);

	// Last of all: the in-game settings panel, over everything, including the backdrop.
	DrawOverlayPass(cmd);

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
	color.clearValue.color = { { 0, 0, 0, 1 } };
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
		DrawLayer(0, bottomX, bottomY, bottomW, bottomH);
	if (m_sceneView)
	{
		// The 3D scene is rendered into an image the size of the swapchain and is already
		// positioned/scaled correctly inside it (see CVulkan3D), so blit it 1:1.
		DrawLayer(1, 0, 0, (int)extent.width, (int)extent.height);
	}
	if (m_drawTop && m_uploaded[1])
	{
		int glY = (int)m_yOff + (int)m_correction;
		int topH = (int)m_yRes;
		int topY = (int)extent.height - (glY + topH);
		DrawLayer(2, (int)m_xOff - (int)m_correction, topY, (int)m_xRes, topH);
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
bool CVulkan2D::LoadBezelImage()
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
		path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Assets) << "Supermodel3Bezel.png";

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
 * BezelGameRect():
 *
 * The area of the swapchain the emulated picture actually occupies, in Vulkan coordinates (y down
 * from the top). Everything outside it is black bar. The geometry itself lives in BezelLayout.h so
 * that it can be tested without a GPU - see the property test in develop.md.
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

	// Slot 3 is the backdrop; see kCompositeLayerCount.
	LayerPush push = { 3u, m_bezelBrightness };
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
