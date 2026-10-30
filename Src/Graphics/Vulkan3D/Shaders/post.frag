/*
 * Model 3-Vulkan - post.frag
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

// Final post-processing pass for the Vulkan backend.
//
// Runs once over the fully composited frame (3D scene plus both tile layers) on its way to the
// swapchain, so it sees exactly what the player will see. Stages, all optional and driven by push
// constants so one pipeline covers every combination:
//
//   1. FXAA   - luma-gradient edge antialiasing, for cases where hardware MSAA is not affordable.
//               MSAA (see CVulkan3D) is preferable: it smooths geometry coverage without touching
//               texel sharpness, whereas FXAA works on the finished image and softens everything.
//   2. Glow   - a soft, bright-weighted halo standing in for the light that scatters through the
//               faceplate glass of a CRT. Real tubes bloom around bright phosphor; this reproduces
//               the wide, low-frequency part of it, which is what makes the image read as
//               "glowing glass" rather than "sharp pixels with a grid drawn over them".
//   3. CRT    - beam profile (scanlines) and the phosphor mask. Applied last so the mask lands on
//               the antialiased, already-glowing image, the way a real shadow mask sits behind the
//               phosphor. The mask is evaluated in OUTPUT pixels, which is what makes a 1:1 mapping
//               against the panel possible.
//   4. Gain   - the mask and scanlines each remove light (a shadow mask passes roughly a third of
//               it), so the picture would otherwise come out dim and muddy. A real tube compensates
//               with very high peak brightness, which is also why an arcade monitor looks so punchy.
//               Without an HDR output pipeline this simple gain is the stand-in, and it is the
//               control to reach for if the mask looks too dark.
//
// Every stage is neutral at its default, so an untouched configuration is still an exact
// pass-through and the pillarbox bars stay black.

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

layout(set = 0, binding = 0) uniform sampler uSampler;
layout(set = 0, binding = 1) uniform texture2D uImage;

layout(push_constant) uniform PushConstants
{
	vec2  resolution;           // Size of the image being sampled, in texels.
	float fxaaEnabled;          // 0.0 disables the FXAA stage.
	float scanlineIntensity;    // 0.0..1.0 depth of the horizontal scanlines.
	float maskIntensity;        // 0.0..1.0 strength of the phosphor mask.
	float scanlineCount;        // Number of scanlines across the image height.
	int   crtMode;              // 0 = off, 1 = scanlines, 2 = scanlines + phosphor mask.
	int   maskType;             // 0 = aperture grille, 1 = slot mask, 2 = shadow mask (dot triad).
	float maskScale;            // Output pixels per mask cell. 1.0 is a 1:1 mapping.
	float glowStrength;         // 0.0..1.0 amount of glass glow added.
	float gain;                 // Brightness compensation, 1.0 = unchanged.
	int   glowLevels;           // Mip levels accumulated for the glow (0 disables it).
	int   glowThreshold;        // Luma (0..100) a level must exceed before it contributes to the glow.
	int   crtColors;            // Regional CRT colour preset, 0 = off, 1..5 = the SuperAA matrices.
} pc;

#define FXAA_EDGE_THRESHOLD       (1.0 / 8.0)
#define FXAA_EDGE_THRESHOLD_MIN   (1.0 / 24.0)
#define GLOW_MAX_LEVELS           8

/*
 * Base image fetch, explicitly at mip level 0.
 *
 * The LOD must be pinned. The composite carries a full mip chain for the glow to read, and the
 * sampler's maxLod is open so the post pass can reach those levels - which means an implicit
 * texture() sample is free to pick a blurrier level than intended. When it did, the whole picture
 * came out as a dark, washed-out average instead of the sharp frame.
 */
vec3 Fetch(vec2 uv)
{
	return textureLod(sampler2D(uImage, uSampler), uv, 0.0).rgb;
}

float Luma(vec3 c)
{
	// FXAA and the bright pass both want perceptual luma; these are the green-weighted Rec.601
	// coefficients the FXAA reference implementation uses.
	return dot(c, vec3(0.299, 0.587, 0.114));
}

/*
 * FXAA 3.11 (the "quality"/console variant): find the direction of the local luma gradient, step
 * along it looking for where the edge ends, then blend a small neighbourhood along that direction.
 * This smooths stair-stepped polygon edges while leaving high-contrast interior detail alone.
 */
