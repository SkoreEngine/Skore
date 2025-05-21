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

#include "Entity.hpp"

#include "Skore/Core/Span.hpp"
#include "Component.hpp"
#include "Scene.hpp"
#include "SceneTypes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Allocator.hpp"


namespace Skore
{
	Entity::~Entity() {}

	void Entity::SetActive(bool active)
	{
		m_active = active;
		SetParentActivated(active);

		SceneEventDesc desc;
		desc.type = active ? SceneEventType::EntityActivated : SceneEventType::EntityDeactivated;

		Entity::NotifyEvent(desc, true);
	}

	bool Entity::IsActive() const
	{
		return m_active;
	}

	bool Entity::HasChildren() const
	{
		return !m_children.Empty();
	}

	StringView Entity::GetName() const
	{
		return m_name;
	}

	void Entity::SetName(StringView name)
	{
		m_name = name;
	}

	UUID Entity::GetUUID() const
	{
		return m_uuid;
	}

	void Entity::SetUUID(const UUID& uuid)
	{
		if (m_uuid == uuid) return;
		if (m_uuid && m_scene)
		{
			m_scene->m_entities.Erase(m_uuid);
		}

		m_uuid = uuid;

		if (m_uuid && m_scene)
		{
			m_scene->m_entities.Insert(m_uuid, this);
		}
	}

	Entity* Entity::GetParent() const
	{
		return m_parent;
	}

	void Entity::SetParent(Entity* parent)
	{
		if (m_parent == parent)
		{
			return;
		}

		// Remove from old parent if exists
		if (m_parent)
		{
			for (usize i = 0; i < m_parent->m_children.Size(); ++i)
			{
				if (m_parent->m_children[i] == this)
				{
					m_parent->m_children.Remove(i);
					break;
				}
			}
		}

		// Set new parent
		m_parent = parent;

		// Add to new parent's children array
		if (m_parent)
		{
			m_parent->m_children.EmplaceBack(this);
			SetScene(m_parent->GetScene());
			// Update parent activated state
			SetParentActivated(m_parent->m_active && m_parent->m_parentActivated);
			m_parentActivated = m_parent->IsActive();
		}
	}

	void Entity::DetachFromParent()
	{
		if (m_parent)
		{
			for (usize i = 0; i < m_parent->m_children.Size(); ++i)
			{
				if (m_parent->m_children[i] == this)
				{
					m_parent->m_children.Remove(i);
					break;
				}
			}
			m_parent = nullptr;
		}
		else
		{
			m_scene->SetRootEntity(nullptr);
		}
	}

	u64 Entity::GetSiblingIndex() const
	{
		if (!m_parent)
		{
			return U64_MAX;
		}

		for (u64 i = 0; i < m_parent->m_children.Size(); ++i)
		{
			if (m_parent->m_children[i] == this)
			{
				return i;
			}
		}

		return U64_MAX;
	}

	void Entity::SetSiblingIndex(u64 index)
	{
		if (!m_parent || index == U64_MAX)
		{
			return;
		}

		u64 currentIndex = GetSiblingIndex();
		if (currentIndex == U64_MAX || currentIndex == index)
		{
			return;
		}

		if (index >= m_parent->m_children.Size())
		{
			index = m_parent->m_children.Size() - 1;
		}

		m_parent->m_children.Remove(currentIndex);

		if (currentIndex < index)
		{
			index -= 1;
		}
		m_parent->m_children.Insert(m_parent->m_children.begin() + index, this);
	}

	bool Entity::IsChildOf(Entity* parent) const
	{
		if (!parent || !m_parent)
		{
			return false;
		}
		if (m_parent == parent)
		{
			return true;
		}
		return m_parent->IsChildOf(parent);
	}

	Span<Entity*> Entity::Children() const
	{
		return m_children;
	}

	Entity* Entity::FindChildByOrigin(UUID origin) const
	{
		for (Entity* child : m_children)
		{
			if (child->m_origin == origin)
			{
				return child;
			}
		}
		return nullptr;
	}

	Entity* Entity::Duplicate() const
	{
		return Duplicate(m_parent);
	}

	Entity* Entity::Duplicate(Entity* newParent) const
	{
		Entity* copy = Instantiate(newParent, GetName());

		for (Component* component : m_components)
		{
			if (ReflectType* type = component->GetType())
			{
				if (Component* newComponent = copy->AddComponent(type))
				{
					type->DeepCopy(component, newComponent);
				}
			}
		}

		for (Entity* child : m_children)
		{
			child->Duplicate(copy);
		}

		return copy;
	}

