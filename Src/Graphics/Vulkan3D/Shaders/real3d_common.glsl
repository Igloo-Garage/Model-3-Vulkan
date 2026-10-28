
/*
 * Pull-model fragment stage with Real3D quad interpolation.
 *
 * The primitive arrives as four corners in a storage buffer, never as a triangle's
 * interpolation. This stage solves for the quad's own (s, t) at this pixel and
 * evaluates every per-vertex quantity bilinearly there, which is what removes the
 * crease a triangle split puts down the 0-2 diagonal: texture coordinates, normals
 * and shading vary smoothly across the whole quad instead of changing formula at
 * the seam.
 *
 * The solve is a ray/quadric intersection in view space (see QuadParams), so it
 * needs the four corners, and that is why they are pulled rather than passed as
 * varyings: four corners of uv, normal, shade and view position would be about
 * forty components of shader I/O. Instead the vertex stage publishes the view-space
 * ray and the quad's base index, and this stage reads four 64-byte records -
 * contiguous, and already in the cache because the vertex stage just touched them.
 *
 * The pull and the solve can be switched off per session (ubo.renderOptions.x == 0):
 * the vertex stage also publishes hardware-interpolated uv, normal and shade, and
 * the fast path uses those instead. That is the low-end preset's trade - no storage
 * reads and no quadratic solve per pixel, at the cost of a visible diagonal crease
 * on quads that are not planar.
 */

struct GpuQuadVertex
{
	vec4 pos;			// xyz = model-space position, w unused
	vec4 normal;		// xyz = vertex normal, w = textureNP
	vec4 uvShade;		// xy = texture coordinates, z = fixed shade
	vec4 face;			// xyz = polygon normal, w = colour bits
};

layout(std430, set = 0, binding = 4) readonly buffer RomVertices
{
	GpuQuadVertex v[];
} romVerts;

layout(std430, set = 0, binding = 5) readonly buffer RamVertices
{
	GpuQuadVertex v[];
} ramVerts;

// 0 = ROM geometry, 1 = polygon-RAM geometry; see real3d.vert.
layout(constant_id = 0) const int kVertexSource = 0;

// 1 = the sorted-transparency capture stage, where the layered-dust softening is
// enabled. The normal two-layer pipelines leave this at 0, so their output stays
// bit-exact and the shared body still has only one copy.
layout(constant_id = 1) const int kSoftEffects = 0;

// The first of this primitive's four vertex slots, from the vertex stage. Flat, so
// every fragment of the quad reads the same records.
layout(location = 0) flat in uint vQuadBase;
// View-space position at this fragment: the eye ray the quad solve is performed on.
layout(location = 1) centroid in vec3 vView;

// Hardware-interpolated corner data for the fast path. The exact path below does
// not read these: it pulls the four records and evaluates them at the quad's own
// (s, t) instead, which is what keeps the diagonal seam out of the picture.
layout(location = 2) centroid in vec3 vNormal;
layout(location = 3) centroid in vec3 vUvShade;
layout(location = 4) flat in vec3 vFaceNormal;
layout(location = 5) flat in float vTextureNP;
layout(location = 6) flat in uint vColourBits;

/*
 * The three scene targets, in the same arrangement as New3D's R3DFrameBuffers.
 *
 * The colour layer writes zero into both transparency targets: that is what erases a translucent
 * surface which a nearer opaque polygon covers, and it is the only thing that makes the layers work
 * across priority passes. New3D gets it for free because all three are attachments of one FBO; here
 * they are separate images, but every scene pass binds all three and the pipeline's colour write
 * mask selects which one the layer actually lands in.
 *
 * extra.z carries the layer: 0 = colour, 1 = trans1, 2 = trans2.
 */
struct R3DMaterial
{
	uint flags;
	int texX;
	int texY;
	int texWidth;
	int texHeight;
	int texFormat;
	int texturePage;
	int wrapModeX;
	int wrapModeY;
	int microTextureID;
	float microMinLOD;
	float shininess;
	float specularValue;
	float fogIntensity;
	int hdTexIndex;		// bindless slot of an HD replacement, or -1 to sample the packed page
	float pad1;
};

layout(set = 0, binding = 0) uniform sampler uSamplers[16];
layout(set = 0, binding = 1) uniform ViewportUBO
{
	vec4 spotEllipse;
	vec4 spotRange;
	vec4 spotColor;
	vec4 spotFogColor;
	// x = texture filtering mode (0 = radial hardware LOD, 1 = derivative LOD,
	// 2 = derivative LOD with anisotropic taps), y = maximum taps. See GetTextureValue.
	vec4 texFilter;
	// x = quad interpolation: 1 = exact four-corner solve (default), 0 = the fast
	// hardware-triangle path. yzw unused. See ShadeFragment.
	vec4 renderOptions;
} ubo;
layout(std430, set = 0, binding = 2) readonly buffer Materials
{
	R3DMaterial m[];
} materials;
// The two Real3D texture pages, exactly as they exist in hardware: 2048x1024
// 16-bit texels with the mip pyramid of each sheet packed into the same image.
// This is the only texture source the fragment shader samples, just like New3D.
layout(set = 0, binding = 3) uniform usampler2D uPages[2];

/*
 * HD replacement textures, indexed by R3DMaterial.hdTexIndex.
 *
 * A runtime array rather than one binding per texture: the pack is chosen by the
 * user at run time and can hold hundreds of images, and every one of them would
 * otherwise need its own descriptor. The binding is partially bound, so the
 * slots a pack does not fill are never read - a material with hdTexIndex < 0 takes
 * the original packed-page path below instead of sampling slot 0.
 *
 * nonuniformEXT because the index comes out of a storage buffer and the compiler
 * cannot prove it uniform, even though it is constant across a draw.
 */
layout(set = 2, binding = 0) uniform sampler2D u_hdTextures[];

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

