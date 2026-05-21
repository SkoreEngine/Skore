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
		instanceDataBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = sizeof(InstanceData) * InitialInstanceNumber,
			.usage = ResourceUsage::UnorderedAccess,
			.hostVisible = true,
			.persistentMapped = true,
			.debugName = "InstanceBuffer"
		});
	}

	RenderSceneObjects::~RenderSceneObjects()
	{
		if (instanceDataBuffer)
		{
			instanceDataBuffer->Destroy();
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

	DrawcallRef RenderSceneObjects::CreateDrawcall(const DrawcallDesc& desc, RendererComponent* owner, u32 primitiveIndex)
	{
		DrawcallRef ref;

		MaterialResourceCachePtr material = desc.material ? RenderResourceCache::GetMaterialCache(desc.material, asyncLoad) : nullptr;
		if (material == nullptr)
		{
			return ref;
		}

		ref.transparent = material->transparent;

		if ((desc.mesh && !desc.mesh->IsLoaded()) || !material->IsLoaded())
		{
			AddPendingDrawcall(owner, primitiveIndex, desc.mesh, material, desc, ref);
			return ref;
		}

		DoCreateDrawcall(desc, material, owner, primitiveIndex, ref);
		return ref;
	}

	void RenderSceneObjects::DoCreateDrawcall(const DrawcallDesc& desc, const MaterialResourceCachePtr& material, RendererComponent* owner, u32 primitiveIndex, DrawcallRef& ref)
	{
		u32 vertexByteOffset = desc.mesh ? desc.mesh->vertexByteOffset : desc.vertexByteOffset;
		u32 indexByteOffset  = desc.mesh ? desc.mesh->indexByteOffset  : desc.indexByteOffset;

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
		dc.vertexByteOffset = vertexByteOffset;
		dc.indexByteOffset = indexByteOffset;
		dc.mesh = desc.mesh;
		dc.material = material;
		dc.userData = desc.userData;
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
			sdc.vertexByteOffset = vertexByteOffset;
			sdc.indexByteOffset = indexByteOffset;
			sdc.material = material;
			sdc.userData = desc.userData;
			sdc.vertexLayoutIndex = desc.vertexLayoutIndex;
			sdc.bones = desc.bones;
			sdc.localAabb = desc.aabb;
			sdc.aabb = dc.aabb;
			sdc.transform = desc.transform;
			sdc.layerMask = desc.layerMask;
		}

		if (!instanceFreeIndices.Empty())
		{
			ref.instanceIndex = instanceFreeIndices.Back();
			instanceFreeIndices.PopBack();
		}
		else
		{
			ref.instanceIndex = instanceDataSize++;
		}

		instanceDataCount++;

		if (instanceDataBuffer->GetDesc().size < sizeof(instanceFreeIndices) * instanceDataSize)
		{
			GPUBuffer* newBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = instanceDataSize * sizeof(InstanceData) * 2,
				.usage = ResourceUsage::UnorderedAccess,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "InstanceBuffer"
			});

			memcpy(newBuffer->GetMappedData(), instanceDataBuffer->GetMappedData(), instanceDataSize * sizeof(InstanceData));
			instanceDataBuffer->Destroy();
			instanceDataBuffer = newBuffer;
		}

		InstanceData* data = static_cast<InstanceData*>(instanceDataBuffer->GetMappedData());
		new(data + ref.instanceIndex) InstanceData{
			.transform = desc.transform,
			.materialIndex = material->materialIndex,
			.vertexByteOffset = vertexByteOffset,
			.vertexLayoutIndex = desc.vertexLayoutIndex,
			.indexCount = desc.indexCount,
			.aabbMin = dc.aabb.min,
			.firstIndex = desc.firstIndex,
			.aabbMax = dc.aabb.max,
			.pipelineIndex = ref.pipelineIndex
		};
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
		if (ref.pendingDrawcallIndex != U32_MAX)
		{
			RemovePendingDrawcall(ref.pendingDrawcallIndex);
		}
		if (ref.instanceIndex != U32_MAX)
		{
			instanceDataCount--;
			instanceFreeIndices.EmplaceBack(ref.instanceIndex);
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
		RenderResourceCache::Flush();

		for (u32 i = 0; i < pendingDrawcalls.Size(); )
		{
			PendingDrawcallEntry& pending = pendingDrawcalls[i];
			if (!pending.mesh || !pending.mesh->IsLoaded() || !pending.material || !pending.material->IsLoaded())
			{
				++i;
				continue;
			}

			RendererComponent*       component = pending.component;
			u32                      primitiveIndex = pending.primitiveIndex;
			DrawcallDesc             desc = pending.desc;
			MaterialResourceCachePtr material = pending.material;

			component->references[primitiveIndex].pendingDrawcallIndex = U32_MAX;
			RemovePendingDrawcall(i);

			DrawcallRef& ref = component->references[primitiveIndex];
			DoCreateDrawcall(desc, material, component, primitiveIndex, ref);
		}
	}

}
