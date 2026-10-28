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
