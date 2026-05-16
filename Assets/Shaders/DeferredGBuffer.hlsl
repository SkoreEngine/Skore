#include "Common.hlsli"
#include "Materials.hlsli"

struct PushConstants
{
	matrix world;
	uint   materialIndex;
	uint   lightmapTextureIndex;
	float2 lightmapScale;
	float2 lightmapOffset;
	uint   lightmapMode; // 0 = Indirect, 1 = Subtractive, 2 = Shadowmask
	uint   pad;
};

[[vk::push_constant]] PushConstants pushConstants;

#ifdef HAS_BONES
cbuffer SkinnedBuffer : register(b0, space1)
{
    matrix boneMatrices[SK_MAX_BONES];
};
#endif

// Vertex layout matches MeshImporter order:
// position(Vec3), normal(Vec3), uv0(Vec2), [uv1(Vec2)], [color(Vec3)], tangent(Vec4), [boneIndices(u32x4), boneWeights(Vec4)]
struct VertexInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 texCoord : TEXCOORD0;
#ifdef HAS_UV1
	float2 uv1      : TEXCOORD1;
#endif
#ifdef HAS_COLOR
	float3 color    : COLOR;
#endif
	float4 tangent  : TANGENT;
#ifdef HAS_BONES
	uint4  boneIndices  : BONEINDICES;
	float4 boneWeights  : BONEWEIGHTS;
#endif
};

struct PixelInput
{
	float4   position    : SV_POSITION;
	float3   normal      : NORMAL;
	float2   texCoord    : TEXCOORD0;
#ifdef HAS_UV1
	float2   uv1         : TEXCOORD1;
#endif
	float3   color       : COLOR;
	float3x3 TBN         : TBN1;
	float3   worldPos    : POSITION1;
	float3   viewPos     : POSITION2;
	float3   fragViewPos : POSITION3;
};

PixelInput MainVS(VertexInput input)
{
	PixelInput output;

#ifdef HAS_BONES
	float3 position = 0.0;
	float3 normal = 0.0;
	float3 tangent = 0.0;

	[unroll]
	for (int i = 0; i < 4; i++)
	{
		float weight = input.boneWeights[i];
		matrix boneTransform = boneMatrices[input.boneIndices[i]];

		float4 localPosition = mul(boneTransform, float4(input.position, 1.0f));
		position += localPosition.xyz * weight;

		float3 localNormal = mul((float3x3)boneTransform, input.normal);
		normal += localNormal * weight;

		float3 localTangent = mul((float3x3)boneTransform, input.tangent.xyz);
		tangent += localTangent * weight;
	}

	normal = normalize(normal);
	tangent = normalize(tangent);
#else
	float3 position = input.position;
	float3 normal = input.normal;
	float3 tangent = input.tangent.xyz;
#endif

	float4 worldPosition = mul(pushConstants.world, float4(position, 1.0f));
	output.position      = mul(viewProjection, worldPosition);
	output.worldPos      = worldPosition.xyz;

	output.texCoord = input.texCoord;
#ifdef HAS_UV1
	output.uv1 = input.uv1;
#endif

	float3x3 normalMat = (float3x3)pushConstants.world;
	output.normal       = normalize(mul(normalMat, normal));

	float4 T = float4(normalize(mul(normalMat, tangent)), input.tangent.w);
	float3 B = T.w * cross(output.normal, T.xyz);
	output.TBN = transpose(float3x3(T.xyz, B, output.normal));

#ifdef HAS_COLOR
	output.color = input.color;
#else
	output.color = float3(1.0, 1.0, 1.0);
#endif

	output.viewPos     = cameraPosition;
	output.fragViewPos = mul(view, worldPosition).xyz;

	return output;
}

struct PSOutput
{
	float4 outAlbedoMetallic      :  SV_TARGET0;
	float2 outRoughnessAo         :  SV_TARGET1;
	float2 outNormal              :  SV_TARGET2;
	float3 outEmissive            :  SV_TARGET3;
};

[earlydepthstencil]
PSOutput MainPS(PixelInput input)
{
	MaterialSample material = SampleMaterial(pushConstants.materialIndex, input.texCoord, input.normal, input.TBN);

	PSOutput output = (PSOutput)0;
	output.outAlbedoMetallic    = float4(material.baseColor.rgb, material.metallic);
	output.outRoughnessAo       = float2(material.roughness, material.occlusion);
	output.outNormal            = OctohedralEncode(material.normal);
	output.outEmissive          = material.emissive;
	return output;
}