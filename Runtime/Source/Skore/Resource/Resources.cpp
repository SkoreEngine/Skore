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

#include "Resources.hpp"

#include <mutex>
#include <concurrentqueue.h>

#include "Skore/Events.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Queue.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/IO/FileSystem.hpp"

#define SK_PAGE(value)    u32((value)/SK_PAGE_SIZE)
#define SK_OFFSET(value)  (u32)((value) & (SK_PAGE_SIZE - 1))

namespace Skore
{
	struct UndoRedoChange
	{
		ResourceStorage* storage;
		ResourceInstance before;
		ResourceInstance after;

		~UndoRedoChange();
	};

	struct UndoRedoScope
	{
		String name;

		Array<std::unique_ptr<UndoRedoChange>> changes;

		void PushChange(ResourceStorage* storage, ResourceInstance before, ResourceInstance after);
		void Undo();
		void Redo();
	};

	struct NewItemsLookup
	{
		RID parent;
		u32 index;

		friend bool operator==(const NewItemsLookup& lhs, const NewItemsLookup& rhs)
		{
			return lhs.parent == rhs.parent
				&& lhs.index == rhs.index;
		}

		friend bool operator!=(const NewItemsLookup& lhs, const NewItemsLookup& rhs)
		{
			return !(lhs == rhs);
		}
	};

	template <>
	struct Hash<NewItemsLookup>
	{
		constexpr static bool hasHash = true;

		constexpr static usize Value(const NewItemsLookup& value)
		{
			usize seed{};
			HashCombine(seed, HashValue(value.parent), HashValue(value.index));
			return seed;
		}
	};


	namespace
	{
		struct DestroyResourcePayload
		{
			ResourceType*    type;
			ResourceInstance instance;
		};

		std::mutex                            resourceTypeMutex{};
		HashMap<TypeID, Array<ResourceType*>> typesById;
		HashMap<String, Array<ResourceType*>> typesByName;
		HashMap<TypeID, HashSet<TypeID>>      typesByAttribute;

		Logger& logger = Logger::GetLogger("Skore::Resources");

		struct ResourcePage
		{
			ResourceStorage elements[SK_PAGE_SIZE];
			bool            used[SK_PAGE_SIZE];
		};


		std::atomic_size_t counter{};
		usize              pageCount{};
		ResourcePage*      pages[SK_PAGE_SIZE]{};
		std::mutex         pageMutex{};

		std::mutex         byUUIDMutex{};
		HashMap<UUID, RID> byUUID{};

		std::mutex           byPathMutex{};
		HashMap<String, RID> byPath{};


		HashMap<TypeID, Array<RID>> resourceByType;

		moodycamel::ConcurrentQueue<DestroyResourcePayload> toCollectItems = moodycamel::ConcurrentQueue<DestroyResourcePayload>(100);


		RID GetFreeID()
		{
			u64 index = counter++;
			return RID{index};
		}

		SK_FINLINE ResourceStorage* GetStorage(RID rid)
		{
			return &pages[SK_PAGE(rid.id)]->elements[SK_OFFSET(rid.id)];
		}

		RID GetID(UUID uuid)
		{
			if (uuid)
			{
				std::unique_lock lock(byUUIDMutex);
				if (auto it = byUUID.Find(uuid))
				{
					return it->second;
				}
			}
			RID rid = GetFreeID();

			if (uuid)
			{
				std::unique_lock lock(byUUIDMutex);
				byUUID.Insert(uuid, rid);
			}
			return rid;
		}


		ResourceStorage* GetOrAllocate(RID rid, UUID uuid)
		{
			auto page = SK_PAGE(rid.id);
			auto offset = SK_OFFSET(rid.id);

			if (pages[page] == nullptr)
			{
				std::unique_lock lock(pageMutex);
				if (pages[page] == nullptr)
				{
					pages[page] = Alloc<ResourcePage>();
					pageCount++;
					memset(pages[page]->used, 0, sizeof(pages[page]->used));
				}
			}

			ResourceStorage* storage = &pages[page]->elements[offset];

			if (!pages[page]->used[offset])
			{
				pages[page]->used[offset] = true;
				new(storage) ResourceStorage{
					.rid = rid,
					.uuid = uuid
				};
			}
			return storage;
		}

		template <typename F>
		void IterateReferences(ResourceStorage* storage, F&& f)
		{
			ResourceObject object(storage, nullptr);
			for (ResourceField* field : storage->resourceType->GetFields())
			{
				if (field && object.HasValueOnThisObject(field->GetIndex()))
				{
					switch (field->GetType())
					{
						case ResourceFieldType::Reference:
							f(field->GetIndex(), field->GetType(), object.GetReference(field->GetIndex()));
							break;
						case ResourceFieldType::ReferenceArray:
							for (RID rid : object.GetReferenceArray(field->GetIndex()))
							{
								f(field->GetIndex(), field->GetType(), rid);
							}
							break;
						default:
							break;
					}
				}
			}
		}

		template <typename F>
		void IterateSubObjects(ResourceStorage* storage, F&& f)
		{
			ResourceObject object(storage, nullptr);
			for (ResourceField* field : storage->resourceType->GetFields())
			{
				if (field && object.HasValueOnThisObject(field->GetIndex()))
				{
					switch (field->GetType())
					{
						case ResourceFieldType::SubObject:
							f(field->GetIndex(), object.GetSubObject(field->GetIndex()));
							break;
						case ResourceFieldType::SubObjectList:
							object.IterateSubObjectList(field->GetIndex(), [&](RID rid)
							{
								f(field->GetIndex(), rid);
							});
							break;
						default:
							break;
					}
				}
			}
		}

