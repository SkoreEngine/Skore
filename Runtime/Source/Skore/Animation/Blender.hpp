#pragma once

#include "Pose.hpp"
#include "AnimXForm.hpp"

namespace Skore::Anim
{
	class BoneMask;

	enum class RootMotionBlendMode : u8
	{
		Blend = 0,
		Additive,
		IgnoreSource,
		IgnoreTarget
	};

	// All pose-blending operations. Port of Esoterica's Blender — the make-or-break math of
	// the system. See Docs/AnimationSystemPort.md §4.6–4.9.
	//
	// PORT NOTE: Esoterica's quaternion multiply is the reverse of Skore's (Hamilton), so
	// every explicit quat multiply ported here has its operands swapped. Translation and the
	// uniform scale are blended as the two parts of Esoterica's packed translation+scale.
	class SK_API Blender
	{
	public:
		// Parent-space blend source -> target by blendWeight; optional bone mask (per-bone
		// weight multiplies into blendWeight: mask 0 => source, mask 1 => full blend).
		static void ParentSpaceBlend(Skeleton::LOD lod, const Pose* source, const Pose* target, f32 blendWeight, const BoneMask* boneMask, Pose* result);

		// Parent-space additive blend: applies `target` (an additive pose) on top of `source`.
		static void AdditiveBlend(Skeleton::LOD lod, const Pose* source, const Pose* target, f32 blendWeight, const BoneMask* boneMask, Pose* result);

		// Blend source -> the skeleton's reference (bind) pose. (boneMask param is accepted
		// for API parity but unused, as in Esoterica.)
		static void ParentSpaceBlendToReferencePose(Skeleton::LOD lod, const Pose* source, f32 blendWeight, const BoneMask* boneMask, Pose* result);

		// Blend the reference (bind) pose -> target.
		static void ParentSpaceBlendFromReferencePose(Skeleton::LOD lod, const Pose* target, f32 blendWeight, const BoneMask* boneMask, Pose* result);

		// Bake an additive pose onto the reference pose, producing a normal pose (used to
		// "flatten" an additive final pose before skinning).
		static void ApplyAdditiveToReferencePose(Skeleton::LOD lod, const Pose* additivePose, f32 blendWeight, const BoneMask* boneMask, Pose* result);

		// Model-space (global) overlay blend — blends rotations in model space so an overlay
		// feels world-correct. REQUIRES a bone mask. Used by the layer system (S6); not
		// exercised by the S1 gate, so treat as best-effort until then.
		static void ModelSpaceBlend(Skeleton::LOD lod, const Pose* basePose, const Pose* layerPose, f32 layerWeight, const BoneMask* boneMask, Pose* result);

		// Blend two root-motion deltas together.
		static XForm BlendRootMotionDeltas(const XForm& source, const XForm& target, f32 blendWeight, RootMotionBlendMode mode = RootMotionBlendMode::Blend);
	};
}
