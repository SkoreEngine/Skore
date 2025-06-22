#include "PropertiesWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/World/Component.hpp"
#include "Skore/World/Entity.hpp"
#include "Skore/World/WorldCommon.hpp"

namespace Skore
{
	PropertiesWindow::PropertiesWindow()
	{
		Event::Bind<OnEntitySelection, &PropertiesWindow::EntitySelection>(this);
		Event::Bind<OnEntityDeselection, &PropertiesWindow::EntityDeselection>(this);

		Event::Bind<OnEntityDebugSelection, &PropertiesWindow::EntityDebugSelection>(this);
		Event::Bind<OnEntityDebugDeselection, &PropertiesWindow::EntityDebugDeselection>(this);

		Event::Bind<OnAssetSelection, &PropertiesWindow::AssetSelection>(this);
	}

	PropertiesWindow::~PropertiesWindow()
	{
		Event::Unbind<OnEntitySelection, &PropertiesWindow::EntitySelection>(this);
		Event::Unbind<OnEntityDeselection, &PropertiesWindow::EntityDeselection>(this);

		Event::Unbind<OnEntityDebugSelection, &PropertiesWindow::EntityDebugSelection>(this);
		Event::Unbind<OnEntityDebugDeselection, &PropertiesWindow::EntityDebugDeselection>(this);

		Event::Unbind<OnAssetSelection, &PropertiesWindow::AssetSelection>(this);
	}

	void PropertiesWindow::DrawEntity(u32 id, WorldEditor* worldEditor, RID entity)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		if (!entity) return;

		ResourceObject entityObject = Resources::Read(entity);


		bool readOnly = worldEditor->IsReadOnly();

		ImGuiInputTextFlags nameFlags = 0;
		if (readOnly)
		{
			nameFlags |= ImGuiInputTextFlags_ReadOnly;
		}

