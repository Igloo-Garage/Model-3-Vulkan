/*
 * Model 3-Vulkan - border.frag
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
 * Sinden light-gun border: a flat white rectangle at the requested brightness.
 *
 * The border exists so the Sinden camera can see the edge of the picture area; it has no texture
 * and no shading of its own. Brightness is pushed rather than hard-coded because the correct level
 * depends on the room and on the camera's exposure.
 */

layout(location = 0) out vec4 outColour;

layout(push_constant) uniform PushConstants
{
    vec2 extent;        // output size, pixels
    float thickness;    // border width, pixels
    float brightness;   // 0..1
} pc;

void main()
{
    outColour = vec4(vec3(pc.brightness), 1.0);
}
