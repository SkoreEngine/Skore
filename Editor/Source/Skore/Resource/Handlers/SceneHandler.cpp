#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Utils/PreviewGenerator.hpp"

namespace Skore
{
	struct SceneHandler : ResourceAssetHandler
	{
		SK_CLASS(SceneHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".scene";
		}

		void OpenAsset(RID asset) override
		{
			if (ResourceObject object = Resources::Read(asset))
			{
				Editor::GetWorkspaceOfType(WorkspaceTypes::Scene)->GetSceneEditor()->OpenScene(object.GetSubObject(ResourceAsset::Object));
			}
		}

		 RID Create(UUID uuid, UndoRedoScope* scope) override
		 {
		 	RID asset = Resources::Create(GetResourceTypeId(), UUID::RandomUUID(), scope);
		 	ResourceObject assetObject = Resources::Write(asset);

			assetObject.AddToSubObjectList(SceneResource::Entities, Resources::CreateFromPrototype(Resources::FindByPath("Skore://Prototypes/Lighting.entity")));
			assetObject.AddToSubObjectList(SceneResource::Entities, Resources::CreateFromPrototype(Resources::FindByPath("Skore://Prototypes/PostProcessing.entity")));

		 	assetObject.Commit(scope);
		 	return asset;
		 }

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<SceneResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Scene";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_CUBES;
		}

		bool CanInherit(RID rid) override
		{
			//can?
			return false;
		}
	};


	void RegisterSceneHandler()
	{
		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/New Scene", .icon = ICON_FA_CUBES,
			.priority = 10,
			.action = ProjectBrowserWindow::AssetNew,
			.visible = ProjectBrowserWindow::CanCreateAsset,
			.userData = TypeInfo<SceneResource>::ID()
		});

		Reflection::Type<SceneHandler>();
	}
}
