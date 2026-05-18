#include "Skore/Scene/Scene.hpp"

#include "Skore/Scene/Component.hpp"
#include "Skore/App.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Events.hpp"
#include "Skore/Profiler.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Reflection.hpp"


namespace Skore
{
	Scene::Scene(RID rid, bool enableResourceSync) : m_enableResourceSync(enableResourceSync), m_sceneRID(rid)
	{
		if (ResourceObject sceneResource = Resources::Read(rid))
		{
			Span<RID> entitiesRID = sceneResource.GetSubObjectList(SceneResource::Entities);
			for (RID entityRID : entitiesRID)
			{
				Entity::Instantiate(this, nullptr, entityRID, true);
			}
		}

		if (enableResourceSync)
		{
			Resources::GetStorage(rid)->RegisterEvent(ResourceEventType::Changed, OnSceneResourceChange, this);
			Event::Bind<OnPluginReloaded, &Scene::DoReflectionUpdated>(this);
		}
	}

	Scene::Scene(TypedRID<EntityResource> rid, bool enableResourceSync) : m_enableResourceSync(enableResourceSync)
	{
		Entity::Instantiate(this, nullptr, rid, true);
		if (enableResourceSync)
		{
			Event::Bind<OnPluginReloaded, &Scene::DoReflectionUpdated>(this);
		}
	}

	Scene::~Scene()
	{
		if (m_enableResourceSync && m_sceneRID)
		{
			Resources::GetStorage(m_sceneRID)->UnregisterEvent(ResourceEventType::Changed, OnSceneResourceChange, this);
		}
		Event::Unbind<OnPluginReloaded, &Scene::DoReflectionUpdated>(this);

		for (Entity* entity : entities)
		{
			entity->DestroyInternal(false);
		}

		for (auto& it : entitiesByRID)
		{
			//entity was never added to a parent/scene
			if (it.second->m_scene == nullptr)
			{
				it.second->DestroyInternal(false);
			}
		}
		entitiesByRID.Clear();
		entities.Clear();
	}

	bool Scene::IsResourceSyncEnabled() const
	{
		return m_enableResourceSync;
	}

	Entity* Scene::FindEntityByRID(RID rid) const
	{
		if (auto it = entitiesByRID.Find(rid))
		{
			return it->second;
		}
		return nullptr;
	}

	Entity* Scene::FindFirstByName(StringView name) const
	{
		if (auto it = entitiesByName.Find(name))
		{
			if (!it->second.Empty())
			{
				return it->second[0];
			}
		}

		return nullptr;
	}

	Component* Scene::FindFirstComponent(TypeID typeId) const
	{
		for (Entity* entity : entities)
		{
			if (Component* component = entity->FindFirstComponentOnHierarchy(typeId))
			{
				return component;
			}
		}
		return nullptr;
	}

	Entity* Scene::CreateEntity()
	{
		return Entity::Instantiate(this);
	}

	Entity* Scene::CreateEntityFromRID(RID rid)
	{
		return Entity::Instantiate(this, nullptr, rid, false);
	}

	Span<Entity*> Scene::GetEntities() const
	{
		return entities;
	}

	AABB Scene::GetBounds() const
	{
		AABB bounds;
		for (Entity* entity : entities)
		{
			bounds.Expand(entity->GetBounds());
		}
		return bounds;
	}

	void Scene::RegisterType(NativeReflectType<Scene>& type)
	{
		type.Function<&Scene::FindEntityByRID>("FindEntityByRID", "rid");
		type.Function<&Scene::FindFirstByName>("FindFirstByName", "name");
		type.Function<&Scene::CreateEntity>("CreateEntity");
		type.Function<&Scene::CreateEntityFromRID>("CreateEntityFromRID", "rid");
	}

	void Scene::NotifyEvent(const EntityEventDesc& event, bool notifyChildren)
	{
		for (Entity* entity : entities)
		{
			entity->NotifyEvent(event, notifyChildren);
		}
	}

	Entity* Scene::FindOrCreateInstance(RID rid)
	{
		if (!rid) return {};

		auto it = entitiesByRID.Find(rid);
		if (it == entitiesByRID.end())
		{
			Entity* entity = static_cast<Entity*>(MemAlloc(sizeof(Entity)));
			new(PlaceHolder{}, entity) Entity();
			it = entitiesByRID.Insert(rid, entity).first;
		}

		return it->second;
	}

