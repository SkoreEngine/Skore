#pragma once


#include <Skore/Common.hpp>
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Resource/ResourceBuffer.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	class GPUTexture;
}

namespace Skore
{
	struct ResourceImportedAsset
	{
		enum : u8
		{
			OriginalFileName,
			Extension,
			ContentHash,
			ImporterId,
			CookerVersion,
			ImportSettings,
			OriginalData,
			OriginalSize,
			SubResources,
			Dependencies,
		};
	};

	struct ResourceSubIdEntry
	{
		enum : u8
		{
			SubId,
			TargetUUID,
			TypeName,
		};
	};

	struct ResourceDependencyEntry
	{
		enum : u8
		{
			RelPath,
			Data,
			Size,
		};
	};

	SK_API UUID SubResourceUUID(RID importedAsset, StringView subId);
	SK_API void RegisterResourceImportedAssetTypes();
	SK_API void ReloadAssetHandlers();

	struct SubResourceDecl
	{
		String subId;
		UUID   uuid;
		TypeID type;
	};

	struct DependencyDecl
	{
		String     relPath;
		ByteBuffer bytes;
	};

	struct SK_API IngestContext
	{
		RID            importedAsset;
		StringView     sourcePath;
		Span<u8>       sourceBytes;
		UndoRedoScope* scope = nullptr;

		Array<SubResourceDecl> subResources;
		Array<DependencyDecl>  dependencies;

		UUID DeclareSubResource(StringView subId, TypeID type);
		void AddDependency(StringView relPath, Span<u8> bytes);
		bool HasDependency(StringView relPath) const;
	};

	struct SK_API CookContext
	{
		RID            importedAsset;
		RID						 importSettings;
		Span<u8>       sourceBytes;
		UndoRedoScope* scope = nullptr;

		Array<RID> produced;

		RID        SubResource(StringView subId, TypeID type);
		ByteBuffer Dependency(StringView relPath) const;

		template <typename T>
		RID SubResource(StringView subId)
		{
			return SubResource(subId, TypeInfo<T>::ID());
		}
	};

	SK_API void ApplyIngest(IngestContext& ctx);

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
			ReadOnly,
			ImportedAsset
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
		virtual StringView    OutputExtension() { return {}; }
		virtual u32           CookerVersion() { return 1; }
		virtual TypeID        GetSettingsType() { return {}; }
		virtual void          Ingest(IngestContext& ctx) {}
		virtual void          Cook(CookContext& ctx) {}

		virtual bool          ImportAsset(RID directory, ConstPtr settings, StringView path, UndoRedoScope* scope) { return false; }
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
		static RID                     CreateImportedAssetWrapper(RID parent, StringView desiredName, StringView extension, UndoRedoScope* scope);
		static void                    EnsureCooked(RID rid);
		static void                    ReimportAssetFromFile(RID object);
		static RID                     GetWrapperForSubResource(RID subResource);
		static RID                     GetImportSettings(RID object);
		static void                    InitTestFolders(StringView rootDir);
		static void                    ClearCookCacheState();
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
