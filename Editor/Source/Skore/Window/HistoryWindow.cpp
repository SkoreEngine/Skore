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

#include "HistoryWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/Commands/UndoRedoSystem.hpp"

namespace Skore
{
	void HistoryWindow::Draw(u32 id, bool& open)
	{
		if (!ImGuiBegin(id, ICON_FA_CLOCK_ROTATE_LEFT " History", &open))
		{
			ImGui::End();
			return;
		}

		const Array<Ref<Transaction>>& undoStack = UndoRedoSystem::GetUndoStack();
		const Array<Ref<Transaction>>& redoStack = UndoRedoSystem::GetRedoStack();

		if (ImGui::BeginChild("HistoryList", ImGui::GetContentRegionAvail(), ImGuiChildFlags_Border))
		{
			const ImVec4 undoColor = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);   // Blue for undo stack
			const ImVec4 redoColor = ImVec4(0.8f, 0.3f, 0.8f, 1.0f);   // Purple for redo stack
			const ImVec4 activeColor = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Yellow for active command

			// If the history is empty, show a message
			if (undoStack.Empty() && redoStack.Empty())
			{
				ImGuiCentralizedText("No command history");
			}
			else
			{
				if (!undoStack.Empty())
				{
					ImGui::TextDisabled("Undo Stack:");

					for (i32 i = (i32)undoStack.Size() - 1; i >= 0; i--)
					{
						const Ref<Transaction>& transaction = undoStack[i];

						bool isActive = (i == undoStack.Size() - 1);

						// Choose color
						ImGui::PushStyleColor(ImGuiCol_Text, isActive ? activeColor : undoColor);
						ImGui::TextUnformatted(transaction->GetName().CStr());

						ImGui::PopStyleColor();
					}
				}
				else
				{
					ImGui::TextDisabled("Undo Stack: (Empty)");
				}

				ImGui::Separator();

				if (!redoStack.Empty())
				{
					ImGui::TextDisabled("Redo Stack:");

					for (i32 i = (i32)redoStack.Size() - 1; i >= 0; i--)
					{
						// Get the transaction
						const Ref<Transaction>& transaction = redoStack[i];

						// Highlight the current command
						bool isActive = (i == redoStack.Size() - 1);

						ImGui::PushStyleColor(ImGuiCol_Text, isActive ? activeColor : redoColor);
						ImGui::TextUnformatted(transaction->GetName().CStr());
						ImGui::PopStyleColor();
					}
				}
				else
				{
					ImGui::TextDisabled("Redo Stack: (Empty)");
				}
			}

			if (m_shouldAutoScroll)
			{
				ImGui::SetScrollHereY(1.0f);
				m_shouldAutoScroll = false;
			}

			ImGui::EndChild();
		}
		
		ImGui::End();
	}

	void HistoryWindow::OpenHistoryWindow(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<HistoryWindow>();
	}

	void HistoryWindow::RegisterType(NativeReflectType<HistoryWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/History", .action = OpenHistoryWindow});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::RightTop,
			.createOnInit = true,
			.order = 10
		});
	}
}