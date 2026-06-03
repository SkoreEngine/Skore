#ifndef SK_MATERIALS_HLSLI
#define SK_MATERIALS_HLSLI

#include "GlobalBindings.hlsli"

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

inline float GetLod(in uint2 dim, in float2 uv_dx, in float2 uv_dy)
{
	return log2(max(length(uv_dx * dim), length(uv_dy * dim)));
}

//credits https://wickedengine.net/2024/06/texture-streaming/
inline void WriteMipmapFeedback(uint materialIndex, float4 uvsets_dx, float4 uvsets_dy)
{
	const float lod_uvset0 = GetLod(65536u, uvsets_dx.xy, uvsets_dy.xy);
	const float lod_uvset1 = GetLod(65536u, uvsets_dx.zw, uvsets_dy.zw);
	const uint resolution0 = 65536u >> uint(max(0, lod_uvset0));
	const uint resolution1 = 65536u >> uint(max(0, lod_uvset1));
	const uint mask = resolution0 | (resolution1 << 16u);
	const uint waveMask = WaveActiveBitOr(mask);
	if(WaveIsFirstLane())
	{
		InterlockedOr(MaterialMaskBuffer[materialIndex], waveMask);
	}
}

MaterialSample SampleMaterial(uint materialIndex, float2 texCoord, float3 worldNormal)
{
	MaterialSample material;
	MaterialData mat = MaterialDataBuffer[materialIndex];

	texCoord *= mat.uvScale;

	material.baseColor = mat.baseColor;
	material.alpha = 1.0;

	if (mat.baseColorTexture >= 0)
	{
		float4 texColor = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].Sample(samplers[mat.GetBaseColorSamplerIndex()], texCoord);
		material.baseColor = pow(texColor.rgb * mat.baseColor, 2.2);
		material.alpha = texColor.a;
	}

	if (mat.alphaMode == 2 && material.alpha < mat.alphaCutoff)
	{
		discard;
	}

    WriteMipmapFeedback(materialIndex, ddx_coarse(float4(texCoord, 0, 0)), ddy_coarse(float4(texCoord, 0, 0)));

	material.normal = worldNormal;

	material.roughness = mat.roughness;
	if (mat.roughnessTexture >= 0)
	{
		float4 value = BindlessTextures[NonUniformResourceIndex(mat.roughnessTexture)].Sample(samplers[mat.GetRoughnessSamplerIndex()], texCoord);
		material.roughness = value.g;
	}

	material.metallic = mat.metallic;
	if (mat.metallicTexture >= 0)
	{
		float4 value = BindlessTextures[NonUniformResourceIndex(mat.metallicTexture)].Sample(samplers[mat.GetMetallicSamplerIndex()], texCoord);
		material.metallic = value.b;
	}

	material.emissive = mat.emissiveColor * mat.emissiveFactor;
	if (mat.emissiveTexture >= 0)
	{
		float3 emTex = BindlessTextures[NonUniformResourceIndex(mat.emissiveTexture)].Sample(samplers[mat.GetEmissiveSamplerIndex()], texCoord).rgb;
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
		float3 n = BindlessTextures[NonUniformResourceIndex(mat.normalTexture)].Sample(samplers[mat.GetNormalSamplerIndex()], uv).rgb * 2.0 - 1.0;
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
		float4 texColor = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].SampleLevel(samplers[mat.GetBaseColorSamplerIndex()], texCoord, level);
		material.baseColor = pow(texColor.rgb * mat.baseColor, 2.2);
		material.alpha = texColor.a;
	}

	material.normal = worldNormal;

	material.roughness = mat.roughness;
	if (mat.roughnessTexture >= 0)
	{
		float4 value = BindlessTextures[NonUniformResourceIndex(mat.roughnessTexture)].SampleLevel(samplers[mat.GetRoughnessSamplerIndex()], texCoord, level);
		material.roughness = value.g;
	}

	material.roughness = max(material.roughness, 0.002);

	material.metallic = mat.metallic;
	if (mat.metallicTexture >= 0)
	{
		float4 value = BindlessTextures[NonUniformResourceIndex(mat.metallicTexture)].SampleLevel(samplers[mat.GetMetallicSamplerIndex()], texCoord, level);
		material.metallic = value.b;
	}

	material.emissive = mat.emissiveColor * mat.emissiveFactor;
	if (mat.emissiveTexture >= 0)
	{
		float3 emTex = BindlessTextures[NonUniformResourceIndex(mat.emissiveTexture)].SampleLevel(samplers[mat.GetEmissiveSamplerIndex()], texCoord, level).rgb;
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
		float3 n = BindlessTextures[NonUniformResourceIndex(mat.normalTexture)].SampleLevel(samplers[mat.GetNormalSamplerIndex()], uv, level).rgb * 2.0 - 1.0;
		material.normal = normalize(mul(TBN, n));
	}

	return material;
}

float3 TriplanarBlendWeights(float3 worldNormal)
{
	float3 w = pow(abs(worldNormal), 4.0);
	return w / max(w.x + w.y + w.z, 0.0001);
}

