#pragma once

#include "Fyrion/Editor/EditorTypes.hpp"
#include "Fyrion/Editor/MenuItem.hpp"

namespace Fyrion
{
    class FY_API SettingsWindow : public EditorWindow
    {
    public:
        FY_BASE_TYPES(EditorWindow);

        void Init(u32 id, VoidPtr userData) override;
        void Draw(u32 id, bool& open) override;

        static void Open(const MenuItemEventData& eventData);
    private:
        String title;
        TypeID type = 0;
        String searchText;

        void DrawTree();
        void DrawSelected();
    };
}
