#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

layout(set = 0, binding = 0) uniform sampler uLinear;
// Nearest filtering, selected by UpscaleMode::Nearest. A separate binding rather than an array so
// the choice needs no dynamically indexed sampler array.
layout(set = 0, binding = 2) uniform sampler uNearest;
// 0 = bottom tile layer, 1 = 3D scene, 2 = top tile layer, 3 = bezel backdrop, 4/5 = the two
// transparency targets. The scene and transparency views are swapped in at runtime (see
// SetSceneColor / SetTransColor) because they change when the targets are recreated.
layout(set = 0, binding = 1) uniform texture2D uLayers[6];

// Every layer is drawn by this one shader, so brightness has to be a per-draw value and every caller
// has to supply it. It is 1.0 for the scene and tile layers; only the backdrop is ever dimmed. Note
// that a caller which pushes fewer bytes than the block declares leaves the rest undefined, which is
// why DrawLayer() pushes the whole struct rather than just the layer index.
//
// upscaleMode is UpscaleMode from Supermodel.h: 0 = nearest, 1 = biquintic, 2 = bilinear,
// 3 = bicubic. It is ignored for the scene and the backdrop, which pass 2.
layout(push_constant) uniform PushConstants
{
	uint  layer;
	float brightness;
	int   upscaleMode;
	// 1 when the 3D pass already folded Real3D's two transparency layers into the scene target
	// (see scene_composite.frag). The target is premultiplied in that case, which is also how the
	// legacy path leaves it, so only the two-layer block below is skipped.
	uint  sceneComposited;
	// 1 for AlphaSortMode = 2: the transparency targets hold a checkerboard (trans1 on the even
	// cells, trans2 on the odd ones) and the two-layer block below reconstructs it at half
	// resolution instead of treating both targets as full coverage.
	uint  checkerboard;
	// Test hook (SM3D_CHECKER_RAW=1): 1 = skip the reconstruction and blend the pixel's own cell.
	// That leaves the one-pixel checkerboard visible instead of averaging it into a smooth
	// half-strength blend, which is how the board's output looks when it is not de-interlaced.
	uint  checkerRaw;
} pc;

/*
 * The two high-order filters are the ones the OpenGL 2D path uses (see Shaders2D.h), applied here
 * instead because in Vulkan the tile layers are magnified by the composite draw rather than by GL
 * texture filtering. Both need a linear sampler: they compute their own weights and read four
 * texels through the hardware's bilinear unit.
 *
 * Biquintic remaps the fractional position through a quintic smoothstep before a single bilinear
 * lookup. That is one fetch, and its slightly softer pass band removes the hard texel corners of
 * nearest-neighbour without ringing. Bicubic is the separable 4x4 Mitchell-style kernel done as
 * four bilinear fetches, which keeps edges crisper at the cost of three extra samples.
 */
vec4 Biquintic(uint layer, vec2 uv)
{
	vec2 ts = vec2(textureSize(sampler2D(uLayers[layer], uLinear), 0));
	uv = uv * ts + 0.5;

	vec2 i = floor(uv);
	vec2 f = uv - i;
	f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0) - 0.5;

	uv = (i + f) / ts;
	return texture(sampler2D(uLayers[layer], uLinear), uv);
}

vec4 Cubic(float v)
{
	vec4 n = vec4(1.0, 2.0, 3.0, 4.0) - v;
	vec4 s = n * n * n;
	float x = s.x;
	float y = s.y - 4.0 * s.x;
	float z = s.z - 4.0 * s.y + 6.0 * s.x;
	float w = 6.0 - x - y - z;
	return vec4(x, y, z, w) * (1.0 / 6.0);
}