		void ExecuteEvents(ResourceEventType type, ResourceStorage* resourceStorage, ResourceObject&& oldValue, ResourceObject&& newValue, UndoRedoScope* scope)
		{
			for (const ResourceEvent& eventStorage : resourceStorage->events[static_cast<u32>(type)])
			{
				eventStorage.function(oldValue, newValue, eventStorage.userData);
			}

			if (type != ResourceEventType::Changed)
			{
				return;
			}

			if (!resourceStorage->prototypeInstances.Empty())
			{
				for (ResourceField* field : resourceStorage->resourceType->GetFields())
				{
					if (field->GetType() == ResourceFieldType::SubObjectList)
					{
						struct CompareSubObjectListUserData
						{
							ResourceStorage* storage;
							ResourceField*   field;
							UndoRedoScope*   scope;
						};

						CompareSubObjectListUserData userData = {};
						userData.storage = resourceStorage;
						userData.field = field;
						userData.scope = scope;

						Resources::CompareSubObjectList(oldValue, newValue, field->GetIndex(), &userData, [](const CompareSubObjectListResult& result, VoidPtr userDataPtr)
						{
							CompareSubObjectListUserData& userData = *static_cast<CompareSubObjectListUserData*>(userDataPtr);

							if (result.type == CompareSubObjectSetType::Removed)
							{
								for (RID instance : userData.storage->prototypeInstances)
								{
									ResourceObject write = Resources::Write(instance);
									Array<RID>     removed = write.RemoveFromSubObjectListByPrototype(userData.field->GetIndex(), result.rid);
									write.Commit(userData.scope);

									for (RID removedInstance : removed)
									{
										Resources::Destroy(removedInstance, userData.scope);
									}
								}
							}
							else if (result.type == CompareSubObjectSetType::Added)
							{
								for (RID instance : userData.storage->prototypeInstances)
								{
									RID newSubObject = Resources::CreateFromPrototype(result.rid, UUID{}, userData.scope);

									ResourceObject write = Resources::Write(instance);
									write.AddToSubObjectList(userData.field->GetIndex(), newSubObject);
									write.Commit(userData.scope);
								}
							}
						});
					}
				}
			}

			if (resourceStorage->resourceType != nullptr)
			{
				for (const ResourceEvent& eventType : resourceStorage->resourceType->GetEvents())
				{
					eventType.function(oldValue, newValue, eventType.userData);
				}
			}

			IterateSubObjects(resourceStorage, [&](u32 index, RID subObject)
			{
				ResourceStorage* subOjectStorage = GetStorage(subObject);
				subOjectStorage->parent = resourceStorage;
				subOjectStorage->parentFieldIndex = index;

				return true;
			});
		}

		void UpdateVersion(ResourceStorage* resourceStorage)
		{
			ResourceStorage* current = resourceStorage;
			while (current != nullptr)
			{
				++current->version;

				ExecuteEvents(ResourceEventType::VersionUpdated,
				              current,
				              ResourceObject(nullptr, nullptr),
				              ResourceObject(current, current->instance.load()),
				              nullptr);

				current = current->parent;
			}
		}

		template <typename T>
		void IterateObjectSubObjects(ResourceStorage* resourceStorage, ResourceInstance instance, T&& func)
		{
			for (ResourceField* field : resourceStorage->resourceType->GetFields())
			{
				if (*reinterpret_cast<bool*>(&instance[sizeof(ResourceInstanceInfo) + field->GetIndex()]))
				{
					switch (field->GetType())
					{
						case ResourceFieldType::SubObject:
						{
							if (RID rid = *reinterpret_cast<RID*>(&instance[field->GetOffset()]))
							{
								func(field->GetIndex(), rid);
							}
							break;
						}
						case ResourceFieldType::SubObjectList:
						{
							const SubObjectList& subObjectList = *reinterpret_cast<SubObjectList*>(&instance[field->GetOffset()]);
							for (RID rid : subObjectList.subObjects)
							{
								func(field->GetIndex(), rid);
							}
							break;
						}
					}
				}
			}
		}

		void FinishCreation(ResourceStorage* storage)
		{
			if (storage->resourceType)
			{
				auto it = resourceByType.Find(storage->resourceType->GetID());
				if (it == resourceByType.end())
				{
					it = resourceByType.Emplace(storage->resourceType->GetID(), Array<RID>{}).first;
				}
				it->second.EmplaceBack(storage->rid);
			}
		}
	}

	RID CloneSubObject(ResourceStorage* parentStorage, u32 fieldIndex, RID origin, UndoRedoScope* scope)
	{
		ResourceStorage* originStorage = GetStorage(origin);

		RID clone = Resources::Clone(origin, originStorage->uuid ? UUID::RandomUUID() : UUID{}, scope);

		ResourceStorage* subOjectStorage = GetStorage(clone);
		subOjectStorage->parent = parentStorage;
		subOjectStorage->parentFieldIndex = fieldIndex;
		return clone;
	}

	//clone = recreate subobjects
	ResourceInstance CreateResourceInstanceClone(ResourceStorage* storage, ResourceInstance origin, UndoRedoScope* scope)
	{
		if (origin == nullptr || storage == nullptr || storage->resourceType == nullptr) return nullptr;

		ResourceInstance instance = storage->resourceType->Allocate();
		*reinterpret_cast<ResourceInstanceInfo*>(instance) = *reinterpret_cast<ResourceInstanceInfo*>(origin);
		memcpy(instance + sizeof(ResourceInstanceInfo), origin + sizeof(ResourceInstanceInfo), storage->resourceType->GetFields().Size());

		for (ResourceField* field : storage->resourceType->GetFields())
		{
			if (*reinterpret_cast<bool*>(&instance[sizeof(ResourceInstanceInfo) + field->GetIndex()]))
			{
				switch (field->GetType())
				{
					case ResourceFieldType::Blob:
						new(reinterpret_cast<ByteBuffer*>(&instance[field->GetOffset()])) ByteBuffer(*reinterpret_cast<ByteBuffer*>(&origin[field->GetOffset()]));
						break;
					case ResourceFieldType::ReferenceArray:
						new(reinterpret_cast<Array<RID>*>(&instance[field->GetOffset()])) Array(*reinterpret_cast<Array<RID>*>(&origin[field->GetOffset()]));
						break;
					case ResourceFieldType::SubObject:
					{
						RID clone = CloneSubObject(storage, field->GetIndex(), *reinterpret_cast<RID*>(&origin[field->GetOffset()]), scope);
						new(reinterpret_cast<RID*>(&instance[field->GetOffset()])) RID(clone);
						break;
					}
					case ResourceFieldType::SubObjectList:
					{
						const SubObjectList& subObjectList = *reinterpret_cast<SubObjectList*>(&origin[field->GetOffset()]);

						SubObjectList copySubObjectList = {};
						copySubObjectList.prototypeRemoved = subObjectList.prototypeRemoved;

						for (RID subobject : subObjectList.subObjects)
						{
							copySubObjectList.subObjects.EmplaceBack(CloneSubObject(storage, field->GetIndex(), subobject, scope));
						}
						new(reinterpret_cast<SubObjectList*>(&instance[field->GetOffset()])) SubObjectList{copySubObjectList};
						break;
					}
					case ResourceFieldType::String:
						new(reinterpret_cast<String*>(&instance[field->GetOffset()])) String(*reinterpret_cast<String*>(&origin[field->GetOffset()]));
						break;
					default:
						memcpy(&instance[field->GetOffset()], &origin[field->GetOffset()], field->GetSize());
						break;
				}
			}
		}
		return instance;
	}

