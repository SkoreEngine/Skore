#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Utils/ThumbnailGenerator.hpp"

namespace Skore
{


	static Logger& logger = Logger::GetLogger("Skore::MeshHandler");

	struct MeshThumbnailGenerator : ThumbnailGenerator
	{
		RID mesh;

		explicit MeshThumbnailGenerator(const RID& mesh)
			: mesh(mesh) {}

		void SetupScene(Scene* scene) override
		{
			Entity* entity = scene->CreateEntity();
			entity->AddComponent<Transform>();
			StaticMeshRenderer* staticMeshRenderer = entity->AddComponent<StaticMeshRenderer>();
			staticMeshRenderer->SetMesh(mesh);
		}
	};

	struct MeshHandler : ResourceAssetHandler
	{
		SK_CLASS(MeshHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".mesh";
		}

		void OpenAsset(RID rid) override
		{
			//TODO
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<MeshResource>::ID();
		}

		RID Create(UUID uuid, UndoRedoScope* scope) override
		{
			RID mesh = Resources::Create(GetResourceTypeId(), UUID::RandomUUID(), scope);

			ResourceObject meshObject = Resources::Write(mesh);
			meshObject.SetString(MeshResource::Name, "Mesh");
			meshObject.Commit(scope);

			return mesh;
		}

		StringView GetDesc() override
		{
			return "Mesh";
		}

		static void GenerateThumbnail(RID asset)
		{
			if (ResourceObject object = Resources::Read(asset))
			{
				if (object.GetType()->GetID() == TypeInfo<ResourceAsset>::ID())
				{
					MeshThumbnailGenerator meshThumbnailGenerator{object.GetSubObject(ResourceAsset::Object)};
					meshThumbnailGenerator.GenerateThumbnail(asset);
				}
				else if (object.GetType()->GetID() == TypeInfo<MeshResource>::ID())
				{
					MeshThumbnailGenerator meshThumbnailGenerator{asset};
					meshThumbnailGenerator.GenerateThumbnail(asset);
				}
			}
		}

		FnThumbnailGenerator GetThumbnailGenerator(RID rid) const override
		{
			return GenerateThumbnail;
		}
	};

	void RegisterMeshHandler()
	{
		Reflection::Type<MeshHandler>();
	}
}
