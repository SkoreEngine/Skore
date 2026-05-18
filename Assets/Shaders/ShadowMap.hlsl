#include "Common.hlsli"
#include "VertexPulling.hlsli"

cbuffer CameraBuffer
{
	matrix viewProjection;
};

struct PushConstants
{
	matrix world;
	uint   meshIndex;
	uint   vertexLayoutIndex;
	uint2  pad;
};

[[vk::push_constant]] PushConstants pushConstants;

#ifdef HAS_BONES
cbuffer SkinnedBuffer : register(b0, space1)
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

	uint meshIdx = pushConstants.meshIndex;
	uint layoutIdx = pushConstants.vertexLayoutIndex;
	float3 inputPosition = GetVertexPosition(meshIdx, layoutIdx, vertexId);

#ifdef HAS_BONES
	float3 position = 0.0;
	uint4 boneIndices = GetVertexBoneIndices(meshIdx, layoutIdx, vertexId);
	float4 boneWeights = GetVertexBoneWeights(meshIdx, layoutIdx, vertexId);

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
