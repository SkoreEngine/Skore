#include "Skore/Graphics/RenderSceneObjects.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Profiler.hpp"

namespace Skore
{

	struct RenderableObjectStorage
	{
		SK_NO_COPY_CONSTRUCTOR(RenderableObjectStorage);
		RenderableObjectStorage(RenderSceneObjects* objects) : renderSceneObjects(objects) {}

		RenderSceneObjects* renderSceneObjects;

		RID                  mesh;
		Array<RID>           materials;
		bool                 castShadows = true;
		Mat4                 transform = Mat4(1.0);
		u64                  userData = 0;
		bool                 visible = true;
		u64                  layerMask = 1ULL;
		GPUDescriptorSet*    bonesDescriptor = nullptr;

		MeshResourceCachePtr            meshCache;
		Array<MaterialResourceCachePtr> overrideMaterialsCache;

		Array<DrawcallRef> references;
		AABB               aabb = {};

		bool meshDirty = false;
		bool materialsDirty = false;
		bool meshCacheExplicit = false;
	};

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
		for (RenderableObjectStorage* obj : renderables)
		{
			DestroyAndFree(obj);
		}
		renderables.Clear();
		pendingUpdate.clear();
		pendingBlas.clear();

		if (instanceDataBuffer)
		{
			instanceDataBuffer->Destroy();
		}
		if (tlas) tlas->Destroy();
		if (tlasScratchBuffer) tlasScratchBuffer->Destroy();
	}

	RenderableObject RenderSceneObjects::CreateRenderable()
	{
		RenderableObjectStorage* obj = Alloc<RenderableObjectStorage>(this);
		renderables.Insert(obj);
		return RenderableObject{obj};
	}

	void RenderSceneObjects::DestroyRenderable(RenderableObject obj)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		ClearDrawcalls(o);
		pendingUpdate.erase(o);
		pendingBlas.erase(o);
		renderables.Erase(o);
		DestroyAndFree(o);
	}

	void RenderSceneObjects::SetTransform(RenderableObject obj, const Mat4& transform)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();

		o->transform = transform;
		UpdateAABB(o);

		for (DrawcallRef& ref : o->references)
		{
			if (ref.pipelineIndex == U32_MAX) continue;

			Array<DrawPipeline>& storage = ref.transparent ? transparentPipelines : opaquePipelines;
			Drawcall& dc = storage[ref.pipelineIndex].drawcalls[ref.handle];
			dc.transform = transform;
			dc.aabb = Math::TransformAABB(dc.localAabb, transform);

			if (ref.shadowPipelineIndex != U32_MAX && ref.shadowHandle != U64_MAX)
			{
				Drawcall& sdc = shadowPipelines[ref.shadowPipelineIndex].drawcalls[ref.shadowHandle];
				sdc.transform = transform;
				sdc.aabb = dc.aabb;
			}

			if (ref.instanceIndex != U32_MAX)
			{
				InstanceData* data = static_cast<InstanceData*>(instanceDataBuffer->GetMappedData());
				InstanceData& inst = data[ref.instanceIndex];
				inst.transform = transform;
				inst.aabbMin = dc.aabb.min;
				inst.aabbMax = dc.aabb.max;
			}

			if (ref.instanceDescIndex != U32_MAX)
			{
				instances[ref.instanceDescIndex].transform = transform;
				tlasTransformsDirty = true;
			}
		}
	}

	Mat4 RenderSceneObjects::GetTransform(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->transform : Mat4(1.0);
	}

	void RenderSceneObjects::SetMesh(RenderableObject obj, RID mesh)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		if (!o->meshCacheExplicit && o->mesh == mesh) return;
		o->mesh = mesh;
		o->meshCacheExplicit = false;
		o->meshDirty = true;
		MarkDirty(o);
	}

	RID RenderSceneObjects::GetMesh(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->mesh : RID{};
	}

	void RenderSceneObjects::SetMeshCache(RenderableObject obj, MeshResourceCachePtr meshCache)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		o->mesh = RID{};
		o->meshCache = std::move(meshCache);
		o->meshCacheExplicit = o->meshCache != nullptr;
		o->meshDirty = true;
		MarkDirty(o);
	}

	MeshResourceCachePtr RenderSceneObjects::GetMeshCache(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->meshCache : nullptr;
	}

	void RenderSceneObjects::SetMaterials(RenderableObject obj, Span<RID> materials)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();

		if (o->materials.Size() == materials.Size())
		{
			bool same = true;
			for (usize i = 0; i < o->materials.Size(); ++i)
			{
				if (o->materials[i] != materials[i]) { same = false; break; }
			}
			if (same) return;
		}
		o->materials.Clear();
		o->materials.Reserve(materials.Size());
		for (RID rid : materials) o->materials.EmplaceBack(rid);
		o->materialsDirty = true;
		MarkDirty(o);
	}

	Span<RID> RenderSceneObjects::GetMaterials(RenderableObject obj) const
	{
		return obj ? Span<RID>{obj.ToPtr<RenderableObjectStorage>()->materials} : Span<RID>{};
	}

	void RenderSceneObjects::SetCastShadows(RenderableObject obj, bool castShadows)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		if (o->castShadows == castShadows) return;
		o->castShadows = castShadows;
		MarkDirty(o);
	}

	bool RenderSceneObjects::GetCastShadows(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->castShadows : false;
	}

	void RenderSceneObjects::SetUserData(RenderableObject obj, u64 userData)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		o->userData = userData;
		for (const DrawcallRef& ref : o->references)
		{
			if (ref.pipelineIndex != U32_MAX)
			{
				Array<DrawPipeline>& storage = ref.transparent ? transparentPipelines : opaquePipelines;
				storage[ref.pipelineIndex].drawcalls[ref.handle].userData = userData;
			}
			if (ref.shadowPipelineIndex != U32_MAX && ref.shadowHandle != U64_MAX)
			{
				shadowPipelines[ref.shadowPipelineIndex].drawcalls[ref.shadowHandle].userData = userData;
			}
		}
	}

	u64 RenderSceneObjects::GetUserData(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->userData : 0;
	}

	void RenderSceneObjects::SetVisible(RenderableObject obj, bool visible)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		if (o->visible == visible) return;
		o->visible = visible;
		MarkDirty(o);
	}

	bool RenderSceneObjects::GetVisible(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->visible : false;
	}

	void RenderSceneObjects::SetLayerMask(RenderableObject obj, u64 layerMask)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		if (o->layerMask == layerMask) return;
		o->layerMask = layerMask;
		for (const DrawcallRef& ref : o->references)
		{
			if (ref.pipelineIndex != U32_MAX)
			{
				Array<DrawPipeline>& storage = ref.transparent ? transparentPipelines : opaquePipelines;
				storage[ref.pipelineIndex].drawcalls[ref.handle].layerMask = layerMask;
			}
			if (ref.shadowPipelineIndex != U32_MAX && ref.shadowHandle != U64_MAX)
			{
				shadowPipelines[ref.shadowPipelineIndex].drawcalls[ref.shadowHandle].layerMask = layerMask;
			}
			if (ref.instanceIndex != U32_MAX)
			{
				InstanceData* data = static_cast<InstanceData*>(instanceDataBuffer->GetMappedData());
				data[ref.instanceIndex].layerMask = layerMask;
			}
		}
	}

	u64 RenderSceneObjects::GetLayerMask(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->layerMask : 0;
	}

	void RenderSceneObjects::SetBonesDescriptor(RenderableObject obj, GPUDescriptorSet* bones)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		if (o->bonesDescriptor == bones) return;
		o->bonesDescriptor = bones;
		MarkDirty(o);
	}

	GPUDescriptorSet* RenderSceneObjects::GetBonesDescriptor(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->bonesDescriptor : nullptr;
	}

	AABB RenderSceneObjects::GetAABB(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->aabb : AABB();
	}

	void RenderSceneObjects::ForEachVisibleDrawcallRef(RenderableObject obj, ForEachDrawcallFn fn, void* userData) const
	{
		if (!obj) return;
		const RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		const u32 transparentOffset = static_cast<u32>(opaquePipelines.Size());
		for (const DrawcallRef& ref : o->references)
		{
			if (ref.pipelineIndex == U32_MAX) continue;
			if (!ref.transparent)
			{
				fn(ref.pipelineIndex, opaquePipelines[ref.pipelineIndex].drawcalls[ref.handle], userData);
			}
			else
			{
				fn(transparentOffset + ref.pipelineIndex, transparentPipelines[ref.pipelineIndex].drawcalls[ref.handle], userData);
			}
		}
	}

	void RenderSceneObjects::MarkDirty(RenderableObjectStorage* obj)
	{
		pendingUpdate.insert(obj);
	}

	void RenderSceneObjects::ClearDrawcalls(RenderableObjectStorage* obj)
	{
		for (const DrawcallRef& ref : obj->references)
		{
			RemoveDrawcall(ref);
		}
		obj->references.Clear();
	}

	void RenderSceneObjects::UpdateAABB(RenderableObjectStorage* obj)
	{
		obj->aabb = AABB();
		if (obj->meshCache)
		{
			obj->aabb = Math::TransformAABB(obj->meshCache->aabb, obj->transform);
		}
	}

	void RenderSceneObjects::RefreshMeshCache(RenderableObjectStorage* obj)
	{
		if (!obj->meshDirty) return;
		obj->meshDirty = false;
		if (obj->meshCacheExplicit) return;
		obj->meshCache = obj->mesh ? RenderResourceCache::GetMeshCache(obj->mesh, asyncLoad) : nullptr;
	}

	void RenderSceneObjects::RefreshMaterialsCache(RenderableObjectStorage* obj)
	{
		if (!obj->materialsDirty) return;
		obj->materialsDirty = false;
		obj->overrideMaterialsCache.Clear();
		obj->overrideMaterialsCache.Resize(obj->materials.Size());
		for (usize i = 0; i < obj->materials.Size(); ++i)
		{
			if (obj->materials[i])
			{
				obj->overrideMaterialsCache[i] = RenderResourceCache::GetMaterialCache(obj->materials[i], asyncLoad);
			}
		}
	}

	MaterialResourceCachePtr RenderSceneObjects::GetMaterialCache(RenderableObjectStorage* obj, u32 materialIndex) const
	{
		if (materialIndex < obj->overrideMaterialsCache.Size() && obj->overrideMaterialsCache[materialIndex])
		{
			return obj->overrideMaterialsCache[materialIndex];
		}
		if (obj->meshCache && materialIndex < obj->meshCache->materials.Size())
		{
			return obj->meshCache->materials[materialIndex];
		}
		return nullptr;
	}

	bool RenderSceneObjects::TryRebuild(RenderableObjectStorage* obj)
	{
		RefreshMeshCache(obj);
		RefreshMaterialsCache(obj);

		ClearDrawcalls(obj);
		// Reset BLAS-pickup tracking; the rebuild below decides whether this renderable still
		// needs to be polled for a BLAS that hasn't landed yet.
		pendingBlas.erase(obj);

		if (!obj->visible || !obj->meshCache)
		{
			UpdateAABB(obj);
			return true;
		}

		// Wait for the mesh to finish uploading before we can stream drawcalls.
		if (!obj->meshCache->IsLoaded()) return false;

		// Wait for any override material that is still loading.
		for (const MaterialResourceCachePtr& mat : obj->overrideMaterialsCache)
		{
			if (mat && !mat->IsLoaded()) return false;
		}

		// Wait for mesh-default materials referenced by primitives that have no override.
		for (u32 p = 0; p < obj->meshCache->primitives.Size(); ++p)
		{
			const MeshPrimitive& primitive = obj->meshCache->primitives[p];
			MaterialResourceCachePtr mat = GetMaterialCache(obj, primitive.materialIndex);
			if (mat && !mat->IsLoaded()) return false;
		}

		UpdateAABB(obj);

		obj->references.Resize(obj->meshCache->primitives.Size());

		for (u32 p = 0; p < obj->meshCache->primitives.Size(); ++p)
		{
			const MeshPrimitive& primitive = obj->meshCache->primitives[p];
			MaterialResourceCachePtr material = GetMaterialCache(obj, primitive.materialIndex);
			if (!material || material->materialIndex == U32_MAX)
			{
				continue;
			}
			CreatePrimitiveDrawcall(obj, p, material);
		}

		// Mesh is rendered but BLAS isn't ready yet — queue for deferred TLAS enrollment.
		if (obj->meshCache->wantsBlas && !obj->meshCache->IsBlasReady())
		{
			pendingBlas.insert(obj);
		}

		return true;
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

	void RenderSceneObjects::CreatePrimitiveDrawcall(RenderableObjectStorage* obj, u32 primitiveIndex, const MaterialResourceCachePtr& material)
	{
		const MeshPrimitive& primitive = obj->meshCache->primitives[primitiveIndex];

		DrawcallRef& ref = obj->references[primitiveIndex];
		ref = DrawcallRef{};
		ref.transparent = material->transparent;

		const u32 vertexByteOffset = obj->meshCache->vertexByteOffset;
		const u32 indexByteOffset  = obj->meshCache->indexByteOffset;
		const u32 vertexLayoutIndex = obj->meshCache->vertexLayoutId;

		bool frontFace = Determinant(Mat34(obj->transform)) < 0.0f;
		CullMode cullMode = !frontFace ? CullMode::Back : CullMode::Front;
		bool hasBones = obj->bonesDescriptor != nullptr;

		DrawPipelineDesc pipelineDesc;
		pipelineDesc.cullMode = cullMode;
		pipelineDesc.hasBones = hasBones;
		pipelineDesc.shader = material->shader;

		Array<DrawPipeline>& pipelineStorage = ref.transparent ? transparentPipelines : opaquePipelines;
		ref.pipelineIndex = GetOrCreatePipeline(pipelineStorage, pipelineDesc);
		ref.handle = pipelineStorage[ref.pipelineIndex].drawcalls.Insert();

		Drawcall& dc = pipelineStorage[ref.pipelineIndex].drawcalls[ref.handle];
		dc.firstIndex = primitive.firstIndex;
		dc.indexCount = primitive.indexCount;
		dc.vertexByteOffset = vertexByteOffset;
		dc.indexByteOffset = indexByteOffset;
		dc.mesh = obj->meshCache;
		dc.material = material;
		dc.userData = obj->userData;
		dc.vertexLayoutIndex = vertexLayoutIndex;
		dc.bones = obj->bonesDescriptor;
		dc.localAabb = primitive.aabb;
		dc.aabb = Math::TransformAABB(primitive.aabb, obj->transform);
		dc.transform = obj->transform;
		dc.layerMask = obj->layerMask;

		// Shadow rendering is being moved to GPU-driven indirect draws (compute-cull + indirect).
		// The shadow pipeline list still needs to be populated so the shadow-cull pass and the
		// shadow draw pass can build pipelines per (cullMode, hasBones) bucket, but the per-primitive
		// shadow Drawcalls are no longer needed on the CPU. Flip the flag to bring the old path back.
		constexpr bool kInsertShadowDrawcalls = false;

		const bool castShadow = obj->castShadows && !ref.transparent;
		if (castShadow)
		{
			DrawPipelineDesc shadowDesc;
			shadowDesc.cullMode = CullMode::Front;
			shadowDesc.hasBones = hasBones;

			ref.shadowPipelineIndex = GetOrCreatePipeline(shadowPipelines, shadowDesc);

			if (kInsertShadowDrawcalls)
			{
				ref.shadowHandle = shadowPipelines[ref.shadowPipelineIndex].drawcalls.Insert();

				Drawcall& sdc = shadowPipelines[ref.shadowPipelineIndex].drawcalls[ref.shadowHandle];
				sdc.firstIndex = primitive.firstIndex;
				sdc.indexCount = primitive.indexCount;
				sdc.vertexByteOffset = vertexByteOffset;
				sdc.indexByteOffset = indexByteOffset;
				sdc.material = material;
				sdc.userData = obj->userData;
				sdc.vertexLayoutIndex = vertexLayoutIndex;
				sdc.bones = obj->bonesDescriptor;
				sdc.localAabb = primitive.aabb;
				sdc.aabb = dc.aabb;
				sdc.transform = obj->transform;
				sdc.layerMask = obj->layerMask;
			}
		}

		ref.instanceIndex = instanceDataCount++;
		instanceOwners.EmplaceBack(InstanceOwner{obj, primitiveIndex});

		if (instanceDataBuffer->GetDesc().size < sizeof(InstanceData) * instanceDataCount)
		{
			GPUBuffer* newBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = instanceDataCount * sizeof(InstanceData) * 2,
				.usage = ResourceUsage::UnorderedAccess,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "InstanceBuffer"
			});

			memcpy(newBuffer->GetMappedData(), instanceDataBuffer->GetMappedData(), instanceDataCount * sizeof(InstanceData));
			instanceDataBuffer->Destroy();
			instanceDataBuffer = newBuffer;
		}

		u32 primitiveInfoIndex = primitiveIndex < obj->meshCache->primitiveInfoSlots.Size()
			? obj->meshCache->primitiveInfoSlots[primitiveIndex]
			: U32_MAX;

		InstanceData* data = static_cast<InstanceData*>(instanceDataBuffer->GetMappedData());
		new(data + ref.instanceIndex) InstanceData{
			.transform = obj->transform,
			.materialIndex = material->materialIndex,
			.vertexByteOffset = vertexByteOffset,
			.vertexLayoutIndex = vertexLayoutIndex,
			.primitiveInfoIndex = primitiveInfoIndex,
			.aabbMin = dc.aabb.min,
			.pad0 = 0,
			.aabbMax = dc.aabb.max,
			.pipelineIndex = ref.pipelineIndex,
			.drawcallIndex = static_cast<u32>(ref.handle),
			.transparent = ref.transparent,
			.layerMask = obj->layerMask,
			.shadowPipelineIndex = castShadow ? ref.shadowPipelineIndex : U32_MAX,
		};

		EnrollBlasInstance(obj, primitiveIndex);
	}

	void RenderSceneObjects::EnrollBlasInstance(RenderableObjectStorage* obj, u32 primitiveIndex)
	{
		if (primitiveIndex >= obj->references.Size()) return;
		DrawcallRef& ref = obj->references[primitiveIndex];

		// Already enrolled, or there's no drawcall (e.g., missing material) — nothing to do.
		if (ref.instanceDescIndex != U32_MAX) return;
		if (ref.pipelineIndex == U32_MAX) return;
		if (ref.transparent) return;
		if (obj->bonesDescriptor != nullptr) return;

		if (!obj->meshCache || primitiveIndex >= obj->meshCache->blasArray.Size()) return;
		GPUBottomLevelAS* blas = obj->meshCache->blasArray[primitiveIndex];
		if (!blas) return;

		ref.instanceDescIndex = static_cast<u32>(instances.Size());
		instances.EmplaceBack(InstanceDesc{
			.bottomLevelAS = blas,
			.transform = obj->transform,
			.instanceID = ref.instanceIndex,
			.forceOpaque = true,
		});
		instanceDescOwners.EmplaceBack(InstanceOwner{obj, primitiveIndex});
		tlasTopologyDirty = true;
	}

	void RenderSceneObjects::RemoveDrawcall(const DrawcallRef& ref)
	{
		if (ref.pipelineIndex != U32_MAX)
		{
			Array<DrawPipeline>& storage = ref.transparent ? transparentPipelines : opaquePipelines;
			storage[ref.pipelineIndex].drawcalls.Remove(ref.handle);
		}
		if (ref.shadowPipelineIndex != U32_MAX && ref.shadowHandle != U64_MAX)
		{
			shadowPipelines[ref.shadowPipelineIndex].drawcalls.Remove(ref.shadowHandle);
		}
		if (ref.instanceIndex != U32_MAX)
		{
			u32 last = instanceDataCount - 1;
			if (ref.instanceIndex != last)
			{
				InstanceData* data = static_cast<InstanceData*>(instanceDataBuffer->GetMappedData());
				data[ref.instanceIndex] = data[last];

				instanceOwners[ref.instanceIndex] = instanceOwners[last];
				InstanceOwner& moved = instanceOwners[ref.instanceIndex];
				DrawcallRef& movedRef = moved.renderable->references[moved.primitiveIndex];
				movedRef.instanceIndex = ref.instanceIndex;
				if (movedRef.instanceDescIndex != U32_MAX)
				{
					instances[movedRef.instanceDescIndex].instanceID = ref.instanceIndex;
					tlasTopologyDirty = true;
				}
			}
			instanceOwners.PopBack();
			instanceDataCount--;
		}
		if (ref.instanceDescIndex != U32_MAX)
		{
			u32 last = static_cast<u32>(instances.Size()) - 1;
			if (ref.instanceDescIndex != last)
			{
				instances[ref.instanceDescIndex] = instances[last];
				instanceDescOwners[ref.instanceDescIndex] = instanceDescOwners[last];
				InstanceOwner& moved = instanceDescOwners[ref.instanceDescIndex];
				moved.renderable->references[moved.primitiveIndex].instanceDescIndex = ref.instanceDescIndex;
			}
			instances.PopBack();
			instanceDescOwners.PopBack();
			tlasTopologyDirty = true;
		}
	}

	void RenderSceneObjects::DoUpdate(GPUCommandBuffer* cmd)
	{
		SK_SCOPED_CPU_ZONE("RenderSceneObjects::DoUpdate");

		if (!pendingUpdate.empty())
		{
			Array<RenderableObjectStorage*> done;
			done.Reserve(pendingUpdate.size());

			for (RenderableObjectStorage* obj : pendingUpdate)
			{
				if (TryRebuild(obj))
				{
					done.EmplaceBack(obj);
				}
			}
			for (RenderableObjectStorage* obj : done)
			{
				pendingUpdate.erase(obj);
			}
		}

		// Pick up any meshes whose BLAS finished building since we last looked. The drawcalls
		// already exist — we just enroll the TLAS instance for each primitive that has a BLAS.
		if (!pendingBlas.empty())
		{
			Array<RenderableObjectStorage*> blasDone;
			blasDone.Reserve(pendingBlas.size());

			for (RenderableObjectStorage* obj : pendingBlas)
			{
				if (!obj->meshCache || !obj->meshCache->IsBlasReady()) continue;

				for (u32 p = 0; p < obj->references.Size(); ++p)
				{
					EnrollBlasInstance(obj, p);
				}
				blasDone.EmplaceBack(obj);
			}
			for (RenderableObjectStorage* obj : blasDone)
			{
				pendingBlas.erase(obj);
			}
		}

		const bool tlasDirty = tlasTopologyDirty || tlasTransformsDirty;
		if (tlasDirty && Graphics::GetDevice()->GetFeatures().rayTracing)
		{
			if (!instances.Empty())
			{
				SK_SCOPED_ZONE("RenderSceneObjects - TLAS Update", cmd);

				const bool needsRebuild = tlasTopologyDirty || tlas == nullptr || instances.Size() > tlasMaxInstances;

				if (needsRebuild && (tlas == nullptr || instances.Size() > tlasMaxInstances))
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
					tlasDesc.flags = BuildAccelerationStructureFlags::PreferFastTrace | BuildAccelerationStructureFlags::AllowUpdate;
					tlasDesc.debugName = "SceneTLAS";

					tlas = Graphics::CreateTopLevelAS(tlasDesc);

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

				tlas->UpdateInstances(instances);

				// Refit only when topology is unchanged and we already have a valid TLAS to update.
				const bool update = !needsRebuild;
				cmd->BuildTopLevelAS(tlas, AccelerationStructureBuildInfo{.update = update, .scratchBuffer = tlasScratchBuffer});
			}

			tlasTopologyDirty = false;
			tlasTransformsDirty = false;
		}
	}
}
