#include "Common.hlsli"
#include "SceneBindings.hlsli"
#include "VertexPulling.hlsli"
#ifdef HAS_MASK
#include "GlobalBindings.hlsli"
#endif

cbuffer CameraBuffer : register(b0, space2)
{
	matrix cascadeViewProjection;
	uint   disableMask;
	uint3  cameraPad;
};

#ifdef HAS_BONES
StructuredBuffer<float4x4> BoneMatrices[] : register(t0, space3);
#endif

struct PixelInput
{
    float4 position : SV_POSITION;
#ifdef HAS_MASK
    float2 texCoord : TEXCOORD0;
    nointerpolation uint matIndex : MATERIAL1;
#endif
};

PixelInput MainVS(uint vertexId : SV_VertexID, [[vk::builtin("BaseInstance")]] uint idx : SV_StartInstanceLocation)
{
	PixelInput output;

	InstanceData instance = instances[NonUniformResourceIndex(idx)];

	uint vboff     = instance.vertexByteOffset;
	uint layoutIdx = instance.vertexLayoutIndex;

	float3 inputPosition = GetVertexPosition(vboff, layoutIdx, vertexId);

#ifdef HAS_BONES
	float3 position = inputPosition;
	if (instance.boneBufferIndex != 0xFFFFFFFF)
	{
		position = 0.0;
		uint4 boneIndices = GetVertexBoneIndices(vboff, layoutIdx, vertexId);
		float4 boneWeights = GetVertexBoneWeights(vboff, layoutIdx, vertexId);

		[unroll]
		for (int i = 0; i < 4; i++)
		{
			float weight = boneWeights[i];
			matrix boneTransform = BoneMatrices[NonUniformResourceIndex(instance.boneBufferIndex)][boneIndices[i]];

			float4 localPosition = mul(boneTransform, float4(inputPosition, 1.0f));
			position += localPosition.xyz * weight;
		}
	}
#else
	float3 position = inputPosition;
#endif

	float4 worldPosition = mul(instance.transform, float4(position, 1.0f));
	output.position = mul(cascadeViewProjection, worldPosition);

#ifdef HAS_MASK
	output.texCoord = GetVertexUV(vboff, layoutIdx, vertexId);
	output.matIndex = instance.materialIndex;
#endif

    return output;
}

void MainPS(PixelInput input)
{
#ifdef HAS_MASK
	if (disableMask == 0)
	{
		MaterialData mat = MaterialDataBuffer[input.matIndex];
		float2 uv = input.texCoord * mat.uvScale;
		float  alpha = 1.0;
		if (mat.baseColorTexture >= 0)
		{
			alpha = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].Sample(samplers[mat.GetBaseColorSamplerIndex()], uv).a;
		}
		if (alpha < mat.alphaCutoff)
		{
			discard;
		}
	}
#endif
}
