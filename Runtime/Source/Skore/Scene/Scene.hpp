#pragma once
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Physics.hpp"
#include "Skore/Navigation/Navigation.hpp"
#include "Skore/Core/UnorderedDense.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Resource/ResourceReflection.hpp"

namespace Skore
{
	class SK_API Scene : public Object
	{
	public:
		SK_CLASS(Scene, Object);
		SK_NO_COPY_CONSTRUCTOR(Scene);

		Scene() = default;
		Scene(RID rid, bool enableResourceSync = false);
		Scene(TypedRID<EntityResource> rid, bool enableResourceSync = false);
		~Scene() override;

		bool    IsResourceSyncEnabled() const;

		Entity* FindEntityByRID(RID rid) const;
		Entity* FindFirstByName(StringView name) const;

		//SLOW! use with careful
		Component* FindFirstComponent(TypeID typeId) const;

		Entity* CreateEntity();
		Entity* CreateEntityFromRID(RID rid);

		Span<Entity*> GetEntities() const;

		AABB GetBounds() const;

		friend class Entity;
		friend class SceneManager;
		friend class Component;
		friend class SceneEditor;

		friend class ResourceCast<Entity*>;

		RenderSceneObjects renderObjects{this};
		PhysicsScene       physicsScene;
		NavigationScene    navigationScene;

		void NotifyEvent(const EntityEventDesc& event, bool notifyChildren = false);

		static void RegisterType(NativeReflectType<Scene>& type);

		static Scene* CreateFromEntity(RID rid, bool enableResourceSync = false);

	private:
		Array<Entity*>                  entities;
		HashMap<RID, Entity*>           entitiesByRID;
		HashMap<String, Array<Entity*>> entitiesByName;

		bool m_enableResourceSync = false;
		RID  m_sceneRID = {};

		Queue<Entity*>    m_queueToStart;
		Queue<Component*> m_componentsToStart;
		Queue<Component*> m_updateToAdd;
		Queue<Component*> m_updateToRemove;
		Queue<Entity*>    m_queueToDestroy;

		DenseSet<Component*> m_updateComponents = {};
		DenseSet<Component*> m_fixedUpdateComponents = {};


		Entity* FindOrCreateInstance(RID rid);

		void OnSceneDeactivated();
		void OnSceneActivated();
		void ExecuteEvents(bool executeComponentUpdates = true);
		void Update();
		void DoReflectionUpdated();

		static void OnSceneResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
	};
}
