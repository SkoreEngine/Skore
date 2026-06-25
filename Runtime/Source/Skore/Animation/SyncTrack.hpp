#pragma once

#include "Percentage.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore::Anim
{
	// The phase-locking timeline baked onto each clip. Synchronized blending advances
	// clips in *event space* so clips of different real durations stay foot-locked. A full
	// port of Esoterica's SyncTrack (AnimationSyncTrack.{h,cpp}). See Docs/AnimationSystemPort.md §4.
	//
	// Event IDs are u32 (0 == invalid) rather than an interned StringID for now (risk R5);
	// they only matter for sync-event matching in transitions (S5), so the ID-search helpers
	// are deferred until then. The default/authored tracks don't need them.

	//-------------------------------------------------------------------------
	// A position on a sync track: which event, and how far through that event.
	struct SyncTrackTime
	{
		SyncTrackTime() = default;
		SyncTrackTime(i32 eventIdx, Percentage percentageThrough)
			: m_eventIdx(eventIdx), m_percentageThrough(percentageThrough) {}

		// event index + fraction as a single float (e.g. event 2, 50% -> 2.5)
		f32 ToFloat() const { return m_percentageThrough.ToFloat() + m_eventIdx; }

		i32        m_eventIdx = 0;
		Percentage m_percentageThrough = 0.0f;
	};

	// A range in sync-track event space — used to drive a synchronized update.
	struct SyncTrackTimeRange
	{
		SyncTrackTimeRange() = default;
		explicit SyncTrackTimeRange(const SyncTrackTime& time) : m_startTime(time), m_endTime(time) {}
		SyncTrackTimeRange(const SyncTrackTime& start, const SyncTrackTime& end) : m_startTime(start), m_endTime(end) {}

		SyncTrackTime m_startTime;
		SyncTrackTime m_endTime;
	};

	//-------------------------------------------------------------------------
	class SK_API SyncTrack
	{
	public:
		static constexpr u32 InvalidEventId = 0;

		// Calculates the duration resulting from a synchronized blend of two sync tracks.
		static f32 CalculateDurationSynchronized(f32 duration0, f32 duration1, i32 numEvents0, i32 numEvents1, i32 eventsLCM, f32 blendWeight)
		{
			f32 scaledDuration0 = duration0 * (f32(eventsLCM) / numEvents0);
			f32 scaledDuration1 = duration1 * (f32(eventsLCM) / numEvents1);
			return scaledDuration0 + (scaledDuration1 - scaledDuration0) * blendWeight; // lerp
		}

		// A simple marker used to instantiate a sync track from authored sync points.
		struct EventMarker
		{
			EventMarker() = default;
			EventMarker(Percentage startTime, u32 id) : m_startTime(startTime), m_id(id) {}
			bool operator<(const EventMarker& rhs) const { return m_startTime < rhs.m_startTime; }

			Percentage m_startTime = 0.0f;
			u32        m_id = InvalidEventId;
		};

		// A baked sync event: id + normalized start + duration (gap-to-next, cyclic).
		struct Event
		{
			Event() = default;
			Event(u32 id, Percentage startTime, Percentage duration = 0.0f)
				: m_id(id), m_startTime(startTime), m_duration(duration) {}

			u32        m_id = InvalidEventId;
			Percentage m_startTime = 0.0f;
			Percentage m_duration = 1.0f;
		};

	public:
		// Default track: a single event spanning the whole clip (unsynced clips still flow
		// through the synchronized machinery).
		SyncTrack();

		// Build from sorted authored markers (durations become gaps-to-next, last wraps).
		explicit SyncTrack(const Array<EventMarker>& markers, i32 startEventOffset = 0);

		// Build by blending two tracks via LCM of their event counts (durations only).
		SyncTrack(const SyncTrack& track0, const SyncTrack& track1, f32 blendWeight);

		i32                 GetNumEvents() const { return static_cast<i32>(m_syncEvents.Size()); }
		const Array<Event>& GetEvents() const { return m_syncEvents; }

		bool HasStartOffset() const { return m_startEventOffset != 0; }
		i32  GetStartEventOffset() const { return m_startEventOffset; }

		// Get the event at index i, applying the start-event offset.
		const Event& GetEvent(i32 i) const { return m_syncEvents[ClampIndexToTrack(i + m_startEventOffset)]; }
		// Get the event at index i, ignoring the start-event offset.
		const Event& GetEventWithoutOffset(i32 i) const { return m_syncEvents[ClampIndexToTrack(i)]; }

		// Time conversions (the spine — see §4.3)
		//-------------------------------------------------------------------------

		// percentage-through-clip -> sync-track time (from the offset's "first" event)
		SyncTrackTime GetTime(Percentage percentage) const { return GetTime(percentage, true); }
		SyncTrackTime GetTimeWithoutOffset(Percentage percentage) const { return GetTime(percentage, false); }

		// sync-track time -> percentage-through-clip (inverse of GetTime)
		Percentage GetPercentageThrough(const SyncTrackTime& time) const { return GetPercentageThrough(time, true); }
		Percentage GetPercentageThroughWithoutOffset(const SyncTrackTime& time) const { return GetPercentageThrough(time, false); }

		// Advance a sync time by a percentage delta (used by unsynchronized root advance).
		SyncTrackTime UpdateEventTime(const SyncTrackTime& startTime, Percentage deltaTime) const
		{
			Percentage startPct = GetPercentageThrough(startTime, true);
			return GetTime(startPct + deltaTime, false);
		}

		SyncTrackTime GetStartTime() const { return SyncTrackTime(0, 0.0f); }
		SyncTrackTime GetEndTime() const { return SyncTrackTime(GetNumEvents() - 1, 1.0f); }

	private:
		i32 ClampIndexToTrack(i32 eventIndex) const
		{
			i32 numEvents = GetNumEvents();
			i32 clamped = eventIndex % numEvents;
			if (clamped < 0) clamped += numEvents;
			return clamped;
		}

		SyncTrackTime GetTime(Percentage percentage, bool withOffset) const;
		Percentage    GetPercentageThrough(const SyncTrackTime& time, bool withOffset) const;

	private:
		Array<Event> m_syncEvents;        // number + position of the sync periods
		i32          m_startEventOffset = 0; // which event is logically "first"
	};
}
