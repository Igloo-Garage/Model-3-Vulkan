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
#include "Vulkan3D.h"
#include "Vulkan2D.h"
#include "OSD/Logger.h"
#include "Shaders/real3d_vert.spv.h"
#include "Shaders/real3d_frag.spv.h"
#include "Shaders/real3d_oit_frag.spv.h"
#include "Shaders/oit_resolve_vert.spv.h"
#include "Shaders/oit_resolve_frag.spv.h"
#include "Shaders/scene_composite_vert.spv.h"
#include "Shaders/scene_composite_frag.spv.h"
#include "Shaders/scene_composite_ms_frag.spv.h"
#include "Shaders/fog_vert.spv.h"
#include "Shaders/fog_frag.spv.h"
#include "Shaders/los_comp.spv.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>

namespace Vulkan3D {

static const int kMicroX[8] = { 0, 0, 128, 128, 0, 0, 128, 128 };
static const int kMicroY[8] = { 0, 128, 0, 128, 256, 384, 256, 384 };
static constexpr float kNearPlane = 1e-3f;
// The largest batch of quads 16-bit indices can address: 65536 vertices.
static constexpr uint32_t kQuadsPerIndexBatch = 16384;

static uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
	return (value + alignment - 1u) & ~(alignment - 1u);
}

CVulkan3D::CVulkan3D(const Util::Config::Node& config, const std::string& gameName, CVulkanContext& context, CVulkan2D& output)
	: m_config(config), m_gameName(gameName), m_context(context), m_output(output), m_sunClamp(true), m_blockCulling(false)
{
	// Before Init(): the pack opens HDTextures/<set>/ and starts its loader there.
	m_textures.SetGameName(gameName);
	m_wide = config["WideScreen"].ValueAs<bool>();
	m_noWhiteFlash = config["NoWhiteFlash"].ValueAs<bool>();

	/*
	 * Transparency ordering: 0 = New3D's two fixed layers, 1 = per-pixel sorted, 2 = arcade
	 * checkerboard.
	 *
	 * Clamped rather than trusted: the value selects a rendering path, and an unknown value must not
	 * silently mean "one of them". The environment variable is the test hook, matching
	 * SM3D_INST_INTERP: SM3D_ALPHA_SORT=0/1/2 selects a mode for one run without touching the
	 * configuration the user saved.
	 */
	{
		const char* sortEnv = std::getenv("SM3D_ALPHA_SORT");
		m_alphaSortMode = std::clamp(config["AlphaSortMode"].ValueAsDefault<int>(0), 0, 2);
		if (sortEnv != nullptr && sortEnv[0] >= '0' && sortEnv[0] <= '2' && sortEnv[1] == '\0')
			m_alphaSortMode = sortEnv[0] - '0';
	}
	/*
	 * Test hook, matching the other SM3D_* switches: leave the arcade checkerboard unresolved so the
	 * one-pixel pattern is visible. Comparing that against the reconstructed picture is how the
	 * half-resolution resolve was tuned, and it is the only way to see what mode 2 actually writes.
	 */
	{
		const char* rawEnv = std::getenv("SM3D_CHECKER_RAW");
		m_checkerRaw = rawEnv != nullptr && rawEnv[0] == '1' && rawEnv[1] == '\0';
	}

	/*
	 * Instance interpolation defaults to off.
	 *
	 * Enabling it smooths a car's wheels and body up to the panel rate, but it does so by
	 * interpolating every instance on its own. Adjacent models that share an edge and have
	 * different matrix slots are then moved by slightly different amounts, which opens a sub-pixel
	 * seam between them for one frame - the "white line / missing triangle showing fog" that shows
	 * up on the road. That is not a matching bug (the pair is correct, and the validation below
	 * rejects none of it), it is the cost of independent interpolation, so the conservative default
	 * is the camera-only picture the user already verified.
	 *
	 * The environment variable can force either way for testing: SM3D_INST_INTERP=1 turns it on,
	 * SM3D_INST_INTERP=0 turns it off, and otherwise the InstanceInterpolation setting decides.
	 */
	{
		const char* instanceEnv = std::getenv("SM3D_INST_INTERP");
		m_instanceInterpolation = config["InstanceInterpolation"].ValueAsDefault<bool>(false);
		if (instanceEnv != nullptr && instanceEnv[0] != '\0' && instanceEnv[1] == '\0')
			m_instanceInterpolation = instanceEnv[0] == '1';
	}

	// Hardware MSAA level: 1 (or 0) disables it, 2, 4 and 8 select that many samples. Only powers
	// of two between 1 and 8 are accepted; anything else falls back to no multisampling.
	const int msaa = config["MSAA"].ValueAsDefault<int>(0);
	if (msaa > 1 && msaa <= 8 && (msaa & (msaa - 1)) == 0)
	{
		// Ask the driver whether it really supports this sample count for both attachment formats
		// before committing to it. Nothing checked this before, so an unsupported combination would
		// have been requested blind - and the result of that is confusing rather than obvious: the
		// multisampled scene never appears while the 2D layers still draw, so only the text and UI
		// are visible.
		//
		// Done here, in the constructor, because the pipelines are built in Init() with
		// m_samples baked into their multisample state; choosing a different value later would leave
		// the pipelines disagreeing with their render targets.
		VkPhysicalDevice physical = context.PhysicalDevice();
		VkImageFormatProperties colorProps = {};
		VkImageFormatProperties depthProps = {};
		// The depth image has to be sampleable as well as renderable when multisampling, because the
		// line-of-sight readback reads it through a compute shader (a multisampled depth image cannot
		// be copied out). Probing with the usage the image will really carry is what makes "4x MSAA
		// selected but silently lost LOS" impossible: if the combination is unsupported the request
		// drops to 1x here, before the pipelines and targets are built around it.
		const VkImageUsageFlags depthProbeUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		const bool colorOk = physical != VK_NULL_HANDLE &&
			vkGetPhysicalDeviceImageFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
				VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &colorProps) == VK_SUCCESS;
		const bool depthOk = physical != VK_NULL_HANDLE &&
			vkGetPhysicalDeviceImageFormatProperties(physical, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TYPE_2D,
				VK_IMAGE_TILING_OPTIMAL, depthProbeUsage, 0, &depthProps) == VK_SUCCESS;
		const VkSampleCountFlagBits wanted = (VkSampleCountFlagBits)msaa;
		const bool colorSupports = colorOk && (colorProps.sampleCounts & wanted) != 0;
		const bool depthSupports = depthOk && (depthProps.sampleCounts & wanted) != 0;

		// The compute pass also needs a queue that can dispatch, which on a graphics queue is
		// effectively always true - but the check costs three lines and turns a device where it is
		// not into a clean fallback to 1x instead of a validation error every frame.
		bool computeSupports = false;
		if (physical != VK_NULL_HANDLE)
		{
			uint32_t familyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(familyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
			const uint32_t family = context.QueueFamily();
			computeSupports = family < familyCount && (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
		}

		InfoLog("Vulkan: MSAA %dx requested - colour format supported: %s, depth format supported: %s, compute queue: %s.",
			msaa, colorSupports ? "yes" : "NO", depthSupports ? "yes" : "NO", computeSupports ? "yes" : "NO");

		if (colorSupports && depthSupports && computeSupports)
		{
			m_sampleBits = (uint32_t)msaa;
			m_samples = wanted;
		}
		else
		{
			ErrorLog("Vulkan: %dx MSAA requires a sampleable depth image and a compute queue for the "
				"line-of-sight readback; using 1x.", msaa);
		}
	}
}

CVulkan3D::~CVulkan3D()
{
	/*
	 * Progress is logged at each stage, not just at the end.
	 *
	 * A crash in here left the log's last line as "3D targets destroyed" with no way to tell which of
	 * the six steps that follow was responsible - and heap corruption is reported when the heap is next
	 * touched, not where the bad write happened, so the crash point and the cause are already known to
	 * be able to differ. Naming each stage turns the next occurrence into a one-line answer instead of
	 * a bisection.
	 */
	VkDevice device = m_context.Device();
	if (device)
		vkDeviceWaitIdle(device);
	m_textures.Shutdown();
	DestroyTargets();
	InfoLog("Vulkan3D: teardown - textures and targets released.");
	m_context.Memory().DestroyBuffer(m_rom);
	m_context.Memory().DestroyBuffer(m_materials);
	m_context.Memory().DestroyBuffer(m_los);
	m_context.Memory().DestroyBuffer(m_dynamicVerts);
	m_context.Memory().DestroyBuffer(m_quadIndices);
	InfoLog("Vulkan3D: teardown - buffers released.");
	if (!device)
		return;
	for (auto& layer : m_scenePipelines)
		for (auto& variant : layer)
			for (VkPipeline& pipeline : variant)
				if (pipeline)
				{
					vkDestroyPipeline(device, pipeline, nullptr);
					pipeline = VK_NULL_HANDLE;
				}
	if (m_layout)
		vkDestroyPipelineLayout(device, m_layout, nullptr);
	if (m_fog)
		vkDestroyPipeline(device, m_fog, nullptr);
	if (m_fogLayout)
		vkDestroyPipelineLayout(device, m_fogLayout, nullptr);
	if (m_fogVert)
		vkDestroyShaderModule(device, m_fogVert, nullptr);
	if (m_fogFrag)
		vkDestroyShaderModule(device, m_fogFrag, nullptr);
	if (m_vert)
		vkDestroyShaderModule(device, m_vert, nullptr);
	if (m_frag)
		vkDestroyShaderModule(device, m_frag, nullptr);
	if (m_losPipeline)
		vkDestroyPipeline(device, m_losPipeline, nullptr);
	if (m_losPipeLayout)
		vkDestroyPipelineLayout(device, m_losPipeLayout, nullptr);
	if (m_losModule)
		vkDestroyShaderModule(device, m_losModule, nullptr);
	if (m_losPool)
		vkDestroyDescriptorPool(device, m_losPool, nullptr);
	if (m_losLayout)
		vkDestroyDescriptorSetLayout(device, m_losLayout, nullptr);
	if (m_losSampler)
		vkDestroySampler(device, m_losSampler, nullptr);

	for (auto& row : m_oitPipelines)
		for (VkPipeline& pipeline : row)
			if (pipeline)
			{
				vkDestroyPipeline(device, pipeline, nullptr);
				pipeline = VK_NULL_HANDLE;
			}
	if (m_oitResolve)
		vkDestroyPipeline(device, m_oitResolve, nullptr);
	if (m_oitResolveLayout)
		vkDestroyPipelineLayout(device, m_oitResolveLayout, nullptr);
	if (m_oitResolveVert)
		vkDestroyShaderModule(device, m_oitResolveVert, nullptr);
	if (m_oitResolveFrag)
		vkDestroyShaderModule(device, m_oitResolveFrag, nullptr);
	if (m_oitFrag)
		vkDestroyShaderModule(device, m_oitFrag, nullptr);
	if (m_oitPool)
		vkDestroyDescriptorPool(device, m_oitPool, nullptr);
	if (m_oitSetLayout)
		vkDestroyDescriptorSetLayout(device, m_oitSetLayout, nullptr);

	for (auto& layer : m_scenePassPipelines)
		for (auto& variant : layer)
			for (VkPipeline& pipeline : variant)
				if (pipeline)
				{
					vkDestroyPipeline(device, pipeline, nullptr);
					pipeline = VK_NULL_HANDLE;
				}
	if (m_sceneComposite)
		vkDestroyPipeline(device, m_sceneComposite, nullptr);
	if (m_sceneCompositeLayout)
		vkDestroyPipelineLayout(device, m_sceneCompositeLayout, nullptr);
	if (m_sceneCompositeVert)
		vkDestroyShaderModule(device, m_sceneCompositeVert, nullptr);
	if (m_sceneCompositeFrag)
		vkDestroyShaderModule(device, m_sceneCompositeFrag, nullptr);
	if (m_sceneInputPool)
		vkDestroyDescriptorPool(device, m_sceneInputPool, nullptr);
	if (m_sceneInputLayout)
		vkDestroyDescriptorSetLayout(device, m_sceneInputLayout, nullptr);
	if (m_fogPass)
		vkDestroyPipeline(device, m_fogPass, nullptr);
	if (m_scenePass)
		vkDestroyRenderPass(device, m_scenePass, nullptr);
	InfoLog("Vulkan3D: teardown - pipelines and shaders released.");
}

void CVulkan3D::AttachMemory(const uint32_t* cullingRAMLoPtr, const uint32_t* cullingRAMHiPtr, const uint32_t* polyRAMPtr, const uint32_t* vromPtr, const uint16_t* textureRAMPtr)
{
	m_scene.AttachMemory(cullingRAMLoPtr, cullingRAMHiPtr, polyRAMPtr, vromPtr);
	m_textures.AttachMemory(textureRAMPtr);
}

void CVulkan3D::SetStepping(int stepping)
{
	m_scene.SetStepping(stepping);
}

void CVulkan3D::SetSunClamp(bool enable) { m_sunClamp = enable; }
void CVulkan3D::SetBlockCulling(bool enable) { m_blockCulling = enable; }
float CVulkan3D::GetLosValue(int layer)
{
	if (layer < 0 || layer > 3)
		return 0;
	std::lock_guard<std::mutex> guard(m_losMutex);
	return m_losValue[layer];
}

void CVulkan3D::UploadTextures(unsigned level, unsigned x, unsigned y, unsigned width, unsigned height)
{
	m_textures.UploadTextures(level, x, y, width, height);
}

Result CVulkan3D::Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned)
{
	m_xOff = xOffset;
	m_yOff = yOffset;
	m_xRes = xRes;
	m_yRes = yRes;
	m_totalX = totalXRes;
	m_totalY = totalYRes;
	m_wide = m_config["WideScreen"].ValueAs<bool>();

	/*
	 * Low-resolution rendering.
	 *
	 * 496x384 is the Model 3's native signal and the size of the 2D tile layer, so rendering the 3D
	 * scene there costs a seventh of the pixels a 1080p picture does and the result still maps onto
	 * the picture rectangle with the same aspect - no letterboxing, no distortion. The 2D layer is
	 * composited at its own resolution afterwards, so the HUD does not lose any sharpness.
	 *
	 * 992x768 is the same thing at twice the size: still an exact multiple of the native signal, so
	 * the upscale stays even, but the 3D scene keeps twice the detail of the 1x image. The key is an
	 * index rather than a boolean so this second size needs no second setting.
	 */
	const int internalResolution = m_config["InternalResolution"].ValueAsDefault<int>(0);
	if (internalResolution == 1)
	{
		m_internalExtent.width = 496;
		m_internalExtent.height = 384;
	}
	else if (internalResolution >= 2)
	{
		m_internalExtent.width = 992;
		m_internalExtent.height = 768;
	}
	else
	{
		m_internalExtent = {};
	}
	// Widescreen widens the frustum over the whole output, which a fixed 4:3-ish internal target
	// cannot express; the scene stays inside the picture instead. The settings window greys the
	// option out while low-resolution rendering is on, so this is a belt-and-braces guard rather
	// than a surprise.
	if (m_internalExtent.width > 0)
		m_wide = false;
	RefreshSceneViewport();
	m_targetGeneration = ~0ull;

	// Clamped rather than trusted: the value reaches a shader branch and a loop bound.
	m_texFilterMode = std::clamp(m_config["TexFilter"].ValueAsDefault<int>(0), 0, 2);

	/*
	 * Exact quad interpolation, on by default.
	 *
	 * Off is the fast path: hardware triangle interpolation instead of pulling four
	 * corners and solving a quadratic per pixel. The visible difference is the split
	 * along the 0-2 diagonal of a non-planar quad, which is why this is not the
	 * default even though it is much cheaper on a small integrated GPU.
	 *
	 * SM3D_QUAD_INTERP=1/0 forces either way for one run, matching the other test
	 * hooks (SM3D_ALPHA_SORT, SM3D_INST_INTERP).
	 */
	m_quadInterpolation = m_config["QuadInterpolation"].ValueAsDefault<bool>(true);
	if (const char* quadEnv = std::getenv("SM3D_QUAD_INTERP"))
	{
		if (quadEnv[0] != '\0' && quadEnv[1] == '\0')
			m_quadInterpolation = quadEnv[0] != '0';
	}
	InfoLog("Vulkan: quad interpolation %s.",
		m_quadInterpolation ? "exact (four-corner solve)" : "fast (hardware triangle)");

	if (m_layout)
		return Result::OKAY;
	if (!m_textures.Init(m_context))
	{
		ErrorLog("Vulkan3D: texture cache init failed.");
		return Result::FAIL;
	}

	VkDeviceSize align = m_context.StorageAlignment();
	m_materialSlice = AlignUp((uint32_t)(kMaxMaterials * sizeof(R3DMaterial)), (uint32_t)align);
	if (!m_context.Memory().CreateBuffer(m_materialSlice * kMaxFramesInFlight,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_materials))
	{
		ErrorLog("Vulkan3D: material buffer allocation failed.");
		return Result::FAIL;
	}
	/*
	 * Sized from kMaxRomVerts rather than a round number.
	 *
	 * This buffer receives the geometry cache's accumulated ROM vertices, and the cache is allowed to
	 * grow to kMaxRomVerts before it flushes itself. A flat 64 MB held only 1048576 of those vertices
	 * at 64 bytes each - a third short of what the cache could hand over - so once a level had been
	 * played for a couple of minutes the buffer filled, the upload was rejected, and every mesh beyond
	 * the cut silently stopped being drawn. Because the missing geometry was simply absent rather than
	 * corrupted, it presented as large black areas and looked like a texture fault.
	 *
	 * Deriving the size from the same constant the cache uses is what makes the two limits impossible
	 * to disagree.
	 */
	const VkDeviceSize romBufferBytes = (VkDeviceSize)kMaxRomVerts * sizeof(GpuQuadVertex);
	if (!m_context.Memory().CreateBuffer(romBufferBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_rom))
	{
		ErrorLog("Vulkan3D: ROM vertex buffer allocation failed (%llu MB for %u vertices).",
			(unsigned long long)(romBufferBytes / (1024 * 1024)), (unsigned)kMaxRomVerts);
		return Result::FAIL;
	}
	if (!m_context.Memory().CreateBuffer(sizeof(float) * 4 * kMaxFramesInFlight,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_los))
	{
		ErrorLog("Vulkan3D: line-of-sight buffer allocation failed.");
		return Result::FAIL;
	}
	// Only needed for the multisampled path. A failure is reported but not fatal: losing the LOS
	// register under MSAA is the behaviour this build had before, not a reason to refuse to run.
	if (m_samples > VK_SAMPLE_COUNT_1_BIT && !CreateLosPipeline())
		ErrorLog("Vulkan3D: line-of-sight compute pipeline unavailable; LOS reads will stall at their last value under MSAA.");

	m_textures.WriteDynamicBuffers(m_context.Ring().Buffer(), sizeof(ViewportUBO), m_materials.buffer, kMaxMaterials * sizeof(R3DMaterial));
	/*
	 * The geometry bindings are written here, before anything is drawn, and again
	 * whenever the dynamic vertex buffer is reallocated. Pointing the RAM binding at
	 * the ROM buffer keeps every descriptor valid from the start; the RAM pipeline is
	 * the only reader and it is not bound until there is RAM geometry to draw.
	 */
	m_textures.WriteVertexBuffers(m_rom.buffer, m_rom.buffer, sizeof(GpuQuadVertex));

	if (!CreateQuadIndexBuffer())
		return Result::FAIL;

	VkDevice device = m_context.Device();
	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = real3d_vert_spv_size;
	moduleInfo.pCode = real3d_vert_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_vert) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: vertex shader failed to load.");
		return Result::FAIL;
	}
	moduleInfo.codeSize = real3d_frag_spv_size;
	moduleInfo.pCode = real3d_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_frag) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: fragment shader failed to load.");
		return Result::FAIL;
	}

	/*
	 * Sorted-transparency setup, when the configuration asks for it.
	 *
	 * Every step here degrades to the two-layer path rather than failing the session: the mode is an
	 * optional picture improvement, and refusing to start because a driver lacks a feature or a pool
	 * allocation failed would be the wrong trade. The reason is logged once, and m_alphaSortMode = 0
	 * makes every later path take the original code.
	 */
	if (m_alphaSortMode == 1)
	{
		if (!m_context.FragmentStoresAndAtomicsSupported())
		{
			ErrorLog("Vulkan3D: AlphaSortMode=1 needs fragmentStoresAndAtomics, which this device does not "
				"expose; using New3D's two-layer transparency instead.");
			m_alphaSortMode = 0;
		}
		else
		{
			moduleInfo.codeSize = real3d_oit_frag_spv_size;
			moduleInfo.pCode = real3d_oit_frag_spv;
			if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_oitFrag) != VK_SUCCESS)
			{
				ErrorLog("Vulkan3D: sorted-transparency fragment shader failed to load; using the two-layer path.");
				m_alphaSortMode = 0;
			}
			else if (!CreateOitDescriptors())
			{
				ErrorLog("Vulkan3D: sorted-transparency descriptors could not be created; using the two-layer path.");
				m_alphaSortMode = 0;
			}
		}
	}

	// Always present, so set 1 means "sorted transparency" and set 2 means "HD replacements" in every
	// pipeline regardless of which transparency mode this session uses.
	if (!CreateOitSetLayout())
	{
		ErrorLog("Vulkan3D: sorted-transparency descriptor set layout could not be created.");
		return Result::FAIL;
	}

	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	range.size = sizeof(R3DPush);
	/*
	 * Set 0 is the texture/material/geometry set every scene draw uses. Set 1 holds the
	 * sorted-transparency resources and set 2 the bindless HD replacement array. Both layouts are
	 * always present so the set numbers are fixed across modes: a shader binds its resources to a
	 * set number at compile time, and HD textures must be available whichever transparency path is
	 * selected. A set a pipeline does not statically use simply goes unbound.
	 */
	VkDescriptorSetLayout setLayouts[3] = { m_textures.Layout(), m_oitSetLayout, m_textures.HDSetLayout() };
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 3;
	layoutInfo.pSetLayouts = setLayouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: pipeline layout creation failed.");
		return Result::FAIL;
	}
	bool pipelinesOk = true;
	for (int layer = 0; layer < 3; layer++)
		for (int overlay = 0; overlay < 2; overlay++)
			for (int ram = 0; ram < 2; ram++)
				pipelinesOk = CreatePipeline(layer, overlay != 0, ram != 0) && pipelinesOk;

	/*
	 * The two-subpass scene render pass for the default transparency mode.
	 *
	 * Optional on purpose: everything here can fail on an allocation or a pipeline error, and the
	 * dynamic-rendering path produces the same picture (it is what the backend used before), so a
	 * failure costs the optimisation, not the session. DrawScene decides per frame whether to use it.
	 */
	if (m_alphaSortMode == 0)
	{
		// Test hook: SM3D_NO_SUBPASS=1 forces the dynamic-rendering path, which is what the line-of-sight
		// games and AlphaSortMode=1 use, so the two ways of drawing the same scene can be compared
		// frame for frame - and a user whose driver misbehaves on the render pass has a way back.
		const char* noSubpass = std::getenv("SM3D_NO_SUBPASS");
		const bool subpassDisabled = noSubpass != nullptr && noSubpass[0] == '1' && noSubpass[1] == '\0';
		if (subpassDisabled)
			InfoLog("Vulkan3D: SM3D_NO_SUBPASS is set; using dynamic rendering for the scene.");
		else if (!CreateScenePass() || !CreateSceneCompositePipeline())
		{
			ErrorLog("Vulkan3D: two-subpass scene pass unavailable; the default transparency mode will use dynamic rendering.");
			if (m_scenePass != VK_NULL_HANDLE)
			{
				vkDestroyRenderPass(device, m_scenePass, nullptr);
				m_scenePass = VK_NULL_HANDLE;
			}
		}
		m_scenePassReady = m_scenePass != VK_NULL_HANDLE && m_sceneComposite != VK_NULL_HANDLE;
	}
	if (m_alphaSortMode == 1)
	{
		m_oitPerSample = m_samples > VK_SAMPLE_COUNT_1_BIT && m_context.SampleRateShadingSupported();
		// Test hook, matching SM3D_ALPHA_SORT: forces the resolve to run once per pixel so the
		// cost of the per-sample path can be measured against it. No effect on the capture stage.
		if (const char* perSampleEnv = std::getenv("SM3D_OIT_PER_SAMPLE"))
			if (perSampleEnv[0] == '0' && perSampleEnv[1] == '\0')
				m_oitPerSample = false;
		for (int overlay = 0; overlay < 2; overlay++)
			for (int ram = 0; ram < 2; ram++)
				pipelinesOk = CreatePipeline(1, overlay != 0, ram != 0, true) && pipelinesOk;
		if (!CreateOitResolvePipeline())
		{
			ErrorLog("Vulkan3D: sorted-transparency resolve pipeline creation failed; using the two-layer path.");
			m_alphaSortMode = 0;
		}
	}
	/*
	 * Name the transparency path this session runs. The three modes share almost all of their code and
	 * can fail over to mode 0 at any of several points above, so a log that does not state the final
	 * value cannot be used to interpret a screenshot or a report.
	 */
	InfoLog("Vulkan: transparency sorting %d (%s).", m_alphaSortMode,
		m_alphaSortMode == 0 ? "two layers, subpass composite"
		: m_alphaSortMode == 1 ? "per-pixel sorted A-buffer"
		: "arcade checkerboard, half resolution");
	if (m_checkerRaw && m_alphaSortMode == 2)
		InfoLog("Vulkan: SM3D_CHECKER_RAW is set; the checkerboard is left unresolved (test hook).");
	if (!pipelinesOk)
	{
		ErrorLog("Vulkan3D: graphics pipeline creation failed.");
		return Result::FAIL;
	}
	if (!CreateFogPipeline())
	{
		ErrorLog("Vulkan3D: fog pipeline creation failed.");
		return Result::FAIL;
	}
	if (m_scenePassReady && !CreateFogPassPipeline())
	{
		ErrorLog("Vulkan3D: fog pipeline for the scene render pass failed; using dynamic rendering.");
		m_scenePassReady = false;
	}
	if (m_scenePassReady)
	{
		for (int layer = 0; layer < 3; layer++)
			for (int overlay = 0; overlay < 2; overlay++)
				for (int ram = 0; ram < 2; ram++)
					pipelinesOk = CreatePipeline(layer, overlay != 0, ram != 0, false, m_scenePass) && pipelinesOk;
		if (!pipelinesOk)
		{
			ErrorLog("Vulkan3D: scene pipelines for the render pass failed; using dynamic rendering.");
			m_scenePassReady = false;
		}
	}
	InfoLog("Vulkan3D: renderer ready%s.", m_scenePassReady ? " (two-subpass scene composite)" : "");
	return Result::OKAY;
}

