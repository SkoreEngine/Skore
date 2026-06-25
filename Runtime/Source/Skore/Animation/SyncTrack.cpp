#include "SyncTrack.hpp"

namespace Skore::Anim
{
	namespace
	{
		f32 Lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

		i32 Gcd(i32 a, i32 b)
		{
			while (b != 0) { i32 t = b; b = a % b; a = t; }
			return a;
		}

		i32 LowestCommonMultiple(i32 a, i32 b)
		{
			i32 g = Gcd(a, b);
			return g != 0 ? (a / g) * b : 0;
		}
	}

	SyncTrack::SyncTrack()
	{
		m_syncEvents.EmplaceBack(Event()); // single default event spanning the whole clip
	}

	SyncTrack::SyncTrack(const Array<EventMarker>& markers, i32 startEventOffset)
	{
		if (markers.Empty())
		{
			m_syncEvents.EmplaceBack(Event());
			return;
		}

		m_syncEvents.Reserve(markers.Size());
		for (const EventMarker& marker : markers)
		{
			// Markers are expected pre-sorted by start time.
			m_syncEvents.EmplaceBack(Event(marker.m_id, marker.m_startTime));
		}

		i32 numSyncEvents = GetNumEvents();

		// Durations are gaps-to-next.
		for (i32 i = 0; i < numSyncEvents - 1; ++i)
		{
			m_syncEvents[i].m_duration = m_syncEvents[i + 1].m_startTime - m_syncEvents[i].m_startTime;
		}

		// The last event covers the remaining duration, wrapping around the loop point.
		m_syncEvents[numSyncEvents - 1].m_duration =
			Percentage(1.0f) - (m_syncEvents[numSyncEvents - 1].m_startTime - m_syncEvents[0].m_startTime);

		m_startEventOffset = ClampIndexToTrack(startEventOffset);
	}

	SyncTrack::SyncTrack(const SyncTrack& track0, const SyncTrack& track1, f32 blendWeight)
	{
		i32 numEvents0 = track0.GetNumEvents();
		i32 numEvents1 = track1.GetNumEvents();

		// The blended track has LCM(n0,n1) events; scale each track's durations to it.
		i32 LCM = LowestCommonMultiple(numEvents0, numEvents1);
		f32 durationScale0 = f32(numEvents0) / LCM;
		f32 durationScale1 = f32(numEvents1) / LCM;

		Percentage blendedStartPercent = 0.0f;

		m_syncEvents.Reserve(LCM);
		for (i32 i = 0; i < LCM; ++i)
		{
			// GetEvent applies each track's start-event offset.
			const Event& event0 = track0.GetEvent(i);
			const Event& event1 = track1.GetEvent(i);
			f32 event0Duration = event0.m_duration.ToFloat() * durationScale0;
			f32 event1Duration = event1.m_duration.ToFloat() * durationScale1;

			Percentage blendedDuration = Lerp(event0Duration, event1Duration, blendWeight);
			m_syncEvents.EmplaceBack(Event(blendWeight > 0.5f ? event1.m_id : event0.m_id, blendedStartPercent, blendedDuration));
			blendedStartPercent += blendedDuration;
		}

		// Normalize so the track spans [0,1] (blendedStartPercent == total blended duration).
		f32 normalizedScale = 1.0f / blendedStartPercent.ToFloat();
		for (i32 i = 0; i < LCM; ++i)
		{
			m_syncEvents[i].m_startTime = m_syncEvents[i].m_startTime.ToFloat() * normalizedScale;
			m_syncEvents[i].m_duration = m_syncEvents[i].m_duration.ToFloat() * normalizedScale;
		}

		// Ensure the last event reaches the end of the track.
		m_syncEvents[LCM - 1].m_duration = Percentage(1.0f) - m_syncEvents[LCM - 1].m_startTime;
	}

	SyncTrackTime SyncTrack::GetTime(Percentage percentage, bool withOffset) const
	{
		i32 numSyncEvents = GetNumEvents();
		SyncTrackTime time;

		// Special-case the very end: return the last event at 100% rather than the first at 0%.
		if (percentage == 1.0f)
		{
			time.m_eventIdx = numSyncEvents - 1;
			time.m_percentageThrough = 1.0f;
			return time;
		}

		// Normalized percentage + how many loops it represents.
		i32        loopCount = 0;
		Percentage percentageThrough = 0.0f;
		percentage.GetLoopCountAndNormalizedTime(loopCount, percentageThrough);

		if (percentageThrough < m_syncEvents[0].m_startTime)
		{
			// Before the first event's start -> we wrapped, so we're in the last event.
			time.m_eventIdx = numSyncEvents - 1;
			f32 eventDelta = m_syncEvents[0].m_startTime.ToFloat() - percentageThrough.ToFloat();
			time.m_percentageThrough =
				(m_syncEvents[numSyncEvents - 1].m_duration.ToFloat() - eventDelta) / m_syncEvents[numSyncEvents - 1].m_duration.ToFloat();
		}
		else
		{
			// Find the first event whose end (start + duration) is past the playback percent.
			for (i32 syncEventIdx = 0; syncEventIdx < numSyncEvents; ++syncEventIdx)
			{
				if ((m_syncEvents[syncEventIdx].m_startTime + m_syncEvents[syncEventIdx].m_duration) >= percentageThrough)
				{
					time.m_eventIdx = syncEventIdx;
					time.m_percentageThrough =
						(percentageThrough - m_syncEvents[syncEventIdx].m_startTime) / m_syncEvents[syncEventIdx].m_duration;
					break;
				}
			}
		}

		// Apply loop count, then fold the offset back in.
		time.m_eventIdx += loopCount * numSyncEvents;
		i32 offset = withOffset ? m_startEventOffset : 0;
		time.m_eventIdx = ClampIndexToTrack(time.m_eventIdx - offset);

		return time;
	}

	Percentage SyncTrack::GetPercentageThrough(const SyncTrackTime& time, bool withOffset) const
	{
		i32 offset = withOffset ? m_startEventOffset : 0;
		i32 eventIdx = ClampIndexToTrack(time.m_eventIdx + offset);
		Percentage percentageThroughSyncTrack =
			m_syncEvents[eventIdx].m_startTime + (m_syncEvents[eventIdx].m_duration * time.m_percentageThrough);

		// Handle looping sequences (start + duration*frac can spill past 1.0).
		while (percentageThroughSyncTrack > 1.0f)
		{
			percentageThroughSyncTrack -= Percentage(1.0f);
		}

		return percentageThroughSyncTrack;
	}
}
