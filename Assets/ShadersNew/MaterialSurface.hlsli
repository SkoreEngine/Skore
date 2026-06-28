#ifndef SK_MATERIAL_SURFACE_HLSLI
#define SK_MATERIAL_SURFACE_HLSLI

struct MaterialInputs
{
	float2 texCoord;
	float3 normal;
	float3 worldPos;
	float3 vertexColor;
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
