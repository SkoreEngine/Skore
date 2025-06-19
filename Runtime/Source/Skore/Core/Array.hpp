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
#include "Traits.hpp"
#include "Allocator.hpp"
//#include "TypeInfo.hpp"
#include <initializer_list>

#include "TypeInfo.hpp"

namespace Skore
{
	template <typename T>
	class Span;

	template <typename T, usize BufferSize>
	class FixedArray;

	template <typename T>
	class Array final
	{
	public:
		typedef T*       Iterator;
		typedef const T* ConstIterator;

		typedef T value_type;

		Array() = default;
		Array(const Array& other);
		Array(Array&& other) noexcept;
		Array(usize size);
		Array(usize size, const T& value);
		Array(const T* first, const T* last);
		Array(std::initializer_list<T> initializerList);
		Array(const Span<T>& span);
		template <usize size>
		Array(const FixedArray<T, size>& arr);
		Array(Allocator* allocator);
		Array(Allocator* allocator, usize size);


		Iterator      begin();
		Iterator      end();
		ConstIterator begin() const;
		ConstIterator end() const;

		Array&   operator=(const Array& other);
		Array&   operator=(Array&& other) noexcept;
		bool     operator==(const Array& other) const;
		bool     operator!=(const Array& other) const;
		T&       operator[](usize idx);
		const T& operator[](usize idx) const;

		const T* Data() const;
		T*       Data();
		const T& Back() const;
		T&       Back();
		usize    Size() const;
		usize    Capacity() const;
		bool     Empty() const;

		void Reserve(usize newCapacity);
		void Resize(usize size);
		void Resize(usize size, const T& value);
		void Clear();
		void PopBack();
		T& PushBack(const T& value);

		void     Assign(const T* first, const T* last);
		void     Insert(Iterator where, const T& value);
		void     Insert(Iterator where, const T* first, const T* last);
		void	 Append(const T* first, usize size);
		Iterator Erase(Iterator first, Iterator last);
		Iterator Erase(Iterator first);
		void     Remove(usize index);

		template <typename... Args>
		T& EmplaceBack(Args&&... args);

		void ShrinkToFit();
		void Swap(Array& other);

		usize IndexOf(T object)
		{
			return FindFirstIndex(begin(), end(), object);
		}

		~Array();

	private:
		T* m_first = nullptr;
		T* m_last = nullptr;
		T* m_capacity = nullptr;

		Allocator* m_allocator = MemoryGlobals::GetDefaultAllocator();
	};

	template <typename T>
	template <usize size>
	Array<T>::Array(const FixedArray<T, size>& arr)
	{
		Reserve(size);
		Insert(begin(), arr.begin(), arr.end());
	}

	template <typename T>
	Array<T>::Array(Allocator* allocator) : m_allocator(allocator)
	{
	}

	template <typename T>
	Array<T>::Array(Allocator* allocator, usize size) : m_allocator(allocator)
	{
		Resize(size);
	}

	template <typename T>
	Array<T>::Array(const Span<T>& span)
	{
		Reserve(span.Size());
		Insert(begin(), span.begin(), span.end());
	}

	template <typename T>
	SK_FINLINE Array<T>::Array(const Array& other) : m_first(0), m_last(0), m_capacity(0), m_allocator(other.m_allocator)
	{
		Reserve(other.Size());
		Insert(begin(), other.begin(), other.m_last);
	}

	template <typename T>
	SK_FINLINE Array<T>::Array(Array&& other) noexcept
	{
		this->~Array();

		m_first = other.m_first;
		m_last = other.m_last;
		m_capacity = other.m_capacity;
		m_allocator = other.m_allocator;

		other.m_first = nullptr;
		other.m_last = nullptr;
		other.m_capacity = nullptr;
	}

	template <typename T>
	SK_FINLINE Array<T>::Array(usize size) : m_first(0), m_last(0), m_capacity(0)
	{
		Resize(size);
	}

	template <typename T>
	SK_FINLINE Array<T>::Array(usize size, const T& value) : m_first(0), m_last(0), m_capacity(0)
	{
		Resize(size, value);
	}

	template <typename T>
	SK_FINLINE Array<T>::Array(const T* first, const T* last) : m_first(0), m_last(0), m_capacity(0)
	{
		Insert(begin(), first, last);
	}

	template <typename T>
	Array<T>::Array(std::initializer_list<T> initializerList)
	{
		Insert(begin(), initializerList.begin(), initializerList.end());
	}

	template <typename T>
	SK_FINLINE typename Array<T>::Iterator Array<T>::begin()
	{
		return m_first;
	}

	template <typename T>
	SK_FINLINE typename Array<T>::Iterator Array<T>::end()
	{
		return m_last;
	}

	template <typename T>
	SK_FINLINE typename Array<T>::ConstIterator Array<T>::begin() const
	{
		return m_first;
	}

	template <typename T>
	SK_FINLINE typename Array<T>::ConstIterator Array<T>::end() const
	{
		return m_last;
	}

	template <typename T>
	Array<T>& Array<T>::operator=(const Array& other)
	{
		Array(other).Swap(*this);
		return *this;
	}

