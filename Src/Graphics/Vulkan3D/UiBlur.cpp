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
 * UiBlur.cpp
 *
 * See UiBlur.h for what this is for and why the chain is shaped the way it is.
 */

#include "UiBlur.h"

#include "VulkanCommon.h"
#include "VulkanContext.h"
#include "OSD/Logger.h"

#include "Shaders/composite_vert.spv.h"
#include "Shaders/ui_blur_down_frag.spv.h"
#include "Shaders/ui_blur_up_frag.spv.h"
#include "Shaders/ui_frost_frag.spv.h"

#include <algorithm>

namespace Vulkan3D
{

CUiBlur::CUiBlur(CVulkanContext& context) : m_context(context)
{
}

CUiBlur::~CUiBlur()
{
	Destroy();
}

bool CUiBlur::CreatePipelines()
{
	VkDevice device = m_context.Device();

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	// No mip chain behind any of these images, and the shaders address level 0 only.
	samplerInfo.maxLod = 0.0f;
	if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
	{
		ErrorLog("UiBlur: could not create the backdrop sampler.");
		return false;
	}

	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = composite_vert_spv_size;
	moduleInfo.pCode = composite_vert_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_vert) != VK_SUCCESS)
	{
		ErrorLog("UiBlur: the full-screen quad vertex shader failed to load.");
		return false;
	}
	moduleInfo.codeSize = ui_blur_down_frag_spv_size;
	moduleInfo.pCode = ui_blur_down_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_downFrag) != VK_SUCCESS)
	{
		ErrorLog("UiBlur: the downsample shader failed to load.");
		return false;
	}
	moduleInfo.codeSize = ui_blur_up_frag_spv_size;
	moduleInfo.pCode = ui_blur_up_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_upFrag) != VK_SUCCESS)
	{
		ErrorLog("UiBlur: the upsample shader failed to load.");
		return false;
	}
	moduleInfo.codeSize = ui_frost_frag_spv_size;
	moduleInfo.pCode = ui_frost_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_frostFrag) != VK_SUCCESS)
	{
		ErrorLog("UiBlur: the frost shader failed to load.");
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
	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS)
	{
		ErrorLog("UiBlur: could not create the descriptor set layout.");
		return false;
	}

	/*
	 * One set per pass rather than one set rewritten between draws: a descriptor set may not be
	 * updated while a command buffer that binds it is still pending, and the passes here are three
	 * draws apart within a single command buffer.
	 */
	VkDescriptorPoolSize sizes[2] = {};
	sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
	sizes[0].descriptorCount = 3;
	sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	sizes[1].descriptorCount = 3;
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 3;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = sizes;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
	{
		ErrorLog("UiBlur: could not create the descriptor pool.");
		return false;
	}

	VkDescriptorSetLayout setLayouts[3] = { m_setLayout, m_setLayout, m_setLayout };
	VkDescriptorSet sets[3] = {};
	VkDescriptorSetAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.descriptorPool = m_pool;
	alloc.descriptorSetCount = 3;
	alloc.pSetLayouts = setLayouts;
	if (vkAllocateDescriptorSets(device, &alloc, sets) != VK_SUCCESS)
	{
		ErrorLog("UiBlur: could not allocate the descriptor sets.");
		return false;
	}
	m_downSet = sets[0];
	m_upSet = sets[1];
	m_frostSet = sets[2];

	for (VkDescriptorSet set : sets)
	{
		VkDescriptorImageInfo samplerDescriptor = {};
		samplerDescriptor.sampler = m_sampler;
		VkWriteDescriptorSet samplerWrite = {};
		samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		samplerWrite.dstSet = set;
		samplerWrite.dstBinding = 0;
		samplerWrite.descriptorCount = 1;
		samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		samplerWrite.pImageInfo = &samplerDescriptor;
		vkUpdateDescriptorSets(device, 1, &samplerWrite, 0, nullptr);
	}

	// The range covers the largest of the three blocks (ui_frost.frag's). A shader that declares a
	// smaller block is still served by it - the range is a window, not a requirement to match.
	VkPushConstantRange pushRange = {};
	pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof(FrostPush);
	VkPipelineLayoutCreateInfo pipeLayoutInfo = {};
	pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeLayoutInfo.setLayoutCount = 1;
	pipeLayoutInfo.pSetLayouts = &m_setLayout;
	pipeLayoutInfo.pushConstantRangeCount = 1;
	pipeLayoutInfo.pPushConstantRanges = &pushRange;
	if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &m_pipeLayout) != VK_SUCCESS)
	{
		ErrorLog("UiBlur: could not create the pipeline layout.");
		return false;
	}

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

	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamics;

	// All three passes render into images in the swapchain's format (see Ensure), so one attachment
	// format describes them all and the pipelines survive a resize.
	VkFormat colorFormat = m_context.SwapFormat();
	VkPipelineRenderingCreateInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = &colorFormat;

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = m_vert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "main";

	VkPipelineColorBlendAttachmentState replaceBlend = {};
	replaceBlend.blendEnable = VK_FALSE;
	replaceBlend.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo replaceState = {};
	replaceState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	replaceState.attachmentCount = 1;
	replaceState.pAttachments = &replaceBlend;

	// The merge pass: 50/50 between the tent of the coarse level and the fine level already in the
	// attachment. See ui_blur_up.frag for why this is not the ONE/ONE of a bloom chain.
	VkPipelineColorBlendAttachmentState mergeBlend = {};
	mergeBlend.blendEnable = VK_TRUE;
	mergeBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	mergeBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	mergeBlend.colorBlendOp = VK_BLEND_OP_ADD;
	mergeBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	mergeBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	mergeBlend.alphaBlendOp = VK_BLEND_OP_ADD;
	mergeBlend.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo mergeState = {};
	mergeState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	mergeState.attachmentCount = 1;
	mergeState.pAttachments = &mergeBlend;

	struct Pass
	{
		VkShaderModule fragment;
		const VkPipelineColorBlendStateCreateInfo* blend;
		const char* name;
		VkPipeline* pipeline;
	};
	const Pass passes[] = {
		{ m_downFrag, &replaceState, "downsample", &m_downPipeline },
		{ m_upFrag, &mergeState, "upsample", &m_upPipeline },
		{ m_frostFrag, &replaceState, "frost", &m_frostPipeline },
	};

	for (const Pass& pass : passes)
	{
		stages[1].module = pass.fragment;
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
		info.pColorBlendState = pass.blend;
		info.pDynamicState = &dynamic;
		info.layout = m_pipeLayout;
		if (vkCreateGraphicsPipelines(device, m_context.PipelineCache(), 1, &info, nullptr, pass.pipeline) != VK_SUCCESS)
		{
			ErrorLog("UiBlur: the %s pipeline failed to build.", pass.name);
			return false;
		}
	}

	m_pipelinesReady = true;
	return true;
}

