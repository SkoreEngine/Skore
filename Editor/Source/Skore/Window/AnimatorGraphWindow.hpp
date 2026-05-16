#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/ImGui/GraphEditor.hpp"

namespace Skore
{
	class AnimatorEditor;

	class AnimatorGraphWindow : public EditorWindow
	{
	public:
		SK_CLASS(AnimatorGraphWindow, EditorWindow);
		void Draw(u32 id, bool& open) override;

		static void AddMenuItem(const MenuItemCreation& menuItem);
		static void RegisterType(NativeReflectType<AnimatorGraphWindow>& type);

	private:
		GraphEditor m_editor{};

		RID  m_contextState = {};
		Vec2 m_popupMousePos = {};
		bool m_openPopup = false;

		static MenuItemContext menuItemContext;

		// Menu actions
		static void NewEmptyState(const MenuItemEventData& eventData);
		static void NewTransition(const MenuItemEventData& eventData);
		static void SetAsDefaultState(const MenuItemEventData& eventData);
		static void DuplicateState(const MenuItemEventData& eventData);
		static void DeleteState(const MenuItemEventData& eventData);
		static bool HasContextState(const MenuItemEventData& eventData);
		static bool HasNoContextState(const MenuItemEventData& eventData);
	};
}
