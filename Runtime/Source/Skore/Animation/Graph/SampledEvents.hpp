#pragma once

#include "Skore/Animation/AnimXForm.hpp" // InvalidIndex

namespace Skore::Anim
{
	// A [startIdx, endIdx) range into the sampled-events buffer. The end index is the start
	// of the next range / end of the buffer. Spec §5.1.
	struct SampledEventRange
	{
		SampledEventRange() = default;
		explicit SampledEventRange(i16 index) : m_startIdx(index), m_endIdx(index) {}
		SampledEventRange(i16 startIndex, i16 endIndex) : m_startIdx(startIndex), m_endIdx(endIndex) {}

		bool IsValid() const { return m_startIdx != static_cast<i16>(InvalidIndex) && m_endIdx >= m_startIdx; }
		i32  GetLength() const { return m_endIdx - m_startIdx; }
		void Reset() { m_startIdx = m_endIdx = static_cast<i16>(InvalidIndex); }

		i16 m_startIdx = static_cast<i16>(InvalidIndex);
		i16 m_endIdx = static_cast<i16>(InvalidIndex);
	};

	// Per-frame buffer of events sampled while evaluating the graph. S2 stub: no events are
	// sampled yet, so this only tracks the count (always 0) and hands out empty ranges. The
	// full SampledEvent system (animation + graph events, branch marking, range blending)
	// lands at M6/S5. Spec §5.1, §6.
	class SK_API SampledEventsBuffer
	{
	public:
		void Clear() { m_numSampledEvents = 0; }
		i16  GetNumSampledEvents() const { return m_numSampledEvents; }

		// An empty range anchored at the current end of the buffer.
		SampledEventRange GetEmptyRange() const { return SampledEventRange(m_numSampledEvents); }

		// S3 stub: event-range blending isn't implemented yet (events are M6); return empty.
		SampledEventRange BlendEventRanges(const SampledEventRange&, const SampledEventRange&, f32) const { return GetEmptyRange(); }

	private:
		i16 m_numSampledEvents = 0;
	};
}
