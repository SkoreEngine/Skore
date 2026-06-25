#pragma once

#include "Skore/Animation/TaskSystem/Task.hpp"

namespace Skore::Anim
{
	class BoneMask;

	// Shared base for the two-input blend tasks: transfers dep0 (becomes the output) and
	// borrows dep1. S2 takes an optional raw BoneMask* (the caller keeps it alive for the
	// frame); the deferred BoneMaskTaskList materialization is M8.
	class SK_API BlendTaskBase : public Task
	{
	public:
		BlendTaskBase(i8 sourceTaskIdx, i8 targetTaskIdx, f32 blendWeight, const BoneMask* boneMask = nullptr);

	protected:
		f32             m_blendWeight = 0.0f;
		const BoneMask* m_boneMask = nullptr;
	};

	// Parent-space blend of source -> target.
	class SK_API BlendTask final : public BlendTaskBase
	{
	public:
		using BlendTaskBase::BlendTaskBase;
		void Execute(const TaskContext& context) override;
	};

	// Additive blend: applies the target (an additive pose) on top of the source.
	class SK_API AdditiveBlendTask final : public BlendTaskBase
	{
	public:
		using BlendTaskBase::BlendTaskBase;
		void Execute(const TaskContext& context) override;
	};
}
