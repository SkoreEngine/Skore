#include "Common.hlsli"
#include "Materials.hlsli"

struct PushConstants
{
	matrix world;
	uint   materialIndex;
	uint   meshIndex;
	uint   vertexLayoutIndex;
	uint   pad;
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
	float4   position    : SV_POSITION;
	float3   normal      : NORMAL;
	float2   texCoord    : TEXCOORD0;
	float2   uv1         : TEXCOORD1;
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
	uint layoutIdx = pushConstants.vertexLayoutIndex;
	float3 inputPosition = GetVertexPosition(meshIdx, layoutIdx, vertexId);
	float3 inputNormal = GetVertexNormal(meshIdx, layoutIdx, vertexId);
	float4 inputTangent = GetVertexTangent(meshIdx, layoutIdx, vertexId);

#ifdef HAS_BONES
	float3 position = 0.0;
	float3 normal = 0.0;
	float3 tangent = 0.0;
	uint4 boneIndices = GetVertexBoneIndices(meshIdx, layoutIdx, vertexId);
	float4 boneWeights = GetVertexBoneWeights(meshIdx, layoutIdx, vertexId);

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

	output.texCoord = GetVertexUV(meshIdx, layoutIdx, vertexId);
	output.uv1 = GetVertexUV1(meshIdx, layoutIdx, vertexId);

	float3x3 normalMat = (float3x3)pushConstants.world;
	output.normal       = normalize(mul(normalMat, normal));

	float4 T = float4(normalize(mul(normalMat, tangent)), inputTangent.w);
	float3 B = T.w * cross(output.normal, T.xyz);
	output.TBN = transpose(float3x3(T.xyz, B, output.normal));

	output.color = GetVertexColor(meshIdx, layoutIdx, vertexId);

	output.viewPos     = cameraPosition;
	output.fragViewPos = mul(view, worldPosition).xyz;

	return output;
}

struct PSOutput
{
	float4 outAlbedoMetallic      :  SV_TARGET0;
	float2 outRoughnessAo         :  SV_TARGET1;
	float2 outNormal              :  SV_TARGET2;
	float3 outEmissive            :  SV_TARGET3;
};

[earlydepthstencil]
PSOutput MainPS(PixelInput input)
{
	MaterialSample material = SampleMaterial(pushConstants.materialIndex, input.texCoord, input.normal, input.TBN);

	PSOutput output = (PSOutput)0;
	output.outAlbedoMetallic    = float4(material.baseColor.rgb, material.metallic);
	output.outRoughnessAo       = float2(material.roughness, material.occlusion);
	output.outNormal            = OctohedralEncode(material.normal);
	output.outEmissive          = material.emissive;
	return output;
}
