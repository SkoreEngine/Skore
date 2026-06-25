#include "DefaultPoseTask.hpp"

namespace Skore::Anim
{
	void ReferencePoseTask::Execute(const TaskContext& context)
	{
		PoseBuffer* pResult = GetNewPoseBuffer(context);
		pResult->ResetPose(Pose::Type::ReferencePose);
		MarkTaskComplete(context);
	}

	void ZeroPoseTask::Execute(const TaskContext& context)
	{
		PoseBuffer* pResult = GetNewPoseBuffer(context);
		pResult->ResetPose(Pose::Type::ZeroPose);
		MarkTaskComplete(context);
	}
}
