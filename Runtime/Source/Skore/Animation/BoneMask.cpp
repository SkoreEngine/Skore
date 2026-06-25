#include "BoneMask.hpp"

#include "AnimationSkeleton.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore::Anim
{
	namespace
	{
		bool NearEqual(f32 a, f32 b) { return Math::Abs(a - b) < Epsilon; }
		f32  Lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
	}

	BoneMask::BoneMask(const Skeleton* skeleton)
		: m_skeleton(skeleton)
	{
		m_weights.Resize(skeleton->GetNumBones(), 0.0f);
		m_weightInfo = WeightInfo::Zero;
	}

	BoneMask::BoneMask(const Skeleton* skeleton, f32 fixedWeight)
		: m_skeleton(skeleton)
	{
		ResetWeights(fixedWeight);
	}

	bool BoneMask::IsValid() const
	{
		return m_skeleton != nullptr && !m_weights.Empty() && static_cast<i32>(m_weights.Size()) == m_skeleton->GetNumBones();
	}

	void BoneMask::SetWeightInfo(f32 fixedWeight)
	{
		if (fixedWeight == 0.0f)      m_weightInfo = WeightInfo::Zero;
		else if (fixedWeight == 1.0f) m_weightInfo = WeightInfo::One;
		else                          m_weightInfo = WeightInfo::Mixed;
	}

	void BoneMask::ResetWeights()
	{
		const usize n = m_weights.Size();
		for (usize i = 0; i < n; ++i) m_weights[i] = 0.0f;
		m_weightInfo = WeightInfo::Zero;
	}

	void BoneMask::ResetWeights(f32 fixedWeight)
	{
		m_weights.Resize(m_skeleton->GetNumBones());
		const usize n = m_weights.Size();
		for (usize i = 0; i < n; ++i) m_weights[i] = fixedWeight;
		SetWeightInfo(fixedWeight);
	}

	void BoneMask::ResetWeights(const Array<f32>& weights)
	{
		m_weights.Resize(weights.Size());
		const usize n = m_weights.Size();
		for (usize i = 0; i < n; ++i) m_weights[i] = weights[i];

		// Recompute the fast-path flag.
		m_weightInfo = WeightInfo::Zero;
		f32 fixedWeight = n > 0 ? m_weights[0] : 0.0f;
		for (usize i = 0; i < n; ++i)
		{
			if (m_weights[i] != fixedWeight)
			{
				m_weightInfo = WeightInfo::Mixed;
				return;
			}
		}
		SetWeightInfo(fixedWeight);
	}

	BoneMask& BoneMask::operator*=(const BoneMask& rhs)
	{
		const usize n = m_weights.Size();
		for (usize i = 0; i < n; ++i) m_weights[i] *= rhs.m_weights[i];
		// NOTE: like Esoterica, the fast-path flag is left as-is here (combine is used by
		// the bone-mask task system, S6, which manages weightInfo explicitly).
		return *this;
	}

	void BoneMask::ScaleWeights(f32 scale)
	{
		if (scale == 1.0f) return;
		if (scale == 0.0f) { ResetWeights(); return; }

		const usize n = m_weights.Size();
		for (usize i = 0; i < n; ++i) m_weights[i] *= scale;
		m_weightInfo = WeightInfo::Mixed;
	}

	void BoneMask::BlendFrom(const BoneMask& source, f32 blendWeight)
	{
		// 0 = fully source, 1 = fully this(target).
		if (&source == this) return;
		if (source.m_weightInfo != WeightInfo::Mixed && source.m_weightInfo == m_weightInfo) return;
		if (NearEqual(blendWeight, 1.0f)) return; // fully in this mask
		if (NearEqual(blendWeight, 0.0f))         // fully in source
		{
			m_weights = source.m_weights;
			m_weightInfo = source.m_weightInfo;
			return;
		}

		const usize n = m_weights.Size();
		for (usize i = 0; i < n; ++i) m_weights[i] = Lerp(source.m_weights[i], m_weights[i], blendWeight);
		m_weightInfo = WeightInfo::Mixed;
	}

	void BoneMask::BlendTo(const BoneMask& target, f32 blendWeight)
	{
		// 0 = fully this(source), 1 = fully target.
		if (&target == this) return;
		if (target.m_weightInfo != WeightInfo::Mixed && target.m_weightInfo == m_weightInfo) return;
		if (NearEqual(blendWeight, 0.0f)) return; // fully in this mask
		if (NearEqual(blendWeight, 1.0f))         // fully in target
		{
			m_weights = target.m_weights;
			m_weightInfo = target.m_weightInfo;
			return;
		}

		const usize n = m_weights.Size();
		for (usize i = 0; i < n; ++i) m_weights[i] = Lerp(m_weights[i], target.m_weights[i], blendWeight);
		m_weightInfo = WeightInfo::Mixed;
	}
}
