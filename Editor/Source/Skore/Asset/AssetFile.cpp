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

#include "AssetFile.hpp"

#include "AssetEditor.hpp"
#include "AssetTypes.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::AssetFile");

	void RemoveFileByAbsolutePath(StringView path);
	void AddFileByAbsolutePath(StringView path, AssetFile* file);

	AssetFile::~AssetFile()
	{
		if (m_instance)
		{
			DestroyAndFree(m_instance);
		}

		for (AssetFile* child : m_children)
		{
			DestroyAndFree(child);
		}
	}

	AssetHandler* AssetFile::GetHandler() const
	{
		return m_handler;
	}

	StringView AssetFile::GetExtension() const
	{
		return m_extension;
	}

	StringView AssetFile::GetName() const
	{
		static String cache;
		cache = m_fileName + m_extension;
		return cache;
	}

	StringView AssetFile::GetAbsolutePath() const
	{
		return m_absolutePath;
	}

	StringView AssetFile::GetPath() const
	{
		return m_path;
	}

	UUID AssetFile::GetUUID() const
	{
		return m_uuid;
	}

	Asset* AssetFile::GetInstance()
	{
		if (m_instance == nullptr && m_handler)
		{
			if (ReflectType* type = Reflection::FindTypeById(m_handler->GetAssetTypeId()))
			{
				m_instance = type->NewObject()->SafeCast<Asset>();
				m_instance->interface = this;
				m_handler->LoadInstance(this, m_instance);
			}
		}
		return m_instance;
	}

	void AssetFile::AddChild(AssetFile* child)
	{
		m_children.EmplaceBack(child);
		child->m_parent = this;
	}

	bool AssetFile::IsDirty() const
	{
		return m_currentVersion > m_persistedVersion;
	}

	bool AssetFile::IsDirectory() const
	{
		return m_type == AssetFileType::Directory || m_type == AssetFileType::Root;
	}

	bool AssetFile::IsChildOf(AssetFile* item) const
	{
		if (m_parent != nullptr)
		{
			if (m_parent == item)
			{
				return true;
			}

			if (m_parent->IsChildOf(item))
			{
				return true;
			}
		}
		return false;
	}

	bool AssetFile::IsActive() const
	{
		return true;
	}

	StringView AssetFile::GetFileName() const
	{
		return m_fileName;
	}

	bool AssetFile::CanAcceptNewChild() const
	{
		return true;
	}

	AssetFile* AssetFile::GetParent() const
	{
		return m_parent;
	}

	Span<AssetFile*> AssetFile::Children() const
	{
		return m_children;
	}

	void AssetFile::Rename(StringView newName)
	{
		if (m_fileName != newName)
		{
			m_fileName = AssetEditor::CreateUniqueName(m_parent, newName);
			FileSystemUpdated();
		}
	}

	void AssetFile::MoveTo(AssetFile* newParent)
	{
		RemoveFromParent();

		m_parent = newParent;
		newParent->m_children.EmplaceBack(this);

		FileSystemUpdated();
	}

	void AssetFile::Delete()
	{
		RemoveFromParent();
		AssetEditor::RemoveAssetFile(this);

		for (AssetFile* child : m_children)
		{
			child->m_parent = nullptr;
			child->Delete();
		}
		m_children.Clear();

		if (m_type == AssetFileType::ImportedAsset)
		{
			FileSystem::Remove(Path::Join(Path::Parent(m_absolutePath), m_fileName + m_extension + SK_IMPORT_EXTENSION));
			FileSystem::Remove(Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + m_fileName + m_extension + SK_ASSET_EXTENSION));
			FileSystem::Remove(Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + m_fileName + m_extension + SK_INFO_EXTENSION));
		}

		if (m_type == AssetFileType::Asset)
		{
			FileSystem::Remove(Path::Join(Path::Parent(m_absolutePath), m_fileName + m_extension + SK_INFO_EXTENSION));
		}

		FileSystem::Remove(m_absolutePath);
		RemoveFileByAbsolutePath(m_absolutePath);

		DestroyAndFree(this);
	}

	void AssetFile::Reimport()
	{
		m_status = AssetStatus::None;
		m_missingFiles.Clear();

		if (m_importer->ImportAsset(this, m_absolutePath))
		{
			Save();
		}
	}

	void AssetFile::AddAssociatedFile(StringView path)
	{
		//TODO: add associated file
	}

	void AssetFile::AddMissingFile(StringView path)
	{
		m_missingFiles.EmplaceBack(path);
	}

	const Array<String>& AssetFile::GetMissingFiles() const
	{
		return m_missingFiles;
	}

	void AssetFile::SetStatus(AssetStatus assetStatus)
	{
		m_status = assetStatus;
	}

	AssetStatus AssetFile::GetStatus() const
	{
		return m_status;
	}

	TypeID AssetFile::GetAssetTypeId() const
	{
		if (m_instance)
		{
			return m_instance->GetTypeId();
		}

		if (m_handler)
		{
			return m_handler->GetAssetTypeId();
		}

		if (m_importer)
		{
			return m_importer->GetAssetTypeId();
		}
		return 0;
	}

	AssetFileType AssetFile::GetAssetTypeFile() const
	{
		return m_type;
	}

	u64 AssetFile::GetPersistedVersion() const
	{
		return m_persistedVersion;
	}

	GPUTexture* AssetFile::GetThumbnail()
	{
		return IsDirectory() ? AssetEditor::GetDirectoryThumbnail() : AssetEditor::GetFileThumbnail();
	}

	void AssetFile::Iterator(const std::function<void(AssetFile*)>& function)
	{
		Queue<AssetFile*> pending;
		AssetFile*        current = this;

		while (current != nullptr)
		{
			function(current);

			for (AssetFile* child : current->m_children)
			{
				pending.Enqueue(child);
			}

			current = nullptr;
			if (!pending.IsEmpty())
			{
				current = pending.Dequeue();
			}
		}
	}

	void AssetFile::ChildrenIterator(const std::function<void(AssetFile*)>& function)
	{
		if (!m_children.Empty())
		{
			Queue<AssetFile*> pending;

			{
				for (AssetFile* child : m_children)
				{
					pending.Enqueue(child);
				}
			}

			AssetFile* current = pending.Dequeue();

			while (current != nullptr)
			{
				function(current);

				for (AssetFile* child : current->m_children)
				{
					pending.Enqueue(child);
				}

				current = nullptr;
				if (!pending.IsEmpty())
				{
					current = pending.Dequeue();
				}
			}
		}
	}

	void AssetFile::Save()
	{
		static Array<u8> data;
		data.Reserve(1000);

		String newAbsolutePathNoExtension = Path::Join(m_parent->m_absolutePath, m_fileName);
		String newAbsolutePath = newAbsolutePathNoExtension + m_extension;

		bool moved = !m_absolutePath.Empty() && newAbsolutePath != m_absolutePath;

		if (m_type == AssetFileType::Directory)
		{
			if (FileSystem::GetFileStatus(m_absolutePath).exists)
			{
				if (moved)
				{
					FileSystem::Rename(m_absolutePath, newAbsolutePath);
				}
			}
			else
			{
				FileSystem::CreateDirectory(newAbsolutePath);
			}
		}
		else if (m_type == AssetFileType::Asset)
		{
			//save info
			{
				YamlArchiveWriter writer;
				SerializeInfo(writer);
				FileSystem::SaveFileAsString(Path::Join(Path::Parent(newAbsolutePath), m_fileName + m_extension +  SK_INFO_EXTENSION), writer.EmitAsString());
			}

			YamlArchiveWriter writer;
			Serialize(writer);
			FileSystem::SaveFileAsString(newAbsolutePath, writer.EmitAsString());

			if (moved)
			{
				FileSystem::Remove(Path::Join(Path::Parent(m_absolutePath), Path::Name(m_absolutePath) + m_extension + SK_INFO_EXTENSION));
				FileSystem::Remove(m_absolutePath);
			}
		}
		else if (m_type == AssetFileType::ImportedAsset)
		{
			if (moved)
			{
				String oldFileName = Path::Name(m_absolutePath);

				FileSystem::Rename(Path::Join(Path::Parent(m_absolutePath), oldFileName + m_extension + SK_IMPORT_EXTENSION), newAbsolutePath + SK_IMPORT_EXTENSION);

				FileSystem::Rename(Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + oldFileName + m_extension + SK_ASSET_EXTENSION),
				                   Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + m_fileName + m_extension + SK_ASSET_EXTENSION));

				FileSystem::Rename(Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + oldFileName + m_extension + SK_INFO_EXTENSION),
				                   Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + m_fileName + m_extension + SK_INFO_EXTENSION));

				FileSystem::Rename(m_absolutePath, newAbsolutePath);
			}
			else
			{
				//import file
				{
					String importFile = newAbsolutePath + SK_IMPORT_EXTENSION;
					YamlArchiveWriter writer;
					SerializeInfo(writer);
					FileSystem::SaveFileAsString(importFile, writer.EmitAsString());
				}

				//asset data
				{
					data.Clear();
					Iterator([&](AssetFile* current)
					{
						BinaryArchiveWriter writer;
						current->GetInstance()->Serialize(writer);
						current->m_importedOffset = data.Size();
						current->m_importedSize = writer.GetData().Size();
						data.Append(writer.GetData().begin(), writer.GetData().Size());
					});
					FileSystem::SaveFileAsByteArray(Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + m_fileName + m_extension + SK_ASSET_EXTENSION), data);
				}

				//info
				{
					BinaryArchiveWriter writer;
					writer.BeginSeq("assets");
					Iterator([&](const AssetFile* current)
					{
						writer.BeginMap();

						AssetInfo assetInfo;
						assetInfo.uuid = current->m_uuid;
						assetInfo.name = current->m_fileName;
						assetInfo.offset = current->m_importedOffset;
						assetInfo.size = current->m_importedSize;

						if (ReflectType* type = Reflection::FindTypeById(current->GetAssetTypeId()))
						{
							assetInfo.type = type->GetName();
						}

						assetInfo.Serialize(writer);

						writer.EndMap();
					});
					writer.EndSeq();
					FileSystem::SaveFileAsByteArray(Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + m_fileName + m_extension + SK_INFO_EXTENSION), writer.GetData());
				}
			}
		}

		else if (m_type == AssetFileType::Other)
		{
			if (moved)
			{
				FileSystem::Rename(m_absolutePath, newAbsolutePath);
			}
		}

		m_persistedVersion = m_currentVersion;

		UpdateAbsolutePath(newAbsolutePath);
	}

	void AssetFile::Register()
	{
		if (m_type == AssetFileType::Asset)
		{
			if (String str = FileSystem::ReadFileAsString(GetInfoPathFile()); !str.Empty())
			{
				YamlArchiveReader yamlArchiveReader(str);
				DeserializeInfo(yamlArchiveReader);
			}
		}
		else if (m_type == AssetFileType::ImportedAsset && m_importer)
		{
			bool pendingImport = false;

			String importFile = Path::Join(m_parent->m_absolutePath, m_fileName + m_extension + SK_IMPORT_EXTENSION);
			if (!FileSystem::GetFileStatus(importFile).exists)
			{
				pendingImport = true;
				m_uuid = UUID::RandomUUID();
			}
			else
			{
				if (String str = FileSystem::ReadFileAsString(importFile); !str.Empty())
				{
					YamlArchiveReader yamlArchiveReader(str);
					DeserializeInfo(yamlArchiveReader);
				}
			}

			String infoPathFile = GetInfoPathFile();

			if (!FileSystem::GetFileStatus(infoPathFile).exists)
			{
				pendingImport = true;
			}

			if (pendingImport)
			{
				if (m_importer->ImportAsset(this, m_absolutePath))
				{
					logger.Info("File {} imported successfully ", m_absolutePath);
					Save();
				}
				else
				{
					m_status = AssetStatus::Error;
					logger.Error("File {} import failed ", m_absolutePath);
				}
			}
			else if (m_uuid)
			{
				Array<u8> bytes = FileSystem::ReadFileAsByteArray(infoPathFile);

				BinaryArchiveReader reader(bytes);
				reader.BeginSeq("assets");
				while (reader.NextSeqEntry())
				{
					reader.BeginMap();

					AssetInfo assetInfo;
					assetInfo.Deserialize(reader);

					AssetFile* file = nullptr;

					if (assetInfo.uuid == m_uuid)
					{
						file = this;
					}
					else if (ReflectType* reflectType = Reflection::FindTypeByName(assetInfo.type))
					{
						file = AssetEditor::CreateAsset(this, reflectType->GetProps().typeId, assetInfo.name, assetInfo.uuid);
					}

					if (file)
					{
						file->m_importedOffset = assetInfo.offset;
						file->m_importedSize = assetInfo.size;
					}

					reader.EndMap();
				}
				reader.EndMap();
			}
		}
		Assets::Register(m_path, m_uuid, this);
	}

	void AssetFile::MarkDirty()
	{
		if (m_instance)
		{
			m_instance->Changed();
		}
		m_currentVersion++;
	}

	void AssetFile::FileSystemUpdated()
	{
		UpdatePath();
		MarkDirty();

		if (FileSystem::GetFileStatus(m_absolutePath).exists)
		{
			Save();
		}
	}

	void AssetFile::UpdatePath()
	{
		m_path = m_parent->m_path + "/" + m_fileName + m_extension;

		for (AssetFile* child : m_children)
		{
			child->UpdatePath();
		}
	}

	void AssetFile::RemoveFromParent()
	{
		if (m_parent)
		{
			if (auto it = FindFirst(m_parent->m_children.begin(), m_parent->m_children.end(), this))
			{
				m_parent->m_children.Erase(it);
			}
		}
	}

	void AssetFile::SerializeInfo(ArchiveWriter& archiveWriter) const
	{
		AssetInternalInfo assetInternalInfo;
		assetInternalInfo.uuid = GetUUID();
		if (m_status == AssetStatus::Error)
		{
			assetInternalInfo.status = "Error";
		}
		else if (m_status == AssetStatus::Warning)
		{
			assetInternalInfo.status = "Warning";
		}
		assetInternalInfo.missingFiles = m_missingFiles;
		assetInternalInfo.Serialize(archiveWriter);
	}

	void AssetFile::DeserializeInfo(ArchiveReader& archiveReader)
	{
		AssetInternalInfo assetInternalInfo;
		assetInternalInfo.Deserialize(archiveReader);
		m_uuid = assetInternalInfo.uuid;
		if (assetInternalInfo.status == "Error")
		{
			m_status = AssetStatus::Error;
		}
		else if (assetInternalInfo.status == "Warning")
		{
			m_status = AssetStatus::Warning;
		}
		m_missingFiles = assetInternalInfo.missingFiles;
	}

	String AssetFile::GetImportAssetFile() const
	{
		if (m_type == AssetFileType::ImportedAsset)
		{
			return Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + m_fileName + m_extension + SK_ASSET_EXTENSION);
		}

		if (m_type == AssetFileType::Child)
		{
			if (m_parent)
			{
				return m_parent->GetImportAssetFile();
			}
		}
		return "";
	}

	u64 AssetFile::GetImportedSize() const
	{
		return m_importedSize;
	}

	u64 AssetFile::GetImportedOffset() const
	{
		return m_importedOffset;
	}

	void AssetFile::Serialize(ArchiveWriter& archiveWriter)
	{
		if (Asset* instance = GetInstance())
		{
			instance->Serialize(archiveWriter);
		}
	}

	void AssetFile::UpdateAbsolutePath(StringView newPath)
	{
		if (m_absolutePath != newPath)
		{
			if (!m_absolutePath.Empty())
			{
				RemoveFileByAbsolutePath(m_absolutePath);
			}
			m_absolutePath = newPath;
			AddFileByAbsolutePath(m_absolutePath, this);
		}
	}

	String AssetFile::GetInfoPathFile() const
	{
		if (m_type == AssetFileType::Asset)
		{
			return Path::Join(Path::Parent(m_absolutePath), m_fileName + m_extension + SK_INFO_EXTENSION);
		}

		if (m_type == AssetFileType::ImportedAsset)
		{
			return Path::Join(AssetEditor::GetLibFolder(), "Imported", GetUUID().ToString() + "_" + m_fileName + m_extension + SK_INFO_EXTENSION);
		}

		return "";
	}
}
