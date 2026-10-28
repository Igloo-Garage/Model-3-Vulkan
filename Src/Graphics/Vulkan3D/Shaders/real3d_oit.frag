#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

/*
 * Sorted-transparency capture stage (AlphaSortMode = 1).
 *
 * The shading body is the same one the normal scene pass runs; only the destination differs.
 * Instead of writing the two fixed transparency attachments, this stage stores the fragment in a
 * fixed-slot per-pixel array, and a later full-screen resolve sorts those slots back to front.
 * That removes the two-layer ceiling and the submission-order dependence of hardware alpha
 * blending, which is what allows more than two translucent surfaces to overlap correctly.
 *
 * The storage is an A-buffer, not a linked list:
 *
 *   - One slot per fragment at the pixel, up to kOitLayers of them, in a structure-of-arrays
 *     layout: slot s of pixel p lives at s * pixelCount + p. Adjacent pixels in a warp therefore
 *     touch adjacent 8-byte words in the same slot plane, which the L2 can service as one
 *     coalesced access. A linked list does the opposite - every node is at an arbitrary address
 *     and the resolve has to chase the `next` pointers.
 *   - No global allocation counter. Each pixel's count lives in an r32ui image and is bumped with
 *     an imageAtomicAdd on that pixel's own word, so there is no single hot address for millions
 *     of fragments to serialise on.
 *   - A node is 8 bytes: packUnorm4x8(colour) and a 32-bit key. The key is the depth in the upper
 *     20 bits, the layer rank in the next 2 and the rasterisation coverage mask in the low 8; the
 *     low 12 bits of the float depth are dropped, which is far below anything that matters for a
 *     visibility order. Bit 9 of the key is the dust marker (IsDustEffect); the depth sort masks it
 *     out, so it only tells the resolve which fragments take part in the layered-polygon rule.
 *
 * `early_fragment_tests` is the whole point of this stage existing separately: the depth test must
 * run before the shader, because its side effects (the atomic and the store) cannot be undone if
 * the depth test later fails. gl_FragDepth is deliberately not written here: the normal stage
 * writes `projection[3][2] * gl_FragCoord.w`, and for every projection this backend builds
 * (Mat4::FrustumRZ, whose third row is (0,0,0,near)) that is exactly gl_FragCoord.z. A fragment
 * shader that writes gl_FragDepth cannot have its depth test hoisted above it, so the two
 * requirements would be mutually exclusive. The equality is what lets this stage keep the
 * hardware depth while dropping the write; see CR3DSceneBuilder::CalcViewport.
 *
 * One node per pixel and not per sample: the pipeline leaves sampleShadingEnable false, so this
 * runs once per covered pixel, and the rasteriser's coverage mask is stored in the key for the
 * resolve to apply per sample. That keeps the node count at one per pixel rather than one per
 * sample while preserving MSAA edges on translucent geometry.
 */

#include "real3d_common.glsl"

layout(early_fragment_tests) in;

// Must match kOitLayers in Vulkan3D.cpp.
const int kOitLayers = 4;

// The key's low 12 bits are unused by the depth, which occupies bits 31..12. Coverage uses
// bits 0..7 and the layer rank bits 10..11, so bit 9 is free for the dust marker (IsDustEffect).
const uint kLayeredBit = 1u << 9;

/*
 * slot s of the pixel at linear index p is nodes[s * pixelCount + p].
 * Count per pixel is in oitCount; 0 means no translucent fragment reached the pixel this pass.
 * Both are in GENERAL for their whole life: they are written through storage-image atomics and
 * read back as storage images, and the count is cleared once per priority pass.
 */
layout(std430, set = 1, binding = 0) writeonly buffer OitNodes
{
	uvec2 nodes[];
} oitNodes;

layout(r32ui, set = 1, binding = 1) uniform uimage2D oitCount;

void main()
{
	vec4 colour = ShadeFragment();

	/*
	 * Fully transparent fragments must not allocate a slot. ShadeFragment already discards
	 * anything below 1/32, but that is a discard inside the shared body; the threshold is restated
	 * here so this stage's cost model does not depend on a rule that lives elsewhere. Without it,
	 * the enormous alpha-0 areas of layered water and spray would each consume a slot, a store and
	 * an atomic.
	 */
	if (colour.a <= 0.0)
		return;

	const ivec2 coord = ivec2(gl_FragCoord.xy);
	const uint slot = imageAtomicAdd(oitCount, coord, 1u);
	if (slot >= uint(kOitLayers))
		return;

	const uint pixelCount = uint(imageSize(oitCount).x) * uint(imageSize(oitCount).y);
	const uint linear = uint(coord.y) * uint(imageSize(oitCount).x) + uint(coord.x);

	// trans1 sorts above trans2 at equal depth, matching the order the layers used to be drawn in.
	const uint rank = (pc.extra.z < 1.5) ? 1u : 0u;
	const uint coverage = uint(gl_SampleMaskIn[0]) & 0xFFu;
	const uint layered = IsDustEffect(materials.m[floatBitsToUint(pc.extra.w)].flags) ? kLayeredBit : 0u;
	// Drop the 12 least significant depth bits to make room for rank and coverage. Depth is on
	// [0,1], so 20 bits still resolve about a million distinct distances.
	const uint key = (floatBitsToUint(gl_FragCoord.z) & 0xFFFFF000u) | (rank << 10) | coverage | layered;

	oitNodes.nodes[slot * pixelCount + linear] =
		uvec2(packUnorm4x8(vec4(clamp(colour.rgb, vec3(0.0), vec3(1.0)), clamp(colour.a, 0.0, 1.0))), key);
}
