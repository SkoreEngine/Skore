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
#include "TypeInfo.hpp"

namespace Skore {
	struct ArchiveReader;
}

namespace Skore {
	struct ArchiveWriter;
}

namespace Skore
{
	class ReflectType;


#define SK_CLASS(Type, BaseType)												\
	using Base = BaseType;														\
	virtual TypeID GetTypeId() const override {return TypeInfo<Type>::ID();}	\
	virtual bool IsBaseOf(TypeID typeId) override {if (BaseType::IsBaseOf(typeId)) return true; return typeId == TypeInfo<Type>::ID(); }


	class SK_API Object
	{
	public:
		virtual ~Object() = default;
		ReflectType* GetType() const;

		virtual bool IsBaseOf(TypeID typeId)
		{
			return typeId == TypeInfo<Object>::ID();
		}

		virtual TypeID GetTypeId() const
		{
			return TypeInfo<Object>::ID();
		}

		template <typename T>
		T* SafeCast()
		{
			if (this->IsBaseOf(TypeInfo<T>::ID()))
			{
				return static_cast<T*>(this);
			}
			return nullptr;
		}

		template <typename T>
		T* Cast()
		{
			return static_cast<T*>(this);
		}

		virtual void Serialize(ArchiveWriter& archiveWriter) const;
		virtual void Deserialize(ArchiveReader& archiveReader);
	};
}
