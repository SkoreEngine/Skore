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
}
