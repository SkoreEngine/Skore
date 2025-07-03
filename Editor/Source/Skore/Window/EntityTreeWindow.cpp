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
#include "imgui_internal.h"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/SceneCommon.hpp"

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

	void EntityTreeWindow::DrawRIDEntity(SceneEditor* sceneEditor, RID entity, bool& entitySelected, RID parent, bool disabled, bool removed)
	{
		auto& style = ImGui::GetStyle();

		if (!entity) return;
		ResourceObject entityObject = Resources::Read(entity);
		if (!entityObject) return;

		if (!disabled && parent)
		{
			disabled = Resources::GetParent(entity) != parent;

			if (!disabled)
			{
				lastParentNotPrototype = entity;
			}
		}

		bool       root = sceneEditor->GetRootEntity() == entity;
		bool       drawNode = entityObject.GetSubObjectSetCount(EntityResource::Children) > 0 || entityObject.GetPrototypeRemovedCount(EntityResource::Children);
		StringView name = entityObject.GetString(EntityResource::Name);
		bool       active = !entityObject.GetBool(EntityResource::Deactivated);
		bool       locked = entityObject.GetBool(EntityResource::Locked);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (!root)
		{
			DrawMovePayload(entity.id, entity);
		}

		stringCache.Clear();
		stringCache += root ? ICON_FA_CUBES : ICON_FA_CUBE;
		stringCache += " ";
		stringCache += name;

		bool isSelected = sceneEditor->IsSelected(entity);
		bool open = false;

		if (root || sceneEditor->IsParentOfSelected(entity))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		}

		if (removed)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
		}
		else if (disabled)
		{
			ImVec4 color = style.Colors[ImGuiCol_Text];
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.x, color.y, color.z, style.Alpha * style.DisabledAlpha));
		}
		else if (entityObject.GetPrototype())
		{
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

			if (!renamingFocus)
			{
				renamingFocus = true;
			}

			ImGui::SetCursorPos(cursorPos);
		}
		else if (drawNode)
		{
			open = ImGuiTreeNode(IntToPtr(entity.id), stringCache.CStr());
		}
		else
		{
			ImGuiTreeLeaf(IntToPtr(entity.id), stringCache.CStr());
		}

		if (removed || disabled || entityObject.GetPrototype())
		{
			ImGui::PopStyleColor();
		}

		bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
		bool ctrlDown = ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_RightCtrl));

		if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && isHovered)
		{
			if (!disabled)
			{
				sceneEditor->SelectEntity(entity, !ctrlDown);
			}
			else
			{
				sceneEditor->ClearSelection();
				parentOnPopupSelection = lastParentNotPrototype;
				entityOnPopupSelection = entity;
			}
		}

		if ((ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right)) && isHovered)
		{
			if (!disabled && sceneEditor->IsSelected(entity))
			{
				if (!ctrlDown)
				{
					sceneEditor->SelectEntity(entity, true);
				}
			}
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
			parentOnPopupSelection = lastParentNotPrototype;
			entityOnPopupSelection = entity;
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ENTITY_PAYLOAD))
			{
				sceneEditor->ChangeParentOfSelected(entity);
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::TableNextColumn();
		{
			ImGui::BeginDisabled(sceneEditor->IsReadOnly());

			char buffer[35]{};
			sprintf(buffer, "activated-button%llu", entity.id);

			TableButtonStyle buttonStyle;
			ScopedStyleColor textColor{ImGuiCol_Text, !active ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : ImGui::GetStyle().Colors[ImGuiCol_Text]};

			ImGui::PushID(buffer);

			if (ImGui::Button(active ? ICON_FA_EYE : ICON_FA_EYE_SLASH, ImVec2(ImGui::GetColumnWidth(), 0)))
			{
				sceneEditor->SetActivated(entity, !active);
			}

			ImGui::PopID();

			ImGui::EndDisabled();
		}

		ImGui::TableNextColumn();
		{
			ImGui::BeginDisabled(sceneEditor->IsReadOnly());

			char buffer[35]{};
			sprintf(buffer, "lock-button%llu", entity.id);

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
			fillRowWithColour(ImVec4(1.0f, 1.0f, 1.0f, 0.2f));
		}

		if (open)
		{
			entityObject.IterateSubObjectSet(EntityResource::Children, true, [&](RID child)
			{
				DrawRIDEntity(sceneEditor, child, entitySelected, entity, disabled, removed);
				return true;
			});

			entityObject.IteratePrototypeRemoved(EntityResource::Children, true, [&](RID child)
			{
				DrawRIDEntity(sceneEditor, child, entitySelected, entity, disabled, true);
				return true;
			});

			ImGui::TreePop();
		}
	}

	void EntityTreeWindow::DrawEntity(SceneEditor* sceneEditor, Entity* entity, bool& entitySelected)
	{

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		bool root = entity->GetParent() == nullptr;
		bool isSelected = sceneEditor->IsSelected(entity);

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
			sceneEditor->SelectEntity(entity, !ctrlDown && !sceneEditor->IsSelected(entity));
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
				DrawEntity(sceneEditor, child, entitySelected);
			}
			ImGui::TreePop();
		}

	}

	void EntityTreeWindow::DrawMovePayload(u64 id, RID moveTo) const
	{
		ImVec2 screenPos = ImVec2(ImGui::GetWindowPos().x, ImGui::GetCursorScreenPos().y);
		ImVec2 screenPos2 = screenPos + ImVec2(ImGui::GetContentRegionMax().x, std::ceil(1 * ImGui::GetStyle().ScaleFactor));

		if (ImGui::BeginDragDropTargetCustom(ImRect(screenPos, screenPos2), HashInt32(id)))
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ENTITY_PAYLOAD))
			{
				// SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
				//
				// Array<Entity*> selectedEntities;
				// for (const auto& selected : sceneEditor->GetSelectedEntities())
				// {
				// 	if (Entity* entity = sceneEditor->GetCurrentScene()->FindEntityByUUID(selected))
				// 	{
				// 		selectedEntities.EmplaceBack(entity);
				// 	}
				// }
				//
				// if (!selectedEntities.Empty())
				// {
				// 	//TODO - transactions
				//
				// 	// Ref<Transaction> transaction = UndoRedoSystem::BeginTransaction("Move Entity");
				// 	//
				// 	// for (Entity* entity : selectedEntities)
				// 	// {
				// 	// 	if (moveTo && moveTo->IsChildOf(entity))
				// 	// 		continue;
				// 	//
				// 	// 	if (entity == moveTo)
				// 	// 		continue;
				// 	//
				// 	// 	transaction->AddCommand(Alloc<MoveEntityCommand>(sceneEditor, entity, parent, moveTo));
				// 	// }
				// 	//
				// 	// UndoRedoSystem::EndTransaction(transaction);
				// }
			}
			ImGui::EndDragDropTarget();
		}
	}

	void EntityTreeWindow::Draw(u32 id, bool& open)
	{
		SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();

		bool  entitySelected = false;
		auto& style = ImGui::GetStyle();
		auto  originalWindowPadding = style.WindowPadding;

		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (!ImGuiBegin(id, ICON_FA_LIST " Entity Tree", &open, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::End();
			return;
		}

		if (!sceneEditor->GetRootEntity())
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

					bool drawRID = !showSceneEntity && !sceneEditor->IsSimulationRunning();
					if (drawRID)
					{
						DrawRIDEntity(sceneEditor, sceneEditor->GetRootEntity(), entitySelected, RID{}, sceneEditor->IsReadOnly(), false);
					}
					else if (sceneEditor->GetCurrentScene() != nullptr)
					{
						DrawEntity(sceneEditor, sceneEditor->GetCurrentScene()->GetRootEntity(), entitySelected);
					}

					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					if (drawRID)
					{
						DrawMovePayload(PtrToInt(sceneEditor), RID{});
					}

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
		else
		{
			parentOnPopupSelection = {};
			entityOnPopupSelection = {};
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
		if (SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor())
		{
			sceneEditor->Create();
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

		SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		if (!sceneEditor) return;

		if (!sceneEditor->HasSelectedEntities()) return;

		window->renamingSelected = true;
		window->renamingFocus = false;
	}

	void EntityTreeWindow::DuplicateSceneEntity(const MenuItemEventData& eventData)
	{
		if (SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor())
		{
			sceneEditor->DuplicateSelected();
		}
	}
	void EntityTreeWindow::DeleteSceneEntity(const MenuItemEventData& eventData)
	{
		if (SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor())
		{
			sceneEditor->DestroySelected();
		}
	}

	bool EntityTreeWindow::CheckEntityActions(const MenuItemEventData& eventData)
	{
		return !CheckSelectedPrototypeEntity(eventData);
	}

	bool EntityTreeWindow::CheckSelectedEntity(const MenuItemEventData& eventData)
	{
		SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		if (!sceneEditor) return false;

		return !sceneEditor->IsReadOnly() && sceneEditor->HasSelectedEntities();
	}

	bool EntityTreeWindow::CheckSelectedPrototypeEntity(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (!window->entityOnPopupSelection) return false;

		RID root = Editor::GetCurrentWorkspace().GetSceneEditor()->GetRootEntity();
		RID current = window->entityOnPopupSelection;
		while (current != root)
		{
			current = Resources::GetParent(current);
			if (!current)
			{
				return true;
			}
		}
		return false;
	}

	bool EntityTreeWindow::CheckReadOnly(const MenuItemEventData& eventData)
	{
		SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
		if (!sceneEditor) return false;
		return !sceneEditor->IsReadOnly();
	}

	void EntityTreeWindow::ShowSceneEntity(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		window->showSceneEntity = !window->showSceneEntity;
	}

	bool EntityTreeWindow::IsShowSceneEntitySelected(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		return window->showSceneEntity;
	}

	void EntityTreeWindow::OverridePrototype(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (!window-> parentOnPopupSelection || !window->entityOnPopupSelection) return;
		if (SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor())
		{
			sceneEditor->OverrideEntity(window->parentOnPopupSelection, window->entityOnPopupSelection);
		}
	}

	void EntityTreeWindow::RemoveFromThisInstance(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (!window-> parentOnPopupSelection || !window->entityOnPopupSelection) return;
		if (SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor())
		{
			sceneEditor->RemoveFromThisInstance(window->parentOnPopupSelection, window->entityOnPopupSelection);
		}
	}

	void EntityTreeWindow::RemoveOverride(const MenuItemEventData& eventData)
	{
		if (SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor())
		{
			sceneEditor->RemoveOverrideFromSelected();
		}
	}

	bool EntityTreeWindow::CheckIsOverride(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (!window->entityOnPopupSelection) return false;

		return Resources::GetStorage(window->entityOnPopupSelection)->instantiated != nullptr;
	}

	void EntityTreeWindow::OpenEntityTree(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<EntityTreeWindow>();
	}

	void EntityTreeWindow::RegisterType(NativeReflectType<EntityTreeWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Entity Tree", .action = OpenEntityTree});

		AddMenuItem(MenuItemCreation{.itemName = "Remove Override", .priority = -100, .action = RemoveOverride, .enable = CheckReadOnly, .visible = CheckIsOverride});
		AddMenuItem(MenuItemCreation{.itemName = "Override", .priority = -100, .action = OverridePrototype, .visible = CheckSelectedPrototypeEntity});
		AddMenuItem(MenuItemCreation{.itemName = "Remove From This Instance", .priority = -95, .action = RemoveFromThisInstance, .visible = CheckSelectedPrototypeEntity});
		AddMenuItem(MenuItemCreation{.itemName = "Add Back This Instance", .priority = -95, .action = RemoveFromThisInstance, .visible = CheckSelectedPrototypeEntity});

		AddMenuItem(MenuItemCreation{.itemName = "Create Entity", .priority = 0, .action = AddSceneEntity, .enable = CheckReadOnly, .visible = CheckEntityActions});
		AddMenuItem(MenuItemCreation{.itemName = "Create Entity From Asset", .priority = 15, .action = AddSceneEntityFromAsset, .enable = CheckReadOnly, .visible = CheckEntityActions});
		AddMenuItem(MenuItemCreation{.itemName = "Add Component", .priority = 20, .action = AddComponent, .enable = CheckReadOnly, .visible = CheckEntityActions});

		AddMenuItem(MenuItemCreation{.itemName = "Rename", .priority = 200, .itemShortcut = {.presKey = Key::F2}, .action = RenameSceneEntity, .enable = CheckSelectedEntity, .visible = CheckEntityActions});
		AddMenuItem(MenuItemCreation{.itemName = "Duplicate", .priority = 210, .itemShortcut = {.ctrl = true, .presKey = Key::D}, .action = DuplicateSceneEntity, .enable = CheckSelectedEntity, .visible = CheckEntityActions});
		AddMenuItem(MenuItemCreation{.itemName = "Delete", .priority = 220, .itemShortcut = {.presKey = Key::Delete}, .action = DeleteSceneEntity, .enable = CheckSelectedEntity, .visible = CheckEntityActions});

		AddMenuItem(MenuItemCreation{.itemName = "(Debug) Show Scene Entity", .priority = 1000, .action = ShowSceneEntity, .selected = IsShowSceneEntitySelected});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::RightTop,
			.createOnInit = true
		});
	}
}