	void Entity::Destroy() const
	{
		if (m_scene)
		{
			m_scene->m_queueToDestroy.Enqueue(const_cast<Entity*>(this));
		}
	}

	void Entity::SetOverride(StringView field)
	{
		m_overrides.Emplace(field);
	}

	void Entity::ClearOverrides()
	{
		m_overrides.Clear();
	}

	bool Entity::HasOverride(StringView field) const
	{
		return m_overrides.Has(field);
	}

	void Entity::RemoveOverride(StringView field)
	{
		m_overrides.Erase(field);
	}

	bool Entity::HasOverrides() const
	{
		return !m_overrides.Empty();
	}


	void Entity::RemoveComponent(Component* component)
	{
		for (usize i = 0; i < m_components.Size(); ++i)
		{
			if (m_components[i] == component)
			{
				RemoveComponentAt(i);
				break;
			}
		}
	}

	void Entity::RemoveComponent(TypeID typeId)
	{
		usize index = U32_MAX;

		for (usize i = 0; i < m_components.Size(); ++i)
		{
			if (m_components[i]->GetTypeId() == typeId)
			{
				index = i;
			}
		}

		if (index != U32_MAX)
		{
			RemoveComponentAt(index);
		}
	}

	void Entity::RemoveComponentAt(u32 index)
	{
		RemoveComponentAt(index, true);
	}

	void Entity::RemoveComponentAt(u32 index, bool destroy)
	{
		if (m_components[index]->IsUpdateEnabled() && m_scene)
		{
			m_scene->UnregisterComponentForUpdate(m_components[index]);
		}

		if (m_prefab)
		{
			m_removedComponents.Emplace(m_components[index]->GetUUID());
		}

		if (destroy)
		{
			m_components[index]->Destroy();
			DestroyAndFree(m_components[index]);
		}
		m_components.Remove(index);
	}

	Entity* Entity::InstantiateFromOrigin(Entity* origin, Entity* parent)
	{
		struct PendingItems
		{
			Entity* origin;
			Entity* parent;
		};

		Entity* createdRoot = nullptr;

		PendingItems currentItem = {origin, parent};
		Queue<PendingItems> pending;

		while (currentItem.origin != nullptr)
		{
			if (Entity* instance = Instantiate(currentItem.parent, currentItem.origin->GetName(), UUID::RandomUUID()))
			{
				instance->m_prefab = origin->GetScene()->GetUUID();
				instance->m_origin = currentItem.origin->GetUUID();

				for (Component* originComponent : currentItem.origin->m_components)
				{
					if (ReflectType* componentType = originComponent->GetType())
					{
						if (Component* newComponent = instance->AddComponent(componentType))
						{
							componentType->DeepCopy(originComponent, newComponent);
							newComponent->m_prefab = originComponent->m_uuid;
						}
					}
				}

				for (Entity* originChild : currentItem.origin->m_children)
				{
					pending.Enqueue({originChild, instance});
				}

				instance->GetType()->DeepCopy(currentItem.origin, instance);

				if (createdRoot == nullptr)
				{
					createdRoot = instance;
				}
			}

			currentItem = {};

			if (!pending.IsEmpty())
			{
				currentItem = pending.Dequeue();
			}
		}

		return createdRoot;
	}

	void Entity::LoadPrefab()
	{
		for (Entity* child : m_children)
		{
			child->LoadPrefab();
		}

		if (m_prefab)
		{
			if (Scene* scene = Assets::Get<Scene>(m_prefab))
			{
				Entity* origin = scene->FindEntityByUUID(m_origin);
				if (!origin) return;

				ReflectType* reflectType = GetType();

				for (ReflectField* field : reflectType->GetFields())
				{
					if (!m_overrides.Has(field->GetName()))
					{
						field->CopyFromType(origin, this);
					}
				}

				//TODO : check component removed.
				for (Component* originComponent : origin->m_components)
				{
					if (ReflectType* componentType = originComponent->GetType())
					{
						Component* newComponent = FindComponentByPrefab(originComponent->GetUUID());
						if (newComponent == nullptr)
						{
							newComponent = AddComponent(componentType);
						}

						if (newComponent != nullptr)
						{
							for (ReflectField* field : componentType->GetFields())
							{
								if (!newComponent->m_overrides.Has(field->GetName()))
								{
									field->CopyFromType(originComponent, newComponent);
								}
							}
						}
					}
				}

				for (Entity* prefabChild :  origin->Children())
				{
					if (!m_removedEntities.Find(prefabChild->GetUUID()))
					{
						if (FindChildByOrigin(prefabChild->GetUUID()) == nullptr)
						{
							InstantiateFromOrigin(prefabChild, this);
						}
					}
				}
			}
		}
	}

