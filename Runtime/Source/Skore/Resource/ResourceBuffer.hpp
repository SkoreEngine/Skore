#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/String.hpp"
#include <memory>

#include "Skore/IO/FileTypes.hpp"

namespace Skore
{
	struct ResourceBufferInstance
	{
		u64 id = 0;
		bool readOnly = true;
		String filePath;
		FileHandler handler;
		u64 size = 0;
		u64 offset = 0;
	};

	class SK_API ResourceBuffer
	{
	public:
		ResourceBuffer() = default;
		ResourceBuffer(const ResourceBuffer&) = default;
		ResourceBuffer(u64 id);
		ResourceBuffer(StringView idString);

		void MapFile(StringView path, bool readOnly, u64 offset, u64 size) const;
		void MapFile(FileHandler handler, bool readOnly, u64 offset, u64 size) const;
		u64  GetSize() const;
		u64  CopyData(VoidPtr data, u64 size, u64 offset = 0) const;

		//only works if the buffer is not readonly, and it's mapped to a file.
		FileHandler OpenFile(AccessMode accessMode) const;

		String  GetIdAsString() const;
		bool		IsReadOnly() const;

		operator bool() const;

		friend class ResourceObject;
	private:
		std::shared_ptr<ResourceBufferInstance> m_instance = nullptr;
	};
}
