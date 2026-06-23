#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Utils/PreviewGenerator.hpp"

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
			return "Material";
		}

		TypeID GetPreviewGenerator() override
		{
			return TypeInfo<MaterialPreviewGenerator>::ID();
		}
	};


	void RegisterMaterialHandler()
	{
		Reflection::Type<MaterialPreviewGenerator>();
		Reflection::Type<MaterialHandler>();
	}
}
