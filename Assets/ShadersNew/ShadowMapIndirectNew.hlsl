#include "SceneBindingsNew.hlsli"
#include "GlobalBindingsNew.hlsli"

cbuffer CascadeBuffer : register(b0, space0)
{
	matrix cascadeViewProjection;
	uint   disableMask;
	uint3  cascadePad;
};

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

	uint vboff = instance.vertexByteOffset;
	uint layoutIdx = instance.vertexLayoutIndex;

	float3 position = GetVertexPosition(vboff, layoutIdx, vertexId);

	float4 worldPosition = mul(instance.transform, float4(position, 1.0));
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
			alpha = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].Sample(Samplers[mat.GetBaseColorSamplerIndex()], uv).a;
		}
		if (alpha < mat.alphaCutoff)
		{
			discard;
		}
	}
#endif
}
