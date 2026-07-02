#ifndef SK_GLOBAL_BINDINGS_NEW_HLSLI
#define SK_GLOBAL_BINDINGS_NEW_HLSLI

#define SK_LINEAR_SAMPLER        0
#define SK_NEAREST_SAMPLER       1
#define SK_LINEAR_CLAMP_SAMPLER  2
#define SK_NEAREST_CLAMP_SAMPLER 3
#define SK_SAMPLER_COUNT         4

struct MaterialData
{
	float3 baseColor;
	float  roughness;
	float  metallic;
	int    baseColorTexture;
	int    normalTexture;
	int    roughnessTexture;
	int    metallicTexture;
	float2 uvScale;
	float  emissiveFactor;
	float3 emissiveColor;
	int    emissiveTexture;
	int    alphaMode;
	float  alphaCutoff;
	uint   samplerIndices0;
	uint   samplerIndices1;

	uint GetBaseColorSamplerIndex() { return  samplerIndices0       & 0xFF; }
	uint GetEmissiveSamplerIndex()  { return  samplerIndices1       & 0xFF; }
};

struct VertexLayoutOffset
{
	uint stride;
	uint positionOffset;
	uint normalOffset;
	uint uvOffset;
	uint uv1Offset;
	uint colorOffset;
	uint tangentOffset;
	uint boneIndicesOffset;
	uint boneWeightsOffset;
	uint custom0Offset;
	uint custom1Offset;
	uint custom2Offset;
};

#define SK_MAX_LODS 10

struct MeshLODInfo
{
	uint  firstIndex;
	uint  indexCount;
	float screenSize;
	float pad;
};

struct MeshPrimitiveInfo
{
	uint        lodCount;
	uint        pad0;
	uint        pad1;
	uint        pad2;
	MeshLODInfo lods[SK_MAX_LODS];
};

StructuredBuffer<MaterialData>      MaterialDataBuffer         : register(t0, space2);
RWStructuredBuffer<uint>            MaterialMaskBuffer         : register(u1, space2);
SamplerState                        Samplers[SK_SAMPLER_COUNT] : register(s2, space2);
Texture2D                           BindlessTextures[]         : register(t3, space2);
ByteAddressBuffer                   MeshDataBuffer             : register(t4, space2);
ConstantBuffer<VertexLayoutOffset>  VertexLayouts[]            : register(b5, space2);
StructuredBuffer<MeshPrimitiveInfo> MeshLODBuffer              : register(t6, space2);

float3 GetVertexPosition(uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	return asfloat(MeshDataBuffer.Load3(vertexByteOffset + vertexId * layout.stride + layout.positionOffset));
}

float3 GetVertexNormal(uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	return asfloat(MeshDataBuffer.Load3(vertexByteOffset + vertexId * layout.stride + layout.normalOffset));
}

float2 GetVertexUV(uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	return asfloat(MeshDataBuffer.Load2(vertexByteOffset + vertexId * layout.stride + layout.uvOffset));
}

float2 GetVertexUV1(uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.uv1Offset == 0xFFFFFFFF) return GetVertexUV(vertexByteOffset, layoutIdx, vertexId);
	return asfloat(MeshDataBuffer.Load2(vertexByteOffset + vertexId * layout.stride + layout.uv1Offset));
}

float3 GetVertexColor(uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.colorOffset == 0xFFFFFFFF) return float3(1, 1, 1);
	return asfloat(MeshDataBuffer.Load3(vertexByteOffset + vertexId * layout.stride + layout.colorOffset));
}

float4 GetVertexTangent(uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.tangentOffset == 0xFFFFFFFF) return float4(0, 0, 0, 0);
	return asfloat(MeshDataBuffer.Load4(vertexByteOffset + vertexId * layout.stride + layout.tangentOffset));
}

