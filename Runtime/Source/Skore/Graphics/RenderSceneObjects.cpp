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

	void RenderSceneObjects::AddInstance(RendererComponent* component, u32 primitiveIndex, const InstanceDesc& desc, const InstanceData& data)
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

		component->instanceSlots[primitiveIndex] = {dataId, descIndex};
		tlasDirty = true;
	}

	void RenderSceneObjects::RemoveInstance(RendererComponent* component, u32 primitiveIndex)
	{
		if (primitiveIndex >= component->instanceSlots.Size()) return;

		InstanceSlot& slot = component->instanceSlots[primitiveIndex];
		if (slot.dataId == U32_MAX) return;

		FreeInstanceId(slot.dataId);

		u32 removeIdx = slot.descIndex;
		u32 lastIdx = static_cast<u32>(instances.Size()) - 1;

		if (removeIdx != lastIdx)
		{
			instances[removeIdx] = instances[lastIdx];
			instanceEntries[removeIdx] = instanceEntries[lastIdx];

			instanceEntries[removeIdx].component->instanceSlots[instanceEntries[removeIdx].primitiveIndex].descIndex = removeIdx;
		}

		instances.PopBack();
		instanceEntries.PopBack();

		slot = {U32_MAX, U32_MAX};
		tlasDirty = true;
	}

	void RenderSceneObjects::UpdateInstanceTransform(u32 descIndex, const Mat4& transform)
	{
		if (descIndex >= instances.Size()) return;
		instances[descIndex].transform = transform;
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
