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
#ifndef INCLUDED_VULKAN3D_COMMON_H
#define INCLUDED_VULKAN3D_COMMON_H

/*
 * Shared Vulkan helpers and the Real3D vertex / push-constant layouts used by
 * CVulkan3D. Push constants are 208 bytes, matching the std140 block below.
 */

#define VK_NO_PROTOTYPES
#include "volk/volk.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Vulkan3D {

constexpr uint32_t kMaxFramesInFlight = 2;
constexpr uint32_t kMaxTextureSlots = 4096;
constexpr uint32_t kMaxMaterials = 16384;
constexpr VkDeviceSize kRingSliceBytes = 64ull * 1024ull * 1024ull;

/*
 * How many ROM (static, VROM-backed) vertices the geometry cache may accumulate before it is flushed
 * and rebuilt.
 *
 * This number has to be visible to both the cache that fills it and the code that sizes the GPU buffer
 * that receives it. They were separate before - the cache used a local 1500000 while the buffer was
 * allocated as a flat 64 MB - and the two disagreed by a third, so part way through a level the buffer
 * filled and every mesh beyond it silently stopped being drawn. Whole models disappeared and the
 * symptom read as a texture fault, which is why it took so long to find.
 *
 * Defined once, here, so the pair cannot drift apart again.
 *
 * Sized deliberately larger than a single track needs rather than merely larger than the old, broken
 * figure. Reaching the limit costs a flush, and a flush drops the accumulated ROM models for one frame
 * - a visible pop if it lands mid-race. Measured accumulation on Scud Race is roughly 6500 vertices a
 * second, so a five-minute track builds about two million; six million leaves comfortable headroom for
 * a long session without a flush ever occurring during normal play.
 *
 * The cost is real and linear: 64 bytes per vertex on the GPU and 60 in the CPU-side cache, so six
 * million holds 384 MB of device memory and about 360 MB of system memory once filled. On a modern card
 * that is a small fraction of what is available. On a card with 2 GB it is worth knowing about, which is
 * why the allocation logs its size and why a failure to allocate is reported rather than silently
 * falling back.
 *
 * A vertex here is a quad corner, not a triangle corner: each primitive occupies four slots (a triangle
 * repeats its last vertex), so the same six million holds one and a half million quads - half again as
 * many primitives as the old six-vertex per quad triangle list did in the same space.
 */
constexpr uint32_t kMaxRomVerts = 6000000;

enum MaterialFlag : uint32_t
{
	kMatTextured		= 1u << 0,
	kMatAlphaTest		= 1u << 1,
	kMatTextureAlpha	= 1u << 2,
	kMatLighting		= 1u << 3,
	kMatFixedShade		= 1u << 4,
	kMatSpecular		= 1u << 5,
	kMatTranslator		= 1u << 6,
	kMatInverted		= 1u << 7,
	kMatSmooth			= 1u << 8,
	kMatPolyAlpha		= 1u << 9,
	kMatMicro			= 1u << 10,
	// LAY=1. Only the sorted-transparency path consumes this today: it makes one
	// layered fragment per pixel win, which is what the Real3D stencil did. The
	// two-layer path ignores it, exactly as before.
	kMatLayered			= 1u << 11,
};

// std430, 64 bytes. Keep in sync with real3d.frag.
//
// This mirrors the uniform set New3D feeds its fragment shader. The texture is
// addressed exactly like the real hardware: baseTexInfo is a rectangle inside
// one of the two packed 2048x1024 texture pages (which also contain the packed
// mip pyramid), and the shader does texelFetch maths on it.
struct R3DMaterial
{
	uint32_t flags;
	int32_t  texX;             // baseTexInfo.x (page pixels, includes translator offset)
	int32_t  texY;             // baseTexInfo.y
	int32_t  texWidth;         // baseTexInfo.z (texels)
	int32_t  texHeight;        // baseTexInfo.w (texels)
	int32_t  texFormat;        // 0..11, see ExtractColour
	int32_t  texturePage;      // 0 or 1
	int32_t  wrapModeX;        // Mesh::TexWrapMode
	int32_t  wrapModeY;        // Mesh::TexWrapMode
	int32_t  microTextureID;   // 0..7
	float    microMinLOD;
	float    shininess;
	float    specularValue;
	float    fogIntensity;
	// Bindless index into the shader's u_hdTextures[] array, or -1 when this material keeps sampling
	// the original sub-texture out of the packed Real3D page. The slot is assigned by the HD texture
	// pack; the shader takes the fast path (textureGrad) whenever it is non-negative.
	int32_t  hdTexIndex;
	float    pad1;
};

