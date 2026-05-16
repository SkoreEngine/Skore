#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Utils/ThumbnailGenerator.hpp"

namespace Skore
{
	struct EntityThumbnailGenerator : ThumbnailGenerator
	{
		RID entityRID;

		explicit EntityThumbnailGenerator(const RID& entity)
			: entityRID(entity) {}

		void SetupScene(Scene* scene) override
		{
			scene->CreateEntityFromRID(entityRID);
		}
	};

	struct EntityHandler : ResourceAssetHandler
	{
		SK_CLASS(EntityHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".entity";
		}

		void OpenAsset(RID asset) override
		{
			if (ResourceObject object = Resources::Read(asset))
			{
				Editor::GetWorkspaceOfType(WorkspaceTypes::Scene)->GetSceneEditor()->OpenEntity(object.GetSubObject(ResourceAsset::Object));
			}
		}

		RID Create(UUID uuid, UndoRedoScope* scope) override
		{
			RID entity = Resources::Create(GetResourceTypeId(), UUID::RandomUUID(), scope);
			ResourceObject assetObject = Resources::Write(entity);
			assetObject.SetString(EntityResource::Name, "Entity");
			assetObject.Commit(scope);
			return entity;
		}

		static void GenerateThumbnail(RID asset)
		{
			if (ResourceObject object = Resources::Read(asset))
			{
				EntityThumbnailGenerator entityThumbnailGenerator{object.GetSubObject(ResourceAsset::Object)};
				entityThumbnailGenerator.GenerateThumbnail(asset);
			}
		}

		FnThumbnailGenerator GetThumbnailGenerator(RID rid) const override
		{
			return GenerateThumbnail;
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<EntityResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Entity";
		}

		bool CanInherit(RID rid) override
		{
			return true;
		}
	};


	void RegisterEntityHandler()
	{
		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/New Entity", .icon = ICON_FA_CUBE,
			.priority = 20,
			.action = ProjectBrowserWindow::AssetNew,
			.visible = ProjectBrowserWindow::CanCreateAsset,
			.userData = TypeInfo<EntityResource>::ID()
		});

		Reflection::Type<EntityHandler>();
	}
}