vec3 ApplyFxaa(vec2 uv, vec2 texel)
{
	vec3 rgbM = Fetch(uv);
	float lumaM = Luma(rgbM);

	float lumaNW = Luma(Fetch(uv + vec2(-1.0, -1.0) * texel));
	float lumaNE = Luma(Fetch(uv + vec2( 1.0, -1.0) * texel));
	float lumaSW = Luma(Fetch(uv + vec2(-1.0,  1.0) * texel));
	float lumaSE = Luma(Fetch(uv + vec2( 1.0,  1.0) * texel));

	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
	float lumaRange = lumaMax - lumaMin;

	// Flat area: nothing to smooth. Also skip low-contrast noise, which FXAA would otherwise smear.
	if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD))
		return rgbM;

	// Gradient direction, from the corners of the 3x3 neighbourhood.
	vec2 dir;
	dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * (1.0 / 8.0)), 1.0 / 128.0);
	float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * texel;

	// Step along the edge in both directions and blend across it.
	vec3 rgbA = 0.5 * (Fetch(uv + dir * (1.0 / 3.0 - 0.5)) + Fetch(uv + dir * (2.0 / 3.0 - 0.5)));
	vec3 rgbB = rgbA * 0.5 + 0.25 * (Fetch(uv + dir * -0.5) + Fetch(uv + dir * 0.5));

	float lumaB = Luma(rgbB);
	// If the wider blend overshoots the local luma range it has picked up unrelated detail, so fall
	// back to the narrower one.
	if (lumaB < lumaMin || lumaB > lumaMax)
		return rgbA;
	return rgbB;
}

/*
 * Glow: a multi-scale blur read straight out of the composite's mip chain.
 *
 * The mip levels were produced by vkCmdBlitImage, so level n is a box-filtered average of level n-1
 * - a blur whose radius doubles every level. Summing several levels with falling weights therefore
 * gives the wide, soft halo of light scattered through a CRT's faceplate glass, and each level
 * stays local: sampling level 6 at a UV returns the average of the small patch of screen that maps
 * there, not the average of the whole image. That locality is what makes this read as a glow around
 * bright things rather than a uniform lift.
 *
 * A bright pass is folded in per level so dark areas stay dark instead of turning into grey haze.
 *
 * Compare with the obvious approach, a chain of custom blur passes: this produces the same
 * multi-scale result from a handful of hardware blits and one shader pass, which is why the CRT
 * look is reachable without a general multi-pass framework.
 */
vec3 ComputeGlow(vec2 uv)
{
	vec3 sum = vec3(0.0);
	// Normalising by the falloff total rather than by the weight total is what keeps this a halo
	// instead of a blurred copy of the picture.
	//
	// Dividing by the weight total made the result a weighted average, so any area that cleared the
	// threshold returned roughly its own colour and the *amount* by which it cleared the threshold
	// was thrown away: a sunlit field sitting just above the threshold glowed exactly as strongly as
	// a white line sitting far above it. The whole field then came out uniformly lifted, which is
	// what "the pitch looks like it is glowing" was.
	//
	// Dividing by the falloff total instead lets the weight keep its magnitude, so how far a region
	// exceeds the threshold stays proportional to how much it glows. It also remains independent of
	// glowLevels, because the numerator and the denominator carry the same falloff sum.
	float falloffSum = 0.0;

	for (int level = 1; level <= GLOW_MAX_LEVELS; ++level)
	{
		if (level > pc.glowLevels)
			break;
		float lod = float(level);

		vec3 sampleColour = textureLod(sampler2D(uImage, uSampler), uv, lod).rgb;

		// How far above the threshold this level is. The threshold is adjustable because how much
		// ought to glow is a matter of taste and of what the game looks like: a high value keeps the
		// effect to genuine highlights, while a low one lets broad bright areas such as grass or a
		// snowfield contribute.
		float weight = max(Luma(sampleColour) - float(pc.glowThreshold) / 100.0, 0.0);
		// Coarser levels cover more screen, so weight them down or the widest level dominates into a
		// flat wash.
		float falloff = 1.0 / lod;

		sum += sampleColour * weight * falloff;
		falloffSum += falloff;
	}

	return falloffSum > 0.0 ? sum / falloffSum : vec3(0.0);
}

