#pragma once


#include <Skore/Common.hpp>
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Resource/ResourceBuffer.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	class GPUTexture;
}

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
			PathId,
			Directory,
			AssetFile,
			SourcePath,
			ReadOnly
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
	typedef void (*FnThumbnailGenerator)(RID asset);

	struct SK_API ResourceAssetHandler : Object
	{
		SK_CLASS(ResourceAssetHandler, Object);

		virtual StringView Extension() = 0;
		virtual void       OpenAsset(RID asset) = 0;
		virtual TypeID     GetResourceTypeId() = 0;
		virtual StringView GetDesc() = 0;

		virtual RID  Load(RID asset, StringView absolutePath);
		virtual void Save(RID object, StringView absolutePath);
		virtual RID  Create(UUID uuid, UndoRedoScope* scope);
		virtual void Reloaded(RID asset, StringView absolutePath);
		virtual void AfterMove(RID asset, StringView oldAbsolutePath, StringView newAbsolutePath);
		virtual void Export(RID object, ArchiveWriter& writer);

		virtual FnThumbnailGenerator GetThumbnailGenerator(RID rid) const;

		virtual const char* GetIcon() const;

		virtual int GetLoadOrder() const { return INT32_MAX; }

		virtual bool GetAssetName(RID rid, String& name)
		{
			return false;
		}

		virtual bool CanExtractAsset(RID rid)
		{
			return false;
		}

		virtual bool CanInherit(RID rid)
		{
			return false;
		}

		virtual void ExtractAsset(RID directory, RID object, UndoRedoScope* scope) {}
	};

	struct SK_API ResourceAssetImporter : Object
	{
		SK_CLASS(ResourceAssetImporter, Object);

		virtual Array<String> ImportedExtensions() = 0;
		virtual bool          ImportAsset(RID directory, ConstPtr settings, StringView path, UndoRedoScope* scope) = 0;
	};

	struct SK_API ResourceAssets
	{
		static RID                     ScanPackageFromDirectory(StringView packageName, StringView packagePath);
		static void                    SaveAssetsToDirectory(StringView directory, RID package, Span<UpdatedAssetInfo> items);
		static Array<UpdatedAssetInfo> GetUpdatedAssets(RID directory);
		static void                    OpenAsset(RID rid);
		static void                    ImportAsset(RID parent, StringView path);
		static RID                     CreateAsset(RID parent, TypeID typeId, StringView desiredName, UndoRedoScope* scope);
		static RID									   CreateAssetFile(RID parent, StringView desiredName, String extension, UndoRedoScope* scope);
		static RID                     DuplicateAsset(RID parent, RID sourceAsset, StringView desiredName, UndoRedoScope* scope);
		static RID                     CreateInheritedAsset(RID parent, RID sourceAsset, StringView desiredName, UndoRedoScope* scope);
		static RID                     CreateImportedAsset(RID parent, TypeID typeId, StringView desiredName, UndoRedoScope* scope, StringView sourcePath);
		static RID                     CreateExtractedAsset(RID parent, TypeID typeId, StringView desiredName, RID object, UndoRedoScope* scope);
		static RID                     FindAssetOnDirectory(RID directory, TypeID typeId, StringView name);
		static RID                     CreateDirectory(RID parent, StringView desiredName, UndoRedoScope* scope);
		static String                  CreateUniqueAssetName(RID parent, StringView desiredName, StringView extension, bool directory);
		static void                    MoveAsset(RID newParent, RID rid, UndoRedoScope* scope);
		static String                  GetDirectoryPathId(RID directory);
		static StringView              GetAbsolutePath(RID asset);
		static StringView              GetPathId(RID asset);
		static RID                     GetAssetPayload(RID rid);
		static RID                     GetResourceAssetFromResourceRecursive(RID rid);
		static String                  GetAbsolutePathFromPathId(StringView pathId);
		static RID                     GetParentAsset(RID rid);
		static bool                    IsChildOf(RID parent, RID child);
		static bool                    IsUpdated(RID rid);
		static bool                    GetAssetVersions(RID rid, u64& currentVersion, u64& persistedVersion);
		static u64                     GetAssetObjectVersion(RID rid);
		static ResourceAssetHandler*   GetAssetHandler(RID rid);
		static ResourceAssetHandler*   GetAssetHandler(TypeID typeId);
		static String                  GetAssetName(RID rid);
		static String                  GetAssetFullName(RID rid);
		static UUID                    GetAssetUUID(RID rid);
		static void                    ExportPackages(Span<RID> packages, StringView path, StringView name);
		static GPUTexture*             GetThumbnail(RID rid);
		static GPUTexture*             GetDefaultThumbnail();
		static const char*						 GetIcon(RID rid);
		static void                    UpdateThumbnail(RID rid, Span<u8> data);
		static ResourceBuffer          CreateTempBuffer();
		static ResourceBuffer          CreateTempBuffer(VoidPtr bytes, usize size);
		static void                    RegisterAssetByType(RID asset);
		static void                    UnregisterAssetByType(RID asset);
		static Array<RID>              GetAssets(TypeID typeId);
	};
}
