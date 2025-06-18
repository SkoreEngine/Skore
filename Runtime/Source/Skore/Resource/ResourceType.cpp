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

#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Span.hpp"

namespace Skore
{
	constexpr static TypeProps fieldProps[] = {
		TypeInfo<void>::GetProps(),         // ResourceFieldType::None
		TypeInfo<bool>::GetProps(),         // ResourceFieldType::Bool
		TypeInfo<i64>::GetProps(),          // ResourceFieldType::Int,
		TypeInfo<u64>::GetProps(),          // ResourceFieldType::UInt,
		TypeInfo<f64>::GetProps(),          // ResourceFieldType::Float,
		TypeInfo<String>::GetProps(),       // ResourceFieldType::String,
		TypeInfo<Vec2>::GetProps(),         // ResourceFieldType::Vec2,
		TypeInfo<Vec3>::GetProps(),         // ResourceFieldType::Vec3,
		TypeInfo<Vec4>::GetProps(),         // ResourceFieldType::Vec4,
		TypeInfo<Quat>::GetProps(),         // ResourceFieldType::Quat,
		TypeInfo<Color>::GetProps(),        // ResourceFieldType::Color,
		TypeInfo<i64>::GetProps(),          // ResourceFieldType::Enum,
		TypeInfo<ByteBuffer>::GetProps(),   // ResourceFieldType::Blob,
		TypeInfo<RID>::GetProps(),          // ResourceFieldType::Reference,
		TypeInfo<Array<RID>>::GetProps(),   // ResourceFieldType::ReferenceArray,
		TypeInfo<RID>::GetProps(),          // ResourceFieldType::SubObject,
		TypeInfo<SubObjectSet>::GetProps(), // ResourceFieldType::SubObjectSet,
	};

	static_assert(sizeof(fieldProps) / sizeof(TypeProps) == static_cast<usize>(ResourceFieldType::MAX), "Invalid field size array");


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

	FieldProps ResourceField::GetProps() const
	{
		TypeProps typeProps = fieldProps[static_cast<usize>(type)];

		FieldProps props{};
		props.typeId = typeProps.typeId;
		props.typeApi = typeProps.typeApi;
		props.name = typeProps.name;
		props.getTypeApi = typeProps.getTypeApi;
		props.size = typeProps.size;
		props.alignment = typeProps.alignment;
		props.isTriviallyCopyable = typeProps.isTriviallyCopyable;
		props.isEnum = typeProps.isEnum;

		return props;
	}

	ResourceType::ResourceType(TypeID type, StringView name) : type(type), name(name) {}

	ResourceType::~ResourceType()
	{
		for (ResourceField* field : fields)
		{
			if (field == nullptr) continue;

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

	void ResourceType::RegisterEvent(FnObjectEvent event, VoidPtr userData)
	{
		events.EmplaceBack(event, userData);
	}

	void ResourceType::UnregisterEvent(FnObjectEvent event, VoidPtr userData)
	{
		ResourceEvent typeEvent = {event, userData};
		if (auto itAsset = FindFirst(events.begin(), events.end(), typeEvent))
		{
			events.Erase(itAsset);
		}
	}

	Span<ResourceEvent> ResourceType::GetEvents() const
	{
		return events;
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

		if (index >= resourceType->fields.Size())
		{
			resourceType->fields.Resize(index + 1);
		}

		resourceType->fields[index] = resourceField;

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
			if (field == nullptr) continue;

			const TypeProps& props = fieldProps[static_cast<usize>(field->type)];
			field->size = props.size;
			field->offset = resourceType->allocSize;
			resourceType->allocSize += Math::Max(props.size, props.alignment);
		}

		return *this;
	}

	ResourceType* ResourceTypeBuilder::GetResourceType() const
	{
		return resourceType;
	}
}