const uint kTextured     = 1u << 0;
const uint kAlphaTest    = 1u << 1;
const uint kTextureAlpha = 1u << 2;
const uint kLighting     = 1u << 3;
const uint kFixedShade   = 1u << 4;
const uint kSpecular     = 1u << 5;
const uint kTranslator   = 1u << 6;
const uint kInverted     = 1u << 7;
const uint kSmooth       = 1u << 8;
const uint kMicro        = 1u << 10;
const uint kLayeredPoly  = 1u << 11;

// srally2's dust is the one Model 3 effect with this state: LAY=1, no lighting, no texture
// alpha, and textured. "Textured" is required so the *untextured* layered decal under the car
// (flags 0x300 - the soft shadow / skid mark mesh) is left alone: both the OIT layered dedup and
// the softening only make sense for the actual dust sprites, and touching the shadow draws creases
// across the road.
bool IsDustEffect(uint flags)
{
	return (flags & kLayeredPoly) != 0u &&
		(flags & kLighting) == 0u &&
		(flags & kTextureAlpha) == 0u &&
		(flags & kTextured) != 0u;
}

float Sqr(float a) { return a * a; }

// ---------------------------------------------------------------------------
// Quad interpolation
// ---------------------------------------------------------------------------

GpuQuadVertex FetchVertex(uint index)
{
	GpuQuadVertex v;
	if (kVertexSource != 0)
		v = ramVerts.v[index];
	else
		v = romVerts.v[index];
	return v;
}

/*
 * 2D cross product inside the plane perpendicular to the ray I.
 *
 * (a x I) and (b x I) both lie in that plane, and the scalar cross product of the
 * two planar vectors is the triple product a . (b x I), scaled by |I|. Every term
 * below carries the same scale, so it cancels and is never divided out.
 */
float PlaneCross(vec3 a, vec3 b, vec3 I)
{
	return dot(a, cross(b, I));
}

/*
 * How plausible a (s, t) candidate is: outside the unit square first, then depth.
 *
 * A candidate outside the quad always loses to one inside it, however close the
 * depth comparison happens to be.
 */
float QuadScore(vec3 v0, vec3 e, vec3 f, vec3 g, vec3 I, vec2 st)
{
	vec3 P = v0 + st.x * e + st.y * f + (st.x * st.y) * g;
	// |P| should equal |I|: both are points on the same ray.
	float depthError = abs(dot(P, I) / max(dot(I, I), 1e-20) - 1.0);
	float outside = max(max(-st.x, st.x - 1.0), max(-st.y, st.y - 1.0));
	return (outside > 0.0 ? 1.0 + min(outside, 1.0) : 0.0) + min(depthError, 1.0) * 0.5;
}

/*
 * Solve P(s, t) = the point this pixel's ray meets, for
 *
 *     P(s, t) = v0 + s e + t f + s t g
 *     e = v1 - v0,  f = v3 - v0,  g = v0 - v1 + v2 - v3
 *
 * The eye is at the origin in view space, so the ray through this fragment is
 * simply the line through I = vView. Requiring P to be parallel to I and crossing
 * the whole equation with I projects it into the plane perpendicular to the ray,
 * where it is a bilinear system in (s, t). Eliminating t leaves
 *
 *     k2 s^2 + k1 s + k0 = 0,  k2 = q1 x q3,  k1 = q0 x q3 + q1 x q2,  k0 = q0 x q2
 *
 * Why the solve has to be written carefully:
 *
 *   - A parallelogram has g == 0, so k2 vanishes identically and the textbook
 *     quadratic formula divides by zero. A near-parallelogram has a k2 small
 *     enough that the two roots lose most of their digits. Both take the linear
 *     branch below, which is exact for the first and well conditioned for the
 *     second.
 *   - A triangle arrives with v3 == v2, which makes g == -e; k2 is then a triple
 *     product with a repeated vector and the same linear branch applies. Nothing
 *     special has to be done for triangles at all - and no branch is taken on the
 *     primitive type, so a wavefront holding both costs nothing extra.
 *   - The quadratic's two roots are not "one real and one spurious". A line meets a
 *     bilinear patch at up to two points, so both are genuine intersections of the
 *     ray with the surface. They are told apart by which lands inside the unit
 *     square, and, when both do (a folded quad seen edge-on), by which is at the
 *     depth the rasteriser placed this fragment at.
 *
 * The result is the *view-space* parametrisation of the quad, not a barycentric
 * split of it, so it is continuous across the diagonal for any primitive shape.
 */
vec2 QuadParams(vec3 v0, vec3 v1, vec3 v2, vec3 v3, vec3 I)
{
	const float kTiny = 1e-20;

	vec3 e = v1 - v0;
	vec3 f = v3 - v0;
	vec3 g = v0 - v1 + v2 - v3;

	float k2 = PlaneCross(e, g, I);
	float k1 = PlaneCross(v0, g, I) + PlaneCross(e, f, I);
	float k0 = PlaneCross(v0, f, I);

	// Two candidates in s. The linear case fills both with the same value, which
	// keeps the selection below free of a second code path.
	float sA;
	float sB;
	if (abs(k2) > 1e-6 * (abs(k1) + abs(k0) + kTiny))
	{
		float disc = max(k1 * k1 - 4.0 * k0 * k2, 0.0);
		float root = sqrt(disc);
		// Citardauq form. Computing the root that is near zero as (-k1 -/+ root)/(2 k2)
		// subtracts two numbers that nearly cancel and then divides the error by a
		// small k2; adding the sign-matched root to k1 first avoids both.
		float q = -0.5 * (k1 + (k1 < 0.0 ? -root : root));
		sA = (k2 != 0.0) ? q / k2 : 0.0;
		sB = (q != 0.0) ? k0 / q : sA;
	}
	else
	{
		sA = (abs(k1) > kTiny) ? -k0 / k1 : 0.0;
		sB = sA;
	}

	// t follows from the same planar equation crossed with q1 this time.
	float tNumerator = -PlaneCross(v0, e, I);
	float tBase = PlaneCross(f, e, I);
	float tSlope = PlaneCross(g, e, I);

	float denA = tBase + sA * tSlope;
	vec2 stA = vec2(sA, (abs(denA) > kTiny) ? tNumerator / denA : 0.0);
	float denB = tBase + sB * tSlope;
	vec2 stB = vec2(sB, (abs(denB) > kTiny) ? tNumerator / denB : 0.0);

	// Inside the unit square first; between two that are, the depth the rasteriser
	// chose decides. A NaN candidate loses every comparison, which is the intended
	// outcome - it came from a degenerate solve.
	float scoreA = QuadScore(v0, e, f, g, I, stA);
	float scoreB = QuadScore(v0, e, f, g, I, stB);
	vec2 st = (scoreA <= scoreB) ? stA : stB;

	// Fragments at a quad's silhouette can be shaded from a centroid just outside
	// it under MSAA. Clamping keeps the texture coordinate inside its rectangle,
	// which is what the old centroid varyings bought: sampling past the edge of a
	// base rectangle lands on whatever texture is packed next to it.
	return clamp(st, 0.0, 1.0);
}

