#include "PoseBufferPool.hpp"

#include "Skore/Animation/AnimationSkeleton.hpp"

namespace Skore::Anim
{
	PoseBufferPool::PoseBufferPool(const Skeleton* skeleton)
		: m_skeleton(skeleton)
	{
		for (i8 i = 0; i < s_numInitialBuffers; ++i)
		{
			m_poseBuffers.EmplaceBack(new PoseBuffer(skeleton));
		}
	}

	PoseBufferPool::~PoseBufferPool()
	{
		for (PoseBuffer* buffer : m_poseBuffers)
		{
			delete buffer;
		}
		m_poseBuffers.Clear();
	}

	void PoseBufferPool::Reset()
	{
		for (PoseBuffer* buffer : m_poseBuffers)
		{
			buffer->Release();
		}
		m_firstFreeBuffer = 0;
	}

	i8 PoseBufferPool::RequestPoseBuffer()
	{
		if (m_firstFreeBuffer == static_cast<i8>(m_poseBuffers.Size()))
		{
			for (i8 i = 0; i < s_bufferGrowAmount; ++i)
			{
				m_poseBuffers.EmplaceBack(new PoseBuffer(m_skeleton));
			}
		}

		i8 freeBufferIdx = m_firstFreeBuffer;
		m_poseBuffers[freeBufferIdx]->m_isUsed = true;

		// Advance the free index to the next unused buffer.
		i8 numBuffers = static_cast<i8>(m_poseBuffers.Size());
		for (; m_firstFreeBuffer < numBuffers; ++m_firstFreeBuffer)
		{
			if (!m_poseBuffers[m_firstFreeBuffer]->m_isUsed) break;
		}

		return freeBufferIdx;
	}

	void PoseBufferPool::ReleasePoseBuffer(i8 bufferIdx)
	{
		m_poseBuffers[bufferIdx]->m_isUsed = false;
		if (bufferIdx < m_firstFreeBuffer) m_firstFreeBuffer = bufferIdx;
	}
}
