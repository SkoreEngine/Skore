#pragma once

#include "Array.hpp"

namespace Skore
{
	template <typename T>
	class PackedArray
	{
	public:
		using Handle = uint64_t;

		Handle Insert()
		{
			Handle pIndex = m_payload.Size();

			Handle index{};
			if (m_freeSlots.Empty())
			{
				index = m_slots.Size();
				m_slots.EmplaceBack(pIndex);
			}
			else
			{
				index = m_freeSlots.Back();
				m_slots[index] = pIndex;
				m_freeSlots.PopBack();
			}

			m_payload.EmplaceBack();
			m_handlers.EmplaceBack(index);

			return index;
		}

		void Remove(Handle index)
		{
			m_slots[m_handlers[m_payload.Size() - 1]] = m_slots[index];

			m_payload[m_slots[index]] = std::move(m_payload[m_payload.Size() - 1]);
			m_handlers[m_slots[index]] = m_handlers[m_payload.Size() - 1];

			m_payload.PopBack();
			m_handlers.PopBack();

			if (index == m_slots.Size() - 1)
			{
				m_slots.PopBack();
			}
			else
			{
				m_freeSlots.EmplaceBack(index);
			}
		}

		T& operator[](Handle index)
		{
			return m_payload[m_slots[index]];
		}

		const T& operator[](Handle index) const
		{
			return m_payload[m_slots[index]];
		}

		auto begin()
		{
			return m_payload.begin();
		}

		auto end()
		{
			return m_payload.end();
		}

		auto begin() const
		{
			return m_payload.begin();
		}

		auto end() const
		{
			return m_payload.end();
		}

		bool Has(Handle index)
		{
			return index < m_slots.Size() && m_slots[index] < m_payload.Size();
		}

		usize Size() const
		{
			return m_payload.Size();
		}

	private:
		Array<T>      m_payload{};
		Array<Handle> m_handlers{};
		Array<Handle> m_slots{};
		Array<Handle> m_freeSlots{};
	};
}