/*
 * Shape-based phosphor rendering.
 *
 * The older approach here (and in most simple CRT filters) multiplies the image by a repeating
 * pattern: a grid of light/dark factors. That works for a grille but cannot represent the single
 * most characteristic property of a phosphor mask, which is that the lit area of each dot grows
 * with the beam current. On a real tube a bright pixel lights a fat blob of phosphor and a dim one
 * a small speck, and it is that variation which reads as glowing grain rather than as a fine grid
 * drawn over the picture.
 *
 * So the mask is evaluated as a distance field instead. For every output pixel we find our position
 * inside one mask cell, then for each colour channel measure the distance to that channel's phosphor
 * sub-hole. The hole's radius is modulated by brightness, and the result is a soft edge rather than
 * a hard cut, which is what the electron beam's focus actually gives.
 *
 * This is an independent implementation of the technique. The well-known Libretro shaders
 * (PhosphorDot, PhosphorLine, CRT-Royale and friends) do something similar; their code is not used
 * here, in part because PhosphorDot carries an explicit no-commercial-use notice that is
 * incompatible with this project's licence.
 */

// Sub-hole offsets within a cell, in cell units (each axis spans -0.5..0.5).
// Aperture grille: three vertical stripes, so the holes are spread along x only.
// Slot mask: stripes, but the middle hole is offset vertically so the cells interlock.
// Shadow mask: a 2x2 triad, R and G on the top row with B below between them.
vec2 PhosphorHole(int channel, float rowParity)
{
	if (pc.maskType == 0)
	{
		// Aperture grille: R, G, B stripes left to right.
		return vec2((float(channel) - 1.0) * 0.3333, 0.0);
	}
	else if (pc.maskType == 1)
	{
		// Slot mask: stripes with the centre slot pushed up or down on alternate rows, which is
		// what makes the holes read as separate slots instead of continuous lines.
		float stagger = (rowParity < 0.5 ? -1.0 : 1.0) * 0.25;
		float x = (float(channel) - 1.0) * 0.3333;
		float y = (channel == 1) ? stagger : -stagger * 0.5;
		return vec2(x, y);
	}

	// Shadow mask: R top-left, G top-right, B bottom-centre.
	if (channel == 0) return vec2(-0.25, -0.25);
	if (channel == 1) return vec2( 0.25, -0.25);
	return vec2(0.0, 0.25);
}

/*
 * Coverage of one phosphor hole at cell-local position 'sub' (each axis -0.5..0.5).
 *
 * radius is in cell units; softness controls the edge falloff and stands in for beam focus. A
 * minimum radius keeps dim areas from collapsing to nothing, which would look like dead phosphor.
 */
float HoleCoverage(vec2 sub, vec2 cellAspect, float radius, float softness)
{
	vec2 d = sub * cellAspect;
	float dist = length(d);
	return clamp((radius - dist) / max(softness, 0.001) + 0.5, 0.0, 1.0);
}

vec3 ApplyMask(vec2 pixel, vec3 colour)
{
	// Cells are measured in output pixels, so maskScale directly controls how large a phosphor dot
	// appears and 1.0 is a 1:1 mapping against the panel.
	float scale = max(pc.maskScale, 1.0);
	vec2 cellSpace = pixel / scale;
	vec2 cellIndex = floor(cellSpace);
	vec2 sub = cellSpace - cellIndex - 0.5;

	// Cells are not square at every scale: with a scale of 2 the cell is 2x2 output pixels, which is
	// square, but odd scales and non-integer aspect corrections make them elongated. Normalising by
	// the cell size keeps the dots round (or the slots the right height).
	vec2 cellAspect = vec2(1.0, 1.0);
	if (pc.maskType == 2)
		cellAspect = vec2(1.0, 2.0);        // shadow mask cells are twice as tall as wide
	else
		cellAspect = vec2(1.0, 1.0);

	// Beam-dependent hole size. This is the property that makes the mask look like glowing phosphor:
	// bright pixels light a big area of phosphor, dark ones a small spec. The range is kept fairly
	// wide rather than narrow, because a mask that only opens up on highlights takes most of the
	// picture's light away - see the note on gain in post.frag.
	//
	// The brightness is sampled at the *cell* centre, not at this output pixel. A phosphor dot is
	// one dot: it is lit by one beam current, so the whole cell must share one hole size. Driving
	// the radius from the per-pixel colour instead made adjacent pixels inside one cell disagree,
	// and on a finely detailed surface - Daytona 2's waterfall, rock faces, foliage - that turned
	// the texture's own high-frequency detail into per-pixel red/green/blue speckle. It looked like
	// blue noise over the artwork, and it moved as the camera did. Sampling the cell centre is both
	// what the tube does and what keeps the mask free of colour noise.
	vec2 cellCentrePixel = (cellIndex + 0.5) * scale;
	float luma = Luma(Fetch(cellCentrePixel / pc.resolution));
	float radius = 0.24 + 0.28 * clamp(luma, 0.0, 1.0);
	float softness = 0.16;

	float rowParity = mod(cellIndex.y, 2.0);
	vec3 holes = vec3(
		HoleCoverage(sub - PhosphorHole(0, rowParity), cellAspect, radius, softness),
		HoleCoverage(sub - PhosphorHole(1, rowParity), cellAspect, radius, softness),
		HoleCoverage(sub - PhosphorHole(2, rowParity), cellAspect, radius, softness));

	// The floor keeps an unlit channel glowing a little. A low floor looks more like a real mask but
	// costs a lot of light, and since the gain has to make that back up on an SDR display, a floor
	// that is too low simply crushes the picture.
	return colour * mix(vec3(1.0), 0.35 + 0.65 * holes, pc.maskIntensity);
}

