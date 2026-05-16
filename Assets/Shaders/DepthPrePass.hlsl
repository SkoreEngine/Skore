#include "Common.hlsli"

cbuffer SceneBuffer : register(b0, space0)
{
	matrix viewProjection;
	matrix view;
	matrix projection;
	matrix viewInv;
	matrix projectionInv;
	matrix viewProjInv;
	float3 cameraPosition;
	float  pad0;
	int2   outputSize;
	float2 pad1;
	float2 jitter;
	float2 prevJitter;
};

struct PushConstants
{
	matrix world;
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
    float4 position : SV_POSITION;
};

PixelInput MainVS(VertexInput input)
{
	PixelInput output;

#ifdef HAS_BONES
	float3 position = 0.0;

	[unroll]
	for (int i = 0; i < 4; i++)
	{
		float weight = input.boneWeights[i];
		matrix boneTransform = boneMatrices[input.boneIndices[i]];

		float4 localPosition = mul(boneTransform, float4(input.position, 1.0f));
		position += localPosition.xyz * weight;
	}
#else
	float3 position = input.position;
#endif

	float4 worldPosition = mul(pushConstants.world, float4(position, 1.0f));
	output.position = mul(viewProjection, worldPosition);

    return output;
}

void MainPS(PixelInput input)
{
    //depth only - no color output
}
