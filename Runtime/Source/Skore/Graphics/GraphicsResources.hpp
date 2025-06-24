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

#pragma once
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	enum class TextureChannel : u8
	{
		Red   = 0,
		Green = 1,
		Blue  = 2,
		Alpha = 3
	};

	struct ShaderVariantResource
	{
		enum
		{
			Name,         //String
			Spriv,        //Blob
			PipelineDesc, //Subobject
			Stages,       //SubobjectSet
		};
	};


	struct ShaderResource
	{
		enum
		{
			Name,     //String
			Variants, //SubobjectSet
		};

		static RID GetVariant(RID shader, StringView name);
	};


	struct TextureResource
	{
		enum
		{
			Name,       //String
			Extent,     //Vec3
			Format,     //Enum
			WrapMode,   //Enum
			FilterMode, //Enum
			Pixels,     //Blob
		};
	};

	struct MeshResource
	{
		struct Vertex
		{
			Vec3 position;
			Vec3 normal;
			Vec2 texCoord;
			Vec3 color;
			Vec4 tangent;
		};

		struct Primitive
		{
			u32 firstIndex;
			u32 indexCount;
			u32 materialIndex;
		};

		enum
		{
			Name,       //String
			Materials,  //ReferenceArray
			AABB,       //Subobject
			Vertices,   //Blob
			Indices,    //Blob
			Primitives, //Blob
		};
	};

	struct MaterialResource
	{
		enum class MaterialType
		{
			Opaque,
			SkyboxEquirectangular,
		};

		struct Buffer
		{
			Vec3 baseColor;
			f32  alphaCutoff;

			f32 metallic;
			f32 roughness;

			i32 textureFlags;
			i32 textureProps;
		};

		enum class MaterialAlphaMode
		{
			None        = 0,
			Opaque      = 1,
			Mask        = 2,
			Blend       = 3
		};


		enum
		{
			Name,                    //String
			Type,                    //Enum
			BaseColor,               //Color
			BaseColorTexture,        //Reference
			NormalTexture,           //Reference
			NormalMultiplier,        //Float
			Metallic,                //Float
			MetallicTexture,         //Reference,
			MetallicTextureChannel,  //Enum
			Roughness,               //Float
			RoughnessTexture,        //Reference,
			RoughnessTextureChannel, //Enum
			EmissiveColor,           //Color
			EmissiveFactor,          //Float
			EmissiveTexture,         //Reference
			OcclusionTexture,        //Reference
			OcclusionStrength,       //Float
			OcclusionTextureChannel, //Enum
			AlphaCutoff,             //Float
			AlphaMode,               //Enum
			UvScale,                 //Vec2
			SphericalTexture,		 //Reference
			Exposure,			     //Float
			BackgroundColor,		 //Color
		};
	};

	struct DCCAssetResource
	{
		enum
		{
			Name,      //String
			Meshes,    //SubobjectSet
			Materials, //SubObjectSet
			Textures,  //SubObjectSet
			Entity,    //SubObject
		};
	};
}
