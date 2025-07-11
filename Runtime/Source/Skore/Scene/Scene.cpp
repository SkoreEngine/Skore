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

#include "Scene.hpp"

#include "Component.hpp"
#include "Entity.hpp"
#include "Skore/Graphics/Graphics.hpp"


namespace Skore
{
	Scene::Scene(RID rid, bool enableResourceSync) : m_enableResourceSync(enableResourceSync)
	{
		m_rootEntity = Entity::Instantiate(this, rid);
	}

	Scene::~Scene()
	{
		Graphics::WaitIdle();

		if (m_rootEntity)
		{
			m_rootEntity->DestroyInternal(false);
		}
	}

	Entity* Scene::GetRootEntity() const
	{
		return m_rootEntity;
	}

	bool Scene::IsResourceSyncEnabled() const
	{
		return m_enableResourceSync;
	}

	RenderStorage* Scene::GetRenderStorage()
	{
		return &m_renderStorage;
	}

	Entity* Scene::FindEntityByRID(RID rid) const
	{
		if (auto it = m_entities.Find(rid))
		{
			return it->second;
		}
		return nullptr;
	}

	void Scene::Update()
	{
		while (!m_queueToStart.IsEmpty())
		{
			Entity* entity = m_queueToStart.Dequeue();
			entity->DoStart();
		}

		while (!m_componentsToStart.IsEmpty())
		{
			Component* component = m_componentsToStart.Dequeue();
			component->OnStart();
		}

		while (!m_queueToDestroy.IsEmpty())
		{
			Entity* entity = m_queueToDestroy.Dequeue();
			entity->DestroyInternal(true);
		}

		for (Component* component : m_updateComponents)
		{
			component->OnUpdate();
		}
	}
}
