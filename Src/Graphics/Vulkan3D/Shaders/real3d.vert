#version 450

/*
 * Pull-model vertex stage.
 *
 * Real3D rasterises quads. Splitting one into two triangles and letting the
 * rasteriser interpolate across each of them produces a crease along the shared
 * diagonal, so this backend does what the hardware does instead: four vertices
 * describe the primitive and every interpolated quantity is recovered from the
 * quad's own bilinear parametrisation in the fragment shader.
 *
 * There is therefore no vertex input at all. This stage reads the single corner
 * it owns out of a storage buffer, computes gl_Position, and publishes that
 * corner's interpolatable attributes (normal, uv, shade) together with the
 * polygon-wide ones (face normal, texture NP, colour) as varyings.
 *
 * The exact path in the fragment stage ignores every varying except vView: it
 * pulls all four corners and evaluates them at the quad's own (s, t), which is
 * what removes the diagonal crease. The fast path (ubo.renderOptions.x == 0)
 * takes the varyings instead, so it needs no per-pixel storage-buffer reads and
 * no quadratic solve - at the cost of the rasteriser's two-triangle interpolation.
 * Both paths are fed from the same fetch below, so they cannot disagree about
 * which corner is which.
 *
 * Each primitive occupies exactly four consecutive slots (a triangle repeats its
 * last vertex), so `gl_VertexIndex & ~3` is the first slot of the primitive and
 * the index buffer's values (0..3) are relative to it. gl_VertexIndex already
 * includes the draw's base vertex, so this holds whatever primitive is being
 * drawn.
 */

struct GpuQuadVertex
{
	vec4 pos;			// xyz = model-space position, w unused
	vec4 normal;		// xyz = vertex normal, w = textureNP
	vec4 uvShade;		// xy = texture coordinates, z = fixed shade
	vec4 face;			// xyz = polygon normal, w = colour bits (see real3d.frag)
};

layout(std430, set = 0, binding = 4) readonly buffer RomVertices
{
	GpuQuadVertex v[];
} romVerts;

layout(std430, set = 0, binding = 5) readonly buffer RamVertices
{
	GpuQuadVertex v[];
} ramVerts;

// 0 = ROM geometry, 1 = polygon-RAM geometry. A specialization constant rather
// than a uniform so the unused buffer is compiled out of the pipeline entirely.
layout(constant_id = 0) const int kVertexSource = 0;

// The exact path's handle on the four corner records. Flat, so every fragment
// of the quad reads the same base.
layout(location = 0) flat out uint vQuadBase;
// The view-space position, interpolated perspective-correct and at the centroid
// under MSAA. The fragment shader uses it as the eye ray for the pixel, which is
// what makes the quad solve independent of how the quad was split into triangles.
// The fast path also uses it as the shaded point.
layout(location = 1) centroid out vec3 vView;

// Fast path attributes. These are the standard hardware interpolants the exact
// path deliberately does without; publishing them costs a few components of
// shader I/O, and saves four 64-byte storage-buffer reads per pixel when
// quad interpolation is switched off.
layout(location = 2) centroid out vec3 vNormal;
layout(location = 3) centroid out vec3 vUvShade;
layout(location = 4) flat out vec3 vFaceNormal;
layout(location = 5) flat out float vTextureNP;
layout(location = 6) flat out uint vColourBits;

layout(push_constant) uniform PushConstants
{
	mat4 modelView;
	mat4 projection;
	vec4 lightDir;
	vec4 lightParams;
	vec4 fogColour;
	vec4 fogParams;
	vec4 extra;
} pc;

void main()
{
	uint index = uint(gl_VertexIndex);
	vQuadBase = index & ~3u;

	GpuQuadVertex vertex;
	if (kVertexSource != 0)
		vertex = ramVerts.v[index];
	else
		vertex = romVerts.v[index];

	vec4 view = pc.modelView * vec4(vertex.pos.xyz, 1.0);
	vView = view.xyz;

	vNormal = vertex.normal.xyz;
	vUvShade = vertex.uvShade.xyz;
	vFaceNormal = vertex.face.xyz;
	vTextureNP = vertex.normal.w;
	vColourBits = floatBitsToUint(vertex.face.w);

	gl_Position = pc.projection * view;
}
