#pragma once

#include "PoseBufferPool.hpp"
#include "Skore/Animation/AnimXForm.hpp"
#include "Skore/Animation/AnimationSkeleton.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore::Anim
{
	class Task;

	enum class TaskUpdateStage : u8
	{
		Any = 0,
		PrePhysics,
		PostPhysics,
	};

	using TaskDependencies = Array<i8>; // most tasks have <= 2 dependencies

	// Per-frame execution context handed to each Task::Execute.
	struct TaskContext
	{
		explicit TaskContext(PoseBufferPool& posePool) : m_posePool(posePool) {}

		XForm           m_worldTransform = XForm::Identity();
		XForm           m_worldTransformInverse = XForm::Identity();
		Array<Task*>    m_dependencies;          // resolved dependency task pointers
		PoseBufferPool& m_posePool;
		f32             m_deltaTime = 0.0f;
		TaskUpdateStage m_updateStage = TaskUpdateStage::Any;
		i8              m_currentTaskIdx = static_cast<i8>(InvalidIndex);
		Skeleton::LOD   m_skeletonLOD = Skeleton::LOD::High;
	};

	// A deferred pose-computation step. Evaluating the graph only *registers* tasks; the
	// TaskSystem executes this DAG to produce poses. Zero-copy reuse comes from Transfer
	// (steal a dependency's buffer) vs Access (borrow read-only). Port of Esoterica's Task.
	// Serialization + dev-tools + cached poses are deferred. Spec §7.
	class SK_API Task
	{
	public:
		Task(TaskUpdateStage updateStage = TaskUpdateStage::Any, const TaskDependencies& dependencies = TaskDependencies())
			: m_updateStage(updateStage), m_dependencies(dependencies) {}
		virtual ~Task() = default;

		virtual void Execute(const TaskContext& context) = 0;

		i8                      GetResultBufferIndex() const { return m_bufferIdx; }
		bool                    IsComplete() const { return m_isComplete; }
		const TaskDependencies& GetDependencyIndices() const { return m_dependencies; }
		i32                     GetNumDependencies() const { return static_cast<i32>(m_dependencies.Size()); }
		TaskUpdateStage         GetRequiredUpdateStage() const { return m_updateStage; }
		TaskUpdateStage         GetActualUpdateStage() const { return m_actualUpdateStage; }
		bool                    HasPhysicsDependency() const { return m_updateStage != TaskUpdateStage::Any; }

	protected:
		// Request a fresh buffer and make it our result.
		PoseBuffer* GetNewPoseBuffer(const TaskContext& context)
		{
			m_bufferIdx = context.m_posePool.RequestPoseBuffer();
			return context.m_posePool.GetBuffer(m_bufferIdx);
		}

		void ReleasePoseBuffer(const TaskContext& context)
		{
			context.m_posePool.ReleasePoseBuffer(m_bufferIdx);
			m_bufferIdx = static_cast<i8>(InvalidIndex);
		}

		// STEAL a dependency's result buffer (it had no other consumer) and reuse it as our
		// output -> no allocation, no copy.
		PoseBuffer* TransferDependencyPoseBuffer(const TaskContext& context, i8 dependencyIdx)
		{
			Task* pDependencyTask = context.m_dependencies[dependencyIdx];
			m_bufferIdx = pDependencyTask->m_bufferIdx;
			pDependencyTask->m_bufferIdx = static_cast<i8>(InvalidIndex);
			return context.m_posePool.GetBuffer(m_bufferIdx);
		}

		// BORROW a dependency's result buffer read-only; must Release it when done.
		PoseBuffer* AccessDependencyPoseBuffer(const TaskContext& context, i8 dependencyIdx)
		{
			Task* pDependencyTask = context.m_dependencies[dependencyIdx];
			return context.m_posePool.GetBuffer(pDependencyTask->m_bufferIdx);
		}

		void ReleaseDependencyPoseBuffer(const TaskContext& context, i8 dependencyIdx)
		{
			context.m_dependencies[dependencyIdx]->ReleasePoseBuffer(context);
		}

		void MarkTaskComplete(const TaskContext& context)
		{
			m_isComplete = true;
			m_actualUpdateStage = context.m_updateStage;
		}

	protected:
		TaskUpdateStage  m_updateStage = TaskUpdateStage::Any;
		i8               m_bufferIdx = static_cast<i8>(InvalidIndex);
		TaskUpdateStage  m_actualUpdateStage = TaskUpdateStage::Any;
		bool             m_isComplete = false;
		TaskDependencies m_dependencies;
	};
}
