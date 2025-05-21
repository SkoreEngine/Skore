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
#include "Skore/Core/Allocator.hpp"
#include "Skore/Graphics/BasicSceneRenderer.hpp"

namespace Skore
{
	Scene::Scene()
	{
		m_renderStorage = Alloc<RenderStorage>();

		//root entity creation
		m_rootEntity = Alloc<Entity>();
		m_rootEntity->m_scene = this;
		m_rootEntity->SetName("Entity");
		m_rootEntity->SetUUID(UUID::RandomUUID());
		m_entities.Insert(m_rootEntity->GetUUID(), m_rootEntity);
	}

	Scene::~Scene()
	{
		DestroyAndFree(m_renderStorage);
	}

	void Scene::FlushQueues()
	{
		while (!m_queueToDestroy.IsEmpty())
		{
			Entity* entity = m_queueToDestroy.Dequeue();
			entity->DestroyInternal(true);
		}

		while (!m_queueToStart.IsEmpty())
		{
			Entity* entity = m_queueToStart.Dequeue();
			entity->DoStart();
		}

		while (!m_componentsToStart.IsEmpty())
		{
			Component* component = m_componentsToStart.Dequeue();
			component->Start();
		}
	}

	void Scene::Update(f64 deltaTime)
	{
		FlushQueues();

		for (Component* component : m_updateComponents)
		{
			if (component->CanUpdate())
			{
				component->Update(deltaTime);
			}
		}
	}

	void Scene::SetRootEntity(Entity* rootEntity)
	{
		if (m_rootEntity)
		{
			m_rootEntity->m_scene = nullptr;
			m_rootEntity->m_parent = nullptr;
		}

		m_rootEntity = rootEntity;

		if (m_rootEntity)
		{
			m_rootEntity->m_scene = this;
			m_rootEntity->m_parent = nullptr;
		}
	}

	Entity* Scene::GetRootEntity() const
	{
		return m_rootEntity;
	}

	void Scene::Serialize(ArchiveWriter& archiveWriter) const
	{
		if (!m_rootEntity) return;

		archiveWriter.WriteInt("version", 1);

		archiveWriter.BeginSeq("entities");
		Queue<const Entity*> pending;
		const Entity* current = m_rootEntity;

		while (current != nullptr)
		{
			archiveWriter.BeginMap();
			current->Serialize(archiveWriter);
			archiveWriter.EndMap();

			for (Entity* child : current->Children())
			{
				pending.Enqueue(child);
			}

			current = nullptr;

			if (!pending.IsEmpty())
			{
				current = pending.Dequeue();
			}
		}

		archiveWriter.EndSeq();
	}

	void Scene::Deserialize(ArchiveReader& archiveReader)
	{
		archiveReader.BeginSeq("entities");

		auto createEntity = [&]() -> Entity*
		{
			Entity* entity = Alloc<Entity>();
			entity->m_scene = this;
			entity->Deserialize(archiveReader);
			return entity;
		};

		//first = root
		if (archiveReader.NextSeqEntry())
		{
			archiveReader.BeginMap();
			m_rootEntity->Deserialize(archiveReader);
			archiveReader.EndMap();
		}

		while (archiveReader.NextSeqEntry())
		{
			archiveReader.BeginMap();
			createEntity();
			archiveReader.EndMap();
		}
		archiveReader.EndSeq();

		if (m_rootEntity)
		{
			m_rootEntity->LoadPrefab();
		}

	}

	Entity* Scene::FindEntityByUUID(UUID uuid)
	{
		if (auto it = m_entities.Find(uuid))
		{
			return it->second;
		}
		return nullptr;
	}

	RenderStorage* Scene::GetRenderStorage() const
	{
		return m_renderStorage;
	}

	void Scene::RegisterComponentForUpdate(Component* component)
	{
		m_updateComponents.emplace(component);
	}

	void Scene::UnregisterComponentForUpdate(Component* component)
	{
		m_updateComponents.erase(component);
	}

	void Scene::RegisterType(NativeReflectType<Scene>& type)
	{
		type.Function<&Scene::GetRootEntity>("GetRootEntity");
		type.Function<&Scene::FindEntityByUUID>("FindByUUID", "uuid");
	}
}
