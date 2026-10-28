#version 450

/*
 * A light-gun crosshair, generated from gl_VertexIndex.
 *
 * This is the four inward-pointing triangles the in-game overlay used to record into an ImGui
 * foreground draw list, moved into a pipeline of its own so that a crosshair-only frame does not
 * have to build an ImGui frame at all. The proportions are the ones the draw list used, and the
 * push constant carries the same scale the overlay applied, so the result is pixel-for-pixel the
 * same shape.
 *
 * Positions arrive in window pixels with the origin at the top left, which is the coordinate space
 * the caller computes them in, and are converted here to Vulkan's clip space. There is no y flip:
 * Vulkan's framebuffer y axis points down, the same way the caller's does.
 */

layout(location = 0) out vec4 vColour;

layout(push_constant) uniform PushConstants
{
    vec2 centre;    // crosshair centre, window pixels
    vec2 extent;    // output size, pixels
    vec4 colour;    // 16-byte aligned, so it follows the two vec2s
    float scale;    // pixels per shape unit
} pc;

void main()
{
    const float dist = 2.0;
    const float height = 9.6;
    const float base = 4.8;

    // Four triangles, clockwise from the top one. Two vertices are shared in value but not in
    // index, because the topology is a plain triangle list.
    const vec2 corners[12] = vec2[12](
        vec2(0.0, -dist), vec2(-base * 0.5, -dist - height), vec2(base * 0.5, -dist - height),
        vec2(0.0, dist), vec2(-base * 0.5, dist + height), vec2(base * 0.5, dist + height),
        vec2(-dist, 0.0), vec2(-dist - height, -base * 0.5), vec2(-dist - height, base * 0.5),
        vec2(dist, 0.0), vec2(dist + height, -base * 0.5), vec2(dist + height, base * 0.5));

    const vec2 pixel = pc.centre + corners[gl_VertexIndex] * pc.scale;
    gl_Position = vec4(pixel / pc.extent * 2.0 - 1.0, 0.0, 1.0);
    vColour = pc.colour;
}
