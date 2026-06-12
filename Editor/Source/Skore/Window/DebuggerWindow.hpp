#pragma once
#include "imgui.h"
#include "Skore/EditorCommon.hpp"
#include "Skore/Profiler.hpp"

namespace Skore
{
	class DebuggerWindow : public EditorWindow
	{
	public:
		SK_CLASS(DebuggerWindow, EditorWindow);

		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;

		static void RegisterType(NativeReflectType<DebuggerWindow>& type);

	private:
		static constexpr u32 MaxHistoryFrames = 300;
		static constexpr u32 MaxSegments = 32;
		static constexpr u32 MaxTasks = 256;

		struct FrameSegment
		{
			f32 duration = 0.0f;
			u32 color = 0;
		};

		struct FrameRecord
		{
			FrameSegment segments[MaxSegments]{};
			u32          segmentCount = 0;
			f32          total = 0.0f;
		};

		// Per-profiler (CPU or GPU) UI state: cached tasks, frame history and
		// toolbar selections are tracked independently for each tab.
		struct ProfilerView
		{
			Profiler::TaskEntry tasks[MaxTasks]{};
			u32                 taskCount = 0;

			FrameRecord         history[MaxHistoryFrames]{};
			u32                 historyWritePos = 0;
			u32                 historyCount = 0;

			bool                paused = false;
			float               maxFrameTime = 1.0f / 60.0f;
			int                 scaleIndex = 3;
		};

		void DrawStats();
		void DrawProfiler(ProfilerView& view, bool gpu);
		void DrawProfilerTree(const Profiler::TaskEntry* entries, u32 count, bool gpu);
		void DrawProfilerChart(ProfilerView& view, const char* label, ImVec2 size);
		void UpdateHistory(ProfilerView& view, bool gpu, const Profiler::TaskEntry* entries, u32 count);

		ProfilerView m_cpuView;
		ProfilerView m_gpuView;
	};
}
