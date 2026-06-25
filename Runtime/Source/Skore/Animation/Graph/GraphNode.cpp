#include "GraphNode.hpp"

namespace Skore::Anim
{
	void GraphNode::Initialize(GraphContext& context)
	{
		if (IsInitialized())
		{
			++m_initializationCount; // ref-counted: a node referenced by N parents inits once
		}
		else
		{
			InitializeInternal(context);
		}
	}

	void GraphNode::Shutdown(GraphContext& context)
	{
		if (--m_initializationCount == 0)
		{
			ShutdownInternal(context);
		}
	}

	void GraphNode::InitializeInternal(GraphContext& context)
	{
		(void) context;
		++m_initializationCount;
	}

	void GraphNode::ShutdownInternal(GraphContext& context)
	{
		(void) context;
		m_lastUpdateID = 0xFFFFFFFF;
	}

	//-------------------------------------------------------------------------

	void PoseNode::Initialize(GraphContext& context, const SyncTrackTime& initialTime)
	{
		if (IsInitialized())
		{
			++m_initializationCount;
		}
		else
		{
			InitializeInternal(context, initialTime);
		}
	}

	void PoseNode::InitializeInternal(GraphContext& context, const SyncTrackTime& initialTime)
	{
		(void) initialTime;
		GraphNode::InitializeInternal(context);

		// Reset node state; nodes set the duration correctly during their own init.
		m_loopCount = 0;
		m_previousTime = 0.0f;
		m_currentTime = 0.0f;
		m_duration = 0.0f;
	}
}
