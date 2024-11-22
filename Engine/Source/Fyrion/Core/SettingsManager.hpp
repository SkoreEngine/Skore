#pragma once
#include "Array.hpp"
#include "Fyrion/Common.hpp"


namespace Fyrion
{
    class FY_API SettingsItem
    {
    public:
        ~SettingsItem();

        void                SetLabel(StringView label);
        StringView          GetLabel() const;
        void                AddChild(SettingsItem* child);
        void                SetTypeHandler(TypeHandler* typeHandler);
        TypeHandler*        GetTypeHandler() const;
        VoidPtr             GetInstance() const;
        Span<SettingsItem*> GetChildren() const;

        void Instantiate();

    private:
        String               label;
        VoidPtr              instance{};
        TypeHandler*         typeHandler{};
        Array<SettingsItem*> children;
    };


    class FY_API SettingsManager
    {
    public:
        static void                Init(TypeID typeId);
        static Span<SettingsItem*> GetItems(TypeID typeId);
    };
}
