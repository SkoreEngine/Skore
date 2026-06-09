#pragma once
#include "imgui.h"
#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Scene/SceneEditor.hpp"

namespace Skore
{
	class EntityTreeWindow : public EditorWindow
	{
	public:
		SK_CLASS(EntityTreeWindow, EditorWindow);


		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;

		static void AddMenuItem(const MenuItemCreation& menuItem);
		static void AddSceneEntity(const MenuItemEventData& eventData);
		static void AddSceneEntityFromAsset(const MenuItemEventData& eventData);
		static void AddComponent(const MenuItemEventData& eventData);
		static void RenameSceneEntity(const MenuItemEventData& eventData);
		static void DuplicateSceneEntity(const MenuItemEventData& eventData);
		static void DeleteSceneEntity(const MenuItemEventData& eventData);
		static bool CheckEntityActions(const MenuItemEventData& eventData);
		static bool CheckSelectedEntity(const MenuItemEventData& eventData);
		static bool CheckReadOnly(const MenuItemEventData& eventData);
		static void ShowSceneEntity(const MenuItemEventData& eventData);
		static bool IsShowSceneEntitySelected(const MenuItemEventData& eventData);

		static bool CheckIsOverride(const MenuItemEventData& eventData);
		static void RemoveOverride(const MenuItemEventData& eventData);
		static bool CheckIsRemoved(const MenuItemEventData& eventData);
		static void AddBackToThisInstance(const MenuItemEventData& eventData);

		static void ShowResourceInspector(const MenuItemEventData& eventData);
		static void OpenEntityTree(const MenuItemEventData& eventData);
		static void RegisterType(NativeReflectType<EntityTreeWindow>& type);

	private:
		String searchEntity{};
		String stringCache{};


		bool ctrlDown = false;
		bool shiftDown = false;

		bool cancelSelection = false;
		bool pushSelection = false;
		RID parentSelection = {};
		RID entitySelection = {};

		//pending runtime selection (Scene*/Entity* mode), committed on mouse release like the editor RID path
		Entity* entityPtrSelection = nullptr;

		RID lastParentSelection = {};
		RID lastEntitySelection = {};
		bool lastSelectionRemoved = false;

		bool   renamingFocus{};
		bool   renamingSelected{};
		String renamingStringCache{};
		f32    iconSize = {};

		bool showSceneEntity = false;
		bool openAssetSelectionPopup = false;

		static MenuItemContext menuItemContext;

		void DrawEntity(SceneEditor* sceneEditor, RID rid, RID parent, bool removed);
		void DrawEntity(SceneEditor* sceneEditor, Entity* entity, bool& entitySelected);
		void DrawMovePayload(u64 id, RID moveBefore) const;

		ImGuiTextFilter filter;
	};
}
