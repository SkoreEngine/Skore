#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	class SceneEditor;
	class ReflectField;
	struct MenuItemEventData;
	class Entity;

	class PropertiesWindow : public EditorWindow
	{
	public:
		SK_CLASS(PropertiesWindow, EditorWindow);

		PropertiesWindow();
		~PropertiesWindow() override;
		void Draw(u32 id, bool& open) override;

		static void RegisterType(NativeReflectType<PropertiesWindow>& type);

	private:
		String stringCache{};
		RID    selectedEntity{};
		bool   renamingFocus{};
		String renamingCache{};
		RID    renamingEntity{};
		String searchComponentString{};
		RID    selectedComponent = {};
		u32    selectedComponentIndex = U32_MAX;
		RID    selectedAsset = {};
		RID    selectedResource = {};

		Entity* selectedDebugEntity = nullptr;

		void ClearSelection();

		static void OpenProperties(const MenuItemEventData& eventData);

		void DrawEntity(u32 id, SceneEditor* sceneEditor, RID entity);
		void DrawDebugEntity(u32 id, SceneEditor* sceneEditor, Entity* entity);
		void DrawAsset(u32 id, RID asset);
		void DrawResource(u32 id, RID resource);

		void EntityDebugSelection(u32 workspaceId, Entity* entity);
		void EntityDebugDeselection(u32 workspaceId, Entity* entity);

		void EntitySelection(u32 workspaceId, RID entityId);
		void EntityDeselection(u32 workspaceId, RID entityId);

		void AssetSelection(u32 workspaceId, RID assetId);
		void ResourceSelection(u32 workspaceId, RID resourceId);
	};
}
