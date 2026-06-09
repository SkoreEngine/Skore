#include "Skore/Window/EntityTreeWindow.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "Skore/Window/SceneViewWindow.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Window/ResourceDebuggerWindow.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	MenuItemContext EntityTreeWindow::menuItemContext = {};

	namespace
	{
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


	const char* EntityTreeWindow::GetTitle() const
	{
		return ICON_FA_LIST " Entity Tree";
	}

	void EntityTreeWindow::Init(VoidPtr userData)
	{
		iconSize = ImGui::CalcTextSize(ICON_FA_EYE).x;
	}

	void EntityTreeWindow::DrawEntity(SceneEditor* sceneEditor, RID rid, RID parent, bool removed)
	{
		auto& style = ImGui::GetStyle();

		if (!rid) return;
		ResourceObject resourceObject = Resources::Read(rid);
		if (!resourceObject) return;

		bool root = sceneEditor->GetOpenedResource() == rid;
		bool isScene = root && sceneEditor->IsOpenedScene();
		u32  childrenField = SceneEditor::GetChildrenField(rid);
		bool drawNode = (resourceObject.GetSubObjectListCount(childrenField) > 0 || resourceObject.GetPrototypeRemovedCount(childrenField)) && !removed && !filter.IsActive();

		StringView name = "";

		if (root)
		{
			if (ResourceObject assetObject = Resources::Read(Resources::GetParent(rid)))
			{
				name = assetObject.GetString(ResourceAsset::Name);
			}
		}
		else if (resourceObject)
		{
			name = resourceObject.GetString(EntityResource::Name);
		}

		if (!isScene)
		{
			if (!filter.PassFilter(name.CStr()))
			{
				return;
			}
		}

		bool active = isScene ? true : !resourceObject.GetBool(EntityResource::Deactivated);
		bool locked = isScene ? false : resourceObject.GetBool(EntityResource::Locked);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (!root)
		{
			DrawMovePayload(rid.id, rid);
		}

		stringCache.Clear();
		stringCache += root ? ICON_FA_CUBES : ICON_FA_CUBE;
		stringCache += " ";
		stringCache += name;

		bool isSelected = sceneEditor->IsSelected(rid) || rid == entitySelection;
		bool open = false;
		bool hasOpen = false;

		if (root || sceneEditor->IsParentOfSelected(rid))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		}

		if (removed)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
		}
		else if (resourceObject.GetPrototype())
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
				sceneEditor->Rename(rid, renamingStringCache);
			}

			if (!renamingFocus)
			{
				renamingFocus = true;
			}

			ImGui::SetCursorPos(cursorPos);
		}
		else if (drawNode)
		{

			VoidPtr ptrId = IntToPtr(rid.id);
			ImGuiID id = ImGui::GetCurrentWindow()->GetID(ptrId);

			auto flags = ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_OpenOnDoubleClick |
				ImGuiTreeNodeFlags_SpanAvailWidth |
				ImGuiTreeNodeFlags_SpanFullWidth |
				ImGuiTreeNodeFlags_FramePadding;

			hasOpen = ImGui::TreeNodeUpdateNextOpen(id,  flags);
			open = ImGuiTreeNode(ptrId, stringCache.CStr());
		}
		else
		{
			ImGuiTreeLeaf(IntToPtr(rid.id), stringCache.CStr());
		}

		bool nodeChanged = (!hasOpen && open) || (hasOpen && !open);
		if (nodeChanged)
		{
			cancelSelection = true;
		}

		if (removed || resourceObject.GetPrototype())
		{
			ImGui::PopStyleColor();
		}

		bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

		if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			SceneViewWindow::ViewEntity(rid);
		}

		if (!nodeChanged && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && isHovered)
		{
			parentSelection = parent;
			entitySelection = rid;

			lastParentSelection = parentSelection;
			lastEntitySelection = entitySelection;

			lastSelectionRemoved = removed;
		}

		if (!root && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
		{
			ImGui::SetDragDropPayload(SK_ENTITY_PAYLOAD, nullptr, 0);
			ImGui::Text("%s", name.CStr());
			ImGui::EndDragDropSource();
			pushSelection = true;
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ENTITY_PAYLOAD))
			{
				sceneEditor->ChangeParentOfSelected(rid);
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::TableNextColumn();
		{
			ImGui::BeginDisabled(sceneEditor->IsReadOnly() || removed);

			char buffer[35]{};
			sprintf(buffer, "activated-button%llu", rid.id);

			TableButtonStyle buttonStyle;
			ScopedStyleColor textColor{ImGuiCol_Text, !active ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : ImGui::GetStyle().Colors[ImGuiCol_Text]};

			ImGui::PushID(buffer);

			if (ImGui::Button(active ? ICON_FA_EYE : ICON_FA_EYE_SLASH, ImVec2(ImGui::GetColumnWidth(), 0)))
			{
				sceneEditor->SetActivated(rid, !active);
			}

			ImGui::PopID();

			ImGui::EndDisabled();
		}

		ImGui::TableNextColumn();
		{
			ImGui::BeginDisabled(sceneEditor->IsReadOnly() || removed);

			char buffer[35]{};
			sprintf(buffer, "lock-button%llu", rid.id);

			TableButtonStyle buttonStyle;
			ScopedStyleColor textColor{ImGuiCol_Text, locked ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : ImGui::GetStyle().Colors[ImGuiCol_Text]};

			ImGui::PushID(buffer);

			if (ImGui::Button(locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN, ImVec2(ImGui::GetColumnWidth(), 0)))
			{
				sceneEditor->SetLocked(rid, !locked);
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
			resourceObject.IterateSubObjectList(childrenField, [&](RID child)
			{
				DrawEntity(sceneEditor, child, rid, removed);
				return true;
			});

			resourceObject.IteratePrototypeRemoved(childrenField, [&](RID child)
			{
				DrawEntity(sceneEditor, child,  rid, true);
			});

			ImGui::TreePop();
		}
	}

	void EntityTreeWindow::DrawEntity(SceneEditor* sceneEditor, Entity* entity, bool& entitySelected)
	{
		if (entity == nullptr) return;

		bool root = entity->GetParent() == nullptr;
		bool isSelected = sceneEditor->IsSelected(entity);
		bool hasChildren = !entity->GetChildren().Empty();
		bool active = entity->IsActive();

		StringView name = entity->GetName();

		//when a filter is active the tree collapses to a flat list: skip non-matching
		//nodes but keep recursing so matching descendants stay reachable
		if (!filter.PassFilter(name.CStr()))
		{
			for (Entity* child : entity->GetChildren())
			{
				DrawEntity(sceneEditor, child, entitySelected);
			}
			return;
		}

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		stringCache.Clear();
		stringCache += root ? ICON_FA_CUBES : ICON_FA_CUBE;
		stringCache += " ";
		stringCache += name;

		bool open = false;
		bool hasOpen = false;

		if (root)
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		}

		if (!active)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		}

		if (hasChildren && !filter.IsActive())
		{
			ImGuiID id = ImGui::GetCurrentWindow()->GetID(entity);

			auto flags = ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_OpenOnDoubleClick |
				ImGuiTreeNodeFlags_SpanAvailWidth |
				ImGuiTreeNodeFlags_SpanFullWidth |
				ImGuiTreeNodeFlags_FramePadding;

			hasOpen = ImGui::TreeNodeUpdateNextOpen(id, flags);
			open = ImGuiTreeNode(entity, stringCache.CStr());
		}
		else
		{
			ImGuiTreeLeaf(entity, stringCache.CStr());
		}

		if (!active)
		{
			ImGui::PopStyleColor();
		}

		//a click that toggles the node open/closed must not also select the entity
		bool nodeChanged = (!hasOpen && open) || (hasOpen && !open);
		if (nodeChanged)
		{
			cancelSelection = true;
		}

		bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

		//record the click here, but commit the selection on mouse release in Draw() to mimic the editor RID path
		if (!nodeChanged && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && isHovered)
		{
			entityPtrSelection = entity;
		}

		ImGui::TableNextColumn();
		ImGui::TableNextColumn();

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
			for (Entity* child : entity->GetChildren())
			{
				DrawEntity(sceneEditor, child, entitySelected);
			}
			ImGui::TreePop();
		}
		else if (filter.IsActive())
		{
			//flat filtered list: recurse without a tree node so matching descendants still show
			for (Entity* child : entity->GetChildren())
			{
				DrawEntity(sceneEditor, child, entitySelected);
			}
		}
	}

	void EntityTreeWindow::DrawMovePayload(u64 id, RID moveBefore) const
	{
		ImVec2 screenPos = ImVec2(ImGui::GetWindowPos().x, ImGui::GetCursorScreenPos().y);
		ImVec2 screenPos2 = screenPos + ImVec2(ImGui::GetContentRegionMax().x, std::ceil(1.5 * ImGui::GetStyle().ScaleFactor));

		if (ImGui::BeginDragDropTargetCustom(ImRect(screenPos, screenPos2), HashInt32(id)))
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ENTITY_PAYLOAD))
			{
				SceneEditor* sceneEditor = workspace->GetSceneEditor();
				sceneEditor->MoveSelectedBefore(moveBefore);
			}
			ImGui::EndDragDropTarget();
		}
	}

	void EntityTreeWindow::Draw(bool& open)
	{
		SceneEditor* sceneEditor = workspace->GetSceneEditor();

		bool  entitySelected = false;
		auto& style = ImGui::GetStyle();
		auto  originalWindowPadding = style.WindowPadding;

		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (!ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::End();
			return;
		}

		pushSelection = false;
		cancelSelection = false;
		ctrlDown = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
		shiftDown = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

		//defer clear to avoid flickering
		bool shouldClear = false;

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_NoPopupHierarchy) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			if (!ctrlDown)
			{
				shouldClear = true;
			}
			entitySelection = {};
			parentSelection = {};
			entityPtrSelection = nullptr;
		}

		if (!sceneEditor->GetOpenedResource())
		{
			ImGuiCentralizedText("Open a scene or entity in the Project Browser");
			ImGui::End();
			return;
		}

		//RID/resource hierarchy while editing; live Scene*/Entity* hierarchy while simulating or when the debug toggle is on
		bool drawRID = !showSceneEntity && !sceneEditor->IsSimulationRunning();
		if (drawRID)
		{
			//not showing the live scene; drop any stale pending runtime selection
			entityPtrSelection = nullptr;
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
			if (ImGuiSearchInputText(id + 10, searchEntity))
			{
				memset(filter.InputBuf, 0, sizeof(filter.InputBuf));
				memcpy(filter.InputBuf, searchEntity.CStr(), Math::Min(searchEntity.Size(), sizeof(filter.InputBuf)));
				filter.Build();
			}
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

					if (drawRID)
					{
						if (!filter.IsActive())
						{
							DrawEntity(sceneEditor, sceneEditor->GetOpenedResource(), RID{}, false);
						}
						else
						{
							//TODO - improve this code
							std::function<void(RID, RID)> draw;
							draw = [&](RID parent, RID entity)
							{
								DrawEntity(sceneEditor, entity, parent, false);

								if (ResourceObject entityObject = Resources::Read(entity))
								{
									entityObject.IterateSubObjectList(SceneEditor::GetChildrenField(entity), [&](RID child)
									{
										draw(entity, child);
										return true;
									});
								}
							};
							draw({}, sceneEditor->GetOpenedResource());
						}

					}
					else if (Scene* scene = sceneEditor->GetCurrentScene())
					{
						for (Entity* entity : scene->GetEntities())
						{
							DrawEntity(sceneEditor, entity, entitySelected);
						}
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

		if (!cancelSelection && shouldClear && ((!entitySelection && !entityPtrSelection) || lastSelectionRemoved))
		{
			sceneEditor->ClearSelection();
		}

		bool shouldClearInternal = false;

		bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_NoPopupHierarchy);
		bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Left);

		if (!cancelSelection && !lastSelectionRemoved && entitySelection && (released || pushSelection))
		{
			sceneEditor->SelectEntity(entitySelection, !ctrlDown);
			shouldClearInternal = true;
		}

		//runtime Scene*/Entity* selection: same deferred commit, but on Entity* instead of RID
		if (!cancelSelection && entityPtrSelection && (released || pushSelection))
		{
			sceneEditor->SelectEntity(entityPtrSelection, !ctrlDown);
			shouldClearInternal = true;
		}

		if (hovered)
		{
			if (menuItemContext.ExecuteHotKeys(this))
			{
				closePopup = true;
			}

			if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
			{
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

		ImGuiResourceSelectionPopup(id + 100, TypeInfo<EntityResource>::ID(), {}, openAssetSelectionPopup,
			[](RID rid, VoidPtr userData)
			{
				SceneEditor* se = static_cast<SceneEditor*>(userData);
				if (rid)
				{
					RID payload = ResourceAssets::GetAssetPayload(rid);
					RID parent = se->GetSelectedEntities().Empty() ? se->GetOpenedResource() : se->GetSelectedEntities()[0];
					se->CreateFromAsset(parent, payload ? payload : rid, nullptr);
				}
			}, sceneEditor);
		openAssetSelectionPopup = false;

		ImGui::End();

		if (shouldClearInternal)
		{
			entitySelection = {};
			parentSelection = {};
			entityPtrSelection = nullptr;
		}
	}

	void EntityTreeWindow::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuItemContext.AddMenuItem(menuItem);
	}

	void EntityTreeWindow::AddSceneEntity(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (SceneEditor* sceneEditor = window->workspace->GetSceneEditor())
		{
			sceneEditor->Create(static_cast<EntityCreationType>(eventData.userData));
		}
	}
	void EntityTreeWindow::AddSceneEntityFromAsset(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		window->openAssetSelectionPopup = true;
	}

	void EntityTreeWindow::AddComponent(const MenuItemEventData& eventData)
	{
		//TODO
	}

	void EntityTreeWindow::RenameSceneEntity(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);

		SceneEditor* sceneEditor = window->workspace->GetSceneEditor();
		if (!sceneEditor) return;

		if (!sceneEditor->HasSelectedEntities()) return;

		window->renamingSelected = true;
		window->renamingFocus = false;
	}

	void EntityTreeWindow::DuplicateSceneEntity(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (SceneEditor* sceneEditor = window->workspace->GetSceneEditor())
		{
			sceneEditor->DuplicateSelected();
		}
	}
	void EntityTreeWindow::DeleteSceneEntity(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (SceneEditor* sceneEditor = window->workspace->GetSceneEditor())
		{
			sceneEditor->DestroySelected();
		}
	}

	bool EntityTreeWindow::CheckEntityActions(const MenuItemEventData& eventData)
	{
		if (CheckIsRemoved(eventData)) return false;
		return true;
	}

	bool EntityTreeWindow::CheckSelectedEntity(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		SceneEditor* sceneEditor = window->workspace->GetSceneEditor();
		if (!sceneEditor) return false;
		if (sceneEditor->IsReadOnly() || !sceneEditor->HasSelectedEntities()) return false;

		// disable actions when only the root is selected
		Span<RID> selected = sceneEditor->GetSelectedEntities();
		RID root = sceneEditor->GetOpenedResource();
		for (RID entity : selected)
		{
			if (entity != root) return true;
		}
		return false;
	}

	bool EntityTreeWindow::CheckReadOnly(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		SceneEditor* sceneEditor = window->workspace->GetSceneEditor();
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


	bool EntityTreeWindow::CheckIsOverride(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (!window->lastEntitySelection) return false;

		return false;
	}

	void EntityTreeWindow::RemoveOverride(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (!window->lastEntitySelection) return;
	}

	bool EntityTreeWindow::CheckIsRemoved(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (!window->lastEntitySelection || !window->lastParentSelection) return false;

		if (ResourceObject parentObject = Resources::Read(window->lastParentSelection))
		{
			return parentObject.IsRemoveFromPrototypeSubObjectList(SceneEditor::GetChildrenField(window->lastParentSelection),  window->lastEntitySelection);
		}

		return false;
	}

	void EntityTreeWindow::AddBackToThisInstance(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (!window->lastEntitySelection || !window->lastParentSelection) return;

		SceneEditor* sceneEditor = window->workspace->GetSceneEditor();
		sceneEditor->AddBackToThisInstance(window->lastParentSelection, window->lastEntitySelection);
	}

	void EntityTreeWindow::ShowResourceInspector(const MenuItemEventData& eventData)
	{
		EntityTreeWindow* window = static_cast<EntityTreeWindow*>(eventData.drawData);
		if (SceneEditor* sceneEditor = window->workspace->GetSceneEditor())
		{
			Span<RID> selected = sceneEditor->GetSelectedEntities();
			if (!selected.Empty() && selected[0].id)
			{
				ResourceDebuggerWindow::InspectResource(selected[0]);
			}
		}
	}

	void EntityTreeWindow::OpenEntityTree(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<EntityTreeWindow>();
	}

	void EntityTreeWindow::RegisterType(NativeReflectType<EntityTreeWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Entity Tree", .action = OpenEntityTree});

		AddMenuItem(MenuItemCreation{.itemName = "Revert Instance Overrides", .priority = -100, .action = RemoveOverride, .enable = CheckReadOnly, .visible = CheckIsOverride});
		AddMenuItem(MenuItemCreation{.itemName = "Add Back This Instance", .priority = -95, .action = AddBackToThisInstance, .visible = CheckIsRemoved});

		AddMenuItem(MenuItemCreation{.itemName = "Create Entity", .priority = 0, .action = AddSceneEntity, .enable = CheckReadOnly, .visible = CheckEntityActions, .userData = static_cast<u64>(EntityCreationType::Empty)});
		AddMenuItem(MenuItemCreation{.itemName = "Create Entity From Asset", .priority = 15, .action = AddSceneEntityFromAsset, .enable = CheckReadOnly, .visible = CheckEntityActions});

		AddMenuItem(MenuItemCreation{.itemName = "Rename", .priority = 200, .itemShortcut = {.presKey = Key::F2}, .action = RenameSceneEntity, .visible = CheckSelectedEntity});
		AddMenuItem(MenuItemCreation{.itemName = "Duplicate", .priority = 210, .itemShortcut = {.ctrl = true, .presKey = Key::D}, .action = DuplicateSceneEntity, .visible = CheckSelectedEntity});
		AddMenuItem(MenuItemCreation{.itemName = "Delete", .priority = 220, .itemShortcut = {.presKey = Key::Delete}, .action = DeleteSceneEntity, .visible = CheckSelectedEntity});

		AddMenuItem(MenuItemCreation{.itemName = "Show Resource Inspector", .icon = ICON_FA_MAGNIFYING_GLASS, .priority = 500, .action = ShowResourceInspector});

		AddMenuItem(MenuItemCreation{.itemName = "Show Scene Entity", .priority = 1000, .action = ShowSceneEntity, .selected = IsShowSceneEntitySelected, .debugOption = true});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::RightTop,
			.workspaceTypes = {WorkspaceTypes::Scene}
		});
	}
}