/*
 * CRT stage: beam profile (scanlines) and the phosphor mask.
 *
 * The mask runs on the pre-scanline colour so that the brightness that drives the hole size is the
 * signal, not the signal already dimmed by the scanline, and the scanline is applied afterwards so
 * it modulates the finished phosphor.
 */
vec3 ApplyCrt(vec2 pixel, vec3 colour)
{
	if (pc.crtMode <= 0)
		return colour;

	vec3 result = colour;

	if (pc.crtMode >= 2 && pc.maskIntensity > 0.0)
		result = ApplyMask(pixel, result);

	// Scanlines, as a beam profile.
	//
	// The beam has width, so an output pixel does not see the profile at its centre - it sees the
	// average of it over the part of the scan the pixel covers. That average has a closed form:
	//
	//     integral of cos^2 over [t0, t1], divided by (t1 - t0)
	//         = 1/2 + (sin(2*t1) - sin(2*t0)) / (4*(t1 - t0))
	//
	// Integrating matters most on high-resolution displays. Point-sampling the profile at pixel
	// centres only lines up when a scanline spans a whole number of pixels, and at 4K the Model 3's
	// 384 rows across 2160 output rows is 5.625 pixels per line, so the phase drifts on every row and
	// the point-sampled version beats into broad light and dark bands. The integral removes the
	// dependency on that ratio: any scanline count renders smoothly, and a count fine enough that two
	// lines fall inside one pixel fades out on its own instead of turning into moire.
	if (pc.scanlineIntensity > 0.0)
	{
		float theta = vUv.y * pc.scanlineCount * 3.14159265358979;
		float dTheta = pc.scanlineCount * 3.14159265358979 / pc.resolution.y;
		float theta0 = theta - 0.5 * dTheta;
		float theta1 = theta + 0.5 * dTheta;
		float beam = dTheta > 0.0
			? 0.5 + (sin(2.0 * theta1) - sin(2.0 * theta0)) / (4.0 * dTheta)
			: 1.0;
		result *= 1.0 - pc.scanlineIntensity * (1.0 - beam);
	}

	return result;
}

/*
 * Regional CRT colour adaptation, ported from the OpenGL path (SuperAA.cpp).
 *
 * Colour temperature and gamut are properties of the tube, so they belong before the scanline and
 * mask stages: those model the beam and the phosphor, which sit between the signal and the glass.
 * Earlier still would be wrong as well - FXAA and the glow expect the picture they are given - but
 * the mask reacts to brightness, so correcting after it would feed the mask a colour the tube never
 * produced.
 *
 * The matrices convert a Japanese/European/American broadcast gamut to sRGB, the space the rest of
 * the pipeline works in. They are the same five matrices, in the same order and with the same
 * meaning as the OpenGL shader, taken from the colour matrix adaptations by danmons (see
 * retrorgb.com/colour-malarkey.html). "linear *= matrix" deliberately mirrors the OpenGL
 * expression: the constructor arguments fill columns, and GLSL's vector-times-matrix convention is
 * part of what the ported values were computed for.
 *
 * Presets 1 and 2 decode with the tube's own gamma (2.5 / 2.25) because those monitors were not
 * sRGB; the BT.601 presets decode as sRGB. All of them encode back to sRGB for output.
 */
