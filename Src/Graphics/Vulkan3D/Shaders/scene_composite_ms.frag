/*
 * Model 3-Vulkan - scene_composite_ms.frag
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
 * Subpass 1 of the scene render pass, multisampled variant.
 *
 * The pipeline runs once per sample (sampleShadingEnable, see CreateSceneCompositePipeline), so each
 * invocation reads the transparency value of the sample it is shading and the blender composites
 * into that sample. The colour target is resolved to a single-sampled image by the render pass's
 * resolve attachment after the subpass, which is where the antialiasing of both the opaque scene and
 * the transparency edges comes from.
 *
 * See scene_composite.frag for what the body does and why.
 */
#define SCENE_COMPOSITE_MS 1
#include "scene_composite.glsl"
