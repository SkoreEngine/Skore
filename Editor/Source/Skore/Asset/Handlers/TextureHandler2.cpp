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

#include "Skore/Asset/AssetFileOld.hpp"
#include "Skore/Asset/AssetTypes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsAssets.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Window/TextureViewWindow.hpp"

namespace Skore
{
	struct TextureHandler2 : AssetHandler
	{
		SK_CLASS(TextureHandler2, AssetHandler);

		TypeID GetAssetTypeId() override
		{
			return TypeInfo<TextureAsset>::ID();
		}

		void OpenAsset(AssetFileOld* assetFile) override
		{
			if (TextureAsset* textureAsset = assetFile->GetInstance()->SafeCast<TextureAsset>())
			{
				if (GPUTexture* texture = textureAsset->GetTexture())
				{
					TextureViewWindow::Open(texture);
				}
			}
		}

		String Extension() override
		{
			return ".texture";
		}

		String Name() override
		{
			return "Texture";
		}
	};


	struct TextureImporter2 : AssetImporter
	{
		SK_CLASS(TextureImporter2, AssetImporter);

		Array<String> ImportExtensions() override
		{
			return {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"};
		}

		bool ImportAsset(AssetFileOld* assetFile, StringView path) override
		{
			if (TextureAsset* textureAsset = assetFile->GetInstance()->SafeCast<TextureAsset>())
			{
				return textureAsset->SetTextureDataFromFile(path, Path::Extension(path) == ".hdr", true, true);	//TODO: get from import settings
			}
			return false;
		}

		TypeID GetAssetTypeId() override
		{
			return TypeInfo<TextureAsset>::ID();
		}
	};

	void RegisterTextureHandler2()
	{
		Reflection::Type<TextureImporter2>();
		Reflection::Type<TextureHandler2>();
	}
}
