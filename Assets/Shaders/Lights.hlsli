#ifndef SK_LIGHTS_HLSLI
#define SK_LIGHTS_HLSLI

#include "SceneBindings.hlsli"
#include "LightCommon.hlsli"

//#define CASCADE_DEBUG

static const float4x4 biasMat = float4x4(
	0.5, 0.0, 0.0, 0.5,
	0.0, 0.5, 0.0, 0.5,
	0.0, 0.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0
);

float SampleShadowTap(float2 sampleUV, float sampleDepth, uint cascadeIndex)
{
	if (sampleDepth > -1.0 && sampleDepth < 1.0)
	{
#if SK_USE_COMPARISON_STATE
		return shadowMapTexture.SampleCmpLevelZero(shadowMapSampler, float3(sampleUV, cascadeIndex), sampleDepth).r;
#else
		float dist = shadowMapTexture.Sample(shadowMapSampler, float3(sampleUV, cascadeIndex)).r;
		if (dist < sampleDepth)
		{
			return 0.0;
		}
#endif
	}
	return 1.0;
}

struct LightPixelData
{
	float3 baseColor;
	float3 normal;
	float  roughness;
	float  metallic;
	float3 viewPos;
	float3 fragViewPos;
	float3 worldPos;
};

float SampleShadowCascade(float3 worldPos, float3 N, uint cascadeIndex)
{
	float3 lightDir = normalize(-lights[shadowLightIndex].direction.xyz);
	float  nDotL = dot(N, lightDir);

	float  normalBias = 0.02 * saturate(1.0 - nDotL);
	float3 biasedPos = worldPos + N * normalBias;

	float4 shadowCoord4 = mul(biasMat, mul(cascadeViewProjMat[cascadeIndex], float4(biasedPos, 1.0)));
	float3 shadowCoord  = shadowCoord4.xyz / shadowCoord4.w;

	float nDotLClamp = max(saturate(nDotL), 0.05);
	float slopeTan   = sqrt(1.0 - nDotLClamp * nDotLClamp) / nDotLClamp;
	float depthBias  = 0.0008 + 0.0015 * slopeTan;
	depthBias        = min(depthBias, 0.01);

	int3 texDim;
	shadowMapTexture.GetDimensions(texDim.x, texDim.y, texDim.z);
	float2 texelSize = 1.0 / float2(texDim.x, texDim.y);

	float shadowFactor = 0.0;
	int count = 0;
	int range = SK_PCF_RANGE;

	[unroll]
	for (int y = -range; y <= range; y++)
	{
		[unroll]
		for (int x = -range; x <= range; x++)
		{
			float2 sampleOffset = float2(x, y) * texelSize;
			shadowFactor += SampleShadowTap(shadowCoord.xy + sampleOffset, shadowCoord.z - depthBias, cascadeIndex);
			count++;
		}
	}
	return shadowFactor / count;
}

float3 SampleLights(in LightPixelData input)
{
	float3 V = normalize(input.viewPos - input.worldPos);
	float3 N = input.normal;
	float  cosLo = max(dot(N, V), 0.0);
	float3 Lr   = 2.0 * cosLo * N - V;

	float maxShadowZ = cascadeSplits[SHADOW_MAP_CASCADE_COUNT - 1];

	float shadow = 1.0;
	uint  cascadeIndex = 0;

	if (input.fragViewPos.z > maxShadowZ)
	{
		for (uint i = 0; i < SHADOW_MAP_CASCADE_COUNT - 1; ++i)
		{
			if (input.fragViewPos.z < cascadeSplits[i])
			{
				cascadeIndex = i + 1;
			}
		}

		shadow = SampleShadowCascade(input.worldPos, N, cascadeIndex);

		if (cascadeIndex < SHADOW_MAP_CASCADE_COUNT - 1)
		{
			float splitZ = cascadeSplits[cascadeIndex];
			float blendDist = abs(splitZ) * 0.1;
			float blendStart = splitZ + blendDist;
			float blendFactor = saturate((blendStart - input.fragViewPos.z) / blendDist);
			if (blendFactor > 0.0)
			{
				float nextShadow = SampleShadowCascade(input.worldPos, N, cascadeIndex + 1);
				shadow = lerp(shadow, nextShadow, blendFactor);
			}
		}

		float fadeRange = abs(maxShadowZ) * 0.03;
		float distanceFade = smoothstep(maxShadowZ, maxShadowZ + fadeRange, input.fragViewPos.z);
		shadow = lerp(1.0, shadow, distanceFade);
	}

	float3 F0 = 0.04;
	F0 = lerp(F0, input.baseColor, input.metallic);

	float3 lighting = 0.0;

	for (uint i = 0; i < lightCount; i++)
	{
		float3 contrib = EvaluateDirectLighting(lights[i], N, V, input.worldPos, input.baseColor, input.roughness, input.metallic, F0);

		if (shadowLightIndex == i)
		{
			contrib *= shadow;
		}
		lighting += contrib;
	}

	#ifdef CASCADE_DEBUG
	    static const float3 cascadeColors[4] = {
	        float3(1.0f, 0.0f, 0.0f),
	        float3(0.0f, 1.0f, 0.0f),
	        float3(0.0f, 0.0f, 1.0f),
	        float3(1.0f, 1.0f, 0.0f)
	    };
	    return float3(cascadeColors[cascadeIndex] * lighting);
	#endif

	return lighting;
}

#endif
