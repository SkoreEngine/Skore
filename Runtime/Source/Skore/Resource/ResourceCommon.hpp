#pragma once

#include <mutex>

#include "ResourceCommon.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Hash.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/TypeInfo.hpp"
#include "Skore/Core/UUID.hpp"

namespace Skore {
	class ResourceBuffer;
}

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
		Mat4,
		Color,
		Enum,
		Blob,
		Reference,
		ReferenceArray,
		SubObject,
		SubObjectList,
		Buffer,
		TypeID,
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
	struct SubObjectRID : RID
	{
		constexpr static TypeID typeId = TypeInfo<T>::ID();

		SubObjectRID() = default;
		SubObjectRID(RID rid) : RID(rid){}
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

	struct SubObjectList
	{
		Array<RID> subObjects;
		HashSet<RID> prototypeRemoved;
	};

	class ResourceObject;
	struct ResourceStorage;
	struct UndoRedoScope;
	typedef CharPtr ResourceInstance;
	typedef void(*FnRIDCallback)(RID rid, VoidPtr userData);
	typedef void(*FnSubObjectCallback)(u32 index, RID rid, VoidPtr userData);
	typedef void(*FnResourceBufferCallback)(const ResourceBuffer&, VoidPtr userData);
	typedef void (*FnObjectEvent)(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);


	struct CompareSubObjectListResult
	{
		CompareSubObjectSetType type;
		RID rid;
	};

	typedef void (*FnCompareSubObjectListCallback)(const CompareSubObjectListResult& result, VoidPtr userData);

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
			Name
		};
	};


	struct ResourceStorage
	{
		RID                           rid;
		UUID                          uuid;
		String                        path;
		ResourceType*                 resourceType = nullptr;
		u32                           resourceTypeVersion = 0;
		std::atomic<ResourceInstance> instance = {};
		std::atomic<u64>              version = 1;
		ResourceStorage*              parent = nullptr;
		u32                           parentFieldIndex = U32_MAX;
		ResourceStorage*              prototype = nullptr;
		HashSet<RID>                  prototypeInstances;
		std::mutex                    prototypeInstancesMutex;

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

		Array<RID> GetPrototypeInstancesSafe();
	};


	struct ResourceLoader : Object
	{
		SK_CLASS(ResourceLoader, Object);

		virtual TypeID GetResourceTypeId() = 0;
		virtual void LoadResource(RID object) = 0;
	};
}
