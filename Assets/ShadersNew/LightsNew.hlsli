#ifndef SK_LIGHTS_NEW_HLSLI
#define SK_LIGHTS_NEW_HLSLI

//Layout mirrored in Runtime/Source/Skore/Graphics/PipelineNew/PipelineCommonNew.hpp,
//bindings filled by LightSetupPassNew into the scene descriptor set (space1).

#define SK_MAX_CASCADES 4

#define SK_LIGHT_FLAGS_HAS_AMBIENT_TEXTURE    (1 << 1)
#define SK_LIGHT_FLAGS_HAS_AMBIENT_COLOR      (1 << 2)
#define SK_LIGHT_FLAGS_HAS_REFLECTION_TEXTURE (1 << 3)

static const float SK_PI = 3.14159265359;
static const float SK_EPSILON = 0.0001;

struct Light
{
	uint   type;
	float3 position;
	float4 direction;
	float4 color;
	float  intensity;
	float  range;
	float  innerConeAngle;
	float  outerConeAngle;
};

cbuffer LightBuffer : register(b2, space1)
{
	uint     lightCount;
	uint     shadowLightIndex;
	uint     lightFlags;
	float    ambientMultiplier;

	float3   ambientLight;
	float    reflectionMultiplier;

	float4   cascadeSplits;
	float4x4 cascadeViewProjMat[SK_MAX_CASCADES];
};

StructuredBuffer<Light> lights : register(t10, space1);

Texture2DArray         SkShadowMap     : register(t3, space1);
SamplerComparisonState SkShadowSampler : register(s4, space1);

TextureCube  SkDiffuseIrradianceMap : register(t5, space1);
TextureCube  SkSpecularMap          : register(t7, space1);
Texture2D    SkBrdfLut              : register(t8, space1);
SamplerState SkEnvSampler           : register(s9, space1);

#define SK_SHADOW_PCF_RANGE 1

static const float4x4 SK_ShadowBiasMat = float4x4(
	0.5, 0.0, 0.0, 0.5,
	0.0, 0.5, 0.0, 0.5,
	0.0, 0.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0
);

float SampleShadowTap(float2 sampleUV, float sampleDepth, uint cascadeIndex)
{
	if (sampleDepth > -1.0 && sampleDepth < 1.0)
	{
		return SkShadowMap.SampleCmpLevelZero(SkShadowSampler, float3(sampleUV, cascadeIndex), sampleDepth);
	}
	return 1.0;
}

float SampleShadowCascade(float3 worldPos, float3 N, uint cascadeIndex)
{
	float3 lightDir = normalize(-lights[shadowLightIndex].direction.xyz);
	float  nDotL = dot(N, lightDir);

	float  normalBias = 0.004 * saturate(1.0 - nDotL);
	float3 biasedPos = worldPos + N * normalBias;

	float4 shadowCoord4 = mul(SK_ShadowBiasMat, mul(cascadeViewProjMat[cascadeIndex], float4(biasedPos, 1.0)));
	float3 shadowCoord = shadowCoord4.xyz / shadowCoord4.w;

	float nDotLClamp = max(saturate(nDotL), 0.05);
	float slopeTan = sqrt(1.0 - nDotLClamp * nDotLClamp) / nDotLClamp;
	float depthBias = 0.00015 + 0.00035 * slopeTan;
	depthBias = min(depthBias, 0.0015);

	int3 texDim;
	SkShadowMap.GetDimensions(texDim.x, texDim.y, texDim.z);
	float2 texelSize = 1.0 / float2(texDim.x, texDim.y);

	float shadowFactor = 0.0;
	int   count = 0;

	[unroll]
	for (int y = -SK_SHADOW_PCF_RANGE; y <= SK_SHADOW_PCF_RANGE; y++)
	{
		[unroll]
		for (int x = -SK_SHADOW_PCF_RANGE; x <= SK_SHADOW_PCF_RANGE; x++)
		{
			shadowFactor += SampleShadowTap(shadowCoord.xy + float2(x, y) * texelSize, shadowCoord.z - depthBias, cascadeIndex);
			count++;
		}
	}
	return shadowFactor / count;
}

//cascadeSplits are negative view-space depths; fragViewZ = mul(view, worldPos).z
float SampleDirectionalShadow(float3 worldPos, float3 N, float fragViewZ)
{
	if (shadowLightIndex == 0xFFFFFFFF)
	{
		return 1.0;
	}

	float maxShadowZ = cascadeSplits[SK_MAX_CASCADES - 1];
	if (fragViewZ <= maxShadowZ)
	{
		return 1.0;
	}

	uint cascadeIndex = 0;
	for (uint i = 0; i < SK_MAX_CASCADES - 1; ++i)
	{
		if (fragViewZ < cascadeSplits[i])
		{
			cascadeIndex = i + 1;
		}
	}

	float shadow = SampleShadowCascade(worldPos, N, cascadeIndex);

	if (cascadeIndex < SK_MAX_CASCADES - 1)
	{
		float splitZ = cascadeSplits[cascadeIndex];
		float blendDist = abs(splitZ) * 0.1;
		float blendStart = splitZ + blendDist;
		float blendFactor = saturate((blendStart - fragViewZ) / blendDist);
		if (blendFactor > 0.0)
		{
			float nextShadow = SampleShadowCascade(worldPos, N, cascadeIndex + 1);
			shadow = lerp(shadow, nextShadow, blendFactor);
		}
	}

	float fadeRange = abs(maxShadowZ) * 0.03;
	float distanceFade = smoothstep(maxShadowZ, maxShadowZ + fadeRange, fragViewZ);
	return lerp(1.0, shadow, distanceFade);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float ndh = saturate(dot(N, H));
	float denom = ndh * ndh * (a2 - 1.0) + 1.0;
	return a2 / (SK_PI * denom * denom + 1e-7);
}

