#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/IO/InputTypes.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/SharedPtr.hpp"
#include "Skore/Core/HashMap.hpp"

namespace Skore
{
    struct MenuItemEventData
    {
        VoidPtr drawData{};
        u64     userData{};
    };


    typedef void (*FnMenuItemAction)(const MenuItemEventData& eventData);
    typedef bool (*FnMenuItemEnable)(const MenuItemEventData& eventData);

    struct MenuItemCreation
    {
        StringView       itemName{};
        StringView       icon{};
        i32              priority{};
        Shortcut         itemShortcut{};
        FnMenuItemAction action{};
        FnMenuItemEnable enable{};
        u64              userData{};
    };

    class SK_API MenuItemContext
    {
    public:
        void AddMenuItem(const MenuItemCreation& menuItem);
        void Draw(VoidPtr userData = nullptr);
        bool ExecuteHotKeys(VoidPtr userData = nullptr, bool executeOnFocus = false);

    private:
        String                                      m_label{};
        String                                      m_itemName{};
        i32                                         m_priority{};
        Array<MenuItemContext*>                     m_children{};
        HashMap<String, SharedPtr<MenuItemContext>> m_menuItemsMap{};
        FnMenuItemAction                            m_action{};
        FnMenuItemEnable                            m_enable{};
        Shortcut                                    m_itemShortcut{};
        u64                                         m_itemUserData{};

        static void DrawMenuItemChildren(MenuItemContext* context, VoidPtr userData);
        static bool ExecuteHotKeys(MenuItemContext* context, VoidPtr userData, bool executeOnFocus);
    };
}
