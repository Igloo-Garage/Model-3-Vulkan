#version 450

/*
 * Full-screen triangle with no vertex input, for the OIT resolve.
 *
 * The resolve indexes the head image with gl_FragCoord, so there is nothing to interpolate; the
 * viewport is set to the whole scene target before the draw.
 */
void main()
{
	vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
