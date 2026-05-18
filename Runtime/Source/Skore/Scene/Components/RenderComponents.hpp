#pragma once

#include "Transform.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Scene/Component.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	struct BoneNode;
	class Skeleton;


	class SK_API RendererComponent : public Component
	{
	public:
		SK_CLASS(RendererComponent, Component);

		void Create() override;
		void Destroy() override;
		void ProcessEvent(const EntityEventDesc& event) override;

		void SetMesh(RID mesh);
		RID  GetMesh() const;

		void SetCastShadows(bool castShadows);
		bool GetCastShadows() const;

		const MaterialArray& GetMaterials() const;
		void SetMaterials(const MaterialArray& materials);
		void SetMaterial(u32 index, RID material);

		AABB GetAABB() const { return aabb; }

		template<typename Fn>
		SK_FINLINE void ForEachVisibleDrawcallRef(Fn&& fn) const;

		virtual bool IsStatic() const = 0;

		static void RegisterType(NativeReflectType<RendererComponent>& type);

		friend class RenderSceneObjects;

	protected:
		TypedRID<MeshResource> m_mesh = {};
		MaterialArray          m_materials = {};
		bool                   m_castShadows = true;

		Array<DrawcallRef> references;

		AABB aabb = {};

		void Rebuild();
		void UpdateAABB();
		void ClearDrawcalls();

		// Called from Rebuild once the mesh cache is resolved (may be null).
		virtual void OnMeshResolved(MeshResourceCache* meshCache) {}

		// Bones descriptor for skinned drawcalls; nullptr for non-skinned renderers.
		virtual GPUDescriptorSet* GetBonesDescriptor() const { return nullptr; }
	};


	class SK_API StaticMeshRenderer : public RendererComponent
	{
	public:
		SK_CLASS(StaticMeshRenderer, RendererComponent);

		bool IsStatic() const override
		{
			return true;
		}

		static void RegisterType(NativeReflectType<StaticMeshRenderer>& type);
	};

	class SK_API SkinnedMeshRenderer : public RendererComponent
	{
	public:
		SK_CLASS(SkinnedMeshRenderer, RendererComponent);

		void Destroy() override;

		void SetSkeleton(Entity* skeleton);
		Entity* GetSkeleton() const;

		void ProcessEvent(const EntityEventDesc& event) override;

		void UpdateBones(Span<Mat4> bones) const;

		bool IsStatic() const override
		{
			return false;
		}

		static void RegisterType(NativeReflectType<SkinnedMeshRenderer>& type);

	protected:
		void              OnMeshResolved(MeshResourceCache* meshCache) override;
		GPUDescriptorSet* GetBonesDescriptor() const override { return m_bonesDescriptor; }

	private:
		Entity*            m_skeleton = nullptr;
		SkinResourceCache* m_skinCache = nullptr;
		GPUBuffer*         m_bonesBuffer = nullptr;
		GPUDescriptorSet*  m_bonesDescriptor = nullptr;

		void EnsureBonesData();
	};

	template<typename Fn>
	SK_FINLINE void RendererComponent::ForEachVisibleDrawcallRef(Fn&& fn) const
	{
		const u32 transparentOffset = static_cast<u32>(scene->renderObjects.opaquePipelines.Size());
		for (const DrawcallRef& ref : references)
		{
			if (ref.pipelineIndex == U32_MAX) continue;
			if (!ref.transparent)
			{
				fn(ref.pipelineIndex, scene->renderObjects.opaquePipelines[ref.pipelineIndex].drawcalls[ref.handle]);
			}
			else
			{
				fn(transparentOffset + ref.pipelineIndex, scene->renderObjects.transparentPipelines[ref.pipelineIndex].drawcalls[ref.handle]);
			}
		}
	}

	struct AnimChannel
	{
		u32         boneIndex = U32_MAX;
		Array<Vec3> positions;
		Array<Quat> rotations;
		Array<Vec3> scales;
	};

	struct AnimClipData
	{
		u32 frames = 0;
		f32 duration = 0;
		f32 frameRate = 0;
		f32 timeBegin = 0;
		f32 timeEnd = 0;
		Array<AnimChannel> channels;
	};

	struct RuntimeAnimParameter
	{
		AnimationParameterType type = AnimationParameterType::Float;
		f32  floatVal = 0;
		i32  intVal = 0;
		bool boolVal = false;
	};

	struct RuntimeAnimCondition
	{
		u32                          paramIndex = 0;
		AnimationTransitionCondition condition = AnimationTransitionCondition::Greater;
		f32                          value = 0;
	};

	struct RuntimeAnimTransition
	{
		u32  fromState = 0;
		u32  toState = 0;
		f32  crossFadeTime = 0.3f;
		bool hasExitTime = false;
		f32  exitTime = 1.0f;
		bool fixedDuration = true;
		f32  transitionOffset = 0.0f;
		Array<RuntimeAnimCondition> conditions;
	};

	struct RuntimeAnimState
	{
		String       name;
		AnimClipData clip;
		f32          speed = 1.0f;
	};

	struct RuntimeAnimLayer
	{
		String name;
		f32    weight = 1.0f;
		AnimationLayerBlendMode      blendMode = AnimationLayerBlendMode::Override;
		Array<RuntimeAnimState>      states;
		Array<RuntimeAnimTransition> transitions;
		u32 defaultState = 0;
		Array<bool> boneMask;

		// root motion config
		RootMotionMode rootMotionMode = RootMotionMode::None;
		RootMotionAxes rootMotionAxes = RootMotionAxes::XZ;
		bool           applyRotation  = true;
		u32            rootBoneIndex  = U32_MAX;

		// root motion output
		Vec3 rootDeltaPosition = {};
		Quat rootDeltaRotation = {0, 0, 0, 1};

		// runtime playback
		u32  currentState = 0;
		u32  prevState = U32_MAX;
		f32  currentTime = 0.0f;
		f32  prevTime = 0.0f;
		f32  prevCurrentTime = 0.0f;
		f32  prevPrevTime = 0.0f;
		f32  fadeDuration = 0.0f;
		f32  fadeElapsed = 0.0f;
		bool transitioning = false;
	};

	class SK_API AnimationPlayer : public Component, public Tickable
	{
	public:
		SK_CLASS(AnimationPlayer, Component);

		void Create() override;
		void OnStart() override;
		void OnUpdate(f64 deltaTime) override;

		void SetController(RID controller);
		RID  GetController() const;

		void SetFloat(const String& name, f32 value);
		void SetInt(const String& name, i32 value);
		void SetBool(const String& name, bool value);
		void SetTrigger(const String& name);
		f32  GetFloat(const String& name) const;
		i32  GetInt(const String& name) const;
		bool GetBool(const String& name) const;

		void SetApplyRootMotion(bool value);
		bool GetApplyRootMotion() const;
		Vec3 GetRootMotionDelta() const;
		Quat GetRootMotionDeltaRotation() const;

		static void RegisterType(NativeReflectType<AnimationPlayer>& type);

	private:
		TypedRID<AnimationControllerResource> m_controller = {};

		Array<RuntimeAnimLayer>     m_layers;
		Array<RuntimeAnimParameter> m_parameters;
		HashMap<String, u32>        m_parameterMap;

		Skeleton*  m_skeleton = nullptr;
		Transform* m_transform = nullptr;
		bool       m_applyRootMotion = false;

		void LoadController();
		void LoadClipData(AnimClipData& clip, RID animationRID);
		void BuildBoneMask(RuntimeAnimLayer& layer, RID avatarRID);
		void EvaluateTransitions(RuntimeAnimLayer& layer, f32 dt);
		bool CheckConditions(const Array<RuntimeAnimCondition>& conditions) const;
		void SampleClip(const AnimClipData& clip, f32 time, Array<BoneNode>& bones, const Array<bool>& boneMask, bool first, f32 weight);
		void ConsumeTriggers();
		u32  FindRootBoneIndex() const;
		void ExtractRootMotionDelta(const AnimClipData& clip, f32 currentTime, f32 prevTime, f32 speed, Vec3& outPos, Quat& outRot);
		void ApplyRootMotion(RuntimeAnimLayer& layer, f32 dt);
	};

	struct BoneNode
	{
		String      name;
		u32         parentIndex = {};
		Vec3        position = {};
		Quat        rotation = {};
		Vec3        scale = {};

		static void RegisterType(NativeReflectType<BoneNode>& type);
	};

	class SK_API Skeleton : public Component
	{
	public:
		SK_CLASS(Skeleton, Component);
		void Create() override;

		void SetBones(const Array<BoneNode>& bones);
		const Array<BoneNode>& GetBones() const;

		static void RegisterType(NativeReflectType<Skeleton>& type);

		Array<BoneNode> bones;
		Array<Mat4>     localToWorldBones;

		void UpdateWorldBones();
	};

	struct SkeletonUpdatedData
	{
		Entity* entity;
		Skeleton* skeleton;
	};
}
