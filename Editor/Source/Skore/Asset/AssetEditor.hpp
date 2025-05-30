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

#pragma once
#include "Skore/Core/Span.hpp"
#include "Skore/Core/UUID.hpp"

namespace Skore
{
	class AssetFile;
	class GPUTexture;

	class AssetEditor
	{
	public:
		static void             SetProject(StringView name, StringView directory);
		static void             AddPackage(StringView name, StringView directory);
		static AssetFile*       GetProject();
		static Span<AssetFile*> GetPackages();
		static AssetFile*       CreateDirectory(AssetFile* parent);
		static AssetFile*       FindOrCreateAsset(AssetFile* parent, TypeID typeId, StringView suggestedName);
		static AssetFile*       CreateAsset(AssetFile* parent, TypeID typeId, StringView suggestedName = "", UUID uuid = {});
		static String           CreateUniqueName(AssetFile* parent, StringView desiredName);
		static void             GetUpdatedAssets(Array<AssetFile*>& updatedAssets);
		static Span<AssetFile*> GetAssetsByType(TypeID typeId);
		static void             ImportFile(AssetFile* parent, StringView path);
		static AssetFile*       GetFileByAbsolutePath(StringView path);

		static StringView GetLibFolder();

		static GPUTexture* GetDirectoryThumbnail();
		static GPUTexture* GetFileThumbnail();

		friend class AssetFile;

		static void AssetEditorOnUpdate();

	private:
		static AssetFile* ScanForAssets(AssetFile* parent, StringView name, StringView path);
		static AssetFile* CreateAssetFile(AssetFile* parent, StringView name, StringView extension);
		static void       AddAssetFile(AssetFile* assetFile);
		static void       RemoveAssetFile(AssetFile* assetFile);
	};
}
