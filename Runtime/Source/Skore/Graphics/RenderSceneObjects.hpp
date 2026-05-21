#pragma once

#include "Device.hpp"
#include "RenderResourceCache.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/PackedArray.hpp"

namespace Skore
{
	class RendererComponent;

	struct DrawcallVisibility
	{
		constexpr static u8 None       = 0;
		constexpr static u8 CastShadow = 1 << 0;
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
		u32 firstIndex = 0; // mesh-local index of the primitive's first triangle vertex
		u32 indexCount = 0;

		MeshResourceCachePtr     mesh;
		MaterialResourceCachePtr material;
		u64                      userData = 0;
		u32                      vertexByteOffset = U32_MAX; // mesh's vertex slab offset in MeshDataBuffer
		u32                      indexByteOffset = U32_MAX;  // mesh's index slab offset in MeshDataBuffer
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
		u32  pipelineIndex = U32_MAX;
		u64  handle = U64_MAX;
		u32  shadowPipelineIndex = U32_MAX;
		u64  shadowHandle = U64_MAX;
		u32  instanceIndex = U32_MAX;
		u32  pendingDrawcallIndex = U32_MAX;
		bool transparent = false;
	};

	constexpr static u32 InitialInstanceNumber = 1000;

	struct InstanceData
	{
		Mat4 transform;
		u32  materialIndex;
		u32  vertexByteOffset;
		u32  vertexLayoutIndex;
		u32  indexCount;

		Vec3 aabbMin;
		u32  firstIndex;

		Vec3 aabbMax;
		u32  pipelineIndex;

		u32	 drawcallIndex;
		Vec3 pad;
	};

	struct DrawcallDesc
	{
		MeshResourceCachePtr mesh;
		RID                  material;

		u32                  firstIndex = 0;
		u32                  indexCount = 0;

		Mat4 transform = Mat4(1.0);
		AABB aabb      = {};
		u64  userData  = 0;
		u64  layerMask = 1ULL;

		GPUDescriptorSet*        bones = nullptr;

		u32               vertexByteOffset  = U32_MAX;
		u32               indexByteOffset   = U32_MAX;
		u32               vertexLayoutIndex = U32_MAX;

		u8 visibility = DrawcallVisibility::CastShadow;
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

		GPUBuffer* instanceDataBuffer = nullptr;
		u32        instanceDataSize = 0;
		u32        instanceDataCount = 0;
		Array<u32> instanceFreeIndices;

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

		bool asyncLoad = true;
	private:

		struct PendingDrawcallEntry
		{
			MeshResourceCachePtr     mesh;
			MaterialResourceCachePtr material;
			RendererComponent*       component = nullptr;
			u32                      primitiveIndex = 0;
			DrawcallDesc             desc;
		};
		Array<PendingDrawcallEntry> pendingDrawcalls;

		static u32 GetOrCreatePipeline(Array<DrawPipeline>& pipelines, const DrawPipelineDesc& desc);

		void DoCreateDrawcall(const DrawcallDesc& desc, const MaterialResourceCachePtr& material, RendererComponent* owner, u32 primitiveIndex, DrawcallRef& ref);
		void AddPendingDrawcall(RendererComponent* component, u32 primitiveIndex, const MeshResourceCachePtr& mesh, const MaterialResourceCachePtr& material, const DrawcallDesc& desc, DrawcallRef& outRef);
		void RemovePendingDrawcall(u32 idx);
	};
}
