#include "doctest.h"

#include "Skore/Animation/Percentage.hpp"
#include "Skore/Animation/SyncTrack.hpp"
#include "Skore/Animation/AnimXForm.hpp"
#include "Skore/Animation/AnimationSkeleton.hpp"
#include "Skore/Animation/AnimationClip.hpp"
#include "Skore/Animation/Pose.hpp"
#include "Skore/Animation/BoneMask.hpp"
#include "Skore/Animation/Blender.hpp"
#include "Skore/Animation/TaskSystem/TaskSystem.hpp"
#include "Skore/Animation/TaskSystem/Tasks/DefaultPoseTask.hpp"
#include "Skore/Animation/TaskSystem/Tasks/BlendTask.hpp"
#include "Skore/Animation/Graph/GraphDefinition.hpp"
#include "Skore/Animation/Graph/GraphInstance.hpp"
#include "Skore/Animation/Graph/Nodes/ReferencePoseNode.hpp"
#include "Skore/Animation/Graph/Nodes/AnimationClipNode.hpp"
#include "Skore/Animation/Graph/Nodes/ControlParameterNodes.hpp"
#include "Skore/Animation/Graph/Nodes/Blend1DNode.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Core/Math.hpp"

// S1 gate (Docs/AnimationPortProgress.md): the make-or-break sync + blend math. These run
// against the ported value types with no scene/engine setup. NOTE: Skeleton bone rotations
// must be spelled {0,0,0,1} — Skore's Quat() default-constructs to {0,0,0,0}.

using namespace Skore;

namespace
{
	void CheckVec3Approx(const Vec3& a, const Vec3& b, f32 eps = 0.001f)
	{
		CHECK(Math::Abs(a.x - b.x) < eps);
		CHECK(Math::Abs(a.y - b.y) < eps);
		CHECK(Math::Abs(a.z - b.z) < eps);
	}

	void CheckQuatApprox(const Quat& a, const Quat& b, f32 eps = 0.002f)
	{
		CHECK(Math::Abs(a.x - b.x) < eps);
		CHECK(Math::Abs(a.y - b.y) < eps);
		CHECK(Math::Abs(a.z - b.z) < eps);
		CHECK(Math::Abs(a.w - b.w) < eps);
	}

	void CheckXFormApprox(const Anim::XForm& a, const Anim::XForm& b)
	{
		constexpr f32 eps = 0.001f;
		CHECK(Math::Abs(a.m_translation.x - b.m_translation.x) < eps);
		CHECK(Math::Abs(a.m_translation.y - b.m_translation.y) < eps);
		CHECK(Math::Abs(a.m_translation.z - b.m_translation.z) < eps);
		CHECK(Math::Abs(a.m_rotation.x - b.m_rotation.x) < eps);
		CHECK(Math::Abs(a.m_rotation.y - b.m_rotation.y) < eps);
		CHECK(Math::Abs(a.m_rotation.z - b.m_rotation.z) < eps);
		CHECK(Math::Abs(a.m_rotation.w - b.m_rotation.w) < eps);
		CHECK(Math::Abs(a.m_scale - b.m_scale) < eps);
	}

	// Build a simple 3-bone chain skeleton (root -> b1 -> b2) for blend tests.
	Anim::Skeleton MakeTestSkeleton(Skeleton& sceneSkel)
	{
		sceneSkel.bones.Resize(3);
		sceneSkel.bones[0].name = "root";
		sceneSkel.bones[0].parentIndex = U32_MAX;
		sceneSkel.bones[0].position = {0, 0, 0};
		sceneSkel.bones[0].rotation = {0, 0, 0, 1};
		sceneSkel.bones[0].scale = {1, 1, 1};

		sceneSkel.bones[1].name = "b1";
		sceneSkel.bones[1].parentIndex = 0;
		sceneSkel.bones[1].position = {0, 1, 0};
		sceneSkel.bones[1].rotation = {0, 0, 0, 1};
		sceneSkel.bones[1].scale = {1, 1, 1};

		sceneSkel.bones[2].name = "b2";
		sceneSkel.bones[2].parentIndex = 1;
		sceneSkel.bones[2].position = {0, 1, 0};
		sceneSkel.bones[2].rotation = {0, 0, 0, 1};
		sceneSkel.bones[2].scale = {1, 1, 1};

		Anim::Skeleton skel;
		skel.BuildFrom(&sceneSkel);
		return skel;
	}
}

