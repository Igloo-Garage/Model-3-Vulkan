/*
 * Model 3-Vulkan - fog.vert
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

// Full-screen quad for the ambient / scroll fog background pass. The fog is
// evaluated per fragment (spotlight on fog needs gl_FragCoord), so this shader
// only has to forward the screen position.
layout(location = 0) out vec2 vScreen;

void main()
{
	vec2 pos[4] = vec2[4](
		vec2(-1.0, -1.0),
		vec2(-1.0,  1.0),
		vec2( 1.0, -1.0),
		vec2( 1.0,  1.0));
	gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
	vScreen = pos[gl_VertexIndex];
}
