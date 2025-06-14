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

#include "World.hpp"
#include "WorldCommon.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	Entity::Entity(World* world) : m_world(world)
	{

	}

	Entity::Entity(World* world, RID rid) : m_rid(rid), m_world(world)
	{
		if (ResourceObject entityObject = Resources::Read(rid))
		{

			entityObject.IterateSubObjectSet(EntityResource::Children, true, [&](RID child)
			{
				CreateChildFromAsset(child);
				return true;
			});
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

	Entity* Entity::CreateChild()
	{
		return nullptr;
	}

	Entity* Entity::CreateChildFromAsset(RID rid)
	{
		Entity* child = Instantiate(m_world, rid);
		m_children.EmplaceBack(child);
		return child;
	}

	void Entity::Destroy()
	{
		m_world->m_queueToDestroy.Enqueue(this);
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

		DestroyAndFree(this);
	}

	void Entity::UpdateTransform()
	{

	}
}