TEST_CASE("Animation::Percentage")
{
	Anim::Percentage p(2.5f);
	i32             loops = 0;
	Anim::Percentage norm;
	p.GetLoopCountAndNormalizedTime(loops, norm);
	CHECK(loops == 2);
	CHECK(norm.ToFloat() == doctest::Approx(0.5f));

	// A value exactly on a loop boundary normalizes to 1.0 (end of clip), not 0.0.
	CHECK(Anim::Percentage(1.0f).GetNormalizedTime().ToFloat() == doctest::Approx(1.0f));
	CHECK(Anim::Percentage(1.5f).GetClamped(false).ToFloat() == doctest::Approx(1.0f)); // clamp, no loop
}

TEST_CASE("Animation::SyncTrackRoundTrip")
{
	// Four events at 0, 0.25, 0.5, 0.75 -> durations 0.25 each.
	Array<Anim::SyncTrack::EventMarker> markers;
	markers.EmplaceBack(Anim::Percentage(0.0f), 1u);
	markers.EmplaceBack(Anim::Percentage(0.25f), 2u);
	markers.EmplaceBack(Anim::Percentage(0.5f), 3u);
	markers.EmplaceBack(Anim::Percentage(0.75f), 4u);
	Anim::SyncTrack track(markers);

	CHECK(track.GetNumEvents() == 4);

	// GetTime <-> GetPercentageThrough must round-trip.
	const f32 samples[] = {0.1f, 0.3f, 0.55f, 0.9f};
	for (f32 p : samples)
	{
		Anim::SyncTrackTime time = track.GetTime(Anim::Percentage(p));
		Anim::Percentage    back = track.GetPercentageThrough(time);
		CHECK(back.ToFloat() == doctest::Approx(p).epsilon(0.001f));
	}
}

TEST_CASE("Animation::SyncTrackLCMDuration")
{
	// Worked example (Spec §4.4): walk (4 events, 1.0s) blended 50% with run (2 events, 0.6s).
	// LCM(4,2)=4 -> scaled durations 1.0 and 1.2 -> lerp(.5) = 1.1s.
	f32 duration = Anim::SyncTrack::CalculateDurationSynchronized(1.0f, 0.6f, 4, 2, 4, 0.5f);
	CHECK(duration == doctest::Approx(1.1f));
}

TEST_CASE("Animation::BlendSelfIsIdentity")
{
	Skeleton      sceneSkel;
	Anim::Skeleton skel = MakeTestSkeleton(sceneSkel);

	Anim::Pose poseA(&skel);
	poseA.SetTransform(1, Anim::XForm{Quat(0, 0, 0, 1), Vec3{1, 2, 3}, 1.0f});
	poseA.SetTransform(2, Anim::XForm{Quat(0, 0, 0, 1), Vec3{4, 5, 6}, 1.0f});

	// Blending a pose with itself at any weight yields the same pose.
	Anim::Pose result(&skel);
	Anim::Blender::ParentSpaceBlend(Anim::Skeleton::LOD::High, &poseA, &poseA, 0.5f, nullptr, &result);
	for (i32 b = 0; b < skel.GetNumBones(); ++b)
	{
		CheckXFormApprox(result.GetTransform(b), poseA.GetTransform(b));
	}
}

TEST_CASE("Animation::BlendReferencePoseEndpoints")
{
	Skeleton      sceneSkel;
	Anim::Skeleton skel = MakeTestSkeleton(sceneSkel);

	Anim::Pose poseA(&skel);
	poseA.SetTransform(1, Anim::XForm{Quat(0, 0, 0, 1), Vec3{1, 2, 3}, 1.0f});

	Anim::Pose result(&skel);

	// w = 0 -> fully the source pose.
	Anim::Blender::ParentSpaceBlendToReferencePose(Anim::Skeleton::LOD::High, &poseA, 0.0f, nullptr, &result);
	for (i32 b = 0; b < skel.GetNumBones(); ++b)
	{
		CheckXFormApprox(result.GetTransform(b), poseA.GetTransform(b));
	}

	// w = 1 -> fully the reference (bind) pose.
	Anim::Blender::ParentSpaceBlendToReferencePose(Anim::Skeleton::LOD::High, &poseA, 1.0f, nullptr, &result);
	const Array<Anim::XForm>& ref = skel.GetParentSpaceReferencePose();
	for (i32 b = 0; b < skel.GetNumBones(); ++b)
	{
		CheckXFormApprox(result.GetTransform(b), ref[b]);
	}
}

