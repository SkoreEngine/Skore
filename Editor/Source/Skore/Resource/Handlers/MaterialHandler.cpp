#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Utils/ThumbnailGenerator.hpp"

namespace Skore
{

	struct MaterialThumbnailGenerator : ThumbnailGenerator
	{
		RID material;

		explicit MaterialThumbnailGenerator(const RID& material)
			: material(material) {}

		void SetupScene(Scene* scene) override
		{
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

		static void GenerateThumbnail(RID asset)
		{
			if (ResourceObject object = Resources::Read(asset))
			{
				MaterialThumbnailGenerator materialThumbnailGenerator{object.GetSubObject(ResourceAsset::Object)};
				materialThumbnailGenerator.GenerateThumbnail(asset);
			}
		}

		FnThumbnailGenerator GetThumbnailGenerator(RID rid) const override
		{
			return GenerateThumbnail;
		}

		bool CanInherit(RID rid) override
		{
			return true;
		}
	};


	void RegisterMaterialHandler()
	{
		Reflection::Type<MaterialHandler>();
	}
}
