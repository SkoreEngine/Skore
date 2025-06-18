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
#include "World.hpp"
#include "WorldCommon.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	Entity::Entity(World* world) : Entity(world, {}) {}
	Entity::Entity(World* world, RID rid) : Entity(world, nullptr, rid) {}
	Entity::Entity(World* world, Entity* parent) : Entity(world, parent, {}) {}

	Entity::Entity(World* world, Entity* parent, RID rid) : m_rid(rid), m_world(world), m_parent(parent)
	{
		if (rid)
		{
			if (world->IsResourceSyncEnabled())
			{
				Resources::GetStorage(rid)->RegisterEvent(ResourceEventType::Changed, OnEntityResourceChange, this);
			}

			if (ResourceObject entityObject = Resources::Read(rid))
			{
				SetName(entityObject.GetString(EntityResource::Name));

				if (RID transform = entityObject.GetReference(EntityResource::Transform))
				{
					Resources::FromResource(transform, &m_transform);
					UpdateTransform();

					if (m_world->IsResourceSyncEnabled())
					{
						Resources::GetStorage(transform)->RegisterEvent(ResourceEventType::VersionUpdated, OnTransformResourceChange, this);
					}
				}

				entityObject.IterateSubObjectSet(EntityResource::Components, true, [&](RID component)
				{
					if (ResourceType* type = Resources::GetType(component))
					{
						AddComponent(type->GetReflectType(), component);
					}
					return true;
				});

				entityObject.IterateSubObjectSet(EntityResource::Children, true, [&](RID child)
				{
					CreateChildFromAsset(child);
					return true;
				});
			}
		}
	}

	Entity::~Entity()
	{
		if (m_world->IsResourceSyncEnabled() && m_rid)
		{
			Resources::GetStorage(m_rid)->UnregisterEvent(ResourceEventType::Changed, OnEntityResourceChange, this);

			if (ResourceObject entityObject = Resources::Read(m_rid))
			{
				if (RID transform = entityObject.GetReference(EntityResource::Transform))
				{
					Resources::GetStorage(transform)->RegisterEvent(ResourceEventType::VersionUpdated, OnTransformResourceChange, &m_transform);
				}
			}
		}
	}

	Transform& Entity::GetTransform()
	{
		return m_transform;
	}

	World* Entity::GetWorld() const
	{
		return m_world;
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

	void Entity::SetName(StringView name)
	{
		m_name = name;
	}

	StringView Entity::GetName() const
	{
		return m_name;
	}

	Entity* Entity::CreateChild()
	{
		Entity* child = Instantiate(m_world, this);
		m_children.EmplaceBack(child);
		return child;
	}

	Entity* Entity::CreateChildFromAsset(RID rid)
	{
		Entity* child = Instantiate(m_world, this, rid);
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

			if (m_world->IsResourceSyncEnabled())
			{
				Resources::GetStorage(component->m_rid)->RegisterEvent(ResourceEventType::VersionUpdated, OnComponentResourceChange, component);
			}
		}

		component->Create();

		m_components.EmplaceBack(component);

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
		m_world->m_queueToDestroy.Enqueue(this);
	}

	void Entity::NotifyEvent(const EntityEventDesc& event, bool notifyChildren)
	{
		if (event.type == EntityEventType::TransformUpdated)
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

	void Entity::DestroyComponent(Component* component) const
	{
		component->Destroy();
		if (m_world->IsResourceSyncEnabled())
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
		}

		for (CompareSubObjectSetResult res : Resources::CompareSubObjectSet(oldValue, newValue, EntityResource::Children))
		{
			if (res.type == CompareSubObjectSetType::Added)
			{
				entity->CreateChildFromAsset(res.rid);
			}
		}

		for (CompareSubObjectSetResult res : Resources::CompareSubObjectSet(oldValue, newValue, EntityResource::Components))
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
