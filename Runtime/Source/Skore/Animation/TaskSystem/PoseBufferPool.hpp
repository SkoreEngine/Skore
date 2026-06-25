#pragma once

#include "Skore/Animation/Pose.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore::Anim
{
	class Skeleton;

	// Storage for one skeleton's pose during a task-system update. Esoterica's PoseBuffer
	// holds primary + secondary-skeleton poses; S2 is single-skeleton, so just one Pose.
	// (Secondary skeletons are deferred.) Spec §7.3.
	class SK_API PoseBuffer
	{
		friend class PoseBufferPool;

	public:
		explicit PoseBuffer(const Skeleton* skeleton) : m_pose(skeleton) {}

		void        CopyFrom(const PoseBuffer& rhs) { m_pose.CopyFrom(rhs.m_pose); }
		bool        IsPoseSet() const { return m_pose.IsPoseSet(); }
		bool        IsAdditive() const { return m_pose.IsAdditivePose(); }
		Pose*       GetPrimaryPose() { return &m_pose; }
		const Pose* GetPrimaryPose() const { return &m_pose; }
		void        ResetPose(Pose::Type type = Pose::Type::None, bool calcModelSpace = false) { m_pose.Reset(type, calcModelSpace); }
		void        CalculateModelSpaceTransforms() { m_pose.CalculateModelSpaceTransforms(); }

	private:
		void Release(Pose::Type type = Pose::Type::None, bool calcModelSpace = false)
		{
			m_pose.Reset(type, calcModelSpace);
			m_isUsed = false;
		}

		Pose m_pose;
		bool m_isUsed = false;
	};

	// Pool of pose buffers enabling zero-copy reuse: tasks Transfer (steal), Access (borrow)
	// and Release buffers by index. Buffers are heap-owned so their pointers stay stable as
	// the pool grows mid-frame. Cached poses (forced transitions) are deferred to M6.
	class SK_API PoseBufferPool
	{
		static constexpr i8 s_numInitialBuffers = 6;
		static constexpr i8 s_bufferGrowAmount = 3;

	public:
		explicit PoseBufferPool(const Skeleton* skeleton);
		PoseBufferPool(const PoseBufferPool&) = delete;
		PoseBufferPool& operator=(const PoseBufferPool&) = delete;
		~PoseBufferPool();

		void Reset();

		const Skeleton* GetSkeleton() const { return m_skeleton; }

		i8          RequestPoseBuffer();
		void        ReleasePoseBuffer(i8 bufferIdx);
		PoseBuffer* GetBuffer(i8 bufferIdx) { return m_poseBuffers[bufferIdx]; }

	private:
		Array<PoseBuffer*> m_poseBuffers; // heap-owned -> pointers stay stable across growth
		i8                 m_firstFreeBuffer = 0;
		const Skeleton*    m_skeleton = nullptr;
	};
}
