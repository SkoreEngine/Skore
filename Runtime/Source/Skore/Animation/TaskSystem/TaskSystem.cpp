#include "TaskSystem.hpp"

#include "Tasks/DefaultPoseTask.hpp"
#include "Skore/Animation/Blender.hpp"

namespace Skore::Anim
{
	TaskSystem::TaskSystem(const Skeleton* skeleton)
		: m_posePool(skeleton)
		, m_taskContext(m_posePool)
		, m_finalPoseBuffer(skeleton)
	{
		m_finalPoseBuffer.CalculateModelSpaceTransforms();
	}

	TaskSystem::~TaskSystem()
	{
		Reset();
	}

	void TaskSystem::Reset()
	{
		for (Task* pTask : m_tasks)
		{
			delete pTask;
		}
		m_tasks.Clear();
		m_posePool.Reset();
		m_hasPhysicsDependency = false;
	}

	bool TaskSystem::AddTaskChainToPrePhysicsList(i8 taskIdx)
	{
		Task* pTask = m_tasks[taskIdx];
		for (i8 depIdx : pTask->GetDependencyIndices())
		{
			if (!AddTaskChainToPrePhysicsList(depIdx)) return false;
		}

		// Can't have a dependency that relies on physics.
		if (pTask->GetRequiredUpdateStage() == TaskUpdateStage::PostPhysics) return false;

		// Can't add the same task twice (codependency).
		for (i8 idx : m_prePhysicsTaskIndices)
		{
			if (idx == taskIdx) return false;
		}

		m_prePhysicsTaskIndices.EmplaceBack(taskIdx);
		return true;
	}

	void TaskSystem::UpdatePrePhysics(f32 deltaTime, const XForm& worldTransform, const XForm& worldTransformInverse)
	{
		m_taskContext.m_currentTaskIdx = static_cast<i8>(InvalidIndex);
		m_taskContext.m_deltaTime = deltaTime;
		m_taskContext.m_worldTransform = worldTransform;
		m_taskContext.m_worldTransformInverse = worldTransformInverse;
		m_taskContext.m_updateStage = TaskUpdateStage::PrePhysics;

		m_prePhysicsTaskIndices.Clear();
		m_hasCodependentPhysicsTasks = false;

		if (m_hasPhysicsDependency)
		{
			// Gather every chain that must run before physics.
			i8 numTasks = static_cast<i8>(m_tasks.Size());
			for (i8 i = 0; i < numTasks; ++i)
			{
				if (m_tasks[i]->GetRequiredUpdateStage() == TaskUpdateStage::PrePhysics)
				{
					if (!AddTaskChainToPrePhysicsList(i))
					{
						m_hasCodependentPhysicsTasks = true;
						break;
					}
				}
			}

			if (m_hasCodependentPhysicsTasks)
			{
				// Pathological: fall back to a reference pose.
				RegisterTask<ReferencePoseTask>();
				m_taskContext.m_dependencies.Clear();
				m_tasks[m_tasks.Size() - 1]->Execute(m_taskContext);
			}
			else
			{
				for (i8 prePhysicsTaskIdx : m_prePhysicsTaskIndices)
				{
					m_taskContext.m_currentTaskIdx = prePhysicsTaskIdx;
					m_taskContext.m_dependencies.Clear();
					for (i8 depIdx : m_tasks[prePhysicsTaskIdx]->GetDependencyIndices())
					{
						m_taskContext.m_dependencies.EmplaceBack(m_tasks[depIdx]);
					}
					m_tasks[prePhysicsTaskIdx]->Execute(m_taskContext);
				}
			}
		}
		else // No physics dependency: just run everything now.
		{
			ExecuteTasks();
		}
	}

	void TaskSystem::UpdatePostPhysics()
	{
		m_taskContext.m_updateStage = TaskUpdateStage::PostPhysics;

		if (m_hasCodependentPhysicsTasks) return;

		// Run the remaining (post-physics) tasks; if no physics dependency, all already ran.
		if (m_hasPhysicsDependency) ExecuteTasks();

		// Produce the final pose buffer from the last task.
		if (!m_tasks.Empty())
		{
			Task*       pFinalTask = m_tasks[m_tasks.Size() - 1];
			PoseBuffer* pResultBuffer = m_posePool.GetBuffer(pFinalTask->GetResultBufferIndex());

			// Always return a non-additive pose.
			if (pResultBuffer->IsAdditive())
			{
				Blender::ApplyAdditiveToReferencePose(m_taskContext.m_skeletonLOD, pResultBuffer->GetPrimaryPose(), 1.0f, nullptr, m_finalPoseBuffer.GetPrimaryPose());
			}
			else
			{
				m_finalPoseBuffer.CopyFrom(*pResultBuffer);
			}

			m_finalPoseBuffer.CalculateModelSpaceTransforms();
			m_posePool.ReleasePoseBuffer(pFinalTask->GetResultBufferIndex());
		}
		else
		{
			m_finalPoseBuffer.ResetPose(Pose::Type::ReferencePose, true);
		}
	}

	void TaskSystem::ExecuteTasks()
	{
		i8 numTasks = static_cast<i8>(m_tasks.Size());
		for (i8 i = 0; i < numTasks; ++i)
		{
			if (!m_tasks[i]->IsComplete())
			{
				m_taskContext.m_currentTaskIdx = i;
				m_taskContext.m_dependencies.Clear();
				for (i8 depIdx : m_tasks[i]->GetDependencyIndices())
				{
					m_taskContext.m_dependencies.EmplaceBack(m_tasks[depIdx]);
				}
				m_tasks[i]->Execute(m_taskContext);
			}
		}
		m_needsUpdate = false;
	}
}
