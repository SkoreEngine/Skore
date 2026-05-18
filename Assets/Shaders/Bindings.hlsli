#ifndef SK_BINDINGS_HLSLI
#define SK_BINDINGS_HLSLI

#include "VertexPulling.hlsli"

cbuffer GlobalSceneBuffer : register(b0, space0)
{
	matrix viewProjection;
	matrix view;
	matrix projection;
	matrix viewInv;
	matrix projectionInv;
	matrix viewProjInv;
	float3 cameraPosition;
	float  farClip;
	int2   outputSize;
	float2 pad1;
	float2 jitter;
	float2 prevJitter;
};

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

StructuredBuffer<MaterialData> MaterialDataBuffer : register(t0, space2);
SamplerState LinearSampler : register(s1, space2);
Texture2D BindlessTextures[] : register(t2, space2);

#endif