float GeometrySchlickGGX(float ndx, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return ndx / (ndx * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
	return GeometrySchlickGGX(saturate(dot(N, V)), roughness) * GeometrySchlickGGX(saturate(dot(N, L)), roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	float3 r = 1.0 - roughness;
	return F0 + (max(r, F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float GetRangeAttenuation(float distance, float range)
{
	float distanceSq = max(distance * distance, SK_EPSILON);
	float attenuation = rcp(distanceSq);

	if (range > 0.0)
	{
		float distanceOverRange = distance / max(range, SK_EPSILON);
		float distanceOverRangeSq = distanceOverRange * distanceOverRange;
		attenuation *= saturate(1.0 - distanceOverRangeSq * distanceOverRangeSq);
	}

	return attenuation;
}

float GetSpotAttenuation(Light light, float3 lightDir)
{
	float innerAngle = min(abs(light.innerConeAngle), abs(light.outerConeAngle));
	float outerAngle = max(abs(light.innerConeAngle), abs(light.outerConeAngle));
	float innerConeCos = cos(innerAngle);
	float outerConeCos = cos(outerAngle);
	float cosRange = max(innerConeCos - outerConeCos, SK_EPSILON);
	float theta = dot(lightDir, normalize(-light.direction.xyz));
	return saturate((theta - outerConeCos) / cosRange);
}

//Unshadowed radiance contribution from a single light; the caller applies shadowing.
float3 EvaluateDirectLighting(Light light, float3 N, float3 V, float3 worldPos, float3 baseColor, float roughness, float metallic, float3 F0)
{
	float3 lightDir;
	float  attenuation;

	switch (light.type)
	{
		case 0: // Directional
			lightDir = normalize(-light.direction.xyz);
			attenuation = 1.0;
			break;
		case 1: // Point
		{
			float3 toLight = light.position - worldPos;
			lightDir = normalize(toLight);
			attenuation = GetRangeAttenuation(length(toLight), light.range);
			break;
		}
		default: // Spot
		{
			float3 toLight = light.position - worldPos;
			lightDir = normalize(toLight);
			attenuation = GetRangeAttenuation(length(toLight), light.range) * GetSpotAttenuation(light, lightDir);
			break;
		}
	}

	float  ndl = saturate(dot(N, lightDir));
	float3 H = normalize(V + lightDir);

	float  d = DistributionGGX(N, H, roughness);
	float  g = GeometrySmith(N, V, lightDir, roughness);
	float3 f = FresnelSchlick(saturate(dot(H, V)), F0);

	float3 specular = (d * g * f) / max(4.0 * saturate(dot(N, V)) * ndl, SK_EPSILON);
	float3 kd = (1.0 - f) * (1.0 - metallic);

	float3 radiance = light.color.xyz * light.intensity * attenuation;
	return (kd * baseColor + specular) * radiance * ndl;
}

//Image-based ambient term: diffuse irradiance (cubemap or flat color) plus
//split-sum specular reflections from the prefiltered HDR environment.
float3 EvaluateAmbient(float3 N, float3 V, float3 baseColor, float metallic, float roughness, float occlusion, float3 F0)
{
	float  cosLo = saturate(dot(N, V));
	float3 Lr = 2.0 * cosLo * N - V;

	float3 diffuseIBL = float3(0.0, 0.0, 0.0);

	[branch]
	if (lightFlags & SK_LIGHT_FLAGS_HAS_AMBIENT_TEXTURE)
	{
		float3 irradiance = SkDiffuseIrradianceMap.SampleLevel(SkEnvSampler, N, 0.0).rgb;
		float3 kd = lerp(1.0 - F0, 0.0, metallic);
		diffuseIBL = kd * baseColor * irradiance * ambientMultiplier;
	}
	else if (lightFlags & SK_LIGHT_FLAGS_HAS_AMBIENT_COLOR)
	{
		diffuseIBL = baseColor * ambientLight * ambientMultiplier;
	}

	float3 specularIBL = float3(0.0, 0.0, 0.0);

	[branch]
	if (lightFlags & SK_LIGHT_FLAGS_HAS_REFLECTION_TEXTURE)
	{
		uint width, height, levels;
		SkSpecularMap.GetDimensions(0, width, height, levels);

		float3 F = FresnelSchlickRoughness(cosLo, F0, roughness);
		float3 specularIrradiance = SkSpecularMap.SampleLevel(SkEnvSampler, Lr, roughness * levels).rgb;
		float2 specularBRDF = SkBrdfLut.SampleLevel(SkEnvSampler, float2(cosLo, roughness), 0).rg;
		specularIBL = (specularIrradiance * (F * specularBRDF.x + specularBRDF.y)) * reflectionMultiplier;
	}

	return (diffuseIBL + specularIBL) * occlusion;
}

#endif