TEST_CASE("Animation::BoneMaskFastPaths")
{
	Skeleton      sceneSkel;
	Anim::Skeleton skel = MakeTestSkeleton(sceneSkel);

	Anim::Pose poseA(&skel);
	poseA.SetTransform(1, Anim::XForm{Quat(0, 0, 0, 1), Vec3{1, 2, 3}, 1.0f});
	Anim::Pose poseB(&skel);
	poseB.SetTransform(1, Anim::XForm{Quat(0, 0, 0, 1), Vec3{-1, -2, -3}, 2.0f});

	Anim::BoneMask maskZero(&skel, 0.0f);
	Anim::BoneMask maskOne(&skel, 1.0f);
	Anim::Pose     result(&skel);

	// Mask 0 (with full weight) -> source pose for every bone.
	Anim::Blender::ParentSpaceBlend(Anim::Skeleton::LOD::High, &poseA, &poseB, 1.0f, &maskZero, &result);
	for (i32 b = 0; b < skel.GetNumBones(); ++b)
	{
		CheckXFormApprox(result.GetTransform(b), poseA.GetTransform(b));
	}

	// Mask 1 (with full weight) -> target pose for every bone.
	Anim::Blender::ParentSpaceBlend(Anim::Skeleton::LOD::High, &poseA, &poseB, 1.0f, &maskOne, &result);
	for (i32 b = 0; b < skel.GetNumBones(); ++b)
	{
		CheckXFormApprox(result.GetTransform(b), poseB.GetTransform(b));
	}
}

TEST_CASE("Animation::TaskSystemBlendDAG")
{
	Skeleton       sceneSkel;
	Anim::Skeleton skel = MakeTestSkeleton(sceneSkel);

	Anim::TaskSystem taskSystem(&skel);

	// Hand-build a Sample-shaped DAG without clips: ReferencePose, ReferencePose, Blend(0,1).
	// Blending two reference poses at any weight yields the reference pose.
	i8 a = taskSystem.RegisterTask<Anim::ReferencePoseTask>();
	i8 b = taskSystem.RegisterTask<Anim::ReferencePoseTask>();
	taskSystem.RegisterTask<Anim::BlendTask>(a, b, 0.5f);

	CHECK(taskSystem.HasTasks());

	taskSystem.UpdatePrePhysics(0.016f, Anim::XForm::Identity(), Anim::XForm::Identity());
	taskSystem.UpdatePostPhysics();

	const Anim::Pose*         finalPose = taskSystem.GetPrimaryPose();
	const Array<Anim::XForm>& ref = skel.GetParentSpaceReferencePose();
	for (i32 i = 0; i < skel.GetNumBones(); ++i)
	{
		CheckXFormApprox(finalPose->GetTransform(i), ref[i]);
	}
}

TEST_CASE("Animation::GraphInstanceReferencePose")
{
	Skeleton       sceneSkel;
	Anim::Skeleton skel = MakeTestSkeleton(sceneSkel);

	// Hand-build a one-node graph (a ReferencePoseNode root) — exercises the definition's
	// instance-memory layout + the instance's placement-new instantiation.
	Anim::GraphDefinition def;
	def.SetSkeleton(&skel);
	auto* refNodeDef = def.CreateNodeDefinition<Anim::ReferencePoseNode>();
	def.SetRootNodeIndex(refNodeDef->m_nodeIdx);
	CHECK(def.IsValid());

	Anim::GraphInstance instance(&def, 0);
	CHECK(instance.IsValid());

	// Evaluate (root registers a ReferencePoseTask) then execute the pose-task DAG.
	Anim::GraphPoseNodeResult result = instance.EvaluateGraph(0.016f, Anim::XForm::Identity());
	CHECK(result.HasRegisteredTasks());

	instance.ExecutePrePhysicsPoseTasks(Anim::XForm::Identity());
	instance.ExecutePostPhysicsPoseTasks();

	// The whole spine must produce the skeleton's reference pose.
	const Anim::Pose*         finalPose = instance.GetPrimaryPose();
	const Array<Anim::XForm>& ref = skel.GetParentSpaceReferencePose();
	for (i32 i = 0; i < skel.GetNumBones(); ++i)
	{
		CheckXFormApprox(finalPose->GetTransform(i), ref[i]);
	}
}

