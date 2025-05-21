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
    class Span;

    template<typename T, usize bufferSize>
	class FixedArray
	{
	public:
		typedef T ValueType;
		typedef T      * Iterator;
		typedef const T* ConstIterator;

		FixedArray() = default;

		FixedArray(const FixedArray&) = default;

		constexpr FixedArray(std::initializer_list<T> initializerList)
		{
			SK_ASSERT(initializerList.size() <= bufferSize, "initializerList larger than bufferSize");
			auto      dest = begin();
			for (auto it   = initializerList.begin(); it != initializerList.end(); ++it)
			{
				*(dest++) = *it;
			}
		};

        constexpr FixedArray(Span<T> span)
        {
            SK_ASSERT(span.Size() <= bufferSize, "Span larger than bufferSize");
            Insert(m_array, span.begin(), span.end());
        }

		constexpr const T* Data() const
		{
			return begin();
		}

		constexpr Iterator begin()
		{
			return m_array;
		}

		constexpr Iterator end()
		{
			return m_array + bufferSize;
		}

		constexpr ConstIterator begin() const
		{
			return m_array;
		}

		constexpr ConstIterator end() const
		{
			return m_array + bufferSize;
		}

		constexpr usize Size() const
		{
			return bufferSize;
		}

		bool Empty() const
		{
			return Size() == 0;
		}

		T& operator[](usize idx)
		{
			return begin()[idx];
		}

		constexpr const T& Back() const
		{
			return *begin()[Size() - 1];
		}

		constexpr T& Back()
		{
			return begin()[Size() - 1];
		}

	private:
		T m_array[bufferSize] = {};
	};

}