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


namespace Skore
{
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

	void Entity::SetActive(bool active) {}

	bool Entity::IsActive() const
	{
		return false;
	}

	void Entity::AddChild(Entity* entity)
	{
		//TODO
	}

	Entity* Entity::GetParent() const
	{
		//TODO
		return nullptr;
	}

	bool Entity::HasChildren() const
	{
		return false;
	}

	bool Entity::IsChildOf(Entity* parent) const
	{
		return false;
	}

	Span<Entity*> Entity::Children() const
	{
		return {};
	}

	Span<Component*> Entity::GetAllComponents() const
	{
		return {};
	}

	void Entity::UpdateTransform()
	{
		//TODO
	}

	Entity* Entity::New()
	{
		Entity* instance = static_cast<Entity*>(MemAlloc(sizeof(Entity)));
		new(instance) Entity();

		instance->m_uuid = UUID::RandomUUID();

		return instance;
	}

	void Entity::RegisterType(NativeReflectType<Entity>& type) {}
}
