// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef SK_LIGHTS_HLSLI
#define SK_LIGHTS_HLSLI

#include "PBR.hlsli"

#define MAX_LIGHTS 64

//flags
#define SK_LIGHT_FLAGS_HAS_ENVIRONMENT 	1 << 1

#define SHADOW_MAP_CASCADE_COUNT 4
//#define CASCADE_DEBUG

static const float4x4 biasMat = float4x4(
	0.5, 0.0, 0.0, 0.5,
	0.0, 0.5, 0.0, 0.5,
	0.0, 0.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0
);

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

cbuffer LightBuffer : register(b0, space2)
{
	uint     lightCount;
	float3   ambientLight;
	uint	 shadowLightIndex;
	uint	 lightFlags;
	float2 	 _pad;
	float4   cascadeSplits;
	float4x4 cascadeViewProjMat[SHADOW_MAP_CASCADE_COUNT];	
	Light    lights[MAX_LIGHTS];
};

Texture2DArray         shadowMapTexture     : register(t1, space2);
SamplerComparisonState shadowMapSampler     : register(s2, space2);
TextureCube            diffuseIrradiance    : register(t3, space2);

float TextureProj(float4 shadowCoord, float2 offset, uint cascadeIndex)
{
	float bias = 0.003;
	if ( shadowCoord.z > -1.0 && shadowCoord.z < 1.0 )
	{
		return shadowMapTexture.SampleCmpLevelZero(shadowMapSampler, float3(shadowCoord.xy + offset, cascadeIndex), shadowCoord.z - bias).r;
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
	int range = 3;

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

float3 SampleLights(in LightPixelData input)
{
	float3 V = normalize(input.viewPos - input.worldPos);
	float3 N = input.normal;
    float  cosLo = max(dot(N, V), 0.0);
    float3 Lr   = 2.0 * cosLo * N - V;	

	uint cascadeIndex = 0;
    for(uint i = 0; i < SHADOW_MAP_CASCADE_COUNT - 1; ++i)
    {
        if(input.fragViewPos.z < cascadeSplits[i])
        {
            cascadeIndex = i + 1;
        }
    }
	
    // Depth compare for shadowing
    float4 shadowCoord = mul(biasMat, mul(cascadeViewProjMat[cascadeIndex], float4(input.worldPos, 1.0)));
    float  shadow = FilterPCF(shadowCoord / shadowCoord.w, cascadeIndex);

    float3 F0 = 0.04;
    F0 = lerp(F0, input.baseColor, input.metallic);	

	float3 lighting = 0.0;

	for (uint i = 0; i < lightCount; i++)
	{
		Light light =  lights[i];	
		float attenuation;

		float3 lightDir;

		switch (lights[i].type)
		{
			case 0:
			{
				lightDir = normalize(-light.direction.xyz);		
				attenuation = shadowLightIndex == i ? 1.0 * shadow : 1.0;				
				break;
			}
			case 1:
			{
				lightDir = normalize(light.position - input.worldPos);

				float distance = length(light.position - input.worldPos);
				attenuation = 1.0 - saturate(distance / light.range);
				attenuation = attenuation * attenuation;
				break;
			}
				
			case 2:
			{
				lightDir = normalize(light.position - input.worldPos);

				float distance = length(light.position - input.worldPos);
				attenuation = 1.0 - saturate(distance / light.range);
				attenuation = attenuation * attenuation;

				// Spot effect
				float theta = dot(lightDir, normalize(-light.direction.xyz));
				float epsilon = light.innerConeAngle - light.outerConeAngle;
				float spotIntensity = saturate((theta - cos(light.outerConeAngle)) / epsilon);
				
				attenuation = spotIntensity * attenuation;
				break;
			}

		}

		float nDotL = max(dot(N, lightDir), 0.0);
		float3 H 	= normalize(V + lightDir);

		// Cook-Torrance BRDF
        float  NDF  = DistributionGGX(N, H, input.roughness);
        float  G    = GeometrySmith(N, V, lightDir, input.roughness);
        float3 F    = FresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

        float3 numerator    = NDF * G * F;
        float  denominator  = 4.0 * max(dot(N, V), 0.0) * max(dot(N, lightDir), 0.0) + 0.0001;
        float3 specular     = numerator / denominator;

		float3 kD = 1.0 - F;
        kD *= 1.0 - input.metallic;

		float3 radiance = light.color.xyz * light.intensity * attenuation;
		lighting += (kD * (input.baseColor) + specular) * radiance * nDotL;
	}


	[branch]
	if (lightFlags & SK_LIGHT_FLAGS_HAS_ENVIRONMENT)
	{
		float3 irradiance = diffuseIrradiance.SampleLevel(linearSampler, N, 0.0).rgb;
		float3 F = FresnelSchlickRoughness(cosLo, F0, input.roughness);
		float3 kd = lerp(1.0 - F, 0.0, input.metallic);

    	float3 diffuseIBL = 0.0;
    	diffuseIBL = kd * input.baseColor * irradiance;

    	float3  specularIBL = 0.0;

		//TODO specualr IBL

		//Lo += ((diffuseIBL + specularIBL) * ao) * indirectMultiplier;

		lighting += (diffuseIBL + specularIBL) * 2.0;
	} 
	else 
	{
		lighting += input.baseColor * ambientLight;		
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