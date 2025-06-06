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

#include "TextureImporter.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/Path.hpp"

#include <stb_image.h>

namespace Skore
{
	struct TextureImporter : ResourceAssetImporter
	{
		SK_CLASS(TextureImporter, ResourceAssetImporter);

		Array<String> ImportedExtensions() override
		{
			return {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"};
		}

		bool ImportAsset(RID directory, ConstPtr settings, StringView path, UndoRedoScope* scope) override
		{
			RID texture = ResourceAssets::CreateImportedAsset(directory, TypeInfo<TextureResource>::ID(), Path::Name(path), scope, path);

			i32 width{};
			i32 height{};
			i32 channels{};
			i32 desiredChannels = 4;

			u8* bytes = stbi_load(path.CStr(), &width, &height, &channels, desiredChannels);

			ResourceObject textureObject = Resources::Write(texture);
			textureObject.SetString(TextureResource::Name, Path::Name(path));
			textureObject.SetVec3(TextureResource::Extent, Vec3{static_cast<f32>(width), static_cast<f32>(height), 1});
			textureObject.SetBlob(TextureResource::Pixels, Span{bytes, static_cast<usize>(width * height * desiredChannels)});
			textureObject.Commit(scope);

			stbi_image_free(bytes);

			return true;
		}

	};

	void RegisterTextureImporter()
	{
		Reflection::Type<TextureImporter>();
	}
}
