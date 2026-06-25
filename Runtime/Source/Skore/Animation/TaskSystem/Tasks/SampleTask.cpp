#include "SampleTask.hpp"

#include "Skore/Animation/AnimationClip.hpp"

namespace Skore::Anim
{
	SampleTask::SampleTask(const AnimationClip* animation, Percentage time)
		: m_animation(animation)
		, m_time(time)
	{
	}

	void SampleTask::Execute(const TaskContext& context)
	{
		PoseBuffer* pResult = GetNewPoseBuffer(context);
		m_animation->Sample(*pResult->GetPrimaryPose(), m_time);
		MarkTaskComplete(context);
	}
}
