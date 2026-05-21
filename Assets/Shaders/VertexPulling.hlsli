#ifndef SK_VERTEX_PULLING_HLSLI
#define SK_VERTEX_PULLING_HLSLI

struct VertexLayoutOffset
{
	uint stride;
	uint positionOffset;
	uint normalOffset;
	uint uvOffset;
	uint uv1Offset;
	uint colorOffset;
	uint tangentOffset;
	uint boneIndicesOffset;
	uint boneWeightsOffset;
	uint pad0;
	uint pad1;
	uint pad2;
};

ByteAddressBuffer VertexBuffers[] : register(t3, space0);
ByteAddressBuffer IndexBuffers[] : register(t4, space0);
ConstantBuffer<VertexLayoutOffset> VertexLayouts[] : register(b5, space0);

float3 GetVertexPosition(uint meshIdx, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load3(vertexId * layout.stride + layout.positionOffset));
}

float3 GetVertexNormal(uint meshIdx, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load3(vertexId * layout.stride + layout.normalOffset));
}

float2 GetVertexUV(uint meshIdx, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load2(vertexId * layout.stride + layout.uvOffset));
}

float2 GetVertexUV1(uint meshIdx, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.uv1Offset == 0xFFFFFFFF) return float2(0, 0);
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load2(vertexId * layout.stride + layout.uv1Offset));
}

float3 GetVertexColor(uint meshIdx, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.colorOffset == 0xFFFFFFFF) return float3(1, 1, 1);
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load3(vertexId * layout.stride + layout.colorOffset));
}

float4 GetVertexTangent(uint meshIdx, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.tangentOffset == 0xFFFFFFFF) return float4(0, 0, 0, 0);
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load4(vertexId * layout.stride + layout.tangentOffset));
}

uint4 GetVertexBoneIndices(uint meshIdx, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.boneIndicesOffset == 0xFFFFFFFF) return uint4(0, 0, 0, 0);
	return VertexBuffers[NonUniformResourceIndex(meshIdx)].Load4(vertexId * layout.stride + layout.boneIndicesOffset);
}

float4 GetVertexBoneWeights(uint meshIdx, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.boneWeightsOffset == 0xFFFFFFFF) return float4(1, 0, 0, 0);
	return asfloat(VertexBuffers[NonUniformResourceIndex(meshIdx)].Load4(vertexId * layout.stride + layout.boneWeightsOffset));
}

#endif
