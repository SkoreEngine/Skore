#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/MaterialGraph/MaterialNode.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"

namespace Skore
{
	struct MaterialGraphHandler : ResourceAssetHandler
	{
		SK_CLASS(MaterialGraphHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".matgraph";
		}

		void OpenAsset(RID asset) override
		{
			if (ResourceObject object = Resources::Read(asset))
			{
				RID graph = object.GetSubObject(ResourceAsset::Object);
				Editor::GetWorkspaceOfType(WorkspaceTypes::Material)->GetMaterialEditor().OpenMaterialGraph(graph);
			}
		}

		RID Create(UUID uuid, UndoRedoScope* scope) override
		{
			RID graph = Resources::Create(GetResourceTypeId(), UUID::RandomUUID(), scope);

			RID outputNode = Resources::Create<MaterialGraphNodeResource>(UUID::RandomUUID(), scope);
			{
				ResourceObject nodeObj = Resources::Write(outputNode);
				nodeObj.SetString(MaterialGraphNodeResource::Type, MaterialNodeRegistry::OutputTypeId());
				nodeObj.SetVec2(MaterialGraphNodeResource::Position, Vec2{400.0f, 220.0f});
				nodeObj.Commit(scope);
			}

			ResourceObject graphObj = Resources::Write(graph);
			graphObj.SetString(MaterialGraphResource::Name, "MaterialGraph");
			graphObj.AddToSubObjectList(MaterialGraphResource::Nodes, outputNode);
			graphObj.SetReference(MaterialGraphResource::OutputNode, outputNode);
			graphObj.SetEnum(MaterialGraphResource::AlphaMode, MaterialGraphResource::GraphAlphaMode::Opaque);
			graphObj.SetFloat(MaterialGraphResource::MaskCutoff, 0.5f);
			graphObj.Commit(scope);

			return graph;
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<MaterialGraphResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Material Graph";
		}

		const char* GetIcon() const override
		{
			return ICON_FA_DIAGRAM_PROJECT;
		}
	};

	void RegisterMaterialGraphHandler()
	{
		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/New Material Graph",
			.icon = ICON_FA_DIAGRAM_PROJECT,
			.priority = 35,
			.action = ProjectBrowserWindow::AssetNew,
			.visible = ProjectBrowserWindow::CanCreateAsset,
			.userData = TypeInfo<MaterialGraphResource>::ID()
		});

		Reflection::Type<MaterialGraphHandler>();
	}
}
