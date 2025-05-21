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

//TODO replace with allocator.

namespace Skore
{
	template <typename T>
	class Queue
	{
		void Resize(usize newCapacity)
		{
			T* newArray =  new T[newCapacity];

			for (int i = 0; i < m_size; i++)
			{
				auto x = (m_front + i) % m_capacity;
				newArray[i] = Traits::Move(m_array[x]);
			}

			delete[] m_array;

			m_array = newArray;
			m_capacity = newCapacity;
			m_front = 0;
			m_rear = m_size - 1;
		}

	public:

		Queue(int capacity = 10)
		{
			m_capacity = capacity;
			m_array =  new T[m_capacity];
			m_front = 0;
			m_rear = -1;
			m_size = 0;
		}

		~Queue()
		{
			delete[] m_array;
		}

		void Enqueue(const T& args)
		{
			if (IsFull())
			{
				Resize(m_capacity * 2);
			}

			m_rear = (m_rear + 1) % m_capacity;
			new (PlaceHolder{}, &m_array[m_rear]) T{args};
			m_size++;
		}

		T Dequeue()
		{
			if (IsEmpty())
			{
				SK_ASSERT(false, "Queue is empty");
			}

			T item = m_array[m_front];
			m_front = (m_front + 1) % m_capacity;
			m_size--;


			if (m_size > 0 && m_size < m_capacity / 4)
			{
				Resize(m_capacity / 2);
			}

			return item;
		}

		T Peek() const
		{
			if (IsEmpty())
			{
				SK_ASSERT(false, "Queue is empty");
			}

			return m_array[m_front];
		}

		bool IsEmpty() const
		{
			return m_size == 0;
		}

		bool IsFull() const
		{
			return m_size == m_capacity;
		}

		int GetSize() const
		{
			return m_size;
		}

		int GetCapacity() const
		{
			return m_capacity;
		}

	private:
		T*    m_array;
		usize m_capacity;
		usize m_front;
		usize m_rear;
		usize m_size;
	};
}
