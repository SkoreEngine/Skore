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
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Hash.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/TypeInfo.hpp"
#include "Skore/Core/UUID.hpp"

namespace Skore
{
	class ReflectField;
	class ResourceType;

	enum class ResourceFieldType
	{
		None = 0,
		Bool,
		Int,
		UInt,
		Float,
		String,
		Vec2,
		Vec3,
		Vec4,
		Quat,
		Color,
		Enum,
		Reference,
		ReferenceArray,
		SubObject,
		SubObjectSet,
		MAX
	};

	enum class ResourceEventType
	{
		OnSubObjectSetAdded,
		OnSubObjectSetRemoved,
		MAX
	};

	struct RID
	{
		u64 id{};

		explicit operator bool() const noexcept
		{
			return this->id > 0;
		}

		bool operator==(const RID& rid) const
		{
			return this->id == rid.id;
		}

		bool operator!=(const RID& rid) const
		{
			return !(*this == rid);
		}
	};

	template <>
	struct Hash<RID>
	{
		constexpr static bool hasHash = true;

		constexpr static usize Value(const RID& rid)
		{
			return Hash<u64>::Value(rid.id);
		}
	};

	template <typename T>
	struct TypedRID : RID
	{
		constexpr static TypeID typeId = TypeInfo<T>::ID();
	};

	struct TypedRIDApi
	{
		TypeID (*GetTypeID)();
		RID (*   GetRID)(ConstPtr instance);
	};

	template <typename Type>
	struct TypeApi<TypedRID<Type>>
	{
		static TypeID GetTypedID()
		{
			return TypedRID<Type>::typeId;
		}

		static RID GetRID(ConstPtr instance)
		{
			return *static_cast<const TypedRID<Type>*>(instance);
		}

		static void ExtractApi(VoidPtr pointer)
		{
			TypedRIDApi* typedRidApi = static_cast<TypedRIDApi*>(pointer);
			typedRidApi->GetTypeID = GetTypedID;
			typedRidApi->GetRID = GetRID;
		}

		static constexpr TypeID GetApiId()
		{
			return TypeInfo<TypedRIDApi>::ID();
		}
	};

	struct SubObjectSet
	{
		HashSet<RID> subObjects;
		HashSet<RID> prototypeRemoved;
	};

	class ResourceObject;
	struct ResourceStorage;
	struct UndoRedoScope;
	typedef CharPtr ResourceInstance;
	typedef bool(*FnRIDCallback)(RID rid, VoidPtr userData);


	struct ResourceStorage
	{
		RID                           rid;
		UUID                          uuid;
		ResourceType*                 resourceType = nullptr;
		std::atomic<ResourceInstance> instance = {};
		std::atomic<u64>              version = 1;
		ResourceStorage*              parent = nullptr;
		u32                           parentFieldIndex = U32_MAX;
		ResourceStorage*              prototype = nullptr;
	};
}
