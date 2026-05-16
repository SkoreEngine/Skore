#pragma once

#include "Device.hpp"
#include "GraphicsCommon.hpp"
#include "RenderResourceCache.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/PackedArray.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Core/UnorderedDense.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"

namespace Skore
{
	class UICanvas;

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

	class RenderSceneObjects;
	struct MaterialResourceCache;
	struct MeshResourceCache;

	struct InstanceSlot
	{
		u32 dataId = U32_MAX;
		u32 descIndex = U32_MAX;
	};

	class SK_API DrawableObject
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(DrawableObject);

		DrawableObject(RenderSceneObjects* objects) : renderSceneObjects(objects) {}

		void SetTransform(const Mat4& transform);
		Mat4 GetTransform() const;

		void SetMesh(RID mesh);
		RID  GetMesh() const;

		void SetUUID(UUID uuid);
		UUID GetUUID() const;

		void      SetMaterials(Span<RID> materials);
		Span<RID> GetMaterial();

		void SetCastShadows(bool castShadows);
		bool GetCastShadows() const;

		void SetUserData(u64 userData);
		u64  GetUserData() const;

		void SetVisible(bool visible);
		bool GetVisible() const;

		void SetLayerMask(u64 layerMask);
		u64  GetLayerMask() const;

		AABB GetAABB() const;

		void Destroy();

		const Array<DrawcallRef>& GetOpaqueDrawcallRefs() const
		{
			return opaqueDrawcallRefs;
		}

		const Array<DrawcallRef>& GetTransparentDrawcallRefs() const
		{
			return transparentDrawcallRefs;
		}

		const Array<DrawcallRef>& GetShadowDrawcallRefs() const
		{
			return shadowDrawcallRefs;
		}

		template<typename Fn>
		SK_FINLINE void ForEachVisibleDrawcallRef(Fn&& fn) const;

		friend class RenderSceneObjects;

		void UpdateBones(Span<Mat4> bones) const;

	private:
		RenderSceneObjects* renderSceneObjects;

		RID              mesh;
		UUID             uuid;
		Array<RID>       materials;
		bool             castShadows = true;
		Mat4             transform;
		u64              userData = 0;
		bool             visible = true;
		u64              layerMask = 1ULL;

		u64 lastUpdatedVersion = 0;
		u64 currentVersion = 1;

		AABB aabb = {};

		MeshResourceCache*            meshCache = nullptr;
		Array<MaterialResourceCache*> overrideMaterialsCache;

		Array<DrawcallRef> opaqueDrawcallRefs;
		Array<DrawcallRef> transparentDrawcallRefs;
		Array<DrawcallRef> shadowDrawcallRefs;

		bool materialsDirty = false;

		GPUDescriptorSet* bonesDescriptor = nullptr;
		GPUBuffer* bonesBuffer = nullptr;

		Array<InstanceSlot> instanceSlots;

		void UpdateSkinData();
		void MarkDirty();
		void UpdateAABB();

