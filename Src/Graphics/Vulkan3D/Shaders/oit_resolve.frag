/*
 * Model 3-Vulkan - oit_resolve.frag
 * Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 *
 * Additional Terms under GNU GPLv3 Section 7 apply to this file
 * (see LICENSE-ADDITIONAL-TERMS.md).
 *
 * This shader is part of Model 3-Vulkan, a fork of Supermodel
 * (Copyright 2003-2026 The Supermodel Team), and is distributed under the
 * GNU General Public License v3 or later.  There is NO WARRANTY.
 */
#version 450

/*
 * Per-pixel sorted-transparency resolve.
 *
 * Reads the fixed-slot A-buffer written by real3d_oit.frag, sorts the slots far to near and
 * composites them back to front. The output is a premultiplied colour that the pipeline blends with
 * ONE / ONE_MINUS_SRC_ALPHA, which is source-over against whatever the opaque pass left in the
 * colour attachment - including the fog background, whose alpha is what lets the 2D bottom layer
 * show through empty sky.
 *
 * Both arrays are indexed by constant expressions only, and the sort is a 5-comparator network over
 * four slots, so the colours and keys stay in registers. The previous linked-list version had to
 * chase pointers into random memory and sort with a running index, which spilled to local memory;
 * at 4K that was the difference between frame rate and half of it.
 */

// Must match kOitLayers in Vulkan3D.cpp.
const int kOitLayers = 4;

// Bit 9 of the node key: this fragment is a srally2 dust particle (IsDustEffect). It is masked
// out of the depth sort and used only by the layered-polygon rule below.
const uint kLayeredBit = 1u << 9;

// 1 = one invocation per sample (MSAA with sampleRateShading); 0 = one per pixel.
layout(constant_id = 0) const int kPerSample = 1;

layout(location = 0) out vec4 outColour;

layout(std430, set = 0, binding = 0) readonly buffer OitNodes
{
	uvec2 nodes[];
} oitNodes;

layout(r32ui, set = 0, binding = 1) uniform uimage2D oitCount;

// Ascending compare-exchange on (depth, layer rank). Depth is the upper 20 bits of the key, so a
// plain integer compare is exact; the rank only decides equal depths.
void CompareSwap(inout vec4 ca, inout uint ka, inout vec4 cb, inout uint kb)
{
	// The LAY marker is not part of the visibility order. Masking it here keeps the existing
	// depth/rank ordering for every node; the bit is carried along with its colour below.
	if ((ka & ~kLayeredBit) > (kb & ~kLayeredBit))
	{
		vec4 ct = ca; ca = cb; cb = ct;
		uint kt = ka; ka = kb; kb = kt;
	}
}

void main()
{
	const ivec2 coord = ivec2(gl_FragCoord.xy);
	const uint count = imageLoad(oitCount, coord).r;

	/*
	 * An empty pixel is the common case and writes nothing. Discarding matters for cost: without
	 * it the pass still blends a zero contribution into every sample of every pixel, which at 4K
	 * with 4x MSAA is 33 million read-modify-write operations per resolve. The whole pixel shares
	 * one count, so every per-sample invocation of an empty pixel takes this branch.
	 */
	if (count == 0u)
		discard;

	// Per sample, include only fragments whose stored rasterisation coverage contains this sample.
	// Without per-sample shading the mask is ignored, which is the 1x-quality fallback.
	const uint sampleBit = (kPerSample != 0) ? (1u << gl_SampleID) : 0xFFFFFFFFu;

	const ivec2 size = imageSize(oitCount);
	const uint pixelCount = uint(size.x) * uint(size.y);
	const uint linear = uint(coord.y) * uint(size.x) + uint(coord.x);
	const uint used = min(count, uint(kOitLayers));

	vec4 colours[kOitLayers];
	uint keys[kOitLayers];

	for (int i = 0; i < kOitLayers; ++i)
	{
		if (uint(i) < used)
		{
			const uvec2 node = oitNodes.nodes[uint(i) * pixelCount + linear];
			const bool visible = (node.y & sampleBit) != 0u;
			colours[i] = visible ? unpackUnorm4x8(node.x) : vec4(0.0);
			keys[i] = visible ? node.y : 0u;
		}
		else
		{
			// Empty slot: alpha 0, far end of the sort, composites as a no-op.
			colours[i] = vec4(0.0);
			keys[i] = 0u;
		}
	}

	// Ascending sort network for four inputs.
	CompareSwap(colours[0], keys[0], colours[1], keys[1]);
	CompareSwap(colours[2], keys[2], colours[3], keys[3]);
	CompareSwap(colours[0], keys[0], colours[2], keys[2]);
	CompareSwap(colours[1], keys[1], colours[3], keys[3]);
	CompareSwap(colours[1], keys[1], colours[2], keys[2]);

	// Back to front: source-over with the far slot first. A dust fragment is allowed to
	// contribute only once per pixel/sample, and the one the hardware stencil would have kept is
	// the nearest of them. The array is sorted far to near, so find the last visible dust node
	// before compositing; skipping every other one prevents a dust shell's overlapping quads from
	// double-blending into a dark crease.
	int nearestLayered = -1;
	for (int i = kOitLayers - 1; i >= 0; --i)
	{
		if (colours[i].a > 0.0 && (keys[i] & kLayeredBit) != 0u)
		{
			nearestLayered = i;
			break;
		}
	}

	vec4 result = vec4(0.0);
	for (int i = 0; i < kOitLayers; ++i)
	{
		if ((keys[i] & kLayeredBit) != 0u && i != nearestLayered)
			continue;

		const float a = colours[i].a;
		result.rgb = colours[i].rgb * a + result.rgb * (1.0 - a);
		result.a = a + result.a * (1.0 - a);
	}

	outColour = result;
}
