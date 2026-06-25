#pragma once

#include "Task.hpp"
#include "PoseBufferPool.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore::Anim
{
	// Executes the deferred pose-task DAG registered while evaluating the graph. Tasks are
	// registered bottom-up (a dependency before its dependents), so a plain forward pass
	// respects the DAG. Splits execution around physics (pre/post) for ragdoll/IK; with no
	// physics dependency everything runs in UpdatePrePhysics. Port of Esoterica's TaskSystem
	// (serialization + cached poses + dev-tools deferred). Spec §7.4.
	class SK_API TaskSystem
	{
	public:
		explicit TaskSystem(const Skeleton* skeleton);
		TaskSystem(const TaskSystem&) = delete;
		TaskSystem& operator=(const TaskSystem&) = delete;
		~TaskSystem();

		void Reset();

		// The final primary pose produced this frame (valid after UpdatePostPhysics).
		const Pose*     GetPrimaryPose() const { return m_finalPoseBuffer.GetPrimaryPose(); }
		const Skeleton* GetSkeleton() const { return m_posePool.GetSkeleton(); }
		void            SetSkeletonLOD(Skeleton::LOD lod) { m_taskContext.m_skeletonLOD = lod; }
		Skeleton::LOD   GetSkeletonLOD() const { return m_taskContext.m_skeletonLOD; }

		bool RequiresUpdate() const { return m_needsUpdate; }
		bool HasPhysicsDependency() const { return m_hasPhysicsDependency; }
		bool HasTasks() const { return !m_tasks.Empty(); }

		void UpdatePrePhysics(f32 deltaTime, const XForm& worldTransform, const XForm& worldTransformInverse);
		void UpdatePostPhysics();

		// Register a task; returns its index (used as a dependency by later tasks). The task
		// is owned by the system and freed on Reset.
		template<typename T, typename... Args>
		i8 RegisterTask(Args&&... args)
		{
			Task* pNewTask = new T(static_cast<Args&&>(args)...);
			m_tasks.EmplaceBack(pNewTask);
			m_hasPhysicsDependency |= pNewTask->HasPhysicsDependency();
			m_needsUpdate = true;
			return static_cast<i8>(m_tasks.Size() - 1);
		}

	private:
		bool AddTaskChainToPrePhysicsList(i8 taskIdx);
		void ExecuteTasks();

		Array<Task*>   m_tasks;
		PoseBufferPool m_posePool;
		TaskContext    m_taskContext;
		Array<i8>      m_prePhysicsTaskIndices;
		PoseBuffer     m_finalPoseBuffer;
		bool           m_hasPhysicsDependency = false;
		bool           m_hasCodependentPhysicsTasks = false;
		bool           m_needsUpdate = false;
	};
}
