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

#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Component.hpp"


namespace Skore
{
	static HashMap<UUID, Entity*> entities;



	UUID Entity::GetUUID() const
	{
		return m_uuid;
	}

	StringView Entity::GetName() const
	{
		return m_name;
	}

	void Entity::SetName(StringView name)
	{
		m_name = name;
	}

	void Entity::SetActive(bool active)
	{
		m_active = active;
	}

	bool Entity::IsActive() const
	{
		return m_active;
	}

	void Entity::AddChild(Entity* entity)
	{
		entity->m_parent = this;
		m_children.EmplaceBack(entity);
	}

	Entity* Entity::GetParent() const
	{
		return m_parent;
	}

	bool Entity::HasChildren() const
	{
		return !m_children.Empty();
	}

	bool Entity::IsChildOf(const Entity* parent) const
	{
		if (m_parent == nullptr || parent == nullptr) return false;

		while (parent != nullptr)
		{
			if (m_parent == parent)
			{
				return true;
			}
			parent = m_parent->m_parent;
		}
		return false;
	}

	Span<Entity*> Entity::Children() const
	{
		return m_children;
	}

	Component* Entity::AddComponent(TypeID typeId)
	{
		if (ReflectType* type = Reflection::FindTypeById(typeId))
		{
			Component* newComponent = type->NewObject()->SafeCast<Component>();
			m_components.EmplaceBack(newComponent);
			return newComponent;
		}
		return nullptr;
	}

	Span<Component*> Entity::GetAllComponents() const
	{
		return m_components;
	}

	void Entity::UpdateTransform()
	{
		//TODO
	}

	Entity* Entity::Instantiate(UUID uuid, StringView name)
	{
		Entity* instance = static_cast<Entity*>(MemAlloc(sizeof(Entity)));
		new(instance) Entity();

		instance->m_uuid = uuid;
		instance->m_name = name;

		//TODO: mutex lock here?
		entities.Insert(instance->m_uuid, instance);

		return instance;
	}

	Entity* Entity::Instantiate()
	{
		return Instantiate(UUID::RandomUUID(), "");
	}

	Entity* Entity::FindByUUID(const UUID& uuid)
	{
		if (auto it = entities.Find(uuid))
		{
			return it->second;
		}
		return nullptr;
	}

	void Entity::RegisterType(NativeReflectType<Entity>& type) {}
}