bool CVulkan3D::CreatePipeline(int layer, bool overlay, bool ramVertices, bool oitCapture, VkRenderPass renderPass)
{
	/*
	 * The vertex source is a specialization constant, so the pipeline compiles down
	 * to exactly one of the two storage-buffer bindings. That matters more than it
	 * looks: a runtime branch would leave both bindings reachable, and a driver that
	 * hoists a speculative load out of the branch would read the buffer that is not
	 * bound for this draw.
	 *
	 * Both shaders need the same value - the vertex shader fetches its own corner,
	 * the fragment shader fetches all four - so the vertex-source entry is present in
	 * both maps. The fragment map additionally carries the soft-effect switch, which
	 * only the OIT capture stage sets: it is what keeps the default two-layer picture
	 * bit-exact while the sorted path gets the layered-dust treatment.
	 */
	const int32_t vertexSource = ramVertices ? 1 : 0;
	const int32_t softEffects = oitCapture ? 1 : 0;

	VkSpecializationMapEntry vertEntry = {};
	vertEntry.constantID = 0;
	vertEntry.offset = 0;
	vertEntry.size = sizeof(vertexSource);
	VkSpecializationInfo vertSpec = {};
	vertSpec.mapEntryCount = 1;
	vertSpec.pMapEntries = &vertEntry;
	vertSpec.dataSize = sizeof(vertexSource);
	vertSpec.pData = &vertexSource;

	struct FragmentSpecData
	{
		int32_t vertexSource;
		int32_t softEffects;
	} fragmentSpecData = { vertexSource, softEffects };
	VkSpecializationMapEntry fragEntries[2] = {};
	fragEntries[0].constantID = 0;
	fragEntries[0].offset = offsetof(FragmentSpecData, vertexSource);
	fragEntries[0].size = sizeof(int32_t);
	fragEntries[1].constantID = 1;
	fragEntries[1].offset = offsetof(FragmentSpecData, softEffects);
	fragEntries[1].size = sizeof(int32_t);
	VkSpecializationInfo fragSpec = {};
	fragSpec.mapEntryCount = 2;
	fragSpec.pMapEntries = fragEntries;
	fragSpec.dataSize = sizeof(fragmentSpecData);
	fragSpec.pData = &fragmentSpecData;

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = m_vert;
	stages[0].pName = "main";
	stages[0].pSpecializationInfo = &vertSpec;
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = oitCapture ? m_oitFrag : m_frag;
	stages[1].pName = "main";
	stages[1].pSpecializationInfo = &fragSpec;

	/*
	 * No vertex input at all. Both shaders read GpuQuadVertex records straight out
	 * of a storage buffer, indexed by gl_VertexIndex (vertex shader) or by the quad
	 * base published as a flat varying (fragment shader), so there is no attribute
	 * plumbing and no vertex buffer binding.
	 */
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
	// Overlay (decal) polygons share exactly the same 3D coordinates as the base surface
	// they sit on, so a plain GREATER compare would let the base win the tie. nudge the
	// overlay slightly towards the viewer so the decal always passes the depth test.
	raster.depthBiasEnable = overlay ? VK_TRUE : VK_FALSE;
	raster.depthBiasConstantFactor = overlay ? 1.0f : 0.0f;
	raster.depthBiasSlopeFactor = overlay ? 1.0f : 0.0f;
	raster.depthBiasClamp = 0.0f;
	VkPipelineMultisampleStateCreateInfo multi = {};
	multi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multi.rasterizationSamples = m_samples;
	/*
	 * One fragment-shader invocation per covered pixel, never one per sample, for both the normal
	 * scene pipelines and the capture stage.
	 *
	 * For the capture stage this is a correctness requirement as well as a cost one: four samples
	 * of the same pixel would each allocate their own slot and each atomically bump the same
	 * pixel's counter, quadrupling the stored fragments and the atomic traffic for no benefit. The
	 * rasteriser's coverage mask is stored in the node instead, and the resolve applies it per
	 * sample. alphaToCoverage must stay off for the same reason - it would turn texture alpha into
	 * per-sample coverage and make the capture run at sample rate.
	 */
	multi.sampleShadingEnable = VK_FALSE;
	multi.alphaToCoverageEnable = VK_FALSE;
	VkPipelineDepthStencilStateCreateInfo depth = {};
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.depthTestEnable = VK_TRUE;
	// New3D draws both transparency layers with depth writes enabled (SetRenderStates sets
	// glDepthMask(GL_TRUE) and only switches the compare to GREATER). Without the write, polygons
	// later in the same layer paint over nearer ones regardless of depth; on the Carnotaurus that
	// let the dark far side of the mouth cover the snout as a black patch.
	//
	// The sorted-transparency capture turns the write off on purpose. Its nodes are sorted and
	// composited back to front, so "the nearest translucent fragment wins the pixel" is no longer
	// wanted - it is exactly the behaviour that loses the third and later layers. Leaving the
	// depth buffer untouched by translucency also means the resolve's blend target still holds the
	// opaque surface the capture depth-tested against, for the whole pass.
	depth.depthWriteEnable = oitCapture ? VK_FALSE : VK_TRUE;
	// New3D uses GL_GEQUAL for the colour layer and GL_GREATER only for the two transparency
	// layers (SetRenderStates sets GEQUAL, and RenderFrame switches to GREATER after the colour
	// pass). The colour layer depends on that "equal depth passes" rule: where a decal polygon
	// is coplanar with the surface beneath it, the later polygon is the one the game means to be
	// visible. With GREATER the earlier one wins the tie instead, and once MSAA splits the tie
	// per sample the result is a dithered patch of the wrong texture - the Daytona 2 waterfall
	// came out blue instead of white for exactly this reason.
	depth.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

	/*
	 * Three colour attachments, exactly like New3D's R3DFrameBuffers.
	 *
	 * The colour layer writes all three - its fragment shader puts zero into the two transparency
	 * outputs - and that is what erases a translucent surface behind an opaque polygon. Without it a
	 * later priority layer cannot remove the earlier layer's transparency, and VF3's sea (priority 1)
	 * is composited over the fighters (priority 2) at the end of the frame.
	 *
	 * Each transparency layer writes only its own target: its mask zeroes the other two.
	 */
	VkPipelineColorBlendAttachmentState blendAtt[3] = {};
	for (int i = 0; i < 3; i++)
	{
		blendAtt[i].blendEnable = VK_FALSE;
		blendAtt[i].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAtt[i].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAtt[i].colorBlendOp = VK_BLEND_OP_ADD;
		blendAtt[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAtt[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAtt[i].alphaBlendOp = VK_BLEND_OP_ADD;
		// The capture stage writes only the count image and the node array; nothing goes to any
		// colour attachment, and its pass does not bind one. The attachments still have to be named
		// for the normal scene variants, so every mask is zero there.
		blendAtt[i].colorWriteMask = oitCapture ? 0x0 : ((layer == 0 || layer == i) ? 0xF : 0x0);
	}
	VkPipelineColorBlendStateCreateInfo blend = {};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	/*
	 * The capture pipeline is created for a pass with no colour attachments at all.
	 *
	 * That is not cosmetic: a 4K 4x MSAA colour attachment is 133 MB, and binding three of them to a
	 * pass that cannot write to them makes the driver load and store 800 MB per priority for nothing.
	 * With sorted transparency needing two passes per transparent priority, that traffic was the
	 * difference between running at the display rate and running at half of it.
	 */
	blend.attachmentCount = oitCapture ? 0u : 3u;
	blend.pAttachments = oitCapture ? nullptr : blendAtt;
	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamics;

	VkFormat colorFormats[3] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
	VkPipelineRenderingCreateInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = oitCapture ? 0u : 3u;
	rendering.pColorAttachmentFormats = oitCapture ? nullptr : colorFormats;
	rendering.depthAttachmentFormat = depthFormat;

	VkGraphicsPipelineCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	// A pipeline created for dynamic rendering has a null render pass and may not be bound inside a
	// render pass instance, so the scene pass needs its own copies of all twelve variants. They share
	// the shader modules and the layout; only the pipeline object differs.
	if (renderPass != VK_NULL_HANDLE)
	{
		info.renderPass = renderPass;
		info.subpass = 0;
	}
	else
	{
		info.pNext = &rendering;
	}
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &vertex;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multi;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = m_layout;
	VkPipeline* dest = oitCapture
		? &m_oitPipelines[overlay ? 1 : 0][ramVertices ? 1 : 0]
		: (renderPass != VK_NULL_HANDLE
			? &m_scenePassPipelines[layer][overlay ? 1 : 0][ramVertices ? 1 : 0]
			: &m_scenePipelines[layer][overlay ? 1 : 0][ramVertices ? 1 : 0]);
	VkResult pipeResult = vkCreateGraphicsPipelines(m_context.Device(), m_context.PipelineCache(), 1, &info, nullptr, dest);
	if (pipeResult != VK_SUCCESS)
		ErrorLog("Vulkan3D: vkCreateGraphicsPipelines (%s layer %d, %s, %s source) failed (%d).",
			oitCapture ? "sorted-transparency" : "scene", layer, overlay ? "overlay" : "base",
			ramVertices ? "RAM" : "ROM", (int)pipeResult);
	return pipeResult == VK_SUCCESS;
}

/*
 * The two-subpass render pass the default transparency mode draws the scene into.
 *
 * Subpass 0 is the whole 3D scene: one pass instead of one per priority, with the depth cleared
 * between priorities by vkCmdClearAttachments, and both Real3D transparency layers drawn into their
 * own attachments. Subpass 1 folds those two into the colour target (scene_composite.frag).
 *
 * The transparency attachments never leave the render pass: they are cleared at the start, read as
 * input attachments in subpass 1, and not stored. That is the entire point on a low-bandwidth part -
 * the dynamic-rendering path used to store and resolve 133 MB of 4K 4x MSAA surface per attachment
 * per transparency pass, and sample the resolved copies back in the 2D compositor.
 *
 * Colour attachment 0 is the 1x image the compositor samples in every case: with MSAA it is the
 * resolve target of subpass 1 (pResolveAttachments, so the hardware ROP downsamples the composited
 * samples in one go), without MSAA it is the compositor's texture itself - no extra image, no copy.
 *
 * A two-subpass VkRenderPass and subpassLoad() were chosen over VK_KHR_dynamic_rendering_local_read
 * deliberately: that extension is Vulkan 1.4-era and absent on the Intel Gen9.5 parts this is meant
 * to help, while multi-subpass render passes are Vulkan 1.0 and supported everywhere.
 */
bool CVulkan3D::CreateScenePass()
{
	VkDevice device = m_context.Device();
	const bool msaa = m_samples > VK_SAMPLE_COUNT_1_BIT;

	VkAttachmentDescription attachments[5] = {};
	// 0: the scene colour the 2D compositor samples.
	attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples = m_samples;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	// Single-sampled: attachment 0 is already the image the compositor samples, so the render pass
	// ends by handing it over in the layout the sampler needs. Multisampled: the resolve attachment
	// (4) is the one handed over, and this one is not read again before the next frame clears it.
	attachments[0].finalLayout = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// 1, 2: Real3D's trans1 / trans2. Written in subpass 0, read as input attachments in subpass 1,
	// discarded at the end - nothing outside the render pass ever sees them.
	for (int i = 0; i < 2; i++)
	{
		VkAttachmentDescription& trans = attachments[1 + i];
		trans.format = VK_FORMAT_R8G8B8A8_UNORM;
		trans.samples = m_samples;
		trans.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		trans.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		trans.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		trans.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		trans.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		trans.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	// 3: depth. Cleared per priority from inside subpass 0; nothing reads it after the pass.
	//
	// DEPTH_STENCIL_ATTACHMENT_OPTIMAL rather than DEPTH_ATTACHMENT_OPTIMAL: without the
	// separateDepthStencilLayouts feature a render pass attachment may not name a depth-only layout,
	// even for a D32 format. The image is depth-only, so the two are equivalent here.
	attachments[3].format = VK_FORMAT_D32_SFLOAT;
	attachments[3].samples = m_samples;
	attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[3].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// 4: the 1x resolve target, written by subpass 1's pResolveAttachments.
	if (msaa)
	{
		attachments[4].format = VK_FORMAT_R8G8B8A8_UNORM;
		attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[4].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[4].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	VkAttachmentReference colorRefs[3] = {
		{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
	VkAttachmentReference depthRef = { 3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	VkAttachmentReference inputRefs[2] = {
		{ 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
	VkAttachmentReference colorRef1 = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference resolveRef = { 4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpasses[2] = {};
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = 3;
	subpasses[0].pColorAttachments = colorRefs;
	subpasses[0].pDepthStencilAttachment = &depthRef;
	subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[1].colorAttachmentCount = 1;
	subpasses[1].pColorAttachments = &colorRef1;
	subpasses[1].inputAttachmentCount = 2;
	subpasses[1].pInputAttachments = inputRefs;
	subpasses[1].pResolveAttachments = msaa ? &resolveRef : nullptr;

	VkSubpassDependency deps[3] = {};
	// The subpass-local dependency. BY_REGION is what makes it a per-tile / per-pixel-group barrier
	// rather than a full-surface flush, and it is the flag that lets a tiled GPU keep the two
	// transparency attachments in tile memory from one subpass to the next.
	deps[0].srcSubpass = 0;
	deps[0].dstSubpass = 1;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[0].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	// Start: the attachments arrive from the previous frame (or from the layout transitions recorded
	// just before this pass) and are cleared by loadOp, so the only real requirement is ordering.
	deps[1].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].dstSubpass = 0;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	// End: the 2D compositor samples the resolved colour later in the same command buffer.
	deps[2].srcSubpass = 1;
	deps[2].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = msaa ? 5u : 4u;
	info.pAttachments = attachments;
	info.subpassCount = 2;
	info.pSubpasses = subpasses;
	info.dependencyCount = 3;
	info.pDependencies = deps;
	if (vkCreateRenderPass(device, &info, nullptr, &m_scenePass) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: scene render pass creation failed.");
		m_scenePass = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

bool CVulkan3D::CreateSceneInputDescriptors()
{
	VkDevice device = m_context.Device();
	VkDescriptorSetLayoutBinding bindings[2] = {};
	for (int i = 0; i < 2; i++)
	{
		bindings[i].binding = (uint32_t)i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = bindings;
	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_sceneInputLayout) != VK_SUCCESS)
		return false;

	VkDescriptorPoolSize size = {};
	size.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	size.descriptorCount = 2;
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &size;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_sceneInputPool) != VK_SUCCESS)
		return false;

	VkDescriptorSetAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.descriptorPool = m_sceneInputPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &m_sceneInputLayout;
	return vkAllocateDescriptorSets(device, &alloc, &m_sceneInputSet) == VK_SUCCESS;
}

bool CVulkan3D::CreateSceneCompositePipeline()
{
	/*
	 * Per-sample compositing is not optional in this mode: the multisampled variant reads the sample
	 * it is shading, which requires sample-rate shading (and gl_SampleID). Without the feature the
	 * only fallback would be to apply one sample's transparency to the whole pixel, which is worse
	 * than the dynamic-rendering path, so the render pass is simply not used.
	 */
	if (m_samples > VK_SAMPLE_COUNT_1_BIT && !m_context.SampleRateShadingSupported())
	{
		ErrorLog("Vulkan3D: the two-subpass composite needs sampleRateShading under MSAA; using dynamic rendering.");
		return false;
	}

	VkDevice device = m_context.Device();
	if (!CreateSceneInputDescriptors())
	{
		ErrorLog("Vulkan3D: scene composite descriptor set creation failed.");
		return false;
	}

	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = scene_composite_vert_spv_size;
	moduleInfo.pCode = scene_composite_vert_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_sceneCompositeVert) != VK_SUCCESS)
		return false;
	// The multisampled variant has to name the sample it reads; the single-sampled one must not. See
	// scene_composite.glsl for the two-line difference between them.
	moduleInfo.codeSize = (m_samples > VK_SAMPLE_COUNT_1_BIT) ? scene_composite_ms_frag_spv_size : scene_composite_frag_spv_size;
	moduleInfo.pCode = (m_samples > VK_SAMPLE_COUNT_1_BIT) ? scene_composite_ms_frag_spv : scene_composite_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_sceneCompositeFrag) != VK_SUCCESS)
		return false;

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &m_sceneInputLayout;
	if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_sceneCompositeLayout) != VK_SUCCESS)
		return false;

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = m_sceneCompositeVert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = m_sceneCompositeFrag;
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
	/*
	 * Per-sample under MSAA.
	 *
	 * Subpass 0 shades once per pixel, but the transparency attachment holds a value per sample, and
	 * this is the stage that decides what each of those samples becomes. Running the composite per
	 * sample is what keeps the transparency edges antialiased; the resolve attachment then averages
	 * the composited result. Without per-sample shading the composite could only apply one sample's
	 * transparency to the whole pixel, which is the aliasing this mode exists to avoid.
	 */
	const bool perSample = m_samples > VK_SAMPLE_COUNT_1_BIT && m_context.SampleRateShadingSupported();
	VkPipelineMultisampleStateCreateInfo multi = {};
	multi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multi.rasterizationSamples = m_samples;
	multi.sampleShadingEnable = perSample ? VK_TRUE : VK_FALSE;
	multi.minSampleShading = perSample ? 1.0f : 0.0f;
	VkPipelineDepthStencilStateCreateInfo depth = {};
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.depthTestEnable = VK_FALSE;
	depth.depthWriteEnable = VK_FALSE;
	depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

	// Source-over into the premultiplied scene. Alpha uses the same factors, which is what carries
	// the scene's coverage to the 2D compositor so the Render2D bottom layer shows through the sky.
	VkPipelineColorBlendAttachmentState blendAtt = {};
	blendAtt.blendEnable = VK_TRUE;
	blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
	blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
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

	VkGraphicsPipelineCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.renderPass = m_scenePass;
	info.subpass = 1;
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &vertex;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multi;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = m_sceneCompositeLayout;
	VkResult result = vkCreateGraphicsPipelines(device, m_context.PipelineCache(), 1, &info, nullptr, &m_sceneComposite);
	if (result != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: scene composite pipeline creation failed (%d).", (int)result);
		return false;
	}
	return true;
}

/*
 * Descriptor set 1: the node array and the per-pixel count image. Created once per session; the
 * buffers and images they point at are rewritten whenever the scene targets are rebuilt (see
 * EnsureOitTargets).
 */
bool CVulkan3D::CreateOitSetLayout()
{
	if (m_oitSetLayout != VK_NULL_HANDLE)
		return true;

	VkDevice device = m_context.Device();
	VkDescriptorSetLayoutBinding bindings[2] = {};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = bindings;
	return vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_oitSetLayout) == VK_SUCCESS;
}

bool CVulkan3D::CreateOitDescriptors()
{
	VkDevice device = m_context.Device();
	if (!CreateOitSetLayout())
		return false;

	VkDescriptorPoolSize sizes[2] = {};
	sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	sizes[0].descriptorCount = 1;
	sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	sizes[1].descriptorCount = 1;
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = sizes;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_oitPool) != VK_SUCCESS)
		return false;

	VkDescriptorSetAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.descriptorPool = m_oitPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &m_oitSetLayout;
	return vkAllocateDescriptorSets(device, &alloc, &m_oitSet) == VK_SUCCESS;
}