//-------------------------------------------------------------------------
// Numeric tests for the load-bearing / D13-sensitive math (XForm compose+inverse, FK,
// intermediate-weight blends, quat helpers, bone-mask ops, root motion). These assert
// hand-computed values, not just identity/fast-path cases.
//-------------------------------------------------------------------------

TEST_CASE("Animation::XFormComposeAndInverse")
{
	// Translation-only compose: child(1,0,0) in parent(0,2,0) -> (1,2,0).
	Anim::XForm child;
	child.m_translation = {1, 0, 0};
	Anim::XForm parent;
	parent.m_translation = {0, 2, 0};
	Anim::XForm composed = child * parent;
	CheckVec3Approx(composed.m_translation, Vec3{1, 2, 0});
	CheckQuatApprox(composed.m_rotation, Quat(0, 0, 0, 1));
	CHECK(Math::Abs(composed.m_scale - 1.0f) < 0.001f);

	// Scale compose: child scale 2 + translation (1,0,0) in parent scale 3 -> trans (3,0,0), scale 6.
	Anim::XForm childS;
	childS.m_translation = {1, 0, 0};
	childS.m_scale = 2.0f;
	Anim::XForm parentS;
	parentS.m_scale = 3.0f;
	Anim::XForm composedS = childS * parentS;
	CheckVec3Approx(composedS.m_translation, Vec3{3, 0, 0});
	CHECK(Math::Abs(composedS.m_scale - 6.0f) < 0.001f);

	// X * X^-1 and X^-1 * X must both be identity (validates compose AND inverse together).
	Anim::XForm x;
	x.m_rotation = Quat::AngleAxis(1.2f, Vec3{0, 1, 0});
	x.m_translation = {1, 2, 3};
	x.m_scale = 2.0f;
	Anim::XForm xInv = x.GetInverse();
	CheckXFormApprox(x * xInv, Anim::XForm::Identity());
	CheckXFormApprox(xInv * x, Anim::XForm::Identity());
}

TEST_CASE("Animation::QuatHelpers")
{
	Quat q = Quat::AngleAxis(Skore::HalfPI, Vec3{0, 1, 0});

	// Inverse of a unit quat composed with itself is identity.
	CheckQuatApprox(Anim::QuatInverse(q) * q, Quat(0, 0, 0, 1));

	// QuatDelta(parent, child) is the local rotation d with `parent * d == child` (Skore Hamilton).
	Quat parentRot = Quat::AngleAxis(0.5f, Vec3{0, 1, 0});
	Quat childRot  = Quat::AngleAxis(1.3f, Vec3{0, 1, 0});
	Quat delta     = Anim::QuatDelta(parentRot, childRot);
	CheckQuatApprox(parentRot * delta, childRot);
}

TEST_CASE("Animation::PoseModelSpaceFK")
{
	Skeleton       sceneSkel;
	Anim::Skeleton skel = MakeTestSkeleton(sceneSkel);

	// Bind-pose model-space ref (computed by BuildFrom): root(0,0,0) -> b1(0,1,0) -> b2(0,2,0).
	const Array<Anim::XForm>& modelRef = skel.GetModelSpaceReferencePose();
	CheckVec3Approx(modelRef[0].m_translation, Vec3{0, 0, 0});
	CheckVec3Approx(modelRef[1].m_translation, Vec3{0, 1, 0});
	CheckVec3Approx(modelRef[2].m_translation, Vec3{0, 2, 0});

	// Rotate b1 by 90 deg about Z; the chain swings so b2 lands at (-1, 1, 0).
	Anim::Pose  pose(&skel);
	Anim::XForm b1 = pose.GetTransform(1);
	b1.m_rotation = Quat::AngleAxis(Skore::HalfPI, Vec3{0, 0, 1});
	pose.SetTransform(1, b1);
	pose.CalculateModelSpaceTransforms();

	const Array<Anim::XForm>& model = pose.GetModelSpaceTransforms();
	CheckVec3Approx(model[0].m_translation, Vec3{0, 0, 0});
	CheckVec3Approx(model[1].m_translation, Vec3{0, 1, 0});
	CheckVec3Approx(model[2].m_translation, Vec3{-1, 1, 0});
}

