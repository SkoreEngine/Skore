#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Utils/PreviewGenerator.hpp"

namespace Skore
{
	struct DCCAssetPreviewGenerator : PreviewGenerator
	{
		SK_CLASS(DCCAssetPreviewGenerator, PreviewGenerator);

		void SetupScene(Scene* scene) override
		{
			RID dccObject = asset;
			if (ResourceObject object = Resources::Read(asset))
			{
				if (object.GetType()->GetID() == TypeInfo<ResourceAsset>::ID())
				{
					dccObject = object.GetSubObject(ResourceAsset::Object);
				}
			}

			ResourceObject dcc = Resources::Read(dccObject);
			if (!dcc) return;

			RID rootEntity = dcc.GetSubObject(DCCAsset::RootEntity);
			if (!rootEntity) return;

			scene->CreateEntityFromRID(rootEntity);
		}
	};

	struct DCCAssetHandler : ResourceAssetHandler
	{
		SK_CLASS(DCCAssetHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".dcc_asset";
		}

		void OpenAsset(RID asset) override
		{
			if (ResourceObject assetObject = Resources::Read(asset))
			{
				if (RID object = assetObject.GetSubObject(ResourceAsset::Object))
				{
					Editor::GetActiveWorkspace()->OpenAsset(object);
				}
			}
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<DCCAsset>::ID();
		}

		StringView GetDesc() override
		{
			return "DCC Asset";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_CUBES;
		}

		TypeID GetPreviewGenerator() override
		{
			return TypeInfo<DCCAssetPreviewGenerator>::ID();
		}

	};

	void UnregisterDCCSubObjects(ResourceObject& object, u32 field)
	{
		object.IterateSubObjectList(field, [](RID rid)
		{
			ResourceAssets::UnregisterAssetByType(rid);
		});
	}

	void RegisterDCCSubObjects(ResourceObject& object, u32 field)
	{
		object.IterateSubObjectList(field, [](RID rid)
		{
			ResourceAssets::RegisterAssetByType(rid);
		});
	}

	void OnDCCAssetChanged(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		if (oldValue)
		{
			UnregisterDCCSubObjects(oldValue, DCCAsset::Meshes);
			UnregisterDCCSubObjects(oldValue, DCCAsset::Textures);
			UnregisterDCCSubObjects(oldValue, DCCAsset::Materials);
			UnregisterDCCSubObjects(oldValue, DCCAsset::AnimationClips);

			if (RID rootEntity = oldValue.GetSubObject(DCCAsset::RootEntity))
			{
				ResourceAssets::UnregisterAssetByType(rootEntity);
			}
		}

		if (newValue)
		{
			RegisterDCCSubObjects(newValue, DCCAsset::Meshes);
			RegisterDCCSubObjects(newValue, DCCAsset::Textures);
			RegisterDCCSubObjects(newValue, DCCAsset::Materials);
			RegisterDCCSubObjects(newValue, DCCAsset::AnimationClips);

			if (RID rootEntity = newValue.GetSubObject(DCCAsset::RootEntity))
			{
				ResourceAssets::RegisterAssetByType(rootEntity);
			}
		}
	}

	void RegisterDCCAssetHandler()
	{
		Reflection::Type<DCCAssetPreviewGenerator>();
		Reflection::Type<DCCAssetHandler>();

		if (ResourceType* dccType = Resources::FindType<DCCAsset>())
		{
			dccType->RegisterEvent(ResourceEventType::Changed, OnDCCAssetChanged, nullptr);
		}
	}
}