/*
 * The resolve pipeline: one full-screen triangle that reads the list and composites it into the
 * colour attachment with source-over blending.
 *
 * The output is premultiplied, so the blend is ONE / ONE_MINUS_SRC_ALPHA. That is algebraically
 * the same as New3D's SRC_ALPHA / ONE_MINUS_SRC_ALPHA applied to a straight-alpha source, and it
 * is what allows the whole sorted list to be collapsed into one fragment: source-over is
 * associative, so compositing N layers into one premultiplied colour and blending that once over
 * the opaque result is identical to blending the layers one at a time.
 */
bool CVulkan3D::CreateOitResolvePipeline()
{
	VkDevice device = m_context.Device();
	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = oit_resolve_vert_spv_size;
	moduleInfo.pCode = oit_resolve_vert_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_oitResolveVert) != VK_SUCCESS)
		return false;
	moduleInfo.codeSize = oit_resolve_frag_spv_size;
	moduleInfo.pCode = oit_resolve_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_oitResolveFrag) != VK_SUCCESS)
		return false;

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &m_oitSetLayout;
	if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_oitResolveLayout) != VK_SUCCESS)
		return false;

	const int32_t perSample = m_oitPerSample ? 1 : 0;
	VkSpecializationMapEntry specEntry = {};
	specEntry.constantID = 0;
	specEntry.offset = 0;
	specEntry.size = sizeof(perSample);
	VkSpecializationInfo specInfo = {};
	specInfo.mapEntryCount = 1;
	specInfo.pMapEntries = &specEntry;
	specInfo.dataSize = sizeof(perSample);
	specInfo.pData = &perSample;

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = m_oitResolveVert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = m_oitResolveFrag;
	stages[1].pName = "main";
	stages[1].pSpecializationInfo = &specInfo;

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

	/*
	 * Per-sample resolve under MSAA.
	 *
	 * The capture stage stores the rasteriser's coverage mask in each node, so the resolve has to
	 * run once per sample to apply it: with one invocation per pixel it could only apply the
	 * transparency to the whole pixel, which is exactly the antialiasing loss this avoids. The
	 * device feature is optional and checked at Init; when it is absent the specialization constant
	 * turns the per-sample branch off and the mode resolves at pixel rate.
	 */
	VkPipelineMultisampleStateCreateInfo multi = {};
	multi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multi.rasterizationSamples = m_samples;
	multi.sampleShadingEnable = m_oitPerSample ? VK_TRUE : VK_FALSE;
	multi.minSampleShading = m_oitPerSample ? 1.0f : 0.0f;

	VkPipelineDepthStencilStateCreateInfo depth = {};
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.depthTestEnable = VK_FALSE;
	depth.depthWriteEnable = VK_FALSE;
	depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

	VkPipelineColorBlendAttachmentState blendAtt[3] = {};
	blendAtt[0].blendEnable = VK_TRUE;
	blendAtt[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAtt[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAtt[0].colorBlendOp = VK_BLEND_OP_ADD;
	blendAtt[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAtt[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAtt[0].alphaBlendOp = VK_BLEND_OP_ADD;
	blendAtt[0].colorWriteMask = 0xF;
	/*
	 * One colour attachment and no depth attachment. The two transparency targets are not used in
	 * this mode, and the depth buffer is not read here either - the capture already applied the
	 * depth test. Naming them would make the pass load and store 400 MB of 4K 4x MSAA surfaces for
	 * nothing.
	 */
	VkPipelineColorBlendStateCreateInfo blend = {};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = blendAtt;

	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamics;

	VkFormat colorFormats[3] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
	VkPipelineRenderingCreateInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = colorFormats;
	rendering.depthAttachmentFormat = VK_FORMAT_UNDEFINED;

	VkGraphicsPipelineCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.pNext = &rendering;
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &vertex;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multi;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = m_oitResolveLayout;
	VkResult result = vkCreateGraphicsPipelines(device, m_context.PipelineCache(), 1, &info, nullptr, &m_oitResolve);
	if (result != VK_SUCCESS)
		ErrorLog("Vulkan3D: OIT resolve pipeline creation failed (%d).", (int)result);
	return result == VK_SUCCESS;
}

/*
 * The index buffer every scene draw uses.
 *
 * Six indices describe one primitive: two triangles over the 0-2 diagonal of the
 * four corners. They are quad-local (0..3), so the same block of indices serves
 * every primitive and the draw's base vertex - always a multiple of four - chooses
 * which one.
 *
 * The block is repeated for a batch of quads rather than stored once, because
 * indexCount has to cover the whole draw. 16384 quads is the largest batch 16-bit
 * indices can address (65536 vertices), which keeps the buffer at 192 KB instead
 * of the 36 MB an index per ROM vertex would need; a mesh larger than that is
 * split into several draws against the same block.
 *
 * The diagonal is fixed. Which one is used does not affect shading here, because
 * every fragment solves for the quad's own (s, t) instead of using a triangle's
 * barycentric coordinates; what it does decide is which of the two triangles
 * covers a fragment for depth and coverage purposes.
 */
bool CVulkan3D::CreateQuadIndexBuffer()
{
	const VkDeviceSize bytes = (VkDeviceSize)kQuadsPerIndexBatch * 6 * sizeof(uint16_t);
	if (!m_context.Memory().CreateBuffer(bytes,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_quadIndices))
	{
		ErrorLog("Vulkan3D: quad index buffer allocation failed (%llu KB).", (unsigned long long)(bytes / 1024));
		return false;
	}

	uint16_t* indices = static_cast<uint16_t*>(m_quadIndices.mapped);
	if (indices == nullptr)
	{
		ErrorLog("Vulkan3D: quad index buffer is not mapped.");
		return false;
	}
	static const uint16_t kPattern[6] = { 0, 1, 2, 0, 2, 3 };
	for (uint32_t quad = 0; quad < kQuadsPerIndexBatch; quad++)
	{
		uint16_t* dst = indices + (size_t)quad * 6;
		uint16_t base = (uint16_t)(quad * 4);
		for (int i = 0; i < 6; i++)
			dst[i] = (uint16_t)(base + kPattern[i]);
	}
	return true;
}

// Ambient / scroll fog background pass. This mirrors New3D, which draws a
// full-viewport quad into the opaque layer before any 3D geometry. Depth test
// and writes are off, so the quad only seeds the background colour; the 3D
// geometry is then drawn straight over it.
bool CVulkan3D::CreateFogPipeline()
{
	VkDevice device = m_context.Device();

	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = fog_vert_spv_size;
	moduleInfo.pCode = fog_vert_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_fogVert) != VK_SUCCESS)
		return false;
	moduleInfo.codeSize = fog_frag_spv_size;
	moduleInfo.pCode = fog_frag_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_fogFrag) != VK_SUCCESS)
		return false;

	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	range.size = sizeof(R3DFogPush);
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_fogLayout) != VK_SUCCESS)
		return false;

	return CreateFogPipelineFor(VK_NULL_HANDLE, &m_fog);
}

/*
 * The same fog pipeline, created for subpass 0 of the scene render pass.
 *
 * A pipeline built for dynamic rendering cannot be bound inside a render pass instance, so the
 * subpass path needs its own. The shader modules, the layout and every state are shared; only the
 * render pass the pipeline is compiled against differs.
 */
bool CVulkan3D::CreateFogPassPipeline()
{
	return CreateFogPipelineFor(m_scenePass, &m_fogPass);
}

bool CVulkan3D::CreateFogPipelineFor(VkRenderPass renderPass, VkPipeline* dest)
{
	VkDevice device = m_context.Device();

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = m_fogVert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = m_fogFrag;
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
	multi.rasterizationSamples = m_samples;
	VkPipelineDepthStencilStateCreateInfo depth = {};
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.depthTestEnable = VK_FALSE;
	depth.depthWriteEnable = VK_FALSE;
	depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
	// The fog pass runs inside the scene render pass, which binds all three scene targets. It only
	// writes the colour layer, so the other two attachments are masked off.
	VkPipelineColorBlendAttachmentState blendAtt[3] = {};
	blendAtt[0].blendEnable = VK_FALSE;
	blendAtt[0].colorWriteMask = 0xF;
	blendAtt[1].colorWriteMask = 0x0;
	blendAtt[2].colorWriteMask = 0x0;
	VkPipelineColorBlendStateCreateInfo blend = {};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 3;
	blend.pAttachments = blendAtt;
	VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamics;

	VkFormat colorFormats[3] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
	VkPipelineRenderingCreateInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 3;
	rendering.pColorAttachmentFormats = colorFormats;
	rendering.depthAttachmentFormat = depthFormat;

	VkGraphicsPipelineCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	if (renderPass != VK_NULL_HANDLE)
	{
		info.renderPass = renderPass;
		info.subpass = 0;
	}
	else
	{
		info.pNext = &rendering;
	}
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &vertex;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multi;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = m_fogLayout;
	VkResult result = vkCreateGraphicsPipelines(device, m_context.PipelineCache(), 1, &info, nullptr, dest);
	if (result != VK_SUCCESS)
		ErrorLog("Vulkan3D: vkCreateGraphicsPipelines (fog) failed (%d).", (int)result);
	return result == VK_SUCCESS;
}

/*
 * CreateLosPipeline():
 *
 * Builds the one-invocation compute pipeline that reads the LOS texel out of a multisampled depth
 * image (see Shaders/los.comp and CopyLos). Called only when multisampling is in force.
 *
 * The descriptor set is completed later: the depth view does not exist until EnsureTargets creates
 * the render targets, so only the result buffer is written here and WriteLosDescriptor() fills in
 * binding 0 whenever those targets are (re)built.
 */
bool CVulkan3D::CreateLosPipeline()
{
	VkDevice device = m_context.Device();

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	// texelFetch does no filtering, so the filter modes are irrelevant; CLAMP_TO_EDGE and NEAREST
	// only make the sampler well defined for a coordinate that is already an exact texel.
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if (vkCreateSampler(device, &samplerInfo, nullptr, &m_losSampler) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: line-of-sight sampler creation failed.");
		return false;
	}

	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = los_comp_spv_size;
	moduleInfo.pCode = los_comp_spv;
	if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_losModule) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: line-of-sight compute shader failed to load.");
		return false;
	}

	VkDescriptorSetLayoutBinding bindings[2] = {};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo setLayoutInfo = {};
	setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutInfo.bindingCount = 2;
	setLayoutInfo.pBindings = bindings;
	if (vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &m_losLayout) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: line-of-sight descriptor layout creation failed.");
		return false;
	}

	VkDescriptorPoolSize sizes[2] = {};
	sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sizes[0].descriptorCount = 1;
	sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	sizes[1].descriptorCount = 1;
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = sizes;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_losPool) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: line-of-sight descriptor pool creation failed.");
		return false;
	}
	VkDescriptorSetAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.descriptorPool = m_losPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &m_losLayout;
	if (vkAllocateDescriptorSets(device, &alloc, &m_losSet) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: line-of-sight descriptor set allocation failed.");
		return false;
	}

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = m_los.buffer;
	bufferInfo.offset = 0;
	bufferInfo.range = VK_WHOLE_SIZE;
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_losSet;
	write.dstBinding = 1;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.pBufferInfo = &bufferInfo;
	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	range.size = sizeof(int32_t) * 4;   // ivec2 pos, uint index, uint samples
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &m_losLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_losPipeLayout) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: line-of-sight pipeline layout creation failed.");
		return false;
	}

	VkPipelineShaderStageCreateInfo stage = {};
	stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = m_losModule;
	stage.pName = "main";
	VkComputePipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage = stage;
	pipelineInfo.layout = m_losPipeLayout;
	if (vkCreateComputePipelines(device, m_context.PipelineCache(), 1, &pipelineInfo, nullptr, &m_losPipeline) != VK_SUCCESS)
	{
		ErrorLog("Vulkan3D: line-of-sight compute pipeline creation failed.");
		return false;
	}
	return true;
}

// Points the compute descriptor at the current multisampled depth view. Called from EnsureTargets,
// because that is the only place the view is (re)created.
void CVulkan3D::WriteLosDescriptor()
{
	if (!m_losSet || m_depth.view == VK_NULL_HANDLE)
		return;
	VkDescriptorImageInfo info = {};
	info.sampler = m_losSampler;
	info.imageView = m_depth.view;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_losSet;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &info;
	vkUpdateDescriptorSets(m_context.Device(), 1, &write, 0, nullptr);
}

