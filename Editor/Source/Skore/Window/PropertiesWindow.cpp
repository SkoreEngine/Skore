#include "PropertiesWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
// #include "Skore/Asset/AssetEditor.hpp"
// #include "Skore/Asset/AssetFile.hpp"
// #include "Skore/Commands/SceneCommands.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Scene/Component.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	PropertiesWindow::PropertiesWindow()
	{
		Event::Bind<OnEntitySelection, &PropertiesWindow::EntitySelection>(this);
		Event::Bind<OnEntityDeselection, &PropertiesWindow::EntityDeselection>(this);
		Event::Bind<OnAssetSelection, &PropertiesWindow::AssetSelection>(this);
	}

	PropertiesWindow::~PropertiesWindow()
	{
		Event::Unbind<OnEntitySelection, &PropertiesWindow::EntitySelection>(this);
		Event::Unbind<OnEntityDeselection, &PropertiesWindow::EntityDeselection>(this);
		Event::Unbind<OnAssetSelection, &PropertiesWindow::AssetSelection>(this);
	}

	void PropertiesWindow::DrawSceneEntity(u32 id, SceneEditor* sceneEditor, Entity* entity)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		bool readOnly = sceneEditor->IsReadOnly();

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

			stringCache = entity->GetName();

			u32 hash = HashValue(reinterpret_cast<usize>(&entity));

			if (ImGuiInputText(hash, stringCache, nameFlags))
			{
				renamingCache = stringCache;
				renamingFocus = true;
				renamingEntity = entity;
			}

			if (!ImGui::IsItemActive() && renamingFocus)
			{
				sceneEditor->Rename(renamingEntity, renamingCache);
				renamingEntity = {};
				renamingFocus = false;
				renamingCache.Clear();
			}

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("UUID");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			String uuid = entity->GetUUID().ToString();
			ImGuiInputText(hash + 10, uuid, ImGuiInputTextFlags_ReadOnly);
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

		// if (entity->GetPrefab() != nullptr)
		// {
		// 	ImGui::BeginHorizontal(9999, ImVec2(width, size));
		// 	ImGui::Spring(1.f);
		//
		// 	if (ImGui::BorderedButton("Open Prefab", ImVec2((width * 2) / 3, size)))
		// 	{
		// 		//TODO defer open scene.
		// 	}
		//
		// 	ImGui::Spring(1.f);
		// 	ImGui::EndHorizontal();
		// }

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * style.ScaleFactor);

		bool openComponentSettings = false;

		this->currentEntity = entity;

		for (ReflectField* field : entity->GetType()->GetFields())
		{
			this->currentField = field;
			if (const Object* object = field->GetObject(entity))
			{
				if (ImGui::CollapsingHeader(FormatName(field->GetName()).CStr(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::BeginDisabled(readOnly);
					ImGuiDrawObject(ImGuiDrawObjectInfo{
						.object = const_cast<Object*>(object),
						.userData = this,
						.callback = [](const ImGuiDrawFieldContext& context, VoidPtr pointer, usize size)
						{
							PropertiesWindow* window = static_cast<PropertiesWindow*>(context.userData);
							window->currentField->Set(window->currentEntity, context.object, U32_MAX);
							Editor::GetCurrentWorkspace().GetSceneEditor()->MarkDirty();
						}
					});
					ImGui::EndDisabled();
				}
			}
		}

		for (Component* component : entity->GetAllComponents())
		{
			bool propClicked = false;
			bool open = ImGuiCollapsingHeaderProps(HashValue(reinterpret_cast<usize>(component)), FormatName(component->GetType()->GetSimpleName()).CStr(), &propClicked);
			if (propClicked)
			{
				openComponentSettings = true;
				selectedComponent = component;
			}

			if (open)
			{
				ImGui::BeginDisabled(readOnly);
				ImGui::Indent();

				ImGuiDrawObject(ImGuiDrawObjectInfo{
					.object = component,
					.userData = this,
					.callback = [](const ImGuiDrawFieldContext& context, VoidPtr pointer, usize size)
					{
						//context.reflectField

						Editor::GetCurrentWorkspace().GetSceneEditor()->MarkDirty();
						//
						// Ref<Transaction> transaction = UndoRedoSystem::BeginTransaction("ComponentUpdate");
						// UpdateComponentCommand* command = Alloc<UpdateComponentCommand>();
						// transaction->AddCommand(command);
						//
						// UndoRedoSystem::EndTransaction(transaction);
					}
				});
				ImGui::Unindent();
				ImGui::EndDisabled();
			}
		}

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
						sceneEditor->AddComponent(entity, componentId);
					}
				}
			}
		}
		ImGuiEndPopupMenu(popupRes);

		if (openComponentSettings)
		{
			ImGui::OpenPopup("open-component-settings");
		}

		bool popupOpenSettings = ImGuiBeginPopupMenu("open-component-settings", 0, false);
		if (popupOpenSettings && selectedComponent)
		{
			if (ImGui::MenuItem("Reset"))
			{
				sceneEditor->ResetComponent(entity, selectedComponent);
				ImGui::CloseCurrentPopup();
			}


			// if (entity->GetPrefab() != nullptr && entity->IsComponentOverride(entity->FindComponentByUUID(selectedComponent)))
			// {
			// 	if (ImGui::MenuItem("Remove prefab override"))
			// 	{
			// 		sceneEditor.RemoveComponentOverride(entity, entity->FindComponentByUUID(selectedComponent));
			// 	}
			// }

			if (ImGui::MenuItem("Remove"))
			{
				sceneEditor->RemoveComponent(entity, selectedComponent);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGuiEndPopupMenu(popupOpenSettings);
	}

	void PropertiesWindow::DrawAsset(u32 id, AssetFileOld* assetFile)
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

			stringCache = assetFile->GetFileName();
			u32 hash = HashValue(PtrToInt(assetFile));

			if (ImGuiInputText(hash, stringCache))
			{
				renamingCache = stringCache;
				renamingFocus = true;
			}

			if (!ImGui::IsItemActive() && renamingFocus)
			{
				assetFile->Rename(renamingCache);
				renamingFocus = false;
				renamingCache.Clear();
			}

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("UUID");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);

			String uuid = assetFile->GetUUID().ToString();
			ImGuiInputTextReadOnly(hash + 10, uuid);
			ImGui::EndTable();
		}
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * style.ScaleFactor);

		ReflectType* reflectType = Reflection::FindTypeById(assetFile->GetAssetTypeId());

		if (ImGui::CollapsingHeader(FormatName(reflectType->GetSimpleName()).CStr()), ImGuiTreeNodeFlags_DefaultOpen)
		{
			ImGui::Indent();
			ImGuiDrawObject(ImGuiDrawObjectInfo{
				.object = assetFile->GetInstance(),
				.userData = assetFile,
				.callback = [](const ImGuiDrawFieldContext& context, VoidPtr pointer, usize size)
				{
					static_cast<AssetFileOld*>(context.userData)->MarkDirty();
				}
			});
			ImGui::Unindent();
		}
	}

	void PropertiesWindow::Draw(u32 id, bool& open)
	{
		ImGuiBegin(id, ICON_FA_CIRCLE_INFO " Properties", &open, ImGuiWindowFlags_NoScrollbar);
		if (selectedEntity)
		{
			SceneEditor* sceneEditor = Editor::GetCurrentWorkspace().GetSceneEditor();
			if (Entity* entity = sceneEditor->GetCurrentScene()->FindEntityByUUID(selectedEntity))
			{
				DrawSceneEntity(id, sceneEditor, entity);
			}
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
	}

	void PropertiesWindow::OpenProperties(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow<PropertiesWindow>();
	}

	void PropertiesWindow::EntitySelection(u32 workspaceId, UUID entityId)
	{
		if (Editor::GetCurrentWorkspace().GetId() != workspaceId) return;


		if (!entityId && !selectedEntity) return;

		ClearSelection();
		selectedEntity = entityId;
	}

	void PropertiesWindow::EntityDeselection(u32 workspaceId, UUID entityId)
	{
		if (Editor::GetCurrentWorkspace().GetId() != workspaceId) return;

		if (!entityId && !selectedEntity) return;
		if (selectedEntity == entityId)
		{
			ClearSelection();
		}
	}

	void PropertiesWindow::AssetSelection(AssetFileOld* assetFile)
	{
		if (assetFile == nullptr && selectedAsset == nullptr) return;
		ClearSelection();
		selectedAsset = assetFile;
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
