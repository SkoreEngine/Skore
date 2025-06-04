// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "Skore/Asset/AssetTypes.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Asset/AssetEditor.hpp"
#include "Skore/Asset/AssetFileOld.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/Window/ProjectBrowserWindow.hpp"

namespace Skore
{

	namespace
	{
		bool IsSceneSelected(const MenuItemEventData& eventData)
		{
			ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
			return projectBrowserWindow->GetLastSelectedItem() && projectBrowserWindow->GetLastSelectedItem()->GetAssetTypeId() == TypeInfo<Scene>::ID();
		}

		void OpenScene(const MenuItemEventData& eventData)
		{
			ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
			if (projectBrowserWindow->GetLastSelectedItem())
			{
				Editor::GetCurrentWorkspace().GetSceneEditor()->OpenScene(projectBrowserWindow->GetLastSelectedItem());
			}

		}

		void NewInheritedScene(const MenuItemEventData& eventData)
		{
			ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
			if (projectBrowserWindow->GetOpenDirectory() != nullptr && projectBrowserWindow->GetLastSelectedItem() != nullptr)
			{
				if (AssetFileOld* newAsset = AssetEditor::CreateAsset(projectBrowserWindow->GetOpenDirectory(), TypeInfo<Scene>::ID(), projectBrowserWindow->GetLastSelectedItem()->GetFileName()))
				{
					if (Scene* scene = newAsset->GetInstance()->SafeCast<Scene>())
					{
						Entity* entity = Entity::Instantiate(projectBrowserWindow->GetLastSelectedItem()->GetUUID(), nullptr);
						scene->SetRootEntity(entity);
						newAsset->MarkDirty();
					}
				}
			}
		}
	}

	struct SceneAssetHandler : AssetHandler
	{
		SK_CLASS(SceneAssetHandler, AssetHandler);

		TypeID GetAssetTypeId() override
		{
			return TypeInfo<Scene>::ID();
		}

		void OpenAsset(AssetFileOld* assetFile) override
		{
			Editor::GetCurrentWorkspace().GetSceneEditor()->OpenScene(assetFile);
		}

		String Extension() override
		{
			return ".scene";
		}

		String Name() override
		{
			return "Scene";
		}
	};

	void RegisterSceneAssetHandler()
	{
		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{.itemName = "Open Scene", .icon = ICON_FA_FOLDER_OPEN, .priority = 100, .action = OpenScene, .visible = IsSceneSelected});
		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{.itemName = "New Inherited Scene", .icon = ICON_FA_CLAPPERBOARD, .priority = 105, .action = NewInheritedScene, .visible = IsSceneSelected});

		Reflection::Type<SceneAssetHandler>();
	}
}
