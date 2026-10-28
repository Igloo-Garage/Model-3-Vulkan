#ifndef _MODEL_H_
#define _MODEL_H_

#include <vector>
#include <memory>
#include <cstring>
#include "Types.h"
#include "Mat4.h"

namespace New3D {

struct Vertex					// half vertex
{
	float pos[4];
	float normal[3];
	float texcoords[2];
	float fixedShade;

	static bool Equal(const Vertex& p1, const Vertex& p2)
	{
		return (p1.pos[0] == p2.pos[0] &&
			p1.pos[1] == p2.pos[1] &&
			p1.pos[2] == p2.pos[2]);
	}

	static void Average(const Vertex& p1, const Vertex& p2, Vertex& p3)
	{
		p3.pos[3] = 1.0f;	//always 1
		p3.fixedShade = (p1.fixedShade + p2.fixedShade) * 0.5f;

		for (int i = 0; i < 3; i++)	{ p3.pos[i] = (p1.pos[i] + p2.pos[i]) * 0.5f; }
		for (int i = 0; i < 3; i++)	{ p3.normal[i] = (p1.normal[i] + p2.normal[i]) * 0.5f; }
		for (int i = 0; i < 2; i++)	{ p3.texcoords[i] = (p1.texcoords[i] + p2.texcoords[i]) * 0.5f; }
	}
};

struct R3DPoly
{
	Vertex v[4];			// just easier to have them as an array
	float faceNormal[3];	// we need this to help work out poly winding, i assume the h/w uses this instead of calculating normals itself
	UINT8 faceColour[4];	// per face colour
	float textureNP;
	int number = 4;
};

struct FVertex				// full vertex including face attributes
{
	Vertex base;			// composition instead of inheritance keeps FVertex standard-layout, making offsetof well-defined
	float faceNormal[3];
	UINT8 faceColour[4];
	float textureNP;

	FVertex& operator=(const Vertex& vertex)
	{
		base = vertex;
		return *this;
	}

	FVertex() {}
	FVertex(const R3DPoly& r3dPoly, int index)
	{
		for (int i = 0; i < 4; i++) { faceColour[i] = r3dPoly.faceColour[i]; }
		for (int i = 0; i < 3; i++) { faceNormal[i] = r3dPoly.faceNormal[i]; }
		textureNP = r3dPoly.textureNP;

		*this = r3dPoly.v[index];
	}

	FVertex(const R3DPoly& r3dPoly, int index1, int index2)		// average of 2 points
	{
		Vertex::Average(r3dPoly.v[index1], r3dPoly.v[index2], base);

		// copy face attributes
		for (int i = 0; i < 4; i++) { faceColour[i] = r3dPoly.faceColour[i]; }
		for (int i = 0; i < 3; i++) { faceNormal[i] = r3dPoly.faceNormal[i]; }
		textureNP = r3dPoly.textureNP;
	}

	static bool Equal(const FVertex& p1, const FVertex& p2)
	{
		return Vertex::Equal(p1.base, p2.base);
	}

	static void Average(const FVertex& p1, const FVertex& p2, FVertex& p3)
	{
		Vertex::Average(p1.base, p2.base, p3.base);
		for (int i = 0; i < 4; i++) { p3.faceColour[i] = p1.faceColour[i]; }
		for (int i = 0; i < 3; i++) { p3.faceNormal[i] = p1.faceNormal[i]; }
	}
};

enum class Layer { colour, trans1, trans2, trans12 /*both 1&2*/, all, none };

struct Mesh
{
	//helper funcs
	bool Render(Layer layer, float nodeAlpha)
	{
		bool nAlpha = nodeAlpha < 1.0f;

		switch (layer)
		{
		case Layer::colour:
			if (polyAlpha || nAlpha) {
				return false;
			}
			break;
		case Layer::trans1:
			if ((!textureAlpha && !polyAlpha && !nAlpha) || transLSelect) {
				return false;
			}
			break;
		case Layer::trans2:
			if ((!textureAlpha && !polyAlpha && !nAlpha) || !transLSelect) {
				return false;
			}
			break;
		default:					// not using these types
			return false;
		}

		return true;
	}

	enum TexWrapMode : int { repeat = 0, repeatClamp, mirror, mirrorClamp };

	// texture
	int		format		= 0;
	int		x			= 0;
	int		y			= 0;
	int		width		= 0;
	int		height		= 0;
	int		page		= 0;
	bool	inverted	= false;

	TexWrapMode wrapModeU = TexWrapMode::repeat;
	TexWrapMode wrapModeV = TexWrapMode::repeat;

	// microtexture
	bool	microTexture		= false;
	int		microTextureID		= 0;
	float	microTextureMinLOD	= 0;

