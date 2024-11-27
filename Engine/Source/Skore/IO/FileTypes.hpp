#pragma once

#include <fstream>

#include "Skore/Common.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{

    enum class AccessMode
    {
        None            = 0,
        ReadOnly        = 1,
        WriteOnly       = 2,
        ReadAndWrite    = 3
    };

    SK_HANDLER(FileHandler);

    struct FileStatus
    {
        bool    exists{};
        bool    isDirectory{};
        u64     lastModifiedTime{};
        u64     fileSize{};
        u64     fileId{};
    };

    class SK_API DirIterator
    {
    private:
        String  m_directory{};
        String  m_path{};
        VoidPtr m_handler{};
    public:
        DirIterator() = default;

        explicit DirIterator(const StringView& directory);

        String& operator*()
        {
            return m_path;
        }

        String* operator->()
        {
            return &m_path;
        }

        friend bool operator==(const DirIterator& a, const DirIterator& b)
        {
            return a.m_path == b.m_path;
        }

        friend bool operator!=(const DirIterator& a, const DirIterator& b)
        {
            return a.m_path != b.m_path;
        }

        DirIterator& operator++();

        virtual ~DirIterator();
    };

    class SK_API DirectoryEntries
    {

    private:
        String m_directory{};
    public:

        DirectoryEntries(const StringView& directory) : m_directory(directory)
        {}

        DirIterator begin()
        {
            return DirIterator{m_directory};
        }

        DirIterator end()
        {
            return DirIterator{};
        }
    };

    class SK_API OutputFileStream
    {
    public:
        OutputFileStream(StringView file);
        usize Write(u8* data, usize size);
        void  Close();
    private:
        std::ofstream stream;
        usize streamSize = 0;
    };
}