	template <typename T>
	Array<T>& Array<T>::operator=(Array&& other) noexcept
	{
		this->~Array();

		m_first = other.m_first;
		m_last = other.m_last;
		m_capacity = other.m_capacity;
		m_allocator = other.m_allocator;

		other.m_first = nullptr;
		other.m_last = nullptr;
		other.m_capacity = nullptr;

		return *this;
	}

	template <typename T>
	SK_FINLINE T& Array<T>::operator[](usize idx)
	{
		return m_first[idx];
	}

	template <typename T>
	SK_FINLINE const T& Array<T>::operator[](usize idx) const
	{
		return m_first[idx];
	}

	template <typename T>
	bool Array<T>::operator==(const Array& other) const
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

	template <typename T>
	bool Array<T>::operator!=(const Array& other) const
	{
		return !((*this) == other);
	}

	template <typename T>
	SK_FINLINE const T& Array<T>::Back() const
	{
		return m_last[-1];
	}

	template <typename T>
	SK_FINLINE T& Array<T>::Back()
	{
		return m_last[-1];
	}

	template <typename T>
	SK_FINLINE const T* Array<T>::Data() const
	{
		return m_first;
	}

	template <typename T>
	SK_FINLINE T* Array<T>::Data()
	{
		return m_first;
	}

	template <typename T>
	SK_FINLINE usize Array<T>::Size() const
	{
		return m_last - m_first;
	}

	template <typename T>
	SK_FINLINE usize Array<T>::Capacity() const
	{
		return m_capacity - m_first;
	}

	template <typename T>
	SK_FINLINE bool Array<T>::Empty() const
	{
		return m_last == m_first;
	}

	template <typename T>
	SK_FINLINE void Array<T>::Reserve(usize newCapacity)
	{
		if (m_first + newCapacity <= m_capacity)
		{
			return;
		}

		const usize size = m_last - m_first;
		T*          newFirst = (T*)m_allocator->MemAlloc(m_allocator->allocator, sizeof(T) * newCapacity);
		T*          dest = newFirst;

		for (T* it = m_first; it != m_last; ++it, ++dest)
		{
			new(PlaceHolder(), dest) T(Traits::Forward<T>(*it));
			it->~T();
		}

		m_allocator->MemFree(m_allocator->allocator, m_first);

		m_first = newFirst;
		m_last = newFirst + size;
		m_capacity = newFirst + newCapacity;
	}

	template <typename T>
	SK_FINLINE void Array<T>::Resize(usize size)
	{
		Reserve(size);

		for (T* it = m_last; it < m_first + size; ++it)
		{
			new(PlaceHolder(), it) T{};
		}

		for (T* it = m_first + size; it < m_last; ++it)
		{
			it->~T();
		}

		m_last = m_first + size;
	}

	template <typename T>
	SK_FINLINE void Array<T>::Resize(usize size, const T& value)
	{
		Reserve(size);

		for (T* it = m_last; it < m_first + size; ++it)
		{
			new(PlaceHolder(), it) T(value);
		}

		for (T* it = m_first + size; it < m_last; ++it)
		{
			it->~T();
		}

		m_last = m_first + size;
	}

	template <typename T>
	template <typename... Args>
	SK_FINLINE T& Array<T>::EmplaceBack(Args&&... args)
	{
		T* where = m_last;

		if (m_last == m_capacity)
		{
			Reserve((m_last - m_first) * 3 / 2 + 1);
			where = m_first + (m_last - m_first);
		}

		++m_last;

		if constexpr (Traits::IsAggregate<T>)
		{
			new(PlaceHolder(), where) T{Traits::Forward<Args>(args)...};
		}
		else
		{
			new(PlaceHolder(), where) T(Traits::Forward<Args>(args)...);
		}
		return *where;
	}

	template <typename T>
	SK_FINLINE void Array<T>::Clear()
	{
		for (T* it = m_first; it < m_last; ++it)
		{
			it->~T();
		}
		m_last = m_first;
	}

	template <typename T>
	SK_FINLINE void Array<T>::PopBack()
	{
		T* where = m_last - 1;
		where->~T();
		--m_last;
	}

	template <typename T>
	T& Array<T>::PushBack(const T& value)
	{
		T* where = m_last;

		if (m_last == m_capacity)
		{
			Reserve((m_last - m_first) * 3 / 2 + 1);
			where = m_first + (m_last - m_first);
		}

		++m_last;

		if constexpr (Traits::IsAggregate<T>)
		{
			new(PlaceHolder(), where) T{value};
		}
		else
		{
			new(PlaceHolder(), where) T(value);
		}
		return *where;
	}

	template <typename T>
	void Array<T>::Insert(Iterator where, const T& value)
	{
		Insert(where, &value, &value + 1);
	}

