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

#include "AssetFileOld.hpp"

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
	void AddFileByAbsolutePath(StringView path, AssetFileOld* file);

	AssetFileOld::~AssetFileOld()
	{
		if (m_instance)
		{
			DestroyAndFree(m_instance);
		}

		for (AssetFileOld* child : m_children)
		{
			DestroyAndFree(child);
		}
	}

	AssetHandler* AssetFileOld::GetHandler() const
	{
		return m_handler;
	}

	StringView AssetFileOld::GetExtension() const
	{
		return m_extension;
	}

	StringView AssetFileOld::GetName() const
	{
		static String cache;
		cache = m_fileName + m_extension;
		return cache;
	}

	StringView AssetFileOld::GetAbsolutePath() const
	{
		return m_absolutePath;
	}

	StringView AssetFileOld::GetPath() const
	{
		return m_path;
	}

	UUID AssetFileOld::GetUUID() const
	{
		return m_uuid;
	}

	Asset* AssetFileOld::GetInstance()
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

	void AssetFileOld::AddChild(AssetFileOld* child)
	{
		m_children.EmplaceBack(child);
		child->m_parent = this;
	}

	bool AssetFileOld::IsDirty() const
	{
		return m_currentVersion > m_persistedVersion;
	}

	bool AssetFileOld::IsDirectory() const
	{
		return m_type == AssetFileType::Directory || m_type == AssetFileType::Root;
	}

	bool AssetFileOld::IsChildOf(AssetFileOld* item) const
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

	bool AssetFileOld::IsActive() const
	{
		return true;
	}

	StringView AssetFileOld::GetFileName() const
	{
		return m_fileName;
	}

	bool AssetFileOld::CanAcceptNewChild() const
	{
		return true;
	}

	AssetFileOld* AssetFileOld::GetParent() const
	{
		return m_parent;
	}

	Span<AssetFileOld*> AssetFileOld::Children() const
	{
		return m_children;
	}

	void AssetFileOld::Rename(StringView newName)
	{
		if (m_fileName != newName)
		{
			m_fileName = AssetEditor::CreateUniqueName(m_parent, newName);
			FileSystemUpdated();
		}
	}

	void AssetFileOld::MoveTo(AssetFileOld* newParent)
	{
		RemoveFromParent();

		m_parent = newParent;
		newParent->m_children.EmplaceBack(this);

		FileSystemUpdated();
	}

	void AssetFileOld::Delete()
	{
		RemoveFromParent();
		AssetEditor::RemoveAssetFile(this);

		for (AssetFileOld* child : m_children)
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

	void AssetFileOld::Reimport()
	{
		m_status = AssetStatus::None;
		m_missingFiles.Clear();

		if (m_importer->ImportAsset(this, m_absolutePath))
		{
			Save();
		}
	}

	void AssetFileOld::AddAssociatedFile(StringView path)
	{
		//TODO: add associated file
	}

	void AssetFileOld::AddMissingFile(StringView path)
	{
		m_missingFiles.EmplaceBack(path);
	}

	const Array<String>& AssetFileOld::GetMissingFiles() const
	{
		return m_missingFiles;
	}

	void AssetFileOld::SetStatus(AssetStatus assetStatus)
	{
		m_status = assetStatus;
	}

	AssetStatus AssetFileOld::GetStatus() const
	{
		return m_status;
	}

	TypeID AssetFileOld::GetAssetTypeId() const
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

	AssetFileType AssetFileOld::GetAssetTypeFile() const
	{
		return m_type;
	}

	u64 AssetFileOld::GetPersistedVersion() const
	{
		return m_persistedVersion;
	}

	GPUTexture* AssetFileOld::GetThumbnail()
	{
		return IsDirectory() ? AssetEditor::GetDirectoryThumbnail() : AssetEditor::GetFileThumbnail();
	}

	void AssetFileOld::Iterator(const std::function<void(AssetFileOld*)>& function)
	{
		Queue<AssetFileOld*> pending;
		AssetFileOld*        current = this;

		while (current != nullptr)
		{
			function(current);

			for (AssetFileOld* child : current->m_children)
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

	void AssetFileOld::ChildrenIterator(const std::function<void(AssetFileOld*)>& function)
	{
		if (!m_children.Empty())
		{
			Queue<AssetFileOld*> pending;

			{
				for (AssetFileOld* child : m_children)
				{
					pending.Enqueue(child);
				}
			}

			AssetFileOld* current = pending.Dequeue();

			while (current != nullptr)
			{
				function(current);

				for (AssetFileOld* child : current->m_children)
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

	void AssetFileOld::Save()
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
					Iterator([&](AssetFileOld* current)
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
					Iterator([&](const AssetFileOld* current)
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

	void AssetFileOld::Register()
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
				Array<u8> bytes;
				FileSystem::ReadFileAsByteArray(infoPathFile, bytes);

				BinaryArchiveReader reader(bytes);
				reader.BeginSeq("assets");
				while (reader.NextSeqEntry())
				{
					reader.BeginMap();

					AssetInfo assetInfo;
					assetInfo.Deserialize(reader);

					AssetFileOld* file = nullptr;

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

	void AssetFileOld::MarkDirty()
	{
		if (m_instance)
		{
			m_instance->Changed();
		}
		m_currentVersion++;
	}

	void AssetFileOld::FileSystemUpdated()
	{
		UpdatePath();
		MarkDirty();

		if (FileSystem::GetFileStatus(m_absolutePath).exists)
		{
			Save();
		}
	}

	void AssetFileOld::UpdatePath()
	{
		m_path = m_parent->m_path + "/" + m_fileName + m_extension;

		for (AssetFileOld* child : m_children)
		{
			child->UpdatePath();
		}
	}

	void AssetFileOld::RemoveFromParent()
	{
		if (m_parent)
		{
			if (auto it = FindFirst(m_parent->m_children.begin(), m_parent->m_children.end(), this))
			{
				m_parent->m_children.Erase(it);
			}
		}
	}

	void AssetFileOld::SerializeInfo(ArchiveWriter& archiveWriter) const
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

	void AssetFileOld::DeserializeInfo(ArchiveReader& archiveReader)
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

	String AssetFileOld::GetImportAssetFile() const
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

	u64 AssetFileOld::GetImportedSize() const
	{
		return m_importedSize;
	}

	u64 AssetFileOld::GetImportedOffset() const
	{
		return m_importedOffset;
	}

	void AssetFileOld::Serialize(ArchiveWriter& archiveWriter)
	{
		if (Asset* instance = GetInstance())
		{
			instance->Serialize(archiveWriter);
		}
	}

	void AssetFileOld::UpdateAbsolutePath(StringView newPath)
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

	String AssetFileOld::GetInfoPathFile() const
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
