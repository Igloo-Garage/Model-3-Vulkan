/*
 * Model 3-Vulkan - scene_composite.frag
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
#extension GL_GOOGLE_include_directive : require

/*
 * Subpass 1 of the scene render pass, single-sampled variant: fold Real3D's two transparency
 * targets into the colour target.
 *
 * This is New3D's R3DFrameBuffers::Draw() moved inside the render pass. Reading the two targets
 * through subpassLoad() instead of as separate textures means they never have to be stored to
 * memory, resolved to a single-sampled image, or sampled back: on a tile-based GPU they stay in the
 * tile, and on a desktop GPU they stay in cache. The colour target is not read at all - the pipeline
 * blends ONE / ONE_MINUS_SRC_ALPHA into it, and the scene shader stores a premultiplied colour, so
 * the fixed-function blender performs exactly the source-over the old composite shader computed by
 * hand.
 *
 * The early discard is the point of the exercise on a low-bandwidth part: most pixels of a Model 3
 * frame have no translucent fragment at all, and discarding before the blend keeps the colour
 * target's read-modify-write traffic off those pixels entirely.
 */
#define SCENE_COMPOSITE_MS 0
#include "scene_composite.glsl"
