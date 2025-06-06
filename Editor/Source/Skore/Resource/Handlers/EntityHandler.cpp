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

#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Window/ProjectBrowserWindow.hpp"
#include "Skore/World/WordCommon.hpp"

namespace Skore
{
	struct EntityHandler : ResourceAssetHandler
	{
		SK_CLASS(EntityHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".entity";
		}

		void OpenAsset(RID rid) override
		{

		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<EntityResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Entity";
		}
	};


	void RegisterEntityHandler()
	{
		ProjectBrowserWindow::AddMenuItem(MenuItemCreation{
			.itemName = "New Entity", .icon = ICON_FA_CUBE,
			.priority = 10,
			.action = ProjectBrowserWindow::AssetNew,
			.enable = ProjectBrowserWindow::CanCreateAsset,
			.userData = TypeInfo<EntityResource>::ID()
		});

		Reflection::Type<EntityHandler>();
	}
}
