#include "Skore://Shaders/Materials.hlsli"
#include "Skore://Shaders/SceneBindings.hlsli"
#include "Skore://Shaders/Lights.hlsli"
#include "Skore://Shaders/Tonemapping.hlsli"

struct PushConstants
{
	matrix world;
	uint   materialIndex;
	uint   vertexByteOffset;
	uint   vertexLayoutIndex;
	uint   ambientFlags;
	float3 ambientLight;
	float  ambientMultiplier;
	float  reflectionMultiplier;
};

[[vk::push_constant]] PushConstants pushConstants;

TextureCube  diffuseIrradianceMap : register(t0, space2);
TextureCube  specularMap          : register(t1, space2);
SamplerState iblSampler           : register(s2, space2);

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

	uint vboff = pushConstants.vertexByteOffset;
	uint layoutIdx = pushConstants.vertexLayoutIndex;
	float3 inputPosition = GetVertexPosition(vboff, layoutIdx, vertexId);
	float3 inputNormal = GetVertexNormal(vboff, layoutIdx, vertexId);
	float4 inputTangent = GetVertexTangent(vboff, layoutIdx, vertexId);

#ifdef HAS_BONES
	float3 position = 0.0;
	float3 normal = 0.0;
	float3 tangent = 0.0;
	uint4 boneIndices = GetVertexBoneIndices(vboff, layoutIdx, vertexId);
	float4 boneWeights = GetVertexBoneWeights(vboff, layoutIdx, vertexId);

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

	output.texCoord = GetVertexUV(vboff, layoutIdx, vertexId);

	float3x3 normalMat = (float3x3)pushConstants.world;
	output.normal       = normalize(mul(normalMat, normal));

	float4 T = float4(normalize(mul(normalMat, tangent)), inputTangent.w);
	float3 B = T.w * cross(output.normal, T.xyz);
	output.TBN = transpose(float3x3(T.xyz, B, output.normal));

	output.color = GetVertexColor(vboff, layoutIdx, vertexId);

	output.viewPos     = cameraPosition;
	output.fragViewPos = mul(view, worldPosition).xyz;

	return output;
}

struct PixelOutput
{
	float4 color : SV_TARGET0;
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

	float3 color = SampleLights(lightData);

	float3 N = material.normal;
	float3 V = normalize(input.viewPos - input.worldPos);
	float  nDotV = max(dot(N, V), 0.0);

	float3 F0 = lerp(0.04, baseColor, material.metallic);

	float3 ambient = 0.0;

	if (pushConstants.ambientFlags & SK_LIGHT_FLAGS_HAS_AMBIENT_TEXTURE)
	{
		float3 irradiance = diffuseIrradianceMap.SampleLevel(iblSampler, N, 0.0).rgb;
		float3 kd = lerp(1.0 - F0, 0.0, material.metallic);
		ambient += kd * baseColor * irradiance * pushConstants.ambientMultiplier;
	}
	else if (pushConstants.ambientFlags & SK_LIGHT_FLAGS_HAS_AMBIENT_COLOR)
	{
		ambient += baseColor * pushConstants.ambientLight * pushConstants.ambientMultiplier;
	}

	if (pushConstants.ambientFlags & SK_LIGHT_FLAGS_HAS_REFLECTION_TEXTURE)
	{
		uint width, height, levels;
		specularMap.GetDimensions(0, width, height, levels);
		float3 R = reflect(-V, N);
		float3 prefiltered = specularMap.SampleLevel(iblSampler, R, material.roughness * float(levels - 1)).rgb;
		float3 F = FresnelSchlickRoughness(nDotV, F0, material.roughness);
		ambient += prefiltered * F * pushConstants.reflectionMultiplier;
	}

	color += ambient * material.occlusion + material.emissive;

	color = ApplyTonemapping(color, 1.4);

	PixelOutput output;
	output.color = float4(color, 1.0);
	return output;
}
