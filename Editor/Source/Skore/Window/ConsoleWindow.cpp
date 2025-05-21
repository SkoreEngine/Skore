// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Skore/Window/ConsoleWindow.hpp"
#include "Skore/Core/Sinks.hpp"
#include "imgui.h"
#include "Skore/Editor.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"

namespace Skore
{
	ConsoleSink& GetConsoleSink();

	void ConsoleWindow::Draw(u32 id, bool& open)
	{
		ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
		if(!ImGuiBegin(id, ICON_FA_TERMINAL " Console", &open))
		{
			ImGui::End();
			return;
		}

		ConsoleSink& sink = GetConsoleSink();


		if (version != sink.Version())
		{
			sink.GetMessages(logs);
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
		ImGui::Checkbox("Auto-scroll", &shouldScrollToBottom);
		
		ImGui::Separator();
		
		// Filter
		filter.Draw("Filter", 180);
		
		ImGui::Separator();
		
		// Log window
		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
		
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
		
		for(const auto& log : logs)
		{
			// Skip logs based on filter level
			if((!showTrace && log.level == LogLevel::Trace) ||
			   (!showDebug && log.level == LogLevel::Debug) ||
			   (!showInfo && log.level == LogLevel::Info) ||
			   (!showWarn && log.level == LogLevel::Warn) ||
			   (!showError && log.level == LogLevel::Error) ||
			   (!showCritical && log.level == LogLevel::Critical))
			{
				continue;
			}
			
			//Skip logs based on text filter
			if(!filter.PassFilter(log.message.CStr()))
			{
				continue;
			}
			
			ImVec4 color;
			bool hasColor = false;
			
			switch(log.level)
			{
				case LogLevel::Trace:
					color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
					hasColor = true;
					break;
				case LogLevel::Debug:
					color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
					hasColor = true;
					break;
				case LogLevel::Info:
					color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
					hasColor = true;
					break;
				case LogLevel::Warn:
					color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f);
					hasColor = true;
					break;
				case LogLevel::Error:
					color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
					hasColor = true;
					break;
				case LogLevel::Critical:
					color = ImVec4(0.9f, 0.1f, 0.1f, 1.0f);
					hasColor = true;
					break;
				default:
					break;
			}
			
			if(hasColor)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, color);
			}
			
			ImGui::TextUnformatted(log.message.begin(), log.message.end());
			
			if(hasColor)
			{
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
			.createOnInit = true,
			.order = 10
		});
	}

	void ConsoleWindow::OpenHistoryWindow(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<ConsoleWindow>();
	}
}
