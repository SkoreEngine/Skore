#include "Skore/Resource/ResourceBuffer.hpp"
#include "Skore/IO/FileSystem.hpp"

namespace Skore
{
	ResourceBuffer::ResourceBuffer(u64 id)
	{
		m_instance = std::make_shared<ResourceBufferInstance>(id);
	}

	ResourceBuffer::ResourceBuffer(StringView idString) : ResourceBuffer(HexTo64(idString))
	{
	}

	void ResourceBuffer::MapFile(StringView path, bool readOnly, u64 offset, u64 size) const
	{
		m_instance->filePath = path;
		m_instance->readOnly = readOnly;
		m_instance->size = size;
		m_instance->offset = offset;
	}

	u64 ResourceBuffer::GetSize() const
	{
		SK_ASSERT(m_instance, "Invalid buffer");
		if (m_instance)
		{
			if (m_instance->size > 0)
			{
				return m_instance->size;
			}

			if (!m_instance->filePath.Empty())
			{
				return FileSystem::GetFileSize(m_instance->filePath);
			}
		}
		return 0;
	}

	u64 ResourceBuffer::CopyData(VoidPtr data, u64 size, u64 offset) const
	{
		SK_ASSERT(m_instance, "Invalid buffer");
		if (m_instance)
		{
			if (!m_instance->filePath.Empty())
			{
				FileHandler fileHandler = FileSystem::OpenFile(m_instance->filePath, AccessMode::ReadOnly);
				FileSystem::ReadFileAt(fileHandler, data, size, m_instance->offset + offset);
				FileSystem::CloseFile(fileHandler);
			}
		}
		return 0;
	}

	FileHandler ResourceBuffer::OpenFile(AccessMode accessMode) const
	{
		if (m_instance && !m_instance->readOnly && !m_instance->filePath.Empty())
		{
			return FileSystem::OpenFile(m_instance->filePath, accessMode);
		}
		return {};
	}

	String ResourceBuffer::GetIdAsString() const
	{
		SK_ASSERT(m_instance, "Invalid buffer");
		if (m_instance)
		{
			char  strBuffer[17]{};
			usize bufSize = U64ToHex(m_instance->id, strBuffer);
			return {strBuffer, bufSize};
		}
		return "";
	}

	bool ResourceBuffer::IsReadOnly() const
	{
		if (m_instance)
		{
			return m_instance->readOnly;
		}
		return false;
	}

	ResourceBuffer::operator bool() const
	{
		return m_instance != nullptr;
	}
}