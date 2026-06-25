#include "AnimationClip.hpp"

#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore::Anim
{
	AnimationClip* AnimationClip::CreateFromResource(RID clipRID, const Skeleton* skeleton)
	{
		if (!clipRID || skeleton == nullptr) return nullptr;

		ResourceObject animObj = Resources::Read(clipRID);
		if (!animObj) return nullptr;

		AnimationClip* clip = new AnimationClip();
		clip->m_numFrames = animObj.GetUInt(AnimationClipResource::NumFrames);
		clip->m_duration  = animObj.GetFloat(AnimationClipResource::Duration);
		clip->m_frameRate = animObj.GetFloat(AnimationClipResource::FrameRate);
		clip->m_timeBegin = animObj.GetFloat(AnimationClipResource::TimeBegin);
		clip->m_timeEnd   = animObj.GetFloat(AnimationClipResource::TimeEnd);
		clip->m_syncTrack = SyncTrack(); // default single-event track (M0); authored sync events arrive at S6/E4

		// Copy the packed keyframe buffer out once (mirror AnimationPlayer::LoadClipData).
		ResourceBuffer buffer = animObj.GetBuffer(AnimationClipResource::KeyFramesBuffer);
		ByteBuffer dataBuffer;
		dataBuffer.Resize(buffer.GetSize());
		buffer.CopyData(dataBuffer.begin(), dataBuffer.Size(), 0);

		// Bone name -> skeleton index. Channels are keyed by name; the pose is indexed,
		// so we resolve once here and store per-bone-index channels.
		HashMap<String, u32> boneNameToIndex;
		const Array<String>& boneIDs = skeleton->GetBoneIDs();
		for (u32 i = 0; i < boneIDs.Size(); ++i)
		{
			boneNameToIndex.Insert(boneIDs[i], i);
		}

		for (RID channelRID : animObj.GetSubObjectList(AnimationClipResource::Channels))
		{
			ResourceObject channelObj = Resources::Read(channelRID);
			if (!channelObj) continue;

			String boneName = channelObj.GetString(AnimationChannelResource::Name);

			auto it = boneNameToIndex.Find(boneName);
			if (!it) continue; // channel targets a bone absent from this skeleton -> skip

			Channel channel;
			channel.boneIndex = it->second;
			channel.positions.Resize(clip->m_numFrames);
			channel.rotations.Resize(clip->m_numFrames);
			channel.scales.Resize(clip->m_numFrames);

			u64 offset = channelObj.GetUInt(AnimationChannelResource::BufferOffset);
			for (u32 i = 0; i < clip->m_numFrames; ++i)
			{
				const AnimationKeyFrame* kf = reinterpret_cast<const AnimationKeyFrame*>(
					dataBuffer.begin() + offset + i * sizeof(AnimationKeyFrame));
				channel.positions[i] = kf->position;
				channel.rotations[i] = kf->rotation;
				channel.scales[i]    = kf->scale;
			}

			clip->m_channels.EmplaceBack(channel);
		}

		// Cache the root bone trajectory for root-motion delta extraction. The root bone is
		// the first bone with no parent (mirror AnimationPlayer::FindRootBoneIndex).
		const Array<i32>& parents = skeleton->GetParentBoneIndices();
		for (i32 i = 0; i < static_cast<i32>(parents.Size()); ++i)
		{
			if (parents[i] == InvalidIndex)
			{
				clip->m_rootBoneIndex = i;
				break;
			}
		}
		if (clip->m_rootBoneIndex != InvalidIndex)
		{
			for (const Channel& ch : clip->m_channels)
			{
				if (static_cast<i32>(ch.boneIndex) == clip->m_rootBoneIndex)
				{
					clip->m_rootPositions = ch.positions;
					clip->m_rootRotations = ch.rotations;
					break;
				}
			}
		}

		return clip;
	}

	XForm AnimationClip::GetRootMotionDelta(f32 prevTime, f32 curTime) const
	{
		XForm delta; // identity

		f32 animDuration = m_timeEnd - m_timeBegin;
		if (m_rootPositions.Empty() || animDuration <= 0.0f) return delta;

		auto sampleAt = [&](f32 t, Vec3& pos, Quat& rot)
		{
			while (t >= animDuration) t -= animDuration;
			while (t < 0.0f) t += animDuration;

			f32 frameTime  = t * m_frameRate;
			u32 frameIndex = static_cast<u32>(frameTime);
			u32 f0 = Math::Min(frameIndex, m_numFrames - 1);
			u32 f1 = Math::Min(frameIndex + 1, m_numFrames - 1);
			f32 frac = frameTime - static_cast<f32>(frameIndex);

			pos = Vec3::Mix(m_rootPositions[f0], m_rootPositions[f1], frac);
			rot = Quat::Slerp(m_rootRotations[f0], m_rootRotations[f1], frac);
		};

		auto wrapTime = [&](f32 t) -> f32
		{
			while (t >= animDuration) t -= animDuration;
			while (t < 0.0f) t += animDuration;
			return t;
		};

		f32  normPrev = wrapTime(prevTime);
		f32  normCurr = wrapTime(curTime);
		bool looped = (normCurr < normPrev); // forward playback (S1)

		if (!looped)
		{
			Vec3 p0, p1;
			Quat r0, r1;
			sampleAt(normPrev, p0, r0);
			sampleAt(normCurr, p1, r1);

			delta.m_translation = p1 - p0;
			delta.m_rotation    = QuatInverse(r0) * r1; // rotation from prev to curr
		}
		else
		{
			// segment A: prev -> end ; segment B: begin -> curr
			Vec3 a0, a1, b0, b1;
			Quat ra0, ra1, rb0, rb1;
			sampleAt(normPrev, a0, ra0);
			sampleAt(animDuration - 0.0001f, a1, ra1);
			sampleAt(0.0f, b0, rb0);
			sampleAt(normCurr, b1, rb1);

			Quat deltaRotA = QuatInverse(ra0) * ra1;
			Quat deltaRotB = QuatInverse(rb0) * rb1;

			delta.m_translation = (a1 - a0) + (b1 - b0);
			delta.m_rotation    = deltaRotA * deltaRotB; // Skore Hamilton == Esoterica post*pre reversed (D13)
		}

		delta.m_scale = 1.0f;
		return delta;
	}

	void AnimationClip::Sample(Pose& pose, f32 time) const
	{
		if (m_numFrames == 0) return;

		// Wrap time into [0, animDuration) — mirror AnimationPlayer::SampleClip.
		f32 animDuration = m_timeEnd - m_timeBegin;
		if (animDuration > 0.0f)
		{
			while (time >= animDuration) time -= animDuration;
			while (time < 0.0f) time += animDuration;
		}

		f32 frameTime  = time * m_frameRate;
		u32 frameIndex = static_cast<u32>(frameTime);
		u32 f0 = Math::Min(frameIndex, m_numFrames - 1);
		u32 f1 = Math::Min(frameIndex + 1, m_numFrames - 1);
		f32 t  = frameTime - static_cast<f32>(frameIndex);

		SampleChannels(pose, f0, f1, t);
	}

	void AnimationClip::Sample(Pose& pose, Percentage percentage) const
	{
		if (m_numFrames == 0) return;

		// Percentage-through-clip -> frame. GetNormalizedTime maps a loop boundary (1.0) to
		// the last frame rather than wrapping to 0.
		f32 p          = Math::Clamp(percentage.GetNormalizedTime().ToFloat(), 0.0f, 1.0f);
		f32 frameTime  = p * static_cast<f32>(m_numFrames - 1);
		u32 frameIndex = static_cast<u32>(frameTime);
		u32 f0 = Math::Min(frameIndex, m_numFrames - 1);
		u32 f1 = Math::Min(frameIndex + 1, m_numFrames - 1);
		f32 t  = frameTime - static_cast<f32>(frameIndex);

		SampleChannels(pose, f0, f1, t);
	}

	AnimationClip* AnimationClip::CreateRootMotionTestClip(const Array<Vec3>& rootPositions, const Array<Quat>& rootRotations, f32 frameRate)
	{
		AnimationClip* clip = new AnimationClip();
		clip->m_numFrames = static_cast<u32>(rootPositions.Size());
		clip->m_frameRate = frameRate;
		clip->m_timeBegin = 0.0f;
		clip->m_timeEnd   = (clip->m_numFrames > 1 && frameRate > 0.0f) ? (static_cast<f32>(clip->m_numFrames - 1) / frameRate) : 0.0f;
		clip->m_duration  = clip->m_timeEnd;
		clip->m_rootBoneIndex = 0;
		clip->m_rootPositions = rootPositions;
		clip->m_rootRotations = rootRotations;
		return clip;
	}

	AnimationClip* AnimationClip::CreateSingleBoneTestClip(i32 boneIndex, const Array<Vec3>& positions, const Array<Quat>& rotations, f32 frameRate)
	{
		AnimationClip* clip = new AnimationClip();
		clip->m_numFrames = static_cast<u32>(positions.Size());
		clip->m_frameRate = frameRate;
		clip->m_timeBegin = 0.0f;
		clip->m_timeEnd   = (clip->m_numFrames > 1 && frameRate > 0.0f) ? (static_cast<f32>(clip->m_numFrames - 1) / frameRate) : 0.0f;
		clip->m_duration  = (clip->m_timeEnd > 0.0f) ? clip->m_timeEnd : 1.0f; // non-zero so the clip node can advance time
		clip->m_syncTrack = SyncTrack();

		Channel channel;
		channel.boneIndex = static_cast<u32>(boneIndex);
		channel.positions = positions;
		channel.rotations = rotations;
		channel.scales.Resize(clip->m_numFrames);
		for (u32 i = 0; i < clip->m_numFrames; ++i)
		{
			channel.scales[i] = Vec3{1.0f, 1.0f, 1.0f};
		}
		clip->m_channels.EmplaceBack(channel);

		return clip;
	}

	void AnimationClip::SampleChannels(Pose& pose, u32 frame0, u32 frame1, f32 t) const
	{
		i32 numBones = pose.GetNumBones();
		for (const Channel& channel : m_channels)
		{
			if (channel.boneIndex >= static_cast<u32>(numBones)) continue;

			XForm x;
			x.m_translation = Vec3::Mix(channel.positions[frame0], channel.positions[frame1], t);
			x.m_rotation    = Quat::Slerp(channel.rotations[frame0], channel.rotations[frame1], t);
			x.m_scale       = Vec3::Mix(channel.scales[frame0], channel.scales[frame1], t).x;
			pose.SetTransform(static_cast<i32>(channel.boneIndex), x);
		}
	}
}