/*
 * Bilinear weights for the four corners, in the order the vertices are stored:
 * (1-s)(1-t), s(1-t), st, (1-s)t.
 */
vec4 QuadWeights(vec2 st)
{
	return vec4((1.0 - st.x) * (1.0 - st.y), st.x * (1.0 - st.y), st.x * st.y, (1.0 - st.x) * st.y);
}

// ---------------------------------------------------------------------------
// Texture decode / fetch
//
// The code below is a direct port of New3D's R3DShaderCommon.h so that the
// Vulkan backend reproduces the Real3D hardware exactly: palette formats are
// unpacked in the shader, the mip pyramid is addressed by shifting the base
// position (r3d only packs mips down to 2:2), wrapping masks against the mip
// size, and bilinear filtering is computed manually with wrap-aware taps.
// ---------------------------------------------------------------------------

vec4 ExtractColour(int type, uint value)
{
	vec4 c = vec4(0.0);

	if (type == 0) {			// T1RGB5
		c.r = float((value >> 10) & 0x1Fu);
		c.g = float((value >> 5) & 0x1Fu);
		c.b = float((value) & 0x1Fu);
		c.rgb *= (1.0 / 31.0);
		c.a = 1.0 - float((value >> 15) & 0x1u);
	}
	else if (type == 1) {		// interleaved A4L4 (low byte)
		c.rgb = vec3(float(value & 0xFu));
		c.a = float((value >> 4) & 0xFu);
		c *= (1.0 / 15.0);
	}
	else if (type == 2) {
		c.a = float(value & 0xFu);
		c.rgb = vec3(float((value >> 4) & 0xFu));
		c *= (1.0 / 15.0);
	}
	else if (type == 3) {
		c.rgb = vec3(float((value >> 8) & 0xFu));
		c.a = float((value >> 12) & 0xFu);
		c *= (1.0 / 15.0);
	}
	else if (type == 4) {
		c.a = float((value >> 8) & 0xFu);
		c.rgb = vec3(float((value >> 12) & 0xFu));
		c *= (1.0 / 15.0);
	}
	else if (type == 5) {
		c = vec4(float(value & 0xFFu) / 255.0);
		c.a = (c.a == 1.0) ? 0.0 : 1.0;
	}
	else if (type == 6) {
		c = vec4(float((value >> 8) & 0xFFu) / 255.0);
		c.a = (c.a == 1.0) ? 0.0 : 1.0;
	}
	else if (type == 7) {		// RGBA4
		c.r = float((value >> 12) & 0xFu);
		c.g = float((value >> 8) & 0xFu);
		c.b = float((value >> 4) & 0xFu);
		c.a = float((value >> 0) & 0xFu);
		c *= (1.0 / 15.0);
	}
	else if (type == 8) {		// low byte, low nibble
		c = vec4(float(value & 0xFu) / 15.0);
		c.a = (c.a == 1.0) ? 0.0 : 1.0;
	}
	else if (type == 9) {		// low byte, high nibble
		c = vec4(float((value >> 4) & 0xFu) / 15.0);
		c.a = (c.a == 1.0) ? 0.0 : 1.0;
	}
	else if (type == 10) {		// high byte, low nibble
		c = vec4(float((value >> 8) & 0xFu) / 15.0);
		c.a = (c.a == 1.0) ? 0.0 : 1.0;
	}
	else if (type == 11) {		// high byte, high nibble
		c = vec4(float((value >> 12) & 0xFu) / 15.0);
		c.a = (c.a == 1.0) ? 0.0 : 1.0;
	}

	return c;
}

// Real3D stores each mip level of a texture inside the packed texture page at a
// fixed base offset (the same tables New3D uses in TextureBank::UploadTextures).
// All levels therefore read from the single physical page; only the base offset
// changes. New3D gets this for free because it uploads each level into the
// matching GL mip level, but our Vulkan page is a plain RAM copy with one level,
// so we add the offset ourselves and always fetch level 0.
const int kMipX[12] = int[](0, 1024, 1536, 1792, 1920, 1984, 2016, 2032, 2040, 2044, 2046, 2047);
const int kMipY[12] = int[](0, 512, 768, 896, 960, 992, 1008, 1016, 1020, 1022, 1023, 1023);

uint FetchTexel(int page, ivec2 coordinate, int level)
{
	ivec2 addr = coordinate + ivec2(kMipX[level], kMipY[level]);

	return texelFetch(uPages[nonuniformEXT(page)], addr, 0).r;
}

// Wrap around is a good guess for reads outside the texture sheet.
ivec2 WrapTexCoords(ivec2 pos, ivec2 coordinate, int level)
{
	ivec2 newCoord;
	newCoord.x = coordinate.x & (2047 >> level);
	newCoord.y = coordinate.y & (1023 >> level);

	return newCoord;
}

ivec2 GetTextureSize(int level, ivec2 size)
{
	int mipDivisor = 1 << level;

	return size / mipDivisor;
}

