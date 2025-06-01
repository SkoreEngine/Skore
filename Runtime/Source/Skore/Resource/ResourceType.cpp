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

#include "ResourceType.hpp"

#include "Skore/Core/Math.hpp"
#include "Skore/Core/Span.hpp"

namespace Skore
{
	struct ResourceFieldProps
	{
		usize size;
		usize align;
	};

	constexpr static ResourceFieldProps fieldProps[] = {
		{0, 0},                                        // ResourceFieldType::None
		{sizeof(bool), alignof(bool)},                 // ResourceFieldType::Bool
		{sizeof(i64), alignof(i64)},                   // ResourceFieldType::Int,
		{sizeof(u64), alignof(u64)},                   // ResourceFieldType::UInt,
		{sizeof(f64), alignof(f64)},                   // ResourceFieldType::Float,
		{sizeof(String), alignof(String)},             // ResourceFieldType::String,
		{sizeof(Vec2), alignof(Vec2)},                 // ResourceFieldType::Vec2,
		{sizeof(Vec2), alignof(Vec2)},                 // ResourceFieldType::Vec3,
		{sizeof(Vec2), alignof(Vec2)},                 // ResourceFieldType::Vec4,
		{sizeof(Vec2), alignof(Vec2)},                 // ResourceFieldType::Quat,
		{sizeof(Vec2), alignof(Vec2)},                 // ResourceFieldType::Color,
		{sizeof(i64), alignof(i64)},                   // ResourceFieldType::Enum,
		{sizeof(RID), alignof(RID)},                   // ResourceFieldType::Reference,
		{sizeof(Array<RID>), alignof(Array<RID>)},     // ResourceFieldType::ReferenceArray,
		{sizeof(RID), alignof(RID)},                   // ResourceFieldType::SubObject,
		{sizeof(SubObjectSet), alignof(SubObjectSet)}, // ResourceFieldType::SubObjectSet,
	};

	static_assert(sizeof(fieldProps) / sizeof(ResourceFieldProps) == static_cast<usize>(ResourceFieldType::MAX), "Invalid field size array");


	StringView ResourceField::GetName() const
	{
		return name;
	}

	u32 ResourceField::GetIndex() const
	{
		return index;
	}

	u32 ResourceField::GetSize() const
	{
		return size;
	}

	u32 ResourceField::GetOffset() const
	{
		return offset;
	}

	ResourceFieldType ResourceField::GetType() const
	{
		return type;
	}

	TypeID ResourceField::GetSubType() const
	{
		return subType;
	}

	ResourceType::ResourceType(TypeID type, StringView name) : type(type), name(name) {}

	ResourceType::~ResourceType()
	{
		for (ResourceField* field : fields)
		{
			DestroyAndFree(field);
		}
	}

	ResourceInstance ResourceType::Allocate() const
	{
		SK_ASSERT(allocSize > 0, "Invalid resource type alloc size, did you call Build()?");

		ResourceInstance instance = static_cast<ResourceInstance>(MemAlloc(allocSize));
		memset(instance, 0, allocSize);
		return instance;
	}

	TypeID ResourceType::GetID() const
	{
		return type;
	}

	String ResourceType::GetName() const
	{
		return name;
	}

	RID ResourceType::GetDefaultValue() const
	{
		return defaultValue;
	}

	u32 ResourceType::GetAllocSize() const
	{
		return allocSize;
	}

	ReflectType* ResourceType::GetReflectType() const
	{
		return reflectType;
	}

	Span<ResourceField*> ResourceType::GetFields() const
	{
		return fields;
	}

	ResourceField* ResourceType::FindFieldByName(StringView name) const
	{
		for (ResourceField* field : fields)
		{
			if (field->name == name)
			{
				return field;
			}
		}
		return nullptr;
	}

	void ResourceType::SetDefaultValue(RID defaultValue)
	{
		this->defaultValue = defaultValue;
	}

	ResourceTypeBuilder& ResourceTypeBuilder::Field(u32 index, StringView name, ResourceFieldType type)
	{
		Field(index, name, type, 0);
		return *this;
	}

	ResourceTypeBuilder& ResourceTypeBuilder::Field(u32 index, StringView name, ResourceFieldType type, TypeID subType)
	{
		ResourceField* resourceField = Alloc<ResourceField>();
		resourceField->index = index;
		resourceField->name = name;
		resourceField->type = type;
		resourceField->subType = subType;
		resourceType->fields.EmplaceBack(resourceField);

		return *this;
	}

	ResourceTypeBuilder& ResourceTypeBuilder::FieldEvent(u32 index, ResourceEventType type, FnObjectFieldEvent objectFieldEvent)
	{
		//TODO
		return *this;
	}

	ResourceTypeBuilder& ResourceTypeBuilder::Build()
	{
		//info
		resourceType->allocSize = sizeof(ResourceInstanceInfo);

		//null check
		//keep align to 4 bytes
		usize checkSize = resourceType->fields.Size() + 3 & ~3;
		resourceType->allocSize += checkSize;

		for (ResourceField* field : resourceType->fields)
		{
			const ResourceFieldProps& props = fieldProps[static_cast<usize>(field->type)];
			field->size = props.size;
			field->offset = resourceType->allocSize;
			resourceType->allocSize += Math::Max(props.size, props.align);
		}

		return *this;
	}

	ResourceType* ResourceTypeBuilder::GetResourceType() const
	{
		return resourceType;
	}
}
