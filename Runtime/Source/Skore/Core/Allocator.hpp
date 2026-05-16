#pragma once

#include "Skore/Common.hpp"
#include "Traits.hpp"

namespace Skore
{
	struct Allocator
	{
		VoidPtr   allocator;
		VoidPtr (*MemAlloc)(VoidPtr allocator, usize bytes);
		void (*   MemFree)(VoidPtr allocator, VoidPtr ptr);
		VoidPtr (*MemRealloc)(VoidPtr allocator, VoidPtr ptr, usize newSize);

		template <typename Type, typename... Args>
		Type* Alloc(Args&&... args)
		{
			auto ptr = static_cast<Type*>(MemAlloc(allocator, sizeof(Type)));
			if constexpr (Traits::IsAggregate<Type>)
			{
				new(PlaceHolder(), ptr) Type{Traits::Forward<Args>(args)...};
			}
			else
			{
				new(PlaceHolder(), ptr) Type(Traits::Forward<Args>(args)...);
			}
			return ptr;
		}

		template <typename Type>
		void DestroyAndFree(Type* type)
		{
			if constexpr (std::is_destructible_v<Type>)
			{
				type->~Type();
			}
			MemFree(allocator, type);
		}

		template <typename Type>
		void DestroyAndFree(const Type* type)
		{
			if constexpr (std::is_destructible_v<Type>)
			{
				type->~Type();
			}
			MemFree(allocator, const_cast<Type*>(type));
		}
	};

	struct SK_API MemoryGlobals
	{
		static Allocator* GetDefaultAllocator();
		static Allocator* GetHeapAllocator();
	};

	template <typename Type, typename... Args>
	Type* Alloc(Args&&... args)
	{
		return MemoryGlobals::GetDefaultAllocator()->Alloc<Type>(Traits::Forward<Args>(args)...);
	}

	template <typename Type>
	void DestroyAndFree(Type* type)
	{
		MemoryGlobals::GetDefaultAllocator()->DestroyAndFree(type);
	}

	inline VoidPtr MemAlloc(usize bytes)
	{
		Allocator* alloc = MemoryGlobals::GetDefaultAllocator();
		return alloc->MemAlloc(alloc->allocator, bytes);
	}

	inline VoidPtr MemRealloc(VoidPtr ptr, usize bytes)
	{
		Allocator* alloc = MemoryGlobals::GetDefaultAllocator();
		return alloc->MemRealloc(alloc->allocator, ptr, bytes);
	}

	template <typename Type>
	Type* AllocElements(usize elements)
	{
		return static_cast<Type*>(MemAlloc(elements * sizeof(Type)));
	}

	template <typename Type>
	Type* ReallocElements(Type* ptr, usize elements)
	{
		return static_cast<Type*>(MemRealloc(ptr, elements * sizeof(Type)));
	}

	inline void MemFree(VoidPtr ptr)
	{
		Allocator* alloc = MemoryGlobals::GetDefaultAllocator();
		alloc->MemFree(alloc->allocator, ptr);
	}


	template <typename T>
	class StdAllocator
	{
	public:
		using value_type = T;

		StdAllocator() noexcept {}

		template <typename U>
		StdAllocator(const StdAllocator<U>&) noexcept {}

		T* allocate(std::size_t n)
		{
			return static_cast<T*>(MemAlloc(n * sizeof(T)));
		}

		void deallocate(T* p, std::size_t n)
		{
			MemFree(p);
		}
	};

	// Equality operators outside class
	template <typename T, typename U>
	bool operator==(const StdAllocator<T>&, const StdAllocator<U>&) noexcept
	{
		return true;
	}

	template <typename T, typename U>
	bool operator!=(const StdAllocator<T>&, const StdAllocator<U>&) noexcept
	{
		return false;
	}
}
