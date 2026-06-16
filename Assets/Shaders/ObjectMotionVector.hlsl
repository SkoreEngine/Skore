#include "Common.hlsli"
#include "GlobalBindings.hlsli"

struct DrawData
{
	matrix currentModelViewProjection;
	matrix currentModelViewProjectionNoJitter;
	matrix previousModelViewProjectionNoJitter;
	uint   materialIndex;
	uint   vertexByteOffset;
	uint   vertexLayoutIndex;
	uint   pad;
};

StructuredBuffer<DrawData> DrawDataBuffer : register(t0, space2);

struct PushConstants
{
	uint drawDataIndex;
};

[[vk::push_constant]] PushConstants pushConstants;

struct PixelInput
{
	float4 position        : SV_POSITION;
	float4 currentPosition : POSITION0;
	float4 previousPosition: POSITION1;
	float2 texCoord        : TEXCOORD0;
};

PixelInput MainVS(uint vertexId : SV_VertexID)
{
	PixelInput output;
	DrawData drawData = DrawDataBuffer[pushConstants.drawDataIndex];

	uint vboff = drawData.vertexByteOffset;
	uint layoutIdx = drawData.vertexLayoutIndex;

	float4 localPosition = float4(GetVertexPosition(vboff, layoutIdx, vertexId), 1.0f);
	output.position = mul(drawData.currentModelViewProjection, localPosition);
	output.currentPosition = mul(drawData.currentModelViewProjectionNoJitter, localPosition);
	output.previousPosition = mul(drawData.previousModelViewProjectionNoJitter, localPosition);
	output.texCoord = GetVertexUV(vboff, layoutIdx, vertexId);

	return output;
}

void ClipMaterialAlpha(uint materialIndex, float2 texCoord)
{
	MaterialData material = MaterialDataBuffer[materialIndex];
	if (material.alphaMode != 2)
	{
		return;
	}

	float alpha = 1.0f;
	if (material.baseColorTexture >= 0)
	{
		float2 uv = texCoord * material.uvScale;
		alpha = BindlessTextures[NonUniformResourceIndex(material.baseColorTexture)].Sample(samplers[material.GetBaseColorSamplerIndex()], uv).a;
	}

	if (alpha < material.alphaCutoff)
	{
		discard;
	}
}

float2 MainPS(PixelInput input) : SV_TARGET0
{
	DrawData drawData = DrawDataBuffer[pushConstants.drawDataIndex];
	ClipMaterialAlpha(drawData.materialIndex, input.texCoord);

	float2 currentNdc = input.currentPosition.xy / input.currentPosition.w;
	float2 previousNdc = input.previousPosition.xy / input.previousPosition.w;

	return (currentNdc - previousNdc) * float2(0.5f, -0.5f);
}