		if (ImGui::BeginTable("#entity-table", 2))
		{
			ImGui::BeginDisabled(readOnly);

			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();

			ImGui::Text("Name");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = entityObject.GetString(EntityResource::Name);

			u32 hash = HashValue(reinterpret_cast<usize>(&entity));

			if (ImGuiInputText(hash, stringCache, nameFlags))
			{
				renamingCache = stringCache;
				renamingFocus = true;
				renamingEntity = entity;
			}

			if (!ImGui::IsItemActive() && renamingFocus)
			{
				worldEditor->Rename(renamingEntity, renamingCache);
				renamingEntity = {};
				renamingFocus = false;
				renamingCache.Clear();
			}

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("UUID");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = entityObject.GetUUID().ToString();

			ImGuiInputText(hash + 10, stringCache, ImGuiInputTextFlags_ReadOnly);
			ImGui::EndDisabled();
			ImGui::EndTable();
		}

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10 * style.ScaleFactor);

		f32  width = ImGui::GetContentRegionAvail().x;
		auto size = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;

		ImGui::BeginHorizontal("horizontal-01", ImVec2(width, size));

		ImGui::Spring(1.f);
		bool addComponent = false;

		ImGui::BeginDisabled(readOnly);

		if (ImGuiBorderedButton("Add Component", ImVec2(width * 2 / 3, size)))
		{
			addComponent = true;
		}

		ImGui::EndDisabled();

		ImVec2 max = ImGui::GetItemRectMax();
		ImVec2 min = ImGui::GetItemRectMin();

		ImGui::Spring(1.f);

		ImGui::EndHorizontal();

		if (entityObject.GetPrototype())
		{
			ImGui::BeginHorizontal(9999, ImVec2(width, size));
			ImGui::Spring(1.f);

			if (ImGuiBorderedButton("Open Prefab", ImVec2((width * 2) / 3, size)))
			{
				RID prototype = entityObject.GetPrototype();
				Editor::ExecuteOnMainThread([prototype]()
				{
					Editor::GetCurrentWorkspace().GetWorldEditor()->OpenEntity(prototype);
				});
			}

			ImGui::Spring(1.f);
			ImGui::EndHorizontal();
		}

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * style.ScaleFactor);

		bool openComponentSettings = false;

		auto drawCollapsingHeader = [&](RID rid, StringView formattedName, StringView scopeName)
		{
			bool fromPrototype = Resources::GetParent(rid) != entity;

			bool propClicked = false;
			bool open = ImGuiCollapsingHeaderProps(HashValue(rid.id), formattedName.CStr(), &propClicked);
			if (propClicked)
			{
				openComponentSettings = true;
				selectedComponent = rid;
			}

			if (open)
			{
				ImGui::BeginDisabled(readOnly || fromPrototype);
				ImGui::Indent();

				ImGuiDrawResource(ImGuiDrawResourceInfo{
					.rid = rid,
					.scopeName = scopeName
				});

				ImGui::Unindent();
				ImGui::EndDisabled();
			}
		};

		RID transform = entityObject.GetSubObject(EntityResource::Transform);
		if (transform)
		{
			drawCollapsingHeader(transform, "Transform", "Transform Update");
		}

		entityObject.IterateSubObjectSet(EntityResource::Components, true, [&](RID component)
		{
			if (ResourceType* componentType = Resources::GetType(component);
				componentType != nullptr &&
				componentType->GetReflectType() != nullptr)
			{
				String formattedName = FormatName(componentType->GetReflectType()->GetSimpleName());
				String scope = String(formattedName).Append(" Update");
				drawCollapsingHeader(component, formattedName, scope);
			}

			return true;
		});

		if (addComponent)
		{
			ImGui::OpenPopup("add-component-popup");
		}

		ImGui::SetNextWindowPos(ImVec2(min.x, max.y + 5));
		auto sizePopup = max.x - min.x;
		ImGui::SetNextWindowSize(ImVec2(sizePopup, 0), ImGuiCond_Appearing);

		auto popupRes = ImGuiBeginPopupMenu("add-component-popup", 0, false);
		if (popupRes)
		{
			ImGui::SetNextItemWidth(sizePopup - style.WindowPadding.x * 2);
			ImGuiSearchInputText(id + 100, searchComponentString);
			ImGui::Separator();

			for (TypeID componentId : Reflection::GetDerivedTypes(TypeInfo<Component>::ID()))
			{
				if (ReflectType* refletionType = Reflection::FindTypeById(componentId))
				{
					String name = FormatName(refletionType->GetSimpleName());
					if (ImGui::Selectable(name.CStr()))
					{
						worldEditor->AddComponent(entity, componentId);
					}
				}
			}
		}
		ImGuiEndPopupMenu(popupRes);

		if (openComponentSettings)
		{
			ImGui::OpenPopup("open-component-settings");
		}

		bool canRemove = !readOnly && selectedComponent != transform;

		bool popupOpenSettings = ImGuiBeginPopupMenu("open-component-settings", 0, false);
		if (popupOpenSettings && selectedComponent)
		{
			if (ImGui::MenuItem("Reset"))
			{
				worldEditor->ResetComponent(entity, selectedComponent);
				ImGui::CloseCurrentPopup();
			}


			// if (entity->GetPrefab() != nullptr && entity->IsComponentOverride(entity->FindComponentByUUID(selectedComponent)))
			// {
			// 	if (ImGui::MenuItem("Remove prefab override"))
			// 	{
			// 		sceneEditor.RemoveComponentOverride(entity, entity->FindComponentByUUID(selectedComponent));
			// 	}
			// }

			if (canRemove && ImGui::MenuItem("Remove"))
			{
				worldEditor->RemoveComponent(entity, selectedComponent);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGuiEndPopupMenu(popupOpenSettings);
	}

	void PropertiesWindow::DrawDebugEntity(u32 id, WorldEditor* worldEditor, Entity* entity)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		if (ImGui::BeginTable("#entity-table", 2))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();

			ImGui::Text("Name");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = entity->GetName();
			u32 hash = HashValue(PtrToInt(entity));

			ImGuiInputTextReadOnly(hash, stringCache);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("UUID");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			if (entity->GetRID())
			{
				static String uuid = Resources::GetUUID(entity->GetRID()).ToString();
				ImGuiInputTextReadOnly(hash + 10, uuid);
			}
			else
			{
				static String uuid = UUID().ToString();
				ImGuiInputTextReadOnly(hash + 10, uuid);
			}

			ImGui::EndTable();
		}

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * style.ScaleFactor);

		{
			Transform* transform = &entity->GetTransform();

			ImGui::PushID(transform);
			ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_AllowItemOverlap))
			{
				ImGui::Indent();

				ImGuiDrawObject(ImGuiDrawObjectInfo{
					.object = transform,
					.userData = this,
				});

				ImGui::Unindent();
			}
			ImGui::PopID();
		}

		for (Component* component: entity->GetComponents())
		{
			ImGui::PushID(component);
			ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
			if (ImGui::CollapsingHeader(FormatName(component->GetType()->GetSimpleName()).CStr(), ImGuiTreeNodeFlags_AllowItemOverlap))
			{
				ImGui::Indent();

				ImGuiDrawObject(ImGuiDrawObjectInfo{
					.object = component,
					.userData = this,
				});

				ImGui::Unindent();
			}
			ImGui::PopID();
		}

	}

	void PropertiesWindow::EntityDebugSelection(u32 workspaceId, Entity* entity)
	{
		if (Editor::GetCurrentWorkspace().GetId() != workspaceId) return;

		if (!entity && !selectedDebugEntity) return;

		ClearSelection();

		selectedDebugEntity = entity;
	}

	void PropertiesWindow::EntityDebugDeselection(u32 workspaceId, Entity* entity)
	{
		if (Editor::GetCurrentWorkspace().GetId() != workspaceId) return;

		if (!entity && !selectedDebugEntity) return;
		if (selectedDebugEntity == entity)
		{
			ClearSelection();
		}
	}

	void PropertiesWindow::DrawAsset(u32 id, RID asset)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		if (ImGui::BeginTable("#asset-table", 2))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();

			ImGui::Text("Name");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = ResourceAssets::GetAssetName(asset);
			u32 hash = HashValue(asset);

			if (ImGuiInputText(hash, stringCache))
			{
				renamingCache = stringCache;
				renamingFocus = true;
			}

			if (!ImGui::IsItemActive() && renamingFocus)
			{
				if (!renamingCache.Empty())
				{
					UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Rename Finished");
					ResourceObject write = Resources::Write(asset);
					write.SetString(ResourceAsset::Name, renamingCache);
					write.Commit(scope);
				}

				renamingFocus = false;
				renamingCache.Clear();
			}

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("UUID");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			stringCache = ResourceAssets::GetAssetUUID(asset).ToString();
			ImGuiInputTextReadOnly(hash + 10, stringCache);
			ImGui::EndTable();
		}
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * style.ScaleFactor);


		ResourceType* type = Resources::GetType(asset);

		if (ImGui::CollapsingHeader(FormatName(type->GetSimpleName()).CStr()), ImGuiTreeNodeFlags_DefaultOpen)
		{
			ImGui::Indent();
			ImGuiDrawResource(ImGuiDrawResourceInfo{
				.rid = asset,
				.scopeName = "Asset Edit"
			});
			ImGui::Unindent();
		}
	}

	void PropertiesWindow::Draw(u32 id, bool& open)
	{
		ImGuiBegin(id, ICON_FA_CIRCLE_INFO " Properties", &open, ImGuiWindowFlags_NoScrollbar);
		if (selectedEntity)
		{
			WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor();
			DrawEntity(id, worldEditor, selectedEntity);
		}
		else if (selectedDebugEntity)
		{
			WorldEditor* worldEditor = Editor::GetCurrentWorkspace().GetWorldEditor();
			DrawDebugEntity(id, worldEditor, selectedDebugEntity);
		}
		else if (selectedAsset)
		{
			DrawAsset(id, selectedAsset);
		}
		else
		{
			ImGuiCentralizedText("Select something...");
		}
		ImGui::End();
	}


	void PropertiesWindow::ClearSelection()
	{
		selectedEntity = {};
		selectedComponent = {};
		selectedDebugEntity = nullptr;
		selectedAsset = {};
	}

	void PropertiesWindow::OpenProperties(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<PropertiesWindow>();
	}

	void PropertiesWindow::EntitySelection(u32 workspaceId, RID entity)
	{
		if (Editor::GetCurrentWorkspace().GetId() != workspaceId) return;

		if (!entity && !selectedEntity) return;

		ClearSelection();
		selectedEntity = entity;
	}

	void PropertiesWindow::EntityDeselection(u32 workspaceId, RID entity)
	{
		if (Editor::GetCurrentWorkspace().GetId() != workspaceId) return;

		if (!entity && !selectedEntity) return;
		if (selectedEntity == entity)
		{
			ClearSelection();
		}
	}

	void PropertiesWindow::AssetSelection(u32 workspaceId, RID assetId)
	{
		if (Editor::GetCurrentWorkspace().GetId() != workspaceId) return;

		ClearSelection();
		selectedAsset = assetId;
	}

	void PropertiesWindow::RegisterType(NativeReflectType<PropertiesWindow>& type)
	{
		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Properties", .action = OpenProperties});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::RightBottom,
			.createOnInit = true
		});
	}
}