/*
 * One corner of a Real3D primitive, as the shaders see it.
 *
 * Real3D rasterises quads, not triangles: four vertices carry the polygon and the
 * hardware interpolates across them quadratically. To reproduce that without a
 * geometry shader, four consecutive slots describe one primitive (a triangle
 * repeats its last vertex in slot 3) and both shaders pull what they need out of
 * this array instead of receiving vertex attributes:
 *
 *   - the vertex shader fetches the single corner it owns, for gl_Position;
 *   - the fragment shader fetches all four and solves for the quad's (s, t).
 *
 * The layout is four 16-byte groups so that it is identical under std430 and
 * under the C rules, with no vec3 straddling a 16-byte boundary. That is why the
 * position and normal are stored as float[4] rather than float[3]: a GLSL `vec3`
 * member would be padded to 16 bytes and every offset after it would move.
 *
 * The polygon normal lives in the last group. It is stored once per corner (the
 * four copies are identical) because the vertices are uploaded as a flat array
 * with a uniform stride; the winding test and the texture LOD both need it.
 * `colour` occupies the same group's w slot and is read back with
 * floatBitsToUint(), the same trick the push constants use for the material
 * index. Keeping it there rather than adding a fifth group is what holds the
 * struct at 64 bytes - it is the element size of a buffer sized at six million
 * vertices.
 */
struct GpuQuadVertex
{
	float pos[4];			// xyz = model-space position, w unused (1.0)
	float normal[4];		// xyz = vertex normal, w = textureNP
	float uv[4];			// xy = texture coordinates, z = fixed shade, w unused
	float faceNormal[3];	// polygon normal
	uint32_t colour;		// unorm8x4
};

// std140 push constants, 208 bytes.
// extra.x = cota, extra.y = modelScale, extra.z = discardAlpha (0 or 1),
// extra.w = material index bitcast to float.
struct R3DPush
{
	float modelView[16];
	float projection[16];
	float lightDir[4];
	float lightParams[4];
	float fogColour[4];
	float fogParams[4];
	float extra[4];
};

// std140 viewport uniform (spotlight). Dynamic offset into the ring buffer.
struct ViewportUBO
{
	float spotEllipse[4];
	float spotRange[4];
	float spotColor[4];
	float spotFogColor[4];
	/*
	 * Texture filtering for the 3D scene, carried here because this is the block the
	 * scene shader already reads per node and it is per-session state.
	 *
	 *   x = mode: 0 = radial hardware LOD (default, what the hardware did),
	 *             1 = screen-space derivative LOD,
	 *             2 = derivative LOD with anisotropic taps.
	 *   y = maximum number of taps for mode 2.
	 */
	float texFilter[4];
	/*
	 * Scene shader options.
	 *
	 *   x = quad interpolation: 1 = the exact four-corner solve (default, what
	 *       this backend has always produced), 0 = hardware triangle interpolation.
	 *       The fast path skips four storage-buffer reads and the per-pixel
	 *       quadratic solve, which is what the ultra-low preset is for. It shows
	 *       the rasteriser's diagonal on non-planar quads, so it is opt-in.
	 *   y = arcade checkerboard transparency (AlphaSortMode = 2): 1 = each
	 *       transparency layer writes only its half of the checkerboard, 0 = both
	 *       layers write every pixel. Carried here rather than as a specialization
	 *       constant so the mode needs no second set of twelve scene pipelines;
	 *       it is per-session state, which is exactly what this block holds.
	 *   zw unused.
	 */
	float renderOptions[4];
};

// std140 push constants for the ambient / scroll fog background pass. 64 bytes.
struct R3DFogPush
{
	float fogColour[4];			// rgb = fog colour, a = scroll fog amount
	float fogAmbientAtt[4];		// x = fog ambient, y = fog attenuation, zw unused
	float spotEllipse[4];		// spotlight ellipse: xy = centre, zw = size
	float spotFogColor[3];		// spotlight on fog colour
	float pad;
};

static_assert(sizeof(R3DMaterial) == 64, "R3DMaterial size");
static_assert(sizeof(GpuQuadVertex) == 64, "GpuQuadVertex size");
static_assert(offsetof(GpuQuadVertex, normal) == 16, "GpuQuadVertex std430 layout");
static_assert(offsetof(GpuQuadVertex, uv) == 32, "GpuQuadVertex std430 layout");
static_assert(offsetof(GpuQuadVertex, faceNormal) == 48, "GpuQuadVertex std430 layout");
static_assert(offsetof(GpuQuadVertex, colour) == 60, "GpuQuadVertex std430 layout");
static_assert(sizeof(R3DPush) == 208, "R3DPush size");
static_assert(sizeof(ViewportUBO) == 96, "ViewportUBO size");
static_assert(sizeof(R3DFogPush) == 64, "R3DFogPush size");

