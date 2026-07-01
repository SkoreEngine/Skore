#include "Skore/Graphics/RenderSceneObjects.hpp"

#include <algorithm>

#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		constexpr u32 SkinnedBlasVertexStride = sizeof(Vec3);

		GPUPipeline* skinnedBlasSkinningPipeline = nullptr;

		struct SkinnedBlasSkinningPushConstants
		{
			u32 vertexByteOffset = 0;
			u32 vertexLayoutIndex = 0;
			u32 vertexCount = 0;
			u32 pad = 0;
		};

		GPUPipeline* GetSkinnedBlasSkinningPipeline()
		{
			if (!skinnedBlasSkinningPipeline)
			{
				skinnedBlasSkinningPipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
					.shader = Resources::FindByPath("Skore://Shaders/SkinnedBlasSkinning.comp"),
					.allowImmediateSet = true,
					.debugName = "SkinnedBlasSkinningPipeline",
					.descriptorSetsOverride = {
						DescriptorSetOverride{
							.set = 0,
							.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
						}
					}
				});
			}
			return skinnedBlasSkinningPipeline;
		}
	}

	struct RenderableObjectStorage
	{
		SK_NO_COPY_CONSTRUCTOR(RenderableObjectStorage);
		RenderableObjectStorage(RenderSceneObjects* objects) : renderSceneObjects(objects) {}

		RenderSceneObjects* renderSceneObjects;

		RID                  mesh;
		Array<RID>           materials;
		RID                  shader;
		u32                  rayHitGroup = 0;
		bool                 castShadows = true;
		Mat4                 transform = Mat4(1.0);
		u64                  userData = 0;
		bool                 visible = true;
		u64                  layerMask = 1ULL;
		GPUDescriptorSet*    bonesDescriptor = nullptr;
		GPUBuffer*           bonesBuffer = nullptr;
		u32                  boneBufferSlot = U32_MAX;
		u32                  movedRenderableIndex = U32_MAX;

		MeshResourceCachePtr            meshCache;
		Array<MaterialResourceCachePtr> overrideMaterialsCache;

		Array<DrawcallRef> references;
		AABB               aabb = {};

		GPUBuffer*               skinnedRayTracingVertexBuffer = nullptr;
		Array<GPUBottomLevelAS*> skinnedRayTracingBlas;
		u32                      skinnedRayTracingVertexCount = 0;
		bool                     skinnedRayTracingBuilt = false;

		bool meshDirty = false;
		bool materialsDirty = false;
		bool meshCacheExplicit = false;
	};

	void RenderSceneObjectsInit()
	{
	}

	void RenderSceneObjectsShutdown()
	{
		if (skinnedBlasSkinningPipeline)
		{
			skinnedBlasSkinningPipeline->Destroy();
			skinnedBlasSkinningPipeline = nullptr;
		}
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

		skinningDescriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
			.bindings = {
				DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = DescriptorType::StorageBuffer,
					.renderType = RenderType::RuntimeArray,
					.shaderStage = ShaderStage::Vertex
				},
			},
			.debugName = "SceneSkinningDescriptorSet"
		});

		previousSkinningDescriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
			.bindings = {
				DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = DescriptorType::StorageBuffer,
					.renderType = RenderType::RuntimeArray,
					.shaderStage = ShaderStage::Vertex
				},
			},
			.debugName = "ScenePreviousSkinningDescriptorSet"
		});

		fallbackBoneBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = sizeof(Mat4) * MaxBones,
			.usage = ResourceUsage::ShaderResource,
			.hostVisible = true,
			.persistentMapped = true,
			.debugName = "SceneFallbackBoneBuffer"
		});

		Mat4* bones = static_cast<Mat4*>(fallbackBoneBuffer->GetMappedData());
		for (u32 i = 0; i < MaxBones; ++i)
		{
			new(bones + i) Mat4{Mat4(1.0)};
		}

		Event::Bind<OnBeginRecordRenderCommands, &RenderSceneObjects::OnBeginRecord>(this);
		Event::Bind<OnEndRecordRenderCommands, &RenderSceneObjects::OnEndRecord>(this);
	}

	RenderSceneObjects::~RenderSceneObjects()
	{
		Event::Unbind<OnBeginRecordRenderCommands, &RenderSceneObjects::OnBeginRecord>(this);
		Event::Unbind<OnEndRecordRenderCommands, &RenderSceneObjects::OnEndRecord>(this);

		for (RenderableObjectStorage* obj : renderables)
		{
			DestroySkinnedRayTracingResources(obj);
			DestroyAndFree(obj);
		}
		renderables.Clear();
		pendingUpdate.clear();
		pendingBlas.clear();
		dirtyInstances.clear();

		if (instanceDataBuffer)
		{
			instanceDataBuffer->Destroy();
		}
		if (skinningDescriptorSet) skinningDescriptorSet->Destroy();
		if (previousSkinningDescriptorSet) previousSkinningDescriptorSet->Destroy();
		if (fallbackBoneBuffer) fallbackBoneBuffer->Destroy();
		if (tlas) tlas->Destroy();
		if (tlasScratchBuffer) tlasScratchBuffer->Destroy();
		if (skinnedBlasScratchBuffer) skinnedBlasScratchBuffer->Destroy();
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
		RemoveMovedRenderable(o);
		ClearDrawcalls(o);
		ReleaseBoneBufferSlot(o->boneBufferSlot);
		o->boneBufferSlot = U32_MAX;
		o->bonesBuffer = nullptr;
		pendingUpdate.erase(o);
		pendingBlas.erase(o);
		renderables.Erase(o);
		DestroyAndFree(o);
	}

	void RenderSceneObjects::SetTransform(RenderableObject obj, const Mat4& transform)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		if (o->transform == transform) return;

		const Mat4 previousTransform = o->transform;
		TrackMovedRenderable(o, previousTransform, transform);

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
				MarkInstanceDirty(ref.instanceDescIndex);
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

	void RenderSceneObjects::SetShader(RenderableObject obj, RID shader)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		if (o->shader == shader) return;
		o->shader = shader;
		o->rayHitGroup = ShaderResource::GetRayHitGroup(shader);
		MarkDirty(o);
	}

	RID RenderSceneObjects::GetShader(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->shader : RID{};
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

	void RenderSceneObjects::SetBonesBuffer(RenderableObject obj, GPUBuffer* bonesBuffer)
	{
		if (!obj) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();
		if (o->bonesBuffer == bonesBuffer) return;

		o->bonesBuffer = bonesBuffer;
		UpdateRenderableBoneSlot(o);
		MarkDirty(o);
	}

	GPUBuffer* RenderSceneObjects::GetBonesBuffer(RenderableObject obj) const
	{
		return obj ? obj.ToPtr<RenderableObjectStorage>()->bonesBuffer : nullptr;
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

	void RenderSceneObjects::TrackMovedRenderable(RenderableObjectStorage* obj, const Mat4& previousTransform, const Mat4& transform)
	{
		if (!obj || previousTransform == transform || obj->references.Empty()) return;

		if (obj->movedRenderableIndex != U32_MAX)
		{
			MovedRenderableObject& moved = movedRenderables[obj->movedRenderableIndex];
			if (moved.previousTransform == transform)
			{
				RemoveMovedRenderable(obj);
			}
			return;
		}

		obj->movedRenderableIndex = static_cast<u32>(movedRenderables.Size());
		movedRenderables.EmplaceBack(MovedRenderableObject{
			.object = RenderableObject{obj},
			.previousTransform = previousTransform,
		});
	}

	void RenderSceneObjects::TrackMovedRenderableBones(RenderableObjectStorage* obj)
	{
		if (!obj || obj->references.Empty()) return;

		if (obj->movedRenderableIndex != U32_MAX)
		{
			movedRenderables[obj->movedRenderableIndex].bonesChanged = true;
			return;
		}

		obj->movedRenderableIndex = static_cast<u32>(movedRenderables.Size());
		movedRenderables.EmplaceBack(MovedRenderableObject{
			.object = RenderableObject{obj},
			.previousTransform = obj->transform,
			.bonesChanged = true,
		});
	}

	void RenderSceneObjects::RemoveMovedRenderable(RenderableObjectStorage* obj)
	{
		if (!obj) return;

		const u32 index = obj->movedRenderableIndex;
		if (index == U32_MAX)
		{
			return;
		}

		obj->movedRenderableIndex = U32_MAX;
		if (index >= movedRenderables.Size())
		{
			return;
		}

		const u32 last = static_cast<u32>(movedRenderables.Size() - 1);
		if (index != last)
		{
			movedRenderables[index] = movedRenderables[last];
			RenderableObjectStorage* movedObj = movedRenderables[index].object.ToPtr<RenderableObjectStorage>();
			movedObj->movedRenderableIndex = index;
		}
		movedRenderables.PopBack();
	}

	void RenderSceneObjects::MarkDirty(RenderableObjectStorage* obj)
	{
		pendingUpdate.insert(obj);
	}

	void RenderSceneObjects::MarkInstanceDirty(u32 instanceDescIndex)
	{
		dirtyInstances.insert(instanceDescIndex);
	}

	void RenderSceneObjects::ClearDrawcalls(RenderableObjectStorage* obj)
	{
		for (const DrawcallRef& ref : obj->references)
		{
			RemoveDrawcall(ref);
		}
		obj->references.Clear();
		DestroySkinnedRayTracingResources(obj);
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

		UpdateAABB(obj);

		if (!obj->visible || !obj->meshCache)
		{
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
		if (requireTlas &&  obj->meshCache->wantsBlas && !obj->meshCache->IsBlasReady())
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
		ref.masked = material->masked;

		const u32 vertexByteOffset = obj->meshCache->vertexByteOffset;
		const u32 indexByteOffset  = obj->meshCache->indexByteOffset;
		const u32 vertexLayoutIndex = obj->meshCache->vertexLayoutId;

		bool mirrored = Determinant(Mat34(obj->transform)) < 0.0f;
		CullMode cullMode = material->cullMode;
		if (mirrored && cullMode == CullMode::Back)
		{
			cullMode = CullMode::Front;
		}
		else if (mirrored && cullMode == CullMode::Front)
		{
			cullMode = CullMode::Back;
		}
		bool hasBones = obj->bonesDescriptor != nullptr || obj->boneBufferSlot != U32_MAX;

		DrawPipelineDesc pipelineDesc;
		pipelineDesc.cullMode = cullMode;
		pipelineDesc.hasBones = hasBones;
		pipelineDesc.masked   = material->masked;
		pipelineDesc.depthWrite = material->depthWrite;
		pipelineDesc.depthTest  = material->depthTest;
		pipelineDesc.shader   = obj->shader;
		pipelineDesc.materialGraph = material->materialGraph;

		Array<DrawPipeline>& pipelineStorage = ref.transparent ? transparentPipelines : opaquePipelines;
		ref.pipelineIndex = GetOrCreatePipeline(pipelineStorage, pipelineDesc);
		ref.handle = pipelineStorage[ref.pipelineIndex].drawcalls.Insert();

		Drawcall& dc = pipelineStorage[ref.pipelineIndex].drawcalls[ref.handle];
		dc.firstIndex = primitive.firstIndex;
		dc.indexCount = primitive.indexCount;
		dc.vertexByteOffset = vertexByteOffset;
		dc.indexByteOffset = indexByteOffset;
		dc.mesh = obj->meshCache;
		dc.meshVersion = obj->meshCache->version;
		dc.material = material;
		dc.userData = obj->userData;
		dc.vertexLayoutIndex = vertexLayoutIndex;
		dc.bones = obj->bonesDescriptor;
		dc.boneBufferIndex = obj->boneBufferSlot;
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
			shadowDesc.cullMode = material->masked ? CullMode::None : cullMode;
			shadowDesc.hasBones = hasBones;
			shadowDesc.masked   = material->masked;

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
				sdc.boneBufferIndex = obj->boneBufferSlot;
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
			.boneBufferIndex = obj->boneBufferSlot,
		};

		EnrollBlasInstance(obj, primitiveIndex);
	}

	void RenderSceneObjects::EnrollBlasInstance(RenderableObjectStorage* obj, u32 primitiveIndex)
	{
		if (!requireTlas) return;
		if (primitiveIndex >= obj->references.Size()) return;
		DrawcallRef& ref = obj->references[primitiveIndex];

		// Already enrolled, or there's no drawcall (e.g., missing material) — nothing to do.
		if (ref.instanceDescIndex != U32_MAX) return;
		if (ref.pipelineIndex == U32_MAX) return;
		if (ref.transparent) return;
		if (ref.masked) return;

		GPUBottomLevelAS* blas = nullptr;
		const bool hasBones = obj->bonesBuffer != nullptr || obj->boneBufferSlot != U32_MAX || obj->bonesDescriptor != nullptr;
		if (hasBones)
		{
			if (!EnsureSkinnedRayTracingResources(obj)) return;
			if (primitiveIndex >= obj->skinnedRayTracingBlas.Size()) return;
			blas = obj->skinnedRayTracingBlas[primitiveIndex];
		}
		else
		{
			if (!obj->meshCache || primitiveIndex >= obj->meshCache->blasArray.Size()) return;
			blas = obj->meshCache->blasArray[primitiveIndex];
		}

		if (!blas) return;

		ref.instanceDescIndex = static_cast<u32>(instances.Size());
		instances.EmplaceBack(InstanceDesc{
			.bottomLevelAS = blas,
			.transform = obj->transform,
			.instanceID = ref.instanceIndex,
			.instanceShaderBindingTableRecordOffset = obj->rayHitGroup,
			.forceOpaque = true,
		});
		instanceDescOwners.EmplaceBack(InstanceOwner{obj, primitiveIndex});
		MarkInstanceDirty(ref.instanceDescIndex);
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
					MarkInstanceDirty(movedRef.instanceDescIndex);
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
				MarkInstanceDirty(ref.instanceDescIndex);
			}
			instances.PopBack();
			instanceDescOwners.PopBack();
			tlasTopologyDirty = true;
		}
	}

	bool RenderSceneObjects::EnsureSkinnedRayTracingResources(RenderableObjectStorage* obj)
	{
		if (!Graphics::GetDevice()->GetFeatures().rayTracing) return false;
		if (!obj || !obj->meshCache || !obj->meshCache->hasSkin || !obj->bonesBuffer) return false;
		if (obj->meshCache->vertexByteOffset == U32_MAX || obj->meshCache->indexByteOffset == U32_MAX) return false;
		if (!RenderResourceCache::GetMeshDataBuffer()) return false;

		const u32 primitiveCount = static_cast<u32>(obj->meshCache->primitives.Size());
		if (primitiveCount == 0) return false;

		if (obj->skinnedRayTracingVertexBuffer &&
			obj->skinnedRayTracingVertexCount == obj->meshCache->vertexCount &&
			obj->skinnedRayTracingBlas.Size() == primitiveCount)
		{
			return true;
		}

		DestroySkinnedRayTracingResources(obj);

		obj->skinnedRayTracingVertexCount = obj->meshCache->vertexCount;
		obj->skinnedRayTracingVertexBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = static_cast<usize>(obj->skinnedRayTracingVertexCount) * SkinnedBlasVertexStride,
			.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess | ResourceUsage::AccelerationStructure,
			.hostVisible = false,
			.persistentMapped = false,
			.debugName = String(obj->meshCache->debugName) + "_SkinnedRTPositions"
		});

		Array<GeometryDesc>      geometries(primitiveCount);
		Array<BottomLevelASDesc> blasDescs(primitiveCount);
		u64                      maxScratch = 0;

		obj->skinnedRayTracingBlas.Resize(primitiveCount, nullptr);
		for (u32 p = 0; p < primitiveCount; ++p)
		{
			u32 firstIndexUnits = 0;
			u32 indexCount = 0;
			if (!RenderResourceCache::GetMeshPrimitiveBlasLod(obj->meshCache, p, firstIndexUnits, indexCount))
			{
				DestroySkinnedRayTracingResources(obj);
				return false;
			}

			geometries[p] = GeometryDesc{};
			geometries[p].type = GeometryType::Triangles;
			geometries[p].triangles.vertexBuffer = obj->skinnedRayTracingVertexBuffer;
			geometries[p].triangles.vertexOffset = 0;
			geometries[p].triangles.vertexCount = obj->skinnedRayTracingVertexCount;
			geometries[p].triangles.vertexStride = SkinnedBlasVertexStride;
			geometries[p].triangles.vertexFormat = Format::RGB32_FLOAT;
			geometries[p].triangles.indexBuffer = RenderResourceCache::GetMeshDataBuffer();
			geometries[p].triangles.indexOffset = static_cast<u64>(firstIndexUnits) * sizeof(u32);
			geometries[p].triangles.indexCount = indexCount;
			geometries[p].triangles.indexType = IndexType::Uint32;
			geometries[p].triangles.opaque = true;

			blasDescs[p] = BottomLevelASDesc{};
			blasDescs[p].geometries = Span<GeometryDesc>(&geometries[p], 1);
			blasDescs[p].flags = BuildAccelerationStructureFlags::PreferFastBuild | BuildAccelerationStructureFlags::AllowUpdate;
			blasDescs[p].debugName = String(obj->meshCache->debugName) + "_SkinnedBLAS_" + ToString(p);

			obj->skinnedRayTracingBlas[p] = Graphics::CreateBottomLevelAS(blasDescs[p]);
			if (!obj->skinnedRayTracingBlas[p])
			{
				DestroySkinnedRayTracingResources(obj);
				return false;
			}
			maxScratch = std::max(maxScratch, static_cast<u64>(Graphics::GetAccelerationStructureBuildScratchSize(blasDescs[p])));
		}

		EnsureSkinnedBlasScratchBuffer(maxScratch);
		obj->skinnedRayTracingBuilt = false;
		return true;
	}

	void RenderSceneObjects::DestroySkinnedRayTracingResources(RenderableObjectStorage* obj)
	{
		if (!obj) return;

		for (GPUBottomLevelAS* blas : obj->skinnedRayTracingBlas)
		{
			if (blas) blas->Destroy();
		}
		obj->skinnedRayTracingBlas.Clear();

		if (obj->skinnedRayTracingVertexBuffer)
		{
			obj->skinnedRayTracingVertexBuffer->Destroy();
			obj->skinnedRayTracingVertexBuffer = nullptr;
		}

		obj->skinnedRayTracingVertexCount = 0;
		obj->skinnedRayTracingBuilt = false;
	}

	void RenderSceneObjects::EnsureSkinnedBlasScratchBuffer(u64 requiredSize)
	{
		if (requiredSize <= skinnedBlasScratchSize) return;

		if (skinnedBlasScratchBuffer)
		{
			skinnedBlasScratchBuffer->Destroy();
		}

		skinnedBlasScratchBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = requiredSize,
			.usage = ResourceUsage::ShaderResource,
			.hostVisible = false,
			.persistentMapped = false,
			.debugName = "SceneSkinnedBLAS_Scratch"
		});
		skinnedBlasScratchSize = requiredSize;
	}

	void RenderSceneObjects::UpdateSkinnedRayTracing(GPUCommandBuffer* cmd)
	{
		if (!cmd || !Graphics::GetDevice()->GetFeatures().rayTracing) return;

		bool hasSkinnedBlas = false;
		for (RenderableObjectStorage* obj : renderables)
		{
			if (obj->skinnedRayTracingVertexBuffer && !obj->skinnedRayTracingBlas.Empty() && obj->bonesBuffer)
			{
				hasSkinnedBlas = true;
				break;
			}
		}
		if (!hasSkinnedBlas) return;

		GPUPipeline* pipeline = GetSkinnedBlasSkinningPipeline();
		if (!pipeline) return;

		bool dispatched = false;
		cmd->BindPipeline(pipeline);
		cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());

		for (RenderableObjectStorage* obj : renderables)
		{
			if (!obj->skinnedRayTracingVertexBuffer || obj->skinnedRayTracingBlas.Empty() || !obj->bonesBuffer) continue;

			SkinnedBlasSkinningPushConstants pc;
			pc.vertexByteOffset = obj->meshCache->vertexByteOffset;
			pc.vertexLayoutIndex = obj->meshCache->vertexLayoutId;
			pc.vertexCount = obj->skinnedRayTracingVertexCount;

			cmd->SetBuffer(pipeline, 2, 0, obj->bonesBuffer, 0, sizeof(Mat4) * MaxBones);
			cmd->SetBuffer(pipeline, 2, 1, obj->skinnedRayTracingVertexBuffer, 0, obj->skinnedRayTracingVertexBuffer->GetDesc().size);
			cmd->PushConstants(pipeline, ShaderStage::Compute, 0, sizeof(SkinnedBlasSkinningPushConstants), &pc);
			cmd->Dispatch((obj->skinnedRayTracingVertexCount + 63) / 64, 1, 1);
			dispatched = true;
		}

		if (dispatched)
		{
			cmd->MemoryBarrier();
		}

		bool built = false;
		for (RenderableObjectStorage* obj : renderables)
		{
			if (obj->skinnedRayTracingBlas.Empty() || !obj->bonesBuffer) continue;

			for (u32 p = 0; p < obj->skinnedRayTracingBlas.Size(); ++p)
			{
				GPUBottomLevelAS* blas = obj->skinnedRayTracingBlas[p];
				if (!blas) continue;
				cmd->BuildBottomLevelAS(blas, AccelerationStructureBuildInfo{
					.update = obj->skinnedRayTracingBuilt,
					.scratchBuffer = skinnedBlasScratchBuffer
				});
				if (p + 1 < obj->skinnedRayTracingBlas.Size())
				{
					cmd->MemoryBarrier();
				}
				built = true;
			}

			obj->skinnedRayTracingBuilt = true;
		}

		if (built)
		{
			cmd->MemoryBarrier();
		}
	}

	bool RenderSceneObjects::HasSkinnedTlasInstances() const
	{
		for (const InstanceOwner& owner : instanceDescOwners)
		{
			if (!owner.renderable) continue;
			if (owner.primitiveIndex < owner.renderable->skinnedRayTracingBlas.Size() &&
				owner.renderable->skinnedRayTracingBlas[owner.primitiveIndex] != nullptr)
			{
				return true;
			}
		}
		return false;
	}

	u32 RenderSceneObjects::AcquireBoneBufferSlot(GPUBuffer* bonesBuffer)
	{
		SK_ASSERT(bonesBuffer, "bones buffer is required");

		u32 slot = U32_MAX;
		if (!freeBoneBufferSlots.Empty())
		{
			slot = freeBoneBufferSlots.Back();
			freeBoneBufferSlots.PopBack();
		}
		else
		{
			slot = static_cast<u32>(boneBuffers.Size());
			SK_ASSERT(slot < MaxBindlessResources, "scene skinning buffer table is full");
			boneBuffers.EmplaceBack(nullptr);
		}

		UpdateBoneBufferSlot(slot, bonesBuffer);
		return slot;
	}

	void RenderSceneObjects::ReleaseBoneBufferSlot(u32 slot)
	{
		if (slot == U32_MAX || slot >= boneBuffers.Size()) return;
		boneBuffers[slot] = nullptr;
		if (fallbackBoneBuffer)
		{
			WriteBoneSlot(skinningDescriptorSet, slot, fallbackBoneBuffer);
			WriteBoneSlot(previousSkinningDescriptorSet, slot, fallbackBoneBuffer);
		}
		freeBoneBufferSlots.EmplaceBack(slot);
	}

	void RenderSceneObjects::WriteBoneSlot(GPUDescriptorSet* set, u32 slot, GPUBuffer* bonesBuffer)
	{
		if (!set || slot == U32_MAX || bonesBuffer == nullptr) return;

		DescriptorUpdate update = {};
		update.type = DescriptorType::StorageBuffer;
		update.binding = 0;
		update.arrayElement = slot;
		update.buffer = bonesBuffer;
		update.bufferOffset = 0;
		update.bufferRange = sizeof(Mat4) * MaxBones;
		set->Update(update);
	}

	void RenderSceneObjects::UpdateBoneBufferSlot(u32 slot, GPUBuffer* bonesBuffer)
	{
		if (slot == U32_MAX || slot >= boneBuffers.Size() || bonesBuffer == nullptr) return;

		boneBuffers[slot] = bonesBuffer;
		WriteBoneSlot(skinningDescriptorSet, slot, bonesBuffer);
	}

	void RenderSceneObjects::UpdateSkinnedBones(RenderableObject obj, GPUBuffer* currentBones, GPUBuffer* previousBones)
	{
		if (!obj || currentBones == nullptr) return;
		RenderableObjectStorage* o = obj.ToPtr<RenderableObjectStorage>();

		if (o->boneBufferSlot == U32_MAX)
		{
			o->bonesBuffer = currentBones;
			UpdateRenderableBoneSlot(o);
		}
		else if (o->bonesBuffer != currentBones)
		{
			o->bonesBuffer = currentBones;
			UpdateBoneBufferSlot(o->boneBufferSlot, currentBones);
		}

		if (o->boneBufferSlot != U32_MAX)
		{
			WriteBoneSlot(previousSkinningDescriptorSet, o->boneBufferSlot, previousBones ? previousBones : currentBones);
		}

		TrackMovedRenderableBones(o);
	}

	void RenderSceneObjects::UpdateRenderableBoneSlot(RenderableObjectStorage* obj)
	{
		if (obj->bonesBuffer)
		{
			if (obj->boneBufferSlot == U32_MAX)
			{
				obj->boneBufferSlot = AcquireBoneBufferSlot(obj->bonesBuffer);
			}
			else
			{
				UpdateBoneBufferSlot(obj->boneBufferSlot, obj->bonesBuffer);
			}
		}
		else
		{
			ReleaseBoneBufferSlot(obj->boneBufferSlot);
			obj->boneBufferSlot = U32_MAX;
		}

		for (const DrawcallRef& ref : obj->references)
		{
			if (ref.pipelineIndex != U32_MAX)
			{
				Array<DrawPipeline>& storage = ref.transparent ? transparentPipelines : opaquePipelines;
				storage[ref.pipelineIndex].drawcalls[ref.handle].boneBufferIndex = obj->boneBufferSlot;
			}
			if (ref.shadowPipelineIndex != U32_MAX && ref.shadowHandle != U64_MAX)
			{
				shadowPipelines[ref.shadowPipelineIndex].drawcalls[ref.shadowHandle].boneBufferIndex = obj->boneBufferSlot;
			}
			if (ref.instanceIndex != U32_MAX)
			{
				InstanceData* data = static_cast<InstanceData*>(instanceDataBuffer->GetMappedData());
				data[ref.instanceIndex].boneBufferIndex = obj->boneBufferSlot;
			}
		}
	}

	void RenderSceneObjects::OnBeginRecord(GPUCommandBuffer* cmd)
	{
		if (bindEvents) Begin(cmd);
	}

	void RenderSceneObjects::OnEndRecord(GPUCommandBuffer* cmd)
	{
		if (bindEvents) End(cmd);
	}

	void RenderSceneObjects::End(GPUCommandBuffer* cmd)
	{
		for (MovedRenderableObject& moved : movedRenderables)
		{
			if (!moved.object) continue;
			RenderableObjectStorage* obj = moved.object.ToPtr<RenderableObjectStorage>();
			obj->movedRenderableIndex = U32_MAX;
		}
		movedRenderables.Clear();
	}

	void RenderSceneObjects::Begin(GPUCommandBuffer* cmd)
	{
		SK_SCOPED_CPU_ZONE("RenderSceneObjects::Begin");

		if (u64 cacheVersion = RenderResourceCache::GetMeshReloadVersion(); cacheVersion != meshReloadVersion)
		{
			meshReloadVersion = cacheVersion;
			for (RenderableObjectStorage* obj : renderables)
			{
				for (const DrawcallRef& ref : obj->references)
				{
					if (ref.pipelineIndex == U32_MAX) continue;
					Array<DrawPipeline>& storage = ref.transparent ? transparentPipelines : opaquePipelines;
					const Drawcall& dc = storage[ref.pipelineIndex].drawcalls[ref.handle];
					if (dc.mesh && dc.mesh->version != dc.meshVersion)
					{
						MarkDirty(obj);
						break;
					}
				}
			}
		}

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

		UpdateSkinnedRayTracing(cmd);

		const bool tlasDirty = tlasTopologyDirty || tlasTransformsDirty ;
		if (requireTlas && tlasDirty && Graphics::GetDevice()->GetFeatures().rayTracing)
		{
			if (!instances.Empty())
			{
				SK_SCOPED_GPU_ZONE("RenderSceneObjects - TLAS Update", cmd);

				bool recreated = false;
				const bool wantsFastBuildTlas = HasSkinnedTlasInstances();
				if (tlas == nullptr || instances.Size() > tlasMaxInstances || tlasFastBuild != wantsFastBuildTlas)
				{
					if (tlas) tlas->Destroy();
					if (tlasScratchBuffer) tlasScratchBuffer->Destroy();

					u32 newCapacity = static_cast<u32>(instances.Size()) * 2;
					if (newCapacity < 256) newCapacity = 256;

					TopLevelASDesc tlasDesc{};
					tlasDesc.instances = instances;
					tlasDesc.maxInstances = newCapacity;
					tlasDesc.flags = (wantsFastBuildTlas ? BuildAccelerationStructureFlags::PreferFastBuild : BuildAccelerationStructureFlags::PreferFastTrace)
						| BuildAccelerationStructureFlags::AllowUpdate;
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
					tlasFastBuild = wantsFastBuildTlas;
					recreated = true;
				}

				if (recreated)
				{
					dirtyInstances.clear();
				}
				else
				{
					for (u32 index : dirtyInstances)
					{
						if (index < instances.Size())
						{
							tlas->UpdateInstance(index, instances[index]);
						}
					}
					tlas->SetInstanceCount(static_cast<u32>(instances.Size()));
					dirtyInstances.clear();
				}

				const bool needsRebuild = tlasTopologyDirty || recreated;
				const bool update = !needsRebuild;
				cmd->BuildTopLevelAS(tlas, AccelerationStructureBuildInfo{.update = update, .scratchBuffer = tlasScratchBuffer});
			}
			else
			{
				dirtyInstances.clear();
			}

			tlasTopologyDirty = false;
			tlasTransformsDirty = false;
		}
	}
}