	void Scene::OnSceneDeactivated()
	{
		//TODO
	}

	void Scene::OnSceneActivated()
	{
		//TODO
	}

	void Scene::ExecuteEvents(bool executeComponentUpdates)
	{
		for (Component* component : m_pendingUpdate)
		{
			EntityEventDesc event = {};
			event.type = EntityEventType::ComponentUpdated;
			component->ProcessEvent(event);
			component->m_currentVersion++;
		}
		m_pendingUpdate.clear();


		SK_SCOPED_CPU_ZONE("Scene - ExecuteEvents");
		while (!m_queueToStart.IsEmpty())
		{
			Entity* entity = m_queueToStart.Dequeue();
			entity->DoStart(executeComponentUpdates);
		}

		if (executeComponentUpdates)
		{
			while (!m_componentsToStart.IsEmpty())
			{
				Component* component = m_componentsToStart.Dequeue();
				component->OnStart();
			}
		}

		while (!m_queueToDestroy.IsEmpty())
		{
			Entity* entity = m_queueToDestroy.Dequeue();
			entity->DestroyInternal(true);
		}

		while (!m_updateToAdd.IsEmpty())
		{
			m_updateComponents.emplace(m_updateToAdd.Dequeue());
		}

		while (!m_updateToRemove.IsEmpty())
		{
			m_updateComponents.erase(m_updateToRemove.Dequeue());
		}

		while (!m_fixedUpdateToAdd.IsEmpty())
		{
			m_fixedUpdateComponents.emplace(m_fixedUpdateToAdd.Dequeue());
		}

		while (!m_fixedUpdateToRemove.IsEmpty())
		{
			m_fixedUpdateComponents.erase(m_fixedUpdateToRemove.Dequeue());
		}

		physicsScene.ExecuteEvents();
	}

	void Scene::Update()
	{
		SK_SCOPED_CPU_ZONE("Scene - Update");

		Physics::SetCurrentScene(&physicsScene);
		Navigation::SetCurrentScene(&navigationScene);

		ExecuteEvents();

		physicsScene.UpdateCharacterControllers();

		f32 stepSize = physicsScene.GetFixedTimeStep();
		if (stepSize > 0.0f)
		{
			m_physicsAccumulator += App::DeltaTime();
			while (m_physicsAccumulator >= stepSize)
			{
				SK_SCOPED_CPU_ZONE("Scene - OnFixedUpdate");
				for (FixedTickable* fixedTickable : m_fixedUpdateComponents)
				{
					fixedTickable->OnFixedUpdate(stepSize);
				}
				physicsScene.DoFixedUpdate(stepSize);
				m_physicsAccumulator -= stepSize;
			}
		}

		physicsScene.WriteBackTransforms();
		physicsScene.ProcessCollisionEvents();
		physicsScene.ProcessPendingBodiesToAdd();

		navigationScene.Update(static_cast<f32>(App::DeltaTime()));

		{
			SK_SCOPED_CPU_ZONE("Scene - OnUpdate");

			f64 deltaTime = App::DeltaTime();
			for (Tickable* tickable : m_updateComponents)
			{
				tickable->OnUpdate(deltaTime);
			}
		}
	}

	void Scene::OnSceneResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		Scene* scene = static_cast<Scene*>(userData);

		for (CompareSubObjectListResult res : Resources::CompareSubObjectList(oldValue, newValue, SceneResource::Entities))
		{
			if (res.type == CompareSubObjectSetType::Added)
			{
				if (!scene->FindEntityByRID(res.rid))
				{
					Entity::Instantiate(scene, nullptr, res.rid, true);
				}
			}
			else if (res.type == CompareSubObjectSetType::Removed)
			{
				for (Entity* entity : scene->entities)
				{
					if (entity->m_rid == res.rid)
					{
						entity->DestroyInternal(true);
						break;
					}
				}
			}
		}
	}

	void Scene::DoReflectionUpdated()
	{
		for (Entity* entity : entities)
		{
			entity->ReflectionReload();
		}
	}
}