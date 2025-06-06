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
#include "Skore/Core/HashSet.hpp"


namespace Skore
{
	void ResourceCommit(ResourceStorage* storage, ResourceInstance instance, UndoRedoScope* scope);
	void DestroyResourceInstance(ResourceType* resourceType, ResourceInstance instance);


	ResourceObject::ResourceObject(ResourceStorage* storage, ResourceInstance writeInstance) : storage(storage), writeInstance(writeInstance) {}

	ResourceObject::~ResourceObject()
	{
		if (writeInstance && !reinterpret_cast<ResourceInstanceInfo*>(writeInstance)->readOnly)
		{
			DestroyResourceInstance(storage->resourceType, writeInstance);
		}
	}

	void ResourceObject::SetBool(u32 index, bool value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Bool, "Invalid field type");
		SetValue(index, &value, sizeof(bool));
	}

	void ResourceObject::SetInt(u32 index, i64 value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Int, "Invalid field type");
		SetValue(index, &value, sizeof(i64));
	}

	void ResourceObject::SetUInt(u32 index, u64 value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::UInt, "Invalid field type");
		SetValue(index, &value, sizeof(u64));
	}

	void ResourceObject::SetFloat(u32 index, f64 value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Float, "Invalid field type");
		SetValue(index, &value, sizeof(f64));
	}

	void ResourceObject::SetString(u32 index, StringView value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::String, "Invalid field type");
		if (String* str = GetMutPtr<String>(index))
		{
			*str = value;
		}
	}

	void ResourceObject::SetVec2(u32 index, Vec2 value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Vec2, "Invalid field type");
		SetValue(index, &value, sizeof(Vec2));
	}

	void ResourceObject::SetVec3(u32 index, Vec3 value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Vec3, "Invalid field type");
		SetValue(index, &value, sizeof(Vec3));
	}

	void ResourceObject::SetVec4(u32 index, Vec4 value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Vec4, "Invalid field type");
		SetValue(index, &value, sizeof(Vec4));
	}

	void ResourceObject::SetQuat(u32 index, Quat value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Quat, "Invalid field type");
		SetValue(index, &value, sizeof(Quat));
	}

	void ResourceObject::SetColor(u32 index, Color value)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Color, "Invalid field type");
		SetValue(index, &value, sizeof(Color));
	}

	void ResourceObject::SetEnum(u32 index, i64 enumValue)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Enum, "Invalid field type");
		SetValue(index, &enumValue, sizeof(i64));
	}

	void ResourceObject::SetReference(u32 index, RID rid)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::Reference, "Invalid field type");
		SetValue(index, &rid, sizeof(RID));
	}

	void ResourceObject::SetReferenceArray(u32 index, Span<RID> refs)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (Array<RID>* arr = GetMutPtr<Array<RID>>(index))
		{
			*arr = refs;
		}
	}

	void ResourceObject::AddToReferenceArray(u32 index, RID ref)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (Array<RID>* arr = GetMutPtr<Array<RID>>(index))
		{
			arr->EmplaceBack(ref);
		}
	}

	void ResourceObject::ClearReferenceArray(u32 index)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::ReferenceArray, "Invalid field type");
		if (HasValueOnThisObject(index))
		{
			if (Array<RID>* arr = GetMutPtr<Array<RID>>(index))
			{
				arr->Clear();
			}
		}
	}

	void ResourceObject::SetSubObject(u32 index, RID subObject)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObject, "Invalid field type");
		SetValue(index, &subObject, sizeof(RID));
	}

	void ResourceObject::AddToSubObjectSet(u32 index, RID subObject)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
		{
			subObjectSet->subObjects.Emplace(subObject);
		}
	}

	void ResourceObject::AddToSubObjectSet(u32 index, Span<RID> subObjects)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
		{
			for (RID subObject : subObjects)
			{
				subObjectSet->subObjects.Emplace(subObject);
			}
		}
	}

	void ResourceObject::RemoveFromSubObjectSet(u32 index, RID subObject)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (HasValueOnThisObject(index))
		{
			if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
			{
				subObjectSet->subObjects.Erase(subObject);
			}
		}
	}

	void ResourceObject::RemoveFromSubObjectSet(u32 index, const Span<RID>& subObjects)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (HasValueOnThisObject(index))
		{
			if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
			{
				for (RID subObject : subObjects)
				{
					subObjectSet->subObjects.Erase(subObject);
				}
			}
		}
	}

	void ResourceObject::ClearSubObjectSet(u32 index)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (HasValueOnThisObject(index))
		{
			if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
			{
				subObjectSet->subObjects.Clear();
			}
		}
	}

	usize ResourceObject::GetRemoveFromPrototypeSubObjectSetCount(u32 index) const
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (const SubObjectSet* subObjectSet = GetPtr<SubObjectSet>(index))
		{
			return subObjectSet->prototypeRemoved.Size();
		}
		return 0;
	}

	void ResourceObject::GetRemoveFromPrototypeSubObjectSet(u32 index, Span<RID> remove) const
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (const SubObjectSet* subObjectSet = GetPtr<SubObjectSet>(index))
		{
			usize i = 0;
			for (RID removed : subObjectSet->prototypeRemoved)
			{
				remove[i++] = removed;
			}
		}
	}


	void ResourceObject::RemoveFromPrototypeSubObjectSet(u32 index, RID remove)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
		{
			subObjectSet->prototypeRemoved.Emplace(remove);
		}
	}

	void ResourceObject::RemoveFromPrototypeSubObjectSet(u32 index, const Span<RID>& remove)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
		{
			for (RID subObject : remove)
			{
				subObjectSet->prototypeRemoved.Emplace(subObject);
			}
		}
	}

	void ResourceObject::CancelRemoveFromPrototypeSubObjectSet(u32 index, RID remove)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
		{
			subObjectSet->prototypeRemoved.Erase(remove);
		}
	}

	void ResourceObject::CancelRemoveFromPrototypeSubObjectSet(u32 index, const Span<RID>& remove)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
		{
			for (RID subObject : remove)
			{
				subObjectSet->prototypeRemoved.Erase(subObject);
			}
		}
	}

	void ResourceObject::ClearRemoveFromPrototypeSubObjectSet(u32 index)
	{
		SK_ASSERT(storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet, "Invalid field type");
		if (SubObjectSet* subObjectSet = GetMutPtr<SubObjectSet>(index))
		{
			subObjectSet->prototypeRemoved.Clear();
		}
	}

	void ResourceObject::RemoveSubObject(u32 index, RID rid)
	{
		if (storage->resourceType->fields[index]->type == ResourceFieldType::SubObject)
		{
			if (const RID* value = GetPtr<RID>(index); value != nullptr && *value == rid)
			{
				UpdateHasValue(index, false);
			}
		}
		else if (storage->resourceType->fields[index]->type == ResourceFieldType::SubObjectSet)
		{
			RemoveFromSubObjectSet(index, rid);
		}
	}

	bool ResourceObject::HasValue(u32 index) const
	{
		return GetPtr(index) != nullptr;
	}

	bool ResourceObject::HasValueOnThisObject(u32 index) const
	{
		if (ResourceInstance instance = writeInstance ? writeInstance : storage->instance.load())
		{
			return *reinterpret_cast<bool*>(&instance[sizeof(ResourceInstanceInfo) + index]);
		}
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

	Span<RID> ResourceObject::GetReferenceArray(u32 index) const
	{
		if (const Array<RID>* value = GetPtr<Array<RID>>(index))
		{
			return *value;
		}
		return {};
	}

	bool ResourceObject::HasOnReferenceArray(u32 index, RID rid) const
	{
		if (const Array<RID>* value = GetPtr<Array<RID>>(index))
		{
			return FindFirst(value->begin(), value->end(), rid) != nullptr;
		}
		return false;
	}

	usize ResourceObject::GetSubObjectSetCount(u32 index) const
	{
		usize count = 0;

		IterateSubObjectSet(index, true, [&](RID rid)
		{
			count++;
			return true;
		});

		return count;
	}

	void ResourceObject::GetSubObjectSet(u32 index, Span<RID> subObjects) const
	{
		usize i = 0;

		IterateSubObjectSet(index, true, [&](RID rid)
		{
			subObjects[i] = rid;
			i++;
			return true;
		});
	}

	Array<RID> ResourceObject::GetSubObjectSetAsArray(u32 index) const
	{
		Array<RID> subobjects;
		subobjects.Resize(GetSubObjectSetCount(index));
		GetSubObjectSet(index, subobjects);
		return subobjects;
	}

	bool ResourceObject::HasSubObjectSet(u32 index, RID rid) const
	{
		bool found = false;
		IterateSubObjectSet(index, true, [&](RID subobect)
		{
			if (rid == subobect)
			{
				found = true;
				return false;
			}
			return true;
		});

		return found;
	}

	void ResourceObject::IterateSubObjectSet(u32 index, bool prototypeIterate, FnRIDCallback callback, VoidPtr userData) const
	{
		ResourceStorage* currentStorage = storage;
		while (currentStorage != nullptr)
		{
			if (ResourceInstance currentInstance = currentStorage->instance.load())
			{
				if (*reinterpret_cast<bool*>(&currentInstance[sizeof(ResourceInstanceInfo) + index]))
				{
					const SubObjectSet* subObjectSet = reinterpret_cast<SubObjectSet*>(&currentInstance[storage->resourceType->fields[index]->offset]);
					for (RID rid: subObjectSet->subObjects)
					{
						if (ValidSubObjectOnSet(currentStorage, index, rid))
						{
							if (!callback(rid, userData))
							{
								return;
							}
						}
					}
				}
			}
			if (!prototypeIterate) break;
			currentStorage = currentStorage->prototype;
		}
	}

	RID ResourceObject::GetRID() const
	{
		return storage->rid;
	}

	RID ResourceObject::GetPrototype() const
	{
		return storage->prototype ? storage->prototype->rid : RID();
	}

	UUID ResourceObject::GetUUID() const
	{
		return storage->uuid;
	}

	ResourceType* ResourceObject::GetType() const
	{
		return storage->resourceType;
	}

	ResourceStorage* ResourceObject::GetStorage() const
	{
		return storage;
	}

	u64 ResourceObject::GetVersion() const
	{
		return storage->version;
	}

	void ResourceObject::Commit(UndoRedoScope* scope)
	{
		ResourceCommit(storage, writeInstance, scope);
	}

	ResourceObject::operator bool() const
	{
		return storage->instance.load() != nullptr || writeInstance != nullptr;
	}

	void ResourceObject::SetValue(u32 index, ConstPtr value, usize size) const
	{
		if (writeInstance)
		{
			UpdateHasValue(index, true);
			memcpy(&writeInstance[storage->resourceType->fields[index]->offset], value, size);
		}
	}

	void ResourceObject::UpdateHasValue(u32 index, bool hasValue) const
	{
		if (writeInstance)
		{
			*reinterpret_cast<bool*>(&writeInstance[sizeof(ResourceInstanceInfo) + index]) = hasValue;
		}
	}

	ConstPtr ResourceObject::GetPtr(u32 index) const
	{
		if (storage == nullptr) return nullptr;

		ResourceStorage* currentStorage = storage;

		while (currentStorage != nullptr)
		{
			if (ResourceInstance currentInstance = currentStorage->instance.load())
			{
				if (*reinterpret_cast<bool*>(&currentInstance[sizeof(ResourceInstanceInfo) + index]))
				{
					return &currentInstance[storage->resourceType->fields[index]->offset];
				}
			}
			currentStorage = currentStorage->prototype;
		}
		return nullptr;
	}

	VoidPtr ResourceObject::GetMutPtr(u32 index) const
	{
		SK_ASSERT(writeInstance, "write instance is null");

		if (writeInstance)
		{
			return &writeInstance[storage->resourceType->fields[index]->offset];
		}
		return nullptr;
	}

	bool ResourceObject::ValidSubObjectOnSet(const ResourceStorage* readingStorage, u32 index, RID rid) const
	{
		if (readingStorage == storage) return true;


		ResourceStorage* currentStorage = storage;
		while (currentStorage != nullptr)
		{
			if (ResourceInstance currentInstance = currentStorage->instance.load())
			{
				if (*reinterpret_cast<bool*>(&currentInstance[sizeof(ResourceInstanceInfo) + index]))
				{
					const SubObjectSet* subObjectSet = reinterpret_cast<SubObjectSet*>(&currentInstance[storage->resourceType->fields[index]->offset]);
					if (subObjectSet->prototypeRemoved.Has(rid))
					{
						return false;
					}
				}
			}

			currentStorage = currentStorage->prototype;

			if (currentStorage == readingStorage)
			{
				break;
			}
		}

		return true;
	}
}