	ResourceInstance CreateResourceInstanceCopy(ResourceType* type, ResourceInstance origin)
	{
		if (origin == nullptr || type == nullptr) return nullptr;

		ResourceInstance instance = type->Allocate();
		*reinterpret_cast<ResourceInstanceInfo*>(instance) = *reinterpret_cast<ResourceInstanceInfo*>(origin);
		memcpy(instance + sizeof(ResourceInstanceInfo), origin + sizeof(ResourceInstanceInfo), type->GetFields().Size());

		for (ResourceField* field : type->GetFields())
		{
			if (*reinterpret_cast<bool*>(&instance[sizeof(ResourceInstanceInfo) + field->GetIndex()]))
			{
				switch (field->GetType())
				{
					case ResourceFieldType::Blob:
						new(reinterpret_cast<ByteBuffer*>(&instance[field->GetOffset()])) ByteBuffer(*reinterpret_cast<ByteBuffer*>(&origin[field->GetOffset()]));
						break;
					case ResourceFieldType::ReferenceArray:
						new(reinterpret_cast<Array<RID>*>(&instance[field->GetOffset()])) Array(*reinterpret_cast<Array<RID>*>(&origin[field->GetOffset()]));
						break;
					case ResourceFieldType::SubObjectList:
						new(reinterpret_cast<SubObjectList*>(&instance[field->GetOffset()])) SubObjectList(*reinterpret_cast<SubObjectList*>(&origin[field->GetOffset()]));
						break;
					case ResourceFieldType::String:
						new(reinterpret_cast<String*>(&instance[field->GetOffset()])) String(*reinterpret_cast<String*>(&origin[field->GetOffset()]));
						break;
					default:
						memcpy(&instance[field->GetOffset()], &origin[field->GetOffset()], field->GetSize());
						break;
				}
			}
		}
		return instance;
	}

	void DestroyResourceInstance(ResourceType* type, ResourceInstance instance)
	{
		if (instance == nullptr) return;

		if (type)
		{
			for (ResourceField* field : type->GetFields())
			{
				if (field && *reinterpret_cast<bool*>(&instance[sizeof(ResourceInstanceInfo) + field->GetIndex()]))
				{
					switch (field->GetType())
					{
						case ResourceFieldType::Blob:
							reinterpret_cast<ByteBuffer*>(&instance[field->GetOffset()])->~ByteBuffer();
							break;
						case ResourceFieldType::ReferenceArray:
							reinterpret_cast<Array<RID>*>(&instance[field->GetOffset()])->~Array<RID>();
							break;
						case ResourceFieldType::SubObjectList:
							reinterpret_cast<SubObjectList*>(&instance[field->GetOffset()])->~SubObjectList();
							break;
						case ResourceFieldType::String:
							reinterpret_cast<String*>(&instance[field->GetOffset()])->~String();
							break;
						default:
							break;
					}
				}
			}
		}
		DestroyAndFree(instance);
	}

	ResourceType* Resources::CreateFromReflectType(ReflectType* reflectType)
	{
		ResourceTypeBuilder builder = Type(reflectType->GetProps().typeId, reflectType->GetName());
		for (ReflectField* field : reflectType->GetFields())
		{
			ResourceFieldInfo info = field->GetResourceFieldInfo();
			builder.Field(field->GetIndex(), field->GetName(), info.type, info.subType);
		}

		builder.Build();

		ResourceType* type = builder.GetResourceType();
		type->reflectType = reflectType;

		//default value
		if (ReflectConstructor* defaultConstructor = reflectType->GetDefaultConstructor())
		{
			RID              rid = GetID({});
			ResourceStorage* storage = GetOrAllocate(rid, {});
			storage->resourceType = type;
			storage->instance = nullptr;

			char    buffer[128];
			VoidPtr ptr = &buffer;

			if (reflectType->GetProps().size >= 128)
			{
				ptr = MemAlloc(reflectType->GetProps().size);
			}

			defaultConstructor->Construct(ptr, nullptr);
			ToResource(rid, ptr, nullptr);
			type->defaultValue = rid;

			if (reflectType->GetProps().size >= 128)
			{
				DestroyAndFree(ptr);
			}
		}

		return type;
	}

	ResourceTypeBuilder Resources::Type(TypeID typeId, StringView name)
	{
		auto it = typesById.Find(typeId);
		if (it == typesById.end())
		{
			it = typesById.Emplace(typeId, Array<ResourceType*>()).first;
		}

		auto it2 = typesByName.Find(name);
		if (it2 == typesByName.end())
		{
			it2 = typesByName.Emplace(name, Array<ResourceType*>()).first;
		}

		ResourceType* resourceType = Alloc<ResourceType>(typeId, name);

		it->second.EmplaceBack(resourceType);
		it2->second.EmplaceBack(resourceType);

		resourceType->version = it2->second.Size();

		return {resourceType};
	}

	ResourceType* Resources::FindTypeByID(TypeID typeId)
	{
		std::unique_lock lock(resourceTypeMutex);
		auto             it = typesById.Find(typeId);
		if (it != typesById.end())
		{
			if (!it->second.Empty())
			{
				return it->second.Back();
			}
		}
		return nullptr;
	}

	ResourceType* Resources::FindTypeByName(StringView name)
	{
		std::unique_lock lock(resourceTypeMutex);
		auto             it = typesByName.Find(name);
		if (it != typesByName.end())
		{
			if (!it->second.Empty())
			{
				return it->second.Back();
			}
		}

		return nullptr;
	}

