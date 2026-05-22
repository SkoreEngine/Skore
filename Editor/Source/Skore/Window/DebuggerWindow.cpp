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

	void DebuggerWindow::Init(u32 id, VoidPtr userData)
	{
	}

	void DebuggerWindow::Draw(u32 id, bool& open)
	{
		ImGuiStyle&    style = ImGui::GetStyle();
		ImVec2         pad = style.WindowPadding;

		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ScopedStyleVar cellPadding(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
		ScopedStyleColor windowBg(ImGuiCol_WindowBg, IM_COL32(2, 3, 5, 255));
		ScopedStyleVar itemSpace(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

		if(!ImGuiBegin(id, ICON_FA_BUG " Debugger", &open))
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

			if (ImGui::BeginTabItem("Profiler"))
			{
				ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
				ScopedStyleVar   childPadding(ImGuiStyleVar_WindowPadding, pad);
				if (ImGui::BeginChild("profiler_child", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_AlwaysUseWindowPadding))
				{
					DrawProfiler();
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
			constexpr double KB = 1024.0;
			constexpr double MB = KB * 1024.0;
			constexpr double GB = MB * 1024.0;
			double b = static_cast<double>(bytes);
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

	void DebuggerWindow::DrawProfiler()
	{
		// Fetch profiler data
		u32 count = 0;
		const Profiler::TaskEntry* entries = Profiler::GetTasks(count);

		if (!m_paused && count > 0)
		{
			m_taskCount = std::min(count, MaxTasks);
			std::memcpy(m_tasks, entries, m_taskCount * sizeof(Profiler::TaskEntry));
			UpdateHistory(m_tasks, m_taskCount);
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
			if (ImGui::Button(m_paused ? (ICON_FA_PLAY " Resume") : (ICON_FA_PAUSE " Pause")))
				m_paused = !m_paused;

			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_TRASH " Clear"))
			{
				m_historyCount = 0;
				m_historyWritePos = 0;
				m_taskCount = 0;
			}

			ImGui::SameLine();
			static const char* scaleLabels[] = {"2ms", "4ms", "8ms", "16ms (60fps)", "33ms (30fps)", "66ms (15fps)"};
			static const float scaleValues[] = {0.002f, 0.004f, 0.008f, 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 15.0f};
			ImGui::SetNextItemWidth(150.0f * (ImGui::GetFontSize() / 13.0f));
			if (ImGui::BeginCombo("##scale", scaleLabels[m_scaleIndex]))
			{
				for (int i = 0; i < IM_ARRAYSIZE(scaleLabels); i++)
				{
					bool selected = (m_scaleIndex == i);
					if (ImGui::Selectable(scaleLabels[i], selected))
					{
						m_scaleIndex = i;
						m_maxFrameTime = scaleValues[i];
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		// --- Layout: Left tree + Right charts ---
		ImVec2 avail = ImGui::GetContentRegionAvail();
		if (avail.x < 100 || avail.y < 50) return;

		float treeWidth = avail.x * 0.35f;
		float plotWidth = avail.x - treeWidth;

		// Left panel: hierarchy tree
		{
			ScopedStyleVar treeItemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
			ScopedStyleVar treeCellPadding(ImGuiStyleVar_CellPadding, ImVec2(4, 2));

			ImGui::BeginChild("##profiler_tree", ImVec2(treeWidth, avail.y), 0, ImGuiWindowFlags_AlwaysUseWindowPadding);
			DrawProfilerTree(m_tasks, m_taskCount);
			ImGui::EndChild();
		}

		ImGui::SameLine();

		// Right panel: charts
		{
			ImGui::BeginChild("##profiler_charts", ImVec2(plotWidth, avail.y));

			float spacing = ImGui::GetStyle().ItemSpacing.y;
			float labelHeight = ImGui::GetTextLineHeightWithSpacing();
			float chartHeight = (avail.y - labelHeight * 2 - spacing) * 0.5f;
			chartHeight = std::max(chartHeight, 40.0f);

			ImGui::Text("CPU");
			DrawProfilerChart("##cpu_chart", true, ImVec2(plotWidth, chartHeight));

			ImGui::Text("GPU");
			DrawProfilerChart("##gpu_chart", false, ImVec2(plotWidth, chartHeight));

			ImGui::EndChild();
		}
	}

	void DebuggerWindow::DrawProfilerTree(const Profiler::TaskEntry* entries, u32 count)
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
			ImGuiTableFlags_BordersInnerV;

		if (!ImGui::BeginTable("##profiler_table", 3, tableFlags))
			return;

		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();

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

			// CPU time column
			ImGui::TableSetColumnIndex(1);
			double cpuMs = (entry.cpuEndTime - entry.cpuStartTime) * 1000.0;
			ImGui::Text("%.2f ms", cpuMs);

			// GPU time column
			ImGui::TableSetColumnIndex(2);
			if (entry.hasGPU)
			{
				double gpuMs = (entry.gpuEndTime - entry.gpuStartTime) * 1000.0;
				ImGui::Text("%.2f ms", gpuMs);
			}
			else
			{
				ImGui::TextDisabled("-");
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

	void DebuggerWindow::DrawProfilerChart(const char* label, bool isCpu, ImVec2 size)
	{
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImVec2 endPos = ImVec2(pos.x + size.x, pos.y + size.y);

		// Background
		dl->AddRectFilled(pos, endPos, IM_COL32(15, 15, 20, 255));

		// Reference lines
		auto drawRefLine = [&](float timeMs, const char* text, ImU32 color)
		{
			float normalized = (timeMs * 0.001f) / m_maxFrameTime;
			if (normalized <= 0.0f || normalized >= 1.0f) return;
			float y = pos.y + size.y * (1.0f - normalized);
			dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), color);
			dl->AddText(ImVec2(pos.x + 4, y - ImGui::GetTextLineHeight()), color, text);
		};

		drawRefLine(16.67f, "16.7ms (60fps)", IM_COL32(80, 120, 80, 180));
		drawRefLine(33.33f, "33.3ms (30fps)", IM_COL32(150, 120, 50, 180));

		// Draw frame history bars
		if (m_historyCount > 0)
		{
			float barWidth = size.x / (float)MaxHistoryFrames;
			float barGap = std::max(barWidth * 0.15f, 0.5f);
			float effectiveBarWidth = std::max(barWidth - barGap, 1.0f);

			for (u32 f = 0; f < m_historyCount; f++)
			{
				u32 idx = (m_historyWritePos + MaxHistoryFrames - m_historyCount + f) % MaxHistoryFrames;
				const auto& record = m_history[idx];

				const auto* segments = isCpu ? record.cpuSegments : record.gpuSegments;
				u32 segCount = isCpu ? record.cpuSegmentCount : record.gpuSegmentCount;

				float x = pos.x + f * barWidth;
				float yBottom = pos.y + size.y;

				if (segCount > 0)
				{
					float y = yBottom;
					for (u32 s = 0; s < segCount; s++)
					{
						float segHeight = (segments[s].duration / m_maxFrameTime) * size.y;
						segHeight = std::min(segHeight, y - pos.y);
						if (segHeight < 0.5f) continue;

						dl->AddRectFilled(
							ImVec2(x, y - segHeight),
							ImVec2(x + effectiveBarWidth, y),
							ProfilerToImColorAlpha(segments[s].color, 200)
						);
						y -= segHeight;
					}
				}
				else
				{
					float totalTime = isCpu ? record.totalCpu : record.totalGpu;
					float barHeight = (totalTime / m_maxFrameTime) * size.y;
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
				if (frameIdx >= 0 && frameIdx < (int)m_historyCount)
				{
					u32 idx = (m_historyWritePos + MaxHistoryFrames - m_historyCount + frameIdx) % MaxHistoryFrames;
					const auto& record = m_history[idx];
					float totalTime = isCpu ? record.totalCpu : record.totalGpu;

					ImGui::BeginTooltip();
					ImGui::Text("Frame Time: %.3f ms", totalTime * 1000.0f);

					const auto* segments = isCpu ? record.cpuSegments : record.gpuSegments;
					u32 segCount = isCpu ? record.cpuSegmentCount : record.gpuSegmentCount;
					for (u32 s = 0; s < segCount; s++)
					{
						ImVec4 col = ImGui::ColorConvertU32ToFloat4(ProfilerToImColor(segments[s].color));
						ImGui::TextColored(col, "  %.3f ms", segments[s].duration * 1000.0f);
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

	void DebuggerWindow::UpdateHistory(const Profiler::TaskEntry* entries, u32 count)
	{
		auto& record = m_history[m_historyWritePos];
		record.cpuSegmentCount = 0;
		record.gpuSegmentCount = 0;
		record.totalCpu = 0.0f;
		record.totalGpu = 0.0f;

		for (u32 i = 0; i < count; i++)
		{
			// Only depth-0 entries contribute to stacked bar segments
			if (entries[i].depth == 0)
			{
				f32 cpuDur = (f32)(entries[i].cpuEndTime - entries[i].cpuStartTime);
				record.totalCpu += cpuDur;

				if (record.cpuSegmentCount < MaxSegments)
				{
					record.cpuSegments[record.cpuSegmentCount].duration = cpuDur;
					record.cpuSegments[record.cpuSegmentCount].color = entries[i].color;
					record.cpuSegmentCount++;
				}

				if (entries[i].hasGPU)
				{
					f32 gpuDur = (f32)(entries[i].gpuEndTime - entries[i].gpuStartTime);
					record.totalGpu += gpuDur;

					if (record.gpuSegmentCount < MaxSegments)
					{
						record.gpuSegments[record.gpuSegmentCount].duration = gpuDur;
						record.gpuSegments[record.gpuSegmentCount].color = entries[i].color;
						record.gpuSegmentCount++;
					}
				}
			}
		}

		m_historyWritePos = (m_historyWritePos + 1) % MaxHistoryFrames;
		if (m_historyCount < MaxHistoryFrames) m_historyCount++;
	}
}
