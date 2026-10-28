#version 450

/*
 * Ambient backdrop for the settings window.
 *
 * The frosted panels need something behind them to blur, and behind a launcher there is nothing: the
 * window covers the screen. This paints the selected game's cover art as a full-window wash - scaled
 * to cover, oversaturated, pulled toward the theme's base colour and vignetted - so the panels read
 * as glass over a wallpaper rather than as opaque cards on a flat rectangle.
 *
 * All geometry and colour is in the push constant block, so one pipeline serves every cover and both
 * themes, and switching the selection costs a push constant write rather than a rebuild.
 *
 * The cover is sampled with a "cover" fit (not "contain"): a portrait boxart stretched to a landscape
 * window would keep two thirds of the screen empty, and this is an abstract wash anyway - the whole
 * image is a blur source, never looked at directly.
 */

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

layout(set = 0, binding = 0) uniform sampler uSampler;
layout(set = 0, binding = 1) uniform texture2D uCover;

layout(push_constant) uniform PushConstants
{
	vec2 uvScale;       // cover-fit scale applied to the screen uv
	vec2 uvOffset;      // centring offset for the same fit
	vec4 tint;          // rgb = wash colour, a = how far the cover is pulled toward it
	vec4 params;        // x = saturation, y = vignette, z = exposure, w = unused
} pc;

void main()
{
	vec2 uv = clamp(vUv * pc.uvScale + pc.uvOffset, vec2(0.0), vec2(1.0));
	vec3 colour = texture(sampler2D(uCover, uSampler), uv).rgb * pc.params.z;

	// Desaturating a busy cover is what turns it into a background: at full saturation the eye keeps
	// trying to read it, and every panel edge competes with it for attention.
	const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
	float luma = dot(colour, lumaWeights);
	colour = mix(vec3(luma), colour, pc.params.x);

	// Wash toward the theme colour. Doing this before the vignette means the corners darken the
	// already-tinted result rather than the raw cover, which keeps them neutral instead of colouring
	// them with whatever happened to be in the corner of the artwork.
	colour = mix(colour, pc.tint.rgb, pc.tint.a);

	vec2 d = vUv - vec2(0.5);
	float vignette = 1.0 - pc.params.y * dot(d, d) * 1.6;
	colour *= clamp(vignette, 0.0, 1.0);

	outColour = vec4(clamp(colour, vec3(0.0), vec3(1.0)), 1.0);
}
