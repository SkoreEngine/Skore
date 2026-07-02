#ifndef SK_MATERIAL_SURFACE_HLSLI
#define SK_MATERIAL_SURFACE_HLSLI

struct MaterialInputs
{
	float2 texCoord;
	float2 texCoord1;
	float3 normal;
	float3 worldPos;
	float3 vertexColor;
	float3 viewDir;
	float3 cameraPosition;
	float3 objectPosition;
	float  time;
};

struct SurfaceOutput
{
	float3 baseColor;
	float  metallic;
	float  roughness;
	float3 emissive;
	float3 normal;
	float  occlusion;
	float  opacity;
};

#endif
