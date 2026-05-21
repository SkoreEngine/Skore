#ifndef SK_LIGHT_COMMON_HLSLI
#define SK_LIGHT_COMMON_HLSLI

#include "SceneBindings.hlsli"
#include "PBR.hlsli"

//flags
#define SK_LIGHT_FLAGS_HAS_AMBIENT_TEXTURE 	    1 << 1
#define SK_LIGHT_FLAGS_HAS_AMBIENT_COLOR 	    1 << 2
#define SK_LIGHT_FLAGS_HAS_REFLECTION_TEXTURE 	1 << 3
#define SK_LIGHT_FLAGS_HAS_SSAO_TEXTURE 	    1 << 4
#define SK_LIGHT_FLAGS_SSR_ENABLED 	            1 << 5

// Extracts light direction and max shadow-ray distance for a given light and world position.
void GetLightDirectionAndDistance(Light light, float3 worldPos, out float3 lightDir, out float maxDist)
{
	switch (light.type)
	{
		case 0: // Directional
			lightDir = normalize(-light.direction.xyz);
			maxDist = 10000.0;
			break;
		case 1: // Point
		case 2: // Spot
		default:
			float3 toLight = light.position - worldPos;
			lightDir = normalize(toLight);
			maxDist = length(toLight);
			break;
	}
}

// Returns unshadowed radiance contribution from a single light.
// Caller applies shadow/occlusion externally if needed.
float3 EvaluateDirectLighting(
	Light  light,
	float3 N, float3 V, float3 worldPos,
	float3 baseColor, float roughness, float metallic, float3 F0)
{
    roughness = max(roughness, 0.045);

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
			lightDir = normalize(light.position - worldPos);
			float distance = length(light.position - worldPos);
			attenuation = 1.0 - saturate(distance / light.range);
			attenuation *= attenuation;
			break;
		}
		case 2: // Spot
		{
			lightDir = normalize(light.position - worldPos);
			float distance = length(light.position - worldPos);
			attenuation = 1.0 - saturate(distance / light.range);
			attenuation *= attenuation;
			float theta = dot(lightDir, normalize(-light.direction.xyz));
			float epsilon = light.innerConeAngle - light.outerConeAngle;
			float spotIntensity = saturate((theta - cos(light.outerConeAngle)) / epsilon);
			attenuation *= spotIntensity;
			break;
		}
	}

	float  nDotL = max(dot(N, lightDir), 0.0);
	float3 H    = normalize(V + lightDir);

	float  NDF = DistributionGGX(N, H, roughness);
	float  G   = GeometrySmith(N, V, lightDir, roughness);
	float3 F   = FresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

	float  denominator = max(4.0 * max(dot(N, V), 0.0) * nDotL, Epsilon);
	float3 specular = (NDF * G * F) / denominator;

	float3 kD = (1.0 - F) * (1.0 - metallic);
	float3 radiance = light.color.xyz * light.intensity * attenuation;

	return (kD * baseColor + specular) * radiance * nDotL;
}

#endif
