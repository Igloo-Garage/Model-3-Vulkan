#version 450

/*
 * Sinden light-gun border.
 *
 * Four solid quads around the edge of the output, generated from gl_VertexIndex: no vertex buffer,
 * no descriptor set, just the output size and the border width in the push constant. The
 * coordinate convention matches crosshair.vert (window pixels, origin top left), which is the
 * space the caller knows; the border is deliberately drawn in swapchain coordinates so neither
 * the CRT filter nor a cabinet backdrop can move or soften it.
 */

layout(push_constant) uniform PushConstants
{
    vec2 extent;        // output size, pixels
    float thickness;    // border width, pixels
    float brightness;   // 0..1
} pc;

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0));

    // 0 top, 1 bottom, 2 left, 3 right; six vertices each.
    const int edge = gl_VertexIndex / 6;
    const int index = gl_VertexIndex % 6;

    const float t = pc.thickness;
    vec2 lo;
    vec2 hi;

    if (edge == 0)      { lo = vec2(0.0, 0.0);             hi = vec2(pc.extent.x, t); }
    else if (edge == 1) { lo = vec2(0.0, pc.extent.y - t); hi = vec2(pc.extent.x, pc.extent.y); }
    else if (edge == 2) { lo = vec2(0.0, t);               hi = vec2(t, pc.extent.y - t); }
    else                { lo = vec2(pc.extent.x - t, t);   hi = vec2(pc.extent.x, pc.extent.y - t); }

    const vec2 pixel = mix(lo, hi, corners[index]);
    gl_Position = vec4(pixel / pc.extent * 2.0 - 1.0, 0.0, 1.0);
}
