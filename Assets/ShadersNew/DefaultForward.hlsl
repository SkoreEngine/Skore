#include "SceneBindingsNew.hlsli"
#include "GlobalBindingsNew.hlsli"
#include "LightsNew.hlsli"
#include "MaterialSurface.hlsli"

struct PushConstants
{
	matrix world;
	uint   materialIndex;
	uint   vertexByteOffset;
	uint   vertexLayoutIndex;
	uint   useInstanceData;
};

[[vk::push_constant]] PushConstants pushConstants;

//material-graph code reads material params through this (see MaterialGraphCompiler);
//assigned from the interpolated instance/push-constant material index at the top of MainPS
static uint SK_MaterialIndex = 0;

struct PixelInput
{
	float4 position : SV_POSITION;
	float3 normal   : NORMAL;
	float2 texCoord : TEXCOORD0;
	float3 color    : COLOR;
	float3 worldPos : POSITION1;
	float4 tangent  : TANGENT;

	nointerpolation uint matIndex : MATERIAL1;
};

PixelInput MainVS(uint vertexId : SV_VertexID, [[vk::builtin("BaseInstance")]] uint baseInstance : SV_StartInstanceLocation)
{
	matrix world;
	uint   materialIndex;
	uint   vboff;
	uint   layoutIdx;

	//GPU-driven draws carry the instance index in startInstanceLocation; direct draws push their data
	if (pushConstants.useInstanceData != 0)
	{
		InstanceData instance = instances[NonUniformResourceIndex(baseInstance)];
		world = instance.transform;
		materialIndex = instance.materialIndex;
		vboff = instance.vertexByteOffset;
		layoutIdx = instance.vertexLayoutIndex;
	}
	else
	{
		world = pushConstants.world;
		materialIndex = pushConstants.materialIndex;
		vboff = pushConstants.vertexByteOffset;
		layoutIdx = pushConstants.vertexLayoutIndex;
	}

	float3 position = GetVertexPosition(vboff, layoutIdx, vertexId);
	float3 normal   = GetVertexNormal(vboff, layoutIdx, vertexId);
	float4 tangent  = GetVertexTangent(vboff, layoutIdx, vertexId);

	float4 worldPosition = mul(world, float4(position, 1.0));

	PixelInput output;
	output.position = mul(viewProjection, worldPosition);
	output.worldPos = worldPosition.xyz;

	float3x3 normalMat = (float3x3)world;
	output.normal = normalize(mul(normalMat, normal));

	//w carries the bitangent sign; w == 0 marks a mesh without tangents
	output.tangent = float4(0.0, 0.0, 0.0, 0.0);
	if (dot(tangent.xyz, tangent.xyz) > 0.0)
	{
		output.tangent = float4(normalize(mul(normalMat, tangent.xyz)), tangent.w);
	}

	output.texCoord = GetVertexUV(vboff, layoutIdx, vertexId);
	output.color    = GetVertexColor(vboff, layoutIdx, vertexId);
	output.matIndex = materialIndex;

	return output;
}

float3 ResolveWorldNormal(float3 tangentNormal, float3 vertexNormal, float4 tangent)
{
	float3 N = normalize(vertexNormal);
	if (tangent.w == 0.0)
	{
		return N;
	}
	float3 T = normalize(tangent.xyz - N * dot(N, tangent.xyz));
	float3 B = cross(N, T) * tangent.w;
	return normalize(mul(tangentNormal, float3x3(T, B, N)));
}

float3 ShadeForward(SurfaceOutput surface, float3 baseColor, float3 N, float3 V, float3 worldPos)
{
	float  roughness = clamp(surface.roughness, 0.045, 1.0);
	float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, surface.metallic);

	float fragViewZ = mul(view, float4(worldPos, 1.0)).z;
	float shadow = SampleDirectionalShadow(worldPos, N, fragViewZ);

	float3 color = float3(0.0, 0.0, 0.0);

	for (uint i = 0; i < lightCount; i++)
	{
		float3 contrib = EvaluateDirectLighting(lights[i], N, V, worldPos, baseColor, roughness, surface.metallic, F0);
		if (i == shadowLightIndex)
		{
			contrib *= shadow;
		}
		color += contrib;
	}

	color += EvaluateAmbient(N, V, baseColor, surface.metallic, roughness, surface.occlusion, F0);

	return color + surface.emissive;
}

// @SK_MATERIAL_GLOBALS@

#ifndef SK_MATERIAL_UNLIT
#define SK_MATERIAL_UNLIT 0
#endif

SurfaceOutput EvaluateMaterial(MaterialInputs mat)
{
	SurfaceOutput surface = (SurfaceOutput)0;
	surface.baseColor = float3(0.8, 0.8, 0.8);
	surface.metallic  = 0.0;
	surface.roughness = 0.5;
	surface.emissive  = float3(0.0, 0.0, 0.0);
	surface.normal    = float3(0.0, 0.0, 1.0);
	surface.occlusion = 1.0;
	surface.opacity   = 1.0;

	// @SK_MATERIAL_GRAPH@

	return surface;
}

float4 MainPS(PixelInput input) : SV_Target0
{
	SK_MaterialIndex = input.matIndex;

	MaterialInputs mat;
	mat.texCoord    = input.texCoord;
	mat.normal      = input.normal;
	mat.worldPos    = input.worldPos;
	mat.vertexColor = input.color;

	SurfaceOutput surface = EvaluateMaterial(mat);

	float3 baseColor = surface.baseColor * input.color;
	float3 N = ResolveWorldNormal(surface.normal, input.normal, input.tangent);
	float3 V = normalize(cameraPosition - input.worldPos);

#if SK_MATERIAL_UNLIT
	float3 color = baseColor;
#else
	float3 color = ShadeForward(surface, baseColor, N, V, input.worldPos);
#endif

	return float4(color, surface.opacity);
}
