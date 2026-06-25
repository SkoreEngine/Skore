#include "Blend1DNode.hpp"

#include "Skore/Animation/Blender.hpp"
#include "Skore/Animation/TaskSystem/TaskSystem.hpp"
#include "Skore/Animation/TaskSystem/Tasks/BlendTask.hpp"

namespace Skore::Anim
{
	ParameterizedBlendNode::Parameterization ParameterizedBlendNode::Parameterization::CreateParameterization(const Array<f32>& values)
	{
		struct IndexValue
		{
			i16 m_idx;
			f32 m_value;
		};

		i16               numSources = static_cast<i16>(values.Size());
		Array<IndexValue> sorted;
		sorted.Resize(numSources);
		for (i16 i = 0; i < numSources; ++i)
		{
			sorted[i].m_idx = i;
			sorted[i].m_value = values[i];
		}

		// Insertion sort by value, then index (N is small).
		for (i16 i = 1; i < numSources; ++i)
		{
			IndexValue key = sorted[i];
			i16        j = static_cast<i16>(i - 1);
			while (j >= 0 && (sorted[j].m_value > key.m_value || (sorted[j].m_value == key.m_value && sorted[j].m_idx > key.m_idx)))
			{
				sorted[j + 1] = sorted[j];
				--j;
			}
			sorted[j + 1] = key;
		}

		Parameterization p;
		i32              numRanges = numSources - 1;
		p.m_blendRanges.Resize(numRanges);
		for (i32 i = 0; i < numRanges; ++i)
		{
			p.m_blendRanges[i].m_inputIdx0 = sorted[i].m_idx;
			p.m_blendRanges[i].m_inputIdx1 = sorted[i + 1].m_idx;
			p.m_blendRanges[i].m_parameterValueRange = FloatRange(sorted[i].m_value, sorted[i + 1].m_value);
		}
		p.m_parameterRange = FloatRange(sorted[0].m_value, sorted[numSources - 1].m_value);
		return p;
	}

	//-------------------------------------------------------------------------

	void ParameterizedBlendNode::Definition::InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const
	{
		// Called with NodeAlreadyCreated by the derived (e.g. Blend1D) definition; only wire ptrs.
		(void) options;
		auto* pNode = reinterpret_cast<ParameterizedBlendNode*>(context.m_nodePtrs[m_nodeIdx]);
		context.SetNodePtrFromIndex(m_inputParameterValueNodeIdx, pNode->m_pInputParameterValueNode);

		pNode->m_sourceNodes.Reserve(m_sourceNodeIndices.Size());
		for (i16 sourceIdx : m_sourceNodeIndices)
		{
			PoseNode* src = nullptr;
			context.SetNodePtrFromIndex(sourceIdx, src);
			pNode->m_sourceNodes.EmplaceBack(src);
		}
	}

	bool ParameterizedBlendNode::IsValid() const
	{
		if (!PoseNode::IsValid()) return false;
		if (m_pInputParameterValueNode == nullptr || !m_pInputParameterValueNode->IsValid()) return false;
		for (PoseNode* src : m_sourceNodes)
		{
			if (src == nullptr || !src->IsValid()) return false;
		}
		return true;
	}

	void ParameterizedBlendNode::InitializeInternal(GraphContext& context, const SyncTrackTime& initialTime)
	{
		PoseNode::InitializeInternal(context, initialTime);

		m_pInputParameterValueNode->Initialize(context);
		for (PoseNode* src : m_sourceNodes)
		{
			src->Initialize(context, initialTime);
		}

		if (IsValid())
		{
			EvaluateBlendSpace(context);
			m_currentTime = m_previousTime = m_blendedSyncTrack.GetPercentageThrough(initialTime);
		}
	}

	void ParameterizedBlendNode::ShutdownInternal(GraphContext& context)
	{
		for (PoseNode* src : m_sourceNodes)
		{
			src->Shutdown(context);
		}
		m_pInputParameterValueNode->Shutdown(context);
		m_bsr.Reset();
		PoseNode::ShutdownInternal(context);
	}

