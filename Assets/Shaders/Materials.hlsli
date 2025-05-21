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

#ifndef SK_MATERIALS_HLSLI
#define SK_MATERIALS_HLSLI

#define SK_HAS_BASE_COLOR_TEXTURE 	1 << 1;
#define SK_HAS_NORMAL_TEXTURE 		1 << 2;
#define SK_HAS_ROUGHNESS_TEXTURE 	1 << 3;
#define SK_HAS_METALLIC_TEXTURE 	1 << 4;
#define SK_HAS_EMISSIVE_TEXTURE 	1 << 5;
#define SK_HAS_OCCLUSION_TEXTURE	1 << 6;

struct MaterialBuffer 
{
	float3 baseColor;
	float  alphaCutOff;

	float  metallic;
	float  roughness;

	int textureFlags;
	int textureProps;

	bool HasBaseColorTexture()
	{
		return textureFlags & SK_HAS_BASE_COLOR_TEXTURE;
	}

	bool HasNormalTexture()
	{
		return textureFlags & SK_HAS_NORMAL_TEXTURE;
	}

	bool HasRoughnessTexture()
	{
		return textureFlags & SK_HAS_ROUGHNESS_TEXTURE;
	}

	bool HasMetallicTexture()
	{
		return textureFlags & SK_HAS_METALLIC_TEXTURE;
	}

	int GetRoughnessChannel()
	{
		return textureProps & 0xFF;
	}

	int GetMetallicChannel()
	{
		return (textureProps >> 8) & 0xFF;
	}
};

ConstantBuffer<MaterialBuffer> materialBuffer : register(b0, space1);

SamplerState 	linearSampler 		: register(s1, space1);
Texture2D 		baseColorTexture 	: register(t2, space1);
Texture2D 		normalTexture 		: register(t3, space1);
Texture2D 		roughnessTexture	: register(t4, space1);
Texture2D 		metallicTexture		: register(t5, space1);


struct MaterialSample
{
	float3     baseColor;
	float      alpha;
	float3     normal;
	float      roughness;
	float      metallic;
	float3     emissive;
	float      occlusion;
};


MaterialSample SampleMaterial(in float2 texCoord, in float3 normal, in float3x3 TBN)
{
	MaterialSample material;

	float4 baseColorValue = baseColorTexture.Sample(linearSampler, texCoord);
	float3 baseColor = baseColorValue.rgb;
	float  alpha = baseColorValue.a;

	if (alpha < materialBuffer.alphaCutOff)
	{
		discard;
	}

	material.baseColor = pow(baseColor * materialBuffer.baseColor, 2.2);
	material.alpha = alpha;

	[branch]
	if (materialBuffer.HasNormalTexture())
	{
		float3 n = normalTexture.Sample(linearSampler, texCoord).rgb * 2.0 - 1.0;
		material.normal = normalize(mul(TBN, n));
	} 
	else 
	{
		material.normal = normal;
	}

	[branch]
	if (materialBuffer.HasRoughnessTexture())
	{
		int channel = materialBuffer.GetRoughnessChannel();
		float4 value = roughnessTexture.Sample(linearSampler, texCoord);		
		material.roughness = value[channel];
	} 
	else 
	{
		material.roughness = materialBuffer.roughness;
	}

	[branch]
	if (materialBuffer.HasMetallicTexture())
	{
		int channel = materialBuffer.GetMetallicChannel();
		float4 value = metallicTexture.Sample(linearSampler, texCoord);
		material.metallic = value[channel];
	} 
	else 
	{
		material.metallic = materialBuffer.metallic;
	}

	
	return material;
}


#endif