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
#ifndef INCLUDED_VULKAN3D_H
#define INCLUDED_VULKAN3D_H

#include "Graphics/IRender3D.h"
#include "MotionInterpolation.h"
#include "R3DSceneBuilder.h"
#include "VulkanTextureCache.h"
#include "Util/NewConfig.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vulkan3D {

class CVulkan2D;

class CVulkan3D final : public IRender3D
{
public:
	CVulkan3D(const Util::Config::Node& config, const std::string& gameName, CVulkanContext& context, CVulkan2D& output);
	~CVulkan3D() override;

	void PrepareFrame(void) override;
	void ResetInterpolation(void) override;
	void RenderFrame(void) override;
	void RenderFrame(double alpha, bool newSimulation) override;
	void BeginFrame(void) override;
	void EndFrame(void) override;
	void UploadTextures(unsigned level, unsigned x, unsigned y, unsigned width, unsigned height) override;
	void AttachMemory(const uint32_t* cullingRAMLoPtr, const uint32_t* cullingRAMHiPtr, const uint32_t* polyRAMPtr, const uint32_t* vromPtr, const uint16_t* textureRAMPtr) override;
	void SetStepping(int stepping) override;
	Result Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned aaTarget) override;
	void SetSunClamp(bool enable) override;
	void SetBlockCulling(bool enable) override;
	float GetLosValue(int layer) override;

	/*
	 * The MSAA level in force, which is not always the one asked for: a driver that cannot provide
	 * the sample count for both the colour and depth formats has it reduced to 1 during Init, and the
	 * settings window would still be showing the request. Read out for the statistics display, so that
	 * "the setting says 4x but nothing is being antialiased" is something a user can see.
	 */
	uint32_t SampleCount() const { return m_sampleBits; }
	uint32_t LastDrawCount() const { return m_lastDrawCount; }

	/*
	 * The size the 3D scene is rasterised at, or a zero-sized extent when it follows the picture.
	 *
	 * The statistics readout needs it: the picture's size is the output in the low-resolution mode,
	 * not the amount of work being done. Zero means "whatever the visible picture is", which is what
	 * the readout reports as the render size in that case.
	 */
	VkExtent2D RenderExtent() const { return m_internalExtent; }

	/*
	 * What the last snapshot had to say about motion, for the PACE line and the
	 * statistics readout. `cuts` is the number of viewports that snapped this
	 * frame because of a camera cut or a layout change; `matched` is how many
	 * instances found their counterpart in the previous snapshot. A matched count
	 * near zero means the instance keys are wrong; a cut count at the viewport
	 * count every frame means the thresholds are too tight.
	 */
	struct MotionStats
	{
		uint32_t viewports = 0;
		uint32_t cuts = 0;
		uint32_t huds = 0;
		uint32_t instances = 0;
		uint32_t matched = 0;
		uint32_t snapped = 0;
	};
	const MotionStats& LastMotionStats() const { return m_motionStats; }
	uint64_t TotalCuts() const { return m_totalCuts; }
	float CurrentAlpha() const { return m_lastAlpha; }

