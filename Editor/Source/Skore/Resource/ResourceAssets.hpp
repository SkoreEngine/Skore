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


#include <Skore/Common.hpp>
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	struct ResourceAssetPackage
	{
		enum :u8
		{
			Name,
			AbsolutePath,
			Files,
			Root
		};
	};

	struct ResourceAssetFile
	{
		enum :u8
		{
			AssetRef,
			AbsolutePath,
			RelativePath,
			PersistedVersion,
			TotalSizeInDisk,
			LastModifiedTime
		};
	};

	struct ResourceAsset
	{
		enum : u8
		{
			Name,
			Extension,
			Type,
			Object,
			Parent,
			Path,
			Directory,
			AssetFile,
			SourcePath
		};
	};

	struct ResourceAssetDirectory
	{
		enum : u8
		{
			DirectoryAsset,
			Directories,
			Assets
		};
	};

	enum class UpdatedAssetType
	{
		Created,
		Updated,
		Deleted
	};

	struct UpdatedAssetInfo
	{
		UpdatedAssetType type;
		RID              asset;
		RID              assetFile;
		String           displayName;
		String           path;
		bool             shouldUpdate;
	};

	typedef RID (* FnResourceAssetLoader)(StringView path);
	typedef void (*FnResourceExtractAssets)(RID parent, RID asset);
	typedef void (*FnResourceGetAssetName)(RID rid, String& assetName);

	struct SK_API ResourceAssetHandler : Object
	{
		SK_CLASS(ResourceAssetHandler, Object);

		virtual StringView Extension() = 0;
		virtual void       OpenAsset(RID asset) = 0;
		virtual TypeID     GetResourceTypeId() = 0;
		virtual StringView GetDesc() = 0;

		virtual RID Load(StringView absolutePath);

		virtual bool GetAssetName(RID rid, String& name)
		{
			return false;
		}


		virtual bool CanExtractAsset(RID rid)
		{
			return false;
		}

		virtual void ExtractAsset(RID directory, RID asset) {}
	};

	struct SK_API ResourceAssetImporter : Object
	{
		SK_CLASS(ResourceAssetImporter, Object);

		virtual Array<String> ImportedExtensions() = 0;
		virtual bool          ImportAsset(RID directory, ConstPtr settings, StringView path, UndoRedoScope* scope) = 0;
	};

	struct SK_API ResourceAssets
	{
		static RID                   ScanAssetsFromDirectory(StringView packageName, StringView directory);
		static void                  SaveAssetsToDirectory(StringView directory, RID package, Span<UpdatedAssetInfo> items);
		static void                  GetUpdatedAssets(RID directory, Array<UpdatedAssetInfo>& items);
		static void                  OpenAsset(RID rid);
		static void                  ImportAssets(RID parent, Span<String> paths);
		static RID                   CreateAsset(RID parent, TypeID typeId, StringView desiredName, UndoRedoScope* scope);
		static RID                   CreateImportedAsset(RID parent, TypeID typeId, StringView desiredName, UndoRedoScope* scope, StringView sourcePath);
		static RID                   CreateDirectory(RID parent, StringView desiredName, UndoRedoScope* scope);
		static String                CreateUniqueAssetName(RID parent, StringView desiredName, bool directory);
		static String                GetDirectoryPath(RID directory);
		static StringView            GetAbsolutePath(RID asset);
		static RID                   GetAsset(RID rid);
		static RID                   GetParentAsset(RID rid);
		static bool                  IsChildOf(RID parent, RID child);
		static bool                  IsUpdated(RID rid);
		static ResourceAssetHandler* GetAssetHandler(RID rid);
		static ResourceAssetHandler* GetAssetHandler(TypeID typeId);
		static String                GetAssetName(RID rid);
		static Span<RID>             GetResourceAssetsByType(TypeID typeId);
	};
}
