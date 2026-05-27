#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	class AnimatorEditor;

	class AnimatorTreeViewWindow : public EditorWindow
	{
	public:
		SK_CLASS(AnimatorTreeViewWindow, EditorWindow);
		~AnimatorTreeViewWindow();
		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;

		static void AddMenuItem(const MenuItemCreation& menuItem);
		static void RegisterType(NativeReflectType<AnimatorTreeViewWindow>& type);

	private:
		void DrawLayers(AnimatorEditor& editor, RID controller);
		void DrawParameters(AnimatorEditor& editor, RID controller);
		void DrawAvatars(AnimatorEditor& editor, RID controller);

		void SelectItem(RID item, UndoRedoScope* scope = nullptr);
		RID  GetSelectedItem() const;
		bool IsSelected(RID item) const;
		RID  GetActionTarget() const;
		bool IsItemLayer(RID item) const;
		bool IsItemParameter(RID item) const;
		bool IsItemAvatar(RID item) const;

		RID    m_windowState = {};

		bool   m_renamingItem = false;
		bool   m_renamingFocus = false;
		RID    m_renamingItemRID = {};
		String m_renamingCache{};

		RID    m_contextItem = {};
		bool   m_openPopup = false;
		bool   m_newSelection = false;

		// Force open tree nodes when items are created
		bool   m_forceOpenLayers = false;
		bool   m_forceOpenAvatars = false;

		// Track which section the context menu was opened from
		enum class ContextSection { Layer, Parameter, Avatar };
		ContextSection m_contextSection = ContextSection::Layer;

		static MenuItemContext menuItemContext;

		// Menu actions
		static void NewLayer(const MenuItemEventData& eventData);
		static void NewParameter(const MenuItemEventData& eventData);
		static void NewAvatar(const MenuItemEventData& eventData);
		static void RenameItem(const MenuItemEventData& eventData);
		static void DeleteItem(const MenuItemEventData& eventData);
		static bool HasContextItem(const MenuItemEventData& eventData);
		static bool IsLayerContext(const MenuItemEventData& eventData);
		static bool IsParamContext(const MenuItemEventData& eventData);
		static bool IsAvatarContext(const MenuItemEventData& eventData);
		static bool IsNoContext(const MenuItemEventData& eventData);

		static void OnTreeSelectionChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
	};
}