private:
	bool CreatePipeline(int layer, bool overlay, bool ramVertices, bool oitCapture = false, VkRenderPass renderPass = VK_NULL_HANDLE);
	bool CreateScenePass();
	bool CreateSceneCompositePipeline();
	bool CreateSceneInputDescriptors();
	bool CreateFogPipeline();
	bool CreateFogPassPipeline();
	bool CreateFogPipelineFor(VkRenderPass renderPass, VkPipeline* dest);
	bool CreateLosPipeline();
	bool CreateOitPipelines();
	bool CreateOitResolvePipeline();
	bool CreateOitSetLayout();
	bool CreateOitDescriptors();
	bool EnsureOitTargets();
	void DestroyOitTargets();
	void OitMemoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
		VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) const;
	bool CreateQuadIndexBuffer();
	void WriteLosDescriptor();
	bool EnsureTargets();
	VkExtent2D SceneTargetExtent() const;
	void RefreshSceneViewport();
	void DestroyTargets();
	GpuQuadVertex ConvertVertex(const New3D::FVertex& src) const;
	void CollectTextures();
	void BuildDrawList();
	void DrawBucket(VkCommandBuffer cmd, int priority, int overlay, int layer, double alpha, bool oitCapture = false, bool insideScenePass = false);
	void PrepareMotion();
	const float* EffectiveModelMatrix(std::size_t nodeIndex, std::size_t modelIndex, float alpha, float out[16]);
	const float* EffectiveProjection(std::size_t nodeIndex, float alpha, float out[16]);
	void UploadGeometry(VkCommandBuffer cmd, bool newSimulation);
	bool EnsureDynamicBuffer(size_t vertexCount);
	void DrawScene(VkCommandBuffer cmd, double alpha, bool newSimulation);
	void DrawScenePass(VkCommandBuffer cmd, double alpha);
	bool SceneNeedsLos() const;
	void DrawOitCapture(VkCommandBuffer cmd, int priority, int overlay, double alpha);
	void ResolveOit(VkCommandBuffer cmd);
	void DrawFog(VkCommandBuffer cmd, bool insideScenePass = false);
	void CopyLos(VkCommandBuffer cmd, int priority);
	void FillPush(R3DPush& push, const float modelView[16], const float projection[16], const New3D::Model& model, const New3D::Viewport& vp, uint32_t materialIndex, int layer) const;
	void FillMaterial(R3DMaterial& material, const New3D::Mesh& mesh, const New3D::Model& model);

	const Util::Config::Node& m_config;
	std::string m_gameName;
	CVulkanContext& m_context;
	CVulkan2D& m_output;
	CR3DSceneBuilder m_scene;
	CVulkanTextureCache m_textures;

	unsigned m_xOff = 0, m_yOff = 0, m_xRes = 496, m_yRes = 384, m_totalX = 496, m_totalY = 384;
	bool m_wide = false;

	/*
	 * Low-resolution rendering.
	 *
	 * A zero-sized extent means off: the scene targets are the size of the swapchain and the 3D
	 * viewport sits inside them at m_xOff/m_yOff/m_xRes/m_yRes. A non-zero extent replaces every
	 * scene target (colour, depth, the two transparency layers, their resolves and the sorted-
	 * transparency A-buffer) with that size, and moves the whole scene coordinate system to it: offset
	 * 0,0 and a viewport of the full target.
	 *
	 * Fixed for the session. It decides the size of the render targets and therefore cannot change
	 * under a recording command buffer; like MSAA it is read once, in Init.
	 */
	VkExtent2D m_internalExtent = {};
	/*
	 * The rectangle the scene is drawn into, in scene-target coordinates (OpenGL convention: y from
	 * the bottom). Equal to the visible picture, and offset into a swapchain-sized target, when
	 * low-resolution rendering is off; a full-target rectangle at the origin when it is on. Every
	 * place that used m_xOff/m_yOff/m_xRes/m_yRes for scene coordinates reads these instead, so the
	 * two cases cannot drift apart.
	 */
	unsigned m_sceneXOff = 0, m_sceneYOff = 0, m_sceneXRes = 496, m_sceneYRes = 384;
	unsigned m_sceneTotalX = 496, m_sceneTotalY = 384;
	bool m_noWhiteFlash = false;
	// One-shot diagnostic for the first scene built after a state load/reset. Set by
	// ResetInterpolation(), consumed by PrepareFrame().
	bool m_logNextPrepare = false;
	/*
	 * How the scene shader chooses its texture mip level.
	 *
	 * 0 is New3D's radial estimate, which is what the Real3D hardware does and what
	 * this backend has always produced: the polygon's texture-NP factor divided by
	 * w squared, isotropic across the polygon. 1 and 2 replace it with the level the
	 * pixel's own uv footprint implies (2 also takes extra samples along a stretched
	 * footprint). The alternatives are sharper on surfaces seen at a grazing angle
	 * but are not what the hardware did, so they are opt-in.
	 *
	 * Read once, in Init: these are baked into the scene shader's uniform block and a
	 * change takes effect on the next session, like MSAA.
	 */
	int m_texFilterMode = 0;
	/*
	 * Whether the scene shader reconstructs each quad from its four corners.
	 *
	 * True is the exact path and has always been the picture this backend draws.
	 * False is the fast path: the fragment stage consumes the vertex stage's
	 * hardware-interpolated attributes instead of pulling the four corner records
	 * and solving for the quad's (s, t). It trades a diagonal crease on non-planar
	 * quads for roughly forty ALU instructions and four 64-byte reads per pixel -
	 * the ultra-low preset turns it off for integrated GPUs.
	 *
	 * Read once, in Init, like TexFilter: the value reaches the shader through the
	 * per-node uniform block, so a change takes effect on the next session.
	 */
	bool m_quadInterpolation = true;
	// Last HD texture switch states, so a transition is logged once instead of every frame.
	bool m_hdLoadActive = false;
	bool m_hdDumpActive = false;
	std::atomic<bool> m_sunClamp;
	std::atomic<bool> m_blockCulling;

	/*
	 * Transparency ordering.
	 *
	 * 0 keeps the original two-layer scheme: New3D's trans1 / trans2 attachments, drawn unblended and
	 * combined by the compositor with the hardware's two-layer formula. It is bit-for-bit what this
	 * backend did before and stays the default.
	 *
	 * 1 replaces both layers with a fixed-slot per-pixel A-buffer. Every translucent fragment that
	 * passes the depth test against the current priority's opaque geometry is stored in a slot, and
	 * a full-screen resolve sorts the slots back to front and composites them into the colour target
	 * before the next priority begins. Resolving per priority - rather than once at the end of the
	 * frame - is what keeps Real3D's rule that a later priority's opaque geometry covers an earlier
	 * priority's transparency: the earlier transparency has already been composited into the colour
	 * target by the time the later pass clears depth and draws.
	 *
	 * 2 reproduces the board itself. Each transparency layer writes only its half of a checkerboard
	 * (trans1 the even cells, trans2 the odd ones), so the other half keeps the opaque scene and a
	 * dense effect resolves at half alpha instead of turning solid where two sprites land on
	 * different layers. It cannot use the subpass composite - the reconstruction reads the 2x2
	 * neighbourhood, which subpassLoad() cannot address - so it takes the dynamic-rendering path.
	 * See composite.frag for the resolve.
	 *
	 * Read once, in the constructor, like MSAA: the capture pipelines and the resolve are built around
	 * it and a change takes effect on the next session.
	 */
	int m_alphaSortMode = 0;
	// SM3D_CHECKER_RAW: skip the 2x2 reconstruction of mode 2 and blend the pixel's own cell.
	bool m_checkerRaw = false;

	VkPipelineLayout m_layout = VK_NULL_HANDLE;
	// One pipeline per scene target (0 = colour, 1 = trans1, 2 = trans2), decal
	// variant and vertex source (0 = ROM geometry, 1 = polygon-RAM geometry).
	//
	// The two vertex sources are separate storage buffers, and which one a shader
	// reads is a specialization constant rather than a runtime branch: a branch
	// would leave both bindings live and let the compiler issue a speculative load
	// from the one that is not bound for this draw.
	VkPipeline m_scenePipelines[3][2][2] = {};
	// The same twelve pipelines, created for m_scenePass subpass 0 instead of dynamic rendering.
	// A pipeline built for VK_KHR_dynamic_rendering has a null render pass and cannot be bound
	// inside a render pass instance, so the two paths need their own sets. They share the shader
	// modules and the pipeline layout; only the pipeline object differs.
	VkPipeline m_scenePassPipelines[3][2][2] = {};
	VkShaderModule m_vert = VK_NULL_HANDLE;
	VkShaderModule m_frag = VK_NULL_HANDLE;

	/*
	 * The two-subpass render pass that draws the whole 3D scene of the default transparency mode
	 * and composites Real3D's two transparency layers on chip.
	 *
	 * Subpass 0 is the scene exactly as the dynamic-rendering path records it: one pass, the fog
	 * quad, every priority (depth cleared between them with vkCmdClearAttachments), colour layer
	 * then both transparency layers. Subpass 1 is a full-screen triangle that reads the two
	 * transparency targets as input attachments and blends them into the colour target - see
	 * scene_composite.frag. Neither transparency target is ever stored, resolved or sampled.
	 */
	VkRenderPass m_scenePass = VK_NULL_HANDLE;
	VkFramebuffer m_sceneFramebuffer = VK_NULL_HANDLE;
	VkPipeline m_sceneComposite = VK_NULL_HANDLE;
	VkPipelineLayout m_sceneCompositeLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_sceneInputLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_sceneInputPool = VK_NULL_HANDLE;
	VkDescriptorSet m_sceneInputSet = VK_NULL_HANDLE;
	VkShaderModule m_sceneCompositeVert = VK_NULL_HANDLE;
	VkShaderModule m_sceneCompositeFrag = VK_NULL_HANDLE;
	bool m_scenePassReady = false;
	// True for a frame whose transparency was folded into the colour target by the subpass above;
	// the 2D compositor then skips its own two-layer block (and treats the scene as premultiplied,
	// which both paths store).
	bool m_scenePrecomposited = false;

	// The primitive topology is generated, not stored: every quad is two triangles
	// over the same diagonal, and every draw is 6 indices into a four-corner
	// primitive. It never changes, so one 12-byte buffer serves the whole session.
	AllocatedBuffer m_quadIndices;

	// Ambient / scroll fog background pass (see DrawFog).
	VkPipelineLayout m_fogLayout = VK_NULL_HANDLE;
	VkPipeline m_fog = VK_NULL_HANDLE;
	// The same pipeline created for m_scenePass subpass 0.
	VkPipeline m_fogPass = VK_NULL_HANDLE;
	VkShaderModule m_fogVert = VK_NULL_HANDLE;
	VkShaderModule m_fogFrag = VK_NULL_HANDLE;

	/*
	 * Line-of-sight readback under MSAA.
	 *
	 * A multisampled depth image cannot be copied out, so the depth at the LOS texel is read by a
	 * one-invocation compute dispatch (Shaders/los.comp) and written into m_los, the same host-visible
	 * buffer the single-sampled path copies into. Created only when multisampling is active, because
	 * the 1x path needs none of it.
	 */
	VkSampler m_losSampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_losLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_losPool = VK_NULL_HANDLE;
	VkDescriptorSet m_losSet = VK_NULL_HANDLE;
	VkPipelineLayout m_losPipeLayout = VK_NULL_HANDLE;
	VkPipeline m_losPipeline = VK_NULL_HANDLE;
	VkShaderModule m_losModule = VK_NULL_HANDLE;

	AllocatedImage m_color;
	AllocatedImage m_depth;
	// Hardware MSAA. When m_sampleBits > 1 the scene is rasterised into a multisampled m_color and
	// resolved into m_colorResolve, which is the image the 2D layer composites. Antialiasing is then
	// done by the rasteriser on geometry coverage, so polygon edges are smoothed without touching
	// texel sharpness - decals and small textures stay crisp, unlike a post-process blur.
	AllocatedImage m_colorResolve;
	// The two transparency layers are rendered into their own targets, not into m_color. New3D keeps
	// them in separate buffers and composites them as a unit: blending them sequentially onto the
	// colour layer leaks whatever is behind through the LOD cross-fade, and adding them together in a
	// single buffer over-brightens wherever several translucent surfaces overlap. Each target is
	// drawn without blending (the depth test chooses the visible polygon) and the compositor applies
	// New3D's two-layer formula.
	AllocatedImage m_trans1;
	AllocatedImage m_trans1Resolve;
	AllocatedImage m_trans2;
	AllocatedImage m_trans2Resolve;
	uint32_t m_sampleBits = 1;
	VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageLayout m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout m_depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout m_resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout m_trans1Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout m_trans1ResolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout m_trans2Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout m_trans2ResolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	uint64_t m_targetGeneration = ~0ull;

	AllocatedBuffer m_rom;
	AllocatedBuffer m_materials;
	AllocatedBuffer m_los;

	/*
	 * Sorted transparency (AlphaSortMode = 1).
	 *
	 * m_oitCount is an r32ui image holding one fragment count per pixel: the capture stage bumps
	 * it with an imageAtomicAdd on that pixel's own word. m_oitNodes is the fixed-slot A-buffer,
	 * four slots per pixel in a structure-of-arrays layout (slot s of pixel p at s * pixelCount + p),
	 * 8 bytes per node. See real3d_oit.frag for why this replaced a per-pixel linked list.
	 *
	 * There is no global allocation counter: its single hot address was what millions of fragments
	 * serialised on, and that measured as the entire cost of the mode before the A-buffer.
	 */
	AllocatedImage m_oitCount;
	VkImageLayout m_oitCountLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	AllocatedBuffer m_oitNodes;
	bool m_oitReady = false;
	bool m_oitPerSample = false;
	VkDescriptorSetLayout m_oitSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_oitPool = VK_NULL_HANDLE;
	VkDescriptorSet m_oitSet = VK_NULL_HANDLE;
	VkPipeline m_oitPipelines[2][2] = {};		// [overlay][ROM/RAM]
	VkShaderModule m_oitFrag = VK_NULL_HANDLE;
	VkPipeline m_oitResolve = VK_NULL_HANDLE;
	VkPipelineLayout m_oitResolveLayout = VK_NULL_HANDLE;
	VkShaderModule m_oitResolveVert = VK_NULL_HANDLE;
	VkShaderModule m_oitResolveFrag = VK_NULL_HANDLE;

	/*
	 * Dynamic (polygon-RAM) vertices, double buffered per simulation step.
	 *
	 * They used to come from the per-present ring buffer, whose cursor is reset by
	 * every BeginFrame(). That is fine when a present and a simulation step are the
	 * same thing, but a high-refresh loop presents several times from one step: the
	 * second present would bind an offset whose contents the next ring allocation
	 * had already overwritten. This buffer is written once per simulation step and
	 * stays valid until the following step, which is exactly the lifetime the
	 * interpolated presents need.
	 */
	AllocatedBuffer m_dynamicVerts;
	VkDeviceSize m_dynamicSlotBytes = 0;
	uint32_t m_dynamicSlot = 0;
	VkDeviceSize m_materialSlice = 0;
	size_t m_romGpuVerts = 0;
	VkDeviceSize m_ramOffset = 0;
	bool m_ramReady = false;

	// Diagnostics for the ROM vertex buffer, which is a fixed-size GPU buffer the CPU-side geometry
	// cache is allowed to outgrow. Both counters exist so the ceiling can be seen approaching in the
	// log rather than only inferred from missing geometry on screen.
	size_t m_romVertsSeen = 0;
	uint64_t m_romReportFrames = 0;
	uint64_t m_romFullReports = 0;

	/*
	 * Diagnostics for the "something was thrown away and never rebuilt" class of bug.
	 *
	 * A resize destroys and recreates the render targets, and anything still referring to what was
	 * destroyed fails without saying so - which is why a session that has been minimised and restored
	 * can look different from one that has not, with no error anywhere to explain it.
	 *
	 * Each counter below is a path that drops geometry or texture silently today:
	 *
	 *   m_skipRomBeyond        a mesh lies past the end of the uploaded ROM geometry
	 *   m_skipRamNotReady      a dynamic mesh was drawn before its ring allocation succeeded
	 *   m_skipDegenerateNode   a node's viewport collapsed, taking every model in that node with it
	 *
	 * Reported together and only when a value changes, so a normal run stays quiet while a
	 * reproduction names the path that lost the geometry instead of leaving it to be guessed at.
	 */
	uint64_t m_skipRomBeyond = 0;
	uint64_t m_skipRamNotReady = 0;
	uint64_t m_skipDegenerateNode = 0;
	uint64_t m_skipLoggedRom = 0;
	uint64_t m_skipLoggedRam = 0;
	uint64_t m_skipLoggedNode = 0;

	// How many wholly-unuploaded texture regions have had their coordinates reported. Bounded, because
	// the point is to see the shape of the first few rather than to fill the log.
	unsigned m_sampleMissingRegions = 0;

	struct ViewportMotion
	{
		TRS prevBase;
		TRS currBase;
		float prevProjection[16] = {};
		float currProjection[16] = {};
		bool hasPrev = false;
		bool cut = false;
		bool hud = false;
	};

	struct InstanceMotion
	{
		TRS prevLocal;
		TRS currLocal;
		// The raw matrices, kept alongside the decomposition so the interpolated output can be a
		// plain linear blend (LerpMatrix) rather than a slerp: linear blending is what keeps an
		// edge shared by two models coincident, which is what closes the one-frame seams.
		float prevLocalMat[16] = {};
		float currLocalMat[16] = {};
		bool hasPrev = false;
		bool staticLocal = false;
		bool snap = false;
	};

	struct ViewportHistory
	{
		uint32_t sourceAddress = 0;
		float emaDistance = 0.0f;
		float emaAngle = 0.0f;
		uint32_t samples = 0;
		uint64_t lastSeenFrame = 0;
	};

	/*
	 * Recent motion of one instance, keyed by its instance key.
	 *
	 * Matching two frames by key proves that the same slot was addressed twice, not that the same
	 * object was there both times: a game may reuse a matrix slot or a culling path for a different
	 * model between frames. The one property that separates a real pair from a mismatched one is
	 * continuity - an object moves a little between two frames, an unrelated one appears somewhere
	 * else - and that can only be judged against the object's own recent motion, because there is no
	 * absolute speed that fits a walking pedestrian and a car passing the camera in the same scene.
	 */
	struct InstanceHistory
	{
		float emaDistance = 0.0f;
		float emaAngle = 0.0f;
		uint32_t samples = 0;
		uint64_t lastSeenFrame = 0;
	};
	struct DrawItem
	{
		uint32_t nodeIndex = 0;
		uint32_t modelIndex = 0;
		uint32_t materialIndex = 0;
		uint32_t vertexOffset = 0;
		uint32_t vertexCount = 0;
		bool rom = true;
		bool discardAlpha = false;
	};

	// [priority][overlay][layer], where layer is 0=colour, 1=trans2, 2=trans1.
	std::vector<DrawItem> m_drawBuckets[4][2][3];
	// The material table for the current simulation frame, kept on the CPU and
	// copied into the present's own GPU slice in DrawScene.
	std::vector<R3DMaterial> m_materialData;
	uint32_t m_lastDrawCount = 0;
	bool m_instanceInterpolation = false;
	MotionStats m_motionStats;
	uint64_t m_totalCuts = 0;
	uint64_t m_cutsSinceLog = 0;
	uint64_t m_cutFramesSinceLog = 0;
	uint64_t m_framesSinceLog = 0;
	float m_lastAlpha = 1.0f;

	std::vector<ViewportMotion> m_viewportMotion;      // one per current node
	std::vector<std::vector<InstanceMotion>> m_modelMotion;
	std::vector<ViewportHistory> m_viewportHistory;
	std::unordered_map<uint64_t, InstanceHistory> m_instanceHistory;
	uint64_t m_motionFrame = 0;
	float m_cameraCutAngleDeg = 15.0f;
	float m_instanceCutAngleDeg = 30.0f;
	float m_cutDistanceScale = 8.0f;
	int m_interpolationScope = 0;

	bool m_drew = false;
	uint64_t m_frameSerial = 1;

	float m_losValue[4] = {};
	bool m_losReady[kMaxFramesInFlight] = {};
	bool m_losComputeLogged = false;
	std::mutex m_losMutex;
};

} // Vulkan3D

#endif