	void ResourceAddTypeByAttribute(TypeID attributeId, TypeID resourceId)
	{
		std::unique_lock lock(resourceTypeMutex);
		auto             it = typesByAttribute.Find(attributeId);
		if (it == typesByAttribute.end())
		{
			it = typesByAttribute.Insert(attributeId, HashSet<TypeID>{}).first;
		}
		it->second.Insert(resourceId);
	}

	Array<TypeID> Resources::FindTypesByAttribute(TypeID attributeId)
	{
		std::unique_lock lock(resourceTypeMutex);
		Array<TypeID> ret;
		if (auto it = typesByAttribute.Find(attributeId))
		{
			ret.Reserve(it->second.Size());

			for (TypeID typeId : it->second)
			{
				ret.EmplaceBack(typeId);
			}
		}
		return ret;
	}

	RID Resources::Create(TypeID typeId, UUID uuid, UndoRedoScope* scope)
	{
		RID              rid = GetID(uuid);
		ResourceStorage* storage = GetOrAllocate(rid, uuid);
		storage->instance = nullptr;
		storage->resourceType = FindTypeByID(typeId);

		if (storage->resourceType == nullptr && typeId != 0)
		{
			if (ReflectType* reflectType = Reflection::FindTypeById(typeId))
			{
				storage->resourceType = CreateFromReflectType(reflectType);
			}
		}

		if (storage->resourceType && storage->resourceType->defaultValue)
		{
			ResourceStorage* defaultValueStorage = GetStorage(storage->resourceType->defaultValue);
			storage->instance = CreateResourceInstanceClone(storage, defaultValueStorage->instance.load(), scope);
		}

		FinishCreation(storage);

		if (scope && storage->instance)
		{
			scope->PushChange(storage, nullptr, storage->instance.load());
		}

		return rid;
	}

	RID ResourcesCreateFromPrototype(HashMap<NewItemsLookup, UUID>& newItems, RID rootPrototype, RID prototypeRid, UUID uuid, UndoRedoScope* scope)
	{
		ResourceStorage* prototype = GetStorage(prototypeRid);
		SK_ASSERT(prototype->resourceType, "prototype type cannot be null");

		if (!uuid && prototype->uuid)
		{
			uuid = UUID::RandomUUID();
		}

		RID rid = GetID(uuid);

		ResourceStorage* storage = GetOrAllocate(rid, uuid);
		storage->resourceType = prototype->resourceType;
		storage->prototype = prototype;
		prototype->prototypeInstances.Insert(rid);

		ResourceObject object = Resources::Write(rid);

		IterateSubObjects(prototype, [&](u32 index, RID subobject)
		{
			ResourceStorage* subobjectStorage = GetStorage(subobject);

			NewItemsLookup item = {
				.parent = subobjectStorage->parent->rid,
				.index = subobjectStorage->parentFieldIndex
			};

			UUID subobjectUUID = {};
			if (auto it = newItems.Find(item))
			{
				uuid = it->second;
			}

			RID subObjectPrototype = ResourcesCreateFromPrototype(newItems, rootPrototype, subobject, subobjectUUID, scope);
			if (!subobjectUUID)
			{
				newItems.Insert(item, Resources::GetUUID(subObjectPrototype));
			}

			if (ResourceField* field = storage->resourceType->GetFields()[index])
			{
				if (field->GetType() == ResourceFieldType::SubObjectList)
				{
					object.AddToSubObjectList(field->GetIndex(), subObjectPrototype);
				}
				else if (field->GetType() == ResourceFieldType::SubObject)
				{
					object.SetSubObject(field->GetIndex(), subObjectPrototype);
				}
			}
		});

		IterateReferences(prototype, [&](u32 index, ResourceFieldType type, RID reference)
		                  {
			                  if (Resources::IsParentOf(rootPrototype, reference))
			                  {
				                  ResourceStorage* refStorage = GetStorage(reference);

				                  NewItemsLookup item = {
					                  .parent = refStorage->parent->rid,
					                  .index = refStorage->parentFieldIndex
				                  };

				                  auto it = newItems.Find(item);
				                  if (it == newItems.end())
				                  {
					                  UUID newUUID = UUID::RandomUUID();
					                  it = newItems.Insert(item, newUUID).first;
				                  }

				                  if (type == ResourceFieldType::Reference)
				                  {
					                  object.SetReference(index, Resources::FindOrReserveByUUID(it->second));
				                  }
			                  }
		                  }
		);

		object.Commit(scope);

		FinishCreation(storage);

		if (scope)
		{
			scope->PushChange(storage, nullptr, storage->instance.load());
		}

		return rid;
	}

	RID Resources::CreateFromPrototype(RID prototypeRid, UUID uuid, UndoRedoScope* scope)
	{
		HashMap<NewItemsLookup, UUID> newItems;
		return ResourcesCreateFromPrototype(newItems, prototypeRid, prototypeRid, uuid, scope);
	}

	ResourceStorage* Resources::GetStorage(RID rid)
	{
		return Skore::GetStorage(rid);
	}

	RID Resources::Clone(RID origin, UUID uuid, UndoRedoScope* scope)
	{
		ResourceStorage* originStorage = GetStorage(origin);

		RID rid = GetID(uuid);

		ResourceStorage* storage = GetOrAllocate(rid, uuid);
		storage->resourceType = originStorage->resourceType;
		storage->prototype = originStorage->prototype;

		storage->instance = CreateResourceInstanceClone(storage, originStorage->instance.load(), scope);

		FinishCreation(storage);

		if (scope)
		{
			scope->PushChange(storage, nullptr, storage->instance.load());
		}

		return rid;
	}

	void Resources::Reset(RID rid, UndoRedoScope* scope)
	{
		ResourceStorage* storage = GetStorage(rid);

		ResourceInstance newInstance = nullptr;

		if (storage->resourceType && storage->resourceType->defaultValue)
		{
			ResourceStorage* defaultValueStorage = GetStorage(storage->resourceType->defaultValue);
			newInstance = CreateResourceInstanceClone(storage, defaultValueStorage->instance.load(), scope);
		}

		ResourceInstance oldInstance = storage->instance.exchange(newInstance);

		if (scope)
		{
			scope->PushChange(storage, oldInstance, newInstance);
		}

		UpdateVersion(storage);
		ExecuteEvents(ResourceEventType::Changed, storage, ResourceObject(storage, oldInstance), ResourceObject(storage, newInstance), scope);
	}

