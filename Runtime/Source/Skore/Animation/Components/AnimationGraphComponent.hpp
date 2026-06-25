#pragma once

#include "Skore/Scene/Component.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Animation/AnimationSkeleton.hpp"

namespace Skore
{
	class Skeleton;
	class Transform;

	namespace Anim
	{
		class AnimationClip;
		class GraphDefinition;
		class GraphInstance;
		struct XForm;
	}

	// The runtime seam: drives a compiled animation graph and writes its result pose into the
	// child Skeleton's bones (FK via UpdateWorldBones -> skinning). Registered ALONGSIDE the
	// legacy AnimationPlayer (both coexist; Migration Phase A). See Docs/AnimationMigrationPlan.md §2.
	//
	// S2: hand-builds a single-clip graph (one AnimationClipNode) from m_previewClip and drives
	// a real GraphInstance. The graph is authored from an .animgraph asset starting at S3/S4, at
	// which point m_previewClip is replaced by a compiled graph-definition RID.
	class SK_API AnimationGraphComponent : public Component, public Tickable
	{
	public:
		SK_CLASS(AnimationGraphComponent, Component);

		void OnStart() override;
		void OnDestroy() override;
		void OnUpdate(f64 deltaTime) override;

		void SetAnimationClip(RID clip);
		RID  GetAnimationClip() const;

		void SetApplyRootMotion(bool value);
		bool GetApplyRootMotion() const;

		static void RegisterType(NativeReflectType<AnimationGraphComponent>& type);

	private:
		TypedRID<AnimationClipResource> m_previewClip = {};

		Skeleton*  m_skeleton = nullptr;
		Transform* m_transform = nullptr;
		bool       m_applyRootMotion = false;

		Anim::Skeleton         m_animSkeleton;             // runtime skeleton built from m_skeleton
		Anim::AnimationClip*   m_clip = nullptr;           // owned
		Anim::GraphDefinition* m_graphDefinition = nullptr; // owned; hand-built single-clip graph
		Anim::GraphInstance*   m_graphInstance = nullptr;   // owned

		void BuildGraph();
		void DestroyGraph();
		void ApplyRootMotion(const Anim::XForm& delta, f32 dt);
	};
}
