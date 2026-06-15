#include "EntityTracker.hpp"

#include <atomic>
#include <mutex>

#include "Skore/Core/Event.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	namespace
	{
		//scenes can be created/destroyed from different threads
		std::mutex       entitiesMutex;
		HashSet<Entity*> aliveEntities;
		std::atomic<u64> aliveEntitiesRevision = 0;

		void EntityCreated(Entity* entity)
		{
			std::unique_lock lock(entitiesMutex);
			aliveEntities.Insert(entity);
			aliveEntitiesRevision.fetch_add(1, std::memory_order_relaxed);
		}

		void EntityRemoved(Entity* entity)
		{
			std::unique_lock lock(entitiesMutex);
			aliveEntities.Erase(entity);
			aliveEntitiesRevision.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void EntityTracker::Init()
	{
		Event::Bind<OnEntityCreated, &EntityCreated>();
		Event::Bind<OnEntityRemoved, &EntityRemoved>();
	}

	void EntityTracker::Shutdown()
	{
		Event::Unbind<OnEntityCreated, &EntityCreated>();
		Event::Unbind<OnEntityRemoved, &EntityRemoved>();

		std::unique_lock lock(entitiesMutex);
		aliveEntities.Clear();
		aliveEntitiesRevision.fetch_add(1, std::memory_order_relaxed);
	}

	bool EntityTracker::IsAlive(Entity* entity)
	{
		if (!entity) return false;
		std::unique_lock lock(entitiesMutex);
		return aliveEntities.Has(entity);
	}

	u64 EntityTracker::GetRevision()
	{
		return aliveEntitiesRevision.load(std::memory_order_relaxed);
	}
}
