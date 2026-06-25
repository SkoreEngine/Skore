#pragma once

#include "Skore/Core/Math.hpp"

#include <cmath>

namespace Skore::Anim
{
	// A thin f32 wrapper carrying loop-count / normalized-time helpers. Ported from
	// Esoterica's `Percentage` (Base/Types/Percentage.h). Semantically a "percentage
	// through" a clip or sync track: the fractional part is [0,1); the integer part
	// encodes loop count (e.g. 2.5 == looped twice, halfway through the third pass).
	// See Docs/AnimationSystemPort.md §4.
	class Percentage
	{
	public:
		Percentage() = default;
		constexpr Percentage(f32 value) : m_value(value) {}

		constexpr operator f32() const { return m_value; }
		constexpr f32 ToFloat() const { return m_value; }

		i32 GetLoopCount() const { return static_cast<i32>(std::floor(m_value)); }

		void       Invert() { m_value = 1.0f - m_value; }
		Percentage GetInverse() const { return Percentage(1.0f - m_value); }

		// Split into integer loop count + normalized [0,1) remainder.
		void GetLoopCountAndNormalizedTime(i32& loopCount, Percentage& normalized) const
		{
			f32 integerPortion;
			normalized = Percentage(std::modf(m_value, &integerPortion));
			loopCount = static_cast<i32>(integerPortion);
		}

		// Normalized time in [0,1]. A value landing exactly on a loop boundary (1.0, 2.0,
		// …) returns 1.0 rather than 0.0, so "end of clip" reads as fully through.
		Percentage GetNormalizedTime() const
		{
			f32 loopCount;
			f32 normalized = std::modf(m_value, &loopCount);
			if (loopCount > 0 && normalized == 0.0f) normalized = 1.0f;
			return Percentage(normalized);
		}

		// Clamp to [0,1]; if allowLooping, wrap into [0,1) instead of clamping.
		// TODO(S3): verify exact looping/boundary semantics against Esoterica
		// Percentage.cpp when the clip-node time advance is ported.
		static Percentage Clamp(Percentage value, bool allowLooping = true)
		{
			if (allowLooping)
			{
				f32 intPart;
				f32 frac = std::modf(value.m_value, &intPart);
				if (frac < 0.0f) frac += 1.0f;
				return Percentage(frac);
			}
			return Percentage(Math::Clamp(value.m_value, 0.0f, 1.0f));
		}

		Percentage  GetClamped(bool allowLooping = true) const { return Clamp(*this, allowLooping); }
		Percentage& Clamp(bool allowLooping = true) { *this = Clamp(*this, allowLooping); return *this; }

		constexpr Percentage  operator+(Percentage f) const { return Percentage(m_value + f.m_value); }
		constexpr Percentage  operator-(Percentage f) const { return Percentage(m_value - f.m_value); }
		Percentage&           operator+=(Percentage f) { m_value += f.m_value; return *this; }
		Percentage&           operator-=(Percentage f) { m_value -= f.m_value; return *this; }
		constexpr Percentage  operator*(Percentage f) const { return Percentage(m_value * f.m_value); }
		constexpr Percentage  operator/(Percentage f) const { return Percentage(m_value / f.m_value); }

	private:
		f32 m_value = 0.0f;
	};
}
