#include "ReferencePoseNode.hpp"

#include "Skore/Animation/TaskSystem/TaskSystem.hpp"
#include "Skore/Animation/TaskSystem/Tasks/DefaultPoseTask.hpp"

namespace Skore::Anim
{
	void ReferencePoseNode::Definition::InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const
	{
		CreateNode<ReferencePoseNode>(context, options);
	}

	void ReferencePoseNode::InitializeInternal(GraphContext& context, const SyncTrackTime& initialTime)
	{
		PoseNode::InitializeInternal(context, initialTime);
		m_duration = 0.0f; // a static pose has no meaningful duration
	}

	GraphPoseNodeResult ReferencePoseNode::Update(GraphContext& context, const SyncTrackTimeRange* pUpdateRange)
	{
		(void) pUpdateRange;
		MarkNodeActive(context);

		GraphPoseNodeResult result;
		result.m_sampledEventRange = context.GetEmptySampledEventRange();
		result.m_taskIdx = context.m_pTaskSystem->RegisterTask<ReferencePoseTask>();
		return result;
	}
}
