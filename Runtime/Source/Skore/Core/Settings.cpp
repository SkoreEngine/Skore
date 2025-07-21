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

#include "Settings.hpp"

#include <mutex>

#include "Span.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	struct SettingTypeResource
	{
		enum
		{
			Settings
		};
	};


	namespace
	{
		struct SettingTypeStorage
		{
			RID                  rid;
			HashMap<TypeID, RID> instances;
		};

		HashMap<TypeID, Array<SettingsItem*>> items;
		HashMap<String, SettingsItem*>        itemsByPath;

		std::mutex                          settingTypesMutex;
		HashMap<TypeID, SettingTypeStorage> settingTypes;
	}

	SettingsItem::SettingsItem(TypeID settingType) : settingType(settingType) {}

	SettingsItem::~SettingsItem() {}

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

	void SettingsItem::SetType(ResourceType* type)
	{
		this->type = type;
	}

	RID SettingsItem::GetRID() const
	{
		return rid;
	}

	ResourceType* SettingsItem::GetType() const
	{
		return type;
	}

	Span<SettingsItem*> SettingsItem::GetChildren() const
	{
		return children;
	}

	void SettingsItem::Instantiate()
	{
		if (type)
		{
			rid = Settings::GetSetting(settingType, type->GetID());
		}
	}

	void Settings::Register(TypeID settingType, StringView label, TypeID group)
	{

	}

	void Settings::Init(TypeID settingsId)
	{
		Array<SettingsItem*>& itemsArr = items.Emplace(settingsId, Array<SettingsItem*>()).first->second;

		// for (ResourceType* type : ResourceTypes::FindByAttribute<EditableSettings>())
		// {
		// 	if (const EditableSettings* editableSettings = type->GetAttribute<EditableSettings>())
		// 	{
		// 		if (editableSettings->type == settingsId)
		// 		{
		// 			Array<String> items = {};
		// 			Split(StringView{editableSettings->path}, StringView{"/"}, [&](const StringView& item)
		// 			{
		// 				items.EmplaceBack(item);
		// 			});
		//
		// 			if (items.Empty())
		// 			{
		// 				items.EmplaceBack(editableSettings->path);
		// 			}
		//
		// 			String        path = "";
		// 			SettingsItem* lastItem = nullptr;
		// 			for (int i = 0; i < items.Size(); ++i)
		// 			{
		// 				const String& itemLabel = items[i];
		// 				path += "/" + itemLabel;
		// 				auto itByPath = itemsByPath.Find(path);
		// 				if (itByPath == itemsByPath.end())
		// 				{
		// 					SettingsItem* newItem = Alloc<SettingsItem>(settingsId);
		// 					newItem->SetLabel(itemLabel);
		// 					itByPath = itemsByPath.Insert(path, newItem).first;
		//
		// 					if (lastItem != nullptr)
		// 					{
		// 						lastItem->AddChild(newItem);
		// 					}
		// 					else
		// 					{
		// 						itemsArr.EmplaceBack(newItem);
		// 					}
		// 				}
		// 				lastItem = itByPath->second;
		// 			}
		//
		// 			if (lastItem != nullptr)
		// 			{
		// 				lastItem->SetType(type);
		// 				lastItem->Instantiate();
		// 			}
		// 		}
		// 	}
		// }
	}

	Span<SettingsItem*> Settings::GetItems(TypeID typeId)
	{
		if (auto it = items.Find(typeId))
		{
			return it->second;
		}
		return {};
	}

	RID Settings::GetSetting(TypeID settingType, TypeID typeId)
	{
		std::unique_lock lock(settingTypesMutex);

		auto itInstances = settingTypes.Find(settingType);
		if (itInstances == settingTypes.end())
		{
			RID rid = Resources::Create<SettingTypeResource>(UUID::RandomUUID());
			itInstances = settingTypes.Emplace(settingType, SettingTypeStorage{rid}).first;
		}
		SettingTypeStorage& settingTypeStorage = itInstances->second;

		auto it = settingTypeStorage.instances.Find(typeId);
		if (it == settingTypeStorage.instances.end())
		{
			RID rid = Resources::Create(typeId, UUID::RandomUUID());
			Resources::Write(rid).Commit();

			ResourceObject settingResources = Resources::Write(settingTypeStorage.rid);
			settingResources.AddToSubObjectList(SettingTypeResource::Settings, rid);
			settingResources.Commit();

			it = settingTypeStorage.instances.Insert(typeId, rid).first;
		}

		return it->second;
	}

	RID Settings::Load(StringView settingsFile, TypeID settingType)
	{
		auto itInstances = settingTypes.Find(settingType);
		if (itInstances == settingTypes.end())
		{
			SettingTypeStorage storage;

			if (String file = FileSystem::ReadFileAsString(settingsFile); !file.Empty())
			{
			//	storage.rid = Serialization::YamlDeserialize(file);
			}
			else
			{
				storage.rid = Resources::Create<SettingTypeResource>(UUID::RandomUUID());
			}

			if (ResourceObject settingResources = Resources::Read(storage.rid))
			{
				settingResources.IterateSubObjectList(SettingTypeResource::Settings, [&](RID rid)
				{
					//storage.instances.Insert(Resources::GetResourceType(rid)->type, rid);
				});

				settingTypes.Emplace(settingType, Traits::Move(storage));
			}
			return storage.rid;
		}
		SK_ASSERT(false, "settings already loaded");
		return {};
	}

	void Settings::Save(StringView settingsFile, TypeID settingType)
	{
		std::unique_lock lock(settingTypesMutex);
		if (auto it = settingTypes.Find(settingType))
		{
			//FileSystem::SaveFileAsString(settingsFile, Serialization::YamlSerialize(it->second.rid));
		}
	}

	void RegisterSettingsType()
	{
		Resources::Type<SettingTypeResource>()
			.Field<SettingTypeResource::Settings>(ResourceFieldType::SubObjectList)
			.Build();
	}

	void SettingsShutdown()
	{
		items.Clear();
		itemsByPath.Clear();

		settingTypes.Clear();
	}
}