void CUiBlur::DestroyImages()
{
	if (m_context.Device() == VK_NULL_HANDLE)
		return;

	m_context.Memory().DestroyImage(m_level0);
	m_context.Memory().DestroyImage(m_level1);
	m_context.Memory().DestroyImage(m_output);
	m_level0Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_level1Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_outputLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void CUiBlur::RetireImages()
{
	if (m_level0.image == VK_NULL_HANDLE && m_level1.image == VK_NULL_HANDLE && m_output.image == VK_NULL_HANDLE)
		return;

	// Four frames is more than the one frame a stale ImGui descriptor can survive: the caller
	// re-registers as soon as it sees a different view, and rebuilding the panel is throttled to
	// 60 Hz, so two records are already enough. The margin costs a few megabytes for a few frames.
	m_retired.push_back(RetiredSet{ m_level0, m_level1, m_output, 4 });
	m_level0 = AllocatedImage();
	m_level1 = AllocatedImage();
	m_output = AllocatedImage();
	m_level0Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_level1Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_outputLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void CUiBlur::DrainRetired()
{
	if (m_context.Device() == VK_NULL_HANDLE)
		return;

	for (size_t i = 0; i < m_retired.size();)
	{
		if (m_retired[i].framesLeft > 0)
			--m_retired[i].framesLeft;
		if (m_retired[i].framesLeft > 0)
		{
			++i;
			continue;
		}
		m_context.Memory().DestroyImage(m_retired[i].level0);
		m_context.Memory().DestroyImage(m_retired[i].level1);
		m_context.Memory().DestroyImage(m_retired[i].output);
		m_retired.erase(m_retired.begin() + (std::ptrdiff_t)i);
	}
}

void CUiBlur::DestroyObjects()
{
	VkDevice device = m_context.Device();
	if (device == VK_NULL_HANDLE)
		return;

	DestroyImages();
	for (RetiredSet& set : m_retired)
	{
		m_context.Memory().DestroyImage(set.level0);
		m_context.Memory().DestroyImage(set.level1);
		m_context.Memory().DestroyImage(set.output);
	}
	m_retired.clear();

	if (m_downPipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(device, m_downPipeline, nullptr);
	if (m_upPipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(device, m_upPipeline, nullptr);
	if (m_frostPipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(device, m_frostPipeline, nullptr);
	m_downPipeline = m_upPipeline = m_frostPipeline = VK_NULL_HANDLE;

	if (m_pipeLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(device, m_pipeLayout, nullptr);
	m_pipeLayout = VK_NULL_HANDLE;
	if (m_pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(device, m_pool, nullptr);
	m_pool = VK_NULL_HANDLE;
	m_downSet = m_upSet = m_frostSet = VK_NULL_HANDLE;
	if (m_setLayout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
	m_setLayout = VK_NULL_HANDLE;

	if (m_vert != VK_NULL_HANDLE)
		vkDestroyShaderModule(device, m_vert, nullptr);
	if (m_downFrag != VK_NULL_HANDLE)
		vkDestroyShaderModule(device, m_downFrag, nullptr);
	if (m_upFrag != VK_NULL_HANDLE)
		vkDestroyShaderModule(device, m_upFrag, nullptr);
	if (m_frostFrag != VK_NULL_HANDLE)
		vkDestroyShaderModule(device, m_frostFrag, nullptr);
	m_vert = m_downFrag = m_upFrag = m_frostFrag = VK_NULL_HANDLE;

	if (m_sampler != VK_NULL_HANDLE)
		vkDestroySampler(device, m_sampler, nullptr);
	m_sampler = VK_NULL_HANDLE;

	m_pipelinesReady = false;
	m_ready = false;
}

void CUiBlur::Destroy()
{
	if (m_context.Device() != VK_NULL_HANDLE)
		vkDeviceWaitIdle(m_context.Device());
	DestroyObjects();
}

bool CUiBlur::Ensure(VkExtent2D fullExtent)
{
	if (!m_context.Device())
		return false;
	if (fullExtent.width == 0 || fullExtent.height == 0)
		return false;

	const VkFormat format = m_context.SwapFormat();
	const uint32_t halfW = (std::max)(1u, fullExtent.width / 2);
	const uint32_t halfH = (std::max)(1u, fullExtent.height / 2);
	const uint32_t quarterW = (std::max)(1u, halfW / 2);
	const uint32_t quarterH = (std::max)(1u, halfH / 2);

	if (m_ready && m_level0.width == halfW && m_level0.height == halfH &&
		m_level1.width == quarterW && m_level1.height == quarterH && m_format == format)
		return true;

	if (!m_pipelinesReady && !CreatePipelines())
	{
		DestroyObjects();
		return false;
	}

	/*
	 * The device is waited on because the previous images may still be named by a descriptor that
	 * was handed to ImGui, and by command buffers that have not completed. A resize is the only
	 * event that gets here, and it already stalls the frame loop waiting for the swapchain.
	 */
	vkDeviceWaitIdle(m_context.Device());
	DrainRetired();
	RetireImages();
	m_format = format;
	m_sourceExtent = fullExtent;

	/*
	 * Swapchain format, not RGBA8.
	 *
	 * Everything here is plain UNORM and the colours would be identical either way, but the capture
	 * is a scaling vkCmdBlitImage with a linear filter, and Vulkan only permits that between
	 * *format-compatible* images. BGRA8 - the format this backend asks the surface for - is not
	 * compatible with RGBA8, so a chain of a different format would be invalid on the very first
	 * blit even though the numbers in it are the same. Using the swapchain's own format sidesteps
	 * the question entirely, and matches what EnsureComposite() does for the post-processing target.
	 */
	if (!m_context.Memory().CreateImage(halfW, halfH, 1, format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			m_level0))
	{
		ErrorLog("UiBlur: could not allocate the %ux%u backdrop level.", halfW, halfH);
		DestroyObjects();
		return false;
	}
	m_level0.view = m_context.CreateView(m_level0.image, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);

	if (!m_context.Memory().CreateImage(quarterW, quarterH, 1, format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_level1))
	{
		ErrorLog("UiBlur: could not allocate the %ux%u backdrop level.", quarterW, quarterH);
		DestroyObjects();
		return false;
	}
	m_level1.view = m_context.CreateView(m_level1.image, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);

	if (!m_context.Memory().CreateImage(halfW, halfH, 1, format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_output))
	{
		ErrorLog("UiBlur: could not allocate the %ux%u frosted backdrop.", halfW, halfH);
		DestroyObjects();
		return false;
	}
	m_output.view = m_context.CreateView(m_output.image, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);

	if (m_level0.view == VK_NULL_HANDLE || m_level1.view == VK_NULL_HANDLE || m_output.view == VK_NULL_HANDLE)
	{
		ErrorLog("UiBlur: could not create views for the backdrop images.");
		DestroyObjects();
		return false;
	}

	// The three sets follow the images, so they are rewritten whenever the images are recreated.
	const VkImageView sources[3] = { m_level0.view, m_level1.view, m_level0.view };
	const VkDescriptorSet sets[3] = { m_downSet, m_upSet, m_frostSet };
	for (int i = 0; i < 3; ++i)
	{
		VkDescriptorImageInfo info = {};
		info.imageView = sources[i];
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = sets[i];
		write.dstBinding = 1;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.pImageInfo = &info;
		vkUpdateDescriptorSets(m_context.Device(), 1, &write, 0, nullptr);
	}

	m_ready = true;
	++m_generation;
	InfoLog("UiBlur: frosted backdrop chain ready (%ux%u -> %ux%u -> %ux%u).",
		fullExtent.width, fullExtent.height, halfW, halfH, quarterW, quarterH);
	return true;
}

void CUiBlur::Transition(VkCommandBuffer cmd, AllocatedImage& image, VkImageLayout& layout, VkImageLayout next)
{
	if (image.image == VK_NULL_HANDLE || layout == next)
		return;
	CmdTransition(cmd, image.image, layout, next, VK_IMAGE_ASPECT_COLOR_BIT);
	layout = next;
}

void CUiBlur::RenderFullscreen(VkCommandBuffer cmd, VkPipeline pipeline, VkDescriptorSet set,
	VkImageView targetView, VkExtent2D extent, bool load, const void* pushData, uint32_t pushSize)
{
	VkRenderingAttachmentInfo colour = {};
	colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colour.imageView = targetView;
	colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colour.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
	colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colour.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderingInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea.extent = extent;
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &colour;

	VkViewport viewport = {};
	viewport.width = (float)extent.width;
	viewport.height = (float)extent.height;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor = { { 0, 0 }, extent };

	CmdBeginRendering(cmd, &rendering);
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &set, 0, nullptr);
	if (pushData != nullptr && pushSize > 0)
		vkCmdPushConstants(cmd, m_pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
	vkCmdDraw(cmd, 4, 1, 0, 0);
	CmdEndRendering(cmd);
}

void CUiBlur::Record(VkCommandBuffer cmd, VkImage source)
{
	if (!m_ready || source == VK_NULL_HANDLE)
		return;

	/*
	 * Capture. The blit is the only full-resolution step in the chain, and the hardware does it, so
	 * the cost of the whole feature is bounded by one half-resolution write rather than by anything
	 * that scales with the display's pixel count times a filter radius.
	 *
	 * The source is put back in COLOR_ATTACHMENT_OPTIMAL because the caller still has the overlay
	 * and the crosshair to draw into it, and the screenshot copy runs after those.
	 */
	Transition(cmd, m_level0, m_level0Layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	CmdTransition(cmd, source, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	VkImageBlit blit = {};
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1;
	blit.srcOffsets[1] = { (int32_t)m_sourceExtent.width, (int32_t)m_sourceExtent.height, 1 };
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.layerCount = 1;
	blit.dstOffsets[1] = { (int32_t)m_level0.width, (int32_t)m_level0.height, 1 };
	vkCmdBlitImage(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_level0.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

	CmdTransition(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
	Transition(cmd, m_level0, m_level0Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	BlurPush blurPush = {};
	const VkExtent2D halfExtent = { m_level0.width, m_level0.height };
	const VkExtent2D quarterExtent = { m_level1.width, m_level1.height };

	// Quarter resolution: reads the half-resolution copy.
	Transition(cmd, m_level1, m_level1Layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	blurPush.texelSize[0] = 1.0f / (float)halfExtent.width;
	blurPush.texelSize[1] = 1.0f / (float)halfExtent.height;
	RenderFullscreen(cmd, m_downPipeline, m_downSet, m_level1.view, quarterExtent, false,
		&blurPush, sizeof(blurPush));
	Transition(cmd, m_level1, m_level1Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Merge back into half resolution. The coarse level is sampled, the fine level is the
	// attachment, and the blend leaves 50/50 between them.
	Transition(cmd, m_level0, m_level0Layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	blurPush.texelSize[0] = 1.0f / (float)quarterExtent.width;
	blurPush.texelSize[1] = 1.0f / (float)quarterExtent.height;
	RenderFullscreen(cmd, m_upPipeline, m_upSet, m_level0.view, halfExtent, true,
		&blurPush, sizeof(blurPush));
	Transition(cmd, m_level0, m_level0Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Final tent plus the colour grade and the grain, into the image ImGui samples.
	Transition(cmd, m_output, m_outputLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	FrostPush frostPush = {};
	frostPush.texelSize[0] = 1.0f / (float)halfExtent.width;
	frostPush.texelSize[1] = 1.0f / (float)halfExtent.height;
	frostPush.saturation = m_saturation;
	frostPush.grain = m_grain;
	frostPush.knee = m_knee;
	frostPush.lightMode = m_lightMode ? 1.0f : 0.0f;
	RenderFullscreen(cmd, m_frostPipeline, m_frostSet, m_output.view, halfExtent, false,
		&frostPush, sizeof(frostPush));
	Transition(cmd, m_output, m_outputLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	DrainRetired();
}

void CUiBlur::SetGrade(float saturation, float grain, float highlightKnee)
{
	m_saturation = (std::max)(1.0f, saturation);
	m_grain = (std::max)(0.0f, grain);
	m_knee = (std::min)(1.0f, (std::max)(0.1f, highlightKnee));
}

} // Vulkan3D
