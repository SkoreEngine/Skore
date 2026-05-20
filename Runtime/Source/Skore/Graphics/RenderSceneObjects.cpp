#include "Skore/Graphics/RenderSceneObjects.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"

namespace Skore
{

	void RenderSceneObjectsInit()
	{
	}

	void RenderSceneObjectsShutdown()
	{
	}

	RenderSceneObjects::RenderSceneObjects()
	{
	}

	RenderSceneObjects::~RenderSceneObjects()
	{
		if (instanceDataBuffer) instanceDataBuffer->Destroy();
		if (tlasScratchBuffer) tlasScratchBuffer->Destroy();
		if (tlas) tlas->Destroy();
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

	void RenderSceneObjects::AddInstance(RendererComponent* component, u32 primitiveIndex, const InstanceDesc& desc, const InstanceData& data, DrawcallRef& outRef)
	{
		u32 dataId = AllocateInstanceId();
		u32 descIndex = static_cast<u32>(instances.Size());

		InstanceDesc instDesc = desc;
		instDesc.instanceID = dataId;

		instances.EmplaceBack(instDesc);
		instanceEntries.EmplaceBack(InstanceEntry{component, primitiveIndex});

		EnsureInstanceDataCapacity(dataId + 1);
		InstanceData* mapped = static_cast<InstanceData*>(instanceDataBuffer->GetMappedData());
		mapped[dataId] = data;

		if (dataId >= instanceDataCount)
		{
			instanceDataCount = dataId + 1;
		}

		outRef.instanceDataId = dataId;
		outRef.instanceDescIndex = descIndex;
		tlasDirty = true;
	}

	void RenderSceneObjects::RemoveInstance(const DrawcallRef& ref)
	{
		if (ref.instanceDescIndex == U32_MAX) return;

		FreeInstanceId(ref.instanceDataId);

		u32 removeIdx = ref.instanceDescIndex;
		u32 lastIdx = static_cast<u32>(instances.Size()) - 1;

		if (removeIdx != lastIdx)
		{
			instances[removeIdx] = instances[lastIdx];
			instanceEntries[removeIdx] = instanceEntries[lastIdx];

			InstanceEntry& moved = instanceEntries[removeIdx];
			moved.component->references[moved.primitiveIndex].instanceDescIndex = removeIdx;
		}

		instances.PopBack();
		instanceEntries.PopBack();
		tlasDirty = true;
	}

	DrawcallRef RenderSceneObjects::CreateDrawcall(const DrawcallDesc& desc, RendererComponent* owner, u32 primitiveIndex)
	{
		DrawcallRef ref;

		MaterialResourceCachePtr material = desc.material ? RenderResourceCache::GetMaterialCache(desc.material, asyncLoad) : nullptr;
		if (material == nullptr || material->materialIndex == U32_MAX)
		{
			return ref;
		}

		ref.transparent = material->transparent;

		// Mesh data (vertex/index buffers + BLAS) is created on the worker. If it's not yet
		// published, park the registration — DoUpdate promotes once IsLoaded() goes true.
		if (desc.mesh && !desc.mesh->IsLoaded())
		{
			AddPendingDrawcall(owner, primitiveIndex, desc.mesh, material, desc, ref);
			return ref;
		}

		DoCreateDrawcall(desc, material, owner, primitiveIndex, ref);
		return ref;
	}

	void RenderSceneObjects::DoCreateDrawcall(const DrawcallDesc& desc, const MaterialResourceCachePtr& material,
		RendererComponent* owner, u32 primitiveIndex, DrawcallRef& ref)
	{
		GPUBuffer* vertexBuffer = desc.mesh ? desc.mesh->vertexBuffer : desc.vertexBuffer;
		GPUBuffer* indexBuffer  = desc.mesh ? desc.mesh->indexBuffer  : desc.indexBuffer;

		ref.transparent = material->transparent;

		bool frontFace = Determinant(Mat34(desc.transform)) < 0.0f;
		CullMode cullMode = !frontFace ? CullMode::Back : CullMode::Front;
		bool hasBones = desc.bones != nullptr;

		DrawPipelineDesc pipelineDesc;
		pipelineDesc.cullMode = cullMode;
		pipelineDesc.hasBones = hasBones;
		pipelineDesc.shader = material->shader;

		Array<DrawPipeline>& pipelineStorage = ref.transparent ? transparentPipelines : opaquePipelines;
		ref.pipelineIndex = GetOrCreatePipeline(pipelineStorage, pipelineDesc);
		ref.handle = pipelineStorage[ref.pipelineIndex].drawcalls.Insert();

		Drawcall& dc = pipelineStorage[ref.pipelineIndex].drawcalls[ref.handle];
		dc.firstIndex = desc.firstIndex;
		dc.indexCount = desc.indexCount;
		dc.vertexBuffer = vertexBuffer;
		dc.indexBuffer = indexBuffer;
		dc.mesh = desc.mesh;
		dc.material = material;
		dc.userData = desc.userData;
		dc.meshIndex = desc.meshIndex;
		dc.vertexLayoutIndex = desc.vertexLayoutIndex;
		dc.bones = desc.bones;
		dc.localAabb = desc.aabb;
		dc.aabb = Math::TransformAABB(desc.aabb, desc.transform);
		dc.transform = desc.transform;
		dc.layerMask = desc.layerMask;

		const bool castShadow = (desc.visibility & DrawcallVisibility::CastShadow) != 0 && !ref.transparent;
		if (castShadow)
		{
			DrawPipelineDesc shadowDesc;
			shadowDesc.cullMode = CullMode::Front;
			shadowDesc.hasBones = hasBones;

			ref.shadowPipelineIndex = GetOrCreatePipeline(shadowPipelines, shadowDesc);
			ref.shadowHandle = shadowPipelines[ref.shadowPipelineIndex].drawcalls.Insert();

			// Shadow drawcall shares the same mesh/material refs that the primary already accounted for.
			Drawcall& sdc = shadowPipelines[ref.shadowPipelineIndex].drawcalls[ref.shadowHandle];
			sdc.firstIndex = desc.firstIndex;
			sdc.indexCount = desc.indexCount;
			sdc.vertexBuffer = vertexBuffer;
			sdc.indexBuffer = indexBuffer;
			sdc.material = material;
			sdc.userData = desc.userData;
			sdc.meshIndex = desc.meshIndex;
			sdc.vertexLayoutIndex = desc.vertexLayoutIndex;
			sdc.bones = desc.bones;
			sdc.localAabb = desc.aabb;
			sdc.aabb = dc.aabb;
			sdc.transform = desc.transform;
			sdc.layerMask = desc.layerMask;
		}

		const bool rayTraced = (desc.visibility & DrawcallVisibility::RayTraced) != 0;
		const bool rtEnabled = Graphics::GetDevice()->GetFeatures().rayTracing;
		if (rtEnabled && rayTraced && desc.meshIndex != U32_MAX && material->materialIndex != U32_MAX)
		{
			// At this point the mesh is loaded — pick the BLAS slot directly (nullptr means
			// no BLAS was produced, e.g. skinned mesh; just skip enrolment).
			GPUBottomLevelAS* blas = nullptr;
			if (desc.blas) blas = desc.blas;
			else if (desc.mesh && primitiveIndex < desc.mesh->blasArray.Size()) blas = desc.mesh->blasArray[primitiveIndex];

			if (blas)
			{
				InstanceDesc instDesc{};
				instDesc.bottomLevelAS = blas;
				instDesc.transform = desc.transform;
				instDesc.forceOpaque = true;

				InstanceData data{
					.meshIndex = desc.meshIndex,
					.vertexLayoutIndex = desc.vertexLayoutIndex,
					.materialIndex = material->materialIndex,
					.firstIndex = desc.firstIndex
				};

				AddInstance(owner, primitiveIndex, instDesc, data, ref);
			}
		}
	}

	void RenderSceneObjects::UpdateTransform(const DrawcallRef& ref, const Mat4& transform)
	{
		if (ref.pipelineIndex != U32_MAX)
		{
			Array<DrawPipeline>& pipelineStorage = ref.transparent ? transparentPipelines : opaquePipelines;
			Drawcall& dc = pipelineStorage[ref.pipelineIndex].drawcalls[ref.handle];
			dc.transform = transform;
			dc.aabb = Math::TransformAABB(dc.localAabb, transform);
		}
		if (ref.shadowPipelineIndex != U32_MAX)
		{
			Drawcall& sdc = shadowPipelines[ref.shadowPipelineIndex].drawcalls[ref.shadowHandle];
			sdc.transform = transform;
			sdc.aabb = Math::TransformAABB(sdc.localAabb, transform);
		}
		if (ref.instanceDescIndex != U32_MAX)
		{
			instances[ref.instanceDescIndex].transform = transform;
			tlasDirty = true;
		}
		if (ref.pendingDrawcallIndex != U32_MAX)
		{
			pendingDrawcalls[ref.pendingDrawcallIndex].desc.transform = transform;
		}
	}

	void RenderSceneObjects::UpdateLayerMask(const DrawcallRef& ref, u64 layerMask)
	{
		if (ref.pipelineIndex != U32_MAX)
		{
			Array<DrawPipeline>& pipelineStorage = ref.transparent ? transparentPipelines : opaquePipelines;
			pipelineStorage[ref.pipelineIndex].drawcalls[ref.handle].layerMask = layerMask;
		}
		if (ref.shadowPipelineIndex != U32_MAX)
		{
			shadowPipelines[ref.shadowPipelineIndex].drawcalls[ref.shadowHandle].layerMask = layerMask;
		}
		if (ref.pendingDrawcallIndex != U32_MAX)
		{
			pendingDrawcalls[ref.pendingDrawcallIndex].desc.layerMask = layerMask;
		}
	}

	void RenderSceneObjects::RemoveDrawcall(const DrawcallRef& ref)
	{
		if (ref.pipelineIndex != U32_MAX)
		{
			Array<DrawPipeline>& pipelineStorage = ref.transparent ? transparentPipelines : opaquePipelines;
			pipelineStorage[ref.pipelineIndex].drawcalls.Remove(ref.handle);
		}
		if (ref.shadowPipelineIndex != U32_MAX)
		{
			shadowPipelines[ref.shadowPipelineIndex].drawcalls.Remove(ref.shadowHandle);
		}
		if (ref.instanceDescIndex != U32_MAX)
		{
			RemoveInstance(ref);
		}
		if (ref.pendingDrawcallIndex != U32_MAX)
		{
			RemovePendingDrawcall(ref.pendingDrawcallIndex);
		}
	}

	void RenderSceneObjects::AddPendingDrawcall(RendererComponent* component, u32 primitiveIndex,
		const MeshResourceCachePtr& mesh, const MaterialResourceCachePtr& material,
		const DrawcallDesc& desc, DrawcallRef& outRef)
	{
		u32 idx = static_cast<u32>(pendingDrawcalls.Size());
		pendingDrawcalls.EmplaceBack(PendingDrawcallEntry{mesh, material, component, primitiveIndex, desc});
		outRef.pendingDrawcallIndex = idx;
	}

	void RenderSceneObjects::RemovePendingDrawcall(u32 idx)
	{
		u32 lastIdx = static_cast<u32>(pendingDrawcalls.Size()) - 1;
		if (idx != lastIdx)
		{
			pendingDrawcalls[idx] = std::move(pendingDrawcalls[lastIdx]);
			PendingDrawcallEntry& moved = pendingDrawcalls[idx];
			moved.component->references[moved.primitiveIndex].pendingDrawcallIndex = idx;
		}
		pendingDrawcalls.PopBack();
	}

	void RenderSceneObjects::DoUpdate(GPUCommandBuffer* cmd)
	{
		// Run any worker-posted callbacks before reading mesh state — this is where the mesh
		// worker publishes vertex/index buffers, blasArray, and the bindless descriptor slots.
		RenderResourceCache::FlushMainThreadTasks();

		// Promote parked drawcalls whose meshes finished loading this frame.
		for (u32 i = 0; i < pendingDrawcalls.Size(); )
		{
			PendingDrawcallEntry& pending = pendingDrawcalls[i];
			if (!pending.mesh || !pending.mesh->IsLoaded())
			{
				++i;
				continue;
			}

			RendererComponent*       component = pending.component;
			u32                      primitiveIndex = pending.primitiveIndex;
			DrawcallDesc             desc = pending.desc;
			MaterialResourceCachePtr material = pending.material;

			// Clear pending slot before re-registering so the new ref doesn't carry the stale
			// parked index. RemovePendingDrawcall fixes up the swapped-in entry's owner.
			component->references[primitiveIndex].pendingDrawcallIndex = U32_MAX;
			RemovePendingDrawcall(i);
			// Don't increment i: swap-and-pop placed a new entry at this index.

			DrawcallRef& ref = component->references[primitiveIndex];
			DoCreateDrawcall(desc, material, component, primitiveIndex, ref);
		}

		bool rtEnabled = Graphics::GetDevice()->GetFeatures().rayTracing;

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
