#pragma once
#include "Array.hpp"
#include "Fyrion/Common.hpp"


namespace Fyrion
{
    class FY_API SettingsItem
    {
    public:
        ~SettingsItem();

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