ivec2 GetTexturePosition(int level, ivec2 pos)
{
	int mipDivisor = 1 << level;

	ivec2 retPos;
	retPos.x = pos.x / mipDivisor;
	retPos.y = pos.y / mipDivisor;

	return retPos;
}

ivec2 GetMicroTexturePos(int id)
{
	const int xCoords[8] = int[](0, 0, 128, 128, 0, 0, 128, 128);
	const int yCoords[8] = int[](0, 128, 0, 128, 256, 384, 256, 384);

	return ivec2(xCoords[id], yCoords[id]);
}

float LinearTexLocations(int wrapMode, float size, float u, out float u0, out float u1)
{
	float texelSize		= 1.0 / size;
	float halfTexelSize	= 0.5 / size;

	if (wrapMode == 0) {						// repeat
		u	= u * size - 0.5;
		u0	= (floor(u) + 0.5) / size;			// + 0.5 offset added to push us into the centre of a pixel, without we'll get rounding errors
		u0	= fract(u0);
		u1	= u0 + texelSize;
		u1	= fract(u1);

		return fract(u);						// return weight
	}
	else if (wrapMode == 1) {					// repeat + clamp
		u	= fract(u);							// must force into 0-1 to start
		u	= u * size - 0.5;
		u0	= (floor(u) + 0.5) / size;
		u1	= u0 + texelSize;

		if (u0 < 0.0)	u0 = 0.0;
		if (u1 >= 1.0)	u1 = 1.0 - halfTexelSize;

		return fract(u);
	}
	else {										// mirror + mirror clamp - both are the same since the edge pixels are repeated anyway
		float odd = floor(mod(u, 2.0));			// odd values are mirrored

		if (odd > 0.0) {
			u = 1.0 - fract(u);
		}
		else {
			u = fract(u);
		}

		u	= u * size - 0.5;
		u0	= (floor(u) + 0.5) / size;
		u1	= u0 + texelSize;

		if (u0 < 0.0)	u0 = 0.0;
		if (u1 >= 1.0)	u1 = 1.0 - halfTexelSize;

		return fract(u);
	}
}

vec4 texBiLinear(int page, ivec2 wrapMode, vec2 texSize, ivec2 texPos,
	vec2 texCoord, int level, int baseTexType, bool alphaTest)
{
	float tx[2], ty[2];
	float a = LinearTexLocations(wrapMode.s, texSize.x, texCoord.x, tx[0], tx[1]);
	float b = LinearTexLocations(wrapMode.t, texSize.y, texCoord.y, ty[0], ty[1]);

	vec4 p0q0 = ExtractColour(baseTexType, FetchTexel(page, WrapTexCoords(texPos, ivec2(vec2(tx[0], ty[0]) * texSize + texPos), level), level));
	vec4 p1q0 = ExtractColour(baseTexType, FetchTexel(page, WrapTexCoords(texPos, ivec2(vec2(tx[1], ty[0]) * texSize + texPos), level), level));
	vec4 p0q1 = ExtractColour(baseTexType, FetchTexel(page, WrapTexCoords(texPos, ivec2(vec2(tx[0], ty[1]) * texSize + texPos), level), level));
	vec4 p1q1 = ExtractColour(baseTexType, FetchTexel(page, WrapTexCoords(texPos, ivec2(vec2(tx[1], ty[1]) * texSize + texPos), level), level));

	if (alphaTest) {
		if (p0q0.a > p1q0.a)	{ p1q0.rgb = p0q0.rgb; }
		if (p0q0.a > p0q1.a)	{ p0q1.rgb = p0q0.rgb; }

		if (p1q0.a > p0q0.a)	{ p0q0.rgb = p1q0.rgb; }
		if (p1q0.a > p1q1.a)	{ p1q1.rgb = p1q0.rgb; }

		if (p0q1.a > p0q0.a)	{ p0q0.rgb = p0q1.rgb; }
		if (p0q1.a > p1q1.a)	{ p0q1.rgb = p1q1.rgb; }

		if (p1q1.a > p0q1.a)	{ p0q1.rgb = p1q1.rgb; }
		if (p1q1.a > p1q0.a)	{ p1q0.rgb = p1q1.rgb; }
	}

	vec4 pInterp_q0 = mix(p0q0, p1q0, a);	// interpolate top row in X direction
	vec4 pInterp_q1 = mix(p0q1, p1q1, a);	// interpolate bottom row in X direction

	return mix(pInterp_q0, pInterp_q1, b);	// interpolate in Y direction
}

/*
 * Screen-space footprint of one pixel, in texture space.
 *
 * The default mip level in this shader is New3D's radial estimate: the polygon's
 * texture-NP factor scaled by 1/w squared, isotropic by construction and blind to
 * the surface's orientation. That is what the real hardware did, which is why it
 * stays the default - but it means a road or a wall running away from the camera
 * is filtered as if it faced the camera, and the far part of it blurs more than
 * the pixel footprint requires.
 *
 * Where the optional mode is on, the level comes from the derivatives of the exact
 * per-pixel uv the quad solve produced. The footprint is two-dimensional, so it
 * also says *how* elongated the pixel is: the level follows the minor axis (that
 * is the axis that would alias) and the extra samples go along the major one.
 *
 * dFdxFine/dFdyFine rather than the coarse variants: coarse derivatives are shared
 * by a 2x2 group, which would throw away the per-pixel accuracy the solve just
 * produced.
 */
struct TexFootprint
{
	float major;		// major axis length, in base-level texels
	float minor;		// minor axis length, in base-level texels
	vec2  axis;		// unit major axis, in texture space (uv * size)
};

