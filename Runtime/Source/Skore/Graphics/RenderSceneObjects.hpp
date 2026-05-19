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

	struct DrawcallVisibility
	{
		constexpr static u8 None       = 0;
		constexpr static u8 CastShadow = 1 << 0;
		constexpr static u8 RayTraced  = 1 << 1;
	};

	struct DrawPipelineDesc
	{
		CullMode cullMode = CullMode::Back;
		bool     hasBones = false;
		RID      shader = {};

		bool operator==(const DrawPipelineDesc& other) const
		{
			return cullMode == other.cullMode
				&& hasBones == other.hasBones
				&& shader == other.shader;
		}
	};

	struct Drawcall
	{
		u32 firstIndex = 0;
		u32 indexCount = 0;

		GPUBuffer*               vertexBuffer = nullptr;
		GPUBuffer*               indexBuffer = nullptr;
		MeshResourceCachePtr     mesh;
		MaterialResourceCachePtr material;
		u64                      userData = 0;
		u32                      meshIndex = U32_MAX;
		u32                      vertexLayoutIndex = U32_MAX;

		GPUDescriptorSet* bones = nullptr;

		AABB localAabb = {};
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
		u32  pipelineIndex       = U32_MAX;
		u64  handle              = U64_MAX;
		u32  shadowPipelineIndex = U32_MAX;
		u64  shadowHandle        = U64_MAX;
		u32  instanceDataId      = U32_MAX;
		u32  instanceDescIndex   = U32_MAX;
		bool transparent         = false;
	};

	struct DrawcallDesc
	{
		MeshResourceCachePtr mesh;
		GPUBuffer*           vertexBuffer = nullptr;
		GPUBuffer*           indexBuffer = nullptr;
		u32                  firstIndex = 0;
		u32                  indexCount = 0;

		Mat4 transform = Mat4(1.0);
		AABB aabb      = {};
		u64  userData  = 0;
		u64  layerMask = 1ULL;

		MaterialResourceCachePtr material;
		GPUDescriptorSet*        bones = nullptr;

		GPUBottomLevelAS* blas              = nullptr;
		u32               meshIndex         = U32_MAX;
		u32               vertexLayoutIndex = U32_MAX;

		u8 visibility = DrawcallVisibility::CastShadow | DrawcallVisibility::RayTraced;
	};

	struct InstanceData
	{
		u32 meshIndex;
		u32 vertexLayoutIndex;
		u32 materialIndex;
		u32 firstIndex;
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

		DrawcallRef CreateDrawcall(const DrawcallDesc& desc, RendererComponent* owner, u32 primitiveIndex);
		void        UpdateTransform(const DrawcallRef& ref, const Mat4& transform);
		void        UpdateLayerMask(const DrawcallRef& ref, u64 layerMask);
		void        RemoveDrawcall(const DrawcallRef& ref);

		friend class RendererComponent;

	private:
		Array<InstanceDesc> instances;

		struct InstanceEntry
		{
			RendererComponent* component = nullptr;
			u32                primitiveIndex = 0;
		};
		Array<InstanceEntry> instanceEntries;
		bool tlasDirty = false;

		GPUBuffer* tlasScratchBuffer = nullptr;
		u32        tlasMaxInstances = 0;
		u32        instanceDataBufferCapacity = 0;

		u32        nextInstanceId = 0;
		Array<u32> freeInstanceIds;

		static u32 GetOrCreatePipeline(Array<DrawPipeline>& pipelines, const DrawPipelineDesc& desc);
		u32  AllocateInstanceId();
		void FreeInstanceId(u32 id);
		void EnsureInstanceDataCapacity(u32 requiredCount);
		void AddInstance(RendererComponent* component, u32 primitiveIndex, const InstanceDesc& desc, const InstanceData& data, DrawcallRef& outRef);
		void RemoveInstance(const DrawcallRef& ref);
	};
}