// Selects the fog parameters and records the background quad(s), mirroring
// New3D's DrawScrollFog() / DrawAmbientFog().
//
// Scroll fog wins when any viewport in the lowest priority layer that has one
// sets it: the colour and scroll amount come from that viewport, while the
// attenuation / ambient / spotlight-on-fog parameters come from any viewport
// sharing the same fog colour that actually enables fogging. Otherwise the
// lowest fogAmbient in the first priority layer containing viewports darkens
// the background with a black quad.
void CVulkan3D::DrawFog(VkCommandBuffer cmd, bool insideScenePass)
{
	const New3D::Viewport* scrollNode = nullptr;

	// Highest scroll fog value, starting from the lowest priority layer.
	for (int layer = 0; layer < 4 && scrollNode == nullptr; layer++)
	{
		float best = 0.0f;
		for (const auto& node : m_scene.Nodes())
		{
			const New3D::Viewport& vp = node.viewport;
			if (vp.priority != layer || node.models.empty())
				continue;
			if (vp.scrollFog > best)
			{
				best = vp.scrollFog;
				scrollNode = &vp;
			}
		}
	}

	// Ambient fog: lowest fogAmbient in the first priority layer with viewports.
	const New3D::Viewport* ambientNode = nullptr;
	if (scrollNode == nullptr)
	{
		for (int layer = 0; layer < 4; layer++)
		{
			bool hasPriority = false;
			const New3D::Viewport* best = nullptr;
			float bestAmbient = 1.0f;
			for (const auto& node : m_scene.Nodes())
			{
				const New3D::Viewport& vp = node.viewport;
				if (vp.priority != layer || node.models.empty())
					continue;
				hasPriority = true;
				if (vp.scrollFog > 0.0f)
					continue;
				// Without a fog density or start the effect is disabled (Lost World)
				if (vp.fogDensity <= 0.0f && vp.fogStart <= 0.0f)
					continue;
				if (vp.fogAmbient < bestAmbient)
				{
					bestAmbient = vp.fogAmbient;
					best = &vp;
				}
			}
			if (best != nullptr)
				ambientNode = best;
			if (ambientNode != nullptr || hasPriority)
				break;
		}
	}

	if (scrollNode == nullptr && ambientNode == nullptr)
		return;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, insideScenePass ? m_fogPass : m_fog);

	auto drawFogQuad = [&](const New3D::Viewport& vp, const float rgba[4], float attenuation, float ambient,
		const float spotFogColor[3], const float spotColour[4])
	{
		int vx = vp.x;
		int vy = (int)m_color.height - (vp.y + vp.height);
		int vw = vp.width;
		int vh = vp.height;
		if (vw <= 0 || vh <= 0)
			return;
		if (vx < 0) { vw += vx; vx = 0; }
		if (vy < 0) { vh += vy; vy = 0; }
		if (vx + vw > (int)m_color.width)
			vw = (int)m_color.width - vx;
		if (vy + vh > (int)m_color.height)
			vh = (int)m_color.height - vy;
		if (vw <= 0 || vh <= 0)
			return;

		R3DFogPush push = {};
		push.fogColour[0] = rgba[0];
		push.fogColour[1] = rgba[1];
		push.fogColour[2] = rgba[2];
		push.fogColour[3] = rgba[3];
		push.fogAmbientAtt[0] = ambient;
		push.fogAmbientAtt[1] = attenuation;
		push.spotEllipse[0] = spotColour[0];
		push.spotEllipse[1] = (float)m_color.height - spotColour[1];
		push.spotEllipse[2] = spotColour[2];
		push.spotEllipse[3] = spotColour[3];
		push.spotFogColor[0] = spotFogColor[0];
		push.spotFogColor[1] = spotFogColor[1];
		push.spotFogColor[2] = spotFogColor[2];
		vkCmdPushConstants(cmd, m_fogLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

		VkViewport view = {};
		view.x = (float)vx;
		view.y = (float)vy;
		view.width = (float)vw;
		view.height = (float)vh;
		view.maxDepth = 1.0f;
		VkRect2D scissor = { { vx, vy }, { (uint32_t)vw, (uint32_t)vh } };
		vkCmdSetViewport(cmd, 0, 1, &view);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdDraw(cmd, 4, 1, 0, 0);
	};

	if (scrollNode != nullptr)
	{
		// Find a viewport with the same fog colour that actually enables fogging.
		// That is where the attenuation, ambient and spotlight parameters come from.
		const New3D::Viewport* source = scrollNode;
		bool found = false;
		for (const auto& node : m_scene.Nodes())
		{
			const New3D::Viewport& vp = node.viewport;
			if (vp.fogColour[0] != scrollNode->fogColour[0] ||
				vp.fogColour[1] != scrollNode->fogColour[1] ||
				vp.fogColour[2] != scrollNode->fogColour[2])
				continue;

			bool spotOnFog = false;
			for (float c : vp.spotFogColor)
				if (c > 0.0f) { spotOnFog = true; break; }

			bool fog = vp.fogDensity > 0.0f || vp.fogStart > 0.0f || vp.scrollAtt > 0.0f;
			bool spotlightFog = vp.fogAttenuation > 0.0f && spotOnFog;

			if (fog || spotlightFog)
			{
				source = &vp;
				found = true;
				break;
			}
		}
		if (!found)
			return;

		// If regular fog is enabled the hardware seems to only apply that and
		// skips the spotlight on fog (ECA).
		float attenuation = source->fogAttenuation;
		bool regularFog = source->fogDensity > 0.0f || source->fogStart > 0.0f || source->scrollAtt > 0.0f;
		bool spotOnFog = false;
		for (float c : source->spotFogColor)
			if (c > 0.0f) { spotOnFog = true; break; }
		if (regularFog && spotOnFog)
			attenuation = 0.0f;

		float rgba[4] = { scrollNode->fogColour[0], scrollNode->fogColour[1], scrollNode->fogColour[2], scrollNode->scrollFog };
		drawFogQuad(*scrollNode, rgba, attenuation, source->fogAmbient, source->spotFogColor, source->spotEllipse);
	}
	else
	{
		// Ambient fog is a black quad whose alpha dims whatever is behind it.
		float rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f - ambientNode->fogAmbient };
		drawFogQuad(*ambientNode, rgba, 0.0f, 1.0f, ambientNode->spotFogColor, ambientNode->spotEllipse);
	}
}

void CVulkan3D::DestroyTargets()
{
	/*
	 * The handles are logged on the way out because that is the only way to tell afterwards whether a
	 * stale reference survived the rebuild.
	 *
	 * Vulkan handles are pointers and a freed one is very often handed straight back to the next
	 * creation. Anything that caches a view and decides whether to rewrite a descriptor by comparing
	 * handles therefore cannot tell "the same image" from "a different image that landed on the same
	 * address" - and the failure shows up much later, as the compositor sampling something that was
	 * destroyed. Naming the values is what makes that visible instead of theoretical.
	 */
	InfoLog("Vulkan: 3D targets destroyed (colour view %p, depth view %p, resolve view %p, trans views %p/%p/%p/%p, generation %llu).",
		(void*)m_color.view, (void*)m_depth.view, (void*)m_colorResolve.view,
		(void*)m_trans1.view, (void*)m_trans1Resolve.view,
		(void*)m_trans2.view, (void*)m_trans2Resolve.view,
		(unsigned long long)m_context.SwapchainGeneration());

	// The framebuffer holds the views that are about to be destroyed, so it has to go first.
	if (m_sceneFramebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(m_context.Device(), m_sceneFramebuffer, nullptr);
		m_sceneFramebuffer = VK_NULL_HANDLE;
	}

	m_context.DestroyView(m_color.view);
	m_color.view = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_color);
	m_context.DestroyView(m_depth.view);
	m_depth.view = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_depth);
	m_context.DestroyView(m_colorResolve.view);
	m_colorResolve.view = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_colorResolve);
	m_context.DestroyView(m_trans1.view);
	m_trans1.view = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_trans1);
	m_context.DestroyView(m_trans1Resolve.view);
	m_trans1Resolve.view = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_trans1Resolve);
	m_context.DestroyView(m_trans2.view);
	m_trans2.view = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_trans2);
	m_context.DestroyView(m_trans2Resolve.view);
	m_trans2Resolve.view = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_trans2Resolve);
	m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans1Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans1ResolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans2Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans2ResolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	DestroyOitTargets();
}

/*
 * The fixed-slot A-buffer, recreated with the scene targets.
 *
 * kOitLayers slots per scene pixel, 8 bytes each, in a structure-of-arrays layout (slot s of pixel
 * p at s * pixelCount + p). At 4K that is 4 * 8.3M * 8 = 265 MB, and the SoA layout is what makes
 * neighbouring pixels in a warp address neighbouring words - the property a linked list could not
 * offer. The per-pixel counts are an r32ui image the size of the scene target.
 */
static constexpr uint32_t kOitLayers = 4;

bool CVulkan3D::EnsureOitTargets()
{
	if (m_alphaSortMode != 1 || m_oitSetLayout == VK_NULL_HANDLE || m_oitFrag == VK_NULL_HANDLE)
		return false;

	DestroyOitTargets();

	const VkExtent2D extent = SceneTargetExtent();
	const uint64_t pixels = (uint64_t)extent.width * (uint64_t)extent.height;

	const VkDeviceSize nodeBytes = (VkDeviceSize)kOitLayers * (VkDeviceSize)pixels * sizeof(uint32_t) * 2;
	if (!m_context.Memory().CreateBuffer(nodeBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_oitNodes))
	{
		ErrorLog("Vulkan: sorted-transparency A-buffer allocation failed (%llu MB); using the two-layer path.",
			(unsigned long long)(nodeBytes / (1024 * 1024)));
		return false;
	}

	if (!m_context.Memory().CreateImage(extent.width, extent.height, 1, VK_FORMAT_R32_UINT,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, m_oitCount))
	{
		ErrorLog("Vulkan: sorted-transparency count image allocation failed; using the two-layer path.");
		m_context.Memory().DestroyBuffer(m_oitNodes);
		return false;
	}
	m_oitCount.view = m_context.CreateView(m_oitCount.image, VK_FORMAT_R32_UINT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
	m_oitCountLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	/*
	 * Rewritten together with the views: the handles change on every resize, and a descriptor
	 * still naming the previous image is the classic "sampled something destroyed" bug. The scene
	 * targets are rebuilt behind a vkDeviceWaitIdle, so no frame can be using either version.
	 */
	VkDescriptorBufferInfo nodesInfo = {};
	nodesInfo.buffer = m_oitNodes.buffer;
	nodesInfo.offset = 0;
	nodesInfo.range = VK_WHOLE_SIZE;
	VkDescriptorImageInfo countInfo = {};
	countInfo.imageView = m_oitCount.view;
	countInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet writes[2] = {};
	for (int i = 0; i < 2; i++)
	{
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = m_oitSet;
		writes[i].dstBinding = (uint32_t)i;
		writes[i].descriptorCount = 1;
	}
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[0].pBufferInfo = &nodesInfo;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[1].pImageInfo = &countInfo;
	vkUpdateDescriptorSets(m_context.Device(), 2, writes, 0, nullptr);

	InfoLog("Vulkan: sorted transparency %ux%u, A-buffer %u slot(s) per pixel (%.1f MB), %s resolve.",
		extent.width, extent.height, kOitLayers, (double)nodeBytes / (1024.0 * 1024.0),
		m_oitPerSample ? "per-sample" : "per-pixel");
	return true;
}

void CVulkan3D::DestroyOitTargets()
{
	m_context.DestroyView(m_oitCount.view);
	m_oitCount.view = VK_NULL_HANDLE;
	m_context.Memory().DestroyImage(m_oitCount);
	m_context.Memory().DestroyBuffer(m_oitNodes);
	m_oitCountLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_oitReady = false;
}

void CVulkan3D::OitMemoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
	VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) const
{
	VkMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	VkDependencyInfo dep = {};
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.memoryBarrierCount = 1;
	dep.pMemoryBarriers = &barrier;
	CmdPipelineBarrier2(cmd, &dep);
}

/*
 * SceneTargetExtent():
 *
 * The size every scene target is allocated at. The visible picture's resolution normally, and the
 * fixed internal one under low-resolution rendering. Kept in one place because five images per
 * resize depend on it: the colour target, its resolve, the depth buffer, and the two transparency
 * targets, each with its own resolve. Any of those left at the swapchain size would either fail the
 * framebuffer's size check or silently sample the wrong region.
 */
VkExtent2D CVulkan3D::SceneTargetExtent() const
{
	if (m_internalExtent.width > 0 && m_internalExtent.height > 0)
		return m_internalExtent;
	return m_context.Extent();
}

/*
 * RefreshSceneViewport():
 *
 * Derives the scene coordinate system from m_xOff/m_yOff/m_xRes/m_yRes and the low-resolution
 * setting, and hands it to the scene builder.
 *
 * With the option off this is the identity of what the renderer always did: the 3D viewport is the
 * visible picture inside a swapchain-sized target. With it on the target *is* the picture, so the
 * scene starts at 0,0 and fills it - and the offsets, which are window coordinates, must not leak into
 * the scene's own viewport arithmetic (they would push every node off the small target).
 *
 * Called from Init and from PrepareFrame; the latter because widescreen is re-read every frame for
 * the in-game overlay.
 */
void CVulkan3D::RefreshSceneViewport()
{
	const bool lowRes = m_internalExtent.width > 0 && m_internalExtent.height > 0;
	m_sceneXOff = lowRes ? 0 : m_xOff;
	m_sceneYOff = lowRes ? 0 : m_yOff;
	m_sceneXRes = lowRes ? m_internalExtent.width : m_xRes;
	m_sceneYRes = lowRes ? m_internalExtent.height : m_yRes;
	m_sceneTotalX = lowRes ? m_internalExtent.width : m_totalX;
	m_sceneTotalY = lowRes ? m_internalExtent.height : m_totalY;
	m_scene.SetViewport(m_sceneXOff, m_sceneYOff, m_sceneXRes, m_sceneYRes,
		m_sceneTotalX, m_sceneTotalY, m_wide && !lowRes);
}