TEST_CASE("Animation::BlendIntermediateWeight")
{
	Skeleton       sceneSkel;
	Anim::Skeleton skel = MakeTestSkeleton(sceneSkel);

	Anim::Pose  poseA(&skel);
	Anim::XForm a1 = poseA.GetTransform(1);
	a1.m_rotation = Quat(0, 0, 0, 1);
	a1.m_translation = {0, 0, 0};
	poseA.SetTransform(1, a1);

	Anim::Pose  poseB(&skel);
	Anim::XForm b1 = poseB.GetTransform(1);
	b1.m_rotation = Quat::AngleAxis(Skore::HalfPI, Vec3{0, 0, 1}); // 90 deg about Z
	b1.m_translation = {2, 0, 0};
	poseB.SetTransform(1, b1);

	Anim::Pose result(&skel);
	Anim::Blender::ParentSpaceBlend(Anim::Skeleton::LOD::High, &poseA, &poseB, 0.5f, nullptr, &result);

	// Halfway: rotation slerps to 45 deg about Z, translation lerps to (1,0,0).
	Anim::XForm r1 = result.GetTransform(1);
	CheckQuatApprox(r1.m_rotation, Quat::AngleAxis(Skore::QuarterPI, Vec3{0, 0, 1}));
	CheckVec3Approx(r1.m_translation, Vec3{1, 0, 0});
}

TEST_CASE("Animation::BoneMaskOps")
{
	Skeleton       sceneSkel;
	Anim::Skeleton skel = MakeTestSkeleton(sceneSkel);

	// ScaleWeights: all 1.0 scaled by 0.5 -> 0.5.
	Anim::BoneMask m(&skel, 1.0f);
	m.ScaleWeights(0.5f);
	CHECK(Math::Abs(m.GetWeight(0) - 0.5f) < 0.001f);

	// BlendTo: 0.0 -> 1.0 at 0.5 -> 0.5.
	Anim::BoneMask src(&skel, 0.0f);
	Anim::BoneMask tgt(&skel, 1.0f);
	src.BlendTo(tgt, 0.5f);
	CHECK(Math::Abs(src.GetWeight(0) - 0.5f) < 0.001f);

	// operator*= (combine): 0.5 * 0.5 -> 0.25.
	Anim::BoneMask p(&skel, 0.5f);
	Anim::BoneMask q2(&skel, 0.5f);
	p *= q2;
	CHECK(Math::Abs(p.GetWeight(0) - 0.25f) < 0.001f);
}

TEST_CASE("Animation::RootMotionDelta")
{
	// Translation trajectory: (0,0,0) -> (1,0,0) -> (2,0,0) at 1 fps (duration 2s).
	Array<Vec3> positions;
	positions.EmplaceBack(Vec3{0, 0, 0});
	positions.EmplaceBack(Vec3{1, 0, 0});
	positions.EmplaceBack(Vec3{2, 0, 0});
	Array<Quat> identityRots;
	identityRots.EmplaceBack(Quat(0, 0, 0, 1));
	identityRots.EmplaceBack(Quat(0, 0, 0, 1));
	identityRots.EmplaceBack(Quat(0, 0, 0, 1));

	Anim::AnimationClip* clip = Anim::AnimationClip::CreateRootMotionTestClip(positions, identityRots, 1.0f);

	// Forward delta 0 -> 1s = (1,0,0).
	CheckVec3Approx(clip->GetRootMotionDelta(0.0f, 1.0f).m_translation, Vec3{1, 0, 0});

	// Loop-wrap 1.5s -> 0.5s composes (1.5->end) + (start->0.5) = (1,0,0). (D13-sensitive path.)
	CheckVec3Approx(clip->GetRootMotionDelta(1.5f, 0.5f).m_translation, Vec3{1, 0, 0});

	delete clip;

	// Rotation trajectory: identity -> 45deg -> 90deg about Y. Delta 0 -> 1s = 45 deg about Y.
	Array<Vec3> noMove;
	noMove.EmplaceBack(Vec3{0, 0, 0});
	noMove.EmplaceBack(Vec3{0, 0, 0});
	noMove.EmplaceBack(Vec3{0, 0, 0});
	Array<Quat> rots;
	rots.EmplaceBack(Quat(0, 0, 0, 1));
	rots.EmplaceBack(Quat::AngleAxis(Skore::QuarterPI, Vec3{0, 1, 0}));
	rots.EmplaceBack(Quat::AngleAxis(Skore::HalfPI, Vec3{0, 1, 0}));

	Anim::AnimationClip* clipRot = Anim::AnimationClip::CreateRootMotionTestClip(noMove, rots, 1.0f);
	CheckQuatApprox(clipRot->GetRootMotionDelta(0.0f, 1.0f).m_rotation, Quat::AngleAxis(Skore::QuarterPI, Vec3{0, 1, 0}));
	delete clipRot;
}

