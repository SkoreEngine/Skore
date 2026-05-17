#include "Skore/Graphics/RenderSceneObjects.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{

	void RenderSceneObjectsInit()
	{
	}

	void RenderSceneObjectsShutdown()
	{
	}

	void DrawableObject::SetTransform(const Mat4& pTransform)
	{
		transform = pTransform;
		UpdateAABB();

		for (const auto& ref : opaqueDrawcallRefs)
		{
			renderSceneObjects->opaquePipelines[ref.pipelineIndex].drawcalls[ref.handle].transform = transform;
		}
		for (const auto& ref : transparentDrawcallRefs)
		{
			renderSceneObjects->transparentPipelines[ref.pipelineIndex].drawcalls[ref.handle].transform = transform;
		}
		for (const auto& ref : shadowDrawcallRefs)
		{
			renderSceneObjects->shadowPipelines[ref.pipelineIndex].drawcalls[ref.handle].transform = transform;
		}

		for (const InstanceSlot& slot : instanceSlots)
		{
			if (slot.descIndex != U32_MAX)
			{
				renderSceneObjects->instances[slot.descIndex].transform = transform;
				renderSceneObjects->tlasDirty = true;
			}
		}
	}

	Mat4 DrawableObject::GetTransform() const
	{
		return transform;
	}

	void DrawableObject::SetMesh(RID pMesh)
	{
		mesh = pMesh;
		MarkDirty();
	}

	RID DrawableObject::GetMesh() const
	{
		return mesh;
	}

	void DrawableObject::SetUUID(UUID pUuid)
	{
		uuid = pUuid;
	}

	UUID DrawableObject::GetUUID() const
	{
		return uuid;
	}

	void DrawableObject::SetMaterials(Span<RID> pMaterials)
	{
		if (materials != pMaterials)
		{
			materialsDirty = true;
			MarkDirty();
		}
		materials = pMaterials;
	}

	Span<RID> DrawableObject::GetMaterial()
	{
		return materials;
	}

	void DrawableObject::SetCastShadows(bool pCastShadows)
	{
		castShadows = pCastShadows;
		MarkDirty();
	}

	bool DrawableObject::GetCastShadows() const
	{
		return castShadows;
	}

	void DrawableObject::SetUserData(u64 pUserData)
	{
		userData = pUserData;
		MarkDirty();
	}

	u64 DrawableObject::GetUserData() const
	{
		return userData;
	}

	void DrawableObject::SetVisible(bool pVisible)
	{
		visible = pVisible;
		MarkDirty();
	}

	bool DrawableObject::GetVisible() const
	{
		return visible;
	}

	void DrawableObject::SetLayerMask(u64 pLayerMask)
	{
		layerMask = pLayerMask;
		for (const auto& ref : opaqueDrawcallRefs)
		{
			renderSceneObjects->opaquePipelines[ref.pipelineIndex].drawcalls[ref.handle].layerMask = layerMask;
		}
		for (const auto& ref : transparentDrawcallRefs)
		{
			renderSceneObjects->transparentPipelines[ref.pipelineIndex].drawcalls[ref.handle].layerMask = layerMask;
		}
		for (const auto& ref : shadowDrawcallRefs)
		{
			renderSceneObjects->shadowPipelines[ref.pipelineIndex].drawcalls[ref.handle].layerMask = layerMask;
		}
	}

	u64 DrawableObject::GetLayerMask() const
	{
		return layerMask;
	}

	void DrawableObject::UpdateSkinData()
	{
		if (bonesBuffer == nullptr)
		{
			bonesBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(Mat4) * MaxBones,
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true
			});

			Mat4* bones = static_cast<Mat4*>(bonesBuffer->GetMappedData());
			for (int i = 0; i < MaxBones; ++i)
			{
				new(bones + i) Mat4{Mat4(1.0)};
			}
		}

		if (bonesDescriptor == nullptr)
		{
			bonesDescriptor = Graphics::CreateDescriptorSet(DescriptorSetDesc{
				.bindings{
					DescriptorSetLayoutBinding{
						.binding = 0,
						.descriptorType = DescriptorType::UniformBuffer,
					}
				}
			});
			bonesDescriptor->UpdateBuffer(0, bonesBuffer, 0, sizeof(Mat4) * MaxBones);
		}
	}

	AABB DrawableObject::GetAABB() const
	{
		return aabb;
	}

	void DrawableObject::Destroy()
	{
		renderSceneObjects->objects.Erase(this);

		if (meshCache)
		{
			meshCache->DecreaseUsage();
		}

		for (MaterialResourceCache* material : overrideMaterialsCache)
		{
			if (material)
			{
				material->DecreaseUsage();
			}
		}

		for (const auto& ref : opaqueDrawcallRefs)
		{
			renderSceneObjects->opaquePipelines[ref.pipelineIndex].drawcalls.Remove(ref.handle);
		}
		for (const auto& ref : transparentDrawcallRefs)
		{
			renderSceneObjects->transparentPipelines[ref.pipelineIndex].drawcalls.Remove(ref.handle);
		}
		for (const auto& ref : shadowDrawcallRefs)
		{
			renderSceneObjects->shadowPipelines[ref.pipelineIndex].drawcalls.Remove(ref.handle);
		}
		opaqueDrawcallRefs.Clear();
		transparentDrawcallRefs.Clear();
		shadowDrawcallRefs.Clear();

		for (u32 p = 0; p < instanceSlots.Size(); ++p)
		{
			renderSceneObjects->RemoveInstance(this, p);
		}

		if (bonesDescriptor) bonesDescriptor->Destroy();
		if (bonesBuffer) bonesBuffer->Destroy();

		DestroyAndFree(this);
	}

	void DrawableObject::MarkDirty()
	{
		if (currentVersion != lastUpdatedVersion)
		{
			renderSceneObjects->pendingUpdate.insert(this);
			lastUpdatedVersion = currentVersion;
		}
	}

	void DrawableObject::UpdateAABB()
	{
		aabb = AABB();
		if (meshCache)
		{
			aabb = Math::TransformAABB(meshCache->aabb, transform);

			if (opaqueDrawcallRefs.Size() == meshCache->primitives.Size())
			{
				for (int i = 0; i < meshCache->primitives.Size(); ++i)
				{
					const auto& ref = opaqueDrawcallRefs[i];
					renderSceneObjects->opaquePipelines[ref.pipelineIndex].drawcalls[ref.handle].aabb =
						Math::TransformAABB(meshCache->primitives[i].aabb, transform);
				}
			}

			if (transparentDrawcallRefs.Size() == meshCache->primitives.Size())
			{
				for (int i = 0; i < meshCache->primitives.Size(); ++i)
				{
					const auto& ref = transparentDrawcallRefs[i];
					renderSceneObjects->transparentPipelines[ref.pipelineIndex].drawcalls[ref.handle].aabb =
						Math::TransformAABB(meshCache->primitives[i].aabb, transform);
				}
			}
		}
	}

	void DrawableObject::UpdateBones(Span<Mat4> bones) const
	{
		if (bonesBuffer && meshCache && meshCache->skin)
		{
			Mat4* data = static_cast<Mat4*>(bonesBuffer->GetMappedData());
			for (i32 i = 0; i < bones.Size(); ++i)
			{
				data[i] = bones[i] * meshCache->skin->poses[i];
			}
		}
	}

	MaterialResourceCache* DrawableObject::GetMaterial(u32 materialIndex) const
	{
		if (!meshCache) return nullptr;

		if (overrideMaterialsCache.Size() > materialIndex)
		{
			return overrideMaterialsCache[materialIndex];
		}

		if (meshCache->materials.Size() > materialIndex)
		{
			return meshCache->materials[materialIndex];
		}

		return nullptr;
	}

	RenderSceneObjects::RenderSceneObjects()
	{
		pendingUpdate.reserve(1000);
	}

	RenderSceneObjects::~RenderSceneObjects()
	{
		if (instanceDataBuffer) instanceDataBuffer->Destroy();
		if (tlasScratchBuffer) tlasScratchBuffer->Destroy();
		if (tlas) tlas->Destroy();
	}

	DrawableObject* RenderSceneObjects::CreateDrawable()
	{
		DrawableObject* drawable = Alloc<DrawableObject>(this);
		objects.Insert(drawable);
		return drawable;
	}

	u32 RenderSceneObjects::AllocateInstanceId()
	{
		if (!freeInstanceIds.Empty())
		{
			u32 id = freeInstanceIds.Back();
			freeInstanceIds.PopBack();
			return id;
		}
		return nextInstanceId++;
	}

	void RenderSceneObjects::FreeInstanceId(u32 id)
	{
		freeInstanceIds.EmplaceBack(id);
	}

	void RenderSceneObjects::EnsureInstanceDataCapacity(u32 requiredCount)
	{
		if (requiredCount > instanceDataBufferCapacity)
		{
			u32 newCapacity = requiredCount * 2;
			if (newCapacity < 256) newCapacity = 256;

			GPUBuffer* newBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = newCapacity * sizeof(InstanceData),
				.usage = ResourceUsage::ShaderResource,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "InstanceDataBuffer"
			});

			if (instanceDataBuffer)
			{
				memcpy(newBuffer->GetMappedData(), instanceDataBuffer->GetMappedData(), instanceDataBufferCapacity * sizeof(InstanceData));
				instanceDataBuffer->Destroy();
			}

			instanceDataBuffer = newBuffer;
			instanceDataBufferCapacity = newCapacity;
		}
	}

	void RenderSceneObjects::AddInstance(DrawableObject* drawable, u32 primitiveIndex, const InstanceDesc& desc, const InstanceData& data)
	{
		u32 dataId = AllocateInstanceId();
		u32 descIndex = static_cast<u32>(instances.Size());

		InstanceDesc instDesc = desc;
		instDesc.instanceID = dataId;

		instances.EmplaceBack(instDesc);
		instanceEntries.EmplaceBack(InstanceEntry{drawable, primitiveIndex});

		EnsureInstanceDataCapacity(dataId + 1);
		InstanceData* mapped = static_cast<InstanceData*>(instanceDataBuffer->GetMappedData());
		mapped[dataId] = data;

		if (dataId >= instanceDataCount)
		{
			instanceDataCount = dataId + 1;
		}

		drawable->instanceSlots[primitiveIndex] = {dataId, descIndex};
		tlasDirty = true;
	}

	void RenderSceneObjects::RemoveInstance(DrawableObject* drawable, u32 primitiveIndex)
	{
		if (primitiveIndex >= drawable->instanceSlots.Size()) return;

		InstanceSlot& slot = drawable->instanceSlots[primitiveIndex];
		if (slot.dataId == U32_MAX) return;

		FreeInstanceId(slot.dataId);

		u32 removeIdx = slot.descIndex;
		u32 lastIdx = static_cast<u32>(instances.Size()) - 1;

		if (removeIdx != lastIdx)
		{
			instances[removeIdx] = instances[lastIdx];
			instanceEntries[removeIdx] = instanceEntries[lastIdx];

			instanceEntries[removeIdx].drawable->instanceSlots[instanceEntries[removeIdx].primitiveIndex].descIndex = removeIdx;
		}

		instances.PopBack();
		instanceEntries.PopBack();

		slot = {U32_MAX, U32_MAX};
		tlasDirty = true;
	}

	u32 RenderSceneObjects::GetOrCreatePipeline(Array<DrawPipeline>& pipelines, const DrawPipelineDesc& desc)
	{
		for (u32 i = 0; i < pipelines.Size(); ++i)
		{
			if (pipelines[i].desc == desc)
			{
				return i;
			}
		}
		u32 index = static_cast<u32>(pipelines.Size());
		pipelines.EmplaceBack(DrawPipeline{.desc = desc});
		return index;
	}

	void RenderSceneObjects::DoUpdate(GPUCommandBuffer* cmd)
	{
		bool rtEnabled = Graphics::GetDevice()->GetFeatures().rayTracing;

		for (auto& drawable : pendingUpdate)
		{
			drawable->currentVersion++;

			//update mesh.
			if (drawable->meshCache == nullptr || drawable->mesh != drawable->meshCache->rid)
			{
				if (drawable->meshCache)
				{
					drawable->meshCache->DecreaseUsage();
				}
				drawable->meshCache = RenderResourceCache::GetMeshCache(drawable->mesh);
				drawable->meshCache->IncreaseUsage();

				if (drawable->meshCache && drawable->meshCache->skin)
				{
					drawable->UpdateSkinData();
				}

				drawable->UpdateAABB();
			}

			if (drawable->materialsDirty)
			{
				drawable->materialsDirty = false;
				for (MaterialResourceCache* material : drawable->overrideMaterialsCache)
				{
					if (material)
					{
						material->DecreaseUsage();
					}

				}
				drawable->overrideMaterialsCache.Clear();
				drawable->overrideMaterialsCache.Resize(drawable->materials.Size());

				for (usize i = 0; i < drawable->materials.Size(); i++)
				{
					if (drawable->materials[i])
					{
						drawable->overrideMaterialsCache[i] = RenderResourceCache::GetMaterialCache(drawable->materials[i]);
						drawable->overrideMaterialsCache[i]->IncreaseUsage();
					}
				}
			}

			for (const auto& ref : drawable->opaqueDrawcallRefs)
			{
				opaquePipelines[ref.pipelineIndex].drawcalls.Remove(ref.handle);
			}
			for (const auto& ref : drawable->transparentDrawcallRefs)
			{
				transparentPipelines[ref.pipelineIndex].drawcalls.Remove(ref.handle);
			}
			for (const auto& ref : drawable->shadowDrawcallRefs)
			{
				shadowPipelines[ref.pipelineIndex].drawcalls.Remove(ref.handle);
			}
			drawable->opaqueDrawcallRefs.Clear();
			drawable->transparentDrawcallRefs.Clear();
			drawable->shadowDrawcallRefs.Clear();

			// Remove old instances
			for (u32 p = 0; p < drawable->instanceSlots.Size(); ++p)
			{
				RemoveInstance(drawable, p);
			}
			drawable->instanceSlots.Clear();

			if (drawable->visible && drawable->meshCache)
			{
				bool frontFace = Determinant(Mat34(drawable->transform)) < 0.0f;
				CullMode cullMode = !frontFace ? CullMode::Back : CullMode::Front;
				bool hasBones = drawable->bonesDescriptor != nullptr;




				for (u32 p = 0; p < drawable->meshCache->primitives.Size(); ++p)
				{
					MeshPrimitive& primitive = drawable->meshCache->primitives[p];
					MaterialResourceCache* material = drawable->GetMaterial(primitive.materialIndex);
					if (material == nullptr || material->materialIndex == U32_MAX)
					{
						continue;
					}

					bool castShadow = drawable->castShadows && !material->transparent;

					Array<DrawPipeline>& pipelineStorage = !material->transparent ? opaquePipelines : transparentPipelines;


					DrawPipelineDesc opaqueDesc;
					opaqueDesc.cullMode = cullMode;
					opaqueDesc.vertexStride = drawable->meshCache->stride;
					opaqueDesc.hasBones = hasBones;
					opaqueDesc.hasUV1 = drawable->meshCache->hasUV1;
					opaqueDesc.hasColor = drawable->meshCache->hasColor;

					u32 pipelineId = GetOrCreatePipeline(pipelineStorage, opaqueDesc);

					DrawPipelineDesc shadowDesc;
					shadowDesc.cullMode = CullMode::Front;
					shadowDesc.vertexStride = drawable->meshCache->stride;
					shadowDesc.hasBones = hasBones;

					auto handle = pipelineStorage[pipelineId].drawcalls.Insert();
					Drawcall& dc = pipelineStorage[pipelineId].drawcalls[handle];
					dc.vertexBuffer = drawable->meshCache->vertexBuffer;
					dc.indexBuffer = drawable->meshCache->indexBuffer;
					dc.firstIndex = primitive.firstIndex;
					dc.indexCount = primitive.indexCount;
					dc.material = material;
					dc.transform = drawable->transform;
					dc.bones = drawable->bonesDescriptor;
					dc.aabb = Math::TransformAABB(primitive.aabb, drawable->transform);
					dc.userData = drawable->userData;
					dc.layerMask = drawable->layerMask;

					if (!material->transparent)
					{
						drawable->opaqueDrawcallRefs.EmplaceBack(DrawcallRef{pipelineId, handle});
					}
					else
					{
						drawable->transparentDrawcallRefs.EmplaceBack(DrawcallRef{pipelineId, handle});
					}

					if (castShadow)
					{
						u32 shadowPipelineIdx = castShadow ? GetOrCreatePipeline(shadowPipelines, shadowDesc) : 0;

						auto shadowHandle = shadowPipelines[shadowPipelineIdx].drawcalls.Insert();
						Drawcall& sdc = shadowPipelines[shadowPipelineIdx].drawcalls[shadowHandle];
						sdc.vertexBuffer = drawable->meshCache->vertexBuffer;
						sdc.indexBuffer = drawable->meshCache->indexBuffer;
						sdc.firstIndex = primitive.firstIndex;
						sdc.indexCount = primitive.indexCount;
						sdc.material = material;
						sdc.transform = drawable->transform;
						sdc.bones = drawable->bonesDescriptor;
						sdc.aabb = Math::TransformAABB(primitive.aabb, drawable->transform);
						sdc.userData = drawable->userData;
						sdc.layerMask = drawable->layerMask;

						drawable->shadowDrawcallRefs.EmplaceBack(DrawcallRef{shadowPipelineIdx, shadowHandle});
					}
				}

				// Register instances for this drawable
				if (rtEnabled && !drawable->meshCache->blasArray.Empty()
					&& drawable->meshCache->geometryIndex != U32_MAX
					)
				{
					drawable->instanceSlots.Resize(drawable->meshCache->primitives.Size());

					for (u32 p = 0; p < drawable->meshCache->primitives.Size(); ++p)
					{
						if (!drawable->meshCache->blasArray[p]) continue;

						u32 matIdx = 0;
						MaterialResourceCache* mat = drawable->GetMaterial(p);
						if (mat && mat->materialIndex != U32_MAX)
						{
							matIdx = mat->materialIndex;
						}

						InstanceDesc desc{};
						desc.bottomLevelAS = drawable->meshCache->blasArray[p];
						desc.transform = drawable->transform;
						desc.forceOpaque = true;

						InstanceData data{
							.meshIndex = drawable->meshCache->geometryIndex,
							.materialIndex = matIdx,
							.firstIndex = drawable->meshCache->primitives[p].firstIndex,
							.pad = 0
						};

						AddInstance(drawable, p, desc, data);
					}
				}
			}
		}
		pendingUpdate.clear();

		if (rtEnabled && tlasDirty)
		{
			tlasDirty = false;

			if (!instances.Empty())
			{
				SK_SCOPED_ZONE("RenderObjects - TLAS Update", cmd);

				if (tlas == nullptr || instances.Size() > tlasMaxInstances)
				{
					if (tlas) tlas->Destroy();
					if (tlasScratchBuffer) tlasScratchBuffer->Destroy();

					u32 newCapacity = static_cast<u32>(instances.Size()) * 2;
					if (newCapacity < 256) newCapacity = 256;

					Array<InstanceDesc> capacityInstances(newCapacity);
					for (u32 i = 0; i < instances.Size(); ++i)
					{
						capacityInstances[i] = instances[i];
					}

					TopLevelASDesc tlasDesc{};
					tlasDesc.instances = capacityInstances;
					tlasDesc.flags = BuildAccelerationStructureFlags::PreferFastTrace;
					tlasDesc.debugName = "SceneTLAS";

					tlas = Graphics::CreateTopLevelAS(tlasDesc);
					tlas->UpdateInstances(instances);

					usize scratchSize = Graphics::GetAccelerationStructureBuildScratchSize(tlasDesc);
					tlasScratchBuffer = Graphics::CreateBuffer(BufferDesc{
						.size = scratchSize,
						.usage = ResourceUsage::ShaderResource,
						.hostVisible = false,
						.persistentMapped = false,
						.debugName = "SceneTLAS_Scratch"
					});

					tlasMaxInstances = newCapacity;
				}
				else
				{
					tlas->UpdateInstances(instances);
				}

				cmd->BuildTopLevelAS(tlas, AccelerationStructureBuildInfo{.scratchBuffer = tlasScratchBuffer});
			}
		}
	}

}