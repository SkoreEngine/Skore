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

#include "ResourceCommon.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/HashMap.hpp"

namespace Skore
{
	class ReflectType;
	typedef void (*FnObjectFieldEvent)(ConstPtr value, ResourceObject& object);

	class SK_API ResourceField
	{
	public:
		StringView        GetName() const;
		u32               GetIndex() const;
		u32               GetSize() const;
		u32               GetOffset() const;
		ResourceFieldType GetType() const;
		TypeID            GetSubType() const;
		FieldProps        GetProps() const;
		ReflectField*     GetReflectField() const;

		friend class ResourceTypeBuilder;
		friend class ResourceType;
		friend class ResourceObject;

	private:
		String                    name;
		u32                       index = U32_MAX;
		u32                       size = 0;
		u32                       offset = 0;
		ResourceFieldType         type = ResourceFieldType::None;
		ReflectField*             reflectField = nullptr;
		TypeID                    subType = 0;
		Array<FnObjectFieldEvent> events[static_cast<u32>(ResourceFieldEventType::MAX)];
	};


	class SK_API ResourceType
	{
	public:
		ResourceType(TypeID type, StringView name);
		~ResourceType();
		SK_NO_COPY_CONSTRUCTOR(ResourceType);

		ResourceInstance Allocate() const;

		TypeID               GetID() const;
		String               GetName() const;
		String               GetSimpleName() const;
		RID                  GetDefaultValue() const;
		void                 SetDefaultValue(RID defaultValue);
		u32                  GetAllocSize() const;
		u32                  GetVersion() const;
		ReflectType*         GetReflectType() const;
		Span<ResourceField*> GetFields() const;
		ResourceField*       FindFieldByName(StringView name) const;
		void                 RegisterEvent(FnObjectEvent event, VoidPtr userData);
		void                 UnregisterEvent(FnObjectEvent event, VoidPtr userData);
		Span<ResourceEvent>  GetEvents() const;
		ConstPtr             GetAttribute(TypeID attributeId) const;


		template<typename AttType>
		const AttType* GetAttribute() const
		{
			return static_cast<const AttType*>(GetAttribute(TypeInfo<AttType>::ID()));
		}

		friend class ResourceTypeBuilder;
		friend class ResourceObject;
		friend class Resources;

	private:
		TypeID       type = 0;
		u32          version = 0;
		String       name;
		String       simpleName;
		RID          defaultValue;
		u32          allocSize = 0;
		ReflectType* reflectType = nullptr;

		Array<ResourceField*>    fields;
		Array<ResourceEvent>     events;
		HashMap<TypeID, VoidPtr> attributes;
	};

	struct ResourceInstanceInfo
	{
		ResourceInstance dataOnWrite;
		bool             readOnly;
	};


	class SK_API ResourceTypeBuilder
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(ResourceTypeBuilder);
		ResourceTypeBuilder(ResourceType* resourceType) : resourceType(resourceType) {}

		ResourceTypeBuilder& Field(u32 index, StringView name, ResourceFieldType type);
		ResourceTypeBuilder& Field(u32 index, StringView name, ResourceFieldType type, TypeID subType);
		ResourceTypeBuilder& Field(ReflectField* field);
		ResourceTypeBuilder& Attribute(TypeID type, ConstPtr value);

		template <typename Type, typename... Args>
		ResourceTypeBuilder& Attribute(Args&&... args)
		{
			Type value = Type{Traits::Forward<Args>(args)...};
			return Attribute(TypeInfo<Type>::ID(), &value);
		}

		template <auto T>
		ResourceTypeBuilder& Field(ResourceFieldType type, TypeID subType = 0)
		{
			return Field(static_cast<u32>(T), GetEnumValueName<T>(), type, subType);
		}

		ResourceTypeBuilder& Build();

		ResourceType* GetResourceType() const;

	private:
		ResourceType* resourceType = nullptr;
	};
}
