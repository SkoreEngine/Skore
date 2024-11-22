#include "SettingsManager.hpp"

#include "Attributes.hpp"
#include "Registry.hpp"
#include "StringUtils.hpp"

namespace Fyrion
{
    namespace
    {
        HashMap<TypeID, Array<SettingsItem*>> items;
        HashMap<String, SettingsItem*>        itemsByPath;
    }

    SettingsItem::~SettingsItem()
    {

    }

    void SettingsItem::SetLabel(StringView label)
    {
        this->label = label;
    }

    StringView SettingsItem::GetLabel() const
    {
        return this->label;
    }

    void SettingsItem::AddChild(SettingsItem* child)
    {
        children.EmplaceBack(child);
    }

    void SettingsItem::SetTypeHandler(TypeHandler* typeHandler)
    {
        this->typeHandler = typeHandler;
    }

    TypeHandler* SettingsItem::GetTypeHandler() const
    {
        return typeHandler;
    }

    VoidPtr SettingsItem::GetInstance() const
    {
        return instance;
    }

    Span<SettingsItem*> SettingsItem::GetChildren() const
    {
        return children;
    }

    void SettingsItem::Instantiate()
    {
        if (typeHandler != nullptr)
        {
            instance = typeHandler->NewInstance();
        }
    }

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
                    SettingsItem* lastItem = nullptr;
                    for (int i = 0; i < items.Size(); ++i)
                    {
                        const String& itemLabel = items[i];
                        path += "/" + itemLabel;
                        auto itByPath = itemsByPath.Find(path);
                        if (itByPath == itemsByPath.end())
                        {
                            SettingsItem* newItem = Alloc<SettingsItem>();
                            newItem->SetLabel(itemLabel);
                            itByPath = itemsByPath.Insert(path, newItem).first;

                            if (lastItem != nullptr)
                            {
                                lastItem->AddChild(newItem);
                            }
                            else
                            {
                                itemsArr.EmplaceBack(newItem);
                            }
                        }
                        lastItem = itByPath->second;
                    }

                    if (lastItem != nullptr)
                    {
                        lastItem->SetTypeHandler(type);
                        lastItem->Instantiate();
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
