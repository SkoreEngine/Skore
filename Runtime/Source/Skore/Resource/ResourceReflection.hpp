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
	namespace ResourceReflection
	{
		// SK_API StringView GetEnumDesc(const ReflectType* reflectType, ConstPtr value);
		// SK_API ConstPtr GetEnumValue(const ReflectType* reflectType, StringView desc);

		//this is different from Reflection::FindTypeById
		//it won't return the type if it's not valid for mapping to Resource, even if it's registered
		SK_API ReflectType* FindTypeToCast(TypeID typeId);
	}

	template<typename T>
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

		static ResourceFieldType GetResourceFieldType()
		{
			if (ResourceReflection::FindTypeToCast(TypeInfo<T>::ID()) != nullptr)
			{
				return ResourceFieldType::SubObject;
			}
			return ResourceFieldType::None;
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
	   static ResourceFieldType GetResourceFieldType()	\
	   {											\
		   return ResourceFieldType::##TYPE_NAME;	\
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

		static ResourceFieldType GetResourceFieldType()
		{
			return ResourceFieldType::String;
		}
	};


}
