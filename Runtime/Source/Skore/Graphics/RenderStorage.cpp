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

#include "RenderStorage.hpp"

#include "Graphics.hpp"
#include "Skore/Resource/Resources.hpp"

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

	namespace
	{
		HashMap<RID, std::shared_ptr<MaterialStorageData>> materialCache;
		HashMap<RID, std::shared_ptr<MeshStorageData>>     meshCache;
		HashMap<RID, std::shared_ptr<TextureStorageData>>  textureCache;

		RID defaultMaterial;


		TextureStorageData* GetOrLoadTexture(RID texture)
		{
			auto it = textureCache.Find(texture);
			if (it == textureCache.end())
			{
				std::shared_ptr<TextureStorageData> textureStorage = std::make_shared<TextureStorageData>();

				if (ResourceObject textureObject = Resources::Read(texture))
				{
					Span<u8> textureData = textureObject.GetBlob(TextureResource::Pixels);

					if (!textureData.Empty())
					{
						StringView    name = textureObject.GetString(TextureResource::Name);
						TextureFormat format = textureObject.GetEnum<TextureFormat>(TextureResource::Format);
						Vec3          extent = textureObject.GetVec3(TextureResource::Extent);

						textureStorage->texture = Graphics::CreateTexture(TextureDesc{
							.extent = {static_cast<u32>(extent.x), static_cast<u32>(extent.y), static_cast<u32>(extent.z)},
							.format = format,
							.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
							.debugName = String(name) + "_Texture"
						});

						TextureDataInfo textureDataInfo{
							.texture = textureStorage->texture,
							.data = textureData.Data(),
							.size = textureData.Size()
						};

						Graphics::UploadTextureData(textureDataInfo);
					}
				}
				it = textureCache.Emplace(texture, Traits::Move(textureStorage)).first;
			}
			return it->second.get();
		}


		bool UpdateTexture(GPUDescriptorSet* descriptorSet, RID texture, u32 slot)
		{
			if (texture)
			{
				TextureStorageData* data = GetOrLoadTexture(texture);
				if (data->texture)
				{
					descriptorSet->UpdateTexture(slot, data->texture);
					return true;
				}
			}

			descriptorSet->UpdateTexture(slot, Graphics::GetWhiteTexture());
			return false;
		}


		MaterialStorageData* GetOrLoadMaterial(RID material)
		{
			auto it = materialCache.Find(material);
			if (it == materialCache.end())
			{
				std::shared_ptr<MaterialStorageData> materialData = std::make_shared<MaterialStorageData>();

				ResourceObject materialObject = Resources::Read(material);
				StringView     name = materialObject.GetString(MaterialResource::Name);

				materialData->type = materialObject.GetEnum<MaterialResource::MaterialType>(MaterialResource::Type);

				if (materialData->type == MaterialResource::MaterialType::Opaque)
				{
					Color baseColor = materialObject.GetColor(MaterialResource::BaseColor);
					RID   baseColorTexture = materialObject.GetReference(MaterialResource::BaseColorTexture);
					RID   normalTexture = materialObject.GetReference(MaterialResource::NormalTexture);
					RID   roughnessTexture = materialObject.GetReference(MaterialResource::RoughnessTexture);
					RID   metallicTexture = materialObject.GetReference(MaterialResource::MetallicTexture);

					u8 roughnessTextureChannel = materialObject.GetUInt(MaterialResource::RoughnessTextureChannel);
					u8 metallicTextureChannel = materialObject.GetUInt(MaterialResource::MetallicTextureChannel);
					u8 occlusionTextureChannel = materialObject.GetUInt(MaterialResource::OcclusionTextureChannel);


					if (materialData->materialBuffer == nullptr)
					{
						materialData->materialBuffer = Graphics::CreateBuffer(BufferDesc{
							.size = sizeof(MaterialResource::Buffer),
							.usage = ResourceUsage::CopyDest | ResourceUsage::ConstantBuffer,
							.hostVisible = false,
							.persistentMapped = false,
							.debugName = String(name) + "_MaterialBuffer"
						});
					}

					materialData->descriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
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
						.debugName = String(name) + "_DescriptorSet"
					});

					materialData->descriptorSet->UpdateBuffer(0, materialData->materialBuffer, 0, 0);
					materialData->descriptorSet->UpdateSampler(1, Graphics::GetLinearSampler());

					MaterialResource::Buffer materialBuffer;
					materialBuffer.baseColor = baseColor.ToVec3();
					materialBuffer.alphaCutoff = 0.5;
					materialBuffer.metallic = 0.0;
					materialBuffer.roughness = 1.0;
					materialBuffer.textureFlags = TextureAssetFlags::None;

					materialBuffer.textureProps = 0;
					materialBuffer.textureProps |= roughnessTextureChannel << 0;
					materialBuffer.textureProps |= metallicTextureChannel << 8;
					materialBuffer.textureProps |= occlusionTextureChannel << 16;

					if (UpdateTexture(materialData->descriptorSet, baseColorTexture, 2))
					{
						materialBuffer.textureFlags |= TextureAssetFlags::HasBaseColorTexture;
					}

					if (UpdateTexture(materialData->descriptorSet, normalTexture, 3))
					{
						materialBuffer.textureFlags |= TextureAssetFlags::HasNormalTexture;
					}

					if (UpdateTexture(materialData->descriptorSet, roughnessTexture, 4))
					{
						materialBuffer.textureFlags |= TextureAssetFlags::HasRoughnessTexture;
					}

					if (UpdateTexture(materialData->descriptorSet, metallicTexture, 5))
					{
						materialBuffer.textureFlags |= TextureAssetFlags::HasMetallicTexture;
					}

					Graphics::UploadBufferData(BufferUploadInfo{
						.buffer = materialData->materialBuffer,
						.data = &materialBuffer,
						.size = sizeof(MaterialResource::Buffer)
					});
				}
				else if (materialData->type == MaterialResource::MaterialType::SkyboxEquirectangular)
				{
					RID sphericalTexture = materialObject.GetReference(MaterialResource::SphericalTexture);

					materialData->descriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
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
					materialData->skyMaterialTexture = GetOrLoadTexture(sphericalTexture);

					UpdateTexture(materialData->descriptorSet, sphericalTexture, 0);
					materialData->descriptorSet->UpdateSampler(1, Graphics::GetLinearSampler());
				}

				it = materialCache.Emplace(material, Traits::Move(materialData)).first;
			}

			return it->second.get();
		}

		MeshStorageData* GetOrLoadMesh(RID mesh)
		{
			auto it = meshCache.Find(mesh);
			if (it == meshCache.end())
			{
				std::shared_ptr<MeshStorageData> meshData = std::make_shared<MeshStorageData>();

				if (ResourceObject meshObject = Resources::Read(mesh))
				{
					StringView name = meshObject.GetString(MeshResource::Name);
					Span<RID>  materials = meshObject.GetReferenceArray(MeshResource::Materials);
					Span<u8>   vertices = meshObject.GetBlob(MeshResource::Vertices);
					Span<u8>   indices = meshObject.GetBlob(MeshResource::Indices);
					Span<u8>   primitives = meshObject.GetBlob(MeshResource::Primitives);


					meshData->vertexBuffer = Graphics::CreateBuffer(BufferDesc{
						.size = vertices.Size(),
						.usage = ResourceUsage::CopyDest | ResourceUsage::VertexBuffer,
						.hostVisible = false,
						.persistentMapped = false,
						.debugName = String(name) + "_VertexBuffer"
					});

					Graphics::UploadBufferData(BufferUploadInfo{
						.buffer = meshData->vertexBuffer,
						.data = vertices.Data(),
						.size = vertices.Size()
					});

					meshData->indexBuffer = Graphics::CreateBuffer(BufferDesc{
						.size = indices.Size(),
						.usage = ResourceUsage::CopyDest | ResourceUsage::IndexBuffer,
						.hostVisible = false,
						.persistentMapped = false,
						.debugName = String(name) + "_IndexBuffer"
					});

					Graphics::UploadBufferData(BufferUploadInfo{
						.buffer = meshData->indexBuffer,
						.data = indices.Data(),
						.size = indices.Size()
					});

					meshData->primitives.Resize(primitives.Size() / sizeof(MeshPrimitive));
					memcpy(meshData->primitives.Data(), primitives.Data(), primitives.Size());


					if (!materials.Empty())
					{
						meshData->materials.Reserve(materials.Size());
						for (usize i = 0; i < materials.Size(); i++)
						{
							meshData->materials.EmplaceBack(GetOrLoadMaterial(materials[i])->descriptorSet);
						}
					}
					else
					{
						if (!defaultMaterial)
						{
							defaultMaterial = Resources::FindByPath("Skore://Materials/DefaultMaterial.material");
						}
						meshData->materials.EmplaceBack(GetOrLoadMaterial(defaultMaterial)->descriptorSet);
					}
				}

				it = meshCache.Emplace(mesh, Traits::Move(meshData)).first;
			}
			return it->second.get();
		}
	}

	void ResourceStorageShutdown()
	{
		for (auto& it : textureCache)
		{
			it.second->texture->Destroy();
		}

		for (auto& it : materialCache)
		{
			it.second->descriptorSet->Destroy();
			if (it.second->materialBuffer)
			{
				it.second->materialBuffer->Destroy();
			}
		}

		for (auto& it : meshCache)
		{
			if (it.second->indexBuffer)
			{
				it.second->indexBuffer->Destroy();
			}

			if (it.second->vertexBuffer)
			{
				it.second->vertexBuffer->Destroy();
			}
		}
		meshCache.Clear();
	}


	GPUDescriptorSet* MeshRenderData::GetMaterial(u32 index) const
	{
		if (!mesh) return nullptr;

		if (overrideMaterials.Size() > index)
		{
			return overrideMaterials[index];
		}

		if (mesh->materials.Size() > index)
		{
			return mesh->materials[index];
		}

		return nullptr;
	}

	void RenderStorage::RegisterStaticMeshProxy(VoidPtr owner, u64 id)
	{
		staticMeshes.emplace(owner, MeshRenderData{
			                     .id = id,
			                     .mesh = {},
			                     .transform = {},
			                     .visible = true,
		                     });
	}


	void RenderStorage::RemoveStaticMeshProxy(VoidPtr owner)
	{
		staticMeshes.erase(owner);
	}

	void RenderStorage::SetStaticMeshTransform(VoidPtr owner, const Mat4& worldTransform)
	{
		if (const auto& it = staticMeshes.find(owner); it != staticMeshes.end())
		{
			it->second.transform = worldTransform;
		}
	}

	void RenderStorage::SetStaticMesh(VoidPtr owner, RID meshAsset)
	{
		if (const auto& it = staticMeshes.find(owner); it != staticMeshes.end())
		{
			it->second.mesh = GetOrLoadMesh(meshAsset);
		}
	}

	void RenderStorage::SetStaticMeshVisible(VoidPtr owner, bool visible)
	{
		if (const auto& it = staticMeshes.find(owner); it != staticMeshes.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::SetStaticMeshMaterials(VoidPtr owner, Span<RID> materials)
	{
		if (const auto& it = staticMeshes.find(owner); it != staticMeshes.end())
		{
			it->second.overrideMaterials.Clear();
			it->second.overrideMaterials.Resize(materials.Size());
			for (usize i = 0; i < materials.Size(); i++)
			{
				if (materials[i])
				{
					it->second.overrideMaterials[i] = GetOrLoadMaterial(materials[i])->descriptorSet;
				}
			}
		}
	}

	void RenderStorage::SetStaticMeshCastShadows(VoidPtr owner, bool castShadows)
	{
		if (const auto& it = staticMeshes.find(owner); it != staticMeshes.end())
		{
			it->second.castShadows = castShadows;
		}
	}

	void RenderStorage::RegisterSkinnedMeshProxy(VoidPtr owner)
	{
		GPUBuffer* buffer = Graphics::CreateBuffer(BufferDesc{
			.size = sizeof(BonesRenderData),
			.usage = ResourceUsage::CopyDest | ResourceUsage::ConstantBuffer,
			.hostVisible = true,
			.persistentMapped = true,
		});

		BonesRenderData& data = *static_cast<BonesRenderData*>(buffer->GetMappedData());

		for (int i = 0; i < 128; ++i)
		{
			data.boneMatrices[i].Identity();
		}

		GPUDescriptorSet* descriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
			.bindings = {
				DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = DescriptorType::UniformBuffer
				}
			}
		});

		descriptorSet->UpdateBuffer(0, buffer, 0, sizeof(BonesRenderData));

		skinnedMeshes.emplace(owner, MeshRenderData{
			                      .mesh = {},
			                      .transform = {},
			                      .visible = true,
			                      .bonesBuffer = buffer,
			                      .bonesDescriptorSet = descriptorSet
		                      });
	}

	void RenderStorage::RemoveSkinnedMeshProxy(VoidPtr owner)
	{
		if (const auto& it = skinnedMeshes.find(owner); it != skinnedMeshes.end())
		{
			it->second.bonesDescriptorSet->Destroy();
			it->second.bonesBuffer->Destroy();

			skinnedMeshes.erase(it);
		}

	}

	void RenderStorage::SetSkinnedMeshTransform(VoidPtr owner, const Mat4& worldTransform)
	{
		if (const auto& it = skinnedMeshes.find(owner); it != skinnedMeshes.end())
		{
			it->second.transform = worldTransform;
		}
	}

	void RenderStorage::SetSkinnedMesh(VoidPtr owner, RID meshAsset)
	{
		if (const auto& it = skinnedMeshes.find(owner); it != skinnedMeshes.end())
		{
			it->second.mesh = GetOrLoadMesh(meshAsset);
		}
	}

	void RenderStorage::SetSkinnedMeshVisible(VoidPtr owner, bool visible)
	{
		if (const auto& it = skinnedMeshes.find(owner); it != skinnedMeshes.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::SetSkinnedMeshMaterials(VoidPtr owner, Span<RID> materials)
	{
		if (const auto& it = skinnedMeshes.find(owner); it != skinnedMeshes.end())
		{
			it->second.overrideMaterials.Clear();
			it->second.overrideMaterials.Resize(materials.Size());
			for (usize i = 0; i < materials.Size(); i++)
			{
				if (materials[i])
				{
					it->second.overrideMaterials[i] = GetOrLoadMaterial(materials[i])->descriptorSet;
				}
			}
		}
	}

	void RenderStorage::SetSkinnedMeshCastShadows(VoidPtr owner, bool castShadows)
	{
		if (const auto& it = skinnedMeshes.find(owner); it != skinnedMeshes.end())
		{
			it->second.castShadows = castShadows;
		}
	}

	void RenderStorage::RegisterEnvironmentProxy(VoidPtr owner)
	{
		environments.emplace(owner, EnvironmentRenderData{
			                     .skyboxMaterial = {}
		                     });
	}

	void RenderStorage::RemoveEnvironmentProxy(VoidPtr owner)
	{
		environments.erase(owner);
	}

	void RenderStorage::SetEnvironmentSkyboxMaterial(VoidPtr owner, RID material)
	{
		if (const auto& it = environments.find(owner); it != environments.end())
		{
			it->second.skyboxMaterial = GetOrLoadMaterial(material);
		}
	}

	void RenderStorage::SetEnvironmentVisible(VoidPtr owner, bool visible)
	{
		if (const auto& it = environments.find(owner); it != environments.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::RegisterLightProxy(VoidPtr owner, u64 id)
	{
		lights.emplace(owner, LightRenderData{
			               .id = id,
			               .type = LightType::Directional,
			               .transform = {},
			               .color = Color::WHITE,
			               .intensity = 1.0f,
			               .range = 100.0f,
			               .innerConeAngle = Math::Radians(30.0f),
			               .outerConeAngle = Math::Radians(45.0f),
			               .visible = true
		               });
	}

	void RenderStorage::RemoveLightProxy(VoidPtr owner)
	{
		lights.erase(owner);
	}

	void RenderStorage::SetLightTransform(VoidPtr owner, const Mat4& worldTransform)
	{
		if (const auto& it = lights.find(owner); it != lights.end())
		{
			it->second.transform = worldTransform;
		}
	}

	void RenderStorage::SetLightType(VoidPtr owner, LightType type)
	{
		if (const auto& it = lights.find(owner); it != lights.end())
		{
			it->second.type = type;
		}
	}

	void RenderStorage::SetLightColor(VoidPtr owner, const Color& color)
	{
		if (const auto& it = lights.find(owner); it != lights.end())
		{
			it->second.color = color;
		}
	}

	void RenderStorage::SetLightIntensity(VoidPtr owner, f32 intensity)
	{
		if (const auto& it = lights.find(owner); it != lights.end())
		{
			it->second.intensity = intensity;
		}
	}

	void RenderStorage::SetLightRange(VoidPtr owner, f32 range)
	{
		if (const auto& it = lights.find(owner); it != lights.end())
		{
			it->second.range = range;
		}
	}

	void RenderStorage::SetLightInnerConeAngle(VoidPtr owner, f32 angle)
	{
		if (const auto& it = lights.find(owner); it != lights.end())
		{
			it->second.innerConeAngle = Math::Radians(-angle);
		}
	}

	void RenderStorage::SetLightOuterConeAngle(VoidPtr owner, f32 angle)
	{
		if (const auto& it = lights.find(owner); it != lights.end())
		{
			it->second.outerConeAngle = Math::Radians(-angle);
		}
	}

	void RenderStorage::SetLightVisible(VoidPtr owner, bool visible)
	{
		if (const auto& it = lights.find(owner); it != lights.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::SetLightEnableShadows(VoidPtr owner, bool enableShadows)
	{
		if (const auto& it = lights.find(owner); it != lights.end())
		{
			it->second.enableShadows = enableShadows;
		}
	}

	void RenderStorage::RegisterCamera(VoidPtr owner, u64 id)
	{
		cameras.emplace(owner, CameraRenderData{
			                .id = id,
		                });
	}

	void RenderStorage::SetCameraViewMatrix(VoidPtr owner, const Mat4& viewMatrix)
	{
		if (const auto& it = cameras.find(owner); it != cameras.end())
		{
			it->second.viewMatrix = viewMatrix;
		}
	}

	void RenderStorage::SetCameraPosition(VoidPtr owner, const Vec3& position)
	{
		if (const auto& it = cameras.find(owner); it != cameras.end())
		{
			it->second.position = position;
		}
	}

	void RenderStorage::SetCameraProjection(VoidPtr owner, Camera::Projection projection)
	{
		if (const auto& it = cameras.find(owner); it != cameras.end())
		{
			it->second.projection = projection;
		}
	}

	void RenderStorage::SetCameraFov(VoidPtr owner, f32 fov)
	{
		if (const auto& it = cameras.find(owner); it != cameras.end())
		{
			it->second.fov = fov;
		}
	}
	void RenderStorage::SetCameraNear(VoidPtr owner, f32 near)
	{
		if (const auto& it = cameras.find(owner); it != cameras.end())
		{
			it->second.nearPlane = near;
		}
	}
	void RenderStorage::SetCameraFar(VoidPtr owner, f32 far)
	{
		if (const auto& it = cameras.find(owner); it != cameras.end())
		{
			it->second.farPlane = far;
		}
	}

	void RenderStorage::SetCameraVisible(VoidPtr owner, bool visible)
	{
		if (const auto& it = cameras.find(owner); it != cameras.end())
		{
			it->second.visible = visible;
		}
	}

	void RenderStorage::RemoveCamera(VoidPtr owner)
	{
		cameras.erase(owner);
	}

	std::optional<CameraRenderData> RenderStorage::GetCurrentCamera()
	{
		for (auto& it : cameras)
		{
			if (it.second.visible)
			{
				return std::make_optional(it.second);
			}
		}
		return std::nullopt;
	}
}