/*
 * The four entry points that differ between Vulkan 1.2 and 1.3, routed so that one build works on
 * either.
 *
 * dynamic rendering and synchronization2 became core in 1.3, but both exist as VK_KHR_ extensions
 * that are widely supported on 1.2 devices - which is most of the hardware that predates the 1.3
 * requirement and no longer needs to be shut out. Each wrapper prefers the core name and falls back
 * to the extension one, so a 1.3 device calls exactly what it did before this existed and a 1.2 device
 * calls the extension.
 *
 * The branch is the same one that used to be here, and its cost is nothing: roughly fifteen tests a
 * frame against a million-plus vertices.
 *
 * What went wrong the first time is worth recording, because restoring the branch without fixing it
 * would repeat it. The path could not work as written: the shaders were compiled to SPIR-V 1.6, which
 * needs Vulkan 1.3 or VK_KHR_maintenance4, and maintenance4 was never requested - so any device that
 * reached the 1.2 path failed later, at shader-module creation, with a message pointing at the shader
 * rather than at the real problem. The shaders are now built for SPIR-V 1.5 (see
 * Scripts/CompileVulkanShaders.cmd), which both 1.2 and 1.3 accept, so the 1.2 path has nothing left
 * to trip over.
 *
 * VK_KHR_push_descriptor remains optional and is still checked at use: without it the texture decode
 * runs on the CPU instead of the GPU, which is a degradation rather than a failure.
 */

inline void CmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo* info)
{
	if (vkCmdBeginRendering)
		vkCmdBeginRendering(cmd, info);
	else if (vkCmdBeginRenderingKHR)
		vkCmdBeginRenderingKHR(cmd, info);
}

inline void CmdEndRendering(VkCommandBuffer cmd)
{
	if (vkCmdEndRendering)
		vkCmdEndRendering(cmd);
	else if (vkCmdEndRenderingKHR)
		vkCmdEndRenderingKHR(cmd);
}

inline void CmdPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* dep)
{
	if (vkCmdPipelineBarrier2)
		vkCmdPipelineBarrier2(cmd, dep);
	else if (vkCmdPipelineBarrier2KHR)
		vkCmdPipelineBarrier2KHR(cmd, dep);
}

inline void QueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits, VkFence fence)
{
	if (vkQueueSubmit2)
		vkQueueSubmit2(queue, count, submits, fence);
	else if (vkQueueSubmit2KHR)
		vkQueueSubmit2KHR(queue, count, submits, fence);
}

/*
 * True when at least one of the two names for each of the three families above is present.
 *
 * Checked once, at device creation, so that a device where neither is loaded is rejected there with a
 * clear reason rather than drawing nothing later on. Every wrapper above silently does nothing when
 * both pointers are null, which is the right behaviour for a call that must not crash but the wrong
 * thing to leave undetected.
 */
inline bool Rendering2EntryPointsAvailable()
{
	return (vkCmdBeginRendering != nullptr || vkCmdBeginRenderingKHR != nullptr) &&
		(vkCmdEndRendering != nullptr || vkCmdEndRenderingKHR != nullptr) &&
		(vkCmdPipelineBarrier2 != nullptr || vkCmdPipelineBarrier2KHR != nullptr) &&
		(vkQueueSubmit2 != nullptr || vkQueueSubmit2KHR != nullptr);
}

inline void CmdTransition(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect, uint32_t baseMip = 0, uint32_t mipCount = 1, VkPipelineStageFlags2 srcStageOverride = 0)
{
	if (oldLayout == newLayout || image == VK_NULL_HANDLE)
		return;

	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.baseMipLevel = baseMip;
	barrier.subresourceRange.levelCount = mipCount;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	switch (oldLayout)
	{
	case VK_IMAGE_LAYOUT_UNDEFINED:
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = 0;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_GENERAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		break;
	default:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		break;
	}

	/*
	 * A swapchain image is acquired by a semaphore that this backend waits on at
	 * COLOR_ATTACHMENT_OUTPUT. A transition recorded straight after vkAcquireNextImageKHR with the
	 * usual PRESENT_SRC/UNDEFINED -> TOP_OF_PIPE source stage runs before that wait, which is a real
	 * WRITE_AFTER_READ hazard (synchronization validation reports it on every frame). Passing the
	 * wait's stage here creates the missing execution dependency. Callers only do this on the first
	 * transition of an acquired image; every later transition keeps the derived stage.
	 */
	if (srcStageOverride != 0)
	{
		barrier.srcStageMask = srcStageOverride;
		barrier.srcAccessMask = 0;
	}

	switch (newLayout)
	{
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_GENERAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		barrier.dstAccessMask = 0;
		break;
	default:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		break;
	}

	VkDependencyInfo dep = {};
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;
	CmdPipelineBarrier2(cmd, &dep);
}

inline float UintToFloatBits(uint32_t u)
{
	float f;
	std::memcpy(&f, &u, sizeof(f));
	return f;
}

} // Vulkan3D

#endif
