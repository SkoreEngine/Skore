#ifndef SK_MATERIALS_HLSLI
#define SK_MATERIALS_HLSLI

#include "Bindings.hlsli"

struct MaterialSample
{
	float3     baseColor;
	float      alpha;
	float3     normal;
	float      roughness;
	float      metallic;
	float3     emissive;
	float      occlusion;
};

MaterialSample SampleMaterial(uint materialIndex, float2 texCoord, float3 worldNormal)
{
	MaterialSample material;
	MaterialData mat = MaterialDataBuffer[materialIndex];

	texCoord *= mat.uvScale;

	material.baseColor = mat.baseColor;
	material.alpha = 1.0;

	if (mat.baseColorTexture >= 0)
	{
		float4 texColor = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].Sample(LinearSampler, texCoord);
		material.baseColor = pow(texColor.rgb * mat.baseColor, 2.2);
		material.alpha = texColor.a;
	}

	if (mat.alphaMode == 2 && material.alpha < mat.alphaCutoff)
	{
		discard;
	}

	material.normal = worldNormal;

	material.roughness = mat.roughness;
	if (mat.roughnessTexture >= 0)
	{
		float4 value = BindlessTextures[NonUniformResourceIndex(mat.roughnessTexture)].Sample(LinearSampler, texCoord);
		material.roughness = value.g;
	}

	material.metallic = mat.metallic;
	if (mat.metallicTexture >= 0)
	{
		float4 value = BindlessTextures[NonUniformResourceIndex(mat.metallicTexture)].Sample(LinearSampler, texCoord);
		material.metallic = value.b;
	}

	material.emissive = mat.emissiveColor * mat.emissiveFactor;
	if (mat.emissiveTexture >= 0)
	{
		float3 emTex = BindlessTextures[NonUniformResourceIndex(mat.emissiveTexture)].Sample(LinearSampler, texCoord).rgb;
		material.emissive *= emTex;
	}
	material.occlusion = 1.0;

	return material;
}

MaterialSample SampleMaterial(uint materialIndex, float2 texCoord, float3 normal, float3x3 TBN)
{
	MaterialSample material = SampleMaterial(materialIndex, texCoord, normal);

	MaterialData mat = MaterialDataBuffer[materialIndex];
	if (mat.normalTexture >= 0)
	{
		float2 uv = texCoord * mat.uvScale;
		float3 n = BindlessTextures[NonUniformResourceIndex(mat.normalTexture)].Sample(LinearSampler, uv).rgb * 2.0 - 1.0;
		material.normal = normalize(mul(TBN, n));
	}

	return material;
}

MaterialSample SampleMaterialLevel(uint materialIndex, float2 texCoord, float3 worldNormal, float level)
{
	MaterialSample material;
	MaterialData mat = MaterialDataBuffer[materialIndex];

	texCoord *= mat.uvScale;

	material.baseColor = mat.baseColor;
	material.alpha = 1.0;

	if (mat.baseColorTexture >= 0)
	{
		float4 texColor = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].SampleLevel(LinearSampler, texCoord, level);
		material.baseColor = pow(texColor.rgb * mat.baseColor, 2.2);
		material.alpha = texColor.a;
	}

	material.normal = worldNormal;

	material.roughness = mat.roughness;
	if (mat.roughnessTexture >= 0)
	{
		float4 value = BindlessTextures[NonUniformResourceIndex(mat.roughnessTexture)].SampleLevel(LinearSampler, texCoord, level);
		material.roughness = value.g;
	}

	material.roughness = max(material.roughness, 0.002);

	material.metallic = mat.metallic;
	if (mat.metallicTexture >= 0)
	{
		float4 value = BindlessTextures[NonUniformResourceIndex(mat.metallicTexture)].SampleLevel(LinearSampler, texCoord, level);
		material.metallic = value.b;
	}

	material.emissive = mat.emissiveColor * mat.emissiveFactor;
	if (mat.emissiveTexture >= 0)
	{
		float3 emTex = BindlessTextures[NonUniformResourceIndex(mat.emissiveTexture)].SampleLevel(LinearSampler, texCoord, level).rgb;
		material.emissive *= emTex;
	}
	material.occlusion = 1.0;

	return material;
}

MaterialSample SampleMaterialLevel(uint materialIndex, float2 texCoord, float3 normal, float3x3 TBN, float level)
{
	MaterialSample material = SampleMaterialLevel(materialIndex, texCoord, normal, level);

	MaterialData mat = MaterialDataBuffer[materialIndex];
	if (mat.normalTexture >= 0)
	{
		float2 uv = texCoord * mat.uvScale;
		float3 n = BindlessTextures[NonUniformResourceIndex(mat.normalTexture)].SampleLevel(LinearSampler, uv, level).rgb * 2.0 - 1.0;
		material.normal = normalize(mul(TBN, n));
	}

	return material;
}

#endif
