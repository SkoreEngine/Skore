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

#include "Component.hpp"
#include "Scene.hpp"
#include "SceneCommon.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	static Logger&               logger = Logger::GetLogger("Skore::Entity");
	static HashMap<RID, Entity*> cacheByRID;

	Entity* Entity::Instantiate(Scene* scene)
	{
		return Instantiate(scene, nullptr, {});
	}

	Entity* Entity::Instantiate(Scene* scene, RID rid)
	{
		return Instantiate(scene, nullptr, rid);
	}

	Entity* Entity::Instantiate(Scene* scene, Entity* parent)
	{
		return Instantiate(scene, parent, {});
	}

	Entity* Entity::Instantiate(Scene* scene, Entity* parent, RID rid)
	{
		Entity* entity = nullptr;

		if (rid)
		{
			if (const auto& it = cacheByRID.Find(rid))
			{
				entity = it->second;
			}
		}

		if (entity == nullptr)
		{
			entity = static_cast<Entity*>(MemAlloc(sizeof(Entity)));
			new(PlaceHolder{}, entity) Entity();
		}

		Instantiate(entity, scene, parent, rid);
		return entity;
	}

	void Entity::Instantiate(Entity* entity, Scene* scene, Entity* parent, RID rid)
	{
		entity->m_rid = rid;
		entity->m_scene = scene;
		entity->m_parent = parent;


		if (entity->m_parent)
		{
			entity->m_parentActive = entity->m_parent->IsActive();
		}

		if (rid)
		{
			cacheByRID.Insert(rid, entity);

			if (scene->IsResourceSyncEnabled())
			{
				Resources::GetStorage(rid)->RegisterEvent(ResourceEventType::Changed, OnEntityResourceChange, entity);
				scene->m_entities.Insert(rid, entity);
			}

			if (ResourceObject entityObject = Resources::Read(rid))
			{
				entity->SetName(entityObject.GetString(EntityResource::Name));
				entity->m_boneIndex = entityObject.GetUInt(EntityResource::BoneIndex);

				if (RID transform = entityObject.GetReference(EntityResource::Transform))
				{
					entity->m_transformRID = transform;
					Resources::FromResource(transform, &entity->m_transform);
					entity->UpdateTransform();

					if (entity->m_scene->IsResourceSyncEnabled())
					{
						Resources::GetStorage(entity->m_transformRID)->RegisterEvent(ResourceEventType::VersionUpdated, OnTransformResourceChange, entity);
					}
				}

				entityObject.IterateSubObjectList(EntityResource::Components, [&](RID component)
				{
					if (ResourceType* type = Resources::GetType(component))
					{
						entity->AddComponent(type->GetReflectType(), component);
					}
				});

				entityObject.IterateSubObjectList(EntityResource::Children, [&](RID child)
				{
					entity->CreateChildFromAsset(child);
				});

				entity->SetActive(!entityObject.GetBool(EntityResource::Deactivated));
			}
		}

		entity->m_scene->m_queueToStart.Enqueue(entity);
	}

	Entity* Entity::FindOrCreateInstance(RID rid)
	{
		if (!rid) return {};

		auto it = cacheByRID.Find(rid);
		if (it == cacheByRID.end())
		{
			Entity* entity = static_cast<Entity*>(MemAlloc(sizeof(Entity)));
			new(PlaceHolder{}, entity) Entity();
			it = cacheByRID.Insert(rid, entity).first;
		}

		return it->second;
	}

	Entity::~Entity()
	{
		if (m_rid)
		{
			cacheByRID.Erase(m_rid);
		}

		if (m_scene)
		{
			if (m_scene->IsResourceSyncEnabled())
			{
				if (m_rid)
				{
					m_scene->m_entities.Erase(m_rid);
					Resources::GetStorage(m_rid)->UnregisterEvent(ResourceEventType::Changed, OnEntityResourceChange, this);
				}

				if (m_transformRID)
				{
					Resources::GetStorage(m_transformRID)->UnregisterEvent(ResourceEventType::VersionUpdated, OnTransformResourceChange, this);
				}
			}

			if (m_scene->m_rootEntity == this)
			{
				m_scene->m_rootEntity = nullptr;
			}
		}
	}

	Transform& Entity::GetTransform()
	{
		return m_transform;
	}

	Scene* Entity::GetScene() const
	{
		return m_scene;
	}

	void Entity::SetParent(Entity* newParent)
	{
		if (m_parent == newParent) return;

		if (m_parent)
		{
			for (int i = 0; i < m_parent->m_children.Size(); ++i)
			{
				if (m_parent->m_children[i] == this)
				{
					m_parent->m_children.Remove(i);
					break;
				}
			}
		}

		m_parent = newParent;
		m_parent->m_children.EmplaceBack(this);

		if (m_parent)
		{
			m_parentActive = m_parent->IsActive();
		}
		else
		{
			m_parentActive = true;
		}

		UpdateTransform();
	}

	Entity* Entity::GetParent() const
	{
		return m_parent;
	}

	Span<Entity*> Entity::GetChildren() const
	{
		return m_children;
	}

	Span<Component*> Entity::GetComponents() const
	{
		return m_components;
	}

	RID Entity::GetRID() const
	{
		return m_rid;
	}

	RID Entity::GetTransformRID() const
	{
		return m_transformRID;
	}

	void Entity::SetName(StringView name)
	{
		m_name = name;
	}

	StringView Entity::GetName() const
	{
		return m_name;
	}

	void Entity::SetActive(bool active)
	{
		if (active == m_active) return;

		m_active = active;

		EntityEventDesc desc{
			.type = m_active ? EntityEventType::EntityActivated : EntityEventType::EntityDeactivated
		};
		NotifyEvent(desc, true);
	}

	bool Entity::IsActive() const
	{
		return m_active && m_parentActive;
	}

	Entity* Entity::CreateChild()
	{
		Entity* child = Instantiate(m_scene, this);
		m_children.EmplaceBack(child);
		return child;
	}

	Entity* Entity::CreateChildFromAsset(RID rid)
	{
		Entity* child = Instantiate(m_scene, this, rid);
		m_children.EmplaceBack(child);
		return child;
	}

	Component* Entity::AddComponent(TypeID typeId)
	{
		return AddComponent(Reflection::FindTypeById(typeId));
	}

	Component* Entity::AddComponent(ReflectType* reflectType)
	{
		return AddComponent(reflectType, {});
	}

	Component* Entity::AddComponent(ReflectType* reflectType, RID rid)
	{
		if (!reflectType) return nullptr;

		Component* component = reflectType->NewObject()->SafeCast<Component>();
		component->entity = this;
		component->m_rid = rid;

		if (rid)
		{
			Resources::FromResource(rid, component);

			if (m_scene->IsResourceSyncEnabled())
			{
				Resources::GetStorage(component->m_rid)->RegisterEvent(ResourceEventType::VersionUpdated, OnComponentResourceChange, component);
			}
		}

		component->Create(component->m_settings);
		component->RegisterEvents();

		m_components.EmplaceBack(component);

		if (m_started)
		{
			m_scene->m_componentsToStart.Enqueue(component);
		}

		return component;
	}

	void Entity::RemoveComponent(Component* component)
	{
		for (int i = 0; i < m_components.Size(); ++i)
		{
			if (m_components[i] == component)
			{
				DestroyComponent(component);
				m_components.Remove(i);
				break;
			}
		}
	}


	void Entity::Destroy()
	{
		m_scene->m_queueToDestroy.Enqueue(this);
	}

	void Entity::NotifyEvent(const EntityEventDesc& event, bool notifyChildren)
	{
		if (event.type == EntityEventType::TransformUpdated)
		{
			const Mat4& parentTransform = m_parent ? m_parent->GetGlobalTransform() : Mat4(1.0);
			m_globalTransform = parentTransform * GetLocalTransform();
		}

		if (event.type == EntityEventType::EntityActivated)
		{
			m_parentActive = true;

			for (Component* component : m_components)
			{
				component->RegisterEvents();
			}
		}
		else if (event.type == EntityEventType::EntityDeactivated)
		{
			m_parentActive = false;

			for (Component* component : m_components)
			{
				component->RemoveEvents();
			}
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

	void Entity::DestroyInternal(bool removeFromParent)
	{
		if (m_parent && removeFromParent)
		{
			for (int i = 0; i < m_parent->m_children.Size(); ++i)
			{
				if (m_parent->m_children[i] == this)
				{
					m_parent->m_children.Remove(i);
					break;
				}
			}
		}

		for (Entity* child : m_children)
		{
			child->DestroyInternal(false);
		}

		for (Component* component : m_components)
		{
			DestroyComponent(component);
		}

		DestroyAndFree(this);
	}

	void Entity::UpdateTransform()
	{
		EntityEventDesc desc{
			.type = EntityEventType::TransformUpdated
		};
		NotifyEvent(desc, true);
	}

	void Entity::DoStart()
	{
		if (m_started)
		{
			logger.Warn("DoStart called on entity that is already started!");
			return;
		}

		m_started = true;

		for (Component* component : m_components)
		{
			component->OnStart();
		}
	}

	void Entity::DestroyComponent(Component* component) const
	{
		component->RemoveEvents();

		component->Destroy();
		if (m_scene->IsResourceSyncEnabled())
		{
			Resources::GetStorage(component->m_rid)->UnregisterEvent(ResourceEventType::VersionUpdated, OnComponentResourceChange, component);
		}
		DestroyAndFree(component);
	}

	void Entity::OnEntityResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		Entity* entity = static_cast<Entity*>(userData);

		//destroyed
		if (oldValue && !newValue)
		{
			entity->DestroyInternal(true);
			return;
		}

		if (newValue)
		{
			entity->SetName(newValue.GetString(EntityResource::Name));
			entity->SetActive(!newValue.GetBool(EntityResource::Deactivated));
			entity->m_boneIndex = newValue.GetUInt(EntityResource::BoneIndex);
		}

		for (CompareSubObjectListResult res : Resources::CompareSubObjectList(oldValue, newValue, EntityResource::Children))
		{
			if (res.type == CompareSubObjectSetType::Added)
			{
				if (Entity* child = entity->GetScene()->FindEntityByRID(res.rid))
				{
					child->SetParent(entity);
				}
				else
				{
					entity->CreateChildFromAsset(res.rid);
				}
			}

			else if (res.type == CompareSubObjectSetType::Removed)
			{
				for (Entity* child : entity->m_children)
				{
					if (child->m_rid == res.rid)
					{
						if (newValue.IsRemoveFromPrototypeSubObjectList(EntityResource::Children, res.rid))
						{
							child->DestroyInternal(true);
						}
					}
				}
			}
		}

		for (CompareSubObjectListResult res : Resources::CompareSubObjectList(oldValue, newValue, EntityResource::Components))
		{
			if (res.type == CompareSubObjectSetType::Added)
			{
				if (ResourceType* type = Resources::GetType(res.rid))
				{
					entity->AddComponent(type->GetReflectType(), res.rid);
				}
			}
			else if (res.type == CompareSubObjectSetType::Removed)
			{
				for (Component* component : entity->m_components)
				{
					if (component->m_rid == res.rid)
					{
						entity->RemoveComponent(component);
						break;
					}
				}
			}
		}
	}

	void Entity::OnComponentResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		Resources::FromResource(newValue, userData);
	}

	void Entity::OnTransformResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		Entity* entity = static_cast<Entity*>(userData);
		Resources::FromResource(newValue, &entity->m_transform);
		entity->UpdateTransform();
	}
}