	void ParameterizedBlendNode::EvaluateBlendSpace(GraphContext& context)
	{
		// Only update the blend space once per frame.
		if (context.m_updateID == m_bsr.m_updateID) return;
		m_bsr.m_updateID = context.m_updateID;

		// Read + clamp the parameter, then find its blend range and the weight within it.
		i32 selectedRangeIdx = InvalidIndex;
		f32 inputParameterValue = m_pInputParameterValueNode->GetValue<f32>(context);
		inputParameterValue = m_parameterization.m_parameterRange.GetClampedValue(inputParameterValue);

		i32 numBlendRanges = static_cast<i32>(m_parameterization.m_blendRanges.Size());
		for (i32 i = 0; i < numBlendRanges; ++i)
		{
			if (m_parameterization.m_blendRanges[i].m_parameterValueRange.ContainsInclusive(inputParameterValue))
			{
				selectedRangeIdx = i;
				m_bsr.m_blendWeight = m_parameterization.m_blendRanges[i].m_parameterValueRange.GetPercentageThrough(inputParameterValue);
				break;
			}
		}

		if (selectedRangeIdx == InvalidIndex) return; // shouldn't happen for a valid parameterization

		const BlendRange& blendRange = m_parameterization.m_blendRanges[selectedRangeIdx];
		if (m_bsr.m_blendWeight == 0.0f)
		{
			m_bsr.m_pSource0 = m_sourceNodes[blendRange.m_inputIdx0];
			m_bsr.m_pSource1 = nullptr;
			m_blendedSyncTrack = SyncTrack(m_bsr.m_pSource0->GetSyncTrack(), m_bsr.m_pSource0->GetSyncTrack(), 0.0f);
			m_duration = m_bsr.m_pSource0->GetDuration();
		}
		else if (m_bsr.m_blendWeight == 1.0f)
		{
			m_bsr.m_pSource0 = m_sourceNodes[blendRange.m_inputIdx1];
			m_bsr.m_pSource1 = nullptr;
			m_blendedSyncTrack = SyncTrack(m_bsr.m_pSource0->GetSyncTrack(), m_bsr.m_pSource0->GetSyncTrack(), 0.0f);
			m_duration = m_bsr.m_pSource0->GetDuration();
		}
		else
		{
			m_bsr.m_pSource0 = m_sourceNodes[blendRange.m_inputIdx0];
			m_bsr.m_pSource1 = m_sourceNodes[blendRange.m_inputIdx1];
			const SyncTrack& s0 = m_bsr.m_pSource0->GetSyncTrack();
			const SyncTrack& s1 = m_bsr.m_pSource1->GetSyncTrack();
			m_blendedSyncTrack = SyncTrack(s0, s1, m_bsr.m_blendWeight);
			m_duration = SyncTrack::CalculateDurationSynchronized(m_bsr.m_pSource0->GetDuration(), m_bsr.m_pSource1->GetDuration(), s0.GetNumEvents(), s1.GetNumEvents(), m_blendedSyncTrack.GetNumEvents(), m_bsr.m_blendWeight);
		}
	}

	GraphPoseNodeResult ParameterizedBlendNode::Update(GraphContext& context, const SyncTrackTimeRange* pUpdateRange)
	{
		GraphPoseNodeResult result;
		if (!IsValid()) return result;

		MarkNodeActive(context);
		EvaluateBlendSpace(context);

		// Compute the update range (event space): synchronized from the parent, else from dt.
		SyncTrackTimeRange updateRange;
		if (pUpdateRange != nullptr)
		{
			updateRange = *pUpdateRange;
		}
		else
		{
			const Definition* pDef = GetDefinition<ParameterizedBlendNode>();
			Percentage        deltaPercentage = (m_duration > 0.0f) ? Percentage(context.m_deltaTime / m_duration) : Percentage(0.0f);
			Percentage        fromTime = m_currentTime;
			Percentage        toTime = Percentage::Clamp(m_currentTime + deltaPercentage, pDef->m_allowLooping);
			updateRange.m_startTime = m_blendedSyncTrack.GetTime(fromTime);
			updateRange.m_endTime = m_blendedSyncTrack.GetTime(toTime);
		}

		if (m_bsr.m_pSource1 == nullptr) // single active source
		{
			result = m_bsr.m_pSource0->Update(context, &updateRange);
			m_previousTime = GetSyncTrack().GetPercentageThrough(updateRange.m_startTime);
			m_currentTime = GetSyncTrack().GetPercentageThrough(updateRange.m_endTime);
		}
		else // two-way blend — drive BOTH sources with the SAME range (phase lock)
		{
			GraphPoseNodeResult sourceResult0 = m_bsr.m_pSource0->Update(context, &updateRange);
			GraphPoseNodeResult sourceResult1 = m_bsr.m_pSource1->Update(context, &updateRange);

			if (sourceResult0.HasRegisteredTasks() && sourceResult1.HasRegisteredTasks())
			{
				result.m_taskIdx = context.m_pTaskSystem->RegisterTask<BlendTask>(sourceResult0.m_taskIdx, sourceResult1.m_taskIdx, m_bsr.m_blendWeight);
				result.m_rootMotionDelta = Blender::BlendRootMotionDeltas(sourceResult0.m_rootMotionDelta, sourceResult1.m_rootMotionDelta, m_bsr.m_blendWeight);
			}
			else
			{
				result = sourceResult0.HasRegisteredTasks() ? sourceResult0 : sourceResult1;
			}

			result.m_sampledEventRange = context.m_pSampledEventsBuffer->BlendEventRanges(sourceResult0.m_sampledEventRange, sourceResult1.m_sampledEventRange, m_bsr.m_blendWeight);

			m_previousTime = GetSyncTrack().GetPercentageThrough(updateRange.m_startTime);
			m_currentTime = GetSyncTrack().GetPercentageThrough(updateRange.m_endTime);
		}

		return result;
	}

	//-------------------------------------------------------------------------

	void Blend1DNode::Definition::InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const
	{
		auto* pNode = CreateNode<Blend1DNode>(context, options);
		ParameterizedBlendNode::Definition::InstantiateNode(context, InstantiationOptions::NodeAlreadyCreated);
		pNode->m_parameterization = m_parameterization;
	}
}
