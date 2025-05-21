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

#include "GraphicsAssets.hpp"

#include "Graphics.hpp"
#include "mikktspace.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/StringUtils.hpp"


namespace Skore
{
	struct TextureAssetFlags
	{
		enum
		{
			None                = 0,
			HasBaseColorTexture = 1 << 1,
			HasNormalTexture    = 1 << 2,
			HasRoughnessTexture = 1 << 3,
			HasMetallicTexture  = 1 << 4,
			HasEmissiveTexture  = 1 << 5,
			HasOcclusionTexture = 1 << 6,
		};
	};

	class Logger;

	namespace
	{
		struct MikkTSpaceUserData
		{
			Span<MeshAsset::Vertex> vertices;
			Span<u32>               indices;
		};

		i32 GetNumFaces(const SMikkTSpaceContext* pContext)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
			return (i32)mesh.indices.Size() / 3;
		}

		i32 GetNumVerticesOfFace(const SMikkTSpaceContext* pContext, const int iFace)
		{
			return 3;
		}

		u32 GetVertexIndex(const SMikkTSpaceContext* context, i32 iFace, i32 iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(context->m_pUserData);
			auto                faceSize = GetNumVerticesOfFace(context, iFace);
			auto                indicesIndex = (iFace * faceSize) + iVert;
			return mesh.indices[indicesIndex];
		}

		void GetPosition(const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);

