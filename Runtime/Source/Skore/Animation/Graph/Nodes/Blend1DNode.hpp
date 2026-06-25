#pragma once

#include "Skore/Animation/Graph/GraphNode.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore::Anim
{
	class FloatValueNode;

	// Minimal inclusive float range for blend-space parameterization (Esoterica's FloatRange).
	struct FloatRange
	{
		FloatRange() = default;
		explicit FloatRange(f32 v) : m_begin(v), m_end(v) {}
		FloatRange(f32 begin, f32 end) : m_begin(begin), m_end(end) {}

		f32  GetClampedValue(f32 v) const { return Math::Clamp(v, m_begin, m_end); }
		bool ContainsInclusive(f32 v) const { return v >= m_begin && v <= m_end; }
		f32  GetPercentageThrough(f32 v) const
		{
			f32 span = m_end - m_begin;
			return (span > 0.0f) ? ((v - m_begin) / span) : 0.0f;
		}

		f32 m_begin = 0.0f;
		f32 m_end = 0.0f;
	};

	// Base for parameterized blends (Blend1D etc.): N source pose nodes blended by a float
	// parameter, fully synchronized — one sync range drives all active sources so they stay
	// phase-locked. Port of Esoterica's ParameterizedBlendNode. Spec §4.5, §9 (Tier 3).
	class SK_API ParameterizedBlendNode : public PoseNode
	{
	public:
		struct BlendRange
		{
			i16        m_inputIdx0 = static_cast<i16>(InvalidIndex);
			i16        m_inputIdx1 = static_cast<i16>(InvalidIndex);
			FloatRange m_parameterValueRange = FloatRange(0.0f);
		};

		struct SK_API Parameterization
		{
			// Build blend ranges from each source's parameter value (sorted ascending).
			static Parameterization CreateParameterization(const Array<f32>& values);
			void Reset()
			{
				m_blendRanges.Clear();
				m_parameterRange = FloatRange(0.0f);
			}

			Array<BlendRange> m_blendRanges;
			FloatRange        m_parameterRange = FloatRange(0.0f);
		};

		struct SK_API Definition : public GraphNode::Definition
		{
			void InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const override;

			Array<i16> m_sourceNodeIndices;
			i16        m_inputParameterValueNodeIdx = static_cast<i16>(InvalidIndex);
			bool       m_allowLooping = true;
		};

	public:
		bool             IsValid() const override;
		const SyncTrack& GetSyncTrack() const override { return m_blendedSyncTrack; }

	protected:
		void                InitializeInternal(GraphContext& context, const SyncTrackTime& initialTime) override;
		void                ShutdownInternal(GraphContext& context) override;
		GraphPoseNodeResult Update(GraphContext& context, const SyncTrackTimeRange* pUpdateRange) override final;

		void EvaluateBlendSpace(GraphContext& context);

	private:
		struct BlendSpaceResult
		{
			void Reset() { *this = BlendSpaceResult(); }

			PoseNode* m_pSource0 = nullptr;
			PoseNode* m_pSource1 = nullptr;
			f32       m_blendWeight = 0.0f;
			u32       m_updateID = 0;
		};

	protected:
		Array<PoseNode*> m_sourceNodes;
		FloatValueNode*  m_pInputParameterValueNode = nullptr;
		BlendSpaceResult m_bsr;
		SyncTrack        m_blendedSyncTrack;
		Parameterization m_parameterization;
	};

	// 1D blend: sources placed along a parameter axis; the float input picks the bracketing
	// pair and the blend weight between them.
	class SK_API Blend1DNode final : public ParameterizedBlendNode
	{
	public:
		struct SK_API Definition final : public ParameterizedBlendNode::Definition
		{
			void InstantiateNode(const InstantiationContext& context, InstantiationOptions options) const override;

			Parameterization m_parameterization;
		};
	};
}