	template <typename T>
	SK_FINLINE void Array<T>::Insert(Array::Iterator where, const T* first, const T* last)
	{
		if (first == last) return;

		const usize offset = where - m_first;
		const usize count = last - first;
		const usize newSize = ((m_last - m_first) + count);
		if (m_first + newSize >= m_capacity)
		{
			Reserve((newSize * 3) / 2);
			where = m_first + offset;
		}

		T* dest = m_first + newSize - 1;
		if (where != last)
		{
			for (T* it = m_last - 1; it >= where; --it, --dest)
			{
				new(PlaceHolder(), dest) T(Traits::Forward<T>(*it));
			}
		}

		for (; first != last; ++first, ++where)
		{
			new(PlaceHolder(), where) T(*first);
		}
		m_last = m_first + newSize;
	}

	template <typename T>
	void Array<T>::Append(const T* first, usize size)
	{
		Insert(m_last, first, first + size);
	}

	template <typename T>
	void Array<T>::Assign(const T* first, const T* last)
	{
		Clear();
		Insert(m_last, first, last);
	}

	template <typename T>
	typename Array<T>::Iterator Array<T>::Erase(Iterator first, Iterator last)
	{
		const usize count = (last - first);

		for (T* it = first; it != last; ++it)
		{
			it->~T();
		}

		T* it = last;
		T* end = m_last;

		for (T* dest = first; it != end; ++it, ++dest)
		{
			new(PlaceHolder(), dest) T(Traits::Forward<T>(*it));
		}

		m_last -= count;

		return first;
	}

	template <typename T>
	typename Array<T>::Iterator Array<T>::Erase(Iterator first)
	{
		return Erase(first, first + 1);
	}

	template <typename T>
	void Array<T>::Remove(usize index)
	{
		Erase(begin() + index, begin() + index + 1);
	}

	template <typename T>
	SK_FINLINE void Array<T>::ShrinkToFit()
	{
		if (m_capacity != m_last)
		{
			if (m_last == m_first)
			{
				m_allocator->MemFree(m_allocator->allocator, m_first);
				m_capacity = m_first = m_last = nullptr;
			}
			else
			{
				const usize size = m_last - m_first;
				T*          newFirst = (T*)m_allocator->MemAlloc(m_allocator->allocator, sizeof(T) * size);
				T*          dest = newFirst;

				for (T* it = m_first; it != m_last; ++it, ++dest)
				{
					new(PlaceHolder(), dest) T(Traits::Forward<T>(*it));
					it->~T();
				}

				m_allocator->MemFree(m_allocator->allocator, m_first);
				m_first = newFirst;
				m_last = newFirst + size;
				m_capacity = m_last;
			}
		}
	}

	template <typename T>
	SK_FINLINE void Array<T>::Swap(Array& other)
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

	template <typename T>
	Array<T>::~Array()
	{
		if (m_first)
		{
			for (auto it = m_first; it < m_last; ++it)
			{
				it->~T();
			}
			m_allocator->MemFree(m_allocator->allocator, m_first);
		}
	}

	struct ArrayApi
	{
		usize (*Size)(ConstPtr instance);
		VoidPtr (*Get)(VoidPtr instance, usize size);
		void (*Set)(VoidPtr instance, usize index, ConstPtr value);
		TypeProps (*GetProps)();
		VoidPtr (*Create)();
		void (*Destroy)(VoidPtr);
		void (*Copy)(VoidPtr desc, ConstPtr src);
		VoidPtr (*PushNew)(VoidPtr instance);
		void (*PopBack)(VoidPtr instance);
	};

	template<typename Type>
	struct TypeApi<Array<Type>>
	{
		static void GetApi(VoidPtr pointer)
		{
			new(PlaceHolder{}, pointer) ArrayApi{};
			ArrayApi& arrayApi = *static_cast<ArrayApi*>(pointer);
			arrayApi.Size = [](ConstPtr instance)
			{
				return static_cast<const Array<Type>*>(instance)->Size();
			};

			arrayApi.Get = [](VoidPtr instance, usize size)
			{
				Array<Type>& arr = *static_cast<Array<Type>*>(instance);
				return static_cast<VoidPtr>(&arr[size]);
			};

			arrayApi.Set = [](VoidPtr instance, usize index, ConstPtr value)
			{
				Array<Type>& arr = *static_cast<Array<Type>*>(instance);
				arr[index] = *static_cast<const Type*>(value);
			};

			arrayApi.GetProps = []
			{
				return TypeInfo<Type>::GetProps();
			};

			arrayApi.Create = []
			{
				return static_cast<VoidPtr>(Alloc<Array<Type>>());
			};

			arrayApi.Destroy = [](VoidPtr instance)
			{
				DestroyAndFree(static_cast<Array<Type>*>(instance));
			};

			arrayApi.Copy = [](VoidPtr desc, ConstPtr src)
			{
				*static_cast<Array<Type>*>(desc) = *static_cast<const Array<Type>*>(src);
			};

			arrayApi.PushNew = [](VoidPtr instance)
			{
				Array<Type>& arr = *static_cast<Array<Type>*>(instance);
				return static_cast<VoidPtr>(&arr.EmplaceBack());
			};

			arrayApi.PopBack = [](VoidPtr instance)
			{
				Array<Type>& arr = *static_cast<Array<Type>*>(instance);
				arr.PopBack();
			};
		}

		static constexpr TypeID GetApiId()
		{
			return TypeInfo<ArrayApi>::ID();
		}
	};
}
