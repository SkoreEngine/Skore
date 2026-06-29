#pragma once

#include "Device.hpp"
#include "RenderResourceCache.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/PackedArray.hpp"
#include "Skore/Core/Traits.hpp"
#include "Skore/Core/UnorderedDense.hpp"

namespace Skore
{
	SK_HANDLER(RenderableObject);

	class RenderSceneObjects;
	struct RenderableObjectStorage;

	struct DrawPipelineDesc
	{
		CullMode cullMode = CullMode::Back;
		bool     hasBones = false;
		bool     masked = false;
		RID      shader = {};
		RID      materialGraph = {};

		bool operator==(const DrawPipelineDesc& other) const
		{
			return cullMode == other.cullMode
				&& hasBones == other.hasBones
				&& masked == other.masked
				&& shader == other.shader
				&& materialGraph == other.materialGraph;
		}
	};

	struct Drawcall
	{
		u32 firstIndex = 0; // mesh-local index of the primitive's first triangle vertex
		u32 indexCount = 0;

		MeshResourceCachePtr     mesh;
		u64                      meshVersion = 0;
		MaterialResourceCachePtr material;
		u64                      userData = 0;
		u32                      vertexByteOffset = U32_MAX; // mesh's vertex slab offset in MeshDataBuffer
		u32                      indexByteOffset = U32_MAX;  // mesh's index slab offset in MeshDataBuffer
		u32                      vertexLayoutIndex = U32_MAX;

		GPUDescriptorSet* bones = nullptr;
		u32               boneBufferIndex = U32_MAX;

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
		u32  instanceDescIndex = U32_MAX;
		bool transparent = false;
		bool masked = false;
	};

	struct MovedRenderableObject
	{
		RenderableObject object;
		Mat4             previousTransform = Mat4(1.0);
		bool             bonesChanged = false;
	};

	constexpr static u32 InitialInstanceNumber = 1000;

	struct InstanceData
	{
		Mat4 transform;
		u32  materialIndex;
		u32  vertexByteOffset;
		u32  vertexLayoutIndex;
		u32  primitiveInfoIndex;

		Vec3 aabbMin;
		u32  pad0;

		Vec3 aabbMax;
		u32  pipelineIndex;

		u32	 drawcallIndex;
		u32	 transparent;
		u64  layerMask;

		// U32_MAX when this instance does not cast a shadow.
		u32  shadowPipelineIndex;
		// U32_MAX when this instance has no scene skinning buffer.
		u32  boneBufferIndex;
		u32  pad3[2];
	};

	class SK_API RenderSceneObjects
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(RenderSceneObjects);
		RenderSceneObjects();
		~RenderSceneObjects();

		RenderableObject CreateRenderable();
		void             DestroyRenderable(RenderableObject obj);

		void SetTransform(RenderableObject obj, const Mat4& transform);
		Mat4 GetTransform(RenderableObject obj) const;

		void SetMesh(RenderableObject obj, RID mesh);
		RID  GetMesh(RenderableObject obj) const;

		void                 SetMeshCache(RenderableObject obj, MeshResourceCachePtr meshCache);
		MeshResourceCachePtr GetMeshCache(RenderableObject obj) const;

		void      SetMaterials(RenderableObject obj, Span<RID> materials);
		Span<RID> GetMaterials(RenderableObject obj) const;

		void SetShader(RenderableObject obj, RID shader);
		RID  GetShader(RenderableObject obj) const;

		void SetCastShadows(RenderableObject obj, bool castShadows);
		bool GetCastShadows(RenderableObject obj) const;

		void SetUserData(RenderableObject obj, u64 userData);
		u64  GetUserData(RenderableObject obj) const;

		void SetVisible(RenderableObject obj, bool visible);
		bool GetVisible(RenderableObject obj) const;

		void SetLayerMask(RenderableObject obj, u64 layerMask);
		u64  GetLayerMask(RenderableObject obj) const;

		void              SetBonesDescriptor(RenderableObject obj, GPUDescriptorSet* bones);
		GPUDescriptorSet* GetBonesDescriptor(RenderableObject obj) const;
		void              SetBonesBuffer(RenderableObject obj, GPUBuffer* bonesBuffer);
		GPUBuffer*        GetBonesBuffer(RenderableObject obj) const;
		void              UpdateSkinnedBones(RenderableObject obj, GPUBuffer* currentBones, GPUBuffer* previousBones);

		AABB GetAABB(RenderableObject obj) const;

		using ForEachDrawcallFn = void(*)(u32 pipelineIndex, const Drawcall& drawcall, void* userData);

		void ForEachVisibleDrawcallRef(RenderableObject obj, ForEachDrawcallFn fn, void* userData) const;

