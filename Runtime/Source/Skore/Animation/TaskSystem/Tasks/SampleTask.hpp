#pragma once

#include "Skore/Animation/TaskSystem/Task.hpp"
#include "Skore/Animation/Percentage.hpp"

namespace Skore::Anim
{
	class AnimationClip;

	// Samples a clip at a given percentage-through into a fresh buffer.
	class SK_API SampleTask final : public Task
	{
	public:
		SampleTask(const AnimationClip* animation, Percentage time);
		void Execute(const TaskContext& context) override;

	private:
		const AnimationClip* m_animation = nullptr;
		Percentage           m_time;
	};
}
