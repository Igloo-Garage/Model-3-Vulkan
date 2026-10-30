/*
 * Model 3-Vulkan - real3d.frag
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
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

/*
 * Scene fragment stage: the shared shading body plus the three-target dispatch.
 *
 * Everything that computes the shaded colour lives in real3d_common.glsl, because the
 * sorted-transparency capture stage (real3d_oit.frag) computes exactly the same colour and
 * only differs in what it does with it. Keeping one copy is what makes it impossible for
 * the two paths to drift apart in shading, fog or discard rules - the previous two-layer
 * code and the OIT path would otherwise have to be kept in step by hand.
 */

#include "real3d_common.glsl"

/*
 * The three scene targets, in the same arrangement as New3D's R3DFrameBuffers.
 *
 * The colour layer writes zero into both transparency targets: that is what erases a translucent
 * surface which a nearer opaque polygon covers, and it is the only thing that makes the layers work
 * across priority passes. New3D gets it for free because all three are attachments of one FBO; here
 * they are separate images, but every scene pass binds all three and the pipeline's colour write
 * mask selects which one the layer actually lands in.
 *
 * extra.z carries the layer: 0 = colour, 1 = trans1, 2 = trans2.
 */
layout(location = 0) out vec4 outColour;
layout(location = 1) out vec4 outTrans1;
layout(location = 2) out vec4 outTrans2;

void main()
{
	/*
	 * Arcade checkerboard transparency (AlphaSortMode = 2).
	 *
	 * The real board has no full-resolution translucent pass: each of the two transparency
	 * layers covers half of the framebuffer in a checkerboard, one on the even cells and the
	 * other on the odd ones. Reproducing that here means the fragment is dropped (not blended)
	 * wherever the pixel belongs to the other layer's half, which is what leaves the other half
	 * of the screen showing the opaque surface behind the effect.
	 *
	 * The mask is evaluated before ShadeFragment() so a dropped fragment costs almost nothing;
	 * a busy effect (Sega Rally 2's snow) covers most of the screen, and half of those fragments
	 * are now free.
	 *
	 * Pixel parity, not arcade-pixel parity: at the native 496x384 target the two are the same
	 * thing, and on a 4K output a one-pixel checkerboard averages into a plain half-strength
	 * blend at normal viewing distance, which is the point. A blocky pattern at 4K would read as
	 * a screen-door artefact instead.
	 */
	if (ubo.renderOptions.y > 0.5 && pc.extra.z > 0.5)
	{
		const int parity = (int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 1;
		// Attachment 1 is trans1 and owns the even cells; attachment 2 is trans2 and owns the odd ones.
		const int wanted = (pc.extra.z < 1.5) ? 0 : 1;
		if (parity != wanted)
			discard;
	}

	vec4 colour = ShadeFragment();

	// Real3D writes a 1/w depth so nearer fragments compare greater.
	//
	// Deliberately the rasteriser's 1/w and not the value implied by the quad solve:
	// the depth buffer has to agree with the surface that was actually rasterised,
	// or a decal sitting on a surface would z-fight against the quad underneath it.
	// The two agree exactly for planar quads and differ only inside a twisted one.
	gl_FragDepth = pc.projection[3][2] * gl_FragCoord.w;

	// Same dispatch as New3D's WriteOutputs(): colour zeroes both transparency layers, each
	// transparency layer writes only itself. The pipeline's colour write mask drops the others.
	vec4 blank = vec4(0.0);
	if (pc.extra.z < 0.5)
	{
		outColour = colour;
		outTrans1 = blank;
		outTrans2 = blank;
	}
	else if (pc.extra.z < 1.5)
	{
		outColour = blank;
		outTrans1 = colour;
		outTrans2 = blank;
	}
	else
	{
		outColour = blank;
		outTrans1 = blank;
		outTrans2 = colour;
	}
}
