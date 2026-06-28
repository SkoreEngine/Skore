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

StructuredBuffer<MaterialData>     MaterialDataBuffer        : register(t0, space2);
SamplerState                       Samplers[SK_SAMPLER_COUNT] : register(s2, space2);
Texture2D                          BindlessTextures[]        : register(t3, space2);
ByteAddressBuffer                  MeshDataBuffer            : register(t4, space2);
ConstantBuffer<VertexLayoutOffset> VertexLayouts[]           : register(b5, space2);

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

float3 GetVertexColor(uint vertexByteOffset, uint layoutIdx, uint vertexId)
{
	VertexLayoutOffset layout = VertexLayouts[NonUniformResourceIndex(layoutIdx)];
	if (layout.colorOffset == 0xFFFFFFFF) return float3(1, 1, 1);
	return asfloat(MeshDataBuffer.Load3(vertexByteOffset + vertexId * layout.stride + layout.colorOffset));
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

	surface.emissive = mat.emissiveColor * mat.emissiveFactor;
	if (mat.emissiveTexture >= 0)
	{
		surface.emissive *= BindlessTextures[NonUniformResourceIndex(mat.emissiveTexture)].Sample(Samplers[mat.GetEmissiveSamplerIndex()], uv).rgb;
	}

	return surface;
}

#endif
