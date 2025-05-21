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

#include <atomic>
#include <utility>
#include <type_traits>
#include "Traits.hpp"
#include "Allocator.hpp"

namespace Skore
{
	template <typename T>
	class Ref
	{
	public:
		Ref() noexcept : ptr(nullptr), controlBlock(nullptr) {}

		explicit Ref(T* rawPtr, Allocator* allocator = MemoryGlobals::GetDefaultAllocator()) 
			: ptr(rawPtr), controlBlock(rawPtr ? allocator->Alloc<ControlBlock>(allocator) : nullptr) {}


		Ref(Traits::NullPtr)
		{
		}

		Ref(const Ref& other) : ptr(other.ptr), controlBlock(other.controlBlock)
		{
			if (controlBlock)
			{
				controlBlock->refCount.fetch_add(1);
			}
		}

		template <typename U, typename = Traits::EnableIf<Traits::IsConvertible<U, T>::Value>>
		Ref(const Ref<U>& other) : ptr(other.Get()), controlBlock(nullptr)
		{
			if (other.Get() != nullptr)
			{
				// Cast the other ref to our type to access its control block
				auto& otherAsThis = reinterpret_cast<const Ref<T>&>(other);
				controlBlock = otherAsThis.controlBlock;
				if (controlBlock)
				{
					controlBlock->refCount.fetch_add(1);
				}
			}
		}

		Ref(Ref&& other) noexcept : ptr(other.ptr), controlBlock(other.controlBlock)
		{
			other.ptr = nullptr;
			other.controlBlock = nullptr;
		}

		template <typename U, typename = Traits::EnableIf<Traits::IsConvertible<U, T>::Value>>
		Ref(Ref<U>&& other) noexcept : ptr(other.Get()), controlBlock(nullptr)
		{
			if (other.Get() != nullptr)
			{
				// Cast the other ref to our type to access its control block
				auto& otherAsThis = reinterpret_cast<Ref<T>&>(other);
				controlBlock = otherAsThis.controlBlock;

				other.ptr = nullptr;
				otherAsThis.controlBlock = nullptr;
			}
		}

		Ref& operator=(const Ref& other)
		{
			if (this != &other)
			{
				Release();

				ptr = other.ptr;
				controlBlock = other.controlBlock;
				if (controlBlock)
				{
					controlBlock->refCount.fetch_add(1);
				}
			}
			return *this;
		}

		template <typename U, typename = typename Traits::EnableIf<Traits::IsConvertible<U*, T*>::Value>::Type>
		Ref& operator=(const Ref<U>& other)
		{
			if (reinterpret_cast<const void*>(this) != reinterpret_cast<const void*>(&other))
			{
				Release();

				ptr = other.Get();
				if (ptr != nullptr)
				{
					// Cast the other ref to our type to access its control block
					auto& otherAsThis = reinterpret_cast<const Ref<T>&>(other);
					controlBlock = otherAsThis.controlBlock;
					if (controlBlock)
					{
						controlBlock->refCount.fetch_add(1);
					}
				}
				else
				{
					controlBlock = nullptr;
				}
			}
			return *this;
		}

		Ref& operator=(Ref&& other) noexcept
		{
			if (this != &other)
			{
				Release();

				ptr = other.ptr;
				controlBlock = other.controlBlock;

				other.ptr = nullptr;
				other.controlBlock = nullptr;
			}
			return *this;
		}

		template <typename U, typename = typename Traits::EnableIf<Traits::IsConvertible<U*, T*>::Value>::Type>
		Ref& operator=(Ref<U>&& other) noexcept
		{
			if (reinterpret_cast<const void*>(this) != reinterpret_cast<const void*>(&other))
			{
				Release();

				ptr = other.Get();
				if (ptr != nullptr)
				{
					// Cast the other ref to our type to access its control block
					auto& otherAsThis = reinterpret_cast<Ref<T>&>(other);
					controlBlock = otherAsThis.controlBlock;

					other.ptr = nullptr;
					otherAsThis.controlBlock = nullptr;
				}
				else
				{
					controlBlock = nullptr;
				}
			}
			return *this;
		}

		~Ref()
		{
			Release();
		}

		T& operator*() const noexcept
		{
			return *ptr;
		}

		T* operator->() const noexcept
		{
			return ptr;
		}

		T* Get() const noexcept
		{
			return ptr;
		}

		void Reset(T* newPtr = nullptr, Allocator* allocator = MemoryGlobals::GetDefaultAllocator())
		{
			Release();
			ptr = newPtr;
			controlBlock = newPtr ? allocator->Alloc<ControlBlock>(allocator) : nullptr;
		}

		size_t UseCount() const noexcept
		{
			return controlBlock ? controlBlock->refCount.load() : 0;
		}

		bool operator==(Traits::NullPtr) const noexcept
		{
			return this->ptr == nullptr;
		}

