#include "EntityTracker.hpp"

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

		void EntityCreated(Entity* entity)
		{
			std::unique_lock lock(entitiesMutex);
			aliveEntities.Insert(entity);
		}

		void EntityRemoved(Entity* entity)
		{
			std::unique_lock lock(entitiesMutex);
			aliveEntities.Erase(entity);
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
	}

	bool EntityTracker::IsAlive(Entity* entity)
	{
		if (!entity) return false;
		std::unique_lock lock(entitiesMutex);
		return aliveEntities.Has(entity);
	}
}
