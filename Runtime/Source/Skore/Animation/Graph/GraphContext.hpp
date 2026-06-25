#pragma once

#include "SampledEvents.hpp"
#include "Skore/Animation/AnimXForm.hpp"

namespace Skore::Anim
{
	class Skeleton;
	class TaskSystem;
	class Pose;

	// Per-frame graph evaluation state. The `m_updateID` is the trick that lets a value
	// node referenced by N parents evaluate only once per frame (a node caches its value
	// and marks itself active for the current update id). Port of Esoterica's GraphContext
	// (layers / physics / dev-tools deferred). Spec §5.2.
	class SK_API GraphContext
	{
	public:
		GraphContext(u64 ownerID, const Skeleton* skeleton);

		void Initialize(TaskSystem* taskSystem, SampledEventsBuffer* sampledEventsBuffer);
		void Shutdown();

		bool IsValid() const { return m_pSkeleton != nullptr && m_pTaskSystem != nullptr; }

		// Advance to the next frame: bumps the update id and stores the per-frame inputs.
		void Update(f32 deltaTime, const XForm& worldTransform);

		SampledEventRange GetEmptySampledEventRange() const { return m_pSampledEventsBuffer->GetEmptyRange(); }

	public:
		// Set at construction
		u64             m_graphUserID = 0;
		const Skeleton* m_pSkeleton = nullptr;

		// Set at initialization
		SampledEventsBuffer* m_pSampledEventsBuffer = nullptr;
		TaskSystem*          m_pTaskSystem = nullptr;
		const Pose*          m_pPreviousPose = nullptr;

		// Runtime values
		u32   m_updateID = 0;
		XForm m_worldTransform = XForm::Identity();
		XForm m_worldTransformInverse = XForm::Identity();
		f32   m_deltaTime = 0.0f;
	};
}