float3 SampleAlbedoTriplanar(int textureIdx, float3 worldPos, float3 weights, float2 uvScale, uint samplerIndex)
{
	float2 uvX = worldPos.zy * uvScale;
	float2 uvY = worldPos.xz * uvScale;
	float2 uvZ = worldPos.xy * uvScale;

	float3 cx = BindlessTextures[NonUniformResourceIndex(textureIdx)].Sample(samplers[samplerIndex], uvX).rgb;
	float3 cy = BindlessTextures[NonUniformResourceIndex(textureIdx)].Sample(samplers[samplerIndex], uvY).rgb;
	float3 cz = BindlessTextures[NonUniformResourceIndex(textureIdx)].Sample(samplers[samplerIndex], uvZ).rgb;

	return cx * weights.x + cy * weights.y + cz * weights.z;
}

float SampleChannelTriplanar(int textureIdx, float3 worldPos, float3 weights, float2 uvScale, uint channel, uint samplerIndex)
{
	float2 uvX = worldPos.zy * uvScale;
	float2 uvY = worldPos.xz * uvScale;
	float2 uvZ = worldPos.xy * uvScale;

	float vx = BindlessTextures[NonUniformResourceIndex(textureIdx)].Sample(samplers[samplerIndex], uvX)[channel];
	float vy = BindlessTextures[NonUniformResourceIndex(textureIdx)].Sample(samplers[samplerIndex], uvY)[channel];
	float vz = BindlessTextures[NonUniformResourceIndex(textureIdx)].Sample(samplers[samplerIndex], uvZ)[channel];

	return vx * weights.x + vy * weights.y + vz * weights.z;
}

float3 SampleNormalTriplanar(int textureIdx, float3 worldPos, float3 worldNormal, float3 weights, float2 uvScale, uint samplerIndex)
{
	float2 uvX = worldPos.zy * uvScale;
	float2 uvY = worldPos.xz * uvScale;
	float2 uvZ = worldPos.xy * uvScale;

	float3 nx = BindlessTextures[NonUniformResourceIndex(textureIdx)].Sample(samplers[samplerIndex], uvX).rgb * 2.0 - 1.0;
	float3 ny = BindlessTextures[NonUniformResourceIndex(textureIdx)].Sample(samplers[samplerIndex], uvY).rgb * 2.0 - 1.0;
	float3 nz = BindlessTextures[NonUniformResourceIndex(textureIdx)].Sample(samplers[samplerIndex], uvZ).rgb * 2.0 - 1.0;

	float3 tnX = float3(0.0, nx.y, nx.x) + worldNormal;
	float3 tnY = float3(ny.x, 0.0, ny.y) + worldNormal;
	float3 tnZ = float3(nz.x, nz.y, 0.0) + worldNormal;

	return normalize(tnX * weights.x + tnY * weights.y + tnZ * weights.z);
}

inline void WriteMipmapFeedbackTriplanar(uint materialIndex, float3 worldPos, float2 uvScale)
{
	float3 dpdx = ddx_coarse(worldPos);
	float3 dpdy = ddy_coarse(worldPos);

	const float lodX = GetLod(65536u, dpdx.zy * uvScale, dpdy.zy * uvScale);
	const float lodY = GetLod(65536u, dpdx.xz * uvScale, dpdy.xz * uvScale);
	const float lodZ = GetLod(65536u, dpdx.xy * uvScale, dpdy.xy * uvScale);
	const float lod  = min(lodX, min(lodY, lodZ));

	const uint resolution = 65536u >> uint(max(0, lod));
	const uint waveMask   = WaveActiveBitOr(resolution);
	if (WaveIsFirstLane())
	{
		InterlockedOr(MaterialMaskBuffer[materialIndex], waveMask);
	}
}

MaterialSample SampleMaterialTriplanar(uint materialIndex, float3 worldPos, float3 worldNormal, float3 weights)
{
	MaterialSample material;
	MaterialData   mat = MaterialDataBuffer[materialIndex];

	material.baseColor = mat.baseColor;
	material.alpha = 1.0;

	if (mat.baseColorTexture >= 0)
	{
		float3 sampled = SampleAlbedoTriplanar(mat.baseColorTexture, worldPos, weights, mat.uvScale, mat.GetBaseColorSamplerIndex());
		material.baseColor = pow(sampled * mat.baseColor, 2.2);
	}

	if (mat.alphaMode == 2 && material.alpha < mat.alphaCutoff)
	{
		discard;
	}

	WriteMipmapFeedbackTriplanar(materialIndex, worldPos, mat.uvScale);

	material.normal = worldNormal;
	if (mat.normalTexture >= 0)
	{
		material.normal = SampleNormalTriplanar(mat.normalTexture, worldPos, worldNormal, weights, mat.uvScale, mat.GetNormalSamplerIndex());
	}

	material.roughness = mat.roughness;
	if (mat.roughnessTexture >= 0)
	{
		material.roughness = SampleChannelTriplanar(mat.roughnessTexture, worldPos, weights, mat.uvScale, 1, mat.GetRoughnessSamplerIndex());
	}
	material.roughness = max(material.roughness, 0.002);

	material.metallic = mat.metallic;
	if (mat.metallicTexture >= 0)
	{
		material.metallic = SampleChannelTriplanar(mat.metallicTexture, worldPos, weights, mat.uvScale, 2, mat.GetMetallicSamplerIndex());
	}

	material.emissive = mat.emissiveColor * mat.emissiveFactor;
	if (mat.emissiveTexture >= 0)
	{
		float3 emTex = SampleAlbedoTriplanar(mat.emissiveTexture, worldPos, weights, mat.uvScale, mat.GetEmissiveSamplerIndex());
		material.emissive *= emTex;
	}
	material.occlusion = 1.0;

	return material;
}

#endif