		template<typename Fn>
		SK_FINLINE void ForEachVisibleDrawcallRef(RenderableObject obj, Fn&& fn) const
		{
			using FnType = Traits::RemoveReference<Fn>;
			ForEachVisibleDrawcallRef(obj,
				[](u32 pipelineIndex, const Drawcall& drawcall, void* userData)
				{
					(*static_cast<FnType*>(userData))(pipelineIndex, drawcall);
				},
				static_cast<void*>(&fn));
		}

		void Begin(GPUCommandBuffer* cmd);
		void End(GPUCommandBuffer* cmd);

		Array<DrawPipeline> opaquePipelines;
		Array<DrawPipeline> transparentPipelines;
		Array<DrawPipeline> shadowPipelines;
		Array<MovedRenderableObject> movedRenderables;

		GPUBuffer* instanceDataBuffer = nullptr;
		u32        instanceDataCount = 0;

		struct InstanceOwner
		{
			RenderableObjectStorage* renderable = nullptr;
			u32                      primitiveIndex = 0;
		};
		Array<InstanceOwner> instanceOwners;

		GPUTopLevelAS*       tlas = nullptr;
		GPUTopLevelAS*       GetTLAS() const { return tlas; }
		Array<InstanceDesc>  instances;
		Array<InstanceOwner> instanceDescOwners;

		GPUDescriptorSet* GetSkinningDescriptorSet() const { return skinningDescriptorSet; }
		GPUDescriptorSet* GetPreviousSkinningDescriptorSet() const { return previousSkinningDescriptorSet; }

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

		bool asyncLoad = true;
		bool requireTlas = true;
		bool bindEvents = true;

	private:
		void OnBeginRecord(GPUCommandBuffer* cmd);
		void OnEndRecord(GPUCommandBuffer* cmd);

		HashSet<RenderableObjectStorage*>  renderables;
		DenseSet<RenderableObjectStorage*> pendingUpdate;
		DenseSet<RenderableObjectStorage*> pendingBlas;
		DenseSet<u32>                      dirtyInstances;

		GPUBuffer* tlasScratchBuffer = nullptr;
		u32        tlasMaxInstances = 0;
		bool       tlasFastBuild = false;

		GPUBuffer* skinnedBlasScratchBuffer = nullptr;
		u64        skinnedBlasScratchSize = 0;

		GPUDescriptorSet* skinningDescriptorSet = nullptr;
		GPUDescriptorSet* previousSkinningDescriptorSet = nullptr;
		GPUBuffer*        fallbackBoneBuffer = nullptr;
		Array<GPUBuffer*> boneBuffers;
		Array<u32>        freeBoneBufferSlots;

		bool       tlasTopologyDirty = false;
		bool       tlasTransformsDirty = false;

		u64        meshReloadVersion = 0;

		static u32 GetOrCreatePipeline(Array<DrawPipeline>& pipelines, const DrawPipelineDesc& desc);

		void TrackMovedRenderable(RenderableObjectStorage* obj, const Mat4& previousTransform, const Mat4& transform);
		void TrackMovedRenderableBones(RenderableObjectStorage* obj);
		void RemoveMovedRenderable(RenderableObjectStorage* obj);
		void MarkDirty(RenderableObjectStorage* obj);
		void MarkInstanceDirty(u32 instanceDescIndex);
		bool TryRebuild(RenderableObjectStorage* obj);
		void ClearDrawcalls(RenderableObjectStorage* obj);
		void UpdateAABB(RenderableObjectStorage* obj);
		void RefreshMeshCache(RenderableObjectStorage* obj);
		void RefreshMaterialsCache(RenderableObjectStorage* obj);
		MaterialResourceCachePtr GetMaterialCache(RenderableObjectStorage* obj, u32 materialIndex) const;
		void CreatePrimitiveDrawcall(RenderableObjectStorage* obj, u32 primitiveIndex, const MaterialResourceCachePtr& material);
		void EnrollBlasInstance(RenderableObjectStorage* obj, u32 primitiveIndex);
		void RemoveDrawcall(const DrawcallRef& ref);
		bool EnsureSkinnedRayTracingResources(RenderableObjectStorage* obj);
		void DestroySkinnedRayTracingResources(RenderableObjectStorage* obj);
		void EnsureSkinnedBlasScratchBuffer(u64 requiredSize);
		void UpdateSkinnedRayTracing(GPUCommandBuffer* cmd);
		bool HasSkinnedTlasInstances() const;
		u32  AcquireBoneBufferSlot(GPUBuffer* bonesBuffer);
		void ReleaseBoneBufferSlot(u32 slot);
		void UpdateBoneBufferSlot(u32 slot, GPUBuffer* bonesBuffer);
		void WriteBoneSlot(GPUDescriptorSet* set, u32 slot, GPUBuffer* bonesBuffer);
		void UpdateRenderableBoneSlot(RenderableObjectStorage* obj);
	};

}