//per-scene bone-matrix palettes, one storage buffer per skinned instance.
//InstanceData.boneBufferIndex selects the palette; 0xFFFFFFFF marks a non-skinned instance.
StructuredBuffer<float4x4> BoneBuffers[] : register(t0, space3);

uint4 GetVertexBoneIndices(uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.boneIndicesOffset == 0xFFFFFFFF) return uint4(0, 0, 0, 0);
	return MeshDataBuffer.Load4(vertexByteOffset + vertexId * layout.stride + layout.boneIndicesOffset);
}

float4 GetVertexBoneWeights(uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.boneWeightsOffset == 0xFFFFFFFF) return float4(1, 0, 0, 0);
	return asfloat(MeshDataBuffer.Load4(vertexByteOffset + vertexId * layout.stride + layout.boneWeightsOffset));
}

bool IsSkinned(uint boneBufferIndex)
{
	return boneBufferIndex != 0xFFFFFFFF;
}

//linear-blend skin matrix: sum of the four influencing bones weighted by their skin weights.
float4x4 GetSkinningMatrix(uint boneBufferIndex, uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	uint4  indices = GetVertexBoneIndices(vertexByteOffset, layoutIdx, vertexId);
	float4 weights = GetVertexBoneWeights(vertexByteOffset, layoutIdx, vertexId);

	StructuredBuffer<float4x4> palette = BoneBuffers[NonUniformResourceIndex(boneBufferIndex)];

	float4x4 skin = weights.x * palette[indices.x];
	skin         += weights.y * palette[indices.y];
	skin         += weights.z * palette[indices.z];
	skin         += weights.w * palette[indices.w];
	return skin;
}

float GetMipmapLod(uint2 dim, float2 uvDx, float2 uvDy)
{
	return log2(max(length(uvDx * dim), length(uvDy * dim)));
}

void WriteMipmapFeedback(uint materialIndex, float4 uvsetsDx, float4 uvsetsDy)
{
	const float lodUvset0 = GetMipmapLod(65536u, uvsetsDx.xy, uvsetsDy.xy);
	const float lodUvset1 = GetMipmapLod(65536u, uvsetsDx.zw, uvsetsDy.zw);
	const uint  resolution0 = 65536u >> uint(max(0, lodUvset0));
	const uint  resolution1 = 65536u >> uint(max(0, lodUvset1));
	const uint  mask = resolution0 | (resolution1 << 16u);
	const uint  waveMask = WaveActiveBitOr(mask);
	if (WaveIsFirstLane())
	{
		InterlockedOr(MaterialMaskBuffer[materialIndex], waveMask);
	}
}

struct SurfaceSample
{
	float3 baseColor;
	float  alpha;
	float3 emissive;
};

SurfaceSample SampleSurface(uint materialIndex, float2 texCoord)
{
	MaterialData mat = MaterialDataBuffer[materialIndex];

	SurfaceSample surface;
	surface.baseColor = mat.baseColor;
	surface.alpha = 1.0;

	float2 uv = texCoord * mat.uvScale;

	if (mat.baseColorTexture >= 0)
	{
		float4 texColor = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].Sample(Samplers[mat.GetBaseColorSamplerIndex()], uv);
		surface.baseColor = pow(texColor.rgb * mat.baseColor, 2.2);
		surface.alpha = texColor.a;
	}

	if (mat.alphaMode == 2 && surface.alpha < mat.alphaCutoff)
	{
		discard;
	}

	WriteMipmapFeedback(materialIndex, ddx_coarse(float4(uv, 0, 0)), ddy_coarse(float4(uv, 0, 0)));

	surface.emissive = mat.emissiveColor * mat.emissiveFactor;
	if (mat.emissiveTexture >= 0)
	{
		surface.emissive *= BindlessTextures[NonUniformResourceIndex(mat.emissiveTexture)].Sample(Samplers[mat.GetEmissiveSamplerIndex()], uv).rgb;
	}

	return surface;
}

#endif
