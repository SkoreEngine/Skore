#include "Buffers.hpp"

#include "Skore/Core/Math.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/FileTypes.hpp"
#include "Skore/IO/Path.hpp"

namespace Skore
{

    struct FileBufferInstance : BufferInstance
    {
        String path;
        u64    size = 0;
        u64    offset = 0;

        FileBufferInstance() = default;
        FileBufferInstance(const String& path, u64 size, u64 offset)
            : path(path),
              size(size),
              offset(offset)
        {

        }

        u64 GetSize() override
        {
            if (this->size == 0)
            {
                this->size = FileSystem::GetFileSize(path);
            }
            return this->size;
        }

        void GetData(VoidPtr data, u64 readSize) const override
        {
            FileHandler handler = FileSystem::OpenFile(path, AccessMode::ReadOnly);
            FileSystem::ReadFileAt(handler, data, readSize > 0 ? readSize : size, offset);
            FileSystem::CloseFile(handler);
        }
    };


    struct TempBufferInstance : FileBufferInstance
    {
        TempBufferInstance(StringView bufferName, VoidPtr data, usize newSize)
        {
            size = newSize;
            path = Path::Join(FileSystem::TempFolder(), String("buffer_").Append(bufferName));

            FileHandler handler = FileSystem::OpenFile(path, AccessMode::WriteOnly);
            FileSystem::WriteFile(handler, data, size);
            FileSystem::CloseFile(handler);
        }

        TempBufferInstance(StringView bufferName, StringView origin)
        {
            size = FileSystem::GetFileStatus(origin).fileSize;
            path = Path::Join(FileSystem::TempFolder(), String("buffer_").Append(bufferName));
            FileSystem::CopyFile(origin, path);
        }

        ~TempBufferInstance() override
        {
            FileSystem::Remove(path);
        }
    };

    ResourceBuffer Buffers::CreateBuffer()
    {
        ResourceBuffer buffer;
        buffer.id = Random::Xorshift64star();
        return buffer;
    }

    ResourceBuffer Buffers::CreateBuffer(u64 id)
    {
        ResourceBuffer buffer;
        buffer.id = id;
        return buffer;
    }

    ResourceBuffer Buffers::CreateBuffer(VoidPtr data, usize size)
    {
        ResourceBuffer buffer;
        buffer.id = Random::Xorshift64star();
        buffer.instance = std::make_shared<TempBufferInstance>(buffer.GetIdAsString(), data, size);
        return buffer;
    }

    ResourceBuffer Buffers::CreateBuffer(StringView origin)
    {
        ResourceBuffer buffer;
        buffer.id = Random::Xorshift64star();
        buffer.instance = std::make_shared<TempBufferInstance>(buffer.GetIdAsString(), origin);
        return buffer;
    }

    bool ResourceBuffer::HasInstance() const
    {
        return instance.get() != nullptr;
    }

    u64 ResourceBuffer::GetSize() const
    {
        if (instance)
        {
            return instance->GetSize();
        }
        return 0;
    }

    void ResourceBuffer::GetData(VoidPtr data, u64 size) const
    {
        if (instance)
        {
            instance->GetData(data, size);
        }
    }

    String ResourceBuffer::GetIdAsString() const
    {
        char  strBuffer[17]{};
        usize bufSize = U64ToHex(id, strBuffer);
        return String{strBuffer, bufSize};
    }

    u64 ResourceBuffer::GetId() const
    {
        return id;
    }

    void ResourceBuffer::MapFile(StringView path, u64 size, u64 offset)
    {
        instance = std::make_shared<FileBufferInstance>(path, size, offset);
    }

    StringView ResourceBuffer::GetMappedPath() const
    {
        if (std::shared_ptr<FileBufferInstance> fileInstance = std::dynamic_pointer_cast<FileBufferInstance>(instance))
        {
            return fileInstance->path;
        }
        return {};
    }

    void ResourceBuffer::SaveTo(StringView path)
    {
        if (instance)
        {
            std::shared_ptr<FileBufferInstance> fileInstance = std::dynamic_pointer_cast<FileBufferInstance>(instance);
            if (fileInstance && fileInstance->path != path)
            {
                FileSystem::CopyFile(fileInstance->path, path);
                instance = std::make_shared<FileBufferInstance>(path, fileInstance->size, fileInstance->offset);
            }
        }
    }

    ResourceBuffer::operator bool() const noexcept
    {
        return HasInstance();
    }
}