TexFootprint ComputeFootprint(vec2 uv, vec2 size)
{
	// Texels covered by one pixel, horizontally and vertically.
	vec2 dx = dFdxFine(uv) * size;
	vec2 dy = dFdyFine(uv) * size;

	// The footprint is the ellipse the unit screen square maps to under [dx dy];
	// its semi-axes are that matrix's singular values, which are the square roots
	// of the eigenvalues of [[a,c],[c,b]]. No eigenvector solver needed - this is
	// the 2x2 symmetric case, where the characteristic quadratic is explicit.
	float a = dot(dx, dx);
	float b = dot(dy, dy);
	float c = dot(dx, dy);
	float trace = a + b;
	float det = max(a * b - c * c, 0.0);
	float disc = sqrt(max(trace * trace - 4.0 * det, 0.0));
	float majorLen = sqrt(max(0.5 * (trace + disc), 1e-16));
	float minorLen = sqrt(max(0.5 * (trace - disc), 1e-16));

	// Eigenvector for the larger eigenvalue. The first form vanishes when the
	// larger axis is the horizontal one (b == majorLen^2 and c == 0), which is the
	// common axis-aligned case; the second covers that, and the third is a guard
	// for a footprint that is a scaled rotation, where every direction is equally
	// major.
	vec2 major = vec2(majorLen * majorLen - b, c);
	if (dot(major, major) < 1e-12)
		major = vec2(c, majorLen * majorLen - a);
	if (dot(major, major) < 1e-12)
		major = vec2(1.0, 0.0);

	TexFootprint fp;
	fp.major = majorLen;
	fp.minor = minorLen;
	fp.axis = normalize(major);
	return fp;
}

/*
 * texBiLinear, averaged over an elongated footprint.
 *
 * `stepUv` is the gap between adjacent taps in uv units and `taps` how many to
 * take; the taps are centred on texCoord. A round footprint yields one tap, so the
 * same call serves all three filtering modes.
 *
 * The taps are combined in premultiplied alpha and the colour is un-premultiplied
 * once on the way out. Averaging straight RGBA weights a transparent texel's colour
 * (usually black) the same as an opaque one's, which shows up as a dark fringe
 * along foliage and railings seen at an angle - the very edge the anisotropic path
 * exists to resolve better. The machine itself filters un-premultiplied, so this is
 * a deliberate improvement rather than a reproduction, and it only applies when
 * there is more than one tap: the single-tap path below is the original call.
 */
vec4 texBiLinearFootprint(int page, ivec2 wrapMode, vec2 texSize, ivec2 texPos, vec2 texCoord,
	int level, int baseTexType, bool alphaTest, vec2 stepUv, int taps)
{
	if (taps <= 1)
		return texBiLinear(page, wrapMode, texSize, texPos, texCoord, level, baseTexType, alphaTest);

	vec3 sumRgb = vec3(0.0);
	// Unweighted sum, kept only for the degenerate case below.
	vec3 plainRgb = vec3(0.0);
	float sumA = 0.0;

	for (int i = 0; i < taps; i++)
	{
		float offset = float(i) + 0.5 - 0.5 * float(taps);
		vec4 c = texBiLinear(page, wrapMode, texSize, texPos, texCoord + stepUv * offset, level, baseTexType, alphaTest);
		sumRgb += c.rgb * c.a;
		plainRgb += c.rgb;
		sumA += c.a;
	}

	float meanA = sumA * (1.0 / float(taps));
	/*
	 * A footprint whose texels are all fully transparent has no colour to
	 * un-premultiply.
	 *
	 * This is not a corner case to shrug at: a T1RGB5 texel carries its
	 * transparency in bit 15, and a material that does not ask for alpha (no
	 * kTextureAlpha, no kAlphaTest) still renders through it - the caller forces
	 * alpha to 1 afterwards and the texture's rgb is the colour. Dividing by
	 * sumA == 0 there turns the whole polygon black, which is exactly how a set
	 * of sky-facing quads in Daytona 2 started rendering as black blocks.
	 * The plain average is what the single-tap path would have produced.
	 */
	vec3 rgb = (sumA > 1e-5) ? (sumRgb / sumA) : (plainRgb * (1.0 / float(taps)));
	return vec4(rgb, meanA);
}