vec4 Bicubic(uint layer, vec2 uv)
{
	vec2 invTexSize = 1.0 / vec2(textureSize(sampler2D(uLayers[layer], uLinear), 0));
	uv = uv * vec2(textureSize(sampler2D(uLayers[layer], uLinear), 0)) - 0.5;
	vec2 uvi = floor(uv);
	vec2 fxy = uv - uvi;
	vec4 xcubic = Cubic(fxy.x);
	vec4 ycubic = Cubic(fxy.y);
	vec4 c = uvi.xxyy + vec2(-0.5, 1.5).xyxy;
	vec4 s = vec4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
	vec4 offset = c + vec4(xcubic.yw, ycubic.yw) / s;
	offset *= invTexSize.xxyy;
	vec4 sample0 = texture(sampler2D(uLayers[layer], uLinear), offset.xz);
	vec4 sample1 = texture(sampler2D(uLayers[layer], uLinear), offset.yz);
	vec4 sample2 = texture(sampler2D(uLayers[layer], uLinear), offset.xw);
	vec4 sample3 = texture(sampler2D(uLayers[layer], uLinear), offset.yw);
	float sx = s.x / (s.x + s.y);
	float sy = s.z / (s.z + s.w);
	return mix(mix(sample3, sample2, sx), mix(sample1, sample0, sx), sy);
}

