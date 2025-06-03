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


#include "SceneTreeWindow.hpp"

#include "imgui_internal.h"
#include "Skore/Editor.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Logger.hpp"


namespace Skore
{

	static Logger logger = Logger::GetLogger("Skore::SceneTreeWindow");

	struct TableButtonStyle
	{
		ScopedStyleVar   padding{ImGuiStyleVar_FramePadding, ImVec2(0, 0)};
		ScopedStyleColor borderColor{ImGuiCol_Border, ImVec4(0, 0, 0, 0)};
		ScopedStyleColor buttonColor{ImGuiCol_Button, ImVec4(0, 0, 0, 0)};
		ScopedStyleColor buttonColorHovered{ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0)};
		ScopedStyleColor buttonColorActive{ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0)};
	};


	MenuItemContext SceneTreeWindow::menuItemContext = {};

	constexpr auto fillRowWithColour = [](const ImColor& colour)
	{
		for (int column = 0; column < ImGui::TableGetColumnCount(); column++)
		{
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, colour, column);
		}
	};

	void SceneTreeWindow::Init(u32 id, VoidPtr userData)
	{
		iconSize = ImGui::CalcTextSize(ICON_FA_EYE).x;
	}

	void SceneTreeWindow::DrawEntity(SceneEditor* sceneEditor, Entity* entity, bool& entitySelected)
	{
		if (!entity) return;

		bool   root = sceneEditor->GetRoot() == entity;
		String name = entity->GetName();

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (!root)
		{
			DrawMovePayload(HashInt32(reinterpret_cast<usize>(entity)), entity->GetParent(), entity);
		}

		stringCache.Clear();
		stringCache += root ? ICON_FA_CUBES : ICON_FA_CUBE;
		stringCache += " ";
		stringCache += name;

		bool isSelected = sceneEditor->IsSelected(entity->GetUUID());
		bool open = false;

		if (root || sceneEditor->IsParentOfSelected(entity))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		}

		if (entity->GetPrefab())
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
				sceneEditor->Rename(entity, renamingStringCache);
			}

			if (!renamingFocus && renamingSelected)
			{
				renamingFocus = true;
			}

			ImGui::SetCursorPos(cursorPos);
		}
		else if (entity->HasChildren())
		{
			open = ImGuiTreeNode(entity, stringCache.CStr());
		}
		else
		{
			ImGuiTreeLeaf(entity, stringCache.CStr());
		}

		if (entity->GetPrefab())
		{
			ImGui::PopStyleColor();
		}

		bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
		bool ctrlDown = ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_RightCtrl));

		if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && isHovered)
		{
			sceneEditor->SelectEntity(entity->GetUUID(), !ctrlDown);
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
			{
				Array<Entity*> selectedCache;
				for (const auto& selected : sceneEditor->GetSelectedEntities())
				{
					if (Entity* entity = sceneEditor->GetCurrentScene()->FindEntityByUUID(selected))
					{
						selectedCache.EmplaceBack(entity);
					}
				}
				sceneEditor->ChangeParent(entity, selectedCache);
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::TableNextColumn();
		{
			ImGui::BeginDisabled(sceneEditor->IsReadOnly());

			bool activated = entity->IsActive();

			char buffer[35]{};
			sprintf(buffer, "activated-button%llu", reinterpret_cast<usize>(entity));

			TableButtonStyle buttonStyle;
			ScopedStyleColor textColor{ImGuiCol_Text, !activated ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : ImGui::GetStyle().Colors[ImGuiCol_Text]};

			ImGui::PushID(buffer);

			if (ImGui::Button(activated ? ICON_FA_EYE : ICON_FA_EYE_SLASH, ImVec2(ImGui::GetColumnWidth(), 0)))
			{
				sceneEditor->SetActive(entity, !activated);
			}

			ImGui::PopID();

			ImGui::EndDisabled();
		}

		ImGui::TableNextColumn();
		{
			ImGui::BeginDisabled(sceneEditor->IsReadOnly());
			bool locked = sceneEditor->IsLocked(entity);

			char buffer[35]{};
			sprintf(buffer, "lock-button%llu", reinterpret_cast<usize>(entity));

			TableButtonStyle buttonStyle;
			ScopedStyleColor textColor{ImGuiCol_Text, locked ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : ImGui::GetStyle().Colors[ImGuiCol_Text]};

			ImGui::PushID(buffer);

			if (ImGui::Button(locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN, ImVec2(ImGui::GetColumnWidth(), 0)))
			{
				sceneEditor->SetLocked(entity, !locked);
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
			for (Entity* child : entity->Children())
			{
				DrawEntity(sceneEditor, child, entitySelected);
			}
			ImGui::TreePop();
		}
	}

	void SceneTreeWindow::DrawMovePayload(u32 id, Entity* parent, Entity* moveTo) const
	{
		ImVec2 screenPos = ImVec2(ImGui::GetWindowPos().x, ImGui::GetCursorScreenPos().y);
		if (ImGui::BeginDragDropTargetCustom(ImRect(screenPos, screenPos + ImVec2(ImGui::GetContentRegionMax().x, std::ceil(1 * ImGui::GetStyle().ScaleFactor))), id))
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ENTITY_PAYLOAD))
			{
				SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();

				Array<Entity*> selectedEntities;
				for (const auto& selected : sceneEditor->GetSelectedEntities())
				{
					if (Entity* entity = sceneEditor->GetCurrentScene()->FindEntityByUUID(selected))
					{
						selectedEntities.EmplaceBack(entity);
					}
				}

				if (!selectedEntities.Empty())
				{
					//TODO - transactions

					// Ref<Transaction> transaction = UndoRedoSystem::BeginTransaction("Move Entity");
					//
					// for (Entity* entity : selectedEntities)
					// {
					// 	if (moveTo && moveTo->IsChildOf(entity))
					// 		continue;
					//
					// 	if (entity == moveTo)
					// 		continue;
					//
					// 	transaction->AddCommand(Alloc<MoveEntityCommand>(sceneEditor, entity, parent, moveTo));
					// }
					//
					// UndoRedoSystem::EndTransaction(transaction);
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	void SceneTreeWindow::Draw(u32 id, bool& open)
	{
		SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();

		bool  entitySelected = false;
		auto& style = ImGui::GetStyle();
		auto  originalWindowPadding = style.WindowPadding;

		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (!ImGuiBegin(id, ICON_FA_LIST " Scene Tree", &open, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::End();
			return;
		}

		if (sceneEditor->GetCurrentScene() == nullptr)
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

		if (sceneEditor->GetRoot() != nullptr)
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

					if (sceneEditor->IsLoaded())
					{
						ScopedStyleVar       padding(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
						ScopedStyleVar       spacing(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 0.0f});
						ImGuiInvisibleHeader invisibleHeader;

						DrawEntity(sceneEditor, sceneEditor->GetRoot(), entitySelected);

						ImGui::TableNextRow();
						ImGui::TableNextColumn();

						DrawMovePayload(HashInt32(reinterpret_cast<usize>(sceneEditor->GetRoot())), sceneEditor->GetRoot(), nullptr);
					}

					ImGui::EndTable();
				}
			}
			ImGui::EndChild();
		}

		//not used anymore
		// else
		// {
		// 	ScopedStyleVar childWindowPadding(ImGuiStyleVar_WindowPadding, originalWindowPadding);
		// 	if (ImGui::BeginChild("scene-tree-view-child", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding))
		// 	{
		// 		ImGui::Text("Create Root Entity:");
		//
		// 		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + originalWindowPadding.y * 2);
		//
		// 		ScopedStyleColor childBg(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
		//
		// 		if (ImGui::Button(ICON_FA_SQUARE " 2D Entity", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
		// 		{
		// 			logger.Error("2D entity type is not implemented yet");
		// 		}
		//
		// 		if (ImGui::Button(ICON_FA_CUBE " 3D Entity", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
		// 		{
		// 			Entity3D* entity = Alloc<Entity3D>();
		// 			sceneEditor->GetCurrentScene()->SetRootEntity(entity);
		// 			entity->SetUUID(UUID::RandomUUID());
		// 			entity->SetName("Entity 3D");
		// 			sceneEditor->MarkDirty();
		// 		}
		//
		// 		if (ImGui::Button(ICON_FA_VECTOR_SQUARE " User Interface Entity", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
		// 		{
		// 			logger.Error("User Interface entity type is not implemented yet");
		// 		}
		// 	}
		// 	ImGui::EndChild();
		// }

		bool closePopup = false;

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && sceneEditor->GetRoot() != nullptr)
		{
			if (menuItemContext.ExecuteHotKeys(this))
			{
				closePopup = true;
			}

			if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
			{
				if (!entitySelected && sceneEditor)
				{
					sceneEditor->ClearSelection();
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

	void SceneTreeWindow::OpenSceneTree(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<SceneTreeWindow>();
	}

	void SceneTreeWindow::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuItemContext.AddMenuItem(menuItem);
	}

	void SceneTreeWindow::AddSceneEntity(const MenuItemEventData& eventData)
	{
		if (SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor())
		{
			sceneEditor->Create();
		}
	}

	void SceneTreeWindow::AddSceneEntityFromAsset(const MenuItemEventData& eventData) {}

	void SceneTreeWindow::AddComponent(const MenuItemEventData& eventData)
	{
		// SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		// if (!sceneEditor) return;
		//
		// Entity* selected = sceneEditor->GetFirstSelected();
		// if (!selected) return;
	}

	void SceneTreeWindow::RenameSceneEntity(const MenuItemEventData& eventData)
	{
		SceneTreeWindow* window = static_cast<SceneTreeWindow*>(eventData.drawData);

		SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		if (!sceneEditor) return;

		if (!sceneEditor->HasSelectedEntities()) return;

		window->renamingSelected = true;
		window->renamingFocus = false;
	}

	void SceneTreeWindow::DuplicateSceneEntity(const MenuItemEventData& eventData)
	{
		if (SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor())
		{
			sceneEditor->DuplicateSelected();
		}
	}

	void SceneTreeWindow::DeleteSceneEntity(const MenuItemEventData& eventData)
	{
		if (SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor())
		{
			sceneEditor->DeleteSelected();
		}
	}

	bool SceneTreeWindow::CheckSelectedEntity(const MenuItemEventData& eventData)
	{
		SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		if (!sceneEditor) return false;

		return !sceneEditor->IsReadOnly() && sceneEditor->HasSelectedEntities();
	}

	bool SceneTreeWindow::CheckReadOnly(const MenuItemEventData& eventData)
	{
		SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		if (!sceneEditor) return false;
		return !sceneEditor->IsReadOnly();
	}

	void SceneTreeWindow::RegisterType(NativeReflectType<SceneTreeWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Scene Tree", .action = OpenSceneTree});

		AddMenuItem(MenuItemCreation{.itemName = "Create Entity", .priority = 0, .action = AddSceneEntity, .enable = CheckReadOnly});
		AddMenuItem(MenuItemCreation{.itemName = "Create Entity From Asset", .priority = 15, .action = AddSceneEntityFromAsset, .enable = CheckReadOnly,});
		AddMenuItem(MenuItemCreation{.itemName = "Add Component", .priority = 20, .action = AddComponent, .enable = CheckReadOnly,});
		AddMenuItem(MenuItemCreation{.itemName = "Rename", .priority = 200, .itemShortcut = {.presKey = Key::F2}, .action = RenameSceneEntity, .enable = CheckSelectedEntity});
		AddMenuItem(MenuItemCreation{.itemName = "Duplicate", .priority = 210, .itemShortcut = {.ctrl = true, .presKey = Key::D}, .action = DuplicateSceneEntity, .enable = CheckSelectedEntity});
		AddMenuItem(MenuItemCreation{.itemName = "Delete", .priority = 220, .itemShortcut = {.presKey = Key::Delete}, .action = DeleteSceneEntity, .enable = CheckSelectedEntity});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::RightTop,
			.createOnInit = true
		});
	}
}
