/*
 * Model 3-Vulkan - ui_blur_down.frag
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
 * Dual Kawase blur - downsample stage.
 *
 * The frosted-glass backdrop in front of the settings panels is produced at half the swapchain
 * resolution by a two-level pyramid: this shader halves it once more (to a quarter), the companion
 * ui_blur_up.frag brings it back. Compared with a large-radius single-pass gaussian this costs a
 * handful of texture fetches per output pixel at 1/4 and 1/2 resolution instead of a few dozen at
 * full resolution, which is what makes it affordable to run on the frame the in-game panel opens.
 *
 * The filter is the classic Kawase "diagonal box": the centre tap plus the four diagonal neighbours
 * at half a source texel, weighted 4:1:1:1:1. Because the sampler is bilinear, each of those taps is
 * itself an average of four texels, so one pass covers a 3x3 neighbourhood with no grid pattern -
 * the "checkerboard" artefact a naive bilinear downsample leaves on high-contrast edges is exactly
 * what the diagonal offsets avoid.
 *
 * Half a texel, not one: at one texel the offsets land between texel centres and the bilinear
 * weights become uneven, which shows up as a faint diamond lattice once the result is magnified.
 */

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

layout(set = 0, binding = 0) uniform sampler uSampler;
layout(set = 0, binding = 1) uniform texture2D uSource;

layout(push_constant) uniform PushConstants
{
	// 1.0 / extent of the image being sampled, in texels.
	vec2 texelSize;
	vec2 padding;
} pc;

void main()
{
	const vec2 halfTexel = pc.texelSize * 0.5;

	vec4 sum = texture(sampler2D(uSource, uSampler), vUv) * 4.0;
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2(-halfTexel.x, -halfTexel.y));
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2( halfTexel.x, -halfTexel.y));
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2(-halfTexel.x,  halfTexel.y));
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2( halfTexel.x,  halfTexel.y));

	// Weights sum to 8, so this keeps the average brightness rather than darkening each level -
	// a chain that drifted darker would make the panel tint unpredictable at different sizes.
	outColour = sum * 0.125;
}
