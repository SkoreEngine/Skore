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

#include "Pair.hpp"
#include "HashBase.hpp"
#include "Array.hpp"

namespace Skore
{
	template<typename Key>
	class HashSet
	{
	public:
		typedef Pair<Key, void>          ValueType;
		typedef HashNode<Key, void>      Node;
		typedef HashIterator<Node>       Iterator;
		typedef HashIterator<const Node> ConstIterator;

		HashSet();
		HashSet(const HashSet& other);
		HashSet(HashSet&& other) noexcept;
		HashSet& operator=(const HashSet& other);

		Iterator begin();
		Iterator end();
		ConstIterator begin() const;
		ConstIterator end() const;

		void    Clear();
		bool    Empty() const;
		usize   Size() const;

		template<typename ParamKey>
		Iterator Find(const ParamKey& key);

		template<typename ParamKey>
		ConstIterator Find(const ParamKey& key) const;

		void Erase(Iterator where);

		template<typename ParamKey>
		void Erase(const ParamKey& key);

		Pair<Iterator, bool> Insert(const Key& key);
		Pair<Iterator, bool> Emplace(const Key& key);

		template<typename ParamKey>
		bool Has(const ParamKey& key) const;

		void Swap(HashSet& other);

		bool     operator==(const HashSet& other) const;
		bool     operator!=(const HashSet& other) const;

		~HashSet();


	private:
		void ReHash();

		usize        m_size{};
		Array<Node*> m_buckets{};
		Allocator*   m_allocator = MemoryGlobals::GetDefaultAllocator();
	};

	template<typename Key>
	HashSet<Key>::HashSet() = default;

	template<typename Key>
	HashSet<Key>::HashSet(const HashSet& other) : m_size(other.m_size)
	{
		if (other.m_buckets.Empty()) return;
		m_buckets.Resize(other.m_buckets.Size());

		for (Node* it = *other.m_buckets.begin(); it != nullptr; it = it->next)
		{
			Node* newNode = new(PlaceHolder(), m_allocator->MemAlloc(m_allocator->allocator, sizeof(Node))) Node{it->first};
			newNode->next = newNode->prev = nullptr;
			HashNodeInsert(newNode, Hash<Key>::Value(it->first), m_buckets.Data(), m_buckets.Size() - 1);
		}
	}

	template<typename Key>
	HashSet<Key>::HashSet(HashSet&& other) noexcept
	{
		m_buckets.Swap(other.m_buckets);
		other.m_size = 0;
	}

	template <typename Key>
	HashSet<Key>& HashSet<Key>::operator=(const HashSet& other)
	{
		HashSet(other).Swap(*this);
		return *this;
	}

	template<typename Key>
    typename HashSet<Key>::Iterator HashSet<Key>::begin()
	{
		Iterator it;
		it.node = !m_buckets.Empty() ? *m_buckets.begin() : nullptr;
		return it;
	}

	template<typename Key>
    typename HashSet<Key>::Iterator HashSet<Key>::end()
	{
		Iterator it;
		it.node = nullptr;
		return it;
	}

	template<typename Key>
    typename HashSet<Key>::ConstIterator HashSet<Key>::begin() const
	{
		ConstIterator it;
		it.node = !m_buckets.Empty() ? *m_buckets.begin() : nullptr;
		return it;
	}

	template<typename Key>
    typename HashSet<Key>::ConstIterator HashSet<Key>::end() const
	{
		ConstIterator it;
		it.node = nullptr;
		return it;
	}

	template<typename Key>
	void HashSet<Key>::Clear()
	{
		if (m_buckets.Empty()) return;

		Node* it = *m_buckets.begin();
		while (it)
		{
			Node* next = it->next;
			it->~HashNode<Key, void>();
			m_allocator->MemFree(m_allocator->allocator, it);
			it = next;
		}

		m_buckets.Clear();
		m_buckets.ShrinkToFit();
		m_size = 0;
	}

	template<typename Key>
	bool HashSet<Key>::Empty() const
	{
		return m_size == 0;
	}

	template<typename Key>
	usize HashSet<Key>::Size() const
	{
		return m_size;
	}