	// attributes
	bool textured		= false;
	bool polyAlpha		= false;		// specified in the rgba colour
	bool textureAlpha	= false;		// use alpha in texture
	bool alphaTest		= false;		// discard fragment based on alpha (ogl does this with fixed function)
	bool layered		= false;		// stencil poly
	bool highPriority	= false;		// rendered over the top
	bool transLSelect	= false;		// actually the transparency layer, false = layer 0, true = layer 1
	bool translatorMap	= false;		// colours are multiplied by 16
	bool noLosReturn	= false;		// line of sight test

	// lighting
	bool fixedShading	= false;
	bool smoothShading	= false;
	bool lighting		= false;
	bool specular		= false;
	float shininess		= 0;
	float specularValue = 0;
	
	// fog
	float fogIntensity = 1.0f;

	// opengl resources
	int vboOffset		= 0;			// this will be calculated later
	int vertexCount		= 0;			// /3 for triangles /4 for quads
};

struct SortingMesh : public Mesh		// This struct temporarily holds the model data, before it gets copied to the main buffer
{
	std::vector<FVertex> verts;
	// Index of the first polygon that contributed to this mesh, in the model's original submission
	// order. Used by the Vulkan scene builder to emit meshes in that order, so translucent blending
	// happens in the order the Real3D hardware would have blended them.
	UINT32	order			= 0;
};

struct Model
{
	std::shared_ptr<std::vector<Mesh>> meshes;	// this reason why this is a shared ptr to an array, is that multiple models might use the same meshes

	//which memory are we in
	bool dynamic = true;

	// texture offsets for model
	int textureOffsetX = 0;
	int textureOffsetY = 0;
	int page = 0;

	//matrices
	float modelMat[16];
	// The model transform relative to the viewport's base matrix (base * local =
	// modelMat). Kept separately so the camera and the instance can be
	// interpolated independently: the camera moves every frame, the instance
	// usually does not.
	float localMat[16] = {};

	// Identity for matching this instance across frames.
	uint32_t sourceModelAddr = 0;
	uint32_t matrixSlotID = 0;
	uint64_t instanceKey = 0;

	//model scale step 1.5+
	float scale = 1.0f;

	//node transparency
	float alpha = 1.0f;
};

struct Viewport
{
	float	vpX;					// these are the original hardware values
	float	vpY;
	float	vpWidth;
	float	vpHeight;
	float	angle_left;
	float	angle_right;
	float	angle_top;
	float	angle_bottom;
	float	cota;

	Mat4	projectionMatrix;		// projection matrix, we will calc this later when we have scene near/far vals

	float	lightingParams[6];		// lighting parameters (see RenderViewport() and vertex shader)
	bool	sunClamp;				// unknown how this is set
	bool	intensityClamp;			// unknown how this is set
	float	spotEllipse[4];			// spotlight ellipse (see RenderViewport())
	float	spotRange[2];			// Z range
	float	spotColor[3];			// color
	float	fogColour[3];
	float	fogStart;
	float	fogDensity;
	float	fogAttenuation;
	float	fogAmbient;
	float	scrollFog;				// a transparency value that determines if fog is blended over the bottom 2D layer
	int		losPosX, losPosY;		// line of sight position
	int		x, y;					// viewport coordinates (scaled and in OpenGL format)
	int		width, height;			// viewport dimensions (scaled for display surface size)
	int		priority;				// priority
	int		select;					// viewport select?
	int		number;					// viewport number
	float	spotFogColor[3];		// spotlight color on fog
	float	scrollAtt;

	int		hardwareStep;			// not really a viewport param but will do here

	// The culling viewport node this came from. Stable for the life of a scene
	// graph, and the key the interpolation code uses to keep the main view and
	// a rear-view mirror apart.
	uint32_t sourceAddress = 0;
	uint32_t matrixBaseAddr = 0;
	float	baseMatrix[16] = {};	// C * matrix slot 0
	bool	fullScreen = false;		// 0,0,496x384 - the gameplay view
	int		screenArea = 0;
};

class NodeAttributes
{
public:

	NodeAttributes();

	bool Push();
	bool Pop();
	bool StackLimit() const;
	void Reset();

	int currentTexOffsetX;
	int currentTexOffsetY;
	int currentPage;
	float currentModelScale;
	float currentModelAlpha;
	bool currentDisableCulling;

private:

	struct NodeAttribs
	{
		int texOffsetX;
		int texOffsetY;
		int page;
		float modelScale;
		float modelAlpha;	// from culling node
		bool disableCulling;
	};
	std::vector<NodeAttribs> m_vecAttribs;
};

struct Node
{
	Viewport viewport;
	std::vector<Model> models;
};

} // New3D


#endif
