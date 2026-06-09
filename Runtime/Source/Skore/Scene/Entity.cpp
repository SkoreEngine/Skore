#include "Skore/Scene/Entity.hpp"

#include "Skore/Scene/Component.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Scene/SceneManager.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::Entity");

	static EventHandler<OnEntityCreated> onEntityCreatedHandler;
	static EventHandler<OnEntityRemoved> onEntityRemovedHandler;

	Entity::Entity()
	{
		onEntityCreatedHandler.Invoke(this);
	}

	Entity::~Entity()
	{
		onEntityRemovedHandler.Invoke(this);
	}


	Entity* Entity::Instantiate()
	{
		return Instantiate(nullptr, {});
	}

	Entity* Entity::Instantiate(Scene* scene)
	{
		return Instantiate(scene, nullptr, {}, false);
	}

	Entity* Entity::Instantiate(RID rid)
	{
		return Instantiate(nullptr, rid);
	}

	Entity* Entity::Instantiate(Entity* parent)
	{
		return Instantiate(parent, {});
	}

	Entity* Entity::Instantiate(Entity* parent, RID rid)
	{
		return Instantiate(SceneManager::GetActiveScene(), parent, rid, true);
	}

	Entity* Entity::Instantiate(Scene* scene, Entity* parent, RID rid, bool instanceOfAsset)
	{
		SK_ASSERT(scene, "Scene cannot be null");
		if (scene == nullptr)
		{
			return nullptr;
		}

		Entity* entity = nullptr;

		if (rid && instanceOfAsset)
		{
			if (const auto& it = scene->entitiesByRID.Find(rid))
			{
				entity = it->second;
			}
		}

		if (entity == nullptr)
		{
			entity = static_cast<Entity*>(MemAlloc(sizeof(Entity)));
			new(PlaceHolder{}, entity) Entity();
		}

		Instantiate(entity, scene, parent, rid, instanceOfAsset);

		return entity;
	}

	void Entity::Instantiate(Entity* entity, Scene* scene, Entity* parent, RID rid, bool instanceOfAsset)
	{
		if (instanceOfAsset)
		{
			entity->m_rid = rid;
		}

		entity->m_scene = scene;
		entity->m_parent = parent;


		if (entity->m_parent)
		{
			entity->m_parentActive = entity->m_parent->IsActive();
		}

		if (rid)
		{
			if (instanceOfAsset && scene->IsResourceSyncEnabled())
			{
				Resources::GetStorage(rid)->RegisterEvent(ResourceEventType::Changed, OnEntityResourceChange, entity);
				scene->entitiesByRID.Insert(rid, entity);
			}

			if (ResourceObject entityObject = Resources::Read(rid))
			{
				entity->SetName(entityObject.GetString(EntityResource::Name));
				entity->m_layer = static_cast<u8>(entityObject.GetUInt(EntityResource::Layer));

				entityObject.IterateSubObjectList(EntityResource::Components, [&](RID component)
				{
					if (ResourceType* type = Resources::GetType(component))
					{
						//TODO need to get from Resources::GetType
						if (type->GetReflectType())
						{
							entity->AddComponent(Reflection::FindTypeById(type->GetReflectType()->GetProps().typeId), component);
						}
					}
				});

				entityObject.IterateSubObjectList(EntityResource::Children, [&](RID child)
				{
					Instantiate(entity->m_scene, entity, child, instanceOfAsset);
				});

				entity->SetActive(!entityObject.GetBool(EntityResource::Deactivated));
			}
		}

		if (entity->m_parent)
		{
			entity->m_parent->m_children.EmplaceBack(entity);
		}
		else
		{
			scene->entities.EmplaceBack(entity);
		}

		entity->m_scene->m_queueToStart.Enqueue(entity);
	}

	Scene* Entity::GetScene() const
	{
		return m_scene;
	}

	void Entity::SetParent(Entity* newParent)
	{
		if (m_parent == newParent) return;

		if (m_parent)
		{
			for (int i = 0; i < m_parent->m_children.Size(); ++i)
			{
				if (m_parent->m_children[i] == this)
				{
					m_parent->m_children.RemoveAt(i);
					break;
				}
			}
		}
		else
		{
			m_scene->entities.Remove(this);
		}

		m_parent = newParent;
		m_parent->m_children.EmplaceBack(this);

		if (m_parent)
		{
			m_parentActive = m_parent->IsActive();
		}
		else
		{
			m_parentActive = true;
		}

		EntityEventDesc event;
		event.type = EntityEventType::EntityParentChanged;
		NotifyEvent(event, true);
	}

	Entity* Entity::GetParent() const
	{
		return m_parent;
	}

	Span<Entity*> Entity::GetChildren() const
	{
		return m_children;
	}

	Entity* Entity::GetChildAt(u32 index) const
	{
		if (m_children.Size() <= index) return nullptr;
		return m_children[index];
	}

	u32 Entity::GetChildrenNum() const
	{
		return m_children.Size();
	}

	Span<Component*> Entity::GetComponents() const
	{
		return m_components;
	}

	RID Entity::GetRID() const
	{
		return m_rid;
	}

	void Entity::AddFlag(EntityFlags flag)
	{
		m_flags |= static_cast<u64>(flag);
	}

	void Entity::RemoveFlag(EntityFlags flag)
	{
		if (HasFlag(flag))
		{
			m_flags &= ~static_cast<u64>(flag);
		}
	}

	bool Entity::HasFlag(EntityFlags flag) const
	{
		return (m_flags & static_cast<u64>(flag)) != 0;
	}


	void Entity::SetName(StringView name)
	{
		m_name = name;
	}

	StringView Entity::GetName() const
	{
		return m_name;
	}

	u8 Entity::GetLayer() const
	{
		return m_layer;
	}

	void Entity::SetLayer(u8 layer)
	{
		if (layer == m_layer) return;
		m_layer = layer;

		EntityEventDesc event;
		event.type = EntityEventType::EntityLayerChanged;
		NotifyEvent(event, false);

		if (HasFlag(EntityFlags::HasPhysics) && m_physicsId != U64_MAX)
		{
			m_scene->physicsScene.PhysicsEntityRequireUpdate(this);
		}
	}

	u64 Entity::GetLayerMask() const
	{
		return LayerToMask(m_layer);
	}

	void Entity::SetWorldTransform(const Mat4& transform)
	{
		m_worldTransform = transform;
	}

	const Mat4& Entity::GetWorldTransform() const
	{
		return m_worldTransform;
	}

	void Entity::SetActive(bool active)
	{
		if (active == m_active) return;

		m_active = active;

		EntityEventDesc desc{
			.type = m_active ? EntityEventType::EntityActivated : EntityEventType::EntityDeactivated
		};
		NotifyEvent(desc, true);
	}

	bool Entity::IsActive() const
	{
		return m_active && m_parentActive;
	}

	Entity* Entity::CreateChild()
	{
		return Instantiate(m_scene, this, {}, false);
	}

	Entity* Entity::CreateChildFromAsset(RID rid)
	{
		return Instantiate(m_scene, this, rid, false);
	}

	Component* Entity::AddComponent(TypeID typeId)
	{
		return AddComponent(Reflection::FindTypeById(typeId));
	}

	Component* Entity::AddComponent(ReflectType* reflectType)
	{
		return AddComponent(reflectType, {});
	}

	Component* Entity::AddComponent(ReflectType* reflectType, RID rid)
	{
		if (!reflectType) return nullptr;
		ReflectConstructor* constructor = reflectType->GetDefaultConstructor();
		if (constructor == nullptr) return nullptr;

		Component* component = constructor->NewObject(MemoryGlobals::GetDefaultAllocator(), nullptr)->SafeCast<Component>();

		component->entity = this;
		component->scene = m_scene;
		component->m_typeVersion = reflectType->GetVersion();

		if (m_rid)
		{
			component->m_rid = rid;
		}

		if (rid)
		{
			Resources::FromResource(rid, component, this);

			if (component->m_rid && m_scene->IsResourceSyncEnabled())
			{
				Resources::GetStorage(component->m_rid)->RegisterEvent(ResourceEventType::VersionUpdated, OnComponentResourceChange, component);
			}
		}

		component->Create();
		component->RegisterEvents();

		m_components.EmplaceBack(component);

		if (m_started)
		{
			m_scene->m_componentsToStart.Enqueue(component);
		}

		return component;
	}

	Component* Entity::GetComponent(TypeID typeId) const
	{
		for (int i = 0; i < m_components.Size(); ++i)
		{
			if (m_components[i]->GetTypeId() == typeId)
			{
				return m_components[i];
			}
		}
		return nullptr;
	}

	void Entity::RemoveComponent(Component* component)
	{
		for (int i = 0; i < m_components.Size(); ++i)
		{
			if (m_components[i] == component)
			{
				DestroyComponent(component);
				m_components.RemoveAt(i);
				break;
			}
		}
	}

	Component* Entity::FindFirstComponentOnHierarchy(TypeID typeId) const
	{
		if (Component* component = GetComponent(typeId))
		{
			return component;
		}
		for (Entity* child : m_children)
		{
			if (Component* component = child->FindFirstComponentOnHierarchy(typeId))
			{
				return component;
			}
		}
		return nullptr;
	}

	void Entity::Destroy()
	{
		if (!m_destroyRequested)
		{
			m_scene->m_queueToDestroy.Enqueue(this);
			m_destroyRequested = true;
		}

	}

	void Entity::DestroyImmediate()
	{
		DestroyInternal(true);
	}

	void Entity::NotifyEvent(const EntityEventDesc& event, bool notifyChildren)
	{
		if (event.type == EntityEventType::EntityActivated)
		{
			m_parentActive = true;

			for (Component* component : m_components)
			{
				component->RegisterEvents();
			}
		}
		else if (event.type == EntityEventType::EntityDeactivated)
		{
			m_parentActive = false;

			for (Component* component : m_components)
			{
				component->RemoveEvents();
			}
		}

		for (Component* component : m_components)
		{
			component->ProcessEvent(event);
		}

		if (notifyChildren)
		{
			for (Entity* child : m_children)
			{
				child->NotifyEvent(event, true);
			}
		}
	}

	void Entity::DestroyInternal(bool removeFromParent)
	{
		if (m_parent && removeFromParent)
		{
			for (int i = 0; i < m_parent->m_children.Size(); ++i)
			{
				if (m_parent->m_children[i] == this)
				{
					m_parent->m_children.RemoveAt(i);
					break;
				}
			}
		}

		if (!m_parent && removeFromParent)
		{
			m_scene->entities.Remove(this);
		}

		for (Entity* child : m_children)
		{
			child->DestroyInternal(false);
		}

		for (Component* component : m_components)
		{
			DestroyComponent(component);
		}

		if (m_physicsId != U64_MAX)
		{
			m_scene->physicsScene.UnregisterPhysicsEntity(this);
		}

		if (m_scene)
		{

			if (!m_name.Empty())
			{
				m_scene->entitiesByName[m_name].Remove(this);
			}

			if (m_scene->IsResourceSyncEnabled())
			{
				if (m_rid)
				{
					m_scene->entitiesByRID.Erase(m_rid);
					Resources::GetStorage(m_rid)->UnregisterEvent(ResourceEventType::Changed, OnEntityResourceChange, this);
				}
			}
		}

		DestroyAndFree(this);
	}

	void Entity::DoStart(bool executeComponentUpdates)
	{
		if (m_started)
		{
			logger.Warn("DoStart called on entity that is already started!");
			return;
		}

		m_started = true;

		if (executeComponentUpdates)
		{
			for (Component* component : m_components)
			{
				component->OnStart();
			}
		}

		if (HasFlag(EntityFlags::HasPhysics))
		{
			m_scene->physicsScene.RegisterPhysicsEntity(this);
		}
	}

	void Entity::DestroyComponent(Component* component) const
	{
		component->RemoveEvents();
		component->Destroy();

		if (m_scene->IsResourceSyncEnabled())
		{
			Resources::GetStorage(component->m_rid)->UnregisterEvent(ResourceEventType::VersionUpdated, OnComponentResourceChange, component);
		}
		DestroyAndFree(component);
	}

	void Entity::OnEntityResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		Entity* entity = static_cast<Entity*>(userData);
		//destroyed
		if (oldValue && !newValue)
		{
			entity->DestroyInternal(true);
			return;
		}

		if (newValue)
		{
			entity->SetName(newValue.GetString(EntityResource::Name));
			entity->SetActive(!newValue.GetBool(EntityResource::Deactivated));
			entity->SetLayer(static_cast<u8>(newValue.GetUInt(EntityResource::Layer)));
		}

		for (CompareSubObjectListResult res : Resources::CompareSubObjectList(oldValue, newValue, EntityResource::Children))
		{
			if (res.type == CompareSubObjectSetType::Added)
			{
				if (Entity* child = entity->GetScene()->FindEntityByRID(res.rid))
				{
					child->SetParent(entity);
				}
				else
				{
					Instantiate(entity->m_scene, entity, res.rid, true);
				}
			}

			else if (res.type == CompareSubObjectSetType::Removed)
			{
				for (Entity* child : entity->m_children)
				{
					if (child->m_rid == res.rid)
					{
						if (newValue.IsRemoveFromPrototypeSubObjectList(EntityResource::Children, res.rid))
						{
							child->DestroyInternal(true);
						}
					}
				}
			}
		}

		for (CompareSubObjectListResult res : Resources::CompareSubObjectList(oldValue, newValue, EntityResource::Components))
		{
			if (res.type == CompareSubObjectSetType::Added)
			{
				if (ResourceType* type = Resources::GetType(res.rid))
				{
					entity->AddComponent(type->GetReflectType(), res.rid);
				}
			}
			else if (res.type == CompareSubObjectSetType::Removed)
			{
				for (Component* component : entity->m_components)
				{
					if (component->m_rid == res.rid)
					{
						entity->RemoveComponent(component);
						break;
					}
				}
			}
		}
	}

	void Entity::OnComponentResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		Resources::FromResource(newValue, userData, static_cast<Component*>(userData)->entity);
	}

	AABB Entity::GetBounds()
	{
		AABB aabb = {};
		EntityEventDesc event;
		event.type = EntityEventType::CalculateEntityAABB;
		event.eventData = &aabb;
		NotifyEvent(event, true);

		return aabb;
	}

	void ResourceCast<Entity*, void>::FromResource(const ResourceObject& object, u32 index, Entity*& value, VoidPtr userData)
	{
		Entity* owner = static_cast<Entity*>(userData);
		if (Entity* entity = owner->GetScene()->FindOrCreateInstance(object.GetReference(index)))
		{
			value = entity;
		}
	}

	void Entity::RegisterType(NativeReflectType<Entity>& type)
	{
		type.Function<&Entity::GetScene>("GetScene");
		type.Function<&Entity::GetParent>("GetParent");
		type.Function<&Entity::SetParent>("SetParent", "parent");
		type.Function<&Entity::GetChildren>("GetChildren");
		type.Function<&Entity::GetChildAt>("GetChildAt", "index");
		type.Function<&Entity::GetChildrenNum>("GetChildrenNum");
		type.Function<&Entity::GetComponents>("GetComponents");
		type.Function<&Entity::GetRID>("GetRID");
		type.Function<&Entity::SetName>("SetName", "name");
		type.Function<&Entity::GetName>("GetName");
		type.Function<&Entity::GetLayer>("GetLayer");
		type.Function<&Entity::SetLayer>("SetLayer", "layer");
		type.Function<&Entity::GetLayerMask>("GetLayerMask");
		type.Function<&Entity::SetWorldTransform>("SetWorldTransform", "transform");
		type.Function<&Entity::GetWorldTransform>("GetWorldTransform");
		type.Function<&Entity::GetWorldPosition>("GetWorldPosition");
		type.Function<&Entity::GetForwardVector>("GetForwardVector");
		type.Function<&Entity::IsActive>("IsActive");
		type.Function<&Entity::SetActive>("SetActive", "active");
		type.Function<&Entity::CreateChild>("CreateChild");
		type.Function<&Entity::CreateChildFromAsset>("CreateChildFromAsset", "rid");

		constexpr Component*(Entity::*addComponentFn)(TypeID) = &Entity::AddComponent;
		type.Function<addComponentFn>("AddComponent", "typeId");

		constexpr Component*(Entity::*getComponentFn)(TypeID) const = &Entity::GetComponent;
		type.Function<getComponentFn>("GetComponent", "typeId");
		type.Function<&Entity::RemoveComponent>("RemoveComponent", "component");
		type.Function<&Entity::FindFirstComponentOnHierarchy>("FindFirstComponentOnHierarchy", "typeId");
		type.Function<&Entity::Destroy>("Destroy");
		type.Function<&Entity::DestroyImmediate>("DestroyImmediate");
		type.Function<&Entity::GetBounds>("GetAABB");
		type.Function<static_cast<Entity*(*)()>(&Entity::Instantiate)>("Instantiate");
		type.Function<static_cast<Entity*(*)(RID)>(&Entity::Instantiate)>("InstantiateFromRID", "rid");
		type.Function<static_cast<Entity*(*)(Entity*)>(&Entity::Instantiate)>("InstantiateWithParent", "parent");
		type.Function<static_cast<Entity*(*)(Entity*, RID)>(&Entity::Instantiate)>("InstantiateWithParentAndRID", "parent", "rid");
	}

	void Entity::ReflectionReload()
	{
		for (int i = 0; i < m_components.Size(); ++i)
		{
			Component* component = m_components[i];

			ReflectType* reflectType = component->GetType();
			if (component->m_typeVersion < reflectType->GetVersion())
			{
				component->RemoveEvents();

				Component* newComponent = reflectType->NewObject()->SafeCast<Component>();
				newComponent->entity = this;
				newComponent->scene = m_scene;
				newComponent->m_typeVersion = reflectType->GetVersion();
				newComponent->m_rid = component->m_rid;

				reflectType->DeepCopy(component, newComponent);

				m_components[i] = newComponent;

				DestroyAndFree(component);

				newComponent->RegisterEvents();
			}
		}

		for (Entity* child : m_children)
		{
			child->ReflectionReload();
		}
	}
}