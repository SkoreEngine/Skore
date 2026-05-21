#include "Common.hlsli"
#include "VertexPulling.hlsli"

// Per-pipeline (per-cascade) camera buffer.
cbuffer CameraBuffer : register(b0, space2)
{
	matrix viewProjection;
};

struct PushConstants
{
	matrix world;
	uint   vertexByteOffset;
	uint   vertexLayoutIndex;
	uint2  pad;
};

[[vk::push_constant]] PushConstants pushConstants;

#ifdef HAS_BONES
cbuffer SkinnedBuffer : register(b0, space3)
{
    matrix boneMatrices[SK_MAX_BONES];
};
#endif

struct PixelInput
{
    float4 position : SV_POSITION;
};

PixelInput MainVS(uint vertexId : SV_VertexID)
{
	PixelInput output;

	uint vboff = pushConstants.vertexByteOffset;
	uint layoutIdx = pushConstants.vertexLayoutIndex;
	float3 inputPosition = GetVertexPosition(vboff, layoutIdx, vertexId);

#ifdef HAS_BONES
	float3 position = 0.0;
	uint4 boneIndices = GetVertexBoneIndices(vboff, layoutIdx, vertexId);
	float4 boneWeights = GetVertexBoneWeights(vboff, layoutIdx, vertexId);

	[unroll]
	for (int i = 0; i < 4; i++)
	{
		float weight = boneWeights[i];
		matrix boneTransform = boneMatrices[boneIndices[i]];

		float4 localPosition = mul(boneTransform, float4(inputPosition, 1.0f));
		position += localPosition.xyz * weight;
	}
#else
	float3 position = inputPosition;
#endif

	float4 worldPosition = mul(pushConstants.world, float4(position, 1.0f));
	output.position = mul(viewProjection, worldPosition);

    return output;
}

void MainPS(PixelInput input)
{
    //nothing
}