	void Resources::Destroy(RID rid, UndoRedoScope* scope)
	{
		ResourceStorage* storage = GetStorage(rid);

		if (storage->parent && storage->parentFieldIndex != U32_MAX && storage->parent->instance)
		{
			ResourceObject parentObject = Write(storage->parent->rid);
			parentObject.RemoveSubObject(storage->parentFieldIndex, rid);
			parentObject.Commit(scope);
		}

		if (ResourceInstance instance = storage->instance.exchange(nullptr))
		{
			if (scope)
			{
				scope->PushChange(storage, instance, nullptr);
			}

			ExecuteEvents(ResourceEventType::Changed, storage, ResourceObject(storage, instance), ResourceObject(storage, nullptr), scope);

			toCollectItems.enqueue(DestroyResourcePayload{
				.instance = instance
			});

			IterateObjectSubObjects(storage, instance, [scope](u32 index, RID subobject)
			{
				Destroy(subobject, scope);
			});
		}
	}

	u64 Resources::GetVersion(RID rid)
	{
		return GetStorage(rid)->version;
	}

	ResourceObject Resources::Write(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		SK_ASSERT(storage->resourceType, "type cannot be null");

		ResourceInstance instance = nullptr;

		if (ResourceInstance current = storage->instance.load())
		{
			instance = CreateResourceInstanceCopy(storage->resourceType, current);
		}
		else
		{
			instance = storage->resourceType->Allocate();
		}

		ResourceInstanceInfo& info = *reinterpret_cast<ResourceInstanceInfo*>(instance);
		info.readOnly = false;
		info.dataOnWrite = storage->instance.load();

		return ResourceObject{storage, instance};
	}

	ResourceObject Resources::Read(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		return ResourceObject{storage, nullptr};
	}

	bool Resources::HasValue(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		return storage->instance != nullptr;
	}

	RID Resources::GetParent(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		if (storage->parent)
		{
			return storage->parent->rid;
		}
		return {};
	}

	RID Resources::GetPrototype(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		return storage->prototype ? storage->prototype->rid : RID{};
	}

	UUID Resources::GetUUID(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		return storage->uuid;
	}

	ResourceType* Resources::GetType(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		return storage->resourceType;
	}

	RID Resources::FindByUUID(const UUID& uuid)
	{
		if (uuid)
		{
			std::unique_lock lock(byUUIDMutex);
			if (auto it = byUUID.Find(uuid))
			{
				return it->second;
			}
		}
		return {};
	}

	RID Resources::FindOrReserveByUUID(const UUID& uuid)
	{
		if (!uuid) return {};

		return GetID(uuid);
	}

	bool Resources::IsParentOf(RID parent, RID child)
	{
		ResourceStorage* parentStorage = GetStorage(parent);
		ResourceStorage* childStorage = GetStorage(child);

		while (childStorage != nullptr)
		{
			if (childStorage == parentStorage)
			{
				return true;
			}
			childStorage = childStorage->parent;
		}

		return false;
	}

	void Resources::SetPath(RID rid, StringView path)
	{
		std::unique_lock lock(byPathMutex);
		GetStorage(rid)->path = path;
		byPath.Insert(path, rid);
	}

	StringView Resources::GetPath(RID rid)
	{
		return GetStorage(rid)->path;
	}

	RID Resources::FindByPath(StringView path)
	{
		std::unique_lock lock(byPathMutex);
		if (auto it = byPath.Find(path))
		{
			return it->second;
		}
		return {};
	}

	Span<RID> Resources::GetResourceByType(TypeID typeId)
	{
		if (auto it = resourceByType.Find(typeId))
		{
			return it->second;
		}
		return {};
	}

