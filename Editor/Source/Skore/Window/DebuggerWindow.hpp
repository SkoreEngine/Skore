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
		void DrawStats();
		void DrawProfiler();
		void DrawProfilerTree(const Profiler::TaskEntry* entries, u32 count);
		void DrawProfilerChart(const char* label, bool isCpu, ImVec2 size);
		void UpdateHistory(const Profiler::TaskEntry* entries, u32 count);

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
			FrameSegment cpuSegments[MaxSegments]{};
			FrameSegment gpuSegments[MaxSegments]{};
			u32          cpuSegmentCount = 0;
			u32          gpuSegmentCount = 0;
			f32          totalCpu = 0.0f;
			f32          totalGpu = 0.0f;
		};

		// Cached current frame tasks
		Profiler::TaskEntry m_tasks[MaxTasks]{};
		u32                 m_taskCount = 0;

		// Frame history ring buffer
		FrameRecord m_history[MaxHistoryFrames]{};
		u32         m_historyWritePos = 0;
		u32         m_historyCount = 0;

		bool  m_paused = false;
		float m_maxFrameTime = 1.0f / 60.0f;
		int   m_scaleIndex = 3;
	};
}
