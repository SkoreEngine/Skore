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

#include "SettingsWindow.hpp"

#include "imgui.h"
#include "Skore/Editor.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/ImGui/ImGui.hpp"

namespace Skore
{
	void SettingsWindow::Init(u32 id, VoidPtr userData)
	{
		settingsType = PtrToInt(userData);

		if (ReflectType* typeHandler = Reflection::FindTypeById(settingsType))
		{
			title = FormatName(typeHandler->GetSimpleName());
		}

		HashMap<String, std::shared_ptr<SettingsItem>> itemsByPath;

		for (TypeID typeId : Resources::FindTypesByAttribute<EditableSettings>())
		{
			ResourceType* resourceType = Resources::FindTypeByID(typeId);
			if (const EditableSettings* editableSettings = resourceType->GetAttribute<EditableSettings>())
			{
				if (editableSettings->type == settingsType)
				{
					Array<String> items = {};
					Split(StringView{editableSettings->path}, StringView{"/"}, [&](const StringView& item)
					{
						items.EmplaceBack(item);
					});

					if (items.Empty())
					{
						items.EmplaceBack(editableSettings->path);
					}

					String path = "";
					std::shared_ptr<SettingsItem> lastItem = nullptr;

					for (int i = 0; i < items.Size(); ++i)
					{
						const String& itemLabel = items[i];
						path += "/" + itemLabel;

						auto itByPath = itemsByPath.Find(path);
						if (itByPath == itemsByPath.end())
						{
							std::shared_ptr<SettingsItem> newItem = std::make_shared<SettingsItem>();
							newItem->label = itemLabel;
							itByPath = itemsByPath.Insert(path, newItem).first;

							if (lastItem != nullptr)
							{
								lastItem->children.EmplaceBack(newItem);
							}
							else
							{
								rootItems.EmplaceBack(newItem);
							}
						}
						lastItem = itByPath->second;
					}

					if (lastItem != nullptr)
					{
						lastItem->type = resourceType;
						lastItem->rid = Settings::Get(settingsType, typeId);
					}
				}
			}
		}
	}

	void SettingsWindow::Draw(u32 id, bool& open)
	{
		auto&  style = ImGui::GetStyle();
		ImVec2 padding = style.WindowPadding;

		ScopedStyleVar   windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ScopedStyleColor tableBorderStyleColor(ImGuiCol_TableBorderLight, IM_COL32(0, 0, 0, 0));

		ImGuiCenterWindow(ImGuiCond_Appearing);

		ImGuiBegin(id, title.CStr(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking);

		if (ImGui::BeginTable("settings-windows-table", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, 300 * style.ScaleFactor);
			ImGui::TableNextColumn();

			ScopedStyleVar childPadding(ImGuiStyleVar_WindowPadding, padding);
			DrawTree();
			ImGui::TableNextColumn();
			DrawSelected();

			ImGui::EndTable();
		}

		ImGui::End();
	}

	void SettingsWindow::Open(TypeID group)
	{
		Editor::OpenWindow<SettingsWindow>(IntToPtr(group));
	}

	void SettingsWindow::DrawTree()
	{
		//left panel
		ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
		ImGui::BeginChild(4000, ImVec2(0, 0), 0, ImGuiWindowFlags_AlwaysUseWindowPadding);

		ImGui::SetNextItemWidth(-1);
		ImGuiSearchInputText(4001, searchText);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * ImGui::GetStyle().ScaleFactor);

		ImGuiBeginTreeNodeStyle();

		for (auto& item : rootItems)
		{
			DrawItem(*item, 0);
		}

		ImGuiEndTreeNodeStyle();

		ImGui::EndChild();
	}

	void SettingsWindow::DrawItem(const SettingsItem& settingsItem, u32 level)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;

		if (selectedItem && selectedItem == settingsItem.rid)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		bool open = false;

		if (!settingsItem.children.Empty())
		{
			ImGui::SetNextItemOpen(level == 0, ImGuiCond_Once);
			open = ImGuiTreeNode(IntToPtr(settingsItem.rid.id), settingsItem.label.CStr(), flags);
		}
		else
		{
			ImGuiTreeLeaf(IntToPtr(settingsItem.rid.id), settingsItem.label.CStr(), flags);
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			selectedItem = settingsItem.rid;
		}

		if (open)
		{
			for (auto& item : settingsItem.children)
			{
				DrawItem(*item, level + 1);
			}

			ImGui::TreePop();
		}
	}

	void SettingsWindow::DrawSelected() const
	{
		ImGui::BeginChild(5000, ImVec2(0, 0), 0, ImGuiWindowFlags_AlwaysUseWindowPadding);

		if (selectedItem)
		{
			ImGuiDrawResource(ImGuiDrawResourceInfo{
				.rid = selectedItem,
				.scopeName = "Settings Edit",
			});
		}
		ImGui::EndChild();
	}


	void SettingsWindow::OpenAction(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<SettingsWindow>(IntToPtr(eventData.userData));
	}

	void SettingsWindow::RegisterType(NativeReflectType<SettingsWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Edit/Project Settings", .priority = 1010, .action = OpenAction, .userData = TypeInfo<ProjectSettings>::ID()});
	}
}
