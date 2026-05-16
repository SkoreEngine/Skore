#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Utils/ThumbnailGenerator.hpp"

namespace Skore
{
	struct DCCAssetThumbnailGenerator : ThumbnailGenerator
	{
		RID entityRID;

		explicit DCCAssetThumbnailGenerator(const RID& entity)
			: entityRID(entity) {}

		void SetupScene(Scene* scene) override
		{
			scene->CreateEntityFromRID(entityRID);
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

		static void GenerateThumbnail(RID asset)
		{
			if (ResourceObject object = Resources::Read(asset))
			{
				RID dccObject = object.GetSubObject(ResourceAsset::Object);
				if (!dccObject) return;

				ResourceObject dcc = Resources::Read(dccObject);
				if (!dcc) return;

				RID rootEntity = dcc.GetSubObject(DCCAsset::RootEntity);
				if (!rootEntity) return;

				DCCAssetThumbnailGenerator generator{rootEntity};
				generator.GenerateThumbnail(asset);
			}
		}

		FnThumbnailGenerator GetThumbnailGenerator(RID rid) const override
		{
			return GenerateThumbnail;
		}

		bool CanExtractAsset(RID rid) override
		{
			return true;
		}

		void ExtractAsset(RID directory, RID object, UndoRedoScope* scope) override;
	};

	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::DCCAssetHandler");
	}

	void ExtractSubObjectList(RID directory, ResourceObject& dccRead, ResourceObject& dccWrite, u32 field, TypeID typeId, UndoRedoScope* scope)
	{
		Array<RID> subObjects;
		dccRead.IterateSubObjectList(field, [&](RID rid)
		{
			subObjects.EmplaceBack(rid);
		});

		for (RID subObject : subObjects)
		{
			ResourceObject subRead = Resources::Read(subObject);
			if (!subRead) continue;

			// Get the name from the resource's Name field (field 0 is Name for Mesh/Texture/Material/AnimationClip)
			StringView nameView = subRead.GetString(0);
			String name = !nameView.Empty() ? String(nameView) : String("Unnamed");

			dccWrite.RemoveFromSubObjectList(field, subObject);
			ResourceAssets::CreateExtractedAsset(directory, typeId, name, subObject, scope);
		}
	}

	void DCCAssetHandler::ExtractAsset(RID directory, RID object, UndoRedoScope* scope)
	{
		ResourceObject dccRead = Resources::Read(object);
		if (!dccRead) return;

		ResourceObject dccWrite = Resources::Write(object);

		ExtractSubObjectList(directory, dccRead, dccWrite, DCCAsset::Meshes, TypeInfo<MeshResource>::ID(), scope);
		ExtractSubObjectList(directory, dccRead, dccWrite, DCCAsset::Textures, TypeInfo<TextureResource>::ID(), scope);
		ExtractSubObjectList(directory, dccRead, dccWrite, DCCAsset::Materials, TypeInfo<MaterialResource>::ID(), scope);
		ExtractSubObjectList(directory, dccRead, dccWrite, DCCAsset::AnimationClips, TypeInfo<AnimationClipResource>::ID(), scope);

		// Extract root entity
		if (RID rootEntity = dccRead.GetSubObject(DCCAsset::RootEntity))
		{
			ResourceObject entityRead = Resources::Read(rootEntity);
			StringView nameView = entityRead ? entityRead.GetString(EntityResource::Name) : "";
			String name = !nameView.Empty() ? String(nameView) : String("Entity");

			dccWrite.SetSubObject(DCCAsset::RootEntity, {});
			ResourceAssets::CreateExtractedAsset(directory, TypeInfo<EntityResource>::ID(), name, rootEntity, scope);
		}

		dccWrite.Commit(scope);

		logger.Info("DCC asset extracted successfully");
	}

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
		Reflection::Type<DCCAssetHandler>();

		if (ResourceType* dccType = Resources::FindType<DCCAsset>())
		{
			dccType->RegisterEvent(ResourceEventType::Changed, OnDCCAssetChanged, nullptr);
		}
	}
}
