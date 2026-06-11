#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Utils/PreviewGenerator.hpp"

namespace Skore
{


	static Logger& logger = Logger::GetLogger("Skore::MeshHandler");

	struct MeshPreviewGenerator : PreviewGenerator
	{
		SK_CLASS(MeshPreviewGenerator, PreviewGenerator);

		void SetupScene(Scene* scene) override
		{
			RID mesh = asset;
			if (ResourceObject object = Resources::Read(asset))
			{
				if (object.GetType()->GetID() == TypeInfo<ResourceAsset>::ID())
				{
					mesh = object.GetSubObject(ResourceAsset::Object);
				}
			}

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

		TypeID GetPreviewGenerator() override
		{
			return TypeInfo<MeshPreviewGenerator>::ID();
		}
	};

	void RegisterMeshHandler()
	{
		Reflection::Type<MeshPreviewGenerator>();
		Reflection::Type<MeshHandler>();
	}
}