TEST_CASE("Animation::Blend1DNode")
{
	Skeleton       sceneSkel;
	Anim::Skeleton skel = MakeTestSkeleton(sceneSkel);

	// Two constant single-bone clips: clip0 puts bone1 at (0,0,0), clip1 at (2,0,0).
	Array<Vec3> pos0;
	pos0.EmplaceBack(Vec3{0, 0, 0});
	pos0.EmplaceBack(Vec3{0, 0, 0});
	Array<Vec3> pos1;
	pos1.EmplaceBack(Vec3{2, 0, 0});
	pos1.EmplaceBack(Vec3{2, 0, 0});
	Array<Quat> rotI;
	rotI.EmplaceBack(Quat(0, 0, 0, 1));
	rotI.EmplaceBack(Quat(0, 0, 0, 1));

	Anim::AnimationClip* clip0 = Anim::AnimationClip::CreateSingleBoneTestClip(1, pos0, rotI, 1.0f);
	Anim::AnimationClip* clip1 = Anim::AnimationClip::CreateSingleBoneTestClip(1, pos1, rotI, 1.0f);

	{
		// Hand-build: [clip0, clip1] -> Blend1D driven by a float control parameter.
		Anim::GraphDefinition def;
		def.SetSkeleton(&skel);

		auto* clip0Def = def.CreateNodeDefinition<Anim::AnimationClipNode>();
		clip0Def->m_dataSlotIdx = def.AddClipResource(clip0);
		auto* clip1Def = def.CreateNodeDefinition<Anim::AnimationClipNode>();
		clip1Def->m_dataSlotIdx = def.AddClipResource(clip1);
		auto* paramDef = def.CreateNodeDefinition<Anim::ControlParameterFloatNode>();
		auto* blendDef = def.CreateNodeDefinition<Anim::Blend1DNode>();

		blendDef->m_sourceNodeIndices.EmplaceBack(clip0Def->m_nodeIdx);
		blendDef->m_sourceNodeIndices.EmplaceBack(clip1Def->m_nodeIdx);
		blendDef->m_inputParameterValueNodeIdx = paramDef->m_nodeIdx;

		Array<f32> sourceValues; // source0 sits at param 0.0, source1 at param 1.0
		sourceValues.EmplaceBack(0.0f);
		sourceValues.EmplaceBack(1.0f);
		blendDef->m_parameterization = Anim::ParameterizedBlendNode::Parameterization::CreateParameterization(sourceValues);

		def.SetRootNodeIndex(blendDef->m_nodeIdx);
		def.AddPersistentNode(paramDef->m_nodeIdx); // control params initialize at construction

		Anim::GraphInstance instance(&def, 0);

		auto sampleBone1AtParam = [&](f32 paramValue) -> Vec3
		{
			instance.SetControlParameterFloat(paramDef->m_nodeIdx, paramValue);
			instance.EvaluateGraph(0.016f, Anim::XForm::Identity());
			instance.ExecutePrePhysicsPoseTasks(Anim::XForm::Identity());
			instance.ExecutePostPhysicsPoseTasks();
			return instance.GetPrimaryPose()->GetTransform(1).m_translation;
		};

		CheckVec3Approx(sampleBone1AtParam(0.0f), Vec3{0, 0, 0}); // fully clip0
		CheckVec3Approx(sampleBone1AtParam(1.0f), Vec3{2, 0, 0}); // fully clip1
		CheckVec3Approx(sampleBone1AtParam(0.5f), Vec3{1, 0, 0}); // halfway blend
	}

	delete clip0;
	delete clip1;
}
