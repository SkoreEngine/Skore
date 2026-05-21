#ifndef SK_GLOBAL_BINDINGS_HLSLI
#define SK_GLOBAL_BINDINGS_HLSLI

// space0 — global bindless: materials, samplers, textures, vertex/index/layout buffers.
// Include this in shaders that need the bindless arrays. Shaders that only need scene
// data should include SceneBindings.hlsli alone.

#include "VertexPulling.hlsli"

struct MaterialData
{
	float3 baseColor;
	float  roughness;
	float  metallic;
	int    baseColorTexture;
	int    normalTexture;
	int    roughnessTexture;
	int    metallicTexture;
	float2 uvScale;
	float  emissiveFactor;
	float3 emissiveColor;
	int    emissiveTexture;
	int		 alphaMode;
	float	 alphaCutoff;
	float	 pad0;
	float  pad1;
};

StructuredBuffer<MaterialData> MaterialDataBuffer : register(t0, space0);
SamplerState LinearSampler : register(s1, space0);
Texture2D BindlessTextures[] : register(t2, space0);

#endif
