#include "AnimationClipNode.hpp"

#include "Skore/Animation/AnimationClip.hpp"
#include "Skore/Animation/TaskSystem/TaskSystem.hpp"
#include "Skore/Animation/TaskSystem/Tasks/SampleTask.hpp"

namespace Skore::Anim
{
	void AnimationClipNode::Definition::InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const
	{
		auto* pNode = CreateNode<AnimationClipNode>(context, options);
		pNode->m_pClip = context.GetClipResource(m_dataSlotIdx);
	}

	void AnimationClipNode::InitializeInternal(GraphContext& context, const SyncTrackTime& initialTime)
	{
		PoseNode::InitializeInternal(context, initialTime);

		if (m_pClip != nullptr)
		{
			m_syncTrack = m_pClip->GetSyncTrack();
			m_duration = m_pClip->GetDuration();
			m_currentTime = m_syncTrack.GetPercentageThrough(initialTime);
			m_previousTime = m_currentTime;
		}
	}

	GraphPoseNodeResult AnimationClipNode::Update(GraphContext& context, const SyncTrackTimeRange* pUpdateRange)
	{
		MarkNodeActive(context);

		GraphPoseNodeResult result;
		result.m_sampledEventRange = context.GetEmptySampledEventRange();

		if (m_pClip == nullptr) return result;

		const Definition* pDef = GetDefinition<AnimationClipNode>();

		// Advance time
		//-------------------------------------------------------------------------
		if (pUpdateRange != nullptr) // synchronized: parent dictates the event-space range
		{
			m_previousTime = m_syncTrack.GetPercentageThrough(pUpdateRange->m_startTime);
			m_currentTime  = m_syncTrack.GetPercentageThrough(pUpdateRange->m_endTime);
		}
		else // unsynchronized: advance by the frame delta
		{
			f32 deltaPercentage = (m_duration > 0.0f) ? ((context.m_deltaTime * pDef->m_speedMultiplier) / m_duration) : 0.0f;
			m_previousTime = m_currentTime;

			f32 newTime = m_currentTime.ToFloat() + deltaPercentage;
			if (newTime >= 1.0f)
			{
				if (pDef->m_allowLooping)
				{
					m_loopCount++;
					newTime -= 1.0f; // single-loop wrap (frame deltas are small in S2)
				}
				else
				{
					newTime = 1.0f;
				}
			}
			m_currentTime = Percentage(newTime);
		}

		// Register the sample task (the pose recipe)
		//-------------------------------------------------------------------------
		result.m_taskIdx = context.m_pTaskSystem->RegisterTask<SampleTask>(m_pClip, m_currentTime);

		// Root motion delta for [previous, current] (translation in root-local space)
		//-------------------------------------------------------------------------
		if (pDef->m_sampleRootMotion && m_pClip->HasRootMotion())
		{
			f32 dur = m_pClip->GetSamplingDuration();
			f32 prevSeconds = m_previousTime.ToFloat() * dur;
			f32 curSeconds  = m_currentTime.ToFloat() * dur;
			result.m_rootMotionDelta = m_pClip->GetRootMotionDelta(prevSeconds, curSeconds);
		}

		return result;
	}
}
