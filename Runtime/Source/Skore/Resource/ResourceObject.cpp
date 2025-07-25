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

#include "ResourceObject.hpp"

#include "Resources.hpp"
#include "ResourceType.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/HashSet.hpp"


namespace Skore
{
	void ResourceCommit(ResourceStorage* storage, ResourceInstance instance, UndoRedoScope* scope);
	void ResourceRemoveParent(RID rid);
	void DestroyResourceInstance(ResourceType* resourceType, ResourceInstance instance);


	ResourceObject::ResourceObject(ResourceStorage* storage, ResourceInstance writeInstance) : m_storage(storage), m_currentInstance(writeInstance) {}

	ResourceObject::ResourceObject(ResourceObject&& resourceObject) noexcept
	{
		m_storage = resourceObject.m_storage;
		m_currentInstance = resourceObject.m_currentInstance;
		resourceObject.m_storage = nullptr;
		resourceObject.m_currentInstance = nullptr;
	}

	ResourceObject::~ResourceObject()
	{
		if (m_storage && m_currentInstance && !reinterpret_cast<ResourceInstanceInfo*>(m_currentInstance)->readOnly)
		{
			DestroyResourceInstance(m_storage->resourceType, m_currentInstance);
		}
	}

	void ResourceObject::SetBool(u32 index, bool value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Bool, "Invalid field type");
		SetValue(index, &value, sizeof(bool));
	}

	void ResourceObject::SetInt(u32 index, i64 value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Int, "Invalid field type");
		SetValue(index, &value, sizeof(i64));
	}

	void ResourceObject::SetUInt(u32 index, u64 value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::UInt, "Invalid field type");
		SetValue(index, &value, sizeof(u64));
	}

	void ResourceObject::SetFloat(u32 index, f64 value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Float, "Invalid field type");
		SetValue(index, &value, sizeof(f64));
	}

	void ResourceObject::SetString(u32 index, StringView value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::String, "Invalid field type");
		if (String* str = GetMutPtr<String>(index))
		{
			*str = value;
		}
	}

	void ResourceObject::SetVec2(u32 index, Vec2 value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Vec2, "Invalid field type");
		SetValue(index, &value, sizeof(Vec2));
	}

	void ResourceObject::SetVec3(u32 index, Vec3 value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Vec3, "Invalid field type");
		SetValue(index, &value, sizeof(Vec3));
	}

	void ResourceObject::SetVec4(u32 index, Vec4 value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Vec4, "Invalid field type");
		SetValue(index, &value, sizeof(Vec4));
	}

	void ResourceObject::SetQuat(u32 index, Quat value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Quat, "Invalid field type");
		SetValue(index, &value, sizeof(Quat));
	}

	void ResourceObject::SetColor(u32 index, Color value)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Color, "Invalid field type");
		SetValue(index, &value, sizeof(Color));
	}

	void ResourceObject::SetEnum(u32 index, i64 enumValue)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Enum, "Invalid field type");
		SetValue(index, &enumValue, sizeof(i64));
	}

	void ResourceObject::SetBlob(u32 index, Span<u8> bytes)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Blob, "Invalid field type");
		if (ByteBuffer* arr = GetMutPtr<ByteBuffer>(index))
		{
			*arr = bytes;
		}
	}

	void ResourceObject::SetReference(u32 index, RID rid)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::Reference, "Invalid field type");
		SetValue(index, &rid, sizeof(RID));
	}

	void ResourceObject::SetReferenceArray(u32 index, Span<RID> refs)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (Array<RID>* arr = GetMutPtr<Array<RID>>(index))
		{
			*arr = refs;
		}
	}

	void ResourceObject::SetReferenceArray(u32 index, usize arrIndex, RID ref)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (Array<RID>* arr = GetMutPtr<Array<RID>>(index))
		{
			if (arr->Size() <  arrIndex)
			{
				arr->Resize(arrIndex + 1);
			}
			(*arr)[arrIndex] = ref;
		}
	}

	void ResourceObject::AddToReferenceArray(u32 index, RID ref)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (Array<RID>* arr = GetMutPtr<Array<RID>>(index))
		{
			arr->EmplaceBack(ref);
		}
	}

	void ResourceObject::ClearReferenceArray(u32 index)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (HasValueOnThisObject(index))
		{
			if (Array<RID>* arr = GetMutPtr<Array<RID>>(index))
			{
				arr->Clear();
			}
		}
	}

	void ResourceObject::RemoveFromReferenceArray(u32 index, RID ref)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (HasValueOnThisObject(index))
		{
			if (Array<RID>* arr = GetMutPtr<Array<RID>>(index))
			{
				 if (auto it = FindFirst(arr->begin(), arr->end(), ref))
				 {
				 	arr->Erase(it);
				 }
			}
		}
	}

	void ResourceObject::RemoveFromReferenceArray(u32 index, usize arrIndex)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (HasValueOnThisObject(index))
		{
			if (Array<RID>* arr = GetMutPtr<Array<RID>>(index))
			{
				if (arr->Size() > arrIndex)
				{
					arr->Erase(arr->begin() + arrIndex);
				}
			}
		}
	}

	void ResourceObject::SetSubObject(u32 index, RID subObject)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::SubObject, "Invalid field type");
		SetValue(index, &subObject, sizeof(RID));
	}

	void ResourceObject::AddToSubObjectList(u32 index, RID subObject)
	{
		AddToSubObjectList(index, Span(&subObject, 1));
	}

	void ResourceObject::AddToSubObjectList(u32 index, Span<RID> subObjects)
	{
		AddToSubObjectListAt(index, subObjects, SIZE_MAX);
	}

	void ResourceObject::AddToSubObjectListAt(u32 index, Span<RID> subObjects, usize arrIndex)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectList, "Invalid field type");
		if (SubObjectList* subObjectList = GetMutPtr<SubObjectList>(index))
		{
			bool atEnd = arrIndex == SIZE_MAX || arrIndex >= subObjectList->subObjects.Size();
			auto where = atEnd ? subObjectList->subObjects.end() : subObjectList->subObjects.begin() + arrIndex;
			subObjectList->subObjects.Insert(where, subObjects.begin(), subObjects.end());

			for (RID subObject : subObjects)
			{
				if (ResourceStorage* prototypeSubObject = Resources::GetStorage(subObject)->prototype)
				{
					if (auto it = subObjectList->prototypeRemoved.Find(prototypeSubObject->rid))
					{
						subObjectList->prototypeRemoved.Erase(it);
					}
				}
			}

		}
	}

	void ResourceObject::RemoveFromSubObjectList(u32 index, RID subObject)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectList, "Invalid field type");
		if (HasValueOnThisObject(index))
		{
			if (SubObjectList* subObjectList = GetMutPtr<SubObjectList>(index))
			{
				if (auto it = FindFirst(subObjectList->subObjects.begin(), subObjectList->subObjects.end(), subObject))
				{
					subObjectList->subObjects.Erase(it);
					ResourceRemoveParent(subObject);

					ResourceStorage* prototypeInstance = GetStorage()->prototype;
					ResourceStorage* prototypeSubObject = Resources::GetStorage(subObject)->prototype;

					if (prototypeInstance && prototypeSubObject && prototypeSubObject->parent == prototypeInstance)
					{
						subObjectList->prototypeRemoved.Emplace(prototypeSubObject->rid);
					}
				}
			}
		}
	}

	Array<RID> ResourceObject::RemoveFromSubObjectListByPrototype(u32 index, RID prototype)
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectList, "Invalid field type");
		if (HasValueOnThisObject(index))
		{
			if (SubObjectList* subObjectList = GetMutPtr<SubObjectList>(index))
			{
				Array<RID> removedArr;
				for (RID subObject : subObjectList->subObjects)
				{
					if (Resources::GetPrototype(subObject) == prototype)
					{
						removedArr.EmplaceBack(subObject);
					}
				}

				for (RID removed : removedArr)
				{
					if (auto it = FindFirst(subObjectList->subObjects.begin(), subObjectList->subObjects.end(), removed))
					{
						subObjectList->subObjects.Erase(it);
						ResourceRemoveParent(removed);
					}
				}

				return removedArr;
			}
		}
		return {};
	}

	void ResourceObject::RemoveSubObject(u32 index, RID rid)
	{
		if (m_storage->resourceType->fields[index]->type == ResourceFieldType::SubObject)
		{
			if (const RID* value = GetPtr<RID>(index); value != nullptr && *value == rid)
			{
				UpdateHasValue(index, false);
			}
		}
		else if (m_storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectList)
		{
			RemoveFromSubObjectList(index, rid);
		}
	}

	bool ResourceObject::HasValue(u32 index) const
	{
		return GetPtr(index) != nullptr;
	}

	bool ResourceObject::HasValueOnThisObject(u32 index) const
	{
		if (ResourceInstance instance = m_currentInstance ? m_currentInstance : m_storage->instance.load())
		{
			return *reinterpret_cast<bool*>(&instance[sizeof(ResourceInstanceInfo) + index]);
		}
		return false;
	}

	bool ResourceObject::IsValueOverridden(u32 index) const
	{
		return GetStorage()->prototype && HasValueOnThisObject(index);
	}

	bool ResourceObject::CopyValue(u32 index, VoidPtr buffer, usize size) const
	{
		usize fieldSize = m_storage->resourceType->GetFields()[index]->GetProps().size;
		if (fieldSize > size)
		{
			return false;
		}

		if (ConstPtr value = GetPtr(index))
		{
			memcpy(buffer, value, fieldSize);
			return true;
		}

		memset(buffer, 0, fieldSize);
		return false;
	}

	bool ResourceObject::GetBool(u32 index) const
	{
		if (const bool* value = GetPtr<bool>(index))
		{
			return *value;
		}
		return {};
	}

	i64 ResourceObject::GetInt(u32 index) const
	{
		if (const i64* value = GetPtr<i64>(index))
		{
			return *value;
		}
		return {};
	}

	u64 ResourceObject::GetUInt(u32 index) const
	{
		if (const u64* value = GetPtr<u64>(index))
		{
			return *value;
		}
		return {};
	}

	f64 ResourceObject::GetFloat(u32 index) const
	{
		if (const f64* value = GetPtr<f64>(index))
		{
			return *value;
		}
		return {};
	}

	StringView ResourceObject::GetString(u32 index) const
	{
		if (const String* value = GetPtr<String>(index))
		{
			return *value;
		}
		return {};
	}

	Vec2 ResourceObject::GetVec2(u32 index) const
	{
		if (const Vec2* value = GetPtr<Vec2>(index))
		{
			return *value;
		}
		return {};
	}

	Vec3 ResourceObject::GetVec3(u32 index) const
	{
		if (const Vec3* value = GetPtr<Vec3>(index))
		{
			return *value;
		}
		return {};
	}

	Vec4 ResourceObject::GetVec4(u32 index) const
	{
		if (const Vec4* value = GetPtr<Vec4>(index))
		{
			return *value;
		}
		return {};
	}

	Quat ResourceObject::GetQuat(u32 index) const
	{
		if (const Quat* value = GetPtr<Quat>(index))
		{
			return *value;
		}
		return {};
	}

	Color ResourceObject::GetColor(u32 index) const
	{
		if (const Color* value = GetPtr<Color>(index))
		{
			return *value;
		}
		return {};
	}

	i64 ResourceObject::GetEnum(u32 index) const
	{
		if (const i64* value = GetPtr<i64>(index))
		{
			return *value;
		}
		return {};
	}

	RID ResourceObject::GetSubObject(u32 index) const
	{
		if (const RID* value = GetPtr<RID>(index))
		{
			return *value;
		}
		return {};
	}

	RID ResourceObject::GetReference(u32 index) const
	{
		if (const RID* value = GetPtr<RID>(index))
		{
			return *value;
		}
		return {};
	}

	Span<u8> ResourceObject::GetBlob(u32 index) const
	{
		if (const ByteBuffer* value = GetPtr<ByteBuffer>(index))
		{
			return *value;
		}
		return {};
	}

	Span<RID> ResourceObject::GetReferenceArray(u32 index) const
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (const Array<RID>* value = GetPtr<Array<RID>>(index))
		{
			return *value;
		}
		return {};
	}

	bool ResourceObject::HasOnReferenceArray(u32 index, RID rid) const
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (const Array<RID>* value = GetPtr<Array<RID>>(index))
		{
			return FindFirst(value->begin(), value->end(), rid) != nullptr;
		}
		return false;
	}

	void ResourceObject::IterateSubObjectList(u32 index, FnRIDCallback callback, VoidPtr userData) const
	{
		SK_ASSERT(m_storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectList, "Invalid field type");

		if (const SubObjectList* value = GetPtr<SubObjectList>(index))
		{
			for (RID subobject : value->subObjects)
			{
				if (!value->prototypeRemoved.Has(subobject))
				{
					callback(subobject, userData);
				}
			}
		}
	}

	u64 ResourceObject::GetSubObjectListCount(u32 index) const
	{
		u64 count = 0;
		IterateSubObjectList(index, [&](RID rid)
		{
			count++;
			return true;
		});
		return count;
	}

	Span<RID> ResourceObject::GetSubObjectList(u32 index) const
	{
		if (const SubObjectList* value = GetPtr<SubObjectList>(index))
		{
			return value->subObjects;
		}
		return {};
	}

	bool ResourceObject::HasOnSubObjectList(u32 index, RID rid) const
	{
		if (const SubObjectList* value = GetPtr<SubObjectList>(index))
		{
			return FindFirst(value->subObjects.begin(), value->subObjects.end(), rid) != nullptr;
		}
		return false;
	}

	u64 ResourceObject::GetPrototypeRemovedCount(u32 index) const
	{
		if (const SubObjectList* value = GetPtr<SubObjectList>(index))
		{
			return value->prototypeRemoved.Size();
		}
		return 0;
	}

	bool ResourceObject::IsRemoveFromPrototypeSubObjectList(u32 index, RID rid) const
	{
		if (const SubObjectList* value = GetPtr<SubObjectList>(index))
		{
			return value->prototypeRemoved.Has(rid);
		}
		return false;
	}

	void ResourceObject::IteratePrototypeRemoved(u32 index, FnRIDCallback callback, VoidPtr userData) const
	{
		if (const SubObjectList* value = GetPtr<SubObjectList>(index))
		{
			for (RID rid : value->prototypeRemoved)
			{
				callback(rid, userData);
			}
		}
	}

	u32 ResourceObject::GetIndex(StringView fieldName) const
	{
		for (u32 i = 0; i < m_storage->resourceType->fields.Size(); i++)
		{
			if (m_storage->resourceType->fields[i]->name == fieldName)
			{
				return i;
			}
		}
		return U32_MAX;
	}

	RID ResourceObject::GetRID() const
	{
		return m_storage->rid;
	}

	RID ResourceObject::GetPrototype() const
	{
		return m_storage->prototype ? m_storage->prototype->rid : RID();
	}

	UUID ResourceObject::GetUUID() const
	{
		return m_storage->uuid;
	}

	ResourceType* ResourceObject::GetType() const
	{
		return m_storage->resourceType;
	}

	ResourceStorage* ResourceObject::GetStorage() const
	{
		return m_storage;
	}

	u64 ResourceObject::GetVersion() const
	{
		return m_storage->version;
	}

	void ResourceObject::Commit(UndoRedoScope* scope)
	{
		ResourceCommit(m_storage, m_currentInstance, scope);
	}

	ResourceObject::operator bool() const
	{
		if (m_storage == nullptr)
		{
			return false;
		}
		return m_storage->instance.load() != nullptr || m_currentInstance != nullptr;
	}

	bool ResourceObject::Compare(const ResourceObject& left, const ResourceObject& right, u32 index)
	{
		if (!left || !right)
		{
			return false;
		}

		if (left.m_storage->resourceType != right.m_storage->resourceType)
		{
			return false;
		}

		SK_ASSERT(false, "Not implemented");

		return false;
	}

	void ResourceObject::SetValue(u32 index, ConstPtr value, usize size) const
	{
		if (m_currentInstance)
		{
			UpdateHasValue(index, true);
			memcpy(&m_currentInstance[m_storage->resourceType->fields[index]->offset], value, size);
		}
	}

	void ResourceObject::ResetValue(u32 index)
	{
		if (m_currentInstance)
		{
			UpdateHasValue(index, false);
		}
	}

	void ResourceObject::UpdateHasValue(u32 index, bool hasValue) const
	{
		if (m_currentInstance)
		{
			*reinterpret_cast<bool*>(&m_currentInstance[sizeof(ResourceInstanceInfo) + index]) = hasValue;
		}
	}

	ConstPtr ResourceObject::GetPtr(u32 index) const
	{
		if (m_storage == nullptr) return nullptr;

		auto getPtrFromStorage = [&](const ResourceStorage* storage) -> ConstPtr
		{
			while (storage != nullptr)
			{
				if (ResourceInstance instance = storage->instance.load())
				{
					if (*reinterpret_cast<bool*>(&instance[sizeof(ResourceInstanceInfo) + index]))
					{
						return &instance[m_storage->resourceType->fields[index]->offset];
					}
				}
				storage = storage->prototype;
			}
			return nullptr;
		};

		if (m_currentInstance)
		{
			if (*reinterpret_cast<bool*>(&m_currentInstance[sizeof(ResourceInstanceInfo) + index]))
			{
				return &m_currentInstance[m_storage->resourceType->fields[index]->offset];
			}

			return getPtrFromStorage(m_storage->prototype);
		}

		return getPtrFromStorage(m_storage);
	}

	VoidPtr ResourceObject::GetMutPtr(u32 index) const
	{
		SK_ASSERT(m_currentInstance, "write instance is null");

		if (m_currentInstance)
		{
			return &m_currentInstance[m_storage->resourceType->fields[index]->offset];
		}
		return nullptr;
	}
}
