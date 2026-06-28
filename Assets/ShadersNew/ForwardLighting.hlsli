#ifndef SK_FORWARD_LIGHTING_HLSLI
#define SK_FORWARD_LIGHTING_HLSLI

float3 ShadeForward(float3 baseColor, float3 emissive, float3 worldNormal)
{
	float3 N      = normalize(worldNormal);
	float3 sunDir = normalize(float3(0.5, 1.0, 0.4));
	float  ndl    = saturate(dot(N, sunDir));

	float3 ambient = float3(0.18, 0.20, 0.24);
	float3 color   = baseColor * (ambient + ndl * float3(1.0, 0.97, 0.92));
	color += emissive;
	return color;
}

#endif
