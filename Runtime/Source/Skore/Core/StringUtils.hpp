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

#include "String.hpp"
#include "StringView.hpp"

namespace Skore
{
    inline String Last(const StringView& txt, const StringView& separator)
    {
        String ret{};
        Split(txt, separator, [&](const StringView& str)
        {
            ret = str;
        });
        return ret;
    }

    inline String WithoutLast(const StringView& txt, const StringView& separator)
    {
        String     ret{};
        StringView temp{};
        Split(txt, separator, [&](const StringView& str)
        {
            if (!ret.Empty())
            {
                ret += separator;
            }
            ret += temp;
            temp = str;
        });
        return ret;
    }

    inline String ToUpper(const StringView& string)
    {
        String ret;
        ret.Resize(string.Size());

        for(usize i = 0; i < string.Size(); ++i)
        {
            ret[i] = toupper(string[i]);
        }
        return ret;
    }

    inline void ToUpper(const StringView& string, String& ret)
    {
        ret.Resize(string.Size());

        for(usize i = 0; i < string.Size(); ++i)
        {
            ret[i] = toupper(string[i]);
        }
    }

    inline bool ContainsIgnoreCase(const StringView& string, const StringView& search)
    {
        String buffer{};
        buffer.Resize(search.Size());
        for (const char* it = string.begin(); it != string.end(); ++it)
        {
            buffer.Clear();

            for (int j = 0; j < search.Size() && it + j != string.end(); ++j)
            {
                buffer[j] = *(it + j);
            }

            if (buffer.Compare(search) == 0)
            {
                return true;
            }
        }
        return false;
    }

    inline String FormatName(const StringView& property)
    {
        String name = property;
        if (!name.Empty())
        {
            name[0] = toupper(name[0]);

            for (int i = 1; i < name.Size(); ++i)
            {
                auto p = name.begin() + i;
                if (*p == *" ")
                {
                    *p = toupper(*p);
                } else if (isupper(*p))
                {
                    bool insertSpace = true;
                    if (i<name.Size()-1)
                    {
                        auto pn = name.begin() + i + 1;
                        if (isupper(*pn))
                        {
                            insertSpace = false;
                        }
                    }

                    if (insertSpace)
                    {
                        name.Insert(p, " ");
                    }
                    i++;
                }
            }
        }
        return name;
    }

    inline String ToString(u64 value)
    {
        char buffer[20]{};
        i32 size = sprintf(buffer,"%llu", value);
        return {buffer, static_cast<usize>(size)};
    }


}
