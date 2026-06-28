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
	//Both "Material Graph" and "Material Instance" assets are the same MaterialGraphResource behind one
	//.matgraph handler; they differ only by the Kind field. Graphs are authored in the node editor;
	//instances reuse a parent graph and are edited (parameter overrides) in the Properties window.
	struct MaterialGraphHandler : ResourceAssetHandler
	{
		SK_CLASS(MaterialGraphHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".matgraph";
		}

		void OpenAsset(RID asset) override
		{
			ResourceObject object = Resources::Read(asset);
			if (!object) return;

			RID graph = object.GetSubObject(ResourceAsset::Object);

			MaterialGraphResource::MaterialKind kind = MaterialGraphResource::MaterialKind::Graph;
			if (ResourceObject graphObj = Resources::Read(graph))
			{
				kind = graphObj.GetEnum<MaterialGraphResource::MaterialKind>(MaterialGraphResource::Kind);
			}

			if (kind == MaterialGraphResource::MaterialKind::Instance)
			{
				//instances have no node network of their own; they're authored in the Properties window,
				//so just make this the active selection.
				Editor::GetActiveWorkspace()->OpenAsset(graph);
				return;
			}

			Editor::GetWorkspaceOfType(WorkspaceTypes::Material)->GetMaterialEditor().OpenMaterialGraph(graph);
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
			graphObj.SetEnum(MaterialGraphResource::Kind, MaterialGraphResource::MaterialKind::Graph);
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

	//Creates a Material Instance: the same MaterialGraphResource but Kind=Instance, with the default
	//graph network stripped (an instance defers its network to a parent assigned later).
	void CreateMaterialInstanceAsset(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);

		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Create Material Instance");
		RID            instance = ResourceAssets::CreateAsset(projectBrowserWindow->GetOpenDirectory(), TypeInfo<MaterialGraphResource>::ID(), "", scope);

		{
			ResourceObject graphObj = Resources::Write(instance);
			graphObj.SetString(MaterialGraphResource::Name, "MaterialInstance");
			graphObj.SetEnum(MaterialGraphResource::Kind, MaterialGraphResource::MaterialKind::Instance);

			//strip the default output node the graph create seeded; an instance has no network of its own
			Array<RID> nodes;
			for (RID node : graphObj.GetSubObjectList(MaterialGraphResource::Nodes))
			{
				nodes.EmplaceBack(node);
			}
			for (RID node : nodes)
			{
				graphObj.RemoveFromSubObjectList(MaterialGraphResource::Nodes, node);
			}
			graphObj.SetReference(MaterialGraphResource::OutputNode, {});
			graphObj.Commit(scope);
		}

		projectBrowserWindow->SetRenameItem(Resources::GetParent(instance), scope);
	}

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

		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/New Material Instance",
			.icon = ICON_FA_LAYER_GROUP,
			.priority = 36,
			.action = CreateMaterialInstanceAsset,
			.visible = ProjectBrowserWindow::CanCreateAsset
		});

		Reflection::Type<MaterialGraphHandler>();
	}
}
