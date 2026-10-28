/*
 * Shared body of the scene composite subpass, included by scene_composite.frag (single-sampled)
 * and scene_composite_ms.frag (multisampled).
 *
 * The only difference between the two is how an input attachment is read: a multisampled one has to
 * name the sample, a single-sampled one must not. Keeping the body here is what makes it impossible
 * for the two variants to compute different colours.
 */

layout(location = 0) out vec4 outColour;

#if SCENE_COMPOSITE_MS
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInputMS uTrans1;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInputMS uTrans2;
#define LOAD_TRANS_1() subpassLoad(uTrans1, gl_SampleID)
#define LOAD_TRANS_2() subpassLoad(uTrans2, gl_SampleID)
#else
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput uTrans1;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput uTrans2;
#define LOAD_TRANS_1() subpassLoad(uTrans1)
#define LOAD_TRANS_2() subpassLoad(uTrans2)
#endif

void main()
{
	vec4 t1 = LOAD_TRANS_1();
	vec4 t2 = LOAD_TRANS_2();

	if (t1.a * t2.a > 0.0)
	{
		// Two overlapping layers are opaque on the real hardware: there is no pixel of the
		// background left for them to blend against.
		vec3 mixCol = mix(t1.rgb, t2.rgb, (t2.a + (1.0 - t1.a)) * 0.5);
		outColour = vec4(mixCol, 1.0);
		return;
	}

	// New3D composites the layer that is covered; a zero alpha in both means no translucency here.
	vec4 trans = t1.a > 0.0 ? t1 : t2;
	if (trans.a <= 0.0)
		discard;

	// Premultiplied, because the blend is ONE / ONE_MINUS_SRC_ALPHA: source-over against the
	// premultiplied opaque colour, which is algebraically New3D's SRC_ALPHA / ONE_MINUS_SRC_ALPHA
	// applied to the same straight-alpha layer.
	outColour = vec4(trans.rgb * trans.a, trans.a);
}