bool CVulkan3D::EnsureTargets()
{
	VkExtent2D extent = SceneTargetExtent();
	if (extent.width == 0 || extent.height == 0)
		return false;
	if (m_color.image && m_color.width == extent.width && m_color.height == extent.height && m_targetGeneration == m_context.SwapchainGeneration())
		return true;
	if (m_context.Device())
		vkDeviceWaitIdle(m_context.Device());
	DestroyTargets();

	// Hardware MSAA: render multisampled and resolve into a single-sampled image that the 2D layer
	// can sample. MSAA smooths polygon coverage only, so texel content (car decals, small sprites)
	// keeps its full sharpness. The sample count was validated against the driver in the constructor.
	const VkImageUsageFlags colorUsage = m_samples > VK_SAMPLE_COUNT_1_BIT
		? (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		: (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	if (!m_context.Memory().CreateImage(extent.width, extent.height, 1, VK_FORMAT_R8G8B8A8_UNORM,
			colorUsage, m_color, (uint32_t)m_samples))
		return false;
	m_color.view = m_context.CreateView(m_color.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
	// TRANSFER_SRC is only legal on a single-sampled image, so the depth image must not ask for it
	// when multisampling: only TRANSFER_DST is permitted on a multisampled image. Under MSAA the
	// readback cannot use a copy anyway; it samples the image through the compute shader instead
	// (see CopyLos), which is why SAMPLED is requested there - and why the constructor probes for
	// exactly this combination before committing to a sample count.
	const VkImageUsageFlags depthUsage = (m_samples > VK_SAMPLE_COUNT_1_BIT)
		? (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		: (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	if (!m_context.Memory().CreateImage(extent.width, extent.height, 1, VK_FORMAT_D32_SFLOAT,
			depthUsage, m_depth, (uint32_t)m_samples))
		return false;
	m_depth.view = m_context.CreateView(m_depth.image, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1);
	if (m_samples > VK_SAMPLE_COUNT_1_BIT)
		WriteLosDescriptor();
	if (m_samples > VK_SAMPLE_COUNT_1_BIT)
	{
		if (!m_context.Memory().CreateImage(extent.width, extent.height, 1, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_colorResolve))
			return false;
		m_colorResolve.view = m_context.CreateView(m_colorResolve.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
	}
	m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans1Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans1ResolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans2Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans2ResolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	// The two transparency targets: same size and sample count as the colour target. In the default
	// mode their resolved forms are read by the 2D composite; in the subpass mode they are read as
	// input attachments by subpass 1, which is why INPUT_ATTACHMENT is requested unconditionally.
	const VkImageUsageFlags transUsage = colorUsage | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	auto createTransTarget = [&](AllocatedImage& image, AllocatedImage& resolve) -> bool
	{
		if (!m_context.Memory().CreateImage(extent.width, extent.height, 1, VK_FORMAT_R8G8B8A8_UNORM,
				transUsage, image, (uint32_t)m_samples))
			return false;
		image.view = m_context.CreateView(image.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
		if (m_samples > VK_SAMPLE_COUNT_1_BIT)
		{
			if (!m_context.Memory().CreateImage(extent.width, extent.height, 1, VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, resolve))
				return false;
			resolve.view = m_context.CreateView(resolve.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
		}
		return true;
	};
	if (!createTransTarget(m_trans1, m_trans1Resolve) || !createTransTarget(m_trans2, m_trans2Resolve))
		return false;
	m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans1Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans1ResolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans2Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_trans2ResolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_targetGeneration = m_context.SwapchainGeneration();
	m_output.SetSceneColor(m_samples > VK_SAMPLE_COUNT_1_BIT ? m_colorResolve.view : m_color.view);
	m_output.SetTransColor(m_samples > VK_SAMPLE_COUNT_1_BIT ? m_trans1Resolve.view : m_trans1.view,
		m_samples > VK_SAMPLE_COUNT_1_BIT ? m_trans2Resolve.view : m_trans2.view);

	/*
	 * Give the compositor's images the layout its descriptors declare before anything samples them.
	 *
	 * The 2D layer draws before the 3D renderer does (CModel3::RenderFrame runs TileGen first), so
	 * the first composite of a session samples these images before any scene pass has named a
	 * layout for them. A freshly created image is in UNDEFINED, and a descriptor that declares
	 * SHADER_READ_ONLY over that is exactly the mismatch the validation layer reports. DrawScene
	 * moves each image back to an attachment layout on the frames it renders into it, so this is
	 * only the initial state - it runs when the targets are created, not every frame.
	 */
	{
		VkCommandBuffer cmd = m_context.Cmd();
		auto handOver = [&](AllocatedImage& image, VkImageLayout& layout)
		{
			if (image.image == VK_NULL_HANDLE || layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				return;
			CmdTransition(cmd, image.image, layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT);
			layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		};
		if (m_samples > VK_SAMPLE_COUNT_1_BIT)
		{
			handOver(m_colorResolve, m_resolveLayout);
			handOver(m_trans1Resolve, m_trans1ResolveLayout);
			handOver(m_trans2Resolve, m_trans2ResolveLayout);
		}
		else
		{
			handOver(m_color, m_colorLayout);
			handOver(m_trans1, m_trans1Layout);
			handOver(m_trans2, m_trans2Layout);
		}
	}

	/*
	 * The framebuffer and the input-attachment descriptors for the scene render pass.
	 *
	 * Both are per-target rather than per-session because they name the views, and the views change
	 * on every resize. A failure here costs the subpass path, not the session: m_scenePassReady goes
	 * false and DrawScene falls back to the dynamic-rendering path, which produces the same picture
	 * (see DrawScene).
	 */
	if (m_scenePass != VK_NULL_HANDLE && m_sceneComposite != VK_NULL_HANDLE && m_sceneInputSet != VK_NULL_HANDLE)
	{
		m_scenePassReady = true;
		VkImageView views[5] = { m_color.view, m_trans1.view, m_trans2.view, m_depth.view, m_colorResolve.view };
		const uint32_t viewCount = (m_samples > VK_SAMPLE_COUNT_1_BIT) ? 5u : 4u;
		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = m_scenePass;
		framebufferInfo.attachmentCount = viewCount;
		framebufferInfo.pAttachments = views;
		framebufferInfo.width = extent.width;
		framebufferInfo.height = extent.height;
		framebufferInfo.layers = 1;
		if (vkCreateFramebuffer(m_context.Device(), &framebufferInfo, nullptr, &m_sceneFramebuffer) != VK_SUCCESS)
		{
			ErrorLog("Vulkan3D: scene framebuffer creation failed; the default transparency mode will use dynamic rendering.");
			m_scenePassReady = false;
		}
		else
		{
			VkDescriptorImageInfo images[2] = {};
			images[0].imageView = m_trans1.view;
			images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			images[1].imageView = m_trans2.view;
			images[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkWriteDescriptorSet writes[2] = {};
			for (int i = 0; i < 2; i++)
			{
				writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet = m_sceneInputSet;
				writes[i].dstBinding = (uint32_t)i;
				writes[i].descriptorCount = 1;
				writes[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
				writes[i].pImageInfo = &images[i];
			}
			vkUpdateDescriptorSets(m_context.Device(), 2, writes, 0, nullptr);
		}
	}
	else
	{
		m_scenePassReady = false;
	}
	// Sorted transparency is optional: a failure here leaves m_oitReady false and DrawScene takes
	// the original two-layer path for the rest of the session (or until the next resize succeeds).
	if (m_alphaSortMode == 1)
		m_oitReady = EnsureOitTargets();
	InfoLog("Vulkan: 3D target %ux%u, %ux MSAA%s.", extent.width, extent.height, (unsigned)m_samples,
		m_samples > VK_SAMPLE_COUNT_1_BIT ? " (resolved before compositing)" : "");
	if (m_internalExtent.width > 0)
	{
		// Stated on every rebuild, not only once: this line is what makes a log from a user who
		// reports "the picture is soft" answerable without asking them to open the settings window.
		InfoLog("Vulkan: low-resolution rendering - 3D scene %ux%u upscaled onto the %ux%u picture.",
			extent.width, extent.height, m_xRes, m_yRes);
	}
	InfoLog("Vulkan: 3D targets rebuilt (colour view %p, depth view %p, resolve view %p -> compositor, generation %llu, ROM verts %zu, RAM ready %d).",
		(void*)m_color.view, (void*)m_depth.view, (void*)m_colorResolve.view,
		(unsigned long long)m_targetGeneration, m_romGpuVerts, m_ramReady ? 1 : 0);
	return m_color.view && m_depth.view;
}

GpuQuadVertex CVulkan3D::ConvertVertex(const New3D::FVertex& src) const
{
	GpuQuadVertex dst = {};
	dst.pos[0] = src.base.pos[0];
	dst.pos[1] = src.base.pos[1];
	dst.pos[2] = src.base.pos[2];
	dst.pos[3] = 1.0f;		// unused, but never left as a trap pattern
	dst.normal[0] = src.base.normal[0];
	dst.normal[1] = src.base.normal[1];
	dst.normal[2] = src.base.normal[2];
	// Radial texture LOD is a per-polygon quantity; it rides in the normal's spare
	// slot so the layout stays four 16-byte groups.
	dst.normal[3] = src.textureNP;
	dst.uv[0] = src.base.texcoords[0];
	dst.uv[1] = src.base.texcoords[1];
	dst.uv[2] = src.base.fixedShade;
	dst.faceNormal[0] = src.faceNormal[0];
	dst.faceNormal[1] = src.faceNormal[1];
	dst.faceNormal[2] = src.faceNormal[2];
	dst.colour = (uint32_t)src.faceColour[0] | ((uint32_t)src.faceColour[1] << 8) | ((uint32_t)src.faceColour[2] << 16) | ((uint32_t)src.faceColour[3] << 24);
	return dst;
}

void CVulkan3D::BeginFrame(void)
{
	uint32_t frame = m_context.FrameIndex();
	if (m_losReady[frame] && m_los.mapped)
	{
		float values[4];
		std::memcpy(values, static_cast<uint8_t*>(m_los.mapped) + frame * sizeof(values), sizeof(values));
		std::lock_guard<std::mutex> guard(m_losMutex);
		for (int i = 0; i < 4; i++)
		{
			float depth = values[i];
			float z = depth / kNearPlane;
			auto* bytes = reinterpret_cast<unsigned char*>(&z);
			if (depth == 0.0f)
				bytes[0] |= 1;
			else
				bytes[0] &= 0xFE;
			m_losValue[i] = z;
		}
	}
	/*
	 * Reading a line-of-sight value consumes the copy. Without clearing the
	 * flag the same texel is read back on every present, which at 240/480 Hz is
	 * a readback per sub-frame for a value only the simulation consumes once.
	 */
	if (m_losReady[frame])
		m_losReady[frame] = false;

	m_frameSerial++;
	m_drew = false;
}

void CVulkan3D::EndFrame(void)
{
	if (!m_context.IsRecording() || !m_color.image)
		return;
	// Hand the scene colour over to the compositor.
	//
	// Under MSAA that has to be the resolved image, not the multisampled one. A multisampled image
	// cannot be read through a plain sampler2D (it needs texture2DMS), so sampling it anyway is
	// undefined behaviour - and the visible result was the 3D scene collapsing into a flat wash of
	// colour while the 2D layers drew normally. EnsureTargets set this correctly, but it returns
	// early once the targets exist, so it only ever ran on the frame the targets were created.
	// Setting it here too, where it runs every frame, is what keeps the compositor pointed at the
	// resolved image.
	if (m_samples > VK_SAMPLE_COUNT_1_BIT)
	{
		if (m_resolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			CmdTransition(m_context.Cmd(), m_colorResolve.image, m_resolveLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			m_resolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		m_output.SetSceneColor(m_colorResolve.view);
	}
	else
	{
		if (m_colorLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			CmdTransition(m_context.Cmd(), m_color.image, m_colorLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			m_colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		m_output.SetSceneColor(m_color.view);
	}
	// Tell the compositor whether this frame's scene target already holds the two transparency
	// layers (see scene_composite.glsl). False on every path that still draws them into their own
	// attachments, including the LOS fallback and all of AlphaSortMode=1.
	//
	// Not gated on m_drew: a present that runs no DrawScene composites the previous scene target,
	// and m_scenePrecomposited still describes that image.
	m_output.SetScenePrecomposited(m_scenePrecomposited);
	// Arcade checkerboard transparency (AlphaSortMode = 2) is not folded into the scene target, so
	// the compositor has to do the reconstruction itself. Cleared on every other path so a mode
	// change between sessions cannot leave the flag on.
	m_output.SetArcadeCheckerboard(m_alphaSortMode == 2);
	// Test hook: keep the one-pixel checkerboard visible instead of reconstructing it. Only
	// meaningful with mode 2 and read once per session, like the mode itself.
	m_output.SetCheckerRaw(m_checkerRaw);
}

void CVulkan3D::CollectTextures()
{
	m_textures.SetFrameSerial(m_frameSerial);
}

void CVulkan3D::BuildDrawList()
{
	/*
	 * HD texture pack, read live so toggling either switch from the in-game panel takes effect on the
	 * next simulation frame. Reload requests (F4 / the panel button) are applied here too, which is
	 * why this is also where the device is idled if one is pending.
	 */
	const bool hdLoad = m_config["LoadHDTextures"].ValueAsDefault<bool>(false);
	const bool hdDump = m_config["DumpHDTextures"].ValueAsDefault<bool>(false);
	if (hdLoad != m_hdLoadActive)
	{
		m_hdLoadActive = hdLoad;
		InfoLog("HD textures: loading %s.", hdLoad ? "on" : "off");
	}
	if (hdDump != m_hdDumpActive)
	{
		m_hdDumpActive = hdDump;
		InfoLog("HD textures: dumping %s.", hdDump ? "on" : "off");
	}
	m_textures.TickHDTextures(hdLoad, hdDump);
	// Dumping needs the hash whether or not a pack exists; loading only needs it when there is
	// something to look the hash up in.
	const bool hdHash = hdDump || (hdLoad && !m_textures.HDPackEmpty());

	for (int p = 0; p < 4; p++)
		for (int o = 0; o < 2; o++)
			for (int l = 0; l < 3; l++)
				m_drawBuckets[p][o][l].clear();

	/*
	 * The material array is kept on the CPU and uploaded at present time.
	 *
	 * It used to be written straight into both GPU slices here, which is a data
	 * race: this runs on the simulation tick, before the current frame slot's
	 * fence has been waited on, so the slice belonging to the *other* in-flight
	 * frame can still be executing - and a material overwritten mid-frame is a
	 * polygon that samples the wrong texture region for exactly that one present.
	 * It presented as an occasional white or smeared block, one frame long.
	 *
	 * A copy of the whole table is at most a few tens of kilobytes per present and
	 * never changes between sub-frames, so uploading it in DrawScene costs nothing
	 * measurable at 480 Hz.
	 */
	m_materialData.clear();
	m_materialData.reserve(1024);
	m_lastDrawCount = 0;

	auto& nodes = m_scene.Nodes();
	for (uint32_t ni = 0; ni < (uint32_t)nodes.size(); ni++)
	{
		New3D::Node& node = nodes[ni];
		const int priority = std::clamp(node.viewport.priority, 0, 3);

		/*
		 * A viewport whose dimensions collapsed takes every model with it, exactly
		 * as the old per-pass traversal did. Do it once here rather than per draw.
		 */
		if (node.viewport.width <= 0 || node.viewport.height <= 0)
		{
			if (!node.models.empty())
				m_skipDegenerateNode++;
			continue;
		}

		for (uint32_t mi = 0; mi < (uint32_t)node.models.size(); mi++)
		{
			New3D::Model& model = node.models[mi];
			if (!model.meshes)
				continue;

			for (auto& mesh : *model.meshes)
			{
				if (mesh.vertexCount <= 0)
					continue;

				const int overlay = mesh.highPriority ? 1 : 0;
				for (int layerIndex = 0; layerIndex < 3; layerIndex++)
				{
					const New3D::Layer layer = layerIndex == 0 ? New3D::Layer::colour
						: (layerIndex == 1 ? New3D::Layer::trans2 : New3D::Layer::trans1);
					if (!mesh.Render(layer, model.alpha))
						continue;

					if (m_materialData.size() >= kMaxMaterials)
					{
						if (m_materialData.size() == kMaxMaterials)
							ErrorLog("Vulkan: material table is full (%u); the rest of the scene is not drawn.", kMaxMaterials);
						return;
					}

					R3DMaterial material;
					FillMaterial(material, mesh, model);

					if ((material.flags & kMatTextured) != 0 && hdHash)
					{
						const uint64_t textureHash = m_textures.TextureHash(
							material.texturePage & 1, material.texX, material.texY,
							material.texWidth, material.texHeight, material.texFormat);
						if (textureHash != 0)
						{
							if (hdDump)
								m_textures.DumpTexture(textureHash, material.texturePage & 1,
									material.texX, material.texY, material.texWidth, material.texHeight,
									material.texFormat);
							if (hdLoad)
								material.hdTexIndex = m_textures.ResolveHDTexture(
									textureHash, material.wrapModeX, material.wrapModeY);
						}
					}

					/*
					 * Texture coverage diagnostics belong to the simulation frame, not to
					 * each present: at 240/480 Hz this block used to run once per sub-frame
					 * and dominated the draw submission cost. It runs here, where both the
					 * mesh and its material rectangle are known, and disappears from the
					 * interpolated presents entirely.
					 */
					if ((material.flags & kMatTextured) != 0)
					{
						const CVulkanTextureCache::Coverage coverage = m_textures.RegionCoverage(
							material.texturePage & 1, material.texX, material.texY,
							material.texWidth, material.texHeight);
						m_textures.NoteTexturedReference(coverage);

						if (coverage == CVulkanTextureCache::Coverage::None && m_sampleMissingRegions < 12)
						{
							m_sampleMissingRegions++;
							const unsigned tiles = m_textures.RegionTileCount(
								material.texturePage & 1, material.texX, material.texY, material.texWidth, material.texHeight);
							const unsigned written = m_textures.RegionEverWrittenTiles(
								material.texturePage & 1, material.texX, material.texY, material.texWidth, material.texHeight);
							const bool overflows = (material.texX + material.texWidth > 2048) ||
								(material.texY + material.texHeight > 1024);
							InfoLog("Vulkan: unuploaded region %u - page %d, rect (%d,%d %dx%d)%s; "
								"game wrote %u of %u tile(s) in it, so this is %s.",
								m_sampleMissingRegions, material.texturePage & 1,
								material.texX, material.texY, material.texWidth, material.texHeight,
								overflows ? " WHICH RUNS PAST THE PAGE EDGE" : "",
								written, tiles,
								written == 0 ? "a region the game never filled (faithful, not a bug here)"
									: "a region that was written and did not reach the GPU (a bug here)");
						}
					}

					const uint32_t materialIndex = (uint32_t)m_materialData.size();
					m_materialData.push_back(material);

					DrawItem item;
					item.nodeIndex = ni;
					item.modelIndex = mi;
					item.materialIndex = materialIndex;
					item.vertexOffset = (uint32_t)mesh.vboOffset;
					item.vertexCount = (uint32_t)mesh.vertexCount;
					item.rom = !model.dynamic;
					item.discardAlpha = (layerIndex == 0);

					m_drawBuckets[priority][overlay][layerIndex].push_back(item);
					m_lastDrawCount++;
				}
			}
		}
	}
}

void CVulkan3D::ResetInterpolation()
{
	m_logNextPrepare = true;
	m_scene.ResetInterpolation();
	m_viewportMotion.clear();
	m_modelMotion.clear();
	m_viewportHistory.clear();
	m_instanceHistory.clear();
	m_motionFrame = 0;
	m_motionStats = MotionStats();
	m_totalCuts = 0;
	m_cutsSinceLog = 0;
	m_cutFramesSinceLog = 0;
	m_framesSinceLog = 0;
}

void CVulkan3D::PrepareMotion()
{
	m_motionFrame++;

	m_cameraCutAngleDeg = m_config["CameraCutAngleDegrees"].ValueAsDefault<float>(15.0f);
	m_instanceCutAngleDeg = m_config["InstanceCutAngleDegrees"].ValueAsDefault<float>(30.0f);
	m_cutDistanceScale = m_config["CutDistanceScale"].ValueAsDefault<float>(8.0f);
	m_interpolationScope = m_config["InterpolationScope"].ValueAsDefault<int>(0);
	if (m_interpolationScope < 0 || m_interpolationScope > 2)
		m_interpolationScope = 0;
	m_motionStats = MotionStats();

	auto& nodes = m_scene.Nodes();
	auto& prevNodes = m_scene.PreviousNodes();
	m_viewportMotion.assign(nodes.size(), ViewportMotion());
	m_modelMotion.assign(nodes.size(), std::vector<InstanceMotion>());
	for (size_t i = 0; i < nodes.size(); i++)
		m_modelMotion[i].resize(nodes[i].models.size());

	if (!m_scene.HasPreviousScene())
	{
		for (auto& motion : m_viewportMotion)
			motion.cut = true;
		return;
	}

	// Previous instances, keyed by the path/model/slot signature built during
	// traversal. A duplicate key is marked ambiguous rather than matched to the
	// first one: the wrong matrix is worse than no interpolation.
	struct PrevRef { uint32_t node; uint32_t model; };
	std::unordered_map<uint64_t, PrevRef> prevByKey;
	prevByKey.reserve(prevNodes.size() * 16);
	for (uint32_t pi = 0; pi < (uint32_t)prevNodes.size(); pi++)
	{
		for (uint32_t mi = 0; mi < (uint32_t)prevNodes[pi].models.size(); mi++)
		{
			const uint64_t key = prevNodes[pi].models[mi].instanceKey;
			if (key == 0)
				continue;
			auto inserted = prevByKey.emplace(key, PrevRef{ pi, mi });
			if (!inserted.second)
				inserted.first->second.node = UINT32_MAX;
		}
	}

	/*
	 * The same test for the frame being matched.
	 *
	 * A key that appears twice identifies nothing, and checking only the previous frame leaves half
	 * the problem open: two models drawn from one path with the same address and matrix slot - two
	 * LOD levels pointing at the same model, which is ordinary - both find the single previous entry
	 * and both interpolate from a transform that describes one of them. Counting here costs one pass
	 * over the same models that are about to be walked anyway, and only when the feature is on.
	 */
	std::unordered_map<uint64_t, uint8_t> currKeyCount;
	if (m_instanceInterpolation)
	{
		currKeyCount.reserve(nodes.size() * 8);
		for (const auto& node : nodes)
			for (const auto& model : node.models)
				if (model.instanceKey != 0)
					currKeyCount[model.instanceKey]++;
	}

	/*
	 * Whether the two models are the same geometry, which the instance key alone does not establish.
	 *
	 * The key says the same slot was addressed, not that the same object was there: a game that
	 * rewrites a matrix slot or reuses a culling path for a different model between two frames
	 * produces a matching key for unrelated geometry, and interpolating between their matrices draws
	 * a tile somewhere it never was. Everything that changes the picture - the model data, the texture
	 * window, the node scale - has to agree before the matrices are comparable.
	 */
	auto sameGeometry = [](const New3D::Model& a, const New3D::Model& b) -> bool
	{
		if (a.sourceModelAddr != b.sourceModelAddr || std::fabs(a.scale - b.scale) > 1.0e-6f)
			return false;
		if (a.textureOffsetX != b.textureOffsetX || a.textureOffsetY != b.textureOffsetY || a.page != b.page)
			return false;
		if (!a.meshes || !b.meshes || a.meshes->size() != b.meshes->size())
			return false;
		return true;
	};

	// Per-frame tallies for the diagnostic line. `far` is the one that matters when a picture is
	// wrong: it counts pairs that were rejected because the object appeared somewhere else. The
	// snap reasons are split because "everything snaps" is the symptom of a decomposition that is
	// failing wholesale rather than of objects that really are turning quickly.
	unsigned ambiguousCount = 0, rejectedCount = 0, farCount = 0;
	unsigned snapAngleCount = 0, snapScaleCount = 0, decomposeFailed = 0;
	unsigned turnLt1 = 0, turnLt5 = 0, turnLt15 = 0, turnLt30 = 0, turnGe30 = 0, turnGe90 = 0;
	float maxMove = 0.0f;
	float maxAngle = 0.0f;

	std::unordered_map<uint32_t, const New3D::Viewport*> prevViewport;
	prevViewport.reserve(prevNodes.size());
	for (auto& pn : prevNodes)
	{
		auto inserted = prevViewport.emplace(pn.viewport.sourceAddress, &pn.viewport);
		if (!inserted.second)
			inserted.first->second = nullptr;
	}

	auto sameLayout = [](const New3D::Viewport& a, const New3D::Viewport& b) -> bool
	{
		if (a.matrixBaseAddr != b.matrixBaseAddr || a.priority != b.priority ||
			a.select != b.select || a.number != b.number || a.fullScreen != b.fullScreen)
			return false;
		return std::fabs(a.vpX - b.vpX) < 0.01f && std::fabs(a.vpY - b.vpY) < 0.01f &&
			std::fabs(a.vpWidth - b.vpWidth) < 0.01f && std::fabs(a.vpHeight - b.vpHeight) < 0.01f;
	};

	for (size_t ni = 0; ni < nodes.size(); ni++)
	{
		const New3D::Viewport& vp = nodes[ni].viewport;
		ViewportMotion& motion = m_viewportMotion[ni];
		motion.currBase = TRS();
		DecomposeRigid(vp.baseMatrix, motion.currBase);
		std::memcpy(motion.currProjection, vp.projectionMatrix.currentMatrix, sizeof(motion.currProjection));

		auto pit = prevViewport.find(vp.sourceAddress);
		if (pit == prevViewport.end() || pit->second == nullptr)
		{
			motion.cut = true;
			continue;
		}

		const New3D::Viewport& prevVp = *pit->second;
		std::memcpy(motion.prevProjection, prevVp.projectionMatrix.currentMatrix, sizeof(motion.prevProjection));
		const bool prevBaseOk = DecomposeRigid(prevVp.baseMatrix, motion.prevBase);
		motion.hasPrev = prevBaseOk && motion.currBase.valid;

		float deltaDistance = 0.0f;
		float deltaAngle = 0.0f;
		float deltaScale = 0.0f;
		if (motion.hasPrev)
		{
			deltaDistance = TranslationDelta(motion.prevBase, motion.currBase);
			deltaAngle = RotationDeltaDeg(motion.prevBase, motion.currBase);
			deltaScale = ScaleDelta(motion.prevBase, motion.currBase);
		}

		// The history is per viewport, keyed by the culling node address. A
		// rear-view mirror therefore has its own motion average and its own cut
		// decisions; it can never inherit the main camera's.
		ViewportHistory* history = nullptr;
		for (auto& entry : m_viewportHistory)
		{
			if (entry.sourceAddress == vp.sourceAddress)
			{
				history = &entry;
				break;
			}
		}
		if (history == nullptr)
		{
			m_viewportHistory.push_back(ViewportHistory());
			history = &m_viewportHistory.back();
			history->sourceAddress = vp.sourceAddress;
		}

		bool distanceCut = false;
		if (history->samples > 0 && motion.hasPrev)
		{
			const float threshold = (std::max)(1.0f, m_cutDistanceScale * history->emaDistance);
			distanceCut = deltaDistance > threshold;
		}

		const bool angleCut = motion.hasPrev && deltaAngle > m_cameraCutAngleDeg;
		motion.cut = !motion.hasPrev || !sameLayout(prevVp, vp) || angleCut || distanceCut || deltaScale > 0.25f;

		if (history->samples == 0)
		{
			history->emaDistance = deltaDistance;
			history->emaAngle = deltaAngle;
		}
		else
		{
			// Fast enough to follow a camera, slow enough that one normal frame
			// cannot raise the cut threshold out of reach.
			const float follow = 0.2f;
			history->emaDistance = (1.0f - follow) * history->emaDistance + follow * deltaDistance;
			history->emaAngle = (1.0f - follow) * history->emaAngle + follow * deltaAngle;
		}
		if (history->samples < 1000000u)
			history->samples++;
		history->lastSeenFrame = m_motionFrame;

		/*
		 * Which viewports are UI. Only a viewport with a fixed camera and a small
		 * screen area is treated as one: a rear-view mirror is also small, but its
		 * base matrix moves with the car, so it stays in the interpolated set.
		 * The explicit scopes let a game that does not fit the heuristic be forced
		 * either way from the settings.
		 */
		if (m_interpolationScope == 1)
			motion.hud = false;
		else if (m_interpolationScope == 2)
			motion.hud = !vp.fullScreen;
		else
		{
			const bool small = vp.screenArea < (496 * 384) / 4;
			motion.hud = !vp.fullScreen && small && motion.hasPrev &&
				deltaDistance < 0.01f && deltaAngle < 0.1f;
		}

		if (motion.cut || motion.hud || !motion.hasPrev)
			continue;

		for (size_t mi = 0; mi < nodes[ni].models.size(); mi++)
		{
			const New3D::Model& model = nodes[ni].models[mi];
			InstanceMotion& inst = m_modelMotion[ni][mi];
			if (model.instanceKey == 0)
				continue;

			if (!m_instanceInterpolation)
				continue;

			auto duplicate = currKeyCount.find(model.instanceKey);
			if (duplicate != currKeyCount.end() && duplicate->second > 1)
			{
				ambiguousCount++;
				continue;
			}

			auto found = prevByKey.find(model.instanceKey);
			if (found == prevByKey.end() || found->second.node == UINT32_MAX)
				continue;

			const New3D::Model& prevModel = prevNodes[found->second.node].models[found->second.model];
			if (!sameGeometry(prevModel, model))
			{
				rejectedCount++;
				continue;
			}

			inst.hasPrev = true;
			inst.staticLocal = std::memcmp(prevModel.localMat, model.localMat, sizeof(model.localMat)) == 0;
			std::memcpy(inst.prevLocalMat, prevModel.localMat, sizeof(inst.prevLocalMat));
			std::memcpy(inst.currLocalMat, model.localMat, sizeof(inst.currLocalMat));
			if (inst.staticLocal)
				continue;

			if (!DecomposeRigid(prevModel.localMat, inst.prevLocal) ||
				!DecomposeRigid(model.localMat, inst.currLocal))
			{
				decomposeFailed++;
				inst.hasPrev = false;
				continue;
			}

			const float distance = TranslationDelta(inst.prevLocal, inst.currLocal);
			const float angle = RotationDeltaDeg(inst.prevLocal, inst.currLocal);
			const float scaleDelta = ScaleDelta(inst.prevLocal, inst.currLocal);
			if (distance > maxMove)
				maxMove = distance;
			if (angle > maxAngle)
				maxAngle = angle;
			/*
			 * Buckets rather than a mean: the cut threshold sits at 30 degrees, and the question is
			 * whether the instances being snapped are rotating a little more than that (a threshold
			 * that is merely too tight) or close to 180 (a decomposition sign flip, which would make
			 * the snap itself spurious).
			 */
			if (angle < 1.0f) turnLt1++;
			else if (angle < 5.0f) turnLt5++;
			else if (angle < 15.0f) turnLt15++;
			else if (angle < 30.0f) turnLt30++;
			else { turnGe30++; if (angle >= 90.0f) turnGe90++; }

			/*
			 * An object that moves a little between two frames is being animated; one that appears
			 * somewhere else is a different object. The threshold scales with the instance's own
			 * recent motion, exactly as the camera cut detector does it, because no absolute value
			 * suits a walking pedestrian and a car passing the camera in the same scene.
			 *
			 * A rejected pair is snapped, which draws it at its current matrix - the same picture the
			 * camera-only path produced. That is what makes this a safe default: the worst a bad pair
			 * can do is cost smoothness, never correctness.
			 */
			InstanceHistory& history = m_instanceHistory[model.instanceKey];
			bool distanceCut = false;
			if (history.samples > 0)
			{
				const float threshold = (std::max)(1.0f, m_cutDistanceScale * history.emaDistance);
				distanceCut = distance > threshold;
			}

			if (distanceCut)
			{
				// Deliberately not averaged in: a mismatch that repeats would otherwise raise its
				// own threshold until it passed.
				farCount++;
			}
			else if (history.samples == 0)
			{
				history.emaDistance = distance;
				history.emaAngle = angle;
				history.samples = 1;
			}
			else
			{
				// Fast enough to follow an object, slow enough that one frame cannot make the
				// threshold meaningless.
				const float follow = 0.2f;
				history.emaDistance = (1.0f - follow) * history.emaDistance + follow * distance;
				history.emaAngle = (1.0f - follow) * history.emaAngle + follow * angle;
				if (history.samples < 1000000u)
					history.samples++;
			}
			history.lastSeenFrame = m_motionFrame;

			inst.snap = distanceCut || angle > m_instanceCutAngleDeg || scaleDelta > 0.25f;
			if (!distanceCut)
			{
				if (angle > m_instanceCutAngleDeg)
					snapAngleCount++;
				if (scaleDelta > 0.25f)
					snapScaleCount++;
			}
		}
	}

	// A viewport that has not been seen for ten seconds cannot contribute a
	// meaningful motion average when it returns.
	if (m_motionFrame > 600)
	{
		m_viewportHistory.erase(std::remove_if(m_viewportHistory.begin(), m_viewportHistory.end(),
			[&](const ViewportHistory& h) { return h.lastSeenFrame + 600 < m_motionFrame; }),
			m_viewportHistory.end());

		// Instance history is keyed by a signature that a level change invalidates wholesale, so
		// stale entries would accumulate for the rest of the session without ever being consulted.
		for (auto it = m_instanceHistory.begin(); it != m_instanceHistory.end(); )
		{
			if (it->second.lastSeenFrame + 600 < m_motionFrame)
				it = m_instanceHistory.erase(it);
			else
				++it;
		}
	}

	/*
	 * Per-frame tallies, then one diagnostic line every few seconds.
	 *
	 * The tallies are summed every frame rather than sampled at the log point.
	 * Sampling a single frame every 300 reported "0 cuts" for scenes that cut
	 * regularly, because the sample kept landing between two of them - which is
	 * the one reading this line exists to distinguish. Counting is a handful of
	 * adds over at most a dozen viewports.
	 */
	unsigned cutsNow = 0, hudsNow = 0;
	unsigned models = 0, matched = 0, staticLocal = 0, snapped = 0;
	for (size_t i = 0; i < m_viewportMotion.size(); i++)
	{
		const ViewportMotion& vm = m_viewportMotion[i];
		cutsNow += vm.cut ? 1u : 0u;
		hudsNow += vm.hud ? 1u : 0u;
		for (const auto& im : m_modelMotion[i])
		{
			models++;
			matched += im.hasPrev ? 1u : 0u;
			staticLocal += im.staticLocal ? 1u : 0u;
			snapped += im.snap ? 1u : 0u;
		}
	}
	m_motionStats.viewports = (uint32_t)m_viewportMotion.size();
	m_motionStats.cuts = cutsNow;
	m_motionStats.huds = hudsNow;
	m_motionStats.instances = models;
	m_motionStats.matched = matched;
	m_motionStats.snapped = snapped;
	m_totalCuts += cutsNow;
	m_cutsSinceLog += cutsNow;
	m_framesSinceLog++;
	if (cutsNow > 0)
		m_cutFramesSinceLog++;

	if (m_motionFrame % 300 == 0)
	{
		InfoLog("Interp: viewports %u (%u HUD), instances %u (matched %u, static %u, snap %u "
			"[angle %u scale %u], far %u, rejected %u, ambiguous %u, decompose fail %u, max move %.3f, max turn %.1f, "
			"turn <1 %u <5 %u <15 %u <30 %u >=30 %u (>=90 %u)); "
			"%llu cut(s) in %llu frames (%llu frames with a cut).",
			(unsigned)m_motionStats.viewports, hudsNow, models, matched, staticLocal, snapped,
			snapAngleCount, snapScaleCount, farCount, rejectedCount, ambiguousCount, decomposeFailed, maxMove, maxAngle,
			turnLt1, turnLt5, turnLt15, turnLt30, turnGe30, turnGe90,
			(unsigned long long)m_cutsSinceLog, (unsigned long long)m_framesSinceLog,
			(unsigned long long)m_cutFramesSinceLog);
		m_cutsSinceLog = 0;
		m_cutFramesSinceLog = 0;
		m_framesSinceLog = 0;
	}
}

const float* CVulkan3D::EffectiveModelMatrix(std::size_t nodeIndex, std::size_t modelIndex, float alpha, float out[16])
{
	const auto& nodes = m_scene.Nodes();
	if (nodeIndex >= nodes.size() || modelIndex >= nodes[nodeIndex].models.size())
		return nullptr;

	const New3D::Model& model = nodes[nodeIndex].models[modelIndex];
	if (alpha >= 0.999f || nodeIndex >= m_viewportMotion.size() ||
		modelIndex >= m_modelMotion[nodeIndex].size())
		return model.modelMat;

	const ViewportMotion& vp = m_viewportMotion[nodeIndex];
	const InstanceMotion& inst = m_modelMotion[nodeIndex][modelIndex];
	if (!vp.hasPrev || vp.cut || vp.hud || !vp.prevBase.valid || !vp.currBase.valid)
		return model.modelMat;

	float base[16];
	InterpolateRigid(vp.prevBase, vp.currBase, alpha, base);

	if (inst.hasPrev && !inst.staticLocal && !inst.snap && inst.prevLocal.valid && inst.currLocal.valid)
	{
		float local[16];
		// Linear blend of the instance's own transform, not a slerp. The camera above keeps the
		// quaternion slerp (it is one global matrix, so it cannot open a seam); the instance must be
		// a linear combination so that an edge shared with another model stays shared for every
		// alpha. See LerpMatrix().
		LerpMatrix(inst.prevLocalMat, inst.currLocalMat, (float)alpha, local);
		Mul4x4(base, local, out);
	}
	else
	{
		Mul4x4(base, model.localMat, out);
	}
	return out;
}

const float* CVulkan3D::EffectiveProjection(std::size_t nodeIndex, float alpha, float out[16])
{
	const auto& nodes = m_scene.Nodes();
	if (nodeIndex >= nodes.size())
		return nullptr;

	const New3D::Viewport& vp = nodes[nodeIndex].viewport;
	if (alpha >= 0.999f || nodeIndex >= m_viewportMotion.size())
		return vp.projectionMatrix.currentMatrix;

	const ViewportMotion& motion = m_viewportMotion[nodeIndex];
	if (!motion.hasPrev || motion.cut || motion.hud)
		return vp.projectionMatrix.currentMatrix;

	for (int i = 0; i < 16; i++)
		out[i] = (1.0f - alpha) * motion.prevProjection[i] + alpha * motion.currProjection[i];
	return out;
}

void CVulkan3D::UploadGeometry(VkCommandBuffer cmd, bool newSimulation)
{
	(void)newSimulation;
	if (m_scene.RomReset())
	{
		m_romGpuVerts = 0;
		m_scene.ClearRomReset();
	}
	auto& rom = m_scene.RomVerts();
	if (rom.size() > m_romGpuVerts)
	{
		size_t count = rom.size() - m_romGpuVerts;
		VkDeviceSize bytes = count * sizeof(GpuQuadVertex);
		VkDeviceSize offset = 0;
		void* ptr = nullptr;
		if ((m_romGpuVerts + count) * sizeof(GpuQuadVertex) <= m_rom.size && m_context.Ring().Alloc(bytes, 16, offset, ptr))
		{
			GpuQuadVertex* dst = static_cast<GpuQuadVertex*>(ptr);
			for (size_t i = 0; i < count; i++)
				dst[i] = ConvertVertex(rom[m_romGpuVerts + i]);
			VkBufferCopy copy = {};
			copy.srcOffset = offset;
			copy.dstOffset = m_romGpuVerts * sizeof(GpuQuadVertex);
			copy.size = bytes;
			vkCmdCopyBuffer(cmd, m_context.Ring().Buffer(), m_rom.buffer, 1, &copy);
			VkBufferMemoryBarrier2 barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			// Both stages read geometry now: the vertex shader its own corner, the
			// fragment shader the other three.
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = m_rom.buffer;
			barrier.offset = copy.dstOffset;
			barrier.size = bytes;
			VkDependencyInfo dep = {};
			dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dep.bufferMemoryBarrierCount = 1;
			dep.pBufferMemoryBarriers = &barrier;
			CmdPipelineBarrier2(cmd, &dep);
			m_romGpuVerts += count;
		}
		else
		{
			/*
			 * The ROM vertex buffer could not take the new vertices, so they are dropped and every mesh
			 * that lives beyond them is skipped below. That is silently missing geometry - whole models
			 * vanishing part way through a level - and it is worth a line in the log because the visible
			 * symptom (large black or untextured areas) gives no hint that geometry is the cause.
			 *
			 * The two numbers that matter are how many vertices fit and how many the CPU side was
			 * prepared to hand over; those two limits are what have to agree. This repeats every frame
			 * until the buffer is flushed, so it is throttled to the first occurrence to keep the log
			 * readable while still making the moment visible.
			 */
			if (m_romFullReports++ == 0)
			{
				ErrorLog("Vulkan: ROM vertex buffer is FULL - %zu vertices fit in the %llu MB buffer "
					"(%zu bytes each). %zu new vertex/vertices dropped; every mesh beyond them will not be "
					"drawn, which shows up as missing geometry rather than as a texture fault.",
					m_rom.size / sizeof(GpuQuadVertex), (unsigned long long)(m_rom.size / (1024 * 1024)),
					sizeof(GpuQuadVertex), count);
			}
		}
	}

	// Report the fill level as it climbs. The ceiling is only reachable after a long stretch of play, and
	// the interesting question is not whether it is full but how close to full it normally gets - which
	// is also what decides the right size for the buffer.
	m_romVertsSeen = std::max(m_romVertsSeen, m_romGpuVerts);
	if (++m_romReportFrames % 600 == 0 && m_romVertsSeen > 0)
	{
		InfoLog("Vulkan: ROM geometry - %zu of %zu vertices used so far (%.1f%% of the buffer's capacity).",
			m_romVertsSeen, m_rom.size / sizeof(GpuQuadVertex),
			100.0 * (double)m_romVertsSeen / (double)(m_rom.size / sizeof(GpuQuadVertex)));
	}

	auto& ram = m_scene.RamVerts();
	m_ramReady = false;
	if (!ram.empty() && EnsureDynamicBuffer(ram.size()))
	{
		/*
		 * The dynamic vertices live in their own buffer, not the per-present ring.
		 *
		 * Ring allocations are reset by every BeginFrame(), so an offset that was
		 * valid for the simulation step's first present could be overwritten by
		 * the time the second present of the same step was recorded. Phase 1
		 * presents the same scene repeatedly; Phase 2 presents interpolated
		 * matrices from it. Both need the geometry to outlive the ring slot.
		 *
		 * Alternating between two slots keeps the previous step's geometry alive
		 * while its frames are still in flight.
		 */
		m_dynamicSlot = (m_dynamicSlot + 1) % 2;
		m_ramOffset = (VkDeviceSize)m_dynamicSlot * m_dynamicSlotBytes;
		GpuQuadVertex* dst = reinterpret_cast<GpuQuadVertex*>(
			static_cast<uint8_t*>(m_dynamicVerts.mapped) + m_ramOffset);
		for (size_t i = 0; i < ram.size(); i++)
			dst[i] = ConvertVertex(ram[i]);
		m_ramReady = true;
	}
}

bool CVulkan3D::EnsureDynamicBuffer(size_t vertexCount)
{
	constexpr size_t kInitialVertices = 262144;	// 16 MB per slot at 64 bytes/vertex
	if (vertexCount == 0)
		return false;

	if (m_dynamicVerts.buffer != VK_NULL_HANDLE && vertexCount <= (size_t)(m_dynamicSlotBytes / sizeof(GpuQuadVertex)))
		return true;

	// Replacing a buffer the GPU may still be reading is the one place a wait
	// is unavoidable. It happens when a game first exceeds the initial size and
	// then only as the high-water mark grows, so it is not a per-frame cost.
	if (m_context.Device())
		vkDeviceWaitIdle(m_context.Device());
	m_context.Memory().DestroyBuffer(m_dynamicVerts);

	size_t capacity = kInitialVertices;
	while (capacity < vertexCount)
		capacity *= 2;

	/*
	 * The slot offset is handed to the shader as a dynamic storage-buffer offset,
	 * so each slot has to start on a boundary the device accepts. The capacity is a
	 * power of two of 64-byte records, which already satisfies every alignment the
	 * spec allows, but rounding here makes that independent of the initial size.
	 */
	const VkDeviceSize slotAlign = m_context.StorageAlignment();
	m_dynamicSlotBytes = ((VkDeviceSize)capacity * sizeof(GpuQuadVertex) + slotAlign - 1) & ~(slotAlign - 1);
	const VkDeviceSize total = m_dynamicSlotBytes * 2;
	if (!m_context.Memory().CreateBuffer(total,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			m_dynamicVerts))
	{
		ErrorLog("Vulkan: could not allocate the dynamic vertex buffer (%llu MB for %zu vertices per slot).",
			(unsigned long long)(total / (1024 * 1024)), vertexCount);
		m_dynamicSlotBytes = 0;
		return false;
	}

	// The shaders reach the new buffer through the descriptor set, which the idle
	// wait above has made safe to rewrite.
	m_textures.WriteVertexBuffers(m_rom.buffer, m_dynamicVerts.buffer, m_dynamicSlotBytes);

	InfoLog("Vulkan: dynamic vertex buffer %.1f MB (%zu vertices per slot, two slots).",
		(double)total / (1024.0 * 1024.0), capacity);
	m_dynamicSlot = 1;	// the upload below advances to 0
	return true;
}

void CVulkan3D::FillMaterial(R3DMaterial& material, const New3D::Mesh& mesh, const New3D::Model& model)
{
	material = {};
	// Zero is a valid bindless slot, so "no replacement" has to be stated explicitly.
	material.hdTexIndex = -1;
	material.shininess = mesh.shininess;
	material.specularValue = mesh.specularValue;
	material.fogIntensity = mesh.fogIntensity;
	material.microMinLOD = mesh.microTextureMinLOD;
	material.texX = mesh.x + model.textureOffsetX;
	material.texY = mesh.y + model.textureOffsetY;
	material.texWidth = mesh.width;
	material.texHeight = mesh.height;
	material.texFormat = mesh.format;
	material.texturePage = (mesh.page ^ model.page) & 1;
	material.wrapModeX = (int32_t)mesh.wrapModeU;
	material.wrapModeY = (int32_t)mesh.wrapModeV;
	material.microTextureID = mesh.microTextureID & 7;
	if (mesh.textured)
		material.flags |= kMatTextured;
	if (mesh.alphaTest)
		material.flags |= kMatAlphaTest;
	if (mesh.textureAlpha)
		material.flags |= kMatTextureAlpha;
	if (mesh.lighting)
		material.flags |= kMatLighting;
	if (mesh.fixedShading)
		material.flags |= kMatFixedShade;
	if (mesh.specular)
		material.flags |= kMatSpecular;
	if (mesh.translatorMap)
		material.flags |= kMatTranslator;
	if (mesh.inverted)
		material.flags |= kMatInverted;
	if (mesh.smoothShading)
		material.flags |= kMatSmooth;
	if (mesh.polyAlpha)
		material.flags |= kMatPolyAlpha;
	if (mesh.microTexture)
		material.flags |= kMatMicro;
	if (mesh.layered)
		material.flags |= kMatLayered;
}

void CVulkan3D::FillPush(R3DPush& push, const float modelView[16], const float projection[16], const New3D::Model& model, const New3D::Viewport& vp, uint32_t materialIndex, int layer) const
{
	std::memcpy(push.modelView, modelView, sizeof(push.modelView));
	std::memcpy(push.projection, projection, sizeof(push.projection));
	push.projection[1] = -push.projection[1];
	push.projection[5] = -push.projection[5];
	push.projection[9] = -push.projection[9];
	push.projection[13] = -push.projection[13];
	push.lightDir[0] = vp.lightingParams[0];
	push.lightDir[1] = vp.lightingParams[1];
	push.lightDir[2] = vp.lightingParams[2];
	push.lightDir[3] = vp.lightingParams[3];
	push.lightParams[0] = vp.lightingParams[4];
	push.lightParams[1] = m_sunClamp ? 1.0f : 0.0f;
	push.lightParams[2] = vp.intensityClamp ? 1.0f : 0.0f;
	push.lightParams[3] = (float)vp.hardwareStep;

	push.fogParams[3] = model.alpha;
	push.fogColour[0] = vp.fogColour[0];
	push.fogColour[1] = vp.fogColour[1];
	push.fogColour[2] = vp.fogColour[2];
	push.fogColour[3] = vp.fogDensity;
	push.fogParams[0] = vp.fogStart;
	push.fogParams[1] = vp.fogAttenuation;
	push.fogParams[2] = vp.fogAmbient;
	push.extra[0] = vp.cota;
	push.extra[1] = model.scale == 0.0f ? 1.0f : model.scale;
	push.extra[2] = (float)layer;
	push.extra[3] = UintToFloatBits(materialIndex);
}

void CVulkan3D::CopyLos(VkCommandBuffer cmd, int priority)
{
	const New3D::Viewport* vp = nullptr;
	for (const auto& node : m_scene.Nodes())
	{
		if (node.viewport.priority == priority && (node.viewport.losPosX || node.viewport.losPosY))
		{
			vp = &node.viewport;
			break;
		}
	}
	if (!vp || !m_los.mapped)
		return;
	int inY = 384 - vp->losPosY;
	// Scene coordinates: the game's 496x384 position scaled into whatever rectangle the scene is
	// being drawn at. With low-resolution rendering that is the fixed internal target, so the
	// window offset must not be added - the target starts at 0,0.
	int glX = (int)m_sceneXOff + (int)(vp->losPosX * (m_sceneXRes / 496.0f));
	int glY = (int)m_sceneYOff + (int)(inY * (m_sceneYRes / 384.0f));
	int vkY = (int)m_color.height - 1 - glY;
	if (glX < 0 || vkY < 0 || glX >= (int)m_color.width || vkY >= (int)m_color.height)
		return;

	const uint32_t index = m_context.FrameIndex() * 4 + (uint32_t)priority;

	if (m_samples > VK_SAMPLE_COUNT_1_BIT)
	{
		/*
		 * A multisampled depth image cannot be copied to a buffer: Vulkan only allows the copy from a
		 * single-sampled image, and D32_SFLOAT has no resolve. The same texel is therefore read by a
		 * one-invocation compute dispatch, which takes the nearest of the samples and writes it where
		 * the readback below expects it. That keeps GetLosValue() correct at 2x, 4x and 8x instead of
		 * freezing it at its last value for the whole session.
		 *
		 * max() rather than the average: the depth buffer is cleared to zero and tested with GREATER,
		 * so the largest sample is the nearest surface. Averaging a partially covered pixel would
		 * return a depth between the surface and the clear value, which is not a depth the hardware
		 * ever produced.
		 */
		if (!m_losPipeline || !m_losSet)
			return;

		// Stated once, so a log from a user who reports a LOS problem shows whether the
		// multisampled path was actually the one running.
		if (!m_losComputeLogged)
		{
			m_losComputeLogged = true;
			InfoLog("Vulkan: line-of-sight readback via compute pass (%ux MSAA).", m_sampleBits);
		}

		CmdTransition(cmd, m_depth.image, m_depthLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
		m_depthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_losPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_losPipeLayout, 0, 1, &m_losSet, 0, nullptr);
		struct { int32_t pos[2]; uint32_t index; uint32_t samples; } push = { { glX, vkY }, index, m_sampleBits };
		vkCmdPushConstants(cmd, m_losPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
		vkCmdDispatch(cmd, 1, 1, 1);

		// Make the shader's write visible to the host read in BeginFrame(). The frame fence alone
		// orders execution, not the shader-write-to-host-read dependency.
		VkMemoryBarrier2 barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
		VkDependencyInfo dep = {};
		dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.memoryBarrierCount = 1;
		dep.pMemoryBarriers = &barrier;
		CmdPipelineBarrier2(cmd, &dep);

		// The next pass renders into the depth attachment again; put it back explicitly so the
		// tracked layout stays accurate even if that pass is skipped.
		CmdTransition(cmd, m_depth.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
		m_depthLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

		m_losReady[m_context.FrameIndex()] = true;
		return;
	}

	CmdTransition(cmd, m_depth.image, m_depthLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
	m_depthLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	VkBufferImageCopy copy = {};
	copy.bufferOffset = index * sizeof(float);
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageOffset = { glX, vkY, 0 };
	copy.imageExtent = { 1, 1, 1 };
	vkCmdCopyImageToBuffer(cmd, m_depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_los.buffer, 1, &copy);
	CmdTransition(cmd, m_depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
	m_depthLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

	// The copy lands after this frame's fence. Convert 1/w depth the same way New3D does
	// once the buffer is visible; until then the previous value stands. The bit below is
	// applied when the copy is read back, using a zero depth as "no geometry".
	m_losReady[m_context.FrameIndex()] = true;
	(void)kNearPlane;
}

void CVulkan3D::DrawBucket(VkCommandBuffer command, int priority, int overlay, int layer, double alpha, bool oitCapture, bool insideScenePass)
{
	auto& items = m_drawBuckets[priority][overlay][layer];
	if (items.empty())
		return;

	// Bucket 0 is the colour layer, 1 holds trans2, 2 holds trans1 (see the New3D::Layer mapping
	// where the buckets are built). The attachment index is what the pipeline writes and what the
	// fragment shader uses to pick its output.
	const int attachment = (layer == 0) ? 0 : (layer == 1 ? 2 : 1);

	int currentNode = -1;
	int boundVariant = -1;

	// The projection only changes when the viewport does, and interpolating it is
	// a sixteen-element lerp that used to be repeated for every draw in the
	// bucket - a few thousand per frame in the busiest scenes.
	float interpProjection[16];
	const float* projection = nullptr;

	for (const DrawItem& item : items)
	{
		auto& node = m_scene.Nodes()[item.nodeIndex];
		auto& model = node.models[item.modelIndex];

		if (item.rom && (size_t)(item.vertexOffset + item.vertexCount) > m_romGpuVerts)
		{
			m_skipRomBeyond++;
			continue;
		}
		if (!item.rom && !m_ramReady)
		{
			m_skipRamNotReady++;
			continue;
		}

		if (currentNode != (int)item.nodeIndex)
		{
			auto& vp = node.viewport;
			int vx = vp.x;
			int vy = (int)m_color.height - (vp.y + vp.height);
			int vw = vp.width;
			int vh = vp.height;
			if (vw <= 0 || vh <= 0)
				continue;
			if (vx < 0) { vw += vx; vx = 0; }
			if (vy < 0) { vh += vy; vy = 0; }
			if (vx + vw > (int)m_color.width)
				vw = (int)m_color.width - vx;
			if (vy + vh > (int)m_color.height)
				vh = (int)m_color.height - vy;
			if (vw <= 0 || vh <= 0)
			{
				m_skipDegenerateNode++;
				continue;
			}

			VkDeviceSize uboOffset = 0;
			void* ptr = nullptr;
			if (!m_context.Ring().Alloc(sizeof(ViewportUBO), m_context.UniformAlignment(), uboOffset, ptr))
				continue;
			ViewportUBO ubo = {};
			ubo.spotEllipse[0] = vp.spotEllipse[0];
			ubo.spotEllipse[1] = (float)m_color.height - vp.spotEllipse[1];
			ubo.spotEllipse[2] = vp.spotEllipse[2];
			ubo.spotEllipse[3] = vp.spotEllipse[3];
			ubo.spotRange[0] = vp.spotRange[0];
			ubo.spotRange[1] = vp.spotRange[1];
			ubo.spotColor[0] = vp.spotColor[0];
			ubo.spotColor[1] = vp.spotColor[1];
			ubo.spotColor[2] = vp.spotColor[2];
			ubo.spotFogColor[0] = vp.spotFogColor[0];
			ubo.spotFogColor[1] = vp.spotFogColor[1];
			ubo.spotFogColor[2] = vp.spotFogColor[2];
			ubo.texFilter[0] = (float)m_texFilterMode;
			ubo.texFilter[1] = 4.0f;	// taps for the anisotropic mode
			ubo.renderOptions[0] = m_quadInterpolation ? 1.0f : 0.0f;
			ubo.renderOptions[1] = (m_alphaSortMode == 2) ? 1.0f : 0.0f;
			std::memcpy(ptr, &ubo, sizeof(ubo));
			uint32_t offsets[3] = {
				(uint32_t)uboOffset,
				(uint32_t)(m_context.FrameIndex() * m_materialSlice),
				(uint32_t)m_ramOffset
			};
			VkDescriptorSet set = m_textures.Set();
			vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &set, 3, offsets);
			// Set 2 is the HD replacement array; set 1 (sorted transparency) is bound by
			// DrawOitCapture() before it calls this, so binding only these two keeps that binding
			// intact for the capture pipelines.
			VkDescriptorSet hdSet = m_textures.HDSet();
			vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 2, 1, &hdSet, 0, nullptr);
			VkViewport view = {};
			view.x = (float)vx;
			view.y = (float)vy;
			view.width = (float)vw;
			view.height = (float)vh;
			view.maxDepth = 1;
			VkRect2D scissor = { { vx, vy }, { (uint32_t)vw, (uint32_t)vh } };
			vkCmdSetViewport(command, 0, 1, &view);
			vkCmdSetScissor(command, 0, 1, &scissor);
			currentNode = (int)item.nodeIndex;
			projection = EffectiveProjection(item.nodeIndex, (float)alpha, interpProjection);
		}

		if (projection == nullptr)
			continue;

		float interpModel[16];
		const float* modelView = EffectiveModelMatrix(item.nodeIndex, item.modelIndex, (float)alpha, interpModel);
		if (modelView == nullptr)
			continue;

		// The vertex source selects a pipeline variant rather than a bound vertex
		// buffer: ROM and polygon-RAM geometry are different storage buffers, and the
		// shader's choice between them is compiled in. The dynamic offset that picks
		// the RAM slot is part of the set binding above, so it is the same for both
		// variants and stays valid whichever one is bound.
		const int variant = item.rom ? 0 : 1;
		if (variant != boundVariant)
		{
			VkPipeline pipeline = oitCapture
				? m_oitPipelines[overlay ? 1 : 0][variant]
				: (insideScenePass
					? m_scenePassPipelines[attachment][overlay ? 1 : 0][variant]
					: m_scenePipelines[attachment][overlay ? 1 : 0][variant]);
			vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			boundVariant = variant;
		}

		R3DPush push;
		FillPush(push, modelView, projection, model, node.viewport, item.materialIndex, attachment);
		vkCmdPushConstants(command, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

		/*
		 * Every primitive is four vertices and the index buffer holds the fixed two-triangle
		 * pattern repeated, so the draw is six indices per primitive and the base vertex picks the
		 * primitive. A mesh larger than one batch is drawn in several slices of the same block; the
		 * base vertex stays a multiple of four, which is what lets the shaders recover the quad from
		 * gl_VertexIndex.
		 */
		uint32_t remaining = item.vertexCount / 4;
		uint32_t quadBase = item.vertexOffset;
		while (remaining != 0)
		{
			const uint32_t batch = std::min(remaining, kQuadsPerIndexBatch);
			vkCmdDrawIndexed(command, 6 * batch, 1, 0, (int32_t)quadBase, 0);
			quadBase += batch * 4;
			remaining -= batch;
		}
	}
}

void CVulkan3D::DrawOitCapture(VkCommandBuffer command, int priority, int overlay, double alpha)
{
	// Set 1 is the sorted-transparency A-buffer: the node array and the per-pixel counts.
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 1, 1, &m_oitSet, 0, nullptr);

	// Both layers go into the same slots. Their depth decides the order, with the layer only
	// breaking exact ties (see real3d_oit.frag); the old "dominant layer first" ordering existed
	// to work around the shared depth buffer, which this pass does not write.
	DrawBucket(command, priority, overlay, 1, alpha, true);
	DrawBucket(command, priority, overlay, 2, alpha, true);
}

void CVulkan3D::ResolveOit(VkCommandBuffer command)
{
	// The resolve is a full-screen draw: the viewport and scissor still hold the last node's
	// rectangle from DrawBucket, so they have to be replaced.
	VkViewport view = {};
	view.width = (float)m_color.width;
	view.height = (float)m_color.height;
	view.minDepth = 0.0f;
	view.maxDepth = 1.0f;
	VkRect2D scissor = { { 0, 0 }, { m_color.width, m_color.height } };
	vkCmdSetViewport(command, 0, 1, &view);
	vkCmdSetScissor(command, 0, 1, &scissor);
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_oitResolve);
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_oitResolveLayout, 0, 1, &m_oitSet, 0, nullptr);
	vkCmdDraw(command, 3, 1, 0, 0);
}

void CVulkan3D::DrawScene(VkCommandBuffer cmd, double alpha, bool newSimulation)
{
	VkCommandBuffer command = cmd;
	m_lastAlpha = (float)alpha;
	// Cleared every frame; DrawScenePass sets it when it ran, so the 2D compositor knows the
	// transparency layers are already folded into the scene target.
	m_scenePrecomposited = false;

	/*
	 * Upload the material table into this present's slice.
	 *
	 * The slice belongs to the frame slot whose fence BeginFrame() has just
	 * waited on, so nothing is still reading it. Doing this per present rather
	 * than once per simulation frame is what keeps an in-flight frame from
	 * sampling a table that was rewritten underneath it; the data itself does not
	 * change between sub-frames, so the cost is one copy of a few kilobytes.
	 */
	if (!m_materialData.empty() && m_materials.mapped != nullptr)
	{
		std::memcpy(static_cast<uint8_t*>(m_materials.mapped) +
			m_context.FrameIndex() * m_materialSlice,
			m_materialData.data(), m_materialData.size() * sizeof(R3DMaterial));
	}
	auto toColorAttachment = [&](AllocatedImage& image, VkImageLayout& layout)
	{
		if (layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			CmdTransition(command, image.image, layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
	};
	/*
	 * Order a pass's clear (or first write) against earlier writes to the same attachment.
	 *
	 * A same-layout barrier, so it is not a layout transition and changes nothing about the image:
	 * it exists because loadOp = CLEAR writes the attachment again, and nothing else orders that
	 * write when the previous one came from a different submission. The subpass render pass hands
	 * attachments 1 and 2 back in COLOR_ATTACHMENT_OPTIMAL (their finalLayout), so the next frame's
	 * dynamic pass finds the right layout and records no transition - which leaves the previous
	 * submission's write unordered against this one. That is a WRITE_AFTER_WRITE hazard, and sync
	 * validation reports it for exactly the two transparency targets. Synchronization scopes reach
	 * back into earlier submissions on the same queue, so this barrier is the complete fix.
	 */
	auto attachmentWriteDependency = [&](VkImage image)
	{
		if (image == VK_NULL_HANDLE)
			return;
		VkImageMemoryBarrier2 barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VkDependencyInfo dep = {};
		dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &barrier;
		CmdPipelineBarrier2(command, &dep);
	};
	/*
	 * Opens a scene pass with all three targets bound, exactly as New3D's colour FBO holds them.
	 *
	 * Every scene pass binds all three; the pipeline's colour write mask decides which one a layer
	 * actually writes (see CreatePipeline). That is what lets the colour layer zero the transparency
	 * targets where opaque geometry covers them, which is how a later priority layer erases the
	 * transparency left over from an earlier one.
	 *
	 * resolveMask names the attachments this pass writes and therefore has to resolve. An
	 * attachment's resolve is a full-surface copy from the multisampled image, so resolving one this
	 * pass did not write reads 133 MB of 4K 4x MSAA and writes it back unchanged. More importantly,
	 * resolving is the only way a multisampled attachment reaches the 1x image the compositor
	 * samples, so a written attachment that is NOT resolved keeps the previous pass's 1x contents -
	 * with no error and no log line. That was the state of the transparency targets: the capture of
	 * the transparency pass was never resolved, so every game whose translucency lives in the last
	 * priority it draws (Daytona 2's attract scene: one priority, one transparent layer) lost that
	 * layer entirely under MSAA, while VF3 looked right only because a later priority's colour pass
	 * happened to resolve it.
	 */
	auto begin = [&](bool clearScene, bool clearDepth, bool clearTrans, unsigned resolveMask)
	{
		AllocatedImage* targets[3] = { &m_color, &m_trans1, &m_trans2 };
		AllocatedImage* resolves[3] = { &m_colorResolve, &m_trans1Resolve, &m_trans2Resolve };
		VkImageLayout* layouts[3] = { &m_colorLayout, &m_trans1Layout, &m_trans2Layout };
		VkImageLayout* resolveLayouts[3] = { &m_resolveLayout, &m_trans1ResolveLayout, &m_trans2ResolveLayout };
		VkRenderingAttachmentInfo colors[3] = {};
		for (int i = 0; i < 3; i++)
		{
			toColorAttachment(*targets[i], *layouts[i]);
			VkRenderingAttachmentInfo& color = colors[i];
			color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			color.imageView = targets[i]->view;
			color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			color.loadOp = ((i == 0) ? clearScene : clearTrans) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			// The transparency targets clear to zero: a stale value would be composited over this frame.
			color.clearValue.color.float32[0] = 0;
			color.clearValue.color.float32[1] = 0;
			color.clearValue.color.float32[2] = 0;
			color.clearValue.color.float32[3] = 0;
			// The clear below is a write, so it must be ordered against any earlier write to the same
			// attachment - including one made by the previous frame's command buffer.
			if (color.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
				attachmentWriteDependency(targets[i]->image);
			// Resolve the multisampled scene into the single-sampled image the compositor samples.
			// resolveMode AVERAGE is the standard box filter.
			if (m_samples > VK_SAMPLE_COUNT_1_BIT && (resolveMask & (1u << i)) != 0)
			{
				toColorAttachment(*resolves[i], *resolveLayouts[i]);
				color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
				color.resolveImageView = resolves[i]->view;
				color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
		}
		if (m_depthLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
		{
			CmdTransition(command, m_depth.image, m_depthLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
			m_depthLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		}
		VkRenderingAttachmentInfo depth = {};
		depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth.imageView = m_depth.view;
		depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth.loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth.clearValue.depthStencil.depth = 0;
		VkRenderingInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		info.renderArea.extent = { m_color.width, m_color.height };
		info.layerCount = 1;
		info.colorAttachmentCount = 3;
		info.pColorAttachments = colors;
		info.pDepthAttachment = &depth;
		CmdBeginRendering(command, &info);
	};
	// Closes the scene pass and, when multisampling, prepares the resolved image for the 2D layer to
	// sample. Every path that ends the scene rendering must go through here.
	auto finishScene = [&]()
	{
		CmdEndRendering(command);
		if (m_samples > VK_SAMPLE_COUNT_1_BIT && m_resolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			CmdTransition(command, m_colorResolve.image, m_resolveLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			m_resolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		if (m_samples > VK_SAMPLE_COUNT_1_BIT && m_trans1ResolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			CmdTransition(command, m_trans1Resolve.image, m_trans1ResolveLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			m_trans1ResolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		if (m_samples > VK_SAMPLE_COUNT_1_BIT && m_trans2ResolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			CmdTransition(command, m_trans2Resolve.image, m_trans2ResolveLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			m_trans2ResolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
	};
	/*
	 * Hand the images the 2D compositor samples over in the layout its descriptors declare.
	 *
	 * This has to run on every path out of DrawScene, not only the one that falls off the end of
	 * the priority loop: the compositor samples the scene from the previous frame (TileGen draws
	 * before the 3D renderer), so an image left in COLOR_ATTACHMENT by any single frame makes the
	 * *next* frame's composite draw use a mismatched layout. The white-flash path used to return
	 * straight after finishScene() and did exactly that, which is what produced the intermittent
	 * VUID-vkCmdDraw-imageLayout-00344 on uLayers[4]/[5].
	 *
	 * Under MSAA the compositor's slots name the resolved images; without it they name the
	 * multisampled targets themselves, which are also the scan-out-free 1x attachments.
	 */
	auto handOverTargets = [&]()
	{
		if (m_samples > VK_SAMPLE_COUNT_1_BIT)
		{
			if (m_resolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				CmdTransition(command, m_colorResolve.image, m_resolveLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
				m_resolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			if (m_trans1ResolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				CmdTransition(command, m_trans1Resolve.image, m_trans1ResolveLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
				m_trans1ResolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			if (m_trans2ResolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				CmdTransition(command, m_trans2Resolve.image, m_trans2ResolveLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
				m_trans2ResolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		}
		else
		{
			if (m_colorLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				CmdTransition(command, m_color.image, m_colorLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
				m_colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			if (m_trans1Layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				CmdTransition(command, m_trans1.image, m_trans1Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
				m_trans1Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			if (m_trans2Layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				CmdTransition(command, m_trans2.image, m_trans2Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
				m_trans2Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		}
	};
	auto clearDepth = [&]()
	{
		VkClearAttachment att = {};
		att.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		att.clearValue.depthStencil.depth = 0;
		VkClearRect rect = {};
		rect.rect.extent = { m_color.width, m_color.height };
		rect.layerCount = 1;
		vkCmdClearAttachments(command, 1, &att, 1, &rect);
	};
	/*
	 * The capture pass binds the depth buffer and nothing else.
	 *
	 * The depth load is what gives the capture its early-Z culling against the opaque geometry of
	 * this priority. It is not stored back: nothing in the pass writes depth (the capture pipeline
	 * disables depth writes so that every translucent fragment reaches the list), and the resolve
	 * does not read it, so writing 133 MB of 4K 4x MSAA depth back would be pure cost.
	 */
	auto beginCapturePass = [&]()
	{
		/*
		 * Clear the per-pixel counts for this pass.
		 *
		 * The previous pass's resolve read the counts, so the barrier first orders those reads
		 * before the clear. The count image is r32ui and stays in GENERAL for its whole life; the
		 * clear is a transfer operation and must happen outside any render pass, which is why it is
		 * here rather than in the resolve.
		 */
		if (m_oitCountLayout != VK_IMAGE_LAYOUT_GENERAL)
		{
			VkImageMemoryBarrier2 barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			barrier.srcAccessMask = 0;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barrier.oldLayout = m_oitCountLayout;
			barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.image = m_oitCount.image;
			barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			VkDependencyInfo dep = {};
			dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dep.imageMemoryBarrierCount = 1;
			dep.pImageMemoryBarriers = &barrier;
			CmdPipelineBarrier2(command, &dep);
			m_oitCountLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		OitMemoryBarrier(command, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

		VkClearColorValue zero = {};
		VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdClearColorImage(command, m_oitCount.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);

		OitMemoryBarrier(command, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

		if (m_depthLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
		{
			CmdTransition(command, m_depth.image, m_depthLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
			m_depthLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		}
		VkRenderingAttachmentInfo depth = {};
		depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth.imageView = m_depth.view;
		depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
		VkRenderingInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		info.renderArea.extent = { m_color.width, m_color.height };
		info.layerCount = 1;
		info.pDepthAttachment = &depth;
		CmdBeginRendering(command, &info);
	};
	/*
	 * The resolve pass binds the colour target and nothing else: no depth (the capture tested it),
	 * and neither transparency target (unused in this mode). Keeping the pass to the one attachment
	 * it writes is what keeps a 4K 4x MSAA resolve from loading and storing three unused surfaces.
	 */
	auto beginResolvePass = [&]()
	{
		toColorAttachment(m_color, m_colorLayout);
		VkRenderingAttachmentInfo color = {};
		color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color.imageView = m_color.view;
		color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		if (m_samples > VK_SAMPLE_COUNT_1_BIT)
		{
			toColorAttachment(m_colorResolve, m_resolveLayout);
			color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			color.resolveImageView = m_colorResolve.view;
			color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		VkRenderingInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		info.renderArea.extent = { m_color.width, m_color.height };
		info.layerCount = 1;
		info.colorAttachmentCount = 1;
		info.pColorAttachments = &color;
		CmdBeginRendering(command, &info);
	};

	bool white = m_blockCulling && !m_noWhiteFlash;
	/*
	 * Sorted transparency is a per-frame decision, not a per-session one: the resources can be
	 * missing (allocation failed on this resize) even though the mode was selected. m_oitReady is
	 * the answer for this set of targets, and every OIT path below is gated on it.
	 */
	const bool oit = (m_alphaSortMode == 1) && m_oitReady && m_oitResolve != VK_NULL_HANDLE;

	/*
	 * The default transparency mode draws the whole scene in the two-subpass render pass when it is
	 * available (see CreateScenePass and DrawScenePass). Three cases keep the dynamic-rendering path:
	 *
	 *   - sorted transparency (AlphaSortMode=1) needs a barrier between capture and resolve, and a
	 *     barrier may not be recorded inside a render pass;
	 *   - the block-culling white flash emits no 3D geometry at all;
	 *   - line-of-sight readback is a compute dispatch per priority, which cannot run inside the pass
	 *     either. Only the light-gun games set a LOS position, so this costs them the optimisation and
	 *     nobody else anything.
	 */
	const bool scenePass = m_scenePassReady && !oit && !white && !(newSimulation && SceneNeedsLos());
	if (scenePass)
	{
		DrawScenePass(command, alpha);
		return;
	}

	// One pass holds all three targets from here on, matching New3D's colour FBO. Clearing all three
	// once per frame is the only colour clear the scene gets: every later pass loads, because the
	// colour layer's zero writes are what erase transparency left over from earlier priorities.
	begin(true, true, true, 0x7u);
	if (white)
	{
		// Flash the screen white, as the arcade hardware does while it is block culling. Only the
		// game's own 4:3 viewport is blanked: the 3D target is the size of the swapchain and the
		// surrounding pillarbox area belongs to the 2D layer, so clearing the whole target would
		// turn those bars white for as long as the flash lasts (most visible on the startup text
		// screen, which is drawn before the first game frame).
		VkClearAttachment att = {};
		att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		att.colorAttachment = 0;
		att.clearValue.color.float32[0] = att.clearValue.color.float32[1] = att.clearValue.color.float32[2] = att.clearValue.color.float32[3] = 1;
		VkClearRect rect = {};
		// The scene rectangle, not the visible picture: with low-resolution rendering the target is
		// the picture and the flash covers all of it, while without it the target is the swapchain and
		// the surrounding pillarbox belongs to the 2D layer - clearing the whole target would turn the
		// bars white for as long as the flash lasts.
		// m_sceneYOff is measured from the bottom, as in the OpenGL convention the renderers use.
		const int flashX = (int)m_sceneXOff;
		const int flashY = (int)m_color.height - (int)(m_sceneYOff + m_sceneYRes);
		rect.rect.offset = { flashX, flashY };
		rect.rect.extent = { m_sceneXRes, m_sceneYRes };
		// Fall back to the whole target if the viewport is degenerate or out of bounds.
		if (m_sceneXRes == 0 || m_sceneYRes == 0 || m_sceneXRes > m_color.width || m_sceneYRes > m_color.height ||
			flashX < 0 || flashX + (int)m_sceneXRes > (int)m_color.width || flashY < 0)
			rect.rect = { { 0, 0 }, { m_color.width, m_color.height } };
		rect.layerCount = 1;
		vkCmdClearAttachments(command, 1, &att, 1, &rect);
		// The transparency targets were cleared by begin() above, so the compositor will not combine
		// the previous frame's transparency with this frame's white flash.
		finishScene();
		// Same hand-over as the normal path below. Leaving it out here was the bug: the composite
		// draw of the *next* frame samples these images, and they were still in the layout the
		// dynamic-rendering pass left them in (COLOR_ATTACHMENT_OPTIMAL).
		handOverTargets();
		return;
	}

	// Seed the background with the fog colour before any geometry, so that the
	// horizon and empty areas match New3D instead of staying black.
	DrawFog(command);

	// One index buffer for the whole scene: every draw is the same six indices over
	// a different four-vertex primitive. Bound once because index buffer state is
	// command buffer state and none of the scene pipelines change it.
	vkCmdBindIndexBuffer(command, m_quadIndices.buffer, 0, VK_INDEX_TYPE_UINT16);

	bool inRenderPass = true;

	auto hasItems = [&](int priority, int overlay) -> bool
	{
		for (int layerIndex = 0; layerIndex < 3; layerIndex++)
			if (!m_drawBuckets[priority][overlay][layerIndex].empty())
				return true;
		return false;
	};

	for (int priority = 0; priority <= 3; priority++)
	{
		if (!hasItems(priority, 0) && !hasItems(priority, 1))
			continue;

		const bool hasOverlay = hasItems(priority, 1);
		for (int pass = 0; pass < 2; pass++)
		{
			const bool renderOverlay = (pass == 1);
			if (renderOverlay && !hasOverlay)
				break;

			if (!inRenderPass)
			{
				// The colour layer writes the scene and zeroes both transparency targets, so this pass
				// resolves all three.
				begin(false, false, false, 0x7u);
				inRenderPass = true;
			}
			clearDepth();
			// The colour layer writes the scene colour and zeroes both transparency targets where it
			// covers them. That zeroing is what removes a translucent surface an opaque polygon of a
			// later priority layer sits in front of; without it the transparency survives the depth
			// clear and is composited over the opaque geometry at the end of the frame (VF3's sea
			// covering the fighters).
			DrawBucket(command, priority, renderOverlay ? 1 : 0, 0, alpha);
			CmdEndRendering(command);
			inRenderPass = false;

			if (!renderOverlay && newSimulation)
				CopyLos(command, priority);

			const int overlayIndex = renderOverlay ? 1 : 0;
			const bool hasTransparency = !m_drawBuckets[priority][overlayIndex][1].empty() ||
				!m_drawBuckets[priority][overlayIndex][2].empty();

			if (oit)
			{
				/*
				 * Sorted transparency, resolved inside this priority.
				 *
				 * The resolve has to happen here rather than once at the end of the frame: the next
				 * priority clears the depth buffer and draws opaque geometry that must cover this
				 * one's translucency unconditionally. Compositing now bakes the current priority's
				 * result into the colour target, so the next opaque pass simply draws over it - which
				 * is exactly the hardware's ordering and needs no "opaque erases OIT nodes" rule.
				 */
				if (hasTransparency)
				{
					beginCapturePass();
					inRenderPass = true;
					DrawOitCapture(command, priority, overlayIndex, alpha);
					CmdEndRendering(command);
					inRenderPass = false;

					/*
					 * A pipeline barrier may not be recorded inside a dynamic rendering instance, so the
					 * capture and the resolve are two passes with the barrier between them. It orders the
					 * capture's node stores and count-image atomics before the resolve's reads.
					 */
					OitMemoryBarrier(command, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
						VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
						VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
						VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

					beginResolvePass();
					inRenderPass = true;
					ResolveOit(command);
					CmdEndRendering(command);
					inRenderPass = false;
				}
			}
			else
			{
				// Draw the dominant transparency layer first. Both layers share one depth buffer here
				// (the MSAA depth cannot be copied back between them the way New3D's
				// StoreDepth/RestoreDepth does), so the layer that is mostly opaque must not be
				// occluded by the one that is fading out. At equal depth GEQUAL lets the second layer
				// through as well.
				//
				// Both transparency layers write only their own attachment; the pipeline masks decide
				// where they land, so they can share a single pass with the colour layer. This pass
				// writes the two transparency attachments and nothing else, so it resolves exactly
				// those two.
				begin(false, false, false, 0x6u);
				inRenderPass = true;
				DrawBucket(command, priority, overlayIndex, 1, alpha);
				DrawBucket(command, priority, overlayIndex, 2, alpha);
				CmdEndRendering(command);
				inRenderPass = false;
			}

			if (!hasOverlay)
				break;
		}
	}

	if (inRenderPass)
		finishScene();

	// Hand the resolved scene to the compositor even when no render pass is left open.
	//
	// This has to happen unconditionally. Every pass in the loop above ends with CmdEndRendering and
	// clears inRenderPass, so the loop normally finishes with no pass open and finishScene() was
	// therefore never reached - which left the resolve target in COLOR_ATTACHMENT_OPTIMAL while the
	// compositor's descriptor declared it SHADER_READ_ONLY_OPTIMAL. Sampling with a mismatched layout
	// is undefined behaviour and the scene came out black, which is exactly what MSAA users saw.
	//
	// The same reasoning applies to the non-MSAA path, where the compositor samples m_color directly,
	// so both images are handed over here.
	handOverTargets();
}

bool CVulkan3D::SceneNeedsLos() const
{
	// CopyLos() runs per priority and needs the depth buffer as it stands after that priority's
	// colour layer, so it forces the multi-pass path. Only the light-gun games set these.
	for (const auto& node : m_scene.Nodes())
		if (node.viewport.losPosX || node.viewport.losPosY)
			return true;
	return false;
}

/*
 * The default transparency mode's whole 3D scene in one two-subpass render pass.
 *
 * Subpass 0 is what the dynamic-rendering path records as one pass per priority: the fog quad, then
 * every priority (colour layer, then both transparency layers), with the depth cleared between
 * priorities. The difference is that the render pass is never ended in between, so the three colour
 * attachments are never stored between priorities - and with VK_DEPENDENCY_BY_REGION on the subpass
 * dependency, a tile-based GPU keeps them in tile memory across the two subpasses entirely.
 *
 * Subpass 1 is the full-screen composite (scene_composite.frag). It reads the two transparency
 * targets as input attachments, folds them into the colour target with the hardware blender, and
 * discards untouched pixels; the resolve attachment then downsamples once. Nothing here is stored
 * to the two transparency images or resolved into a separate pair of 1x images, which is the
 * bandwidth this path saves over the dynamic-rendering one.
 *
 * The depth clear between priorities uses vkCmdClearAttachments rather than a new render pass
 * because a clear is legal inside a pass and ending it would store every attachment.
 */
void CVulkan3D::DrawScenePass(VkCommandBuffer command, double alpha)
{
	// The render pass's declared initial layouts. Usually already correct (the previous frame ended
	// in them) but not after a resize or a frame that took the dynamic-rendering path.
	if (m_colorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		CmdTransition(command, m_color.image, m_colorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		m_colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	if (m_samples > VK_SAMPLE_COUNT_1_BIT && m_resolveLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		CmdTransition(command, m_colorResolve.image, m_resolveLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		m_resolveLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	for (int i = 0; i < 2; i++)
	{
		AllocatedImage& trans = (i == 0) ? m_trans1 : m_trans2;
		VkImageLayout& layout = (i == 0) ? m_trans1Layout : m_trans2Layout;
		if (layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			CmdTransition(command, trans.image, layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
	}
	if (m_depthLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
	{
		CmdTransition(command, m_depth.image, m_depthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
		m_depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	// The transparency targets and the depth buffer clear to zero; the colour target to transparent
	// black, exactly as the dynamic-rendering path's first pass does.
	VkClearValue clears[5] = {};
	clears[3].depthStencil.depth = 0.0f;

	VkRenderPassBeginInfo passInfo = {};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	passInfo.renderPass = m_scenePass;
	passInfo.framebuffer = m_sceneFramebuffer;
	passInfo.renderArea.extent = { m_color.width, m_color.height };
	passInfo.clearValueCount = (m_samples > VK_SAMPLE_COUNT_1_BIT) ? 5u : 4u;
	passInfo.pClearValues = clears;
	vkCmdBeginRenderPass(command, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

	DrawFog(command, true);
	vkCmdBindIndexBuffer(command, m_quadIndices.buffer, 0, VK_INDEX_TYPE_UINT16);

	auto hasItems = [&](int priority, int overlay) -> bool
	{
		for (int layerIndex = 0; layerIndex < 3; layerIndex++)
			if (!m_drawBuckets[priority][overlay][layerIndex].empty())
				return true;
		return false;
	};
	auto clearDepth = [&]()
	{
		VkClearAttachment att = {};
		att.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		att.clearValue.depthStencil.depth = 0;
		VkClearRect rect = {};
		rect.rect.extent = { m_color.width, m_color.height };
		rect.layerCount = 1;
		vkCmdClearAttachments(command, 1, &att, 1, &rect);
	};

	for (int priority = 0; priority <= 3; priority++)
	{
		if (!hasItems(priority, 0) && !hasItems(priority, 1))
			continue;

		const bool hasOverlay = hasItems(priority, 1);
		for (int pass = 0; pass < 2; pass++)
		{
			const bool renderOverlay = (pass == 1);
			if (renderOverlay && !hasOverlay)
				break;

			clearDepth();
			const int overlayIndex = renderOverlay ? 1 : 0;
			// Same order as the dynamic-rendering path: the colour layer (which also zeroes the two
			// transparency targets where it covers them), then trans2 into attachment 2, then trans1
			// into attachment 1 - DrawBucket maps the layer number to the attachment.
			DrawBucket(command, priority, overlayIndex, 0, alpha, false, true);
			DrawBucket(command, priority, overlayIndex, 1, alpha, false, true);
			DrawBucket(command, priority, overlayIndex, 2, alpha, false, true);

			if (!hasOverlay)
				break;
		}
	}

	vkCmdNextSubpass(command, VK_SUBPASS_CONTENTS_INLINE);
	VkViewport view = {};
	view.width = (float)m_color.width;
	view.height = (float)m_color.height;
	view.minDepth = 0.0f;
	view.maxDepth = 1.0f;
	VkRect2D scissor = { { 0, 0 }, { m_color.width, m_color.height } };
	vkCmdSetViewport(command, 0, 1, &view);
	vkCmdSetScissor(command, 0, 1, &scissor);
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneComposite);
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneCompositeLayout, 0, 1, &m_sceneInputSet, 0, nullptr);
	vkCmdDraw(command, 3, 1, 0, 0);
	vkCmdEndRenderPass(command);

	// What the render pass's finalLayouts left behind.
	m_depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	m_trans1Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	m_trans2Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	// The 2D compositor's descriptor set still names the transparency targets (or, under MSAA, their
	// resolved images) even though the shader skips its two-layer block for this frame - the branch
	// is a uniform push constant, so the bindings stay statically used and validation requires them
	// to be in the layout the descriptors declare. Their contents are undefined here, which is fine:
	// nothing reads them.
	if (m_samples > VK_SAMPLE_COUNT_1_BIT)
	{
		m_colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		m_resolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		if (m_trans1ResolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			CmdTransition(command, m_trans1Resolve.image, m_trans1ResolveLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			m_trans1ResolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		if (m_trans2ResolveLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			CmdTransition(command, m_trans2Resolve.image, m_trans2ResolveLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			m_trans2ResolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
	}
	else
	{
		m_colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		CmdTransition(command, m_trans1.image, m_trans1Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		m_trans1Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		CmdTransition(command, m_trans2.image, m_trans2Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		m_trans2Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
	m_scenePrecomposited = true;
}

void CVulkan3D::PrepareFrame(void)
{
	/*
	 * Everything that depends on the simulation state, done once per emulated
	 * frame and with no command buffer involved:
	 *
	 *   - traverse the Real3D scene database into the current snapshot;
	 *   - classify the viewports and build the draw list (Phase 2/4);
	 *   - keep the texture cache's frame serial current.
	 *
	 * On the interpolated presents that follow, RenderFrame() reuses all of it and
	 * only updates matrices.
	 */
	m_scene.BeginFrame();
	// Re-read every frame so the in-game overlay can toggle it live. It only changes the horizontal
	// frustum, which CalcViewport derives per frame from this flag.
	m_wide = m_config["WideScreen"].ValueAs<bool>() && m_internalExtent.width == 0;
	RefreshSceneViewport();
	if (!(m_blockCulling && !m_noWhiteFlash))
		m_scene.Build();
	for (auto& node : m_scene.Nodes())
		node.viewport.sunClamp = m_sunClamp;
	PrepareMotion();
	BuildDrawList();
	CollectTextures();

	if (m_logNextPrepare)
	{
		m_logNextPrepare = false;
		InfoLog("Vulkan: first prepare after state reset: %zu node(s), %u draw(s), current=%d, blockCulling=%d, noWhiteFlash=%d.",
			m_scene.Nodes().size(), m_lastDrawCount, m_scene.HasCurrentScene() ? 1 : 0,
			m_blockCulling ? 1 : 0, m_noWhiteFlash ? 1 : 0);
	}
}

void CVulkan3D::RenderFrame(void)
{
	RenderFrame(1.0, true);
}

void CVulkan3D::RenderFrame(double alpha, bool newSimulation)
{
	if (!m_context.IsRecording())
		return;
	if (!EnsureTargets())
		return;
	VkCommandBuffer cmd = m_context.Cmd();

	if (newSimulation)
	{
		m_textures.RecordSync(cmd, m_context.Ring(), m_frameSerial);
		UploadGeometry(cmd, true);
	}

	DrawScene(cmd, alpha, newSimulation);
	m_drew = true;

	/*
	 * Report any skip counter that changed since the last report.
	 *
	 * Logged on change rather than per frame: a skip that persists is one event, not thousands, and a
	 * skip that begins after a resize is the thing worth seeing. These three are the paths that drop
	 * geometry - or an entire node - with no other trace at all, so without this a renderer that has
	 * lost half the scene reads exactly like one that has not.
	 */
	if (m_skipRomBeyond != m_skipLoggedRom || m_skipRamNotReady != m_skipLoggedRam ||
		m_skipDegenerateNode != m_skipLoggedNode)
	{
		InfoLog("Vulkan: draw skips - %llu mesh(es) past the uploaded ROM geometry, %llu dynamic "
			"mesh(es) with no ring allocation, %llu node(s) with a collapsed viewport "
			"(ROM verts %zu, RAM verts %zu, RAM ready %d).",
			(unsigned long long)m_skipRomBeyond, (unsigned long long)m_skipRamNotReady,
			(unsigned long long)m_skipDegenerateNode, m_romGpuVerts, m_scene.RamVerts().size(),
			m_ramReady ? 1 : 0);
		m_skipLoggedRom = m_skipRomBeyond;
		m_skipLoggedRam = m_skipRamNotReady;
		m_skipLoggedNode = m_skipDegenerateNode;
	}
}

} // Vulkan3D
