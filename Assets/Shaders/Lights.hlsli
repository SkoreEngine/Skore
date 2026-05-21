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

float TextureProj(float4 shadowCoord, float2 offset, uint cascadeIndex)
{
	float bias = 0.002;
	if ( shadowCoord.z > -1.0 && shadowCoord.z < 1.0 )
	{
#if SK_USE_COMPARISON_STATE
		return shadowMapTexture.SampleCmpLevelZero(shadowMapSampler, float3(shadowCoord.xy + offset, cascadeIndex), shadowCoord.z - bias).r;
#else
		float dist = shadowMapTexture.Sample(shadowMapSampler, float3(shadowCoord.xy + offset, cascadeIndex)).r;
		if (shadowCoord.w > 0 && dist < shadowCoord.z - bias)
		{
			return 0.0;
		}
#endif
	}
	return 1.0;
}

float FilterPCF(float4 sc, uint cascadeIndex)
{
	int3 texDim;
	shadowMapTexture.GetDimensions(texDim.x, texDim.y, texDim.z);
	float scale = 1.0;
	float dx = scale * 1.0 / float(texDim.x);
	float dy = scale * 1.0 / float(texDim.y);

	float shadowFactor = 0.0;
	int count = 0;
	int range = SK_PCF_RANGE;

	for (int x = -range; x <= range; x++) {
		for (int y = -range; y <= range; y++) {
			shadowFactor += TextureProj(sc, float2(dx*x, dy*y), cascadeIndex);
			count++;
		}
	}
	return shadowFactor / count;
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
	// Normal offset bias: push the sample point along the normal to reduce acne
	float3 lightDir = normalize(-lights[shadowLightIndex].direction.xyz);
	float  nDotL = dot(N, lightDir);
	float  normalBias = 0.02 * (1.0 - nDotL);
	float3 biasedPos = worldPos + N * normalBias;

	float4 shadowCoord = mul(biasMat, mul(cascadeViewProjMat[cascadeIndex], float4(biasedPos, 1.0)));
	return FilterPCF(shadowCoord / shadowCoord.w, cascadeIndex);
}

float3 SampleLights(in LightPixelData input)
{
	float3 V = normalize(input.viewPos - input.worldPos);
	float3 N = input.normal;
	float  cosLo = max(dot(N, V), 0.0);
	float3 Lr   = 2.0 * cosLo * N - V;

	// Cascade selection
	uint cascadeIndex = 0;
	for(uint i = 0; i < SHADOW_MAP_CASCADE_COUNT - 1; ++i)
	{
		if(input.fragViewPos.z < cascadeSplits[i])
		{
			cascadeIndex = i + 1;
		}
	}

	// Shadow with cascade blending
	float shadow = SampleShadowCascade(input.worldPos, N, cascadeIndex);

	// Blend with next cascade near the far boundary of the current cascade
	if (cascadeIndex < SHADOW_MAP_CASCADE_COUNT - 1)
	{
		float splitZ = cascadeSplits[cascadeIndex];        // negative value
		float blendDist = abs(splitZ) * 0.1;              // positive blend zone width
		float blendStart = splitZ + blendDist;             // less negative = start of blend zone
		float blendFactor = saturate((blendStart - input.fragViewPos.z) / blendDist);
		if (blendFactor > 0.0)
		{
			float nextShadow = SampleShadowCascade(input.worldPos, N, cascadeIndex + 1);
			shadow = lerp(shadow, nextShadow, blendFactor);
		}
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