vec4 GetTextureValue(R3DMaterial mat, vec2 uv, float lodBase, float vertexAlpha, bool discardAlpha)
{
	bool alphaTest = (mat.flags & kAlphaTest) != 0u;

	/*
	 * HD replacement path.
	 *
	 * When the pack supplied an image for this sub-texture, none of the packed-page
	 * machinery below applies: the image already holds exactly the rectangle the
	 * original page would have been decoded from, at whatever resolution the author
	 * chose, with a full 8-bit alpha channel. `uv` is normalized to that same
	 * rectangle (the quad solve produced it from the mesh's own texture coordinates
	 * and the material's base size), so it maps 1:1 onto the replacement.
	 *
	 * textureGrad rather than texture(): the derivatives come from the exact per-pixel
	 * uv the quad solve produced, which keeps the mip choice correct on a surface
	 * seen at a grazing angle even when the replacement is 4x or 8x the source. The
	 * sampler does the anisotropic filtering; the microtexture branch below is
	 * deliberately skipped, because its repeating low-resolution grid would undo the
	 * detail the replacement is there to add.
	 *
	 * The derivatives are evaluated before the branch so they sit in uniform control
	 * flow. `mat` is the same for every fragment of a draw, but the compiler cannot
	 * prove that from a storage-buffer read.
	 */
	vec2 hdDx = dFdxFine(uv);
	vec2 hdDy = dFdyFine(uv);
	if (mat.hdTexIndex >= 0)
	{
		/*
		 * Wrap modes.
		 *
		 * The replacement image *is* the sub-texture rectangle (see the hash), and every
		 * sampler bound to the pack tiles it. Mode 0 and mode 1 therefore both map to
		 * VK_SAMPLER_ADDRESS_MODE_REPEAT, mode 2/3 to MIRRORED_REPEAT; the uv is passed
		 * through untouched and the sampler does the wrapping.
		 *
		 * This used to wrap mode 1 in the shader with fract() and pair it with a
		 * CLAMP_TO_EDGE sampler, to keep the machine's "repeat, then clamp at the
		 * sub-texture edges" arithmetic (LinearTexLocations). The problem is what
		 * clamping means for a *filter*: every tap of the footprint that lands past
		 * the tile boundary reads the edge texel, so the seam is not half a texel
		 * wide - it is as wide as the filter footprint. On a road running away from
		 * the camera that is a band per tile repetition, which is exactly how
		 * Daytona 2's road apron started showing evenly spaced horizontal stripes once
		 * its (uniform, pattern-free) 256x256 asphalt was replaced at 4x. A flat test
		 * image hid them because both sides of a seam were the same colour.
		 *
		 * Tiling seamlessly is also what the surface expects: the packed-page path
		 * masks the fetch coordinate and blends across the boundary, so the HD path
		 * now matches it instead of introducing a hard edge the original never had.
		 */
		vec4 hd = textureGrad(u_hdTextures[nonuniformEXT(mat.hdTexIndex)], uv, hdDx, hdDy);

		if ((mat.flags & kInverted) != 0u)
			hd.rgb = vec3(1.0) - hd.rgb;

		// The same alpha rules as the packed path below: a test threshold that discards the
		// keyed texels, the two-pass split for kTextureAlpha, and alpha forced opaque for a
		// material that has no alpha of its own.
		if (alphaTest)
		{
			if (hd.a < (32.0 / 255.0))
				discard;
		}

		if ((mat.flags & kTextureAlpha) != 0u)
		{
			if (discardAlpha)
			{
				if (hd.a < 1.0)
					discard;
			}
			else
			{
				if ((hd.a * vertexAlpha) >= 1.0)
					discard;
			}
		}
		else if (!alphaTest)
		{
			hd.a = 1.0;
		}

		return hd;
	}

	/*
	 * Mip level.
	 *
	 * Default: New3D's radial estimate, exactly as before. The explicit level is
	 * what the hardware computed, and gl_FragCoord.w is the rasteriser's 1/w - the
	 * same value the depth written below uses, so the level stays consistent with
	 * the surface actually being drawn.
	 *
	 * Optional: the level this pixel's own uv footprint implies, and, for the
	 * anisotropic mode, the taps that cover the elongated part of it. Note that the
	 * footprint is measured in base-level texels, so the tap spacing is converted
	 * back to uv with the base size and is correct at every level.
	 */
	int taps = 1;
	vec2 tapStep = vec2(0.0);

	/*
	 * New3D's radial estimate, evaluated in every mode: it is the default level, and in
	 * the derivative modes it serves as a one-sided safety bound (see the clamp below).
	 */
	float radial = gl_FragCoord.w * gl_FragCoord.w * lodBase;
	float radialLod = -log2(max(radial, 1e-20));
	float lod = radialLod;

	if (ubo.texFilter.x > 0.5)
	{
		TexFootprint fp = ComputeFootprint(uv, vec2(mat.texWidth, mat.texHeight));

		// Level from the major axis: the longest direction the pixel covers, which
		// is what an isotropic filter has to resolve to stay free of aliasing.
		// (For a round footprint this is the whole story, and is what the radial
		// estimate approximates on a surface facing the camera.)
		lod = log2(fp.major);

		if (ubo.texFilter.x > 1.5)
		{
			float maxTaps = max(ubo.texFilter.y, 1.0);
			float ratio = clamp(fp.major / fp.minor, 1.0, 64.0);
			float used = min(ratio, maxTaps);

			// Below 1.35 the extra taps would land almost on top of each other, and
			// each one costs a full bilinear decode of the packed page, so a mildly
			// stretched footprint is cheaper and no worse with one. This is also what
			// keeps a magnified (lod < 0) isotropic surface down to a single tap and a
			// single mip level, which is the common case up close.
			taps = (used < 1.35) ? 1 : (used < 2.5 ? 2 : int(ceil(used)));

			// The level follows the minor axis, and whatever elongation the taps cannot
			// cover is paid for in the level instead - so a 20:1 footprint with four
			// taps ends up slightly blurred rather than striped.
			lod = log2(fp.minor) + log2(ratio / used);

			float spanTexels = max(used - 1.0, 0.0) * fp.minor;
			tapStep = (fp.axis / vec2(mat.texWidth, mat.texHeight)) * (spanTexels / float(taps));
		}

		/*
		 * One-sided clamp against a derivative that belongs to a different surface.
		 *
		 * Derivatives are read from a 2x2 group, so where the group straddles two quads
		 * whose uv mappings are not continuous with each other the footprint can be
		 * arbitrarily large, and that one pixel samples a far too coarse level - a
		 * one-pixel seam of mush along the quad edge. The sharpening this mode exists
		 * for always goes the other way (a smaller level than the radial estimate), so
		 * bounding the blurrier direction costs nothing real and removes the seam.
		 */
		lod = min(lod, radialLod + 8.0);
	}

	float numLevels = floor(log2(min(float(mat.texWidth), float(mat.texHeight)))) - 1.0;	// r3d only generates down to 2:2 for square textures, otherwise its the min dimension
	float fLevel = clamp(lod, 0.0, numLevels);

	int iLevel = int(fLevel);

	ivec2 wrapMode	= ivec2(mat.wrapModeX, mat.wrapModeY);
	ivec2 basePos	= ivec2(mat.texX, mat.texY);
	ivec2 baseSize	= ivec2(mat.texWidth, mat.texHeight);

	int page = mat.texturePage & 1;
	ivec2 tex1Pos = GetTexturePosition(iLevel, basePos);
	ivec2 tex1Size = GetTextureSize(iLevel, baseSize);
	vec4 tex1Data = texBiLinearFootprint(page, wrapMode, vec2(tex1Size), tex1Pos, uv, iLevel, mat.texFormat, alphaTest, tapStep, taps);

	// init second texel with blank data to avoid any potentially undefined behavior
	vec4 tex2Data = vec4(0.0);

	float blendFactor = 0.0;

	// if LOD < 0, no need to blend with next mipmap level
	//
	// A blend factor below 1/256 cannot change an 8-bit output, and the next level costs
	// another full set of taps, so it is skipped. The microtexture branch keeps its exact
	// original condition (fract == 0), because that is what decides whether the micro
	// texture takes over from the mip blend at all.
	const float kMinMipBlend = 1.0 / 256.0;
	float ffL = fract(fLevel);
	if (ffL > kMinMipBlend)
	{
		ivec2 tex2Pos = GetTexturePosition(iLevel + 1, basePos);
		ivec2 tex2Size = GetTextureSize(iLevel + 1, baseSize);
		tex2Data = texBiLinearFootprint(page, wrapMode, vec2(tex2Size), tex2Pos, uv, iLevel + 1, mat.texFormat, alphaTest, tapStep, taps);

		blendFactor = ffL;
	}
	else if (ffL <= 0.0 && (mat.flags & kMicro) != 0u && lod < 0.0)
	{
		vec4 scaleIndex = vec4(2.0, 4.0, 16.0, 256.0);		// unsure if minLOD=4 has 256x scale? No games appear to use it
		vec2 scale = (vec2(baseSize) / 128.0) * scaleIndex[int(clamp(mat.microMinLOD, 0.0, 3.0))];

		// microtextures are always 128x128 and only use LOD 0 mipmap
		ivec2 tex2Pos = GetMicroTexturePos(mat.microTextureID & 7);
		tex2Data = texBiLinear((page + 1) & 1, ivec2(0), vec2(128.0), tex2Pos, uv * scale, 0, mat.texFormat, alphaTest);

		blendFactor = -lod * exp2(-mat.microMinLOD) * 0.5;
		blendFactor = min(blendFactor, 0.5);
	}

	tex1Data = mix(tex1Data, tex2Data, blendFactor);

	if ((mat.flags & kInverted) != 0u) {
		tex1Data.rgb = vec3(1.0) - tex1Data.rgb;
	}

	if (alphaTest) {
		if (tex1Data.a < (32.0 / 255.0)) {
			discard;
		}
	}

	if ((mat.flags & kTextureAlpha) != 0u) {
		if (discardAlpha) {					// opaque 1st pass
			if (tex1Data.a < 1.0) {
				discard;
			}
		}
		else {								// transparent 2nd pass
			if ((tex1Data.a * vertexAlpha) >= 1.0) {
				discard;
			}
		}
	}

	if ((mat.flags & kTextureAlpha) == 0u && !alphaTest) {
		tex1Data.a = 1.0;
	}

	return tex1Data;
}

