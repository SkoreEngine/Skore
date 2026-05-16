#pragma once

#include <memory>

#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/IO/InputTypes.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"

namespace Skore
{
	struct MenuItemEventData
	{
		VoidPtr drawData{};
		u64     userData{};
	};


	typedef void (*FnMenuItemAction)(const MenuItemEventData& eventData);
	typedef bool (*FnMenuItemCheck)(const MenuItemEventData& eventData);

	struct MenuItemCreation
	{
		StringView       itemName{};
		StringView       icon{};
		i32              priority{};
		Shortcut         itemShortcut{};
		FnMenuItemAction action{};
		FnMenuItemCheck  enable{};
		FnMenuItemCheck  visible{};
		FnMenuItemCheck  selected{};
		u64              userData{};
		bool             debugOption = false;
	};

	class MenuItemContext
	{
	public:
		void AddMenuItem(const MenuItemCreation& menuItem);
		void Draw(VoidPtr userData = nullptr);
		bool ExecuteHotKeys(VoidPtr userData = nullptr, bool executeOnFocus = false);

		bool CanShow(VoidPtr userData) const;

	private:
		String                                            m_label{};
		String                                            m_itemName{};
		i32                                               m_priority{};
		Array<MenuItemContext*>                           m_children{};
		HashMap<String, std::shared_ptr<MenuItemContext>> m_menuItemsMap{};
		FnMenuItemAction                                  m_action{};
		FnMenuItemCheck                                   m_enable{};
		FnMenuItemCheck                                   m_visible{};
		FnMenuItemCheck                                   m_selected{};
		Shortcut                                          m_itemShortcut{};
		u64                                               m_itemUserData{};
		bool                                              m_debugOption = false;

		static void DrawMenuItemChildren(MenuItemContext* context, VoidPtr userData);
		static bool ExecuteHotKeys(MenuItemContext* context, VoidPtr userData, bool executeOnFocus);
	};
}
