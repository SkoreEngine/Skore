#pragma once

#include "Skore/Core/Array.hpp"

namespace Skore::Anim
{
	class Skeleton;

	// Per-bone blend weights in [0,1]. A bone mask multiplies into the global blend weight
	// to drive partial-body animation (mask 0 => source, mask 1 => full blend). Port of
	// Esoterica's BoneMask runtime class. Spec §3.3, §4.7.
	//
	// S1: weights are stored 1:1 with bones (no SIMD 4-padding). The serialized/feathered
	// constructor (authored masks) and the deferred BoneMaskPool / BoneMaskTaskList (the
	// task-system materialization) are deferred to S6 / S2 respectively.
	class SK_API BoneMask
	{
	public:
		enum class WeightInfo : u8
		{
			Zero, // all weights 0
			Mixed,
			One   // all weights 1
		};

	public:
		BoneMask() = default;
		explicit BoneMask(const Skeleton* skeleton);         // all-zero mask
		BoneMask(const Skeleton* skeleton, f32 fixedWeight); // uniform mask

		bool            IsValid() const;
		const Skeleton* GetSkeleton() const { return m_skeleton; }

		i32 GetNumWeights() const { return static_cast<i32>(m_weights.Size()); }
		f32 GetWeight(u32 i) const { return m_weights[i]; }
		f32 operator[](u32 i) const { return m_weights[i]; }

		// Fast-path flags the Blender uses to skip work.
		bool IsZeroWeightMask() const { return m_weightInfo == WeightInfo::Zero; }
		bool IsFullWeightMask() const { return m_weightInfo == WeightInfo::One; }
		bool IsMixedWeightMask() const { return m_weightInfo == WeightInfo::Mixed; }

		void ResetWeights();                          // all zero
		void ResetWeights(f32 fixedWeight);           // uniform
		void ResetWeights(const Array<f32>& weights); // per-bone

		void      ScaleWeights(f32 scale);
		void      CombineWith(const BoneMask& rhs) { operator*=(rhs); }
		BoneMask& operator*=(const BoneMask& rhs);

		// Blend [0:1] where 0 = fully source, 1 = fully target.
		void BlendFrom(const BoneMask& source, f32 blendWeight);
		void BlendTo(const BoneMask& target, f32 blendWeight);

	private:
		void SetWeightInfo(f32 fixedWeight);

		const Skeleton* m_skeleton = nullptr;
		WeightInfo      m_weightInfo = WeightInfo::Zero;
		Array<f32>      m_weights;
	};
}
