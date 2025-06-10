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
#include "ResourceObject.hpp"
#include "Skore/Core/String.hpp"
#include "Resources.hpp"


namespace Skore
{
	struct ResourceFieldInfo
	{
		ResourceFieldType type;
		TypeID            subType;
	};


	namespace ResourceReflection
	{
		// SK_API StringView GetEnumDesc(const ReflectType* reflectType, ConstPtr value);
		// SK_API ConstPtr GetEnumValue(const ReflectType* reflectType, StringView desc);

		//this is different from Reflection::FindTypeById
		//it won't return the type if it's not valid for mapping to Resource, even if it's registered
		SK_API ReflectType* FindTypeToCast(TypeID typeId);
	}

	template<typename T, typename = void>
	struct ResourceCast
	{
		constexpr static bool hasSpecialization = true;

		static void ToResource(ResourceObject& object, u32 index, UndoRedoScope* scope, const T& value)
		{
			RID subobject = Resources::Create<T>({}, scope);
			if (Resources::ToResource(subobject, &value, scope))
			{
				object.SetSubObject(index, subobject);
			}
		}

		static void FromResource(const ResourceObject& object, u32 index, T& value)
		{
			if (RID rid = object.GetSubObject(index))
			{
				Resources::FromResource(rid, &value);
			}
		}

		static ResourceFieldInfo GetResourceFieldInfo()
		{
			if (ResourceReflection::FindTypeToCast(TypeInfo<T>::ID()) != nullptr)
			{
				return ResourceFieldInfo{
					.type = ResourceFieldType::SubObject,
					.subType = TypeInfo<T>::ID()
				};
			}
			return ResourceFieldInfo{
				.type = ResourceFieldType::None
			};
		}
	};

#define RESOURCE_CAST(TYPE, TYPE_NAME)   \
	template<>							 \
	struct ResourceCast<TYPE>			 \
	{	\
		constexpr static bool hasSpecialization = true; \
		static void ToResource(ResourceObject& object, u32 index, UndoRedoScope* scope, const TYPE& value) \
		{ \
			object.Set##TYPE_NAME(index, value); \
		} \
		static void FromResource(const ResourceObject& object, u32 index, TYPE& value) \
		{ \
			value = object.Get##TYPE_NAME(index); \
		}\
		static ResourceFieldInfo GetResourceFieldInfo()	\
		{												\
			return ResourceFieldInfo{					\
				.type = ResourceFieldType::##TYPE_NAME,	\
				.subType = TypeInfo<TYPE>::ID()			\
			};											\
		}\
	}

	RESOURCE_CAST(u8, UInt);
	RESOURCE_CAST(u16, UInt);
	RESOURCE_CAST(u32, UInt);
	RESOURCE_CAST(u64, UInt);

	// For signed integers
	RESOURCE_CAST(i8, Int);
	RESOURCE_CAST(i16, Int);
	RESOURCE_CAST(i32, Int);
	RESOURCE_CAST(i64, Int);

	//floats
	RESOURCE_CAST(f32, Float);
	RESOURCE_CAST(f64, Float);

	//bool
	RESOURCE_CAST(bool, Bool);

	//Math
	RESOURCE_CAST(Vec2, Vec2);
	RESOURCE_CAST(Vec3, Vec3);
	RESOURCE_CAST(Vec4, Vec4);
	RESOURCE_CAST(Quat, Quat);

	//other
	RESOURCE_CAST(Color, Color);

	template<>
	struct ResourceCast<String>
	{
		constexpr static bool hasSpecialization = true;

		static void ToResource(ResourceObject& object, u32 index, UndoRedoScope* scope, const String& value)
		{
			object.SetString(index, value);
		}

		static void FromResource(const ResourceObject& object, u32 index, String& value)
		{
			value = object.GetString(index);
		}

		static ResourceFieldInfo GetResourceFieldInfo()
		{
			return ResourceFieldInfo{
				.type = ResourceFieldType::String,
				.subType = TypeInfo<String>::ID()
			};
		}
	};

	template <typename T>
	struct ResourceCast<T, Traits::EnableIf<Traits::IsEnum<T>>>
	{
		constexpr static bool hasSpecialization = true;

		static void ToResource(ResourceObject& object, u32 index, UndoRedoScope* scope, const T& value)
		{
			object.SetEnum(index, static_cast<i64>(value));
		}

		static void FromResource(const ResourceObject& object, u32 index, T& value)
		{
			value = static_cast<T>(object.GetEnum(index));
		}

		static ResourceFieldInfo GetResourceFieldInfo()
		{
			return ResourceFieldInfo{
				.type = ResourceFieldType::Enum,
				.subType = TypeInfo<T>::ID()
			};
		}
	};


	template<typename T>
	struct ResourceCast<TypedRID<T>>
	{
		constexpr static bool hasSpecialization = true;

		static void ToResource(ResourceObject& object, u32 index, UndoRedoScope* scope, const TypedRID<T>& value)
		{
			object.SetReference(index, value);
		}

		static void FromResource(const ResourceObject& object, u32 index, TypedRID<T>& value)
		{
			value.id = object.GetReference(index).id;
		}

		static ResourceFieldInfo GetResourceFieldInfo()
		{
			return ResourceFieldInfo{
				.type = ResourceFieldType::Reference,
				.subType = TypeInfo<T>::ID()
			};
		}
	};

	template<typename T>
	struct ResourceCast<Array<T>>
	{
		constexpr static bool hasSpecialization = true;

		static void ToResource(ResourceObject& object, u32 index, UndoRedoScope* scope, const Array<T>& array)
		{
			for (const T& element : array)
			{
				RID subobject = Resources::Create<T>({}, scope);
				Resources::ToResource(subobject, &element, scope);
				object.AddToSubObjectSet(index, subobject);
			}
		}

		static void FromResource(const ResourceObject& object, u32 index, Array<T>& array)
		{
			object.IterateSubObjectSet(index, true, [&](RID subobject)
			{
				T value;
				Resources::FromResource(subobject, &value);
				array.EmplaceBack(Traits::Move(value));
				return true;
			});
		}

		static ResourceFieldInfo GetResourceFieldInfo()
		{
			if (ResourceReflection::FindTypeToCast(TypeInfo<T>::ID()) != nullptr)
			{
				return ResourceFieldInfo{
					.type = ResourceFieldType::SubObjectSet,
					.subType = TypeInfo<T>::ID()
				};
			}
			return ResourceFieldInfo{
				.type = ResourceFieldType::None
			};
		}
	};

}
