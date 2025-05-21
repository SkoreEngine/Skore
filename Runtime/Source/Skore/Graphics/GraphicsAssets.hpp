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
#include "Device.hpp"
#include "Skore/IO/Assets.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	class MaterialAsset;

	enum class TextureChannel : u8
	{
		Red   = 0,
		Green = 1,
		Blue  = 2,
		Alpha = 3
	};

	struct TextureAssetImage
	{
		Extent extent;
		u32    mip;
		u32    arrayLayer;

		static void RegisterType(NativeReflectType<TextureAssetImage>& type);
	};

	class SK_API TextureAsset : public Asset
	{
	public:
		SK_CLASS(TextureAsset, Asset)
		~TextureAsset() override;

		bool SetTextureDataFromFileInMemory(Span<const u8> bytes, bool isHDR, bool generateMips, bool compressToGPUFormat);
		bool SetTextureDataFromFile(StringView path, bool isHDR, bool generateMips, bool compressToGPUFormat);
		void SetTextureData(Extent extent, Span<const u8> bytes, TextureFormat format, bool generateMips, bool compressToGPUFormat);

		GPUTexture* GetTexture();

		static void RegisterType(NativeReflectType<TextureAsset>& type);

	private:
		GPUTexture*              m_texture = nullptr;
		TextureFormat            m_format = TextureFormat::Unknown;
		Array<TextureAssetImage> m_images;
		Array<u8>                m_textureData;
		u32                      m_mipLevels = 1;
	};

	class SK_API MeshAsset : public Asset
	{
	public:
		SK_CLASS(MeshAsset, Asset)

		~MeshAsset() override;

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


		void SetVertices(Span<Vertex> vertices);
		void SetIndices(Span<u32> indices);
		void SetPrimitives(Span<Primitive> primitives);
		void SetMaterials(Span<MaterialAsset*> materials);

		void CalcTangents(bool useMikktspace);
		void CalcNormals();

		GPUBuffer* GetVertexBuffer();
		GPUBuffer* GetIndexBuffer();

		Span<Primitive>      GetPrimitives() const;
		Span<MaterialAsset*> GetMaterials() const;

		static void RegisterType(NativeReflectType<MeshAsset>& type);

	private:
		Array<u8>             m_vertices;
		Array<u8>             m_indices;
		Array<Primitive>      m_primitives;
		Array<MaterialAsset*> m_materials;
		AABB                  m_boundingBox;

		GPUBuffer* m_vertexBuffer = nullptr;
		GPUBuffer* m_indexBuffer = nullptr;
	};

	class SK_API MaterialAsset : public Asset
	{
	public:
		enum class MaterialType
		{
			Opaque,
			SkyboxEquirectangular,
		};

		enum class AlphaMode
		{
			None        = 0,
			Opaque      = 1,
			Mask        = 2,
			Blend       = 3
		};

		struct MaterialBuffer
		{
			Vec3 baseColor;
			f32  alphaCutoff;

			f32 metallic;
			f32 roughness;

			i32 textureFlags;
			i32 textureProps;
		};

		SK_CLASS(MaterialAsset, Asset)
		~MaterialAsset() override;

		MaterialType type = MaterialType::Opaque;

		// Opaque material
		Color         baseColor{Color::WHITE};
		TextureAsset* baseColorTexture{};

		//normal texture
		TextureAsset* normalTexture{};
		f32           normalMultiplier{1.0};

		//metallic
		f32            metallic{0.0};
		TextureAsset*  metallicTexture{};
		TextureChannel metallicTextureChannel = TextureChannel::Red;

		f32            roughness{1.0};
		TextureAsset*  roughnessTexture{};
		TextureChannel roughnessTextureChannel = TextureChannel::Red;

		Vec3          emissiveFactor{1.0, 1.0, 1.0};
		TextureAsset* emissiveTexture{};

		TextureAsset*  occlusionTexture{};
		f32            occlusionStrength{1.0};
		TextureChannel occlusionTextureChannel = TextureChannel::Red;

		f32           alphaCutoff{0.5};
		AlphaMode     alphaMode{};
		Vec2          uvScale{1.0f, 1.0f};

		// Skybox material
		TextureAsset* sphericalTexture = {};
		f32           exposure = 1.0;
		Color         backgroundColor = Color::WHITE;

		void              Changed() override;
		GPUDescriptorSet* GetDescriptorSet();

		static void RegisterType(NativeReflectType<MaterialAsset>& type);

	private:
		GPUDescriptorSet* m_descriptorSet = nullptr;
		GPUBuffer* m_materialBuffer = nullptr;

		bool UpdateTexture(TextureAsset* texture, u32 slot) const;
	};

	struct SK_API ShaderStageInfo
	{
		ShaderStage stage{};
		String      entryPoint{};
		u32         offset{};
		u32         size{};

		static void RegisterType(NativeReflectType<ShaderStageInfo>& type);
	};

	struct SK_API ShaderVariant
	{
		ShaderAsset*           shaderAsset;
		String                 name;
		PipelineDesc           pipelineDesc;
		Array<ShaderStageInfo> stages;

		Array<u8> spriv;

		static void RegisterType(NativeReflectType<ShaderVariant>& type);
	};

	class SK_API ShaderAsset : public Asset
	{
	public:
		SK_CLASS(ShaderAsset, Asset)

		ShaderVariant* GetVariant(StringView name) const;
		ShaderVariant* FindOrCreateVariant(StringView name);

		static void RegisterType(NativeReflectType<ShaderAsset>& type);

	private:
		Array<Ref<ShaderVariant>> variants;
	};

	void RegisterGraphicsTypes();
}