	template<typename Key>
	template<typename ParamKey>
    typename HashSet<Key>::Iterator HashSet<Key>::Find(const ParamKey& key)
	{
		if (m_buckets.Empty()) return {};

		const usize bucket = Hash<Key>::Value(key) & (m_buckets.Size() - 2);

		typedef Node* NodePtr;
		for (NodePtr it = m_buckets[bucket], end = m_buckets[bucket + 1]; it != end; it = it->next)
		{
			if (it->first == key)
			{
				return {it};
			}
		}

		return {};
	}

	template<typename Key>
	template<typename ParamKey>
	typename HashSet<Key>::ConstIterator HashSet<Key>::Find(const ParamKey& key) const
	{
		if (m_buckets.Empty()) return {};

		const usize bucket = Hash<Key>::Value(key) & (m_buckets.Size() - 2);

		typedef Node* NodePtr;
		for (NodePtr it = m_buckets[bucket], end = m_buckets[bucket + 1]; it != end; it = it->next)
		{
			if (it->first == key)
			{
				return {it};
			}
		}

		return {};
	}

	template<typename Key>
	void HashSet<Key>::Erase(HashSet::Iterator where)
	{
		HashNodeErase(where.node, Hash<Key>::Value(where.node->first), m_buckets.Data(), m_buckets.Size() - 1);
		where.node->~HashNode();
		m_allocator->MemFree(m_allocator->allocator, where.node);
		--m_size;
	}

	template<typename Key>
	template<typename ParamKey>
	void HashSet<Key>::Erase(const ParamKey& key)
	{
		Iterator it = Find(key);
		if (it)
		{
			Erase(it);
		}
	}

	template<typename Key>
	Pair<typename HashSet<Key>::Iterator, bool> HashSet<Key>::Insert(const Key& key)
	{
		Pair<Iterator, bool> result{};
		result.second = false;
		result.first  = Find(key);
		if (result.first.node != nullptr)
		{
			return result;
		}

		Node* newNode = new(PlaceHolder(), m_allocator->MemAlloc(m_allocator->allocator,  sizeof(Node))) Node{key};
		newNode->next = newNode->prev = nullptr;

		if (m_buckets.Empty())
		{
			m_buckets.Resize(9);
		}

		HashNodeInsert(newNode, Hash<Key>::Value(key), m_buckets.Data(), m_buckets.Size() - 1);

		++m_size;

		ReHash();

		result.first.node = newNode;
		result.second     = true;
		return result;
	}

	template<typename Key>
	Pair<typename HashSet<Key>::Iterator, bool> HashSet<Key>::Emplace(const Key& key)
	{
		return Insert(key);
	}

	template<typename Key>
	template<typename ParamKey>
	bool HashSet<Key>::Has(const ParamKey& key) const
	{
		if (m_buckets.Empty()) return false;
		const usize bucket = Hash<Key>::Value(key) & (m_buckets.Size() - 2);
		typedef Node* NodePtr;
		for (NodePtr it = m_buckets[bucket], end = m_buckets[bucket + 1]; it != end; it = it->next)
		{
			if (it->first == key)
			{
				return true;
			}
		}
		return false;
	}

	template <typename Key>
	void HashSet<Key>::Swap(HashSet& other)
	{
		m_buckets.Swap(other.m_buckets);
		usize size = other.m_size;
		other.m_size = this->m_size;
		m_size = size;
	}

	template <typename Key>
	bool HashSet<Key>::operator==(const HashSet& other) const
	{
		return m_buckets == other.m_buckets;
	}

	template <typename Key>
	bool HashSet<Key>::operator!=(const HashSet& other) const
	{
		return m_buckets != other.m_buckets;
	}

	template<typename Key>
	HashSet<Key>::~HashSet()
	{
		Clear();
	}

	template<typename Key>
	void HashSet<Key>::ReHash()
	{
		if (m_size + 1 > 4 * m_buckets.Size())
		{
			Node* root = *m_buckets.begin();
			const usize newNumberBuckets = (m_buckets.Size() - 1) * 8;

			m_buckets.Clear();
			m_buckets.Resize(newNumberBuckets + 1);

			while (root)
			{
				Node*  next = root->next;
				root->next = root->prev = 0;
				HashNodeInsert(root, Hash<Key>::Value(root->first), m_buckets.Data(), m_buckets.Size() - 1);
				root = next;
			}
		}
	}
}