vec3 SrgbEncode(vec3 colour)
{
	vec3 low = colour * 12.92;
	vec3 high = 1.055 * pow(max(colour, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
	return mix(high, low, lessThanEqual(colour, vec3(0.0031308)));
}

vec3 SrgbDecode(vec3 colour)
{
	vec3 low = colour * (1.0 / 12.92);
	vec3 high = pow((colour + 0.055) / 1.055, vec3(2.4));
	return mix(high, low, lessThanEqual(colour, vec3(0.04045)));
}

mat3 CrtColourMatrix(int mode)
{
	if (mode == 1)
	{
		// ARI/D93 (JP) -> sRGB(D65)
		return mat3(1.3272128334714093, -0.3802108879412366, -0.1003607696463202,
					-0.0241848600268417, 0.9544506228550125, 0.0807358585208939,
					-0.0239585810555497, -0.0409736005706461, 1.4076563728858242);
	}
	if (mode == 2)
	{
		// PVM_20M2U/D93 (JP) -> sRGB(D65)
		return mat3(0.9241392201737613, -0.0690701363506526, -0.0084279079392561,
					0.0231962420140721, 0.9729221778325590, 0.0148832015024337,
					-0.0054875731998806, 0.0098220960740544, 1.3383896683854550);
	}
	if (mode == 3)
	{
		// BT601_525/D93 (JP) -> sRGB(D65)
		return mat3(0.7822490684754086, 0.0506168576073398, 0.0137752498011040,
					0.0147968947158740, 0.9741745313046067, 0.0220301953285839,
					-0.0013501205469208, -0.0044076726925543, 1.3484819844991036);
	}
	if (mode == 4)
	{
		// BT601_525/D65 (US) -> sRGB(D65)
		return mat3(0.9395420637732393, 0.0501813568598678, 0.0102765793668928,
					0.0177722231435608, 0.9657928624969044, 0.0164349143595347,
					-0.0016215999431855, -0.0043697496597356, 1.0059913496029214);
	}
	if (mode == 5)
	{
		// BT601_625/D65 (EUR, AUS) -> sRGB(D65)
		return mat3(1.0440432087628346, -0.0440432087628348, 0.0000000000000001,
					0.0000000000000000, 1.0000000000000002, 0.0000000000000000,
					0.0000000000000000, 0.0117933782840052, 0.9882066217159947);
	}
	return mat3(1.0);
}

vec3 ApplyCrtColour(vec3 colour)
{
	if (pc.crtColors <= 0)
		return colour;

	// The pow() and the sRGB curves are only defined on [0, 1]; the composite is an 8-bit UNORM
	// image, so clamping here cannot lose anything a real signal could carry.
	vec3 linear = clamp(colour, 0.0, 1.0);
	if (pc.crtColors == 1)
		linear = pow(linear, vec3(2.5));
	else if (pc.crtColors == 2)
		linear = pow(linear, vec3(2.25));
	else
		linear = SrgbDecode(linear);

	linear *= CrtColourMatrix(pc.crtColors);
	return SrgbEncode(linear);
}

void main()
{
	vec2 texel = 1.0 / pc.resolution;
	vec2 pixel = vUv * pc.resolution;

	vec3 colour = pc.fxaaEnabled > 0.0 ? ApplyFxaa(vUv, texel) : Fetch(vUv);

	// Tube colour response, before the beam and mask stages below. See ApplyCrtColour.
	colour = ApplyCrtColour(colour);

	// The glow is taken from the pre-mask image. Physically the halo originates in the lit phosphor
	// and then spreads through the glass, so adding it after the mask is both the right order and
	// the one that keeps it visible.
	vec3 glow = (pc.glowStrength > 0.0 && pc.glowLevels > 0) ? ComputeGlow(vUv) : vec3(0.0);

	colour = ApplyCrt(pixel, colour);
	colour += glow * pc.glowStrength;
	colour *= pc.gain;

	// Soft highlight rolloff, instead of letting the gain clip.
	//
	// The gain is there to win back the light the mask and scanlines remove, but applying it as a
	// plain multiply pushes whatever was already bright past white and clamps it flat, and that is
	// what reads as overexposure: the average brightness can be perfectly reasonable while the
	// highlights turn into blown-out patches. A real tube does not do that either - its beam current
	// saturates gradually, so highlights compress rather than disappear.
	//
	// Below the knee the curve is the identity; above it the excess is compressed so that it
	// approaches white asymptotically and can never pass it. The two halves meet with a slope of
	// exactly 1 at the knee, so there is no visible crease at the transition, and the curve is
	// monotonic, so no highlight detail is discarded - bright and brighter stay distinguishable.
	if (pc.gain > 1.0)
	{
		const float knee = 0.72;
		const float span = 1.0 - knee;
		vec3 over = max(colour - knee, vec3(0.0));
		vec3 under = min(colour, vec3(knee));
		colour = under + span * (over / (over + span));
	}

	outColour = vec4(colour, 1.0);
}
