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
#include <functional>

#include "AssetTypes.hpp"
#include "Skore/IO/Assets.hpp"

namespace Skore
{
	class GPUTexture;
	struct AssetHandler;
	struct AssetImporter;
	class AssetFileOld;

	enum class AssetFileType : u8
	{
		None,
		Root,
		Asset,
		ImportedAsset,
		Child,
		Directory,
		Source,
		Other
	};

	class AssetFileOld : public AssetInterface
	{
	public:
		AssetFileOld() = default;
		~AssetFileOld() override;

		SK_NO_COPY_CONSTRUCTOR(AssetFileOld);

		AssetHandler* GetHandler() const;

		bool IsDirty() const;
		bool IsDirectory() const;
		bool IsChildOf(AssetFileOld* item) const;
		bool IsActive() const;

		StringView GetFileName() const;
		StringView GetExtension() const;
		StringView GetName() const override;
		StringView GetAbsolutePath() const override;
		StringView GetPath() const;
		UUID       GetUUID() const override;
		Asset*     GetInstance() override;

		void             AddChild(AssetFileOld* child);
		bool             CanAcceptNewChild() const;
		AssetFileOld*       GetParent() const;
		Span<AssetFileOld*> Children() const;

		void Rename(StringView newName);
		void MoveTo(AssetFileOld* newParent);

		void Save();
		void Register();
		void MarkDirty();
		void Delete();
		void Reimport();

		void AddAssociatedFile(StringView path);

		void                 AddMissingFile(StringView path);
		const Array<String>& GetMissingFiles() const;
		void                 SetStatus(AssetStatus assetStatus);
		AssetStatus          GetStatus() const;

		TypeID        GetAssetTypeId() const;
		AssetFileType GetAssetTypeFile() const;

		String GetImportAssetFile() const;
		u64    GetImportedSize() const;
		u64    GetImportedOffset() const;

		u64 GetPersistedVersion() const;

		GPUTexture* GetThumbnail();

		void Iterator(const std::function<void(AssetFileOld*)>& function);
		void ChildrenIterator(const std::function<void(AssetFileOld*)>& function);

		friend class AssetEditor;

	private:
		UUID           m_uuid;
		String         m_fileName;
		String         m_extension;
		String         m_path;
		String         m_absolutePath;
		AssetFileType  m_type = AssetFileType::None;
		AssetHandler*  m_handler = nullptr;
		AssetImporter* m_importer = nullptr;
		Asset*         m_instance = nullptr;

		AssetStatus   m_status = AssetStatus::None;
		Array<String> m_missingFiles;

		u64 m_importedSize{};
		u64 m_importedOffset{};

		u64 m_currentVersion = 0;
		u64 m_persistedVersion = 0;

		u64 m_listenerId = U64_MAX;

		Array<AssetFileOld*> m_children;
		AssetFileOld*        m_parent = nullptr;

		void FileSystemUpdated();
		void UpdatePath();
		void RemoveFromParent();

		void SerializeInfo(ArchiveWriter& archiveWriter) const;
		void DeserializeInfo(ArchiveReader& archiveReader);

		void Serialize(ArchiveWriter& archiveWriter);

		void UpdateAbsolutePath(StringView newPath);

		String GetInfoPathFile() const;
	};
}
