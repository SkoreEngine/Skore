#pragma once

#include "Skore/Animation/Graph/GraphNode.hpp"

namespace Skore::Anim
{
	// A pose node that outputs the skeleton's reference (bind) pose by registering a
	// ReferencePoseTask. Clip-free, so it's handy as a leaf and for testing the graph spine
	// (definition -> instance -> evaluate -> task -> pose) without a clip resource.
	class SK_API ReferencePoseNode final : public PoseNode
	{
	public:
		struct SK_API Definition final : public GraphNode::Definition
		{
			void InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const override;
		};

		const SyncTrack&    GetSyncTrack() const override { return m_syncTrack; }
		GraphPoseNodeResult Update(GraphContext& context, const SyncTrackTimeRange* pUpdateRange = nullptr) override;

	private:
		void InitializeInternal(GraphContext& context, const SyncTrackTime& initialTime) override;

		SyncTrack m_syncTrack; // default single-event track
	};
}
