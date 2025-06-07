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
#include "Traits.hpp"

namespace Skore
{
    struct Allocator
    {
       VoidPtr allocator;
       VoidPtr (*MemAlloc)(VoidPtr allocator, usize bytes);
       void    (*MemFree)(VoidPtr allocator, VoidPtr ptr);
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
            type->~Type();
            MemFree(allocator, type);
        }

        template <typename Type>
        void DestroyAndFree(const Type* type)
        {
            type->~Type();
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

    inline void MemFree(VoidPtr ptr)
    {
        Allocator* alloc = MemoryGlobals::GetDefaultAllocator();
        alloc->MemFree(alloc->allocator, ptr);
    }
}
