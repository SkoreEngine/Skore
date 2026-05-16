#include "Skore/Core/Settings.hpp"

#include <mutex>

#include "Skore/Core/Event.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Events.hpp"
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

		EventHandler<OnSettingsLoaded> onSettingsLoaded{};
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
					if (!Resources::HasValue(settingItem))
					{
						Resources::Write(settingItem).Commit();
					}
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
				if (!Resources::HasValue(newItem))
				{
					//create default empty object
					Resources::Write(newItem).Commit();
				}

				ResourceObject settingsObject = Resources::Write(itTypes->second.rid);
				settingsObject.AddToSubObjectList(SettingTypeResource::Settings, newItem);
				settingsObject.Commit();

				itInstances = settingTypeStorage.instances.Insert(typeId, newItem).first;
			}
			return itInstances->second;
		}
		return {};
	}

	RID Settings::Load(ArchiveReader& reader, TypeID settingsType)
	{
		RID result = {};
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

					result = storage.rid;
					settingTypes.Emplace(settingsType, Traits::Move(storage));
				}
			}
		}

		if (result)
		{
			onSettingsLoaded.Invoke();
		}

		return result;
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