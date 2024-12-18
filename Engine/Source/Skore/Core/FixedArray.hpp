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