	void Resources::Serialize(RID ridx, ArchiveWriter& writer)
	{
		RID        current = ridx;
		Queue<RID> pendingItems;

		writer.BeginSeq("objects");

		while (current)
		{
			ResourceStorage* storage = GetStorage(current);
			do
			{
				if (!storage->uuid) break;

				writer.BeginMap();

				ResourceObject set = Read(current);
				if (!set) return;

				writer.WriteString("_uuid", storage->uuid.ToString());
				writer.WriteString("_type", storage->resourceType->GetName());

				if (storage->parent && storage->parent->uuid && storage->parentFieldIndex != U32_MAX)
				{
					writer.WriteString("_parent", storage->parent->uuid.ToString());
					writer.WriteString("_parentField", storage->parent->resourceType->fields[storage->parentFieldIndex]->GetName());
				}

				if (storage->prototype && storage->prototype->uuid)
				{
					writer.WriteString("_prototype", storage->prototype->uuid.ToString());
				}

				for (ResourceField* field : storage->resourceType->GetFields())
				{
					if (set.HasValueOnThisObject(field->GetIndex()))
					{
						switch (field->GetType())
						{
							case ResourceFieldType::Bool:
								writer.WriteBool(field->GetName(), set.GetBool(field->GetIndex()));
								break;
							case ResourceFieldType::Int:
								writer.WriteInt(field->GetName(), set.GetInt(field->GetIndex()));
								break;
							case ResourceFieldType::UInt:
								writer.WriteUInt(field->GetName(), set.GetUInt(field->GetIndex()));
								break;
							case ResourceFieldType::Float:
								writer.WriteFloat(field->GetName(), set.GetFloat(field->GetIndex()));
								break;
							case ResourceFieldType::String:
								writer.WriteString(field->GetName(), set.GetString(field->GetIndex()));
								break;
							case ResourceFieldType::Vec2:
								writer.BeginMap(field->GetName());
								writer.WriteFloat("x", set.GetVec2(field->GetIndex()).x);
								writer.WriteFloat("y", set.GetVec2(field->GetIndex()).y);
								writer.EndMap();
								break;
							case ResourceFieldType::Vec3:
								writer.BeginMap(field->GetName());
								writer.WriteFloat("x", set.GetVec3(field->GetIndex()).x);
								writer.WriteFloat("y", set.GetVec3(field->GetIndex()).y);
								writer.WriteFloat("z", set.GetVec3(field->GetIndex()).z);
								writer.EndMap();
								break;
							case ResourceFieldType::Vec4:
								writer.BeginMap(field->GetName());
								writer.WriteFloat("x", set.GetVec4(field->GetIndex()).x);
								writer.WriteFloat("y", set.GetVec4(field->GetIndex()).y);
								writer.WriteFloat("z", set.GetVec4(field->GetIndex()).z);
								writer.WriteFloat("w", set.GetVec4(field->GetIndex()).w);
								writer.EndMap();
								break;
							case ResourceFieldType::Quat:
								writer.BeginMap(field->GetName());
								writer.WriteFloat("x", set.GetQuat(field->GetIndex()).x);
								writer.WriteFloat("y", set.GetQuat(field->GetIndex()).y);
								writer.WriteFloat("z", set.GetQuat(field->GetIndex()).z);
								writer.WriteFloat("w", set.GetQuat(field->GetIndex()).w);
								writer.EndMap();
								break;
							case ResourceFieldType::Color:
								writer.BeginMap(field->GetName());
								writer.WriteUInt("red", set.GetColor(field->GetIndex()).red);
								writer.WriteUInt("green", set.GetColor(field->GetIndex()).green);
								writer.WriteUInt("blue", set.GetColor(field->GetIndex()).blue);
								writer.WriteUInt("alpha", set.GetColor(field->GetIndex()).alpha);
								writer.EndMap();
								break;
							case ResourceFieldType::Enum:
							{
								if (ReflectType* enumType = Reflection::FindTypeById(field->GetSubType()))
								{
									if (ReflectValue* value = enumType->FindValueByCode(set.GetInt(field->GetIndex())))
									{
										writer.WriteString(field->GetName(), value->GetDesc());
									}
								}
								break;
							}
							case ResourceFieldType::Blob:
							{
								Span<u8> blob = set.GetBlob(field->GetIndex());
								writer.WriteBlob(field->GetName(), blob.Data(), blob.Size());
								break;
							}
							case ResourceFieldType::Reference:
								if (UUID uuid = GetUUID(set.GetReference(field->GetIndex())))
								{
									writer.WriteString(field->GetName(), uuid.ToString());
								}
								break;
							case ResourceFieldType::ReferenceArray:
								writer.BeginSeq(field->GetName());
								for (const RID& reference : set.GetReferenceArray(field->GetIndex()))
								{
									if (UUID uuid = GetUUID(reference))
									{
										writer.AddString(uuid.ToString());
									}
								}
								writer.EndSeq();
								break;
							case ResourceFieldType::SubObject:
								if (RID subobject = set.GetSubObject(field->GetIndex()))
								{
									pendingItems.Enqueue(subobject);
								}
								break;
							case ResourceFieldType::SubObjectList:
							{
								set.IterateSubObjectList(field->GetIndex(), [&](RID subobject)
								{
									pendingItems.Enqueue(subobject);
								});

								// bool started = false;
								//
								// set.IteratePrototypeRemoved(field->GetIndex(), [&](RID removed)
								// {
								// 	if (UUID uuid = GetUUID(removed))
								// 	{
								// 		if (!started)
								// 		{
								// 			writer.BeginMap(field->GetName());
								// 			writer.BeginSeq("_removed");
								// 			started = true;
								// 		}
								//
								// 		writer.AddString(uuid.ToString());
								// 	}
								// });
								//
								// if (started)
								// {
								// 	writer.EndSeq();
								// 	writer.EndMap();
								// }

								break;
							}
						}
					}
				}
				writer.EndMap();
			}
			while (false);

			current = {};
			if (!pendingItems.IsEmpty())
			{
				current = pendingItems.Dequeue();
			}
		}
		writer.EndSeq();
	}