	void Entity::MoveComponentTo(Component* component, u32 index)
	{
		u32 currentIndex = GetComponentIndex(component);
		RemoveComponentAt(currentIndex, false);

		if (currentIndex < index)
		{
			index -= 1;
		}
		m_components.Insert(m_components.begin() + index, component);
	}

	u32 Entity::GetComponentIndex(Component* component)
	{
		for (usize i = 0; i < m_components.Size(); ++i)
		{
			if (m_components[i] == component)
			{
				return i;
			}
		}
		return U32_MAX;
	}

	Component* Entity::GetComponent(TypeID typeId) const
	{
		for (usize i = 0; i < m_components.Size(); ++i)
		{
			if (typeId == m_components[i]->GetTypeId())
			{
				return m_components[i];
			}
		}
		return nullptr;
	}

	Array<Component*> Entity::GetComponents(TypeID typeId) const
	{
		Array<Component*> ret;
		for (usize i = 0; i < m_components.Size(); ++i)
		{
			if (typeId == m_components[i]->GetTypeId())
			{
				ret.EmplaceBack(m_components[i]);
			}
		}
		return ret;
	}

	UUID Entity::GetPrefab() const
	{
		return m_prefab;
	}

	Scene* Entity::GetScene() const
	{
		return m_scene;
	}

	void Entity::NotifyEvent(const SceneEventDesc& event, bool notifyChildren)
	{
		if (event.type == SceneEventType::TransformUpdated)
		{
			const Mat4& parentTransform = m_parent ? m_parent->GetWorldTransform() : Mat4(1.0);
			m_worldTransform = parentTransform * GetLocalTransform();
		}

		for (Component* component : m_components)
		{
			component->ProcessEvent(event);
		}

		if (notifyChildren)
		{
			for (Entity* child : m_children)
			{
				child->NotifyEvent(event, true);
			}
		}
	}

	Span<Component*> Entity::GetAllComponents() const
	{
		return m_components;
	}

	Entity* Entity::Instantiate(Entity* parent, StringView name, UUID uuid)
	{
		SK_ASSERT(parent, "parent is required");
		if (parent)
		{
			Entity* entity = Alloc<Entity>();
			entity->m_scene = parent->m_scene;
			entity->m_parent = parent;

			if (!uuid)
			{
				uuid = UUID::RandomUUID();
			}

			entity->SetName(name);
			entity->SetUUID(uuid);

			parent->m_children.EmplaceBack(entity);
			entity->m_scene->m_queueToStart.Enqueue(entity);
			return entity;
		}
		return nullptr;
	}

	Entity* Entity::Instantiate(UUID prefab, Entity* parent, StringView name, UUID uuid)
	{
		if (!parent) return nullptr;

		if (Scene* scene = Assets::Get<Scene>(prefab))
		{
			if (scene == parent->GetScene())
			{
				//SK_ASSERT(false, "Cannot instantiate prefab into the same scene");
				return nullptr;
			}
			return InstantiateFromOrigin(scene->GetRootEntity(), parent);
		}
		return nullptr;
	}

	void Entity::Serialize(ArchiveWriter& archiveWriter) const
	{
		bool isPrefab = m_prefab ? true : false;

		archiveWriter.WriteString("uuid", m_uuid.ToString());
		archiveWriter.WriteString("name", m_name);
		archiveWriter.WriteBool("active", m_active);

		if (m_parent)
		{
			archiveWriter.WriteString("parent", m_parent->GetUUID().ToString());
		}

		if (isPrefab)
		{
			archiveWriter.WriteString("prefab", m_prefab.ToString());
			archiveWriter.WriteString("origin", m_origin.ToString());
		}

		for (ReflectField* field : GetType()->GetFields())
		{
			if (!m_origin || m_overrides.Has(field->GetName()))
			{
				if (const Object* fieldObject = field->GetObject(this))
				{
					archiveWriter.BeginMap(field->GetName());
					fieldObject->Serialize(archiveWriter);
					archiveWriter.EndMap();
				}
			}
		}

		if (!m_removedEntities.Empty())
		{
			archiveWriter.BeginSeq("removedEntities");
			for (UUID uuid : m_removedEntities)
			{
				archiveWriter.AddString(uuid.ToString());
			}
			archiveWriter.EndSeq();
		}

		if (!m_removedComponents.Empty())
		{
			archiveWriter.BeginSeq("removedComponents");
			for (UUID uuid : m_removedComponents)
			{
				archiveWriter.AddString(uuid.ToString());
			}
			archiveWriter.EndSeq();
		}

		if (!m_components.Empty())
		{
			archiveWriter.BeginSeq("components");
			for (Component* component : m_components)
			{
				archiveWriter.BeginMap();
				archiveWriter.WriteString("_type", component->GetType()->GetName());
				archiveWriter.WriteString("_uuid", component->GetUUID().ToString());
				if (component->IsPrefab())
				{
					archiveWriter.WriteString("_prefab", component->GetPrefab().ToString());
				}
				component->Serialize(archiveWriter);
				archiveWriter.EndMap();
			}
			archiveWriter.EndSeq();
		}
	}

