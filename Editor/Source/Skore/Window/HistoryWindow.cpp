#include "Skore/Window/HistoryWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/ImGui/Icons.h"

namespace Skore
{
	void ImGuiDrawUndoRedoActions();

	const char* HistoryWindow::GetTitle() const
	{
		return ICON_FA_CLOCK_ROTATE_LEFT " History";
	}

	void HistoryWindow::Draw(bool& open)
	{
		ImGuiBegin(this, &open);

		if (ImGui::BeginListBox("###", ImGui::GetWindowSize()))
		{
			ImGuiDrawUndoRedoActions();
			ImGui::EndListBox();
		}
		
		ImGui::End();
	}

	void HistoryWindow::OpenHistoryWindow(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<HistoryWindow>();
	}

	void HistoryWindow::RegisterType(NativeReflectType<HistoryWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/History", .action = OpenHistoryWindow});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::RightTop,
			.order = 10,
			.workspaceTypes = {WorkspaceTypes::Scene}
		});
	}
}