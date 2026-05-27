#include "Skore/Window/ConsoleWindow.hpp"
#include "Skore/Core/Sinks.hpp"
#include "imgui.h"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"

namespace Skore
{
	ConsoleSink& GetConsoleSink();

	const char* ConsoleWindow::GetTitle() const
	{
		return ICON_FA_TERMINAL " Console";
	}

	void ConsoleWindow::Draw(bool& open)
	{
		ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
		if(!ImGuiBegin(this, &open))
		{
			ImGui::End();
			return;
		}

		ConsoleSink& sink = GetConsoleSink();


		if (version != sink.Version())
		{
			sink.GetMessages(logs);
			sink.GetCollapsedMessages(collapsedLogs);
			version = sink.Version();
		}

		// Top toolbar
		if(ImGui::Button("Clear"))
		{
			GetConsoleSink().ClearMessages();
		}
		
		ImGui::SameLine();
		ImGui::Separator();
		ImGui::SameLine();
		
		// Log level filters
		ImGui::BeginGroup();
		ImGui::Checkbox("Trace", &showTrace);
		ImGui::SameLine();
		ImGui::Checkbox("Debug", &showDebug);
		ImGui::SameLine();
		ImGui::Checkbox("Info", &showInfo);
		ImGui::SameLine();
		ImGui::Checkbox("Warn", &showWarn);
		ImGui::SameLine();
		ImGui::Checkbox("Error", &showError);
		ImGui::SameLine();
		ImGui::Checkbox("Critical", &showCritical);
		ImGui::EndGroup();

		ImGui::SameLine();
		ImGui::Separator();
		ImGui::SameLine();
		ImGui::Checkbox("Collapse", &collapse);
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &shouldScrollToBottom);
		
		ImGui::Separator();
		
		// Filter
		filter.Draw("Filter", 180);
		
		ImGui::Separator();
		
		// Log window
		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
		
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));

		auto getLogColor = [](LogLevel level) -> ImVec4
		{
			switch(level)
			{
				case LogLevel::Trace:    return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
				case LogLevel::Debug:    return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
				case LogLevel::Info:     return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
				case LogLevel::Warn:     return ImVec4(1.0f, 0.8f, 0.6f, 1.0f);
				case LogLevel::Error:    return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
				case LogLevel::Critical: return ImVec4(0.9f, 0.1f, 0.1f, 1.0f);
				default:                 return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
			}
		};

		auto shouldSkipLevel = [&](LogLevel level) -> bool
		{
			return (!showTrace && level == LogLevel::Trace) ||
			       (!showDebug && level == LogLevel::Debug) ||
			       (!showInfo && level == LogLevel::Info) ||
			       (!showWarn && level == LogLevel::Warn) ||
			       (!showError && level == LogLevel::Error) ||
			       (!showCritical && level == LogLevel::Critical);
		};

		if(collapse)
		{
			for(const auto& log : collapsedLogs)
			{
				if(shouldSkipLevel(log.level)) continue;
				if(!filter.PassFilter(log.message.CStr())) continue;

				ImGui::PushStyleColor(ImGuiCol_Text, getLogColor(log.level));
				ImGui::TextUnformatted(log.message.begin(), log.message.end());
				ImGui::PopStyleColor();
				if(log.count > 1)
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[%u]", log.count);
				}
			}
		}
		else
		{
			for(const auto& log : logs)
			{
				if(shouldSkipLevel(log.level)) continue;
				if(!filter.PassFilter(log.message.CStr())) continue;

				ImGui::PushStyleColor(ImGuiCol_Text, getLogColor(log.level));
				ImGui::TextUnformatted(log.message.begin(), log.message.end());
				ImGui::PopStyleColor();
			}
		}
		
		if(shouldScrollToBottom && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		{
			ImGui::SetScrollHereY(1.0f);
		}
		
		ImGui::PopStyleVar();
		ImGui::EndChild();
		ImGui::End();
	}

	void ConsoleWindow::RegisterType(NativeReflectType<ConsoleWindow>& type)
	{
		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::BottomRight,
			.order = 10,
			.workspaceTypes = {WorkspaceTypes::All}
		});
	}

	void ConsoleWindow::OpenHistoryWindow(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<ConsoleWindow>();
	}
}