// ---------------------------------------------------------------------------
// Shared shading body
// ---------------------------------------------------------------------------

vec4 ShadeFragment()
{
	vec3 viewPos;
	vec3 normal;
	vec3 uvShade;
	vec4 vertexColour;
	float textureNP;
	vec3 faceNormal;

	/*
	 * Two ways to reconstruct the quad, chosen per session through the uniform block.
	 *
	 * ubo.renderOptions.x != 0 is the default exact path. A triangle stores its third
	 * corner twice (see CR3DSceneBuilder::CopyVertexData), which the solve handles as
	 * the degenerate case of a quad - no branch on the primitive type is needed.
	 *
	 * ubo.renderOptions.x == 0 is the fast path the ultra-low preset selects. It takes
	 * the vertex stage's hardware-interpolated attributes and skips both the four
	 * storage-buffer reads and the per-pixel quadratic solve. On a non-planar quad the
	 * rasteriser's two-triangle split then shows as a crease along the 0-2 diagonal,
	 * which is the cost of the ~40 fewer ALU instructions per pixel.
	 */
	if (ubo.renderOptions.x > 0.5)
	{
		GpuQuadVertex c0 = FetchVertex(vQuadBase + 0u);
		GpuQuadVertex c1 = FetchVertex(vQuadBase + 1u);
		GpuQuadVertex c2 = FetchVertex(vQuadBase + 2u);
		GpuQuadVertex c3 = FetchVertex(vQuadBase + 3u);

		vec3 v0 = (pc.modelView * vec4(c0.pos.xyz, 1.0)).xyz;
		vec3 v1 = (pc.modelView * vec4(c1.pos.xyz, 1.0)).xyz;
		vec3 v2 = (pc.modelView * vec4(c2.pos.xyz, 1.0)).xyz;
		vec3 v3 = (pc.modelView * vec4(c3.pos.xyz, 1.0)).xyz;

		// vView is the eye ray for this fragment, so the solve follows the pixel rather
		// than the triangle the rasteriser happened to cover it with.
		vec2 st = QuadParams(v0, v1, v2, v3, vView);
		vec4 w = QuadWeights(st);

		// Everything per-vertex is now evaluated at the quad's own parameters. The
		// perspective is already inside (s, t): it came from intersecting the view ray
		// with the surface, so this is a plain bilinear blend and not a weighted sum of
		// the corners divided by an interpolated 1/w.
		viewPos = v0 * w.x + v1 * w.y + v2 * w.z + v3 * w.w;
		normal = c0.normal.xyz * w.x + c1.normal.xyz * w.y + c2.normal.xyz * w.z + c3.normal.xyz * w.w;
		uvShade = c0.uvShade.xyz * w.x + c1.uvShade.xyz * w.y + c2.uvShade.xyz * w.z + c3.uvShade.xyz * w.w;

		// Polygon-wide quantities: the colour, the face normal and the radial texture
		// LOD factor are the same in all four slots, so one corner serves.
		vertexColour = unpackUnorm4x8(floatBitsToUint(c0.face.w));
		textureNP = c0.normal.w;
		faceNormal = c0.face.xyz;
	}
	else
	{
		viewPos = vView;
		normal = vNormal;
		uvShade = vUvShade;
		vertexColour = unpackUnorm4x8(vColourBits);
		textureNP = vTextureNP;
		faceNormal = vFaceNormal;
	}

	vertexColour.a *= pc.fogParams.w;

	float scale = pc.extra.y != 0.0 ? pc.extra.y : 1.0;
	normal = mat3(pc.modelView) * normal / scale;
	vec3 face = mat3(pc.modelView) * faceNormal;

	// Back-face rejection: the plane through the eye that the polygon lies in.
	// Evaluated at this pixel's own point on the quad instead of being interpolated
	// from the corners, so a polygon that straddles the eye plane is cut where the
	// hardware's per-vertex test cut it.
	float faceDot = dot(viewPos, face);
	if (faceDot > 0.0)
		discard;

	float lodBase = faceDot * -pc.extra.x * textureNP;

	uint index = floatBitsToUint(pc.extra.w);
	R3DMaterial mat = materials.m[index];

	vec4 tex = vec4(1.0);
	if ((mat.flags & kTextured) != 0u)
		tex = GetTextureValue(mat, uvShade.xy, lodBase, vertexColour.a, pc.extra.z < 0.5);   // colour layer discards partial alpha

	vec4 baseColour = vertexColour;
	if ((mat.flags & kTranslator) != 0u)
		baseColour.rgb *= 16.0;
	vec4 colour = tex * baseColour;

	int hardwareStep = int(pc.lightParams.w + 0.5);
	if (hardwareStep == 0x15 && (mat.flags & kLighting) == 0u)
	{
		if ((mat.flags & kFixedShade) != 0u)
			colour.rgb *= 1.0 + uvShade.z + pc.lightParams.x;
		else
			colour.rgb *= 1.5;
	}

	if (colour.a < (1.0 / 32.0))
		discard;

	float ellipse = 1.0 - dot((gl_FragCoord.xy - ubo.spotEllipse.xy) / max(ubo.spotEllipse.zw, vec2(1.0)),
		(gl_FragCoord.xy - ubo.spotEllipse.xy) / max(ubo.spotEllipse.zw, vec2(1.0)));
	ellipse = max(ellipse, 0.0);
	float enable = step(ubo.spotRange.x, -viewPos.z);
	float range = 0.0;
	if (ubo.spotRange.y != 0.0)
	{
		float absExtent = abs(ubo.spotRange.y);
		float d = min(ubo.spotRange.x + absExtent + viewPos.z, 0.0);
		float invR = 1.0 / (1.0 + absExtent);
		range = 1.0 / Sqr(d * invR - 1.0);
		range *= enable;
	}
	float lobe = range * ellipse;
	float lobeFog = enable * ellipse;

	if ((mat.flags & kLighting) != 0u)
	{
		float sunFactor = (mat.flags & kFixedShade) != 0u ? uvShade.z : dot(pc.lightDir.xyz, normalize(normal));
		sunFactor = clamp(sunFactor, -1.0, 1.0);
		if (pc.lightParams.y > 0.5 || colour.a < 0.999)
			sunFactor = max(sunFactor, 0.0);

		vec3 light = vec3(sunFactor * pc.lightDir.w + pc.lightParams.x);
		light += ubo.spotColor.rgb * lobe;
		if (pc.lightParams.z > 0.5)
			light = min(light, vec3(1.0));
		colour.rgb *= light;

		if ((mat.flags & kSpecular) != 0u)
		{
			float spec;
			if ((mat.flags & kSmooth) != 0u)
			{
				float expIndex[4] = float[4](8.0, 16.0, 32.0, 64.0);
				float multIndex[4] = float[4](1.6, 1.6, 2.4, 3.2);
				int shin = clamp(int(mat.shininess + 0.5), 0, 3);
				spec = pow(max(sunFactor, 0.0), expIndex[shin]) * multIndex[shin];
			}
			else
			{
				vec3 R = reflect(-pc.lightDir.xyz, normalize(normal));
				spec = max(R.z, 0.0);
			}
			spec *= mat.specularValue * pc.lightDir.w;
			if (vertexColour.a < 1.0)
				colour.a = max(colour.a, spec);
			colour.rgb += spec;
		}
	}

	colour.rgb = min(colour.rgb, vec3(1.0));

	float fog = mat.fogIntensity * clamp(pc.fogParams.x + (-viewPos.z) * pc.fogColour.a, 0.0, 1.0);
	vec3 fogRgb = pc.fogColour.rgb * pc.fogParams.z;
	fogRgb += ubo.spotFogColor.rgb * pc.fogParams.y * pc.fogColour.rgb * lobeFog;
	colour.rgb = mix(colour.rgb, fogRgb, fog);

	/*
	 * Sorted-transparency path only. srally2's dust is the one Model 3 effect with this exact
	 * state: LAY=1, unlit, no texture alpha, textured. TL=31/32 would otherwise be a 97%-opaque
	 * card, and the mesh is a curved shell, so its silhouette is a hard polygon edge.
	 *
	 * - A 0.70 alpha scale keeps about 32% transmission on a newborn puff and fades the
	 *   TL=9..17 puffs smoothly.
	 * - The view-space rim term only fades where the shell turns away from the eye; a
	 *   camera-facing billboard has |N.V| ~= 1 and is unchanged.
	 *
	 * The fingerprint is IsDustEffect(): LAY=1 + unlit + TLM=0 + textured. The textured term
	 * is what keeps the untextured layered shadow mesh under the car from being softened.
	 */
	if (kSoftEffects != 0 && pc.extra.z >= 0.5 && IsDustEffect(mat.flags))
	{
		colour.a *= 0.70;
		float n2 = dot(normal, normal);
		if (n2 > 1e-12)
		{
			float ndotv = abs(dot(normal * inversesqrt(n2), normalize(-viewPos)));
			colour.a *= smoothstep(0.08, 0.55, ndotv);
		}
		if (colour.a < (1.0 / 32.0))
			discard;
	}

	// New3D's WriteOutputs() forces alpha to 1 for the colour layer. Without it the opaque pass
	// leaves a partially transparent scene behind, and the 2D backdrop is composited with
	// SRC_ALPHA/ONE_MINUS_SRC_ALPHA - so wherever a texture alpha or a vertex alpha is below 1 the
	// sky shows through. On Daytona 2's waterfall that is a blue stipple over the white water, and
	// the alpha-tested foliage on the cliff shows the same speckle.
	if (pc.extra.z < 0.5)
		colour.a = 1.0;

	return colour;
}
