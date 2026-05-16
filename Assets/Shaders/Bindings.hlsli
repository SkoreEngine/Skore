#ifndef SK_BINDINGS_HLSLI
#define SK_BINDINGS_HLSLI


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

struct VertexLayoutOffset
{
	uint stride;
	uint positionOffset;
	uint normalOffset;
	uint uvOffset;
	uint uv1Offset;
	uint colorOffset;
	uint tangentOffset;
	uint pad;
};

StructuredBuffer<MaterialData> MaterialDataBuffer : register(t0, space2);
SamplerState LinearSampler : register(s1, space2);
Texture2D BindlessTextures[] : register(t2, space2);
ByteAddressBuffer VertexBuffers[] : register(t3, space2);
ByteAddressBuffer IndexBuffers[] : register(t4, space2);
ConstantBuffer<VertexLayoutOffset> VertexLayouts[] : register(b5, space2);

float3 GetVertexPosition(uint meshIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(meshIdx)];
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load3(vertexId * layout.stride + layout.positionOffset));
}

float3 GetVertexNormal(uint meshIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(meshIdx)];
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load3(vertexId * layout.stride + layout.normalOffset));
}

float2 GetVertexUV(uint meshIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(meshIdx)];
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load2(vertexId * layout.stride + layout.uvOffset));
}

float2 GetVertexUV1(uint meshIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(meshIdx)];
	if (layout.uv1Offset == 0xFFFFFFFF) return float2(0, 0);
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load2(vertexId * layout.stride + layout.uv1Offset));
}

float3 GetVertexColor(uint meshIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(meshIdx)];
	if (layout.colorOffset == 0xFFFFFFFF) return float3(1, 1, 1);
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load3(vertexId * layout.stride + layout.colorOffset));
}

float4 GetVertexTangent(uint meshIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(meshIdx)];
	if (layout.tangentOffset == 0xFFFFFFFF) return float4(0, 0, 0, 0);
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load4(vertexId * layout.stride + layout.tangentOffset));
}

#endif
