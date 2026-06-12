#include "Skore/Window/DebuggerWindow.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneEditor.hpp"

namespace Skore
{
	namespace
	{
		ImU32 ProfilerToImColor(u32 c)
		{
			return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF);
		}

		ImU32 ProfilerToImColorAlpha(u32 c, u8 alpha)
		{
			return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, alpha);
		}
	}

	const char* DebuggerWindow::GetTitle() const
	{
		return ICON_FA_BUG " Debugger";
	}

	void DebuggerWindow::Init(VoidPtr userData)
	{
	}

	void DebuggerWindow::Draw(bool& open)
	{
		ImGuiStyle&    style = ImGui::GetStyle();
		ImVec2         pad = style.WindowPadding;

		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ScopedStyleVar cellPadding(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		ScopedStyleColor windowBg(ImGuiCol_WindowBg, IM_COL32(2, 3, 5, 255));
		ScopedStyleVar itemSpace(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

		if(!ImGuiBegin(this, &open))
		{
			ImGui::End();
			return;
		}

		if (ImGui::BeginTabBar("##debugger_tabs"))
		{
			if (ImGui::BeginTabItem("Statistics"))
			{
				ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
				ScopedStyleVar   childPadding(ImGuiStyleVar_WindowPadding, pad);
				if (ImGui::BeginChild("statistics_child", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_AlwaysUseWindowPadding))
				{
					DrawStats();
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("CPU Profiler"))
			{
				ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
				ScopedStyleVar   childPadding(ImGuiStyleVar_WindowPadding, pad);
				if (ImGui::BeginChild("cpu_profiler_child", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_AlwaysUseWindowPadding))
				{
					DrawProfiler(m_cpuView, false);
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("GPU Profiler"))
			{
				ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
				ScopedStyleVar   childPadding(ImGuiStyleVar_WindowPadding, pad);
				if (ImGui::BeginChild("gpu_profiler_child", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_AlwaysUseWindowPadding))
				{
					DrawProfiler(m_gpuView, true);
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void DebuggerWindow::RegisterType(NativeReflectType<DebuggerWindow>& type)
	{
		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::BottomRight,
			.order = 20,
			.workspaceTypes = {WorkspaceTypes::All}
		});
	}

	namespace
	{
		void FormatBytes(char* out, usize outSize, u64 bytes)
		{
			constexpr f64 KB = 1024.0;
			constexpr f64 MB = KB * 1024.0;
			constexpr f64 GB = MB * 1024.0;
			f64 b = static_cast<f64>(bytes);
			if (b >= GB)      std::snprintf(out, outSize, "%.2f GB", b / GB);
			else if (b >= MB) std::snprintf(out, outSize, "%.2f MB", b / MB);
			else if (b >= KB) std::snprintf(out, outSize, "%.2f KB", b / KB);
			else              std::snprintf(out, outSize, "%llu B", static_cast<unsigned long long>(bytes));
		}

		u32 CountDrawcalls(const Array<DrawPipeline>& pipelines)
		{
			u32 total = 0;
			for (u32 i = 0; i < pipelines.Size(); ++i)
				total += static_cast<u32>(pipelines[i].drawcalls.Size());
			return total;
		}

		void StatsRow(const char* label, const char* value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value);
		}

		void StatsSectionHeader(const char* label)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 200, 220, 255));
			ImGui::TextUnformatted(label);
			ImGui::PopStyleColor();
			ImGui::TableSetColumnIndex(1);
		}
	}

	void DebuggerWindow::DrawStats()
	{
		ScopedStyleVar statsItemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
		ScopedStyleVar statsCellPadding(ImGuiStyleVar_CellPadding, ImVec2(6, 4));

		ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
		if (!ImGui::BeginTable("##stats_table", 2, tableFlags))
			return;

		ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_WidthFixed, 220.0f * (ImGui::GetFontSize() / 13.0f));
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		char buf[64];
		char buf2[64];

		// --- Frame timing ---
		StatsSectionHeader("Frame");
		std::snprintf(buf, sizeof(buf), "%.1f", App::GetFPS());
		StatsRow("FPS", buf);
		std::snprintf(buf, sizeof(buf), "%.2f ms", App::DeltaTime() * 1000.0);
		StatsRow("Frame time", buf);

		// --- CPU / Memory ---
		StatsSectionHeader("Process");

		f32 cpu = Platform::GetProcessCPUUsage();
		u32 cores = Platform::GetLogicalCoreCount();
		f32 cpuTotal = cores > 0 ? (cpu / static_cast<f32>(cores)) * 100.0f : 0.0f;
		std::snprintf(buf, sizeof(buf), "%.1f%%  (%.1f/core, %u cores)", cpuTotal, cpu * 100.0f, cores);
		StatsRow("CPU", buf);

		ProcessMemoryInfo procMem = Platform::GetProcessMemoryInfo();
		FormatBytes(buf, sizeof(buf), procMem.workingSetBytes);
		FormatBytes(buf2, sizeof(buf2), procMem.peakWorkingSetBytes);
		char memLine[160];
		std::snprintf(memLine, sizeof(memLine), "%s  (peak %s)", buf, buf2);
		StatsRow("Working set", memLine);

		SystemMemoryInfo sysMem = Platform::GetSystemMemoryInfo();
		u64 usedSys = (sysMem.totalBytes >= sysMem.availableBytes) ? (sysMem.totalBytes - sysMem.availableBytes) : 0;
		FormatBytes(buf, sizeof(buf), usedSys);
		FormatBytes(buf2, sizeof(buf2), sysMem.totalBytes);
		std::snprintf(memLine, sizeof(memLine), "%s / %s", buf, buf2);
		StatsRow("System memory", memLine);

		// --- GPU memory (VMA) ---
		StatsSectionHeader("GPU memory");

		Array<MemoryHeapBudget> budgets;
		Graphics::GetMemoryBudgets(budgets);

		u64 devUsage = 0, devBudget = 0;
		u64 hostUsage = 0, hostBudget = 0;
		for (u32 i = 0; i < budgets.Size(); ++i)
		{
			if (budgets[i].deviceLocal)
			{
				devUsage  += budgets[i].usage;
				devBudget += budgets[i].budget;
			}
			else
			{
				hostUsage  += budgets[i].usage;
				hostBudget += budgets[i].budget;
			}
		}

		FormatBytes(buf, sizeof(buf), devUsage);
		FormatBytes(buf2, sizeof(buf2), devBudget);
		std::snprintf(memLine, sizeof(memLine), "%s / %s", buf, buf2);
		StatsRow("Dedicated (VRAM)", memLine);

		FormatBytes(buf, sizeof(buf), hostUsage);
		FormatBytes(buf2, sizeof(buf2), hostBudget);
		std::snprintf(memLine, sizeof(memLine), "%s / %s", buf, buf2);
		StatsRow("Shared (host)", memLine);

		// --- Rendering ---
		StatsSectionHeader("Rendering");

		Scene* scene = nullptr;
		if (workspace)
		{
			if (SceneEditor* sceneEditor = workspace->GetSceneEditor())
			{
				scene = sceneEditor->GetCurrentScene();
			}
		}

		if (scene)
		{
			const RenderSceneObjects& objects = scene->renderObjects;

			u32 opaque      = CountDrawcalls(objects.opaquePipelines);
			u32 transparent = CountDrawcalls(objects.transparentPipelines);
			u32 shadow      = CountDrawcalls(objects.shadowPipelines);

			std::snprintf(buf, sizeof(buf), "%u", opaque);
			StatsRow("Opaque drawcalls", buf);

			std::snprintf(buf, sizeof(buf), "%u", transparent);
			StatsRow("Transparent drawcalls", buf);

			std::snprintf(buf, sizeof(buf), "%u (per cascade)", shadow);
			StatsRow("Shadow drawcalls", buf);

			std::snprintf(buf, sizeof(buf), "%u", opaque + transparent + shadow);
			StatsRow("Total drawcalls", buf);

			std::snprintf(buf, sizeof(buf), "%u", objects.instanceDataCount);
			StatsRow("Instances", buf);

			u32 pipelineCount = static_cast<u32>(
				objects.opaquePipelines.Size() +
				objects.transparentPipelines.Size() +
				objects.shadowPipelines.Size());
			std::snprintf(buf, sizeof(buf), "%u", pipelineCount);
			StatsRow("Render pipelines", buf);
		}
		else
		{
			StatsRow("Scene", "<none>");
		}

		ImGui::EndTable();
	}

	void DebuggerWindow::DrawProfiler(ProfilerView& view, bool gpu)
	{
		// Fetch profiler data
		u32 count = 0;
		const Profiler::TaskEntry* entries = gpu ? Profiler::GetGpuTasks(count) : Profiler::GetCpuTasks(count);

		if (!view.paused && count > 0)
		{
			view.taskCount = std::min(count, MaxTasks);
			std::memcpy(view.tasks, entries, view.taskCount * sizeof(Profiler::TaskEntry));
			UpdateHistory(view, gpu, view.tasks, view.taskCount);
		}

		// --- Toolbar ---
		{
			ScopedStyleVar toolbarSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
			ScopedStyleVar toolbarPadding(ImGuiStyleVar_FramePadding, ImVec2(6, 4));

			bool isActive = Profiler::IsActive();
			if (isActive)
			{
				if (ImGui::Button(ICON_FA_STOP " Stop"))
					Profiler::SetActive(false);
			}
			else
			{
				if (ImGui::Button(ICON_FA_CIRCLE " Record"))
					Profiler::SetActive(true);
			}

			ImGui::SameLine();
			if (ImGui::Button(view.paused ? (ICON_FA_PLAY " Resume") : (ICON_FA_PAUSE " Pause")))
				view.paused = !view.paused;

			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_TRASH " Clear"))
			{
				view.historyCount = 0;
				view.historyWritePos = 0;
				view.taskCount = 0;
				Profiler::ResetStats();
			}

			ImGui::SameLine();
			static const char* scaleLabels[] = {"2ms", "4ms", "8ms", "16ms (60fps)", "33ms (30fps)", "66ms (15fps)"};
			static const float scaleValues[] = {0.002f, 0.004f, 0.008f, 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 15.0f};
			ImGui::SetNextItemWidth(150.0f * (ImGui::GetFontSize() / 13.0f));
			if (ImGui::BeginCombo("##scale", scaleLabels[view.scaleIndex]))
			{
				for (int i = 0; i < IM_ARRAYSIZE(scaleLabels); i++)
				{
					bool selected = (view.scaleIndex == i);
					if (ImGui::Selectable(scaleLabels[i], selected))
					{
						view.scaleIndex = i;
						view.maxFrameTime = scaleValues[i];
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		{
			Profiler::FrameStats fs = gpu ? Profiler::GetGpuFrameStats() : Profiler::GetCpuFrameStats();
			ImGui::Text("Frame: %.2f ms   avg %.2f   min %.2f   max %.2f   (%u samples)",
				fs.current * 1000.0, fs.avg * 1000.0, fs.min * 1000.0, fs.max * 1000.0, fs.count);
		}

		// --- Layout: Left tree + Right chart ---
		ImVec2 avail = ImGui::GetContentRegionAvail();
		if (avail.x < 100 || avail.y < 50) return;

		float treeWidth = avail.x * 0.5f;
		float plotWidth = avail.x - treeWidth;

		// Left panel: hierarchy tree
		{
			ScopedStyleVar treeItemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
			ScopedStyleVar treeCellPadding(ImGuiStyleVar_CellPadding, ImVec2(4, 2));

			ImGui::BeginChild("##profiler_tree", ImVec2(treeWidth, avail.y), 0, ImGuiWindowFlags_AlwaysUseWindowPadding);
			DrawProfilerTree(view.tasks, view.taskCount, gpu);
			ImGui::EndChild();
		}

		ImGui::SameLine();

		// Right panel: chart
		{
			ImGui::BeginChild("##profiler_charts", ImVec2(plotWidth, avail.y));

			float labelHeight = ImGui::GetTextLineHeightWithSpacing();
			float chartHeight = avail.y - labelHeight;
			chartHeight = std::max(chartHeight, 40.0f);

			ImGui::TextUnformatted(gpu ? "GPU" : "CPU");
			DrawProfilerChart(view, "##profiler_chart", ImVec2(plotWidth, chartHeight));

			ImGui::EndChild();
		}
	}

	void DebuggerWindow::DrawProfilerTree(const Profiler::TaskEntry* entries, u32 count, bool gpu)
	{
		if (count == 0)
		{
			ImGui::TextDisabled("No profiler data. Press Record to start.");
			return;
		}

		ImGuiTableFlags tableFlags =
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_Resizable |
			ImGuiTableFlags_ScrollY |
			ImGuiTableFlags_ScrollX |
			ImGuiTableFlags_BordersInnerV;

		// The GPU profiler also reports the CPU cost of recording each zone, so it
		// shows both CPU and GPU columns; the CPU profiler shows CPU only.
		int columns = gpu ? 9 : 5;
		if (!ImGui::BeginTable("##profiler_table", columns, tableFlags))
			return;

		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthFixed, 200.0f);
		ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 52.0f);
		ImGui::TableSetupColumn("avg##cpu", ImGuiTableColumnFlags_WidthFixed, 52.0f);
		ImGui::TableSetupColumn("min##cpu", ImGuiTableColumnFlags_WidthFixed, 52.0f);
		ImGui::TableSetupColumn("max##cpu", ImGuiTableColumnFlags_WidthFixed, 52.0f);
		if (gpu)
		{
			ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, 52.0f);
			ImGui::TableSetupColumn("avg##gpu", ImGuiTableColumnFlags_WidthFixed, 52.0f);
			ImGui::TableSetupColumn("min##gpu", ImGuiTableColumnFlags_WidthFixed, 52.0f);
			ImGui::TableSetupColumn("max##gpu", ImGuiTableColumnFlags_WidthFixed, 52.0f);
		}
		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableHeadersRow();

		auto statCell = [](int col, bool show, f64 ms)
		{
			ImGui::TableSetColumnIndex(col);
			if (show)
				ImGui::Text("%.2f", ms);
			else
				ImGui::TextDisabled("-");
		};

		i32 currentDepth = 0;

		for (u32 i = 0; i < count; i++)
		{
			const auto& entry = entries[i];

			// Pop tree nodes for depth decrease
			while (currentDepth > entry.depth)
			{
				ImGui::TreePop();
				currentDepth--;
			}

			bool hasChildren = (i + 1 < count && entries[i + 1].depth > entry.depth);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen;
			if (!hasChildren)
				nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

			// Draw colored indicator before name
			ImVec2 cursorPos = ImGui::GetCursorScreenPos();
			ImU32 entryColor = ProfilerToImColor(entry.color);

			bool open = ImGui::TreeNodeEx((void*)(uintptr_t)i, nodeFlags, "%s", entry.name);

			// Draw small colored diamond on the left of the text
			{
				ImDrawList* dl = ImGui::GetWindowDrawList();
				ImVec2 itemMin = ImGui::GetItemRectMin();
				float textHeight = ImGui::GetTextLineHeight();
				float diamondSize = textHeight * 0.25f;
				float cx = itemMin.x - diamondSize - 2.0f;
				float cy = itemMin.y + textHeight * 0.5f;
				dl->AddQuadFilled(
					ImVec2(cx, cy - diamondSize),
					ImVec2(cx + diamondSize, cy),
					ImVec2(cx, cy + diamondSize),
					ImVec2(cx - diamondSize, cy),
					entryColor
				);
			}

			statCell(1, entry.present, entry.cpuTime * 1000.0);
			statCell(2, entry.cpuCount > 0, entry.cpuAvg * 1000.0);
			statCell(3, entry.cpuCount > 0, entry.cpuMin * 1000.0);
			statCell(4, entry.cpuCount > 0, entry.cpuMax * 1000.0);

			if (gpu)
			{
				statCell(5, entry.present && entry.hasGPU, entry.gpuTime * 1000.0);
				statCell(6, entry.gpuCount > 0, entry.gpuAvg * 1000.0);
				statCell(7, entry.gpuCount > 0, entry.gpuMin * 1000.0);
				statCell(8, entry.gpuCount > 0, entry.gpuMax * 1000.0);
			}

			if (hasChildren)
			{
				if (open)
				{
					currentDepth = entry.depth + 1;
				}
				else
				{
					// Skip children of closed node
					while (i + 1 < count && entries[i + 1].depth > entry.depth)
						i++;
				}
			}
		}

		// Pop remaining open tree nodes
		while (currentDepth > 0)
		{
			ImGui::TreePop();
			currentDepth--;
		}

		ImGui::EndTable();
	}

	void DebuggerWindow::DrawProfilerChart(ProfilerView& view, const char* label, ImVec2 size)
	{
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImVec2 endPos = ImVec2(pos.x + size.x, pos.y + size.y);

		// Background
		dl->AddRectFilled(pos, endPos, IM_COL32(15, 15, 20, 255));

		// Reference lines
		auto drawRefLine = [&](float timeMs, const char* text, ImU32 color)
		{
			float normalized = (timeMs * 0.001f) / view.maxFrameTime;
			if (normalized <= 0.0f || normalized >= 1.0f) return;
			float y = pos.y + size.y * (1.0f - normalized);
			dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), color);
			dl->AddText(ImVec2(pos.x + 4, y - ImGui::GetTextLineHeight()), color, text);
		};

		drawRefLine(16.67f, "16.7ms (60fps)", IM_COL32(80, 120, 80, 180));
		drawRefLine(33.33f, "33.3ms (30fps)", IM_COL32(150, 120, 50, 180));

		// Draw frame history bars
		if (view.historyCount > 0)
		{
			float barWidth = size.x / (float)MaxHistoryFrames;
			float barGap = std::max(barWidth * 0.15f, 0.5f);
			float effectiveBarWidth = std::max(barWidth - barGap, 1.0f);

			for (u32 f = 0; f < view.historyCount; f++)
			{
				u32 idx = (view.historyWritePos + MaxHistoryFrames - view.historyCount + f) % MaxHistoryFrames;
				const auto& record = view.history[idx];

				float x = pos.x + f * barWidth;
				float yBottom = pos.y + size.y;

				if (record.segmentCount > 0)
				{
					float y = yBottom;
					for (u32 s = 0; s < record.segmentCount; s++)
					{
						float segHeight = (record.segments[s].duration / view.maxFrameTime) * size.y;
						segHeight = std::min(segHeight, y - pos.y);
						if (segHeight < 0.5f) continue;

						dl->AddRectFilled(
							ImVec2(x, y - segHeight),
							ImVec2(x + effectiveBarWidth, y),
							ProfilerToImColorAlpha(record.segments[s].color, 200)
						);
						y -= segHeight;
					}
				}
				else
				{
					float barHeight = (record.total / view.maxFrameTime) * size.y;
					barHeight = std::min(barHeight, size.y);
					if (barHeight >= 0.5f)
					{
						dl->AddRectFilled(
							ImVec2(x, yBottom - barHeight),
							ImVec2(x + effectiveBarWidth, yBottom),
							IM_COL32(80, 180, 80, 200)
						);
					}
				}
			}

			// Tooltip on hover
			if (ImGui::IsMouseHoveringRect(pos, endPos))
			{
				float mx = ImGui::GetMousePos().x - pos.x;
				int frameIdx = (int)(mx / barWidth);
				if (frameIdx >= 0 && frameIdx < (int)view.historyCount)
				{
					u32 idx = (view.historyWritePos + MaxHistoryFrames - view.historyCount + frameIdx) % MaxHistoryFrames;
					const auto& record = view.history[idx];

					ImGui::BeginTooltip();
					ImGui::Text("Frame Time: %.3f ms", record.total * 1000.0f);

					for (u32 s = 0; s < record.segmentCount; s++)
					{
						ImVec4 col = ImGui::ColorConvertU32ToFloat4(ProfilerToImColor(record.segments[s].color));
						ImGui::TextColored(col, "  %.3f ms", record.segments[s].duration * 1000.0f);
					}
					ImGui::EndTooltip();

					// Draw hover highlight line
					float hoverX = pos.x + frameIdx * barWidth + effectiveBarWidth * 0.5f;
					dl->AddLine(
						ImVec2(hoverX, pos.y),
						ImVec2(hoverX, pos.y + size.y),
						IM_COL32(255, 255, 255, 100)
					);
				}
			}
		}

		// Border
		dl->AddRect(pos, endPos, IM_COL32(60, 60, 60, 255));

		ImGui::Dummy(size);
	}

	void DebuggerWindow::UpdateHistory(ProfilerView& view, bool gpu, const Profiler::TaskEntry* entries, u32 count)
	{
		auto& record = view.history[view.historyWritePos];
		record.segmentCount = 0;
		record.total = 0.0f;

		for (u32 i = 0; i < count; i++)
		{
			// Only depth-0 entries contribute to stacked bar segments
			if (entries[i].depth != 0 || !entries[i].present) continue;

			if (gpu && !entries[i].hasGPU) continue;

			f32 dur = (f32)(gpu ? entries[i].gpuTime : entries[i].cpuTime);
			record.total += dur;

			if (record.segmentCount < MaxSegments)
			{
				record.segments[record.segmentCount].duration = dur;
				record.segments[record.segmentCount].color = entries[i].color;
				record.segmentCount++;
			}
		}

		view.historyWritePos = (view.historyWritePos + 1) % MaxHistoryFrames;
		if (view.historyCount < MaxHistoryFrames) view.historyCount++;
	}
}
