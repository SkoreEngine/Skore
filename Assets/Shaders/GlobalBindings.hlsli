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
	uint   samplerIndices0;
	uint   samplerIndices1;

	uint GetBaseColorSamplerIndex() { return  samplerIndices0        & 0xFF; }
	uint GetNormalSamplerIndex()    { return (samplerIndices0 >> 8)  & 0xFF; }
	uint GetRoughnessSamplerIndex() { return (samplerIndices0 >> 16) & 0xFF; }
	uint GetMetallicSamplerIndex()  { return (samplerIndices0 >> 24) & 0xFF; }
	uint GetEmissiveSamplerIndex()  { return  samplerIndices1        & 0xFF; }
};

#define SK_LINEAR_SAMPLER        0
#define SK_NEAREST_SAMPLER       1
#define SK_LINEAR_CLAMP_SAMPLER  2
#define SK_NEAREST_CLAMP_SAMPLER 3
#define SK_SAMPLER_COUNT         4

StructuredBuffer<MaterialData> MaterialDataBuffer : register(t0, space0);
RWStructuredBuffer<uint> MaterialMaskBuffer       : register(u1, space0);
SamplerState samplers[SK_SAMPLER_COUNT]           : register(s2, space0);
Texture2D BindlessTextures[]                      : register(t3, space0);

#endif