	RID Resources::Deserialize(ArchiveReader& reader, UndoRedoScope* scope)
	{
		reader.BeginSeq("objects");

		RID root = {};

		while (reader.NextSeqEntry())
		{
			reader.BeginMap();

			UUID uuid = UUID::FromString(reader.ReadString("_uuid"));
			RID  rid = GetID(uuid);
			if (!root)
			{
				root = rid;
			}

			StringView typeName = reader.ReadString("_type");

			ResourceStorage* storage = GetOrAllocate(rid, uuid);
			storage->instance = nullptr;
			storage->resourceType = FindTypeByName(typeName);

			if (storage->resourceType == nullptr && !typeName.Empty())
			{
				if (ReflectType* reflectType = Reflection::FindTypeByName(typeName))
				{
					storage->resourceType = CreateFromReflectType(reflectType);
				}
			}

			if (RID prototype = FindOrReserveByUUID(UUID::FromString(reader.ReadString("_prototype"))))
			{
				storage->prototype = GetStorage(prototype);
				storage->prototype->prototypeInstances.Insert(rid);
			}

			if (storage->resourceType)
			{
				FinishCreation(storage);

				ResourceObject write = Write(rid);

				while (reader.NextMapEntry())
				{
					StringView fieldName = reader.GetCurrentKey();
					if (ResourceField* field = storage->resourceType->FindFieldByName(fieldName))
					{
						switch (field->GetType())
						{
							case ResourceFieldType::Bool:
								write.SetBool(field->GetIndex(), reader.GetBool());
								break;
							case ResourceFieldType::Int:
								write.SetInt(field->GetIndex(), reader.GetInt());
								break;
							case ResourceFieldType::UInt:
								write.SetUInt(field->GetIndex(), reader.GetUInt());
								break;
							case ResourceFieldType::Float:
								write.SetFloat(field->GetIndex(), reader.GetFloat());
								break;
							case ResourceFieldType::String:
								write.SetString(field->GetIndex(), reader.GetString());
								break;
							case ResourceFieldType::Vec2:
							{
								reader.BeginMap();
								Vec2 vec;
								vec.x = static_cast<Float>(reader.ReadFloat("x"));
								vec.y = static_cast<Float>(reader.ReadFloat("y"));
								write.SetVec2(field->GetIndex(), vec);
								reader.EndMap();
								break;
							}
							case ResourceFieldType::Vec3:
							{
								reader.BeginMap();
								Vec3 vec;
								vec.x = static_cast<Float>(reader.ReadFloat("x"));
								vec.y = static_cast<Float>(reader.ReadFloat("y"));
								vec.z = static_cast<Float>(reader.ReadFloat("z"));
								write.SetVec3(field->GetIndex(), vec);
								reader.EndMap();
								break;
							}
							case ResourceFieldType::Vec4:
							{
								reader.BeginMap();
								Vec4 vec;
								vec.x = static_cast<Float>(reader.ReadFloat("x"));
								vec.y = static_cast<Float>(reader.ReadFloat("y"));
								vec.z = static_cast<Float>(reader.ReadFloat("z"));
								vec.w = static_cast<Float>(reader.ReadFloat("w"));
								write.SetVec4(field->GetIndex(), vec);
								reader.EndMap();
								break;
							}
							case ResourceFieldType::Quat:
							{
								reader.BeginMap();
								Quat quat;
								quat.x = static_cast<Float>(reader.ReadFloat("x"));
								quat.y = static_cast<Float>(reader.ReadFloat("y"));
								quat.z = static_cast<Float>(reader.ReadFloat("z"));
								quat.w = static_cast<Float>(reader.ReadFloat("w"));
								write.SetQuat(field->GetIndex(), quat);
								reader.EndMap();
								break;
							}
							case ResourceFieldType::Color:
							{
								reader.BeginMap();
								Color color;
								color.red = reader.ReadUInt("red");
								color.green = reader.ReadUInt("green");
								color.blue = reader.ReadUInt("blue");
								color.alpha = reader.ReadUInt("alpha");
								write.SetColor(field->GetIndex(), color);
								reader.EndMap();
								break;
							}
							case ResourceFieldType::Enum:
							{
								if (ReflectType* enumType = Reflection::FindTypeById(field->GetSubType()))
								{
									if (ReflectValue* value = enumType->FindValueByName(reader.GetString()))
									{
										write.SetEnum(field->GetIndex(), value->GetCode());
									}
								}
								break;
							}
							case ResourceFieldType::Blob:
								write.SetBlob(field->GetIndex(), reader.GetBlob());
								break;
							case ResourceFieldType::Reference:
								if (RID rid = FindOrReserveByUUID(UUID::FromString(reader.GetString())))
								{
									write.SetReference(field->GetIndex(), rid);
								}
								break;
							case ResourceFieldType::ReferenceArray:
							{
								reader.BeginSeq();
								Array<RID> references;
								while (reader.NextSeqEntry())
								{
									references.EmplaceBack(FindOrReserveByUUID(UUID::FromString(reader.GetString())));
								}
								write.SetReferenceArray(field->GetIndex(), references);
								reader.EndSeq();
								break;
							}
								// case ResourceFieldType::SubObjectList:
								// {
								// 	reader.BeginMap();
								// 	while (reader.NextMapEntry())
								// 	{
								// 		StringView fieldName = reader.GetCurrentKey();
								// 		if (fieldName == "_removed")
								// 		{
								// 			reader.BeginSeq();
								// 			while (reader.NextSeqEntry())
								// 			{
								// 				if (UUID uuid = UUID::FromString(reader.GetString()))
								// 				{
								// 					write.RemoveFromPrototypeSubObjectSet(field->GetIndex(), FindOrReserveByUUID(uuid));
								// 				}
								// 			}
								// 			reader.EndSeq();
								// 		}
								// 	}
								// 	reader.EndMap();
								// }
						}
					}
				}

				write.Commit(scope);

				if (RID parent = FindByUUID(UUID::FromString(reader.ReadString("_parent"))))
				{
					ResourceStorage* parentStorage = GetStorage(parent);
					if (parentStorage->resourceType)
					{
						if (ResourceField* field = parentStorage->resourceType->FindFieldByName(reader.ReadString("_parentField")))
						{
							ResourceObject parentObject = Write(parent);
							if (field->GetType() == ResourceFieldType::SubObjectList)
							{
								parentObject.AddToSubObjectList(field->GetIndex(), rid);
							}
							else if (field->GetType() == ResourceFieldType::SubObject)
							{
								parentObject.SetSubObject(field->GetIndex(), rid);
							}
							parentObject.Commit(scope);
						}
					}
				}
			}

			reader.EndMap();
		}

		reader.EndSeq();

		return root;
	}

	bool Resources::ToResource(RID rid, ConstPtr instance, UndoRedoScope* scope, VoidPtr userData)
	{
		ResourceStorage* storage = GetStorage(rid);
		if (!storage->resourceType) return false;

		ReflectType* reflectType = storage->resourceType->GetReflectType();

		if (!instance || !reflectType || !rid) return false;


		if (ResourceObject resourceObject = Write(rid))
		{
			for (ReflectField* field : reflectType->GetFields())
			{
				field->ToResource(resourceObject, field->GetIndex(), instance, scope, userData);
			}
			resourceObject.Commit(scope);
		}

		return true;
	}

	bool Resources::FromResource(RID rid, VoidPtr instance, VoidPtr userData)
	{
		if (!rid) return false;
		ResourceObject resourceObject = Read(rid);
		return FromResource(resourceObject, instance, userData);
	}

	bool Resources::FromResource(const ResourceObject& resourceObject, VoidPtr instance, VoidPtr userData)
	{
		if (!resourceObject) return false;
		ResourceStorage* storage = resourceObject.GetStorage();
		if (!storage->resourceType) return false;
		ReflectType* reflectType = storage->resourceType->GetReflectType();
		if (!instance || !reflectType) return false;

		if (!resourceObject) return false;

		for (ReflectField* field : reflectType->GetFields())
		{
			field->FromResource(resourceObject, field->GetIndex(), instance, userData);
		}

		return true;
	}

	Array<CompareSubObjectListResult> Resources::CompareSubObjectList(const ResourceObject& oldObject, const ResourceObject& newObject, u32 index)
	{
		Array<CompareSubObjectListResult> results;

		CompareSubObjectList(oldObject, newObject, index, &results, [](const CompareSubObjectListResult& result, VoidPtr userData)
		{
			static_cast<Array<CompareSubObjectListResult>*>(userData)->EmplaceBack(result);
		});

		return results;
	}

