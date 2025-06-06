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

#include "Skore/Common.hpp"
#include <initializer_list>

namespace Skore
{
    template<typename T>
    class Array;

    template<typename T, usize bufferSize>
    class FixedArray;

    template<typename T>
    class BasicByteBuffer;

    template<typename T>
    class Span
    {
    public:
        typedef T ValueType;
        typedef T* Iterator;
        typedef const T* ConstIterator;

        constexpr Span() : m_first(0), m_last(0)
        {}

        constexpr Span(Array<T>& arr) : m_first(arr.Data()), m_last(arr.Data() + arr.Size())
        {
        }

        constexpr Span(const Array<T>& arr) : m_first((T*) arr.Data()), m_last((T*) arr.Data() + arr.Size())
        {
        }

        template<usize size>
        constexpr Span(const FixedArray<T, size>& vec) : m_first((T*) vec.Data()), m_last((T*) vec.Data() + vec.Size())
        {
        }

        constexpr Span(T* t) : m_first(t), m_last(t + 1)
        {
        }

        constexpr Span(T* first, T* last) : m_first(first), m_last(last)
        {}

        constexpr Span(T* first, usize size) : m_first(first), m_last(first + size)
        {}

        constexpr Span(std::initializer_list<T> initializerList) : m_first((T*) initializerList.begin()), m_last((T*) initializerList.end())
        {
        }

        constexpr Span(const BasicByteBuffer<T>& buffer) : m_first((T*)buffer.begin()), m_last((T*)buffer.end())
        {
        }

        constexpr const T* Data() const
        {
            return begin();
        }

        constexpr Iterator begin()
        {
            return m_first;
        }

        constexpr Iterator end()
        {
            return m_last;
        }

        constexpr ConstIterator begin() const
        {
            return m_first;
        }

        constexpr ConstIterator end() const
        {
            return m_last;
        }

        constexpr usize Size() const
        {
            return (usize) (end() - begin());
        }

        constexpr bool Empty() const
        {
            return Size() == 0;
        }

        constexpr T& operator[](usize idx)
        {
            return begin()[idx];
        }

        constexpr const T& operator[](usize idx) const
        {
            return begin()[idx];
        }

        constexpr const T& Back() const
        {
            return begin()[Size() - 1];
        }

        constexpr T& Back()
        {
            return begin()[Size() - 1];
        }

        constexpr bool operator==(const Span& other) const
        {
            if (this->Size() != other.Size()) return false;
            for (int i = 0; i < this->Size(); ++i)
            {
                if (this->operator[](i) != other[i])
                {
                    return false;
                }
            }
            return true;
        }

        constexpr bool operator!=(const Span& other) const
        {
            return !((*this) == other);
        }

        constexpr usize IndexOf(T object)
        {
            return FindFirstIndex(begin(), end(), object);
        }

    private:
        T* m_first;
        T* m_last;
    };

    template<typename T>
    bool operator==(const Span<T>& l, const Array<T>& r)
    {
        if (l.Size() != r.Size()) return false;

        for (usize i = 0; i < l.Size(); ++i)
        {
            if (l[i] != r[i])
            {
                return false;
            }
        }
        return true;
    }

}
