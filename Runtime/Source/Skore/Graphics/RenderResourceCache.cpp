#include "Skore/Graphics/RenderResourceCache.hpp"

#include <mutex>

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		HashMap<RID, std::shared_ptr<FontResourceCache>>     fontCache;
		HashMap<RID, std::shared_ptr<TextureResourceCache>>  textureCache;
		HashMap<RID, std::shared_ptr<MaterialResourceCache>> materialCache;
		HashMap<RID, std::shared_ptr<MeshResourceCache>>     meshCache;
		HashMap<RID, std::shared_ptr<SkinResourceCache>>     skinCache;

		std::mutex fontCacheMutex{};
		std::mutex materialCacheMutex{};
		std::mutex textureCacheMutex{};
		std::mutex meshCacheMutex{};
		std::mutex skinCacheMutex{};

		u32               nextGeometryIndex = 0;
		u32               nextMaterialIndex = 0;
		u32               nextTextureIndex = 0;
		GPUDescriptorSet* globalDescriptorSet = nullptr;
		GPUBuffer*        materialDataBuffer = nullptr;
		u32               materialDataBufferCapacity = 0;
		u32               materialDataCount = 0;

		RID defaultMaterial;

		struct MaterialData
		{
			Vec3 baseColor;
			f32  roughness;

			f32  metallic;
			i32  baseColorTexture;
			i32  normalTexture;
			i32  roughnessTexture;

			i32  metallicTexture;
			Vec2 uvScale;
			f32  emissiveFactor;

			Vec3 emissiveColor;
			i32  emissiveTexture;

			i32  alphaMode;
			f32  alphaCutoff;
			f32 pad0;
			f32 pad1;
		};

		bool UpdateTexture(GPUDescriptorSet* descriptorSet, RID texture, u32 slot)
		{
			if (texture)
			{
				TextureResourceCache* data = RenderResourceCache::GetTextureCache(texture);
				if (data->texture)
				{
					descriptorSet->UpdateTexture(slot, data->texture);
					return true;
				}
			}

			descriptorSet->UpdateTexture(slot, Graphics::GetWhiteTexture());
			return false;
		}

		void UpdateMaterialStorageData(const ResourceObject& materialObject, MaterialResourceCache* materialCache)
		{
			StringView name = materialObject.GetString(MaterialResource::Name);

			materialCache->type = materialObject.GetEnum<MaterialResource::MaterialType>(MaterialResource::Type);
			materialCache->shader = materialObject.GetReference(MaterialResource::Shader);

			if (materialCache->type == MaterialResource::MaterialType::Opaque)
			{
				Color baseColor = materialObject.GetColor(MaterialResource::BaseColor);
				RID   baseColorTexture = materialObject.GetReference(MaterialResource::BaseColorTexture);
				RID   normalTexture = materialObject.GetReference(MaterialResource::NormalTexture);
				RID   roughnessTexture = materialObject.GetReference(MaterialResource::RoughnessTexture);
				RID   metallicTexture = materialObject.GetReference(MaterialResource::MetallicTexture);
				Color emissiveColor = materialObject.GetColor(MaterialResource::EmissiveColor);
				f32   emissiveFactor = materialObject.GetFloat(MaterialResource::EmissiveFactor);
				RID   emissiveTexture = materialObject.GetReference(MaterialResource::EmissiveTexture);

				// Populate global MaterialData buffer for bindless rendering
				if (materialCache->materialIndex == U32_MAX)
				{
					materialCache->materialIndex = nextMaterialIndex++;
				}

				u32 requiredCapacity = materialCache->materialIndex + 1;
				if (requiredCapacity > materialDataBufferCapacity)
				{
					u32 newCapacity = requiredCapacity * 2;
					if (newCapacity < 64) newCapacity = 64;

					GPUBuffer* newBuffer = Graphics::CreateBuffer(BufferDesc{
						.size = newCapacity * sizeof(MaterialData),
						.usage = ResourceUsage::ShaderResource,
						.hostVisible = true,
						.persistentMapped = true,
						.debugName = "MaterialDataBuffer"
					});

					if (materialDataBuffer)
					{
						memcpy(newBuffer->GetMappedData(), materialDataBuffer->GetMappedData(), materialDataBufferCapacity * sizeof(MaterialData));
						materialDataBuffer->Destroy();
					}

					materialDataBuffer = newBuffer;
					materialDataBufferCapacity = newCapacity;
				}

				auto resolveTextureIndex = [](RID textureRID) -> i32
				{
					if (!textureRID) return -1;
					TextureResourceCache* tc = RenderResourceCache::GetTextureCache(textureRID);
					if (tc && tc->textureIndex != U32_MAX)
						return static_cast<i32>(tc->textureIndex);
					return -1;
				};

				MaterialData matData{};
				matData.baseColor = baseColor.ToVec3();
				matData.roughness = materialObject.GetFloat(MaterialResource::Roughness);
				matData.metallic = materialObject.GetFloat(MaterialResource::Metallic);
				matData.baseColorTexture = resolveTextureIndex(baseColorTexture);
				matData.normalTexture = resolveTextureIndex(normalTexture);
				matData.roughnessTexture = resolveTextureIndex(roughnessTexture);
				matData.metallicTexture = resolveTextureIndex(metallicTexture);
				matData.uvScale = materialObject.GetVec2(MaterialResource::UvScale);
				matData.emissiveFactor = emissiveFactor;
				matData.emissiveColor = emissiveColor.ToVec3();
				matData.emissiveTexture = resolveTextureIndex(emissiveTexture);
				matData.alphaMode = materialObject.GetEnum(MaterialResource::AlphaMode);
				matData.alphaCutoff = materialObject.GetFloat(MaterialResource::AlphaCutoff);

				materialCache->transparent = materialObject.GetEnum<MaterialResource::MaterialAlphaMode>(MaterialResource::AlphaMode) == MaterialResource::MaterialAlphaMode::Blend;

				MaterialData* mapped = static_cast<MaterialData*>(materialDataBuffer->GetMappedData());
				mapped[materialCache->materialIndex] = matData;

				materialDataCount = std::max(materialDataCount, materialCache->materialIndex + 1);

				globalDescriptorSet->UpdateBuffer(0, materialDataBuffer, 0, materialDataCount * sizeof(MaterialData));
			}
			else if (materialCache->type == MaterialResource::MaterialType::SkyboxEquirectangular)
			{
				RID sphericalTexture = materialObject.GetReference(MaterialResource::SphericalTexture);

				materialCache->descriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
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

				materialCache->skyMaterialTexture = RenderResourceCache::GetTextureCache(sphericalTexture);

				UpdateTexture(materialCache->descriptorSet, sphericalTexture, 0);
				materialCache->descriptorSet->UpdateSampler(1, Graphics::GetLinearSampler());

				if (materialCache->skyMaterialTexture && materialCache->skyMaterialTexture->texture)
				{
					if (!materialCache->diffuseIrradianceTexture)
					{
						materialCache->diffuseIrradianceTexture = Graphics::CreateTexture(TextureDesc{
							.extent = {64, 64, 1},
							.arrayLayers = 6,
							.format = TextureFormat::R16G16B16A16_FLOAT,
							.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
							.cubemap = true,
							.debugName = "SceneRendererViewport_irradianceTexture"
						});
					}

					if (!materialCache->specularMapTexture)
					{
						materialCache->specularMapTexture = Graphics::CreateTexture(TextureDesc{
							.extent = {256, 256, 1},
							.mipLevels = 8,
							.arrayLayers = 6,
							.format = TextureFormat::R16G16B16A16_FLOAT,
							.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
							.cubemap = true,
							.debugName = "SceneRendererViewport_SpecularMapTexture"
						});
					}

					GPUTexture* cubeMapTexture = Graphics::CreateTexture(TextureDesc{
						.extent = {256, 256, 1},
						.mipLevels = 8,
						.arrayLayers = 6,
						.format = TextureFormat::R16G16B16A16_FLOAT,
						.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
						.cubemap = true,
						.debugName = "SceneRendererViewport_CubemapTexture"
					});

					GPUCommandBuffer* cmd = Graphics::GetFreeCommandBuffer();
					cmd->Begin();

					EquirectangularToCubeMap equirectangularToCubemap;
					equirectangularToCubemap.Init();
					equirectangularToCubemap.Execute(cmd, materialCache->skyMaterialTexture->texture, cubeMapTexture);

					SinglePassDownsampler downsampler;
					downsampler.Init(cubeMapTexture);
					downsampler.Execute(cmd);

					DiffuseIrradianceGenerator diffuseIrradianceGenerator;
					diffuseIrradianceGenerator.Init();
					diffuseIrradianceGenerator.Execute(cmd, cubeMapTexture, materialCache->diffuseIrradianceTexture);

					SpecularMapGenerator specularMapGenerator;
					specularMapGenerator.Init(cubeMapTexture, materialCache->specularMapTexture);
					specularMapGenerator.Execute(cmd);

					cmd->End();
					Graphics::SubmitGPUWork(cmd, true);
					Graphics::AddFreeCommandBuffer(cmd);

					equirectangularToCubemap.Destroy();
					diffuseIrradianceGenerator.Destroy();
					cubeMapTexture->Destroy();
				}
			}
		}

		void GraphicsResourceStorageMaterialReload(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
		{
			MaterialResourceCache* materialCache = static_cast<MaterialResourceCache*>(userData);
			UpdateMaterialStorageData(newValue, materialCache);
		}
	}

	void ResourceCacheBase::DecreaseUsage()
	{
		if (usage > 0) usage--;
	}

	void ResourceCacheBase::IncreaseUsage()
	{
		usage++;
	}

	bool FontResourceCache::GetAdvance(f64& advance, u32 codepoint1, u32 codepoint2)
	{
		auto glyph1 = glyphs.Find(codepoint1);
		auto glyph2 = glyphs.Find(codepoint2);
		if (glyph1 == glyphs.end() || glyph2 == glyphs.end())
		{
			return false;
		}
		advance = glyph1->second.advance;

		auto it = kerning.Find(Pair(glyph1->second.index, glyph2->second.index));
		if (it != kerning.end())
		{
			advance += it->second;
		}
		return true;
	}

	FontResourceCache* RenderResourceCache::GetFontCache(RID font)
	{
		if (!font) return nullptr;

		std::unique_lock lock(fontCacheMutex);
		auto             it = fontCache.Find(font);
		if (it == fontCache.end())
		{
			std::shared_ptr<FontResourceCache> fontData = std::make_shared<FontResourceCache>(font);
			fontData->fontId = fontCache.Size() + 1;

			FontResourceData data;
			String           name;
			{
				ResourceObject fontObject = Resources::Read(font);
				if (!fontObject) return nullptr;

				name = fontObject.GetString(FontResource::Name);

				Span<u8>  compressedData = fontObject.GetBlob(FontResource::FontData);
				usize     size = fontObject.GetUInt(FontResource::FontDataUncompressedSize);
				Array<u8> uncompressedData(size);
				size = Compression::Decompress(uncompressedData.Data(), uncompressedData.Size(), compressedData.Data(), compressedData.Size(), CompressionMode::ZSTD);
				uncompressedData.Resize(size);

				BinaryArchiveReader reader(uncompressedData);
				reader.BeginMap("data");
				data.Deserialize(reader);
				reader.EndMap();
			}

			fontData->maxHeightGlyph = data.maxHeightGlyph;
			fontData->metrics = data.metrics;

			for (const FontGlyph& glyph : data.glyphs)
			{
				fontData->glyphs.Insert(glyph.codepoint, glyph);
			}

			for (const FontKerning& kerning : data.kernings)
			{
				fontData->kerning.Insert(Pair(kerning.first, kerning.second), kerning.offset);
			}

			fontData->texture = Graphics::CreateTexture(TextureDesc{
				.extent = Extent3D(data.atlas.extent.width, data.atlas.extent.height, 1),
				.format = TextureFormat::R32G32B32A32_FLOAT,
				.debugName = "FontAtlas_" + name
			});

			TextureDataInfo textureDataInfo{
				.texture = fontData->texture,
				.data = data.atlas.pixels.Data(),
				.size = data.atlas.pixels.Size()
			};

			Graphics::UploadTextureData(textureDataInfo);

			it = fontCache.Insert(font, fontData).first;
		}
		return it->second.get();
	}

	TextureResourceCache* RenderResourceCache::GetTextureCache(RID texture)
	{
		std::unique_lock lock(textureCacheMutex);
		auto             it = textureCache.Find(texture);
		if (it == textureCache.end())
		{
			std::shared_ptr<TextureResourceCache> textureStorage = std::make_shared<TextureResourceCache>(texture);

			if (ResourceObject textureObject = Resources::Read(texture))
			{
				StringView    name = textureObject.GetString(TextureResource::Name);
				u32           totalUncompressedSize = textureObject.GetUInt(TextureResource::TotalUncompressedSize);
				u32           mipLevels = textureObject.GetUInt(TextureResource::MipLevels);
				TextureFormat format = textureObject.GetEnum<TextureFormat>(TextureResource::Format);

				Span<RID> images = textureObject.GetSubObjectList(TextureResource::Images);

				ResourceObject firstImageObject = Resources::Read(images[0]);
				Vec2           extent = firstImageObject.GetVec2(TextureImageResource::Extent);
				u32            firstMipCompressedSize = firstImageObject.GetUInt(TextureImageResource::DataSize);

				CompressionMode compressionMode = textureObject.GetEnum<CompressionMode>(TextureResource::CompressionMode);
				ResourceBuffer  buffer = textureObject.GetBuffer(TextureResource::PixelData);

				textureStorage->texture = Graphics::CreateTexture(TextureDesc{
					.extent = {static_cast<u32>(extent.x), static_cast<u32>(extent.y), 1},
					.mipLevels = std::max(mipLevels, 1u),
					.format = format,
					.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
					.debugName = String(name) + "_Texture"
				});

				GPUBuffer* gpuBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = totalUncompressedSize,
					.usage = ResourceUsage::CopySource,
					.hostVisible = true,
					.persistentMapped = true
				});

				VoidPtr mem = gpuBuffer->GetMappedData();
				if (compressionMode == CompressionMode::None)
				{
					buffer.CopyData(mem, totalUncompressedSize);
				}
				else
				{
					ByteBuffer byteBuffer = {};
					byteBuffer.Resize(firstMipCompressedSize);
					u32 offset = 0;

					for (RID imageRID : images)
					{
						ResourceObject imageObject = Resources::Read(imageRID);
						u32            compressedSize = imageObject.GetUInt(TextureImageResource::DataSize);
						u32            uncompressedSize = imageObject.GetUInt(TextureImageResource::UncompressedSize);
						SK_ASSERT(offset + uncompressedSize <= totalUncompressedSize, "something went wrong");

						buffer.CopyData(byteBuffer.begin(), compressedSize);

						Compression::Decompress(static_cast<u8*>(mem) + offset, uncompressedSize, byteBuffer.begin(), compressedSize, compressionMode);
						offset += uncompressedSize;
					}
				}

				GPUCommandBuffer* cmd = Graphics::GetFreeCommandBuffer();
				cmd->Begin();
				cmd->ResourceBarrier(textureStorage->texture, ResourceState::Undefined, ResourceState::CopyDest, 0, mipLevels, 0, 1);

				u32 offset = 0;
				for (RID imageRID : images)
				{
					ResourceObject imageObject = Resources::Read(imageRID);

					Vec2 extent = imageObject.GetVec2(TextureImageResource::Extent);
					u32  mip = imageObject.GetUInt(TextureImageResource::Mip);
					cmd->CopyBufferToTexture(gpuBuffer, textureStorage->texture, Extent3D{static_cast<u32>(extent.width), static_cast<u32>(extent.height), 1}, mip, 0, offset);
					offset += imageObject.GetUInt(TextureImageResource::UncompressedSize);
				}

				cmd->ResourceBarrier(textureStorage->texture, ResourceState::CopyDest, ResourceState::ShaderReadOnly, 0, mipLevels, 0, 1);

				cmd->End();
				Graphics::SubmitGPUWork(cmd, true);
				Graphics::AddFreeCommandBuffer(cmd);

				gpuBuffer->Destroy();

				textureStorage->textureIndex = nextTextureIndex++;

				DescriptorUpdate texUpdate;
				texUpdate.type = DescriptorType::SampledImage;
				texUpdate.binding = 2;
				texUpdate.arrayElement = textureStorage->textureIndex;
				texUpdate.texture = textureStorage->texture;
				globalDescriptorSet->Update(texUpdate);
			}
			it = textureCache.Emplace(texture, Traits::Move(textureStorage)).first;
		}
		return it->second.get();
	}

	MaterialResourceCache* RenderResourceCache::GetMaterialCache(RID material)
	{
		std::unique_lock lock(materialCacheMutex);

		auto it = materialCache.Find(material);
		if (it == materialCache.end())
		{
			std::shared_ptr<MaterialResourceCache> materialData = std::make_shared<MaterialResourceCache>(material);
			Resources::GetStorage(material)->RegisterEvent(ResourceEventType::Changed, GraphicsResourceStorageMaterialReload, materialData.get());

			ResourceObject materialObject = Resources::Read(material);
			UpdateMaterialStorageData(materialObject, materialData.get());
			it = materialCache.Emplace(material, Traits::Move(materialData)).first;
		}

		return it->second.get();
	}

	MeshResourceCache* RenderResourceCache::GetMeshCache(RID mesh)
	{
		std::unique_lock lock(meshCacheMutex);

		auto it = meshCache.Find(mesh);
		if (it == meshCache.end())
		{
			std::shared_ptr<MeshResourceCache> meshData = std::make_shared<MeshResourceCache>(mesh);

			if (ResourceObject meshObject = Resources::Read(mesh))
			{
				StringView name = meshObject.GetString(MeshResource::Name);
				meshData->aabb = AABB{meshObject.GetVec3(MeshResource::AABBMin), meshObject.GetVec3(MeshResource::AABBMax)};
				meshData->skin = GetSkinCache(meshObject.GetSubObject(MeshResource::Skin));
				meshData->lightmapSizeHint = meshObject.GetVec2(MeshResource::LightmapSizeHint);

				// Vertex layout attribute offsets (U32_MAX = not present)
				u32 positionOffset = U32_MAX;
				u32 normalOffset   = U32_MAX;
				u32 uvOffset       = U32_MAX;
				u32 uv1Offset      = U32_MAX;
				u32 colorOffset    = U32_MAX;
				u32 tangentOffset  = U32_MAX;

				RID vertexLayoutRID = meshObject.GetSubObject(MeshResource::VertexLayout);
				if (vertexLayoutRID)
				{
					ResourceObject layoutObject = Resources::Read(vertexLayoutRID);
					if (layoutObject)
					{
						meshData->stride = layoutObject.GetUInt(VertexLayoutResource::Stride);

						Span<RID> attrs = layoutObject.GetSubObjectList(VertexLayoutResource::Attributes);
						for (RID attrRID : attrs)
						{
							ResourceObject attrObj = Resources::Read(attrRID);
							if (attrObj)
							{
								StringView attrName = attrObj.GetString(VertexAttributeResource::Name);
								u32 attrOffset = attrObj.GetUInt(VertexAttributeResource::Offset);

								if (attrName == "position") positionOffset = attrOffset;
								else if (attrName == "normal")   normalOffset = attrOffset;
								else if (attrName == "uv0")      uvOffset = attrOffset;
								else if (attrName == "uv1")    { uv1Offset = attrOffset; meshData->hasUV1 = true; }
								else if (attrName == "color")  { colorOffset = attrOffset; meshData->hasColor = true; }
								else if (attrName == "tangent")  tangentOffset = attrOffset;
							}
						}
					}
				}
				Span<RID> materials = meshObject.GetReferenceArray(MeshResource::Materials);

				ResourceBuffer buffer = meshObject.GetBuffer(MeshResource::MeshData);
				Span<RID>      lods = meshObject.GetSubObjectList(MeshResource::MeshLODs);

				ResourceObject meshLodObject = Resources::Read(lods[0]);

				u64 vertexBufferOffset = meshLodObject.GetUInt(MeshLodResource::VerticesOffset);
				u64 vertexBufferSize = meshLodObject.GetUInt(MeshLodResource::VerticesCount) * static_cast<u64>(meshData->stride);
				u32 indexBufferOffset = meshLodObject.GetUInt(MeshLodResource::IndicesOffset);
				u32 indexBufferSize = meshLodObject.GetUInt(MeshLodResource::IndicesCount) * sizeof(u32);
				u64 primitiveOffset = meshLodObject.GetUInt(MeshLodResource::PrimitiveOffset);
				u64 primitiveCount = meshLodObject.GetUInt(MeshLodResource::PrimitiveCount);
				u64 primitiveSize = primitiveCount * sizeof(MeshPrimitive);

				bool          rtSupported = Graphics::GetDevice()->GetFeatures().rayTracing;
				ResourceUsage rtUsage = rtSupported ? ResourceUsage::AccelerationStructure | ResourceUsage::ShaderResource : ResourceUsage::None;

				meshData->vertexBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = vertexBufferSize,
					.usage = ResourceUsage::CopyDest | ResourceUsage::VertexBuffer | rtUsage,
					.hostVisible = false,
					.persistentMapped = false,
					.debugName = String(name) + "_VertexBuffer"
				});

				meshData->indexBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = indexBufferSize,
					.usage = ResourceUsage::CopyDest | ResourceUsage::IndexBuffer | rtUsage,
					.hostVisible = false,
					.persistentMapped = false,
					.debugName = String(name) + "_IndexBuffer"
				});

				GPUBuffer* stagingVertexBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = vertexBufferSize,
					.usage = ResourceUsage::CopySource,
					.hostVisible = true,
					.persistentMapped = true,
				});

				GPUBuffer* stagingIndexBuffer = Graphics::CreateBuffer(BufferDesc{
					.size = indexBufferSize,
					.usage = ResourceUsage::CopySource,
					.hostVisible = true,
					.persistentMapped = true,
				});

				buffer.CopyData(stagingVertexBuffer->GetMappedData(), vertexBufferSize, vertexBufferOffset);
				buffer.CopyData(stagingIndexBuffer->GetMappedData(), indexBufferSize, indexBufferOffset);

				GPUCommandBuffer* cmd = Graphics::GetFreeCommandBuffer();
				cmd->Begin();
				cmd->CopyBuffer(stagingVertexBuffer, meshData->vertexBuffer, vertexBufferSize, 0, 0);
				cmd->CopyBuffer(stagingIndexBuffer, meshData->indexBuffer, indexBufferSize, 0, 0);
				cmd->End();
				Graphics::SubmitGPUWork(cmd, true);
				Graphics::AddFreeCommandBuffer(cmd);

				stagingVertexBuffer->Destroy();
				stagingIndexBuffer->Destroy();

				meshData->primitives.Resize(primitiveCount);
				buffer.CopyData(meshData->primitives.Data(), primitiveSize, primitiveOffset);

				if (rtSupported && !meshData->skin)
				{
					u32 vertexCount = static_cast<u32>(meshLodObject.GetUInt(MeshLodResource::VerticesCount));

					meshData->geometryIndex = nextGeometryIndex++;

					// Create vertex layout offset buffer
					struct VertexLayoutOffsetData
					{
						u32 stride;
						u32 posOff;
						u32 normalOff;
						u32 uvOff;
						u32 uv1Off;
						u32 colorOff;
						u32 tangentOff;
						u32 pad;
					};

					VertexLayoutOffsetData layoutData{};
					layoutData.stride    = meshData->stride;
					layoutData.posOff    = positionOffset;
					layoutData.normalOff = normalOffset;
					layoutData.uvOff     = uvOffset;
					layoutData.uv1Off    = uv1Offset;
					layoutData.colorOff  = colorOffset;
					layoutData.tangentOff = tangentOffset;
					layoutData.pad       = 0;

					meshData->vertexLayoutBuffer = Graphics::CreateBuffer(BufferDesc{
						.size = sizeof(VertexLayoutOffsetData),
						.usage = ResourceUsage::ConstantBuffer,
						.hostVisible = true,
						.persistentMapped = true,
						.debugName = String(name) + "_VertexLayout"
					});
					memcpy(meshData->vertexLayoutBuffer->GetMappedData(), &layoutData, sizeof(layoutData));

					DescriptorUpdate vtxUpdate;
					vtxUpdate.type = DescriptorType::StorageBuffer;
					vtxUpdate.binding = 3;
					vtxUpdate.arrayElement = meshData->geometryIndex;
					vtxUpdate.buffer = meshData->vertexBuffer;
					globalDescriptorSet->Update(vtxUpdate);

					DescriptorUpdate idxUpdate;
					idxUpdate.type = DescriptorType::StorageBuffer;
					idxUpdate.binding = 4;
					idxUpdate.arrayElement = meshData->geometryIndex;
					idxUpdate.buffer = meshData->indexBuffer;
					globalDescriptorSet->Update(idxUpdate);

					DescriptorUpdate layoutUpdate;
					layoutUpdate.type = DescriptorType::UniformBuffer;
					layoutUpdate.binding = 5;
					layoutUpdate.arrayElement = meshData->geometryIndex;
					layoutUpdate.buffer = meshData->vertexLayoutBuffer;
					globalDescriptorSet->Update(layoutUpdate);

					meshData->blasArray.Resize(primitiveCount, nullptr);

					for (u32 p = 0; p < primitiveCount; ++p)
					{
						const MeshPrimitive& prim = meshData->primitives[p];

						GeometryDesc geometry{};
						geometry.type = GeometryType::Triangles;
						geometry.triangles.vertexBuffer = meshData->vertexBuffer;
						geometry.triangles.vertexCount = vertexCount;
						geometry.triangles.vertexStride = meshData->stride;
						geometry.triangles.vertexFormat = TextureFormat::R32G32B32_FLOAT;
						geometry.triangles.indexBuffer = meshData->indexBuffer;
						geometry.triangles.indexOffset = prim.firstIndex * sizeof(u32);
						geometry.triangles.indexCount = prim.indexCount;
						geometry.triangles.indexType = IndexType::Uint32;
						geometry.triangles.opaque = true;

						BottomLevelASDesc blasDesc{};
						blasDesc.geometries = Span<GeometryDesc>(&geometry, 1);
						blasDesc.flags = BuildAccelerationStructureFlags::PreferFastTrace;
						blasDesc.debugName = String(name) + "_BLAS_" + ToString(p);

						meshData->blasArray[p] = Graphics::CreateBottomLevelAS(blasDesc);

						GPUBuffer* scratchBuffer = Graphics::CreateBuffer(BufferDesc{
							.size = Graphics::GetAccelerationStructureBuildScratchSize(blasDesc),
							.usage = ResourceUsage::ShaderResource,
							.hostVisible = false,
							.persistentMapped = false,
							.debugName = String(name) + "_BLASScratch_" + ToString(p)
						});

						GPUCommandBuffer* blasCmd = Graphics::GetFreeCommandBuffer();
						blasCmd->Begin();
						blasCmd->BuildBottomLevelAS(meshData->blasArray[p], AccelerationStructureBuildInfo{.scratchBuffer = scratchBuffer});
						blasCmd->End();
						Graphics::SubmitGPUWork(blasCmd, true);
						Graphics::AddFreeCommandBuffer(blasCmd);

						scratchBuffer->Destroy();
					}
				}

				if (!materials.Empty())
				{
					meshData->materials.Reserve(materials.Size());
					for (usize i = 0; i < materials.Size(); i++)
					{
						meshData->materials.EmplaceBack(GetMaterialCache(materials[i]));
					}
				}
				else
				{
					if (!defaultMaterial)
					{
						defaultMaterial = Resources::FindByPath("Skore://Materials/DefaultMaterial.material");
					}
					meshData->materials.EmplaceBack(GetMaterialCache(defaultMaterial));
				}
			}

			it = meshCache.Emplace(mesh, Traits::Move(meshData)).first;
		}
		return it->second.get();
	}

	GPUDescriptorSet* RenderResourceCache::GetGlobalDescriptorSet()
	{
		return globalDescriptorSet;
	}

	GPUBuffer* RenderResourceCache::GetMaterialDataBuffer()
	{
		return materialDataBuffer;
	}

	u32 RenderResourceCache::GetMaterialDataCount()
	{
		return materialDataCount;
	}

	SkinResourceCache* RenderResourceCache::GetSkinCache(RID cache)
	{
		if (!cache) return nullptr;
		std::unique_lock lock(skinCacheMutex);
		auto             it = skinCache.Find(cache);
		if (it == skinCache.end())
		{
			std::shared_ptr<SkinResourceCache> skinCacheData = std::make_shared<SkinResourceCache>(cache);

			if (ResourceObject skinObject = Resources::Read(cache))
			{
				skinCacheData->poses.Reserve(skinObject.GetSubObjectListCount(SkinResource::Binds));

				for (RID bind : skinObject.GetSubObjectList(SkinResource::Binds))
				{
					ResourceObject bindObject = Resources::Read(bind);
					skinCacheData->poses.EmplaceBack(bindObject.GetMat4(SkinBindResource::Pose));
				}
			}
			it = skinCache.Insert(cache, Traits::Move(skinCacheData)).first;
		}
		return it->second.get();
	}

	void RenderResourceCacheInit()
	{
		globalDescriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
			.bindings = {
				DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = DescriptorType::StorageBuffer
				},
				DescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = DescriptorType::Sampler
				},
				DescriptorSetLayoutBinding{
					.binding = 2,
					.descriptorType = DescriptorType::SampledImage,
					.renderType = RenderType::RuntimeArray
				},
				DescriptorSetLayoutBinding{
					.binding = 3,
					.descriptorType = DescriptorType::StorageBuffer,
					.renderType = RenderType::RuntimeArray
				},
				DescriptorSetLayoutBinding{
					.binding = 4,
					.descriptorType = DescriptorType::StorageBuffer,
					.renderType = RenderType::RuntimeArray
				},
				DescriptorSetLayoutBinding{
					.binding = 5,
					.descriptorType = DescriptorType::UniformBuffer,
					.renderType = RenderType::RuntimeArray
				},
			},
			.debugName = "GlobalDescriptorSet"
		});

		globalDescriptorSet->UpdateSampler(1, Graphics::GetLinearSampler());
	}

	void RenderResourceCacheShutdown()
	{
		if (globalDescriptorSet)
		{
			globalDescriptorSet->Destroy();
			globalDescriptorSet = nullptr;
		}
		if (materialDataBuffer)
		{
			materialDataBuffer->Destroy();
			materialDataBuffer = nullptr;
		}
		nextGeometryIndex = 0;
		nextMaterialIndex = 0;
		nextTextureIndex = 0;
		materialDataBufferCapacity = 0;
		materialDataCount = 0;

		for (auto& it : textureCache)
		{
			it.second->texture->Destroy();
		}

		for (auto& it : fontCache)
		{
			it.second->texture->Destroy();
		}
		fontCache.Clear();

		for (auto& it : materialCache)
		{
			if (it.second->descriptorSet)
			{
				it.second->descriptorSet->Destroy();
			}
			if (it.second->materialBuffer)
			{
				it.second->materialBuffer->Destroy();
			}

			if (it.second->diffuseIrradianceTexture)
			{
				it.second->diffuseIrradianceTexture->Destroy();
			}

			if (it.second->specularMapTexture)
			{
				it.second->specularMapTexture->Destroy();
			}
		}

		for (auto& it : meshCache)
		{
			for (GPUBottomLevelAS* blas : it.second->blasArray)
			{
				if (blas)
				{
					blas->Destroy();
				}
			}

			if (it.second->indexBuffer)
			{
				it.second->indexBuffer->Destroy();
			}

			if (it.second->vertexBuffer)
			{
				it.second->vertexBuffer->Destroy();
			}

			if (it.second->vertexLayoutBuffer)
			{
				it.second->vertexLayoutBuffer->Destroy();
			}
		}
		meshCache.Clear();
	}
}