void main()
{
	// Opaque types cannot be selected with ?:, and a sampler2D constructed from a texture and a
	// sampler is itself opaque - so each mode builds its own at the point of use.
	vec4 colour;
	if (pc.upscaleMode == 3)
		colour = Bicubic(pc.layer, vUv);
	else if (pc.upscaleMode == 1)
		// Alpha comes from the plain sample, as in the OpenGL path: the filters are there to smooth
		// colour detail, and running them over coverage would feather the edges of the layer.
		colour = vec4(Biquintic(pc.layer, vUv).rgb,
			texture(sampler2D(uLayers[pc.layer], uLinear), vUv).a);
	else if (pc.upscaleMode == 0)
		colour = texture(sampler2D(uLayers[pc.layer], uNearest), vUv);
	else
		colour = texture(sampler2D(uLayers[pc.layer], uLinear), vUv);

	// The two transparency targets hold the two Real3D transparency layers, exactly as New3D's
	// R3DFrameBuffers composites them: where both layers cover a pixel the result is one opaque
	// surface, otherwise the covered layer is blended over the scene. This is what keeps a LOD
	// cross-fade opaque instead of letting the background show through the complementary alphas.
	//
	// Runs only when the 3D pass did not fold the layers itself. The scene target is premultiplied
	// (geometry forces alpha to 1, the fog quad premultiplies its own colour), so source-over is
	// written directly instead of the un-premultiply / re-premultiply the straight-alpha version
	// needed; the result is the same colour.
	//
	// Keeping the scene's alpha is not optional: the target is cleared to alpha 0 and the fog quad
	// writes its fog amount there, so VF3's sky (a Render2D bottom layer) shows wherever no 3D
	// geometry and no fog cover it. Forcing alpha to 1 painted the fog colour - black in VF3 - over
	// that sky. The premultiplied form preserves it through the blend, which is why the scene layer
	// uses ONE / ONE_MINUS_SRC_ALPHA (m_pipelineScene in Vulkan2D).
	if (pc.layer == 1u && pc.sceneComposited == 0u)
	{
		if (pc.checkerboard != 0u && pc.checkerRaw == 0u)
		{
			/*
			 * Arcade checkerboard transparency (AlphaSortMode = 2).
			 *
			 * Each transparency layer owns one half of the checkerboard: trans1 the even cells,
			 * trans2 the odd ones. A 2x2 group therefore holds two cells of each layer, and only
			 * one of the two can carry a surface, so the other half of the group still holds the
			 * opaque scene. That is what keeps a dense effect translucent - mode 0 has both layers
			 * at full coverage, so its "both layers present" rule fires wherever two overlapping
			 * sprites land on different layers and turns the whole area solid.
			 *
			 * The resolve is a box filter over the group, not the pixel's own checkerboard cell.
			 * Two of the group's four samples belong to this layer, so a fully covered cell
			 * contributes a quarter of its alpha and the group comes out at half the layer's
			 * alpha - exactly the "effectively halved" the board produces. Sampling the pixel's own
			 * cell instead would give a one-pixel stipple, which at 4K reads as a screen door rather
			 * than as a CRT's half-resolution blend.
			 *
			 * Colour is accumulated premultiplied and divided by the summed alpha: the two cells of
			 * a layer can have different coverage along an edge, and straight-alpha averaging there
			 * would darken the fringe.
			 */
			vec2 ts = vec2(textureSize(sampler2D(uLayers[4], uLinear), 0));
			vec2 origin = floor(vUv * ts * 0.5) * 2.0;
			vec4 c00 = texture(sampler2D(uLayers[4], uLinear), (origin + vec2(0.5, 0.5)) / ts);
			vec4 c11 = texture(sampler2D(uLayers[4], uLinear), (origin + vec2(1.5, 1.5)) / ts);
			vec4 c10 = texture(sampler2D(uLayers[5], uLinear), (origin + vec2(1.5, 0.5)) / ts);
			vec4 c01 = texture(sampler2D(uLayers[5], uLinear), (origin + vec2(0.5, 1.5)) / ts);

			const float sum1 = c00.a + c11.a;
			const float sum2 = c10.a + c01.a;
			/*
			 * Two-layer overlap goes opaque (Daytona 2), and it is defined on the group rather than on
			 * the pixel: with a fixed parity per layer, no single pixel can ever hold both.
			 *
			 * The test is on coverage, not on "any alpha at all". Requiring each layer to fill its own
			 * two cells (sum >= 1.0, i.e. the pair averages at least half opacity) is what stops a
			 * thin dust wisp passing over a soft shadow from turning that shadow into a solid dark
			 * quad - which is exactly what `sum1 > 0.0 && sum2 > 0.0` did on Sega Rally 2's snow.
			 */
			if (sum1 >= 1.0 && sum2 >= 1.0)
			{
				// Both halves of the group carry translucency, so the board has no base pixel left
				// in it and the group resolves opaque (Daytona 2).
				vec3 rgb1 = (c00.rgb * c00.a + c11.rgb * c11.a) / sum1;
				vec3 rgb2 = (c10.rgb * c10.a + c01.rgb * c01.a) / sum2;
				float a1 = 0.25 * sum1;
				float a2 = 0.25 * sum2;
				colour = vec4(mix(rgb1, rgb2, (a2 + (1.0 - a1)) * 0.5), 1.0);
			}
			else
			{
				const float sum = sum1 + sum2;
				if (sum > 0.0)
				{
					vec3 rgb = (c00.rgb * c00.a + c11.rgb * c11.a + c10.rgb * c10.a + c01.rgb * c01.a) / sum;
					float a = 0.25 * sum;
					colour = vec4(rgb * a + colour.rgb * (1.0 - a), a + colour.a * (1.0 - a));
				}
				// sum == 0: no translucency in this group, the scene stands as it is.
			}
		}
		else
		{
			/*
			 * Test hook (SM3D_CHECKER_RAW): sample the two layers with nearest filtering, so the
			 * one-pixel checkerboard survives the upscale to the swapchain. With the linear sampler
			 * a 2480x1920 scene blown up to 3840x2160 averages the pattern away before the shader
			 * ever sees it, which is why the raw mode looked identical to the resolved one.
			 */
			vec4 t1;
			vec4 t2;
			if (pc.checkerRaw != 0u)
			{
				t1 = texture(sampler2D(uLayers[4], uNearest), vUv);
				t2 = texture(sampler2D(uLayers[5], uNearest), vUv);
			}
			else
			{
				t1 = texture(sampler2D(uLayers[4], uLinear), vUv);
				t2 = texture(sampler2D(uLayers[5], uLinear), vUv);
			}
			if (t1.a * t2.a > 0.0)
			{
				// Two overlapping layers are opaque on the real hardware: there is no pixel of the
				// background left for them to blend against.
				vec3 mixCol = mix(t1.rgb, t2.rgb, (t2.a + (1.0 - t1.a)) * 0.5);
				colour = vec4(mixCol, 1.0);
			}
			else
			{
				vec4 trans = t1.a > 0.0 ? t1 : t2;
				colour = vec4(trans.rgb * trans.a + colour.rgb * (1.0 - trans.a),
					trans.a + colour.a * (1.0 - trans.a));
			}
		}
	}

	// rgb only: dimming must not turn a backdrop with soft alpha edges into a more transparent one.
	outColour = vec4(colour.rgb * pc.brightness, colour.a);
}
