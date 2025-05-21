
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

#include "MenuItem.hpp"

#include <imgui.h>

#include "Skore/ImGui/ImGui.hpp"

namespace Skore
{

    void MenuItemContext::AddMenuItem(const MenuItemCreation& menuItem)
    {
        Array<String> items = {};
        Split(StringView{menuItem.itemName}, StringView{"/"}, [&](const StringView& item)
        {
            items.EmplaceBack(item);
        });

        if (items.Empty())
        {
            items.EmplaceBack(menuItem.itemName);
        }

        MenuItemContext* parent = nullptr;
        MenuItemContext* storage = this;

        for(const String& item : items)
        {
            auto it = storage->m_menuItemsMap.Find(item);
            if (it == storage->m_menuItemsMap.end())
            {
                it = storage->m_menuItemsMap.Insert(item, MakeRef<MenuItemContext>()).first;
                it->second->m_label = item;
                storage->m_children.EmplaceBack(it->second.Get());
            }
            parent  = storage;
            storage = it->second.Get();
        }

        if (storage && parent)
        {
            storage->m_action = menuItem.action;
            storage->m_enable = menuItem.enable;
            storage->m_visible = menuItem.visible;
            storage->m_itemShortcut = menuItem.itemShortcut;
            storage->m_itemUserData = menuItem.userData;

            if (!menuItem.icon.Empty())
            {
                String label = storage->m_label;
                storage->m_label = menuItem.icon;
                storage->m_label += " ";
                storage->m_label += label;
            }
            storage->m_priority = menuItem.priority;

            Sort(parent->m_children.begin(), parent->m_children.end(), [](MenuItemContext* a, MenuItemContext* b)
            {
                return a->m_priority < b->m_priority;
            });
        }
    }

    void MenuItemContext::DrawMenuItemChildren(MenuItemContext* context, VoidPtr userData)
    {
        bool enabled = true;
        if (context->m_enable)
        {
            enabled = context->m_enable(MenuItemEventData{
                .drawData = userData,
                .userData = context->m_itemUserData
            });
        }

        bool isItem = context->m_children.Empty();
        if (isItem)
        {
            BufferString<64> shortcut{};

            if (context->m_itemShortcut.ctrl)
            {
                shortcut += "Ctrl+";
            }

            if (context->m_itemShortcut.alt)
            {
                shortcut += "Alt+";
            }

            if (context->m_itemShortcut.shift)
            {
                shortcut += "Shift+";
            }

            if (context->m_itemShortcut.presKey != Key::None)
            {
                shortcut += ImGui::GetKeyName(AsImGuiKey(context->m_itemShortcut.presKey));
            }

            if (ImGui::MenuItem(context->m_label.CStr(), shortcut.begin(), false, enabled))
            {
                if (context->m_action)
                {
                    context->m_action(MenuItemEventData{
                        .drawData = userData,
                        .userData = context->m_itemUserData
                    });
                }
            }
        }
        else
        {
            if (ImGui::BeginMenu(context->m_label.CStr(), enabled))
            {
                i32 lastPriority = context->m_children[0]->m_priority;
                for (MenuItemContext* child: context->m_children)
                {
                    if ((lastPriority + 50) < child->m_priority)
                    {
                        ImGui::Separator();
                    }
                    DrawMenuItemChildren(child, userData);
                    lastPriority = child->m_priority;
                }
                ImGui::EndMenu();
            }
        }
    }

    void MenuItemContext::Draw(VoidPtr userData)
    {
        i32 lastPriority = U32_MAX;
        for (MenuItemContext* menuItemStorage: this->m_children)
       {
            if (menuItemStorage->m_visible)
            {
                if (!menuItemStorage->m_visible(MenuItemEventData{
                    .drawData = userData,
                    .userData = menuItemStorage->m_itemUserData
                }))
                {
                    continue;
                }
            }

            if (lastPriority != U32_MAX && lastPriority + 50 < menuItemStorage->m_priority)
            {
                ImGui::Separator();
            }

            DrawMenuItemChildren(menuItemStorage, userData);
            lastPriority = menuItemStorage->m_priority;
        }
    }

    bool MenuItemContext::ExecuteHotKeys(MenuItemContext* context, VoidPtr userData, bool executeOnFocus)
    {
        if (!executeOnFocus && ImGui::GetIO().WantTextInput) return false;

        bool executed = false;

        if (context->m_itemShortcut.presKey != Key::None && context->m_action)
        {
            bool ctrlHolding = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
            bool shiftHolding = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
            bool altHolding = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);

            bool ctrlState = (context->m_itemShortcut.ctrl && ctrlHolding) || (!context->m_itemShortcut.ctrl && !ctrlHolding);
            bool shitState = (context->m_itemShortcut.shift && shiftHolding) || (!context->m_itemShortcut.shift && !shiftHolding);
            bool altState = (context->m_itemShortcut.alt && altHolding) || (!context->m_itemShortcut.alt && !altHolding);


            bool keyPressed = ImGui::IsKeyPressed(AsImGuiKey(context->m_itemShortcut.presKey));
            if (keyPressed && ctrlState && shitState && altState)
            {
                bool canExecuteAction = true;

                if (context->m_enable)
                {
                    if (!context->m_enable(MenuItemEventData{
                        .drawData = userData, .userData = context->m_itemUserData
                    }))
                    {
                        canExecuteAction = false;
                    }
                }

                if (context->m_visible)
                {
                    if (!context->m_visible(MenuItemEventData{
                        .drawData = userData, .userData = context->m_itemUserData
                    }))
                    {
                        canExecuteAction = false;
                    }
                }

                if (canExecuteAction)
                {
                    context->m_action(MenuItemEventData{
                        .drawData = userData,
                        .userData = context->m_itemUserData
                    });
                    executed = true;
                }
            }
        }

        for (MenuItemContext* menuItemStorage: context->m_children)
        {
            if (ExecuteHotKeys(menuItemStorage, userData, executeOnFocus))
            {
                executed = true;
            }
        }

        return executed;
    }

    bool MenuItemContext::ExecuteHotKeys(VoidPtr userData, bool executeOnFocus)
    {
        return ExecuteHotKeys(this, userData, executeOnFocus);
    }
}
