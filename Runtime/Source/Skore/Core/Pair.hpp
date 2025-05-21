
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

#include "Traits.hpp"

namespace Skore
{
	template<typename Key, typename Value>
	struct Pair
	{
		Key   first{};
		Value second{};

		Pair();
		Pair(const Pair& other);
		Pair(Pair&& other);
		Pair(const Key& key, const Value& value);
		Pair(Key&& key, Value&& value);

		Pair& operator=(const Pair& other);
		Pair& operator=(Pair&& other) noexcept;
	};

	template<typename Key, typename Value>
	SK_FINLINE Pair<Key, Value>::Pair()
	{
	}

	template<typename Key, typename Value>
	SK_FINLINE Pair<Key, Value>::Pair(const Pair& other) : first(other.first), second(other.second)
	{

	}

	template<typename Key, typename Value>
	SK_FINLINE Pair<Key, Value>::Pair(Pair&& other) : first(static_cast<Key&&>(other.first)), second(static_cast<Value&&>(other.second))
	{
	}

	template<typename Key, typename Value>
	SK_FINLINE Pair<Key, Value>::Pair(const Key& key, const Value& value) : first(key), second(value)
	{

	}

	template<typename Key, typename Value>
	SK_FINLINE Pair<Key, Value>::Pair(Key&& key, Value&& value) : first(static_cast<Key&&>(key)), second(static_cast<Value&&>(value))
	{
	}

	template<typename Key, typename Value>
	SK_FINLINE Pair<Key, Value>& Pair<Key, Value>::operator=(const Pair& other)
	{
		first  = other.first;
		second = other.second;
		return *this;
	}

	template<typename Key, typename Value>
	SK_FINLINE Pair<Key, Value>& Pair<Key, Value>::operator=(Pair&& other) noexcept
	{
		first  = static_cast<Key&&>(other.first);
		second = static_cast<Value&&>(other.second);
		return *this;
	}

	template<typename Key, typename Value>
	constexpr Pair<Traits::RemoveReference<Key>, Traits::RemoveReference<Value>> MakePair(Key&& key, Value&& value)
	{
		return Pair<Traits::RemoveReference<Key>, Traits::RemoveReference<Value>>(Traits::Forward<Key>(key), Traits::Forward<Value>(value));
	}

	template<typename Key, typename Value>
	constexpr Pair<Traits::RemoveReference<Key>, Traits::RemoveReference<Value>> MakePair(const Key& key, const Value& value)
	{
		return Pair<Traits::RemoveReference<Key>, Traits::RemoveReference<Value>>(key, value);
	}
}