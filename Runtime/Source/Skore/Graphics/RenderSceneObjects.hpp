#pragma once

#include "Device.hpp"
#include "GraphicsCommon.hpp"
#include "RenderResourceCache.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/PackedArray.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Core/UnorderedDense.hpp"

namespace Skore
{
	class RendererComponent;

	struct DrawPipelineDesc
	{
		CullMode cullMode = CullMode::Back;
		u32      vertexStride = 0;
		bool     hasBones = false;
		bool     hasUV1 = false;
		bool     hasColor = false;

		bool operator==(const DrawPipelineDesc& other) const
		{
			return cullMode == other.cullMode
				&& vertexStride == other.vertexStride && hasBones == other.hasBones
				&& hasUV1 == other.hasUV1 && hasColor == other.hasColor;
		}
	};

	struct Drawcall
	{
		u32 firstIndex = 0;
		u32 indexCount = 0;

		GPUBuffer* vertexBuffer = nullptr;
		GPUBuffer* indexBuffer = nullptr;
		MaterialResourceCache* material = nullptr;
		u64 userData = 0;

		GPUDescriptorSet* bones = nullptr;

		AABB aabb = {};
		Mat4 transform = Mat4(1.0);

		u64 layerMask = 1ULL;
	};

	struct DrawPipeline
	{
		DrawPipelineDesc desc;
		PackedArray<Drawcall> drawcalls;
	};

	struct DrawcallRef
	{
		u32 pipelineIndex = 0;
		u64 handle = 0;
	};

	struct InstanceSlot
	{
		u32 dataId = U32_MAX;
		u32 descIndex = U32_MAX;
	};

	struct InstanceData
	{
		u32 meshIndex;
		u32 materialIndex;
		u32 firstIndex;
		u32 pad;
	};

	class SK_API RenderSceneObjects
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(RenderSceneObjects);
		RenderSceneObjects();
		~RenderSceneObjects();

		void DoUpdate(GPUCommandBuffer* cmd);

		Array<DrawPipeline> opaquePipelines;
		Array<DrawPipeline> transparentPipelines;
		Array<DrawPipeline> shadowPipelines;

		GPUTopLevelAS* tlas = nullptr;
		GPUBuffer*     instanceDataBuffer = nullptr;
		u32            instanceDataCount = 0;

		u32 GetVisiblePipelineCount() const
		{
			return static_cast<u32>(opaquePipelines.Size() + transparentPipelines.Size());
		}

		template<typename Fn>
		SK_FINLINE void ForEachVisiblePipeline(Fn&& fn)
		{
			u32 index = 0;
			for (u32 i = 0; i < opaquePipelines.Size(); i++)
				fn(index++, opaquePipelines[i]);
			for (u32 i = 0; i < transparentPipelines.Size(); i++)
				fn(index++, transparentPipelines[i]);
		}

		template<typename Fn>
		SK_FINLINE void ForEachVisiblePipeline(Fn&& fn) const
		{
			u32 index = 0;
			for (u32 i = 0; i < opaquePipelines.Size(); i++)
				fn(index++, opaquePipelines[i]);
			for (u32 i = 0; i < transparentPipelines.Size(); i++)
				fn(index++, transparentPipelines[i]);
		}

		static u32 GetOrCreatePipeline(Array<DrawPipeline>& pipelines, const DrawPipelineDesc& desc);

		void AddInstance(RendererComponent* component, u32 primitiveIndex, const InstanceDesc& desc, const InstanceData& data);
		void RemoveInstance(RendererComponent* component, u32 primitiveIndex);
		void UpdateInstanceTransform(u32 descIndex, const Mat4& transform);
		void MarkTlasDirty() { tlasDirty = true; }

		friend class RendererComponent;

	private:
		Array<InstanceDesc> instances;

		struct InstanceEntry
		{
			RendererComponent* component = nullptr;
			u32 primitiveIndex = 0;
		};
		Array<InstanceEntry> instanceEntries;
		bool tlasDirty = false;

		GPUBuffer* tlasScratchBuffer = nullptr;
		u32        tlasMaxInstances = 0;
		u32        instanceDataBufferCapacity = 0;

		u32        nextInstanceId = 0;
		Array<u32> freeInstanceIds;

		u32  AllocateInstanceId();
		void FreeInstanceId(u32 id);
		void EnsureInstanceDataCapacity(u32 requiredCount);
	};
}
