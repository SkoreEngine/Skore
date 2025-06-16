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
		Blob,
		Reference,
		ReferenceArray,
		SubObject,
		SubObjectSet,
		MAX
	};

	enum class ResourceFieldEventType
	{
		OnSubObjectSetAdded,
		OnSubObjectSetRemoved,
		MAX
	};

	enum class ResourceEventType
	{
		//triggered with old and new resource objects, only on the changed object
		Changed,

		//triggered when the "version" is updated, (if a subobject is updated) only contains the current value
		VersionUpdated,
		MAX
	};

	enum class CompareSubObjectSetType
	{
		Added,
		Removed,
	};

	template <typename T>
	struct TypedRID : RID
	{
		constexpr static TypeID typeId = TypeInfo<T>::ID();

		TypedRID() = default;
		TypedRID(RID rid) : RID(rid){}

	};

	template <typename T>
	Array<RID> CastRIDArray(const Array<TypedRID<T>>& origin)
	{
		Array<RID> ret;
		ret.Reserve(origin.Size());
		for (TypedRID<T> rid : origin)
		{
			ret.EmplaceBack(rid);
		}
		return ret;
	}


	template <typename T>
	struct TypedRIDInfo : Traits::FalseType
	{
	};

	template <typename T>
	struct TypedRIDInfo<TypedRID<T>> : Traits::TrueType
	{
		constexpr static TypeID typeId = TypedRID<T>::typeId;
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
	typedef void (*FnObjectEvent)(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);


	struct CompareSubObjectSetResult
	{
		CompareSubObjectSetType type;
		RID rid;
	};


	struct ResourceEvent
	{
		FnObjectEvent function;
		VoidPtr       userData;

		friend bool operator==(const ResourceEvent& lhs, const ResourceEvent& rhs)
		{
			return lhs.function == rhs.function && lhs.userData == rhs.userData;
		}

		friend bool operator!=(const ResourceEvent& lhs, const ResourceEvent& rhs)
		{
			return !(lhs == rhs);
		}
	};

	//used for unknown asset file types.
	struct ResourceFile
	{
		enum
		{
			Name,
			Content
		};
	};


	struct ResourceStorage
	{
		RID                           rid;
		UUID                          uuid;
		String						  path;
		ResourceType*                 resourceType = nullptr;
		std::atomic<ResourceInstance> instance = {};
		std::atomic<u64>              version = 1;
		ResourceStorage*              parent = nullptr;
		u32                           parentFieldIndex = U32_MAX;
		ResourceStorage*              prototype = nullptr;

		Array<ResourceEvent> events[static_cast<u32>(ResourceEventType::MAX)];

		void RegisterEvent(ResourceEventType type, FnObjectEvent event, VoidPtr userData)
		{
			events[static_cast<u32>(type)].EmplaceBack(event, userData);
		}

		void UnregisterEvent(ResourceEventType type, FnObjectEvent event, VoidPtr userData)
		{
			ResourceEvent typeEvent = {event, userData};
			if (auto itAsset = FindFirst(events[static_cast<u32>(type)].begin(), events[static_cast<u32>(type)].end(), typeEvent))
			{
				events[static_cast<u32>(type)].Erase(itAsset);
			}
		}
	};
}
