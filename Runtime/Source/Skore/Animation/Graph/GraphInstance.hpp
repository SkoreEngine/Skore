#pragma once

#include "GraphContext.hpp"
#include "GraphNode.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore::Anim
{
	class GraphDefinition;
	class TaskSystem;
	class Pose;

	// A per-character instantiation of a GraphDefinition: one aligned memory block holding
	// every node's mutable state (placement-new'd at precomputed offsets), wired by index.
	// A standalone instance owns its TaskSystem + SampledEventsBuffer. The flow each frame is
	// EvaluateGraph (logic -> task DAG) then ExecutePre/PostPhysicsPoseTasks (pose math).
	// Port of Esoterica's GraphInstance (referenced/external graphs, recording, control-param
	// API, dev-tools deferred). Spec §8.2, §8.4.
	class SK_API GraphInstance
	{
	public:
		GraphInstance(const GraphDefinition* definition, u64 ownerID);
		GraphInstance(const GraphInstance&) = delete;
		GraphInstance& operator=(const GraphInstance&) = delete;
		~GraphInstance();

		bool IsValid() const;
		bool WasInitialized() const;

		const GraphDefinition* GetGraphDefinition() const { return m_pGraphDefinition; }
		const PoseNode*        GetRootNode() const { return m_pRootNode; }
		u32                    GetUpdateID() const { return m_graphContext.m_updateID; }

		// The final primary pose for this frame (valid after ExecutePostPhysicsPoseTasks).
		const Pose* GetPrimaryPose();

		// (Re)initialize the graph state with an initial time.
		void ResetGraphState(SyncTrackTime initTime = SyncTrackTime());

		// Walk the node tree from the root, registering the pose-task DAG. Returns the root
		// result (task index + root motion delta + sampled-event range).
		GraphPoseNodeResult EvaluateGraph(f32 deltaTime, const XForm& startWorldTransform, const SyncTrackTimeRange* pUpdateRange = nullptr, bool resetGraphState = false);

		// Execute the registered pose tasks (split around physics; collapses to one pass when
		// there's no physics dependency).
		void ExecutePrePhysicsPoseTasks(const XForm& endWorldTransform);
		void ExecutePostPhysicsPoseTasks();

		// Control parameters
		//-------------------------------------------------------------------------

		// Set a float control parameter by its node index. (S3: the name-based lookup
		// GetControlParameterIndex(StringID) awaits the interned-string-id decision, R5.)
		void SetControlParameterFloat(i16 nodeIdx, f32 value);

	private:
		const GraphDefinition* m_pGraphDefinition = nullptr;
		Array<GraphNode*>      m_nodes;
		u8*                    m_pAllocatedInstanceMemory = nullptr;
		PoseNode*              m_pRootNode = nullptr;
		u64                    m_ownerID = 0;
		TaskSystem*            m_pTaskSystem = nullptr;
		SampledEventsBuffer*   m_pSampledEventsBuffer = nullptr;
		GraphContext           m_graphContext;
	};
}
