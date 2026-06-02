#include "Skore/Window/PackagesWindow.hpp"

#include "imgui.h"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/IO/Path.hpp"
#include "Skore/Platform/Platform.hpp"

namespace Skore
{
	const char* PackagesWindow::GetTitle() const
	{
		return ICON_FA_BOXES_STACKED " Packages";
	}

	void PackagesWindow::Draw(bool& open)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		ImGuiCenterWindow(ImGuiCond_Appearing);

		ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking);

		if (ImGui::Button(ICON_FA_FOLDER_OPEN " Add Package"))
		{
			Platform::PickFolder([](StringView path)
			{
				Editor::AddProjectPackage(path);
			}, Editor::GetProjectPath(), Graphics::GetWindow());
		}

		ImGui::SameLine();
		ImGui::TextDisabled("A package is a folder containing an \"Assets\" and/or \"Binaries\" folder.");

		ImGui::Spacing();

		constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

		if (ImGui::BeginTable("packages-table", 3, flags, ImVec2(0, ImGui::GetContentRegionAvail().y)))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.30f);
			ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.70f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36 * style.ScaleFactor);
			ImGui::TableHeadersRow();

			String packageToRemove;

			for (const String& package : Editor::GetProjectPackages())
			{
				ImGui::TableNextRow();
				ImGui::PushID(package.CStr());

				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%s", Path::Name(package).CStr());

				ImGui::TableNextColumn();
				ImGui::Text("%s", package.CStr());

				ImGui::TableNextColumn();
				if (ImGui::SmallButton(ICON_FA_TRASH))
				{
					packageToRemove = package;
				}

				ImGui::PopID();
			}

			ImGui::EndTable();

			//deferred removal so we don't mutate the list while iterating it
			if (!packageToRemove.Empty())
			{
				Editor::RemoveProjectPackage(packageToRemove);
			}
		}

		ImGui::End();
	}

	void PackagesWindow::Open(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<PackagesWindow>();
	}

	void PackagesWindow::RegisterType(NativeReflectType<PackagesWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Packages", .priority = 1015, .action = Open});
	}
}