	void Entity::Deserialize(ArchiveReader& archiveReader)
	{
		ReflectType* reflectType = GetType();

		if (UUID prefabUUID = UUID::FromString(archiveReader.ReadString("prefab")))
		{
			m_prefab = prefabUUID;
			m_origin = UUID::FromString(archiveReader.ReadString("origin"));
		}

		while (archiveReader.NextMapEntry())
		{
			StringView key = archiveReader.GetCurrentKey();
			if (key == "uuid")
			{
				m_uuid = UUID::FromString(archiveReader.GetString());
				if (m_uuid)
				{
					m_scene->m_entities.Insert(m_uuid, this);
				}
			}
			else if (key == "active")
			{
				m_active = archiveReader.GetBool();
			}
			else if (key == "name")
			{
				m_name = archiveReader.GetString();
			}
			else if (key == "parent")
			{
				if (Entity* parent = m_scene->FindEntityByUUID(UUID::FromString(archiveReader.GetString())))
				{
					this->SetParent(parent);
				}
			}
			else if (key == "removedEntities")
			{
				archiveReader.BeginSeq();
				while (archiveReader.NextSeqEntry())
				{
					m_removedEntities.Emplace(UUID::FromString(archiveReader.GetString()));
				}
				archiveReader.EndSeq();
			}
			else if (key == "components")
			{
				archiveReader.BeginSeq();
				while (archiveReader.NextSeqEntry())
				{
					archiveReader.BeginMap();
					if (ReflectType* type = Reflection::FindTypeByName(archiveReader.ReadString("_type")))
					{
						UUID uuid = UUID::FromString(archiveReader.ReadString("_uuid"));
						if (Component* component = AddComponent(type, uuid))
						{
							component->m_prefab = UUID::FromString(archiveReader.ReadString("_prefab"));
							component->Deserialize(archiveReader);
						}
					}
					archiveReader.EndMap();
				}
				archiveReader.EndSeq();
			}
			else if (reflectType != nullptr)
			{
				if (ReflectField* field = reflectType->FindField(key))
				{
					if (m_prefab)
					{
						m_overrides.Emplace(field->GetName());
					}
					field->Deserialize(archiveReader, this);
				}
			}
		}
	}

	void Entity::SerializeWithChildren(ArchiveWriter& archiveWriter)
	{
		Serialize(archiveWriter);
		archiveWriter.BeginSeq("_children");
		for (Entity* child : m_children)
		{
			archiveWriter.BeginMap();
			archiveWriter.WriteString("_type", GetType()->GetName());
			child->Serialize(archiveWriter);
			archiveWriter.EndMap();
		}
		archiveWriter.EndSeq();
	}

	void Entity::DeserializeWithChildren(ArchiveReader& archiveReader)
	{
		Deserialize(archiveReader);
		archiveReader.BeginSeq("_children");
		while (archiveReader.NextSeqEntry())
		{
			archiveReader.BeginMap();
			if (ReflectType* type = Reflection::FindTypeByName(archiveReader.ReadString("_type")))
			{
				Entity* entity = type->NewObject()->SafeCast<Entity>();
				entity->m_scene = m_scene;
				entity->Deserialize(archiveReader);
			}
			archiveReader.EndMap();
		}
		archiveReader.EndSeq();
	}

	void Entity::UpdateTransform()
	{
		SceneEventDesc desc{
			.type = SceneEventType::TransformUpdated
		};
		Entity::NotifyEvent(desc, true);
	}

	void Entity::DoStart()
	{
		m_started = true;

		for (Component* component : m_components)
		{
			component->Start();
		}

		for (Entity* child : m_children)
		{
			child->DoStart();
		}
	}

