#pragma once

#include "Skore/Animation/Graph/GraphNode.hpp"

namespace Skore::Anim
{
	class AnimationClip;

	// A pose node that samples a single clip: it advances its own time (synchronized when its
	// parent passes a sync range, otherwise by the frame delta), registers a SampleTask, and
	// outputs the clip's root-motion delta. Minimal S2 port — sync-event offsets, reverse
	// playback, and clip-event sampling are deferred to S3+. Spec §4.5.
	class SK_API AnimationClipNode final : public PoseNode
	{
	public:
		struct SK_API Definition final : public GraphNode::Definition
		{
			void InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const override;

			i16  m_dataSlotIdx = static_cast<i16>(InvalidIndex); // clip resource slot
			f32  m_speedMultiplier = 1.0f;
			bool m_sampleRootMotion = true;
			bool m_allowLooping = true;
		};

		const SyncTrack&    GetSyncTrack() const override { return m_syncTrack; }
		GraphPoseNodeResult Update(GraphContext& context, const SyncTrackTimeRange* pUpdateRange = nullptr) override;

	private:
		void InitializeInternal(GraphContext& context, const SyncTrackTime& initialTime) override;

		const AnimationClip* m_pClip = nullptr;
		SyncTrack            m_syncTrack; // copied from the clip on init (default if no clip)
	};
}
