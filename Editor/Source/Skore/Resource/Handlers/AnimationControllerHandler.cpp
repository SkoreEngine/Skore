#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"

namespace Skore
{
	struct AnimationControllerHandler : ResourceAssetHandler
	{
		SK_CLASS(AnimationControllerHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".animcontroller";
		}

		void OpenAsset(RID asset) override
		{
			if (ResourceObject object = Resources::Read(asset))
			{
				Editor::GetWorkspaceOfType(WorkspaceTypes::Animator)->GetAnimatorEditor().OpenAnimationController(object.GetSubObject(ResourceAsset::Object));
			}
		}

		RID Create(UUID uuid, UndoRedoScope* scope) override
		{
			RID controller = Resources::Create(GetResourceTypeId(), UUID::RandomUUID(), scope);
			ResourceObject object = Resources::Write(controller);
			object.SetString(AnimationControllerResource::Name, "AnimationController");
			object.Commit(scope);
			return controller;
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<AnimationControllerResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Animation Controller";
		}
	};

	void RegisterAnimationControllerHandler()
	{
		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "Create/New Animation Controller", .icon = ICON_FA_DIAGRAM_PROJECT,
			.priority = 50,
			.action = ProjectBrowserWindow::AssetNew,
			.visible = ProjectBrowserWindow::CanCreateAsset,
			.userData = TypeInfo<AnimationControllerResource>::ID()
		});

		Reflection::Type<AnimationControllerHandler>();
	}
}
