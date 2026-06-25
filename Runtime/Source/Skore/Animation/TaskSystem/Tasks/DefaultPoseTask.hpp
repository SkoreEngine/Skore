#pragma once

#include "Skore/Animation/TaskSystem/Task.hpp"

namespace Skore::Anim
{
	// Produces the skeleton's bind (reference) pose into a fresh buffer.
	class SK_API ReferencePoseTask final : public Task
	{
	public:
		ReferencePoseTask() : Task() {}
		void Execute(const TaskContext& context) override;
	};

	// Produces the additive-identity (zero) pose into a fresh buffer.
	class SK_API ZeroPoseTask final : public Task
	{
	public:
		ZeroPoseTask() : Task() {}
		void Execute(const TaskContext& context) override;
	};
}
