#include "GraphContext.hpp"

namespace Skore::Anim
{
	GraphContext::GraphContext(u64 ownerID, const Skeleton* skeleton)
		: m_graphUserID(ownerID)
		, m_pSkeleton(skeleton)
	{
	}

	void GraphContext::Initialize(TaskSystem* taskSystem, SampledEventsBuffer* sampledEventsBuffer)
	{
		m_pTaskSystem = taskSystem;
		m_pSampledEventsBuffer = sampledEventsBuffer;
	}

	void GraphContext::Shutdown()
	{
		m_pTaskSystem = nullptr;
		m_pSampledEventsBuffer = nullptr;
	}

	void GraphContext::Update(f32 deltaTime, const XForm& worldTransform)
	{
		m_updateID++; // powers value-node once-per-frame caching
		m_deltaTime = deltaTime;
		m_worldTransform = worldTransform;
		m_worldTransformInverse = worldTransform.GetInverse();
	}
}