		bool operator!=(Traits::NullPtr) noexcept
		{
			return this->ptr != nullptr;
		}

		explicit operator bool() const noexcept
		{
			return ptr != nullptr;
		}

		template <typename U>
		bool operator==(const Ref<U>& ref) const noexcept
		{
			return this->ptr == ref.ptr;
		}

		template <typename U>
		bool operator!=(const Ref<U>& ref) const noexcept
		{
			return this->ptr != ref.ptr;
		}

		template <typename U>
		bool operator==(U* ptrOther) const noexcept
		{
			return this->ptr == ptrOther;
		}

		void Swap(Ref& other) noexcept
		{
			std::swap(ptr, other.ptr);
			std::swap(controlBlock, other.controlBlock);
		}

	private:
		struct ControlBlock
		{
			std::atomic<size_t> refCount;
			Allocator*          allocator;
			explicit            ControlBlock(Allocator* alloc) : refCount(1), allocator(alloc) {}
		};

		T*            ptr = nullptr;
		ControlBlock* controlBlock = nullptr;

		template <typename U>
		static Ref CreateFromRawWithExistingRef(T* rawPtr, const Ref<U>& existingRef)
		{
			Ref<T> newRef;
			newRef.ptr = rawPtr;

			auto& sourceAsVoid = reinterpret_cast<const Ref<T>&>(existingRef);
			newRef.controlBlock = sourceAsVoid.controlBlock;

			if (newRef.controlBlock)
			{
				newRef.controlBlock->refCount.fetch_add(1);
			}

			return newRef;
		}

		template <typename U, typename V>
		friend Ref<U> StaticPointerCast(const Ref<V>&) noexcept;

		template <typename U, typename V>
		friend Ref<U> DynamicPointerCast(const Ref<V>&) noexcept;

		template <typename U, typename V>
		friend Ref<U> ConstPointerCast(const Ref<V>&) noexcept;

		template <typename U, typename V>
		friend Ref<U> ReinterpretPointerCast(const Ref<V>&) noexcept;

		template <typename U>
		friend class Ref;

		void Release()
		{
			if (ptr && controlBlock)
			{
				if (controlBlock->refCount.fetch_sub(1) == 1)
				{
					Allocator* allocator = controlBlock->allocator;
					allocator->DestroyAndFree(ptr);
					allocator->DestroyAndFree(controlBlock);
				}
			}
		}
	};

	template <typename T>
	void Swap(Ref<T>& a, Ref<T>& b) noexcept
	{
		a.Swap(b);
	}

	template <typename T, typename U>
	Ref<T> StaticPointerCast(const Ref<U>& source) noexcept
	{
		if (source.Get() == nullptr)
		{
			return Ref<T>();
		}

		T* castedPtr = static_cast<T*>(source.Get());
		return Ref<T>::CreateFromRawWithExistingRef(castedPtr, source);
	}

	template <typename T, typename U>
	Ref<T> DynamicPointerCast(const Ref<U>& source) noexcept
	{
		if (source.Get() == nullptr)
		{
			return Ref<T>();
		}

		T* castedPtr = dynamic_cast<T*>(source.Get());
		if (castedPtr == nullptr)
		{
			return Ref<T>();
		}

		return Ref<T>::CreateFromRawWithExistingRef(castedPtr, source);
	}

	template <typename T, typename U>
	Ref<T> ConstPointerCast(const Ref<U>& source) noexcept
	{
		if (source.Get() == nullptr)
		{
			return Ref<T>();
		}

		T* castedPtr = const_cast<T*>(source.Get());
		return Ref<T>::CreateFromRawWithExistingRef(castedPtr, source);
	}

	template <typename T, typename U>
	Ref<T> ReinterpretPointerCast(const Ref<U>& source) noexcept
	{
		if (source.Get() == nullptr)
		{
			return Ref<T>();
		}

		T* castedPtr = reinterpret_cast<T*>(source.Get());
		return Ref<T>::CreateFromRawWithExistingRef(castedPtr, source);
	}

	template <typename T, typename... Args>
	static Ref<T> MakeRef(Args&&... args)
	{
		Allocator* allocator = MemoryGlobals::GetDefaultAllocator();
		T* ptr = allocator->Alloc<T>(Traits::Forward<Args>(args)...);
		return Ref<T>(ptr, allocator);
	}

	template <typename T, typename... Args>
	static Ref<T> MakeRefWith(Allocator* allocator, Args&&... args)
	{
		T* ptr = allocator->Alloc<T>(Traits::Forward<Args>(args)...);
		return Ref<T>(ptr, allocator);
	}

	template<typename T>
	struct Hash<Ref<T>>
	{
		constexpr static bool hasHash = true;
		constexpr static usize Value(const Ref<T>& v)
		{
			if (v)
			{
				return Hash<usize>::Value(reinterpret_cast<usize>(v.Get()));
			}
			return 0;
		}
	};
}
