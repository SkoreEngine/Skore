#pragma once

#include "Skore/Editor/EditorTypes.hpp"
#include "Skore/Editor/MenuItem.hpp"

namespace Skore
{
    class SettingsItem;

    class SK_API SettingsWindow : public EditorWindow
    {
    public:
        SK_BASE_TYPES(EditorWindow);

        void Init(u32 id, VoidPtr userData) override;
        void Draw(u32 id, bool& open) override;

        static void Open(const MenuItemEventData& eventData);

    private:
        String        title;
        TypeID        type = 0;
        String        searchText;
        SettingsItem* selectedItem = nullptr;

        void DrawTree();
        void DrawSelected();
        void DrawItem(SettingsItem* settingsItem);
    };
}
