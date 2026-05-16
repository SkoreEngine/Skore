#include "Skore/Scene/LayerSystem.hpp"

#include "Skore/Events.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	static String s_layerNames[MaxLayers];
	static RID    layerSettingsRID = {};

	void ReloadLayerNames()
	{
		for (u32 i = 0; i < MaxLayers; ++i)
		{
			s_layerNames[i].Clear();
		}

		if (ResourceObject settings = Resources::Read(layerSettingsRID))
		{
			u32 layerIndex = 0;
			settings.IterateSubObjectList(LayerSystemSettings::Layers, [&](RID itemRID)
			{
				if (layerIndex < MaxLayers)
				{
					if (ResourceObject item = Resources::Read(itemRID))
					{
						StringView name = item.GetString(LayerSettings::Name);
						if (!name.Empty())
						{
							s_layerNames[layerIndex] = name;
						}
					}
					layerIndex++;
				}
			});
		}

		if (s_layerNames[0].Empty())
		{
			s_layerNames[0] = "Default";
		}
	}

	void OnLayerSettingsResourceChanged(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		ReloadLayerNames();
	}

	void OnLayerSettingsLoaded()
	{
		layerSettingsRID = Settings::Get<ProjectSettings, LayerSystemSettings>();
		if (layerSettingsRID)
		{
			Resources::GetStorage(layerSettingsRID)->RegisterEvent(ResourceEventType::VersionUpdated, OnLayerSettingsResourceChanged, nullptr);
			ReloadLayerNames();
		}
	}

	void LayerSystemInit()
	{
		for (u32 i = 0; i < MaxLayers; ++i)
		{
			s_layerNames[i].Clear();
		}
		s_layerNames[0] = "Default";

		Event::Bind<OnSettingsLoaded, &OnLayerSettingsLoaded>();
	}

	void LayerSystemShutdown()
	{
		Event::Unbind<OnSettingsLoaded, &OnLayerSettingsLoaded>();

		if (layerSettingsRID)
		{
			layerSettingsRID = {};
		}

		for (u32 i = 0; i < MaxLayers; ++i)
		{
			s_layerNames[i].Clear();
		}
	}

	StringView LayerSystem::GetLayerName(u8 layer)
	{
		if (layer >= MaxLayers) return {};
		return s_layerNames[layer];
	}

	void LayerSystem::SetLayerName(u8 layer, StringView name)
	{
		if (layer >= MaxLayers) return;
		if (layer == 0 && name.Empty()) return;
		s_layerNames[layer] = name;
	}

	i32 LayerSystem::FindLayerByName(StringView name)
	{
		for (u32 i = 0; i < MaxLayers; ++i)
		{
			if (s_layerNames[i] == name)
			{
				return static_cast<i32>(i);
			}
		}
		return -1;
	}
}