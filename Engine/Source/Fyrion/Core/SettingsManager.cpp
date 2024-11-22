#include "SettingsManager.hpp"

#include "Attributes.hpp"
#include "Registry.hpp"

namespace Fyrion
{
    namespace
    {
        HashMap<TypeID, Array<SettingsItem*>> items;
        HashMap<String, SettingsItem*>        itemsByPath;
    }

    SettingsItem::~SettingsItem() {}

    void SettingsManager::Init(TypeID typeId)
    {
        Array<SettingsItem*>& itemsArr = items.Emplace(typeId, Array<SettingsItem*>()).first->second;

        for (TypeHandler* type : Registry::FindTypesByAttribute<Settings>())
        {
            if (const Settings* settings = type->GetAttribute<Settings>())
            {
                if (settings->type == typeId)
                {
                    Array<String> items = {};
                    Split(StringView{settings->path}, StringView{"/"}, [&](const StringView& item)
                    {
                        items.EmplaceBack(item);
                    });

                    if (items.Empty())
                    {
                        items.EmplaceBack(settings->path);
                    }

                    String path = "";
                    for (int i = 0; i < items.Size(); ++i)
                    {
                        const String& item = items[i];
                        path += "/" + item;
                        auto itByPath = itemsByPath.Find(path);
                        if (itByPath == itemsByPath.end())
                        {
                            SettingsItem* settingsItem = Alloc<SettingsItem>();
                            itemsByPath.Insert(path, settingsItem);
                        }
                    }
                }
            }
        }
        //   items.Emplace(typeId, item);
    }

    Span<SettingsItem*> SettingsManager::GetItems(TypeID typeId)
    {
        if (auto it = items.Find(typeId))
        {
            return it->second;
        }
        return {};
    }
}
