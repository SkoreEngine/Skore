#include "Skore/Core/Allocator.hpp"

#include "mimalloc.h"

namespace Skore
{
	static VoidPtr MemAlloc(VoidPtr allocator, usize bytes)
	{
		return mi_malloc(bytes);
	}

	static void MemFree(VoidPtr allocator, VoidPtr ptr)
	{
		mi_free(ptr);
	}

	VoidPtr MemRealloc(VoidPtr allocator, VoidPtr ptr, usize newSize)
	{
		return mi_realloc(ptr, newSize);
	}


	Allocator* MemoryGlobals::GetDefaultAllocator()
	{
		static  Allocator defaultAllocator = {
						.allocator = nullptr,
						.MemAlloc = MemAlloc,
						.MemFree = MemFree,
						.MemRealloc = MemRealloc
		};

		return &defaultAllocator;
	}

	Allocator* MemoryGlobals::GetHeapAllocator()
	{
		static Allocator heapAllocator = {
						.allocator = nullptr,
						.MemAlloc = MemAlloc,
						.MemFree = MemFree,
						.MemRealloc = MemRealloc
		};
		return &heapAllocator;
	}
}