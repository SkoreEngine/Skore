#include "Materials.hlsli"
#include "Lights.hlsli"

struct PushConstants
{
	matrix world;
	uint   materialIndex;
	uint   meshIndex;
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
	float4   position    : SV_POSITION;
	float3   normal      : NORMAL;
	float2   texCoord    : TEXCOORD0;
	float3   color       : COLOR;
	float3x3 TBN         : TBN1;
	float3   worldPos    : POSITION1;
	float3   viewPos     : POSITION2;
	float3   fragViewPos : POSITION3;
};

PixelInput MainVS(uint vertexId : SV_VertexID)
{
	PixelInput output;

	uint meshIdx = pushConstants.meshIndex;
	float3 inputPosition = GetVertexPosition(meshIdx, vertexId);
	float3 inputNormal = GetVertexNormal(meshIdx, vertexId);
	float4 inputTangent = GetVertexTangent(meshIdx, vertexId);

#ifdef HAS_BONES
	float3 position = 0.0;
	float3 normal = 0.0;
	float3 tangent = 0.0;
	uint4 boneIndices = GetVertexBoneIndices(meshIdx, vertexId);
	float4 boneWeights = GetVertexBoneWeights(meshIdx, vertexId);

	[unroll]
	for (int i = 0; i < 4; i++)
	{
		float weight = boneWeights[i];
		matrix boneTransform = boneMatrices[boneIndices[i]];

		float4 localPosition = mul(boneTransform, float4(inputPosition, 1.0f));
		position += localPosition.xyz * weight;

		float3 localNormal = mul((float3x3)boneTransform, inputNormal);
		normal += localNormal * weight;

		float3 localTangent = mul((float3x3)boneTransform, inputTangent.xyz);
		tangent += localTangent * weight;
	}

	normal = normalize(normal);
	tangent = normalize(tangent);
#else
	float3 position = inputPosition;
	float3 normal = inputNormal;
	float3 tangent = inputTangent.xyz;
#endif

	float4 worldPosition = mul(pushConstants.world, float4(position, 1.0f));
	output.position      = mul(viewProjection, worldPosition);
	output.worldPos      = worldPosition.xyz;

	output.texCoord = GetVertexUV(meshIdx, vertexId);

	float3x3 normalMat = (float3x3)pushConstants.world;
	output.normal       = normalize(mul(normalMat, normal));

	float4 T = float4(normalize(mul(normalMat, tangent)), inputTangent.w);
	float3 B = T.w * cross(output.normal, T.xyz);
	output.TBN = transpose(float3x3(T.xyz, B, output.normal));

	output.color = GetVertexColor(meshIdx, vertexId);

	output.viewPos     = cameraPosition;
	output.fragViewPos = mul(view, worldPosition).xyz;

	return output;
}

struct PixelOutput
{
	float4 color  : SV_TARGET0;
};

PixelOutput MainPS(PixelInput input)
{
	MaterialSample material = SampleMaterial(pushConstants.materialIndex, input.texCoord, input.normal, input.TBN);

	float3 baseColor = material.baseColor * input.color;

	LightPixelData lightData;
	lightData.baseColor   = baseColor;
	lightData.normal      = material.normal;
	lightData.roughness   = material.roughness;
	lightData.metallic    = material.metallic;
	lightData.viewPos     = input.viewPos;
	lightData.fragViewPos = input.fragViewPos;
	lightData.worldPos    = input.worldPos;

	float3 color = SampleLights(lightData) + material.emissive;

	PixelOutput output;
	output.color = float4(color, material.alpha);

	return output;
}
