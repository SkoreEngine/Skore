#pragma once
#include "Component.hpp"
#include "SceneCommon.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Resource/ResourceReflection.hpp"

namespace Skore
{
	class Component;
	class Scene;

	class SK_API Entity final : public Object
	{
	public:
		SK_CLASS(Entity, Object);

		~Entity() override;

		Scene*           GetScene() const;
		void             SetParent(Entity* newParent);
		Entity*          GetParent() const;

		Span<Entity*>    GetChildren() const;
		Entity*					 GetChildAt(u32 index) const;
		u32							 GetChildrenNum() const;

		Span<Component*> GetComponents() const;
		RID              GetRID() const;

		void AddFlag(EntityFlags flag);
		void RemoveFlag(EntityFlags flag);
		bool HasFlag(EntityFlags flag) const;

		void       SetName(StringView name);
		StringView GetName() const;

		u8   GetLayer() const;
		void SetLayer(u8 layer);
		u64  GetLayerMask() const;

		void        SetWorldTransform(const Mat4& transform);
		const Mat4& GetWorldTransform() const;

		SK_FINLINE Vec3 GetWorldPosition() const
		{
			return Mat4::GetTranslation(m_worldTransform);
		}

		SK_FINLINE Vec3 GetForwardVector() const
		{
			return Mat4::GetForwardVector(m_worldTransform);
		}

		bool IsActive() const;
		void SetActive(bool active);

		Entity* CreateChild();
		Entity* CreateChildFromAsset(RID rid);

		Component* AddComponent(TypeID typeId);
		Component* AddComponent(ReflectType* reflectType);
		Component* AddComponent(ReflectType* reflectType, RID rid);
		Component* GetComponent(TypeID typeId) const;
		void       RemoveComponent(Component* component);

		//Slow
		Component* FindFirstComponentOnHierarchy(TypeID typeId) const;

		void NotifyEvent(const EntityEventDesc& event, bool notifyChildren = false);

		void Destroy();
		void DestroyImmediate();

		template <typename T>
		T* GetComponent() const
		{
			return static_cast<T*>(GetComponent(TypeInfo<T>::ID()));
		}

		template <typename T>
		T* AddComponent()
		{
			return static_cast<T*>(AddComponent(TypeInfo<T>::ID()));
		}

		AABB GetBounds();

		//TODO - review instantiate
		static Entity* Instantiate();
		static Entity* Instantiate(Scene* scene);
		static Entity* Instantiate(RID rid);
		static Entity* Instantiate(Entity* parent);
		static Entity* Instantiate(Entity* parent, RID rid);

		static void RegisterType(NativeReflectType<Entity>& type);

		friend class Scene;
		friend class Component;
		friend class PhysicsScene;

	private:
		Entity();

		//bool instanceOfAsset = true means it's an instance of RID
		//bool instanceOfAsset = false means it's just a new entity, cloning the RID. (but no reference with the original)
		static Entity* Instantiate(Scene* scene, Entity* parent, RID rid, bool instanceOfAsset);
		static void    Instantiate(Entity* entity, Scene* scene, Entity* parent, RID rid, bool instanceOfAsset = true);

		String m_name = {};
		RID    m_rid = {};
		u64    m_flags = 0;
		u8     m_layer = 0;

		u64 m_physicsId = U64_MAX;
		u64 m_physicsUpdatedFrame = 0;

		bool m_active = true;
		bool m_parentActive = true;

		bool m_started = false;
		bool m_destroyRequested = false;

		Scene*         m_scene = nullptr;
		Entity*        m_parent = nullptr;
		Array<Entity*> m_children;

		Array<Component*> m_components;
		Mat4 m_worldTransform = Mat4(1.0);

		void DestroyInternal(bool removeFromParent = true);

		void DoStart(bool executeComponentUpdates);
		void DestroyComponent(Component* component) const;
		void ReflectionReload();

		static void OnEntityResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
		static void OnComponentResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
	};


	template <>
	struct SK_API ResourceCast<Entity*>
	{
		constexpr static bool hasSpecialization = true;

		static void ToResource(ResourceObject& object, u32 index, UndoRedoScope* scope, const Entity* value, VoidPtr userData)
		{
			if (value && value->GetRID())
			{
				object.SetReference(index, value->GetRID());
			}
		}

		static void FromResource(const ResourceObject& object, u32 index, Entity*& value, VoidPtr userData);

		static ResourceFieldInfo GetResourceFieldInfo()
		{
			return ResourceFieldInfo{
				.type = ResourceFieldType::Reference,
				.subType = TypeInfo<Entity>::ID()
			};
		}
	};
}
