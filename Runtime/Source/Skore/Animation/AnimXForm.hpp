#pragma once

#include "Skore/Core/Math.hpp"

namespace Skore::Anim
{
	// Sentinel for "no bone / no index" (Esoterica's InvalidIndex). -1 so it reads as a
	// root parent and is distinct from any valid bone index.
	constexpr i32 InvalidIndex = -1;

	// Inverse of a unit quaternion (rotation) == its conjugate. Animation quaternions are
	// always unit-length, so the conjugate is the inverse.
	inline Quat QuatInverse(const Quat& q)
	{
		return Quat(-q.x, -q.y, -q.z, q.w);
	}

	// Esoterica blends rotations with a "fast" normalized-lerp slerp. Skore only ships a
	// true Slerp; alias to it for now (correctness first — swap in an NLerp optimization
	// later if profiling wants it). Risk R4.
	inline Quat FastSlerp(const Quat& a, const Quat& b, f32 t)
	{
		return Quat::Slerp(a, b, t);
	}

	// The local rotation that takes a model-space `parent` rotation to a model-space
	// `child` rotation, i.e. the delta `d` with `childModel = parentModel * d`. Esoterica's
	// Quaternion::Delta, expressed for Skore's (Hamilton) multiply convention.
	inline Quat QuatDelta(const Quat& parent, const Quat& child)
	{
		return QuatInverse(parent) * child;
	}

	// Per-bone transform value type: Quat rotation + translation + uniform scale.
	//
	// Deliberately NOT Skore's Transform (which is an ECS component) — every bone of
	// every pose is an XForm, and Esoterica stores uniform scale per bone, which we
	// follow. See Docs/AnimationSystemPort.md §3.1.
	struct XForm
	{
		// NOTE: Skore's Quat() default-constructs to {0,0,0,0}, so identity is explicit.
		Quat m_rotation    = Quat(0.0f, 0.0f, 0.0f, 1.0f);
		Vec3 m_translation = {0.0f, 0.0f, 0.0f};
		f32  m_scale       = 1.0f;

		static XForm Identity() { return XForm{}; }

		// Compose: `*this` is a bone's parent-space (local) transform; `parent` is its
		// parent's model-space (global) transform. Returns the bone's model-space
		// transform — i.e. childGlobal = childLocal * parentGlobal (Spec §3.1, matches
		// Esoterica's Pose::CalculateModelSpaceTransforms).
		XForm operator*(const XForm& parent) const
		{
			XForm result;
			result.m_rotation    = parent.m_rotation * m_rotation;             // apply child then parent
			result.m_scale       = parent.m_scale * m_scale;
			result.m_translation = parent.m_translation + parent.m_rotation * (m_translation * parent.m_scale);
			return result;
		}

		// Inverse transform: X^-1 such that (X^-1 * X) == identity.
		XForm GetInverse() const
		{
			XForm result;
			result.m_rotation    = QuatInverse(m_rotation);
			result.m_scale       = 1.0f / m_scale;
			result.m_translation = (result.m_rotation * (m_translation * -1.0f)) * result.m_scale;
			return result;
		}
	};
}
