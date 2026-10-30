/*
 * Model 3-Vulkan - ui_frost.frag
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
 * Frosted-glass backdrop - final stage.
 *
 * The last upsample of the Dual Kawase chain, and at the same time the colour grade that turns a
 * blurred copy of the frame into something worth putting text on. Everything here is driven by push
 * constants, so one pipeline serves both interface themes:
 *
 *   1. Tent blur.  Same eight-tap filter as ui_blur_up.frag, but this one replaces the destination
 *                  instead of blending over it: it is the end of the chain, and the half-resolution
 *                  image it writes is what ImGui samples.
 *
 *   2. Vibrancy.   Apple's "vibrancy" is saturation restored after a backdrop has been desaturated
 *                  by the material over it. The saturation lift is modest on purpose: the panel tint
 *                  sits over this image at 72-78% opacity, so only a quarter of the colour reaches
 *                  the eye, and too much of it turns the game behind the panel into a poster.
 *
 *   3. Theme grade. The two themes need opposite treatments of the same blurred picture, and getting
 *                  this wrong is what makes a light frosted panel look dirty:
 *
 *                  Dark  - highlights above a knee are compressed with a rational curve. A clamp
 *                          would flatten every value above the knee to one colour, which on a dark
 *                          panel reads as a pale smudge behind the text.
 *
 *                  Light - the picture is mixed 55% toward a warm white. A light pane over an
 *                          unlifted dark game is a grey smear, not vellum; lifting is the analogue of
 *                          the dark theme's darkening, and the tint applied over it then keeps the
 *                          pane bright. Chroma is restored afterwards (mixing toward white washes
 *                          hue out, and a colourless pane reads as dirty rather than as paper).
 *
 *   4. Grain.      1.5/255 of interleaved gradient noise. Two purposes in one: it dithers the 8-bit
 *                  quantisation of the smooth gradient that a blur is mostly made of - without it a
 *                  frosted panel shows obvious banding rings - and it gives the surface the faint
 *                  tooth real etched glass has. Ign() is the standard three-line hash; it is
 *                  screen-space rather than seed-random so the pattern is stable frame to frame and
 *                  does not crawl.
 *
 * The image is in the swapchain's own format, which this backend picks as a plain UNORM, so no
 * transfer function is involved anywhere in the chain and the values read and written here are the
 * same encoded values the rest of the compositor uses.
 */

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

layout(set = 0, binding = 0) uniform sampler uSampler;
layout(set = 0, binding = 1) uniform texture2D uSource;

layout(push_constant) uniform PushConstants
{
	// 1.0 / extent of the image being sampled, in texels.
	vec2  texelSize;
	float saturation;       // 1.0 leaves the colours alone
	float grain;            // peak amplitude, as a fraction of full scale (1.5/255 ~= 0.0059)
	float knee;             // highlights above this are compressed; 1.0 disables the stage
	float lightMode;        // 1.0 for the warm light theme, 0.0 for the dark one
	vec2  padding;
} pc;

const vec3 kLumaWeights = vec3(0.2126, 0.7152, 0.0722);

// Interleaved gradient noise: a single hash producing a well-distributed value per pixel, without
// the clumping a plain fract(sin(...)) hash shows at low amplitudes.
float InterleavedGradientNoise(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

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

	vec3 colour = sum.rgb / 12.0;

	// Vibrancy: Rec.709 luma as the neutral axis, then the distance from it is scaled.
	float luma = dot(colour, kLumaWeights);
	colour = mix(vec3(luma), colour, pc.saturation);

	if (pc.lightMode > 0.5)
	{
		// Warm eye-care light: lift toward the theme's own paper so the pane reads as backlit
		// vellum rather than as a grey wash over a dark game.
		colour = mix(colour, vec3(0.96, 0.94, 0.91), 0.55);

		// Mixing toward white removes chroma; put some back so a colourful cover still shows
		// through as colour instead of as tinted grey.
		float liftedLuma = dot(colour, kLumaWeights);
		colour = mix(vec3(liftedLuma), colour, 1.30);
	}
	else if (pc.knee < 1.0)
	{
		// Soft highlight roll-off. Compresses the excess over the knee by a rational curve, then
		// scales the whole colour by the same factor so hue is preserved. Monotonic: a brighter
		// input is always a brighter output, which is what keeps the bright part of the picture
		// from inverting.
		float peak = max(colour.r, max(colour.g, colour.b));
		if (peak > pc.knee && peak > 1.0e-5)
		{
			float over = peak - pc.knee;
			float compressed = pc.knee + over / (1.0 + over * 2.2);
			colour *= compressed / peak;
		}
	}

	colour = clamp(colour, 0.0, 1.0);

	// Grain last, so the dither also covers the quantisation introduced by the clamp above.
	float noise = InterleavedGradientNoise(gl_FragCoord.xy) - 0.5;
	colour += vec3(noise * 2.0 * pc.grain);

	// Opaque: the panel tint is applied by ImGui over this, and an alpha here would let the
	// unblurred frame show through the middle of a panel.
	outColour = vec4(clamp(colour, 0.0, 1.0), 1.0);
}
