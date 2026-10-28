#version 450

/*
 * Full-screen triangle for the scene composite subpass.
 *
 * The composite reads the two Real3D transparency targets through subpassLoad(), which is addressed
 * by fragment coordinate, so there is nothing to interpolate. The viewport is set to the whole scene
 * target before the draw.
 */
void main()
{
	vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
