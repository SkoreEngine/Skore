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
#include "Allocator.hpp"
#include "Skore/Common.hpp"

namespace Skore
{


	template <typename T>
	class Span;

	template<typename T>
	class BasicByteBuffer
	{
	public:
		typedef T*       Iterator;
		typedef const T* ConstIterator;

		BasicByteBuffer() = default;
		BasicByteBuffer(const Span<T>& span);
		BasicByteBuffer(const BasicByteBuffer& other);
		BasicByteBuffer(BasicByteBuffer&& other) noexcept;

		void Reserve(usize newCapacity);
		void Resize(usize size);
		void Clear();

		usize    Size() const;
		usize    Capacity() const;
		bool     Empty() const;

		BasicByteBuffer&   operator=(const BasicByteBuffer& other);
		BasicByteBuffer&   operator=(BasicByteBuffer&& other) noexcept;
		BasicByteBuffer&   operator=(const Span<T>& span);

		void Insert(Iterator where, const T* first, const T* last);

		Iterator      begin();
		Iterator      end();
		ConstIterator begin() const;
		ConstIterator end() const;

		void Swap(BasicByteBuffer& other);
		~BasicByteBuffer();

	private:
		T* m_first{};
		T* m_last{};
		T* m_capacity{};

		Allocator* m_allocator = MemoryGlobals::GetHeapAllocator();
	};

	template <typename T>
	BasicByteBuffer<T>::BasicByteBuffer(const Span<T>& span)
	{
		Reserve(span.Size());
		Insert(begin(), span.begin(), span.end());
	}

	template <typename T>
	BasicByteBuffer<T>::BasicByteBuffer(const BasicByteBuffer& other)
	{
		Reserve(other.Size());
		Insert(begin(), other.begin(), other.end());
	}

	template <typename T>
	BasicByteBuffer<T>::BasicByteBuffer(BasicByteBuffer&& other) noexcept
	{
		m_first = other.m_first;
		m_last = other.m_last;
		m_capacity = other.m_capacity;
		m_allocator = other.m_allocator;

		other.m_first = nullptr;
		other.m_last = nullptr;
		other.m_capacity = nullptr;
	}

	template<typename T>
	void BasicByteBuffer<T>::Reserve(usize newCapacity)
	{
		if (m_first != nullptr && m_first + newCapacity <= m_capacity)
		{
			return;
		}

		const usize currentSize = m_last - m_first;

		T* newFirst = static_cast<T*>(m_allocator->MemAlloc(m_allocator->allocator, newCapacity));
		memcpy(newFirst, m_first, m_last - m_first);
		if (m_first != nullptr)
		{
			m_allocator->MemFree(m_allocator->allocator, m_first);
		}

		m_first = newFirst;
		m_last = newFirst + currentSize;
		m_capacity = newFirst + newCapacity;
	}

	template <typename T>
	typename BasicByteBuffer<T>::Iterator BasicByteBuffer<T>::begin()
	{
		return m_first;
	}

	template <typename T>
	typename BasicByteBuffer<T>::Iterator BasicByteBuffer<T>::end()
	{
		return m_last;
	}

	template <typename T>
	typename BasicByteBuffer<T>::ConstIterator BasicByteBuffer<T>::begin() const
	{
		return m_first;
	}

	template <typename T>
	typename BasicByteBuffer<T>::ConstIterator BasicByteBuffer<T>::end() const
	{
		return m_last;
	}

	template <typename T>
	void BasicByteBuffer<T>::Swap(BasicByteBuffer& other)
	{
		T* first = m_first;
		T* last = m_last;
		T* capacity = m_capacity;

		Allocator* allocator = m_allocator;

		m_first = other.m_first;
		m_last = other.m_last;
		m_capacity = other.m_capacity;
		m_allocator = other.m_allocator;

		other.m_first = first;
		other.m_last = last;
		other.m_capacity = capacity;
		other.m_allocator = allocator;
	}

	template<typename T>
	void BasicByteBuffer<T>::Resize(usize size)
	{
		Reserve(size);
		m_last = m_first + size;
	}

	template <typename T>
	void BasicByteBuffer<T>::Clear()
	{
		m_last = m_first;
	}

	template <typename T>
	usize BasicByteBuffer<T>::Size() const
	{
		return m_last - m_first;
	}

	template <typename T>
	usize BasicByteBuffer<T>::Capacity() const
	{
		return m_capacity - m_first;
	}

	template <typename T>
	bool BasicByteBuffer<T>::Empty() const
	{
		return m_last == m_first;
	}

	template <typename T>
	BasicByteBuffer<T>& BasicByteBuffer<T>::operator=(const BasicByteBuffer& other)
	{
		BasicByteBuffer(other).Swap(*this);
		return *this;
	}

	template <typename T>
	BasicByteBuffer<T>& BasicByteBuffer<T>::operator=(BasicByteBuffer&& other) noexcept
	{
		m_first = other.m_first;
		m_last = other.m_last;
		m_capacity = other.m_capacity;
		m_allocator = other.m_allocator;

		other.m_first = nullptr;
		other.m_last = nullptr;
		other.m_capacity = nullptr;	return *this;
	}

	template <typename T>
	BasicByteBuffer<T>& BasicByteBuffer<T>::operator=(const Span<T>& span)
	{
		Clear();
		Reserve(span.Size());
		Insert(begin(), span.begin(), span.end());
		return *this;
	}

	template <typename T>
	void BasicByteBuffer<T>::Insert(Iterator where, const T* first, const T* last)
	{
		if (first == last) return;

		const usize offset = where - m_first;
		const usize count = last - first;
		const usize newSize = m_last - m_first + count;

		if (m_first + newSize >= m_capacity)
		{
			Reserve(newSize * 3 / 2);
			where = m_first + offset;
		}

		T* dest = m_first + newSize - 1;

		if (where != last)
		{
			for (T* it = m_last - 1; it >= where; --it, --dest)
			{
				*dest = *it;
			}
		}
		memcpy(where, first, last - first);
		m_last = m_first + newSize;
	}

	template<typename T>
	BasicByteBuffer<T>::~BasicByteBuffer()
	{
		if (m_first)
		{
			m_allocator->MemFree(m_allocator->allocator, m_first);
		}
	}

	using ByteBuffer = BasicByteBuffer<u8>;
}
