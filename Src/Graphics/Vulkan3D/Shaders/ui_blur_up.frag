/*
 * Model 3-Vulkan - ui_blur_up.frag
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
 * Dual Kawase blur - upsample stage.
 *
 * A tent filter over the eight neighbours of the coarse level, one texel apart and two texels along
 * the axes, which is the companion to ui_blur_down.frag's diagonal box. The two together give a
 * smooth falloff with no ringing - a gaussian-like result for a fraction of the fetches.
 *
 * The result is blended over the finer level rather than added to it. The published formulation
 * renders into a copy of the finer level with ONE/ONE, which brightens the image by roughly the
 * amount of the coarse level: fine for a bloom chain where the sum is the point, wrong here, because
 * the frosted backdrop is a *replacement* for the frame the user is looking at and a tint applied on
 * top of it cannot compensate for a brightness that changes with the panel size. Blending 50/50 with
 * SRC_ALPHA therefore lands at 1.0x: the alpha channel carries the weight, and the destination is
 * the finer level already on the attachment.
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
	const vec2 h = pc.texelSize * 0.5;

	vec4 sum = texture(sampler2D(uSource, uSampler), vUv + vec2(-h.x * 2.0, 0.0));
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2(-h.x,  h.y) * 2.0) * 2.0;
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2(0.0,  h.y * 2.0));
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2( h.x,  h.y) * 2.0) * 2.0;
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2( h.x * 2.0, 0.0));
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2( h.x, -h.y) * 2.0) * 2.0;
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2(0.0, -h.y * 2.0));
	sum += texture(sampler2D(uSource, uSampler), vUv + vec2(-h.x, -h.y) * 2.0) * 2.0;

	// Weights sum to 12. The 0.5 alpha is the mixed weight described above.
	outColour = vec4((sum / 12.0).rgb, 0.5);
}