	void Resources::CompareSubObjectList(const ResourceObject& oldObject, const ResourceObject& newObject, u32 index, VoidPtr userData, FnCompareSubObjectListCallback callback)
	{
		//check added
		newObject.IterateSubObjectList(index, [&](RID rid)
		{
			if (!oldObject.HasOnSubObjectList(index, rid))
			{
				callback(CompareSubObjectListResult{
					         CompareSubObjectSetType::Added,
					         rid
				         }, userData);
			}
		});

		//check removed
		oldObject.IterateSubObjectList(index, [&](RID rid)
		{
			if (!newObject.HasOnSubObjectList(index, rid))
			{
				callback(CompareSubObjectListResult{
					         CompareSubObjectSetType::Removed,
					         rid
				         }, userData);
			}
		});
	}


	void Resources::GarbageCollect()
	{
		DestroyResourcePayload payload{};
		while (toCollectItems.try_dequeue(payload))
		{
			DestroyResourceInstance(payload.type, payload.instance);
		}
	}

	void DoReflectionUpdated()
	{
		// for (auto it: typesById)
		// {
		// 	it.second
		// }
	}

	void SK_API ResourceInit()
	{
		Event::Bind<OnReflectionUpdated, DoReflectionUpdated>();
		Resources::Create(0);
	}

	void SK_API ResourceShutdown()
	{
		Resources::GarbageCollect();

		for (u64 i = 0; i < counter; ++i)
		{
			ResourceStorage& storage = pages[SK_PAGE(i)]->elements[SK_OFFSET(i)];
			DestroyResourceInstance(storage.resourceType, storage.instance.load());
		}

		for (u64 i = 0; i < pageCount; ++i)
		{
			if (!pages[i])
			{
				break;
			}
			DestroyAndFree(pages[i]);
			pages[i] = nullptr;
		}

		for (const auto& it : typesById)
		{
			for (ResourceType* type : it.second)
			{
				DestroyAndFree(type);
			}
		}

		typesById.Clear();
		typesByName.Clear();

		byUUID.Clear();
		// byPath.Clear();
		// resourceByType.Clear();
		counter = 0;
		pageCount = 0;

		Event::Unbind<OnReflectionUpdated, DoReflectionUpdated>();
	}

	void ResourceRemoveParent(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		storage->parent = {};
		storage->parentFieldIndex = U32_MAX;
		UpdateVersion(storage);
	}

	void ResourceCommit(ResourceStorage* storage, ResourceInstance instance, UndoRedoScope* scope)
	{
		ResourceInstanceInfo& info = *reinterpret_cast<ResourceInstanceInfo*>(instance);
		info.readOnly = true;

		if (info.dataOnWrite)
		{
			if (storage->instance.compare_exchange_strong(info.dataOnWrite, instance))
			{
				if (scope)
				{
					scope->PushChange(storage, info.dataOnWrite, instance);
				}

				toCollectItems.enqueue(DestroyResourcePayload{
					.type = storage->resourceType,
					.instance = info.dataOnWrite
				});
			}
		}
		else
		{
			if (scope)
			{
				scope->PushChange(storage, nullptr, instance);
			}
			storage->instance = instance;
		}

		UpdateVersion(storage);
		ExecuteEvents(ResourceEventType::Changed, storage, ResourceObject(storage, info.dataOnWrite), ResourceObject(storage, instance), scope);
	}

	UndoRedoChange::~UndoRedoChange()
	{
		DestroyResourceInstance(storage->resourceType, before);
		DestroyResourceInstance(storage->resourceType, after);
	}


	void UndoRedoScope::PushChange(ResourceStorage* storage, ResourceInstance before, ResourceInstance after)
	{
		std::unique_ptr<UndoRedoChange> change = std::make_unique<UndoRedoChange>(storage);

		change->before = CreateResourceInstanceCopy(storage->resourceType, before);
		change->after = CreateResourceInstanceCopy(storage->resourceType, after);

		changes.EmplaceBack(Traits::Move(change));
	}

	void UndoRedoScope::Undo()
	{
		for (u32 i = changes.Size(); i > 0; --i)
		{
			const auto& action = changes[i - 1];

			ResourceInstance newInstance = CreateResourceInstanceCopy(action->storage->resourceType, action->before);
			ResourceInstance oldInstance = action->storage->instance.exchange(newInstance);

			UpdateVersion(action->storage);

			ExecuteEvents(ResourceEventType::Changed, action->storage, ResourceObject(action->storage, oldInstance), ResourceObject(action->storage, newInstance), nullptr);

			toCollectItems.enqueue({
				.type = action->storage->resourceType,
				.instance = oldInstance
			});
		}
	}

	void UndoRedoScope::Redo()
	{
		for (const auto& action : changes)
		{
			ResourceInstance newInstance = CreateResourceInstanceCopy(action->storage->resourceType, action->after);
			ResourceInstance oldInstance = action->storage->instance.exchange(newInstance);

			UpdateVersion(action->storage);

			ExecuteEvents(ResourceEventType::Changed, action->storage, ResourceObject(action->storage, oldInstance), ResourceObject(action->storage, newInstance), nullptr);

			toCollectItems.enqueue({
				.type = action->storage->resourceType,
				.instance = oldInstance
			});
		}
	}


	UndoRedoScope* Resources::CreateScope(StringView name)
	{
		return Alloc<UndoRedoScope>(name);
	}

	void Resources::DestroyScope(UndoRedoScope* scope)
	{
		DestroyAndFree(scope);
	}

	void Resources::Undo(UndoRedoScope* scope)
	{
		scope->Undo();
	}

	void Resources::Redo(UndoRedoScope* scope)
	{
		scope->Redo();
	}

	StringView Resources::GetScopeName(UndoRedoScope* scope)
	{
		return scope->name;
	}

	void Resources::LoadPackage(StringView packageFile)
	{
		Array<u8> buffer;
		FileSystem::ReadFileAsByteArray(packageFile, buffer);
		BinaryArchiveReader reader{buffer};
		reader.BeginSeq("assets");
		{
			while (reader.NextSeqEntry())
			{
				reader.BeginMap();
				String pathId = reader.ReadString("pathId");
				RID    rid = Deserialize(reader);
				logger.Debug("asset {} loaded with rid {} ", pathId, rid.id);
				if (rid)
				{
					SetPath(rid, pathId);
				}
				reader.EndMap();
			}
		}
		reader.EndSeq();
	}
}
