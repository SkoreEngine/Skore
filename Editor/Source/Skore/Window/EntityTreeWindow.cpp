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

#include "EntityTreeWindow.hpp"

#include "imgui.h"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/World/Entity.hpp"
#include "Skore/World/WorldCommon.hpp"

namespace Skore
{
	MenuItemContext EntityTreeWindow::menuItemContext = {};

	namespace
	{
		static Logger logger = Logger::GetLogger("Skore::EntityTreeWindow");

		struct TableButtonStyle
		{
			ScopedStyleVar   padding{ImGuiStyleVar_FramePadding, ImVec2(0, 0)};
			ScopedStyleColor borderColor{ImGuiCol_Border, ImVec4(0, 0, 0, 0)};
			ScopedStyleColor buttonColor{ImGuiCol_Button, ImVec4(0, 0, 0, 0)};
			ScopedStyleColor buttonColorHovered{ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0)};
			ScopedStyleColor buttonColorActive{ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0)};
		};

		constexpr auto fillRowWithColour = [](const ImColor& colour)
		{
			for (int column = 0; column < ImGui::TableGetColumnCount(); column++)
			{
				ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, colour, column);
			}
		};
	}


	void EntityTreeWindow::Init(u32 id, VoidPtr userData)
	{
		iconSize = ImGui::CalcTextSize(ICON_FA_EYE).x;
	}

	void EntityTreeWindow::DrawRIDEntity(WorldEditor* worldEditor, RID entity, bool& entitySelected)
	{
		if (!entity) return;
		ResourceObject entityObject = Resources::Read(entity);
		if (!entityObject) return;

		bool       root = worldEditor->GetRootEntity() == entity;
		usize      childrenCount = entityObject.GetSubObjectSetCount(EntityResource::Children);
		StringView name = entityObject.GetString(EntityResource::Name);
		bool       active = !entityObject.GetBool(EntityResource::Deactivated);
		bool       locked = entityObject.GetBool(EntityResource::Locked);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (!root)
		{
			//DrawMovePayload(HashInt32(reinterpret_cast<usize>(entity)), entity->GetParent(), entity);
		}

		stringCache.Clear();
		stringCache += root ? ICON_FA_CUBES : ICON_FA_CUBE;
		stringCache += " ";
		stringCache += name;

		bool isSelected = worldEditor->IsSelected(entity);
		bool open = false;

		if (root || worldEditor->IsParentOfSelected(entity))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		}

		if (entityObject.GetPrototype())
		{
			//ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(143, 131, 34, 255));
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(138, 178, 242, 255));
		}

		ImVec2 cursorPos = ImGui::GetCursorPos();

		if (isSelected && renamingSelected)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetTreeNodeToLabelSpacing());

			if (!renamingFocus)
			{
				renamingStringCache = name;
				ImGui::SetKeyboardFocusHere();
			}

			ScopedStyleVar framePadding(ImGuiStyleVar_FramePadding, ImVec2{0, 0});

			ImGui::Text(ICON_FA_CUBE);
			ImGui::SameLine();

			auto size = ImGui::CalcTextSize(" ");
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + size.x);

			ImGuiInputText(66554433, renamingStringCache);

			if (!ImGui::IsItemActive() && renamingFocus)
			{
				renamingSelected = false;
				renamingFocus = false;
				worldEditor->Rename(entity, renamingStringCache);
			}

			if (!renamingFocus)
			{
				renamingFocus = true;
			}

			ImGui::SetCursorPos(cursorPos);
		}
		else if (childrenCount > 0)
		{
			open = ImGuiTreeNode(IntToPtr(entity.id), stringCache.CStr());
		}
		else
		{
			ImGuiTreeLeaf(IntToPtr(entity.id), stringCache.CStr());
		}

		if (entityObject.GetPrototype())
		{
			ImGui::PopStyleColor();
		}

		bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
		bool ctrlDown = ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_RightCtrl));

		if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && isHovered)
		{
			worldEditor->SelectEntity(entity, !ctrlDown);
		}

		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
		{
			ImGui::SetDragDropPayload(SK_ENTITY_PAYLOAD, nullptr, 0);
			ImGui::Text("%s", name.CStr());
			ImGui::EndDragDropSource();
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && isHovered)
		{
			entitySelected = true;
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ENTITY_PAYLOAD))
			{	// Array<Entity*> selectedCache;
				// for (const auto& selected : worldEditor->GetSelectedEntities())
				// {
				// 	if (Entity* entity = worldEditor->GetCurrentScene()->FindEntityByUUID(selected))
				// 	{
				// 		selectedCache.EmplaceBack(entity);
				// 	}
				// }
				// worldEditor->ChangeParent(entity, selectedCache);
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::TableNextColumn();
		{
			ImGui::BeginDisabled(worldEditor->IsReadOnly());

			char buffer[35]{};
			sprintf(buffer, "activated-button%llu", entity.id);

			TableButtonStyle buttonStyle;
			ScopedStyleColor textColor{ImGuiCol_Text, !active ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : ImGui::GetStyle().Colors[ImGuiCol_Text]};

			ImGui::PushID(buffer);

			if (ImGui::Button(active ? ICON_FA_EYE : ICON_FA_EYE_SLASH, ImVec2(ImGui::GetColumnWidth(), 0)))
			{
				worldEditor->SetActivated(entity, !active);
			}

			ImGui::PopID();

			ImGui::EndDisabled();
		}

		ImGui::TableNextColumn();
		{
			ImGui::BeginDisabled(worldEditor->IsReadOnly());

			char buffer[35]{};
			sprintf(buffer, "lock-button%llu", entity.id);

			TableButtonStyle buttonStyle;
			ScopedStyleColor textColor{ImGuiCol_Text, locked ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : ImGui::GetStyle().Colors[ImGuiCol_Text]};

			ImGui::PushID(buffer);

			if (ImGui::Button(locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN, ImVec2(ImGui::GetColumnWidth(), 0)))
			{
				worldEditor->SetLocked(entity, !locked);
			}

			ImGui::PopID();

			ImGui::EndDisabled();
		}

		if (isSelected)
		{
			fillRowWithColour(ImVec4(0.26f, 0.59f, 0.98f, 0.67f));
		}
		else if (isHovered)
		{
			fillRowWithColour(ImVec4(0.26f, 0.59f, 0.98f, 0.67f));
		}

		if (open)
		{
			entityObject.IterateSubObjectSet(EntityResource::Children, true, [&](RID child)
			{
				DrawRIDEntity(worldEditor, child, entitySelected);
				return true;
			});
			ImGui::TreePop();
		}
	}

	void EntityTreeWindow::DrawEntity(WorldEditor* worldEditor, Entity* entity, bool& entitySelected)
	{

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		bool root = entity->GetParent() == nullptr;
		bool isSelected = worldEditor->IsSelected(entity);

		stringCache.Clear();
		stringCache += root ? ICON_FA_CUBES : ICON_FA_CUBE;
		stringCache += " ";
		stringCache += entity->GetName();
		bool open = false;

		if (root)
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		}

		if (!entity->GetChildren().Empty())
		{
			open = ImGuiTreeNode(entity, stringCache.CStr());
		}
		else
		{
			ImGuiTreeLeaf(entity, stringCache.CStr());
		}

		bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
		bool ctrlDown = ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_RightCtrl));

		if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && isHovered)
		{
			worldEditor->SelectEntity(entity, !ctrlDown);
		}

		ImGui::TableNextColumn();

		ImGui::TableNextColumn();

		if (isSelected)
		{
			fillRowWithColour(ImVec4(0.26f, 0.59f, 0.98f, 0.67f));
		}
		else if (isHovered)
		{
			fillRowWithColour(ImVec4(0.26f, 0.59f, 0.98f, 0.67f));
		}


		if (open)
		{
			for (Entity* child : entity->GetChildren())
			{
				DrawEntity(worldEditor, child, entitySelected);
			}
			ImGui::TreePop();
		}

	}

	void EntityTreeWindow::Draw(u32 id, bool& open)
	{
		WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor();

		bool  entitySelected = false;
		auto& style = ImGui::GetStyle();
		auto  originalWindowPadding = style.WindowPadding;

		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (!ImGuiBegin(id, ICON_FA_LIST " Entity Tree", &open, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::End();
			return;
		}

		if (!worldEditor->GetRootEntity())
		{
			ImGuiCentralizedText("Open a scene in the Project Browser");
			ImGui::End();
			return;
		}

		bool openPopup = false;

		{
			ScopedStyleVar childWindowPadding(ImGuiStyleVar_WindowPadding, originalWindowPadding);

			ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar;
			ImGui::BeginChild("top-fields", ImVec2(0, (25 * style.ScaleFactor) + originalWindowPadding.y), false, flags);

			if (ImGui::Button(ICON_FA_PLUS))
			{
				openPopup = true;
			}

			ImGui::SameLine();

			ImGui::SetNextItemWidth(-1);
			ImGuiSearchInputText(id + 10, searchEntity);
			ImGui::EndChild();
		}

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + originalWindowPadding.y);

		{
			ScopedStyleVar   cellPadding(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
			ScopedStyleVar   frameRounding(ImGuiStyleVar_FrameRounding, 0);
			ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
			ScopedStyleColor borderColor(ImGuiCol_Border, IM_COL32(45, 46, 48, 255));

			if (ImGui::BeginChild("scene-tree-view-child", ImVec2(0, 0), false))
			{
				static ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody;

				if (ImGui::BeginTable("scene-tree-view-table", 3, tableFlags))
				{
					ImGui::TableSetupColumn("  Name", ImGuiTableColumnFlags_NoHide);
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, iconSize * 1.5f);
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, iconSize * 1.5f);
					ImGui::TableHeadersRow();

					ScopedStyleVar       padding(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					ScopedStyleVar       spacing(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 0.0f});
					ImGuiInvisibleHeader invisibleHeader;

					if (!showWorldEntity && !worldEditor->IsSimulationRunning())
					{
						DrawRIDEntity(worldEditor, worldEditor->GetRootEntity(), entitySelected);
						//DrawMovePayload(HashInt32(reinterpret_cast<usize>(sceneEditor->GetRoot())), sceneEditor->GetRoot(), nullptr);
					}
					else if (worldEditor->GetCurrentWorld() != nullptr)
					{
						DrawEntity(worldEditor, worldEditor->GetCurrentWorld()->GetRootEntity(), entitySelected);
					}

					ImGui::TableNextRow();
					ImGui::TableNextColumn();


					ImGui::EndTable();
				}
			}
			ImGui::EndChild();
		}

		bool closePopup = false;

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
		{
			if (menuItemContext.ExecuteHotKeys(this))
			{
				closePopup = true;
			}

			if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
			{
				if (!entitySelected)
				{
					worldEditor->ClearSelection();
					renamingSelected = false;
				}
				openPopup = true;
			}
		}

		if (openPopup)
		{
			ImGui::OpenPopup("scene-tree-popup");
		}

		bool popupRes = ImGuiBeginPopupMenu("scene-tree-popup");
		if (popupRes)
		{
			menuItemContext.Draw(this);
			if (closePopup)
			{
				ImGui::CloseCurrentPopup();
			}
		}
		ImGuiEndPopupMenu(popupRes);
		ImGui::End();
	}

	void EntityTreeWindow::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuItemContext.AddMenuItem(menuItem);
	}

	void EntityTreeWindow::AddSceneEntity(const MenuItemEventData& eventData)
	{
		if (WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor())
		{
			worldEditor->Create();
		}
	}
	void EntityTreeWindow::AddSceneEntityFromAsset(const MenuItemEventData& eventData)
	{
		//TODO
	}

	void EntityTreeWindow::AddComponent(const MenuItemEventData& eventData)
	{
		//TODO
	}

	void EntityTreeWindow::RenameSceneEntity(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);

		WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor();
		if (!worldEditor) return;

		if (!worldEditor->HasSelectedEntities()) return;

		window->renamingSelected = true;
		window->renamingFocus = false;
	}

	void EntityTreeWindow::DuplicateSceneEntity(const MenuItemEventData& eventData)
	{
		if (WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor())
		{
			worldEditor->DuplicateSelected();
		}
	}
	void EntityTreeWindow::DeleteSceneEntity(const MenuItemEventData& eventData)
	{
		if (WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor())
		{
			worldEditor->DestroySelected();
		}
	}

	bool EntityTreeWindow::CheckSelectedEntity(const MenuItemEventData& eventData)
	{
		WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor();
		if (!worldEditor) return false;

		return !worldEditor->IsReadOnly() && worldEditor->HasSelectedEntities();
	}

	bool EntityTreeWindow::CheckReadOnly(const MenuItemEventData& eventData)
	{
		WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor();
		if (!worldEditor) return false;
		return !worldEditor->IsReadOnly();
	}

	void EntityTreeWindow::ShowWorldEntity(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		window->showWorldEntity = !window->showWorldEntity;
	}

	bool EntityTreeWindow::IsShowWorldEntitySelected(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		return window->showWorldEntity;
	}

	void EntityTreeWindow::OpenEntityTree(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<EntityTreeWindow>();
	}

	void EntityTreeWindow::RegisterType(NativeReflectType<EntityTreeWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Entity Tree", .action = OpenEntityTree});

		AddMenuItem(MenuItemCreation{.itemName = "Create Entity", .priority = 0, .action = AddSceneEntity, .enable = CheckReadOnly});
		AddMenuItem(MenuItemCreation{.itemName = "Create Entity From Asset", .priority = 15, .action = AddSceneEntityFromAsset, .enable = CheckReadOnly,});
		AddMenuItem(MenuItemCreation{.itemName = "Add Component", .priority = 20, .action = AddComponent, .enable = CheckReadOnly,});
		AddMenuItem(MenuItemCreation{.itemName = "Rename", .priority = 200, .itemShortcut = {.presKey = Key::F2}, .action = RenameSceneEntity, .enable = CheckSelectedEntity});
		AddMenuItem(MenuItemCreation{.itemName = "Duplicate", .priority = 210, .itemShortcut = {.ctrl = true, .presKey = Key::D}, .action = DuplicateSceneEntity, .enable = CheckSelectedEntity});
		AddMenuItem(MenuItemCreation{.itemName = "Delete", .priority = 220, .itemShortcut = {.presKey = Key::Delete}, .action = DeleteSceneEntity, .enable = CheckSelectedEntity});
		AddMenuItem(MenuItemCreation{.itemName = "(Debug) Show World Entity", .priority = 1000, .action = ShowWorldEntity, .selected = IsShowWorldEntitySelected});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::RightTop,
			.createOnInit = true
		});
	}
}
