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

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

namespace Skore
{
	struct DCCAssetHandler : ResourceAssetHandler
	{
		SK_CLASS(DCCAssetHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".dcc_asset";
		}

		void OpenAsset(RID rid) override
		{
			if (ResourceObject object = Resources::Read(rid))
			{
				if (ResourceObject dccAsset = Resources::Read(object.GetSubObject(ResourceAsset::Object)))
				{
					if (RID entity = dccAsset.GetSubObject(DCCAssetResource::Entity))
					{
						//TODO - readonly?
						Editor::GetCurrentWorkspace().GetSceneEditor()->OpenEntity(entity);
					}
				}
			}
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<DCCAssetResource>::ID();
		}

		StringView GetDesc() override
		{
			return "DCC Asset";
		}
	};

	void RegisterDCCAssetHandler()
	{
		Reflection::Type<DCCAssetHandler>();
	}
}
