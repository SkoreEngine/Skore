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

#include "World.hpp"

#include "Entity.hpp"


namespace Skore
{
	World::World(RID rid, bool enableResourceSync) : m_enableResourceSync(enableResourceSync)
	{
		m_rootEntity = Entity::Instantiate(this, rid);
	}

	World::~World()
	{
		if (m_rootEntity)
		{
			m_rootEntity->DestroyInternal(false);
		}
	}

	Entity* World::GetRootEntity() const
	{
		return m_rootEntity;
	}

	bool World::IsResourceSyncEnabled() const
	{
		return m_enableResourceSync;
	}

	RenderStorage* World::GetRenderStorage()
	{
		return &m_renderStorage;
	}

	Entity* World::FindEntityByRID(RID rid) const
	{
		if (auto it = m_entities.Find(rid))
		{
			return it->second;
		}
		return nullptr;
	}
}
