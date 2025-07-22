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

#include "Reflection.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		struct SettingTypeStorage
		{
			RID                  rid;
			HashMap<TypeID, RID> instances;
		};


		std::mutex settingTypesMutex;

		HashMap<TypeID, SettingTypeStorage> settingTypes;
	}

	void Settings::CreateDefault(ArchiveWriter& writer, TypeID settingsType)
	{
		RID settings = Resources::Create<SettingTypeResource>(UUID::RandomUUID());
		ResourceObject settingsObject = Resources::Write(settings);

		for (TypeID typeId : Resources::FindTypesByAttribute<EditableSettings>())
		{
			ResourceType* resourceType = Resources::FindTypeByID(typeId);
			if (const EditableSettings* editableSettings = resourceType->GetAttribute<EditableSettings>())
			{
				if (editableSettings->type == settingsType)
				{
					RID settingItem = Resources::Create(resourceType->GetID(), UUID::RandomUUID());
					//create default empty object
					Resources::Write(settingItem).Commit();
					settingsObject.AddToSubObjectList(SettingTypeResource::Settings, settingItem);
				}
			}
		}

		settingsObject.Commit();
		Resources::Serialize(settings, writer);
		Resources::Destroy(settings);
	}

	RID Settings::Get(TypeID settingsType, TypeID typeId)
	{
		std::unique_lock lock(settingTypesMutex);
		if (auto itTypes = settingTypes.Find(settingsType))
		{
			SettingTypeStorage& settingTypeStorage = itTypes->second;
			auto itInstances = settingTypeStorage.instances.Find(typeId);
			if (itInstances == settingTypeStorage.instances.end())
			{
				RID newItem = Resources::Create(typeId, UUID::RandomUUID());
				//create default empty object
				Resources::Write(newItem).Commit();
				itInstances = settingTypeStorage.instances.Insert(typeId, newItem).first;
			}
			return itInstances->second;
		}
		return {};
	}

	RID Settings::Load(ArchiveReader& reader, TypeID settingsType)
	{
		std::unique_lock lock(settingTypesMutex);

		auto itInstances = settingTypes.Find(settingsType);
		if (itInstances == settingTypes.end())
		{
			SettingTypeStorage storage;
			storage.rid = Resources::Deserialize(reader);
			if (storage.rid)
			{
				if (ResourceObject settingResources = Resources::Read(storage.rid))
				{
					settingResources.IterateSubObjectList(SettingTypeResource::Settings, [&](RID rid)
					{
						storage.instances.Insert(Resources::GetType(rid)->GetID(), rid);
					});
				}

				settingTypes.Emplace(settingsType, Traits::Move(storage));
				return storage.rid;
			}
		}
		return {};
	}


	void Settings::Save(ArchiveWriter& writer, TypeID settingsType)
	{
		std::unique_lock lock(settingTypesMutex);
		if (auto it = settingTypes.Find(settingsType))
		{
			Resources::Serialize(it->second.rid, writer);
		}
	}

	void RegisterSettingsType()
	{
		Reflection::Type<EditableSettings>();
		Reflection::Type<ProjectSettings>();

		Resources::Type<SettingTypeResource>()
			.Field<SettingTypeResource::Settings>(ResourceFieldType::SubObjectList)
			.Build();
	}

	void SettingsShutdown()
	{
		settingTypes.Clear();
	}
}
