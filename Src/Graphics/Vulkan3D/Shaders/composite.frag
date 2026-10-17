#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

layout(set = 0, binding = 0) uniform sampler uSampler;
// 0 = bottom tile layer, 1 = 3D scene, 2 = top tile layer, 3 = bezel backdrop. The scene view is
// swapped in at runtime (see SetSceneColor) because it changes when the targets are recreated.
layout(set = 0, binding = 1) uniform texture2D uLayers[4];

// Every layer is drawn by this one shader, so brightness has to be a per-draw value and every caller
// has to supply it. It is 1.0 for the scene and tile layers; only the backdrop is ever dimmed. Note
// that a caller which pushes fewer bytes than the block declares leaves the rest undefined, which is
// why DrawLayer() pushes the whole struct rather than just the layer index.
layout(push_constant) uniform PushConstants
{
	uint layer;
	float brightness;
} pc;

void main()
{
	vec4 colour = texture(sampler2D(uLayers[pc.layer], uSampler), vUv);
	// rgb only: dimming must not turn a backdrop with soft alpha edges into a more transparent one.
	outColour = vec4(colour.rgb * pc.brightness, colour.a);
}
