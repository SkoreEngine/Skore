#include "BlendTask.hpp"

#include "Skore/Animation/Blender.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore::Anim
{
	BlendTaskBase::BlendTaskBase(i8 sourceTaskIdx, i8 targetTaskIdx, f32 blendWeight, const BoneMask* boneMask)
		: Task(TaskUpdateStage::Any)
		, m_blendWeight(blendWeight)
		, m_boneMask(boneMask)
	{
		// Snap near-1 weights to exactly 1 so the Blender's copy fast-path triggers (§4.10).
		if (Math::Abs(m_blendWeight - 1.0f) < Epsilon) m_blendWeight = 1.0f;

		m_dependencies.EmplaceBack(sourceTaskIdx);
		m_dependencies.EmplaceBack(targetTaskIdx);
	}

	void BlendTask::Execute(const TaskContext& context)
	{
		PoseBuffer* pSource = TransferDependencyPoseBuffer(context, 0); // steal dep0 -> our output
		PoseBuffer* pTarget = AccessDependencyPoseBuffer(context, 1);   // borrow dep1
		PoseBuffer* pFinal = pSource;

		bool hasSource = pSource->GetPrimaryPose()->IsPoseSet();
		bool hasTarget = pTarget->GetPrimaryPose()->IsPoseSet();

		if (!hasSource && hasTarget)
		{
			pFinal->GetPrimaryPose()->CopyFrom(*pTarget->GetPrimaryPose());
		}
		else // both poses set (the normal case)
		{
			Blender::ParentSpaceBlend(context.m_skeletonLOD, pSource->GetPrimaryPose(), pTarget->GetPrimaryPose(), m_blendWeight, m_boneMask, pFinal->GetPrimaryPose());
		}

		ReleaseDependencyPoseBuffer(context, 1);
		MarkTaskComplete(context);
	}

	void AdditiveBlendTask::Execute(const TaskContext& context)
	{
		PoseBuffer* pSource = TransferDependencyPoseBuffer(context, 0);
		PoseBuffer* pTarget = AccessDependencyPoseBuffer(context, 1);
		PoseBuffer* pFinal = pSource;

		// A zero (additive-identity) target is a no-op.
		if (!pTarget->GetPrimaryPose()->IsZeroPose())
		{
			Blender::AdditiveBlend(context.m_skeletonLOD, pSource->GetPrimaryPose(), pTarget->GetPrimaryPose(), m_blendWeight, m_boneMask, pFinal->GetPrimaryPose());
		}

		ReleaseDependencyPoseBuffer(context, 1);
		MarkTaskComplete(context);
	}
}
