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

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Serialization.hpp"

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

	namespace
	{
		struct DestroyResourcePayload
		{
			ResourceType* type;
			ResourceInstance instance;
		};

		std::mutex                            resourceTypeMutex{};
		HashMap<TypeID, Array<ResourceType*>> typesById;
		HashMap<String, Array<ResourceType*>> typesByName;

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

		void UpdateVersion(ResourceStorage* resourceStorage)
		{
			ResourceStorage* current = resourceStorage;
			while (current != nullptr)
			{
				++current->version;
				current = resourceStorage->parent;
			}
		}
	}

	ResourceInstance CreateResourceInstanceCopy(ResourceType* type, ResourceInstance origin)
	{
		if (origin == nullptr || type == nullptr) return nullptr;

		ResourceInstance instance = type->Allocate();
		*reinterpret_cast<ResourceInstanceInfo*>(instance) = *reinterpret_cast<ResourceInstanceInfo*>(origin);
		memcpy(instance + sizeof(ResourceInstanceInfo), origin + sizeof(ResourceInstanceInfo), type->GetFields().Size());

		for (ResourceField* field : type->GetFields())
		{
			switch (field->GetType())
			{
				case ResourceFieldType::ReferenceArray:
					new(reinterpret_cast<Array<RID>*>(&instance[field->GetOffset()])) Array(*reinterpret_cast<Array<RID>*>(&origin[field->GetOffset()]));
					break;
				case ResourceFieldType::SubObjectSet:
					new(reinterpret_cast<SubObjectSet*>(&instance[field->GetOffset()])) SubObjectSet(*reinterpret_cast<SubObjectSet*>(&origin[field->GetOffset()]));
					break;
				case ResourceFieldType::String:
					new(reinterpret_cast<String*>(&instance[field->GetOffset()])) String(*reinterpret_cast<String*>(&origin[field->GetOffset()]));
					break;
				default:
					memcpy(&instance[field->GetOffset()], &origin[field->GetOffset()], field->GetSize());
					break;
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
				switch (field->GetType())
				{
					case ResourceFieldType::ReferenceArray:
						reinterpret_cast<Array<RID>*>(&instance[field->GetOffset()])->~Array<RID>();
						break;
					case ResourceFieldType::SubObjectSet:
						reinterpret_cast<SubObjectSet*>(&instance[field->GetOffset()])->~SubObjectSet();
						break;
					case ResourceFieldType::String:
						reinterpret_cast<String*>(&instance[field->GetOffset()])->~String();
						break;
					default:
						break;
				}
			}
		}
		DestroyAndFree(instance);
	}

	template <typename F>
	void IterateSubObjects(ResourceStorage* storage,  F&& f)
	{
		ResourceObject object(storage, nullptr);
		for (ResourceField* field : storage->resourceType->GetFields())
		{
			if (object.HasValueOnThisObject(field->GetIndex()))
			{
				switch (field->GetType())
				{
					case ResourceFieldType::SubObject:
						f(field->GetIndex(), object.GetSubObject(field->GetIndex()));
						break;
					case ResourceFieldType::SubObjectSet:
						object.IterateSubObjectSet(field->GetIndex(), false, [&](RID rid)
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

	ResourceType* Resources::CreateFromReflectType(ReflectType* reflectType)
	{
		ResourceTypeBuilder builder = Type(reflectType->GetProps().typeId, reflectType->GetName());
		for (ReflectField* field : reflectType->GetFields())
		{
			builder.Field(field->GetIndex(), field->GetName(), field->GetResourceFieldType(), field->GetProps().typeId);
		}

		builder.Build();

		ResourceType* type = builder.GetResourceType();
		type->reflectType = reflectType;
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

		return rid;
	}

	RID Resources::CreateFromPrototype(RID prototypeRid, UUID uuid, UndoRedoScope* scope)
	{
		ResourceStorage* prototype = GetStorage(prototypeRid);
		SK_ASSERT(prototype->resourceType, "prototype type cannot be null");

		RID rid = GetID(uuid);

		ResourceStorage* storage = GetOrAllocate(rid, uuid);
		storage->instance = nullptr;
		storage->resourceType = prototype->resourceType;
		storage->prototype = prototype;

		return rid;
	}

	RID  Resources::Clone(RID rid, UUID uuid, UndoRedoScope* scope)
	{
		return {};
	}

	void Resources::Reset(RID rid, UndoRedoScope* scope)
	{
		//TODO
	}

	void Resources::Destroy(RID rid, UndoRedoScope* scope)
	{
		//TODO
	}

	u64 Resources::GetVersion(RID rid)
	{
		return GetStorage(rid)->version;
	}

	ResourceObject Resources::Write(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		SK_ASSERT(storage->resourceType, "type cannot be null");
		ResourceInstance instance = storage->resourceType->Allocate();

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

	UUID Resources::GetUUID(RID rid)
	{
		ResourceStorage* storage = GetStorage(rid);
		return storage->uuid;
	}

	void Resources::Serialize(RID rid, ArchiveWriter& writer)
	{
		ResourceObject set = Read(rid);
		if (!set) return;

		ResourceType* type = set.GetType();


		if (UUID uuid = set.GetUUID())
		{
			writer.WriteString("_uuid", set.GetUUID().ToString());
		}

		writer.WriteString("_type", type->GetName());

		for (ResourceField* field : type->GetFields())
		{
			if (set.HasValue(field->GetIndex())) // cannot be prototyped
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
						writer.WriteFloat("red", set.GetColor(field->GetIndex()).red);
						writer.WriteFloat("green", set.GetColor(field->GetIndex()).green);
						writer.WriteFloat("blue", set.GetColor(field->GetIndex()).blue);
						writer.WriteFloat("alpha", set.GetColor(field->GetIndex()).alpha);
						writer.EndMap();
						break;
					case ResourceFieldType::Enum:
						writer.WriteInt(field->GetName(), set.GetInt(field->GetIndex()));
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
							if (UUID uuid = GetUUID(set.GetReference(field->GetIndex())))
							{
								writer.AddString(uuid.ToString());
							}
						}
						writer.EndSeq();
						break;
					case ResourceFieldType::SubObject:
						writer.BeginMap(field->GetName());
						if (RID subobject = set.GetSubObject(field->GetIndex()))
						{
							Serialize(subobject, writer);
						}
						writer.EndMap();
						break;
					case ResourceFieldType::SubObjectSet:
						writer.BeginSeq(field->GetName());
						set.IterateSubObjectSet(field->GetIndex(), false, [](RID rid, VoidPtr userData)
						{
							ArchiveWriter& writer = *static_cast<ArchiveWriter*>(userData);

							ResourceObject set = Read(rid);
							if (!set) return;

							writer.BeginMap();
							Serialize(rid, writer);
							writer.EndMap();
						}, &writer);
						writer.EndSeq();
						break;
				}
			}
		}
	}

	RID Resources::Deserialize(ArchiveReader& reader)
	{
		StringView typeName = reader.ReadString("_type");
		if (ResourceType* type = FindTypeByName(typeName)) {}
		return RID{};
	}

	bool Resources::ToResource(RID rid, ConstPtr instance, UndoRedoScope* scope)
	{
		ResourceStorage* storage = GetStorage(rid);
		if (!storage->resourceType) return false;

		ReflectType* reflectType = storage->resourceType->GetReflectType();

		if (!instance || !reflectType || !rid) return false;


		if (ResourceObject resourceObject = Write(rid))
		{
			for (ReflectField* field : reflectType->GetFields())
			{
				field->ToResource(resourceObject, field->GetIndex(), instance, scope);
			}
			resourceObject.Commit(scope);
		}

		return true;
	}

	bool Resources::FromResource(RID rid, VoidPtr instance)
	{
		ResourceStorage* storage = GetStorage(rid);
		if (!storage->resourceType) return false;
		ReflectType* reflectType = storage->resourceType->GetReflectType();
		if (!instance || !reflectType || !rid) return false;

		ResourceObject resourceObject = Read(rid);
		if (!resourceObject) return false;

		for (ReflectField* field : reflectType->GetFields())
		{
			field->FromResource(resourceObject, field->GetIndex(), instance);
		}

		return true;
	}


	void Resources::GarbageCollect()
	{
		DestroyResourcePayload payload{};
		while (toCollectItems.try_dequeue(payload))
		{
			DestroyResourceInstance(payload.type, payload.instance);
		}
	}

	void SK_API ResourceInit()
	{
		Resources::Create(0);
	}

	void SK_API ResourceShutdown()
	{
		Resources::GarbageCollect();

		for (u64 i = 0; i < counter; ++i)
		{
			ResourceStorage& storage = pages[SK_PAGE(i)]->elements[SK_OFFSET(i)];
			DestroyResourceInstance(storage.resourceType, storage.instance.load());
			storage.~ResourceStorage();
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

		byUUID.Clear();
		// byPath.Clear();
		// resourceByType.Clear();
		counter = 0;
		pageCount = 0;
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

		// if (eventsEnabled)
		// {
		// 	instance->owner->ExecuteListeners(ResourceObject(instance->dataOnWrite), ResourceObject(instance->owner->instance));
		// }

		IterateSubObjects(storage, [&](u32 index, RID subObject)
		{
			ResourceStorage* subOjectStorage = GetStorage(subObject);
			subOjectStorage->parent = storage;
			subOjectStorage->parentFieldIndex = index;
		});
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

			ResourceInstance oldInstance = action->storage->instance.exchange(CreateResourceInstanceCopy(action->storage->resourceType, action->before));
			UpdateVersion(action->storage);

			// action.storage->ExecuteListeners(ResourceObject(instance), ResourceObject(action.storage->instance.load()));


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
			ResourceInstance oldInstance = action->storage->instance.exchange(CreateResourceInstanceCopy(action->storage->resourceType, action->after));
			UpdateVersion(action->storage);

			//action.storage->ExecuteListeners(ResourceObject(instance), ResourceObject(action.storage->instance.load()));

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
}