	void Entity::DestroyInternal(bool removeFromParent)
	{
		if (removeFromParent)
		{
			if (m_parent)
			{
				for (usize i = 0; i < m_parent->m_children.Size(); ++i)
				{
					if (m_parent->m_children[i] == this)
					{
						if (m_parent->m_prefab && m_origin)
						{
							m_parent->m_removedEntities.Emplace(m_origin);
						}
						m_parent->m_children.Remove(i);
						break;
					}
				}
			}
			else if (this == m_scene->GetRootEntity())
			{
				m_scene->m_rootEntity = nullptr;
			}
		}

		for (auto& child : m_children)
		{
			child->DestroyInternal(false);
		}

		m_children.Clear();

		for (Component* component : m_components)
		{
			if (component->IsUpdateEnabled())
			{
				m_scene->UnregisterComponentForUpdate(component);
			}
			component->Destroy();
			DestroyAndFree(component);
		}
		m_components.Clear();
		m_scene->m_entities.Erase(m_uuid);
		DestroyAndFree(this);
	}

	void Entity::AddComponent(Component* component)
	{
		AddComponent(component, UUID::RandomUUID());
	}

	void Entity::AddComponent(Component* component, UUID uuid)
	{
		component->m_entity = this;
		component->m_scene = m_scene;
		component->m_uuid = uuid;
		component->Init();

		m_components.EmplaceBack(component);

		// Handle update registration
		if (component->IsUpdateEnabled())
		{
			m_scene->RegisterComponentForUpdate(component);
		}

		if (!m_started)
		{
			m_scene->m_componentsToStart.Enqueue(component);
		}
		else
		{
			component->Start();
		}
	}

	Component* Entity::AddComponent(TypeID typeId)
	{
		if (ReflectType* reflectType = Reflection::FindTypeById(typeId))
		{
			return AddComponent(reflectType);
		}
		return nullptr;
	}

	Component* Entity::AddComponent(ReflectType* reflectType)
	{
		return AddComponent(reflectType, {});
	}

	Component* Entity::AddComponent(ReflectType* reflectType, UUID uuid)
	{
		if (reflectType)
		{
			Component* component = reflectType->NewObject()->SafeCast<Component>();
			AddComponent(component, uuid);
			return component;
		}
		return nullptr;
	}

	Component* Entity::FindComponentByUUID(UUID uuid)
	{
		for (Component* component : m_components)
		{
			if (component->GetUUID() == uuid)
			{
				return component;
			}
		}
		return nullptr;
	}

	Component* Entity::FindComponentByPrefab(UUID uuid)
	{
		for (Component* component : m_components)
		{
			if (component->GetPrefab() == uuid)
			{
				return component;
			}
		}
		return nullptr;
	}


	void Entity::SetScene(Scene* scene)
	{
		m_scene = scene;
	}

	void Entity::SetParentActivated(bool parentActivated)
	{
		m_parentActivated = parentActivated;

		for (Entity* child : m_children)
		{
			child->SetParentActivated(parentActivated);
		}
	}

	void Entity::RegisterType(NativeReflectType<Entity>& type)
	{
		type.Field<&Entity::m_transform, &Entity::GetTransform, static_cast<void(Entity::*)(const Transform&)>(&Entity::SetTransform)>("transform");
		type.Function<&Entity::GetName>("GetName");
		type.Function<&Entity::SetName>("SetName", "name");
		type.Function<&Entity::SetActive>("SetActive", "active");
		type.Function<&Entity::IsActive>("IsActive");
		type.Function<&Entity::GetUUID>("GetUUID");
		type.Function<&Entity::GetParent>("GetParent");
		type.Function<&Entity::SetParent>("SetParent", "parent");
		type.Function<&Entity::GetScene>("GetScene");
		type.Function<&Entity::Children>("Children");
		type.Function<static_cast<void(Entity::*)(Component*)>(&Entity::AddComponent)>("AddComponent", "component");
		type.Function<static_cast<Component* (Entity::*)(TypeID)>(&Entity::AddComponent)>("AddComponent", "typeId");
		type.Function<static_cast<Component* (Entity::*)(ReflectType*)>(&Entity::AddComponent)>("AddComponent", "reflectType");
		type.Function<static_cast<void (Entity::*)(Component*)>(&Entity::RemoveComponent)>("RemoveComponent", "component");
		type.Function<static_cast<void (Entity::*)(TypeID)>(&Entity::RemoveComponent)>("RemoveComponent", "typeId");
		type.Function<static_cast<Component* (Entity::*)(TypeID) const>(&Entity::GetComponent)>("GetComponent", "typeId");
		type.Function<static_cast<Array<Component*> (Entity::*)(TypeID) const>(&Entity::GetComponents)>("GetComponents", "typeId");
		type.Function<&Entity::GetAllComponents>("GetAllComponents");
	}
}
