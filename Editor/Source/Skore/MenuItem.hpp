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
	};

	class MenuItemContext
	{
	public:
		void AddMenuItem(const MenuItemCreation& menuItem);
		void Draw(VoidPtr userData = nullptr);
		bool ExecuteHotKeys(VoidPtr userData = nullptr, bool executeOnFocus = false);

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

		static void DrawMenuItemChildren(MenuItemContext* context, VoidPtr userData);
		static bool ExecuteHotKeys(MenuItemContext* context, VoidPtr userData, bool executeOnFocus);
	};
}
