#include "GraphInstance.hpp"

#include "GraphDefinition.hpp"
#include "Nodes/ControlParameterNodes.hpp"
#include "Skore/Animation/TaskSystem/TaskSystem.hpp"

namespace Skore::Anim
{
	GraphInstance::GraphInstance(const GraphDefinition* definition, u64 ownerID)
		: m_pGraphDefinition(definition)
		, m_ownerID(ownerID)
		, m_graphContext(ownerID, definition->GetSkeleton())
	{
		const Skeleton* skeleton = definition->GetSkeleton();

		// Standalone instance: own the task system + sampled-events buffer.
		m_pTaskSystem = new TaskSystem(skeleton);
		m_pSampledEventsBuffer = new SampledEventsBuffer();

		// One allocation for all node state. `new u8[]` is max_align (16) aligned, which
		// covers these nodes; over-aligned (SIMD) nodes would need an aligned alloc later.
		m_pAllocatedInstanceMemory = new u8[definition->m_instanceRequiredMemory];

		m_nodes.Reserve(definition->m_instanceNodeStartOffsets.Size());
		for (u32 offset : definition->m_instanceNodeStartOffsets)
		{
			m_nodes.EmplaceBack(reinterpret_cast<GraphNode*>(m_pAllocatedInstanceMemory + offset));
		}

		// Placement-new each node and wire its child pointers (by index).
		InstantiationContext ic{
			static_cast<i16>(InvalidIndex),
			m_nodes,
			skeleton,
			&definition->m_resources,
			ownerID
		};
		for (i16 i = 0; i < static_cast<i16>(m_nodes.Size()); ++i)
		{
			ic.m_currentNodeIdx = i;
			definition->m_nodeDefinitions[i]->InstantiateNode(ic, InstantiationOptions::CreateNode);
		}

		m_graphContext.Initialize(m_pTaskSystem, m_pSampledEventsBuffer);

		// Initialize persistent nodes (control params etc.); the root (and its active tree)
		// is initialized lazily on the first EvaluateGraph via ResetGraphState.
		for (i16 idx : definition->m_persistentNodeIndices)
		{
			m_nodes[idx]->Initialize(m_graphContext);
		}

		if (definition->m_rootNodeIdx != static_cast<i16>(InvalidIndex))
		{
			m_pRootNode = static_cast<PoseNode*>(m_nodes[definition->m_rootNodeIdx]);
		}
	}

	GraphInstance::~GraphInstance()
	{
		// Shut down the root (recursively shuts down the active tree).
		if (m_pRootNode != nullptr && m_pRootNode->IsInitialized())
		{
			m_pRootNode->Shutdown(m_graphContext);
		}

		// Shut down any still-initialized persistent nodes (reverse order).
		const Array<i16>& persistent = m_pGraphDefinition->m_persistentNodeIndices;
		for (i32 p = static_cast<i32>(persistent.Size()) - 1; p >= 0; --p)
		{
			GraphNode* node = m_nodes[persistent[p]];
			if (node->IsInitialized()) node->Shutdown(m_graphContext);
		}

		m_graphContext.Shutdown();

		// Placement-new => destruct each node manually (reverse order), then free the block.
		for (i32 i = static_cast<i32>(m_nodes.Size()) - 1; i >= 0; --i)
		{
			m_nodes[i]->~GraphNode();
		}

		delete[] m_pAllocatedInstanceMemory;
		delete m_pTaskSystem;
		delete m_pSampledEventsBuffer;
	}

	bool GraphInstance::IsValid() const
	{
		return m_pRootNode != nullptr && m_pRootNode->IsValid();
	}

	bool GraphInstance::WasInitialized() const
	{
		return m_pRootNode != nullptr && m_pRootNode->IsInitialized();
	}

	const Pose* GraphInstance::GetPrimaryPose()
	{
		return m_pTaskSystem->GetPrimaryPose();
	}

	void GraphInstance::ResetGraphState(SyncTrackTime initTime)
	{
		if (m_pRootNode == nullptr) return;

		if (m_pRootNode->IsInitialized())
		{
			m_pRootNode->Shutdown(m_graphContext);
		}
		m_pRootNode->Initialize(m_graphContext, initTime);
	}

	GraphPoseNodeResult GraphInstance::EvaluateGraph(f32 deltaTime, const XForm& startWorldTransform, const SyncTrackTimeRange* pUpdateRange, bool resetGraphState)
	{
		m_pTaskSystem->Reset();
		m_pSampledEventsBuffer->Clear();

		m_graphContext.Update(deltaTime, startWorldTransform);

		if (resetGraphState || !m_pRootNode->IsInitialized())
		{
			ResetGraphState();
		}

		return m_pRootNode->Update(m_graphContext, pUpdateRange);
	}

	void GraphInstance::ExecutePrePhysicsPoseTasks(const XForm& endWorldTransform)
	{
		m_pTaskSystem->UpdatePrePhysics(m_graphContext.m_deltaTime, endWorldTransform, endWorldTransform.GetInverse());
	}

	void GraphInstance::ExecutePostPhysicsPoseTasks()
	{
		m_pTaskSystem->UpdatePostPhysics();
	}

	void GraphInstance::SetControlParameterFloat(i16 nodeIdx, f32 value)
	{
		static_cast<ControlParameterFloatNode*>(m_nodes[nodeIdx])->DirectlySetValue(value);
	}
}
