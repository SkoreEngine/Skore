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

		T* GetArray()
		{
			return m_array;
		}

	private:
		T*    m_array;
		usize m_capacity;
		usize m_front;
		usize m_rear;
		usize m_size;
	};
}
