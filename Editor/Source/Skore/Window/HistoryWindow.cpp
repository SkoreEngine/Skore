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

namespace Skore
{
	void ImGuiDrawUndoRedoActions();

	void HistoryWindow::Draw(u32 id, bool& open)
	{
		ImGuiBegin(id, ICON_FA_CLOCK_ROTATE_LEFT " History", &open);

		if (ImGui::BeginListBox("###", ImGui::GetWindowSize()))
		{
			ImGuiDrawUndoRedoActions();
			ImGui::EndListBox();
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