			MeshAsset::Vertex& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
			fvPosOut[0] = v.position.x;
			fvPosOut[1] = v.position.y;
			fvPosOut[2] = v.position.z;
		}

		void GetNormal(const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
			MeshAsset::Vertex&  v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
			fvNormOut[0] = v.normal.x;
			fvNormOut[1] = v.normal.y;
			fvNormOut[2] = v.normal.z;
		}

		void GetTexCoord(const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
			MeshAsset::Vertex&  v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
			fvTexcOut[0] = v.texCoord.x;
			fvTexcOut[1] = v.texCoord.y;
		}

		void SetTangentSpaceBasic(const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
			MeshAsset::Vertex&  v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
			v.tangent.x = fvTangent[0];
			v.tangent.y = fvTangent[1];
			v.tangent.z = fvTangent[2];
			v.tangent.w = -fSign;
		}

		Vec3 CalculateTangent(const MeshAsset::Vertex& v1, const MeshAsset::Vertex& v2, const MeshAsset::Vertex& v3)
		{
			Vec3 edge1 = v2.position - v1.position;
			Vec3 edge2 = v3.position - v1.position;
			Vec2 deltaUV1 = v2.texCoord - v1.texCoord;
			Vec2 deltaUV2 = v3.texCoord - v1.texCoord;

			Vec3 tangent{};

			float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
			tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
			tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
			tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

			return tangent;
		}

		void CalculateTangents(Span<MeshAsset::Vertex> vertices, Span<u32> indices)
		{
			//Calculate tangents
			for (usize i = 0; i < indices.Size(); i += 3)
			{
				u32 idx0 = indices[i + 0];
				u32 idx1 = indices[i + 1];
				u32 idx2 = indices[i + 2];

				vertices[idx0].tangent = Vec4{CalculateTangent(vertices[idx0], vertices[idx1], vertices[idx2]), 1.0};
				vertices[idx1].tangent = Vec4{CalculateTangent(vertices[idx1], vertices[idx2], vertices[idx0]), 1.0};
				vertices[idx2].tangent = Vec4{CalculateTangent(vertices[idx2], vertices[idx0], vertices[idx1]), 1.0};
			}
		}


		void CalculateTangents(Span<MeshAsset::Vertex> vertices)
		{
			//Calculate tangents
			for (usize i = 0; i < vertices.Size(); i += 3)
			{
				u32 idx0 = i + 0;
				u32 idx1 = i + 1;
				u32 idx2 = i + 2;
				vertices[idx0].tangent = Vec4{CalculateTangent(vertices[idx0], vertices[idx1], vertices[idx2]), 1.0};
				vertices[idx1].tangent = Vec4{CalculateTangent(vertices[idx1], vertices[idx2], vertices[idx0]), 1.0};
				vertices[idx2].tangent = Vec4{CalculateTangent(vertices[idx2], vertices[idx0], vertices[idx1]), 1.0};
			}
		}
	}


	static Logger& logger = Logger::GetLogger("Skore::GraphicsAssets");

	TextureAsset::~TextureAsset()
	{
		if (m_texture)
		{
			m_texture->Destroy();
		}
	}

	bool TextureAsset::SetTextureDataFromFile(StringView path, bool isHDR, bool generateMips, bool compressToGPUFormat)
	{
		i32 width{};
		i32 height{};
		i32 channels{};

		if (!isHDR)
		{
			u8* bytes = stbi_load(path.CStr(), &width, &height, &channels, 4);
			if (bytes)
			{
				SetTextureData(Extent{static_cast<u32>(width), static_cast<u32>(height)}, {bytes, static_cast<usize>(width * height * 4)}, TextureFormat::R8G8B8A8_UNORM, generateMips,
				               compressToGPUFormat);
				stbi_image_free(bytes);
				return true;
			}
		}
		else
		{
			TextureFormat hdrFormat = TextureFormat::R32G32B32A32_FLOAT;
			if (f32* bytes = stbi_loadf(path.CStr(), &width, &height, &channels, 4))
			{
				//Limit HDR values to 50.0f
				for (i32 i = 0; i < width * height * 4; i++)
				{
					bytes[i] = std::min(bytes[i], 50.0f);
				}

				u32 strideSize = GetTextureFormatSize(hdrFormat);

				SetTextureData(Extent{static_cast<u32>(width), static_cast<u32>(height)},
				               {
					               reinterpret_cast<u8*>(bytes),
					               static_cast<usize>(width * height * strideSize)
				               },
				               hdrFormat,
				               generateMips, compressToGPUFormat);

				stbi_image_free(bytes);
				return true;
			}
		}

		return false;
	}

	bool TextureAsset::SetTextureDataFromFileInMemory(Span<const u8> buffer, bool isHDR, bool generateMips, bool compressToGPUFormat)
	{
		i32 width{};
		i32 height{};
		i32 channels{};

		if (!isHDR)
		{
			if (u8* bytes = stbi_load_from_memory(buffer.begin(), static_cast<i32>(buffer.Size()), &width, &height, &channels, 4))
			{
				SetTextureData(Extent{static_cast<u32>(width), static_cast<u32>(height)}, {bytes, static_cast<usize>(width * height * 4)}, TextureFormat::R8G8B8A8_UNORM, generateMips,
				               compressToGPUFormat);
				stbi_image_free(bytes);
				return true;
			}
		}
		else
		{
			SK_ASSERT(false, "HDR textures are not supported yet");
		}

		return false;
	}

	void TextureAsset::SetTextureData(Extent extent, Span<const u8> bytes, TextureFormat format, bool generateMips, bool compressToGPUFormat)
	{
		m_images.Clear();
		m_textureData.Clear();

		m_format = format;
		m_mipLevels = generateMips ? static_cast<u32>(std::min(std::floor(std::log2(std::max(extent.width, extent.height))), 11.0)) + 1 : 1;

		u32 pixelSize = GetTextureFormatSize(format);
		i8  channels = GetTextureFormatNumChannels(format);

		usize totalSize{};

		{
			u32 mipWidth = extent.width;
			u32 mipHeight = extent.height;
			for (u32 i = 0; i < m_mipLevels; i++)
			{
				totalSize += mipWidth * mipHeight * pixelSize;
				if (mipWidth > 1) mipWidth /= 2;
				if (mipHeight > 1) mipHeight /= 2;
			}
		}

		SK_ASSERT(totalSize > bytes.Size(), "Texture data size is too small");

		m_textureData.Resize(totalSize);
		memcpy(m_textureData.Data(), bytes.Data(), bytes.Size());


		u32 mipWidth = extent.width;
		u32 mipHeight = extent.height;

		u32 offset{};

		for (u32 i = 0; i < m_mipLevels; i++)
		{
			m_images.EmplaceBack(TextureAssetImage{
				.extent = {mipWidth, mipHeight},
				.mip = i,
				.arrayLayer = 0,
			});

			u32 mipOffset = offset + mipWidth * mipHeight * pixelSize;

			if (mipWidth > 1 && mipHeight > 1)
			{
				// Box filtering for mip generation
				const u32 nextWidth = mipWidth / 2;
				const u32 nextHeight = mipHeight / 2;

				if (m_format == TextureFormat::R32G32B32A32_FLOAT)
				{
					float* srcData = reinterpret_cast<float*>(m_textureData.Data() + offset);
					float* dstData = reinterpret_cast<float*>(m_textureData.Data() + mipOffset);

					for (u32 y = 0; y < nextHeight; ++y)
					{
						for (u32 x = 0; x < nextWidth; ++x)
						{
							for (i8 c = 0; c < channels; ++c)
							{
								float sum = srcData[((y * 2) * mipWidth + (x * 2)) * channels + c] +
									srcData[((y * 2) * mipWidth + (x * 2 + 1)) * channels + c] +
									srcData[((y * 2 + 1) * mipWidth + (x * 2)) * channels + c] +
									srcData[((y * 2 + 1) * mipWidth + (x * 2 + 1)) * channels + c];
								dstData[(y * nextWidth + x) * channels + c] = sum * 0.25f;
							}
						}
					}
				}
				else
				{
					u8* srcData = m_textureData.Data() + offset;
					u8* dstData = m_textureData.Data() + mipOffset;

					for (u32 y = 0; y < nextHeight; ++y)
					{
						for (u32 x = 0; x < nextWidth; ++x)
						{
							for (i8 c = 0; c < channels; ++c)
							{
								u32 sum = srcData[((y * 2) * mipWidth + (x * 2)) * channels + c] +
									srcData[((y * 2) * mipWidth + (x * 2 + 1)) * channels + c] +
									srcData[((y * 2 + 1) * mipWidth + (x * 2)) * channels + c] +
									srcData[((y * 2 + 1) * mipWidth + (x * 2 + 1)) * channels + c];
								dstData[(y * nextWidth + x) * channels + c] = static_cast<u8>(sum >> 2);
							}
						}
					}
				}
			}

			offset = mipOffset;

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}
	}

	GPUTexture* TextureAsset::GetTexture()
	{
		if (m_texture == nullptr)
		{
			u32 pixelSize = GetTextureFormatSize(m_format);

			if (m_textureData.Empty())
			{
				logger.Error("Texture data is empty");
				return nullptr;
			}

			if (m_images.Empty())
			{
				logger.Error("Texture images are empty");
				return nullptr;
			}

			m_texture = Graphics::CreateTexture(TextureDesc{
				.extent = {(m_images[0].extent.width), m_images[0].extent.height, 1},
				.mipLevels = m_mipLevels,
				.format = m_format,
				.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
				.debugName = String(GetName()) + "_Texture"
			});

			Array<TextureDataRegion> regions{};
			regions.Reserve(m_images.Size());

			u32 offset{};

			for (const TextureAssetImage& textureAssetImage : m_images)
			{
				regions.EmplaceBack(TextureDataRegion{
					.dataOffset = offset,
					.mipLevel = textureAssetImage.mip,
					.arrayLayer = textureAssetImage.arrayLayer,
					.extent = Extent3D{textureAssetImage.extent.width, textureAssetImage.extent.height, 1},
				});

				offset += textureAssetImage.extent.width * textureAssetImage.extent.height * pixelSize;
			}

			TextureDataInfo textureDataInfo{
				.texture = m_texture,
				.data = m_textureData.Data(),
				.size = m_textureData.Size(),
				.regions = regions
			};

			Graphics::UploadTextureData(textureDataInfo);
		}

		return m_texture;
	}

	void TextureAsset::RegisterType(NativeReflectType<TextureAsset>& type)
	{
		type.Field<&TextureAsset::m_format>("format");
		type.Field<&TextureAsset::m_images>("images");
		type.Field<&TextureAsset::m_mipLevels>("mipLevels");
		type.Field<&TextureAsset::m_textureData>("textureData");
	}

	MeshAsset::~MeshAsset()
	{
		if (m_vertexBuffer)
		{
			m_vertexBuffer->Destroy();
		}

		if (m_indexBuffer)
		{
			m_indexBuffer->Destroy();
		}
	}

	void MeshAsset::SetVertices(Span<Vertex> vertices)
	{
		m_boundingBox.min = Vec3{F32_MAX, F32_MAX, F32_MAX};
		m_boundingBox.max = Vec3{F32_LOW, F32_LOW, F32_LOW};
		for (const auto& vertex : vertices)
		{
			m_boundingBox.min = Math::Min(m_boundingBox.min, vertex.position);
			m_boundingBox.max = Math::Max(m_boundingBox.max, vertex.position);
		}

		m_vertices.Clear();
		m_vertices.Resize(vertices.Size() * sizeof(Vertex));
		memcpy(m_vertices.Data(), vertices.Data(), vertices.Size() * sizeof(Vertex));
	}

	void MeshAsset::SetIndices(Span<u32> indices)
	{
		m_indices.Clear();
		m_indices.Resize(indices.Size() * sizeof(u32));
		memcpy(m_indices.Data(), indices.Data(), indices.Size() * sizeof(u32));
	}

	void MeshAsset::SetPrimitives(Span<Primitive> primitives)
	{
		m_primitives = primitives;
	}

	void MeshAsset::SetMaterials(Span<MaterialAsset*> materials)
	{
		m_materials = materials;
	}

	void MeshAsset::CalcTangents(bool useMikktspace)
	{
		Span<Vertex> vertices = Span{reinterpret_cast<Vertex*>(m_vertices.Data()), m_vertices.Size() / sizeof(Vertex)};
		Span<u32>    indices = Span{reinterpret_cast<u32*>(m_indices.Data()), m_indices.Size() / sizeof(u32)};

		if (useMikktspace)
		{
			MikkTSpaceUserData userData{
				.vertices = vertices,
				.indices = indices
			};

			SMikkTSpaceInterface anInterface{
				.m_getNumFaces = GetNumFaces,
				.m_getNumVerticesOfFace = GetNumVerticesOfFace,
				.m_getPosition = GetPosition,
				.m_getNormal = GetNormal,
				.m_getTexCoord = GetTexCoord,
				.m_setTSpaceBasic = SetTangentSpaceBasic
			};

			SMikkTSpaceContext mikkTSpaceContext{
				.m_pInterface = &anInterface,
				.m_pUserData = &userData
			};

			genTangSpaceDefault(&mikkTSpaceContext);
		}
		else
		{
			CalculateTangents(vertices, indices);
		}
	}

	void MeshAsset::CalcNormals()
	{
		Span<Vertex> vertices = Span{reinterpret_cast<Vertex*>(m_vertices.Data()), m_vertices.Size() / sizeof(Vertex)};
		Span<u32>    indices = Span{reinterpret_cast<u32*>(m_indices.Data()), m_indices.Size() / sizeof(u32)};

		// Reset all normals to zero
		for (auto& vertex : vertices)
		{
			vertex.normal = Vec3{0.0f, 0.0f, 0.0f};
		}

		// Calculate face normals and accumulate them to vertices
		for (usize i = 0; i < indices.Size(); i += 3)
		{
			u32 idx0 = indices[i + 0];
			u32 idx1 = indices[i + 1];
			u32 idx2 = indices[i + 2];

			Vec3& v0 = vertices[idx0].position;
			Vec3& v1 = vertices[idx1].position;
			Vec3& v2 = vertices[idx2].position;

			// Calculate face normal using cross product
			Vec3 edge1 = v1 - v0;
			Vec3 edge2 = v2 - v0;
			Vec3 normal = Vec3{
				edge1.y * edge2.z - edge1.z * edge2.y,
				edge1.z * edge2.x - edge1.x * edge2.z,
				edge1.x * edge2.y - edge1.y * edge2.x
			};

			// Accumulate the normal to all vertices of this face
			vertices[idx0].normal = vertices[idx0].normal + normal;
			vertices[idx1].normal = vertices[idx1].normal + normal;
			vertices[idx2].normal = vertices[idx2].normal + normal;
		}

		// Normalize all vertex normals
		for (auto& vertex : vertices)
		{
			// Calculate length
			float length = sqrt(vertex.normal.x * vertex.normal.x +
				vertex.normal.y * vertex.normal.y +
				vertex.normal.z * vertex.normal.z);

			// Avoid division by zero
			if (length > 0.00001f)
			{
				float invLength = 1.0f / length;
				vertex.normal.x *= invLength;
				vertex.normal.y *= invLength;
				vertex.normal.z *= invLength;
			}
			else
			{
				// Default normal if calculation failed
				vertex.normal = Vec3{0.0f, 1.0f, 0.0f};
			}
		}
	}

	GPUBuffer* MeshAsset::GetVertexBuffer()
	{
		StringView assetName = GetName();

		if (m_vertexBuffer == nullptr)
		{
			m_vertexBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = m_vertices.Size(),
				.usage = ResourceUsage::CopyDest | ResourceUsage::VertexBuffer,
				.hostVisible = false,
				.persistentMapped = false,
				.debugName = String(GetName()) + "_VertexBuffer"
			});

			Graphics::UploadBufferData(BufferUploadInfo{
				.buffer = m_vertexBuffer,
				.data = m_vertices.Data(),
				.size = m_vertices.Size()
			});
		}

		return m_vertexBuffer;
	}

	GPUBuffer* MeshAsset::GetIndexBuffer()
	{
		if (m_indexBuffer == nullptr)
		{
			m_indexBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = m_indices.Size(),
				.usage = ResourceUsage::CopyDest | ResourceUsage::IndexBuffer,
				.hostVisible = false,
				.persistentMapped = false,
				.debugName = String(GetName()) + "_IndexBuffer"
			});

			Graphics::UploadBufferData(BufferUploadInfo{
				.buffer = m_indexBuffer,
				.data = m_indices.Data(),
				.size = m_indices.Size()
			});
		}

		return m_indexBuffer;
	}

	Span<MeshAsset::Primitive> MeshAsset::GetPrimitives() const
	{
		return m_primitives;
	}

	Span<MaterialAsset*> MeshAsset::GetMaterials() const
	{
		return m_materials;
	}

	void MeshAsset::RegisterType(NativeReflectType<MeshAsset>& type)
	{
		type.Field<&MeshAsset::m_vertices>("vertices");
		type.Field<&MeshAsset::m_indices>("indices");
		type.Field<&MeshAsset::m_primitives>("primitives");
		type.Field<&MeshAsset::m_materials>("materials");
		type.Field<&MeshAsset::m_boundingBox>("boundingBox");
	}

	MaterialAsset::~MaterialAsset()
	{
		if (m_descriptorSet) m_descriptorSet->Destroy();
		if (m_materialBuffer) m_materialBuffer->Destroy();
	}

	bool MaterialAsset::UpdateTexture(TextureAsset* textureAsset, u32 slot) const
	{
		if (textureAsset)
		{
			if (GPUTexture* texture = textureAsset->GetTexture())
			{
				m_descriptorSet->UpdateTexture(slot, texture);
				return true;
			}
		}
		m_descriptorSet->UpdateTexture(slot, Graphics::GetWhiteTexture());
		return false;
	}

	void MaterialAsset::Changed()
	{
		Graphics::WaitIdle();
		if (m_descriptorSet) m_descriptorSet->Destroy();
		m_descriptorSet = nullptr;
	}

	GPUDescriptorSet* MaterialAsset::GetDescriptorSet()
	{
		if (m_descriptorSet == nullptr)
		{
			if (type == MaterialType::Opaque)
			{
				if (m_materialBuffer == nullptr)
				{
					m_materialBuffer = Graphics::CreateBuffer(BufferDesc{
						.size = sizeof(MaterialBuffer),
						.usage = ResourceUsage::CopyDest | ResourceUsage::ConstantBuffer,
						.hostVisible = false,
						.persistentMapped = false,
						.debugName = String(GetName()) + "_MaterialBuffer"
					});
				}

				m_descriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
					.bindings = {
						DescriptorSetLayoutBinding{
							.binding = 0,
							.descriptorType = DescriptorType::UniformBuffer
						},
						DescriptorSetLayoutBinding{
							.binding = 1,
							.descriptorType = DescriptorType::Sampler
						},
						DescriptorSetLayoutBinding{
							.binding = 2,
							.descriptorType = DescriptorType::SampledImage
						},
						DescriptorSetLayoutBinding{
							.binding = 3,
							.descriptorType = DescriptorType::SampledImage
						},
						DescriptorSetLayoutBinding{
							.binding = 4,
							.descriptorType = DescriptorType::SampledImage
						},
						DescriptorSetLayoutBinding{
							.binding = 5,
							.descriptorType = DescriptorType::SampledImage
						}
					},
					.debugName = String(GetName()) + "_DescriptorSet"
				});

				m_descriptorSet->UpdateBuffer(0, m_materialBuffer, 0, 0);
				m_descriptorSet->UpdateSampler(1, Graphics::GetLinearSampler());


				MaterialBuffer materialBuffer;
				materialBuffer.baseColor = baseColor.ToVec3();
				materialBuffer.alphaCutoff = alphaCutoff;
				materialBuffer.metallic = metallic;
				materialBuffer.roughness = roughness;
				materialBuffer.textureFlags = TextureAssetFlags::None;


				materialBuffer.textureProps = 0;
				materialBuffer.textureProps |= static_cast<u8>(roughnessTextureChannel) << 0;
				materialBuffer.textureProps |= static_cast<u8>(metallicTextureChannel) << 8;
				materialBuffer.textureProps |= static_cast<u8>(occlusionTextureChannel) << 16;

				if (UpdateTexture(baseColorTexture, 2))
				{
					materialBuffer.textureFlags |= TextureAssetFlags::HasBaseColorTexture;
				}

				if (UpdateTexture(normalTexture, 3))
				{
					materialBuffer.textureFlags |= TextureAssetFlags::HasNormalTexture;
				}

				if (UpdateTexture(roughnessTexture, 4))
				{
					materialBuffer.textureFlags |= TextureAssetFlags::HasRoughnessTexture;
				}

				if (UpdateTexture(roughnessTexture, 5))
				{
					materialBuffer.textureFlags |= TextureAssetFlags::HasMetallicTexture;
				}

				Graphics::UploadBufferData(BufferUploadInfo{
					.buffer = m_materialBuffer,
					.data = &materialBuffer,
					.size = sizeof(MaterialBuffer)
				});

			}

			else if (type == MaterialType::SkyboxEquirectangular)
			{
				m_descriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
					.bindings = {
						DescriptorSetLayoutBinding{
							.binding = 0,
							.descriptorType = DescriptorType::SampledImage
						},
						DescriptorSetLayoutBinding{
							.binding = 1,
							.descriptorType = DescriptorType::Sampler
						}
					}
				});

				UpdateTexture(sphericalTexture, 0);
				m_descriptorSet->UpdateSampler(1, Graphics::GetLinearSampler());
			}
		}
		return m_descriptorSet;
	}

	void TextureAssetImage::RegisterType(NativeReflectType<TextureAssetImage>& type)
	{
		type.Field<&TextureAssetImage::extent>("extent");
		type.Field<&TextureAssetImage::mip>("mip");
		type.Field<&TextureAssetImage::arrayLayer>("arrayLayer");
	}

	void MaterialAsset::RegisterType(NativeReflectType<MaterialAsset>& type)
	{
		type.Field<&MaterialAsset::type>("type");

		type.Field<&MaterialAsset::baseColor>("baseColor");
		type.Field<&MaterialAsset::baseColorTexture>("baseColorTexture");

		type.Field<&MaterialAsset::normalTexture>("normalTexture");
		type.Field<&MaterialAsset::normalMultiplier>("normalMultiplier");

		type.Field<&MaterialAsset::metallic>("metallic").Attribute<UISliderProperty>(0.0f, 1.0f);
		type.Field<&MaterialAsset::metallicTexture>("metallicTexture");
		type.Field<&MaterialAsset::metallicTextureChannel>("metallicTexture");

		type.Field<&MaterialAsset::roughness>("roughness").Attribute<UISliderProperty>(0.0f, 1.0f);
		type.Field<&MaterialAsset::roughnessTexture>("roughnessTexture");
		type.Field<&MaterialAsset::roughnessTextureChannel>("roughnessTextureChannel");

		type.Field<&MaterialAsset::emissiveTexture>("emissiveTexture");
		type.Field<&MaterialAsset::emissiveFactor>("emissiveFactor");

		type.Field<&MaterialAsset::occlusionTexture>("occlusionTexture");
		type.Field<&MaterialAsset::occlusionStrength>("occlusionStrength").Attribute<UISliderProperty>(0.0f, 1.0f);

		type.Field<&MaterialAsset::alphaCutoff>("alphaCutoff").Attribute<UISliderProperty>(0.0f, 1.0f);
		type.Field<&MaterialAsset::alphaMode>("alphaMode");
		type.Field<&MaterialAsset::uvScale>("uvScale");

		type.Field<&MaterialAsset::sphericalTexture>("sphericalTexture");
		type.Field<&MaterialAsset::exposure>("exposure");
		type.Field<&MaterialAsset::backgroundColor>("backgroundColor");
	}

	void ShaderStageInfo::RegisterType(NativeReflectType<ShaderStageInfo>& type)
	{
		type.Field<&ShaderStageInfo::stage>("stage");
		type.Field<&ShaderStageInfo::entryPoint>("entryPoint");
		type.Field<&ShaderStageInfo::offset>("offset");
		type.Field<&ShaderStageInfo::size>("size");
	}

	void ShaderVariant::RegisterType(NativeReflectType<ShaderVariant>& type)
	{
		type.Field<&ShaderVariant::name>("name");
		type.Field<&ShaderVariant::pipelineDesc>("pipelineDesc");
		type.Field<&ShaderVariant::spriv>("spriv");
	}

	ShaderVariant* ShaderAsset::GetVariant(StringView name) const
	{
		for (auto& variant : variants)
		{
			if (variant->name == name)
			{
				return variant.Get();
			}
		}
		return nullptr;
	}

	ShaderVariant* ShaderAsset::FindOrCreateVariant(StringView name)
	{
		ShaderVariant* variant = GetVariant(name);
		if (variant != nullptr)
		{
			return variant;
		}

		logger.Debug("shader variant {} created for shader {}", name, GetName());
		return variants.EmplaceBack(MakeRef<ShaderVariant>(ShaderVariant{.shaderAsset = this, .name = name})).Get();
	}

	void ShaderAsset::RegisterType(NativeReflectType<ShaderAsset>& type)
	{
		type.Field<&ShaderAsset::variants>("variants");
	}
}
