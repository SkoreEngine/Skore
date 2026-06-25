#pragma once

#include "Pose.hpp"
#include "SyncTrack.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore::Anim
{
	// Engine-side runtime form of an AnimationClipResource: per-bone-INDEX channels
	// (the resource is keyed by bone NAME) plus a baked SyncTrack, built once against an
	// Anim::Skeleton. The resource keyframes are already uncompressed, so building is a
	// straight reinterpret + reindex. See Docs/AnimationSystemPort.md §3, Migration §3.
	//
	// S0/S1: the clip is component-owned and built once (CreateFromResource). The shared
	// RID-keyed clip cache (decision D5) is deferred — the cache key must incorporate the
	// skeleton identity, which is now available (Anim::Skeleton) but not yet wired up.
	class SK_API AnimationClip
	{
	public:
		// Build a runtime clip from the resource, resolving channel bone names against
		// `skeleton`. Returns nullptr if the resource is unreadable. Caller owns the
		// returned clip and must `delete` it.
		static AnimationClip* CreateFromResource(RID clipRID, const Skeleton* skeleton);

		// Sample the clip at `time` (seconds, auto-wrapped to the clip length) into
		// `pose`. Bones with no channel keep whatever the pose already holds (the bind
		// pose, if the caller reset it first).
		void Sample(Pose& pose, f32 time) const;

		// Sample at a percentage-through the clip [0,1] (the graph's native time unit). Used
		// by the task system's SampleTask.
		void Sample(Pose& pose, Percentage percentage) const;

		f32              GetDuration() const { return m_duration; }
		// The clip's sampling time span (seconds) = the range root-motion deltas are keyed on.
		f32              GetSamplingDuration() const { return m_timeEnd - m_timeBegin; }
		const SyncTrack& GetSyncTrack() const { return m_syncTrack; }

		// Root motion
		//-------------------------------------------------------------------------
		bool HasRootMotion() const { return !m_rootPositions.Empty(); }
		i32  GetRootBoneIndex() const { return m_rootBoneIndex; }

		// Loop-aware root-motion delta between two clip times (seconds). Translation is in
		// root-local (animation) space — apply it via the character's world rotation. S1:
		// forward playback only (reverse playback inverts the query range, added later).
		// Mirrors the proven AnimationPlayer::ExtractRootMotionDelta. Spec §3.5, §4.9.
		XForm GetRootMotionDelta(f32 prevTime, f32 curTime) const;

		// Test-only factory: a clip carrying just a root-bone trajectory (frames 0..N-1 at the
		// given positions/rotations), no skinning channels. For unit-testing root motion.
		static AnimationClip* CreateRootMotionTestClip(const Array<Vec3>& rootPositions, const Array<Quat>& rootRotations, f32 frameRate);

		// Test-only factory: a clip with a single animated bone channel (boneIndex), frames
		// 0..N-1; scales default to 1. For unit-testing clip sampling + blends.
		static AnimationClip* CreateSingleBoneTestClip(i32 boneIndex, const Array<Vec3>& positions, const Array<Quat>& rotations, f32 frameRate);

	private:
		// Shared interpolation: write each channel's [frame0,frame1] lerp/slerp into the pose.
		void SampleChannels(Pose& pose, u32 frame0, u32 frame1, f32 t) const;

		struct Channel
		{
			u32         boneIndex = U32_MAX;
			Array<Vec3> positions;
			Array<Quat> rotations;
			Array<Vec3> scales;
		};

		Array<Channel> m_channels;
		SyncTrack      m_syncTrack;
		u32            m_numFrames = 0;
		f32            m_duration  = 0.0f;
		f32            m_frameRate = 0.0f;
		f32            m_timeBegin = 0.0f;
		f32            m_timeEnd   = 0.0f;

		// Root bone trajectory (cached at build for loop-aware delta extraction).
		i32         m_rootBoneIndex = InvalidIndex;
		Array<Vec3> m_rootPositions;
		Array<Quat> m_rootRotations;
	};
}
