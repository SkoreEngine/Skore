#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Utils/PreviewGenerator.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"

namespace Skore
{

	struct MaterialPreviewGenerator : PreviewGenerator
	{
		SK_CLASS(MaterialPreviewGenerator, PreviewGenerator);

		void SetupScene(Scene* scene) override
		{
			RID material = asset;
			if (ResourceObject object = Resources::Read(asset))
			{
				if (object.GetType()->GetID() == TypeInfo<ResourceAsset>::ID())
				{
					material = object.GetSubObject(ResourceAsset::Object);
				}
			}

			Entity* entity = scene->CreateEntity();
			entity->AddComponent<Transform>();
			StaticMeshRenderer* staticMeshRenderer = entity->AddComponent<StaticMeshRenderer>();
			staticMeshRenderer->SetMesh(Resources::FindByPath("Skore://Meshes/Sphere.mesh"));
			staticMeshRenderer->SetMaterial(0, material);
		}

		f32 PercentageInScreen() override
		{
			return 0.9f;
		}
	};


	struct MaterialHandler : ResourceAssetHandler
	{
		SK_CLASS(MaterialHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".material";
		}

		void OpenAsset(RID asset) override
		{
			if (ResourceObject object = Resources::Read(asset))
			{
				Editor::GetActiveWorkspace()->OpenAsset(object.GetSubObject(ResourceAsset::Object));
			}
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<MaterialResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Sky Material";
		}

		TypeID GetPreviewGenerator() override
		{
			return TypeInfo<MaterialPreviewGenerator>::ID();
		}
	};

	//Legacy PBR materials were replaced by material graphs; .material assets remain only for the
	//skybox path, so creation seeds the sky type directly.
	void CreateSkyMaterialAsset(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);

		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Create Sky Material");
		RID            material = ResourceAssets::CreateAsset(projectBrowserWindow->GetOpenDirectory(), TypeInfo<MaterialResource>::ID(), "", scope);

		ResourceObject materialObject = Resources::Write(material);
		materialObject.SetString(MaterialResource::Name, "SkyMaterial");
		materialObject.SetEnum(MaterialResource::Type, MaterialResource::MaterialType::SkyboxEquirectangular);
		materialObject.Commit(scope);

		projectBrowserWindow->SetRenameItem(Resources::GetParent(material), scope);
	}

	void RegisterMaterialHandler()
	{
		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/New Sky Material",
			.icon = ICON_FA_CLOUD_SUN,
			.priority = 37,
			.action = CreateSkyMaterialAsset,
			.visible = ProjectBrowserWindow::CanCreateAsset
		});

		Reflection::Type<MaterialPreviewGenerator>();
		Reflection::Type<MaterialHandler>();
	}
}
