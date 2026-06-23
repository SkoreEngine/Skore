#include "Skore/Resource/Resources.hpp"

#include <mutex>
#include <concurrentqueue.h>

#include "Skore/Events.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Queue.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

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

	Array<RID> ResourceStorage::GetPrototypeInstancesSafe()
	{
		Array<RID> instances;
		{
			std::unique_lock lock(prototypeInstancesMutex);
			instances.Reserve(prototypeInstances.Size());
			for (RID rid : prototypeInstances)
			{
				instances.EmplaceBack(rid);
			}
		}
		return instances;
	}

	namespace
	{
		struct DestroyResourcePayload
		{
			ResourceType*    type;
			ResourceInstance instance;
		};

		struct CloneContext
		{
			RID origin;
			HashMap<RID, Pair<RID, UUID>> cloneMap;
		};

		struct ResourceLoaderContext
		{
			ResourceLoader* instance;
		};

		std::mutex                             resourceTypeMutex{};
		HashMap<TypeID, Array<ResourceType*>>  typesById;
		HashMap<String, Array<ResourceType*>>  typesByName;
		HashMap<TypeID, HashSet<TypeID>>       typesByAttribute;
		HashMap<TypeID, ResourceLoaderContext> resourceLoaders;

		Array<FileHandler> fileHandlers;

		Logger& logger = Logger::GetLogger("Skore::Resources", LogLevel::Debug);

		struct ImportedResourceField
		{
			u32               index;
			String            name;
			ResourceFieldType type;
			TypeID            subType;
		};

		bool IsTypeRegisteredNoLock(StringView typeName)
		{
			auto it = typesByName.Find(typeName);
			if (it == typesByName.end())
			{
				return false;
			}

			for (ResourceType* existingType : it->second)
			{
				if (existingType != nullptr)
				{
					return true;
				}
			}

			return false;
		}

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

		moodycamel::ConcurrentQueue<DestroyResourcePayload> toCollectItems = moodycamel::ConcurrentQueue<DestroyResourcePayload>(100);

		struct PendingEvent
		{
			ResourceEventType type;
			ResourceStorage*  subject;
			ResourceStorage*  oldStorage;
			ResourceInstance  oldInstance;
			ResourceStorage*  newStorage;
			ResourceInstance  newInstance;
		};

		moodycamel::ConcurrentQueue<PendingEvent> pendingEvents = moodycamel::ConcurrentQueue<PendingEvent>(100);


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
			ResourceStorage* oldStorage = oldValue.GetStorage();
			ResourceStorage* newStorage = newValue.GetStorage();

			pendingEvents.enqueue(PendingEvent{
				.type = type,
				.subject = resourceStorage,
				.oldStorage = oldStorage,
				.oldInstance = oldStorage ? oldValue.GetInstance() : nullptr,
				.newStorage = newStorage,
				.newInstance = newStorage ? newValue.GetInstance() : nullptr,
			});

			if (oldValue && type == ResourceEventType::Changed)
			{
				Array<RID> instances;
				{
					std::unique_lock lock(resourceStorage->prototypeInstancesMutex);
					instances.Reserve(resourceStorage->prototypeInstances.Size());
					for (RID instance : resourceStorage->prototypeInstances)
					{
						instances.EmplaceBack(instance);
					}
				}

				if (!instances.Empty())
				{
					for (ResourceField* field : resourceStorage->resourceType->GetFields())
					{
						if (field && field->GetType() == ResourceFieldType::SubObjectList)
						{
							for (RID instance : instances)
							{
								Array<RID> toRemove;
								Array<RID> toAdd;
								{
									ResourceObject instanceObjRead = Resources::Read(instance);
									instanceObjRead.IterateSubObjectList(field->GetIndex(), [&](RID rid)
									{
										if (RID prototype = Resources::GetPrototype(rid))
										{
											if (!newValue.HasOnSubObjectList(field->GetIndex(), prototype))
											{
												toRemove.EmplaceBack(prototype);
											}
										}
									});
								}

								{
									newValue.IterateSubObjectList(field->GetIndex(), [&](RID prototype)
									{
										bool found = false;
										ResourceObject instanceObjRead = Resources::Read(instance);
										instanceObjRead.IterateSubObjectList(field->GetIndex(), [&](RID rid)
										{
											if (Resources::GetPrototype(rid) == prototype)
											{
												found = true;
											}
										});
										if (!found)
										{
											toAdd.EmplaceBack(prototype);
										}
									});
								}

								if (!toAdd.Empty() || !toRemove.Empty())
								{
									ResourceObject instanceWrite = Resources::Write(instance);

									for (RID add: toAdd)
									{
										RID newSubObject = Resources::CreateFromPrototype(add, Resources::GetUUID(add) != UUID{} ? UUID::RandomUUID() : UUID{}, scope);
										instanceWrite.AddToSubObjectList(field->GetIndex(), newSubObject);
									}

									for (RID remove: toRemove)
									{
										Array<RID> removed = instanceWrite.RemoveFromSubObjectListByPrototype(field->GetIndex(), remove);
										for (RID removedInstance : removed)
										{
											Resources::Destroy(removedInstance, scope);
										}
									}
									instanceWrite.Commit(scope);
								}
							}
						}
					}
				}
			}

			if (type == ResourceEventType::Changed)
			{
				IterateSubObjects(resourceStorage, [&](u32 index, RID subObject)
				{
					ResourceStorage* subOjectStorage = GetStorage(subObject);
					subOjectStorage->parent = resourceStorage;
					subOjectStorage->parentFieldIndex = index;

					return true;
				});
			}
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

		RID GetReference(CloneContext& context, RID reference)
		{
			if (Resources::IsParentOf(context.origin, reference))
			{
				if (auto it = context.cloneMap.Find(reference))
				{
					return it->second.first;
				}

				ResourceStorage* storageOrigin = GetStorage(reference);

				UUID uuid = storageOrigin->uuid ? UUID::RandomUUID() : UUID{};
				RID  clone = GetID(uuid);
				context.cloneMap[reference] = MakePair(clone, uuid);
				return clone;
			}
			return reference;
		}
	}

	ResourceInstance CreateResourceInstanceClone(CloneContext& context, ResourceStorage* storage, ResourceInstance origin, UndoRedoScope* scope);

	void CloneInternal(CloneContext& context, RID origin, RID dest, UUID uuid, UndoRedoScope* scope)
	{
		ResourceStorage* originStorage = GetStorage(origin);

		ResourceStorage* storage = GetOrAllocate(dest, uuid);
		storage->resourceType = originStorage->resourceType;
		storage->resourceTypeVersion = originStorage->resourceTypeVersion;
		storage->prototype = originStorage->prototype;

		storage->instance = CreateResourceInstanceClone(context, storage, originStorage->instance.load(), scope);

		if (scope)
		{
			scope->PushChange(storage, nullptr, storage->instance.load());
		}
	}

	RID CloneSubObject(CloneContext& context, ResourceStorage* parentStorage, u32 fieldIndex, RID origin, UndoRedoScope* scope)
	{
		ResourceStorage* originStorage = GetStorage(origin);

		auto it = context.cloneMap.Find(origin);
		if (it == context.cloneMap.end())
		{
			UUID uuid = originStorage->uuid ? UUID::RandomUUID() : UUID{};
			RID clone = GetID(uuid);
			it = context.cloneMap.Insert(origin, MakePair(clone, uuid)).first;
		}

		CloneInternal(context, origin, it->second.first, it->second.second, scope);

		ResourceStorage* subOjectStorage = GetStorage(it->second.first);
		subOjectStorage->parent = parentStorage;
		subOjectStorage->parentFieldIndex = fieldIndex;
		return it->second.first;
	}

	//clone = recreate subobjects
	ResourceInstance CreateResourceInstanceClone(CloneContext& context, ResourceStorage* storage, ResourceInstance origin, UndoRedoScope* scope)
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
					case ResourceFieldType::SubObject:
					{
						RID clone = CloneSubObject(context, storage, field->GetIndex(), *reinterpret_cast<RID*>(&origin[field->GetOffset()]), scope);
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
							copySubObjectList.subObjects.EmplaceBack(CloneSubObject(context, storage, field->GetIndex(), subobject, scope));
						}
						new(reinterpret_cast<SubObjectList*>(&instance[field->GetOffset()])) SubObjectList{copySubObjectList};
						break;
					}
					case ResourceFieldType::Reference:
					{
						RID originRef = *reinterpret_cast<RID*>(&origin[field->GetOffset()]);
						RID& descRef = *reinterpret_cast<RID*>(&instance[field->GetOffset()]);
						descRef = GetReference(context, originRef);
						break;
					}
					case ResourceFieldType::ReferenceArray:
					{
						const Array<RID>& originRefList = *reinterpret_cast<Array<RID>*>(&origin[field->GetOffset()]);
						Array<RID> destRefList;
						destRefList.Reserve(originRefList.Size());

						for (RID originRef : originRefList)
						{
							destRefList.EmplaceBack(GetReference(context, originRef));
						}

						new(reinterpret_cast<Array<RID>*>(&instance[field->GetOffset()])) Array{destRefList};
						break;
					}
					default:
						field->GetTypeStaticProps().fnCopy(&instance[field->GetOffset()], &origin[field->GetOffset()]);
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
				field->GetTypeStaticProps().fnCopy(&instance[field->GetOffset()], &origin[field->GetOffset()]);
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
					field->GetTypeStaticProps().fnDestroy(&instance[field->GetOffset()]);
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
			builder.Field(field);
		}

		builder.Build();

		ResourceType* type = builder.GetResourceType();
		type->reflectType = reflectType;
		type->scope = reflectType->GetScope();

		//default value
		if (ReflectConstructor* defaultConstructor = reflectType->GetDefaultConstructor())
		{
			RID              rid = GetID({});
			ResourceStorage* storage = GetOrAllocate(rid, {});
			storage->resourceType = type;
			storage->resourceTypeVersion = type->version;
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

	ResourceType* Resources::FindOrCreateTypeByID(TypeID typeId)
	{
		if (ResourceType* type = FindTypeByID(typeId))
		{
			return type;
		}

		if (typeId)
		{
			if (ReflectType* reflectType = Reflection::FindTypeById(typeId))
			{
				return CreateFromReflectType(reflectType);
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

	Array<ResourceType*> Resources::GetTypes()
	{
		std::unique_lock   lock(resourceTypeMutex);
		Array<ResourceType*> ret;
		ret.Reserve(typesById.Size());

		for (auto it = typesById.begin(); it != typesById.end(); ++it)
		{
			if (!it->second.Empty() && it->second.Back() != nullptr)
			{
				ret.EmplaceBack(it->second.Back());
			}
		}

		return ret;
	}

	Array<RID> Resources::GetResourcesByType(ResourceType* type)
	{
		Array<RID> result;
		if (type == nullptr) return result;

		std::unique_lock lock(pageMutex);
		for (u64 i = 0; i < counter; ++i)
		{
			if (pages[SK_PAGE(i)] == nullptr) continue;
			if (!pages[SK_PAGE(i)]->used[SK_OFFSET(i)]) continue;

			ResourceStorage& storage = pages[SK_PAGE(i)]->elements[SK_OFFSET(i)];
			if (storage.resourceType == type)
			{
				result.EmplaceBack(storage.rid);
			}
		}
		return result;
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
		Array<TypeID>    ret;
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
		RID  rid = GetID(uuid);
		ResourceStorage* storage = GetOrAllocate(rid, uuid);

		ResourceType* requestedType = FindTypeByID(typeId);
		if (requestedType == nullptr && typeId)
		{
			if (ReflectType* reflectType = Reflection::FindTypeById(typeId))
			{
				requestedType = CreateFromReflectType(reflectType);
			}
		}

		storage->instance = nullptr;
		storage->resourceType = requestedType;

		if (storage->resourceType)
		{
			storage->resourceTypeVersion = storage->resourceType->version;

			if (storage->resourceType->defaultValue)
			{
				ResourceStorage* defaultValueStorage = GetStorage(storage->resourceType->defaultValue);
				CloneContext context = {
				};
				storage->instance = CreateResourceInstanceClone(context, storage, defaultValueStorage->instance.load(), scope);
			}
		}

		if (scope && storage->instance)
		{
			scope->PushChange(storage, nullptr, storage->instance.load());
		}

		return rid;
	}

	RID ResourcesCreateFromPrototype(CloneContext& context, RID prototypeRid, UUID uuid, UndoRedoScope* scope)
	{
		ResourceStorage* prototype = GetStorage(prototypeRid);
		SK_ASSERT(prototype->resourceType, "prototype type cannot be null");

		auto it = context.cloneMap.Find(prototypeRid);
		if (it == context.cloneMap.end())
		{
			if (!uuid && prototype->uuid)
			{
				uuid = UUID::RandomUUID();
			}
			RID clone = GetID(uuid);
			it = context.cloneMap.Insert(prototypeRid, MakePair(clone, uuid)).first;
		}

		RID rid = it->second.first;

		ResourceStorage* storage = GetOrAllocate(rid, it->second.second);
		storage->resourceType = prototype->resourceType;
		storage->resourceTypeVersion = prototype->resourceTypeVersion;
		storage->prototype = prototype;
		{
			std::unique_lock lock(prototype->prototypeInstancesMutex);
			prototype->prototypeInstances.Insert(rid);
		}

		ResourceObject object = Resources::Write(rid);

		IterateSubObjects(prototype, [&](u32 index, RID subobject)
		{
			RID subObjectPrototype = ResourcesCreateFromPrototype(context, subobject, {}, scope);

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
			RID newRef = GetReference(context, reference);

			if (type == ResourceFieldType::Reference)
			{
				object.SetReference(index, newRef);
			}
			else if (type == ResourceFieldType::ReferenceArray)
			{
				object.AddToReferenceArray(index, newRef);
			}
		});

		object.Commit(scope);

		if (scope)
		{
			scope->PushChange(storage, nullptr, storage->instance.load());
		}

		return rid;
	}

	RID Resources::CreateFromPrototype(RID prototypeRid, UUID uuid, UndoRedoScope* scope)
	{
		CloneContext context{
			.origin = prototypeRid
		};
		return ResourcesCreateFromPrototype(context, prototypeRid, uuid, scope);
	}

	ResourceStorage* Resources::GetStorage(RID rid)
	{
		return Skore::GetStorage(rid);
	}

	RID Resources::Clone(RID origin, UUID uuid, UndoRedoScope* scope)
	{
		CloneContext context = {
			.origin = origin
		};
		RID rid = GetID(uuid);
		CloneInternal(context, origin, rid, uuid, scope);
		return rid ;
	}

	void Resources::Reset(RID rid, UndoRedoScope* scope)
	{
		ResourceStorage* storage = GetStorage(rid);

		ResourceInstance newInstance = nullptr;

		if (storage->resourceType && storage->resourceType->defaultValue)
		{
			ResourceStorage* defaultValueStorage = GetStorage(storage->resourceType->defaultValue);
			CloneContext context = {};
			newInstance = CreateResourceInstanceClone(context, storage, defaultValueStorage->instance.load(), scope);
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

	RID Resources::GetTopParent(RID rid)
	{
		RID parent = rid;
		ResourceStorage* current = GetStorage(rid);
		while (current->parent)
		{
			parent = current->parent->rid;
			current = current->parent;
		}
		return parent;
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
		ResourceStorage* parentChildStorage = GetStorage(child)->parent;

		while (parentChildStorage != nullptr)
		{
			if (parentChildStorage == parentStorage)
			{
				return true;
			}
			parentChildStorage = parentChildStorage->parent;
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
				if (!set)
				{
					break;
				}

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
							case ResourceFieldType::Mat4:
							{
								const Mat4& mat = set.GetMat4(field->GetIndex());
								writer.BeginMap(field->GetName());
								writer.WriteFloat("m00", mat.a[0]);
								writer.WriteFloat("m01", mat.a[1]);
								writer.WriteFloat("m02", mat.a[2]);
								writer.WriteFloat("m03", mat.a[3]);
								writer.WriteFloat("m10", mat.a[4]);
								writer.WriteFloat("m11", mat.a[5]);
								writer.WriteFloat("m12", mat.a[6]);
								writer.WriteFloat("m13", mat.a[7]);
								writer.WriteFloat("m20", mat.a[8]);
								writer.WriteFloat("m21", mat.a[9]);
								writer.WriteFloat("m22", mat.a[10]);
								writer.WriteFloat("m23", mat.a[11]);
								writer.WriteFloat("m30", mat.a[12]);
								writer.WriteFloat("m31", mat.a[13]);
								writer.WriteFloat("m32", mat.a[14]);
								writer.WriteFloat("m33", mat.a[15]);
								writer.EndMap();
								break;
							}
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
							case ResourceFieldType::Buffer:
							{
								if (ResourceBuffer buffer = set.GetBuffer(field->GetIndex()))
								{
									writer.WriteString(field->GetName(), buffer.GetIdAsString());
								}
								break;
							}
							case ResourceFieldType::TypeID:
								if (TypeID typeId = set.GetTypeID(field->GetIndex()))
								{
									if (ReflectType* reflectType = Reflection::FindTypeById(typeId))
									{
										writer.WriteString(field->GetName(), reflectType->GetName());
									}
								}
								break;
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

	struct LookupParent
	{
		RID parentRID;
		u32 fieldIndex;

		friend bool operator==(const LookupParent& lhs, const LookupParent& rhs)
		{
			return lhs.parentRID == rhs.parentRID && lhs.fieldIndex == rhs.fieldIndex;
		}

		friend bool operator!=(const LookupParent& lhs, const LookupParent& rhs)
		{
			return !(lhs == rhs);
		}
	};

	template <>
	struct Hash<LookupParent>
	{
		constexpr static bool hasHash = true;

		constexpr static usize Value(const LookupParent& value)
		{
			usize seed{};
			HashCombine(seed, Hash<RID>::Value(value.parentRID), Hash<u32>::Value(value.fieldIndex));
			return seed;
		}
	};

	struct DeserializeParentInfo
	{
		ResourceStorage*  storage;
		ResourceFieldType type;
		Array<RID>        children;
		RID               subobject;
	};

	RID Resources::Deserialize(ArchiveReader& reader, UndoRedoScope* scope)
	{
		reader.BeginSeq("objects");

		HashMap<LookupParent, DeserializeParentInfo> children;
		HashMap<RID, ResourceObject> objectsToCommit;


		auto WriteCached = [&](RID rid) -> ResourceObject&
		{
			auto itObj = objectsToCommit.Find(rid);
			if (itObj == objectsToCommit.end())
			{
				itObj = objectsToCommit.Emplace(rid, Write(rid)).first;
			}
			return itObj->second;
		};


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

			if (storage->resourceType == nullptr)
			{
				logger.Warn("Resource type not found for type {} '", typeName);
			}

			if (storage->resourceType)
			{
				storage->resourceTypeVersion = storage->resourceType->version;
			}

			UUID prototypeUUID = UUID::FromString(reader.ReadString("_prototype"));
			if (RID prototype = FindOrReserveByUUID(prototypeUUID))
			{
				//it should be GetOrAllocate, but it's not working.
				storage->prototype = GetOrAllocate(prototype, prototypeUUID);
				{
					std::unique_lock lock(storage->prototype->prototypeInstancesMutex);
					storage->prototype->prototypeInstances.Insert(rid);
				}
			}

			if (storage->resourceType)
			{

				ResourceObject& write = WriteCached(rid);

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
							case ResourceFieldType::Mat4:
							{
								reader.BeginMap();
								Mat4 mat;
								mat.a[0] 	= static_cast<Float>(reader.ReadFloat("m00"));
								mat.a[1] 	= static_cast<Float>(reader.ReadFloat("m01"));
								mat.a[2] 	= static_cast<Float>(reader.ReadFloat("m02"));
								mat.a[3] 	= static_cast<Float>(reader.ReadFloat("m03"));
								mat.a[4] 	= static_cast<Float>(reader.ReadFloat("m10"));
								mat.a[5] 	= static_cast<Float>(reader.ReadFloat("m11"));
								mat.a[6] 	= static_cast<Float>(reader.ReadFloat("m12"));
								mat.a[7] 	= static_cast<Float>(reader.ReadFloat("m13"));
								mat.a[8] 	= static_cast<Float>(reader.ReadFloat("m20"));
								mat.a[9] 	= static_cast<Float>(reader.ReadFloat("m21"));
								mat.a[10] = static_cast<Float>(reader.ReadFloat("m22"));
								mat.a[11] = static_cast<Float>(reader.ReadFloat("m23"));
								mat.a[12] = static_cast<Float>(reader.ReadFloat("m30"));
								mat.a[13] = static_cast<Float>(reader.ReadFloat("m31"));
								mat.a[14] = static_cast<Float>(reader.ReadFloat("m32"));
								mat.a[15] = static_cast<Float>(reader.ReadFloat("m33"));
								write.SetMat4(field->GetIndex(), mat);
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
							case ResourceFieldType::Buffer:
								write.SetBuffer(field->GetIndex(), ResourceBuffer(reader.GetString()));
								break;
							case ResourceFieldType::TypeID:
								if (ReflectType* reflectType = Reflection::FindTypeByName(reader.GetString()))
								{
									write.SetTypeID(field->GetIndex(), reflectType->GetProps().typeId);
								}
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

				if (RID parent = FindByUUID(UUID::FromString(reader.ReadString("_parent"))))
				{
					ResourceStorage* parentStorage = GetStorage(parent);
					if (parentStorage->resourceType)
					{
						if (ResourceField* field = parentStorage->resourceType->FindFieldByName(reader.ReadString("_parentField")))
						{
							ResourceObject& parentObject = WriteCached(parent);
							if (field->GetType() == ResourceFieldType::SubObjectList)
							{
								parentObject.AddToSubObjectList(field->GetIndex(), rid);
							}
							else if (field->GetType() == ResourceFieldType::SubObject)
							{
								parentObject.SetSubObject(field->GetIndex(), rid);
							}
						}
					}
				}
			}
			reader.EndMap();
		}
		reader.EndSeq();

		for (auto& it : objectsToCommit)
		{
			it.second.Commit(scope);
		}

		return root;
	}

	void Resources::SerializeTypes(ArchiveWriter& writer)
	{
		writer.BeginSeq("types");

		{
			std::unique_lock lock(resourceTypeMutex);

			for (const auto& pair : typesByName)
			{
				if (pair.second.Empty())
				{
					continue;
				}

				ResourceType* resourceType = pair.second.Back();
				if (resourceType == nullptr)
				{
					continue;
				}

				writer.BeginMap();
				writer.WriteString("name", resourceType->GetName());

				writer.BeginSeq("fields");
				for (ResourceField* field : resourceType->GetFields())
				{
					if (field == nullptr)
					{
						continue;
					}

					writer.BeginMap();
					writer.WriteUInt("index", field->GetIndex());
					writer.WriteString("name", field->GetName());
					writer.WriteUInt("type", static_cast<u32>(field->GetType()));
					writer.WriteUInt("subType", field->GetSubType());
					writer.EndMap();
				}
				writer.EndSeq();

				writer.EndMap();
			}
		}

		writer.EndSeq();
	}

	u32 Resources::DeserializeTypes(ArchiveReader& reader)
	{
		u32 importedCount = 0;

		if (reader.BeginSeq("types"))
		{
			while (reader.NextSeqEntry())
			{
				reader.BeginMap();

				String typeName = reader.ReadString("name");

				Array<ImportedResourceField> fields;
				if (reader.BeginSeq("fields"))
				{
					while (reader.NextSeqEntry())
					{
						reader.BeginMap();
						ImportedResourceField& field = fields.EmplaceBack();
						field.index = static_cast<u32>(reader.ReadUInt("index"));
						field.name = reader.ReadString("name");
						field.type = static_cast<ResourceFieldType>(reader.ReadUInt("type"));
						field.subType = static_cast<TypeID>(reader.ReadUInt("subType"));
						reader.EndMap();
					}
					reader.EndSeq();
				}

				bool alreadyRegistered = false;
				{
					std::unique_lock lock(resourceTypeMutex);
					alreadyRegistered = IsTypeRegisteredNoLock(typeName);
				}

				if (!alreadyRegistered && !typeName.Empty())
				{
					TypeID typeId = static_cast<TypeID>(Hash<StringView>::Value(typeName));
					ResourceTypeBuilder builder = Type(typeId, typeName);
					for (const ImportedResourceField& field : fields)
					{
						u32 fieldTypeValue = static_cast<u32>(field.type);
						if (fieldTypeValue == 0 || fieldTypeValue >= static_cast<u32>(ResourceFieldType::MAX))
						{
							continue;
						}

						builder.Field(field.index, field.name, field.type, field.subType);
					}
					builder.Build();
					++importedCount;
				}

				reader.EndMap();
			}
			reader.EndSeq();
		}

		return importedCount;
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
		if (instance == nullptr) return false;
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

	bool Resources::FromResource(RID rid, Object* object, VoidPtr userData)
	{
		if (object == nullptr) return false;
		return FromResource(rid, object->GetInstance(), userData);
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

	void Resources::DispatchEvents()
	{
		PendingEvent pendingEvent{};
		while (pendingEvents.try_dequeue(pendingEvent))
		{
			auto dispatch = [](ResourceEventType type, ResourceStorage* storage, ResourceObject&& oldValue, ResourceObject&& newValue)
			{
				for (const ResourceEvent& event : storage->events[static_cast<u32>(type)])
				{
					event.function(oldValue, newValue, event.userData);
				}

				if (storage->resourceType != nullptr)
				{
					for (const ResourceEvent& event : storage->resourceType->GetEvents(type))
					{
						event.function(oldValue, newValue, event.userData);
					}
				}
			};

			dispatch(pendingEvent.type,
			         pendingEvent.subject,
			         ResourceObject{pendingEvent.oldStorage, pendingEvent.oldInstance},
			         ResourceObject{pendingEvent.newStorage, pendingEvent.newInstance}
			);

			for (RID rid : pendingEvent.newStorage->GetPrototypeInstancesSafe())
			{
				ResourceStorage* prototypeInstanceStorage = GetStorage(rid);
				ResourceInstance value = prototypeInstanceStorage->instance.load();
				dispatch(
					pendingEvent.type,
					prototypeInstanceStorage,
					ResourceObject{prototypeInstanceStorage, value},
					ResourceObject{prototypeInstanceStorage, value}
				);
			}
		}
	}

	void Resources::EndFrame()
	{
		DispatchEvents();

		//TODO - garbage collect is disabled - it's generating a ton of issues because it was disabled before.
		//need to map all issues caused by enabling it.
		//GarbageCollect();
	}

	void CopyFieldData(ResourceType* oldType, ResourceInstance oldInstance, ResourceField* oldField,
	                   ResourceType* newType, ResourceInstance newInstance, ResourceField* newField)
	{
		// Check if old instance has value for this field
		u8* oldHasValue = reinterpret_cast<u8*>(&oldInstance[sizeof(ResourceInstanceInfo) + oldField->GetIndex()]);
		u8* newHasValue = reinterpret_cast<u8*>(&newInstance[sizeof(ResourceInstanceInfo) + newField->GetIndex()]);

		if (!(*oldHasValue))
		{
			return;  // No value to copy
		}

		*newHasValue = true;

		// Copy data based on field type
		VoidPtr oldData = &oldInstance[oldField->GetOffset()];
		VoidPtr newData = &newInstance[newField->GetOffset()];

		switch (newField->GetType())
		{
			case ResourceFieldType::Bool:
			case ResourceFieldType::Int:
			case ResourceFieldType::UInt:
			case ResourceFieldType::Float:
			case ResourceFieldType::Vec2:
			case ResourceFieldType::Vec3:
			case ResourceFieldType::Vec4:
			case ResourceFieldType::Quat:
			case ResourceFieldType::Mat4:
			case ResourceFieldType::Color:
			case ResourceFieldType::Enum:
			case ResourceFieldType::TypeID:
				MemCopy(newData, oldData, newField->GetSize());
				break;

			case ResourceFieldType::String:
				new(newData) String(*reinterpret_cast<String*>(oldData));
				break;

			case ResourceFieldType::Blob:
				new(newData) Array<u8>(*reinterpret_cast<Array<u8>*>(oldData));
				break;

			case ResourceFieldType::Reference:
				*reinterpret_cast<RID*>(newData) = *reinterpret_cast<RID*>(oldData);
				break;

			case ResourceFieldType::ReferenceArray:
				new(newData) Array<RID>(*reinterpret_cast<Array<RID>*>(oldData));
				break;

			case ResourceFieldType::SubObject:
				*reinterpret_cast<RID*>(newData) = *reinterpret_cast<RID*>(oldData);
				break;

			case ResourceFieldType::SubObjectList:
				new(newData) SubObjectList(*reinterpret_cast<SubObjectList*>(oldData));
				break;

			case ResourceFieldType::Buffer:
				// Buffer handling - copy the ResourceBuffer
				new(newData) ResourceBuffer(*reinterpret_cast<ResourceBuffer*>(oldData));
				break;

			default:
				break;
		}
	}

	void MigrateInstanceData(ResourceType* oldType, ResourceInstance oldInstance,
	                         ResourceType* newType, ResourceInstance newInstance)
	{
		// Copy ResourceInstanceInfo header
		*reinterpret_cast<ResourceInstanceInfo*>(newInstance) =
			*reinterpret_cast<ResourceInstanceInfo*>(oldInstance);

		// Reset the hasValue flags for new instance (they start as 0 from Allocate)
		// We'll set them individually when copying

		for (ResourceField* newField : newType->GetFields())
		{
			if (newField == nullptr) continue;

			// Find matching field by name in old type
			ResourceField* oldField = oldType->FindFieldByName(newField->GetName());

			if (oldField == nullptr)
			{
				// New field, no data to migrate - leave as default
				continue;
			}

			if (oldField->GetType() != newField->GetType())
			{
				// Type mismatch, skip migration for this field
				logger.Warn("Field '{}' type changed, data not migrated", newField->GetName());
				continue;
			}

			// Copy the field data
			CopyFieldData(oldType, oldInstance, oldField, newType, newInstance, newField);
		}
	}

	void MigrateResourceStorage(ResourceStorage* storage, ResourceType* newType)
	{
		ResourceType* oldType = storage->resourceType;
		ResourceInstance oldInstance = storage->instance.load();

		if (oldInstance == nullptr)
		{
			storage->resourceType = newType;
			storage->resourceTypeVersion = newType->GetVersion();
			return;
		}

		ResourceInstance newInstance = newType->Allocate();

		MigrateInstanceData(oldType, oldInstance, newType, newInstance);

		ResourceInstance expected = oldInstance;
		if (storage->instance.compare_exchange_strong(expected, newInstance))
		{
			storage->resourceType = newType;
			storage->resourceTypeVersion = newType->GetVersion();

			toCollectItems.enqueue(DestroyResourcePayload{
				.type = oldType,
				.instance = oldInstance
			});

			UpdateVersion(storage);

			logger.Debug("Migrated resource {} to type version {}", storage->rid.id, newType->GetVersion());
		}
		else
		{
			DestroyResourceInstance(newType, newInstance);
		}
	}

	void MigrateResourceInstances(const HashMap<ResourceType*, ResourceType*>& updatedTypes)
	{
		for (u64 i = 0; i < counter; ++i)
		{
			if (pages[SK_PAGE(i)] == nullptr) continue;
			if (!pages[SK_PAGE(i)]->used[SK_OFFSET(i)]) continue;

			ResourceStorage& storage = pages[SK_PAGE(i)]->elements[SK_OFFSET(i)];

			if (storage.resourceType == nullptr) continue;

			auto it = updatedTypes.Find(storage.resourceType);
			if (it != updatedTypes.end())
			{
				MigrateResourceStorage(&storage, it->second);
			}
		}
	}

	void DoResourcePluginReloaded()
	{
		Resources::MigrateResourcesForReflection();
	}

	void Resources::MigrateResourcesForReflection()
	{
		HashMap<ResourceType*, ResourceType*> updatedTypes;

		for (auto& it : typesById)
		{
			if (it.second.Empty()) continue;

			ResourceType* latestType = it.second.Back();
			ReflectType* currentReflectType = Reflection::FindTypeById(it.first);

			if (currentReflectType && latestType->GetVersion() < currentReflectType->GetVersion())
			{
				ResourceType* newType = CreateFromReflectType(currentReflectType);
				updatedTypes.Insert(latestType, newType);
				latestType = newType;

				logger.Debug("ResourceType {} updated from version {} to {}",
				            latestType->GetName(), latestType->version - 1, newType->GetVersion());
			}

			if (it.second.Size() > 1)
			{
				for (usize i = 0; i < it.second.Size() - 1; ++i)
				{
					ResourceType* oldType = it.second[i];
					if (updatedTypes.Find(oldType) == updatedTypes.end())
					{
						updatedTypes.Insert(oldType, latestType);
						logger.Debug("ResourceType {} version {} mapped to latest version {}",
						            oldType->GetName(), oldType->GetVersion(), latestType->GetVersion());
					}
				}
			}
		}

		if (!updatedTypes.Empty())
		{
			MigrateResourceInstances(updatedTypes);
		}
	}

	void Resources::MigrateResourceForType(TypeID typeId)
	{
		auto it = typesById.Find(typeId);
		if (it == typesById.end() || it->second.Empty()) return;

		ResourceType* latestType = it->second.Back();
		ReflectType* currentReflectType = Reflection::FindTypeById(typeId);

		if (!currentReflectType) return;

		ResourceType* newType = CreateFromReflectType(currentReflectType);

		HashMap<ResourceType*, ResourceType*> updatedTypes;
		for (usize i = 0; i < it->second.Size() - 1; ++i)
		{
			updatedTypes.Insert(it->second[i], newType);
		}
		// The previous latest also needs migration
		updatedTypes.Insert(latestType, newType);

		MigrateResourceInstances(updatedTypes);

		logger.Debug("ResourceType {} migrated to version {}", newType->GetName(), newType->GetVersion());

		EventHandler<OnResourceTypeReloaded> onResourceTypeReloaded{};
		onResourceTypeReloaded.Invoke(typeId);
	}

	void Resources::MigrateResources(ResourceType* oldType, ResourceType* newType)
	{
		HashMap<ResourceType*, ResourceType*> updatedTypes;
		updatedTypes.Insert(oldType, newType);
		MigrateResourceInstances(updatedTypes);
	}

	void SK_API InitResourceLoaders()
	{
		for (TypeID typeId : Reflection::GetDerivedTypes(sktypeid(ResourceLoader)))
		{
			if (ReflectType* type = Reflection::FindTypeById(typeId))
			{
				if (Object* object = type->NewObject())
				{
					if (ResourceLoader* loader = object->SafeCast<ResourceLoader>())
					{
						TypeID resourceTypeId = loader->GetResourceTypeId();
						auto it = resourceLoaders.Find(resourceTypeId);
						if (it == resourceLoaders.end())
						{
							it = resourceLoaders.Insert(resourceTypeId, ResourceLoaderContext{}).first;
						}

						if (it->second.instance != nullptr)
						{
							logger.Warn("Already loaded a loader for type {}", resourceTypeId);
							DestroyAndFree(it->second.instance);
						}

						it->second.instance = loader;
					}
				}
			}
		}

		//ResourceLoader
	}


	void SK_API ResourceInit()
	{
		Event::Bind<OnPluginReloaded, DoResourcePluginReloaded>();
		Resources::Create({});
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

		for (const auto& it : resourceLoaders)
		{
			DestroyAndFree(it.second.instance);
		}

		for (const auto & handler : fileHandlers)
		{
			FileSystem::CloseFile(handler);
		}

		typesById.Clear();
		typesByName.Clear();

		byUUID.Clear();
		// byPath.Clear();
		// resourceByType.Clear();
		counter = 0;
		pageCount = 0;

		Event::Unbind<OnPluginReloaded, DoResourcePluginReloaded>();
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

	UndoRedoScope::UndoRedoScope(StringView name) : name(name) {}

	UndoRedoScope::~UndoRedoScope() = default;

	void UndoRedoScope::PushChange(ResourceStorage* storage, ResourceInstance before, ResourceInstance after)
	{
		std::unique_ptr<UndoRedoChange> change = std::make_unique<UndoRedoChange>(storage);

		change->before = CreateResourceInstanceCopy(storage->resourceType, before);
		change->after = CreateResourceInstanceCopy(storage->resourceType, after);

		{
			std::unique_lock lock(mutex);
			changes.EmplaceBack(Traits::Move(change));
		}
	}

	void UndoRedoScope::Undo()
	{
		std::unique_lock lock(mutex);
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
		std::unique_lock lock(mutex);
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


	UndoRedoScope* UndoRedoScope::Create(StringView name)
	{
		return Alloc<UndoRedoScope>(name);
	}

	void UndoRedoScope::Destroy()
	{
		DestroyAndFree(this);
	}

	StringView UndoRedoScope::GetName() const
	{
		return name;
	}


	RID Resources::LoadResources(StringView filePath)
	{
		Array<u8> compressedBuffer(MemoryGlobals::GetHeapAllocator());
		FileSystem::ReadFileAsByteArray(filePath, compressedBuffer);
		usize decompressedSize = Compression::GetMaxDecompressedBufferSize(compressedBuffer.Data(), compressedBuffer.Size(), CompressionMode::ZSTD);

		Array<u8> uncompressedBuffer(MemoryGlobals::GetHeapAllocator());
		uncompressedBuffer.Resize(decompressedSize);
		Compression::Decompress(uncompressedBuffer.Data(), uncompressedBuffer.Size(), compressedBuffer.Data(), compressedBuffer.Size(), CompressionMode::ZSTD);

		BinaryArchiveReader reader{uncompressedBuffer};

		RID projectSettings = {};
		reader.BeginMap("projectSettings");
		projectSettings = Settings::Load(reader, TypeInfo<ProjectSettings>::ID());
		reader.EndMap();

		String bufferFile = Path::Join(Path::Parent(filePath), Path::Name(filePath) + SK_BUFFER_EXT);
		FileHandler bufferHandler = FileSystem::OpenFile(bufferFile, AccessMode::ReadOnly);
		fileHandlers.EmplaceBack(bufferHandler);

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

				struct BufferInfo
				{
					u64    offset;
					u64    size;
				};
				HashMap<String, BufferInfo> buffers;

				if (reader.ReadUInt("bufferCount") > 0)
				{
					reader.BeginSeq("buffers");

					while (reader.NextSeqEntry())
					{
						reader.BeginMap();
						buffers.Emplace(reader.ReadString("id"), BufferInfo{
							                .offset = reader.ReadUInt("offset"),
							                .size = reader.ReadUInt("size")
						                });
						reader.EndSeq();
					}
					reader.EndSeq();
				}

				reader.EndMap();

				if (ResourceObject resourceObject = Read(rid))
				{
					resourceObject.IterateAllBuffers([&](const ResourceBuffer& buffer)
					{
						if (auto it = buffers.Find(buffer.GetIdAsString()))
						{
							buffer.MapFile(bufferHandler, true, it->second.offset, it->second.size);
							logger.Debug("buffer {} loaded with offset {}, size {}  ", it->first, it->second.offset, it->second.size);
						}
					});
				}

				if (ResourceStorage* storage = GetStorage(rid); storage != nullptr && storage->resourceType != nullptr)
				{
					if (const auto& it = resourceLoaders.Find(storage->resourceType->GetID()))
					{
						it->second.instance->LoadResource(rid);
					}
				}


			}
		}
		reader.EndSeq();

		return projectSettings;
	}
}