		MaterialResourceCache* GetMaterial(u32 materialIndex) const;
	};

	class SK_API LightObject
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(LightObject);

		LightObject(RenderSceneObjects* objects);

		void SetTransform(const Mat4& transform);
		Mat4 GetTransform() const;

		void SetUserData(u64 userData);
		u64  GetUserData() const;

		void SetCullingMask(u64 cullingMask);
		u64  GetCullingMask() const;

		void      SetType(LightType type);
		LightType GetType() const;

		void         SetColor(const Color& color);
		const Color& GetColor() const;

		void SetIntensity(f32 intensity);
		f32  GetIntensity() const;

		void SetRange(f32 range);
		f32  GetRange() const;

		void SetInnerConeAngle(f32 angle);
		f32  GetInnerConeAngle() const;

		void SetOuterConeAngle(f32 angle);
		f32  GetOuterConeAngle() const;

		void SetVisible(bool visible);
		bool GetVisible() const;

		void SetEnableShadows(bool enableShadows);
		bool GetEnableShadows() const;

		void SetSourceRadius(f32 sourceRadius);
		f32  GetSourceRadius() const;

		void Destroy();

		friend class RenderSceneObjects;

	private:
		RenderSceneObjects* objects;
		LightType type = LightType::Directional;
		Mat4      transform = Mat4(1.0);
		Color     color = Color::WHITE;
		f32       intensity = 1.0f;
		f32       range = 10.0f;
		f32       innerConeAngle = Math::Radians(25.0f);
		f32       outerConeAngle = Math::Radians(30.0f);
		bool      visible = true;
		bool      enableShadows = true;
		u64       userData = 0;
		u64       cullingMask = ~0ULL;
		f32       sourceRadius = 0.0f;
	};

	class SK_API EnvironmentObject
	{
	public:
		EnvironmentObject(RenderSceneObjects* objects);

		void SetMaterial(RID material);
		RID  GetMaterial() const;

		void SetVisible(bool visible);
		bool GetVisible() const;

		void SetUseAsSkybox(bool useAsSkybox);
		bool GetUseAsSkybox() const;

		void               SetAmbientLightSource(AmbientLightSource source);
		AmbientLightSource GetAmbientLightSource() const;

		void         SetAmbientLightColor(const Color& color);
		const Color& GetAmbientLightColor() const;

		void SetAmbientLightIntensity(f32 intensity);
		f32  GetAmbientLightIntensity() const;

		void                 SetReflectedLightSource(ReflectedLightSource source);
		ReflectedLightSource GetReflectedLightSource() const;

		void SetReflectedLightIntensity(f32 intensity);
		f32  GetReflectedLightIntensity() const;

		MaterialResourceCache* GetMaterialCache() const;

		void Destroy();

	private:
		RenderSceneObjects* objects;
		RID material;
		bool visible = true;
		bool useAsSkybox = true;

		AmbientLightSource ambientLightSource = AmbientLightSource::Skybox;
		Color ambientLightColor = Color::WHITE;
		f32 ambientLightIntensity = 1.0f;

		ReflectedLightSource reflectedLightSource = ReflectedLightSource::Skybox;
		f32 reflectedLightIntensity = 1.0f;

		MaterialResourceCache*  materialCache = nullptr;
	};

	class SK_API ParticleObject
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(ParticleObject);

		ParticleObject(RenderSceneObjects* objects);

		void SetVisible(bool visible);
		bool GetVisible() const;

		void SetMaxParticles(u32 maxParticles);
		u32  GetMaxParticles() const;

		void UploadData(const void* data, u32 sizeBytes);

		void Destroy();

		friend class RenderSceneObjects;

		GPUBuffer*        particleBuffer = nullptr;
		GPUDescriptorSet* particleDescriptorSet = nullptr;

	private:
		RenderSceneObjects* objects;
		bool visible = true;
		u32  maxParticles = 10000;
	};

	class SK_API CameraObject
	{
	public:
		CameraObject(RenderSceneObjects* objects);

		const Mat4& GetTransform() const;
		void        SetTransform(const Mat4& transform);

		void SetVisible(bool visible);
		bool GetVisible() const;

		void SetUserData(u64 userData);
		u64  GetUserData() const;

		void SetCullingMask(u64 cullingMask);
		u64  GetCullingMask() const;

		void Destroy();

	private:
		RenderSceneObjects* objects;
		Mat4                transform = Mat4(1.0);
		u64                 userData = 0;
		bool                visible = true;
		u64                 cullingMask = ~0ULL;
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
		Scene* scene;

		SK_NO_COPY_CONSTRUCTOR(RenderSceneObjects);
		RenderSceneObjects(Scene* scene);
		~RenderSceneObjects();

		DrawableObject* CreateDrawable();
		LightObject* CreateLightObject();
		EnvironmentObject* CreateEnvironmentObject();
		CameraObject* CreateCameraObject();
		ParticleObject* CreateParticleObject();

		void DoUpdate(GPUCommandBuffer* cmd);

		friend class DrawableObject;

		Array<DrawPipeline> opaquePipelines;
		Array<DrawPipeline> transparentPipelines;
		Array<DrawPipeline> shadowPipelines;

		Array<LightObject*> lights;

		Array<EnvironmentObject*> environmentObjects;
		Array<CameraObject*> cameraObjects;

		Array<ParticleObject*> particleEmitters;
		Array<UICanvas*> canvasList;

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

		bool GetDirectionalLightShadowData(u32& lightIndex, Vec3& direction) const;
		static u32 GetOrCreatePipeline(Array<DrawPipeline>& pipelines, const DrawPipelineDesc& desc);

	private:
		HashSet<DrawableObject*>  objects;
		DenseSet<DrawableObject*> pendingUpdate;
		Array<InstanceDesc> instances;

		struct InstanceEntry
		{
			DrawableObject* drawable = nullptr;
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
		void AddInstance(DrawableObject* drawable, u32 primitiveIndex, const InstanceDesc& desc, const InstanceData& data);
		void RemoveInstance(DrawableObject* drawable, u32 primitiveIndex);
	};

	template<typename Fn>
	SK_FINLINE void DrawableObject::ForEachVisibleDrawcallRef(Fn&& fn) const
	{
		for (const auto& ref : opaqueDrawcallRefs)
		{
			fn(ref.pipelineIndex, renderSceneObjects->opaquePipelines[ref.pipelineIndex].drawcalls[ref.handle]);
		}
		u32 offset = static_cast<u32>(renderSceneObjects->opaquePipelines.Size());
		for (const auto& ref : transparentDrawcallRefs)
		{
			fn(offset + ref.pipelineIndex, renderSceneObjects->transparentPipelines[ref.pipelineIndex].drawcalls[ref.handle]);
		}
	}
}
