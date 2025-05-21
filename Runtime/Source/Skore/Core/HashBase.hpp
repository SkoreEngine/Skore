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

namespace Skore
{
	template<typename Key, typename Value>
	struct HashNode
	{
		const Key first;
		Value     second;

		HashNode* next;
		HashNode* prev;
	};

	template<typename Key>
	struct HashNode<Key, void>
	{
		const Key first;

		HashNode* next;
		HashNode* prev;
	};


	template<typename T>
	struct HashIterator
	{
		T* node{};

		T* operator->() const;
		T& operator*() const;
		explicit operator bool() const noexcept;
	};


	template<typename T>
	struct HashIterator<const T>
	{
		HashIterator() = default;

		HashIterator(T* other) : node(other)
		{
		}

		HashIterator(HashIterator<T> other) : node(other.node)
		{
		}

		T* operator->() const;
		T& operator*() const;
		explicit operator bool() const noexcept;
		T* node{};
	};


	template<typename Key>
	struct HashIterator<HashNode<Key, void>>
	{
		HashNode<Key, void>* node{};

		const Key* operator->() const;
		const Key& operator*() const;
		explicit operator bool() const noexcept;
	};

	template<typename Key>
	struct HashIterator<const HashNode<Key, void>>
	{
		HashNode<Key, void>* node{};

		const Key* operator->() const;
		const Key& operator*() const;
		explicit operator bool() const noexcept;
	};

	template<typename Node>
	inline Node* HashIterator<Node>::operator->() const
	{
		return node;
	}

	template<typename Node>
	inline Node& HashIterator<Node>::operator*() const
	{
		return *node;
	}
	template<typename Node>
	inline Node* HashIterator<const Node>::operator->() const
	{
		return node;
	}

	template<typename Node>
	inline Node& HashIterator<const Node>::operator*() const
	{
		return *node;
	}

	template<typename Node>
	static inline void operator++(HashIterator<Node>& lhs)
	{
		lhs.node = lhs.node->next;
	}

	template<typename LNode, typename RNode>
	static inline bool operator==(const HashIterator<LNode>& lhs, const HashIterator<RNode>& rhs)
	{
		return lhs.node == rhs.node;
	}

	template<typename LNode, typename RNode>
	static inline bool operator!=(const HashIterator<LNode>& lhs, const HashIterator<RNode>& rhs)
	{
		return lhs.node != rhs.node;
	}

	template<typename Node>
	HashIterator<Node>::operator bool() const noexcept
	{
		return node != nullptr;
	}

	template<typename Node>
	HashIterator<const Node>::operator bool() const noexcept
	{
		return node != nullptr;
	}

	template <typename Key>
	const Key* HashIterator<HashNode<Key, void>>::operator->() const
	{
		return node->first;
	}

	template <typename Key>
	const Key& HashIterator<HashNode<Key, void>>::operator*() const
	{
		return node->first;
	}

	template <typename Key>
	HashIterator<HashNode<Key, void>>::operator bool() const noexcept
	{
		return node != nullptr;
	}


	template <typename Key>
	const Key* HashIterator<const HashNode<Key, void>>::operator->() const
	{
		return node->first;
	}

	template <typename Key>
	const Key& HashIterator<const HashNode<Key, void>>::operator*() const
	{
		return node->first;
	}

	template <typename Key>
	HashIterator<const HashNode<Key, void>>::operator bool() const noexcept
	{
		return node != nullptr;
	}

	template<typename Key, typename Value>
	static inline void HashNodeErase(const HashNode<Key, Value>* where, usize hash, HashNode<Key, Value>** buckets, usize sizeBuckets)
	{
		usize bucket = hash & (sizeBuckets - 1);
		HashNode<Key, Value>* next = where->next;
		for (; buckets[bucket] == where; --bucket)
		{
			buckets[bucket] = next;
			if (!bucket)
			{
				break;
			}
		}

		if (where->prev)
		{
			where->prev->next = where->next;
		}

		if (next)
		{
			next->prev = where->prev;
		}
	}


	template<typename Key, typename Value>
	static inline void HashNodeInsert(HashNode<Key, Value>* node, usize hash, HashNode<Key, Value>** buckets, usize sizeBuckets)
	{
		usize bucket = hash & (sizeBuckets - 1);
		HashNode<Key, Value>* it = buckets[bucket + 1];
		node->next = it;
		if (it)
		{
			node->prev           = it->prev;
			it->prev             = node;
			if (node->prev)
				node->prev->next = node;
		}
		else
		{
			usize newBucket = bucket;
			while (newBucket && !buckets[newBucket])
			{
				--newBucket;
			}

			HashNode<Key, Value>* prev = buckets[newBucket];
			while (prev && prev->next)
			{
				prev = prev->next;
			}

			node->prev = prev;
			if (prev)
			{
				prev->next = node;
			}
		}

		// propagate node through buckets
		for (; it == buckets[bucket]; --bucket)
		{
			buckets[bucket] = node;
			if (!bucket)
				break;
		}
	}

}
