#include "AnimationGraphComponent.hpp"

#include "Skore/Animation/AnimXForm.hpp"
#include "Skore/Animation/AnimationClip.hpp"
#include "Skore/Animation/Graph/GraphDefinition.hpp"
#include "Skore/Animation/Graph/GraphInstance.hpp"
#include "Skore/Animation/Graph/Nodes/AnimationClipNode.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"

namespace Skore
{
	namespace
	{
		// Build an Anim::XForm world transform from a scene Transform (uniform scale).
		Anim::XForm MakeWorldXForm(const Transform* transform)
		{
			Anim::XForm x = Anim::XForm::Identity();
			if (transform)
			{
				x.m_rotation = transform->GetRotation();
				x.m_translation = transform->GetPosition();
			}
			return x;
		}
	}

	void AnimationGraphComponent::OnStart()
	{
		m_transform = entity->GetComponent<Transform>();

		for (Entity* child : entity->GetChildren())
		{
			if (child->HasFlag(EntityFlags::HasSkeleton))
			{
				m_skeleton = child->GetComponent<Skeleton>();
				if (m_skeleton) break;
			}
		}

		BuildGraph();
	}

	void AnimationGraphComponent::OnDestroy()
	{
		DestroyGraph();
	}

	void AnimationGraphComponent::DestroyGraph()
	{
		delete m_graphInstance;   // instance dtor reads the definition + skeleton, so free it first
		m_graphInstance = nullptr;
		delete m_graphDefinition;
		m_graphDefinition = nullptr;
		delete m_clip;
		m_clip = nullptr;
	}

	void AnimationGraphComponent::BuildGraph()
	{
		DestroyGraph();

		if (!m_skeleton) return;
		m_animSkeleton.BuildFrom(m_skeleton);

		if (!m_previewClip) return;
		m_clip = Anim::AnimationClip::CreateFromResource(m_previewClip, &m_animSkeleton);
		if (m_clip == nullptr) return;

		// Hand-build a single-clip graph: one AnimationClipNode as the root.
		m_graphDefinition = new Anim::GraphDefinition();
		m_graphDefinition->SetSkeleton(&m_animSkeleton);
		Anim::AnimationClipNode::Definition* clipNodeDef = m_graphDefinition->CreateNodeDefinition<Anim::AnimationClipNode>();
		clipNodeDef->m_dataSlotIdx = m_graphDefinition->AddClipResource(m_clip);
		m_graphDefinition->SetRootNodeIndex(clipNodeDef->m_nodeIdx);

		m_graphInstance = new Anim::GraphInstance(m_graphDefinition, reinterpret_cast<u64>(this));
	}

	void AnimationGraphComponent::OnUpdate(f64 deltaTime)
	{
		if (!m_graphInstance || !m_skeleton) return;

		f32 dt = static_cast<f32>(deltaTime);

		// Evaluate the graph (walks the node tree, registering the pose-task DAG).
		Anim::GraphPoseNodeResult result = m_graphInstance->EvaluateGraph(dt, MakeWorldXForm(m_transform));

		// Move the entity by root motion, then execute the pose tasks at the final transform.
		if (m_applyRootMotion) ApplyRootMotion(result.m_rootMotionDelta, dt);

		m_graphInstance->ExecutePrePhysicsPoseTasks(MakeWorldXForm(m_transform));
		m_graphInstance->ExecutePostPhysicsPoseTasks();

		const Anim::Pose* pose = m_graphInstance->GetPrimaryPose();
		if (pose == nullptr) return;

		// Write the final pose into the scene Skeleton and run FK -> skinning (the seam).
		const Array<Anim::XForm>& transforms = pose->GetTransforms();
		Array<BoneNode>&          bones = m_skeleton->bones;
		u32 count = Math::Min(static_cast<u32>(bones.Size()), static_cast<u32>(transforms.Size()));
		for (u32 i = 0; i < count; ++i)
		{
			const Anim::XForm& x = transforms[i];
			bones[i].position = x.m_translation;
			bones[i].rotation = x.m_rotation;
			bones[i].scale    = Vec3{x.m_scale, x.m_scale, x.m_scale};
		}

		// Strip the root bone's in-plane motion so root motion isn't double-applied (mesh +
		// transform). Mirrors AnimationPlayer; the graph strips this itself once warps land.
		if (m_applyRootMotion && m_clip != nullptr && m_clip->HasRootMotion())
		{
			i32 rootIdx = m_clip->GetRootBoneIndex();
			if (rootIdx >= 0 && rootIdx < static_cast<i32>(bones.Size()))
			{
				bones[rootIdx].position.x = 0.0f;
				bones[rootIdx].position.z = 0.0f;
				f32 yaw = Quat::Yaw(bones[rootIdx].rotation);
				bones[rootIdx].rotation = Quat::AngleAxis(-yaw, Vec3(0.0f, 1.0f, 0.0f)) * bones[rootIdx].rotation;
			}
		}

		m_skeleton->UpdateWorldBones(); // fires SkeletonUpdated -> SkinnedMeshRenderer (GPU)
	}

	void AnimationGraphComponent::ApplyRootMotion(const Anim::XForm& delta, f32 dt)
	{
		if (!m_transform) return;
		(void) dt; // velocity-mode (CharacterController) root motion is added later

		Vec3 worldDelta = m_transform->GetRotation() * delta.m_translation;
		worldDelta.y = 0.0f; // XZ plane (S2 default)
		m_transform->SetPosition(m_transform->GetPosition() + worldDelta);

		f32 yaw = Quat::Yaw(delta.m_rotation);
		m_transform->SetRotation(m_transform->GetRotation() * Quat::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f)));
	}

	void AnimationGraphComponent::SetAnimationClip(RID clip)
	{
		if (m_previewClip == clip) return;
		m_previewClip = clip;
		BuildGraph();
	}

	RID AnimationGraphComponent::GetAnimationClip() const
	{
		return m_previewClip;
	}

	void AnimationGraphComponent::SetApplyRootMotion(bool value)
	{
		m_applyRootMotion = value;
	}

	bool AnimationGraphComponent::GetApplyRootMotion() const
	{
		return m_applyRootMotion;
	}

	void AnimationGraphComponent::RegisterType(NativeReflectType<AnimationGraphComponent>& type)
	{
		type.Field<&AnimationGraphComponent::m_previewClip,
		           &AnimationGraphComponent::GetAnimationClip,
		           &AnimationGraphComponent::SetAnimationClip>("animationClip");
		type.Field<&AnimationGraphComponent::m_applyRootMotion,
		           &AnimationGraphComponent::GetApplyRootMotion,
		           &AnimationGraphComponent::SetApplyRootMotion>("applyRootMotion");
	}
}
