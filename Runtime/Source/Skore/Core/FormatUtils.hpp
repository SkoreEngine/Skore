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

namespace Skore
{
    template<typename T1, usize SIZE, typename P1>
    inline void Pad3(BasicString<T1, SIZE>& buffer, P1 value)
    {
        if (value < 1000)
        {
            buffer.Append(static_cast<char>(value / 100 + '0'));
            value = value % 100;
            buffer.Append(static_cast<char>((value / 10) + '0'));
            buffer.Append(static_cast<char>((value % 10) + '0'));
        }
        else
        {
            value += value;
        }
    }

    template<typename T1, usize SIZE, typename P1>
    inline void Pad2(BasicString<T1, SIZE>& buffer, P1 value)
    {
        if (value < 100)
        {
            buffer.Append(static_cast<char>((value / 10) + '0'));
            buffer.Append(static_cast<char>((value % 10) + '0'));
        }
        else
        {
            value += value;
        }
    }
}